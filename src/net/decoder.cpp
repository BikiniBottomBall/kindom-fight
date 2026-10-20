#include "net/decoder.h"

namespace kingdom_fight {

DecodeResult FrameDecoder::next(Frame& out) {
    if (failed_) {
        return DecodeResult::Error;
    }
    if (buffer_.readable() < kFrameHeaderBytes) {
        return DecodeResult::NeedMore;  // 连头都没收齐
    }

    const char* p = buffer_.data();
    if (read_be16(p) != kProtocolMagic) {
        failed_ = true;
        return DecodeResult::Error;
    }
    if (static_cast<std::uint8_t>(p[2]) != kProtocolVersion) {
        failed_ = true;
        return DecodeResult::Error;
    }

    const auto cmd = static_cast<Cmd>(read_be16(p + 3));
    const std::uint32_t seq = read_be32(p + 5);
    const std::uint32_t payload_length = read_be32(p + 9);
    if (payload_length > kMaxPayloadBytes) {
        failed_ = true;  // 声明超长 payload：直接判非法，别为它分配内存
        return DecodeResult::Error;
    }

    const std::size_t total = kFrameHeaderBytes + payload_length;
    if (buffer_.readable() < total) {
        return DecodeResult::NeedMore;  // payload 还没收齐
    }

    out.cmd = cmd;
    out.seq = seq;
    out.payload.assign(p + kFrameHeaderBytes, payload_length);
    buffer_.consume(total);
    return DecodeResult::Decoded;
}

}  // namespace kingdom_fight
