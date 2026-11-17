// RTS demo 的权威服务端：1 个真人 + 3 个 AI 的 4 人混战。
//
// 结构就是「一个 reactor + 一个 20Hz 定时器」：
//   · poller 等 IO 事件（最长等到下一个 tick）
//   · 有输入就解码、交给 Sim 校验
//   · 到点就让 3 个 AI 决策 + sim.advance()，然后把快照推给客户端
//
// 权威性：客户端只能发「我要出第几张牌、放在哪」，合法性、伤害、胜负全在这里算。
//
//   kingdom_fight_server [--port 9700] [--timeout 秒] [--no-ai]
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/channel.h"
#include "net/poller.h"
#include "net/socket.h"
#include "rts/ai.h"
#include "rts/sim.h"
#include "rts/types.h"
#include "rts/wire.h"

namespace {

using namespace kingdom_fight;
using namespace kingdom_fight::rts;

using Clock = std::chrono::steady_clock;

constexpr auto kTickInterval = std::chrono::milliseconds(1000 / kTickHz);
// 编译时间戳：客户端会显示，用来确认"跑的是不是最新构建"
constexpr const char* kBuildStamp = __DATE__ " " __TIME__;

[[nodiscard]] Cmd to_cmd(wire::Msg msg) { return static_cast<Cmd>(msg); }

struct Client {
    explicit Client(Socket socket) : channel(std::move(socket)) {}

    Channel channel;
    std::uint32_t next_seq = 1;
    bool deck_submitted = false;
    bool in_match = false;
    int territory_version = -1;   // 上次发给这个客户端的领地掩码版本
};

struct Session {
    Sim sim;
    AiLevel ai_level = AiLevel::Normal;
    std::array<AiBrain, kPlayers> brains{};
    std::vector<int> player_deck;
    Alliance player_alliance = Alliance::Vanguard;
    int player_unit = -1;  // 局外 DIY：兵营默认兵种
    bool in_match = false;
    bool over_sent = false;
};

// AI 的卡组从它自己的阵营池里取：前 8 张（池子是 10 张，留 2 张不用）。
// 以前是手写死的 id 列表 —— 现在每个阵营一个池子，写死就会串池。
std::vector<int> ai_deck_for(Alliance alliance) {
    const auto& pool = card_pool(alliance);
    return std::vector<int>(pool.begin(), pool.begin() + kDeckSize);
}

// 快速模式：把 30 分钟 / 25 分钟的节奏压成 3 分钟 / 60 秒，方便试玩和验收
bool fast_mode = false;

void print_usage() {
    std::printf(
        "用法：kingdom_fight_server [--port N] [--timeout 秒] [--no-ai] [--fast]\n"
        "                            [--ai-level easy|normal|hard]\n");
}

void send(wire::Msg msg, const std::string& payload, Client& client) {
    client.channel.queue_frame(to_cmd(msg), client.next_seq++, payload);
}

void start_match(Session& session, Client& client, bool use_ai, Clock::time_point& next_tick) {
    // 每局换一张图 + 一组随机降落点（不再有对称阵型，种子决定一切）
    static std::uint32_t match_counter = 0x1234567u;
    match_counter = match_counter * 1664525u + 1013904223u;
    MatchConfig config;
    config.terrain_variant = static_cast<int>(match_counter % kTerrainVariants);
    config.spawn_seed = match_counter * 2654435761u + 0x9E3779B9u;
    if (fast_mode) {
        config.match_seconds = 180;
        config.hq_unlock_seconds = 60;
    }

    // 玩家选一家联盟，三个 AI 分掉剩下的三家
    std::array<Alliance, kPlayers> factions{};
    factions[0] = session.player_alliance;
    int next = 0;
    for (int p = 1; p < kPlayers; ++p) {
        while (next == static_cast<int>(session.player_alliance)) ++next;
        factions[static_cast<std::size_t>(p)] = static_cast<Alliance>(next++);
    }

    std::array<std::vector<int>, kPlayers> decks;
    decks[0] = session.player_deck;
    for (int p = 1; p < kPlayers; ++p) {
        decks[static_cast<std::size_t>(p)] = ai_deck_for(factions[static_cast<std::size_t>(p)]);
    }

    if (!session.sim.start(decks, config, factions)) {
        send(wire::Msg::S2C_Error, wire::encode_error("start failed"), client);
        return;
    }
    for (int p = 1; p < kPlayers; ++p) {
        session.brains[static_cast<std::size_t>(p)].reset(p, 0x5EEDu + static_cast<std::uint32_t>(p) * 977u);
        session.brains[static_cast<std::size_t>(p)].set_level(session.ai_level);
    }
    session.in_match = true;
    session.over_sent = false;
    next_tick = Clock::now() + kTickInterval;

    if (session.player_unit >= 0) {
        session.sim.set_preferred_unit(0, session.player_unit);
    }
    send(wire::Msg::S2C_Map, wire::encode_map(session.sim), client);
    send(wire::Msg::S2C_MatchStart, wire::encode_match_start(session.sim, 0), client);
    send(wire::Msg::S2C_Snapshot, wire::encode_snapshot(session.sim, 0), client);
    std::printf("[rts] 地图 variant=%d seed=%u 资源点=%d\n", config.terrain_variant,
                config.spawn_seed, static_cast<int>(session.sim.nodes().size()));
    std::fflush(stdout);
    (void)use_ai;
}

}  // namespace

