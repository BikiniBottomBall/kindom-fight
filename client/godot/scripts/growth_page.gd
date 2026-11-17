# 局内成长树：三棵树（军政 / 经济 / 工程），每棵四层。
#
# 规则（和服务端 src/rts/cards.cpp 的 kTreeNodes 一一对应，改表要两边同步）：
# - 同一棵树里同层的两条分支互斥 → 每次点亮都是一次取舍；
# - 后期节点需要别的树做前置 → 三棵树后期联动；
# - 只有科技点，没有等级（节点是一次性解锁）。
extends Control

const Art = preload("res://scripts/art.gd")

signal upgrade_requested(node: int)
signal closed

const TREE_NAMES := ["军政", "经济", "工程"]
const TREE_COLORS := [Color("#ff9f6b"), Color("#f0c45a"), Color("#7fd8ff")]

# 节点静态描述：name / effect / tree / tier / prereq indices / conflict indices
const NODES := [
	# 军政
	{"name": "训练加速", "effect": "兵营训练 -10%", "tree": 0, "tier": 0, "all": [], "any": [], "conflict": []},
	{"name": "精兵路线", "effect": "小兵血 +10%｜互斥狂战", "tree": 0, "tier": 1, "all": [0], "any": [], "conflict": [2]},
	{"name": "狂战路线", "effect": "小兵攻 +12%｜互斥精兵", "tree": 0, "tier": 1, "all": [0], "any": [], "conflict": [1]},
	{"name": "战争机器", "effect": "训练再 -15%", "tree": 0, "tier": 2, "all": [0], "any": [1, 2], "conflict": []},
	# 经济
	{"name": "仓储", "effect": "物资产出 +10%", "tree": 1, "tier": 0, "all": [], "any": [], "conflict": []},
	{"name": "能源网", "effect": "能源 +15%｜互斥贸易", "tree": 1, "tier": 1, "all": [4], "any": [], "conflict": [6]},
	{"name": "贸易路线", "effect": "卡牌 -10%｜互斥能源", "tree": 1, "tier": 1, "all": [4], "any": [], "conflict": [5]},
	{"name": "工业枢纽", "effect": "建筑上限 +1", "tree": 1, "tier": 2, "all": [0, 4], "any": [5, 6], "conflict": []},
	# 工程
	{"name": "工程学", "effect": "建造时间 -15%", "tree": 2, "tier": 0, "all": [], "any": [], "conflict": []},
	{"name": "加固", "effect": "建筑血 +20%｜互斥快建", "tree": 2, "tier": 1, "all": [8], "any": [], "conflict": [10]},
	{"name": "快速施工", "effect": "工程兵 +25%｜互斥加固", "tree": 2, "tier": 1, "all": [8], "any": [], "conflict": [9]},
	{"name": "要塞", "effect": "防御塔血量 +15%", "tree": 2, "tier": 2, "all": [7], "any": [9, 10], "conflict": []},
]
const COLUMN_X := [90, 570, 1050]
const COLUMN_WIDTH := 460
const ROW_Y := 200
const ROW_HEIGHT := 118
const ROW_STEP := 152

var _buttons: Array = []
var _tip: Label
var _panel_box: StyleBox


func _init() -> void:
	name = "GrowthPage"
	position = Vector2.ZERO
	size = Vector2(1600, 900)
	mouse_filter = Control.MOUSE_FILTER_STOP

	var backdrop := ColorRect.new()
	backdrop.color = Color(0.03, 0.04, 0.07, 0.99)
	backdrop.position = Vector2.ZERO
	backdrop.size = Vector2(1600, 900)
	add_child(backdrop)
	_panel_box = _make_panel_box()

	var title := _label("成长树", 44, Color(1.0, 0.85, 0.45))
	title.position = Vector2(90, 56)
	title.size = Vector2(500, 60)
	add_child(title)

	_tip = _label("科技点由研究所产出。同层两条分支互斥，后期节点需要别的树做前置。",
		17, Color(0.62, 0.7, 0.85))
	_tip.position = Vector2(92, 116)
	_tip.size = Vector2(1200, 26)
	add_child(_tip)

	for tree in TREE_NAMES.size():
		var header := _label(TREE_NAMES[tree], 24, TREE_COLORS[tree])
		header.position = Vector2(float(COLUMN_X[tree]), 156.0)
		header.size = Vector2(COLUMN_WIDTH, 32)
		add_child(header)

	# 竖向连线（同一条树里 tier -> tier+1）
	# 同一棵树同一层如果有多张（分支），并排显示
	for i in NODES.size():
		var tree_index := int(NODES[i].tree)
		var tier_index := int(NODES[i].tier)
		var siblings: Array = []
		for j in NODES.size():
			if int(NODES[j].tree) == tree_index and int(NODES[j].tier) == tier_index:
				siblings.append(j)
		var order := siblings.find(i)
		var slot_width := float(COLUMN_WIDTH - 40) / float(siblings.size())
		var card := Button.new()
		card.position = Vector2(float(COLUMN_X[tree_index]) + slot_width * float(order),
			float(ROW_Y + tier_index * ROW_STEP))
		card.size = Vector2(slot_width - 8.0, ROW_HEIGHT)
		card.alignment = HORIZONTAL_ALIGNMENT_LEFT
		card.add_theme_font_size_override("font_size", 13 if siblings.size() > 1 else 15)
		_style_button(card)
		var node_index := i
		card.pressed.connect(func() -> void: upgrade_requested.emit(node_index))
		add_child(card)
		_buttons.append(card)

	var back := Button.new()
	back.text = "返回战场"
	back.position = Vector2(90, 800)
	back.size = Vector2(240, 56)
	back.add_theme_font_size_override("font_size", 22)
	_style_button(back)
	back.pressed.connect(func() -> void: closed.emit())
	add_child(back)


