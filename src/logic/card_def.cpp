#include "logic/card_def.h"

#include <array>
#include <cassert>

namespace kingdom_fight {
namespace {

using enum CardKind;

constexpr std::array<CardDef, kCardCatalogSize> kCards = {{
    // 兵种卡：解锁可招募的兵种（招募时指定兵种）
    {"长枪兵", Unit,    2, 0, UnitType::Spear},
    {"盾卫",   Unit,    2, 0, UnitType::Shield},
    {"骑手",   Unit,    3, 0, UnitType::Rider},
    // 战术卡：决策阶段暗置，结算时揭晓；power = 己方战力加成百分比
    //         不花金币、不花 AP —— 消耗的是"手牌"这个稀缺资源
    {"突袭",   Tactic,  0, 35, UnitType::Spear},
    {"铁壁",   Tactic,  0, 25, UnitType::Shield},
    {"死守",   Tactic,  0, 30, UnitType::Spear},
    {"佯攻",   Tactic,  0, 15, UnitType::Rider},
    // 经济卡：构筑用（买牌/改造牌库），MVP 阶段尚未接入
    {"集市",   Economy, 3, 1, UnitType::Spear},
    {"兵营扩建", Economy, 3, 2, UnitType::Shield},
    {"强征",   Economy, 2, 2, UnitType::Spear},
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

}  // namespace kingdom_fight
