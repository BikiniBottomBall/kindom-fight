#!/usr/bin/env python3
"""RTS 服务端的命令行冒烟客户端（不依赖 Godot）—— CI 的端到端门禁。

它干四件事：
  1. 用真实 TCP 连上服务端，验证帧格式（magic / version / 长度前缀 / 粘包）
  2. 提交一副 8 张的合法卡组，然后按资源随机出牌
  3. 把 S2C_Map / S2C_Snapshot / S2C_Territory 全字段解出来，断言"每个字节都被吃掉"
  4. 断言数值在合理范围：hp <= max、坐标在地图内、资源非负、至少收到 N 个快照

字段顺序以 `src/rts/wire.cpp` 的 `encode_*` 为准；`client/godot/scripts/snapshot_parser.gd`
是同一份顺序的 GDScript 版。**改协议要三处一起改**（C++ / GDScript / 这里），
`tests/test_snapshot.gd` 会兜住 GDScript 那边。

用法：
  python3 tools/smoke_rts.py [--host 127.0.0.1] [--port 9700] [--seconds 20] [--min-snapshots 50]

退出码：0 = 通过；1 = 协议或断言失败；2 = 连不上服务端。
"""
import argparse
import random
import socket
import struct
import sys
import time

MAGIC = 0x4B46
VERSION = 1
HEADER = 13
PLAYERS = 4

C2S_DECK = 103
C2S_PLAY = 105
C2S_REMATCH = 109
S2C_WELCOME = 101
S2C_CARD_POOL = 102
S2C_MATCH_START = 104
S2C_SNAPSHOT = 106
S2C_MATCH_OVER = 107
S2C_ERROR = 108
S2C_PLAY_ACK = 110
S2C_MAP = 111
S2C_DEMOLISH_ACK = 115
S2C_UPGRADE_ACK = 117
S2C_REPAIR_ACK = 119
S2C_DISCARD_ACK = 121
S2C_TERRITORY = 122
S2C_BUILD_TOWER_ACK = 124
S2C_SET_UNIT_ACK = 126
S2C_SET_RALLY_ACK = 128
S2C_UNIT_TABLE = 129

CMD_NAMES = {
    S2C_WELCOME: "Welcome",
    S2C_CARD_POOL: "CardPool",
    S2C_MATCH_START: "MatchStart",
    S2C_SNAPSHOT: "Snapshot",
    S2C_MATCH_OVER: "MatchOver",
    S2C_ERROR: "Error",
    S2C_PLAY_ACK: "PlayAck",
    S2C_MAP: "Map",
    S2C_DEMOLISH_ACK: "DemolishAck",
    S2C_UPGRADE_ACK: "UpgradeAck",
    S2C_REPAIR_ACK: "RepairAck",
    S2C_DISCARD_ACK: "DiscardAck",
    S2C_TERRITORY: "Territory",
    S2C_BUILD_TOWER_ACK: "BuildTowerAck",
    S2C_SET_UNIT_ACK: "SetUnitAck",
    S2C_SET_RALLY_ACK: "SetRallyAck",
    S2C_UNIT_TABLE: "UnitTable",
}

class ProtoError(Exception):
    """协议层错误：帧格式不对、字段读不全、字节数对不上。"""


class Buf:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def left(self):
        return len(self.data) - self.pos

    def _need(self, n):
        if self.left() < n:
            raise ProtoError("读越界：需要 %d 字节，只剩 %d（位置 %d / 共 %d）"
                             % (n, self.left(), self.pos, len(self.data)))

    def u8(self):
        self._need(1)
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u16(self):
        self._need(2)
        (v,) = struct.unpack_from(">H", self.data, self.pos)
        self.pos += 2
        return v

    def u32(self):
        self._need(4)
        (v,) = struct.unpack_from(">I", self.data, self.pos)
        self.pos += 4
        return v

    def i16(self):
        self._need(2)
        (v,) = struct.unpack_from(">h", self.data, self.pos)
        self.pos += 2
        return v

    def raw(self, n):
        self._need(n)
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def text(self):
        n = self.u16()
        return self.raw(n).decode("utf-8", "replace")


