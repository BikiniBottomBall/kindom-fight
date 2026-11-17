#include "rts/wire.h"

#include <algorithm>
#include <cstddef>

namespace kingdom_fight::rts::wire {
namespace {

[[nodiscard]] std::uint16_t clamp_u16(int value) {
    if (value <= 0) return 0;
    if (value >= 0xFFFF) return 0xFFFF;
    return static_cast<std::uint16_t>(value);
}

}  // namespace

// ---- Writer --------------------------------------------------------------

void Writer::u16(std::uint16_t value) {
    out_.push_back(static_cast<char>((value >> 8) & 0xFF));
    out_.push_back(static_cast<char>(value & 0xFF));
}

void Writer::u32(std::uint32_t value) {
    out_.push_back(static_cast<char>((value >> 24) & 0xFF));
    out_.push_back(static_cast<char>((value >> 16) & 0xFF));
    out_.push_back(static_cast<char>((value >> 8) & 0xFF));
    out_.push_back(static_cast<char>(value & 0xFF));
}

void Writer::text(const std::string& value) {
    const auto length = static_cast<std::uint16_t>(std::min<std::size_t>(value.size(), 0xFFFF));
    u16(length);
    out_.append(value.data(), length);
}

void Writer::raw(const std::uint8_t* data, std::size_t length) {
    out_.append(reinterpret_cast<const char*>(data), length);
}

// ---- Reader --------------------------------------------------------------

bool Reader::need(std::size_t count) {
    if (!ok_) return false;
    if (pos_ + count > data_.size()) {
        ok_ = false;
        return false;
    }
    return true;
}

std::uint8_t Reader::u8() {
    if (!need(1)) return 0;
    return static_cast<std::uint8_t>(data_[pos_++]);
}

std::uint16_t Reader::u16() {
    if (!need(2)) return 0;
    const auto hi = static_cast<std::uint16_t>(static_cast<unsigned char>(data_[pos_]));
    const auto lo = static_cast<std::uint16_t>(static_cast<unsigned char>(data_[pos_ + 1]));
    pos_ += 2;
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

std::uint32_t Reader::u32() {
    if (!need(4)) return 0;
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) |
                static_cast<std::uint32_t>(static_cast<unsigned char>(data_[pos_ + i]));
    }
    pos_ += 4;
    return value;
}

std::string Reader::text() {
    const std::uint16_t length = u16();
    if (!need(length)) return {};
    std::string value = data_.substr(pos_, length);
    pos_ += length;
    return value;
}

std::vector<std::uint8_t> Reader::raw(std::size_t length) {
    if (!need(length)) return {};
    std::vector<std::uint8_t> out(length);
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = static_cast<std::uint8_t>(data_[pos_ + i]);
    }
    pos_ += length;
    return out;
}

// ---- 下行 ----------------------------------------------------------------

std::string encode_welcome(int player, int ai_players, int human_players,
                           const std::string& build_stamp) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(player));
    writer.u8(static_cast<std::uint8_t>(ai_players));
    writer.u8(static_cast<std::uint8_t>(human_players));
    writer.u8(static_cast<std::uint8_t>(kPlayers));
    writer.u8(static_cast<std::uint8_t>(kDeckSize));
    writer.u8(static_cast<std::uint8_t>(kHandSize));
    writer.u8(static_cast<std::uint8_t>(kCardCount));
    writer.text(build_stamp);  // 编译时间戳：一眼看出跑的是不是最新构建
    return writer.take();
}

