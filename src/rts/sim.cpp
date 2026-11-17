#include "rts/sim.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <queue>

namespace kingdom_fight::rts {

namespace {
constexpr int kMaskCols = kCols / kFootprintTiles;   // 领地掩码：60 列
constexpr int kMaskRows = kRows / kFootprintTiles;   // 40 行
// 建筑/塔的占地块（单位：地板格）
struct BlockRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

bool blocks_overlap(const BlockRect& a, const BlockRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
}  // namespace
namespace {

constexpr int kTowerDmg = 22;
constexpr int kTowerAtkTicks = 10;  // 0.5s
constexpr int kTowerRange = 280;
constexpr int kTowerOffsetPercent = 55;  // 沿"基地→中心"方向走 55%
constexpr int kTowerSpread = 130;

constexpr int kBaseDmg = 34;
constexpr int kBaseAtkTicks = 12;
constexpr int kBaseRange = 230;

// ---- 产出节奏 ------------------------------------------------------------
// 基地有基础产出（饿不死），建筑把对应资源翻几倍。
// 目标是"前 20~30 秒把两三个建筑摆下去，之后主要靠打架"。
constexpr int kBaseManpowerTicks = 10;  // 2.0 / 秒
constexpr int kBaseMaterielTicks = 11;  // 1.8 / 秒
constexpr int kBaseEnergyTicks = 16;    // 1.3 / 秒（略高一点，让不建能源站的人也能放个技能）
constexpr int kBaseTechTicks = 100;     // 0.2 / 秒

// 生产节奏整体调慢：建筑要更久才回本，开局不能立刻铺满
constexpr int kDepotTicks = 11;    // 物资 ≈1.8/秒（原来 3.3）
constexpr int kReactorTicks = 18;  // 能源 ≈1.1/秒（原来 2）
constexpr int kLabTicks = 26;      // 科技 ≈0.77/秒（原来 1.8）
constexpr int kBarracksTicks = 4;  // 人力 5/秒

constexpr int kFieldMargin = 60;

// 120 秒后资源产出翻倍：四人局最后两家经常互相龟，不加这一条就变成
// "两边都拆不动"的超时局（实测 29% 超时）。
constexpr int kLateGameTick = 20 * 120;

// 大地图的行军速度补偿：地图大了 1.4 倍，单位不提速就变成"全程在赶路"。
constexpr int kMarchSpeedPercent = 100;

constexpr std::array<int, 8> kTrainDx{0, 60, 84, 60, 0, -60, -84, -60};
constexpr std::array<int, 8> kTrainDy{-84, -60, 0, 60, 84, 60, 0, -60};

constexpr std::array<int, 4> kFormX{0, -30, 30, -30};
constexpr std::array<int, 4> kFormY{0, -24, 24, 24};

[[nodiscard]] int isqrt(int value) {
    if (value <= 0) return 0;
    int x = value;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + value / x) / 2;
    }
    return x;
}

[[nodiscard]] int dist2(int ax, int ay, int bx, int by) {
    const int dx = ax - bx;
    const int dy = ay - by;
    return dx * dx + dy * dy;
}

// 放资源点用的确定性 RNG（和模拟本身的状态无关，只在地图生成时跑一次）
struct NodeRng {
    std::uint32_t state;
    explicit NodeRng(std::uint32_t seed) : state(seed == 0 ? 0x9E3779B9u : seed) {}
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int range(int low, int high) {
        if (high <= low) return low;
        return low + static_cast<int>(next() % static_cast<std::uint32_t>(high - low + 1));
    }
};

// 资源/战略片区分布 —— 地图生成的一部分：
//   4 座城市据点（四角均匀）+ 4 个高资源片区（四边中点）+ 12 个普通片区（4×3 网格）＝ 20 个。
// 每个片区中心清一块广场（平地），保证能建、能走、能守；
// 出生点在片区放完之后再随机挑，并避开这些片区（见 Terrain::pick_spawns 的 avoid 参数）。
void place_nodes(std::uint32_t seed, Terrain& terrain, std::vector<ResourceNode>& nodes) {
    nodes.clear();
    NodeRng rng(seed ^ 0x51ED270Bu);

    const auto far_from_nodes = [&](int x, int y, int radius, int extra) {
        for (const auto& node : nodes) {
            const int need = radius + node.radius + extra;
            if (dist2(x, y, node.x, node.y) < need * need) return false;
        }
        return true;
    };

    // 12 个方向的单位圆近似：判断"这一圈是不是都还能走"
    static constexpr int kRingDirs[12][2] = {
        {10, 0}, {9, 3}, {7, 7}, {3, 9}, {0, 10}, {-3, 9},
        {-7, 7}, {-9, 3}, {-10, 0}, {-9, -3}, {-7, -7}, {-3, -9},
    };
    const auto ground_ok = [&](int x, int y, int radius) {
        if (!terrain.passable(x, y)) return false;
        const int rings[3] = {radius / 2, radius * 3 / 4, radius};
        for (const int ring : rings) {
            int passable = 0;
            for (const auto& dir : kRingDirs) {
                const int sx = x + ring * dir[0] / 10;
                const int sy = y + ring * dir[1] / 10;
                if (terrain.passable(sx, sy)) ++passable;
            }
            if (passable < 9) return false;
        }
        return true;
    };

    const auto place_at = [&](int x, int y, NodeKind kind, int radius, int claim_tiles,
                              int plaza_tiles, int spacing) -> bool {
        const int margin_x = std::max(5 * kTileSize, radius + 3 * kTileSize);
        const int margin_y = std::max(4 * kTileSize, radius + 2 * kTileSize);
        if (x < margin_x || x > kFieldW - margin_x) return false;
        if (y < margin_y || y > kFieldH - margin_y) return false;
        if (!ground_ok(x, y, radius)) return false;
        if (!far_from_nodes(x, y, radius, spacing)) return false;
        terrain.clear_area(Point{x, y}, plaza_tiles);
        ResourceNode node;
        node.kind = kind;
        node.x = x;
        node.y = y;
        node.radius = radius;
        node.claim_radius_tiles = claim_tiles;
        if (kind == NodeKind::Town) {
            node.core_hp = kTownCoreHp;
            node.core_max = kTownCoreHp;
        }
        nodes.push_back(node);
        return true;
    };

    // 锚点 + 抖动：放不下就把抖动范围放大，再不行全图随机搜（保证数量固定）
    const auto place_slot = [&](int anchor_x, int anchor_y, NodeKind kind, int radius,
                                int claim_tiles, int plaza_tiles, int spacing) {
        for (int attempt = 0; attempt < 900; ++attempt) {
            const int spread = 140 + attempt / 6;
            const int x = anchor_x + rng.range(-spread, spread);
            const int y = anchor_y + rng.range(-spread * 3 / 4, spread * 3 / 4);
            if (place_at(x, y, kind, radius, claim_tiles, plaza_tiles, spacing)) return;
        }
        // 兜底：全图随机（仍然要满足地面/间距约束）
        for (int attempt = 0; attempt < 4000; ++attempt) {
            const int x = rng.range(4 * kTileSize, kFieldW - 4 * kTileSize);
            const int y = rng.range(3 * kTileSize, kFieldH - 3 * kTileSize);
            if (place_at(x, y, kind, radius, claim_tiles, plaza_tiles, spacing)) return;
        }
    };

    // ---- 1. 城市据点：四角均匀（地图 28% / 72% 位置）--------------------------
    static constexpr int kCityAnchors[4][2] = {{30, 32}, {70, 32}, {30, 70}, {70, 70}};
    for (const auto& anchor : kCityAnchors) {
        place_slot(kFieldW * anchor[0] / 100, kFieldH * anchor[1] / 100,
                   NodeKind::Town, kTownZoneRadius, kTownClaimTiles, 7, 260);
    }

    // ---- 2. 高资源片区：四边中点 ----------------------------------------------
    static constexpr int kRichAnchors[4][2] = {{50, 22}, {80, 50}, {50, 78}, {20, 50}};
    for (const auto& anchor : kRichAnchors) {
        place_slot(kFieldW * anchor[0] / 100, kFieldH * anchor[1] / 100,
                   NodeKind::Rich, kRichZoneRadius, 12, 4, 220);
    }

    // ---- 3. 普通片区：4×3 网格均匀铺开 ----------------------------------------
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int anchor_x = kFieldW * (12 + col * 25) / 100;
            const int anchor_y = kFieldH * (18 + row * 30) / 100;
            const NodeKind kind = ((col + row) % 2 == 0) ? NodeKind::Materiel : NodeKind::Energy;
            place_slot(anchor_x, anchor_y, kind, kSmallZoneRadius, 8, 2, 150);
        }
    }
}

}  // namespace

const char* to_string(PlayResult result) {
    switch (result) {
        case PlayResult::Ok:                return "ok";
        case PlayResult::NotPlaying:        return "not_playing";
        case PlayResult::BadSlot:           return "bad_slot";
        case PlayResult::NotEnoughResources: return "not_enough_resources";
        case PlayResult::BadPlacement:      return "bad_placement";
        case PlayResult::TooManyUnits:      return "too_many_units";
        case PlayResult::TooManyBuildings:  return "too_many_buildings";
        case PlayResult::UnknownCard:       return "unknown_card";
        case PlayResult::Locked:            return "locked";
        case PlayResult::Cooldown:          return "cooldown";
    }
    return "unknown";
}

bool Sim::start(const std::array<std::vector<int>, kPlayers>& decks, const MatchConfig& config,
                const std::array<Alliance, kPlayers>& factions) {
    for (int p = 0; p < kPlayers; ++p) {
        const auto& deck = decks[static_cast<std::size_t>(p)];
        if (static_cast<int>(deck.size()) != kDeckSize) return false;
        std::array<int, kCardCount> copies{};
        for (const int id : deck) {
            if (id < 0 || id >= kCardCount) return false;
            // 阵营专属卡池：拿了别家的卡直接判非法，不给"混搭"的口子
            if (card_def(id).alliance != factions[static_cast<std::size_t>(p)]) return false;
            // 同一种卡最多带 2 张
            if (++copies[static_cast<std::size_t>(id)] > kMaxCardCopies) return false;
        }
    }

    config_ = config;
    if (config_.terrain_variant < 0 || config_.terrain_variant >= kTerrainVariants) {
        config_.terrain_variant = 0;
    }

    // 地图：种子生成 → 先铺片区（城市/高资源/普通）→ 再随机挑 4 个降落点（避开片区）
    terrain_ = Terrain(config_.terrain_variant, config_.spawn_seed);
    place_nodes(config_.spawn_seed, terrain_, nodes_);
    std::vector<Terrain::SpawnAvoid> avoid_zones;
    avoid_zones.reserve(nodes_.size());
    for (const auto& node : nodes_) {
        avoid_zones.push_back(Terrain::SpawnAvoid{node.x, node.y, node.radius});
    }
    if (!Terrain::pick_spawns(config_.spawn_seed, terrain_, avoid_zones, base_pos_.data())) {
        // 兜底：随机挑点失败就退回"四角内缩"，保证一定开得了局
        const int mx = kFieldW / 5;
        const int my = kFieldH / 5;
        base_pos_ = {Point{mx, my}, Point{kFieldW - mx, my}, Point{mx, kFieldH - my},
                     Point{kFieldW - mx, kFieldH - my}};
    }
    // 出生点周围清 5 格（400 像素）：出兵半径 640，别让玩家一点就"不能放在这里"
    for (int p = 0; p < kPlayers; ++p) terrain_.clear_area(base_pos_[static_cast<std::size_t>(p)], 20);
    terrain_.ensure_connected(base_pos_.data(), kPlayers);

    // 领地：开局每家一块一样大的基地领地（半径 12 格 ≈ 240 像素），之后靠占点扩张
    tile_owner_.assign(static_cast<std::size_t>(kCols * kRows), static_cast<std::int8_t>(-1));
    for (int p = 0; p < kPlayers; ++p) {
        claim_territory(p, base_pos_[static_cast<std::size_t>(p)].x,
                        base_pos_[static_cast<std::size_t>(p)].y, kInitialTerritoryTiles);
    }
    recount_territory();

    players_ = {};
    effects_.clear();
    tick_ = 0;
    winner_ = -1;
    reason_ = WinReason::None;
    next_unit_id_ = 1;
    next_building_id_ = 1;
    weather_ = Weather::Clear;
    weather_until_ = 20 * 40;  // 开局 40 秒后才有第一次天气变化
    hq_cooldown_ = {0, 0, 0, 0};
    hq_calls_ = 0;
    event_ = RandomEvent::None;
    event_until_ = 0;
    event_target_ = -1;
    next_event_tick_ = kEventFirstTick;

    // 高战略点开局是"暗"的：5 分钟后才亮，逼着中后期去抢
    for (auto& node : nodes_) {
        node.active = node.kind != NodeKind::Rich;
        node.surge_ticks = 0;
    }

    for (int p = 0; p < kPlayers; ++p) {
        PlayerState& state = players_[static_cast<std::size_t>(p)];
        state.alliance = factions[static_cast<std::size_t>(p)];
        state.res = Resources{kStartManpower, kStartMateriel, kStartEnergy, kStartTech,
                              kStartTerritory};
        state.manpower_acc = 0;
        state.materiel_acc = 0;
        state.energy_acc = 0;
        state.tech_acc = 0;
        state.manpower_frac = 0;
        state.materiel_frac = 0;
        state.energy_frac = 0;
        state.tech_frac = 0;
        // 先锋军团：自家基地更硬
        state.base_hp = state.alliance == Alliance::Vanguard
                            ? kBaseHp * kVanguardHpPercent / 100
                            : kBaseHp;
        state.base_cd = 0;
        state.eliminated = false;
        state.rotation = decks[static_cast<std::size_t>(p)];
        state.units.clear();
        state.buildings.clear();
        reset_towers(p);
    }

    // 领地资源要在所有玩家状态初始化之后再算一次（上面的 res 赋值会把开局领地冲掉）
    recount_territory();

    phase_ = MatchPhase::Playing;
    return true;
}

