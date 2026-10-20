#include <string>
#include <vector>

#include "common/config.h"
#include "logic/engine.h"
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

bool has_event(const std::vector<Event>& events, EventType type, PlayerId actor, int reason) {
    for (const Event& event : events) {
        if (event.type == type && event.actor == actor && event.a == reason) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(InitFourPlayersEachOwnOneRegion) {
    GameEngine engine(GameConfig{}, 4, 1);
    const GameState& state = engine.state();
    CHECK_EQ(state.player_count, 4);
    CHECK_EQ(state.turn, 0);
    CHECK(state.phase == Phase::WaitingForOrders);
    for (PlayerId p = 0; p < 4; ++p) {
        CHECK_EQ(state.regions[static_cast<std::size_t>(p)].owner, p);
        CHECK_EQ(engine.count_owned_regions(p), 1);
    }
}

TEST(InitLeavesCenterRegionNeutral) {
    GameEngine engine(GameConfig{}, 4, 1);
    const GameState& state = engine.state();
    CHECK_EQ(state.regions[4].owner, kNeutralOwner);
    CHECK_EQ(state.regions[4].garrison.total(), 0);
}

TEST(InitGivesStartingGarrisonGoldAndDeck) {
    GameEngine engine(GameConfig{}, 4, 1);
    const GameState& state = engine.state();
    for (PlayerId p = 0; p < 4; ++p) {
        const Player& player = state.players[static_cast<std::size_t>(p)];
        CHECK_EQ(player.gold, kStartingGold);
        CHECK_EQ(player.deck.hand_size(), kOpeningHand);
        CHECK_EQ(player.deck.pile_size(), kDeckSize - kOpeningHand);
        CHECK_EQ(state.regions[static_cast<std::size_t>(p)].garrison.total(),
                 kStartSpear + kStartShield + kStartRider);
    }
}

TEST(PlayerCountIsClampedToSupportedRange) {
    GameEngine too_many(GameConfig{}, 9, 1);
    CHECK_EQ(too_many.state().player_count, kMaxPlayers);
    GameEngine too_few(GameConfig{}, 1, 1);
    CHECK_EQ(too_few.state().player_count, 2);
}

TEST(SubmittingAllPlayersMarksTurnReady) {
    GameEngine engine(GameConfig{}, 4, 1);
    CHECK(!engine.all_submitted());
    for (PlayerId p = 0; p < 4; ++p) {
        engine.submit(p, {});
    }
    CHECK(engine.all_submitted());
}

TEST(TwelveEmptyTurnsFinishTheGame) {
    GameEngine engine(GameConfig{}, 4, 1);
    for (int t = 0; t < kMaxTurns; ++t) {
        for (PlayerId p = 0; p < 4; ++p) {
            engine.submit(p, {});
        }
        const auto events = engine.resolve_turn();
        CHECK_EQ(static_cast<int>(events.size()) > 0, true);
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
        for (PlayerId p = 0; p < 4; ++p) {
            engine.submit(p, {});
        }
        engine.resolve_turn();
    }
    const int log_size_before = static_cast<int>(engine.state().log.size());
    const auto events = engine.resolve_turn();
    CHECK(events.empty());
    CHECK_EQ(static_cast<int>(engine.state().log.size()), log_size_before);
}

TEST(MissingOrdersAreTreatedAsTimeout) {
    GameEngine engine(GameConfig{}, 4, 1);
    engine.submit(0, {});  // 只有 P0 交了指令
    const auto events = engine.resolve_turn();
    CHECK(has_event(events, EventType::OrderRejected, 1, static_cast<int>(RejectReason::Timeout)));
    CHECK(has_event(events, EventType::OrderRejected, 2, static_cast<int>(RejectReason::Timeout)));
    CHECK(has_event(events, EventType::OrderRejected, 3, static_cast<int>(RejectReason::Timeout)));
    // P0 已提交，不应该被判超时
    CHECK(!has_event(events, EventType::OrderRejected, 0,
                     static_cast<int>(RejectReason::Timeout)));
}

TEST(UnimplementedOrdersAreRejectedNotSilentlyDropped) {
    GameEngine engine(GameConfig{}, 4, 1);
    Command move{};
    move.type = OrderType::Move;
    move.from = 0;
    move.to = 1;
    engine.submit(0, {move});
    for (PlayerId p = 1; p < 4; ++p) {
        engine.submit(p, {});
    }
    const auto events = engine.resolve_turn();
    CHECK(has_event(events, EventType::OrderRejected, 0,
                    static_cast<int>(RejectReason::NotImplemented)));
}

TEST(ScoreFormulaMatchesDesign) {
    GameEngine engine(GameConfig{}, 4, 1);
    // 1 块领地 * 3 + 10 兵 * 2 + 5 金币 = 28
    CHECK_EQ(engine.score(0), 3 + 10 * 2 + kStartingGold);
}

TEST(SameSeedSameCommandsProduceIdenticalLog) {
    GameEngine a(GameConfig{}, 4, 777);
    GameEngine b(GameConfig{}, 4, 777);
    for (int t = 0; t < kMaxTurns; ++t) {
        for (PlayerId p = 0; p < 4; ++p) {
            a.submit(p, {});
            b.submit(p, {});
        }
        a.resolve_turn();
        b.resolve_turn();
    }
    CHECK(serialize_log(a.state()) == serialize_log(b.state()));
}

TEST(DifferentSeedsShuffleDecksDifferently) {
    GameEngine a(GameConfig{}, 4, 1);
    GameEngine b(GameConfig{}, 4, 2);
    const bool same = a.state().players[0].deck.hand == b.state().players[0].deck.hand &&
                      a.state().players[0].deck.draw_pile == b.state().players[0].deck.draw_pile;
    CHECK(!same);
}

TEST(EveryEventSerializationIsStableAndNonEmpty) {
    GameEngine engine(GameConfig{}, 4, 3);
    for (int t = 0; t < kMaxTurns; ++t) {
        for (PlayerId p = 0; p < 4; ++p) {
            engine.submit(p, {});
        }
        engine.resolve_turn();
    }
    for (const Event& event : engine.state().log) {
        CHECK(!event.serialize().empty());
        CHECK(!event.describe().empty());
        CHECK(event.serialize() == event.serialize());
    }
}
