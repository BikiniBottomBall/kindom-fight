// RTS 内核单测：多资源经济、地形、4 人规则、确定性。
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "rts/ai.h"
#include "rts/sim.h"
#include "rts/types.h"
#include "test_framework.h"

using namespace kingdom_fight::rts;

namespace {

// 阵营专属卡池：测试统一用联合商会（没有血量加成，断言好写）
inline constexpr Alliance kTestAlliance = Alliance::Combine;

std::array<std::vector<int>, kPlayers> make_decks() {
    const auto& pool = card_pool(kTestAlliance);
    std::array<std::vector<int>, kPlayers> decks;
    for (auto& deck : decks) deck = std::vector<int>(pool.begin(), pool.begin() + kDeckSize);
    return decks;
}

std::array<std::vector<int>, kPlayers> make_decks_with(int slot0_card) {
    auto decks = make_decks();
    decks[0][0] = slot0_card;
    return decks;
}

// 不带任何血量加成的联盟组合（用来断言"标准值"）
std::array<Alliance, kPlayers> make_factions() {
    std::array<Alliance, kPlayers> factions{};
    for (auto& alliance : factions) alliance = kTestAlliance;
    return factions;
}

// 朝地图中心挪 distance 像素：出生点可能贴着边缘，直接 +200 会越界
Point toward_center(const Sim& sim, int p, int distance) {
    const Point base = sim.base_position(p);
    const int dx = kCenter.x - base.x;
    const int dy = kCenter.y - base.y;
    int len = dx * dx + dy * dy;
    int root = len == 0 ? 1 : len;
    while (true) {
        const int next = (root + len / (root == 0 ? 1 : root)) / 2;
        if (next >= root) break;
        root = next;
    }
    const int norm = std::max(1, root);
    return Point{base.x + dx * distance / norm, base.y + dy * distance / norm};
}

std::string digest(const Sim& sim) {
    std::string out;
    for (int p = 0; p < kPlayers; ++p) {
        const PlayerState& state = sim.player(p);
        out += "p" + std::to_string(p) + " base " + std::to_string(state.base_hp) +
               (state.eliminated ? " X" : "");
        out += " res " + std::to_string(state.res.manpower) + "/" +
               std::to_string(state.res.materiel) + "/" + std::to_string(state.res.energy) + "/" +
               std::to_string(state.res.tech);
        out += " b" + std::to_string(state.buildings.size());
        for (const auto& tower : state.towers) out += " t" + std::to_string(tower.hp);
        for (const auto& unit : state.units) {
            out += " u" + std::to_string(unit.id) + ":" + std::to_string(unit.x) + "," +
                   std::to_string(unit.y) + "," + std::to_string(unit.hp);
        }
        out += "\n";
    }
    return out;
}

}  // namespace

TEST(SimRequiresFourValidDecks) {
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}));
    CHECK(sim.phase() == MatchPhase::Playing);

    Sim bad;
    auto short_deck = make_decks();
    short_deck[2] = {0, 1, 2};
    CHECK(!bad.start(short_deck, MatchConfig{}));

    auto out_of_range = make_decks();
    out_of_range[3] = {0, 1, 2, 3, 4, 99};
    CHECK(!bad.start(out_of_range, MatchConfig{}));
}

TEST(StartingResourcesMatchDesign) {
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    for (int p = 0; p < kPlayers; ++p) {
        const Resources& res = sim.resources(p);
        CHECK_EQ(res.manpower, kStartManpower);
        CHECK_EQ(res.materiel, kStartMateriel);
        CHECK_EQ(res.energy, kStartEnergy);
        CHECK_EQ(res.tech, kStartTech);
        CHECK_EQ(sim.base_hp(p), kBaseHp);
        CHECK_EQ(static_cast<int>(sim.player(p).towers.size()), kTowersPerPlayer);
    }
    CHECK_EQ(sim.alive_count(), kPlayers);
    CHECK_EQ(sim.player(0).hand(0), card_pool(kTestAlliance)[0]);
}

TEST(SpawnPointsAreRandomButDeterministic) {
    auto decks = make_decks();
    Sim a;
    Sim b;
    MatchConfig config;
    config.spawn_seed = 4242;
    CHECK(a.start(decks, config));
    CHECK(b.start(decks, config));

    for (int p = 0; p < kPlayers; ++p) {
        const Point pa = a.base_position(p);
        const Point pb = b.base_position(p);
        CHECK_EQ(pa.x, pb.x);
        CHECK_EQ(pa.y, pb.y);
        CHECK(a.terrain().passable(pa.x, pa.y));
    }

    // 换种子就应该换一套出生点（不保证每个点都不同，但整体不能一模一样）
    MatchConfig other;
    other.spawn_seed = 999;
    Sim c;
    CHECK(c.start(decks, other));
    int differences = 0;
    for (int p = 0; p < kPlayers; ++p) {
        if (c.base_position(p).x != a.base_position(p).x ||
            c.base_position(p).y != a.base_position(p).y) {
            ++differences;
        }
    }
    CHECK(differences > 0);
}