void Sim::reset_towers(int p) {
    // 塔不再开局赠送：全部靠塔卡建。这里只负责清空（重开/回流用）。
    players_[static_cast<std::size_t>(p)].towers.clear();
}

int Sim::tower_cap(int player) const {
    if (player < 0 || player >= kPlayers) return 0;
    const PlayerState& state = players_[static_cast<std::size_t>(player)];
    int cap = kTowerCapBase + state.res.territory / kTowerCapPerTiles;
    if (tree_taken(player, static_cast<int>(TreeNode::EngineeringCitadel))) cap += 1;
    return std::clamp(cap, kTowerCapBase, kTowerCapMax);
}

int Sim::tower_count(int player) const {
    if (player < 0 || player >= kPlayers) return 0;
    return static_cast<int>(players_[static_cast<std::size_t>(player)].towers.size());
}

Tower* Sim::tower_by_id(int player, int id) {
    if (player < 0 || player >= kPlayers) return nullptr;
    auto& towers = players_[static_cast<std::size_t>(player)].towers;
    for (auto& tower : towers) {
        if (tower.id == id) return &tower;
    }
    return nullptr;
}

const Tower* Sim::tower_by_id(int player, int id) const {
    if (player < 0 || player >= kPlayers) return nullptr;
    const auto& towers = players_[static_cast<std::size_t>(player)].towers;
    for (const auto& tower : towers) {
        if (tower.id == id) return &tower;
    }
    return nullptr;
}
bool Sim::can_place(int p, CardKind kind, int x, int y) const {
    if (x < kFieldMargin || x > kFieldW - kFieldMargin) return false;
    if (y < kFieldMargin || y > kFieldH - kFieldMargin) return false;
    if (kind == CardKind::Spell) return true;

    const Point base = base_pos_[static_cast<std::size_t>(p)];
    if (dist2(x, y, base.x, base.y) > kPlaceRadius * kPlaceRadius) return false;
    return terrain_.passable(x, y);
}

bool Sim::can_afford(int player_index, int card_id) const {
    if (player_index < 0 || player_index >= kPlayers) return false;
    if (card_id < 0 || card_id >= kCardCount) return false;
    const CardDef& def = card_def(card_id);
    const Resources& res = players_[static_cast<std::size_t>(player_index)].res;
    // 成长树的"卡牌折扣"在这里生效
    return res.manpower >= card_cost(player_index, def.cost_manpower) &&
           res.materiel >= card_cost(player_index, def.cost_materiel) &&
           res.energy >= card_cost(player_index, def.cost_energy) &&
           res.tech >= card_cost(player_index, def.cost_tech);
}

// 新建筑/塔能不能放：既不能压到别人的建筑/塔，也不能贴到任何基地
bool Sim::structure_blocked(int x, int y, int min_spacing) const {
    const int spacing2 = min_spacing * min_spacing;
    for (const auto& player : players_) {
        for (const auto& building : player.buildings) {
            if (dist2(building.x, building.y, x, y) < spacing2) return true;
        }
        for (const auto& tower : player.towers) {
            if (dist2(tower.x, tower.y, x, y) < spacing2) return true;
        }
    }
    const int base_clear2 = kBaseClearance * kBaseClearance;
    for (const auto& base : base_pos_) {
        if (dist2(base.x, base.y, x, y) < base_clear2) return true;
    }
    return false;
}

Point Sim::footprint_origin(int x, int y) const {
    // 4×4 格：把点击点对齐到"包含它的那一块"的左上角
    int col = x / kTileSize - (kFootprintTiles / 2 - 1);
    int row = y / kTileSize - (kFootprintTiles / 2 - 1);
    const int max_col = kCols - kFootprintTiles;
    const int max_row = kRows - kFootprintTiles;
    col = std::clamp(col, 0, max_col);
    row = std::clamp(row, 0, max_row);
    return Point{col, row};
}

bool Sim::footprint_ok(int player, int x, int y, bool needs_territory) const {
    const Point origin = footprint_origin(x, y);
    // 1) 地形：整块都必须是可建造的平地/道路；领地要求"这一块里至少 6 格是我的"
    //    （和 territory_mask 的规则完全一致，客户端绿格不会再骗人）
    int owned = 0;
    for (int dr = 0; dr < kFootprintTiles; ++dr) {
        for (int dc = 0; dc < kFootprintTiles; ++dc) {
            const int col = origin.x + dc;
            const int row = origin.y + dr;
            const int px = col * kTileSize + kTileSize / 2;
            const int py = row * kTileSize + kTileSize / 2;
            if (!terrain_.buildable(px, py)) return false;
            if (owns_tile(player, px, py)) ++owned;
        }
    }
    if (needs_territory && owned < 6) return false;
    // 2) 和已有建筑/塔的 4×4 块不能相交
    auto block_of = [this](int bx, int by) {
        const Point p = footprint_origin(bx, by);
        return BlockRect{p.x, p.y, kFootprintTiles, kFootprintTiles};
    };
    const BlockRect mine = block_of(x, y);
    for (const auto& other : players_) {
        for (const auto& building : other.buildings) {
            if (blocks_overlap(mine, block_of(building.x, building.y))) return false;
        }
        for (const auto& tower : other.towers) {
            if (blocks_overlap(mine, block_of(tower.x, tower.y))) return false;
        }
    }
    // 3) 基地周围留空：用 6×6 格的块当"基地占位"
    for (const auto& base : base_pos_) {
        const BlockRect base_block{base.x / kTileSize - 3, base.y / kTileSize - 3, 6, 6};
        if (blocks_overlap(mine, base_block)) return false;
    }
    return true;
}

int Sim::territory_version(int player) const {
    if (player < 0 || player >= kPlayers) return 0;
    return territory_version_[static_cast<std::size_t>(player)];
}

void Sim::refresh_territory_mask(int player) {
    auto& mask = territory_masks_[static_cast<std::size_t>(player)];
    mask.assign(static_cast<std::size_t>(kMaskCols * kMaskRows), 0);
    // 逐格采样：一块 4×4 里有 >=6 格是自家领地就算"这块能建塔"
    for (int cell_row = 0; cell_row < kMaskRows; ++cell_row) {
        for (int cell_col = 0; cell_col < kMaskCols; ++cell_col) {
            int owned = 0;
            for (int dr = 0; dr < kFootprintTiles; ++dr) {
                for (int dc = 0; dc < kFootprintTiles; ++dc) {
                    const int col = cell_col * kFootprintTiles + dc;
                    const int row = cell_row * kFootprintTiles + dr;
                    if (owns_tile(player, col * kTileSize + kTileSize / 2,
                                  row * kTileSize + kTileSize / 2)) {
                        ++owned;
                    }
                }
            }
            mask[static_cast<std::size_t>(cell_row * kMaskCols + cell_col)] =
                owned >= 6 ? 1 : 0;
        }
    }
    ++territory_version_[static_cast<std::size_t>(player)];
}

const std::vector<std::uint8_t>& Sim::territory_mask(int player) const {
    static const std::vector<std::uint8_t> empty;
    if (player < 0 || player >= kPlayers) return empty;
    return territory_masks_[static_cast<std::size_t>(player)];
}

PlayResult Sim::play_card(int player, int slot, int x, int y) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;

    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (state.eliminated) return PlayResult::NotPlaying;
    if (slot < 0 || slot >= kHandSize) return PlayResult::BadSlot;
    if (slot >= static_cast<int>(state.rotation.size())) return PlayResult::BadSlot;

    const int card_id = state.rotation[static_cast<std::size_t>(slot)];
    if (card_id < 0 || card_id >= kCardCount) return PlayResult::UnknownCard;

    const CardDef& def = card_def(card_id);
    if (!can_afford(player, card_id)) return PlayResult::NotEnoughResources;
    if (!can_place(player, def.kind, x, y)) return PlayResult::BadPlacement;

    if (def.kind == CardKind::Unit) {
        if (static_cast<int>(state.units.size()) + def.count > kMaxUnits) {
            return PlayResult::TooManyUnits;
        }
    }
    // 建筑上限不再固定：领地越大能盖的越多（4~10 座）
    if (def.kind == CardKind::Building && def.building != BuildingType::Tower &&
        state.building_count() >= building_cap(player)) {
        return PlayResult::TooManyBuildings;
    }
    // 所有建筑/塔：固定 4×4 格占地，整块都要合法（地形 + 不与别的建筑/基地重叠）
    const bool is_tower = def.kind == CardKind::Building && def.building == BuildingType::Tower;
    if (def.kind == CardKind::Building && !footprint_ok(player, x, y, is_tower)) {
        return PlayResult::BadPlacement;
    }
    // 塔：必须盖在自家领地 + 可建造空地（平地/道路）上，而且不能贴脸叠塔
    if (is_tower) {
        if (tower_count(player) >= tower_cap(player)) return PlayResult::TooManyBuildings;
        for (const auto& tower : state.towers) {
            if (dist2(tower.x, tower.y, x, y) < kTowerMinSpacing * kTowerMinSpacing) {
                return PlayResult::BadPlacement;
            }
        }
        for (const auto& other : players_) {
            for (const auto& tower : other.towers) {
                if (dist2(tower.x, tower.y, x, y) < kTowerMinSpacing * kTowerMinSpacing) {
                    return PlayResult::BadPlacement;
                }
            }
        }
    }

    state.res.manpower -= card_cost(player, def.cost_manpower);
    state.res.materiel -= card_cost(player, def.cost_materiel);
    state.res.energy -= card_cost(player, def.cost_energy);
    state.res.tech -= card_cost(player, def.cost_tech);

    switch (def.kind) {
        case CardKind::Unit:     spawn_squad(player, def, x, y); break;
        case CardKind::Building: spawn_building(player, def, x, y); break;
        case CardKind::Spell:    apply_spell(player, def, x, y); break;
    }

    state.rotation.erase(state.rotation.begin() + slot);
    state.rotation.push_back(card_id);
    return PlayResult::Ok;
}

