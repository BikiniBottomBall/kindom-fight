// 内部工厂声明：把具体实现藏进 .cpp，上层只看到 Poller 接口。
#pragma once

#include <memory>

#include "net/poller.h"

namespace kingdom_fight {

std::unique_ptr<Poller> make_select_poller();
std::unique_ptr<Poller> make_epoll_poller();  // 非 Linux 平台返回 nullptr

}  // namespace kingdom_fight
