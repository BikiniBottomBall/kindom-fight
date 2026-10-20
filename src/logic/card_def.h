// 卡牌目录：数据驱动。加新卡只改 card_def.cpp 的一张表，引擎代码不动。
#pragma once

#include <string_view>
#include <vector>

#include "logic/types.h"

namespace kingdom_fight {

enum class CardKind : int {
    Unit = 0,
    Tactic,
    Economy,
    Scheme,  // 阴谋卡：MVP 阶段故意不用（最容易失衡的一类），枚举位先留着
};
inline constexpr int kCardKindCount = 4;

// MVP 10 张（设计稿 v0.2：砍掉阴谋卡）
inline constexpr int kCardCatalogSize = 10;

struct CardDef {
    std::string_view name;
    CardKind kind = CardKind::Unit;
    int cost = 0;    // 金币花费；战术卡为 0（手牌本身就是资源，打出去即消耗）
    int power = 0;   // 战术卡 = 战力加成百分比；经济卡 = 预留参数
    UnitType unit = UnitType::Spear;  // 仅兵种卡有意义
};

[[nodiscard]] const CardDef& card_def(int card_id);
[[nodiscard]] const char* to_string(CardKind kind);

// 一个玩家的牌库/手牌/弃牌。只存 id，方便以后序列化与按人裁剪下发（S17）。
struct DeckState {
    std::vector<int> draw_pile;
    std::vector<int> hand;
    std::vector<int> discard;

    [[nodiscard]] int hand_size() const { return static_cast<int>(hand.size()); }
    [[nodiscard]] int pile_size() const { return static_cast<int>(draw_pile.size()); }
};

}  // namespace kingdom_fight
