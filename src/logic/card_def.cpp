#include "logic/card_def.h"

#include <cassert>

namespace landgrab {
namespace {

using enum CardKind;

constexpr std::array<CardDef, kCardCatalogSize> kCards = {{
    // 兵种卡：解锁可招募的兵种
    {"长枪兵", Unit,    2, 0, UnitType::Spear},
    {"盾卫",   Unit,    2, 0, UnitType::Shield},
    {"骑手",   Unit,    3, 0, UnitType::Rider},
    // 战术卡：决策阶段暗置，结算时揭晓，power = 战力加成百分比
    {"突袭",   Tactic,  2, 30, UnitType::Spear},
    {"铁壁",   Tactic,  2, 30, UnitType::Shield},
    {"死守",   Tactic,  2, 25, UnitType::Spear},
    {"佯攻",   Tactic,  1, 15, UnitType::Rider},
    // 经济卡：power = 产出加成
    {"集市",   Economy, 3, 1, UnitType::Spear},
    {"兵营扩建", Economy, 3, 2, UnitType::Shield},
    {"强征",   Economy, 2, 2, UnitType::Spear},
    // 阴谋卡：power = 扣对手金币
    {"断粮",   Scheme,  2, 2, UnitType::Spear},
    {"离间",   Scheme,  3, 3, UnitType::Rider},
}};

}  // namespace

const CardDef& card_def(int card_id) {
    assert(card_id >= 0 && card_id < kCardCatalogSize);
    return kCards[static_cast<std::size_t>(card_id)];
}

const char* to_string(CardKind kind) {
    switch (kind) {
        case CardKind::Unit:    return "兵种";
        case CardKind::Tactic:  return "战术";
        case CardKind::Economy: return "经济";
        case CardKind::Scheme:  return "阴谋";
    }
    return "未知";
}

}  // namespace landgrab