void Sim::spawn_squad(int p, const CardDef& def, int x, int y) {
    PlayerState& state = players_[static_cast<std::size_t>(p)];
    const UnitStat& stat = unit_stat(def.unit);
    for (int i = 0; i < def.count; ++i) {
        const auto index = static_cast<std::size_t>(std::min(i, 3));
        Unit unit;
        unit.id = next_unit_id_++;
        unit.owner = p;
        unit.type = def.unit;
        unit.x = std::clamp(x + kFormX[index], kFieldMargin, kFieldW - kFieldMargin);
        unit.y = std::clamp(y + kFormY[index], kFieldMargin, kFieldH - kFieldMargin);
        unit.hp = stat.hp;
        unit.max_hp = stat.hp;
        unit.cd = 0;
        state.units.push_back(unit);
    }
}

void Sim::spawn_building(int p, const CardDef& def, int x, int y) {
    PlayerState& owner_state = players_[static_cast<std::size_t>(p)];
    if (def.building == BuildingType::Tower) {
        Tower tower;
        tower.owner = p;
        tower.id = next_tower_id_++;
        tower.x = x;
        tower.y = y;
        // 塔统一用 kTowerHp 做基准（卡表里的 420 是旧值，会让塔卡建的塔比原来赠送的塔弱）
        int tower_base_hp = kTowerHp;
        // 先锋军团：建筑和塔 +30%（以前只加在开局赠送的塔上，塔卡建的塔漏了）
        if (owner_state.alliance == Alliance::Vanguard) {
            tower_base_hp = tower_base_hp * kVanguardHpPercent / 100;
        }
        tower.max_hp = tower_base_hp * tower_hp_percent(p) / 100;
        // 和建筑一样先当工地：只有 hp*20% 的血，造完才满血、才会开火
        tower.hp = std::max(1, tower.max_hp * kTowerSiteHpPercent / 100);
        tower.build_total = std::max(1, build_ticks_for(p));
        tower.build_ticks = 0;
        tower.cd = 0;
        owner_state.towers.push_back(tower);
        return;
    }
    PlayerState& state = players_[static_cast<std::size_t>(p)];
    Building building;
    building.id = next_building_id_++;
    building.owner = p;
    building.type = def.building;
    building.x = x;
    building.y = y;
    // 先锋军团：经济建筑也 +30%
    building.hp = state.alliance == Alliance::Vanguard
                      ? def.building_hp * kVanguardHpPercent / 100
                      : def.building_hp;
    building.max_hp = building.hp;
    building.yield_ticks = 0;
    // 落地先是个工地：要 kBuildTicks 才完工，工程兵能加速
    building.build_total = build_ticks_for(p);
    building.build_ticks = 0;
    building.ready = false;
    building.hp = building.max_hp * 30 / 100;  // 工地只有 30% 血，容易被打断
    state.buildings.push_back(building);
}

void Sim::push_effect(int p, const CardDef& def, int x, int y) {
    Effect effect;
    effect.kind = static_cast<std::uint8_t>(def.spell);
    effect.owner = static_cast<std::uint8_t>(p);
    effect.x = x;
    effect.y = y;
    effect.radius = def.radius;
    effect.ttl = 12;
    effects_.push_back(effect);
}

void Sim::apply_spell(int p, const CardDef& def, int x, int y) {
    const int radius2 = def.radius * def.radius;
    PlayerState& self = players_[static_cast<std::size_t>(p)];

    switch (def.spell) {
        case SpellKind::None:
        case SpellKind::Count:
            break;  // 不该走到这里（建筑卡/单位卡没有法术效果）
        // 下面这些是真法术
        case SpellKind::Fire:
        case SpellKind::Frost: {
            for (int e = 0; e < kPlayers; ++e) {
                if (e == p) continue;
                PlayerState& enemy = players_[static_cast<std::size_t>(e)];
                if (enemy.eliminated) continue;
                for (auto& unit : enemy.units) {
                    if (dist2(unit.x, unit.y, x, y) > radius2) continue;
                    if (def.spell == SpellKind::Fire) {
                        unit.hp -= def.amount;
                    } else {
                        unit.slow_ticks = std::max(unit.slow_ticks, def.duration_ticks);
                    }
                }
                if (def.spell == SpellKind::Fire) {
                    for (auto& building : enemy.buildings) {
                        if (dist2(building.x, building.y, x, y) <= radius2) {
                            building.hp -= def.amount;
                        }
                    }
                }
            }
            break;
        }
        case SpellKind::Mend: {
            for (auto& unit : self.units) {
                if (dist2(unit.x, unit.y, x, y) <= radius2) {
                    unit.hp = std::min(unit.max_hp, unit.hp + def.amount);
                }
            }
            break;
        }
        case SpellKind::Clear: {
            const int tiles = std::max(1, def.radius / kTileSize);
            for (int dr = -tiles; dr <= tiles; ++dr) {
                for (int dc = -tiles; dc <= tiles; ++dc) {
                    const int c = x / kTileSize + dc;
                    const int r = y / kTileSize + dr;
                    if (c < 0 || c >= kCols || r < 0 || r >= kRows) continue;
                    const int dx = (c * kTileSize + kTileSize / 2) - x;
                    const int dy = (r * kTileSize + kTileSize / 2) - y;
                    if (dx * dx + dy * dy > def.radius * def.radius) continue;
                    terrain_.clear_obstacle(c, r);
                }
            }
            break;
        }
        case SpellKind::Rally: {
            for (auto& unit : self.units) {
                if (dist2(unit.x, unit.y, x, y) <= radius2) {
                    unit.buff_ticks = std::max(unit.buff_ticks, def.duration_ticks);
                }
            }
            break;
        }
    }

    push_effect(p, def, x, y);
}

bool Sim::pick_building_target(int p, const Unit& unit, Target& out) const {
    int best = 0x7FFFFFFF;
    bool found = false;

    for (int e = 0; e < kPlayers; ++e) {
        if (e == p) continue;
        const PlayerState& enemy = players_[static_cast<std::size_t>(e)];
        if (enemy.eliminated) continue;

        for (std::size_t i = 0; i < enemy.towers.size(); ++i) {
            const Tower& tower = enemy.towers[i];
            if (tower.hp <= 0) continue;
            const int d = dist2(unit.x, unit.y, tower.x, tower.y);
            if (!found || d < best) {
                found = true;
                best = d;
                out.kind = Target::Kind::Tower;
                out.owner = e;
                out.index = tower.id;   // 塔用稳定 id，不用数组下标
                out.x = tower.x;
                out.y = tower.y;
            }
        }

        for (std::size_t i = 0; i < enemy.buildings.size(); ++i) {
            const Building& building = enemy.buildings[i];
            if (building.hp <= 0) continue;
            const int d = dist2(unit.x, unit.y, building.x, building.y);
            if (!found || d < best) {
                found = true;
                best = d;
                out.kind = Target::Kind::Building;
                out.owner = e;
                out.index = static_cast<int>(i);
                out.x = building.x;
                out.y = building.y;
            }
        }

        if (base_exposed(e)) {
            const Point base = base_pos_[static_cast<std::size_t>(e)];
            const int d = dist2(unit.x, unit.y, base.x, base.y);
            if (!found || d < best) {
                found = true;
                best = d;
                out.kind = Target::Kind::Base;
                out.owner = e;
                out.index = 0;
                out.x = base.x;
                out.y = base.y;
            }
        }
    }

    return found;
}

bool Sim::pick_target(int p, const Unit& unit, int aggro, Target& out) const {
    const int aggro2 = aggro * aggro;
    int best = 0x7FFFFFFF;
    bool found = false;

    for (int e = 0; e < kPlayers; ++e) {
        if (e == p) continue;
        const PlayerState& enemy = players_[static_cast<std::size_t>(e)];
        if (enemy.eliminated) continue;
        for (std::size_t i = 0; i < enemy.units.size(); ++i) {
            const Unit& other = enemy.units[i];
            if (other.hp <= 0) continue;
            const int d = dist2(unit.x, unit.y, other.x, other.y);
            if (d <= aggro2 && (!found || d < best)) {
                found = true;
                best = d;
                out.kind = Target::Kind::Unit;
                out.owner = e;
                out.index = static_cast<int>(i);
                out.x = other.x;
                out.y = other.y;
            }
        }
    }

    // 城镇核心：走进片区就打它（这是"据点防守"的核心环节）
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const ResourceNode& node = nodes_[i];
        if (node.kind != NodeKind::Town || node.core_hp <= 0) continue;
        const int d = dist2(unit.x, unit.y, node.x, node.y);
        if (d <= node.radius * node.radius && (!found || d < best)) {
            found = true;
            best = d;
            out.kind = Target::Kind::Node;
            out.owner = -1;
            out.index = static_cast<int>(i);
            out.x = node.x;
            out.y = node.y;
        }
    }
    if (found) return true;

    // 顺路占点：身边 900 像素内有能占的资源点就先去占，没有才去打建筑/基地。
    // 不加这条的话，部队只会往敌方基地直线走，地图上的资源点永远没人抢。
    int node_best = 900 * 900;
    const ResourceNode* chosen = nullptr;
    for (const auto& node : nodes_) {
        if (!node.active || node.owner == p) continue;
        const int d = dist2(unit.x, unit.y, node.x, node.y);
        if (d < node_best) {
            node_best = d;
            chosen = &node;
        }
    }
    if (chosen != nullptr) {
        out.kind = Target::Kind::Node;
        out.owner = chosen->owner;
        out.index = 0;
        out.x = chosen->x;
        out.y = chosen->y;
        return true;
    }

    return pick_building_target(p, unit, out);
}

void Sim::apply_damage(int p, Unit& attacker, const Target& target) {
    (void)p;
    const UnitStat& stat = unit_stat(attacker.type);
    int dmg = stat.dmg;
    if (attacker.buff_ticks > 0) dmg = dmg * 140 / 100;
    dmg = dmg * terrain_.damage_percent(attacker.x, attacker.y) / 100;

    // 打建筑加成：普通单位 130%，攻城车 260%。
    // 130% 是调出来的：150% 时前 20 秒就能推掉一家，100% 时塔又太硬、对局拖到超时。
    // 只有攻城兵能有效拆建筑：普通兵 35%，攻城兵 300%
    const int building_mult = is_siege_unit(attacker.type) ? kSiegeBuildingDamagePercent
                                                          : kNormalBuildingDamagePercent;
    PlayerState& enemy = players_[static_cast<std::size_t>(target.owner)];

    switch (target.kind) {
        case Target::Kind::Node: {
            // 只有城镇核心会被打：普通片区靠近只是"占"
            if (target.index < 0 || target.index >= static_cast<int>(nodes_.size())) return;
            ResourceNode& node = nodes_[static_cast<std::size_t>(target.index)];
            if (node.kind != NodeKind::Town || node.core_hp <= 0) return;
            const int mult = is_siege_unit(attacker.type) ? kSiegeBuildingDamagePercent
                                                          : kNormalBuildingDamagePercent;
            node.core_hp = std::max(0, node.core_hp - dmg * mult / 100);
            return;
        }
        case Target::Kind::Unit: {
            if (target.index < 0 || target.index >= static_cast<int>(enemy.units.size())) return;
            Unit& victim = enemy.units[static_cast<std::size_t>(target.index)];
            dmg = dmg * counter_percent(attacker.type, victim.type) / 100;
            // 攻城器械打兵很弱（40%），专业对口才平衡
            if (is_siege_unit(attacker.type)) dmg = dmg * 40 / 100;
            victim.hp -= std::max(1, dmg);
            break;
        }
        case Target::Kind::Tower: {
            for (auto& tower : enemy.towers) {
                if (tower.id != target.index) continue;
                tower.hp -= dmg * building_mult / 100;
                break;
            }
            break;
        }
        case Target::Kind::Building: {
            if (target.index < 0 || target.index >= static_cast<int>(enemy.buildings.size())) return;
            enemy.buildings[static_cast<std::size_t>(target.index)].hp -= dmg * building_mult / 100;
            break;
        }
        case Target::Kind::Base: {
            enemy.base_hp -= dmg * building_mult / 100;
            break;
        }
    }
}

