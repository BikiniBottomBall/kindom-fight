// RTS 自动对局统计（4 人局）：AI vs AI 跑 N 局，输出胜负 / 时长 / 结束原因分布。
//
// 用法：
//   build\bin\kingdom_fight_bench.exe --matches 200
//   build\bin\kingdom_fight_bench.exe --matches 1 --watch
//
// 为什么先做这个：数值平衡靠拍脑袋必然反复。先有分布，再改参数。
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rts/ai.h"
#include "rts/sim.h"

using namespace kingdom_fight::rts;

namespace {

struct Stats {
    int matches = 0;
    std::array<int, kPlayers> wins{};
    int draws = 0;
    long long total_ticks = 0;
    int min_ticks = 1 << 30;
    int max_ticks = 0;
    int reason_base = 0;
    int reason_timeout = 0;
    long long total_base_hp_left = 0;
    long long total_survivors = 0;
    long long total_first_blood = 0;  // 第一次有人出局的 tick
    int first_blood_count = 0;
    std::array<int, kAllianceCount> alliance_games{};
    std::array<int, kAllianceCount> alliance_wins{};
    long long total_owned_nodes = 0;
    int owned_samples = 0;
    int total_nodes = 0;
    // 僵持局的诊断：结束时大家手里有多少领地、总部支援到底用没用出去。
    // （总部支援是设计里的"确定性结束手段"，它没被用起来就说明僵持局是卡在这。）
    long long total_territory = 0;
    long long total_hq_calls = 0;
};

std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

std::vector<int> make_deck(std::uint32_t& rng, Alliance alliance) {
    // 只能从自己阵营的 10 张池子里挑 8 张（不放回，所以不会出现 3 张重复）
    const auto& source = card_pool(alliance);
    std::vector<int> pool(source.begin(), source.end());
    std::vector<int> deck;
    for (int i = 0; i < kDeckSize; ++i) {
        const auto index = static_cast<std::size_t>(next_random(rng) % pool.size());
        deck.push_back(pool[index]);
        std::swap(pool[index], pool.back());
        pool.pop_back();
    }
    return deck;
}

void print_watch_line(const Sim& sim) {
    std::printf("t=%5d  base", sim.tick());
    for (int p = 0; p < kPlayers; ++p) std::printf(" %4d", sim.base_hp(p));
    std::printf("  mp");
    for (int p = 0; p < kPlayers; ++p) {
        std::printf(" %3d/%3d", sim.resources(p).manpower, sim.player(p).manpower_cap());
    }
    std::printf("  units");
    for (int p = 0; p < kPlayers; ++p) {
        std::printf(" %2zu", sim.player(p).units.size());
    }
    std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
    int matches = 50;
    bool watch = false;
    bool verbose = false;
    // 默认是"压缩节奏"：900 秒一局、120 秒解锁总部支援，跑得快、能在一分钟里看到后期机制。
    // 要验**出厂节奏**就显式传 --match-seconds 1800 --hq-unlock 900，
    // 否则改一局时长/解锁时间的时候，bench 根本不会反映出来。
    int match_seconds = 900;
    int hq_unlock_seconds = 120;
    // --shipping：不吃上面那两个压缩值，直接用 MatchConfig 的出厂默认
    //（跟着 types.h 走，改默认值时门禁自动同步，不用在两处抄数字）
    bool shipping_timing = false;
    // 节奏门禁（CI 用）：--check 时按阈值判定，超了返回 1。
    // 这是一道**下限保险**，不是平衡目标 —— 阈值故意留得很宽，只用来接住
    // "节奏彻底崩掉（全都在拖时长）" 这种回归。平衡目标见 README 的数值基线。
    bool check = false;
    int max_avg_seconds = 840;
    int max_timeout_percent = 85;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--matches") == 0 && i + 1 < argc) {
            matches = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--watch") == 0) {
            watch = true;
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (std::strcmp(argv[i], "--check") == 0) {
            check = true;
        } else if (std::strcmp(argv[i], "--max-avg-seconds") == 0 && i + 1 < argc) {
            max_avg_seconds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--max-timeout-percent") == 0 && i + 1 < argc) {
            max_timeout_percent = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--match-seconds") == 0 && i + 1 < argc) {
            match_seconds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--hq-unlock") == 0 && i + 1 < argc) {
            hq_unlock_seconds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--shipping") == 0) {
            shipping_timing = true;
        } else {
            std::printf(
                "用法: %s [--matches N] [--watch] [--verbose]\n"
                "      [--shipping] [--match-seconds 900] [--hq-unlock 120]\n"
                "      [--check] [--max-avg-seconds 840] [--max-timeout-percent 85]\n"
                "默认是压缩节奏（900 秒 / 120 秒），跑得快但**验不出真局的问题**；\n"
                "验出厂节奏用 --shipping（直接取 MatchConfig 的默认值）。\n",
                argv[0]);
            return 2;
        }
    }
    if (matches <= 0) matches = 1;