TEST(UnitsOnlyInsideDeployRadius) {
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}));
    const Point base = sim.base_position(0);

    CHECK(sim.play_card(0, 0, toward_center(sim, 0, 200).x, toward_center(sim, 0, 200).y) ==
          PlayResult::Ok);
    CHECK(sim.play_card(0, 0, kFieldW - 200, kFieldH - 200) == PlayResult::BadPlacement);
    CHECK(sim.play_card(0, 0, -50, 300) == PlayResult::BadPlacement);
    // 湖里不行（默认地图左上角那片水：列 6-8、行 4-5）
    CHECK(sim.play_card(0, 0, 7 * kTileSize, 4 * kTileSize + 20) == PlayResult::BadPlacement);
}

TEST(SpellsCanBePlacedAnywhere) {
    Sim sim;
    auto decks = make_decks_with(13);  // 冰霜：12 能源，起始 20 能源够
    CHECK(sim.start(decks, MatchConfig{}));
    CHECK(sim.can_afford(0, 13));
    CHECK(sim.play_card(0, 0, kFieldW - 100, kFieldH - 100) == PlayResult::Ok);
}

TEST(PlayingCardSpendsResourcesAndCyclesHand) {
    // 卡池已经改成「建筑 + 法术」：兵种由兵营训练，不再进手牌
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}));
    const Resources before = sim.resources(0);
    const Point spot = toward_center(sim, 0, 150);

    // 手牌第一张是物资站（16 物资）
    CHECK_EQ(sim.player(0).hand(0), card_pool(kTestAlliance)[0]);
    CHECK(sim.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);
    CHECK_EQ(sim.resources(0).materiel, before.materiel - 16);
    CHECK_EQ(static_cast<int>(sim.player(0).rotation.size()), kDeckSize);
    CHECK_EQ(sim.player(0).rotation.back(), card_pool(kTestAlliance)[0]);

    // 第二张：能源站（18 物资）
    CHECK_EQ(sim.player(0).hand(0), card_pool(kTestAlliance)[1]);
    CHECK(sim.play_card(0, 0, spot.x + 110, spot.y) == PlayResult::Ok);
    CHECK_EQ(sim.resources(0).materiel, before.materiel - 34);

    // 第三张：兵营（18 物资 + 8 能源）—— 物资只剩 26，够
    CHECK_EQ(sim.player(0).hand(0), card_pool(kTestAlliance)[2]);
    CHECK(sim.play_card(0, 0, spot.x + 220, spot.y) == PlayResult::Ok);
    CHECK_EQ(sim.resources(0).materiel, before.materiel - 52);
    CHECK_EQ(sim.resources(0).energy, before.energy - 8);

    // 第四张：研究所（22 物资）—— 开局资源调高后还出得起，正好验证"手牌继续轮转"
    CHECK_EQ(sim.player(0).hand(0), card_pool(kTestAlliance)[3]);
    CHECK(sim.can_afford(0, card_pool(kTestAlliance)[3]));
    {
        const Point base = sim.base_position(0);
        bool placed4 = false;
        for (int dy = -240; dy <= 240 && !placed4; dy += 80) {
            for (int dx = -240; dx <= 240 && !placed4; dx += 80) {
                if (dx * dx + dy * dy < 120 * 120) continue;
                if (sim.play_card(0, 0, base.x + dx, base.y + dy) == PlayResult::Ok) placed4 = true;
            }
        }
        CHECK(placed4);
    }
    CHECK_EQ(sim.resources(0).materiel, before.materiel - 74);
}

TEST(BarracksTrainsUnitsOverTime) {
    // 兵营每 kTrainTicks 训练一个单位，不是靠卡牌召唤
    const int barracks_card = card_pool(kTestAlliance)[2];
    auto decks = make_decks();
    decks[0][0] = barracks_card;
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    const Point spot = toward_center(sim, 0, 160);
    CHECK(sim.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);
    CHECK(sim.player(0).units.empty());

    // 建筑要 kBuildTicks 才完工，完工后才开始训练
    for (int i = 0; i < kBuildTicks + 2; ++i) sim.advance();
    for (int i = 0; i < kTrainTicks - 20; ++i) sim.advance();
    CHECK(sim.player(0).units.empty());  // 还没到时间

    for (int i = 0; i < 40; ++i) sim.advance();
    CHECK(!sim.player(0).units.empty());  // 训练出来了
    if (sim.player(0).units.empty()) return;
    CHECK_EQ(static_cast<int>(sim.player(0).units.front().type),
             static_cast<int>(trained_unit(kTestAlliance)));
}

