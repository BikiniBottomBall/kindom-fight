#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ai/simple_ai.h"
#include "common/config.h"
#include "logic/card_def.h"
#include "logic/engine.h"
#include "logic/region.h"
#include "test_framework.h"

using namespace kingdom_fight;

namespace {

std::string serialize_log(const GameState& state) {
    std::string out;
    for (const Event& event : state.log) {
        out += event.serialize();
        out += '\n';
    }
    return out;
}

int count_events(const GameState& state, EventType type) {
    int n = 0;
    for (const Event& event : state.log) {
        if (event.type == type) {
            ++n;
        }
    }
    return n;
}

bool has_reject(const std::vector<Event>& events, PlayerId player, RejectReason reason) {
    for (const Event& event : events) {
        if (event.type == EventType::OrderRejected && event.actor == player &&
            event.a == static_cast<int>(reason)) {
            return true;
        }
    }
    return false;
}

bool has_event_of(const std::vector<Event>& events, EventType type, PlayerId actor) {
    for (const Event& event : events) {
        if (event.type == type && event.actor == actor) {
            return true;
        }
    }
    return false;
}

void pass_all(GameEngine& engine, int players) {
    for (PlayerId p = 0; p < players; ++p) {
        engine.submit(p, {});
    }
}

// 找一个 P0 起手带战术卡的种子（手牌是洗出来的，测试不能假设固定牌序）
int find_seed_with_tactic(PlayerId player) {
    for (std::uint64_t seed = 1; seed < 500; ++seed) {
        GameEngine engine(GameConfig{}, 4, seed);
        const DeckState& deck =
            engine.state().players[static_cast<std::size_t>(player)].deck;
        for (const int card : deck.hand) {
            if (card_def(card).kind == CardKind::Tactic) {
                return static_cast<int>(seed);
            }
        }
    }
    return -1;
}

const Army& garrison_of(const GameEngine& engine, RegionId region) {
    return engine.state().regions[static_cast<std::size_t>(region)].garrison;
}

PlayerId owner_of(const GameEngine& engine, RegionId region) {
    return engine.state().regions[static_cast<std::size_t>(region)].owner;
}

Command attack_from(RegionId from, RegionId to, int count) {
    Command cmd{};
    cmd.type = OrderType::Attack;
    cmd.from = from;
    cmd.to = to;
    cmd.count = count;
    return cmd;
}

}  // namespace

// ---------------------------------------------------------------- 初始化

TEST(InitFourPlayersEachOwnOneRegion) {
    GameEngine engine(GameConfig{}, 4, 1);
    const GameState& state = engine.state();
    CHECK_EQ(state.player_count, 4);
    CHECK_EQ(state.turn, 0);
    CHECK(state.phase == Phase::WaitingForOrders);
    for (PlayerId p = 0; p < 4; ++p) {
        CHECK_EQ(owner_of(engine, p), p);
        CHECK_EQ(engine.count_owned_regions(p), 1);
    }
}

TEST(InitLeavesCenterRegionToNeutralMilitia) {
    GameEngine engine(GameConfig{}, 4, 1);
    CHECK_EQ(owner_of(engine, 4), kNeutralOwner);
    CHECK_EQ(garrison_of(engine, 4).total(), kNeutralGarrison);
}

TEST(InitGivesStartingGarrisonGoldAndDeck) {
    GameEngine engine(GameConfig{}, 4, 1);
    const GameState& state = engine.state();
    for (PlayerId p = 0; p < 4; ++p) {
        const Player& player = state.players[static_cast<std::size_t>(p)];
        CHECK_EQ(player.gold, kStartingGold);
        CHECK_EQ(player.deck.hand_size(), kOpeningHand);
        CHECK_EQ(player.deck.pile_size(), kDeckSize - kOpeningHand);
        CHECK_EQ(garrison_of(engine, p).total(), kStartSpear + kStartShield + kStartRider);
    }
}

TEST(PlayerCountIsClampedToSupportedRange) {
    GameEngine too_many(GameConfig{}, 9, 1);
    CHECK_EQ(too_many.state().player_count, kMaxPlayers);
    GameEngine too_few(GameConfig{}, 1, 1);
    CHECK_EQ(too_few.state().player_count, kMinPlayers);
}

TEST(SubmittingAllPlayersMarksTurnReady) {
    GameEngine engine(GameConfig{}, 4, 1);
    CHECK(!engine.all_submitted());
    pass_all(engine, 4);
    CHECK(engine.all_submitted());
}

