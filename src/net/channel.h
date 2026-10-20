// 连接通道：一个 socket（RAII 持有）+ 一个读缓冲（帧解码器）+ 一个写队列。
//
// 职责就三件：
//   1. 把内核里的字节读出来、切成帧（粘包/半包交给 FrameDecoder）
//   2. 把要发的帧排进写队列（非阻塞 send 可能只写出去一部分）
//   3. 告诉调用方"还能不能继续用"（对端关闭 / 协议出错 / 写失败 → false）
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/decoder.h"
#include "net/protocol.h"
#include "net/socket.h"

namespace kingdom_fight {

class Channel {
public:
    explicit Channel(Socket socket) : socket_(std::move(socket)) {}

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) noexcept = default;
    Channel& operator=(Channel&&) noexcept = default;

    [[nodiscard]] SocketHandle handle() const { return socket_.handle(); }
    [[nodiscard]] bool closed() const { return closed_; }
    [[nodiscard]] const FrameDecoder& decoder() const { return decoder_; }

    // 读到 EAGAIN 为止，把所有完整帧追加进 frames。
    // 返回 false 表示连接已不可用（对端关闭 / 解码失败 / 读错误），调用方应当关闭它。
    bool read_frames(std::vector<Frame>& frames);

    void queue_frame(Cmd cmd, std::uint32_t seq, const std::string& payload);
    void queue_frame(const Frame& frame) { queue_frame(frame.cmd, frame.seq, frame.payload); }

    [[nodiscard]] bool has_pending_write() const { return write_offset_ < write_buffer_.size(); }

    // 尽量把写队列刷出去。返回 false 表示写失败，连接应关闭。
    bool flush();

    void mark_closed() { closed_ = true; }

private:
    Socket socket_;
    FrameDecoder decoder_;
    std::string write_buffer_;
    std::size_t write_offset_ = 0;
    bool closed_ = false;
};

}  // namespace kingdom_fight
