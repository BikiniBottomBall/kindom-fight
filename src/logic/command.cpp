#include "logic/command.h"

namespace landgrab {

const char* to_string(OrderType type) {
    switch (type) {
        case OrderType::Pass:       return "略过";
        case OrderType::Build:      return "建造";
        case OrderType::Recruit:    return "招募";
        case OrderType::Move:       return "调动";
        case OrderType::Attack:     return "进攻";
        case OrderType::PlayTactic: return "暗置战术";
    }
    return "未知";
}

}  // namespace landgrab
