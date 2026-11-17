// RTS 线协议单测：编码后必须能被自己的解码器读回来，且字段对得上。
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "rts/ai.h"
#include "rts/terrain.h"
#include "rts/wire.h"
#include "test_framework.h"

using namespace kingdom_fight::rts;

namespace {

std::array<std::vector<int>, kPlayers> make_decks() {
    const auto& pool = card_pool(Alliance::Combine);
    std::array<std::vector<int>, kPlayers> decks;
    for (auto& deck : decks) deck = std::vector<int>(pool.begin(), pool.begin() + kDeckSize);
    return decks;
}

// 朝地图中心挪一段：出生点可能贴边，直接 +200 会越界
Point toward_center(const Sim& sim, int p, int distance) {
    const Point base = sim.base_position(p);
    const int dx = kCenter.x - base.x;
    const int dy = kCenter.y - base.y;
    int root = dx * dx + dy * dy;
    if (root <= 0) return base;
    int next = (root + 1) / 2;
    while (next < root) {
        root = next;
        next = (root + (dx * dx + dy * dy) / (root == 0 ? 1 : root)) / 2;
    }
    const int norm = std::max(1, root);
    return Point{base.x + dx * distance / norm, base.y + dy * distance / norm};
}

}  // namespace

TEST(CardPoolCarriesCostsAndNames) {
    const std::string payload = wire::encode_card_pool();
    wire::Reader reader(payload);

    const std::uint16_t count = reader.u16();
    CHECK_EQ(static_cast<int>(count), kCardCount);

    for (int i = 0; i < count; ++i) {
        const std::uint16_t id = reader.u16();
        const std::uint8_t alliance = reader.u8();  // 阵营专属卡池
        const std::uint8_t kind = reader.u8();
        const std::uint16_t manpower = reader.u16();
        const std::uint16_t food = reader.u16();
        const std::uint16_t iron = reader.u16();
        const std::uint16_t tech = reader.u16();
        const std::uint16_t radius = reader.u16();
        const std::uint8_t building = reader.u8();
        const std::uint8_t spell = reader.u8();
        const std::string name = reader.text();
        CHECK(reader.ok());
        CHECK_EQ(static_cast<int>(id), i);
        CHECK(static_cast<int>(alliance) < kAllianceCount);
        CHECK_EQ(static_cast<int>(card_def(i).alliance), static_cast<int>(alliance));
        CHECK(static_cast<int>(kind) <= 2);
        CHECK(!name.empty());
        // 每张卡至少要花一种资源
        CHECK(manpower + food + iron + tech > 0);
        // 范围半径只对法术有意义；建筑类型 255 = 不是建筑
        CHECK_EQ(static_cast<int>(radius), card_def(i).radius);
        if (card_def(i).kind == CardKind::Building) {
            CHECK_EQ(static_cast<int>(building), static_cast<int>(card_def(i).building));
        } else {
            CHECK_EQ(static_cast<int>(building), 255);
        }
        CHECK_EQ(static_cast<int>(spell), static_cast<int>(card_def(i).spell));
    }
    CHECK_EQ(static_cast<int>(reader.remaining()), 0);
}

