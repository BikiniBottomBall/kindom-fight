#include "rts/terrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>

namespace kingdom_fight::rts {
namespace {

constexpr int index_of(int col, int row) { return row * kCols + col; }

// 稳定的整数哈希：同样的 (x, y, seed) 一定得到同样的噪声值。
std::uint32_t mix(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

int noise_percent(int x, int y, std::uint32_t seed) {
    const std::uint32_t h = mix(static_cast<std::uint32_t>(x) * 73856093u ^
                                static_cast<std::uint32_t>(y) * 19349663u ^ seed);
    return static_cast<int>(h % 100u);
}

// ---- 连贯噪声（值噪声 + 平滑插值 + 多倍频）-----------------------------
// 为什么不用逐格哈希：逐格随机会把边界打成麻点，看起来像电视雪花。
// 值噪声在格点之间做平滑插值，同一块区域天然是连着的，切出来的地形才"成片"。
float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }

float lattice_value(int x, int y, std::uint32_t seed) {
    const std::uint32_t h =
        mix(static_cast<std::uint32_t>(x) * 374761393u ^ static_cast<std::uint32_t>(y) * 668265263u ^ seed);
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

float value_noise(float x, float y, std::uint32_t seed) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = smoothstep01(x - static_cast<float>(xi));
    const float fy = smoothstep01(y - static_cast<float>(yi));
    const float v00 = lattice_value(xi, yi, seed);
    const float v10 = lattice_value(xi + 1, yi, seed);
    const float v01 = lattice_value(xi, yi + 1, seed);
    const float v11 = lattice_value(xi + 1, yi + 1, seed);
    const float top = v00 + (v10 - v00) * fx;
    const float bottom = v01 + (v11 - v01) * fx;
    return top + (bottom - top) * fy;
}

// 多倍频叠加：大尺度决定"哪片是高地/低地"，小尺度只加一点细节
float fbm(float x, float y, std::uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += value_noise(x * frequency, y * frequency,
                           seed + static_cast<std::uint32_t>(i) * 1013u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.5f;
}

// 很小的确定性 RNG（和模拟里用的同一套思路，但这里是独立的，不影响模拟状态）
struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) : state(seed == 0 ? 0x9E3779B9u : seed) {}
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int range(int low, int high) {  // [low, high]
        if (high <= low) return low;
        return low + static_cast<int>(next() % static_cast<std::uint32_t>(high - low + 1));
    }
};

}  // namespace

const char* Terrain::to_string(Tile tile) {
    switch (tile) {
        case Tile::Plain:  return "plain";
        case Tile::Road:   return "road";
        case Tile::Forest: return "forest";
        case Tile::Hill:   return "hill";
        case Tile::Water:  return "water";
        case Tile::Rock:   return "rock";
        case Tile::Count:  break;
    }
    return "unknown";
}

Terrain::Terrain() : Terrain(0, 1) {}

Terrain::Terrain(int variant, std::uint32_t seed)
    : tiles_(static_cast<std::size_t>(kCols * kRows), static_cast<std::uint8_t>(Tile::Plain)) {
    generate(variant, seed == 0 ? 1u : seed);
}

void Terrain::paint(int col, int row, int w, int h, Tile tile) {
    for (int r = row; r < row + h; ++r) {
        if (r < 0 || r >= kRows) continue;
        for (int c = col; c < col + w; ++c) {
            if (c < 0 || c >= kCols) continue;
            tiles_[static_cast<std::size_t>(index_of(c, r))] = static_cast<std::uint8_t>(tile);
        }
    }
}

// 一团不规则的地形：以 (center_col, center_row) 为心，边缘用噪声打碎。
void Terrain::blob(int center_col, int center_row, int radius, Tile tile, std::uint32_t seed,
                   int keep_percent) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) continue;
            const int c = center_col + dx;
            const int r = center_row + dy;
            if (c < 0 || c >= kCols || r < 0 || r >= kRows) continue;
            // 越靠边越容易被噪声削掉，中心基本保留
            const int distance = (dx * dx + dy * dy) * 100 / (radius * radius + 1);
            const int keep = keep_percent - distance / 3;
            if (noise_percent(c, r, seed) >= keep) continue;
            tiles_[static_cast<std::size_t>(index_of(c, r))] = static_cast<std::uint8_t>(tile);
        }
    }
}