TEST(TowerNeedsOwnTerritoryAndOpenGround) {
    // 塔不再进卡池：走 build_tower（固定槽位 + 70 物 / 30 能）
    auto decks = make_decks();
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    const Point base = sim.base_position(0);
    const int towers_before = static_cast<int>(sim.player(0).towers.size());
    const Resources before = sim.resources(0);

    // 1) 远离基地（不是领地）→ 拒绝
    CHECK(sim.build_tower(0, base.x - 1500, base.y) == PlayResult::BadPlacement);
    // 2) 领地内空地 → 接受，并扣资源
    CHECK(sim.build_tower(0, base.x + 170, base.y + 40) == PlayResult::Ok);
    CHECK_EQ(static_cast<int>(sim.player(0).towers.size()), towers_before + 1);
    CHECK_EQ(sim.resources(0).materiel, before.materiel - kTowerBuildMateriel);
    CHECK_EQ(sim.resources(0).energy, before.energy - kTowerBuildEnergy);
    // 3) 贴着刚才那座再盖 → 槽位上限/间距都不允许
    CHECK(sim.build_tower(0, base.x + 190, base.y + 50) != PlayResult::Ok);
}
TEST(DeckNeedsEightCardsAndAtMostTwoCopies) {
    std::array<std::vector<int>, kPlayers> decks;
    const auto& pool = card_pool(kTestAlliance);
    for (auto& deck : decks) deck.clear();

    // 差一张 → 非法
    for (int i = 0; i < kDeckSize - 1; ++i) decks[0].push_back(pool[i % 10]);
    CHECK(!Sim().start(decks, MatchConfig{}, make_factions()));

    // 同一种卡三张 → 非法
    for (auto& deck : decks) deck.clear();
    for (auto& deck : decks) {
        deck.assign({pool[0], pool[0], pool[0]});
        for (int i = 3; i < kDeckSize; ++i) deck.push_back(pool[i]);
    }
    CHECK(!Sim().start(decks, MatchConfig{}, make_factions()));

    // 正常 8 张（允许两张同名）→ 合法
    for (auto& deck : decks) deck.clear();
    for (auto& deck : decks) {
        deck.assign({pool[0], pool[0]});
        for (int i = 1; i < kDeckSize - 1; ++i) deck.push_back(pool[i]);
    }
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
}

TEST(DiscardCyclesHandCardToDeckEnd) {
    Sim sim;
    CHECK(sim.start(make_decks(), MatchConfig{}, make_factions()));
    const int first = sim.player(0).hand(0);
    const int second = sim.player(0).hand(1);
    CHECK(sim.discard(0, 0) == PlayResult::Ok);
    // 弃掉的牌回到牌堆末尾，手牌整体前移
    CHECK_EQ(sim.player(0).hand(0), second);
    CHECK_EQ(sim.player(0).rotation.back(), first);
    CHECK(sim.discard(0, 99) == PlayResult::BadSlot);
}

TEST(GrowthTreeBranchesAreExclusive) {
    Sim sim;
    CHECK(sim.start(make_decks(), MatchConfig{}, make_factions()));
    sim.advance();
    const_cast<Resources&>(sim.resources(0)).tech = 200;

    // 前置没点，后面的节点点不了
    CHECK(!sim.tree_available(0, static_cast<int>(TreeNode::MilitaryElite)));
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::MilitaryElite)) == PlayResult::Locked);

    // 先点根节点
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::MilitaryDrill)) == PlayResult::Ok);
    CHECK(sim.tree_taken(0, static_cast<int>(TreeNode::MilitaryDrill)));
    CHECK_EQ(sim.train_ticks_for(0), kTrainTicks * 100 / 110);

    // 两条分支互斥：点了精兵就不能点狂战
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::MilitaryElite)) == PlayResult::Ok);
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::MilitaryFury)) == PlayResult::Locked);
    CHECK_EQ(sim.unit_hp_percent(0), 110);
    CHECK_EQ(sim.unit_dmg_percent(0), 100);

    // 战争机器需要"任一路线"：已经有精兵，可以点
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::MilitaryMachine)) == PlayResult::Ok);
    CHECK_EQ(sim.train_ticks_for(0), kTrainTicks * 100 / 125);

    // 跨树前置：工业枢纽要 仓储 + 训练加速（军政树）+ 经济树任一路线
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::EconomyHub)) == PlayResult::Locked);
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::EconomyDepot)) == PlayResult::Ok);
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::EconomyHub)) == PlayResult::Locked);  // 还差"任一路线"
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::EconomyTrade)) == PlayResult::Ok);
    const int cap_before = sim.building_cap(0);
    CHECK(sim.upgrade(0, static_cast<int>(TreeNode::EconomyHub)) == PlayResult::Ok);
    CHECK_EQ(sim.building_cap(0), cap_before + 1);
}

