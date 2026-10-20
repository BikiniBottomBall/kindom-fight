#include "logic/engine.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "logic/combat.h"

namespace kingdom_fight {
namespace {

Army starting_garrison() {
    Army army;
    army.add(UnitType::Spear, kStartSpear)
        .add(UnitType::Shield, kStartShield)
        .add(UnitType::Rider, kStartRider);
    return army;
}

// 中立领地（中枢）的民兵：按 kNeutralGarrison 拆成 枪/盾/骑
Army neutral_garrison() {
    Army army;
    const int spear = kNeutralGarrison / 2;
    const int shield = kNeutralGarrison / 4;
    const int rider = kNeutralGarrison - spear - shield;
    army.add(UnitType::Spear, spear).add(UnitType::Shield, shield).add(UnitType::Rider, rider);
    return army;
}

struct PendingMove {
    PlayerId player = kNeutralOwner;
    RegionId from = -1;
    RegionId to = -1;
    int count = 0;
};

struct PendingAttack {
    PlayerId player = kNeutralOwner;
    RegionId from = -1;
    RegionId to = -1;
    int count = 0;
};

void emit_losses(Event& event, PlayerId who, RegionId region, const Army& losses) {
    event.type = EventType::Casualties;
    event.actor = who;
    event.a = region;
    event.b = losses.of(UnitType::Spear);
    event.c = losses.of(UnitType::Shield);
    event.d = losses.of(UnitType::Rider);
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
        // 先给一份 AP：否则 AI/客户端在"第 1 回合提交前"读到 ap == 0，会白白空过一回合
        player.ap = state_.cfg.ap_per_turn;
        player.alive = true;
        player.submitted = false;
        player.tactic_card = -1;
        player.tactic_target = -1;

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

    // 剩下的区域放中立民兵（5 块地 4 人局时只剩中枢）
    for (RegionId r = 0; r < kRegionCount; ++r) {
        if (state_.regions[static_cast<std::size_t>(r)].owner == kNeutralOwner) {
            state_.regions[static_cast<std::size_t>(r)].garrison = neutral_garrison();
        }
    }

    state_.turn = 0;
    state_.phase = Phase::WaitingForOrders;

    Event begin{};
    begin.type = EventType::GameBegin;
    begin.a = state_.player_count;
    emit(begin);
}

void GameEngine::emit(Event event) { state_.log.push_back(std::move(event)); }

void GameEngine::emit_casualties(PlayerId who, RegionId region, const Army& losses) {
    if (losses.empty()) {
        return;
    }
    Event event{};
    emit_losses(event, who, region, losses);
    emit(event);
}

void GameEngine::reject(PlayerId player, RejectReason reason, OrderType type) {
    Event event{};
    event.type = EventType::OrderRejected;
    event.actor = player;
    event.a = static_cast<int>(reason);
    event.b = static_cast<int>(type);
    emit(event);
}

void GameEngine::shuffle_deck(std::vector<int>& cards) {
    for (std::size_t i = cards.size(); i > 1; --i) {
        const int j = rng_.next_int(0, static_cast<int>(i) - 1);
        std::swap(cards[i - 1], cards[static_cast<std::size_t>(j)]);
    }
}

void GameEngine::draw_card(Player& player) {
    if (player.deck.hand_size() >= kHandLimit) {
        return;
    }
    if (player.deck.draw_pile.empty()) {
        if (player.deck.discard.empty()) {
            return;  // 牌全在手上，抽不到
        }
        player.deck.draw_pile = player.deck.discard;
        player.deck.discard.clear();
        shuffle_deck(player.deck.draw_pile);
    }
    const int card = player.deck.draw_pile.back();
    player.deck.draw_pile.pop_back();
    player.deck.hand.push_back(card);

    Event event{};
    event.type = EventType::CardDrawn;
    event.actor = player.id;
    event.a = card;
    event.b = player.deck.hand_size();
    emit(event);
}

void GameEngine::discard_card(Player& player, int card_id) {
    auto& hand = player.deck.hand;
    const auto it = std::find(hand.begin(), hand.end(), card_id);
    if (it != hand.end()) {
        hand.erase(it);
        player.deck.discard.push_back(card_id);
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

bool GameEngine::owns(PlayerId player, RegionId region) const {
    if (region < 0 || region >= kRegionCount || player == kNeutralOwner) {
        return false;
    }
    return state_.regions[static_cast<std::size_t>(region)].owner == player;
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
    state_.recruited_this_turn.fill(0);

    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        p.tactic_card = -1;
        p.tactic_target = -1;
        if (!p.alive) {
            continue;
        }
        if (!p.submitted) {
            // 服务端不会为没交指令的人等下去：按超时托管处理，并留下痕迹。
            reject(i, RejectReason::Timeout, OrderType::Pass);
            p.pending_orders.clear();
            p.submitted = true;
        }
        // 注意：这里不重置 AP。行动点是"为下回合准备"的 —— 见 end_turn()。
        for (int d = 0; d < kDrawPerTurn; ++d) {
            draw_card(p);
        }

        Event accepted{};
        accepted.type = EventType::OrdersAccepted;
        accepted.actor = i;
        accepted.a = static_cast<int>(p.pending_orders.size());
        emit(accepted);
    }
}

void GameEngine::phase_produce_resources() {
    // 按区域 id 升序产出，保证事件顺序固定
    for (RegionId r = 0; r < kRegionCount; ++r) {
        const Region& region = state_.regions[static_cast<std::size_t>(r)];
        if (region.owner == kNeutralOwner || region.owner >= state_.player_count) {
            continue;
        }
        const int gold = region_spec(r).gold_yield + region.building_level;
        if (gold <= 0) {
            continue;
        }
        state_.players[static_cast<std::size_t>(region.owner)].gold += gold;

        Event event{};
        event.type = EventType::ResourceProduced;
        event.actor = region.owner;
        event.a = gold;
        emit(event);
    }
}

void GameEngine::commit_tactic(Player& player, const Command& cmd) {
    if (cmd.card < 0 || cmd.card >= kCardCatalogSize) {
        reject(player.id, RejectReason::IllegalTarget, cmd.type);
        return;
    }
    if (cmd.to < 0 || cmd.to >= kRegionCount) {
        reject(player.id, RejectReason::IllegalTarget, cmd.type);
        return;
    }
    if (card_def(cmd.card).kind != CardKind::Tactic) {
        reject(player.id, RejectReason::IllegalTarget, cmd.type);
        return;
    }
    const auto& hand = player.deck.hand;
    if (std::find(hand.begin(), hand.end(), cmd.card) == hand.end()) {
        reject(player.id, RejectReason::IllegalTarget, cmd.type);
        return;
    }
    if (player.tactic_card >= 0) {
        // 一回合只能暗置一张
        reject(player.id, RejectReason::IllegalTarget, cmd.type);
        return;
    }
    player.tactic_card = cmd.card;
    player.tactic_target = cmd.to;
}

int GameEngine::consume_tactic(PlayerId player, RegionId battle_region) {
    if (player == kNeutralOwner || player < 0 || player >= state_.player_count) {
        return 0;
    }
    Player& p = state_.players[static_cast<std::size_t>(player)];
    if (p.tactic_card < 0 || p.tactic_target != battle_region) {
        return 0;
    }
    const int card = p.tactic_card;
    p.tactic_card = -1;
    p.tactic_target = -1;
    discard_card(p, card);

    Event event{};
    event.type = EventType::TacticRevealed;
    event.actor = player;
    event.a = card;
    event.b = battle_region;
    event.c = card_def(card).power;
    emit(event);
    return event.c;
}

void GameEngine::execute_build(Player& player, const Command& cmd) {
    if (!owns(player.id, cmd.from)) {
        reject(player.id, RejectReason::IllegalSource, cmd.type);
        return;
    }
    Region& region = state_.regions[static_cast<std::size_t>(cmd.from)];
    if (region.building_level >= kBuildMaxLevel) {
        reject(player.id, RejectReason::BuildingMaxed, cmd.type);
        return;
    }
    if (player.gold < kBuildCost) {
        reject(player.id, RejectReason::NotEnoughGold, cmd.type);
        return;
    }
    if (player.ap <= 0) {
        reject(player.id, RejectReason::NotEnoughAp, cmd.type);
        return;
    }
    player.ap -= 1;
    player.gold -= kBuildCost;
    ++region.building_level;

    Event event{};
    event.type = EventType::BuildingCompleted;
    event.actor = player.id;
    event.a = cmd.from;
    event.b = region.building_level;
    emit(event);
}

void GameEngine::execute_recruit(Player& player, const Command& cmd) {
    if (!owns(player.id, cmd.from)) {
        reject(player.id, RejectReason::IllegalSource, cmd.type);
        return;
    }
    if (cmd.count <= 0) {
        reject(player.id, RejectReason::IllegalTarget, cmd.type);
        return;
    }
    const int cap = region_spec(cmd.from).troop_yield -
                    state_.recruited_this_turn[static_cast<std::size_t>(cmd.from)];
    if (cmd.count > cap) {
        reject(player.id, RejectReason::RecruitLimit, cmd.type);
        return;
    }
    const int cost = cmd.count * kRecruitCostPerUnit;
    if (player.gold < cost) {
        reject(player.id, RejectReason::NotEnoughGold, cmd.type);
        return;
    }
    if (player.ap <= 0) {
        reject(player.id, RejectReason::NotEnoughAp, cmd.type);
        return;
    }
    player.ap -= 1;
    player.gold -= cost;
    Region& region = state_.regions[static_cast<std::size_t>(cmd.from)];
    region.garrison.add(cmd.unit, cmd.count);
    state_.recruited_this_turn[static_cast<std::size_t>(cmd.from)] += cmd.count;

    Event event{};
    event.type = EventType::UnitsRecruited;
    event.actor = player.id;
    event.a = cmd.from;
    event.b = (cmd.unit == UnitType::Spear) ? cmd.count : 0;
    event.c = (cmd.unit == UnitType::Shield) ? cmd.count : 0;
    event.d = (cmd.unit == UnitType::Rider) ? cmd.count : 0;
    emit(event);
}

void GameEngine::execute_move(PlayerId player, RegionId from, RegionId to, int count) {
    if (player < 0 || player >= state_.player_count) {
        return;
    }
    Player& p = state_.players[static_cast<std::size_t>(player)];
    if (!p.alive) {
        return;
    }
    if (!owns(player, from) || !owns(player, to)) {
        reject(player, RejectReason::IllegalTarget, OrderType::Move);
        return;
    }
    if (!adjacent(from, to)) {
        reject(player, RejectReason::NotAdjacent, OrderType::Move);
        return;
    }
    Region& src = state_.regions[static_cast<std::size_t>(from)];
    if (count <= 0 || src.garrison.total() < count) {
        reject(player, RejectReason::NotEnoughTroops, OrderType::Move);
        return;
    }
    if (p.ap <= 0) {
        reject(player, RejectReason::NotEnoughAp, OrderType::Move);
        return;
    }
    p.ap -= 1;
    const Army moved = src.garrison.take(count);
    state_.regions[static_cast<std::size_t>(to)].garrison += moved;

    Event event{};
    event.type = EventType::ArmyMoved;
    event.actor = player;
    event.a = from;
    event.b = to;
    event.c = moved.of(UnitType::Spear);
    event.d = moved.of(UnitType::Shield);
    event.e = moved.of(UnitType::Rider);
    emit(event);
}

void GameEngine::execute_attack(PlayerId player, RegionId from, RegionId to, int count) {
    if (player < 0 || player >= state_.player_count) {
        return;
    }
    Player& p = state_.players[static_cast<std::size_t>(player)];
    if (!p.alive) {
        return;
    }
    if (!owns(player, from)) {
        reject(player, RejectReason::IllegalSource, OrderType::Attack);
        return;
    }
    if (owns(player, to)) {
        // 自己的区域用调动，不用进攻
        reject(player, RejectReason::IllegalTarget, OrderType::Attack);
        return;
    }
    if (!adjacent(from, to)) {
        reject(player, RejectReason::NotAdjacent, OrderType::Attack);
        return;
    }
    Region& src = state_.regions[static_cast<std::size_t>(from)];
    if (count <= 0 || src.garrison.total() < count) {
        reject(player, RejectReason::NotEnoughTroops, OrderType::Attack);
        return;
    }
    if (p.ap <= 0) {
        reject(player, RejectReason::NotEnoughAp, OrderType::Attack);
        return;
    }

    p.ap -= 1;
    Region& target = state_.regions[static_cast<std::size_t>(to)];
    const PlayerId defender = target.owner;
    const Army committed = src.garrison.take(count);

    const int attacker_bonus = consume_tactic(player, to);
    const int defender_bonus = consume_tactic(defender, to);

    const BattleResult result = resolve_battle(committed, target.garrison, attacker_bonus,
                                               defender_bonus);

    Event battle{};
    battle.type = EventType::BattleResolved;
    battle.actor = player;  // 谁发起的进攻
    battle.a = to;
    battle.b = player;
    battle.c = defender;
    battle.d = result.attacker_power;
    battle.e = result.defender_power;
    battle.f = result.attacker_won ? 1 : 0;
    emit(battle);

    emit_casualties(player, to, result.attacker_losses);
    emit_casualties(defender, to, result.defender_losses);

    target.garrison -= result.defender_losses;

    if (result.attacker_won) {
        Army survivors = committed;
        survivors -= result.attacker_losses;
        const PlayerId previous = target.owner;
        target.owner = player;
        target.garrison = survivors;

        Event captured{};
        captured.type = EventType::RegionCaptured;
        captured.actor = player;
        captured.a = to;
        captured.b = previous;
        emit(captured);
    }
}

void GameEngine::phase_execute_orders() {
    std::vector<PendingMove> moves;
    std::vector<PendingAttack> attacks;

    // 1) 发育类（建造/招募/暗置战术）：只影响自己的资源，按玩家、指令顺序执行
    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        if (!p.alive) {
            continue;
        }
        for (const Command& cmd : p.pending_orders) {
            switch (cmd.type) {
                case OrderType::Pass:
                    break;
                case OrderType::Build:
                    execute_build(p, cmd);
                    break;
                case OrderType::Recruit:
                    execute_recruit(p, cmd);
                    break;
                case OrderType::PlayTactic:
                    commit_tactic(p, cmd);
                    break;
                case OrderType::Move:
                    moves.push_back({p.id, cmd.from, cmd.to, cmd.count});
                    break;
                case OrderType::Attack:
                    attacks.push_back({p.id, cmd.from, cmd.to, cmd.count});
                    break;
            }
        }
    }

    // 2) 调动先于进攻。排序固定（起点区域 → 玩家 id），保证同一份输入产出同一结果。
    std::stable_sort(moves.begin(), moves.end(), [](const PendingMove& lhs, const PendingMove& rhs) {
        if (lhs.from != rhs.from) {
            return lhs.from < rhs.from;
        }
        return lhs.player < rhs.player;
    });
    for (const PendingMove& move : moves) {
        execute_move(move.player, move.from, move.to, move.count);
    }

    // 3) 进攻。同一目标区域按"轮转顺序"结算（每回合换一个先手），
    //    否则固定按玩家 id 升序 = 后手总能捡残兵，这是系统性偏差。
    const int players = state_.player_count;
    const int offset = (players > 0) ? (state_.turn % players) : 0;
    std::stable_sort(attacks.begin(), attacks.end(),
                     [players, offset](const PendingAttack& lhs, const PendingAttack& rhs) {
                         if (lhs.to != rhs.to) {
                             return lhs.to < rhs.to;
                         }
                         const int rank_l = (lhs.player + players - offset) % players;
                         const int rank_r = (rhs.player + players - offset) % players;
                         return rank_l < rank_r;
                     });
    for (const PendingAttack& attack : attacks) {
        execute_attack(attack.player, attack.from, attack.to, attack.count);
    }
}

void GameEngine::phase_check_victory() {
    std::array<int, kMaxPlayers> owned{};
    std::array<int, kMaxPlayers> troops{};
    for (const Region& region : state_.regions) {
        if (region.owner != kNeutralOwner && region.owner < state_.player_count) {
            owned[static_cast<std::size_t>(region.owner)] += 1;
            troops[static_cast<std::size_t>(region.owner)] += region.garrison.total();
        }
    }

    // 淘汰：既无领地也无兵力（兵力只存在于领地上，所以基本等于无领地）
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
            const int current = score(i);
            if (current > best_score) {
                best_score = current;
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
    // 没用掉的战术卡作废（进弃牌堆、不揭晓）：暗置猜错目标就是白扣一张牌
    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        if (p.tactic_card >= 0) {
            const int card = p.tactic_card;
            p.tactic_card = -1;
            p.tactic_target = -1;
            discard_card(p, card);
        }
    }

    Event event{};
    event.type = EventType::TurnEnd;
    event.a = state_.turn;
    emit(event);

    for (int i = 0; i < state_.player_count; ++i) {
        Player& p = state_.players[static_cast<std::size_t>(i)];
        p.pending_orders.clear();
        p.submitted = false;
        // 行动点在这里发放：结算结束时，玩家拿到的就是"下一回合可用"的 AP。
        // 如果放在 begin_turn（结算开头）发，玩家在提交指令时看到的是上一回合剩下的 0 点，
        // 界面会误导人，AI 也会隔一回合不动一次（真实踩过的坑）。
        if (p.alive) {
            p.ap = state_.cfg.ap_per_turn;
        }
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

    // 结算顺序固定：产出 → 指令 → 胜负。
    // 顺序一旦变化，同一份输入就会产出不同结果，确定性就没了。
    phase_produce_resources();
    phase_execute_orders();
    end_turn();
    phase_check_victory();

    if (state_.phase != Phase::Finished) {
        state_.phase = Phase::WaitingForOrders;
    }

    return {state_.log.begin() + static_cast<std::ptrdiff_t>(mark), state_.log.end()};
}

}  // namespace kingdom_fight