TEST(DeckPayloadDecodesAndRejectsBadSizes) {
    std::vector<int> cards;
    Alliance alliance = Alliance::Vanguard;

    wire::Writer writer;
    writer.u8(static_cast<std::uint8_t>(kDeckSize));
    // 用 Combine 卡池的前 kDeckSize 张（同卡最多 2 张的校验在 Sim 里，这里只测协议）
    for (int id = 0; id < kDeckSize; ++id) {
        writer.u16(static_cast<std::uint16_t>(card_pool(Alliance::Combine)[id]));
    }
    writer.u8(static_cast<std::uint8_t>(Alliance::Order));
    int preferred_unit = -1;
    CHECK(wire::decode_deck(writer.bytes(), cards, alliance, preferred_unit));
    CHECK_EQ(preferred_unit, -1);  // 老客户端不发这一字节
    CHECK_EQ(static_cast<int>(cards.size()), kDeckSize);
    CHECK(alliance == Alliance::Order);

    wire::Writer too_short;
    too_short.u8(5);
    for (int i = 0; i < 5; ++i) too_short.u16(0);
    too_short.u8(0);
    CHECK(!wire::decode_deck(too_short.bytes(), cards, alliance, preferred_unit));

    wire::Writer out_of_range;
    out_of_range.u8(6);
    for (int i = 0; i < 5; ++i) out_of_range.u16(0);
    out_of_range.u16(99);
    out_of_range.u8(0);
    CHECK(!wire::decode_deck(out_of_range.bytes(), cards, alliance, preferred_unit));

    wire::Writer bad_alliance;
    bad_alliance.u8(6);
    for (int i = 0; i < kDeckSize; ++i) bad_alliance.u16(0);
    bad_alliance.u8(static_cast<std::uint8_t>(Alliance::Count));
    CHECK(!wire::decode_deck(bad_alliance.bytes(), cards, alliance, preferred_unit));
}

TEST(PlayPayloadRoundTrips) {
    wire::Writer writer;
    writer.u8(2);
    writer.i16(1234);
    writer.i16(-56);

    int slot = 0;
    int x = 0;
    int y = 0;
    CHECK(wire::decode_play(writer.bytes(), slot, x, y));
    CHECK_EQ(slot, 2);
    CHECK_EQ(x, 1234);
    CHECK_EQ(y, -56);
    CHECK(!wire::decode_play(std::string("\x01\x02", 2), slot, x, y));
}

TEST(MapPayloadCarriesTerrainAndSpawnPositions) {
    Sim sim;
    auto decks = make_decks();
    MatchConfig config;
    config.terrain_variant = 1;
    config.spawn_seed = 777;
    CHECK(sim.start(decks, config));

    wire::Reader reader(wire::encode_map(sim));
    CHECK_EQ(static_cast<int>(reader.u16()), kCols);
    CHECK_EQ(static_cast<int>(reader.u16()), kRows);
    CHECK_EQ(static_cast<int>(reader.u16()), kTileSize);

    // 每家的基地满血（先锋军团 +30%，所以是逐家下发的）
    for (int p = 0; p < kPlayers; ++p) {
        const int expected = sim.player(p).alliance == Alliance::Vanguard
                                 ? kBaseHp * kVanguardHpPercent / 100
                                 : kBaseHp;
        CHECK_EQ(static_cast<int>(reader.u16()), expected);
    }

    for (int p = 0; p < kPlayers; ++p) {
        const Point base = sim.base_position(p);
        CHECK_EQ(static_cast<int>(reader.i16()), base.x);
        CHECK_EQ(static_cast<int>(reader.i16()), base.y);
    }

    for (int p = 0; p < kPlayers; ++p) {
        CHECK_EQ(static_cast<int>(reader.u8()), static_cast<int>(sim.player(p).alliance));
    }

    const auto tiles = reader.raw(static_cast<std::size_t>(kCols * kRows));
    CHECK_EQ(static_cast<int>(tiles.size()), kCols * kRows);
    CHECK_EQ(static_cast<int>(reader.remaining()), 0);

    const auto& source = sim.terrain().raw();
    bool same = source.size() == tiles.size();
    for (std::size_t i = 0; same && i < tiles.size(); ++i) {
        if (source[i] != tiles[i]) same = false;
    }
    CHECK(same);
}

