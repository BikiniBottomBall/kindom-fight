// 地形：40×26 的格子地图（每格 80×80 = 3200×2080），决定单位能走哪、走多快、站在哪打得疼。
//
// 和上一版的区别：
//   · 地图是**种子生成**的，湖泊/河流/林地都带噪声边缘，不再做四向镜像 —— 看起来自然。
//   · 降落点不再是对称阵型，而是每局随机挑 4 个满足约束的点（能站、够远、周围开阔、彼此连通）。
//   · 公平性不再靠对称保证，而是靠"约束 + 清场 + 必要时修路"。
#pragma once

#include <cstdint>
#include <vector>

#include "rts/types.h"

namespace kingdom_fight::rts {

enum class Tile : std::uint8_t {
    Plain = 0,  // 平地
    Road,       // 道路：移速 +25%
    Forest,     // 森林：移速 -30%
    Hill,       // 高地：站在上面伤害 +25%
    Water,      // 水域：不可通行
    Rock,       // 岩石：不可通行
    Count,
};

inline constexpr int kTileSize = 20;  // 砖块再细一档：40→20，格子 240×160=38400
inline constexpr int kCols = 240;
inline constexpr int kRows = 160;

inline constexpr int kTerrainVariants = 3;

// 地图名（第一张叫「秘密森林」，界面和文档里用它）
inline constexpr const char* kTerrainVariantNames[kTerrainVariants] = {
    "秘密森林", "双河渡口", "火山口"};

class Terrain {
public:
    Terrain();
    // variant + seed 决定整张图。同样的输入一定生成同样的地图。
    Terrain(int variant, std::uint32_t seed);

    [[nodiscard]] Tile at(int x, int y) const;
    [[nodiscard]] Tile cell(int col, int row) const;

    [[nodiscard]] bool passable(int x, int y) const;
    [[nodiscard]] int speed_percent(int x, int y) const;   // 100 = 正常
    [[nodiscard]] int damage_percent(int x, int y) const;  // 站在高地上的加成

    [[nodiscard]] const std::vector<std::uint8_t>& raw() const { return tiles_; }

    // 测试用：从 (ax,ay) 出发能否走到 (bx,by)
    [[nodiscard]] bool reaches(int ax, int ay, int bx, int by) const;

    // 把某个点周围的水/石头清成平地（降落点、资源点用）
    void clear_area(const Point& center, int radius_tiles);
    // 把一格森林/岩石变成平地（水和界外不动）
    void clear_obstacle(int col, int row);
    // 找半径内最近的一格障碍清掉，成功返回 true
    bool clear_obstacle_near(int x, int y, int radius_px);

    // 出生点要避开的区域（城市/高资源/普通片区）
    struct SpawnAvoid {
        int x = 0;
        int y = 0;
        int radius = 0;
    };
    // 随机挑 4 个降落点：能站、两两够远、周围开阔、避开片区。失败返回 false（调用方兜底）。
    [[nodiscard]] static bool pick_spawns(std::uint32_t seed, const Terrain& terrain,
                                          Point out[kPlayers]);
    [[nodiscard]] static bool pick_spawns(std::uint32_t seed, const Terrain& terrain,
                                          const std::vector<SpawnAvoid>& avoid,
                                          Point out[kPlayers]);

    // 保证四个降落点互相连通：不连通就沿直线开一条路。
    void ensure_connected(const Point* spawns, int count);

    // 某个点周围 radius 格内的可通行比例（0~100）
    [[nodiscard]] int open_percent(int x, int y, int radius_tiles) const;
    // "可建造的空地"：只有平地/道路能放塔和建筑（森林、高地、水、石头都不行）
    [[nodiscard]] bool buildable(int x, int y) const;
    [[nodiscard]] int buildable_percent(int x, int y, int radius_tiles) const;

    [[nodiscard]] static const char* to_string(Tile tile);

private:
    void generate(int variant, std::uint32_t seed);
    void paint(int col, int row, int w, int h, Tile tile);
    void blob(int center_col, int center_row, int radius, Tile tile, std::uint32_t seed,
              int keep_percent);
    void carve_line(const Point& a, const Point& b);

    std::vector<std::uint8_t> tiles_;
};

}  // namespace kingdom_fight::rts
