// 指令 = 玩家的「意图」，不是「事实」。
//
// 客户端只能提交意图，能不能成立由服务端判定（S4）。这是服务端权威的根基，
// 也是防作弊面的全部：客户端没有任何"我已经出了这张牌"的话语权。
#pragma once

#include "logic/types.h"

namespace kingdom_fight {

enum class OrderType : int {
    Pass = 0,     // 放弃/超时占位
    Build,        // 建造/升级建筑（S3）
    Recruit,      // 招募军队（S4）
    Move,         // 向相邻区域调动（S4）
    Attack,       // 向相邻区域进攻（S4/S5）
    PlayTactic,   // 暗置战术卡（S5）
};

[[nodiscard]] const char* to_string(OrderType type);

struct Command {
    OrderType type = OrderType::Pass;
    RegionId from = -1;
    RegionId to = -1;
    UnitType unit = UnitType::Spear;
    int count = 0;
    int card = -1;
};

}  // namespace kingdom_fight