void Terrain::generate(int variant, std::uint32_t seed) {
    // 地形 = 两张连贯噪声场：高程（水/丘/岩）+ 湿度（森林），再按阈值切分。
    // 切分之后做两轮 3×3 多数平滑，专门清掉单格噪点。
    const std::uint32_t field_seed = seed ^ 0x5F356495u;
    std::vector<float> wet_field(static_cast<std::size_t>(kCols * kRows), 0.5f);

    float water_level = 0.30f;
    float hill_level = 0.62f;
    float rock_level = 0.75f;
    float forest_wet = 0.54f;
    switch (variant) {
        case 0:  // 秘密森林：森林过半，但留三成以上空地
            water_level = 0.26f;
            hill_level = 0.66f;
            rock_level = 0.79f;
            forest_wet = 0.48f;
            break;
        case 1:  // 双河：更开阔，河另外加
            water_level = 0.26f;
            hill_level = 0.60f;
            rock_level = 0.72f;
            forest_wet = 0.58f;
            break;
        case 2:  // 火山口：中间高、四周低
            water_level = 0.23f;
            hill_level = 0.56f;
            rock_level = 0.70f;
            forest_wet = 0.56f;
            break;
        default:
            break;
    }

    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const float nx = static_cast<float>(c) * 0.055f;
            const float ny = static_cast<float>(r) * 0.055f;
            const float base = fbm(nx, ny, field_seed, 4);
            const float detail = fbm(nx * 2.7f + 17.0f, ny * 2.7f + 9.0f, field_seed ^ 0x9E37u, 2);
            const float elevation = base * 0.80f + detail * 0.20f;
            const float wet = fbm(nx * 1.6f + 41.0f, ny * 1.6f + 23.0f, field_seed ^ 0x51EDu, 3);
            wet_field[static_cast<std::size_t>(index_of(c, r))] = wet;

            Tile tile = Tile::Plain;
            if (elevation < water_level) {
                tile = Tile::Water;
            } else if (elevation > rock_level) {
                tile = Tile::Rock;
            } else if (elevation > hill_level) {
                tile = Tile::Hill;
            } else if (wet > forest_wet) {
                tile = Tile::Forest;
            }
            tiles_[static_cast<std::size_t>(index_of(c, r))] = static_cast<std::uint8_t>(tile);
        }
    }

    // 变体特色
    if (variant == 1) {
        // 两条蜿蜒的河：沿正弦曲线走，宽度 2~3 格
        const int river_a = kCols / 3;
        const int river_b = kCols * 2 / 3;
        for (int r = 0; r < kRows; ++r) {
            const int wobble_a = static_cast<int>(8.0f * std::sin(static_cast<float>(r) * 0.07f + 1.3f));
            const int wobble_b = static_cast<int>(8.0f * std::sin(static_cast<float>(r) * 0.06f + 4.1f));
            paint(river_a + wobble_a, r, 2, 1, Tile::Water);
            paint(river_b + wobble_b, r, 2, 1, Tile::Water);
        }
    } else if (variant == 2) {
        // 火山口：中央岩浆湖 + 一圈岩壁
        const int cx = kCols / 2;
        const int cy = kRows / 2;
        for (int dy = -22; dy <= 22; ++dy) {
            for (int dx = -26; dx <= 26; ++dx) {
                const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                const int c = cx + dx;
                const int r2 = cy + dy;
                if (c < 0 || c >= kCols || r2 < 0 || r2 >= kRows) continue;
                if (distance < 9.0f) {
                    tiles_[static_cast<std::size_t>(index_of(c, r2))] =
                        static_cast<std::uint8_t>(Tile::Water);
                } else if (distance < 13.0f) {
                    tiles_[static_cast<std::size_t>(index_of(c, r2))] =
                        static_cast<std::uint8_t>(Tile::Rock);
                }
            }
        }
    }

    // 两轮 3×3 多数平滑：把孤立的单格噪点并进周围
    const auto smooth_once = [&]() {
        std::vector<std::uint8_t> copy = tiles_;
        for (int r = 1; r < kRows - 1; ++r) {
            for (int c = 1; c < kCols - 1; ++c) {
                int counts[static_cast<int>(Tile::Count)] = {};
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        ++counts[copy[static_cast<std::size_t>(index_of(c + dc, r + dr))]];
                    }
                }
                const int current = copy[static_cast<std::size_t>(index_of(c, r))];
                int best = current;
                for (int t = 0; t < static_cast<int>(Tile::Count); ++t) {
                    if (counts[t] > counts[best]) best = t;
                }
                // 只有明显多数（≥6/9）才改，避免把窄河/窄路抹掉
                if (best != current && counts[best] >= 6) {
                    tiles_[static_cast<std::size_t>(index_of(c, r))] = static_cast<std::uint8_t>(best);
                }
            }
        }
    };
    smooth_once();
    smooth_once();

    // 地图最外圈压成岩石边界：防止单位/建筑贴边，也让地图"有框"
    for (int c = 0; c < kCols; ++c) {
        tiles_[static_cast<std::size_t>(index_of(c, 0))] = static_cast<std::uint8_t>(Tile::Rock);
        tiles_[static_cast<std::size_t>(index_of(c, kRows - 1))] =
            static_cast<std::uint8_t>(Tile::Rock);
    }
    for (int r = 0; r < kRows; ++r) {
        tiles_[static_cast<std::size_t>(index_of(0, r))] = static_cast<std::uint8_t>(Tile::Rock);
        tiles_[static_cast<std::size_t>(index_of(kCols - 1, r))] =
            static_cast<std::uint8_t>(Tile::Rock);
    }
}
int Terrain::open_percent(int x, int y, int radius_tiles) const {
    const int col = x / kTileSize;
    const int row = y / kTileSize;
    int total = 0;
    int open = 0;
    for (int r = row - radius_tiles; r <= row + radius_tiles; ++r) {
        for (int c = col - radius_tiles; c <= col + radius_tiles; ++c) {
            ++total;
            if (c < 0 || c >= kCols || r < 0 || r >= kRows) continue;
            const Tile tile = static_cast<Tile>(tiles_[static_cast<std::size_t>(index_of(c, r))]);
            if (tile != Tile::Water && tile != Tile::Rock) ++open;
        }
    }
    return total == 0 ? 0 : open * 100 / total;
}

