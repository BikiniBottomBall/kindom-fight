#include <cstdio>
#include "rts/terrain.h"
using namespace kingdom_fight::rts;

int main() {
    for (int variant = 0; variant < kTerrainVariants; ++variant) {
        for (int s = 1; s <= 6; ++s) {
            Terrain terrain(variant, static_cast<std::uint32_t>(s) * 7717u);
            int counts[6] = {0, 0, 0, 0, 0, 0};
            for (auto value : terrain.raw()) counts[value]++;
            const int total = kCols * kRows;
            Point spawns[kPlayers] = {};
            const bool ok = Terrain::pick_spawns(static_cast<std::uint32_t>(s) * 7717u ^ 0x51u, terrain, spawns);
            std::printf("v%d s%d  plain=%2d%% forest=%2d%% hill=%2d%% water=%2d%% rock=%2d%%  spawn=%s\n",
                        variant, s, counts[0] * 100 / total, counts[2] * 100 / total,
                        counts[3] * 100 / total, counts[4] * 100 / total,
                        counts[5] * 100 / total, ok ? "ok" : "FAIL");
        }
    }
    return 0;
}
