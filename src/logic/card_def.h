// 卡牌目录：数据驱动。加新卡只改 card_def.cpp 的一张表，引擎代码不动。
#pragma once

#include <string_view>
#include <vector>

#include "logic/types.h"

namespace landgrab {

enum class CardKind : int { Unit = 0, Tactic, Economy, Scheme };
inline constexpr int kCardKindCount = 4;

// MVP 12 张（见 docs/02-game-design.md）
inline constexpr int kCardCatalogSize = 12;

struct CardDef {
    std::string_view name;
    CardKind kind = CardKind::Unit;
    int cost = 1;    // 购买/建造花费（金币）
    int power = 0;   // 通用强度：战术卡=加成%，经济卡=产出，阴谋卡=扣金
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

}  // namespace landgrab