bool Terrain::buildable(int x, int y) const {
    const Tile tile = at(x, y);
    return tile == Tile::Plain || tile == Tile::Road;
}

int Terrain::buildable_percent(int x, int y, int radius_tiles) const {
    const int col = x / kTileSize;
    const int row = y / kTileSize;
    int total = 0;
    int usable = 0;
    for (int r = row - radius_tiles; r <= row + radius_tiles; ++r) {
        for (int c = col - radius_tiles; c <= col + radius_tiles; ++c) {
            ++total;
            if (c < 0 || c >= kCols || r < 0 || r >= kRows) continue;
            const Tile tile = static_cast<Tile>(tiles_[static_cast<std::size_t>(index_of(c, r))]);
            if (tile == Tile::Plain || tile == Tile::Road) ++usable;
        }
    }
    return total == 0 ? 0 : usable * 100 / total;
}

bool Terrain::pick_spawns(std::uint32_t seed, const Terrain& terrain, Point out[kPlayers]) {
    return pick_spawns(seed, terrain, std::vector<SpawnAvoid>{}, out);
}

bool Terrain::pick_spawns(std::uint32_t seed, const Terrain& terrain,
                          const std::vector<SpawnAvoid>& avoid, Point out[kPlayers]) {
    // 纯随机降落点：不再按象限分，只看"能不能发育 + 离片区够远 + 四家互相拉开"。
    // 片区（城市/高资源）由 place_nodes 先放好，这里用 avoid 列表把它们圈出来。
    Rng rng(mix(seed) ^ 0x9E3779B9u);
    const auto clear_of_zones = [&](int x, int y) {
        for (const auto& zone : avoid) {
            const int need = zone.radius + 300;   // 降落点不能压在片区上（留 300 给基地清场）
            const int dx = x - zone.x;
            const int dy = y - zone.y;
            if (dx * dx + dy * dy < need * need) return false;
        }
        return true;
    };

    // 三轮放宽：先找又开阔又能建的地方，找不到再退一步
    const int limits[3][2] = {{72, 48}, {64, 40}, {56, 34}};
    for (int i = 0; i < kPlayers; ++i) {
        int best_x = -1;
        int best_y = -1;
        int best_score = -1;
        for (int round = 0; round < 3 && best_x < 0; ++round) {
            for (int attempt = 0; attempt < 6000; ++attempt) {
                const int col = rng.range(10, kCols - 11);
                const int row = rng.range(9, kRows - 10);
                const int x = col * kTileSize + kTileSize / 2;
                const int y = row * kTileSize + kTileSize / 2;
                if (!terrain.passable(x, y)) continue;
                if (!clear_of_zones(x, y)) continue;
                const int open = terrain.open_percent(x, y, 12);
                const int buildable = terrain.buildable_percent(x, y, 12);
                if (open < limits[round][0] || buildable < limits[round][1]) continue;
                bool blocked = false;
                for (int j = 0; j < i; ++j) {
                    const int dx = out[j].x - x;
                    const int dy = out[j].y - y;
                    if (dx * dx + dy * dy < 1300 * 1300) {
                        blocked = true;
                        break;
                    }
                }
                if (blocked) continue;
                const int score = buildable * 2 + open + static_cast<int>(rng.range(0, 25));
                if (score > best_score) {
                    best_score = score;
                    best_x = x;
                    best_y = y;
                }
            }
        }
        if (best_x < 0) {
            // 兜底：网格扫描（比随机更稳，保证极端种子也能开局）
            for (int row = 10; row < kRows - 10 && best_x < 0; row += 4) {
                for (int col = 10; col < kCols - 10; ++col) {
                    const int x = col * kTileSize + kTileSize / 2;
                    const int y = row * kTileSize + kTileSize / 2;
                    if (!terrain.passable(x, y)) continue;
                    if (!clear_of_zones(x, y)) continue;
                    if (terrain.open_percent(x, y, 12) < 40) continue;
                    if (terrain.buildable_percent(x, y, 12) < 30) continue;
                    bool near = false;
                    for (int j = 0; j < i; ++j) {
                        const int dx = out[j].x - x;
                        const int dy = out[j].y - y;
                        if (dx * dx + dy * dy < 950 * 950) {
                            near = true;
                            break;
                        }
                    }
                    if (near) continue;
                    best_x = x;
                    best_y = y;
                    break;
                }
            }
        }
        if (best_x < 0) return false;
        out[i] = Point{best_x, best_y};
    }
    return true;
}
void Terrain::clear_area(const Point& center, int radius_tiles) {
    const int col = center.x / kTileSize;
    const int row = center.y / kTileSize;
    for (int r = row - radius_tiles; r <= row + radius_tiles; ++r) {
        for (int c = col - radius_tiles; c <= col + radius_tiles; ++c) {
            if (c < 0 || c >= kCols || r < 0 || r >= kRows) continue;
            const auto index = static_cast<std::size_t>(index_of(c, r));
            const Tile tile = static_cast<Tile>(tiles_[index]);
            // 基地周围清成空地：水/石要清（不然出兵点可能不可走），
            // 森林/高地也清（"可建造的空地"才够放塔）
            if (tile != Tile::Plain) {
                tiles_[index] = static_cast<std::uint8_t>(Tile::Plain);
            }
        }
    }
}