// ---------------------------------------------------------------- 回合循环

TEST(TwelveEmptyTurnsFinishTheGame) {
    GameEngine engine(GameConfig{}, 4, 1);
    for (int t = 0; t < kMaxTurns; ++t) {
        pass_all(engine, 4);
        CHECK(!engine.resolve_turn().empty());
    }
    const GameState& state = engine.state();
    CHECK_EQ(state.turn, kMaxTurns);
    CHECK(state.phase == Phase::Finished);
    CHECK(state.winner.has_value());
    CHECK_EQ(count_events(state, EventType::TurnBegin), kMaxTurns);
    CHECK_EQ(count_events(state, EventType::GameOver), 1);
}

TEST(ResolvingAfterGameOverReturnsNothing) {
    GameEngine engine(GameConfig{}, 4, 1);
    for (int t = 0; t < kMaxTurns; ++t) {
        pass_all(engine, 4);
        engine.resolve_turn();
    }
    const auto before = engine.state().log.size();
    CHECK(engine.resolve_turn().empty());
    CHECK_EQ(engine.state().log.size(), before);
}

TEST(MissingOrdersAreTreatedAsTimeout) {
    GameEngine engine(GameConfig{}, 4, 1);
    engine.submit(0, {});
    const auto events = engine.resolve_turn();
    CHECK(has_reject(events, 1, RejectReason::Timeout));
    CHECK(has_reject(events, 2, RejectReason::Timeout));
    CHECK(has_reject(events, 3, RejectReason::Timeout));
    CHECK(!has_reject(events, 0, RejectReason::Timeout));
}

TEST(ScoreFormulaMatchesDesign) {
    GameEngine engine(GameConfig{}, 4, 1);
    CHECK_EQ(engine.score(0), 3 + 10 * 2 + kStartingGold);
}

// ---------------------------------------------------------------- 经济（S3）

TEST(FirstTurnProducesRegionIncome) {
    GameEngine engine(GameConfig{}, 4, 1);
    pass_all(engine, 4);
    engine.resolve_turn();
    // 北岭 gold_yield = 2，建筑 Lv0
    CHECK_EQ(engine.state().players[0].gold, kStartingGold + 2);
}

TEST(BuildSpendsGoldAndRaisesLevel) {
    GameEngine engine(GameConfig{}, 4, 1);
    Command build{};
    build.type = OrderType::Build;
    build.from = 0;
    engine.submit(0, {build});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    // 产出 2 后金币 7，建造 -4 → 3
    CHECK_EQ(engine.state().players[0].gold, kStartingGold + 2 - kBuildCost);
    CHECK_EQ(engine.state().regions[0].building_level, 1);
    CHECK(has_event_of(events, EventType::BuildingCompleted, 0));
}

TEST(BuildIsRejectedWhenGoldRunsOut) {
    GameEngine engine(GameConfig{}, 4, 1);
    Command build{};
    build.type = OrderType::Build;
    build.from = 0;
    engine.submit(0, {build, build});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK_EQ(engine.state().regions[0].building_level, 1);
    CHECK(has_reject(events, 0, RejectReason::NotEnoughGold));
}

TEST(BuildIsRejectedAtMaxLevel) {
    GameEngine engine(GameConfig{}, 4, 1);
    pass_all(engine, 4);
    engine.resolve_turn();
    pass_all(engine, 4);
    engine.resolve_turn();

    Command build{};
    build.type = OrderType::Build;
    build.from = 0;
    engine.submit(0, {build, build, build});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK_EQ(engine.state().regions[0].building_level, kBuildMaxLevel);
    CHECK(has_reject(events, 0, RejectReason::BuildingMaxed));
}

TEST(RecruitAddsTroopsAndCostsGold) {
    GameEngine engine(GameConfig{}, 4, 1);
    Command recruit{};
    recruit.type = OrderType::Recruit;
    recruit.from = 0;
    recruit.unit = UnitType::Spear;
    recruit.count = 1;
    engine.submit(0, {recruit});
    pass_all(engine, 4);
    engine.resolve_turn();
    CHECK_EQ(engine.state().players[0].gold, kStartingGold + 2 - kRecruitCostPerUnit);
    CHECK_EQ(garrison_of(engine, 0).total(), 11);
    CHECK_EQ(garrison_of(engine, 0).of(UnitType::Spear), kStartSpear + 1);
}

