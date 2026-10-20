#pragma once

#include <vector>

#include "logic/card_def.h"
#include "logic/command.h"
#include "logic/types.h"

namespace landgrab {

struct Player {
    PlayerId id = 0;
    int leader = 0;         // 领导者（被动 + 专属卡，后续用数据表扩展）
    int gold = 0;
    int ap = 0;             // 本回合剩余行动点
    bool alive = true;
    bool submitted = false; // 本回合是否已提交（含超时托管）
    DeckState deck;

    // 本回合已提交、尚未结算的指令。结算后清空 —— 这是「同时决策」的落点：
    // 所有人的意图先攒着，等所有人都交齐（或超时）才一起结算。
    std::vector<Command> pending_orders;
};

}  // namespace landgrab
