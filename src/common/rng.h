// 确定性随机数（splitmix64）。
//
// 为什么不用 std::mt19937 也不 rand()：
//   1. 同一种子必须产出同一序列 —— 这是回放、反作弊、线上问题复现的地基（S7）。
//   2. 不用全局状态，随机源跟着 GameEngine 走，测试可以随便造多个实例。
//   3. 不依赖标准库实现（std::uniform_int_distribution 的实现在不同 libstdc++ 版本间可能不同）。
#pragma once

#include <cstdint>

namespace kingdom_fight {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) : state_(seed) {}

    [[nodiscard]] std::uint64_t next_u64() {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // 闭区间 [lo, hi]
    [[nodiscard]] int next_int(int lo, int hi) {
        const auto span = static_cast<std::uint64_t>(hi - lo + 1);
        return lo + static_cast<int>(next_u64() % span);
    }

    // 按百分比扰动：base * [lo_pct, hi_pct] / 100
    [[nodiscard]] int jitter(int base, int lo_pct, int hi_pct) {
        return base * next_int(lo_pct, hi_pct) / 100;
    }

    [[nodiscard]] std::uint64_t state() const { return state_; }

private:
    std::uint64_t state_;
};

}  // namespace kingdom_fight
