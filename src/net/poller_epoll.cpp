// epoll 实现（Linux 专用）。
//
// 用的是**水平触发（LT）**，不是 ET。理由：
//   ET 要求每次事件都必须把数据读到 EAGAIN，漏读一次这条连接就会永久饿死，
//   而且是"偶发、难复现"的那种 bug。先用 LT 保证正确性，等压测阶段再对比
//   ET 的收益（这正好是简历里可以讲的"取舍"而不是"我知道 ET"）。
#include "net/poller_impl.h"

#if defined(__linux__)

#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

namespace kingdom_fight {
namespace {

std::uint32_t to_native_events(std::uint32_t interests) {
    std::uint32_t events = 0;
    if ((interests & kPollRead) != 0) {
        events |= EPOLLIN;
    }
    if ((interests & kPollWrite) != 0) {
        events |= EPOLLOUT;
    }
    return events;
}

class EpollPoller final : public Poller {
public:
    EpollPoller() {
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    }

    ~EpollPoller() override {
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
        }
    }

    [[nodiscard]] const char* name() const override { return "epoll(LT)"; }

    bool add(SocketHandle handle, std::uint32_t interests) override {
        if (epoll_fd_ < 0) {
            return false;
        }
        epoll_event event{};
        event.events = to_native_events(interests);
        event.data.fd = static_cast<int>(handle);
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, static_cast<int>(handle), &event) == 0;
    }

    bool modify(SocketHandle handle, std::uint32_t interests) override {
        if (epoll_fd_ < 0) {
            return false;
        }
        epoll_event event{};
        event.events = to_native_events(interests);
        event.data.fd = static_cast<int>(handle);
        return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, static_cast<int>(handle), &event) == 0;
    }

    bool remove(SocketHandle handle) override {
        if (epoll_fd_ < 0) {
            return false;
        }
        return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, static_cast<int>(handle), nullptr) == 0;
    }

    int wait(std::vector<PollEvent>& ready, int timeout_ms) override {
        if (epoll_fd_ < 0) {
            return -1;
        }
        constexpr int kMaxEvents = 128;
        epoll_event events[kMaxEvents];
        const int count = epoll_wait(epoll_fd_, events, kMaxEvents, timeout_ms);
        if (count < 0) {
            if (errno == EINTR) {
                ready.clear();
                return 0;
            }
            return -1;
        }
        ready.clear();
        ready.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            PollEvent event;
            event.handle = static_cast<SocketHandle>(events[i].data.fd);
            if ((events[i].events & EPOLLIN) != 0) {
                event.flags |= kPollRead;
            }
            if ((events[i].events & EPOLLOUT) != 0) {
                event.flags |= kPollWrite;
            }
            if ((events[i].events & (EPOLLERR | EPOLLHUP)) != 0) {
                event.error = true;
            }
            ready.push_back(event);
        }
        return count;
    }

private:
    int epoll_fd_ = -1;
};

}  // namespace

std::unique_ptr<Poller> make_epoll_poller() { return std::make_unique<EpollPoller>(); }

}  // namespace kingdom_fight

#else

namespace kingdom_fight {
std::unique_ptr<Poller> make_epoll_poller() { return nullptr; }
}  // namespace kingdom_fight

#endif
