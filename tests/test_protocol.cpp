#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "net/byte_buffer.h"
#include "net/decoder.h"
#include "net/protocol.h"
#include "test_framework.h"

using namespace kingdom_fight;

namespace {

int byte_at(const std::string& bytes, std::size_t index) {
    return static_cast<int>(static_cast<unsigned char>(bytes[index]));
}

// 一次性喂完，然后把所有完整帧都取出来
int decode_all(FrameDecoder& decoder, std::vector<Frame>& frames) {
    frames.clear();
    Frame frame{};
    while (true) {
        const DecodeResult result = decoder.next(frame);
        if (result == DecodeResult::Decoded) {
            frames.push_back(frame);
            continue;
        }
        if (result == DecodeResult::Error) {
            return -1;
        }
        return static_cast<int>(frames.size());
    }
}

}  // namespace

TEST(BigEndianHelpersRoundTrip) {
    std::string out;
    append_be16(out, 0x1234);
    append_be32(out, 0xDEADBEEF);
    CHECK_EQ(static_cast<int>(out.size()), 6);
    CHECK_EQ(static_cast<int>(byte_at(out, 0)), 0x12);
    CHECK_EQ(static_cast<int>(byte_at(out, 1)), 0x34);
    CHECK_EQ(static_cast<int>(byte_at(out, 2)), 0xDE);
    CHECK_EQ(static_cast<int>(byte_at(out, 3)), 0xAD);
    CHECK_EQ(static_cast<int>(byte_at(out, 4)), 0xBE);
    CHECK_EQ(static_cast<int>(byte_at(out, 5)), 0xEF);

    CHECK_EQ(static_cast<int>(read_be16(out.data())), 0x1234);
    CHECK_EQ(read_be32(out.data() + 2), 0xDEADBEEFu);
}

TEST(EncodedFrameHasDocumentedLayout) {
    const std::string bytes = encode_frame(Cmd::Ping, 0x01020304, "AB");
    CHECK_EQ(static_cast<int>(bytes.size()), static_cast<int>(kFrameHeaderBytes) + 2);

    CHECK_EQ(static_cast<int>(byte_at(bytes, 0)), 0x4B);  // magic "K"
    CHECK_EQ(static_cast<int>(byte_at(bytes, 1)), 0x46);  // magic "F"
    CHECK_EQ(static_cast<int>(byte_at(bytes, 2)), kProtocolVersion);
    CHECK_EQ(static_cast<int>(byte_at(bytes, 3)), 0x00);  // cmd 高字节
    CHECK_EQ(static_cast<int>(byte_at(bytes, 4)), 0x01);  // cmd = Ping
    CHECK_EQ(static_cast<int>(byte_at(bytes, 5)), 0x01);  // seq 高字节
    CHECK_EQ(static_cast<int>(byte_at(bytes, 8)), 0x04);  // seq 低字节
    CHECK_EQ(static_cast<int>(byte_at(bytes, 12)), 0x02);  // payload_len = 2
    CHECK_EQ(bytes.substr(kFrameHeaderBytes), std::string("AB"));
}

TEST(DecodeSingleFrameRoundTrip) {
    FrameDecoder decoder;
    decoder.feed(encode_frame(Cmd::Echo, 42, "hello"));

    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::Decoded);
    CHECK(frame.cmd == Cmd::Echo);
    CHECK_EQ(static_cast<int>(frame.seq), 42);
    CHECK_EQ(frame.payload, std::string("hello"));
    CHECK(decoder.next(frame) == DecodeResult::NeedMore);
    CHECK_EQ(static_cast<int>(decoder.buffered()), 0);
}

TEST(DecodeEmptyPayloadIsValid) {
    FrameDecoder decoder;
    decoder.feed(encode_frame(Cmd::Ping, 1, ""));
    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::Decoded);
    CHECK(frame.cmd == Cmd::Ping);
    CHECK(frame.payload.empty());
}

