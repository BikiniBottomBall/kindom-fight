#include "logic/types.h"

namespace landgrab {

const char* to_string(UnitType type) {
    switch (type) {
        case UnitType::Spear:  return "长枪";
        case UnitType::Shield: return "盾卫";
        case UnitType::Rider:  return "骑手";
    }
    return "未知";
}

bool counters(UnitType attacker, UnitType defender) {
    return (attacker == UnitType::Spear && defender == UnitType::Rider) ||
           (attacker == UnitType::Rider && defender == UnitType::Shield) ||
           (attacker == UnitType::Shield && defender == UnitType::Spear);
}

int counter_percent(UnitType attacker, UnitType defender) {
    if (counters(attacker, defender)) {
        return 125;
    }
    if (counters(defender, attacker)) {
        return 80;
    }
    return 100;
}

}  // namespace landgrab
