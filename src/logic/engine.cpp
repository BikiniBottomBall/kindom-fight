#include "logic/engine.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace kingdom_fight {
namespace {

Army starting_garrison() {
    Army army;
    army.add(UnitType::Spear, kStartSpear)
        .add(UnitType::Shield, kStartShield)
        .add(UnitType::Rider, kStartRider);
    return army;
}

}  // namespace

const char* to_string(Phase phase) {
    switch (phase) {
        case Phase::WaitingForOrders: return "收指令中";
        case Phase::Resolving:        return "结算中";
        case Phase::Finished:         return "已结束";
    }
    return "未知";
}

GameEngine::GameEngine(GameConfig cfg, int player_count, std::uint64_t seed) : rng_(seed) {
    state_.cfg = cfg;
    state_.player_count = std::clamp(player_count, kMinPlayers, std::min(kMaxPlayers, kRegionCount));

    // 区域：全部无主，无驻军
    for (RegionId r = 0; r < kRegionCount; ++r) {
        Region& region = state_.regions[static_cast<std::size_t>(r)];
        region.id = r;
        region.owner = kNeutralOwner;
        region.garrison = Army{};
        region.building_level = 0;
    }

    // 玩家：每人先占一块领地，拿一份确定洗过的牌库
    for (int i = 0; i < state_.player_count; ++i) {
        Player& player = state_.players[static_cast<std::size_t>(i)];
        player.id = i;
        player.leader = i;
        player.gold = state_.cfg.starting_gold;
        player.ap = 0;
        player.alive = true;
        player.submitted = false;

        for (int c = 0; c < kDeckSize; ++c) {
            player.deck.draw_pile.push_back(c % kCardCatalogSize);
        }
        shuffle_deck(player.deck.draw_pile);
        for (int k = 0; k < kOpeningHand && !player.deck.draw_pile.empty(); ++k) {
            player.deck.hand.push_back(player.deck.draw_pile.back());
            player.deck.draw_pile.pop_back();
        }

        Region& home = state_.regions[static_cast<std::size_t>(i)];
        home.owner = i;
        home.garrison = starting_garrison();
    }

    state_.turn = 0;
    state_.phase = Phase::WaitingForOrders;

    Event begin{};
    begin.type = EventType::GameBegin;
    begin.a = state_.player_count;
    emit(begin);
}

void GameEngine::emit(Event event) { state_.log.push_back(std::move(event)); }

void GameEngine::shuffle_deck(std::vector<int>& cards) {
    for (std::size_t i = cards.size(); i > 1; --i) {
        const int j = rng_.next_int(0, static_cast<int>(i) - 1);
        std::swap(cards[i - 1], cards[static_cast<std::size_t>(j)]);
    }
}

void GameEngine::submit(PlayerId player, std::vector<Command> orders) {
    if (player < 0 || player >= state_.player_count) {
        return;
    }
    Player& p = state_.players[static_cast<std::size_t>(player)];
    if (!p.alive || state_.phase == Phase::Finished) {
        return;
    }
    p.pending_orders.insert(p.pending_orders.end(), orders.begin(), orders.end());
    p.submitted = true;
}

void GameEngine::submit_timeout(PlayerId player) {
    if (player < 0 || player >= state_.player_count) {
        return;
    }
    Player& p = state_.players[static_cast<std::size_t>(player)];
    if (!p.alive) {
        return;
    }
    p.pending_orders.clear();
    p.submitted = true;
}

bool GameEngine::all_submitted() const {
    for (int i = 0; i < state_.player_count; ++i) {
        const Player& p = state_.players[static_cast<std::size_t>(i)];
        if (p.alive && !p.submitted) {
            return false;
        }
    }
    return true;
}

int GameEngine::count_owned_regions(PlayerId player) const {
    int n = 0;
    for (const Region& region : state_.regions) {
        if (region.owner == player) {
            ++n;
        }
    }
    return n;
}

int GameEngine::count_total_troops(PlayerId player) const {
    int n = 0;
    for (const Region& region : state_.regions) {
        if (region.owner == player) {
            n += region.garrison.total();
        }
    }
    return n;
}

int GameEngine::score(PlayerId player) const {
    if (player < 0 || player >= state_.player_count) {
        return 0;
    }
    const Player& p = state_.players[static_cast<std::size_t>(player)];
    return count_owned_regions(player) * 3 + count_total_troops(player) * 2 + p.gold;
}

void GameEngine::begin_turn() {
    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        if (!p.alive) {
            continue;
        }
        if (!p.submitted) {
            // 服务端不会为没交指令的人等下去：按超时托管处理，并留下痕迹。
            Event rejected{};
            rejected.type = EventType::OrderRejected;
            rejected.actor = i;
            rejected.a = static_cast<int>(RejectReason::Timeout);
            rejected.b = static_cast<int>(OrderType::Pass);
            emit(rejected);
            p.pending_orders.clear();
            p.submitted = true;
        }
        p.ap = state_.cfg.ap_per_turn;

        Event accepted{};
        accepted.type = EventType::OrdersAccepted;
        accepted.actor = i;
        accepted.a = static_cast<int>(p.pending_orders.size());
        emit(accepted);
    }
}

