// 字节缓冲：net 层的最小积木。
//
// 一个读缓冲里可能同时有"半帧"和"两帧"，也可能一个字都没有 —— 这就是 TCP
// 的常态（面向字节流，没有消息边界）。切帧的工作交给 FrameDecoder。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace kingdom_fight {

class ByteBuffer {
public:
    void append(const void* data, std::size_t length) {
        const auto* bytes = static_cast<const char*>(data);
        storage_.insert(storage_.end(), bytes, bytes + length);
    }

    void append(const std::string& data) { append(data.data(), data.size()); }

    // 第一个未读字节
    [[nodiscard]] const char* data() const { return storage_.data() + read_pos_; }

    [[nodiscard]] std::size_t readable() const { return storage_.size() - read_pos_; }

    [[nodiscard]] bool empty() const { return readable() == 0; }

    void consume(std::size_t length) {
        read_pos_ += length;
        if (read_pos_ >= storage_.size()) {
            storage_.clear();
            read_pos_ = 0;
            return;
        }
        // 已读部分攒多了就回收，避免长连接跑久了 vector 越涨越大
        if (read_pos_ >= 8192 && read_pos_ >= storage_.size() / 2) {
            compact();
        }
    }

    // 回收已读空间，避免长时间运行时 vector 无限增长
    void compact() {
        if (read_pos_ == 0) {
            return;
        }
        storage_.erase(storage_.begin(), storage_.begin() + static_cast<std::ptrdiff_t>(read_pos_));
        read_pos_ = 0;
    }

private:
    std::vector<char> storage_;
    std::size_t read_pos_ = 0;
};

}  // namespace kingdom_fight
