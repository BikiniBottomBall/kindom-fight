#!/usr/bin/env bash
# Linux / WSL 构建脚本。Windows 本机请用 build.ps1（那里没有 cmake）。
#
#   ./build.sh            编译全部 + 跑单测
#   ./build.sh test       只编译并运行单测
#   ./build.sh echo       只编译 echo 服务端（S12 验收用）
#
# 为什么单独一个脚本：这台机器上 WSL 里能跑真正的 epoll，而 Windows 只能跑 select。
# 两条路径都要能编过、都要能跑，否则"跨平台"就只是嘴上说说。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

OUT="$ROOT/build-linux"
mkdir -p "$OUT"

CXX="${CXX:-g++}"
CXXFLAGS=(-std=c++20 -Wall -Wextra -Wpedantic -Wshadow -I"$ROOT/src" -O2 -g)

mapfile -t CORE < <(find src -name '*.cpp' ! -name 'main.cpp' | sort)

build() {
    local name="$1"
    shift
    echo "[cxx] $name"
    "$CXX" "${CXXFLAGS[@]}" -o "$OUT/$name" "${CORE[@]}" "$@"
}

case "${1:-all}" in
    test)
        build kf_tests tests/*.cpp
        "$OUT/kf_tests"
        ;;
    echo)
        build kf_echo tools/echo_server.cpp
        ;;
    all)
        build kf_tests tests/*.cpp
        build kf_echo tools/echo_server.cpp
        build kf_cli client/cli/main.cpp
        build kf_demo src/main.cpp
        echo ''
        echo '--- unit tests ---'
        "$OUT/kf_tests" | tail -n 2
        echo ''
        echo "build ok: $OUT/{kf_tests,kf_echo,kf_cli,kf_demo}"
        ;;
    *)
        echo "usage: build.sh [all|test|echo]" >&2
        exit 2
        ;;
esac
