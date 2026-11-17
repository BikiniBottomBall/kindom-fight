#!/usr/bin/env bash
# Linux / WSL 构建脚本。Windows 本机请用 build.ps1（那里没有 cmake）。
#
#   ./build.sh            编译全部 + 跑单测
#   ./build.sh test       只编译并运行单测
#   ./build.sh echo       只编译 echo 服务端（S12 验收用）
#   ./build.sh bench [N]  跑 N 局 AI 对局统计（默认 12，带节奏门禁）
#   ./build.sh smoke      起服务端 + 用 python 冒烟客户端打一遍真实 TCP（CI 用）
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
    bench)
        # 出厂节奏 + 门禁：和 CI 用同一条命令、同一组阈值
        build kf_bench tools/rts_bench.cpp
        "$OUT/kf_bench" --matches "${2:-12}" --shipping --check \
            --max-avg-seconds 720 --max-timeout-percent 30
        ;;
    smoke)
        # 真实 TCP 端到端：起服务端 → 等端口 → python 客户端断言协议 → 收掉。
        # 不依赖 Godot，所以 CI 上能跑（客户端 GDScript 那套见 tools/run_godot_tests.sh）。
        build kf_server tools/rts_server.cpp
        PORT="${2:-9777}"
        "$OUT/kf_server" --port "$PORT" > /tmp/kf_smoke_server.log 2>&1 &
        SERVER_PID=$!
        trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT
        python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
for _ in range(200):
    try:
        socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
        sys.exit(0)
    except OSError:
        time.sleep(0.05)
sys.exit("服务端 3 秒内没起来（看 /tmp/kf_smoke_server.log）")
PY
        python3 tools/smoke_rts.py --port "$PORT" --seconds "${3:-20}"
        echo '--- 服务端日志 ---'
        tail -n 20 /tmp/kf_smoke_server.log
        ;;
    all)
        build kf_tests tests/*.cpp
        build kf_echo tools/echo_server.cpp
        build kf_cli client/cli/main.cpp
        build kf_demo src/main.cpp
        build kf_server tools/rts_server.cpp
        build kf_bench tools/rts_bench.cpp
        echo ''
        echo '--- unit tests ---'
        "$OUT/kf_tests" | tail -n 2
        echo ''
        echo "build ok: $OUT/{kf_tests,kf_echo,kf_cli,kf_demo,kf_server,kf_bench}"
        ;;
    *)
        echo "usage: build.sh [all|test|echo|bench [N]|smoke [port] [seconds]]" >&2
        exit 2
        ;;
esac
