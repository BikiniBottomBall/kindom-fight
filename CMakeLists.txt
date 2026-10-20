# CMake 构建（Linux / WSL / CI 用）。
# 本机（Windows，无 cmake）请用 build.ps1 —— 两者编译同一批源码。
cmake_minimum_required(VERSION 3.20)
project(kingdom_fight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- 统一警告开关 ---------------------------------------------------------
add_library(kingdom_fight_warnings INTERFACE)
if(MSVC)
    # /utf-8: 源码与执行字符集都用 UTF-8，否则中文卡牌名会乱码
    target_compile_options(kingdom_fight_warnings INTERFACE /W4 /utf-8)
else()
    target_compile_options(kingdom_fight_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# --- 核心库：纯逻辑 + AI，零网络、零数据库、零线程 -------------------------
add_library(kingdom_fight_core STATIC
    src/logic/types.cpp
    src/logic/region.cpp
    src/logic/card_def.cpp
    src/logic/command.cpp
    src/logic/combat.cpp
    src/logic/event.cpp
    src/logic/engine.cpp
    src/logic/report.cpp
    src/ai/simple_ai.cpp
    src/net/protocol.cpp
    src/net/decoder.cpp
    src/net/socket.cpp
    src/net/poller.cpp
    src/net/poller_select.cpp
    src/net/poller_epoll.cpp
    src/net/channel.cpp
)
target_include_directories(kingdom_fight_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kingdom_fight_core PRIVATE kingdom_fight_warnings)
if(WIN32)
    target_link_libraries(kingdom_fight_core PUBLIC ws2_32)
endif()

# --- AI 自动对局 demo -----------------------------------------------------
add_executable(kingdom_fight src/main.cpp)
target_link_libraries(kingdom_fight PRIVATE kingdom_fight_core kingdom_fight_warnings)

# --- S10 终端客户端：你 vs AI ---------------------------------------------
add_executable(kingdom_fight_cli client/cli/main.cpp)
target_link_libraries(kingdom_fight_cli PRIVATE kingdom_fight_core kingdom_fight_warnings)

# --- S12 验收用 echo 服务端 -----------------------------------------------
add_executable(kingdom_fight_echo tools/echo_server.cpp)
target_link_libraries(kingdom_fight_echo PRIVATE kingdom_fight_core kingdom_fight_warnings)

# --- 单元测试 ------------------------------------------------------------
enable_testing()
add_executable(kingdom_fight_tests
    tests/main.cpp
    tests/test_types.cpp
    tests/test_rng.cpp
    tests/test_region.cpp
    tests/test_cards.cpp
    tests/test_combat.cpp
    tests/test_protocol.cpp
    tests/test_engine.cpp
)
target_include_directories(kingdom_fight_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(kingdom_fight_tests PRIVATE kingdom_fight_core kingdom_fight_warnings)
add_test(NAME unit COMMAND kingdom_fight_tests)