void Sim::add_materiel(PlayerState& state, int amount) {
    state.res.materiel += amount;
    if (state.alliance != Alliance::Combine) return;
    // +25%：每产出 1 单位攒 25，攒满 100 就多给 1 单位（整数、不丢精度）
    state.materiel_frac += amount * 25;
    while (state.materiel_frac >= 100) {
        state.materiel_frac -= 100;
        ++state.res.materiel;
    }
}

void Sim::add_energy(PlayerState& state, int amount) {
    state.res.energy += amount;
    if (state.alliance != Alliance::Order) return;
    state.energy_frac += amount * 40;
    while (state.energy_frac >= 100) {
        state.energy_frac -= 100;
        ++state.res.energy;
    }
}

void Sim::add_manpower(PlayerState& state, int amount) {
    const int cap = state.manpower_cap();
    if (state.res.manpower >= cap) return;
    state.res.manpower += amount;
    if (state.alliance != Alliance::Swarm) return;
    state.manpower_frac += amount * 25;
    while (state.manpower_frac >= 100 && state.res.manpower < cap) {
        state.manpower_frac -= 100;
        ++state.res.manpower;
    }
}

void Sim::add_tech(PlayerState& state, int amount) {
    state.res.tech += amount;
    if (state.alliance != Alliance::Order) return;
    state.tech_frac += amount * 25;
    while (state.tech_frac >= 100) {
        state.tech_frac -= 100;
        ++state.res.tech;
    }
}

int Sim::match_ticks() const {
    const int seconds = std::clamp(config_.match_seconds, 30, 4 * 3600);
    return seconds * kTickHz;
}

int Sim::hq_unlock_tick() const {
    const int seconds = std::clamp(config_.hq_unlock_seconds, 0, config_.match_seconds);
    return seconds * kTickHz;
}

int Sim::remaining_ticks() const {
    const int total = match_ticks();
    return total > tick_ ? total - tick_ : 0;
}

int Sim::day_phase_percent() const { return (tick_ % kDayCycleTicks) * 100 / kDayCycleTicks; }

bool Sim::is_night() const { return day_phase_percent() >= kNightStartPercent; }

// 射程/索敌的全局修正：夜里看不清、雾里更看不清
int Sim::range_percent() const {
    int percent = 100;
    if (is_night()) percent = percent * 85 / 100;
    if (weather_ == Weather::Fog) percent = percent * 75 / 100;
    return percent;
}

int Sim::weather_speed_percent() const {
    if (weather_ == Weather::Rain) return 90;
    if (weather_ == Weather::Storm) return 95;
    return 100;
}

int Sim::event_ticks_left() const {
    if (event_ == RandomEvent::None) return 0;
    return event_until_ > tick_ ? event_until_ - tick_ : 0;
}

void Sim::tick_events() {
    if (event_ != RandomEvent::None && tick_ >= event_until_) {
        event_ = RandomEvent::None;
        event_target_ = -1;
    }
    if (tick_ < next_event_tick_) return;
    next_event_tick_ = tick_ + kEventIntervalTicks;

    // 确定性随机：同一个 tick + 同一个种子，永远得到同一个事件
    std::uint32_t h = static_cast<std::uint32_t>(tick_) * 2654435761u ^ config_.spawn_seed;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;

    const bool supply = (h % 2u) == 0u;
    if (supply) {
        const int who = static_cast<int>((h >> 8) % static_cast<std::uint32_t>(kPlayers));
        PlayerState& state = players_[static_cast<std::size_t>(who)];
        if (state.eliminated) return;
        state.res.materiel += kSupplyDropMateriel;
        state.res.energy += kSupplyDropEnergy;
        event_ = RandomEvent::SupplyDrop;
        event_target_ = who;
        event_until_ = tick_ + 20 * 8;  // 提示 8 秒
    } else {
        if (nodes_.empty()) return;
        const auto index = static_cast<std::size_t>((h >> 8) % nodes_.size());
        nodes_[index].surge_ticks = kSurgeTicks;
        event_ = RandomEvent::NodeSurge;
        event_target_ = static_cast<int>(index);
        event_until_ = tick_ + kSurgeTicks;
    }
}

void Sim::tick_weather() {
    if (tick_ < weather_until_) return;
    std::uint32_t h = static_cast<std::uint32_t>(tick_) * 2654435761u ^ config_.spawn_seed;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    weather_ = static_cast<Weather>(h % static_cast<std::uint32_t>(Weather::Count));
    const int duration = 40 * kTickHz + static_cast<int>((h >> 9) % static_cast<std::uint32_t>(30 * kTickHz));
    weather_until_ = tick_ + duration;
}

int Sim::hq_state(int p) const {
    if (p < 0 || p >= kPlayers) return 0;
    if (tick_ < hq_unlock_tick()) return 0;
    if (hq_cooldown_[static_cast<std::size_t>(p)] > 0) return 3;
    if (players_[static_cast<std::size_t>(p)].res.territory < config_.hq_cost_territory) return 2;
    return 1;
}

bool Sim::set_preferred_unit(int player, int unit_type) {
    if (player < 0 || player >= kPlayers) return false;
    if (unit_type < 0) {
        players_[static_cast<std::size_t>(player)].preferred_unit = -1;
        return true;
    }
    // 只允许常规兵种（工程兵由工程站训练，巨兽/攻城车太强不进自选）
    switch (static_cast<UnitType>(unit_type)) {
        case UnitType::Militia:
        case UnitType::Spear:
        case UnitType::Shield:
        case UnitType::Rider:
        case UnitType::Archer:
        // 攻城兵种：每个阵营都能在兵营里自选（贵、慢，但拆建筑专业对口）
        case UnitType::Ram:
        case UnitType::Catapult:
        case UnitType::Ballista:
        case UnitType::Cannon:
            players_[static_cast<std::size_t>(player)].preferred_unit = unit_type;
            return true;
        default:
            return false;
    }
}

bool Sim::tree_taken(int player, int node) const {
    if (player < 0 || player >= kPlayers) return false;
    if (node < 0 || node >= kTreeNodeCount) return false;
    return (players_[static_cast<std::size_t>(player)].tree_mask &
            tree_node_bit(static_cast<TreeNode>(node))) != 0;
}

bool Sim::tree_available(int player, int node) const {
    if (player < 0 || player >= kPlayers) return false;
    if (node < 0 || node >= kTreeNodeCount) return false;
    if (tree_taken(player, node)) return false;
    const PlayerState& state = players_[static_cast<std::size_t>(player)];
    const TreeNodeInfo& info = tree_node(static_cast<TreeNode>(node));
    // 前置必须全都有
    if ((state.tree_mask & info.prereq) != info.prereq) return false;
    // 备选前置：要求"任一条分支"，但必须至少有一条
    if (info.any_of != 0 && (state.tree_mask & info.any_of) == 0) return false;
    // 互斥分支：已选过对面就不行
    if ((state.tree_mask & info.conflicts) != 0) return false;
    return true;
}

int Sim::upgrade_cost_for(int player, int node) const {
    (void)player;
    if (node < 0 || node >= kTreeNodeCount) return 0;
    return tree_node(static_cast<TreeNode>(node)).cost;
}

int Sim::unit_hp_percent(int player) const {
    return 100 + (tree_taken(player, static_cast<int>(TreeNode::MilitaryElite)) ? 10 : 0);
}

int Sim::unit_dmg_percent(int player) const {
    return 100 + (tree_taken(player, static_cast<int>(TreeNode::MilitaryFury)) ? 12 : 0);
}

int Sim::tree_bonus_train_speed(int player) const {
    int speed = 100;
    if (tree_taken(player, static_cast<int>(TreeNode::MilitaryDrill))) speed += 10;
    if (tree_taken(player, static_cast<int>(TreeNode::MilitaryMachine))) speed += 15;
    return speed - 100;
}

int Sim::train_ticks_for(int player) const {
    int speed = 100;
    if (tree_taken(player, static_cast<int>(TreeNode::MilitaryDrill))) speed += 10;
    if (tree_taken(player, static_cast<int>(TreeNode::MilitaryMachine))) speed += 15;
    return kTrainTicks * 100 / speed;
}

int Sim::card_cost(int player, int cost) const {
    if (cost <= 0) return 0;
    const int discount = tree_taken(player, static_cast<int>(TreeNode::EconomyTrade)) ? 10 : 0;
    return std::max(1, cost * (100 - discount) / 100);
}

int Sim::build_ticks_for(int player) const {
    const int speed = tree_taken(player, static_cast<int>(TreeNode::EngineeringBasics)) ? 115 : 100;
    return kBuildTicks * 100 / speed;
}

int Sim::engineer_bonus(int player) const {
    return kEngineerBuildBonus +
           (tree_taken(player, static_cast<int>(TreeNode::EngineeringSpeed)) ? 25 : 0);
}

int Sim::building_hp_percent(int player) const {
    return 100 + (tree_taken(player, static_cast<int>(TreeNode::EngineeringFortify)) ? 20 : 0);
}

int Sim::tower_hp_percent(int player) const {
    return 100 + (tree_taken(player, static_cast<int>(TreeNode::EngineeringCitadel)) ? 15 : 0);
}

int Sim::material_percent(int player) const {
    return 100 + (tree_taken(player, static_cast<int>(TreeNode::EconomyDepot)) ? 10 : 0);
}

int Sim::territory_yield_percent(int player) const {
    if (player < 0 || player >= kPlayers) return 100;
    const int territory = players_[static_cast<std::size_t>(player)].res.territory;
    return std::clamp(100 + territory * kTerritoryYieldPercent, 100, 180);
}

bool Sim::is_siege_unit(UnitType type) {
    switch (type) {
        case UnitType::Ram:
        case UnitType::Catapult:
        case UnitType::Ballista:
        case UnitType::Cannon:
        case UnitType::Siege:
            return true;
        default:
            return false;
    }
}

int Sim::energy_percent(int player) const {    return 100 + (tree_taken(player, static_cast<int>(TreeNode::EconomyPower)) ? 15 : 0);
}

PlayResult Sim::upgrade(int player, int node) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;
    if (node < 0 || node >= kTreeNodeCount) return PlayResult::BadSlot;
    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (state.eliminated) return PlayResult::NotPlaying;
    if (!tree_available(player, node)) return PlayResult::Locked;  // 前置没到 / 互斥
    const int cost = upgrade_cost_for(player, node);
    if (state.res.tech < cost) return PlayResult::NotEnoughResources;
    state.res.tech -= cost;
    state.tech_spent += cost;
    state.tree_mask = static_cast<std::uint16_t>(
        state.tree_mask | tree_node_bit(static_cast<TreeNode>(node)));
    return PlayResult::Ok;
}
int Sim::building_cap(int player) const {
    const PlayerState& state = players_[static_cast<std::size_t>(player)];
    const int hub = tree_taken(player, static_cast<int>(TreeNode::EconomyHub)) ? 1 : 0;
    return std::clamp(4 + state.res.territory / 10 + hub, 4, 11);
}

