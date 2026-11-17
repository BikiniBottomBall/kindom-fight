#!/usr/bin/env bash
# 编译并启动 RTS 服务端（Linux / WSL）。
#
#   ./tools/serve_wsl.sh [port] [easy|normal|hard]
#
# 现在默认不用它了：2026-11-17 复核过，Windows 本机直接跑服务端没问题
# （所谓 Device Guard 拦未签名 exe 的说法已经过时），demo.ps1 默认走本机。
# 这个脚本留着是因为 WSL 里跑的是真正的 epoll，要验那条路径时还得用它。
set -euo pipefail

PORT="${1:-9700}"
AI_LEVEL="${2:-normal}"
cd "$(dirname "${BASH_SOURCE[0]}")/.."

OUT="build/server"
mkdir -p "$OUT"
BIN="$OUT/kingdom_fight_server"

# 只有源文件比二进制新才重编：第二次启动应该 1 秒内起来
NEED_BUILD=0
if [ ! -x "$BIN" ]; then
    NEED_BUILD=1
elif [ -n "$(find src/rts src/net tools/rts_server.cpp src/common -newer "$BIN" -print -quit 2>/dev/null)" ]; then
    NEED_BUILD=1
fi

if [ "$NEED_BUILD" = "1" ]; then
    echo "[serve] 编译服务端…"
    g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wshadow -I src \
        src/rts/cards.cpp src/rts/terrain.cpp src/rts/sim.cpp src/rts/ai.cpp src/rts/wire.cpp \
        src/net/protocol.cpp src/net/decoder.cpp src/net/socket.cpp \
        src/net/poller.cpp src/net/poller_select.cpp src/net/poller_epoll.cpp src/net/channel.cpp \
        tools/rts_server.cpp -o "$BIN"
else
    echo "[serve] 二进制是最新的，跳过编译"
fi

echo "[serve] 启动 0.0.0.0:${PORT}"
exec "$BIN" --port "$PORT" --ai-level "$AI_LEVEL"
