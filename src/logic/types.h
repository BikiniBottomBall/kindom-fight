// 基础类型：玩家/区域 id、兵种、军队。
#pragma once

#include <array>
#include <cstdint>

namespace landgrab {

using PlayerId = int;
using RegionId = int;

// 无主（中立）领地
inline constexpr PlayerId kNeutralOwner = -1;

enum class UnitType : int { Spear = 0, Shield = 1, Rider = 2 };
inline constexpr int kUnitTypeCount = 3;

[[nodiscard]] const char* to_string(UnitType type);

// 三角克制：长枪 > 骑手 > 盾卫 > 长枪
[[nodiscard]] bool counters(UnitType attacker, UnitType defender);

// 克制系数百分比：125 克制 / 80 被克 / 100 互不相克
[[nodiscard]] int counter_percent(UnitType attacker, UnitType defender);

// 一支军队 = 每种兵有多少。没有单兵单位、没有走位、没有阵型（ADR/设计稿四条红线）。
struct Army {
    std::array<int, kUnitTypeCount> count{};

    [[nodiscard]] int total() const { return count[0] + count[1] + count[2]; }

    [[nodiscard]] int of(UnitType type) const { return count[static_cast<int>(type)]; }

    Army& add(UnitType type, int n) {
        count[static_cast<int>(type)] += n;
        return *this;
    }

    Army& operator+=(const Army& other) {
        for (int i = 0; i < kUnitTypeCount; ++i) {
            count[i] += other.count[i];
        }
        return *this;
    }

    friend bool operator==(const Army& lhs, const Army& rhs) { return lhs.count == rhs.count; }
    friend bool operator!=(const Army& lhs, const Army& rhs) { return !(lhs == rhs); }
};

}  // namespace landgrab