TEST(RecruitIsCappedByRegionYield) {
    GameEngine engine(GameConfig{}, 4, 1);
    Command recruit{};
    recruit.type = OrderType::Recruit;
    recruit.from = 0;  // 北岭招募上限 1
    recruit.unit = UnitType::Spear;
    recruit.count = 2;
    engine.submit(0, {recruit});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK(has_reject(events, 0, RejectReason::RecruitLimit));
    CHECK_EQ(garrison_of(engine, 0).total(), 10);
}

// ---------------------------------------------------------------- 调动与战斗（S4/S5）

TEST(MoveTransfersTroopsBetweenOwnRegions) {
    GameEngine engine(GameConfig{}, 4, 1);
    engine.submit(0, {attack_from(0, 4, 10)});  // 先打下中枢
    pass_all(engine, 4);
    engine.resolve_turn();
    CHECK_EQ(owner_of(engine, 4), 0);

    Command move{};
    move.type = OrderType::Move;
    move.from = 4;
    move.to = 0;
    move.count = 3;
    engine.submit(0, {move});
    pass_all(engine, 4);
    engine.resolve_turn();
    CHECK_EQ(garrison_of(engine, 0).total(), 3);
    CHECK_EQ(garrison_of(engine, 4).total(), 4);
}

TEST(MoveToEnemyRegionIsRejected) {
    GameEngine engine(GameConfig{}, 4, 1);
    Command move{};
    move.type = OrderType::Move;
    move.from = 0;
    move.to = 1;  // P1 的领地
    move.count = 3;
    engine.submit(0, {move});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK(has_reject(events, 0, RejectReason::IllegalTarget));
    CHECK_EQ(garrison_of(engine, 0).total(), 10);
}

TEST(AttackCapturesNeutralCenter) {
    GameEngine engine(GameConfig{}, 4, 1);
    engine.submit(0, {attack_from(0, 4, 10)});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK(has_event_of(events, EventType::BattleResolved, 0));
    CHECK(has_event_of(events, EventType::RegionCaptured, 0));
    CHECK_EQ(owner_of(engine, 4), 0);
    CHECK_EQ(garrison_of(engine, 0).total(), 0);   // 全部出征
    CHECK_EQ(garrison_of(engine, 4).total(), 7);   // 存活部队进驻
    CHECK_EQ(engine.count_owned_regions(0), 2);
}

TEST(AttackOnEqualForceIsRepelled) {
    GameEngine engine(GameConfig{}, 4, 1);
    engine.submit(0, {attack_from(0, 1, 10)});  // 打 P1 老家，兵力相同
    pass_all(engine, 4);
    engine.resolve_turn();
    CHECK_EQ(owner_of(engine, 1), 1);            // 守方胜，地盘不掉
    CHECK_EQ(garrison_of(engine, 1).total(), 6); // 守方也付出代价
    CHECK_EQ(garrison_of(engine, 0).total(), 0); // 攻方全灭
}

TEST(AttackOnOwnRegionIsRejected) {
    GameEngine engine(GameConfig{}, 4, 1);
    engine.submit(0, {attack_from(0, 0, 5)});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK(has_reject(events, 0, RejectReason::IllegalTarget));
}

TEST(AttackOverCommittingTroopsIsRejected) {
    GameEngine engine(GameConfig{}, 4, 1);
    engine.submit(0, {attack_from(0, 4, 99)});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK(has_reject(events, 0, RejectReason::NotEnoughTroops));
    CHECK_EQ(owner_of(engine, 4), kNeutralOwner);
}

TEST(TacticCardIsRevealedAndAddsPower) {
    const int seed = find_seed_with_tactic(0);
    CHECK(seed > 0);
    if (seed <= 0) {
        return;
    }
    GameEngine engine(GameConfig{}, 4, static_cast<std::uint64_t>(seed));

    int tactic_card = -1;
    for (const int card : engine.state().players[0].deck.hand) {
        if (card_def(card).kind == CardKind::Tactic) {
            tactic_card = card;
            break;
        }
    }
    CHECK(tactic_card >= 0);

    Command tactic{};
    tactic.type = OrderType::PlayTactic;
    tactic.card = tactic_card;
    tactic.to = 4;
    engine.submit(0, {tactic, attack_from(0, 4, 10)});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();

    CHECK(has_event_of(events, EventType::TacticRevealed, 0));
    CHECK_EQ(owner_of(engine, 4), 0);
    // 手牌被消耗：起手 3 + 抽 1 - 打出 1 = 3
    CHECK_EQ(engine.state().players[0].deck.hand_size(), kOpeningHand);
}