PlayResult Sim::discard(int player, int slot) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;
    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (state.eliminated) return PlayResult::NotPlaying;
    if (slot < 0 || slot >= kHandSize) return PlayResult::BadSlot;
    if (slot >= static_cast<int>(state.rotation.size())) return PlayResult::BadSlot;
    const int card_id = state.rotation[static_cast<std::size_t>(slot)];
    state.rotation.erase(state.rotation.begin() + slot);
    state.rotation.push_back(card_id);
    return PlayResult::Ok;
}

PlayResult Sim::set_building_unit(int player, int index, int unit_type) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;
    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (index < 0 || index >= static_cast<int>(state.buildings.size())) {
        return PlayResult::BadPlacement;
    }
    Building& building = state.buildings[static_cast<std::size_t>(index)];
    if (building.type != BuildingType::Barracks) return PlayResult::BadPlacement;
    if (unit_type < 0) {
        building.train_unit = -1;
        return PlayResult::Ok;
    }
    if (unit_type >= kUnitTypeCount) return PlayResult::BadPlacement;
    const UnitType type = static_cast<UnitType>(unit_type);
    switch (type) {
        case UnitType::Militia:
        case UnitType::Spear:
        case UnitType::Shield:
        case UnitType::Rider:
        case UnitType::Archer:
        case UnitType::Ram:
        case UnitType::Catapult:
        case UnitType::Ballista:
        case UnitType::Cannon:
            building.train_unit = unit_type;
            building.train_ticks = 0;   // 换兵种重新计时
            return PlayResult::Ok;
        default:
            return PlayResult::BadPlacement;
    }
}

PlayResult Sim::set_building_rally(int player, int index, int x, int y) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;
    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (index < 0 || index >= static_cast<int>(state.buildings.size())) {
        return PlayResult::BadPlacement;
    }
    Building& building = state.buildings[static_cast<std::size_t>(index)];
    if (building.type != BuildingType::Barracks) return PlayResult::BadPlacement;
    if (x < 0 || y < 0 || x > kFieldW || y > kFieldH) return PlayResult::BadPlacement;
    building.rally_x = x;
    building.rally_y = y;
    return PlayResult::Ok;
}

PlayResult Sim::build_tower(int player, int x, int y) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;
    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (state.eliminated) return PlayResult::NotPlaying;

    if (tower_count(player) >= tower_cap(player)) return PlayResult::TooManyBuildings;
    if (!footprint_ok(player, x, y, true)) return PlayResult::BadPlacement;
    // 塔和塔之间仍然要留间距（和塔卡一致）
    for (const auto& other : players_) {
        for (const auto& tower : other.towers) {
            if (dist2(tower.x, tower.y, x, y) < kTowerMinSpacing * kTowerMinSpacing) {
                return PlayResult::BadPlacement;
            }
        }
    }
    if (state.res.materiel < kTowerBuildMateriel) return PlayResult::NotEnoughResources;
    if (state.res.energy < kTowerBuildEnergy) return PlayResult::NotEnoughResources;

    const Point origin = footprint_origin(x, y);
    const int center_x = (origin.x + kFootprintTiles / 2) * kTileSize;
    const int center_y = (origin.y + kFootprintTiles / 2) * kTileSize;

    state.res.materiel -= kTowerBuildMateriel;
    state.res.energy -= kTowerBuildEnergy;

    Tower tower;
    tower.owner = player;
    tower.id = next_tower_id_++;
    tower.x = center_x;
    tower.y = center_y;
    int tower_base_hp = kTowerHp;
    if (state.alliance == Alliance::Vanguard) {
        tower_base_hp = tower_base_hp * kVanguardHpPercent / 100;
    }
    tower.max_hp = tower_base_hp * tower_hp_percent(player) / 100;
    tower.hp = std::max(1, tower.max_hp * kTowerSiteHpPercent / 100);
    tower.build_total = std::max(1, build_ticks_for(player));
    tower.build_ticks = 0;
    tower.cd = 0;
    state.towers.push_back(tower);
    return PlayResult::Ok;
}

PlayResult Sim::repair(int player, int kind, int index) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;
    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (state.eliminated) return PlayResult::NotPlaying;

    int* hp = nullptr;
    int* max_hp = nullptr;
    if (kind == 0) {
        if (index < 0 || index >= static_cast<int>(state.buildings.size())) {
            return PlayResult::BadPlacement;
        }
        hp = &state.buildings[static_cast<std::size_t>(index)].hp;
        max_hp = &state.buildings[static_cast<std::size_t>(index)].max_hp;
    } else if (kind == 1) {
        Tower* tower = tower_by_id(player, index);
        if (tower == nullptr) return PlayResult::BadPlacement;
        if (!tower->complete()) return PlayResult::BadPlacement;  // 工地不能修
        hp = &tower->hp;
        max_hp = &tower->max_hp;
    } else {
        return PlayResult::BadPlacement;
    }

    const int missing = *max_hp - *hp;
    if (missing <= 0) return PlayResult::BadPlacement;  // 满血不用修
    // 修得越多越贵：每 20 点血 1 物资，另收 4 点起步价
    const int cost = 4 + missing / 20;
    if (state.res.materiel < cost) return PlayResult::NotEnoughResources;
    state.res.materiel -= cost;
    *hp = *max_hp;
    return PlayResult::Ok;
}
PlayResult Sim::demolish(int player, int kind, int index) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (player < 0 || player >= kPlayers) return PlayResult::NotPlaying;
    PlayerState& state = players_[static_cast<std::size_t>(player)];
    if (state.eliminated) return PlayResult::NotPlaying;

    if (kind == 0) {
        if (index < 0 || index >= static_cast<int>(state.buildings.size())) {
            return PlayResult::BadPlacement;
        }
        state.buildings.erase(state.buildings.begin() + index);
        for (std::size_t i = 0; i < state.buildings.size(); ++i) {
            state.buildings[i].id = static_cast<int>(i) + 1;
        }
        return PlayResult::Ok;
    }
    if (kind == 1) {
        for (std::size_t i = 0; i < state.towers.size(); ++i) {
            if (state.towers[i].id != index) continue;
            // 拆塔直接释放上限；不再强制"至少留一座"（基地本身会防守）
            state.towers.erase(state.towers.begin() + static_cast<std::ptrdiff_t>(i));
            return PlayResult::Ok;
        }
        return PlayResult::BadPlacement;
    }
    return PlayResult::BadPlacement;
}

PlayResult Sim::call_hq(int p, int x, int y) {
    if (phase_ != MatchPhase::Playing) return PlayResult::NotPlaying;
    if (p < 0 || p >= kPlayers) return PlayResult::NotPlaying;
    PlayerState& state = players_[static_cast<std::size_t>(p)];
    if (state.eliminated) return PlayResult::NotPlaying;
    if (tick_ < hq_unlock_tick()) return PlayResult::Locked;
    if (hq_cooldown_[static_cast<std::size_t>(p)] > 0) return PlayResult::Cooldown;
    if (state.res.territory < config_.hq_cost_territory) return PlayResult::NotEnoughResources;

    state.res.territory -= config_.hq_cost_territory;
    hq_cooldown_[static_cast<std::size_t>(p)] = kHqCooldownTicks;
    ++hq_calls_;

    const int radius2 = kHqRadius * kHqRadius;
    for (int e = 0; e < kPlayers; ++e) {
        if (e == p) continue;
        PlayerState& enemy = players_[static_cast<std::size_t>(e)];
        if (enemy.eliminated) continue;

        for (auto& unit : enemy.units) {
            if (dist2(unit.x, unit.y, x, y) > radius2) continue;
            unit.hp -= kHqDamage;
            if (unit.hp < 1) unit.hp = 0;
        }
        for (auto& tower : enemy.towers) {
            if (tower.hp <= 0) continue;
            if (dist2(tower.x, tower.y, x, y) > radius2) continue;
            tower.hp -= kHqDamage;
            if (tower.hp < 1) tower.hp = 0;
        }
        for (auto& building : enemy.buildings) {
            if (dist2(building.x, building.y, x, y) > radius2) continue;
            building.hp -= kHqDamage;
            if (building.hp < 1) building.hp = 0;
        }
        if (base_exposed(e)) {
            const Point base = base_pos_[static_cast<std::size_t>(e)];
            if (dist2(base.x, base.y, x, y) <= radius2) {
                enemy.base_hp -= kHqDamage * 2;
                if (enemy.base_hp < 0) enemy.base_hp = 0;
            }
        }
    }

    Effect effect;
    effect.kind = static_cast<std::uint8_t>(SpellKind::Fire);
    effect.owner = static_cast<std::uint8_t>(p);
    effect.x = x;
    effect.y = y;
    effect.radius = kHqRadius;
    effect.ttl = 60;
    effects_.push_back(effect);
    return PlayResult::Ok;
}

void Sim::tick_production(int p) {
    PlayerState& state = players_[static_cast<std::size_t>(p)];

    // 基地基础产出
    if (++state.manpower_acc >= kBaseManpowerTicks) {
        state.manpower_acc -= kBaseManpowerTicks;
        add_manpower(state, 1);
    }
    if (++state.materiel_acc >= kBaseMaterielTicks) {
        state.materiel_acc -= kBaseMaterielTicks;
        add_materiel(state, 1);
    }
    if (++state.energy_acc >= kBaseEnergyTicks) {
        state.energy_acc -= kBaseEnergyTicks;
        add_energy(state, 1);
    }
    if (++state.tech_acc >= kBaseTechTicks) {
        state.tech_acc -= kBaseTechTicks;
        add_tech(state, 1);
    }

    // 建筑产出
    for (auto& building : state.buildings) {
        ++building.yield_ticks;
        if (!building.ready) continue;  // 工地不产出（建造进度在 tick_construction 里推进）
        switch (building.type) {
            case BuildingType::Depot: {
                const int depot_ticks = std::max(1, kDepotTicks * 100 /
                    (material_percent(p) * territory_yield_percent(p) / 100));
                if (building.yield_ticks >= depot_ticks) {
                    building.yield_ticks -= depot_ticks;
                    add_materiel(state, 1);
                }
                break;
            }
            case BuildingType::Reactor: {
                const int reactor_ticks = std::max(1, kReactorTicks * 100 /
                    (energy_percent(p) * territory_yield_percent(p) / 100));
                if (building.yield_ticks >= reactor_ticks) {
                    building.yield_ticks -= reactor_ticks;
                    add_energy(state, 1);
                }
                break;
            }
            case BuildingType::Barracks: {
                if (building.yield_ticks >= kBarracksTicks) {
                    building.yield_ticks -= kBarracksTicks;
                    add_manpower(state, 1);
                }
                // 兵营训练：兵种 = 这个兵营指定的（没有就用阵营默认），
                // 每个兵种训练时间不同（见 UnitStat::train_ticks）
                const UnitType chosen_type = building.train_unit >= 0
                                                 ? static_cast<UnitType>(building.train_unit)
                                                 : (state.preferred_unit >= 0
                                                        ? static_cast<UnitType>(state.preferred_unit)
                                                        : trained_unit(state.alliance));
                const UnitStat& chosen_stat = unit_stat(chosen_type);
                const int speed = std::max(1, 100 + tree_bonus_train_speed(p));
                const int train_ticks = std::max(1, chosen_stat.train_ticks * 100 / speed);
                if (building.train_ticks < train_ticks) ++building.train_ticks;
                if (building.train_ticks >= train_ticks) {
                    const UnitType type = chosen_type;
                    const UnitStat& stat = unit_stat(type);
                    const bool room = static_cast<int>(state.units.size()) < kMaxUnits;
                    if (room && state.res.manpower >= stat.cost_manpower &&
                        state.res.materiel >= stat.cost_materiel) {
                        state.res.manpower -= stat.cost_manpower;
                        state.res.materiel -= stat.cost_materiel;
                        Unit unit;
                        unit.id = next_unit_id_++;
                        unit.owner = p;
                        unit.type = type;
                        const int slot = static_cast<int>(building.id) % 8;
                        unit.x = std::clamp(building.x + kTrainDx[static_cast<std::size_t>(slot)],
                                            kFieldMargin, kFieldW - kFieldMargin);
                        unit.y = std::clamp(building.y + kTrainDy[static_cast<std::size_t>(slot)],
                                            kFieldMargin, kFieldH - kFieldMargin);
                        const int hp = stat.hp * unit_hp_percent(p) / 100;
                        unit.hp = hp;
                        unit.max_hp = hp;
                        state.units.push_back(unit);
                        building.train_ticks = 0;
                    }
                }
                break;
            }
            case BuildingType::Tower:
                break;  // 塔不产出，塔的攻击在 tick_towers 里
            case BuildingType::Workshop: {
                if (!building.ready) break;
                const int train_ticks = train_ticks_for(p);
                if (building.train_ticks < train_ticks) ++building.train_ticks;
                if (building.train_ticks >= train_ticks) {
                    const UnitStat& stat = unit_stat(UnitType::Engineer);
                    const bool room = static_cast<int>(state.units.size()) < kMaxUnits;
                    if (room && state.res.manpower >= stat.cost_manpower &&
                        state.res.materiel >= stat.cost_materiel) {
                        state.res.manpower -= stat.cost_manpower;
                        state.res.materiel -= stat.cost_materiel;
                        Unit unit;
                        unit.id = next_unit_id_++;
                        unit.owner = p;
                        unit.type = UnitType::Engineer;
                        const int slot = static_cast<int>(building.id) % 8;
                        unit.x = std::clamp(building.x + kTrainDx[static_cast<std::size_t>(slot)],
                                            kFieldMargin, kFieldW - kFieldMargin);
                        unit.y = std::clamp(building.y + kTrainDy[static_cast<std::size_t>(slot)],
                                            kFieldMargin, kFieldH - kFieldMargin);
                        const int hp = stat.hp * unit_hp_percent(p) / 100;
                        unit.hp = hp;
                        unit.max_hp = hp;
                        state.units.push_back(unit);
                        building.train_ticks = 0;
                    }
                }
                break;
            }
            case BuildingType::Lab:
                if (building.yield_ticks >= kLabTicks) {
                    building.yield_ticks -= kLabTicks;
                    add_tech(state, 1);
                }
                break;
            case BuildingType::Count: break;
        }
    }
}

