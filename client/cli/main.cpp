// S10 终端客户端：你（P0）和 3 个 AI 打一局。
//
// 职责边界（ADR 0002 的护栏二在这里同样适用）：
//   · 客户端只做两件事：把 GameState 渲染给人看、把人的意图翻译成 Command
//   · 任何合法性判定都在引擎里。客户端做的检查只是为了少让人白按一次键，
//     即使全部删掉，服务端依然正确。
//   · 因此这里没有任何规则常量，所有数字都从 GameState / CardDef 里读。
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ai/simple_ai.h"
#include "common/config.h"
#include "logic/engine.h"
#include "logic/event.h"
#include "logic/region.h"
#include "logic/report.h"

namespace {

using namespace kingdom_fight;

constexpr PlayerId kHuman = 0;

void print_banner(int players, std::uint64_t seed) {
    std::printf("==============================================\n");
    std::printf(" 王国之争 / kingdom_fight   —— 你是 P0\n");
    std::printf("==============================================\n");
    std::printf("对手：%d 个 AI    随机种子：%llu\n", players - 1,
                static_cast<unsigned long long>(seed));
    std::printf("胜利条件：控制 %d 块领地 / 对手全灭 / 回合用尽时分数最高\n", kWinRegions);
    std::printf("总回合数上限：%d\n\n", kMaxTurns);
}

void print_help() {
    std::printf("\n--- 指令（都只写数字，区域编号见地图）---\n");
    std::printf("  b <区域>                 建造：花 %d 金，等级+1（每级 +1 金/回合，上限 %d 级）\n",
                kBuildCost, kBuildMaxLevel);
    std::printf("  r <区域> <s|h|r> <数量>   招募：s=长枪 h=盾卫 r=骑手，%d 金/兵，受该区域招募上限限制\n",
                kRecruitCostPerUnit);
    std::printf("  m <起点> <终点> <数量>    调动：只能在自己相邻的两块领地之间\n");
    std::printf("  a <起点> <终点> <数量>    进攻：相邻的敌方/中立领地\n");
    std::printf("  t <手牌序号> <区域>      暗置战术卡：不花 AP，该区域发生战斗时给己方加战力\n");
    std::printf("  c                       清空本回合已输入的指令\n");
    std::printf("  done                    提交并结算本回合（AI 同时行动）\n");
    std::printf("  help / quit             说明 / 退出\n");
    std::printf("------------------------------------------\n\n");
}

const char* unit_tag(UnitType unit) {
    switch (unit) {
        case UnitType::Spear:  return "枪";
        case UnitType::Shield: return "盾";
        case UnitType::Rider:  return "骑";
    }
    return "?";
}

std::string army_text(const Army& army) {
    std::string s = std::to_string(army.total());
    s += "(";
    s += std::to_string(army.of(UnitType::Spear));
    s += "/";
    s += std::to_string(army.of(UnitType::Shield));
    s += "/";
    s += std::to_string(army.of(UnitType::Rider));
    s += ")";
    return s;
}

std::string hand_text(const DeckState& deck) {
    if (deck.hand.empty()) {
        return "（空）";
    }
    std::string s;
    for (std::size_t i = 0; i < deck.hand.size(); ++i) {
        const CardDef& card = card_def(deck.hand[i]);
        s += "[" + std::to_string(i) + "] ";
        s += card.name;
        if (card.kind == CardKind::Tactic) {
            s += "(战术 +" + std::to_string(card.power) + "%)";
        }
        s += "  ";
    }
    return s;
}

// 中文字符在 UTF-8 里占 3 字节但只占 2 个显示列，printf 的 %-Ns 按字节补空格，
// 表格必然错位。所以这里自己算显示宽度。
int display_width(const std::string& text) {
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[i]);
        int length = 1;
        unsigned int code = first;
        if ((first & 0xE0) == 0xC0) {
            length = 2;
            code = first & 0x1F;
        } else if ((first & 0xF0) == 0xE0) {
            length = 3;
            code = first & 0x0F;
        } else if ((first & 0xF8) == 0xF0) {
            length = 4;
            code = first & 0x07;
        }
        for (int k = 1; k < length && i + static_cast<std::size_t>(k) < text.size(); ++k) {
            code = (code << 6) |
                   (static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]) & 0x3F);
        }
        i += static_cast<std::size_t>(length);
        const bool wide = (code >= 0x1100 && code <= 0x115F) ||
                          (code >= 0x2E80 && code <= 0xA4CF) ||
                          (code >= 0xAC00 && code <= 0xD7A3) ||
                          (code >= 0xF900 && code <= 0xFAFF) ||
                          (code >= 0xFE30 && code <= 0xFE6F) ||
                          (code >= 0xFF00 && code <= 0xFF60) ||
                          (code >= 0xFFE0 && code <= 0xFFE6);
        width += wide ? 2 : 1;
    }
    return width;
}