TEST(TacticCardNotInHandIsRejected) {
    GameEngine engine(GameConfig{}, 4, 1);
    const DeckState& deck = engine.state().players[0].deck;
    int missing = -1;
    for (int id = 0; id < kCardCatalogSize; ++id) {
        if (card_def(id).kind != CardKind::Tactic) {
            continue;
        }
        bool in_hand = false;
        for (const int card : deck.hand) {
            if (card == id) {
                in_hand = true;
            }
        }
        if (!in_hand) {
            missing = id;
            break;
        }
    }
    CHECK(missing >= 0);
    if (missing < 0) {
        return;
    }

    Command tactic{};
    tactic.type = OrderType::PlayTactic;
    tactic.card = missing;
    tactic.to = 4;
    engine.submit(0, {tactic});
    pass_all(engine, 4);
    const auto events = engine.resolve_turn();
    CHECK(has_reject(events, 0, RejectReason::IllegalTarget));
}

// ---------------------------------------------------------------- 决定论

TEST(SameSeedSameCommandsProduceIdenticalLog) {
    GameEngine a(GameConfig{}, 4, 777);
    GameEngine b(GameConfig{}, 4, 777);
    for (int t = 0; t < kMaxTurns; ++t) {
        a.submit(0, {attack_from(0, 4, 10)});
        b.submit(0, {attack_from(0, 4, 10)});
        pass_all(a, 4);
        pass_all(b, 4);
        a.resolve_turn();
        b.resolve_turn();
    }
    CHECK(serialize_log(a.state()) == serialize_log(b.state()));
}

TEST(DifferentSeedsShuffleDecksDifferently) {
    GameEngine a(GameConfig{}, 4, 1);
    GameEngine b(GameConfig{}, 4, 2);
    const auto& deck_a = a.state().players[0].deck;
    const auto& deck_b = b.state().players[0].deck;
    const bool identical = deck_a.hand == deck_b.hand && deck_a.draw_pile == deck_b.draw_pile;
    CHECK(!identical);
}

TEST(EveryEventSerializationIsStableAndNonEmpty) {
    GameEngine engine(GameConfig{}, 4, 3);
    for (int t = 0; t < kMaxTurns; ++t) {
        for (PlayerId p = 0; p < 4; ++p) {
            engine.submit(p, ai::make_orders(engine, p));
        }
        engine.resolve_turn();
    }
    for (const Event& event : engine.state().log) {
        CHECK(!event.serialize().empty());
        CHECK(!event.describe().empty());
        CHECK(event.serialize() == event.serialize());
    }
}

// ---------------------------------------------------------------- AI 集成

TEST(FourAiPlayersFinishAGameWithRealBattles) {
    GameEngine engine(GameConfig{}, 4, 99);
    int turns = 0;
    while (engine.state().phase != Phase::Finished && turns < kMaxTurns) {
        for (PlayerId p = 0; p < 4; ++p) {
            engine.submit(p, ai::make_orders(engine, p));
        }
        engine.resolve_turn();
        ++turns;
    }
    CHECK(engine.state().phase == Phase::Finished);
    CHECK(engine.state().winner.has_value());
    CHECK(count_events(engine.state(), EventType::BattleResolved) > 0);
    // AI 除了打仗还得会发育（建造或招募，二者之一）
    const int economy_actions = count_events(engine.state(), EventType::UnitsRecruited) +
                                count_events(engine.state(), EventType::BuildingCompleted);
    CHECK(economy_actions > 0);
}

TEST(AiNeverCommitsItsWholeGarrison) {
    // 全压上去 = 老家空城被邻居白捡，会把对局变成"谁先出手谁赢"的快攻游戏
    GameEngine engine(GameConfig{}, 4, 99);
    engine.submit(0, ai::make_orders(engine, 0));
    pass_all(engine, 4);
    engine.resolve_turn();
    // P0 若发起进攻，老家必须还有人
    if (count_events(engine.state(), EventType::BattleResolved) > 0) {
        CHECK(garrison_of(engine, 0).total() >= 1);
    }
}