TEST(RepairCostsMaterielAndRestoresHp) {
    const int depot = card_pool(kTestAlliance)[0];
    auto decks = make_decks();
    decks[0][0] = depot;
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    const Point spot = toward_center(sim, 0, 140);
    CHECK(sim.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);

    for (int i = 0; i < kBuildTicks + 5; ++i) sim.advance();
    CHECK(sim.player(0).buildings.front().ready);

    // 打掉一半血再修
    const_cast<Building&>(sim.player(0).buildings.front()).hp = 40;
    const int materiel_before = sim.resources(0).materiel;
    CHECK(sim.repair(0, 0, 0) == PlayResult::Ok);
    CHECK_EQ(sim.player(0).buildings.front().hp, sim.player(0).buildings.front().max_hp);
    CHECK(sim.resources(0).materiel < materiel_before);

    // 满血再修 → 拒绝
    CHECK(sim.repair(0, 0, 0) == PlayResult::BadPlacement);
}

TEST(DepotIncreasesMaterielIncome) {
    const int depot = card_pool(kTestAlliance)[0];
    auto decks = make_decks();
    decks[0][0] = depot;

    Sim with_depot;
    Sim baseline;
    CHECK(with_depot.start(decks, MatchConfig{}));
    CHECK(baseline.start(make_decks(), MatchConfig{}));

    CHECK(with_depot.play_card(0, 0, toward_center(with_depot, 0, 150).x,
                               toward_center(with_depot, 0, 150).y) == PlayResult::Ok);

    // 工地不产出：先等它建好，再看 200 tick 的差值
    for (int i = 0; i < kBuildTicks + 10; ++i) {
        with_depot.advance();
        baseline.advance();
    }
    for (int i = 0; i < 200; ++i) {
        with_depot.advance();
        baseline.advance();
    }
    // 产出节奏放缓之后，200 tick 里的差值也变小了（物资站 1/6 tick）
    const int difference = with_depot.resources(0).materiel - baseline.resources(0).materiel;
    CHECK(difference >= 20);
}

TEST(ManpowerRecoversUpToCap) {
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}));
    for (int i = 0; i < 400; ++i) sim.advance();
    CHECK(sim.resources(0).manpower > kStartManpower);
    CHECK(sim.resources(0).manpower <= sim.player(0).manpower_cap());
}

TEST(BuildingsCannotOverlap) {
    auto decks = make_decks();
    // 手牌前两张都换成建筑（物资站 / 能源站），方便连着放
    decks[0][0] = card_pool(kTestAlliance)[0];
    decks[0][1] = card_pool(kTestAlliance)[1];
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    const Point spot = toward_center(sim, 0, 300);
    CHECK(sim.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);
    // 贴着第一座再放 → 判定重叠
    CHECK(sim.play_card(0, 1, spot.x + 40, spot.y) == PlayResult::BadPlacement);
    // 拉开距离就能放
    // 在基地周围撒网格找一个"整块 4×4 都合法"的位置（地形可能有森林/水，还必须离基地 ≤640）
    const Point base = sim.base_position(0);
    bool placed = false;
    for (int dy = -240; dy <= 240 && !placed; dy += 80) {
        for (int dx = -240; dx <= 240 && !placed; dx += 80) {
            if (dx == 0 && dy == 0) continue;
            if (sim.play_card(0, 1, base.x + dx, base.y + dy) == PlayResult::Ok) placed = true;
        }
    }
    CHECK(placed);
}