void Terrain::clear_obstacle(int col, int row) {
    if (col < 0 || col >= kCols || row < 0 || row >= kRows) return;
    const auto index = static_cast<std::size_t>(index_of(col, row));
    const Tile tile = static_cast<Tile>(tiles_[index]);
    if (tile == Tile::Forest || tile == Tile::Rock || tile == Tile::Hill) {
        tiles_[index] = static_cast<std::uint8_t>(Tile::Plain);
    }
}

bool Terrain::clear_obstacle_near(int x, int y, int radius_px) {
    const int center_col = x / kTileSize;
    const int center_row = y / kTileSize;
    const int tiles = std::max(1, radius_px / kTileSize);
    int best = 0x7FFFFFFF;
    int best_col = -1;
    int best_row = -1;
    for (int dr = -tiles; dr <= tiles; ++dr) {
        for (int dc = -tiles; dc <= tiles; ++dc) {
            const int c = center_col + dc;
            const int r = center_row + dr;
            if (c < 0 || c >= kCols || r < 0 || r >= kRows) continue;
            const Tile tile = static_cast<Tile>(tiles_[static_cast<std::size_t>(index_of(c, r))]);
            if (tile != Tile::Forest && tile != Tile::Rock) continue;
            const int dx = (c * kTileSize + kTileSize / 2) - x;
            const int dy = (r * kTileSize + kTileSize / 2) - y;
            const int d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                best_col = c;
                best_row = r;
            }
        }
    }
    if (best_col < 0) return false;
    clear_obstacle(best_col, best_row);
    return true;
}

