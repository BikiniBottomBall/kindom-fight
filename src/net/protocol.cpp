#include "net/protocol.h"

namespace kingdom_fight {

const char* to_string(Cmd cmd) {
    switch (cmd) {
        case Cmd::Ping:    return "Ping";
        case Cmd::Pong:    return "Pong";
        case Cmd::Echo:    return "Echo";
        case Cmd::EchoAck: return "EchoAck";
    }
    return "Unknown";
}

std::uint16_t read_be16(const char* bytes) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t read_be32(const char* bytes) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes);
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void append_be16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void append_be32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

std::string encode_frame(Cmd cmd, std::uint32_t seq, const std::string& payload) {
    std::string out;
    out.reserve(kFrameHeaderBytes + payload.size());
    append_be16(out, kProtocolMagic);
    out.push_back(static_cast<char>(kProtocolVersion));
    append_be16(out, static_cast<std::uint16_t>(cmd));
    append_be32(out, seq);
    append_be32(out, static_cast<std::uint32_t>(payload.size()));
    out += payload;
    return out;
}

std::string encode_frame(const Frame& frame) {
    return encode_frame(frame.cmd, frame.seq, frame.payload);
}

}  // namespace kingdom_fight