std::string encode_card_pool() {
    Writer writer;
    writer.u16(static_cast<std::uint16_t>(kCardCount));
    for (int id = 0; id < kCardCount; ++id) {
        const CardDef& def = card_def(id);
        writer.u16(static_cast<std::uint16_t>(def.id));
        writer.u8(static_cast<std::uint8_t>(def.alliance));  // 这张卡属于哪个阵营的池子
        writer.u8(static_cast<std::uint8_t>(def.kind));
        // 四项资源价格：人力 / 物资 / 能源 / 科技
        writer.u16(static_cast<std::uint16_t>(def.cost_manpower));
        writer.u16(static_cast<std::uint16_t>(def.cost_materiel));
        writer.u16(static_cast<std::uint16_t>(def.cost_energy));
        writer.u16(static_cast<std::uint16_t>(def.cost_tech));
        // 范围技能的作用半径（0 = 不是范围技能）：客户端画格子预览要用
        writer.u16(static_cast<std::uint16_t>(def.radius));
        // 建筑类型（255 = 不是建筑）和法术类型（0 = 不是法术）：客户端画占地/范围
        writer.u8(def.kind == CardKind::Building ? static_cast<std::uint8_t>(def.building) : 255);
        writer.u8(static_cast<std::uint8_t>(def.spell));
        writer.text(def.name);
    }
    return writer.take();
}

std::string encode_map(const Sim& sim) {
    Writer writer;
    writer.u16(static_cast<std::uint16_t>(kCols));
    writer.u16(static_cast<std::uint16_t>(kRows));
    writer.u16(static_cast<std::uint16_t>(kTileSize));
    // 每家的基地满血（先锋军团 +30%，所以不能只发一个数）
    for (int p = 0; p < kPlayers; ++p) {
        writer.u16(clamp_u16(sim.player(p).alliance == Alliance::Vanguard
                                 ? kBaseHp * kVanguardHpPercent / 100
                                 : kBaseHp));
    }
    for (int p = 0; p < kPlayers; ++p) {
        const Point base = sim.base_position(p);
        writer.i16(static_cast<std::int16_t>(base.x));
        writer.i16(static_cast<std::int16_t>(base.y));
    }
    for (int p = 0; p < kPlayers; ++p) {
        writer.u8(static_cast<std::uint8_t>(sim.player(p).alliance));
    }
    const auto& tiles = sim.terrain().raw();
    writer.raw(tiles.data(), tiles.size());
    return writer.take();
}

std::string encode_match_start(const Sim& sim, int player) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(player));
    writer.u16(static_cast<std::uint16_t>(sim.match_ticks() / kTickHz));
    writer.u16(static_cast<std::uint16_t>(sim.hq_unlock_tick() / kTickHz));
    return writer.take();
}

