#include <cstdio>
#include <vector>
#include "rts/sim.h"
using namespace kingdom_fight::rts;

int main() {
    std::array<std::vector<int>, kPlayers> decks;
    const auto& pool = card_pool(Alliance::Combine);
    for (auto& deck : decks) deck = std::vector<int>(pool.begin(), pool.begin() + kDeckSize);
    std::array<Alliance, kPlayers> factions{};
    for (auto& a : factions) a = Alliance::Combine;
    Sim sim;
    if (!sim.start(decks, MatchConfig{}, factions)) { std::printf("start failed\n"); return 1; }

    // 把手牌第一张换成兵营
    const int barracks = card_pool(Alliance::Combine)[2];
    auto& st = const_cast<PlayerState&>(sim.player(0));
    st.rotation[0] = barracks;

    const Point base = sim.base_position(0);
    const PlayResult r = sim.play_card(0, 0, base.x + 100, base.y + 60);
    std::printf("play=%d buildings=%zu\n", static_cast<int>(r), sim.player(0).buildings.size());
    for (int t = 0; t <= 900; ++t) {
        if (t % 120 == 0) {
            const auto& b = sim.player(0).buildings;
            std::printf("t=%4d ready=%d build=%d/%d units=%zu res(mp=%d mat=%d)\n", t,
                        b.empty() ? -1 : (b.front().ready ? 1 : 0),
                        b.empty() ? 0 : b.front().build_ticks,
                        b.empty() ? 0 : b.front().build_total,
                        sim.player(0).units.size(), sim.resources(0).manpower,
                        sim.resources(0).materiel);
        }
        sim.advance();
    }
    return 0;
}
