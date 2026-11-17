#include <cstdio>
#include <vector>
#include "rts/ai.h"
#include "rts/sim.h"
using namespace kingdom_fight::rts;

int main() {
    std::array<std::vector<int>, kPlayers> decks;
    const auto& pool = card_pool(Alliance::Combine);
    for (auto& deck : decks) deck = std::vector<int>(pool.begin(), pool.begin() + kDeckSize);
    std::array<Alliance, kPlayers> factions{};
    for (auto& alliance : factions) alliance = Alliance::Combine;
    MatchConfig config;
    config.match_seconds = 400;
    Sim sim;
    if (!sim.start(decks, config, factions)) return 1;
    std::array<AiBrain, kPlayers> brains;
    for (int p = 0; p < kPlayers; ++p) brains[p].reset(p, 900 + p);
    for (int tick = 0; tick < 2400; ++tick) {
        for (auto& brain : brains) brain.update(sim);
        sim.advance();
    }
    // 打印每个资源点：位置、是否激活、占用进度、离最近的兵多远
    for (std::size_t i = 0; i < sim.nodes().size(); ++i) {
        const auto& node = sim.nodes()[i];
        int best = 1 << 30;
        int best_player = -1;
        for (int p = 0; p < kPlayers; ++p) {
            for (const auto& unit : sim.player(p).units) {
                const int dx = unit.x - node.x;
                const int dy = unit.y - node.y;
                const int d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; best_player = p; }
            }
        }
        std::printf("node%2zu kind=%d active=%d owner=%d progress=%d nearest=%dpx(p%d)\n",
            i, static_cast<int>(node.kind), node.active ? 1 : 0, node.owner,
            node.progress, best_player < 0 ? -1 : [&] { int v = 0; while ((v+1)*(v+1) <= best) ++v; return v; }(), best_player);
    }
    return 0;
}