void Sim::tick_towers(int p) {
    if (players_[static_cast<std::size_t>(p)].eliminated) return;
    PlayerState& state = players_[static_cast<std::size_t>(p)];

    // 1) 工地推进：每 tick +1，和建筑一个节奏（工程树/工程兵不影响，塔只认时间）
    for (auto& tower : state.towers) {
        if (tower.complete()) continue;
        ++tower.build_ticks;
        if (tower.complete()) tower.hp = tower.max_hp;  // 完工瞬间补满
    }

    // 2) 完工的塔才开火
    for (auto& tower : state.towers) {
        if (!tower.complete() || tower.hp <= 0) continue;
        if (tower.cd > 0) --tower.cd;

        int best = 0x7FFFFFFF;
        int target_owner = -1;
        int target_index = -1;

        for (int e = 0; e < kPlayers; ++e) {
            if (e == p) continue;
            const PlayerState& enemy = players_[static_cast<std::size_t>(e)];
            if (enemy.eliminated) continue;
            for (std::size_t i = 0; i < enemy.units.size(); ++i) {
                const Unit& unit = enemy.units[i];
                if (unit.hp <= 0) continue;
                const int d = dist2(tower.x, tower.y, unit.x, unit.y);
                const int tower_range = kTowerRange * range_percent() / 100;
                if (d <= tower_range * tower_range && d < best) {
                    best = d;
                    target_owner = e;
                    target_index = static_cast<int>(i);
                }
            }
        }

        if (target_owner >= 0 && target_index >= 0 && tower.cd == 0) {
            players_[static_cast<std::size_t>(target_owner)]
                .units[static_cast<std::size_t>(target_index)]
                .hp -= kTowerDmg;
            tower.cd = kTowerAtkTicks;
        }
    }

    // 3) 被摧毁的塔从数组里删掉：上限随之释放，客户端看 id 消失就播爆炸
    auto& towers = state.towers;
    towers.erase(std::remove_if(towers.begin(), towers.end(),
                                [](const Tower& tower) { return tower.hp <= 0; }),
                 towers.end());
}
void Sim::tick_base(int p) {
    PlayerState& state = players_[static_cast<std::size_t>(p)];
    if (state.eliminated || state.base_hp <= 0) return;
    if (state.base_cd > 0) --state.base_cd;

    const Point base = base_pos_[static_cast<std::size_t>(p)];
    int best = 0x7FFFFFFF;
    int target_owner = -1;
    int target_index = -1;

    for (int e = 0; e < kPlayers; ++e) {
        if (e == p) continue;
        const PlayerState& enemy = players_[static_cast<std::size_t>(e)];
        if (enemy.eliminated) continue;
        for (std::size_t i = 0; i < enemy.units.size(); ++i) {
            const Unit& unit = enemy.units[i];
            if (unit.hp <= 0) continue;
            const int d = dist2(base.x, base.y, unit.x, unit.y);
            if (d <= kBaseRange * kBaseRange && d < best) {
                best = d;
                target_owner = e;
                target_index = static_cast<int>(i);
            }
        }
    }

    if (target_owner >= 0 && target_index >= 0 && state.base_cd == 0) {
        players_[static_cast<std::size_t>(target_owner)]
            .units[static_cast<std::size_t>(target_index)]
            .hp -= kBaseDmg;
        state.base_cd = kBaseAtkTicks;
    }
}

void Sim::move_unit(int p, Unit& unit, int target_x, int target_y, int speed) {
    (void)p;
    const int dx = target_x - unit.x;
    const int dy = target_y - unit.y;
    const int distance = isqrt(dx * dx + dy * dy);
    if (distance <= 0) return;

    if (distance <= speed) {
        if (terrain_.passable(target_x, target_y)) {
            unit.x = target_x;
            unit.y = target_y;
        }
        return;
    }

    int vx = dx * speed / distance;
    int vy = dy * speed / distance;
    if (vx == 0 && vy == 0) {
        if (std::abs(dx) >= std::abs(dy)) {
            vx = (dx > 0) ? 1 : -1;
        } else {
            vy = (dy > 0) ? 1 : -1;
        }
    }

    if (terrain_.passable(unit.x + vx, unit.y + vy)) {
        unit.x += vx;
        unit.y += vy;
    } else if (vx != 0 && terrain_.passable(unit.x + vx, unit.y)) {
        unit.x += vx;
    } else if (vy != 0 && terrain_.passable(unit.x, unit.y + vy)) {
        unit.y += vy;
    } else {
        const int side_x = -vy;
        const int side_y = vx;
        if (terrain_.passable(unit.x + side_x, unit.y + side_y)) {
            unit.x += side_x;
            unit.y += side_y;
        } else if (terrain_.passable(unit.x - side_x, unit.y - side_y)) {
            unit.x -= side_x;
            unit.y -= side_y;
        }
    }

    unit.x = std::clamp(unit.x, kFieldMargin, kFieldW - kFieldMargin);
    unit.y = std::clamp(unit.y, kFieldMargin, kFieldH - kFieldMargin);
}

void Sim::tick_nodes() {
    for (auto& node : nodes_) {
        // 高战略点 5 分钟后点亮；亮之前不能占
        if (!node.active && tick_ >= kRichActivateSeconds * kTickHz) node.active = true;
        if (node.surge_ticks > 0) --node.surge_ticks;

        // 数每个玩家在半径内有多少兵
        int counts[kPlayers] = {0, 0, 0, 0};
        for (int p = 0; p < kPlayers; ++p) {
            const PlayerState& state = players_[static_cast<std::size_t>(p)];
            if (state.eliminated) continue;
            for (const auto& unit : state.units) {
                if (dist2(unit.x, unit.y, node.x, node.y) <= node.radius * node.radius) ++counts[p];
            }
        }

        int leader = -1;
        int leader_count = 0;
        for (int p = 0; p < kPlayers; ++p) {
            if (counts[p] > leader_count) {
                leader_count = counts[p];
                leader = p;
            }
        }

        // 城镇据点：核心没打掉之前不能占；核心打掉后守军炮台也停火
        if (node.kind == NodeKind::Town && node.core_hp > 0) {
            // 守军炮台：每个片区自己的固定炮台，不主动出击，只打进入片区的敌人
            if (node.guard_cd > 0) --node.guard_cd;
            if (node.guard_cd == 0) {
                int best_d = 0x7FFFFFFF;
                int best_p = -1;
                int best_i = -1;
                for (int p = 0; p < kPlayers; ++p) {
                    auto& state = players_[static_cast<std::size_t>(p)];
                    if (state.eliminated) continue;
                    for (std::size_t i = 0; i < state.units.size(); ++i) {
                        const Unit& unit = state.units[i];
                        if (unit.hp <= 0) continue;
                        const int d = dist2(unit.x, unit.y, node.x, node.y);
                        if (d <= kTownGuardRange * kTownGuardRange && d < best_d) {
                            best_d = d;
                            best_p = p;
                            best_i = static_cast<int>(i);
                        }
                    }
                }
                if (best_p >= 0) {
                    players_[static_cast<std::size_t>(best_p)]
                        .units[static_cast<std::size_t>(best_i)]
                        .hp -= kTownGuardDmg;
                    node.guard_cd = kTownGuardTicks;
                }
            }
            node.capturer = -1;
            node.progress = 0;
            continue;
        }

        if (leader >= 0 && leader != node.owner && node.active) {
            if (node.capturer != leader) {
                node.capturer = leader;
                node.progress = 0;
            }
            // 人多占得快，但最多按 2 个兵算，避免"一波兵瞬间翻点"
            node.progress += std::min(leader_count, 2);
            const int need = node.kind == NodeKind::Town ? kTownCaptureTicks : kNodeCaptureTicks;
            if (node.progress >= need) {
                node.owner = leader;
                node.progress = 0;
                node.capturer = -1;
                node.core_hp = 0;
                // 占下一个片区 = 把一整片并进自家领地（据点给得更多）
                claim_territory(leader, node.x, node.y, node.claim_radius_tiles);
                recount_territory();
            }
        } else if (leader < 0) {
            node.capturer = -1;
            if (node.progress > 0) --node.progress;  // 人走了就慢慢退回去
        }

        if (node.owner < 0) continue;
        if (players_[static_cast<std::size_t>(node.owner)].eliminated) {
            node.owner = -1;
            node.progress = 0;
            continue;
        }

        ++node.yield_ticks;
        const int amount = node.surge_ticks > 0 ? 2 : 1;  // 矿脉爆发：产量翻倍
        // 领地现在按"占了多少格"算（见 recount_territory），不再按时间涨
        const int yield_percent = territory_yield_percent(node.owner);
        const int materiel_ticks = std::max(1, kNodeMaterielTicks * 100 / yield_percent);
        if (node.kind == NodeKind::Materiel || node.kind == NodeKind::Rich ||
            node.kind == NodeKind::Town) {
            if (node.yield_ticks % materiel_ticks == 0) {
                add_materiel(players_[static_cast<std::size_t>(node.owner)], amount);
            }
        }
        const int energy_ticks = std::max(1, kNodeEnergyTicks * 100 / yield_percent);
        if (node.kind == NodeKind::Energy || node.kind == NodeKind::Rich ||
            node.kind == NodeKind::Town) {
            if (node.yield_ticks % energy_ticks == 0) {
                add_energy(players_[static_cast<std::size_t>(node.owner)], amount);
            }
        }
    }
}

