// RTS 对局内核：4 人混战、多资源经济、服务端权威的实时模拟。
//
// 一个 tick = 50ms（20Hz）。所有状态变化都发生在 advance() 里，输入只有 play_card()。
// 因此「同一初始状态 + 同一串 (tick, 操作)」= 同一结果，可以录播、可以 bench。
//
// 经济：兵源（出兵池）/ 食物 / 铁 / 科技。基地有基础产出，农场/矿井/兵营/研究所提速。
// 地图：多张地形 + 随机降落点，开局由 MatchConfig 决定。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "rts/terrain.h"
#include "rts/types.h"

namespace kingdom_fight::rts {

enum class PlayResult : std::uint8_t {
    Ok = 0,
    NotPlaying,
    BadSlot,
    NotEnoughResources,  // 四项资源有一项不够
    BadPlacement,
    TooManyUnits,
    TooManyBuildings,
    UnknownCard,
    Locked,    // 总部支援还没解锁
    Cooldown,  // 总部支援冷却中
};

[[nodiscard]] const char* to_string(PlayResult result);

struct PlayerState {
    Alliance alliance = Alliance::Vanguard;
    Resources res;
    int manpower_acc = 0;
    int materiel_acc = 0;
    int energy_acc = 0;
    int tech_acc = 0;
    // 联盟加成的小数余量（每 100 = 1 单位），避免百分比加成被整数除法吃掉
    int manpower_frac = 0;
    int materiel_frac = 0;
    int energy_frac = 0;
    int tech_frac = 0;

    int base_hp = kBaseHp;
    int base_cd = 0;
    bool eliminated = false;

    std::vector<int> rotation;  // 6 张卡的循环队列；手牌 = 前 4 张
    std::vector<Unit> units;
    std::vector<Building> buildings;
    // 开局有 kTowersPerPlayer 座，之后可以用「防御塔」卡继续盖，所以是动态数组
    std::vector<Tower> towers;
    // 局内成长树：每项当前等级（科技点花在升级上）
    // 成长树：12 个节点，做过的用位标记（同一分支互斥，所以不需要等级）
    std::uint16_t tree_mask = 0;
    int preferred_unit = -1;  // 兵营默认训练的兵种（-1 = 用阵营默认）
    int tech_spent = 0;

    [[nodiscard]] int hand(int slot) const {
        if (slot < 0 || slot >= kHandSize) return -1;
        if (slot >= static_cast<int>(rotation.size())) return -1;
        return rotation[static_cast<std::size_t>(slot)];
    }

    [[nodiscard]] int building_count() const { return static_cast<int>(buildings.size()); }

    [[nodiscard]] int count_of(BuildingType type) const {
        int count = 0;
        for (const auto& building : buildings) {
            if (building.type == type) ++count;
        }
        return count;
    }

    [[nodiscard]] int manpower_cap() const {
        const int bonus = alliance == Alliance::Swarm ? kSwarmCapBonus : 0;
        return kManpowerCapBase + count_of(BuildingType::Barracks) * kManpowerCapPerBarracks +
               bonus;
    }
};

class Sim {
public:
    Sim() = default;

    // 默认四家都是联合商会（没有血量加成，测试里断言"标准值"最方便）
    [[nodiscard]] bool start(
        const std::array<std::vector<int>, kPlayers>& decks, const MatchConfig& config,
        const std::array<Alliance, kPlayers>& factions = std::array<Alliance, kPlayers>{
            Alliance::Combine, Alliance::Combine, Alliance::Combine, Alliance::Combine});

    [[nodiscard]] PlayResult play_card(int player, int slot, int x, int y);

    void advance();

    [[nodiscard]] MatchPhase phase() const { return phase_; }
    [[nodiscard]] int tick() const { return tick_; }
    [[nodiscard]] int winner() const { return winner_; }
    [[nodiscard]] WinReason reason() const { return reason_; }

    [[nodiscard]] const PlayerState& player(int p) const {
        return players_[static_cast<std::size_t>(p)];
    }
    [[nodiscard]] const Terrain& terrain() const { return terrain_; }
    [[nodiscard]] const std::vector<ResourceNode>& nodes() const { return nodes_; }