class Conn:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.05)
        self.buf = b""
        self.seq = 0

    def send(self, cmd, payload=b""):
        self.seq += 1
        header = struct.pack(">H B H I I", MAGIC, VERSION, cmd, self.seq, len(payload))
        self.sock.sendall(header + payload)

    def read_frames(self):
        frames = []
        try:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ProtoError("服务端关闭了连接")
            self.buf += chunk
        except socket.timeout:
            pass
        while len(self.buf) >= HEADER:
            magic, ver, cmd, seq, length = struct.unpack_from(">H B H I I", self.buf, 0)
            if magic != MAGIC:
                raise ProtoError("帧头 magic 不对: 0x%04X" % magic)
            if ver != VERSION:
                raise ProtoError("帧版本不对: %d" % ver)
            if length > 64 * 1024:
                raise ProtoError("帧长度超上限: %d" % length)
            if len(self.buf) < HEADER + length:
                break
            payload = self.buf[HEADER:HEADER + length]
            self.buf = self.buf[HEADER + length:]
            frames.append((cmd, seq, payload))
        return frames

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def parse_snapshot(payload):
    """逐字段对应 wire::encode_snapshot（viewer = 自己）。"""
    b = Buf(payload)
    snap = {"tick": b.u32()}

    # 人力 / 人力上限 / 物资 / 能源 / 科技 / 领地
    snap["manpower"] = b.u16()
    snap["manpower_cap"] = b.u16()
    snap["materiel"] = b.u16()
    snap["energy"] = b.u16()
    snap["tech"] = b.u16()
    snap["territory"] = b.u16()

    snap["hand"] = [b.u16() for _ in range(b.u8())]
    snap["bases"] = [b.u16() for _ in range(PLAYERS)]

    towers = []
    for _ in range(b.u8()):
        towers.append({"owner": b.u8(), "id": b.u16(), "progress": b.u8(),
                       "x": b.i16(), "y": b.i16(),
                       "hp": b.u16(), "max": b.u16()})
    snap["towers"] = towers

    units = []
    for _ in range(b.u16()):
        units.append({"id": b.u16(), "owner": b.u8(), "type": b.u8(),
                      "x": b.i16(), "y": b.i16(),
                      "hp": b.u16(), "max": b.u16(), "flags": b.u8()})
    snap["units"] = units

    buildings = []
    for _ in range(b.u8()):
        buildings.append({"owner": b.u8(), "type": b.u8(),
                          "x": b.i16(), "y": b.i16(),
                          "hp": b.u16(), "max": b.u16(), "progress": b.u8(),
                          "train_unit": b.u8(), "rally_x": b.i16(), "rally_y": b.i16()})
    snap["buildings"] = buildings

    effects = []
    for _ in range(b.u8()):
        effects.append({"kind": b.u8(), "owner": b.u8(),
                        "x": b.i16(), "y": b.i16(),
                        "radius": b.u16(), "ttl": b.u8()})
    snap["effects"] = effects

    snap["phase"] = b.u8()
    snap["remaining"] = b.u16()
    snap["alive"] = b.u8()
    snap["weather"] = b.u8()
    snap["day_phase"] = b.u8()
    snap["hq_state"] = b.u8()
    snap["event"] = b.u8()
    snap["event_left"] = b.u16()

    # 资源片区：位置 + 半径 + 归属 + 占点进度 + 城镇核心
    nodes = []
    for _ in range(b.u8()):
        nodes.append({"kind": b.u8(), "x": b.i16(), "y": b.i16(),
                      "owner": b.u8() - 1, "progress": b.u8(), "capturer": b.u8() - 1,
                      "flags": b.u8(), "radius": b.u16(),
                      "core_hp": b.u16(), "core_max": b.u16()})
    snap["nodes"] = nodes

    # 局内成长树：12 个节点（是否点亮 + 价格）
    snap["upgrades"] = [{"taken": b.u8() != 0, "cost": b.u16()} for _ in range(b.u8())]
    snap["building_cap"] = b.u8()
    snap["tower_cap"] = b.u8()

    if b.left() != 0:
        raise ProtoError("快照长度对不上：解析完还剩 %d 字节（字段顺序漂移）" % b.left())
    return snap


