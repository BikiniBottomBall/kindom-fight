# 快照解析器 —— 和 C++ `wire::encode_snapshot` 逐字段对应。
#
# 为什么单独成文件：
#   1. main.gd 只管"拿到数据后怎么显示"，字段顺序改错不会静默错位；
#   2. 它能脱网单测 —— tests/test_snapshot.gd 用手写黄金字节逐字段断言。
#
# 规矩：读之前先检查剩余字节，不够就返回 ok=false（不要返回半截垃圾数据）。
extends RefCounted


class Cursor:
	var buf: StreamPeerBuffer
	var ok := true
	var error := ""

	func _init(payload: PackedByteArray) -> void:
		buf = StreamPeerBuffer.new()
		buf.big_endian = true
		buf.data_array = payload

	func left() -> int:
		return buf.get_size() - buf.get_position()

	func _need(count: int) -> bool:
		if not ok:
			return false
		if left() < count:
			ok = false
			error = "快照截断：需要 %d 字节，只剩 %d（位置 %d / 总长 %d）" % [
				count, left(), buf.get_position(), buf.get_size()]
			return false
		return true

	func u8() -> int:
		return buf.get_u8() if _need(1) else 0

	func u16() -> int:
		return buf.get_u16() if _need(2) else 0

	func i16() -> int:
		return buf.get_16() if _need(2) else 0

	func u32() -> int:
		return buf.get_u32() if _need(4) else 0


static func parse(payload: PackedByteArray) -> Dictionary:
	var c := Cursor.new(payload)
	var snap := {}

	snap["tick"] = c.u32()
	# 线上顺序：人力、人力上限、物资、能源、科技、领地
	var manpower := c.u16()
	var manpower_cap := c.u16()
	var materiel := c.u16()
	var energy := c.u16()
	var tech := c.u16()
	var territory := c.u16()
	snap["manpower_cap"] = manpower_cap
	snap["res"] = [manpower, materiel, energy, tech, territory]

	var hand := []
	for i in c.u8():
		hand.append(c.u16())
	snap["hand"] = hand

	var bases := []
	for i in 4:
		bases.append(c.u16())
	snap["bases"] = bases

	var towers := []
	for i in c.u8():
		towers.append({
			"owner": c.u8(), "id": c.u16(), "progress": c.u8(),
			"x": c.i16(), "y": c.i16(),
			"hp": c.u16(), "max_hp": c.u16(),
		})
	snap["towers"] = towers

	var units := []
	for i in c.u16():
		units.append({
			"id": c.u16(), "owner": c.u8(), "type": c.u8(),
			"x": c.i16(), "y": c.i16(),
			"hp": c.u16(), "max": c.u16(), "flags": c.u8(),
		})
	snap["units"] = units

	var buildings := []
	for i in c.u8():
		buildings.append({
			"owner": c.u8(), "type": c.u8(),
			"x": c.i16(), "y": c.i16(),
			"hp": c.u16(), "max": c.u16(), "progress": c.u8(),
			"train_unit": c.u8(), "rally_x": c.i16(), "rally_y": c.i16(),
		})
	snap["buildings"] = buildings

	var effects := []
	for i in c.u8():
		effects.append({
			"kind": c.u8(), "owner": c.u8(),
			"x": c.i16(), "y": c.i16(),
			"radius": c.u16(), "ttl": c.u8(),
		})
	snap["effects"] = effects

	snap["phase"] = c.u8()
	snap["remaining"] = c.u16()
	snap["alive"] = c.u8()
	snap["weather"] = c.u8()
	snap["day_phase"] = c.u8()
	snap["hq_state"] = c.u8()
	snap["event"] = c.u8()
	snap["event_left"] = c.u16()

	# 地图片区：位置 + 半径 + 归属 + 占点进度 + 城镇核心
	var nodes := []
	for i in c.u8():
		var kind := c.u8()
		var x := c.i16()
		var y := c.i16()
		var owner := c.u8() - 1
		var progress := c.u8()
		var capturer := c.u8() - 1
		var flags := c.u8()
		if kind > 3:
			push_warning("节点 kind 异常=%d（快照解析可能错位）" % kind)
			kind = 0
		nodes.append({
			"kind": kind, "x": x, "y": y,
			"owner": owner, "progress": progress, "capturer": capturer, "flags": flags,
			"radius": c.u16(), "core_hp": c.u16(), "core_max": c.u16(),
		})
	snap["nodes"] = nodes

	# 局内成长树：12 个节点（是否点亮 + 价格）
	var upgrades := []
	for i in c.u8():
		upgrades.append({"taken": c.u8() != 0, "cost": c.u16()})
	snap["upgrades"] = upgrades
	snap["building_cap"] = c.u8()
	snap["tower_cap"] = c.u8()

	snap["ok"] = c.ok
	snap["error"] = c.error
	snap["bytes_left"] = c.left()
	if c.ok and c.left() != 0:
		# 不是致命错误，但说明编码/解析顺序已经不一致，必须立刻看到
		push_warning("快照还有 %d 字节没读（编码/解析顺序不一致）" % c.left())
	return snap
