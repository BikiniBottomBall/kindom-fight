# 客户端协议层的脱网测试：不需要编辑器 UI，也不需要服务端在跑。
#
#   godot --headless --path client/godot --script res://tests/test_decoder.gd
#
# 读的是 res://tests/golden_frames.txt，和 C++ 侧 tests/test_golden_protocol.cpp
# 完全同一份向量。任何一侧偷改帧格式，两边必有一侧变红。
extends SceneTree

const ProtocolScript = preload("res://scripts/kf_protocol.gd")
const GOLDEN := "res://tests/golden_frames.txt"

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("运行 GDScript 协议测试")
	_run()
	print("\n%d 通过 / %d 失败" % [_passed, _failed])
	quit(1 if _failed > 0 else 0)


func _run() -> void:
	var cases := _load_cases()
	if cases.is_empty():
		_failed += 1
		printerr("  [FAIL] 没读到任何 golden 用例（%s）" % GOLDEN)
		return

	_check_new_instance_is_clean()
	_check_decode_matches_vector(cases)
	_check_encode_round_trip(cases)
	_check_error_vectors(cases)
	_check_concatenated_stream(cases)
	_check_byte_by_byte(cases)
	_check_failure_is_sticky(cases)


# ---- 用例 ------------------------------------------------------------------

func _check_new_instance_is_clean() -> void:
	var d := ProtocolScript.new()
	_ok(not d.failed(), "新解码器不该是失败态")
	_ok(d.buffered() == 0, "新解码器缓冲应为空")


func _check_decode_matches_vector(cases: Array) -> void:
	for c in cases:
		if c.error:
			continue
		var d := ProtocolScript.new()
		var r: Dictionary = d.feed(c.bytes)
		_ok(r.error == "", "%s: 解码不该报错（%s）" % [c.name, r.error])
		var frames: Array = r.frames
		_ok(frames.size() == 1, "%s: 应解出 1 帧，实际 %d" % [c.name, frames.size()])
		if frames.size() != 1:
			continue
		var f: Dictionary = frames[0]
		_ok(f.cmd == c.cmd, "%s: cmd 期望 %d，实际 %d" % [c.name, c.cmd, f.cmd])
		_ok(f.seq == c.seq, "%s: seq 期望 %d，实际 %d" % [c.name, c.seq, f.seq])
		_ok(f.payload == c.payload, "%s: payload 不一致" % c.name)
		_ok(d.buffered() == 0, "%s: 整帧消费后缓冲应清空" % c.name)


func _check_encode_round_trip(cases: Array) -> void:
	for c in cases:
		if c.error:
			continue
		var encoded: PackedByteArray = ProtocolScript.encode(c.cmd, c.seq, c.payload)
		_ok(encoded == c.bytes, "%s: encode() 与向量不一致（客户端上行会写错帧）" % c.name)


func _check_error_vectors(cases: Array) -> void:
	for c in cases:
		if not c.error:
			continue
		var d := ProtocolScript.new()
		var r: Dictionary = d.feed(c.bytes)
		_ok(r.error != "", "%s: 应该判协议违规" % c.name)
		var frames: Array = r.frames
		_ok(frames.is_empty(), "%s: 违规帧不该产出任何帧" % c.name)
		_ok(d.failed(), "%s: 失败状态应被锁住" % c.name)


func _check_concatenated_stream(cases: Array) -> void:
	var stream := PackedByteArray()
	var expected := 0
	for c in cases:
		if c.error:
			continue
		stream.append_array(c.bytes)
		expected += 1

	var d := ProtocolScript.new()
	var r: Dictionary = d.feed(stream)
	_ok(r.error == "", "粘包：一次性喂 %d 帧不该报错（%s）" % [expected, r.error])
	var frames: Array = r.frames
	_ok(frames.size() == expected, "粘包：期望 %d 帧，实际 %d" % [expected, frames.size()])


func _check_byte_by_byte(cases: Array) -> void:
	var stream := PackedByteArray()
	var expected := 0
	for c in cases:
		if c.error:
			continue
		stream.append_array(c.bytes)
		expected += 1

	var d := ProtocolScript.new()
	var got := 0
	for i in stream.size():
		var single := PackedByteArray()
		single.append(stream[i])
		var r: Dictionary = d.feed(single)
		if r.error != "":
			_ok(false, "逐字节：第 %d 个字节处报错（%s）" % [i, r.error])
			return
		var frames: Array = r.frames
		got += frames.size()
	_ok(got == expected, "逐字节：期望 %d 帧，实际 %d（半包处理有问题）" % [expected, got])


func _check_failure_is_sticky(cases: Array) -> void:
	var good := PackedByteArray()
	for c in cases:
		if not c.error:
			good = c.bytes
			break

	var d := ProtocolScript.new()
	var garbage := PackedByteArray()
	garbage.resize(13)  # 全 0：magic 不对
	var bad: Dictionary = d.feed(garbage)
	_ok(bad.error != "", "坏 magic 应报错")
	var after: Dictionary = d.feed(good)
	_ok(after.error != "", "失败后不该被合法数据复活")


# ---- golden 文件 ------------------------------------------------------------------

func _load_cases() -> Array:
	var cases: Array = []
	var path := _golden_path()
	if not FileAccess.file_exists(path):
		printerr("  找不到 %s" % path)
		return cases

	var text := FileAccess.get_file_as_string(path)
	var line_no := 0
	for raw in text.split("\n"):
		line_no += 1
		var line: String = raw.strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue

		var parts := line.split("|")
		for i in parts.size():
			parts[i] = parts[i].strip_edges()

		if parts.size() < 2:
			_failed += 1
			printerr("  [FAIL] golden 第 %d 行格式不对" % line_no)
			continue

		var case_name: String = parts[0]
		var is_error := case_name.begins_with("!")
		if is_error:
			case_name = case_name.substr(1)

		var entry := {
			"name": case_name,
			"bytes": _hex_to_bytes(parts[1]),
			"cmd": 0,
			"seq": 0,
			"payload": PackedByteArray(),
			"error": is_error,
		}

		if not is_error:
			if parts.size() < 5:
				_failed += 1
				printerr("  [FAIL] %s: 成功用例需要 name|hex|cmd|seq|payload" % case_name)
				continue
			entry.cmd = int(parts[2])
			entry.seq = int(parts[3])
			entry.payload = PackedByteArray() if parts[4] == "-" else _hex_to_bytes(parts[4])

		cases.append(entry)

	return cases


func _golden_path() -> String:
	# 和 C++ 侧一样支持 KF_GOLDEN_FILE 覆盖，方便做"故意改坏向量"的变异测试。
	var from_env := OS.get_environment("KF_GOLDEN_FILE")
	return from_env if not from_env.is_empty() else GOLDEN


func _hex_to_bytes(text: String) -> PackedByteArray:
	const DIGITS := "0123456789abcdef"
	var digits := ""
	for c in text.to_lower():
		if DIGITS.contains(c):
			digits += c

	var out := PackedByteArray()
	var i := 0
	while i + 1 < digits.length():
		out.append(("0x" + digits.substr(i, 2)).hex_to_int())
		i += 2
	return out


# ---- 断言 ------------------------------------------------------------------

func _ok(condition: bool, message: String) -> void:
	if condition:
		_passed += 1
	else:
		_failed += 1
		printerr("  [FAIL] " + message)
