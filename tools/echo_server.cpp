// S12 验收用的 echo 服务端。
//
// 它不玩牌，只做三件事：接受连接、按帧收发、正确关闭。
// 目的是验证 net 层（poller + channel + 编解码）真的能扛真实 TCP，
// 后面的房间/会话都建在它上面。
//
//   kingdom_fight_echo --port 9527 [--max-connections N] [--timeout 秒]
//
// --max-connections / --timeout 是为了让自动化测试能自己退出，不用 kill。
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/channel.h"
#include "net/poller.h"
#include "net/socket.h"

namespace {

using namespace kingdom_fight;

struct Options {
    int port = 9527;
    int max_connections = 0;  // 0 = 不限
    int timeout_seconds = 0;  // 0 = 不限
};

void print_usage() {
    std::printf("用法：kingdom_fight_echo [--port N] [--max-connections N] [--timeout 秒]\n");
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            options.port = std::atoi(argv[++i]);
        } else if (arg == "--max-connections" && i + 1 < argc) {
            options.max_connections = std::atoi(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            options.timeout_seconds = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            print_usage();
            return 0;
        }
    }

    if (!net_init()) {
        std::printf("net_init 失败：%s\n", net_last_error().c_str());
        return 1;
    }

    Socket listener =
        Socket::listen_tcp("127.0.0.1", static_cast<std::uint16_t>(options.port), 128);
    if (!listener.valid()) {
        std::printf("监听失败：%s\n", net_last_error().c_str());
        return 1;
    }

    auto poller = Poller::create();
    if (!poller->add(listener.handle(), kPollRead)) {
        std::printf("注册监听 socket 失败\n");
        return 1;
    }
    std::printf("[echo] 监听 127.0.0.1:%d  poller=%s\n", options.port, poller->name());
    std::fflush(stdout);

    std::unordered_map<SocketHandle, Channel> channels;
    const auto started_at = std::chrono::steady_clock::now();
    int accepted_total = 0;
    int echoed_total = 0;
    bool running = true;

    while (running) {
        std::vector<PollEvent> ready;
        const int ready_count = poller->wait(ready, 200);
        if (ready_count < 0) {
            std::printf("[echo] poller 出错，退出\n");
            break;
        }

        for (const PollEvent& event : ready) {
            if (event.handle == listener.handle()) {
                // 一次事件里可能有多个连接排队，全部接掉
                while (true) {
                    Socket connection = listener.accept_connection();
                    if (!connection.valid()) {
                        break;
                    }
                    const SocketHandle handle = connection.handle();
                    poller->add(handle, kPollRead);
                    channels.emplace(handle, Channel(std::move(connection)));
                    ++accepted_total;
                    std::printf("[echo] 接受连接 #%d (fd=%lld)\n", accepted_total,
                                static_cast<long long>(handle));
                    std::fflush(stdout);
                }
                continue;
            }

            const auto it = channels.find(event.handle);
            if (it == channels.end()) {
                continue;
            }
            Channel& channel = it->second;

            if ((event.flags & kPollRead) != 0) {
                std::vector<Frame> frames;
                if (!channel.read_frames(frames)) {
                    std::printf("[echo] 关闭连接 (fd=%lld)\n",
                                static_cast<long long>(event.handle));
                    std::fflush(stdout);
                    poller->remove(event.handle);
                    channels.erase(it);
                    continue;
                }
                for (const Frame& frame : frames) {
                    // Ping → Pong，其余（Echo）→ EchoAck，payload 原样带回
                    const Cmd reply_cmd = (frame.cmd == Cmd::Ping) ? Cmd::Pong : Cmd::EchoAck;
                    channel.queue_frame(reply_cmd, frame.seq, frame.payload);
                    ++echoed_total;
                }
            }

            if (!channel.flush()) {
                std::printf("[echo] 写失败，关闭连接 (fd=%lld)\n",
                            static_cast<long long>(event.handle));
                std::fflush(stdout);
                poller->remove(event.handle);
                channels.erase(it);
            }
        }

        // 关注事件随写队列变化：有积压就同时等可写
        for (auto& entry : channels) {
            const std::uint32_t interests =
                entry.second.has_pending_write() ? (kPollRead | kPollWrite) : kPollRead;
            poller->modify(entry.first, interests);
        }

        if (options.max_connections > 0 && accepted_total >= options.max_connections &&
            channels.empty()) {
            running = false;
        }
        if (options.timeout_seconds > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - started_at);
            if (elapsed.count() >= options.timeout_seconds) {
                running = false;
            }
        }
    }

    std::printf("[echo] 结束：接受 %d 个连接，回显 %d 帧\n", accepted_total, echoed_total);
    return 0;
}
