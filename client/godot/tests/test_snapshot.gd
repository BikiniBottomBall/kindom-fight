# 快照解析的脱网测试：手写一份"黄金字节"，逐字段断言解析结果。
#
#   godot --headless --path client/godot --script res://tests/test_snapshot.gd
#
# 为什么需要它：客户端以前的 51 项测试只覆盖帧头/卡池，快照字段漂移（比如服务器给
# 节点加了 radius/core_hp，客户端没跟着读）不会报错，只会在运行时把 kind 读成 96
# 然后数组越界。这个文件就是那份"字段清单"——服务器改快照格式时它必须先红。
extends SceneTree

const Parser = preload("res://scripts/snapshot_parser.gd")

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("运行快照解析测试")
	_run()
	print("\n%d 通过 / %d 失败" % [_passed, _failed])
	quit(1 if _failed > 0 else 0)


func _run() -> void:
	_check_full_snapshot()
	_check_truncated_snapshot()
	_check_bad_node_kind_is_clamped()
	_check_trailing_bytes_are_reported()


# ---- 黄金字节 ---------------------------------------------------------------

## 按 C++ encode_snapshot 的顺序拼一份快照；每个字段都用一眼能认出来的值，
## 断言时不用猜"这个 37 是哪来的"。
func _build_bytes() -> PackedByteArray:
	var out := PackedByteArray()

	var u8 := func(v: int) -> void: out.append(v & 0xFF)
	var u16 := func(v: int) -> void:
		out.append((v >> 8) & 0xFF)
		out.append(v & 0xFF)
	var i16 := func(v: int) -> void: u16.call(v & 0xFFFF)
	var u32 := func(v: int) -> void:
		out.append((v >> 24) & 0xFF)
		out.append((v >> 16) & 0xFF)
		out.append((v >> 8) & 0xFF)
		out.append(v & 0xFF)

	u32.call(123456)            # tick
	u16.call(150)               # 人力
	u16.call(160)               # 人力上限
	u16.call(240)               # 物资
	u16.call(160)               # 能源
	u16.call(6)                 # 科技
	u16.call(22)                # 领地

	u8.call(4)                  # 手牌 4 张
	for id in [0, 5, 12, 28]:
		u16.call(id)

	for hp in [6500, 5200, 5200, 5200]:   # 四家基地
		u16.call(hp)

	u8.call(1)                  # 1 座塔
	u8.call(2)                  # owner
	u16.call(77)                # id
	u8.call(50)                 # 建造进度
	i16.call(900)               # x
	i16.call(-300)              # y（负数，验证有符号）
	u16.call(300)               # hp
	u16.call(600)               # max_hp

	u16.call(2)                 # 2 个单位
	for unit in [[11, 0, 2, 400, 500, 90, 110, 3], [12, 1, 5, 700, 800, 250, 520, 0]]:
		u16.call(unit[0])
		u8.call(unit[1])
		u8.call(unit[2])
		i16.call(unit[3])
		i16.call(unit[4])
		u16.call(unit[5])
		u16.call(unit[6])
		u8.call(unit[7])

	u8.call(1)                  # 1 座建筑
	u8.call(0)                  # owner
	u8.call(1)                  # 能源站
	i16.call(-700)
	i16.call(200)
	u16.call(420)
	u16.call(420)
	u8.call(100)                # 已完工
	u8.call(8)                  # 兵营指定训练：冲车
	i16.call(1500)              # 指定攻击区域 x
	i16.call(1300)              # 指定攻击区域 y

	u8.call(1)                  # 1 个特效
	u8.call(1)                  # 火
	u8.call(3)                  # owner
	i16.call(1200)
	i16.call(1400)
	u16.call(220)               # 半径
	u8.call(9)                  # 剩余 tick

	u8.call(1)                  # phase 进行中
	u16.call(987)               # 剩余秒
	u8.call(4)                  # 存活人数
	u8.call(2)                  # 天气
	u8.call(40)                 # 昼夜
	u8.call(1)                  # HQ 状态
	u8.call(3)                  # 事件
	u16.call(55)                # 事件剩余

	u8.call(2)                  # 2 个片区
	# 高资源片区：kind=2，owner 中立，半径 220
	u8.call(2)
	i16.call(1500)
	i16.call(1300)
	u8.call(0)                  # owner+1 = 0 → 中立
	u8.call(35)                 # 占点进度
	u8.call(2)                  # capturer+1 = 2 → player 1
	u8.call(1)                  # 已激活
	u16.call(220)               # radius
	u16.call(0)                 # core_hp
	u16.call(0)                 # core_max
	# 城镇据点：kind=3，核心 2400
	u8.call(3)
	i16.call(2600)
	i16.call(2100)
	u8.call(3)                  # owner+1 = 3 → player 2
	u8.call(80)
	u8.call(0)                  # 中立 capturer
	u8.call(3)                  # active + surge
	u16.call(260)
	u16.call(1700)
	u16.call(2400)

	u8.call(12)                 # 12 个成长树节点
	for i in 12:
		u8.call(1 if i % 3 == 0 else 0)
		u16.call(10 + i)

	u8.call(6)                  # 建筑上限
	u8.call(3)                  # 塔上限
	return out


