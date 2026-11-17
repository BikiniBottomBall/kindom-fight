# 一张竖版手牌。
#
# 卡面图（assets/cards/face_<id>.png）负责边框/类型条/插画，文字（名字、费用）由这里画，
# 这样换语言或调数值都不用重出美术。没有卡面图时回退到纯色卡牌，缺图不会崩。
extends Control

signal pressed(slot: int)
signal right_clicked(slot: int)

const Art = preload("res://scripts/art.gd")

const CARD_SIZE := Vector2(132, 184)
const KIND_COLORS := [Color("#f0b45a"), Color("#63a8ff"), Color("#8be08b")]
const COST_COLORS := [Color("#7fd8ff"), Color("#f0b45a"), Color("#8be0c0"), Color("#c792ea")]
const COST_NAMES := ["人", "物", "能", "科"]

var slot := 0
var card: Dictionary = {}
var selected := false
var hovered := false
var lift_amount := 12.0   # 选中时向上抬多少（构筑界面里设成 0）
var base_position := Vector2.ZERO  # 布局给的原始位置；选中抬手不能把它冲掉
# 动画全部交给 main.gd 的"飞行残影"系统：CardView 只负责画一张静止的牌。

var _face: Texture2D = null
var _shadow := StyleBoxFlat.new()
var copies := 0   # 构筑界面里"这张带了几张"（手牌不设置，所以不显示角标）


func _init() -> void:
	size = CARD_SIZE
	mouse_filter = Control.MOUSE_FILTER_STOP
	_shadow.bg_color = Color(0, 0, 0, 0.35)
	_shadow.set_corner_radius_all(10)


func _ready() -> void:
	mouse_entered.connect(func() -> void:
		hovered = true
		queue_redraw())
	mouse_exited.connect(func() -> void:
		hovered = false
		queue_redraw())


func set_card(value: Dictionary) -> void:
	card = value
	_face = null
	if card.has("id"):
		_face = Art.load_card_face(int(card["id"]))
	queue_redraw()


func set_selected(value: bool) -> void:
	selected = value
	# 只改 y 会把手牌/构筑界面的布局位置冲掉（旧版就是这样，点一下牌就跳到第一行）。
	# 现在统一按 base_position 重算：选中 = 原位置向上抬 lift_amount。
	if lift_amount > 0.0:
		position = base_position + (Vector2(0, -lift_amount) if selected else Vector2.ZERO)
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	var mouse := event as InputEventMouseButton
	if mouse == null or not mouse.pressed:
		return
	if mouse.button_index == MOUSE_BUTTON_LEFT:
		pressed.emit(slot)
	elif mouse.button_index == MOUSE_BUTTON_RIGHT:
		right_clicked.emit(slot)
	accept_event()


func set_copies(value: int) -> void:
	copies = value
	queue_redraw()


func _draw() -> void:
	var rect := Rect2(Vector2.ZERO, size)
	var kind := int(card.get("kind", 2))
	var tint: Color = KIND_COLORS[kind] if kind < KIND_COLORS.size() else KIND_COLORS[2]

	# 投影，让牌"浮"在战场上
	draw_style_box(_shadow, Rect2(rect.position + Vector2(3, 5), rect.size))

	if _face != null:
		draw_texture_rect(_face, rect, false)
		# 像素硬边：两像素外框 + 切角，和 UI 组件统一
		draw_rect(rect, Color(0.04, 0.06, 0.1), false, 2.0)
		draw_rect(Rect2(rect.position + Vector2(2, 2), rect.size - Vector2(4, 4)),
			Color(0, 0, 0, 0.35), false, 1.0)
	else:
		var body := StyleBoxFlat.new()
		body.bg_color = Color("#1b2334") if not selected else Color("#243252")
		body.border_color = tint
		body.set_border_width_all(2)
		body.set_corner_radius_all(10)
		draw_style_box(body, rect)

	if card.is_empty():
		return

	# 选中：外圈发光
	if selected:
		var glow := StyleBoxFlat.new()
		glow.bg_color = Color(0, 0, 0, 0)
		glow.border_color = tint.lightened(0.4)
		glow.set_border_width_all(3)
		glow.set_corner_radius_all(11)
		draw_style_box(glow, rect)
	elif hovered:
		var hover := StyleBoxFlat.new()
		hover.bg_color = Color(0, 0, 0, 0)
		hover.border_color = Color(0.7, 0.8, 1.0, 0.7)
		hover.set_border_width_all(2)
		hover.set_corner_radius_all(11)
		draw_style_box(hover, rect)

	var font := ThemeDB.fallback_font
	# 名字：底部文字区第一行，居中
	var name_text := str(card.get("name", "?"))
	var name_width := font.get_string_size(name_text, HORIZONTAL_ALIGNMENT_LEFT, -1, 16).x
	draw_string(font, Vector2((size.x - name_width) * 0.5, size.y - 44), name_text,
		HORIZONTAL_ALIGNMENT_LEFT, -1, 16, Color(0.95, 0.97, 1.0))

	# 构筑界面：这张带了几张（x1 白、x2 金，选了就看得见）
	if copies > 0:
		var full := copies >= 2
		var badge := StyleBoxFlat.new()
		badge.bg_color = Color(0.10, 0.14, 0.20, 0.95)
		badge.border_color = Color(1.0, 0.8, 0.35) if full else Color(0.55, 0.7, 0.95, 0.9)
		badge.set_border_width_all(2 if full else 1)
		badge.set_corner_radius_all(9)
		draw_style_box(badge, Rect2(size.x - 42, 6, 36, 22))
		draw_string(ThemeDB.fallback_font, Vector2(size.x - 36, 22), "x%d" % copies,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 14,
			Color(1.0, 0.85, 0.35) if full else Color(0.9, 0.96, 1.0))

	# 费用：底部一行，色点 + 数字
	var costs: Array = card.get("costs", [])
	var parts := []
	for i in costs.size():
		if int(costs[i]) <= 0:
			continue
		parts.append({"index": i, "amount": int(costs[i])})
	if parts.is_empty():
		return
	var total_width := 0.0
	var widths := []
	for part in parts:
		var text := "%d%s" % [int(part.amount), COST_NAMES[int(part.index)]]
		var w := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x + 12.0
		widths.append(w)
		total_width += w
	var x := (size.x - total_width) * 0.5
	var y := size.y - 18
	for i in parts.size():
		var part: Dictionary = parts[i]
		draw_circle(Vector2(x + 4, y - 5), 4.0, COST_COLORS[int(part.index)])
		var text := "%d%s" % [int(part.amount), COST_NAMES[int(part.index)]]
		draw_string(font, Vector2(x + 12, y), text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13,
			Color(0.86, 0.92, 1.0))
		x += float(widths[i])
