// 帧解码器（S13）：把 TCP 字节流切成帧。
//
// TCP 是字节流，"一次 recv 拿到多少字节"完全没有保证：
//   · 一次 recv 可能只拿到半个 header
//   · 一次 recv 可能拿到 3 个完整的帧
//   · 一次 recv 可能拿到"1.5 个帧"
// 所以必须缓冲 + 状态机。这是 C++ 后端面试的经典考点，也是本项目"必须自己写"的部分。
//
// 出错策略：**fail-fast**。magic 不对 / 版本不支持 / 长度超限 → 判定为非法帧，
// 直接把连接断开。游戏协议不需要"跳过垃圾字节重新同步"这种宽容度，
// 容忍反而会让攻击面变大。
#pragma once

#include <cstddef>

#include "net/byte_buffer.h"
#include "net/protocol.h"

namespace kingdom_fight {

enum class DecodeResult {
    Decoded,   // out 里有一帧
    NeedMore,  // 数据不够，等下次 recv
    Error,     // 流已经被污染，调用方应该断开连接
};

class FrameDecoder {
public:
    void feed(const void* data, std::size_t length) { buffer_.append(data, length); }

    void feed(const std::string& data) { buffer_.append(data); }

    // 尝试取出一帧。可重复调用直到返回 NeedMore。
    DecodeResult next(Frame& out);

    [[nodiscard]] bool failed() const { return failed_; }

    [[nodiscard]] std::size_t buffered() const { return buffer_.readable(); }

private:
    ByteBuffer buffer_;
    bool failed_ = false;
};

}  // namespace kingdom_fight