void Sim::claim_territory(int p, int x, int y, int radius_tiles) {
    if (tile_owner_.size() != static_cast<std::size_t>(kCols * kRows)) return;
    const int col = x / kTileSize;
    const int row = y / kTileSize;
    const int radius2 = radius_tiles * radius_tiles;
    for (int r = row - radius_tiles; r <= row + radius_tiles; ++r) {
        if (r < 0 || r >= kRows) continue;
        for (int c = col - radius_tiles; c <= col + radius_tiles; ++c) {
            if (c < 0 || c >= kCols) continue;
            const int dx = c - col;
            const int dy = r - row;
            if (dx * dx + dy * dy > radius2) continue;
            tile_owner_[static_cast<std::size_t>(r * kCols + c)] = static_cast<std::int8_t>(p);
        }
    }
}

bool Sim::owns_tile(int p, int x, int y) const {
    if (x < 0 || y < 0) return false;
    if (tile_owner_.size() != static_cast<std::size_t>(kCols * kRows)) return false;
    const int c = x / kTileSize;
    const int r = y / kTileSize;
    if (c < 0 || c >= kCols || r < 0 || r >= kRows) return false;
    return tile_owner_[static_cast<std::size_t>(r * kCols + c)] == static_cast<std::int8_t>(p);
}

void Sim::recount_territory() {
    std::array<int, kPlayers> counts{};
    for (const auto owner : tile_owner_) {
        if (owner >= 0 && owner < kPlayers) ++counts[static_cast<std::size_t>(owner)];
    }
    for (int p = 0; p < kPlayers; ++p) {
        players_[static_cast<std::size_t>(p)].res.territory =
            counts[static_cast<std::size_t>(p)] / kTerritoryTilesPerPoint;
        // 领地变了就同步掩码，客户端的放置预览才不会和服务器对不上
        refresh_territory_mask(p);
    }
}

void Sim::rebuild_flow() {
    constexpr int kDx[4] = {1, -1, 0, 0};
    constexpr int kDy[4] = {0, 0, 1, -1};

    for (int p = 0; p < kPlayers; ++p) {
        auto& field = flow_[static_cast<std::size_t>(p)];
        field.assign(static_cast<std::size_t>(kCols * kRows), -1);
        std::deque<int> queue;

        // 多源 BFS：所有"敌方"基地都是源
        for (int e = 0; e < kPlayers; ++e) {
            if (e == p) continue;
            if (players_[static_cast<std::size_t>(e)].eliminated) continue;
            const Point base = base_pos_[static_cast<std::size_t>(e)];
            const int col = base.x / kTileSize;
            const int row = base.y / kTileSize;
            if (col < 0 || col >= kCols || row < 0 || row >= kRows) continue;
            const int start = row * kCols + col;
            if (field[static_cast<std::size_t>(start)] >= 0) continue;
            field[static_cast<std::size_t>(start)] = 0;
            queue.push_back(start);
        }

        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop_front();
            const int c = current % kCols;
            const int r = current / kCols;
            const auto base_distance = field[static_cast<std::size_t>(current)];
            for (int i = 0; i < 4; ++i) {
                const int nc = c + kDx[i];
                const int nr = r + kDy[i];
                if (nc < 0 || nc >= kCols || nr < 0 || nr >= kRows) continue;
                const Tile tile = terrain_.cell(nc, nr);
                if (tile == Tile::Water || tile == Tile::Rock) continue;
                const auto index = static_cast<std::size_t>(nr * kCols + nc);
                if (field[index] >= 0) continue;
                field[index] = static_cast<std::int16_t>(base_distance + 1);
                queue.push_back(nr * kCols + nc);
            }
        }
    }
}

bool Sim::holding_for_node(int p, const Unit& unit) const {
    for (const auto& node : nodes_) {
        if (node.owner == p) continue;
        // 用每个片区自己的半径：以前写死 240，小队会在半径 240 停住，
        // 而小片区半径只有 160 —— 结果永远站在圈外，占点进度一直是 0
        if (dist2(unit.x, unit.y, node.x, node.y) > node.radius * node.radius) continue;
        return true;
    }
    return false;
}

void Sim::move_along_flow(int p, Unit& unit, int speed) {
    const auto& field = flow_[static_cast<std::size_t>(p)];
    if (field.empty()) return;

    const int col = unit.x / kTileSize;
    const int row = unit.y / kTileSize;
    if (col < 0 || col >= kCols || row < 0 || row >= kRows) return;

    const int current = field[static_cast<std::size_t>(row * kCols + col)];
    if (current <= 0) return;  // 已经在目标格，或者这张图到不了

    int best = current;
    int best_col = col;
    int best_row = row;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dc == 0 && dr == 0) continue;
            const int nc = col + dc;
            const int nr = row + dr;
            if (nc < 0 || nc >= kCols || nr < 0 || nr >= kRows) continue;
            // 斜向不给切角：两条正交边都必须能走
            if (dc != 0 && dr != 0) {
                const Tile ox = terrain_.cell(col + dc, row);
                const Tile oy = terrain_.cell(col, row + dr);
                if (ox == Tile::Water || ox == Tile::Rock) continue;
                if (oy == Tile::Water || oy == Tile::Rock) continue;
            }
            const int value = field[static_cast<std::size_t>(nr * kCols + nc)];
            if (value < 0 || value >= best) continue;
            best = value;
            best_col = nc;
            best_row = nr;
        }
    }
    if (best_col == col && best_row == row) return;

    const int waypoint_x = best_col * kTileSize + kTileSize / 2;
    const int waypoint_y = best_row * kTileSize + kTileSize / 2;
    move_unit(p, unit, waypoint_x, waypoint_y, speed);
}

// 寻路网格：4×4 个砖块合并成一个导航格（80 像素），块内至少 12/16 可走才算通。
bool Sim::nav_passable(int col, int row) const {
    constexpr int kNavTiles = 4;
    int passable = 0;
    for (int dr = 0; dr < kNavTiles; ++dr) {
        for (int dc = 0; dc < kNavTiles; ++dc) {
            const int c = col * kNavTiles + dc;
            const int r = row * kNavTiles + dr;
            if (c >= kCols || r >= kRows) continue;
            if (terrain_.passable(c * kTileSize + 1, r * kTileSize + 1)) ++passable;
        }
    }
    return passable >= 12;
}