func _make_panel_box() -> StyleBox:
	var texture: Texture2D = Art.load_ui("panel_dark")
	if texture == null:
		var flat := StyleBoxFlat.new()
		flat.bg_color = Color(0.06, 0.08, 0.12, 0.85)
		return flat
	var box := StyleBoxTexture.new()
	box.texture = texture
	box.texture_margin_left = 8
	box.texture_margin_right = 8
	box.texture_margin_top = 8
	box.texture_margin_bottom = 8
	return box


func _draw() -> void:
	# 三棵树的底板：画在连线之前，否则会盖住连接线
	for i in 3:
		draw_style_box(_panel_box, Rect2(78 + i * 480, 146, 464, 470))
	# 树形连线：同一棵树里画竖向线；跨树前置画一条虚线提示
	for i in NODES.size():
		var tree := int(NODES[i].tree)
		var tier := int(NODES[i].tier)
		if tier >= 3:
			continue
		var x: float = float(COLUMN_X[tree]) + (COLUMN_WIDTH - 40) * 0.5
		var y0: float = ROW_Y + tier * ROW_STEP + ROW_HEIGHT
		var y1: float = ROW_Y + (tier + 1) * ROW_STEP
		draw_line(Vector2(x, y0), Vector2(x, y1), TREE_COLORS[tree].darkened(0.35), 2.0)
	# 跨树前置：工业枢纽(7) ← 训练加速(0)；要塞(11) ← 工业枢纽(7)
	_draw_link(0, 7)
	_draw_link(7, 11)


func _draw_link(from_node: int, to_node: int) -> void:
	var a := _node_center(from_node)
	var b := _node_center(to_node)
	var color := Color(0.6, 0.75, 1.0, 0.45)
	draw_dashed_line(a, b, color, 2.0, 8.0)


func _node_center(node: int) -> Vector2:
	if node >= 0 and node < _buttons.size():
		var card: Button = _buttons[node]
		return card.position + card.size * 0.5
	return Vector2.ZERO


static func _pixel_box(name: String) -> StyleBox:
	var texture: Texture2D = Art.load_ui(name)
	if texture == null:
		var flat := StyleBoxFlat.new()
		flat.bg_color = Color("#1b2436")
		flat.border_color = Color(0.35, 0.45, 0.7, 0.75)
		flat.set_border_width_all(1)
		return flat
	var box := StyleBoxTexture.new()
	box.texture = texture
	box.texture_margin_left = 4
	box.texture_margin_right = 4
	box.texture_margin_top = 4
	box.texture_margin_bottom = 4
	box.content_margin_left = 10
	box.content_margin_right = 10
	box.content_margin_top = 6
	box.content_margin_bottom = 6
	return box


static func _style_button(button: Button) -> void:
	button.add_theme_stylebox_override("normal", _pixel_box("button"))
	button.add_theme_stylebox_override("hover", _pixel_box("button_hover"))
	button.add_theme_stylebox_override("pressed", _pixel_box("button_pressed"))
	button.add_theme_stylebox_override("disabled", _pixel_box("button_disabled"))
	button.add_theme_color_override("font_color", Color(0.88, 0.93, 1.0))
	button.add_theme_color_override("font_disabled_color", Color(0.42, 0.46, 0.56))


func _label(text: String, font_size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	return label


# states: [{taken: bool, cost: int}, ...]（12 项，顺序和服务端一致）
func refresh(states: Array, tech: int) -> void:
	if _tip != null:
		_tip.text = "科技点：%d　·　同层两条分支互斥，后期节点需要别的树做前置" % tech
	for i in _buttons.size():
		var button: Button = _buttons[i]
		var info: Dictionary = NODES[i]
		if i >= states.size():
			button.text = str(info.name)
			continue
		var taken := bool(states[i].get("taken", false))
		var cost := int(states[i].get("cost", 0))
		var requirement_text := _requirement_text(i, states)
		var state_text := "已点亮"
		if not taken:
			state_text = requirement_text if requirement_text != "" else "科技 %d" % cost
		button.text = "%s\n%s\n%s" % [info.name, info.effect, state_text]
		# 前置没满足 / 互斥 / 科技不够 → 禁用；已点亮也禁用
		button.disabled = taken or requirement_text != "" or tech < cost


# 返回空字符串表示可以点；否则返回"为什么不能点"
func _requirement_text(node: int, states: Array) -> String:
	var info: Dictionary = NODES[node]
	for required in info.all:
		if not _taken(states, int(required)):
			return "需要：%s" % str(NODES[int(required)].name)
	var any_list: Array = info.any
	if not any_list.is_empty():
		var ok := false
		for candidate in any_list:
			if _taken(states, int(candidate)):
				ok = true
				break
		if not ok:
			return "需要：%s 任一路线" % str(NODES[int(any_list[0])].name)
	for blocked in info.conflict:
		if _taken(states, int(blocked)):
			return "与 %s 互斥" % str(NODES[int(blocked)].name)
	return ""


func _taken(states: Array, node: int) -> bool:
	if node < 0 or node >= states.size():
		return false
	return bool(states[node].get("taken", false))