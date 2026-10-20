#include "ai/simple_ai.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "common/config.h"
#include "logic/card_def.h"
#include "logic/combat.h"
#include "logic/region.h"

namespace kingdom_fight::ai {
namespace {

struct AttackCandidate {
    bool valid = false;
    RegionId from = -1;
    RegionId to = -1;
    int troops = 0;
};

// 找一块"打得下且损失不过半"的相邻敌区；多目标时按收益打分。
AttackCandidate pick_attack(const GameState& state, PlayerId self) {
    AttackCandidate best;
    int best_value = -1;

    for (RegionId from = 0; from < kRegionCount; ++from) {
        const Region& src = state.regions[static_cast<std::size_t>(from)];
        if (src.owner != self || src.garrison.total() < 2) {
            continue;
        }
        for (const RegionId to : neighbors(from)) {
            const Region& dst = state.regions[static_cast<std::size_t>(to)];
            if (dst.owner == self) {
                continue;
            }
            // 家里至少留 1 兵：全压上去会导致老家空城被邻居白捡（第一版 AI 就是这么崩的）
            const int commit = src.garrison.total() - 1;
            Army force = src.garrison;
            const Army committed = force.take(commit);

            // 战斗是确定性公式，AI 可以精确预判 —— 这也是"去骰子"的附带收益
            const BattleResult result = resolve_battle(committed, dst.garrison);
            if (!result.attacker_won) {
                continue;
            }
            if (result.attacker_losses.total() * 2 > committed.total()) {
                continue;  // 损失过半，不值得
            }
            const RegionSpec& spec = region_spec(to);
            const int value = spec.gold_yield * 10 + spec.troop_yield * 5 -
                              result.attacker_losses.total() * 3;
            if (value > best_value) {
                best_value = value;
                best.valid = true;
                best.from = from;
                best.to = to;
                best.troops = commit;
            }
        }
    }
    return best;
}

}  // namespace

std::vector<Command> make_orders(const GameEngine& engine, PlayerId self) {
    const GameState& state = engine.state();
    if (self < 0 || self >= state.player_count) {
        return {};
    }
    const Player& me = state.players[static_cast<std::size_t>(self)];
    std::vector<Command> orders;
    if (!me.alive || me.ap <= 0) {
        return orders;
    }

    // 1) 有稳赚的仗就打；顺手暗置一张战术卡（不花 AP）
    const AttackCandidate attack = pick_attack(state, self);
    if (attack.valid) {
        for (const int card : me.deck.hand) {
            if (card_def(card).kind == CardKind::Tactic) {
                Command tactic{};
                tactic.type = OrderType::PlayTactic;
                tactic.card = card;
                tactic.to = attack.to;
                orders.push_back(tactic);
                break;
            }
        }
        Command cmd{};
        cmd.type = OrderType::Attack;
        cmd.from = attack.from;
        cmd.to = attack.to;
        cmd.count = attack.troops;
        orders.push_back(cmd);
        return orders;
    }

    // 2) 能造就造（前期投资，长期收益）
    if (me.gold >= kBuildCost) {
        for (RegionId r = 0; r < kRegionCount; ++r) {
            const Region& region = state.regions[static_cast<std::size_t>(r)];
            if (region.owner != self || region.building_level >= kBuildMaxLevel) {
                continue;
            }
            Command cmd{};
            cmd.type = OrderType::Build;
            cmd.from = r;
            orders.push_back(cmd);
            return orders;
        }
    }

    // 3) 招兵（挑本回合还能招的、上限最高的地区）
    if (me.gold >= kRecruitCostPerUnit) {
        RegionId pick = -1;
        int pick_cap = 0;
        for (RegionId r = 0; r < kRegionCount; ++r) {
            if (state.regions[static_cast<std::size_t>(r)].owner != self) {
                continue;
            }
            const int cap = region_spec(r).troop_yield -
                            state.recruited_this_turn[static_cast<std::size_t>(r)];
            if (cap > pick_cap) {
                pick_cap = cap;
                pick = r;
            }
        }
        if (pick >= 0) {
            const int affordable = me.gold / kRecruitCostPerUnit;
            Command cmd{};
            cmd.type = OrderType::Recruit;
            cmd.from = pick;
            cmd.unit = UnitType::Spear;
            cmd.count = std::min(pick_cap, affordable);
            orders.push_back(cmd);
            return orders;
        }
    }

    return orders;  // 空 = 本回合略过
}

}  // namespace kingdom_fight::ai
