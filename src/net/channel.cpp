#include "net/channel.h"

#include <array>
#include <cstddef>

namespace kingdom_fight {

bool Channel::read_frames(std::vector<Frame>& frames) {
    if (closed_) {
        return false;
    }

    std::array<char, 8192> buffer{};
    while (true) {
        bool would_block = false;
        const long received = recv_some(handle(), buffer.data(), buffer.size(), would_block);
        if (received == 0) {
            closed_ = true;  // 对端正常关闭
            return false;
        }
        if (received < 0) {
            if (would_block) {
                return true;  // 读干净了，等下一轮事件
            }
            closed_ = true;
            return false;
        }

        decoder_.feed(buffer.data(), static_cast<std::size_t>(received));
        Frame frame{};
        while (true) {
            const DecodeResult result = decoder_.next(frame);
            if (result == DecodeResult::Decoded) {
                frames.push_back(frame);
                continue;
            }
            if (result == DecodeResult::Error) {
                closed_ = true;  // 协议被污染：直接断开，别试图重新同步
                return false;
            }
            break;  // NeedMore
        }
    }
}

void Channel::queue_frame(Cmd cmd, std::uint32_t seq, const std::string& payload) {
    if (closed_) {
        return;
    }
    // 已写出的部分攒到一半就回收，避免写队列随着长连接无限增长
    if (write_offset_ > 0 && write_offset_ >= write_buffer_.size() / 2) {
        write_buffer_.erase(0, write_offset_);
        write_offset_ = 0;
    }
    write_buffer_ += encode_frame(cmd, seq, payload);
}

bool Channel::flush() {
    if (closed_) {
        return false;
    }
    while (write_offset_ < write_buffer_.size()) {
        bool would_block = false;
        const long sent = send_some(handle(), write_buffer_.data() + write_offset_,
                                    write_buffer_.size() - write_offset_, would_block);
        if (sent < 0) {
            if (would_block) {
                return true;  // 内核缓冲满了，等可写事件再来
            }
            closed_ = true;
            return false;
        }
        write_offset_ += static_cast<std::size_t>(sent);
    }
    write_buffer_.clear();
    write_offset_ = 0;
    return true;
}

}  // namespace kingdom_fight
