// 事件 = 服务端判定后产生的「事实」。
//
// 两条铁律：
//   1. 事件流是唯一的事实来源。客户端只回放事件，不推断状态。
//   2. serialize() 必须稳定且无歧义 —— 确定性比对（S7）、回放、断线重连增量（S18）
//      全都依赖它。字段的语义随 type 变化，必须在 docs/02-game-design.md 里写清楚。
#pragma once

#include <string>

#include "logic/types.h"

namespace landgrab {

enum class EventType : int {
    GameBegin = 0,      // a = 玩家数
    TurnBegin,          // a = 回合号
    OrdersAccepted,     // actor = 提交者, a = 指令条数
    OrderRejected,      // actor = 提交者, a = RejectReason, b = OrderType
    ResourceProduced,   // actor = 收益者, a = 金币, b = 兵源
    BuildingCompleted,  // actor = 归属者, a = 区域, b = 等级
    UnitsRecruited,     // actor = 招募者, a = 区域, b = 长枪, c = 盾卫, d = 骑手
    ArmyMoved,          // actor = 移动者, a = 起点, b = 终点, c = 长枪, d = 盾卫+骑手? 见实现
    BattleResolved,     // a = 区域, b = 攻方, c = 守方, d = 攻方剩余战力
    RegionCaptured,     // actor = 新归属者, a = 区域, b = 原归属者
    TacticRevealed,     // actor = 出牌者, a = 卡 id, b = 区域
    PlayerEliminated,   // actor = 被淘汰者
    TurnEnd,            // a = 回合号
    GameOver,           // actor = 胜者, a = 胜利原因, b = 回合号
};

enum class RejectReason : int {
    Timeout = 0,       // 未在时限内提交，按托管处理
    NotImplemented,    // 该指令类型尚未实现（S3~S5 的预留缝隙）
    IllegalTarget,     // 目标不合法
    IllegalSource,     // 来源不合法
    NotAdjacent,       // 两点不相邻
    NotEnoughAp,       // 行动点不足
    NotEnoughGold,     // 金币不足
    NotEnoughTroops,   // 兵力不足
};

enum class WinReason : int {
    Conquer = 0,   // 控制 kWinRegions 块领地
    LastStanding,  // 对手全部淘汰
    HighestScore,  // 回合用尽，分数最高
};

[[nodiscard]] const char* to_string(EventType type);
[[nodiscard]] const char* to_string(RejectReason reason);
[[nodiscard]] const char* to_string(WinReason reason);

struct Event {
    EventType type{};
    PlayerId actor = kNeutralOwner;
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;

    // 规范化文本：同一条事件必须每次产出完全相同的字符串。
    [[nodiscard]] std::string serialize() const;

    // 人类可读战报（不参与确定性比对）。
    [[nodiscard]] std::string describe() const;
};

}  // namespace landgrab
