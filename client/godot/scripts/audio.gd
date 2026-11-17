# 音效管理器。
#
# 设计原则和美术一样：**文件可选**。assets/sfx/ 下没有对应的 wav 就静音，不会报错。
# 想换音效直接替换同名文件即可（click / place / hit / explosion / build_done / upgrade / victory / defeat）。
extends Node

const SFX_DIR := "res://assets/sfx/"
const SFX_NAMES := ["click", "place", "build_done", "hit", "explosion", "upgrade", "victory", "defeat"]
const POOL_SIZE := 8

var enabled := true

var _players: Array[AudioStreamPlayer] = []
var _streams := {}
var _cursor := 0


func _ready() -> void:
	for i in POOL_SIZE:
		var player := AudioStreamPlayer.new()
		add_child(player)
		_players.append(player)
	for sfx_name in SFX_NAMES:
		var path: String = SFX_DIR + str(sfx_name) + ".wav"
		if ResourceLoader.exists(path):
			_streams[sfx_name] = load(path)
	if _streams.is_empty():
		print("[audio] 没有找到音效文件，静音运行（assets/sfx/）")
	else:
		print("[audio] 已加载 %d 个音效" % _streams.size())


func play(name: String, volume_db := -6.0) -> void:
	if not enabled or not _streams.has(name) or _players.is_empty():
		return
	var player := _players[_cursor]
	_cursor = (_cursor + 1) % _players.size()
	player.stream = _streams[name]
	player.volume_db = volume_db
	player.play()


func toggle() -> bool:
	enabled = not enabled
	return enabled
