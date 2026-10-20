#include <array>

#include "logic/region.h"
#include "test_framework.h"

using kingdom_fight::adjacent;
using kingdom_fight::kRegionCount;
using kingdom_fight::neighbors;
using kingdom_fight::region_spec;
using kingdom_fight::RegionId;

TEST(RegionCountIsFive) { CHECK_EQ(kRegionCount, 5); }

TEST(EachRegionHasExactlyTwoNeighbors) {
    for (RegionId r = 0; r < kRegionCount; ++r) {
        const auto& n = neighbors(r);
        CHECK(n[0] != n[1]);
        CHECK(n[0] >= 0 && n[0] < kRegionCount);
        CHECK(n[1] >= 0 && n[1] < kRegionCount);
    }
}

TEST(AdjacencyIsSymmetric) {
    for (RegionId a = 0; a < kRegionCount; ++a) {
        for (RegionId b = 0; b < kRegionCount; ++b) {
            CHECK_EQ(adjacent(a, b), adjacent(b, a));
        }
    }
}

TEST(AdjacencyIsARing) {
    for (RegionId r = 0; r < kRegionCount; ++r) {
        CHECK(adjacent(r, (r + 1) % kRegionCount));
        CHECK(adjacent(r, (r + kRegionCount - 1) % kRegionCount));
    }
    // 环上隔一个的区域不相邻
    CHECK(!adjacent(0, 2));
    CHECK(!adjacent(1, 3));
    CHECK(!adjacent(2, 4));
}

TEST(NoRegionIsAdjacentToItself) {
    for (RegionId r = 0; r < kRegionCount; ++r) {
        CHECK(!adjacent(r, r));
    }
}

TEST(RegionSpecsHaveNamesAndNonNegativeYields) {
    for (RegionId r = 0; r < kRegionCount; ++r) {
        const auto& spec = region_spec(r);
        CHECK(!spec.name.empty());
        CHECK(spec.gold_yield >= 0);
        CHECK(spec.troop_yield >= 0);
    }
}

TEST(CenterRegionIsNeutralContestedGround) {
    // 设计意图：中枢产出最高（2 金 2 兵），所以它是争夺焦点
    CHECK_EQ(region_spec(4).gold_yield, 2);
    CHECK_EQ(region_spec(4).troop_yield, 2);
}
