// 基础类型：玩家/区域 id、兵种、军队。
#pragma once

#include <array>
#include <cstdint>

namespace kingdom_fight {

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

    Army& operator-=(const Army& other) {
        for (int i = 0; i < kUnitTypeCount; ++i) {
            count[i] -= other.count[i];
            if (count[i] < 0) {
                count[i] = 0;
            }
        }
        return *this;
    }

    [[nodiscard]] bool empty() const { return total() == 0; }

    // 从本军队抽走 amount 兵，返回抽走的部队。
    // 抽取顺序固定为 长枪 → 盾卫 → 骑手 —— 顺序一旦不固定，同样的指令就会产出
    // 不同的结果，决定论就没了。
    Army take(int amount) {
        Army taken;
        int left = amount;
        for (int t = 0; t < kUnitTypeCount && left > 0; ++t) {
            const int n = (count[t] < left) ? count[t] : left;
            taken.count[t] = n;
            count[t] -= n;
            left -= n;
        }
        return taken;
    }

    // 按百分比缩水（向下取整）
    [[nodiscard]] Army scaled(int percent) const {
        Army out;
        for (int t = 0; t < kUnitTypeCount; ++t) {
            out.count[t] = count[t] * percent / 100;
        }
        return out;
    }

    friend bool operator==(const Army& lhs, const Army& rhs) { return lhs.count == rhs.count; }
    friend bool operator!=(const Army& lhs, const Army& rhs) { return !(lhs == rhs); }
};

}  // namespace kingdom_fight