    std::uint32_t rng = 0xC0FFEEu;
    Stats stats;

    for (int index = 0; index < matches; ++index) {
        // 每家随机一个联盟，卡组只能从该阵营的池子里出
        std::array<Alliance, kPlayers> factions{};
        for (int p = 0; p < kPlayers; ++p) {
            factions[static_cast<std::size_t>(p)] =
                static_cast<Alliance>(next_random(rng) % kAllianceCount);
        }

        std::array<std::vector<int>, kPlayers> decks;
        for (int p = 0; p < kPlayers; ++p) {
            decks[static_cast<std::size_t>(p)] =
                make_deck(rng, factions[static_cast<std::size_t>(p)]);
        }

        Sim sim;
        MatchConfig config;
        config.terrain_variant = static_cast<int>(next_random(rng) % kTerrainVariants);
        config.spawn_seed = next_random(rng);
        // 默认压缩节奏；--shipping 用 MatchConfig 的出厂默认（跟着 types.h 走）
        if (shipping_timing) {
            const MatchConfig factory{};
            config.match_seconds = factory.match_seconds;
            config.hq_unlock_seconds = factory.hq_unlock_seconds;
        } else {
            config.match_seconds = match_seconds;
            config.hq_unlock_seconds = hq_unlock_seconds;
        }
        if (!sim.start(decks, config, factions)) {
            std::printf("开局失败\n");
            return 1;
        }
        std::array<AiBrain, kPlayers> brains;
        for (int p = 0; p < kPlayers; ++p) {
            // 固定种子：每个位置一个固定的 AI"性格"。
            // 之前用随机种子，四个位置的胜率会随种子运气波动（P0 15% vs P3 27%），
            // 那测的是运气不是平衡。
            brains[static_cast<std::size_t>(p)].reset(p,
                                                      0x5EEDu + static_cast<std::uint32_t>(p) * 977u);
        }

        int last_print = -100;
        int first_blood = -1;
        while (sim.phase() == MatchPhase::Playing) {
            for (auto& brain : brains) brain.update(sim);
            sim.advance();
            if (first_blood < 0 && sim.alive_count() < kPlayers) first_blood = sim.tick();
            if (watch && sim.tick() - last_print >= 20) {
                last_print = sim.tick();
                print_watch_line(sim);
            }
            if (sim.tick() > sim.match_ticks() + 10) break;  // 保险丝（跟着本局时限走）
        }
        if (first_blood > 0) {
            stats.total_first_blood += first_blood;
            ++stats.first_blood_count;
        }

        if (watch) {
            std::printf("\n--- 对局 %d 结束 ---\n", index + 1);
            print_watch_line(sim);
            // 僵持局诊断：军队到底有没有走到敌人基地跟前？
            // 一支 100+ 人的军队离最近敌基地还有几千像素，那问题不在数值而在移动/索敌。
            for (int p = 0; p < kPlayers; ++p) {
                const auto& units = sim.player(p).units;
                if (units.empty()) continue;
                long long sum_x = 0;
                long long sum_y = 0;
                for (const auto& unit : units) {
                    sum_x += unit.x;
                    sum_y += unit.y;
                }
                const int cx = static_cast<int>(sum_x / static_cast<long long>(units.size()));
                const int cy = static_cast<int>(sum_y / static_cast<long long>(units.size()));
                int best = -1;
                for (int e = 0; e < kPlayers; ++e) {
                    if (e == p || sim.eliminated(e)) continue;
                    const Point enemy = sim.base_position(e);
                    const int dx = enemy.x - cx;
                    const int dy = enemy.y - cy;
                    const int d =
                        static_cast<int>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
                    if (best < 0 || d < best) best = d;
                }
                if (best < 0) {
                    std::printf("      P%d 军队 %3zu 人 中心(%4d,%4d) 距最近敌基地 无\n", p,
                                units.size(), cx, cy);
                } else {
                    std::printf("      P%d 军队 %3zu 人 中心(%4d,%4d) 距最近敌基地 %d\n", p,
                                units.size(), cx, cy, best);
                }
            }
            std::printf("胜者: %d  原因: %s  回合: %d\n\n", sim.winner(),
                        to_string(sim.reason()), sim.tick());
        } else if (verbose && sim.tick() < 500) {
            std::printf("[短局 %d] t=%d 胜者=%d\n", index + 1, sim.tick(), sim.winner());
        }

        ++stats.matches;
        if (sim.winner() < 0) {
            ++stats.draws;
        } else {
            ++stats.wins[static_cast<std::size_t>(sim.winner())];
            ++stats.alliance_wins[static_cast<std::size_t>(
                sim.player(sim.winner()).alliance)];
        }
        for (int p = 0; p < kPlayers; ++p) {
            ++stats.alliance_games[static_cast<std::size_t>(sim.player(p).alliance)];
        }

        int owned = 0;
        for (const auto& node : sim.nodes()) {
            if (node.owner >= 0) ++owned;
        }
        stats.total_owned_nodes += owned;
        ++stats.owned_samples;
        stats.total_nodes = static_cast<int>(sim.nodes().size());
        stats.total_ticks += sim.tick();
        stats.min_ticks = std::min(stats.min_ticks, sim.tick());
        stats.max_ticks = std::max(stats.max_ticks, sim.tick());
        if (sim.reason() == WinReason::BaseDestroyed) {
            ++stats.reason_base;
        } else {
            ++stats.reason_timeout;
        }
        long long hp_left = 0;
        long long territory_left = 0;
        for (int p = 0; p < kPlayers; ++p) {
            hp_left += sim.base_hp(p);
            territory_left += sim.resources(p).territory;
        }
        stats.total_base_hp_left += hp_left;
        stats.total_territory += territory_left;
        stats.total_hq_calls += sim.hq_calls();
        stats.total_survivors += sim.alive_count();
    }