TEST(BarracksCanSwitchUnitAndSetRally) {
    auto decks = make_decks();
    // 手里第一张换成兵营，方便直接造
    decks[0][0] = card_pool(kTestAlliance)[2];
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    const Point spot = toward_center(sim, 0, 220);
    CHECK(sim.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);

    // 找到刚造的兵营（自家建筑下标）
    int index = -1;
    for (std::size_t i = 0; i < sim.player(0).buildings.size(); ++i) {
        if (sim.player(0).buildings[i].type == BuildingType::Barracks) index = static_cast<int>(i);
    }
    CHECK(index >= 0);

    // 换兵种：兵营能出攻城兵，训练时间按兵种走
    CHECK(sim.set_building_unit(0, index, static_cast<int>(UnitType::Cannon)) == PlayResult::Ok);
    CHECK_EQ(sim.player(0).buildings[static_cast<std::size_t>(index)].train_unit,
             static_cast<int>(UnitType::Cannon));
    CHECK(sim.set_building_unit(0, index, static_cast<int>(UnitType::Behemoth)) ==
          PlayResult::BadPlacement);   // 巨兽不开放
    CHECK(sim.set_building_unit(0, 99, static_cast<int>(UnitType::Militia)) ==
          PlayResult::BadPlacement);

    // 指定攻击区域
    CHECK(sim.set_building_rally(0, index, kCenter.x, kCenter.y) == PlayResult::Ok);
    CHECK_EQ(sim.player(0).buildings[static_cast<std::size_t>(index)].rally_x, kCenter.x);
    CHECK(sim.set_building_rally(0, index, -5, 0) == PlayResult::BadPlacement);
}
TEST(UnitsMarchTowardEnemiesAndTakeTowerFire) {
    // 单位来自兵营训练：先造兵营，等它出兵，再看它会不会往前走
    const int barracks_card = card_pool(kTestAlliance)[2];
    auto decks = make_decks();
    decks[0][0] = barracks_card;
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    CHECK(sim.play_card(0, 0, toward_center(sim, 0, 220).x, toward_center(sim, 0, 220).y) ==
          PlayResult::Ok);

    // 先等工地完工，再等兵营练出第一个兵
    for (int i = 0; i < kBuildTicks + kTrainTicks + 400; ++i) sim.advance();
    CHECK(!sim.player(0).units.empty());
    if (sim.player(0).units.empty()) return;

    // 开局期（前 60 秒）训练出来的兵原地待命，先等过去
    while (sim.tick() < kOpeningTicks + 20) sim.advance();

    const int start_x = sim.player(0).units.front().x;
    const int start_y = sim.player(0).units.front().y;
    bool moved = false;
    bool hurt = false;
    int enemy_tower_damage = 0;

    for (int i = 0; i < 2400; ++i) {
        sim.advance();
        for (const auto& unit : sim.player(0).units) {
            if (unit.x != start_x || unit.y != start_y) moved = true;
            if (unit.hp < unit.max_hp) hurt = true;
        }
        for (int p = 1; p < kPlayers; ++p) {
            for (const auto& tower : sim.player(p).towers) {
                if (tower.hp < tower.max_hp) ++enemy_tower_damage;
            }
        }
    }
    CHECK(moved);
    CHECK(hurt || enemy_tower_damage > 0);
}

TEST(RallyBuffsAlliesInRadius) {
    // 战吼只有蜂群有；单位现在要兵营训练，所以先造兵营、等出兵，再放战吼
    const auto& swarm_pool = card_pool(Alliance::Swarm);
    std::array<std::vector<int>, kPlayers> decks;
    for (auto& deck : decks) deck = {swarm_pool[2], swarm_pool[4], swarm_pool[0],
                                     swarm_pool[1], swarm_pool[3], swarm_pool[5],
                                     swarm_pool[6], swarm_pool[7]};
    std::array<Alliance, kPlayers> factions{};
    for (auto& alliance : factions) alliance = Alliance::Swarm;

    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, factions));

    // 第一张是兵营
    CHECK(sim.play_card(0, 0, toward_center(sim, 0, 150).x, toward_center(sim, 0, 150).y) ==
          PlayResult::Ok);
    for (int i = 0; i < kBuildTicks + kTrainTicks + 400; ++i) sim.advance();
    CHECK(!sim.player(0).units.empty());
    if (sim.player(0).units.empty()) return;

    // 第二张是战吼，丢在自己部队头上
    const int rally = swarm_pool[4];  // 战吼是蜂群池的第 5 张（id 25）
    int rally_slot = -1;
    for (int s = 0; s < kHandSize; ++s) {
        if (sim.player(0).hand(s) == rally) rally_slot = s;
    }
    const Unit& unit = sim.player(0).units.front();
    const int unit_x = unit.x;
    const int unit_y = unit.y;
    CHECK(rally_slot >= 0);
    CHECK(sim.can_afford(0, rally));
    CHECK(sim.play_card(0, rally_slot, unit_x, unit_y) == PlayResult::Ok);

    bool buffed = false;
    for (const auto& soldier : sim.player(0).units) {
        if (soldier.buff_ticks > 0) buffed = true;
    }
    CHECK(buffed);
}

