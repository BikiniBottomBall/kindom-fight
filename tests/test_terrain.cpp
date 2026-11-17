// 地形与资源点单测：自然生成、随机降落点、连通性、资源点约束。
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <vector>
#include <cstdlib>
#include <string>

#include "rts/ai.h"
#include "rts/sim.h"
#include "rts/terrain.h"
#include "rts/types.h"
#include "test_framework.h"

using namespace kingdom_fight::rts;

namespace {

int count_tiles(const Terrain& terrain, Tile wanted) {
    int count = 0;
    for (const auto tile : terrain.raw()) {
        if (static_cast<Tile>(tile) == wanted) ++count;
    }
    return count;
}

std::array<std::vector<int>, kPlayers> make_decks() {
    const auto& pool = card_pool(Alliance::Combine);
    std::array<std::vector<int>, kPlayers> decks;
    for (auto& deck : decks) deck = std::vector<int>(pool.begin(), pool.begin() + kDeckSize);
    return decks;
}

}  // namespace

TEST(SecretForestIsForestHeavyButHasBuildableClearings) {
    // 第一张图「秘密森林」：森林要占多数，但必须留出成片空地给塔和建筑
    for (std::uint32_t seed = 1; seed <= 12; ++seed) {
        Terrain terrain(0, seed * 7919u);
        const int forest = count_tiles(terrain, Tile::Forest);
        const int buildable = count_tiles(terrain, Tile::Plain) + count_tiles(terrain, Tile::Road);
        const int total = kCols * kRows;
        CHECK(forest * 100 / total >= 25);      // 森林为主
        CHECK(buildable * 100 / total >= 30);   // 但空地也得够多，不然没地方放塔
    }
}

TEST(TerrainIsMadeOfRegionsNotSpeckles) {
    // 地形应该是"成片"的：孤立单格（四邻都不同）占比要很低，否则看起来像噪点
    for (int variant = 0; variant < kTerrainVariants; ++variant) {
        for (std::uint32_t seed = 1; seed <= 6; ++seed) {
            Terrain terrain(variant, seed * 7717u);
            int isolated = 0;
            int total = (kCols - 2) * (kRows - 2);
            for (int r = 1; r < kRows - 1; ++r) {
                for (int c = 1; c < kCols - 1; ++c) {
                    const Tile self = terrain.cell(c, r);
                    if (terrain.cell(c - 1, r) != self && terrain.cell(c + 1, r) != self &&
                        terrain.cell(c, r - 1) != self && terrain.cell(c, r + 1) != self) {
                        ++isolated;
                    }
                }
            }
            // 允许零星边界碎片，但绝不允许"麻点地图"
            CHECK(isolated * 1000 / total <= 20);  // ≤ 2%
        }
    }
}

TEST(ForestComesInLargeClumps) {
    // 森林应该成团：平均每块连通区域至少十几格
    for (std::uint32_t seed = 1; seed <= 4; ++seed) {
        Terrain terrain(0, seed * 9973u);
        std::vector<char> seen(static_cast<std::size_t>(kCols * kRows), 0);
        int forest_tiles = 0;
        int forest_regions = 0;
        for (int r = 0; r < kRows; ++r) {
            for (int c = 0; c < kCols; ++c) {
                if (terrain.cell(c, r) != Tile::Forest) continue;
                ++forest_tiles;
                const auto index = static_cast<std::size_t>(r * kCols + c);
                if (seen[index] != 0) continue;
                ++forest_regions;
                std::deque<std::array<int, 2>> queue;
                queue.push_back({c, r});
                seen[index] = 1;
                while (!queue.empty()) {
                    const auto cur = queue.front();
                    queue.pop_front();
                    const int dc[4] = {1, -1, 0, 0};
                    const int dr[4] = {0, 0, 1, -1};
                    for (int i = 0; i < 4; ++i) {
                        const int nc = cur[0] + dc[i];
                        const int nr = cur[1] + dr[i];
                        if (nc < 0 || nc >= kCols || nr < 0 || nr >= kRows) continue;
                        const auto ni = static_cast<std::size_t>(nr * kCols + nc);
                        if (seen[ni] != 0) continue;
                        if (terrain.cell(nc, nr) != Tile::Forest) continue;
                        seen[ni] = 1;
                        queue.push_back({nc, nr});
                    }
                }
            }
        }
        CHECK(forest_tiles > 0);
        CHECK(forest_regions > 0);
        // 平均每块 ≥ 12 格
        CHECK(forest_tiles / forest_regions >= 12);
    }
}
TEST(EveryVariantAndSeedKeepsAllBasesConnectedAndBuildable) {
    // 多跑几轮：3 张图 × 24 个种子，降落点两两可达，且每个基地周围有足够的可建空地
    for (int variant = 0; variant < kTerrainVariants; ++variant) {
        for (std::uint32_t seed = 1; seed <= 24; ++seed) {
            const std::uint32_t map_seed = seed * 2654435761u;
            Terrain terrain(variant, map_seed);

            Point spawns[kPlayers] = {};
            const bool placed = Terrain::pick_spawns(map_seed ^ 0x51u, terrain, spawns);
            CHECK(placed);
            if (!placed) continue;

            for (auto& spawn : spawns) terrain.clear_area(spawn, 20);
            terrain.ensure_connected(spawns, kPlayers);

            for (int i = 0; i < kPlayers; ++i) {
                // 基地周围必须有可建造空地（塔只能盖在空地上）
                CHECK(terrain.buildable_percent(spawns[i].x, spawns[i].y, 10) >= 45);
                for (int j = i + 1; j < kPlayers; ++j) {
                    CHECK(terrain.reaches(spawns[i].x, spawns[i].y, spawns[j].x, spawns[j].y));
                }
            }
        }
    }
}