def check_snapshot(snap, map_w, map_h, failures):
    """数值断言：把"能解析但不合理"的情况也变成红灯。"""
    tag = "tick=%d" % snap["tick"]

    for key in ("manpower", "materiel", "energy", "tech", "territory"):
        if snap[key] < 0:
            failures.append("%s %s 为负" % (tag, key))
    if snap["manpower"] > snap["manpower_cap"]:
        failures.append("%s 人力 %d 超过上限 %d"
                        % (tag, snap["manpower"], snap["manpower_cap"]))
    if not 1 <= len(snap["hand"]) <= 8:
        failures.append("%s 手牌张数异常: %d" % (tag, len(snap["hand"])))

    for t in snap["towers"]:
        if t["hp"] > t["max"]:
            failures.append("%s 防御塔 hp %d > max %d" % (tag, t["hp"], t["max"]))
        if not (0 <= t["x"] <= map_w and 0 <= t["y"] <= map_h):
            failures.append("%s 防御塔坐标越界 (%d,%d)" % (tag, t["x"], t["y"]))
        if not 0 <= t["progress"] <= 100:
            failures.append("%s 防御塔建造进度越界: %d" % (tag, t["progress"]))

    for u in snap["units"]:
        if u["hp"] > u["max"]:
            failures.append("%s 单位 hp %d > max %d" % (tag, u["hp"], u["max"]))
        if not (0 <= u["x"] <= map_w and 0 <= u["y"] <= map_h):
            failures.append("%s 单位坐标越界 (%d,%d)" % (tag, u["x"], u["y"]))

    for b in snap["buildings"]:
        if b["hp"] > b["max"]:
            failures.append("%s 建筑 hp %d > max %d" % (tag, b["hp"], b["max"]))
        if not (0 <= b["x"] <= map_w and 0 <= b["y"] <= map_h):
            failures.append("%s 建筑坐标越界 (%d,%d)" % (tag, b["x"], b["y"]))
        if not 0 <= b["progress"] <= 100:
            failures.append("%s 建筑进度越界: %d" % (tag, b["progress"]))
        # 255 = 用阵营默认兵种
        if b["train_unit"] != 255 and b["train_unit"] > 8:
            failures.append("%s 兵营兵种越界: %d" % (tag, b["train_unit"]))

    for n in snap["nodes"]:
        if not 0 <= n["owner"] < PLAYERS and n["owner"] != -1:
            failures.append("%s 资源点归属越界: %d" % (tag, n["owner"]))
        if n["core_hp"] > n["core_max"]:
            failures.append("%s 城镇核心 hp %d > max %d"
                            % (tag, n["core_hp"], n["core_max"]))

    if len(snap["upgrades"]) != 12:
        failures.append("%s 成长树节点数不是 12: %d" % (tag, len(snap["upgrades"])))
    if snap["building_cap"] <= 0:
        failures.append("%s 建筑上限不合理: %d" % (tag, snap["building_cap"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9700)
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--min-snapshots", type=int, default=50)
    ap.add_argument("--deck", default="0,1,2,3,4,5,6,28",
                    help="8 张卡，必须在同一阵营的池子里（先锋军团 = 0~6 + 28/32/36），"
                         "同卡最多 2 张")
    args = ap.parse_args()

    # Windows 终端默认 cp936，中文日志会花；CI 上是 UTF-8 无所谓，这里统一一下
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except (OSError, ValueError):
            pass

    deck = [int(x) for x in args.deck.split(",") if x.strip()]
    if len(deck) != 8:
        print("[smoke] 卡组必须是 8 张（kDeckSize=8），现在是 %d 张" % len(deck))
        return 1

    try:
        conn = Conn(args.host, args.port)
    except OSError as exc:
        print("[smoke] 连不上 %s:%d —— %s" % (args.host, args.port, exc))
        return 2

    print("[smoke] 已连接 %s:%d，卡组 %s" % (args.host, args.port, deck))

    costs = {}
    bases = [(0, 0)] * PLAYERS
    map_w, map_h = 4800, 3200
    territory_cells = 0
    unit_table = []
    snapshots = 0
    plays = 0
    accepted = 0
    rejected = 0
    last_report = -1000
    last_play_tick = -1000
    seen = {}
    failures = []
    started = time.time()
    rng = random.Random(7)
    result = None
    rematched = False

    def note(cmd):
        seen[CMD_NAMES.get(cmd, "cmd=%d" % cmd)] = \
            seen.get(CMD_NAMES.get(cmd, "cmd=%d" % cmd), 0) + 1

    try:
        while time.time() - started < args.seconds:
            for cmd, _seq, payload in conn.read_frames():
                note(cmd)
                if cmd == S2C_WELCOME:
                    b = Buf(payload)
                    player = b.u8()
                    ai = b.u8()
                    human = b.u8()
                    players = b.u8()
                    deck_size = b.u8()
                    hand_size = b.u8()
                    pool_size = b.u8()
                    build_stamp = b.text()
                    print("[smoke] welcome: player=%d ai=%d human=%d players=%d "
                          "deck=%d hand=%d pool=%d build=%s"
                          % (player, ai, human, players, deck_size, hand_size,
                             pool_size, build_stamp))
                    if deck_size != 8:
                        failures.append("welcome 里 kDeckSize=%d，期望 8" % deck_size)
                    if pool_size != 40:
                        failures.append("welcome 里卡池=%d，期望 40" % pool_size)
                    body = (struct.pack(">B", len(deck))
                            + b"".join(struct.pack(">H", c) for c in deck)
                            + struct.pack(">B", 0))  # 联盟 0 = 先锋军团
                    conn.send(C2S_DECK, body)

                elif cmd == S2C_CARD_POOL:
                    b = Buf(payload)
                    n = b.u16()
                    names = []
                    for _ in range(n):
                        cid = b.u16()
                        b.u8()   # alliance
                        b.u8()   # kind
                        m, f, e, t = b.u16(), b.u16(), b.u16(), b.u16()
                        b.u16()  # radius（范围技能的作用半径）
                        b.u8()   # building（255 = 不是建筑）
                        b.u8()   # spell
                        names.append(b.text())
                        costs[cid] = (m, f, e, t)
                    if b.left() != 0:
                        failures.append("卡池解析还剩 %d 字节" % b.left())
                    print("[smoke] 卡池 %d 张: %s" % (n, " ".join(names[:8]) + " …"))
                    if n != 40:
                        failures.append("卡池张数 %d，期望 40" % n)

                elif cmd == S2C_MAP:
                    b = Buf(payload)
                    cols = b.u16()
                    rows = b.u16()
                    tile = b.u16()
                    base_max = [b.u16() for _ in range(PLAYERS)]
                    bases = [(b.i16(), b.i16()) for _ in range(PLAYERS)]
                    alliances = [b.u8() for _ in range(PLAYERS)]
                    tiles = b.raw(cols * rows)
                    map_w, map_h = cols * tile, rows * tile
                    counts = {}
                    for t in tiles:
                        counts[t] = counts.get(t, 0) + 1
                    print("[smoke] 地图 %dx%d 格（每格 %dpx = %dx%d 像素），基地 %s，满血 %s，"
                          "联盟 %s，地形 %s"
                          % (cols, rows, tile, map_w, map_h, bases, base_max,
                             alliances, sorted(counts.items())))
                    if len(set(bases)) != PLAYERS:
                        failures.append("4 家出生点有重复: %s" % (bases,))
                    if b.left() != 0:
                        failures.append("地图解析还剩 %d 字节" % b.left())

                elif cmd == S2C_MATCH_START:
                    b = Buf(payload)
                    print("[smoke] 开局：player=%d 时限=%ds 总部支援=%ds"
                          % (b.u8(), b.u16(), b.u16()))

                elif cmd == S2C_TERRITORY:
                    b = Buf(payload)
                    size = b.u16()
                    mask = b.raw(size)
                    territory_cells = sum(1 for c in mask if c)
                    if size != 60 * 40:
                        failures.append("领地掩码 %d 字节，期望 2400" % size)

                elif cmd == S2C_UNIT_TABLE:
                    b = Buf(payload)
                    n = b.u8()
                    unit_table = []
                    for _ in range(n):
                        uid = b.u8()
                        train = b.u16()
                        man = b.u16()
                        mat = b.u16()
                        name = b.text()
                        unit_table.append((uid, train, man, mat, name))
                    if b.left() != 0:
                        failures.append("兵种表解析还剩 %d 字节" % b.left())
                    print("[smoke] 兵种表 %d 个: %s"
                          % (n, " ".join("%s(%d练)" % (u[4], u[1] // 20) for u in unit_table)))
                    if n < 8:
                        failures.append("兵种表只有 %d 个，期望 >= 8" % n)

                elif cmd == S2C_SNAPSHOT:
                    snap = parse_snapshot(payload)
                    snapshots += 1
                    check_snapshot(snap, map_w, map_h, failures)

                    if snap["tick"] - last_report >= 100:
                        last_report = snap["tick"]
                        owned = sum(1 for n in snap["nodes"] if n["owner"] >= 0)
                        print("[t=%5d] 人=%d/%d 物=%d 能=%d 科=%d 领=%d bases=%s alive=%d "
                              "units=%d buildings=%d nodes=%d/%d left=%ds"
                              % (snap["tick"], snap["manpower"], snap["manpower_cap"],
                                 snap["materiel"], snap["energy"], snap["tech"],
                                 snap["territory"], snap["bases"], snap["alive"],
                                 len(snap["units"]), len(snap["buildings"]), owned,
                                 len(snap["nodes"]), snap["remaining"]))

                    res = (snap["manpower"], snap["materiel"], snap["energy"], snap["tech"])
                    affordable = [i for i, cid in enumerate(snap["hand"])
                                  if all(costs.get(cid, (9999, 9999, 9999, 9999))[k] <= res[k]
                                         for k in range(4))]
                    if (affordable and snap["tick"] - last_play_tick >= 40
                            and rng.random() < 0.6):
                        slot = rng.choice(affordable)
                        my_base = bases[0]
                        x = my_base[0] + rng.randint(-320, 320)
                        y = my_base[1] + rng.randint(-320, 320)
                        x = max(70, min(map_w - 70, x))
                        y = max(70, min(map_h - 70, y))
                        conn.send(C2S_PLAY, struct.pack(">B h h", slot, x, y))
                        plays += 1
                        last_play_tick = snap["tick"]

                elif cmd == S2C_PLAY_ACK:
                    b = Buf(payload)
                    b.u8()  # slot
                    if b.u8() == 0:
                        accepted += 1
                    else:
                        rejected += 1

                elif cmd == S2C_MATCH_OVER:
                    b = Buf(payload)
                    winner = b.u8()
                    reason = b.u8()
                    final_bases = [b.u16() for _ in range(PLAYERS)]
                    print("[smoke] 对局结束：winner=%s reason=%d bases=%s"
                          % ("无" if winner == 255 else winner, reason, final_bases))
                    if not rematched:
                        rematched = True
                        conn.send(C2S_REMATCH)
                        print("[smoke] 发送 rematch，等新的一局…")
                    else:
                        result = (winner, reason, final_bases)
                        break

                elif cmd == S2C_ERROR:
                    print("[smoke] 服务端错误: %s" % Buf(payload).text())
                    failures.append("服务端返回了 S2C_Error")

                elif cmd in (S2C_DEMOLISH_ACK, S2C_UPGRADE_ACK, S2C_REPAIR_ACK,
                             S2C_DISCARD_ACK, S2C_BUILD_TOWER_ACK, S2C_SET_UNIT_ACK,
                             S2C_SET_RALLY_ACK):
                    pass  # 冒烟客户端不主动发这些指令，收到就忽略

                else:
                    failures.append("收到未知消息 cmd=%d len=%d" % (cmd, len(payload)))

            if isinstance(result, tuple):
                break
            time.sleep(0.005)
    except ProtoError as exc:
        failures.append("协议错误: %s" % exc)
    finally:
        conn.close()

    elapsed = time.time() - started
    print("[smoke] %.1f 秒内收到 %d 个快照 / %d 条消息 %s"
          % (elapsed, snapshots, sum(seen.values()), seen))
    print("[smoke] 出牌 %d 次（接受 %d / 被拒 %d），领地格 %d，兵种 %d 个"
          % (plays, accepted, rejected, territory_cells, len(unit_table)))

    if snapshots < args.min_snapshots:
        failures.append("只收到 %d 个快照，少于要求的 %d" % (snapshots, args.min_snapshots))
    if territory_cells <= 0:
        failures.append("领地掩码全是 0（S2C_Territory 没生效）")
    if accepted <= 0:
        failures.append("一张牌都没出成功（%d 次全被拒）" % plays)

    if failures:
        print("[smoke] 失败 %d 项：" % len(failures))
        for f in dict.fromkeys(failures):
            print("  - %s" % f)
        return 1

    print("[smoke] 通过：帧格式 / 地图 / 快照逐字节对齐 / 领地 / 兵种表 / 出牌回执 全部正常")
    return 0


if __name__ == "__main__":
    sys.exit(main())