std::string encode_snapshot(const Sim& sim, int viewer) {
    const PlayerState& me = sim.player(viewer);

    Writer writer;
    writer.u32(static_cast<std::uint32_t>(sim.tick()));

    // 四资源 + 人力上限（客户端画资源条用）
    const Resources& mine = me.res;
    writer.u16(clamp_u16(mine.manpower));
    writer.u16(clamp_u16(me.manpower_cap()));
    writer.u16(clamp_u16(mine.materiel));
    writer.u16(clamp_u16(mine.energy));
    writer.u16(clamp_u16(mine.tech));
    writer.u16(clamp_u16(mine.territory));

    writer.u8(static_cast<std::uint8_t>(kHandSize));
    for (int slot = 0; slot < kHandSize; ++slot) {
        writer.u16(static_cast<std::uint16_t>(std::max(0, me.hand(slot))));
    }

    for (int p = 0; p < kPlayers; ++p) {
        writer.u16(clamp_u16(sim.base_hp(p)));
    }

    int tower_total = 0;
    for (int p = 0; p < kPlayers; ++p) {
        tower_total += static_cast<int>(sim.player(p).towers.size());
    }
    writer.u8(static_cast<std::uint8_t>(std::min(tower_total, 255)));
    for (int p = 0; p < kPlayers; ++p) {
        for (const Tower& tower : sim.player(p).towers) {
            writer.u8(static_cast<std::uint8_t>(p));
            writer.u16(static_cast<std::uint16_t>(std::clamp(tower.id, 0, 65535)));
            writer.u8(static_cast<std::uint8_t>(tower.complete() ? 100
                : std::clamp(tower.build_total > 0 ? tower.build_ticks * 100 / tower.build_total : 100, 0, 100)));
            writer.i16(static_cast<std::int16_t>(tower.x));
            writer.i16(static_cast<std::int16_t>(tower.y));
            writer.u16(clamp_u16(tower.hp));
            writer.u16(clamp_u16(tower.max_hp));
        }
    }

    std::size_t unit_count = 0;
    for (int p = 0; p < kPlayers; ++p) unit_count += sim.player(p).units.size();
    writer.u16(static_cast<std::uint16_t>(std::min<std::size_t>(unit_count, 0xFFFF)));
    for (int p = 0; p < kPlayers; ++p) {
        for (const Unit& unit : sim.player(p).units) {
            writer.u16(static_cast<std::uint16_t>(unit.id & 0xFFFF));
            writer.u8(static_cast<std::uint8_t>(unit.owner));
            writer.u8(static_cast<std::uint8_t>(unit.type));
            writer.i16(static_cast<std::int16_t>(unit.x));
            writer.i16(static_cast<std::int16_t>(unit.y));
            writer.u16(clamp_u16(unit.hp));
            writer.u16(clamp_u16(unit.max_hp));
            std::uint8_t flags = 0;
            if (unit.slow_ticks > 0) flags |= 1u;
            if (unit.buff_ticks > 0) flags |= 2u;
            writer.u8(flags);
        }
    }

    std::size_t building_total = 0;
    for (int p = 0; p < kPlayers; ++p) building_total += sim.player(p).buildings.size();
    writer.u8(static_cast<std::uint8_t>(std::min<std::size_t>(building_total, 255)));
    for (int p = 0; p < kPlayers; ++p) {
        for (const Building& building : sim.player(p).buildings) {
            writer.u8(static_cast<std::uint8_t>(p));
            writer.u8(static_cast<std::uint8_t>(building.type));
            writer.i16(static_cast<std::int16_t>(building.x));
            writer.i16(static_cast<std::int16_t>(building.y));
            writer.u16(clamp_u16(building.hp));
            writer.u16(clamp_u16(building.max_hp));
            // 建造进度 0~100（100 = 完工），客户端据此画工地进度条
            const int progress = building.ready || building.build_total <= 0
                                     ? 100
                                     : std::clamp(building.build_ticks * 100 / building.build_total, 0, 100);
            writer.u8(static_cast<std::uint8_t>(progress));
            // 兵营：这个兵营训练的兵种（255 = 用阵营默认）+ 指定攻击区域
            writer.u8(static_cast<std::uint8_t>(
                building.train_unit < 0 ? 255 : std::clamp(building.train_unit, 0, 254)));
            writer.i16(static_cast<std::int16_t>(std::clamp(building.rally_x, -1, 65535)));
            writer.i16(static_cast<std::int16_t>(std::clamp(building.rally_y, -1, 65535)));
        }
    }

    writer.u8(static_cast<std::uint8_t>(std::min<std::size_t>(sim.effects().size(), 255)));
    for (const Effect& effect : sim.effects()) {
        writer.u8(effect.kind);
        writer.u8(effect.owner);
        writer.i16(static_cast<std::int16_t>(effect.x));
        writer.i16(static_cast<std::int16_t>(effect.y));
        writer.u16(static_cast<std::uint16_t>(effect.radius));
        writer.u8(static_cast<std::uint8_t>(std::clamp(effect.ttl, 0, 255)));
    }

    writer.u8(static_cast<std::uint8_t>(sim.phase()));
    writer.u16(static_cast<std::uint16_t>(sim.remaining_ticks() / kTickHz));
    writer.u8(static_cast<std::uint8_t>(sim.alive_count()));

    // 昼夜 / 天气 / 总部支援状态
    writer.u8(static_cast<std::uint8_t>(sim.weather()));
    writer.u8(static_cast<std::uint8_t>(sim.day_phase_percent()));
    writer.u8(static_cast<std::uint8_t>(sim.hq_state(viewer)));
    // 随机事件：0 = 无；1 = 补给空投；2 = 矿脉爆发
    writer.u8(static_cast<std::uint8_t>(sim.current_event()));
    writer.u16(static_cast<std::uint16_t>(std::min(sim.event_ticks_left() / kTickHz, 65535)));

    // 地图资源点：位置不变，但归属/进度每帧都可能变，所以放在快照里一起发
    const auto& nodes = sim.nodes();
    writer.u8(static_cast<std::uint8_t>(std::min<std::size_t>(nodes.size(), 255)));
    for (const ResourceNode& node : nodes) {
        writer.u8(static_cast<std::uint8_t>(node.kind));
        writer.i16(static_cast<std::int16_t>(node.x));
        writer.i16(static_cast<std::int16_t>(node.y));
        writer.u8(static_cast<std::uint8_t>(node.owner + 1));  // 0 = 中立
        writer.u8(static_cast<std::uint8_t>(std::clamp(node.progress, 0, 100)));
        writer.u8(static_cast<std::uint8_t>(node.capturer + 1));
        // bit0 = 已激活（高资源片区 5 分钟后才亮），bit1 = 矿脉爆发中
        const std::uint8_t flags = static_cast<std::uint8_t>((node.active ? 1 : 0) |
                                                             (node.surge_ticks > 0 ? 2 : 0));
        writer.u8(flags);
        // 片区半径 + 城镇核心血量（客户端画范围圈和守军血条）
        writer.u16(static_cast<std::uint16_t>(node.radius));
        writer.u16(static_cast<std::uint16_t>(std::clamp(node.core_hp, 0, 65535)));
        writer.u16(static_cast<std::uint16_t>(std::clamp(node.core_max, 0, 65535)));
    }

    // 局内成长树：只发"看的人自己"的三棵树节点状态（12 个）
    // 每个节点：u8 是否已点亮 + u16 价格
    writer.u8(static_cast<std::uint8_t>(kTreeNodeCount));
    for (int i = 0; i < kTreeNodeCount; ++i) {
        writer.u8(sim.tree_taken(viewer, i) ? 1 : 0);
        writer.u16(static_cast<std::uint16_t>(std::min(sim.upgrade_cost_for(viewer, i), 65535)));
    }
    // 建筑上限（工具栏要显示"建筑 3/6"）
    writer.u8(static_cast<std::uint8_t>(std::clamp(sim.building_cap(viewer), 0, 255)));
    // 塔的数量上限（工具栏/提示要用）
    writer.u8(static_cast<std::uint8_t>(std::clamp(sim.tower_cap(viewer), 0, 255)));
    return writer.take();
}

