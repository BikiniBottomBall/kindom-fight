#include "logic/region.h"

#include <cassert>

namespace kingdom_fight {
namespace {

//                       名称   金币  每回合招募上限
// 每块地至少 1 金 1 兵：保证任何一个起始位置都不会"完全无法行动"（死局）。
// 数值全部是占位，等自动对局统计出来后调（见 docs/02-game-design.md §9）。
constexpr std::array<RegionSpec, kRegionCount> kRegionSpecs = {{
    {"北岭", 2, 1},   // 0  富庶，兵源少
    {"东滩", 1, 2},   // 1  兵源地
    {"南关", 1, 1},   // 2  均衡
    {"西原", 1, 2},   // 3  兵源地偏弱
    {"中枢", 2, 2},   // 4  最富，开局中立 —— 争夺焦点
}};

// 环形邻接：i 的两个邻居是 (i+1) 与 (i+4)，升序存放。
constexpr std::array<std::array<RegionId, 2>, kRegionCount> kAdjacency = {{
    {{1, 4}},  // 0 北岭
    {{0, 2}},  // 1 东滩
    {{1, 3}},  // 2 南关
    {{2, 4}},  // 3 西原
    {{0, 3}},  // 4 中枢
}};

}  // namespace

const RegionSpec& region_spec(RegionId region) {
    assert(region >= 0 && region < kRegionCount);
    return kRegionSpecs[static_cast<std::size_t>(region)];
}

const std::array<RegionId, 2>& neighbors(RegionId region) {
    assert(region >= 0 && region < kRegionCount);
    return kAdjacency[static_cast<std::size_t>(region)];
}

bool adjacent(RegionId lhs, RegionId rhs) {
    for (const RegionId n : neighbors(lhs)) {
        if (n == rhs) {
            return true;
        }
    }
    return false;
}

}  // namespace kingdom_fight
