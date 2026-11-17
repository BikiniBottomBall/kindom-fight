#include <cstdio>
#include "rts/ai.h"
#include "rts/sim.h"
using namespace kingdom_fight::rts;

int main() {
    std::array<std::vector<int>, kPlayers> decks;
    for (auto& d : decks) {
        const auto& pool = card_pool(Alliance::Combine);
        d.assign(pool.begin(), pool.begin() + kDeckSize);
    }
    Sim sim;
    MatchConfig config;
    config.spawn_seed = 11;
    std::array<Alliance, kPlayers> factions{};
    for (auto& f : factions) f = Alliance::Combine;
    if (!sim.start(decks, config, factions)) { std::printf("start failed\n"); return 1; }
    std::array<AiBrain, kPlayers> brains;
    for (int p = 0; p < kPlayers; ++p) brains[p].reset(p, 70 + p);
    for (int t = 0; t < 6000 && sim.phase() == MatchPhase::Playing; ++t) {
        for (auto& b : brains) b.update(sim);
        sim.advance();
        if (t % 1500 == 0) {
            std::printf("t=%d ", t);
            for (int p = 0; p < kPlayers; ++p) {
                const auto& st = sim.player(p);
                std::printf("[p%d u=%zu b=%zu tow=%zu res=%d/%d/%d] ", p, st.units.size(),
                            st.buildings.size(), st.towers.size(), st.res.manpower,
                            st.res.materiel, st.res.energy);
            }
            int owned = 0;
            for (const auto& n : sim.nodes()) if (n.owner >= 0) ++owned;
            std::printf("owned=%d\n", owned);
        }
    }
    for (const auto& n : sim.nodes()) {
        std::printf("node kind=%d cap=%d prog=%d owner=%d\n", int(n.kind), n.capturer, n.progress, n.owner);
    }
    int owned = 0;
    for (const auto& n : sim.nodes()) if (n.owner >= 0) ++owned;
    std::printf("mask size=%zu\n", sim.territory_mask(0).size());
    std::printf("final owned=%d nodes=%zu\n", owned, sim.nodes().size());
    for (const auto& n : sim.nodes()) {
        std::printf("  kind=%d owner=%d r=%d core=%d/%d\n", int(n.kind), n.owner, n.radius, n.core_hp, n.core_max);
    }
    return 0;
}