    const double count = static_cast<double>(stats.matches);
    std::printf("=== %d 局（4 人局）===\n", stats.matches);
    for (int p = 0; p < kPlayers; ++p) {
        std::printf("P%d 胜 %3d (%.1f%%)  ", p, stats.wins[static_cast<std::size_t>(p)],
                    100.0 * stats.wins[static_cast<std::size_t>(p)] / count);
    }
    std::printf("平 %d (%.1f%%)\n", stats.draws, 100.0 * stats.draws / count);
    std::printf("结束原因: 只剩一家 %d (%.1f%%)  超时 %d (%.1f%%)\n", stats.reason_base,
                100.0 * stats.reason_base / count, stats.reason_timeout,
                100.0 * stats.reason_timeout / count);
    std::printf("对局时长: 平均 %.1f 秒  最短 %.1f  最长 %.1f\n",
                stats.total_ticks / count / kTickHz, stats.min_ticks / double(kTickHz),
                stats.max_ticks / double(kTickHz));
    std::printf("结束时存活人数: 平均 %.2f / %d，基地总血量 平均 %.0f\n",
                stats.total_survivors / count, kPlayers,
                stats.total_base_hp_left / count);
    if (stats.first_blood_count > 0) {
        std::printf("首次淘汰: 平均 %.1f 秒（%d/%d 局出现过淘汰）\n",
                    stats.total_first_blood / stats.first_blood_count / double(kTickHz),
                    stats.first_blood_count, stats.matches);
    }
    std::printf("联盟胜率: ");
    for (int a = 0; a < kAllianceCount; ++a) {
        const int games = stats.alliance_games[static_cast<std::size_t>(a)];
        const int wins = stats.alliance_wins[static_cast<std::size_t>(a)];
        std::printf("%s %.1f%% (%d/%d)  ", to_string(static_cast<Alliance>(a)),
                    games == 0 ? 0.0 : 100.0 * wins / games, wins, games);
    }
    std::printf("\n");
    if (stats.owned_samples > 0) {
        std::printf("结束时平均占领资源点: %.1f / %d\n",
                    static_cast<double>(stats.total_owned_nodes) / stats.owned_samples,
                    stats.total_nodes);
        std::printf("结束时平均领地: %.0f / 家，总部支援平均呼叫 %.1f 次\n",
                    static_cast<double>(stats.total_territory) / count / kPlayers,
                    static_cast<double>(stats.total_hq_calls) / count);
    }

    if (!check) return 0;

    // ---- 节奏门禁 ----
    const double avg_seconds = static_cast<double>(stats.total_ticks) / count / kTickHz;
    const double timeout_percent = 100.0 * stats.reason_timeout / count;
    bool ok = true;

    std::printf("\n--- 节奏门禁（下限保险，不是平衡目标）---\n");
    std::printf("平均时长   %6.1f 秒  上限 %d 秒      %s\n", avg_seconds, max_avg_seconds,
                avg_seconds > max_avg_seconds ? "失败" : "通过");
    if (avg_seconds > max_avg_seconds) ok = false;
    std::printf("超时局占比 %6.1f %%   上限 %d %%        %s\n", timeout_percent,
                max_timeout_percent, timeout_percent > max_timeout_percent ? "失败" : "通过");
    if (timeout_percent > max_timeout_percent) ok = false;

    std::printf("%s\n", ok ? "门禁通过" : "门禁失败：对局节奏整体垮了，去看 README 的数值基线");
    return ok ? 0 : 1;
}
