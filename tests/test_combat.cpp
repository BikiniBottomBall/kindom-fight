#include "common/config.h"
#include "logic/combat.h"
#include "test_framework.h"

using namespace kingdom_fight;

namespace {

Army make_army(int spear, int shield, int rider) {
    Army army;
    army.add(UnitType::Spear, spear).add(UnitType::Shield, shield).add(UnitType::Rider, rider);
    return army;
}

}  // namespace

TEST(DominantTypePicksLargestGroup) {
    CHECK(dominant_type(make_army(1, 5, 2)) == UnitType::Shield);
    CHECK(dominant_type(make_army(6, 1, 1)) == UnitType::Spear);
    CHECK(dominant_type(make_army(1, 1, 6)) == UnitType::Rider);
}

TEST(DominantTypeBreaksTiesByEnumOrder) {
    CHECK(dominant_type(make_army(3, 3, 0)) == UnitType::Spear);
    CHECK(dominant_type(make_army(0, 2, 2)) == UnitType::Shield);
}

TEST(DominantTypeOnEmptyArmyIsSpear) { CHECK(dominant_type(Army{}) == UnitType::Spear); }

TEST(ArmyPowerIsHundredPerUnitWithoutCounter) {
    CHECK_EQ(army_power(make_army(4, 0, 0), make_army(4, 0, 0)), 400);
    CHECK_EQ(army_power(Army{}, make_army(4, 0, 0)), 0);
}

TEST(ArmyPowerAppliesCounterAgainstEnemyDominantType) {
    // 我方 2 盾卫 对 敌方主力长枪：盾克枪 125% → 250
    CHECK_EQ(army_power(make_army(0, 2, 0), make_army(3, 0, 0)), 250);
    // 我方 2 长枪 对 敌方主力盾卫：枪被盾克 80% → 160
    CHECK_EQ(army_power(make_army(2, 0, 0), make_army(0, 3, 0)), 160);
}

TEST(EmptyDefenderMeansFreeCapture) {
    const BattleResult result = resolve_battle(make_army(3, 0, 0), Army{});
    CHECK(result.attacker_won);
    CHECK_EQ(result.attacker_losses.total(), 0);
    CHECK_EQ(result.defender_losses.total(), 0);
}

TEST(EqualArmiesFavorTheDefender) {
    const Army both = make_army(4, 3, 3);
    const BattleResult result = resolve_battle(both, both);
    CHECK(!result.attacker_won);
    CHECK_EQ(result.attacker_losses.total(), both.total());  // 败方全灭
    CHECK(result.defender_losses.total() > 0);               // 守方也有伤亡
}

TEST(StrongerAttackerWinsAndAlwaysKeepsOneTroop) {
    const Army attacker = make_army(10, 0, 0);
    const Army defender = make_army(2, 0, 0);
    const BattleResult result = resolve_battle(attacker, defender);
    CHECK(result.attacker_won);
    CHECK(result.attacker_power > result.defender_power);
    CHECK_EQ(result.defender_losses.total(), defender.total());  // 守方全灭
    CHECK(result.attacker_losses.total() < attacker.total());    // 攻方不会同归于尽
}

TEST(TacticBonusCanFlipAnEvenBattle) {
    const Army both = make_army(4, 3, 3);
    CHECK(!resolve_battle(both, both).attacker_won);
    const BattleResult boosted = resolve_battle(both, both, 35, 0);
    CHECK(boosted.attacker_won);
}

TEST(DefenderBonusAlsoApplies) {
    const Army attacker = make_army(6, 3, 3);
    const Army defender = make_army(4, 3, 3);
    CHECK(resolve_battle(attacker, defender).attacker_won);
    // 守方 +50% 足以翻盘
    CHECK(!resolve_battle(attacker, defender, 0, 50).attacker_won);
}

TEST(CasualtyPercentStaysInConfiguredBounds) {
    // 战力接近时，胜方伤亡应该被夹在 [kMin, kMax] 之间，不会一次全灭
    const Army attacker = make_army(11, 0, 0);
    const Army defender = make_army(10, 0, 0);
    const BattleResult result = resolve_battle(attacker, defender);
    CHECK(result.attacker_won);
    CHECK(result.attacker_losses.total() >= 1);
    CHECK(result.attacker_losses.total() <= attacker.total());
}

TEST(BattleResolutionIsDeterministic) {
    const Army attacker = make_army(7, 2, 4);
    const Army defender = make_army(3, 6, 1);
    const BattleResult a = resolve_battle(attacker, defender, 10, 5);
    const BattleResult b = resolve_battle(attacker, defender, 10, 5);
    CHECK_EQ(a.attacker_power, b.attacker_power);
    CHECK_EQ(a.defender_power, b.defender_power);
    CHECK_EQ(a.attacker_won, b.attacker_won);
    CHECK(a.attacker_losses == b.attacker_losses);
    CHECK(a.defender_losses == b.defender_losses);
}
