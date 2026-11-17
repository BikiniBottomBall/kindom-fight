#!/usr/bin/env bash
# 客户端（Godot）协议测试。CI 与本机共用同一个入口。
#
#   ./tools/run_godot_tests.sh
#
# Godot 路径按这个顺序找：
#   1. $GODOT_BIN（CI 上装好 Godot 之后把这一条设上就能真正跑起来）
#   2. PATH 里的 godot / godot4
#   3. 本机 Steam 版（D:\steam\...\godot.windows.opt.tools.64.exe，WSL 里也能直接调）
#
# 找不到 Godot 时**不算失败**：打印提示并 exit 0。CI 的 runner 上默认没有 Godot，
# 硬失败只会天天红灯；等你把 runner 配好、或本地跑一遍，这套测试才有意义。
# 注意：跳过 ≠ 通过，日志里会有明确的 [skip] 字样，别当成绿灯。
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

find_godot() {
    if [ -n "${GODOT_BIN:-}" ] && [ -x "${GODOT_BIN}" ]; then echo "$GODOT_BIN"; return 0; fi
    for name in godot godot4 Godot; do
        if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return 0; fi
    done
    for candidate in \
        "/mnt/d/steam/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe" \
        "/d/steam/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
    do
        if [ -x "$candidate" ]; then echo "$candidate"; return 0; fi
    done
    return 1
}

GODOT="$(find_godot)" || {
    echo '[skip] 没找到 Godot：客户端协议测试没跑（不是通过）'
    echo '       设 GODOT_BIN=/path/to/godot 或把 godot 加进 PATH 后重试'
    exit 0
}

echo "[godot] $GODOT"
"$GODOT" --version 2>/dev/null | tail -n 1

status=0
for suite in tests/test_decoder.gd tests/test_snapshot.gd tests/test_art.gd; do
    echo "--- $suite ---"
    if ! "$GODOT" --headless --path client/godot --script "res://$suite"; then
        echo "[fail] $suite 退出码非 0"
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo '[godot] 客户端测试有失败'
    exit 1
fi
echo '[godot] 客户端协议测试全部通过'
