#include "net/poller.h"

#include <cstdlib>
#include <string>

#include "net/poller_impl.h"

namespace kingdom_fight {

std::unique_ptr<Poller> Poller::create() {
    const char* forced = std::getenv("KF_POLLER");
    if (forced != nullptr && std::string(forced) == "select") {
        return make_select_poller();
    }
    if (auto epoll = make_epoll_poller()) {
        return epoll;
    }
    return make_select_poller();
}

}  // namespace kingdom_fight