TEST(SnapshotRoundTripsThroughReader) {
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}));
    const Point spot = toward_center(sim, 0, 200);
    CHECK(sim.play_card(0, 0, spot.x, spot.y) == PlayResult::Ok);
    for (int i = 0; i < 30; ++i) sim.advance();

    wire::Reader reader(wire::encode_snapshot(sim, 0));

    CHECK_EQ(static_cast<int>(reader.u32()), sim.tick());

    const Resources& res = sim.resources(0);
    CHECK_EQ(static_cast<int>(reader.u16()), res.manpower);
    CHECK_EQ(static_cast<int>(reader.u16()), sim.player(0).manpower_cap());
    CHECK_EQ(static_cast<int>(reader.u16()), res.materiel);
    CHECK_EQ(static_cast<int>(reader.u16()), res.energy);
    CHECK_EQ(static_cast<int>(reader.u16()), res.tech);
    CHECK_EQ(static_cast<int>(reader.u16()), res.territory);

    const std::uint8_t hand_count = reader.u8();
    CHECK_EQ(static_cast<int>(hand_count), kHandSize);
    for (int i = 0; i < hand_count; ++i) {
        CHECK_EQ(static_cast<int>(reader.u16()), sim.player(0).hand(i));
    }

    for (int p = 0; p < kPlayers; ++p) {
        CHECK_EQ(static_cast<int>(reader.u16()), sim.base_hp(p));
    }

    const std::uint8_t tower_count = reader.u8();
    CHECK_EQ(static_cast<int>(tower_count), kPlayers * kTowersPerPlayer);
    for (int i = 0; i < tower_count; ++i) {
        const std::uint8_t owner = reader.u8();
        const std::uint8_t index = reader.u8();
        const std::int16_t x = reader.i16();
        const std::int16_t y = reader.i16();
        const std::uint16_t hp = reader.u16();
        const std::uint16_t max_hp = reader.u16();
        CHECK(owner < kPlayers);
        CHECK(index < kTowersPerPlayer);
        CHECK(x >= 0 && x <= kFieldW);
        CHECK(y >= 0 && y <= kFieldH);
        CHECK(hp <= max_hp);
    }

    std::size_t expected_units = 0;
    for (int p = 0; p < kPlayers; ++p) expected_units += sim.player(p).units.size();
    const std::uint16_t unit_count = reader.u16();
    CHECK_EQ(static_cast<int>(unit_count), static_cast<int>(expected_units));
    for (int i = 0; i < unit_count; ++i) {
        reader.u16();
        const std::uint8_t owner = reader.u8();
        const std::uint8_t type = reader.u8();
        reader.i16();
        reader.i16();
        reader.u16();
        reader.u16();
        reader.u8();
        CHECK(owner < kPlayers);
        CHECK(type < static_cast<std::uint8_t>(kUnitTypeCount));
    }

    const std::uint8_t building_count = reader.u8();
    for (int i = 0; i < building_count; ++i) {
        reader.u8();  // owner
        const std::uint8_t type = reader.u8();
        reader.i16();
        reader.i16();
        reader.u16();
        reader.u16();
        const std::uint8_t progress = reader.u8();
        const std::uint8_t train_unit = reader.u8();   // 兵营训练兵种（255 = 默认）
        reader.i16();  // rally_x
        reader.i16();  // rally_y
        CHECK(progress <= 100);
        CHECK(type < static_cast<std::uint8_t>(BuildingType::Count));
        CHECK(train_unit == 255 || train_unit < kUnitTypeCount);
    }

    reader.u8();  // effects
    CHECK_EQ(static_cast<int>(reader.u8()), static_cast<int>(MatchPhase::Playing));
    CHECK(reader.u16() <= sim.match_ticks() / kTickHz);
    CHECK_EQ(static_cast<int>(reader.u8()), sim.alive_count());

    // 昼夜 / 天气 / 总部支援
    CHECK_EQ(static_cast<int>(reader.u8()), static_cast<int>(sim.weather()));
    CHECK(reader.u8() <= 100);  // day_phase 百分比
    CHECK(reader.u8() <= 3);    // 0 锁定 / 1 可用 / 2 领地不足 / 3 冷却
    CHECK(reader.u8() <= 2);    // 随机事件：0 无 / 1 空投 / 2 爆发
    reader.u16();               // 事件剩余秒数

    // 资源点：位置 + 归属 + 占点进度
    const std::uint8_t node_count = reader.u8();
    CHECK_EQ(static_cast<int>(node_count), static_cast<int>(sim.nodes().size()));
    for (int i = 0; i < node_count; ++i) {
        const std::uint8_t kind = reader.u8();
        const std::int16_t x = reader.i16();
        const std::int16_t y = reader.i16();
        const std::uint8_t owner_plus = reader.u8();
        const std::uint8_t progress = reader.u8();
        reader.u8();  // capturer
        reader.u8();  // flags（激活 / 爆发）
        const std::uint16_t radius = reader.u16();   // 片区半径
        const std::uint16_t core_hp = reader.u16();  // 城镇核心
        reader.u16();  // core_max
        CHECK(kind < static_cast<std::uint8_t>(NodeKind::Count));
        CHECK(radius >= 100 && radius <= 400);
        if (kind == static_cast<std::uint8_t>(NodeKind::Town)) {
            CHECK(core_hp <= kTownCoreHp);
        } else {
            CHECK_EQ(static_cast<int>(core_hp), 0);
        }
        CHECK(x >= 0 && x <= kFieldW);
        CHECK(y >= 0 && y <= kFieldH);
        CHECK(owner_plus <= kPlayers);
        CHECK(progress <= 100);
    }

    // 局内成长树：12 个节点（是否点亮 + 价格）
    const std::uint8_t tree_count = reader.u8();
    CHECK_EQ(static_cast<int>(tree_count), kTreeNodeCount);
    for (int i = 0; i < tree_count; ++i) {
        const std::uint8_t taken = reader.u8();
        const std::uint16_t cost = reader.u16();
        CHECK_EQ(taken != 0, sim.tree_taken(0, i));
        CHECK(cost > 0);
    }
    // 建筑上限（工具栏显示 建筑 x/cap）
    const std::uint8_t building_cap = reader.u8();
    CHECK_EQ(static_cast<int>(building_cap), sim.building_cap(0));
    const std::uint8_t tower_cap = reader.u8();
    CHECK_EQ(static_cast<int>(tower_cap), sim.tower_cap(0));
    // 领地掩码走单独消息，快照里没有
    wire::Reader territory_reader(wire::encode_territory(sim, 0));
    const std::uint16_t mask_len = territory_reader.u16();
    CHECK_EQ(static_cast<int>(mask_len), 60 * 40);
    int owned_cells = 0;
    for (int i = 0; i < mask_len; ++i) {
        const std::uint8_t cell = territory_reader.u8();
        CHECK(cell <= 1);
        owned_cells += cell;
    }
    CHECK(owned_cells > 0);   // 开局就有一圈自家领地
    CHECK_EQ(static_cast<int>(reader.remaining()), 0);
}

TEST(MatchOverEncodesWinnerAndAllBases) {
    Sim sim;
    auto decks = make_decks();
    CHECK(sim.start(decks, MatchConfig{}));

    std::array<AiBrain, kPlayers> brains;
    for (int p = 0; p < kPlayers; ++p) brains[static_cast<std::size_t>(p)].reset(p, 50 + p);

    int guard = 0;
    while (sim.phase() == MatchPhase::Playing && guard < kMatchTicks + 10) {
        for (auto& brain : brains) brain.update(sim);
        sim.advance();
        ++guard;
    }

    wire::Reader reader(wire::encode_match_over(sim));
    const std::uint8_t winner = reader.u8();
    const std::uint8_t reason = reader.u8();
    CHECK_EQ(static_cast<int>(winner), sim.winner() < 0 ? 255 : sim.winner());
    CHECK_EQ(static_cast<int>(reason), static_cast<int>(sim.reason()));
    for (int p = 0; p < kPlayers; ++p) {
        CHECK_EQ(static_cast<int>(reader.u16()), sim.base_hp(p));
    }
    CHECK_EQ(static_cast<int>(reader.remaining()), 0);
}