TEST(AlliancePassivesWork) {
    std::array<Alliance, kPlayers> vanguard{};
    std::array<Alliance, kPlayers> combine{};
    std::array<Alliance, kPlayers> order{};
    std::array<Alliance, kPlayers> swarm{};
    for (int p = 0; p < kPlayers; ++p) {
        vanguard[static_cast<std::size_t>(p)] = Alliance::Vanguard;
        combine[static_cast<std::size_t>(p)] = Alliance::Combine;
        order[static_cast<std::size_t>(p)] = Alliance::Order;
        swarm[static_cast<std::size_t>(p)] = Alliance::Swarm;
    }

    // 每个阵营用它自己池子里的牌
    auto deck_for = [](Alliance alliance) {
        const auto& pool = card_pool(alliance);
        std::vector<int> deck(pool.begin(), pool.begin() + kDeckSize);
        return deck;
    };
    std::array<std::vector<int>, kPlayers> vanguard_decks;
    std::array<std::vector<int>, kPlayers> combine_decks;
    std::array<std::vector<int>, kPlayers> order_decks;
    std::array<std::vector<int>, kPlayers> swarm_decks;
    for (auto& deck : vanguard_decks) deck = deck_for(Alliance::Vanguard);
    for (auto& deck : combine_decks) deck = deck_for(Alliance::Combine);
    for (auto& deck : order_decks) deck = deck_for(Alliance::Order);
    for (auto& deck : swarm_decks) deck = deck_for(Alliance::Swarm);

    Sim base;
    Sim comb;
    Sim ener;
    Sim swam;
    CHECK(base.start(vanguard_decks, MatchConfig{}, vanguard));
    CHECK(comb.start(combine_decks, MatchConfig{}, combine));
    CHECK(ener.start(order_decks, MatchConfig{}, order));
    CHECK(swam.start(swarm_decks, MatchConfig{}, swarm));

    // 先锋军团：基地 +25% 血；塔现在走 build_tower，单独验证
    CHECK_EQ(base.base_hp(0), kBaseHp * kVanguardHpPercent / 100);
    CHECK(base.player(0).towers.empty());   // 开局不再送塔
    {
        Sim tower_sim;
        CHECK(tower_sim.start(vanguard_decks, MatchConfig{}, vanguard));
        const Point spot = toward_center(tower_sim, 0, 170);
        CHECK(tower_sim.build_tower(0, spot.x, spot.y) == PlayResult::Ok);
        CHECK_EQ(static_cast<int>(tower_sim.player(0).towers.size()), 1);
        CHECK(tower_sim.player(0).towers[0].max_hp > kTowerHp);   // 先锋塔 +25%
        CHECK(!tower_sim.player(0).towers[0].complete());         // 先是工地
    }
    // 蜂群：人力上限 +50
    CHECK_EQ(swam.player(0).manpower_cap(), kManpowerCapBase + kSwarmCapBonus);

    // 跑久一点：科技产出慢（80 tick 一个），太短的话 +25% 的余数还没攒够 1 点
    for (int i = 0; i < 900; ++i) {
        base.advance();
        comb.advance();
        ener.advance();
        swam.advance();
    }
    // 联合商会物资更多、灵能教团能源/科技更多
    CHECK(comb.resources(0).materiel > base.resources(0).materiel);
    CHECK(ener.resources(0).energy > base.resources(0).energy);
    CHECK(ener.resources(0).tech > base.resources(0).tech);
    // 蜂群的人力不该更少（上限更高 + 恢复更快）
    CHECK(swam.resources(0).manpower >= base.resources(0).manpower);
}

TEST(HqSupportUnlocksAndFires) {
    // 压缩节奏：3 秒解锁，才能在一局短测试里跑到
    MatchConfig config;
    config.match_seconds = 120;
    config.hq_unlock_seconds = 3;
    // 门槛钉成 5：这条测试要断言"够就能打"，不能依赖开局领地恰好是多少
    config.hq_cost_territory = 5;

    Sim sim;
    CHECK(sim.start(make_decks(), config, make_factions()));

    // 没到时间：锁定
    CHECK(sim.call_hq(0, kCenter.x, kCenter.y) == PlayResult::Locked);
    CHECK_EQ(sim.hq_state(0), 0);

    for (int i = 0; i < 80; ++i) sim.advance();  // 4 秒
    // 解锁后：标准门槛下开局领地就够 → 打一发，然后进冷却
    CHECK_EQ(sim.hq_state(0), 1);
    CHECK(sim.call_hq(0, kCenter.x, kCenter.y) == PlayResult::Ok);
    CHECK_EQ(sim.hq_calls(), 1);
    CHECK_EQ(sim.hq_state(0), 3);  // 冷却中
    CHECK(sim.call_hq(0, kCenter.x, kCenter.y) == PlayResult::Cooldown);

    // "领地不够"那条分支：门槛抬到开局领地之上。
    // 不写死 kHqCostTerritory 的具体数值 —— 调价的时候这条测试不用跟着改。
    MatchConfig poor = config;
    poor.hq_cost_territory = 999;
    Sim poor_sim;
    CHECK(poor_sim.start(make_decks(), poor, make_factions()));
    for (int i = 0; i < 80; ++i) poor_sim.advance();
    CHECK_EQ(poor_sim.hq_state(0), 2);
    CHECK(poor_sim.call_hq(0, kCenter.x, kCenter.y) == PlayResult::NotEnoughResources);

    // 跑一局 AI 对打：AI 会抢点攒领地，够了就会砸总部支援
    MatchConfig battle_config = config;
    battle_config.hq_cost_territory = 12;
    Sim battle;
    CHECK(battle.start(make_decks(), battle_config, make_factions()));
    std::array<AiBrain, kPlayers> brains;
    for (int p = 0; p < kPlayers; ++p) brains[static_cast<std::size_t>(p)].reset(p, 900 + p);
    int guard = 0;
    while (battle.phase() == MatchPhase::Playing && guard < 7000) {
        for (auto& brain : brains) brain.update(battle);
        battle.advance();
        ++guard;
    }
    CHECK(battle.hq_calls() > 0);
}