std::string encode_unit_table() {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(kUnitTypeCount));
    for (int i = 0; i < kUnitTypeCount; ++i) {
        const UnitStat& stat = unit_stat(static_cast<UnitType>(i));
        writer.u8(static_cast<std::uint8_t>(i));
        writer.u16(static_cast<std::uint16_t>(std::clamp(stat.train_ticks, 0, 65535)));
        writer.u16(static_cast<std::uint16_t>(std::clamp(stat.cost_manpower, 0, 65535)));
        writer.u16(static_cast<std::uint16_t>(std::clamp(stat.cost_materiel, 0, 65535)));
        writer.text(to_string(static_cast<UnitType>(i)));
    }
    return writer.take();
}

std::string encode_territory(const Sim& sim, int viewer) {
    Writer writer;
    // 60×40 的领地掩码：每格 1 = 自家领地（4×4 地板格一块）。
    // 单独一条消息发：快照只有 ~1KB，把 2.4KB 塞进去会把整包撑爆。
    const auto& mask = sim.territory_mask(viewer);
    writer.u16(static_cast<std::uint16_t>(mask.size()));
    for (const auto cell : mask) writer.u8(cell);
    return writer.take();
}

std::string encode_match_over(const Sim& sim) {
    Writer writer;
    const int winner = sim.winner();
    writer.u8(static_cast<std::uint8_t>(winner < 0 ? 255 : winner));
    writer.u8(static_cast<std::uint8_t>(sim.reason()));
    for (int p = 0; p < kPlayers; ++p) {
        writer.u16(clamp_u16(sim.base_hp(p)));
    }
    return writer.take();
}

