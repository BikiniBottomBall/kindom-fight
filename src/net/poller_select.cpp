// select 实现：Windows 上的开发用实现，也是 POSIX 的兜底。
//
// 局限（面试时正好可以讲）：
//   · 每次 wait 都要把整个 fd 集合从用户态拷进内核，O(n)
//   · 内核要遍历全部 fd，O(n)
//   · fd 数量受 FD_SETSIZE 限制（这里设成 1024）
// 所以它能支撑几十上百连接，撑不到 C10K —— 这正是 epoll 存在的理由。
#include "net/poller_impl.h"

#include <cstddef>
#include <unordered_map>

namespace kingdom_fight {
namespace {

#ifdef _WIN32
using NativeSocket = ::SOCKET;
NativeSocket native(SocketHandle handle) { return static_cast<NativeSocket>(handle); }
#else
using NativeSocket = int;
NativeSocket native(SocketHandle handle) { return static_cast<int>(handle); }
#endif

class SelectPoller final : public Poller {
public:
    [[nodiscard]] const char* name() const override { return "select"; }

    bool add(SocketHandle handle, std::uint32_t interests) override {
        const auto inserted = interests_.emplace(handle, interests);
        return inserted.second;
    }

    bool modify(SocketHandle handle, std::uint32_t interests) override {
        const auto it = interests_.find(handle);
        if (it == interests_.end()) {
            return false;
        }
        it->second = interests;
        return true;
    }

    bool remove(SocketHandle handle) override { return interests_.erase(handle) > 0; }

    int wait(std::vector<PollEvent>& ready, int timeout_ms) override {
        fd_set read_set;
        fd_set write_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);

        SocketHandle max_handle = -1;
        for (const auto& entry : interests_) {
            const SocketHandle handle = entry.first;
            if ((entry.second & kPollRead) != 0) {
                FD_SET(native(handle), &read_set);
            }
            if ((entry.second & kPollWrite) != 0) {
                FD_SET(native(handle), &write_set);
            }
            if (handle > max_handle) {
                max_handle = handle;
            }
        }

        timeval timeout{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
        // POSIX 的 select 签名是非 const 的 timeval*（Windows 也一样），这里不能用 const
        timeval* timeout_ptr = (timeout_ms < 0) ? nullptr : &timeout;

        const int count = ::select(static_cast<int>(max_handle) + 1, &read_set, &write_set, nullptr,
                                   timeout_ptr);
        if (count < 0) {
            ready.clear();
            return 0;  // 被信号打断等：当作"这一轮没有事件"
        }
        ready.clear();
        if (count == 0) {
            return 0;
        }

        for (const auto& entry : interests_) {
            const SocketHandle handle = entry.first;
            PollEvent event;
            event.handle = handle;
            if ((entry.second & kPollRead) != 0 && FD_ISSET(native(handle), &read_set) != 0) {
                event.flags |= kPollRead;
            }
            if ((entry.second & kPollWrite) != 0 && FD_ISSET(native(handle), &write_set) != 0) {
                event.flags |= kPollWrite;
            }
            if (event.flags != 0) {
                ready.push_back(event);
            }
        }
        return static_cast<int>(ready.size());
    }

private:
    std::unordered_map<SocketHandle, std::uint32_t> interests_;
};

}  // namespace

std::unique_ptr<Poller> make_select_poller() { return std::make_unique<SelectPoller>(); }

}  // namespace kingdom_fight
