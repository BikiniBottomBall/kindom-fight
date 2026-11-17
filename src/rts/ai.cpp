// 服务端内置 AI 对手。
//
// 单位已经不由卡牌召唤了：兵营自己按时间训练。所以 AI 只需要做三件事：
//   1. 把经济建筑补齐（物资站 / 能源站 / 兵营 / 研究所）
//   2. 有敌人靠近时丢范围法术
//   3. 总部支援解锁了就去砸对面基地
#include "rts/ai.h"

#include <algorithm>
#include <array>
#include <functional>
#include <vector>

namespace kingdom_fight::rts {
namespace {

constexpr int kThinkIntervalNormal = 20;  // 1.0 秒一次决策
constexpr int kDefendRadius = 700;

struct BuildWant {
    BuildingType type;
    int target_count;
};

// 经济建筑的目标数量：2 物资站 + 1 能源站 + 2 兵营 + 1 研究所（≤ 6 座）
constexpr std::array<BuildWant, 5> kBuildOrder{{
    {BuildingType::Depot, 2},
    {BuildingType::Reactor, 1},
    {BuildingType::Barracks, 2},
    {BuildingType::Workshop, 1},
    {BuildingType::Lab, 1},
}};

[[nodiscard]] int isqrt(int value) {
    if (value <= 0) return 0;
    int x = value;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + value / x) / 2;
    }
    return x;
}

constexpr std::array<Point, 12> kRingDirs{{
    {1000, 0},   {866, 500},   {500, 866},   {0, 1000},   {-500, 866},  {-866, 500},
    {-1000, 0},  {-866, -500}, {-500, -866}, {0, -1000},  {500, -866},  {866, -500},
}};

}  // namespace

void AiBrain::reset(int player, std::uint32_t seed) {
    player_ = player;
    rng_ = seed == 0 ? 0x9E3779B9u : seed;
    next_think_tick_ = 0;
}