void Terrain::carve_line(const Point& a, const Point& b) {
    // 直接沿直线开一条 2 格宽的路：不追求好看，只保证一定能走通
    const int steps = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)) / (kTileSize / 2) + 1;
    for (int i = 0; i <= steps; ++i) {
        const int x = a.x + (b.x - a.x) * i / steps;
        const int y = a.y + (b.y - a.y) * i / steps;
        const int col = x / kTileSize;
        const int row = y / kTileSize;
        paint(col, row, 2, 1, Tile::Road);
        paint(col, row + 1, 2, 1, Tile::Road);
    }
}

void Terrain::ensure_connected(const Point* spawns, int count) {
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (reaches(spawns[i].x, spawns[i].y, spawns[j].x, spawns[j].y)) continue;
            carve_line(spawns[i], spawns[j]);
        }
    }
}

Tile Terrain::cell(int col, int row) const {
    if (col < 0 || col >= kCols || row < 0 || row >= kRows) return Tile::Rock;
    return static_cast<Tile>(tiles_[static_cast<std::size_t>(index_of(col, row))]);
}

Tile Terrain::at(int x, int y) const {
    if (x < 0 || y < 0) return Tile::Rock;  // 界外当实心，挡单位也挡测试
    return cell(x / kTileSize, y / kTileSize);
}

bool Terrain::passable(int x, int y) const {
    const Tile tile = at(x, y);
    return tile != Tile::Water && tile != Tile::Rock;
}

int Terrain::speed_percent(int x, int y) const {
    switch (at(x, y)) {
        case Tile::Road:   return 125;
        case Tile::Forest: return 70;
        case Tile::Hill:   return 85;
        default:           return 100;
    }
}

int Terrain::damage_percent(int x, int y) const {
    return at(x, y) == Tile::Hill ? 125 : 100;
}

bool Terrain::reaches(int ax, int ay, int bx, int by) const {
    const int start_col = ax / kTileSize;
    const int start_row = ay / kTileSize;
    const int goal_col = bx / kTileSize;
    const int goal_row = by / kTileSize;

    if (!passable(ax, ay) || !passable(bx, by)) return false;
    if (start_col == goal_col && start_row == goal_row) return true;

    std::vector<char> seen(static_cast<std::size_t>(kCols * kRows), 0);
    std::deque<std::array<int, 2>> queue;
    queue.push_back({start_col, start_row});
    seen[static_cast<std::size_t>(index_of(start_col, start_row))] = 1;

    constexpr int kDx[4] = {1, -1, 0, 0};
    constexpr int kDy[4] = {0, 0, 1, -1};

    while (!queue.empty()) {
        const auto current = queue.front();
        queue.pop_front();
        if (current[0] == goal_col && current[1] == goal_row) return true;

        for (int i = 0; i < 4; ++i) {
            const int c = current[0] + kDx[i];
            const int r = current[1] + kDy[i];
            if (c < 0 || c >= kCols || r < 0 || r >= kRows) continue;
            const auto flat = static_cast<std::size_t>(index_of(c, r));
            if (seen[flat] != 0) continue;
            const Tile tile = static_cast<Tile>(tiles_[flat]);
            if (tile == Tile::Water || tile == Tile::Rock) continue;
            seen[flat] = 1;
            queue.push_back({c, r});
        }
    }
    return false;
}

}  // namespace kingdom_fight::rts
