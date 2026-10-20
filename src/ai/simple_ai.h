// 简单启发式 AI。
//
// 一个模块，三处复用（docs/02-game-design.md §4③）：
//   1. 单机测试对手 —— 没联网就能反复试玩
//   2. 匹配补位      —— 人数不足也能开局
//   3. 掉线/超时托管 —— 玩家掉线时替他把这一回合打完
//
// 目标不是"强"，而是"行为合理且能完整打完一局"，这样自动对局统计才有意义。
#pragma once

#include <vector>

#include "logic/command.h"
#include "logic/engine.h"
#include "logic/types.h"

namespace kingdom_fight::ai {

// 返回该玩家本回合要提交的指令（可能为空 = 略过）。
std::vector<Command> make_orders(const GameEngine& engine, PlayerId player);

}  // namespace kingdom_fight::ai
