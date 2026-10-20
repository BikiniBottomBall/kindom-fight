#include "logic/report.h"

#include <cstdio>
#include <string>

#include "logic/card_def.h"
#include "logic/region.h"

namespace kingdom_fight {
namespace {

std::string owner_tag(PlayerId id) {
    if (id == kNeutralOwner) {
        return "中立";
    }
    return "P" + std::to_string(id);
}

std::string army_text(const Army& army) {
    std::string s;
    s += std::to_string(army.total());
    s += "（枪";
    s += std::to_string(army.of(UnitType::Spear));
    s += " 盾";
    s += std::to_string(army.of(UnitType::Shield));
    s += " 骑";
    s += std::to_string(army.of(UnitType::Rider));
    s += "）";
    return s;
}

std::string hand_text(const DeckState& deck) {
    if (deck.hand.empty()) {
        return "（空）";
    }
    std::string s;
    for (const int card : deck.hand) {
        s += card_def(card).name;
        s += ' ';
    }
    return s;
}

}  // namespace

void print_game_state(const GameState& state) {
    std::printf("回合 %d / %d    阶段：%s    玩家：%d 名\n", state.turn, state.cfg.max_turns,
                to_string(state.phase), state.player_count);
    if (state.winner) {
        std::printf("胜者：P%d\n", *state.winner);
    }

    std::printf("\n[领地]\n");
    for (const Region& region : state.regions) {
        const RegionSpec& spec = region_spec(region.id);
        std::printf("  %d %-6s 归属 %-4s 驻军 %-20s 产出 金%d 招募上限%d  建筑 Lv%d\n", region.id,
                    std::string(spec.name).c_str(), owner_tag(region.owner).c_str(),
                    army_text(region.garrison).c_str(), spec.gold_yield, spec.troop_yield,
                    region.building_level);
    }

    std::printf("\n[玩家]\n");
    for (int i = 0; i < state.player_count; ++i) {
        const Player& player = state.players[static_cast<std::size_t>(i)];
        int owned = 0;
        int troops = 0;
        for (const Region& region : state.regions) {
            if (region.owner == player.id) {
                ++owned;
                troops += region.garrison.total();
            }
        }
        std::printf("  P%d %s 金币 %-3d AP %d 牌库 %d 领地 %d 兵力 %-3d 分数 %d\n", player.id,
                    player.alive ? "存活" : "出局", player.gold, player.ap, player.deck.pile_size(),
                    owned, troops, owned * 3 + troops * 2 + player.gold);
    }

    std::printf("\n[手牌]\n");
    for (int i = 0; i < state.player_count; ++i) {
        const Player& player = state.players[static_cast<std::size_t>(i)];
        std::printf("  P%d  %s\n", player.id, hand_text(player.deck).c_str());
    }
    std::printf("\n");
}

void print_event(const Event& event) { std::printf("  %s\n", event.describe().c_str()); }

void print_events(const std::vector<Event>& events) {
    for (const Event& event : events) {
        print_event(event);
    }
}

void print_event_summary(const std::vector<Event>& log) {
    std::printf("[事件统计] 共 %zu 条\n", log.size());
    for (int t = 0; t < kEventTypeCount; ++t) {
        int count = 0;
        for (const Event& event : log) {
            if (static_cast<int>(event.type) == t) {
                ++count;
            }
        }
        if (count > 0) {
            std::printf("  %-18s %d\n", to_string(static_cast<EventType>(t)), count);
        }
    }
}

}  // namespace kingdom_fight
