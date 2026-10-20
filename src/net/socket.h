// 跨平台 socket 封装。
//
// 为什么要有这一层：epoll 是 Linux 专有的，Windows 只有 select / IOCP。
// 把平台差异关在这一个文件里，上层（poller / reactor）就能用同一套代码，
// 而"epoll 那部分"仍然是**自己写的**——这正是 C++ 后端面试要问的东西。
//
// 注意：int 装不下 Windows 的 SOCKET（UINT_PTR），所以句柄统一用 intptr_t。
// INVALID_SOCKET 展开成 ~0，转成 intptr_t 正好是 -1，两个平台可以共用同一个哨兵值。
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// 必须在 winsock2.h 之前定义，默认的 64 个 fd 上限对服务端远远不够
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string>

namespace kingdom_fight {

using SocketHandle = std::intptr_t;
inline constexpr SocketHandle kInvalidSocket = -1;

// 进程级初始化（Windows 需要 WSAStartup）。重复调用是安全的。
[[nodiscard]] bool net_init();
void net_cleanup();
[[nodiscard]] std::string net_last_error();

// RAII 句柄：析构自动 close，禁止拷贝、允许移动
class Socket {
public:
    Socket() = default;
    explicit Socket(SocketHandle handle) : handle_(handle) {}
    ~Socket() { close(); }

    Socket(Socket&& other) noexcept : handle_(other.handle_) { other.handle_ = kInvalidSocket; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = kInvalidSocket;
        }
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    [[nodiscard]] bool valid() const { return handle_ != kInvalidSocket; }
    [[nodiscard]] SocketHandle handle() const { return handle_; }

    void close();
    // 交出句柄所有权（之后不再 close）
    [[nodiscard]] SocketHandle release();

    [[nodiscard]] bool set_nonblocking(bool enable);
    [[nodiscard]] bool set_no_delay(bool enable);
    [[nodiscard]] bool set_reuse_address(bool enable);

    // 服务端：监听 address:port（address 传 nullptr 表示任意地址）
    [[nodiscard]] static Socket listen_tcp(const char* address, std::uint16_t port, int backlog = 128);

    // 接受一个连接。非阻塞下没有待处理连接时返回无效 Socket。
    [[nodiscard]] Socket accept_connection();

private:
    SocketHandle handle_ = kInvalidSocket;
};

// 返回值 >0 = 字节数；0 = 对端关闭；-1 = 出错。
// would_block = true 表示"只是暂时没数据"，不代表连接坏了。
long recv_some(SocketHandle handle, void* buffer, std::size_t length, bool& would_block);
long send_some(SocketHandle handle, const void* buffer, std::size_t length, bool& would_block);

}  // namespace kingdom_fight