std::uint32_t AiBrain::next_random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void AiBrain::update(Sim& sim) {
    if (sim.phase() != MatchPhase::Playing) return;
    if (player_ < 0 || player_ >= kPlayers) return;
    if (sim.eliminated(player_)) return;
    if (sim.tick() < next_think_tick_) return;
    // 难度只改"决策频率"和"激进度"，不改规则（AI 不作弊）
    int think = kThinkIntervalNormal;
    switch (level_) {
        case AiLevel::Easy:   think = 32; break;   // 1.6 秒
        case AiLevel::Normal: think = 20; break;   // 1.0 秒
        case AiLevel::Hard:   think = 11; break;   // 0.55 秒
    }
    next_think_tick_ = sim.tick() + think;

    const int me = player_;
    const Point base = sim.base_position(me);
    const PlayerState& self = sim.player(me);

    // 附近有城镇据点要打时，兵营改出攻城兵：
    // 普通兵对核心只有 35% 伤害，光靠步兵几乎拆不掉 2400 血的据点。
    if (self.preferred_unit < 0) {
        bool town_near = false;
        for (const auto& node : sim.nodes()) {
            if (node.kind != NodeKind::Town || node.core_hp <= 0) continue;
            for (const auto& unit : self.units) {
                const int dx = unit.x - node.x;
                const int dy = unit.y - node.y;
                if (dx * dx + dy * dy < 2200 * 2200) {
                    town_near = true;
                    break;
                }
            }
            if (town_near) break;
        }
        if (town_near) {
            UnitType siege = UnitType::Ram;
            switch (self.alliance) {
                case Alliance::Combine: siege = UnitType::Catapult; break;
                case Alliance::Order:   siege = UnitType::Ballista; break;
                case Alliance::Swarm:   siege = UnitType::Cannon; break;
                default: break;
            }
            sim.set_preferred_unit(me, static_cast<int>(siege));
        }
    }
    const auto try_play = [&](int slot, int x, int y) {
        return sim.play_card(me, slot, x, y) == PlayResult::Ok;
    };

    // 在手里找第一张满足条件的牌
    const auto find_card = [&](const std::function<bool(const CardDef&)>& want) {
        for (int slot = 0; slot < kHandSize; ++slot) {
            const int id = self.hand(slot);
            if (id < 0) continue;
            if (!sim.can_afford(me, id)) continue;
            if (want(card_def(id))) return slot;
        }
        return -1;
    };

    // ---- 1. 经济：按目标数量补齐建筑 -------------------------------------
    if (self.building_count() < kMaxBuildings) {
        for (const auto& want : kBuildOrder) {
            if (self.count_of(want.type) >= want.target_count) continue;
            const int slot = find_card([&](const CardDef& def) {
                return def.kind == CardKind::Building && def.building == want.type;
            });
            if (slot < 0) continue;
            bool placed = false;
            for (int attempt = 0; attempt < 6 && !placed; ++attempt) {
                const Point dir =
                    kRingDirs[static_cast<std::size_t>(next_random() % kRingDirs.size())];
                const int distance = 190 + static_cast<int>(next_random() % 240);
                placed = try_play(slot, base.x + dir.x * distance / 1000,
                                  base.y + dir.y * distance / 1000);
            }
            if (placed) return;
        }
    }

    // ---- 1.2 局内成长树：按固定偏好顺序，买得起且前置满足就点亮 ----
    {
        const int order[kTreeNodeCount] = {0, 4, 8, 1, 5, 9, 6, 10, 2, 3, 7, 11};
        for (int i = 0; i < kTreeNodeCount; ++i) {
            const int id = order[i];
            if (!sim.tree_available(me, id)) continue;
            if (self.res.tech < sim.upgrade_cost_for(me, id)) continue;
            if (sim.upgrade(me, id) == PlayResult::Ok) return;
        }
    }

    // ---- 1.5 防御塔：自家领地上补两座（塔只能盖在领地+空地上，服务器会校验）----
    if (level_ != AiLevel::Easy && static_cast<int>(self.towers.size()) <
        kTowersPerPlayer + (level_ == AiLevel::Hard ? 3 : 2)) {
        const int tower_slot = find_card([](const CardDef& def) {
            return def.kind == CardKind::Building && def.building == BuildingType::Tower;
        });
        if (tower_slot >= 0) {
            for (int attempt = 0; attempt < 10; ++attempt) {
                const Point dir =
                    kRingDirs[static_cast<std::size_t>(next_random() % kRingDirs.size())];
                const int distance = 100 + static_cast<int>(next_random() % 120);
                if (try_play(tower_slot, base.x + dir.x * distance / 1000,
                             base.y + dir.y * distance / 1000)) {
                    return;
                }
            }
        }
    }

    // ---- 2. 防守：敌人靠近就丢范围法术 -----------------------------------
    std::vector<std::array<int, 2>> threats;
    for (int e = 0; e < kPlayers; ++e) {
        if (e == me) continue;
        for (const auto& unit : sim.player(e).units) {
            if (unit.hp <= 0) continue;
            const int dx = unit.x - base.x;
            const int dy = unit.y - base.y;
            if (dx * dx + dy * dy > kDefendRadius * kDefendRadius) continue;
            threats.push_back({unit.x, unit.y});
        }
    }

    if (threats.size() >= 2) {
        int sum_x = 0;
        int sum_y = 0;
        for (const auto& threat : threats) {
            sum_x += threat[0];
            sum_y += threat[1];
        }
        const int avg_x = sum_x / static_cast<int>(threats.size());
        const int avg_y = sum_y / static_cast<int>(threats.size());
        const int spell_slot = find_card([](const CardDef& def) {
            return def.kind == CardKind::Spell &&
                   (def.spell == SpellKind::Fire || def.spell == SpellKind::Frost);
        });
        if (spell_slot >= 0 && try_play(spell_slot, avg_x, avg_y)) return;
    }

    // ---- 3. 总部支援：解锁了就砸对面基地 ---------------------------------
    if (sim.hq_state(me) == 1) {
        int victim = -1;
        int best = 0x7FFFFFFF;
        for (int e = 0; e < kPlayers; ++e) {
            if (e == me || sim.eliminated(e)) continue;
            const Point enemy_base = sim.base_position(e);
            const int d = isqrt((enemy_base.x - base.x) * (enemy_base.x - base.x) +
                                (enemy_base.y - base.y) * (enemy_base.y - base.y));
            if (d < best) {
                best = d;
                victim = e;
            }
        }
        if (victim >= 0) {
            const Point enemy_base = sim.base_position(victim);
            if (sim.call_hq(me, enemy_base.x, enemy_base.y) == PlayResult::Ok) return;
        }
    }

    // ---- 4. 给自己的部队加个 buff ----------------------------------------
    const int buff_slot = find_card([](const CardDef& def) {
        return def.kind == CardKind::Spell && def.spell == SpellKind::Rally;
    });
    if (buff_slot >= 0 && !self.units.empty() && (next_random() % 100) < 35) {
        const Unit& unit = self.units[next_random() % self.units.size()];
        if (try_play(buff_slot, unit.x, unit.y)) return;
    }
}

}  // namespace kingdom_fight::rts
