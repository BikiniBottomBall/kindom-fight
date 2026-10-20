#include "logic/types.h"
#include "test_framework.h"

using kingdom_fight::Army;
using kingdom_fight::counter_percent;
using kingdom_fight::counters;
using kingdom_fight::UnitType;

TEST(ArmyTotalAndAdd) {
    Army army;
    CHECK_EQ(army.total(), 0);
    army.add(UnitType::Spear, 4).add(UnitType::Shield, 3).add(UnitType::Rider, 2);
    CHECK_EQ(army.total(), 9);
    CHECK_EQ(army.of(UnitType::Spear), 4);
    CHECK_EQ(army.of(UnitType::Shield), 3);
    CHECK_EQ(army.of(UnitType::Rider), 2);
}

TEST(ArmyEqualityComparesAllTypes) {
    Army a;
    Army b;
    a.add(UnitType::Spear, 1);
    b.add(UnitType::Spear, 1);
    CHECK(a == b);
    b.add(UnitType::Rider, 1);
    CHECK(a != b);
}

TEST(CounterChainSpearBeatsRiderBeatsShieldBeatsSpear) {
    CHECK(counters(UnitType::Spear, UnitType::Rider));
    CHECK(counters(UnitType::Rider, UnitType::Shield));
    CHECK(counters(UnitType::Shield, UnitType::Spear));
}

TEST(CounterIsNotReflexiveOrSymmetric) {
    CHECK(!counters(UnitType::Spear, UnitType::Spear));
    CHECK(!counters(UnitType::Rider, UnitType::Spear));
    CHECK(!counters(UnitType::Shield, UnitType::Rider));
    CHECK(!counters(UnitType::Spear, UnitType::Shield));
}

TEST(CounterPercentValues) {
    CHECK_EQ(counter_percent(UnitType::Spear, UnitType::Rider), 125);
    CHECK_EQ(counter_percent(UnitType::Rider, UnitType::Spear), 80);
    CHECK_EQ(counter_percent(UnitType::Spear, UnitType::Spear), 100);
    CHECK_EQ(counter_percent(UnitType::Rider, UnitType::Rider), 100);
}
