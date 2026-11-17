// 一次性探针：量单位在 1 秒（20 tick）里到底走了多少像素。
// 如果数值接近 1000+，说明 speed 被当成"每 tick 位移"，比设计的 px/s 快了 20 倍。
#include <cstdio>
#include <vector>

#include "rts/sim.h"

using namespace kingdom_fight::rts;

int main() {
    std::array<std::vector<int>, kPlayers> decks;
    const auto& pool = card_pool(Alliance::Vanguard);
    for (auto& deck : decks) deck = std::vector<int>(pool.begin(), pool.begin() + kDeckSize);

    std::array<Alliance, kPlayers> factions{};
    for (auto& alliance : factions) alliance = Alliance::Vanguard;

    Sim sim;
    if (!sim.start(decks, MatchConfig{}, factions)) {
        std::printf("start failed\n");
        return 1;
    }

    const Point base = sim.base_position(0);
    if (sim.play_card(0, 0, base.x + 150, base.y + 150) != PlayResult::Ok) {
        std::printf("play failed\n");
        return 1;
    }
    if (sim.player(0).units.empty()) {
        std::printf("no units\n");
        return 1;
    }

    const Unit before = sim.player(0).units.front();
    sim.advance();  // 只走 1 tick
    const Unit after = sim.player(0).units.front();
    const int dx = after.x - before.x;
    const int dy = after.y - before.y;
    const int per_tick = (dx * dx + dy * dy) > 0 ? 1 : 0;
    std::printf("单位类型=%s  设计 speed=%d\n", to_string(before.type), unit_stat(before.type).speed);
    std::printf("1 tick 位移: dx=%d dy=%d（每 tick 实际移动 %d 像素）\n", dx, dy, per_tick);

    // 再跑一秒，看总的
    const int start_x = after.x;
    const int start_y = after.y;
    for (int i = 0; i < 20; ++i) sim.advance();
    const Unit later = sim.player(0).units.front();
    const int total_dx = later.x - start_x;
    const int total_dy = later.y - start_y;
    std::printf("1 秒（20 tick）位移: dx=%d dy=%d\n", total_dx, total_dy);
    std::printf("换算成速度 ≈ %d 像素/秒（表里写的是 %d）\n", std::abs(total_dx) + std::abs(total_dy),
                unit_stat(before.type).speed);
    return 0;
}