    // ---- 昼夜 / 天气 / 总部支援 ----
    [[nodiscard]] bool is_night() const;
    [[nodiscard]] Weather weather() const { return weather_; }
    [[nodiscard]] int day_phase_percent() const;
    // 0 = 还没解锁，1 = 可以打，2 = 领地不够，3 = 冷却中
    [[nodiscard]] int hq_state(int p) const;
    [[nodiscard]] PlayResult call_hq(int p, int x, int y);
    // 局内成长树：花科技点升一级
    PlayResult upgrade(int player, int upgrade_id);
    // 局外 DIY：设置兵营默认兵种（只允许民兵/长枪/盾卫/骑手/弓手）
    bool set_preferred_unit(int player, int unit_type);
    [[nodiscard]] bool tree_taken(int player, int node) const;
    [[nodiscard]] bool tree_available(int player, int node) const;
    [[nodiscard]] int upgrade_cost_for(int player, int node) const;
    // 各项升级的实际加成（都是百分比，100 = 不变）
    [[nodiscard]] int unit_hp_percent(int player) const;
    [[nodiscard]] int unit_dmg_percent(int player) const;
    [[nodiscard]] int train_ticks_for(int player) const;
    [[nodiscard]] int tree_bonus_train_speed(int player) const;
    [[nodiscard]] int card_cost(int player, int cost) const;
    [[nodiscard]] int build_ticks_for(int player) const;
    [[nodiscard]] int engineer_bonus(int player) const;
    [[nodiscard]] int building_hp_percent(int player) const;
    [[nodiscard]] int tower_hp_percent(int player) const;
    [[nodiscard]] int material_percent(int player) const;
    // 领地越多，资源生产越快（100 + 2%/点，封顶 180%）
    [[nodiscard]] int territory_yield_percent(int player) const;
    // 攻城兵判定
    [[nodiscard]] static bool is_siege_unit(UnitType type);
    [[nodiscard]] int energy_percent(int player) const;
    // 拆除自家建筑（kind: 0 = 建筑, 1 = 塔）
    PlayResult demolish(int player, int kind, int index);
    // 手动修复：花钱把一座受损的自家建筑/塔补满（kind 0 = 建筑下标，1 = 塔 id）
    PlayResult repair(int player, int kind, int index);
    // 造塔（不走卡牌）：固定槽位 + 资源成本，位置规则和以前的塔卡完全一样
    PlayResult build_tower(int player, int x, int y);
    // 兵营：指定这个兵营训练什么兵 / 指定攻击区域
    PlayResult set_building_unit(int player, int index, int unit_type);
    PlayResult set_building_rally(int player, int index, int x, int y);
    // 弃牌：把这张手牌放回牌堆末尾，立刻换下一张抽上来（防卡手）
    PlayResult discard(int player, int slot);
    // 可建造数量上限：领地越多，能盖的越多（4 ~ 10）
    [[nodiscard]] int building_cap(int player) const;
    // 塔：数量上限（领地越大越多）和"按 id 找塔"
    [[nodiscard]] int tower_cap(int player) const;
    [[nodiscard]] int tower_count(int player) const;
    // 领地掩码（60×40，每格 = 4×4 地板格）：客户端的绿格和服务器完全同源
    [[nodiscard]] const std::vector<std::uint8_t>& territory_mask(int player) const;
    void refresh_territory_mask(int player);
    [[nodiscard]] int territory_version(int player) const;
    [[nodiscard]] Tower* tower_by_id(int player, int id);
    [[nodiscard]] const Tower* tower_by_id(int player, int id) const;
    // 统计用：本局一共成功呼叫了几次总部支援（bench / 测试看链路是否通）
    [[nodiscard]] int hq_calls() const { return hq_calls_; }

    // 随机事件（补给空投 / 矿脉爆发），给一局加点变数
    [[nodiscard]] RandomEvent current_event() const { return event_; }
    [[nodiscard]] int event_ticks_left() const;
    [[nodiscard]] int event_target() const { return event_target_; }
    [[nodiscard]] const std::vector<Effect>& effects() const { return effects_; }
    [[nodiscard]] const MatchConfig& config() const { return config_; }
    [[nodiscard]] Point base_position(int p) const { return base_pos_[static_cast<std::size_t>(p)]; }

    [[nodiscard]] int base_hp(int p) const { return player(p).base_hp; }
    [[nodiscard]] const Resources& resources(int p) const { return player(p).res; }
    [[nodiscard]] bool eliminated(int p) const { return player(p).eliminated; }
    [[nodiscard]] int alive_count() const;
    [[nodiscard]] int match_ticks() const;
    [[nodiscard]] int remaining_ticks() const;
    [[nodiscard]] int hq_unlock_tick() const;

    [[nodiscard]] int towers_lost(int p) const;
    // 基地是否"裸露"：一座完工的塔都没有。塔现在只能靠塔卡建，开局就是裸露状态。
    [[nodiscard]] bool base_exposed(int p) const {
        for (const auto& tower : player(p).towers) {
            if (tower.complete() && tower.hp > 0) return false;
        }
        return true;
    }

