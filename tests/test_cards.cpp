#include <set>
#include <string>

#include "logic/card_def.h"
#include "test_framework.h"

using kingdom_fight::card_def;
using kingdom_fight::CardKind;
using kingdom_fight::kCardCatalogSize;

TEST(CardCatalogHasTwelveCards) { CHECK_EQ(kCardCatalogSize, 12); }

TEST(CardNamesAreNonEmptyAndUnique) {
    std::set<std::string> names;
    for (int id = 0; id < kCardCatalogSize; ++id) {
        const std::string name(card_def(id).name);
        CHECK(!name.empty());
        names.insert(name);
    }
    CHECK_EQ(static_cast<int>(names.size()), kCardCatalogSize);
}

TEST(CardCostsArePositive) {
    for (int id = 0; id < kCardCatalogSize; ++id) {
        CHECK(card_def(id).cost > 0);
    }
}

TEST(AllFourCardKindsArePresent) {
    int mask = 0;
    for (int id = 0; id < kCardCatalogSize; ++id) {
        mask |= 1 << static_cast<int>(card_def(id).kind);
    }
    const int expected = (1 << static_cast<int>(CardKind::Unit)) |
                         (1 << static_cast<int>(CardKind::Tactic)) |
                         (1 << static_cast<int>(CardKind::Economy)) |
                         (1 << static_cast<int>(CardKind::Scheme));
    CHECK_EQ(mask, expected);
}

TEST(TacticAndEconomyCardsCarryPower) {
    for (int id = 0; id < kCardCatalogSize; ++id) {
        const auto& card = card_def(id);
        if (card.kind == CardKind::Tactic || card.kind == CardKind::Economy ||
            card.kind == CardKind::Scheme) {
            CHECK(card.power > 0);
        }
    }
}