std::string pad_right(const std::string& text, int width) {
    const int current = display_width(text);
    if (current >= width) {
        return text;
    }
    return text + std::string(static_cast<std::size_t>(width - current), ' ');
}

void print_board(const GameEngine& engine) {
    const GameState& state = engine.state();
    const Player& me = state.players[static_cast<std::size_t>(kHuman)];

    std::printf("── 第 %d 回合 ──  你的金币 %d  AP %d  领地 %d  兵力 %d\n", state.turn + 1, me.gold,
                me.ap, engine.count_owned_regions(kHuman), engine.count_total_troops(kHuman));

    std::string header = "      ";
    header += pad_right("领地", 8);
    header += pad_right("归属", 6);
    header += pad_right("驻军(枪/盾/骑)", 16);
    header += "产出 / 建筑";
    std::printf("%s\n", header.c_str());

    for (RegionId r = 0; r < kRegionCount; ++r) {
        const Region& region = state.regions[static_cast<std::size_t>(r)];
        const RegionSpec& spec = region_spec(r);
        const bool mine = region.owner == kHuman;
        std::string owner = "中立";
        if (region.owner != kNeutralOwner) {
            owner = "P" + std::to_string(region.owner);
        }
        std::string line = mine ? "  *   " : "      ";
        line += pad_right(std::string(spec.name), 8);
        line += pad_right(owner, 6);
        line += pad_right(army_text(region.garrison), 16);
        line += "金" + std::to_string(spec.gold_yield) + " 招募上限" +
                std::to_string(spec.troop_yield) + " 建筑Lv" + std::to_string(region.building_level);
        std::printf("%s\n", line.c_str());
    }

    std::printf("  你的手牌：%s\n", hand_text(me.deck).c_str());

    std::printf("  其他玩家：");
    for (int p = 0; p < state.player_count; ++p) {
        if (p == kHuman) {
            continue;
        }
        const Player& other = state.players[static_cast<std::size_t>(p)];
        std::printf("  P%d%s 领地%d 兵力%d", p, other.alive ? "" : "(出局)",
                    engine.count_owned_regions(p), engine.count_total_troops(p));
    }
    std::printf("\n\n");
}

bool parse_int(const std::string& token, int& out) {
    if (token.empty()) {
        return false;
    }
    for (const char c : token) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    out = std::atoi(token.c_str());
    return true;
}

bool parse_region(const std::string& token, RegionId& out) {
    int value = 0;
    if (!parse_int(token, value)) {
        return false;
    }
    if (value < 0 || value >= kRegionCount) {
        return false;
    }
    out = value;
    return true;
}

bool parse_unit(const std::string& token, UnitType& out) {
    if (token == "s") {
        out = UnitType::Spear;
        return true;
    }
    if (token == "h") {
        out = UnitType::Shield;
        return true;
    }
    if (token == "r") {
        out = UnitType::Rider;
        return true;
    }
    return false;
}

void echo_orders(const std::vector<Command>& orders) {
    if (orders.empty()) {
        std::printf("  （本回合没有指令，直接略过）\n");
        return;
    }
    std::printf("  已加入 %zu 条指令：", orders.size());
    for (const Command& cmd : orders) {
        switch (cmd.type) {
            case OrderType::Build:
                std::printf(" [建造 区域%d]", cmd.from);
                break;
            case OrderType::Recruit:
                std::printf(" [招募 区域%d %s兵%d]", cmd.from, unit_tag(cmd.unit), cmd.count);
                break;
            case OrderType::Move:
            case OrderType::Attack:
                std::printf(" [%s 区域%d→%d %d兵]", to_string(cmd.type), cmd.from, cmd.to, cmd.count);
                break;
            case OrderType::PlayTactic:
                std::printf(" [暗置战术 目标区域%d]", cmd.to);
                break;
            case OrderType::Pass:
                break;
        }
    }
    std::printf("\n");
}

