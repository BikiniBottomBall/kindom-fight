// 应用层协议（S11）。
//
// 关键设计：**传输与协议解耦**（ADR 0002 的要求）。
//   「帧怎么在线上跑」= 长度前缀（TCP）/ WebSocket 帧（浏览器）—— 可替换
//   「帧里装什么」  = 下面这个 header + payload —— 与传输无关
// 以后要加 WebSocket 支持，只需要再写一个编解码外壳，业务代码零改动。
//
// 线上帧格式（全部大端序 —— 网络字节序，跨平台不用操心）：
//
//   +--------+---------+-------+-------+------------+-----------+
//   | magic  | version |  cmd  |  seq  | payload_len|  payload  |
//   | 2 字节 |  1 字节 | 2字节 | 4字节 |   4 字节   |  N 字节   |
//   +--------+---------+-------+-------+------------+-----------+
//   magic = 0x4B46 ("KF")，头总长 13 字节
//   magic = 0x4B46 ("KF")
//
// 为什么带 seq：客户端的每条指令都要能被服务端去重。
// 断线重连后客户端会重发"未确认的指令"，没有 seq 就会重复执行（S18 的地基）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace kingdom_fight {

inline constexpr std::uint16_t kProtocolMagic = 0x4B46;  // "KF"
inline constexpr std::uint8_t kProtocolVersion = 1;

inline constexpr std::size_t kFrameHeaderBytes = 13;
// 单帧 payload 上限。超过就判定为非法帧并断开 —— 防"恶意声明 2GB"这种攻击。
inline constexpr std::uint32_t kMaxPayloadBytes = 64 * 1024;

enum class Cmd : std::uint16_t {
    Ping = 1,
    Pong = 2,
    Echo = 90,     // 联调用的回显
    EchoAck = 91,
};

[[nodiscard]] const char* to_string(Cmd cmd);

struct Frame {
    Cmd cmd{};
    std::uint32_t seq = 0;
    std::string payload;
};

// ---- 大端序读写 ----------------------------------------------------------

[[nodiscard]] std::uint16_t read_be16(const char* bytes);
[[nodiscard]] std::uint32_t read_be32(const char* bytes);
void append_be16(std::string& out, std::uint16_t value);
void append_be32(std::string& out, std::uint32_t value);

// ---- 编码 ----------------------------------------------------------------

[[nodiscard]] std::string encode_frame(Cmd cmd, std::uint32_t seq, const std::string& payload);
[[nodiscard]] std::string encode_frame(const Frame& frame);

}  // namespace kingdom_fight
