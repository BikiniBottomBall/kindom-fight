#include "logic/event.h"

#include <string>

#include "logic/card_def.h"
#include "logic/command.h"
#include "logic/region.h"

namespace kingdom_fight {
namespace {

std::string player_tag(PlayerId id) {
    if (id == kNeutralOwner) {
        return "中立";
    }
    return "P" + std::to_string(id);
}

std::string region_name(RegionId id) {
    if (id < 0 || id >= kRegionCount) {
        return "?";
    }
    return std::string(region_spec(id).name);
}

std::string card_name(int card_id) {
    if (card_id < 0 || card_id >= kCardCatalogSize) {
        return "?";
    }
    return std::string(card_def(card_id).name);
}

std::string army_triplet(int spear, int shield, int rider) {
    return "长枪" + std::to_string(spear) + " 盾卫" + std::to_string(shield) + " 骑手" +
           std::to_string(rider);
}

}  // namespace

const char* to_string(EventType type) {
    switch (type) {
        case EventType::GameBegin:         return "GameBegin";
        case EventType::TurnBegin:         return "TurnBegin";
        case EventType::OrdersAccepted:    return "OrdersAccepted";
        case EventType::OrderRejected:     return "OrderRejected";
        case EventType::ResourceProduced:  return "ResourceProduced";
        case EventType::BuildingCompleted: return "BuildingCompleted";
        case EventType::UnitsRecruited:    return "UnitsRecruited";
        case EventType::ArmyMoved:         return "ArmyMoved";
        case EventType::BattleResolved:    return "BattleResolved";
        case EventType::Casualties:        return "Casualties";
        case EventType::RegionCaptured:    return "RegionCaptured";
        case EventType::TacticRevealed:    return "TacticRevealed";
        case EventType::PlayerEliminated:  return "PlayerEliminated";
        case EventType::TurnEnd:           return "TurnEnd";
        case EventType::GameOver:          return "GameOver";
        case EventType::CardDrawn:         return "CardDrawn";
        case EventType::Count:             break;
    }
    return "Unknown";
}

const char* to_string(RejectReason reason) {
    switch (reason) {
        case RejectReason::Timeout:         return "超时未提交";
        case RejectReason::NotImplemented:  return "该指令尚未实现";
        case RejectReason::IllegalTarget:   return "目标不合法";
        case RejectReason::IllegalSource:   return "来源不合法";
        case RejectReason::NotAdjacent:     return "两区不相邻";
        case RejectReason::NotEnoughAp:     return "行动点不足";
        case RejectReason::NotEnoughGold:   return "金币不足";
        case RejectReason::NotEnoughTroops: return "兵力不足";
        case RejectReason::BuildingMaxed:   return "建筑已满级";
        case RejectReason::RecruitLimit:    return "超出招募上限";
    }
    return "未知原因";
}

const char* to_string(WinReason reason) {
    switch (reason) {
        case WinReason::Conquer:      return "控制半数以上领地";
        case WinReason::LastStanding: return "对手全部淘汰";
        case WinReason::HighestScore: return "回合用尽，分数最高";
    }
    return "未知原因";
}

std::string Event::serialize() const {
    std::string s;
    s.reserve(80);
    s += to_string(type);
    s += '|';
    s += std::to_string(actor);
    s += '|';
    s += std::to_string(a);
    s += '|';
    s += std::to_string(b);
    s += '|';
    s += std::to_string(c);
    s += '|';
    s += std::to_string(d);
    s += '|';
    s += std::to_string(e);
    s += '|';
    s += std::to_string(f);
    return s;
}

std::string Event::describe() const {
    switch (type) {
        case EventType::GameBegin:
            return "[对局] 开始，" + std::to_string(a) + " 名玩家进入战场";
        case EventType::TurnBegin:
            return "── 第 " + std::to_string(a) + " 回合 ──";
        case EventType::TurnEnd:
            return "── 第 " + std::to_string(a) + " 回合结束 ──";
        case EventType::OrdersAccepted:
            return "[指令] " + player_tag(actor) + " 提交了 " + std::to_string(a) + " 条指令";
        case EventType::OrderRejected:
            return "[拒收] " + player_tag(actor) + " 的「" +
                   (b >= 0 && b <= static_cast<int>(OrderType::PlayTactic)
                        ? to_string(static_cast<OrderType>(b))
                        : "?") +
                   "」→ " + to_string(static_cast<RejectReason>(a));
        case EventType::ResourceProduced:
            return "[产出] " + player_tag(actor) + " 获得 " + std::to_string(a) + " 金币";
        case EventType::BuildingCompleted:
            return "[建造] " + region_name(a) + " 升到 " + std::to_string(b) + " 级（占者 " +
                   player_tag(actor) + "）";
        case EventType::UnitsRecruited:
            return "[招募] " + region_name(a) + " 得到 " + army_triplet(b, c, d) + "（" +
                   player_tag(actor) + "）";
        case EventType::ArmyMoved:
            return "[调动] " + player_tag(actor) + " 从 " + region_name(a) + " 向 " + region_name(b) +
                   " 调 " + army_triplet(c, d, e);
        case EventType::BattleResolved:
            return "[战斗] " + region_name(a) + "：攻方 " + player_tag(b) + " 战力 " +
                   std::to_string(d) + " vs 守方 " + player_tag(c) + " 战力 " +
                   std::to_string(e) + " → " + (f == 1 ? "攻方胜" : "守方胜");
        case EventType::Casualties:
            return "[伤亡] " + player_tag(actor) + " 在 " + region_name(a) + " 损失 " +
                   army_triplet(b, c, d);
        case EventType::RegionCaptured:
            return "[占领] " + region_name(a) + " 归属 " + player_tag(actor) + "（原 " +
                   player_tag(b) + "）";
        case EventType::TacticRevealed:
            return "[战术] " + player_tag(actor) + " 在 " + region_name(b) + " 揭晓「" +
                   card_name(a) + "」，战力 +" + std::to_string(c) + "%";
        case EventType::PlayerEliminated:
            return "[淘汰] " + player_tag(actor) + " 出局";
        case EventType::GameOver:
            return "[结束] " + player_tag(actor) + " 获胜（" +
                   to_string(static_cast<WinReason>(a)) + "，第 " + std::to_string(b) + " 回合）";
        case EventType::CardDrawn:
            return "[抽牌] " + player_tag(actor) + " 抽到「" + card_name(a) + "」（手牌 " +
                   std::to_string(b) + "）";
        case EventType::Count:
            break;
    }
    return "[未知事件]";
}

}  // namespace kingdom_fight
