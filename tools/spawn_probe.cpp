#include <cstdio>
#include "rts/sim.h"
using namespace kingdom_fight::rts;

int main() {
    std::array<std::vector<int>, kPlayers> decks;
    for (auto& d : decks) {
        const auto& pool = card_pool(Alliance::Vanguard);
        d.assign(pool.begin(), pool.begin() + kDeckSize);
    }
    std::array<Alliance, kPlayers> factions{};
    for (auto& f : factions) f = Alliance::Vanguard;
    for (std::uint32_t seed = 3; seed <= 8; ++seed) {
        Sim sim;
        MatchConfig config;
        config.spawn_seed = seed;
        if (!sim.start(decks, config, factions)) { std::printf("seed %u start fail\n", seed); continue; }
        int worst = 1 << 30;
        for (const auto& node : sim.nodes()) {
            for (int p = 0; p < kPlayers; ++p) {
                const Point base = sim.base_position(p);
                const int dx = node.x - base.x;
                const int dy = node.y - base.y;
                const int d2 = dx * dx + dy * dy;
                if (d2 < worst) worst = d2;
            }
        }
        std::printf("seed %u nodes=%zu worst_dist=%d (sqrt~%d)\n", seed, sim.nodes().size(),
                    worst, [](int v) { int r = 0; while ((r + 1) * (r + 1) <= v) ++r; return r; }(worst));
        for (int p = 0; p < kPlayers; ++p) {
            const Point b = sim.base_position(p);
            std::printf("  base%d=(%d,%d)", p, b.x, b.y);
        }
        std::printf("\n");
    }
    return 0;
}
