// 对局引擎：一次「同时决策」的回合循环。
//
//   submit(玩家, 指令)  →  所有人交齐（或超时托管）
//   resolve_turn()      →  按固定顺序结算，返回本回合产生的事件序列
//
// 设计约束（都是为后面的网络层铺路）：
//   · 纯逻辑，零网络、零数据库、零线程（logic/ 目录不许 include 任何 IO 头）
//   · 随机数只来自引擎内的 Rng，种子由外部传入 → 决定论
//   · 结算顺序完全固定 → 可复现
//
// 关于「可替换的 RuleSet 接口」：现在只有一套规则，抽象成虚接口是预先设计。
// 等 S11 网络层真的需要换规则时再抽（见 ADR 0001 的原则：不为不存在的变化付钱）。
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

    // 本回合每个区域已招募的兵数（每回合开始清零），用于执行 troop_yield 上限
    std::array<int, kRegionCount> recruited_this_turn{};
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
    // 未提交的玩家自动按超时处理 —— 这是服务端心跳超时语义的落点。
    std::vector<Event> resolve_turn();

    // 便捷查询（供报告 / AI / 后续网络层用）
    [[nodiscard]] int count_owned_regions(PlayerId player) const;
    [[nodiscard]] int count_total_troops(PlayerId player) const;
    [[nodiscard]] int score(PlayerId player) const;
    [[nodiscard]] bool owns(PlayerId player, RegionId region) const;

private:
    void emit(Event event);
    void emit_casualties(PlayerId who, RegionId region, const Army& losses);
    void reject(PlayerId player, RejectReason reason, OrderType type);
    void shuffle_deck(std::vector<int>& cards);
    void draw_card(Player& player);
    void discard_card(Player& player, int card_id);

    void begin_turn();
    void phase_produce_resources();
    void phase_execute_orders();
    void phase_check_victory();
    void end_turn();

    void execute_build(Player& player, const Command& cmd);
    void execute_recruit(Player& player, const Command& cmd);
    void execute_move(PlayerId player, RegionId from, RegionId to, int count);
    void execute_attack(PlayerId player, RegionId from, RegionId to, int count);
    void commit_tactic(Player& player, const Command& cmd);

    // 若该玩家在 battle_region 暗置了战术卡则揭晓并返回加成百分比，否则返回 0
    int consume_tactic(PlayerId player, RegionId battle_region);

    GameState state_;
    Rng rng_;
};

}  // namespace kingdom_fight