// S3：资源产出与建筑完工。当前是预留缝隙，行为为空（不产金、不完工）。
void GameEngine::phase_produce_resources() {}

void GameEngine::phase_complete_buildings() {}

// S4/S5：合法性校验与执行。当前把所有非 Pass 指令判为「尚未实现」，
// 目的是让 S2 的回合循环能空转，同时把真实的校验点标出来。
void GameEngine::phase_execute_orders() {
    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        if (!p.alive) {
            continue;
        }
        for (const Command& cmd : p.pending_orders) {
            if (cmd.type == OrderType::Pass) {
                continue;
            }
            Event rejected{};
            rejected.type = EventType::OrderRejected;
            rejected.actor = i;
            rejected.a = static_cast<int>(RejectReason::NotImplemented);
            rejected.b = static_cast<int>(cmd.type);
            emit(rejected);
        }
    }
}

// S5：战斗结算。当前为空。
void GameEngine::phase_resolve_battles() {}

void GameEngine::phase_check_victory() {
    std::array<int, kMaxPlayers> owned{};
    std::array<int, kMaxPlayers> troops{};
    for (const Region& region : state_.regions) {
        if (region.owner != kNeutralOwner && region.owner < state_.player_count) {
            owned[static_cast<std::size_t>(region.owner)] += 1;
            troops[static_cast<std::size_t>(region.owner)] += region.garrison.total();
        }
    }

    // 淘汰：既无领地也无兵力
    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        if (!p.alive) {
            continue;
        }
        if (owned[static_cast<std::size_t>(i)] == 0 && troops[static_cast<std::size_t>(i)] == 0) {
            p.alive = false;
            Event eliminated{};
            eliminated.type = EventType::PlayerEliminated;
            eliminated.actor = i;
            emit(eliminated);
        }
    }

    std::optional<PlayerId> winner;
    WinReason reason = WinReason::HighestScore;

    for (int i = 0; i < state_.player_count; ++i) {
        if (owned[static_cast<std::size_t>(i)] >= kWinRegions) {
            winner = i;
            reason = WinReason::Conquer;
            break;
        }
    }

    if (!winner) {
        int alive_count = 0;
        PlayerId last_alive = kNeutralOwner;
        for (int i = 0; i < state_.player_count; ++i) {
            if (state_.players[static_cast<std::size_t>(i)].alive) {
                ++alive_count;
                last_alive = i;
            }
        }
        if (alive_count == 1) {
            winner = last_alive;
            reason = WinReason::LastStanding;
        }
    }

    if (!winner && state_.turn >= state_.cfg.max_turns) {
        // 分数最高者胜；同分取玩家 id 较小者 —— 平手规则必须确定，否则不可复现。
        int best_score = -1;
        for (int i = 0; i < state_.player_count; ++i) {
            if (!state_.players[static_cast<std::size_t>(i)].alive) {
                continue;
            }
            const int s = score(i);
            if (s > best_score) {
                best_score = s;
                winner = i;
            }
        }
        reason = WinReason::HighestScore;
    }

    if (winner) {
        state_.winner = winner;
        Event over{};
        over.type = EventType::GameOver;
        over.actor = *winner;
        over.a = static_cast<int>(reason);
        over.b = state_.turn;
        emit(over);
        state_.phase = Phase::Finished;
    }
}

void GameEngine::end_turn() {
    Event e{};
    e.type = EventType::TurnEnd;
    e.a = state_.turn;
    emit(e);

    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        p.pending_orders.clear();
        p.submitted = false;
    }
}

std::vector<Event> GameEngine::resolve_turn() {
    if (state_.phase == Phase::Finished) {
        return {};
    }
    const std::size_t mark = state_.log.size();

    ++state_.turn;
    state_.phase = Phase::Resolving;

    Event turn_begin{};
    turn_begin.type = EventType::TurnBegin;
    turn_begin.a = state_.turn;
    emit(turn_begin);

    begin_turn();

    // 结算顺序固定（设计稿 §4②）：产出 → 完工 → 指令 → 战斗 → 胜负。
    // 顺序一旦变化，同一份输入就会产出不同结果，确定性就没了。
    phase_produce_resources();
    phase_complete_buildings();
    phase_execute_orders();
    phase_resolve_battles();
    end_turn();
    phase_check_victory();

    if (state_.phase != Phase::Finished) {
        state_.phase = Phase::WaitingForOrders;
    }

    return {state_.log.begin() + static_cast<std::ptrdiff_t>(mark), state_.log.end()};
}

}  // namespace kingdom_fight
