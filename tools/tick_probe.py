"""量一下服务端的模拟速度和真实时间是否 1:1。

用法：python3 tools/tick_probe.py --port 9700
输出：每秒采一次，打印 采样秒数 / 模拟 tick / tick 速率 / 资源增长速度。
"""
import argparse
import socket
import struct
import sys
import time

C2S_DECK = 103
S2C_SNAPSHOT = 106
S2C_MATCH_START = 104


class Buf:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u16(self):
        v = struct.unpack_from(">H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self):
        v = struct.unpack_from(">I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def i16(self):
        v = struct.unpack_from(">h", self.data, self.pos)[0]
        self.pos += 2
        return v

    def skip(self, n):
        self.pos += n

    def text(self):
        n = self.u16()
        s = self.data[self.pos:self.pos + n].decode("utf-8", "replace")
        self.pos += n
        return s


def parse_snapshot(payload):
    b = Buf(payload)
    snap = {"tick": b.u32()}
    snap["manpower"] = b.u16()
    b.u16()
    snap["materiel"] = b.u16()
    b.u16()
    b.u16()
    b.u16()  # territory
    n = b.u8()
    b.skip(2 * n)
    b.skip(8)  # bases
    for _ in range(b.u8()):
        b.skip(10)
    for _ in range(b.u16()):
        b.skip(12)
    for _ in range(b.u8()):
        b.skip(10)
    for _ in range(b.u8()):
        b.skip(9)
    b.skip(3)  # phase / remaining(2)
    b.skip(1)  # alive
    b.skip(5)  # weather/day/hq/event(2)
    for _ in range(b.u8()):
        b.skip(9)
    return snap


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9700)
    parser.add_argument("--seconds", type=float, default=15.0)
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def send_frame(cmd, payload):
        sock.sendall(struct.pack(">HBHII", 0x4B46, 1, cmd, 1, len(payload)) + payload)

    # 等卡池，然后提交一副牌（联商会的 6 张）
    deck = [7, 8, 9, 10, 11, 12]
    body = struct.pack(">B", len(deck)) + b"".join(struct.pack(">H", c) for c in deck)
    body += struct.pack(">B", 1)  # alliance = combine
    send_frame(C2S_DECK, body)

    start = time.time()
    samples = []
    first_tick = None
    while time.time() - start < args.seconds:
        data = sock.recv(65536)
        if not data:
            break
        pos = 0
        while pos + 13 <= len(data):
            cmd = struct.unpack_from(">H", data, pos + 3)[0]
            length = struct.unpack_from(">I", data, pos + 9)[0]
            payload = data[pos + 13:pos + 13 + length]
            pos += 13 + length
            if cmd != S2C_SNAPSHOT:
                continue
            snap = parse_snapshot(payload)
            if first_tick is None:
                first_tick = snap["tick"]
            samples.append((time.time() - start, snap["tick"], snap["materiel"]))
        time.sleep(0.05)

    if len(samples) < 2:
        print("没有收到足够的快照")
        return 1

    # 每秒取一个样本
    print("  秒   | 模拟 tick | tick/秒 | 物资")
    last = -1
    for elapsed, tick, materiel in samples:
        whole = int(elapsed)
        if whole == last:
            continue
        last = whole
        if tick > first_tick:
            rate = (tick - first_tick) / max(elapsed, 0.001)
        else:
            rate = 0.0
        print("%6.1f | %9d | %7.1f | %d" % (elapsed, tick, rate, materiel))

    total_elapsed = samples[-1][0] - samples[0][0]
    total_ticks = samples[-1][1] - samples[0][1]
    print("\n实测：%.1f 秒真实时间里跑了 %d tick = %.1f tick/秒（设计值 20）"
          % (total_elapsed, total_ticks, total_ticks / max(total_elapsed, 0.001)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
