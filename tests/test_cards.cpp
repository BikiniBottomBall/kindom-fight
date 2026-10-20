#include <set>
#include <string>

#include "logic/card_def.h"
#include "test_framework.h"

using namespace kingdom_fight;

TEST(CardCatalogHasTenCards) { CHECK_EQ(kCardCatalogSize, 10); }

TEST(CardNamesAreNonEmptyAndUnique) {
    std::set<std::string> names;
    for (int id = 0; id < kCardCatalogSize; ++id) {
        const std::string name(card_def(id).name);
        CHECK(!name.empty());
        names.insert(name);
    }
    CHECK_EQ(static_cast<int>(names.size()), kCardCatalogSize);
}

TEST(UnitAndEconomyCardsCostGold) {
    for (int id = 0; id < kCardCatalogSize; ++id) {
        const CardDef& card = card_def(id);
        if (card.kind == CardKind::Unit || card.kind == CardKind::Economy) {
            CHECK(card.cost > 0);
        }
    }
}

TEST(TacticCardsAreFreeButConsumeHand) {
    // 设计决定：战术卡不花金币、不花 AP，消耗的是"手牌"这个稀缺资源
    for (int id = 0; id < kCardCatalogSize; ++id) {
        const CardDef& card = card_def(id);
        if (card.kind == CardKind::Tactic) {
            CHECK_EQ(card.cost, 0);
            CHECK(card.power > 0);
        }
    }
}

TEST(ThreeCardKindsAreInUse) {
    int mask = 0;
    for (int id = 0; id < kCardCatalogSize; ++id) {
        mask |= 1 << static_cast<int>(card_def(id).kind);
    }
    const int expected = (1 << static_cast<int>(CardKind::Unit)) |
                         (1 << static_cast<int>(CardKind::Tactic)) |
                         (1 << static_cast<int>(CardKind::Economy));
    CHECK_EQ(mask, expected);
}

TEST(SchemeCardsAreIntentionallyAbsent) {
    // MVP 故意不做阴谋卡（最容易失衡的一类），枚举位保留
    for (int id = 0; id < kCardCatalogSize; ++id) {
        CHECK(card_def(id).kind != CardKind::Scheme);
    }
}

TEST(AtLeastThreeTacticCardsExist) {
    int tactics = 0;
    for (int id = 0; id < kCardCatalogSize; ++id) {
        if (card_def(id).kind == CardKind::Tactic) {
            ++tactics;
        }
    }
    CHECK(tactics >= 3);
}