TEST(WeatherAndDayNightCycleAdvance) {
    MatchConfig config;
    config.match_seconds = 400;
    Sim sim;
    CHECK(sim.start(make_decks(), config, make_factions()));

    CHECK(sim.weather() == Weather::Clear);  // 开局 40 秒内不变
    CHECK(!sim.is_night());

    // 跑过一个完整昼夜：白天 → 夜晚
    for (int i = 0; i < kDayCycleTicks * 60 / 100; ++i) sim.advance();
    CHECK(sim.is_night());

    // 天气至少变过一次（40 秒后开始变）
    bool changed = false;
    for (int i = 0; i < 20 * 60 && !changed; ++i) {
        sim.advance();
        if (sim.weather() != Weather::Clear) changed = true;
    }
    CHECK(changed);
}

TEST(MatchEndsWithAValidOutcome) {
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}));

    std::array<AiBrain, kPlayers> brains;
    for (int p = 0; p < kPlayers; ++p) brains[static_cast<std::size_t>(p)].reset(p, 1000 + p);

    int guard = 0;
    while (sim.phase() == MatchPhase::Playing && guard < sim.match_ticks() + 10) {
        for (auto& brain : brains) brain.update(sim);
        sim.advance();
        ++guard;
    }
    CHECK(sim.phase() == MatchPhase::Finished);
    CHECK(sim.winner() >= -1 && sim.winner() < kPlayers);
    CHECK(sim.reason() != WinReason::None);
    CHECK(sim.alive_count() >= 1);
}

TEST(SameInputsProduceSameState) {
    auto decks = make_decks();

    Sim a;
    Sim b;
    MatchConfig config;
    config.terrain_variant = 1;
    config.spawn_seed = 777;
    CHECK(a.start(decks, config));
    CHECK(b.start(decks, config));

    std::array<AiBrain, kPlayers> brains_a;
    std::array<AiBrain, kPlayers> brains_b;
    for (int p = 0; p < kPlayers; ++p) {
        brains_a[static_cast<std::size_t>(p)].reset(p, 200 + p * 7);
        brains_b[static_cast<std::size_t>(p)].reset(p, 200 + p * 7);
    }

    for (int i = 0; i < 1500; ++i) {
        for (auto& brain : brains_a) brain.update(a);
        for (auto& brain : brains_b) brain.update(b);
        a.advance();
        b.advance();
    }

    CHECK_EQ(digest(a), digest(b));
    CHECK_EQ(a.tick(), b.tick());
    CHECK_EQ(a.winner(), b.winner());
}

TEST(BaseExposedUntilTowerIsBuilt) {
    // 塔不再开局赠送：开局就是裸露状态，造好一座完工的塔才"有掩护"
    auto decks = make_decks();
    Sim sim;
    CHECK(sim.start(decks, MatchConfig{}, make_factions()));
    for (int p = 0; p < kPlayers; ++p) {
        CHECK(sim.base_exposed(p));
        CHECK_EQ(sim.towers_lost(p), 0);
    }

    const Point spot = toward_center(sim, 0, 170);
    CHECK(sim.build_tower(0, spot.x, spot.y) == PlayResult::Ok);
    CHECK(sim.base_exposed(0));  // 工地不算掩护

    // 造完之后才算有塔
    for (int i = 0; i < kBuildTicks + 10; ++i) sim.advance();
    CHECK(!sim.base_exposed(0));
    CHECK(sim.player(0).towers[0].complete());
    CHECK_EQ(sim.player(0).towers[0].hp, sim.player(0).towers[0].max_hp);
}