TEST(MapIsNoLongerMirrored) {    // 自然地形不应该四向对称：至少有一处镜像后不一样
    const Terrain terrain(0, 4242);
    int mismatches = 0;
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            if (terrain.cell(col, row) != terrain.cell(kCols - 1 - col, kRows - 1 - row)) ++mismatches;
        }
    }
    CHECK(mismatches > 0);
}

TEST(MapHasEveryTerrainType) {
    const Terrain terrain(0, 7);
    CHECK(count_tiles(terrain, Tile::Plain) > 0);
    CHECK(count_tiles(terrain, Tile::Forest) > 0);
    CHECK(count_tiles(terrain, Tile::Water) > 0);
    CHECK(count_tiles(terrain, Tile::Rock) > 0);
    CHECK_EQ(static_cast<int>(terrain.raw().size()), kCols * kRows);
}

TEST(MapVariantsDiffer) {
    const Terrain zero(0, 99);
    const Terrain one(1, 99);
    const Terrain two(2, 99);
    CHECK(zero.raw() != one.raw());
    CHECK(one.raw() != two.raw());
    CHECK(zero.raw() != two.raw());
}

TEST(SameSeedProducesSameMap) {
    const Terrain a(1, 12345);
    const Terrain b(1, 12345);
    CHECK(a.raw() == b.raw());
    const Terrain c(1, 54321);
    CHECK(a.raw() != c.raw());
}

TEST(RandomSpawnsAreValidAcrossManySeeds) {
    for (int variant = 0; variant < kTerrainVariants; ++variant) {
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
            Terrain terrain(variant, seed);
            Point spawns[kPlayers] = {};
            CHECK(Terrain::pick_spawns(seed, terrain, spawns));

            for (int p = 0; p < kPlayers; ++p) {
                CHECK(terrain.passable(spawns[p].x, spawns[p].y));
                CHECK(spawns[p].x > 0 && spawns[p].x < kFieldW);
                CHECK(spawns[p].y > 0 && spawns[p].y < kFieldH);
            }
            // 两两不能挤在一起
            for (int a = 0; a < kPlayers; ++a) {
                for (int b = a + 1; b < kPlayers; ++b) {
                    const int dx = spawns[a].x - spawns[b].x;
                    const int dy = spawns[a].y - spawns[b].y;
                    CHECK(dx * dx + dy * dy >= 900 * 900);
                }
            }

            // 清场 + 修路之后必须全连通
            for (int p = 0; p < kPlayers; ++p) terrain.clear_area(spawns[p], 3);
            terrain.ensure_connected(spawns, kPlayers);
            for (int p = 1; p < kPlayers; ++p) {
                CHECK(terrain.reaches(spawns[0].x, spawns[0].y, spawns[p].x, spawns[p].y));
            }
        }
    }
}

TEST(SpawnsAreNotSymmetrical) {
    // 随机点不该像旧阵型那样"中心对称"：把四个点整体转 180° 后通常对不上
    int symmetric = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        Terrain terrain(0, seed);
        Point spawns[kPlayers] = {};
        if (!Terrain::pick_spawns(seed, terrain, spawns)) continue;
        bool mirrored = true;
        for (int p = 0; p < kPlayers; ++p) {
            const int opposite_x = kFieldW - spawns[p].x;
            const int opposite_y = kFieldH - spawns[p].y;
            bool matched = false;
            for (int q = 0; q < kPlayers; ++q) {
                if (std::abs(spawns[q].x - opposite_x) < 120 &&
                    std::abs(spawns[q].y - opposite_y) < 120) {
                    matched = true;
                }
            }
            if (!matched) mirrored = false;
        }
        if (mirrored) ++symmetric;
    }
    CHECK(symmetric <= 2);  // 偶发对称可以接受，但不能是常态
}

TEST(ClearAreaRemovesBlockersOnly) {
    Terrain terrain(1, 555);
    // 找一块水，清掉它
    int water_x = -1;
    int water_y = -1;
    for (int row = 0; row < kRows && water_x < 0; ++row) {
        for (int col = 0; col < kCols; ++col) {
            if (terrain.cell(col, row) == Tile::Water) {
                water_x = col * kTileSize + 10;
                water_y = row * kTileSize + 10;
                break;
            }
        }
    }
    CHECK(water_x >= 0);
    terrain.clear_area(Point{water_x, water_y}, 1);
    CHECK(terrain.passable(water_x, water_y));
}