TEST(DecodeThreeFramesFromOneFeed) {
    FrameDecoder decoder;
    std::string stream;
    stream += encode_frame(Cmd::Ping, 1, "a");
    stream += encode_frame(Cmd::Echo, 2, "bb");
    stream += encode_frame(Cmd::Pong, 3, "ccc");
    decoder.feed(stream);

    std::vector<Frame> frames;
    CHECK_EQ(decode_all(decoder, frames), 3);
    CHECK_EQ(frames[0].payload, std::string("a"));
    CHECK_EQ(frames[1].payload, std::string("bb"));
    CHECK_EQ(frames[2].payload, std::string("ccc"));
    CHECK_EQ(static_cast<int>(frames[2].seq), 3);
}

TEST(DecodeFrameArrivingOneByteAtATime) {
    const std::string stream = encode_frame(Cmd::Echo, 7, "split");
    FrameDecoder decoder;
    Frame frame{};

    int decoded = 0;
    for (const char byte : stream) {
        decoder.feed(&byte, 1);
        while (decoder.next(frame) == DecodeResult::Decoded) {
            ++decoded;
        }
    }
    CHECK_EQ(decoded, 1);
    CHECK_EQ(frame.payload, std::string("split"));
}

TEST(DecodeFrameSplitInTheMiddleOfPayload) {
    const std::string stream = encode_frame(Cmd::Echo, 9, "0123456789");
    FrameDecoder decoder;

    // 先喂"头 + 一半 payload"
    const std::size_t half = kFrameHeaderBytes + 5;
    decoder.feed(stream.data(), half);
    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::NeedMore);
    CHECK_EQ(static_cast<int>(decoder.buffered()), static_cast<int>(half));

    // 再喂剩下的
    decoder.feed(stream.data() + half, stream.size() - half);
    CHECK(decoder.next(frame) == DecodeResult::Decoded);
    CHECK_EQ(frame.payload, std::string("0123456789"));
}

TEST(DecodeRejectsBadMagic) {
    FrameDecoder decoder;
    std::string stream = encode_frame(Cmd::Ping, 1, "x");
    stream[0] = 'X';  // 破坏 magic
    decoder.feed(stream);

    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::Error);
    CHECK(decoder.failed());
}

TEST(DecodeRejectsUnsupportedVersion) {
    FrameDecoder decoder;
    std::string stream = encode_frame(Cmd::Ping, 1, "x");
    stream[2] = static_cast<char>(kProtocolVersion + 1);
    decoder.feed(stream);

    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::Error);
}

TEST(DecodeRejectsOversizedPayloadWithoutAllocating) {
    // 手工造一个"声明 2GB payload"的头，解码器必须立刻拒绝，而不是等数据或分配内存
    std::string header;
    append_be16(header, kProtocolMagic);
    header.push_back(static_cast<char>(kProtocolVersion));
    append_be16(header, static_cast<std::uint16_t>(Cmd::Echo));
    append_be32(header, 1);
    append_be32(header, 2u * 1024u * 1024u * 1024u);

    FrameDecoder decoder;
    decoder.feed(header);
    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::Error);
}

TEST(FailedDecoderKeepsFailing) {
    FrameDecoder decoder;
    std::string stream = encode_frame(Cmd::Ping, 1, "x");
    stream[0] = 'X';
    decoder.feed(stream);

    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::Error);
    // 再喂合法数据也不该"复活"——调用方应该已经断开连接了
    decoder.feed(encode_frame(Cmd::Echo, 2, "ok"));
    CHECK(decoder.next(frame) == DecodeResult::Error);
}

TEST(DecodeMaxPayloadBoundaryIsAccepted) {
    const std::string payload(kMaxPayloadBytes, 'z');
    FrameDecoder decoder;
    decoder.feed(encode_frame(Cmd::Echo, 1, payload));

    Frame frame{};
    CHECK(decoder.next(frame) == DecodeResult::Decoded);
    CHECK_EQ(static_cast<int>(frame.payload.size()), static_cast<int>(kMaxPayloadBytes));
}
