# CMake 构建（Linux / WSL / CI 用）。
# 本机（Windows，无 cmake）请用 build.ps1 —— 两者编译同一批源码。
cmake_minimum_required(VERSION 3.20)
project(landgrab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- 统一警告开关 ---------------------------------------------------------
add_library(landgrab_warnings INTERFACE)
if(MSVC)
    # /utf-8: 源码与执行字符集都用 UTF-8，否则中文卡牌名会乱码
    target_compile_options(landgrab_warnings INTERFACE /W4 /utf-8)
else()
    target_compile_options(landgrab_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# --- 核心库：纯逻辑，零网络、零数据库、零线程 -----------------------------
add_library(landgrab_core STATIC
    src/logic/types.cpp
    src/logic/region.cpp
    src/logic/card_def.cpp
    src/logic/command.cpp
    src/logic/event.cpp
    src/logic/engine.cpp
    src/logic/report.cpp
)
target_include_directories(landgrab_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(landgrab_core PRIVATE landgrab_warnings)

# --- demo ----------------------------------------------------------------
add_executable(landgrab src/main.cpp)
target_link_libraries(landgrab PRIVATE landgrab_core landgrab_warnings)

# --- 单元测试 ------------------------------------------------------------
enable_testing()
add_executable(landgrab_tests
    tests/main.cpp
    tests/test_types.cpp
    tests/test_rng.cpp
    tests/test_region.cpp
    tests/test_cards.cpp
    tests/test_engine.cpp
)
target_include_directories(landgrab_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(landgrab_tests PRIVATE landgrab_core landgrab_warnings)
add_test(NAME unit COMMAND landgrab_tests)