func _check_full_snapshot() -> void:
	var snap: Dictionary = Parser.parse(_build_bytes())
	_ok(bool(snap.get("ok", false)), "完整快照应解析成功")
	_eq(int(snap.get("bytes_left", -1)), 0, "完整快照不该有剩余字节")

	_eq(int(snap["tick"]), 123456, "tick")
	_eq(int(snap["manpower_cap"]), 160, "人力上限")
	_eq(int(snap["res"][0]), 150, "人力")
	_eq(int(snap["res"][1]), 240, "物资")
	_eq(int(snap["res"][2]), 160, "能源")
	_eq(int(snap["res"][3]), 6, "科技")
	_eq(int(snap["res"][4]), 22, "领地")
	_eq(int(snap["hand"].size()), 4, "手牌张数")
	_eq(int(snap["hand"][3]), 28, "手牌第 4 张")
	_eq(int(snap["bases"][0]), 6500, "基地血量")

	var tower: Dictionary = snap["towers"][0]
	_eq(int(tower["owner"]), 2, "塔归属")
	_eq(int(tower["id"]), 77, "塔 id")
	_eq(int(tower["progress"]), 50, "塔建造进度")
	_eq(int(tower["x"]), 900, "塔 x")
	_eq(int(tower["y"]), -300, "塔 y 负数要能读对")
	_eq(int(tower["max_hp"]), 600, "塔最大血量")

	_eq(int(snap["units"].size()), 2, "单位数量")
	_eq(int(snap["units"][1]["type"]), 5, "第二个单位是攻城车")
	_eq(int(snap["units"][1]["max"]), 520, "单位最大血量")

	var building: Dictionary = snap["buildings"][0]
	_eq(int(building["type"]), 1, "建筑类型")
	_eq(int(building["x"]), -700, "建筑 x 负数")
	_eq(int(building["progress"]), 100, "建筑完工进度")
	_eq(int(building["train_unit"]), 8, "兵营指定训练兵种")
	_eq(int(building["rally_x"]), 1500, "兵营攻击区域 x")
	_eq(int(building["rally_y"]), 1300, "兵营攻击区域 y")

	_eq(int(snap["effects"][0]["radius"]), 220, "特效半径")
	_eq(int(snap["remaining"]), 987, "剩余秒数")
	_eq(int(snap["alive"]), 4, "存活人数")
	_eq(int(snap["event_left"]), 55, "事件剩余")

	var rich: Dictionary = snap["nodes"][0]
	_eq(int(rich["kind"]), 2, "高资源片区 kind")
	_eq(int(rich["x"]), 1500, "片区 x")
	_eq(int(rich["owner"]), -1, "中立片区 owner = -1")
	_eq(int(rich["progress"]), 35, "占点进度")
	_eq(int(rich["capturer"]), 1, "占点者 = player 1")
	_eq(int(rich["radius"]), 220, "片区半径")
	_eq(int(rich["core_hp"]), 0, "普通片区没有核心")

	var town: Dictionary = snap["nodes"][1]
	_eq(int(town["kind"]), 3, "城镇 kind")
	_eq(int(town["owner"]), 2, "城镇归属")
	_eq(int(town["flags"]), 3, "激活 + 爆发")
	_eq(int(town["radius"]), 260, "城镇半径")
	_eq(int(town["core_hp"]), 1700, "城镇核心血量")
	_eq(int(town["core_max"]), 2400, "城镇核心上限")

	_eq(int(snap["upgrades"].size()), 12, "成长树节点数")
	_eq(int(snap["upgrades"][3]["cost"]), 13, "成长树第 4 个节点价格")
	_ok(bool(snap["upgrades"][0]["taken"]), "成长树第 1 个节点已点亮")
	_eq(int(snap["building_cap"]), 6, "建筑上限")
	_eq(int(snap["tower_cap"]), 3, "塔上限")


