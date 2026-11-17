# 建筑/塔详情面板：点建筑能看到血量、建造进度；兵营还能在这里换训练兵种、指定攻击区域。
# 单独成文件，main.gd 只负责在点击时把目标传进来。
extends Control

signal unit_chosen(building_index: int, unit_type: int)
signal rally_requested(building_index: int)
signal closed

const PANEL_SIZE := Vector2(620, 400)
const TYPE_NAMES := ["物资站", "能源站", "兵营", "研究所", "防御塔", "工程站"]

var _title: Label
var _info: Label
var _unit_box: Control
var _unit_buttons: Array = []
var _hint: Label
var _rally_button: Button
var _index := -1
var _kind := 0


func _init() -> void:
	name = "StructurePanel"
	size = PANEL_SIZE
	position = Vector2(950, 250)
	mouse_filter = Control.MOUSE_FILTER_STOP
	visible = false

	var background := ColorRect.new()
	background.color = Color(0.04, 0.06, 0.10, 0.96)
	background.size = PANEL_SIZE
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)

	_title = _label("建筑详情", 26, Color(1.0, 0.85, 0.45))
	_title.position = Vector2(22, 16)
	_title.size = Vector2(560, 34)
	add_child(_title)

	_info = _label("", 18, Color(0.82, 0.88, 1.0))
	_info.position = Vector2(22, 56)
	_info.size = Vector2(576, 54)
	add_child(_info)

	_unit_box = Control.new()
	_unit_box.position = Vector2(22, 118)
	_unit_box.size = Vector2(576, 200)
	_unit_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_unit_box)

	var box := StyleBoxFlat.new()
	box.bg_color = Color(0.13, 0.17, 0.26)
	box.border_color = Color(0.4, 0.5, 0.7)
	box.set_border_width_all(2)
	box.set_corner_radius_all(6)

	for i in 9:
		var button := Button.new()
		button.position = Vector2((i % 3) * 190, (i / 3) * 62)
		button.size = Vector2(182, 52)
		button.add_theme_stylebox_override("normal", box)
		button.add_theme_stylebox_override("hover", box)
		button.add_theme_stylebox_override("pressed", box)
		button.add_theme_stylebox_override("disabled", box)
		button.add_theme_font_size_override("font_size", 15)
		button.visible = false
		var type_index := i
		button.pressed.connect(func() -> void: unit_chosen.emit(_index, type_index))
		_unit_box.add_child(button)
		_unit_buttons.append(button)

	_hint = _label("", 16, Color(0.62, 0.7, 0.85))
	_hint.position = Vector2(22, 330)
	_hint.size = Vector2(576, 26)
	_hint.visible = false
	add_child(_hint)

	_rally_button = Button.new()
	_rally_button.text = "指定攻击区域"
	_rally_button.position = Vector2(22, 340)
	_rally_button.size = Vector2(240, 46)
	_rally_button.visible = false
	_rally_button.pressed.connect(func() -> void: rally_requested.emit(_index))
	add_child(_rally_button)

	var close_button := Button.new()
	close_button.text = "关闭"
	close_button.position = Vector2(490, 340)
	close_button.size = Vector2(108, 46)
	close_button.pressed.connect(func() -> void: closed.emit())
	add_child(close_button)


func _label(text: String, font_size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	return label


## building: 快照里的建筑字典；index: 服务端用的"自家建筑下标"；unit_table: 兵种表
func show_building(building: Dictionary, index: int, unit_table: Dictionary) -> void:
	_index = index
	_kind = 0
	visible = true
	var type := int(building.get("type", 0))
	_title.text = TYPE_NAMES[type] if type < TYPE_NAMES.size() else "建筑"
	var progress := int(building.get("progress", 100))
	var info := "血量 %d / %d" % [int(building.get("hp", 0)), int(building.get("max", 0))]
	if progress < 100:
		info += "　建造中 %d%%" % progress
	var train_unit := int(building.get("train_unit", 255))
	if type == 2 and train_unit >= 0 and train_unit < 255:
		info += "　当前训练：%s" % _unit_name(train_unit, unit_table)
	_info.text = info + "\n" + _rally_text(building)

	var is_barracks := type == 2
	_rally_button.visible = is_barracks
	_hint.visible = is_barracks
	_hint.text = "点下面按钮切换这个兵营训练什么（不同兵种训练时间不同）"
	_fill_units(is_barracks, train_unit, unit_table)


## tower: 快照里的塔字典
func show_tower(tower: Dictionary, unit_table: Dictionary, tower_range: float) -> void:
	_index = -1
	_kind = 1
	visible = true
	_title.text = "防御塔"
	var progress := int(tower.get("progress", 100))
	var info := "血量 %d / %d" % [int(tower.get("hp", 0)), int(tower.get("max_hp", 0))]
	info += "　射程 %d" % int(tower_range)
	if progress < 100:
		info += "　建造中 %d%%" % progress
	_info.text = info + "\n塔是固定槽位建筑：只能查看，不占卡组"
	_fill_units(false, -1, unit_table)
	_rally_button.visible = false
	_hint.visible = false


func _fill_units(is_barracks: bool, current: int, unit_table: Dictionary) -> void:
	for i in _unit_buttons.size():
		_unit_buttons[i].visible = false
	if not is_barracks:
		return
	var order := [0, 1, 2, 3, 4, 8, 9, 10, 11]
	for i in order.size():
		var type: int = order[i]
		var button: Button = _unit_buttons[i]
		var info: Dictionary = unit_table.get(type, {})
		var seconds := int(info.get("train", 0)) / 20
		button.text = "%s　%ds" % [_unit_name(type, unit_table), seconds]
		button.visible = true
		button.disabled = (type == current)
		button.modulate = Color(1, 1, 1) if type != current else Color(0.6, 0.8, 0.6)


func _unit_name(type: int, unit_table: Dictionary) -> String:
	var info: Dictionary = unit_table.get(type, {})
	if info.is_empty():
		return ["民兵", "长枪兵", "盾卫", "骑手", "弓手", "攻城车", "巨兽", "工程兵",
			"冲车", "投石车", "弩炮", "加农炮"][type] if type < 12 else "?"
	var raw := str(info.get("name", ""))
	match raw:
		"militia": return "民兵"
		"spear": return "长枪兵"
		"shield": return "盾卫"
		"rider": return "骑手"
		"archer": return "弓手"
		"siege": return "攻城车"
		"behemoth": return "巨兽"
		"engineer": return "工程兵"
		"ram": return "冲车"
		"catapult": return "投石车"
		"ballista": return "弩炮"
		"cannon": return "加农炮"
	return raw


func _rally_text(building: Dictionary) -> String:
	var rx := int(building.get("rally_x", -1))
	var ry := int(building.get("rally_y", -1))
	if rx < 0 or ry < 0:
		return "攻击区域：未指定（出兵后会自己找目标）"
	return "攻击区域：(%d, %d)" % [rx, ry]


func hide_panel() -> void:
	visible = false
