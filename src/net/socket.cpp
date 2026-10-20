#include "net/socket.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kingdom_fight {
namespace {

#ifdef _WIN32
bool is_would_block(int error_code) {
    return error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS;
}
::SOCKET raw(SocketHandle handle) { return static_cast<::SOCKET>(handle); }
#else
bool is_would_block(int error_code) { return error_code == EWOULDBLOCK || error_code == EAGAIN; }
int raw(SocketHandle handle) { return static_cast<int>(handle); }
#endif

int last_socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

}  // namespace

bool net_init() {
#ifdef _WIN32
    static bool started = false;
    if (started) {
        return true;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    started = true;
#endif
    return true;
}

void net_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

std::string net_last_error() {
#ifdef _WIN32
    const int code = WSAGetLastError();
    char* text = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<char*>(&text), 0, nullptr);
    std::string message = "wsa error " + std::to_string(code);
    if (length != 0 && text != nullptr) {
        message += ": ";
        message += text;
        LocalFree(text);
    }
    return message;
#else
    return std::string("errno ") + std::to_string(errno) + ": " + std::strerror(errno);
#endif
}

void Socket::close() {
    if (handle_ == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::closesocket(raw(handle_));
#else
    ::close(raw(handle_));
#endif
    handle_ = kInvalidSocket;
}

SocketHandle Socket::release() {
    const SocketHandle handle = handle_;
    handle_ = kInvalidSocket;
    return handle;
}

bool Socket::set_nonblocking(bool enable) {
#ifdef _WIN32
    u_long mode = enable ? 1u : 0u;
    return ioctlsocket(raw(handle_), FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(raw(handle_), F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int updated = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(raw(handle_), F_SETFL, updated) == 0;
#endif
}

bool Socket::set_no_delay(bool enable) {
    const int value = enable ? 1 : 0;
    return setsockopt(raw(handle_), IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
}

bool Socket::set_reuse_address(bool enable) {
    const int value = enable ? 1 : 0;
    return setsockopt(raw(handle_), SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
}

Socket Socket::listen_tcp(const char* address, std::uint16_t port, int backlog) {
    if (!net_init()) {
        return Socket{};
    }

    const auto handle = static_cast<SocketHandle>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (handle == kInvalidSocket) {
        return Socket{};
    }
    Socket socket(handle);
    (void)socket.set_reuse_address(true);  // 失败不致命，只影响重启时能否立刻复用端口

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (address == nullptr) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) {
        return Socket{};
    }

    if (::bind(raw(handle), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return Socket{};
    }
    if (::listen(raw(handle), backlog) != 0) {
        return Socket{};
    }
    if (!socket.set_nonblocking(true)) {
        return Socket{};
    }
    return socket;
}

Socket Socket::accept_connection() {
    sockaddr_in peer{};
#ifdef _WIN32
    int peer_length = static_cast<int>(sizeof(peer));
#else
    socklen_t peer_length = sizeof(peer);
#endif
    const auto accepted =
        static_cast<SocketHandle>(::accept(raw(handle_), reinterpret_cast<sockaddr*>(&peer), &peer_length));
    if (accepted == kInvalidSocket) {
        return Socket{};
    }
    Socket connection(accepted);
    // 非阻塞是硬要求（否则一个慢连接会卡死整个 reactor）；失败就丢弃这个连接
    if (!connection.set_nonblocking(true)) {
        return Socket{};
    }
    (void)connection.set_no_delay(true);  // 游戏消息普遍很小，禁用 Nagle 降低延迟
    return connection;
}

long recv_some(SocketHandle handle, void* buffer, std::size_t length, bool& would_block) {
    would_block = false;
#ifdef _WIN32
    const int result = ::recv(raw(handle), static_cast<char*>(buffer), static_cast<int>(length), 0);
#else
    const ssize_t result = ::recv(raw(handle), buffer, length, 0);
#endif
    if (result < 0) {
        would_block = is_would_block(last_socket_error());
    }
    return static_cast<long>(result);
}

long send_some(SocketHandle handle, const void* buffer, std::size_t length, bool& would_block) {
    would_block = false;
#ifdef _WIN32
    const int result = ::send(raw(handle), static_cast<const char*>(buffer), static_cast<int>(length), 0);
#else
    const ssize_t result = ::send(raw(handle), buffer, length, MSG_NOSIGNAL);
#endif
    if (result < 0) {
        would_block = is_would_block(last_socket_error());
    }
    return static_cast<long>(result);
}

}  // namespace kingdom_fight
