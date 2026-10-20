// 战斗结算：确定性公式，没有骰子。
//
// 设计决策（docs/02-game-design.md §9）：方差是平衡的敌人。
// 有随机数时，你分不清"这局输了"是数值问题还是运气问题；确定性公式让每一次
// 改动都可归因，也让 AI 可以精确预测战斗结果（自动对局统计才有意义）。
// 等数值稳定后再考虑加少量随机，接口已经就位（bonus_percent 参数）。
#pragma once

#include "logic/types.h"

namespace kingdom_fight {

// 敌方主力兵种：数量最多者；并列取枚举序最小的；空军队返回长枪。
[[nodiscard]] UnitType dominant_type(const Army& army);

// 战力：每个兵按"对敌方主力兵种的克制关系"折算，单位是 100 = 1 兵基础战力。
// 例：我方 4 盾卫 对 敌方主力长枪 → 4 × 125 = 500。
[[nodiscard]] int army_power(const Army& self, const Army& enemy);

struct BattleResult {
    bool attacker_won = false;
    int attacker_power = 0;
    int defender_power = 0;
    Army attacker_losses;
    Army defender_losses;
};

// 规则：
//   · 守方空城 → 攻方直接占领，无伤亡
//   · 战力相同 → 守方胜（防守优势，抑制无脑早攻）
//   · 败方全部投入的兵力全灭；胜方按 败/胜 战力比 × kCasualtyFactor 伤亡
[[nodiscard]] BattleResult resolve_battle(const Army& attacker, const Army& defender,
                                          int attacker_bonus_percent = 0,
                                          int defender_bonus_percent = 0);

}  // namespace kingdom_fight