func _check_truncated_snapshot() -> void:
	var full := _build_bytes()
	var cut := full.slice(0, full.size() - 7)
	var snap: Dictionary = Parser.parse(cut)
	_ok(not bool(snap.get("ok", true)), "截断的快照必须返回 ok=false")
	_ok(str(snap.get("error", "")).length() > 0, "截断要有明确错误信息")

	var empty: Dictionary = Parser.parse(PackedByteArray())
	_ok(not bool(empty.get("ok", true)), "空 payload 必须判失败")


func _check_bad_node_kind_is_clamped() -> void:
	# 把第一个片区的 kind 改成 96（就是线上真实踩到的越界值），解析器不能崩
	var bytes := _build_bytes()
	var node_offset := _node_section_offset(bytes)
	bytes[node_offset] = 96
	var snap: Dictionary = Parser.parse(bytes)
	_ok(bool(snap.get("ok", false)), "坏 kind 不该让整份快照失败")
	_eq(int(snap["nodes"][0]["kind"]), 0, "坏 kind 应被降级成 0")


func _check_trailing_bytes_are_reported() -> void:
	var bytes := _build_bytes()
	bytes.append(0xAB)   # 多一个字节 = 解析提前结束
	var snap: Dictionary = Parser.parse(bytes)
	_ok(bool(snap.get("ok", false)), "多字节不算致命错误")
	_eq(int(snap.get("bytes_left", -1)), 1, "剩余字节数应被报出来")


## 用第一个片区的"kind + x + y"字节pattern定位，比手算每一段长度稳得多
## （手算最容易在塔/单位/建筑这些变长段上错一个字节）。
func _node_section_offset(bytes: PackedByteArray) -> int:
	var pattern := PackedByteArray([2, 0x05, 0xDC, 0x05, 0x14])   # kind=2, x=1500, y=1300
	for i in range(bytes.size() - pattern.size() + 1):
		var matched := true
		for j in pattern.size():
			if bytes[i + j] != pattern[j]:
				matched = false
				break
		if matched:
			return i
	_failed += 1
	printerr("  [FAIL] 没找到片区字节 pattern")
	return 0

# ---- 断言 -------------------------------------------------------------------

func _ok(condition: bool, what: String) -> void:
	if condition:
		_passed += 1
	else:
		_failed += 1
		printerr("  [FAIL] ", what)


func _eq(actual: int, expected: int, what: String) -> void:
	if actual == expected:
		_passed += 1
	else:
		_failed += 1
		printerr("  [FAIL] %s：实际 %d，期望 %d" % [what, actual, expected])
