# 客户端协议层 —— 与 src/net/protocol.h + src/net/decoder.cpp 逐字节等价。
#
# 三条硬规矩（见 docs/adr/0002-godot-client.md 的「客户端工程规矩」）：
#   1. 纯字节进、帧出，不碰 socket —— 所以它能脱网单测，不需要服务端在跑。
#   2. 大端读取只有 _be16 / _be32 两个入口。禁止用 PackedByteArray.decode_u16/u32/u64：
#      Godot 的 decode_* 默认小端，而本协议是大端 —— 用错不报错，只会静默错位
#      （典型症状：界面上兵力显示 65536）。
#   3. 唯一契约是 tests/golden_frames.txt，C++ 侧 tests/test_golden_protocol.cpp
#      读的是同一份文件。改帧格式 = 同时改三个地方。
class_name KFProtocol
extends RefCounted

const MAGIC := 0x4B46  # "KF"
const VERSION := 1
const HEADER := 13
const MAX_PAYLOAD := 64 * 1024

var _buf := PackedByteArray()
var _failed := false


func failed() -> bool:
	return _failed


func buffered() -> int:
	return _buf.size()


## 喂任意长度的字节（可能半包、可能一次来好几帧）。返回：
##   {"frames": Array[Dictionary]，每个 {cmd:int, seq:int, payload:PackedByteArray}],
##    "error": String}
## error 非空 = 协议违规，调用方应立即断开；此后本对象保持失败状态（和 C++ 的
## FrameDecoder 一样，不搞"跳过垃圾字节重新同步"）。
func feed(chunk: PackedByteArray) -> Dictionary:
	if _failed:
		return _fail([], "decoder already failed")
	_buf.append_array(chunk)

	var frames: Array[Dictionary] = []
	while _buf.size() >= HEADER:
		var magic := _be16(0)
		var version := _buf[2]
		var cmd := _be16(3)
		var seq := _be32(5)
		var payload_len := _be32(9)

		if magic != MAGIC:
			return _fail(frames, "bad magic 0x%04X" % magic)
		if version != VERSION:
			return _fail(frames, "unsupported version %d" % version)
		if payload_len > MAX_PAYLOAD:
			return _fail(frames, "payload_len %d exceeds %d" % [payload_len, MAX_PAYLOAD])
		if _buf.size() < HEADER + payload_len:
			break  # 半包：等下一次 feed

		var payload := _buf.slice(HEADER, HEADER + payload_len)
		_buf = _buf.slice(HEADER + payload_len)  # 整帧消费掉
		frames.append({"cmd": cmd, "seq": seq, "payload": payload})

	return {"frames": frames, "error": ""}


## 组一帧（客户端上行）。和 C++ encode_frame() 逐字节一致。
static func encode(cmd: int, seq: int, payload: PackedByteArray) -> PackedByteArray:
	var out := PackedByteArray()
	out.resize(HEADER + payload.size())
	_write_be16(out, 0, MAGIC)
	out[2] = VERSION
	_write_be16(out, 3, cmd)
	_write_be32(out, 5, seq)
	_write_be32(out, 9, payload.size())
	for i in payload.size():
		out[HEADER + i] = payload[i]
	return out


func _fail(frames: Array[Dictionary], message: String) -> Dictionary:
	_failed = true
	return {"frames": frames, "error": message}


# ---- 大端读写：本文件唯一允许碰位运算的地方 ------------------------------

func _be16(offset: int) -> int:
	return (_buf[offset] << 8) | _buf[offset + 1]


func _be32(offset: int) -> int:
	return ((_buf[offset] << 24) | (_buf[offset + 1] << 16)
			| (_buf[offset + 2] << 8) | _buf[offset + 3])


static func _write_be16(out: PackedByteArray, offset: int, value: int) -> void:
	out[offset] = (value >> 8) & 0xFF
	out[offset + 1] = value & 0xFF


static func _write_be32(out: PackedByteArray, offset: int, value: int) -> void:
	out[offset] = (value >> 24) & 0xFF
	out[offset + 1] = (value >> 16) & 0xFF
	out[offset + 2] = (value >> 8) & 0xFF
	out[offset + 3] = value & 0xFF