// 网格 A*（8 邻域，斜向要求两条正交边都能走）。
// 地图 60×40 = 2400 格，单次搜索很小；只在"目标变了 / 路径走完"时重算。
bool Sim::find_path(int sx, int sy, int gx, int gy, std::vector<Point>& out) const {
    out.clear();
    constexpr int kNavTiles = 4;
    constexpr int kNavCols = kCols / kNavTiles;
    constexpr int kNavRows = kRows / kNavTiles;
    const int nav_px = kTileSize * kNavTiles;
    const int start_col = sx / nav_px;
    const int start_row = sy / nav_px;
    int goal_col = gx / nav_px;
    int goal_row = gy / nav_px;
    if (start_col < 0 || start_col >= kNavCols || start_row < 0 || start_row >= kNavRows) return false;
    if (goal_col < 0 || goal_col >= kNavCols || goal_row < 0 || goal_row >= kNavRows) return false;

    // 目标格可能是建筑/基地（本身不可通行或者被占），就近找一个可走的落点
    if (!nav_passable(goal_col, goal_row)) {
        bool fixed = false;
        for (int radius = 1; radius <= 4 && !fixed; ++radius) {
            for (int dr = -radius; dr <= radius && !fixed; ++dr) {
                for (int dc = -radius; dc <= radius && !fixed; ++dc) {
                    const int c = goal_col + dc;
                    const int r = goal_row + dr;
                    if (c < 0 || c >= kNavCols || r < 0 || r >= kNavRows) continue;
                    if (!nav_passable(c, r)) continue;
                    goal_col = c;
                    goal_row = r;
                    fixed = true;
                }
            }
        }
        if (!fixed) return false;
    }

    constexpr int kCells = kNavCols * kNavRows;
    std::array<int, 8> dx{1, -1, 0, 0, 1, 1, -1, -1};
    std::array<int, 8> dy{0, 0, 1, -1, 1, -1, 1, -1};

    std::vector<int> cost(static_cast<std::size_t>(kCells), -1);
    std::vector<int> parent(static_cast<std::size_t>(kCells), -1);
    // 小地图上一格 80 像素，代价用 10/14 的整数
    const auto heuristic = [&](int c, int r) {
        const int dx_abs = std::abs(c - goal_col);
        const int dy_abs = std::abs(r - goal_row);
        return 10 * std::max(dx_abs, dy_abs) + 4 * std::min(dx_abs, dy_abs);
    };
    struct Node {
        int estimate;
        int index;
        bool operator>(const Node& other) const { return estimate > other.estimate; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    const int start = start_row * kNavCols + start_col;
    const int goal = goal_row * kNavCols + goal_col;
    cost[static_cast<std::size_t>(start)] = 0;
    open.push(Node{heuristic(start_col, start_row), start});

    bool found = false;
    while (!open.empty()) {
        const Node current = open.top();
        open.pop();
        if (current.index == goal) {
            found = true;
            break;
        }
        const int c = current.index % kNavCols;
        const int r = current.index / kNavCols;
        const int base_cost = cost[static_cast<std::size_t>(current.index)];
        // 已经用更短路径到达过（简单去重）
        if (current.estimate > base_cost + heuristic(c, r)) continue;

        for (int i = 0; i < 8; ++i) {
            const int nc = c + dx[static_cast<std::size_t>(i)];
            const int nr = r + dy[static_cast<std::size_t>(i)];
            if (nc < 0 || nc >= kNavCols || nr < 0 || nr >= kNavRows) continue;
            if (!nav_passable(nc, nr)) continue;
            const bool diagonal = i >= 4;
            if (diagonal) {
                if (!nav_passable(c + dx[static_cast<std::size_t>(i)], r)) continue;
                if (!nav_passable(c, r + dy[static_cast<std::size_t>(i)])) continue;
            }
            const int next = nr * kNavCols + nc;
            const int next_cost = base_cost + (diagonal ? 14 : 10);
            int& known = cost[static_cast<std::size_t>(next)];
            if (known >= 0 && known <= next_cost) continue;
            known = next_cost;
            parent[static_cast<std::size_t>(next)] = current.index;
            open.push(Node{next_cost + heuristic(nc, nr), next});
        }
    }
    if (!found) return false;

    // 回溯成世界坐标航点（去掉起点格）
    int cursor = goal;
    while (cursor >= 0 && cursor != start) {
        const int c = cursor % kNavCols;
        const int r = cursor / kNavCols;
        out.push_back(Point{c * nav_px + nav_px / 2, r * nav_px + nav_px / 2});
        cursor = parent[static_cast<std::size_t>(cursor)];
    }
    std::reverse(out.begin(), out.end());
    return true;
}

// 跟着 A* 路径走：路径点到了就弹掉；被打断（目标变了）就重算
void Sim::follow_path(int p, Unit& unit, int speed, int target_x, int target_y) {
    const bool target_changed = std::abs(target_x - unit.path_target_x) > kTileSize * 4 ||
                                std::abs(target_y - unit.path_target_y) > kTileSize * 4;
    const bool stale = tick_ - unit.path_tick > 160;  // 8 秒重算一次：图更大，A* 更贵
    if (target_changed || stale) {
        if (find_path(unit.x, unit.y, target_x, target_y, unit.path)) {
            unit.path_target_x = target_x;
            unit.path_target_y = target_y;
            unit.path_tick = tick_;
        }
    }

    while (!unit.path.empty()) {
        const Point waypoint = unit.path.front();
        const int d2 = dist2(unit.x, unit.y, waypoint.x, waypoint.y);
        if (d2 > 48 * 48) break;
        unit.path.erase(unit.path.begin());
    }

    if (!unit.path.empty()) {
        const Point waypoint = unit.path.front();
        move_unit(p, unit, waypoint.x, waypoint.y, speed);
    } else {
        move_unit(p, unit, target_x, target_y, speed);
    }
}

// 工地推进：基础 1 tick/帧，附近每个工程兵 +50%
void Sim::tick_construction() {
    for (int p = 0; p < kPlayers; ++p) {
        PlayerState& state = players_[static_cast<std::size_t>(p)];
        if (state.eliminated) continue;
        int engineers_near = 0;
        for (auto& building : state.buildings) {
            if (building.ready) continue;
            engineers_near = 0;
            for (const auto& unit : state.units) {
                if (unit.type != UnitType::Engineer || unit.hp <= 0) continue;
                if (dist2(unit.x, unit.y, building.x, building.y) <= 220 * 220) ++engineers_near;
            }
            const int speed = 100 + engineer_bonus(p) * engineers_near;
            building.build_ticks += std::max(1, speed / 100);
            if (building.build_ticks >= building.build_total) {
                building.build_ticks = building.build_total;
                building.ready = true;
                building.hp = building.max_hp;
            } else {
                // 工地期间血量按进度慢慢长回来
                const int target = building.max_hp * (30 + 70 * building.build_ticks / building.build_total) / 100;
                building.hp = std::min(building.max_hp, std::max(building.hp, target));
            }
        }
    }
}

// 工程兵：每 4 秒把身边的一格森林/岩石清成平地（打通近路）
void Sim::tick_engineers() {
    // 工程兵还负责"维修"：每 tick 给 160 像素内受损的自家建筑回 1 点血
    for (int p = 0; p < kPlayers; ++p) {
        PlayerState& state = players_[static_cast<std::size_t>(p)];
        if (state.eliminated) continue;
        for (auto& unit : state.units) {
            if (unit.type != UnitType::Engineer || unit.hp <= 0) continue;
            for (auto& building : state.buildings) {
                if (building.hp >= building.max_hp) continue;
                if (dist2(unit.x, unit.y, building.x, building.y) > 160 * 160) continue;
                building.hp = std::min(building.max_hp, building.hp + 1);
            }
        }
    }

    for (int p = 0; p < kPlayers; ++p) {
        PlayerState& state = players_[static_cast<std::size_t>(p)];
        if (state.eliminated) continue;
        for (auto& unit : state.units) {
            if (unit.type != UnitType::Engineer || unit.hp <= 0) continue;
            if (unit.cd > 0) {
                --unit.cd;
                continue;
            }
            unit.cd = kEngineerClearTicks;
            terrain_.clear_obstacle_near(unit.x, unit.y, kEngineerClearRadius);
            break;
        }
    }
}

void Sim::tick_units(int p) {
    PlayerState& state = players_[static_cast<std::size_t>(p)];
    if (state.eliminated) return;

    for (auto& unit : state.units) {
        if (unit.hp <= 0) continue;

        if (unit.slow_ticks > 0) --unit.slow_ticks;
        if (unit.buff_ticks > 0) --unit.buff_ticks;
        if (unit.cd > 0) --unit.cd;

        const UnitStat& stat = unit_stat(unit.type);
        Target target;
        if (!pick_target(p, unit, stat.aggro * range_percent() / 100, target)) continue;

        const int distance = isqrt(dist2(unit.x, unit.y, target.x, target.y));
        if (distance <= stat.range && target.kind != Target::Kind::Node) {
            if (unit.cd == 0) {
                apply_damage(p, unit, target);
                unit.cd = stat.atk_ticks;
            }
            continue;
        }

        // 站在别人（或中立）的资源点上就别急着赶路，先把它占下来。
        // 不然部队只是"路过"，占点进度刚涨一点就走了，地图资源点永远抢不下来。
        if (target.kind != Target::Kind::Unit && holding_for_node(p, unit)) continue;

        // 兵营指定了攻击区域：先往那儿走，到了再恢复正常的索敌逻辑
        if (unit.order_x >= 0 && unit.order_y >= 0) {
            const int odx = unit.order_x - unit.x;
            const int ody = unit.order_y - unit.y;
            const int od2 = odx * odx + ody * ody;
            if (od2 <= 140 * 140) {
                unit.order_x = -1;
                unit.order_y = -1;
            } else {
                int order_speed = stat.speed * terrain_.speed_percent(unit.x, unit.y) / 100;
                if (unit.slow_ticks > 0) order_speed = order_speed / 2;
                order_speed = std::max(1, order_speed * weather_speed_percent() / 100);
                const int step = std::max(1, order_speed / kTickHz);
                const int len = std::max(1, isqrt(od2));
                unit.x += odx * step / len;
                unit.y += ody * step / len;
                unit.x = std::clamp(unit.x, kFieldMargin, kFieldW - kFieldMargin);
                unit.y = std::clamp(unit.y, kFieldMargin, kFieldH - kFieldMargin);
                continue;
            }
        }

        // stat.speed 的单位是「像素/秒」，每 tick 只能走整数像素：
        // 用 move_acc 把余数攒起来，平均速度才等于表里的数值。
        int speed_per_second = stat.speed * terrain_.speed_percent(unit.x, unit.y) / 100;
        if (unit.slow_ticks > 0) speed_per_second = speed_per_second / 2;
        if (unit.buff_ticks > 0) speed_per_second = speed_per_second * 120 / 100;
        speed_per_second = speed_per_second * kMarchSpeedPercent / 100;
        speed_per_second = speed_per_second * weather_speed_percent() / 100;
        unit.move_acc += std::max(1, speed_per_second);
        int speed = unit.move_acc / kTickHz;
        unit.move_acc -= speed * kTickHz;
        if (speed <= 0) continue;

        // 开局期：兵原地守着，别 7 秒就冲出家门
        if (tick_ < kOpeningTicks) continue;

        // 追敌兵直接走直线（距离近）；打建筑/基地走 A*（要绕湖绕山）
        const int before_x = unit.x;
        const int before_y = unit.y;

        if (unit.stuck_ticks >= 20) {
            // 连续 1 秒没挪窝：沿着垂直方向横着走一段，强行脱困
            const int dx = target.x - unit.x;
            const int dy = target.y - unit.y;
            const int len = std::max(1, isqrt(dx * dx + dy * dy));
            const int side = (unit.id % 2 == 0) ? 1 : -1;
            move_unit(p, unit, unit.x - dy * 220 * side / len, unit.y + dx * 220 * side / len,
                      speed);
            unit.stuck_ticks = 0;
            unit.path.clear();
        } else if (target.kind == Target::Kind::Unit) {
            move_unit(p, unit, target.x, target.y, speed);
        } else {
            follow_path(p, unit, speed, target.x, target.y);
        }

        if (unit.x == before_x && unit.y == before_y) {
            ++unit.stuck_ticks;
        } else {
            unit.stuck_ticks = 0;
        }
    }
}

int Sim::towers_lost(int p) const {
    int lost = 0;
    for (const auto& tower : player(p).towers) {
        if (tower.hp <= 0) ++lost;
    }
    return lost;
}

int Sim::alive_count() const {
    int alive = 0;
    for (const auto& state : players_) {
        if (!state.eliminated) ++alive;
    }
    return alive;
}

void Sim::check_end() {
    if (phase_ != MatchPhase::Playing) return;

    for (int p = 0; p < kPlayers; ++p) {
        PlayerState& state = players_[static_cast<std::size_t>(p)];
        if (state.eliminated || state.base_hp > 0) continue;
        state.eliminated = true;
        state.base_hp = 0;
        state.units.clear();
        state.buildings.clear();
        for (auto& tower : state.towers) tower.hp = 0;
    }

    if (alive_count() <= 1) {
        phase_ = MatchPhase::Finished;
        reason_ = WinReason::BaseDestroyed;
        winner_ = -1;
        for (int p = 0; p < kPlayers; ++p) {
            if (!players_[static_cast<std::size_t>(p)].eliminated) {
                winner_ = p;
                break;
            }
        }
        return;
    }

    if (tick_ >= match_ticks()) {
        phase_ = MatchPhase::Finished;
        int best_hp = -1;
        int best_player = -1;
        bool tie = false;
        for (int p = 0; p < kPlayers; ++p) {
            const int hp = players_[static_cast<std::size_t>(p)].base_hp;
            if (hp > best_hp) {
                best_hp = hp;
                best_player = p;
                tie = false;
            } else if (hp == best_hp) {
                tie = true;
            }
        }
        if (tie || best_player < 0) {
            winner_ = -1;
            reason_ = WinReason::TimeoutDraw;
        } else {
            winner_ = best_player;
            reason_ = WinReason::TimeoutBaseHp;
        }
    }
}

void Sim::advance() {
    if (phase_ != MatchPhase::Playing) return;

    ++tick_;

    tick_weather();
    tick_events();
    for (int p = 0; p < kPlayers; ++p) {
        if (hq_cooldown_[static_cast<std::size_t>(p)] > 0) --hq_cooldown_[static_cast<std::size_t>(p)];
    }

    // 流场每秒重建一次（基地被打掉/玩家出局都会改图）
    if (tick_ % 20 == 1) rebuild_flow();

    const int production_rounds = (tick_ >= kLateGameTick) ? 2 : 1;
    for (int p = 0; p < kPlayers; ++p) {
        if (players_[static_cast<std::size_t>(p)].eliminated) continue;
        for (int round = 0; round < production_rounds; ++round) tick_production(p);
    }
    tick_construction();
    tick_engineers();
    for (int p = 0; p < kPlayers; ++p) tick_towers(p);
    for (int p = 0; p < kPlayers; ++p) tick_base(p);
    tick_nodes();

    const int first = tick_ % kPlayers;
    for (int i = 0; i < kPlayers; ++i) {
        tick_units((first + i) % kPlayers);
    }

    for (int p = 0; p < kPlayers; ++p) {
        PlayerState& state = players_[static_cast<std::size_t>(p)];
        state.units.erase(
            std::remove_if(state.units.begin(), state.units.end(),
                           [](const Unit& unit) { return unit.hp <= 0; }),
            state.units.end());
        state.buildings.erase(
            std::remove_if(state.buildings.begin(), state.buildings.end(),
                           [](const Building& building) { return building.hp <= 0; }),
            state.buildings.end());
    }

    for (auto& effect : effects_) --effect.ttl;
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                  [](const Effect& effect) { return effect.ttl <= 0; }),
                   effects_.end());

    check_end();
}

int Sim::enemy_units_within(int player, int x, int y, int radius) const {
    const int radius2 = radius * radius;
    int count = 0;
    for (int e = 0; e < kPlayers; ++e) {
        if (e == player) continue;
        for (const auto& unit : players_[static_cast<std::size_t>(e)].units) {
            if (unit.hp <= 0) continue;
            if (dist2(unit.x, unit.y, x, y) <= radius2) ++count;
        }
    }
    return count;
}

bool Sim::has_card(int player, int card_id) const {
    return slot_of_card(player, card_id) >= 0;
}

int Sim::slot_of_card(int player_index, int card_id) const {
    const PlayerState& state = player(player_index);
    for (int slot = 0; slot < kHandSize; ++slot) {
        if (state.hand(slot) == card_id) return slot;
    }
    return -1;
}

}  // namespace kingdom_fight::rts
