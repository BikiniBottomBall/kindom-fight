# 与 C++ 服务端的连接（传输层）。
#
# 只做三件事：连上去、把字节喂给 KFProtocol、把完整的帧用信号发出去。
# 它不理解任何游戏概念 —— 换一套协议只改 main.gd，不改这个文件。
extends Node

signal connected
signal disconnected
signal message_received(cmd: int, payload: PackedByteArray)

const Protocol = preload("res://scripts/kf_protocol.gd")

var peer: StreamPeerTCP = null
var decoder = Protocol.new()
var _is_connected := false
var _seq := 0


func connect_to(host: String, port: int) -> void:
	peer = StreamPeerTCP.new()
	decoder = Protocol.new()
	_is_connected = false
	var err := peer.connect_to_host(host, port)
	if err != OK:
		printerr("connect_to_host 失败: ", err)
		peer = null
		disconnected.emit()


func send(cmd: int, payload: PackedByteArray) -> void:
	if peer == null:
		return
	_seq += 1
	peer.put_data(Protocol.encode(cmd, _seq, payload))


func _process(_delta: float) -> void:
	if peer == null:
		return

	peer.poll()
	var status := peer.get_status()

	if status == StreamPeerTCP.STATUS_CONNECTING:
		return

	if status == StreamPeerTCP.STATUS_ERROR or status == StreamPeerTCP.STATUS_NONE:
		_disconnect()
		return

	if status == StreamPeerTCP.STATUS_CONNECTED:
		if not _is_connected:
			_is_connected = true
			connected.emit()

		while true:
			var available := peer.get_available_bytes()
			if available <= 0:
				break
			var result := peer.get_partial_data(available)
			if result[0] != OK:
				_disconnect()
				return
			var chunk: PackedByteArray = result[1]
			if chunk.is_empty():
				break

			var decoded: Dictionary = decoder.feed(chunk)
			if decoded.error != "":
				printerr("协议错误，断开：", decoded.error)
				_disconnect()
				return
			for frame in decoded.frames:
				message_received.emit(frame.cmd, frame.payload)


func is_socket_open() -> bool:
	return _is_connected


func disconnect_peer() -> void:
	_disconnect()


func _disconnect() -> void:
	if peer != null:
		peer.disconnect_from_host()
		peer = null
	_is_connected = false
	disconnected.emit()
