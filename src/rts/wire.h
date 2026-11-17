// RTS 模式的线协议：消息体编解码（大端序，跑在 net/protocol 的帧壳里）。
//
// 分层和原来一致：
//   net/protocol  只负责「帧怎么在线上跑」（magic/version/cmd/seq/len）
//   rts/wire      负责「帧里装什么」（这些消息）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rts/sim.h"
#include "rts/terrain.h"

namespace kingdom_fight::rts::wire {

enum class Msg : std::uint16_t {
    C2S_Hello = 100,
    S2C_Welcome = 101,
    S2C_CardPool = 102,
    C2S_Deck = 103,
    S2C_MatchStart = 104,
    C2S_Play = 105,
    S2C_Snapshot = 106,
    S2C_MatchOver = 107,
    S2C_Error = 108,
    C2S_Rematch = 109,
    S2C_PlayAck = 110,
    S2C_Map = 111,
    C2S_Hq = 112,        // 呼叫总部支援（对地打击）
    S2C_HqAck = 113,     // 回执：u8 result
    C2S_Demolish = 114,  // 拆除建筑/塔：u8 kind(0=建筑 1=塔), u8 index
    S2C_DemolishAck = 115,  // 回执：u8 kind, u8 index, u8 result
    C2S_Upgrade = 116,      // 成长树升级：u8 upgrade_id
    S2C_UpgradeAck = 117,   // 回执：u8 upgrade_id, u8 level, u8 result
    C2S_Repair = 118,       // 修复建筑/塔：u8 kind, u8 index
    S2C_RepairAck = 119,    // 回执：u8 kind, u8 index, u8 result
    C2S_Discard = 120,      // 弃牌：u8 slot
    S2C_DiscardAck = 121,
    S2C_Territory = 122,
    C2S_BuildTower = 123,     // 造塔：u16 x, u16 y（不走卡牌）
    S2C_BuildTowerAck = 124,  // 回执：u8 result
    C2S_SetUnit = 125,        // 兵营换兵种：u8 building index, u8 unit type
    S2C_SetUnitAck = 126,     // 回执：u8 result
    C2S_SetRally = 127,       // 指定攻击区域：u8 building index, i16 x, i16 y
    S2C_SetRallyAck = 128,    // 回执：u8 result
    S2C_UnitTable = 129,      // 兵种表：名称/训练时间/成本   // 回执：u8 slot, u8 result
};

// ---- 基础读写 ------------------------------------------------------------

class Writer {
public:
    void u8(std::uint8_t value) { out_.push_back(static_cast<char>(value)); }
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void text(const std::string& value);  // u16 长度 + UTF-8 字节
    void raw(const std::uint8_t* data, std::size_t length);

    [[nodiscard]] const std::string& bytes() const { return out_; }
    [[nodiscard]] std::string take() { return std::move(out_); }

private:
    std::string out_;
};

class Reader {
public:
    // 按值持有：允许 Reader reader(encode_xxx(...)) 这种直接吃临时对象的写法。
    // （之前是 const 引用成员，配临时对象会悬垂 —— 测试当场炸出一堆乱码字段。）
    explicit Reader(std::string payload) : data_(std::move(payload)) {}

    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint16_t u16();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    [[nodiscard]] std::string text();
    [[nodiscard]] std::vector<std::uint8_t> raw(std::size_t length);

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] bool eof() const { return pos_ >= data_.size(); }
    [[nodiscard]] std::size_t remaining() const { return data_.size() - pos_; }

private:
    [[nodiscard]] bool need(std::size_t count);

    std::string data_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

// ---- 下行（服务端 → 客户端） ---------------------------------------------

[[nodiscard]] std::string encode_welcome(int player, int ai_players, int human_players,
                                        const std::string& build_stamp);
[[nodiscard]] std::string encode_card_pool();
// 地形 + 四家基地坐标：开局前发一次，客户端不自己生成地图
[[nodiscard]] std::string encode_map(const Sim& sim);
[[nodiscard]] std::string encode_match_start(const Sim& sim, int player);
[[nodiscard]] std::string encode_snapshot(const Sim& sim, int viewer);
// 领地掩码单独一条消息（快照里不放，避免把包撑大）
[[nodiscard]] std::string encode_territory(const Sim& sim, int viewer);
// 兵种表：名称 / 训练时间 / 成本（客户端建筑面板用）
[[nodiscard]] std::string encode_unit_table();
[[nodiscard]] std::string encode_match_over(const Sim& sim);
[[nodiscard]] std::string encode_play_ack(int slot, PlayResult result);
[[nodiscard]] std::string encode_hq_ack(PlayResult result);
[[nodiscard]] std::string encode_demolish_ack(int kind, int index, PlayResult result);
[[nodiscard]] std::string encode_upgrade_ack(int upgrade_id, int level, PlayResult result);
// 解码：u8 kind, u8 index
[[nodiscard]] bool decode_demolish(const std::string& payload, int& kind, int& index);
[[nodiscard]] std::string encode_error(const std::string& message);

// ---- 上行（客户端 → 服务端） ---------------------------------------------

[[nodiscard]] bool decode_deck(const std::string& payload, std::vector<int>& cards,
                               Alliance& alliance, int& preferred_unit);
[[nodiscard]] bool decode_play(const std::string& payload, int& slot, int& x, int& y);
[[nodiscard]] bool decode_hq(const std::string& payload, int& x, int& y);

}  // namespace kingdom_fight::rts::wire
