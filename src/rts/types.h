// RTS + 卡牌构筑模式的共享数据模型（服务端权威）。
//
// 设计取向：
//   · 全部整数运算，没有浮点 —— 同一 tick 序列 + 同一输入 = 同一结果（可回放、可 bench）。
//   · 位置用「世界像素」整数：场地 2400×1600，速度一律取 20 的倍数 → 每 tick 位移是整数。
//   · 这个头文件不许 include 任何 IO / 网络 / 线程（沿用 src/logic 的分层原则）。
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kingdom_fight::rts {

// ---- 场地与节奏 ----------------------------------------------------------
// 60×40 格（每格 80）= 4800×3200 像素。面积是上一版的 1.78 倍：
// 出生点之间要跑二十秒以上，资源点和总部支援才有战略价值。
inline constexpr int kFieldW = 4800;
inline constexpr int kFieldH = 3200;

inline constexpr int kTickHz = 20;
inline constexpr int kMsPerTick = 1000 / kTickHz;

// 4 人局：每人 2 座塔。没有固定兵线，走哪条路是地形决定的。
inline constexpr int kPlayers = 4;
inline constexpr int kTowersPerPlayer = 0;   // 旧的开局赠塔逻辑已废弃，塔只能靠塔卡建

struct Point {
    int x;
    int y;
};

inline constexpr Point kCenter{kFieldW / 2, kFieldH / 2};

// 降落点不再是预设阵型：每局用 spawn_seed 在地图上随机挑 4 个点，
// 只约束"能站、互相够远、彼此连通、周围有地"，不追求对称。
struct MatchConfig {
    int terrain_variant = 0;
    std::uint32_t spawn_seed = 12345;
    // 对局时长（秒）和总部支援解锁时间（秒）。默认 30 分钟 / 25 分钟；
    // bench 和"快速试玩"会传更小的值，不然一分钟一局根本看不到后期机制。
    int match_seconds = 30 * 60;
    int hq_unlock_seconds = 25 * 60;
    // 总部支援的领地门槛（也是扣费）。
    // 30 试过，实际打不出去：领地是"按格子重算"的派生值，开局和收官都在 21 上下，
    // 够不到 30 —— 于是号称"给僵持局一个确定性结束手段"的总部支援从没被用上，
    // bench 里一半的局都是两只 100+ 人的军队互相对峙到 900 秒上限。
    // 15 是"占 1~2 个片区就能点一次"的量级。
    // 注意：这是**唯一**的一处定义，别再另开一个同名常量（以前就撞过一次）。
    int hq_cost_territory = 15;
};

inline constexpr int kBaseHp = 5200;
inline constexpr int kTowerHp = 600;

// 出兵范围：以自家基地为圆心的半径（替代原来"己方半场"的判定）
inline constexpr int kPlaceRadius = 640;

// ---- 联盟（四家各一条被动） ----------------------------------------------

enum class Alliance : std::uint8_t {
    Vanguard = 0,  // 先锋军团：建筑 / 塔 / 基地血量 +30%
    Combine,       // 联合商会：物资产出 +25%
    Order,         // 灵能教团：能源产出 +40%
    Swarm,         // 蜂群：人力上限 +50，人力恢复 +25%
    Count,
};

inline constexpr int kAllianceCount = static_cast<int>(Alliance::Count);

inline constexpr int kSwarmCapBonus = 50;      // 蜂群：人力上限 +50
inline constexpr int kVanguardHpPercent = 125;  // 先锋军团：建筑/塔/基地血量百分比

[[nodiscard]] const char* to_string(Alliance alliance);
[[nodiscard]] const char* alliance_passive(Alliance alliance);

// ---- 资源 ----------------------------------------------------------------
//
// 五个大类，各管一段玩法：
//   人力（manpower）：可部署的人口，有上限、会恢复 —— 出兵的前提
//   物资（materiel）：通用材料，建造和出兵都吃它
//   能源（energy）：高级单位与法术的门槛
//   科技（tech）：科技点，解锁高级卡
//   领地（territory）：**占地资源**，只能靠占住地图资源点慢慢攒，
//                      买不了兵、盖不了房，唯一的用途是呼叫总部支援。
struct Resources {
    int manpower = 0;
    int materiel = 0;
    int energy = 0;
    int tech = 0;
    int territory = 0;
};

// 开局资源给足一点（先把兵营造出来），生产速度整体调慢（见 kDepotTicks/kReactorTicks）
inline constexpr int kStartManpower = 150;
inline constexpr int kStartMateriel = 240;
inline constexpr int kStartEnergy = 160;
inline constexpr int kStartTech = 6;
inline constexpr int kStartTerritory = 0;

inline constexpr int kManpowerCapBase = 160;  // 兵营每座 +40（开局资源调高后上限也抬一点）
inline constexpr int kManpowerCapPerBarracks = 40;

inline constexpr int kHandSize = 4;
inline constexpr int kDeckSize = 8;   // 卡组 8 张（卡池 10 张，留出取舍空间）
inline constexpr int kMaxCardCopies = 2;  // 同一张卡最多带 2 张

// 前期运营别拖太久：240 秒上限，目标 60~120 秒打完
inline constexpr int kMatchTicks = 20 * 240;

inline constexpr int kMaxUnits = 128;    // 每人单位上限
inline constexpr int kMaxBuildings = 6;  // 每人经济建筑上限

// ---- 地图资源点 ----------------------------------------------------------
//
// 地图上散落资源片区，单位在旁边待够时间就能占下来，之后持续给主人产资源并扩大领地。
// "高战略点"（Rich）同时产物资产和能源，而且只刷在远离所有出生点的位置 ——
// 想去拿，就得把兵派出去、并且守得住。
enum class NodeKind : std::uint8_t {
    Materiel = 0,  // 矿脉：物资
    Energy,        // 能源井：能源
    Rich,          // 高资源片区：物资 + 能源，产量翻倍，占地一片
    Town,          // 城镇据点：原星球守军，固定工事 + 固定产出，打掉核心才能占
    Count,
};

// 资源点现在都是"片区"：有半径、有归属、能扩领地。
struct ResourceNode {
    NodeKind kind = NodeKind::Materiel;
    int x = 0;
    int y = 0;
    int radius = 200;    // 片区半径（像素）
    int owner = -1;      // -1 = 中立
    int progress = 0;    // 0..100，占点进度
    int capturer = -1;   // 正在占点的是谁
    int yield_ticks = 0;
    bool active = true;      // 高资源片区前 5 分钟是暗的，不能占
    int surge_ticks = 0;     // 「矿脉爆发」事件期间产量翻倍
    // 城镇据点专用：中立核心 + 守军炮台
    int core_hp = 0;
    int core_max = 0;
    int guard_cd = 0;
    int guard_ticks = 0;
    int claim_radius_tiles = 0;   // 占领后并入领地的半径（格）
};

// 片区半径
inline constexpr int kRichZoneRadius = 220;    // 高资源片区
inline constexpr int kTownZoneRadius = 260;    // 城镇据点
inline constexpr int kSmallZoneRadius = 160;   // 普通矿脉/能源井
inline constexpr int kNodeRadius = 240;        // 兼容旧名：普通片区判定半径
inline constexpr int kNodeCaptureTicks = 100;  // 1 个兵要占 5 秒
inline constexpr int kTownCaptureTicks = 200;  // 城镇要占 10 秒（还得先把核心打掉）
inline constexpr int kNodeMaterielTicks = 8;
inline constexpr int kNodeEnergyTicks = 10;
inline constexpr int kTerritoryYieldPercent = 2;   // 每点领地 +2% 资源产量（封顶 +80%）
// 城镇据点（原星球守军）：中立核心 + 固定炮台，不主动出击
inline constexpr int kTownCoreHp = 2400;
inline constexpr int kTownGuardDmg = 16;
inline constexpr int kTownGuardTicks = 20;     // 1 秒一发
inline constexpr int kTownGuardRange = 300;
inline constexpr int kTownClaimTiles = 14;     // 占领后并进领地的半径（格）

[[nodiscard]] const char* to_string(NodeKind kind);

// ---- 单位 ----------------------------------------------------------------

enum class UnitType : std::uint8_t {
    Militia = 0,
    Spear,
    Shield,
    Rider,
    Archer,
    Siege,
    Behemoth,
    Engineer,     // 工程兵：加速建造、清除障碍
    // ---- 攻城兵种：只有它们能对建筑造成大伤害 ----
    Ram,          // 冲车：慢、硬、只拆建筑
    Catapult,     // 投石车：射程远
    Ballista,     // 弩炮：射程最远、单体
    Cannon,       // 加农炮：伤害最高、最慢
    Count,
};

inline constexpr int kUnitTypeCount = static_cast<int>(UnitType::Count);

struct UnitStat {
    int hp;
    int dmg;
    int atk_ticks;  // 攻击间隔（tick）
    int range;      // 攻击距离（像素）
    int speed;      // 像素/秒（每 tick 只走整数像素，余数累加）
    int aggro;      // 索敌半径
    int cost_manpower;   // 兵营训练一个要多少资源
    int cost_materiel;
    int train_ticks;     // 训练一个要多少 tick（不同兵种不一样）
};

// 兵营每隔这么久训练一个单位（约 7 秒）
inline constexpr int kTrainTicks = 20 * 7;
// 建筑不是瞬间落地的：先是个工地，工程兵能加速
inline constexpr int kBuildTicks = 20 * 12;      // 12 秒
inline constexpr int kEngineerBuildBonus = 50;   // 每个工程兵 +50% 建造速度
inline constexpr int kEngineerClearTicks = 20 * 4;  // 工程兵每 4 秒清一格障碍
inline constexpr int kEngineerClearRadius = 140;    // 清障半径（像素）
// 开局这么久之内，训练出来的兵原地待命（不然 7 秒就冲出去，运营期形同虚设）
inline constexpr int kOpeningTicks = 20 * 60;
// 每个阵营兵营固定训练的兵种
[[nodiscard]] UnitType trained_unit(Alliance alliance);

[[nodiscard]] const UnitStat& unit_stat(UnitType type);
[[nodiscard]] const char* to_string(UnitType type);

// ---- 卡牌与建筑 ----------------------------------------------------------

enum class CardKind : std::uint8_t {
    Unit = 0,
    Spell,
    Building,
};

enum class SpellKind : std::uint8_t {
    None = 0,
    Fire,
    Mend,
    Frost,
    Rally,
    Clear,        // 开路：把一小块森林/岩石清成平地
    Count,
};

// 局内成长树：三棵树（军政 / 经济 / 工程），每棵四层。
// 同层两条分支互斥（真正的取舍）；后期节点需要别的树做前置（跨树联动）。
enum class TreeNode : std::uint8_t {
    // ---- 军政 ----
    MilitaryDrill = 0,  // 训练加速 I
    MilitaryElite,      // 精兵：小兵血量 +10%（与狂战互斥）
    MilitaryFury,       // 狂战：小兵攻击 +12%（与精兵互斥）
    MilitaryMachine,    // 战争机器：需要 训练加速 + 任一路线
    // ---- 经济 ----
    EconomyDepot,       // 仓储：物资 +10%
    EconomyPower,       // 能源网：能源 +15%（与贸易互斥）
    EconomyTrade,       // 贸易：卡牌费用 -10%（与能源网互斥）
    EconomyHub,         // 工业枢纽：需要 仓储 + 任一路线 + 训练加速 → 建筑上限 +1
    // ---- 工程 ----
    EngineeringBasics,  // 工程学：建造时间 -15%
    EngineeringFortify, // 加固：建筑血量 +20%（与快建互斥）
    EngineeringSpeed,   // 快建：工程兵建造加成 +25%（与加固互斥）
    EngineeringCitadel, // 要塞：需要 工业枢纽 + 任一路线 → 塔血量 +15%
    Count,
};

inline constexpr int kTreeNodeCount = static_cast<int>(TreeNode::Count);
inline constexpr int kTreeCount = 3;
inline constexpr int kTreeTiers = 4;

// 一棵树里的位置（给客户端排版用）
struct TreeNodeInfo {
    const char* name;      // UTF-8
    const char* effect;    // 一句话效果
    int cost;              // 科技点
    int tree;              // 0 军政 / 1 经济 / 2 工程
    int tier;              // 0..3
    std::uint16_t prereq;     // 前置节点掩码（全都要有）
    std::uint16_t any_of;     // 备选前置掩码（有其中任意一个即可，0 = 不需要）
    std::uint16_t conflicts;  // 互斥节点掩码（有任意一个就不行）
};

[[nodiscard]] const TreeNodeInfo& tree_node(TreeNode node);
[[nodiscard]] std::uint16_t tree_node_bit(TreeNode node);

enum class BuildingType : std::uint8_t {
    Depot = 0,    // 物资站：产出物资
    Reactor,      // 能源站：产出能源
    Barracks,     // 兵营：恢复人力 + 提高上限
    Lab,          // 研究所：产出科技
    Tower,        // 防御塔：只能盖在"自家领地 + 可建造空地"上
    Workshop,     // 工程站：训练工程兵
    Count,
};

[[nodiscard]] const char* to_string(BuildingType type);

struct CardDef {
    int id;
    const char* name;       // UTF-8
    Alliance alliance;      // 只属于这个阵营的卡池
    CardKind kind;
    UnitType unit;          // kind == Unit / Building 时有效
    BuildingType building;  // kind == Building 时有效
    int count;              // 一次生成的单位数
    SpellKind spell;
    int radius;
    int amount;          // 伤害 / 治疗量
    int duration_ticks;  // 减速 / 增益持续
    int building_hp;
    // 四资源价格：全部满足才能打出
    int cost_manpower;
    int cost_materiel;
    int cost_energy;
    int cost_tech;
};

inline constexpr int kCardsPerAlliance = 10;
inline constexpr int kCardCount = kCardsPerAlliance * kAllianceCount;

[[nodiscard]] const CardDef& card_def(int id);
[[nodiscard]] const char* card_name(int id);
// 某个阵营的卡池（7 个 id）
[[nodiscard]] const std::array<int, kCardsPerAlliance>& card_pool(Alliance alliance);

// ---- 运行时对象 ----------------------------------------------------------

struct Unit {
    int id = 0;
    int owner = 0;
    UnitType type = UnitType::Militia;
    int x = 0;
    int y = 0;
    int hp = 0;
    int max_hp = 0;
    int cd = 0;
    int slow_ticks = 0;
    int buff_ticks = 0;
    int order_x = -1;   // 兵营指定的攻击区域（-1 = 没有）
    int order_y = -1;
    int stuck_ticks = 0;  // 连续走不动的 tick 数：用来触发绕行，避免卡在湖湾
    // 亚像素移动累加器：speed 是"像素/秒"，每 tick 只能移动整数像素，
    // 余数攒起来，避免 56px/s 被截成 2px/tick（40px/s）这种误差。
    int move_acc = 0;
    // A* 路径缓存：只在目标变了或者走完时重算
    int path_target_x = -1;
    int path_target_y = -1;
    int path_tick = 0;
    std::vector<Point> path;
};

struct Building {
    int id = 0;
    int owner = 0;
    BuildingType type = BuildingType::Depot;
    int x = 0;
    int y = 0;
    int hp = 0;
    int max_hp = 0;
    int yield_ticks = 0;  // 产出计时
    int train_ticks = 0;  // 兵营训练计时
    int build_ticks = 0;  // 建造进度（完工前不产出）
    int build_total = 0;  // 总共要多少 tick
    bool ready = true;    // 建好了没
    int train_unit = -1;  // 兵营：指定训练的兵种（-1 = 用阵营默认）
    int rally_x = -1;     // 兵营：指定攻击区域（-1 = 没有）
    int rally_y = -1;
};

struct Tower {
    int owner = 0;
    int id = 0;            // 稳定 id：修/拆都用它，数组下标会变但 id 不变
    int build_ticks = 0;   // 建造进度（塔卡放下后先是工地）
    int build_total = 0;
    [[nodiscard]] bool complete() const { return build_ticks >= build_total; }
    int x = 0;
    int y = 0;
    int hp = 0;
    int max_hp = 0;
    int cd = 0;
};

// 法术的表现层残影：只是一个"这里放了个技能"的事实，客户端自己决定怎么画。
struct Effect {
    std::uint8_t kind = 0;  // SpellKind
    std::uint8_t owner = 0;
    int x = 0;
    int y = 0;
    int radius = 0;
    int ttl = 0;
};

enum class MatchPhase : std::uint8_t {
    WaitingForDeck = 0,
    Playing,
    Finished,
};

enum class WinReason : std::uint8_t {
    None = 0,
    BaseDestroyed,
    TimeoutBaseHp,
    TimeoutDraw,
    Surrender,
};

[[nodiscard]] const char* to_string(WinReason reason);

// ---- 昼夜与天气 ----------------------------------------------------------
//
// 昼/夜按固定周期循环（不影响确定性：只跟 tick 有关）。
// 天气每 40~70 秒随机换一次，只影响射程/移速这种"手感"参数，不改规则。
enum class Weather : std::uint8_t {
    Clear = 0,  // 晴
    Rain,       // 雨：移速 -10%
    Fog,        // 雾：索敌/塔射程 -25%
    Storm,      // 风暴：移速 -5%，能源产出 +25%
    Count,
};

inline constexpr int kDayCycleTicks = 20 * 120;  // 2 分钟一昼夜
inline constexpr int kNightStartPercent = 55;    // 周期过了 55% 就入夜

[[nodiscard]] const char* to_string(Weather weather);

// ---- 总部支援（对地打击） -------------------------------------------------
//
// 打到解锁时间之后，用"领地"买一次全图打击 —— 目的不是加内容，
// 是给僵持局一个**确定性的结束手段**。
// 价格/门槛见 MatchConfig::hq_cost_territory（放在那里是因为测试和 --fast 会覆盖它，
// 写死成常量就会出现"改了常量但没人用"的坑）。
inline constexpr int kHqUnlockDefaultSeconds = 25 * 60;
// 领地：开局的领地是主基地周围一圈（四家一样大），之后靠占资源点向外扩。
inline constexpr int kInitialTerritoryTiles = 12;  // 半径（格）
inline constexpr int kNodeTerritoryTiles = 10;     // 占下一个资源点扩张的半径
inline constexpr int kTerritoryTilesPerPoint = 20;  // 多少格算 1 点领地资源
inline constexpr int kTowerMinSpacing = 120;        // 两座塔之间的最小间距
// 塔不再开局赠送，也不再进卡组：只能走工具栏「造塔」直接建，有数量上限（领地越大能多建一座）
inline constexpr int kStartTowerCount = 0;
inline constexpr int kTowerCapBase = 1;
inline constexpr int kTowerCapPerTiles = 90;
inline constexpr int kTowerCapMax = 3;
inline constexpr int kTowerSiteHpPercent = 20;   // 工地血量占满血的比例
// 塔不再走卡牌：工具栏「造塔」直接建造，固定槽位上限见 tower_cap
inline constexpr int kTowerBuildMateriel = 70;
inline constexpr int kTowerBuildEnergy = 30;
// 只有攻城兵能有效拆建筑：普通兵 35%，攻城兵 300%
inline constexpr int kNormalBuildingDamagePercent = 35;
inline constexpr int kSiegeBuildingDamagePercent = 300;
inline constexpr int kStructureSpacing = 100;      // 任何两座建筑/塔的中心最小间距（不许重叠）
// 建筑/塔占固定 4×4 格（80×80 像素），放置时对齐到地板格 —— 形状和面积都是固定的
inline constexpr int kFootprintTiles = 4;

inline constexpr int kBaseClearance = 120;         // 新建筑离自家/别家基地的最小距离
inline constexpr int kHqDamage = 900;
inline constexpr int kHqRadius = 320;
inline constexpr int kHqCooldownTicks = 20 * 20;  // 20 秒
inline constexpr int kTerritoryPerNodeTicks = 60;   // 每 3 秒给 1 点领地（每个点）

// ---- 高战略点与随机事件 ---------------------------------------------------
inline constexpr int kRichActivateSeconds = 5 * 60;  // 5 分钟后高战略点亮起
inline constexpr int kEventFirstTick = 20 * 60;      // 60 秒后开始刷随机事件
inline constexpr int kEventIntervalTicks = 20 * 75;  // 之后每 75 秒一次
inline constexpr int kSupplyDropMateriel = 60;
inline constexpr int kSupplyDropEnergy = 30;
inline constexpr int kSurgeTicks = 20 * 60;  // 矿脉爆发持续 60 秒

enum class RandomEvent : std::uint8_t {
    None = 0,
    SupplyDrop,   // 补给空投：随机一家拿一堆物资/能源
    NodeSurge,    // 矿脉爆发：随机一个资源点产量翻倍
    Count,
};

// ---- 反克制表 ------------------------------------------------------------
[[nodiscard]] int counter_percent(UnitType attacker, UnitType defender);

}  // namespace kingdom_fight::rts