    // 给 AI / 测试用的查询
    [[nodiscard]] int enemy_units_within(int player, int x, int y, int radius) const;
    [[nodiscard]] bool has_card(int player, int card_id) const;
    [[nodiscard]] int slot_of_card(int player, int card_id) const;
    [[nodiscard]] bool can_afford(int player, int card_id) const;

private:
    struct Target {
        enum class Kind : std::uint8_t { Unit, Tower, Base, Building, Node };
        Kind kind = Kind::Base;
        int owner = 0;
        int index = 0;
        int x = 0;
        int y = 0;
    };

    void reset_towers(int p);
    void tick_production(int p);
    void tick_nodes();
    // 行军流场：每个玩家一张"到最近敌方基地"的 BFS 距离图，每 20 tick 重建。
    // 大地图 + 有机地形下，靠墙滑行会卡在湖湾里，必须有张图指路。
    // 领地：每格记录归属（-1 = 中立），塔/建筑只能盖在自家领地上
    void tick_construction();
    void tick_engineers();
    void claim_territory(int p, int x, int y, int radius_tiles);
    void recount_territory();
    [[nodiscard]] bool owns_tile(int p, int x, int y) const;
    std::vector<std::int8_t> tile_owner_;
    void rebuild_flow();
    void move_along_flow(int p, Unit& unit, int speed);
    // 网格 A*：给单位找一条绕开水/石头的路。起点终点都传世界像素。
    [[nodiscard]] bool find_path(int sx, int sy, int gx, int gy, std::vector<Point>& out) const;
    // 寻路用的粗粒度导航格：4×4 个砖块 = 80 像素。砖块越细，寻路越要粗，
    // 否则 A* 的节点数会随砖块数平方增长（38400 格直接把测试跑挂）。
    [[nodiscard]] bool nav_passable(int col, int row) const;
    void follow_path(int p, Unit& unit, int speed, int target_x, int target_y);
    void tick_weather();
    void tick_events();
    [[nodiscard]] int range_percent() const;
    [[nodiscard]] int weather_speed_percent() const;
    // 单位是不是站在一个"还没拿下的"资源点里（是的话就地占点，不赶路）
    [[nodiscard]] bool holding_for_node(int p, const Unit& unit) const;
    // 资源发放：统一走这里，联盟的百分比加成才有唯一入口
    void add_materiel(PlayerState& state, int amount);
    void add_energy(PlayerState& state, int amount);
    void add_manpower(PlayerState& state, int amount);
    void add_tech(PlayerState& state, int amount);
    void tick_towers(int p);
    void tick_base(int p);
    void tick_units(int p);

    [[nodiscard]] bool pick_target(int p, const Unit& unit, int aggro, Target& out) const;
    [[nodiscard]] bool pick_building_target(int p, const Unit& unit, Target& out) const;
    void apply_damage(int p, Unit& attacker, const Target& target);
    void apply_spell(int p, const CardDef& def, int x, int y);
    void spawn_squad(int p, const CardDef& def, int x, int y);
    void spawn_building(int p, const CardDef& def, int x, int y);
    void push_effect(int p, const CardDef& def, int x, int y);
    void move_unit(int p, Unit& unit, int target_x, int target_y, int speed);
    void check_end();
    [[nodiscard]] bool can_place(int p, CardKind kind, int x, int y) const;
    // 建筑放置：是否压到已有建筑/塔/基地
    [[nodiscard]] bool structure_blocked(int x, int y, int min_spacing) const;
    // 固定的 4×4 格占地：返回左上角格坐标，并检查整块是否合法（地形/领地/重叠）
    [[nodiscard]] Point footprint_origin(int x, int y) const;
    [[nodiscard]] bool footprint_ok(int player, int x, int y, bool needs_territory) const;

    MatchConfig config_{};
    Terrain terrain_{};
    std::vector<ResourceNode> nodes_;
    Weather weather_ = Weather::Clear;
    int weather_until_ = 0;
    std::array<int, kPlayers> hq_cooldown_{};
    int hq_calls_ = 0;
    RandomEvent event_ = RandomEvent::None;
    int event_until_ = 0;
    int event_target_ = -1;
    int next_event_tick_ = kEventFirstTick;
    std::array<std::vector<std::int16_t>, kPlayers> flow_{};
    std::array<PlayerState, kPlayers> players_{};
    std::array<Point, kPlayers> base_pos_{};
    std::vector<Effect> effects_;
    MatchPhase phase_ = MatchPhase::WaitingForDeck;
    int tick_ = 0;
    int winner_ = -1;
    WinReason reason_ = WinReason::None;
    int next_unit_id_ = 1;
    int next_building_id_ = 1;
    int next_tower_id_ = 1;
    std::vector<std::uint8_t> territory_masks_[kPlayers];   // 每格 1 = 自家领地
    int territory_version_[kPlayers] = {0, 0, 0, 0};        // 掩码变了就 +1（服务器据此决定要不要重发）
};

}  // namespace kingdom_fight::rts
