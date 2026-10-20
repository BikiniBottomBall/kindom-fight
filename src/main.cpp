// S2 demo：打印初始局面 → 12 回合空转 → 打印最终局面与事件统计。
//
// 这个 main 是临时的验证壳。S10 会用真正的终端客户端（client/cli/）替掉它。
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdio>

#include "common/config.h"
#include "logic/engine.h"
#include "logic/report.h"

int main() {
#ifdef _WIN32
    // 让 Windows 控制台按 UTF-8 显示中文
    SetConsoleOutputCP(CP_UTF8);
#endif

    using namespace kingdom_fight;

    GameConfig cfg{};
    constexpr std::uint64_t kSeed = 20240101ull;
    GameEngine engine(cfg, kMaxPlayers, kSeed);

    std::printf("========== 初始局面 ==========\n");
    print_game_state(engine.state());

    std::printf("========== 12 回合空转 ==========\n");
    for (int t = 0; t < cfg.max_turns; ++t) {
        for (PlayerId p = 0; p < engine.state().player_count; ++p) {
            engine.submit(p, {});  // 全部略过：这一步只验证回合循环不崩、事件可打印
        }
        const auto events = engine.resolve_turn();
        print_events(events);
        if (engine.state().phase == Phase::Finished) {
            break;
        }
    }

    std::printf("\n========== 最终局面 ==========\n");
    print_game_state(engine.state());
    print_event_summary(engine.state().log);

    return engine.state().phase == Phase::Finished ? 0 : 1;
}
