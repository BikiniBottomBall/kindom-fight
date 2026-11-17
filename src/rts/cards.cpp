#include "rts/types.h"

#include <array>

namespace kingdom_fight::rts {
namespace {

// 单位数值表。注意 speed 的单位是「像素/秒」——每 tick 只走整数像素，余数由 move_acc 累加。
// 单位不再由卡牌召唤，而是兵营按 kTrainTicks 定时训练，成本写在表里。
constexpr std::array<UnitStat, kUnitTypeCount> kUnitStats{{
    // hp, dmg, atk, range, speed, aggro, manpower, materiel
    // 数值整体下调：伤害约 -40%、速度约 -30% —— 打满血塔要围很久，森林/高地这些地形 debuff 才明显
    {110, 6, 13, 40, 130, 220, 10, 8, 20 * 5},     // Militia：5 秒
    {150, 9, 12, 44, 130, 220, 12, 10, 20 * 6},    // Spear：6 秒
    {420, 8, 18, 42, 90, 200, 20, 16, 20 * 8},     // Shield：8 秒（肉盾更慢）
    {220, 16, 11, 44, 240, 260, 18, 18, 20 * 7},   // Rider：7 秒
    {120, 11, 14, 220, 95, 260, 14, 12, 20 * 6},   // Archer：6 秒
    {520, 38, 28, 70, 64, 200, 24, 20, 20 * 12},   // Siege：12 秒
    {900, 22, 20, 55, 92, 220, 36, 30, 20 * 14},   // Behemoth：14 秒
    {140, 2, 40, 30, 120, 90, 8, 6, 20 * 8},       // Engineer：8 秒
    // 攻城兵：速度极慢、血厚；对建筑 300%，对兵 40%
    {700, 34, 40, 44, 62, 160, 30, 24, 20 * 15},   // Ram：15 秒
    {430, 62, 60, 330, 52, 300, 34, 28, 20 * 18},  // Catapult：18 秒
    {380, 48, 46, 360, 66, 320, 30, 26, 20 * 17},  // Ballista：17 秒
    {540, 82, 70, 300, 46, 280, 38, 32, 20 * 20},  // Cannon：20 秒
}};

// 28 张卡 = 四个阵营各 7 张，全部是「建筑 + 法术」。
// 兵种不再进卡池：兵营自己按时间训练（见 trained_unit）。
constexpr std::array<CardDef, kCardCount> kCards{{
    // ---------------- 先锋军团 ----------------
    {0, "物资站", Alliance::Vanguard, CardKind::Building, UnitType::Militia, BuildingType::Depot, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 16, 0, 0},
    {1, "能源站", Alliance::Vanguard, CardKind::Building, UnitType::Militia, BuildingType::Reactor, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 18, 0, 0},
    {2, "兵营", Alliance::Vanguard, CardKind::Building, UnitType::Militia, BuildingType::Barracks, 1,
     SpellKind::None, 0, 0, 0, 320, 0, 18, 8, 0},
    {3, "研究所", Alliance::Vanguard, CardKind::Building, UnitType::Militia, BuildingType::Lab, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 22, 0, 0},
    {4, "轨道炮击", Alliance::Vanguard, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Fire, 150, 140, 0, 0, 0, 0, 16, 10},
    {5, "壁垒号令", Alliance::Vanguard, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Rally, 200, 0, 120, 0, 0, 0, 8, 0},
    {6, "应急治疗", Alliance::Vanguard, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Mend, 170, 180, 0, 0, 0, 0, 10, 0},

    // ---------------- 联合商会 ----------------
    {7, "物资站", Alliance::Combine, CardKind::Building, UnitType::Militia, BuildingType::Depot, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 16, 0, 0},
    {8, "能源站", Alliance::Combine, CardKind::Building, UnitType::Militia, BuildingType::Reactor, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 18, 0, 0},
    {9, "兵营", Alliance::Combine, CardKind::Building, UnitType::Militia, BuildingType::Barracks, 1,
     SpellKind::None, 0, 0, 0, 320, 0, 18, 8, 0},
    {10, "研究所", Alliance::Combine, CardKind::Building, UnitType::Militia, BuildingType::Lab, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 22, 0, 0},
    {11, "火雨", Alliance::Combine, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Fire, 160, 120, 0, 0, 0, 0, 16, 10},
    {12, "应急治疗", Alliance::Combine, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Mend, 170, 180, 0, 0, 0, 0, 10, 0},
    {13, "冰霜", Alliance::Combine, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Frost, 180, 0, 60, 0, 0, 0, 12, 0},

    // ---------------- 灵能教团 ----------------
    {14, "物资站", Alliance::Order, CardKind::Building, UnitType::Militia, BuildingType::Depot, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 16, 0, 0},
    {15, "能源站", Alliance::Order, CardKind::Building, UnitType::Militia, BuildingType::Reactor, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 18, 0, 0},
    {16, "兵营", Alliance::Order, CardKind::Building, UnitType::Militia, BuildingType::Barracks, 1,
     SpellKind::None, 0, 0, 0, 320, 0, 18, 8, 0},
    {17, "研究所", Alliance::Order, CardKind::Building, UnitType::Militia, BuildingType::Lab, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 22, 0, 0},
    {18, "冰霜", Alliance::Order, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Frost, 180, 0, 60, 0, 0, 0, 12, 0},
    {19, "火雨", Alliance::Order, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Fire, 160, 120, 0, 0, 0, 0, 16, 10},
    {20, "应急治疗", Alliance::Order, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Mend, 170, 180, 0, 0, 0, 0, 10, 0},

    // ---------------- 蜂群 ----------------
    {21, "物资站", Alliance::Swarm, CardKind::Building, UnitType::Militia, BuildingType::Depot, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 16, 0, 0},
    {22, "能源站", Alliance::Swarm, CardKind::Building, UnitType::Militia, BuildingType::Reactor, 1,
     SpellKind::None, 0, 0, 0, 280, 0, 18, 0, 0},
    {23, "兵营", Alliance::Swarm, CardKind::Building, UnitType::Militia, BuildingType::Barracks, 1,
     SpellKind::None, 0, 0, 0, 320, 0, 18, 8, 0},
    {24, "研究所", Alliance::Swarm, CardKind::Building, UnitType::Militia, BuildingType::Lab, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 22, 0, 0},
    {25, "战吼", Alliance::Swarm, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Rally, 180, 0, 100, 0, 0, 0, 8, 0},
    {26, "应急治疗", Alliance::Swarm, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Mend, 170, 180, 0, 0, 0, 0, 10, 0},
    {27, "冰霜", Alliance::Swarm, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Frost, 180, 0, 60, 0, 0, 0, 12, 0},

    // ---------------- 防御塔（四家通用，只能盖在"自家领地 + 可建造空地"） ----------------
    // 塔不再进卡池（改用工具栏「造塔」，有固定槽位）；这四张换成辅助牌
    {28, "战地救护", Alliance::Vanguard, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Mend, 220, 90, 0, 0, 0, 0, 20, 10},
    {29, "贸易空投", Alliance::Combine, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Rally, 240, 0, 160, 0, 0, 0, 14, 8},
    {30, "灵能迟滞", Alliance::Order, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Frost, 240, 0, 120, 0, 0, 0, 16, 8},
    {31, "虫群狂热", Alliance::Swarm, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Rally, 260, 0, 160, 0, 0, 0, 14, 8},

    // ---------------- 工程站 + 开路（四家各一张） ----------------
    {32, "工程站", Alliance::Vanguard, CardKind::Building, UnitType::Engineer, BuildingType::Workshop, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 20, 6, 0},
    {33, "工程站", Alliance::Combine, CardKind::Building, UnitType::Engineer, BuildingType::Workshop, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 20, 6, 0},
    {34, "工程站", Alliance::Order, CardKind::Building, UnitType::Engineer, BuildingType::Workshop, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 20, 6, 0},
    {35, "工程站", Alliance::Swarm, CardKind::Building, UnitType::Engineer, BuildingType::Workshop, 1,
     SpellKind::None, 0, 0, 0, 300, 0, 20, 6, 0},
    {36, "开路", Alliance::Vanguard, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Clear, 150, 0, 0, 0, 0, 0, 8, 0},
    {37, "开路", Alliance::Combine, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Clear, 150, 0, 0, 0, 0, 0, 8, 0},
    {38, "开路", Alliance::Order, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Clear, 150, 0, 0, 0, 0, 0, 8, 0},
    {39, "开路", Alliance::Swarm, CardKind::Spell, UnitType::Militia, BuildingType::Depot, 0,
     SpellKind::Clear, 150, 0, 0, 0, 0, 0, 8, 0},
}};

constexpr std::array<const char*, kUnitTypeCount> kUnitNames{
    {"militia", "spear", "shield", "rider", "archer", "siege", "behemoth", "engineer",
     "ram", "catapult", "ballista", "cannon"}};

constexpr std::array<const char*, static_cast<std::size_t>(BuildingType::Count)> kBuildingNames{
    {"depot", "reactor", "barracks", "lab", "tower", "workshop"}};

constexpr std::array<const char*, static_cast<std::size_t>(Alliance::Count)> kAllianceNames{
    {"vanguard", "combine", "order", "swarm"}};

constexpr std::array<const char*, static_cast<std::size_t>(Alliance::Count)> kAlliancePassives{
    {"buildings and towers +25% HP", "materiel output +25%", "energy +40%, tech +25%",
     "manpower cap +50, regen +25%"}};

constexpr std::array<const char*, static_cast<std::size_t>(Weather::Count)> kWeatherNames{
    {"clear", "rain", "fog", "storm"}};

}  // namespace

namespace {
constexpr std::uint16_t bit_of(int index) {
    return static_cast<std::uint16_t>(1u << index);
}
constexpr std::uint16_t kBitMilitaryDrill = bit_of(0);
constexpr std::uint16_t kBitMilitaryElite = bit_of(1);
constexpr std::uint16_t kBitMilitaryFury = bit_of(2);
constexpr std::uint16_t kBitMilitaryMachine = bit_of(3);
constexpr std::uint16_t kBitEconomyDepot = bit_of(4);
constexpr std::uint16_t kBitEconomyPower = bit_of(5);
constexpr std::uint16_t kBitEconomyTrade = bit_of(6);
constexpr std::uint16_t kBitEconomyHub = bit_of(7);
constexpr std::uint16_t kBitEngineeringBasics = bit_of(8);
constexpr std::uint16_t kBitEngineeringFortify = bit_of(9);
constexpr std::uint16_t kBitEngineeringSpeed = bit_of(10);
constexpr std::uint16_t kBitEngineeringCitadel = bit_of(11);

constexpr std::array<TreeNodeInfo, kTreeNodeCount> kTreeNodes{{
    // ---------- 军政 ----------
    {"训练加速", "兵营训练 -10%", 10, 0, 0, 0, 0, 0},
    {"精兵路线", "小兵血量 +10%（与狂战互斥）", 18, 0, 1, kBitMilitaryDrill, 0,
     kBitMilitaryFury},
    {"狂战路线", "小兵攻击 +12%（与精兵互斥）", 18, 0, 1, kBitMilitaryDrill, 0,
     kBitMilitaryElite},
    {"战争机器", "训练再 -15%：需要 训练加速 + 任一路线", 30, 0, 3, kBitMilitaryDrill,
     static_cast<std::uint16_t>(kBitMilitaryElite | kBitMilitaryFury), 0},
    // ---------- 经济 ----------
    {"仓储", "物资产出 +10%", 10, 1, 0, 0, 0, 0},
    {"能源网", "能源产出 +15%（与贸易互斥）", 18, 1, 1, kBitEconomyDepot, 0, kBitEconomyTrade},
    {"贸易路线", "卡牌费用 -10%（与能源网互斥）", 18, 1, 1, kBitEconomyDepot, 0,
     kBitEconomyPower},
    {"工业枢纽", "建筑上限 +1：需要 仓储 + 训练加速 + 任一路线", 30, 1, 3,
     static_cast<std::uint16_t>(kBitEconomyDepot | kBitMilitaryDrill),
     static_cast<std::uint16_t>(kBitEconomyPower | kBitEconomyTrade), 0},
    // ---------- 工程 ----------
    {"工程学", "建造时间 -15%", 10, 2, 0, 0, 0, 0},
    {"加固", "建筑血量 +20%（与快建互斥）", 18, 2, 1, kBitEngineeringBasics, 0,
     kBitEngineeringSpeed},
    {"快速施工", "工程兵建造加成 +25%（与加固互斥）", 18, 2, 1, kBitEngineeringBasics, 0,
     kBitEngineeringFortify},
    {"要塞", "防御塔血量 +15%：需要 工业枢纽 + 任一路线", 30, 2, 3, kBitEconomyHub,
     static_cast<std::uint16_t>(kBitEngineeringFortify | kBitEngineeringSpeed), 0},
}};
}  // namespace

const TreeNodeInfo& tree_node(TreeNode node) {
    const auto index = static_cast<std::size_t>(node);
    if (index >= kTreeNodes.size()) return kTreeNodes[0];
    return kTreeNodes[index];
}

std::uint16_t tree_node_bit(TreeNode node) {
    const auto index = static_cast<std::size_t>(node);
    if (index >= kTreeNodeCount) return 0;
    return static_cast<std::uint16_t>(1u << index);
}
const UnitStat& unit_stat(UnitType type) {
    const int index = static_cast<int>(type);
    if (index < 0 || index >= kUnitTypeCount) return kUnitStats[0];
    return kUnitStats[static_cast<std::size_t>(index)];
}

UnitType workshop_unit() { return UnitType::Engineer; }

UnitType trained_unit(Alliance alliance) {
    switch (alliance) {
        case Alliance::Vanguard: return UnitType::Shield;   // 重装
        case Alliance::Combine:  return UnitType::Rider;    // 机动
        case Alliance::Order:    return UnitType::Archer;   // 远程
        case Alliance::Swarm:    return UnitType::Militia;  // 数量
        case Alliance::Count:    break;
    }
    return UnitType::Militia;
}

const char* to_string(UnitType type) {
    const int index = static_cast<int>(type);
    if (index < 0 || index >= kUnitTypeCount) return "unknown";
    return kUnitNames[static_cast<std::size_t>(index)];
}

const char* to_string(BuildingType type) {
    const auto index = static_cast<std::size_t>(type);
    if (index >= kBuildingNames.size()) return "unknown";
    return kBuildingNames[index];
}

const char* to_string(Alliance alliance) {
    const auto index = static_cast<std::size_t>(alliance);
    if (index >= kAllianceNames.size()) return "unknown";
    return kAllianceNames[index];
}

const char* alliance_passive(Alliance alliance) {
    const auto index = static_cast<std::size_t>(alliance);
    if (index >= kAlliancePassives.size()) return "";
    return kAlliancePassives[index];
}

const char* to_string(Weather weather) {
    const auto index = static_cast<std::size_t>(weather);
    if (index >= kWeatherNames.size()) return "unknown";
    return kWeatherNames[index];
}

const char* to_string(NodeKind kind) {
    switch (kind) {
        case NodeKind::Materiel: return "materiel";
        case NodeKind::Energy:   return "energy";
        case NodeKind::Rich:     return "rich";
        case NodeKind::Town:     return "town";
        case NodeKind::Count:    break;
    }
    return "unknown";
}

const CardDef& card_def(int id) {
    if (id < 0 || id >= kCardCount) return kCards[0];
    return kCards[static_cast<std::size_t>(id)];
}

const char* card_name(int id) { return card_def(id).name; }

const std::array<int, kCardsPerAlliance>& card_pool(Alliance alliance) {
    static const std::array<std::array<int, kCardsPerAlliance>, kAllianceCount> kPools{{
        {{0, 1, 2, 3, 4, 5, 6, 28, 32, 36}},
        {{7, 8, 9, 10, 11, 12, 13, 29, 33, 37}},
        {{14, 15, 16, 17, 18, 19, 20, 30, 34, 38}},
        {{21, 22, 23, 24, 25, 26, 27, 31, 35, 39}},
    }};
    const auto index = static_cast<std::size_t>(alliance);
    if (index >= kPools.size()) return kPools[0];
    return kPools[index];
}

const char* to_string(WinReason reason) {
    switch (reason) {
        case WinReason::None:          return "none";
        case WinReason::BaseDestroyed: return "base_destroyed";
        case WinReason::TimeoutBaseHp: return "timeout_base_hp";
        case WinReason::TimeoutDraw:   return "timeout_draw";
        case WinReason::Surrender:     return "surrender";
    }
    return "unknown";
}

int counter_percent(UnitType attacker, UnitType defender) {
    if (attacker == UnitType::Spear && defender == UnitType::Rider) return 150;
    if (attacker == UnitType::Rider &&
        (defender == UnitType::Archer || defender == UnitType::Siege)) {
        return 150;
    }
    if (attacker == UnitType::Archer && defender == UnitType::Shield) return 60;
    return 100;
}

}  // namespace kingdom_fight::rts