// 返回 false 表示玩家要退出
bool collect_orders(const GameEngine& engine, std::vector<Command>& orders) {
    const GameState& state = engine.state();
    const Player& me = state.players[static_cast<std::size_t>(kHuman)];

    while (true) {
        std::printf("指令> ");
        std::fflush(stdout);

        std::string line;
        if (!std::getline(std::cin, line)) {
            return false;  // EOF / Ctrl+C 管道关闭
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        std::istringstream stream(line);
        std::vector<std::string> tokens;
        for (std::string token; stream >> token;) {
            tokens.push_back(token);
        }
        if (tokens.empty()) {
            continue;
        }

        const std::string& verb = tokens[0];
        if (verb == "done" || verb == "d") {
            return true;
        }
        if (verb == "quit" || verb == "q" || verb == "exit") {
            return false;
        }
        if (verb == "help" || verb == "h" || verb == "?") {
            print_help();
            continue;
        }
        if (verb == "c" || verb == "clear") {
            orders.clear();
            std::printf("  已清空本回合指令\n");
            continue;
        }

        Command cmd{};
        if (verb == "b" && tokens.size() == 2) {
            if (!parse_region(tokens[1], cmd.from)) {
                std::printf("  ! 区域编号要是 0~%d\n", kRegionCount - 1);
                continue;
            }
            cmd.type = OrderType::Build;
            orders.push_back(cmd);
        } else if (verb == "r" && tokens.size() == 4) {
            if (!parse_region(tokens[1], cmd.from)) {
                std::printf("  ! 区域编号要是 0~%d\n", kRegionCount - 1);
                continue;
            }
            if (!parse_unit(tokens[2], cmd.unit)) {
                std::printf("  ! 兵种写 s / h / r\n");
                continue;
            }
            if (!parse_int(tokens[3], cmd.count) || cmd.count <= 0) {
                std::printf("  ! 数量要是正整数\n");
                continue;
            }
            cmd.type = OrderType::Recruit;
            orders.push_back(cmd);
        } else if ((verb == "m" || verb == "a") && tokens.size() == 4) {
            RegionId from = -1;
            RegionId to = -1;
            if (!parse_region(tokens[1], from) || !parse_region(tokens[2], to)) {
                std::printf("  ! 区域编号要是 0~%d\n", kRegionCount - 1);
                continue;
            }
            if (!parse_int(tokens[3], cmd.count) || cmd.count <= 0) {
                std::printf("  ! 数量要是正整数\n");
                continue;
            }
            cmd.type = (verb == "m") ? OrderType::Move : OrderType::Attack;
            cmd.from = from;
            cmd.to = to;
            orders.push_back(cmd);
        } else if (verb == "t" && tokens.size() == 3) {
            int index = -1;
            if (!parse_int(tokens[1], index) || index < 0 ||
                index >= static_cast<int>(me.deck.hand.size())) {
                std::printf("  ! 手牌序号要在这个范围：0~%d\n",
                            static_cast<int>(me.deck.hand.size()) - 1);
                continue;
            }
            if (!parse_region(tokens[2], cmd.to)) {
                std::printf("  ! 区域编号要是 0~%d\n", kRegionCount - 1);
                continue;
            }
            cmd.type = OrderType::PlayTactic;
            cmd.card = me.deck.hand[static_cast<std::size_t>(index)];
            cmd.from = -1;
            orders.push_back(cmd);
        } else {
            std::printf("  ! 看不懂这条指令，输入 help 看说明\n");
            continue;
        }

        echo_orders(orders);
        if (static_cast<int>(orders.size()) > me.ap) {
            std::printf("  ! 注意：你有 %d 个行动点，第 %d 条起会被服务端拒收（战术卡和略过不耗 AP）\n",
                        me.ap, me.ap + 1);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    int players = kMaxPlayers;
    std::uint64_t seed = 20240101ull;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--players" && i + 1 < argc) {
            players = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            std::printf("用法：kingdom_fight_cli [--seed N] [--players 2~4]\n");
            return 0;
        }
    }
    players = (players < kMinPlayers) ? kMinPlayers : players;
    players = (players > kMaxPlayers) ? kMaxPlayers : players;

    GameConfig cfg{};
    GameEngine engine(cfg, players, seed);

    print_banner(engine.state().player_count, seed);
    print_help();

    while (engine.state().phase != Phase::Finished) {
        print_board(engine);

        std::vector<Command> orders;
        if (!collect_orders(engine, orders)) {
            std::printf("已退出。\n");
            return 0;
        }
        engine.submit(kHuman, std::move(orders));
        for (PlayerId p = 1; p < engine.state().player_count; ++p) {
            engine.submit(p, ai::make_orders(engine, p));
        }

        const int resolving = engine.state().turn + 1;
        const auto events = engine.resolve_turn();
        std::printf("\n──────── 第 %d 回合结算 ────────\n", resolving);
        print_events(events);
        std::printf("────────────────────────────────\n\n");

        if (engine.state().phase == Phase::Finished) {
            break;
        }
    }

    std::printf("========== 对局结束 ==========\n");
    if (engine.state().winner) {
        std::printf("胜者：P%d%s\n", *engine.state().winner,
                    *engine.state().winner == kHuman ? "（你）" : "");
    }
    print_game_state(engine.state());
    return 0;
}
