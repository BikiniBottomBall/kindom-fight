// 对局引擎：一次「同时决策」的回合循环。
//
//   submit(玩家, 指令)  →  所有人交齐（或超时托管）
//   resolve_turn()      →  按固定顺序结算，返回本回合产生的事件序列
//
// 设计约束（都是为后面的网络层铺路）：
//   · 纯逻辑，零网络、零数据库、零线程（logic/ 目录不许 include 任何 IO 头）
//   · 随机数只来自引擎内的 Rng，种子由外部传入 → 决定论
//   · 结算顺序完全固定（区域 id 升序 → 玩家 id 升序）→ 可复现
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "common/config.h"
#include "common/rng.h"
#include "logic/card_def.h"
#include "logic/command.h"
#include "logic/event.h"
#include "logic/player.h"
#include "logic/region.h"

namespace kingdom_fight {

enum class Phase : int {
    WaitingForOrders = 0,  // 收指令中
    Resolving,             // 结算中（不会在回合之间停留，事件里能看到）
    Finished,              // 已分胜负
};

[[nodiscard]] const char* to_string(Phase phase);

struct GameState {
    GameConfig cfg{};
    std::array<Region, kRegionCount> regions{};
    std::array<Player, kMaxPlayers> players{};
    int player_count = 0;
    int turn = 0;
    Phase phase = Phase::WaitingForOrders;
    std::optional<PlayerId> winner;
    std::vector<Event> log;  // 本局全部事件（回放 / 确定性比对用）
};

class GameEngine {
public:
    GameEngine(GameConfig cfg, int player_count, std::uint64_t seed);

    [[nodiscard]] const GameState& state() const { return state_; }

    // 提交指令。可以多次提交（追加），也可以提交空列表表示「本回合略过」。
    void submit(PlayerId player, std::vector<Command> orders);

    // 超时/托管：按空指令处理，事件里会留下 OrderRejected(Timeout) 痕迹。
    void submit_timeout(PlayerId player);

    [[nodiscard]] bool all_submitted() const;

    // 结算一个回合，返回本回合产生的事件。已结束则返回空。
    // 前置条件不成立时（未交齐）自动把未提交者按超时处理 —— 这是服务端的心跳超时语义。
    std::vector<Event> resolve_turn();

    // 便捷查询（供报告/AI/后续网络层用）
    [[nodiscard]] int count_owned_regions(PlayerId player) const;
    [[nodiscard]] int count_total_troops(PlayerId player) const;
    [[nodiscard]] int score(PlayerId player) const;

private:
    void emit(Event event);
    void shuffle_deck(std::vector<int>& cards);

    void begin_turn();
    void phase_produce_resources();
    void phase_complete_buildings();
    void phase_execute_orders();
    void phase_resolve_battles();
    void phase_check_victory();
    void end_turn();

    GameState state_;
    Rng rng_;
};

}  // namespace kingdom_fight
