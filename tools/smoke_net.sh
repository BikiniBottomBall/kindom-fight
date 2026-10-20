#!/usr/bin/env bash
# 网络层冒烟测试（S11-S13 验收）：起 echo 服务端 → 用 python3 当客户端 → 验证三种边界情况。
#
#   ./tools/smoke_net.sh [port]
#
# 在 WSL / Linux 上跑的是 epoll；在设置了 KF_POLLER=select 时跑的是 select。
# 两种实现必须都能过同一组用例，否则"跨平台"只是嘴上说说。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PORT="${1:-19527}"
POLLER="${2:-}"
if [ -n "$POLLER" ]; then
    export KF_POLLER="$POLLER"
fi
LOG="$(mktemp -t kf_echo.XXXXXX.log)"

echo "=== 构建 ==="
bash build.sh echo

echo "=== 启动 echo 服务端（port=$PORT, poller=${KF_POLLER:-auto}）==="
./build-linux/kf_echo --port "$PORT" --timeout 30 >"$LOG" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT
sleep 0.4

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "服务端启动失败："
    cat "$LOG"
    exit 1
fi

echo "=== 客户端用例 ==="
python3 - "$PORT" <<'PY'
import socket, struct, sys, time

port = int(sys.argv[1])
MAGIC = 0x4B46


def frame(cmd, seq, payload):
    body = payload.encode()
    return struct.pack('>HBHII', MAGIC, 1, cmd, seq, len(body)) + body


def recv_frame(sock):
    head = b''
    while len(head) < 13:
        chunk = sock.recv(13 - len(head))
        if not chunk:
            raise SystemExit('连接被提前关闭')
        head += chunk
    magic, ver, cmd, seq, plen = struct.unpack('>HBHII', head)
    body = b''
    while len(body) < plen:
        chunk = sock.recv(plen - len(body))
        if not chunk:
            raise SystemExit('连接被提前关闭')
        body += chunk
    return magic, cmd, seq, body.decode()


sock = socket.create_connection(('127.0.0.1', port), timeout=5)
fails = 0

sock.sendall(frame(0x5A, 7, 'hello'))
magic, cmd, seq, payload = recv_frame(sock)
ok = magic == MAGIC and cmd == 0x5B and seq == 7 and payload == 'hello'
print(f'  case1 完整帧           : {"PASS" if ok else "FAIL"}')
fails += 0 if ok else 1

split = frame(0x01, 1, 'pingpong')
sock.sendall(split[:5])
time.sleep(0.10)
sock.sendall(split[5:])
magic, cmd, seq, payload = recv_frame(sock)
ok = cmd == 0x02
print(f'  case2 半包（拆两次写）  : {"PASS" if ok else "FAIL"}')
fails += 0 if ok else 1

sock.sendall(frame(0x5A, 11, 'aa') + frame(0x5A, 12, 'bbb'))
_, _, seq1, payload1 = recv_frame(sock)
_, _, seq2, payload2 = recv_frame(sock)
ok = payload1 == 'aa' and payload2 == 'bbb'
print(f'  case3 粘包（两帧一次发）: {"PASS" if ok else "FAIL"}')
fails += 0 if ok else 1

sock.close()
print(f'RESULT: {"ALL PASS" if fails == 0 else str(fails) + " FAILED"}')
sys.exit(0 if fails == 0 else 1)
PY

echo "=== 服务端日志 ==="
cat "$LOG"