std::string encode_play_ack(int slot, PlayResult result) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(std::clamp(slot, 0, 255)));
    writer.u8(static_cast<std::uint8_t>(result));
    return writer.take();
}

std::string encode_upgrade_ack(int upgrade_id, int level, PlayResult result) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(std::clamp(upgrade_id, 0, 255)));
    writer.u8(static_cast<std::uint8_t>(std::clamp(level, 0, 255)));
    writer.u8(static_cast<std::uint8_t>(result));
    return writer.take();
}

std::string encode_demolish_ack(int kind, int index, PlayResult result) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(std::clamp(kind, 0, 255)));
    writer.u16(static_cast<std::uint16_t>(std::clamp(index, 0, 65535)));
    writer.u8(static_cast<std::uint8_t>(result));
    return writer.take();
}

bool decode_demolish(const std::string& payload, int& kind, int& index) {
    if (payload.size() < 3) return false;
    kind = static_cast<std::uint8_t>(payload[0]);
    index = static_cast<int>(static_cast<std::uint16_t>(
        (static_cast<std::uint8_t>(payload[1]) << 8) | static_cast<std::uint8_t>(payload[2])));
    return true;
}

std::string encode_hq_ack(PlayResult result) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(result));
    return writer.take();
}

bool decode_hq(const std::string& payload, int& x, int& y) {
    Reader reader(payload);
    const auto raw_x = static_cast<std::int16_t>(reader.u16());
    const auto raw_y = static_cast<std::int16_t>(reader.u16());
    if (!reader.ok()) return false;
    x = raw_x;
    y = raw_y;
    return true;
}

std::string encode_error(const std::string& message) {
    Writer writer;
    writer.text(message);
    return writer.take();
}

// ---- 上行 ----------------------------------------------------------------

bool decode_deck(const std::string& payload, std::vector<int>& cards, Alliance& alliance,
                 int& preferred_unit) {
    Reader reader(payload);
    const std::uint8_t count = reader.u8();
    if (!reader.ok() || count != kDeckSize) return false;

    std::vector<int> parsed;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t id = reader.u16();
        if (!reader.ok()) return false;
        if (id >= static_cast<std::uint16_t>(kCardCount)) return false;
        parsed.push_back(static_cast<int>(id));
    }
    const std::uint8_t raw_alliance = reader.u8();
    if (!reader.ok()) return false;
    if (raw_alliance >= static_cast<std::uint8_t>(Alliance::Count)) return false;

    // 可选的"兵营默认兵种"字节（老客户端不发，默认 -1 = 用阵营默认）
    preferred_unit = -1;
    if (reader.remaining() > 0) {
        const std::uint8_t raw_unit = reader.u8();
        if (reader.ok() && raw_unit < static_cast<std::uint8_t>(kUnitTypeCount)) {
            preferred_unit = static_cast<int>(raw_unit);
        }
    }

    cards = std::move(parsed);
    alliance = static_cast<Alliance>(raw_alliance);
    return true;
}

bool decode_play(const std::string& payload, int& slot, int& x, int& y) {
    Reader reader(payload);
    const std::uint8_t raw_slot = reader.u8();
    const std::int16_t raw_x = reader.i16();
    const std::int16_t raw_y = reader.i16();
    if (!reader.ok()) return false;
    slot = raw_slot;
    x = raw_x;
    y = raw_y;
    return true;
}

}  // namespace kingdom_fight::rts::wire
