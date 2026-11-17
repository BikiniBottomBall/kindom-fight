// 服务端内置 AI 对手。
//
// 目的不是"强",而是"像个会玩的人":会防守、会攒钱放大牌、会补金矿、会在敌兵扎堆时丢火雨。
// 决策是确定性的（自带 xorshift32），所以同一 seed + 同一局面 = 同一手，bench 可复现。
#pragma once

#include <cstdint>
#include <cstddef>

#include "rts/sim.h"

namespace kingdom_fight::rts {

// AI 难度：影响决策频率、开局激进度、法术使用
enum class AiLevel : std::uint8_t { Easy = 0, Normal, Hard };

class AiBrain {
public:
    void reset(int player, std::uint32_t seed);
    void set_level(AiLevel level) { level_ = level; }
    [[nodiscard]] AiLevel level() const { return level_; }

    // 每个 tick 调一次；内部自己降频到 ~2.5Hz，避免每 tick 都做决策。
    void update(Sim& sim);

    [[nodiscard]] int player() const { return player_; }

private:
    [[nodiscard]] std::uint32_t next_random();

    int player_ = 1;
    AiLevel level_ = AiLevel::Normal;
    std::uint32_t rng_ = 0x9E3779B9u;
    int next_think_tick_ = 0;
};

}  // namespace kingdom_fight::rts