TEST(OutOfBoundsTilesAreSolid) {
    const Terrain terrain;
    CHECK(!terrain.passable(-10, -10));
    CHECK(!terrain.passable(kFieldW + 10, kFieldH + 10));
    CHECK(!terrain.passable(kFieldW + 10, 100));
}

TEST(ResourceNodesSitOnPassableGroundAwayFromSpawns) {
    for (std::uint32_t seed = 3; seed <= 8; ++seed) {
        Sim sim;
        MatchConfig config;
        config.spawn_seed = seed;
        CHECK(sim.start(make_decks(), config));

        const auto& nodes = sim.nodes();
        CHECK(nodes.size() >= 16);  // 4 城市 + 4 高资源 + 12 普通
        CHECK(nodes.size() <= 24);

        int rich = 0;
        for (const auto& node : nodes) {
            CHECK(sim.terrain().passable(node.x, node.y));
            CHECK_EQ(node.owner, -1);
            if (node.kind == NodeKind::Rich) ++rich;
            for (int p = 0; p < kPlayers; ++p) {
                const Point base = sim.base_position(p);
                const int dx = node.x - base.x;
                const int dy = node.y - base.y;
                CHECK(dx * dx + dy * dy >= 350 * 350);  // 不能白送在基地脸上
            }
        }
        CHECK(rich >= 1);
    }
}

TEST(MapGeneratorPlacesZonesWithPlazas) {
    // 片区是地图生成的一部分：固定 14 个（3 城镇 + 2 高资源 + 9 普通），
    // 中心都要清出可建造广场，城镇要有核心血量。
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        Sim sim;
        MatchConfig config;
        config.spawn_seed = seed * 977u;
        CHECK(sim.start(make_decks(), config));

        const auto& nodes = sim.nodes();
        CHECK_EQ(static_cast<int>(nodes.size()), 20);
        int towns = 0;
        int rich = 0;
        for (const auto& node : nodes) {
            if (node.kind == NodeKind::Town) ++towns;
            if (node.kind == NodeKind::Rich) ++rich;
            // 广场清过之后，中心一定可以建东西
            CHECK(sim.terrain().buildable(node.x, node.y));
            CHECK(node.radius >= 150);
            CHECK(node.claim_radius_tiles >= 8);
            // 片区之间不能叠在一起
            for (const auto& other : nodes) {
                if (&other == &node) continue;
                const int need = node.radius + other.radius;
                const int dx = node.x - other.x;
                const int dy = node.y - other.y;
                CHECK(dx * dx + dy * dy >= need * need);
            }
        }
        CHECK_EQ(towns, 4);
        CHECK_EQ(rich, 4);
        for (const auto& node : nodes) {
            if (node.kind == NodeKind::Town) {
                CHECK(node.core_hp > 0);
                CHECK_EQ(node.core_hp, kTownCoreHp);
            }
        }
    }
}
TEST(NodesGetCapturedWhenArmiesMarch) {
    // 端到端：四个 AI 打一局，路过的部队应该能把资源点占下来
    Sim sim;
    MatchConfig config;
    config.spawn_seed = 11;
    // 这条测的是"占地 → 涨上限"，不是"怎么结束对局"。
    // 总部支援现在 3 分钟就解锁，不关掉的话对局 6 分钟内就被它打完了，
    // 领地还没长起来（下面的循环也只跑到 300 秒）。所以这里把它压到局末 = 等于关掉。
    config.hq_unlock_seconds = config.match_seconds;
    CHECK(sim.start(make_decks(), config));

    std::array<AiBrain, kPlayers> brains;
    for (int p = 0; p < kPlayers; ++p) brains[static_cast<std::size_t>(p)].reset(p, 70 + p);

    int guard = 0;
    while (sim.phase() == MatchPhase::Playing && guard < 6000) {
        for (auto& brain : brains) brain.update(sim);
        sim.advance();
        ++guard;
    }

    int owned = 0;
    int rich_owned = 0;
    for (const auto& node : sim.nodes()) {
        if (node.owner >= 0) {
            ++owned;
            if (node.kind == NodeKind::Rich) ++rich_owned;
        }
    }
    CHECK(owned > 0);
    (void)rich_owned;

    // 占地的意义：领地变大 → 建筑上限跟着涨。不涨的话"占片区"就只是个刷屏特效。
    // 开局领地 21~22 → 上限 6；领跑者占下片区之后必须更高。
    int best_territory = 0;
    int best_cap = 0;
    int best_player = -1;
    for (int p = 0; p < kPlayers; ++p) {
        const int territory = sim.resources(p).territory;
        if (territory > best_territory) {
            best_territory = territory;
            best_cap = sim.building_cap(p);
            best_player = p;
        }
    }
    CHECK(best_player >= 0);
    CHECK(best_territory > 22);
    CHECK(best_cap > 6);
    // 公式：clamp(4 + 领地/10 + 经济枢纽, 4, 11)。枢纽最多 +1，所以下限可以精确断言
    CHECK(best_cap >= std::min(4 + best_territory / 10, 11));
    CHECK(best_cap <= std::min(5 + best_territory / 10, 11));
}
