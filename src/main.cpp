// demo：4 个 AI 打完整局，打印战报与最终局面。
//
// 这是临时的验证壳：S10 会用真正的终端客户端（client/cli/）替掉它。
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdio>

#include "ai/simple_ai.h"
#include "common/config.h"
#include "logic/engine.h"
#include "logic/report.h"

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // 让 Windows 控制台按 UTF-8 显示中文
#endif

    using namespace kingdom_fight;

    GameConfig cfg{};
    constexpr std::uint64_t kSeed = 20240101ull;
    GameEngine engine(cfg, kMaxPlayers, kSeed);

    std::printf("========== 初始局面 ==========\n");
    print_game_state(engine.state());

    std::printf("========== 对局（4 个 AI） ==========\n");
    for (int t = 0; t < cfg.max_turns; ++t) {
        for (PlayerId p = 0; p < engine.state().player_count; ++p) {
            engine.submit(p, ai::make_orders(engine, p));
        }
        print_events(engine.resolve_turn());
        if (engine.state().phase == Phase::Finished) {
            break;
        }
    }

    std::printf("\n========== 最终局面 ==========\n");
    print_game_state(engine.state());
    print_event_summary(engine.state().log);

    return engine.state().phase == Phase::Finished ? 0 : 1;
}
