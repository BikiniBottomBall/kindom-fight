// 纯文本报告：把 GameState 与事件流打成人类可读的东西。
// S10 的终端客户端会复用它；Godot 客户端以后拿的是事件序列（同样的数据类型）。
#pragma once

#include <vector>

#include "logic/engine.h"
#include "logic/event.h"

namespace landgrab {

void print_game_state(const GameState& state);
void print_event(const Event& event);
void print_events(const std::vector<Event>& events);
void print_event_summary(const std::vector<Event>& log);

}  // namespace landgrab
