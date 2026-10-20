#pragma once

#include <vector>

#include "logic/card_def.h"
#include "logic/command.h"
#include "logic/types.h"

namespace kingdom_fight {

struct Player {
    PlayerId id = 0;
    int leader = 0;         // 领导者（被动 + 专属卡；MVP 阶段四人完全对称，暂未启用差异）
    int gold = 0;
    int ap = 0;             // 本回合剩余行动点
    bool alive = true;
    bool submitted = false; // 本回合是否已提交（含超时托管）
    DeckState deck;

    // 本回合暗置的战术卡。决策阶段提交、结算时揭晓；每回合开始清空。
    // 只对 tactic_target 区域发生的战斗生效 —— 该区域这回合没打仗，卡就白扣了。
    int tactic_card = -1;
    RegionId tactic_target = -1;

    // 本回合已提交、尚未结算的指令。结算后清空 —— 这是「同时决策」的落点：
    // 所有人的意图先攒着，等所有人都交齐（或超时）才一起结算。
    std::vector<Command> pending_orders;
};

}  // namespace kingdom_fight