TEST(DemolishFreesTowerSlot) {
    // 拆掉的塔必须立刻把槽位还回来 —— 早版本拆塔后上限仍然被占死，玩家看着"1/3"却造不了。
    Sim sim;
    CHECK(sim.start(make_decks(), MatchConfig{}, make_factions()));
    const Point base = sim.base_position(0);

    // 领地 21~22 → 塔上限 1 + 领地/90 = 1
    CHECK_EQ(sim.tower_cap(0), 1);
    CHECK_EQ(sim.tower_count(0), kStartTowerCount);

    CHECK(sim.build_tower(0, base.x + 170, base.y + 40) == PlayResult::Ok);
    CHECK_EQ(sim.tower_count(0), 1);
    // 槽位满了：第二座必须被拒（而不是先扣了钱才失败）
    const Resources before = sim.resources(0);
    CHECK(sim.build_tower(0, base.x - 170, base.y - 40) == PlayResult::TooManyBuildings);
    CHECK_EQ(sim.resources(0).materiel, before.materiel);

    // 按 id 拆掉 → 槽位立刻释放（不再强制"至少留一座"，基地本身有防守火力）
    const int id = sim.player(0).towers.front().id;
    CHECK(sim.demolish(0, 1, id) == PlayResult::Ok);
    CHECK_EQ(sim.tower_count(0), 0);
    CHECK_EQ(sim.tower_cap(0), 1);
    CHECK(sim.build_tower(0, base.x - 170, base.y - 40) == PlayResult::Ok);
    CHECK_EQ(sim.tower_count(0), 1);
}

TEST(EngineersSpeedUpConstruction) {
    // 工程兵站在工地 220 像素内，建造速度每个 +kEngineerBuildBonus%。
    // 观测点是 Building::build_ticks：同样的楼，有工程兵的一边完工更快。
    const int lab_card = card_pool(kTestAlliance)[3];  // 研究所
    const int workshop_card = 33;                      // 联合商会的工程站

    // 对照组：只有一个研究所，没有工程兵
    auto solo_decks = make_decks();
    solo_decks[0][0] = lab_card;
    Sim solo;
    CHECK(solo.start(solo_decks, MatchConfig{}, make_factions()));
    const Point spot = toward_center(solo, 0, 200);
    CHECK(solo.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);

    // 实验组：先盖工程站，等工程兵出来，再在同一块地上盖研究所
    auto eng_decks = make_decks();
    eng_decks[0][0] = workshop_card;
    eng_decks[0][1] = lab_card;
    Sim eng;
    CHECK(eng.start(eng_decks, MatchConfig{}, make_factions()));
    CHECK(eng.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);

    int guard = 0;
    bool engineer_ready = false;
    while (guard < 2000 && !engineer_ready) {
        eng.advance();
        ++guard;
        for (const auto& unit : eng.player(0).units) {
            if (unit.type == UnitType::Engineer && unit.hp > 0) engineer_ready = true;
        }
    }
    CHECK(engineer_ready);
    if (!engineer_ready) return;

    // 第二栋贴着工程站盖：工程兵就在 60~84 像素的出兵圈上，稳定落在 220 像素内
    const Building& workshop = eng.player(0).buildings.front();
    const int lab_slot = eng.slot_of_card(0, lab_card);
    CHECK(lab_slot >= 0);
    if (lab_slot < 0) return;
    CHECK(eng.play_card(0, lab_slot, workshop.x + 130, workshop.y) == PlayResult::Ok);

    const auto lab_ready = [](const Sim& sim) {
        for (const auto& building : sim.player(0).buildings) {
            if (building.type == BuildingType::Lab && building.ready) return true;
        }
        return false;
    };

    // 两组都用同一个"跑到完工"的循环，只比 ticks
    const auto ticks_to_finish_lab = [&](Sim& sim, int limit) {
        int ticks = 0;
        while (ticks < limit) {
            if (lab_ready(sim)) break;
            sim.advance();
            ++ticks;
        }
        return ticks;
    };

    const int solo_ticks = ticks_to_finish_lab(solo, 2000);
    CHECK(lab_ready(solo));
    const int eng_ticks = ticks_to_finish_lab(eng, 2000);
    CHECK(lab_ready(eng));

    // 没工程兵：满 kBuildTicks；有工程兵：明显更短
    CHECK(solo_ticks >= kBuildTicks);
    CHECK(eng_ticks < solo_ticks);
    CHECK(eng.engineer_bonus(0) >= kEngineerBuildBonus);
}