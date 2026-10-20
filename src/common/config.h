// 全局可调参数。所有"魔法数字"集中在这里，方便 S5 之后调平衡。
#pragma once

namespace kingdom_fight {

// 一局多少人。设计稿：3-4 人混战；2 人局保留以便测试与匹配降级。
inline constexpr int kMaxPlayers = 4;
inline constexpr int kMinPlayers = 2;

// 一局最多多少回合（设计稿硬约束：8~12 回合，20 分钟内）
inline constexpr int kMaxTurns = 12;

// 起始金币、每回合行动点
inline constexpr int kStartingGold = 5;
inline constexpr int kApPerTurn = 2;

// 起手牌数、牌库总数
inline constexpr int kOpeningHand = 3;
inline constexpr int kDeckSize = 10;

// 开局每块领地驻扎的兵力（枪/盾/骑）
inline constexpr int kStartSpear = 4;
inline constexpr int kStartShield = 3;
inline constexpr int kStartRider = 3;

// 胜利条件：控制多少块领地立即获胜
inline constexpr int kWinRegions = 3;

// ---- 经济（S3）----
inline constexpr int kBuildCost = 4;           // 建造一次的花费
inline constexpr int kBuildMaxLevel = 2;       // 每块领地建筑等级上限，每级 +1 金币产出
inline constexpr int kRecruitCostPerUnit = 1;  // 每招募 1 兵的花费
inline constexpr int kNeutralGarrison = 8;     // 中立领地（中枢）的民兵总量
inline constexpr int kDrawPerTurn = 1;         // 每回合抽牌数
inline constexpr int kHandLimit = 5;           // 手牌上限

// ---- 战斗（S5）----
// 胜方伤亡百分比 = 败方战力 / 胜方战力 × 本系数，再夹到 [kMin, kMax] 之间
inline constexpr int kCasualtyFactor = 50;
inline constexpr int kMinCasualtyPercent = 5;
inline constexpr int kMaxCasualtyPercent = 95;

struct GameConfig {
    int max_turns = kMaxTurns;
    int starting_gold = kStartingGold;
    int ap_per_turn = kApPerTurn;
};

}  // namespace kingdom_fight
