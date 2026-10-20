#include "logic/combat.h"

#include <algorithm>

#include "common/config.h"

namespace kingdom_fight {
namespace {

int clamp_casualty(int percent) {
    return std::clamp(percent, kMinCasualtyPercent, kMaxCasualtyPercent);
}

}  // namespace

UnitType dominant_type(const Army& army) {
    int best = -1;
    UnitType best_type = UnitType::Spear;
    for (int t = 0; t < kUnitTypeCount; ++t) {
        if (army.count[static_cast<std::size_t>(t)] > best) {
            best = army.count[static_cast<std::size_t>(t)];
            best_type = static_cast<UnitType>(t);
        }
    }
    return best_type;
}

int army_power(const Army& self, const Army& enemy) {
    const UnitType foe = dominant_type(enemy);
    int power = 0;
    for (int t = 0; t < kUnitTypeCount; ++t) {
        power += self.count[static_cast<std::size_t>(t)] *
                 counter_percent(static_cast<UnitType>(t), foe);
    }
    return power;
}

BattleResult resolve_battle(const Army& attacker, const Army& defender, int attacker_bonus_percent,
                            int defender_bonus_percent) {
    BattleResult result;

    // 空城：直接占领，无伤亡
    if (defender.empty()) {
        result.attacker_won = true;
        result.attacker_power = army_power(attacker, defender);
        result.defender_power = 0;
        return result;
    }
    if (attacker.empty()) {
        result.attacker_won = false;
        result.attacker_power = 0;
        result.defender_power = army_power(defender, attacker);
        return result;
    }

    result.attacker_power = army_power(attacker, defender) * (100 + attacker_bonus_percent) / 100;
    result.defender_power = army_power(defender, attacker) * (100 + defender_bonus_percent) / 100;

    // 平手判守方胜：没有明显优势的进攻不该有收益
    result.attacker_won = result.attacker_power > result.defender_power;

    if (result.attacker_won) {
        result.defender_losses = defender;  // 败方全灭
        const int ratio = result.defender_power * 100 / std::max(result.attacker_power, 1);
        result.attacker_losses = attacker.scaled(clamp_casualty(ratio * kCasualtyFactor / 100));
    } else {
        result.attacker_losses = attacker;  // 败方全灭
        const int ratio = result.attacker_power * 100 / std::max(result.defender_power, 1);
        result.defender_losses = defender.scaled(clamp_casualty(ratio * kCasualtyFactor / 100));
    }
    return result;
}

}  // namespace kingdom_fight