int main(int argc, char** argv) {
    int port = 9700;
    int timeout_seconds = 0;
    bool use_ai = true;
    AiLevel ai_level = AiLevel::Normal;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout_seconds = std::atoi(argv[++i]);
        } else if (arg == "--no-ai") {
            use_ai = false;
        } else if (arg == "--fast") {
            fast_mode = true;
        } else if (arg == "--ai-level" && i + 1 < argc) {
            // 以前这个参数没实现，未知参数又被静默忽略 —— demo.ps1 -AiLevel hard 其实是空转，
            // 一直在跑 Normal。现在真接上，并且写错值直接报错。
            const std::string level = argv[++i];
            if (level == "easy") {
                ai_level = AiLevel::Easy;
            } else if (level == "normal") {
                ai_level = AiLevel::Normal;
            } else if (level == "hard") {
                ai_level = AiLevel::Hard;
            } else {
                std::printf("[rts] 未知 AI 难度：%s（easy / normal / hard）\n", level.c_str());
                return 2;
            }
        } else if (arg == "--help") {
            print_usage();
            return 0;
        } else {
            // 别静默吞掉拼错的参数：宁可起不来，也别让人以为参数生效了
            std::printf("[rts] 未知参数：%s\n", arg.c_str());
            print_usage();
            return 2;
        }
    }

    if (!net_init()) {
        std::printf("[rts] net_init 失败：%s\n", net_last_error().c_str());
        return 1;
    }

    Socket listener = Socket::listen_tcp("0.0.0.0", static_cast<std::uint16_t>(port), 32);
    if (!listener.valid()) {
        std::printf("[rts] 监听失败：%s\n", net_last_error().c_str());
        return 1;
    }

    auto poller = Poller::create();
    if (!poller->add(listener.handle(), kPollRead)) {
        std::printf("[rts] 注册监听 socket 失败\n");
        return 1;
    }

    std::printf("[rts] 监听 0.0.0.0:%d  poller=%s  players=%d  ai=%s\n", port, poller->name(),
                kPlayers, use_ai ? "on" : "off");
    std::fflush(stdout);

    std::unordered_map<SocketHandle, Client> clients;
    Session session;
    // 谁占了这张桌子。只有"提交卡组"的连接才会占位；端口探活/多开客户端不会。
    SocketHandle owner_handle = kInvalidSocket;

    const auto started_at = Clock::now();
    auto next_tick = Clock::now();
    session.ai_level = ai_level;
    bool running = true;

    while (running) {
        int wait_ms = 100;
        if (session.in_match) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       next_tick - Clock::now())
                                       .count();
            wait_ms = static_cast<int>(std::clamp<long long>(remaining, 0, 50));
        }

        std::vector<PollEvent> ready;
        const int ready_count = poller->wait(ready, wait_ms);
        if (ready_count < 0) {
            std::printf("[rts] poller 出错，退出\n");
            break;
        }

        for (const PollEvent& event : ready) {
            if (event.handle == listener.handle()) {
                while (true) {
                    Socket connection = listener.accept_connection();
                    if (!connection.valid()) break;

                    if (!connection.set_no_delay(true)) {
                        std::printf("[rts] 设置 TCP_NODELAY 失败（不影响功能）\n");
                    }
                    const SocketHandle handle = connection.handle();
                    poller->add(handle, kPollRead);
                    auto inserted = clients.emplace(handle, Client(std::move(connection)));
                    Client& client = inserted.first->second;

                    send(wire::Msg::S2C_Welcome, wire::encode_welcome(0, kPlayers - 1, 1, kBuildStamp), client);
    std::printf("[rts] build=%s\n", kBuildStamp);
                    send(wire::Msg::S2C_CardPool, wire::encode_card_pool(), client);
                        send(wire::Msg::S2C_UnitTable, wire::encode_unit_table(), client);
                    std::printf("[rts] 客户端接入 (fd=%lld)\n", static_cast<long long>(handle));
                    std::fflush(stdout);
                }
                // 新连接刚排进去的 Welcome / CardPool 必须立刻刷出去，
                // 否则客户端在等服务端、服务端在等客户端。
                for (auto& entry : clients) entry.second.channel.flush();
                continue;
            }

            const auto it = clients.find(event.handle);
            if (it == clients.end()) continue;
            Client& client = it->second;

            if ((event.flags & kPollRead) != 0) {
                std::vector<Frame> frames;
                if (!client.channel.read_frames(frames)) {
                    std::printf("[rts] 连接断开 (fd=%lld)\n",
                                static_cast<long long>(event.handle));
                    std::fflush(stdout);
                    if (client.in_match) session.in_match = false;
                    if (owner_handle == event.handle) owner_handle = kInvalidSocket;
                    poller->remove(event.handle);
                    clients.erase(it);
                    continue;
                }

                for (const Frame& frame : frames) {
                    const auto msg = static_cast<wire::Msg>(frame.cmd);
                    switch (msg) {
                        case wire::Msg::C2S_Deck: {
                            std::vector<int> deck;
                            Alliance alliance = Alliance::Vanguard;
                            int preferred_unit = -1;
                        if (!wire::decode_deck(frame.payload, deck, alliance, preferred_unit)) {
                                send(wire::Msg::S2C_Error, wire::encode_error("bad deck"), client);
                                break;
                            }
                            if (owner_handle != kInvalidSocket && owner_handle != event.handle) {
                                send(wire::Msg::S2C_Error,
                                     wire::encode_error("table busy, try later"), client);
                                break;
                            }
                            session.player_deck = deck;
                            session.player_alliance = alliance;
                            client.deck_submitted = true;
                            client.in_match = true;
                            owner_handle = event.handle;
                            start_match(session, client, use_ai, next_tick);
                            std::printf("[rts] 开局：%d 人混战（玩家 %d 张卡 + 3 个 AI）\n",
                                        kPlayers, static_cast<int>(deck.size()));
                            std::fflush(stdout);
                            break;
                        }
                        case wire::Msg::C2S_Play: {
                            int slot = 0;
                            int x = 0;
                            int y = 0;
                            if (!wire::decode_play(frame.payload, slot, x, y)) {
                                send(wire::Msg::S2C_Error, wire::encode_error("bad play"), client);
                                break;
                            }
                            const PlayResult result = session.sim.play_card(0, slot, x, y);
                            send(wire::Msg::S2C_PlayAck, wire::encode_play_ack(slot, result),
                                 client);
                            break;
                        }
                        case wire::Msg::C2S_Rematch: {
                            if (session.player_deck.empty() || !client.in_match) break;
                            start_match(session, client, use_ai, next_tick);
                            std::printf("[rts] 再来一局\n");
                            std::fflush(stdout);
                            break;
                        }
                        case wire::Msg::C2S_SetUnit: {
                        if (frame.payload.size() < 2) {
                            send(wire::Msg::S2C_Error, wire::encode_error("bad set unit"), client);
                            break;
                        }
                        const int index = static_cast<std::uint8_t>(frame.payload[0]);
                        const int unit_type = static_cast<std::uint8_t>(frame.payload[1]);
                        const PlayResult result = session.sim.set_building_unit(0, index, unit_type);
                        send(wire::Msg::S2C_SetUnitAck, wire::encode_hq_ack(result), client);
                        break;
                    }
                    case wire::Msg::C2S_SetRally: {
                        if (frame.payload.size() < 5) {
                            send(wire::Msg::S2C_Error, wire::encode_error("bad set rally"), client);
                            break;
                        }
                        const int index = static_cast<std::uint8_t>(frame.payload[0]);
                        const int rx = (static_cast<std::uint8_t>(frame.payload[1]) << 8) |
                                       static_cast<std::uint8_t>(frame.payload[2]);
                        const int ry = (static_cast<std::uint8_t>(frame.payload[3]) << 8) |
                                       static_cast<std::uint8_t>(frame.payload[4]);
                        const PlayResult result = session.sim.set_building_rally(0, index, rx, ry);
                        send(wire::Msg::S2C_SetRallyAck, wire::encode_hq_ack(result), client);
                        break;
                    }
                    case wire::Msg::C2S_BuildTower: {
                        if (frame.payload.size() < 4) {
                            send(wire::Msg::S2C_Error, wire::encode_error("bad build tower"), client);
                            break;
                        }
                        const int tx = (static_cast<std::uint8_t>(frame.payload[0]) << 8) |
                                       static_cast<std::uint8_t>(frame.payload[1]);
                        const int ty = (static_cast<std::uint8_t>(frame.payload[2]) << 8) |
                                       static_cast<std::uint8_t>(frame.payload[3]);
                        const PlayResult result = session.sim.build_tower(0, tx, ty);
                        send(wire::Msg::S2C_BuildTowerAck, wire::encode_hq_ack(result), client);
                        break;
                    }
                    case wire::Msg::C2S_Discard: {
                        if (frame.payload.size() < 1) {
                            send(wire::Msg::S2C_Error, wire::encode_error("bad discard"), client);
                            break;
                        }
                        const int slot = static_cast<std::uint8_t>(frame.payload[0]);
                        const PlayResult result = session.sim.discard(0, slot);
                        send(wire::Msg::S2C_DiscardAck,
                             wire::encode_play_ack(slot, result), client);
                        break;
                    }
                    case wire::Msg::C2S_Repair: {
                        int kind = 0;
                        int index = 0;
                        if (!wire::decode_demolish(frame.payload, kind, index)) {
                            send(wire::Msg::S2C_Error, wire::encode_error("bad repair"), client);
                            break;
                        }
                        const PlayResult result = session.sim.repair(0, kind, index);
                        send(wire::Msg::S2C_RepairAck,
                             wire::encode_demolish_ack(kind, index, result), client);
                        break;
                    }
                    case wire::Msg::C2S_Upgrade: {
                        if (frame.payload.size() < 1) {
                            send(wire::Msg::S2C_Error, wire::encode_error("bad upgrade"), client);
                            break;
                        }
                        const int upgrade_id = static_cast<std::uint8_t>(frame.payload[0]);
                        const PlayResult result = session.sim.upgrade(0, upgrade_id);
                        send(wire::Msg::S2C_UpgradeAck,
                             wire::encode_upgrade_ack(
                                 upgrade_id, session.sim.tree_taken(0, upgrade_id) ? 1 : 0, result),
                             client);
                        if (result == PlayResult::Ok) {
                            std::printf("[rts] 点亮成长树节点 %d\n", upgrade_id);
                            std::fflush(stdout);
                        }
                        break;
                    }
                    case wire::Msg::C2S_Demolish: {
                        int kind = 0;
                        int index = 0;
                        if (!wire::decode_demolish(frame.payload, kind, index)) {
                            send(wire::Msg::S2C_Error, wire::encode_error("bad demolish"), client);
                            break;
                        }
                        const PlayResult result = session.sim.demolish(0, kind, index);
                        send(wire::Msg::S2C_DemolishAck,
                             wire::encode_demolish_ack(kind, index, result), client);
                        break;
                    }
                    case wire::Msg::C2S_Hq: {
                            int x = 0;
                            int y = 0;
                            if (!wire::decode_hq(frame.payload, x, y)) {
                                send(wire::Msg::S2C_Error, wire::encode_error("bad hq"), client);
                                break;
                            }
                            const PlayResult result = session.sim.call_hq(0, x, y);
                            send(wire::Msg::S2C_HqAck, wire::encode_hq_ack(result), client);
                            if (result == PlayResult::Ok) {
                                std::printf("[rts] 总部支援 (%d,%d)\n", x, y);
                                std::fflush(stdout);
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            if (!client.channel.flush()) {
                if (client.in_match) session.in_match = false;
                if (owner_handle == event.handle) owner_handle = kInvalidSocket;
                poller->remove(event.handle);
                clients.erase(it);
            }
        }

        // ---- 定时推进模拟 ---------------------------------------------------
        if (session.in_match) {
            const auto now = Clock::now();
            int steps = 0;
            while (now >= next_tick && steps < 3) {
                if (use_ai) {
                    for (int p = 1; p < kPlayers; ++p) {
                        session.brains[static_cast<std::size_t>(p)].update(session.sim);
                    }
                }
                session.sim.advance();
                next_tick += kTickInterval;
                ++steps;
            }
            if (now > next_tick + std::chrono::milliseconds(250)) {
                next_tick = now + kTickInterval;
            }

            if (steps > 0) {
                for (auto& entry : clients) {
                    if (!entry.second.in_match) continue;
                    send(wire::Msg::S2C_Snapshot, wire::encode_snapshot(session.sim, 0),
                         entry.second);
                    // 领地掩码：版本变了才发（开局 / 每次抢到片区都会变）
                    const int mask_version = session.sim.territory_version(0);
                    if (entry.second.territory_version != mask_version) {
                        entry.second.territory_version = mask_version;
                        send(wire::Msg::S2C_Territory, wire::encode_territory(session.sim, 0),
                             entry.second);
                    }
                    entry.second.channel.flush();
                }
            }

            if (session.sim.phase() == MatchPhase::Finished && !session.over_sent) {
                session.over_sent = true;
                session.in_match = false;
                for (auto& entry : clients) {
                    if (!entry.second.in_match) continue;
                    send(wire::Msg::S2C_MatchOver, wire::encode_match_over(session.sim),
                         entry.second);
                    entry.second.channel.flush();
                }
                std::printf("[rts] 对局结束：胜者 %d（%s），用了 %.1f 秒\n", session.sim.winner(),
                            to_string(session.sim.reason()), session.sim.tick() / 20.0);
                std::fflush(stdout);
            }
        }

        for (auto& entry : clients) {
            const std::uint32_t interests =
                entry.second.channel.has_pending_write() ? (kPollRead | kPollWrite) : kPollRead;
            poller->modify(entry.first, interests);
        }

        if (timeout_seconds > 0) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started_at);
            if (elapsed.count() >= timeout_seconds) running = false;
        }
    }

    std::printf("[rts] 退出\n");
    return 0;
}
