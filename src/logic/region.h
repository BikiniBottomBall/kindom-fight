// 领地：5 个区域组成一个环（0-1-2-3-4-0）。
//
// 红线：地图是"节点 + 邻接表"，不是格子。没有寻路、没有地形、没有迷雾。
// 想在 S10 之后换成更有意思的图（比如环形 + 中枢连对角），只改 region.cpp 的邻接表。
#pragma once

#include <array>
#include <string_view>

#include "logic/types.h"

namespace kingdom_fight {

inline constexpr int kRegionCount = 5;

struct RegionSpec {
    std::string_view name;
    int gold_yield = 1;   // 每回合产金
    int troop_yield = 1;  // 每回合产兵源（S3 启用）
};

[[nodiscard]] const RegionSpec& region_spec(RegionId region);

// 固定返回 2 个相邻区域（环上左右各一）。返回值按 id 升序？
// 不保证顺序稳定用于逻辑，但保证同一 id 每次返回同一顺序 —— 确定性要求。
[[nodiscard]] const std::array<RegionId, 2>& neighbors(RegionId region);

[[nodiscard]] bool adjacent(RegionId lhs, RegionId rhs);

// 领地运行时状态。归属存在区域上（player 不持有区域列表），避免两处不一致。
struct Region {
    RegionId id = 0;
    PlayerId owner = kNeutralOwner;
    Army garrison;
    int building_level = 0;  // 0 = 无，>0 表示建筑等级（S3）
};

}  // namespace kingdom_fight
