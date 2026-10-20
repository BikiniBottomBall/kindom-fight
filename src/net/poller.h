// IO 多路复用抽象（S12）。
//
// 上层只认这个接口，不关心底下是 epoll 还是 select：
//   Linux  → epoll（本项目面试的主要考点）
//   Windows → select（本机开发用；服务端最终跑在 Linux）
// 环境变量 KF_POLLER=select 可以强制走 select —— 这样在 Linux 上也能做两种实现的对比，
// 而不是"只在 Windows 上无意中验证过 select"。
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "net/socket.h"

namespace kingdom_fight {

enum PollFlags : std::uint32_t {
    kPollRead = 1u << 0,
    kPollWrite = 1u << 1,
};

struct PollEvent {
    SocketHandle handle = kInvalidSocket;
    std::uint32_t flags = 0;  // 就绪的事件位
    bool error = false;       // 出错 / 对端挂断（仍然会有读事件，读出来是 0 字节）
};

class Poller {
public:
    virtual ~Poller() = default;

    [[nodiscard]] virtual const char* name() const = 0;

    virtual bool add(SocketHandle handle, std::uint32_t interests) = 0;
    virtual bool modify(SocketHandle handle, std::uint32_t interests) = 0;
    virtual bool remove(SocketHandle handle) = 0;

    // 返回就绪数量（>=0）；<0 表示出错。timeout_ms < 0 表示永久阻塞。
    virtual int wait(std::vector<PollEvent>& ready, int timeout_ms) = 0;

    static std::unique_ptr<Poller> create();
};

}  // namespace kingdom_fight
