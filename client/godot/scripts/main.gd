# 客户端主控：连接 → 构筑卡组 → 对局 → 结算。
#
# 分工（ADR 护栏二）：
#   · 这个文件只做 UI、发意图、放行服务端给的数据
#   · 谁能出牌、打多少伤害、谁赢 —— 全在 C++ 服务端
#   · 它甚至不主动"算"手牌费用是否够：够了就高亮，点下去由服务端裁决
extends Control

const NetScript = preload("res://scripts/kf_net.gd")
const BoardScript = preload("res://scripts/board.gd")

const SERVER_HOST := "127.0.0.1"
const SERVER_PORT := 9700

# 消息号，必须和 src/rts/wire.h 一致
const C2S_DECK := 103
const C2S_PLAY := 105
const C2S_REMATCH := 109
const C2S_HQ := 112
const C2S_DEMOLISH := 114
const S2C_DEMOLISH_ACK := 115
const C2S_UPGRADE := 116
const S2C_UPGRADE_ACK := 117
const C2S_REPAIR := 118
const S2C_REPAIR_ACK := 119
const C2S_DISCARD := 120
const S2C_DISCARD_ACK := 121
const S2C_TERRITORY := 122
const C2S_BUILD_TOWER := 123
const S2C_BUILD_TOWER_ACK := 124
const C2S_SET_UNIT := 125
const S2C_SET_UNIT_ACK := 126
const C2S_SET_RALLY := 127
const S2C_SET_RALLY_ACK := 128
const S2C_UNIT_TABLE := 129
const UPGRADE_NAMES := ["小兵血量", "小兵攻击", "训练速度", "卡牌折扣"]
const UPGRADE_EFFECTS := ["每级 +12% 生命", "每级 +10% 攻击", "每级 +15% 训练速度", "每级 -8% 卡牌费用"]
const S2C_WELCOME := 101
const S2C_CARD_POOL := 102
const S2C_MATCH_START := 104
const S2C_SNAPSHOT := 106
const S2C_MATCH_OVER := 107
const S2C_ERROR := 108
const S2C_PLAY_ACK := 110
const S2C_MAP := 111
const S2C_HQ_ACK := 113

const CARD_KIND_NAMES := ["单位", "法术", "建筑"]

# 和 board.gd 保持一致：4 人 4 色
const PLAYER_COLORS := [
	Color("#4a9eff"), Color("#ff6b6b"), Color("#4ade80"), Color("#fbbf24")
]
const PLAYER_NAMES := ["我", "P1", "P2", "P3"]
# 四个联盟：和 src/rts/types.h 的 Alliance 顺序一致
const ALLIANCE_NAMES := ["先锋军团", "联合商会", "灵能教团", "蜂群"]
const ALLIANCE_SHORT := ["先", "商", "灵", "蜂"]
const ALLIANCE_PASSIVE := ["建筑与塔 +25% 血量", "物资产出 +25%", "能源 +40% 科技 +25%", "人力上限 +50"]

# 五项资源的显示：人力 / 物资 / 能源 / 科技 / 领地
const RES_NAMES := ["人", "物", "能", "科", "领"]
const RES_COLORS := [
	Color("#8ecbff"), Color("#f0b45a"), Color("#5eead4"), Color("#c792ea"), Color("#ff9f6e")
]

# 推荐卡组不再写死 id：四个阵营池子不同，_on_apply_starter 会从当前阵营的 7 张里
# 挑 3 兵 + 2 建筑 + 1 法术。

var net: Node
var board: Node2D
var ui_font: Font

var cards := {}  # id -> {"name": String, "costs": Array, "kind": int, "alliance": int}
var my_player := 0
var deck_required := 8
var selected_cards: Array = []
var card_buttons := {}          # id -> Button
var hand_buttons: Array = []    # 4 个 CardView
var hud_nodes: Array = []       # 工具栏相关节点（统一显示/隐藏）
var base_label: Label
var building_label: Label
var hand_expanded := false
var territory_button: Button
var tower_button: Button
var tower_build_mode := false
var unit_table := {}
var structure_panel: Control
var rally_mode := false
var rally_index := -1
var hand_seen: Array = []
var audio: Node
var _ready_buildings := {}
var preferred_unit := -1
var unit_buttons: Array = []
const UNIT_CHOICES := [0, 1, 2, 3, 4, 8, 9, 10, 11]
const UNIT_CHOICE_NAMES := ["民兵", "长枪兵", "盾卫", "骑手", "弓手", "冲车", "投石车", "弩炮", "加农炮"]
var demolish_mode := false
var repair_mode := false
var demolish_button: Button
var repair_button: Button
var growth_panel: Control
var growth_buttons: Array = []
var growth_button: Button
var growth_subtitle: Label
var deck_pile: Control
const CardViewScript = preload("res://scripts/card_view.gd")
const Art = preload("res://scripts/art.gd")
const SnapshotParser = preload("res://scripts/snapshot_parser.gd")
const StructurePanelScript = preload("res://scripts/structure_panel.gd")

# ---- 底部工具栏 / 手牌布局常量 ----
const TOOLBAR_TOP := 780.0
const CARD_WIDTH := 132.0
const CARD_HEIGHT := 184.0
const HAND_STEP := 136.0
const HAND_PEEK := 72.0          # 鼠标不在附近时只露出的高度
const HAND_LEFT := 530.0
const HAND_SPAN := 3.0 * 136.0 + 132.0
const GrowthPageScript = preload("res://scripts/growth_page.gd")
const ArtScript = preload("res://scripts/art.gd")
const AudioScript = preload("res://scripts/audio.gd")
var hand_signature := ""
var selected_slot := -1
var snapshot: Dictionary = {}
var match_active := false
var last_acked_slot := -1
var last_ack_result := -1
var auto_deck := false
var server_host := SERVER_HOST
var server_port := SERVER_PORT
var got_first_snapshot := false
var shot_path := ""
var shot_delay := 6.0
var bot_mode := false
var bot_timer := 0.0
var selftest_mode := false
var uitest_mode := false
var build_stamp := ""
# 开发用：--show-growth 开局直接打开成长树页（截图/验收）
var show_growth_on_start := false
# 开发用：--ghost-demo 开局放一次慢速"出牌残影"，方便截图检查动画轨迹
var ghost_demo_pending := false
# 开发用：--place-demo 自动选中一张建筑牌并把鼠标悬停在基地旁，方便截图看"可放置格子"
var place_demo_pending := false
var spell_demo_pending := false
var demo_hover_pos := Vector2(-1, -1)
# 开发用：--show-deck 拿到卡池后直接进构筑界面（截图/验收）
var show_deck_on_start := false

# UI 节点
var status_label: Label
var timer_label: Label
var hint_label: Label
var toast_label: Label
var deck_panel: Control
var deck_subtitle: Label
var deck_grid: Control
var deck_confirm: Button
var starter_button: Button
var deck_count_label: Label
var hand_panel: Control
var result_panel: Control
var result_label: Label
var result_detail: Label
var result_bases: Label
var rematch_button: Button
var change_deck_button: Button
var resource_labels: Array = []
var base_max_hp := []          # 每家的基地满血（先锋军团更高）
var alliances := [0, 0, 0, 0]  # 本局四家的联盟
var my_alliance := 0
var deck_faction_title: Label
var deck_faction_label: Label
var deck_faction_passive: Label
var conn_overlay: Control
var conn_title: Label
var conn_detail: Label
var reconnect_delay := 0.0
var hq_button: Button
var hq_targeting := false
var weather_label: Label
var camera_hint_label: Label
var event_label: Label
var zoom_buttons: Array = []
var menu_panel: Control
var pause_panel: Control
var hover_label: Label
var menu_status: Label
var menu_card_preview: Label
var menu_faction_buttons: Array = []


func _ready() -> void:
	# godot --path client/godot -- --autodeck   自动选前 6 张直接开局（录 demo / 冒烟用）
	auto_deck = OS.get_cmdline_user_args().has("--autodeck")
	bot_mode = OS.get_cmdline_user_args().has("--bot")
	selftest_mode = OS.get_cmdline_user_args().has("--selftest")
	uitest_mode = OS.get_cmdline_user_args().has("--uitest")
	show_deck_on_start = OS.get_cmdline_user_args().has("--show-deck")
	show_growth_on_start = OS.get_cmdline_user_args().has("--show-growth")
	ghost_demo_pending = OS.get_cmdline_user_args().has("--ghost-demo")
	place_demo_pending = OS.get_cmdline_user_args().has("--place-demo")
	spell_demo_pending = OS.get_cmdline_user_args().has("--spell-demo")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--server="):
			server_host = arg.split("=", true, 1)[1]
		elif arg.begins_with("--port="):
			server_port = int(arg.split("=", true, 1)[1])
		elif arg.begins_with("--shot="):
			shot_path = arg.split("=", true, 1)[1]
		elif arg.begins_with("--shot-delay="):
			shot_delay = float(arg.split("=", true, 1)[1])
	audio = AudioScript.new()
	audio.name = "Audio"
	add_child(audio)
	_build_theme()
	# 场景里放了一个编辑器占位图（不然在 Godot 编辑器里 2D 视图是空的，因为画面全靠脚本运行时画）
	var placeholder := get_node_or_null("EditorPlaceholder")
	if placeholder != null:
		placeholder.queue_free()
	_build_board()
	_build_hud()
	_build_deck_panel()
	_build_result_panel()
	_build_connection_overlay()
	_build_growth_panel()
	_build_menu()
	_build_pause_panel()
	_build_hover_label()
	_build_structure_panel()
	# 根节点铺满全屏，如果保持默认的 MOUSE_FILTER_STOP，它会吃掉所有鼠标事件，
	# 战场永远收不到点击（合成输入测试抓到过这个坑）。
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_set_hud_visible(false)

	menu_panel.visible = not auto_deck
	conn_overlay.visible = false
	# 一开始就连：开始页要靠服务端下发的卡池才能预览各阵营的 7 张牌
	_connect_server()

	# 开发用：--show-result 直接展示结算页（截图/改版式用，不用真打一局）
	if OS.get_cmdline_user_args().has("--show-result"):
		result_label.text = "胜利"
		result_detail.text = "基地被摧毁"
		result_bases.text = "我先 6500    P1·商 0    P2·灵 0    P3·蜂 0"
		result_panel.visible = true
		menu_panel.visible = false
		_set_hud_visible(false)

	if uitest_mode:
		_run_uitest()


# ---- 手动流程的自动验收（--uitest）--------------------------------------
# 走真实路径：开始页 → 构筑界面选 6 张 → 开始对局 → 点手牌 → 点战场。
# 这是"能不能玩"的底线检查，别用 --autodeck 糊弄过去。
func _wait_until(condition: Callable, timeout: float) -> bool:
	var elapsed := 0.0
	while elapsed < timeout:
		if condition.call():
			return true
		await get_tree().create_timer(0.1).timeout
		elapsed += 0.1
	return false


func _run_uitest() -> void:
	print("[uitest] 等待卡池…")
	var ok: bool = await _wait_until(func(): return not cards.is_empty(), 15.0)
	print("[uitest] 卡池=%s 张数=%d 开始页可见=%s" % [str(ok), cards.size(), str(menu_panel.visible)])
	if not ok:
		get_tree().quit(1)
		return

	_on_menu_start()
	await get_tree().create_timer(0.3).timeout
	print("[uitest] 构筑界面可见=%s 卡牌按钮=%d" % [str(deck_panel.visible), card_buttons.size()])

	var ids: Array = card_buttons.keys()
	# 回归：同一张牌点两次 = 带 2 张，而且牌不能因为选中而跳位置
	var probe_id: int = int(ids[0])
	var probe_view = card_buttons[probe_id]
	var probe_pos: Vector2 = probe_view.position
	_on_deck_card_pressed(probe_id)
	_on_deck_card_pressed(probe_id)
	print("[uitest] 同卡两次：已选=%d 位置变化=%s" % [
		selected_cards.size(), str(probe_view.position != probe_pos)])
	_on_deck_card_pressed(probe_id)  # 第三次应该被拦下
	print("[uitest] 第三次点击后已选=%d（应仍为 2）" % selected_cards.size())
	selected_cards.clear()
	_refresh_deck_selection()
	for i in mini(deck_required, ids.size()):
		_on_deck_card_pressed(ids[i])
	print("[uitest] 已选=%d 确认禁用=%s" % [selected_cards.size(), str(deck_confirm.disabled)])

	_on_confirm_deck()
	ok = await _wait_until(func(): return match_active, 15.0)
	print("[uitest] 开局=%s" % str(ok))
	if not ok:
		get_tree().quit(1)
		return

	await get_tree().create_timer(1.5).timeout
	_on_hand_slot_pressed(0)
	var base: Vector2 = board.bases[0] if board.bases.size() > 0 else Vector2(400, 400)
	var screen: Vector2 = board.world_to_screen(base + Vector2(120, 120))
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = true
	event.position = screen
	Input.parse_input_event(event)
	await get_tree().create_timer(1.5).timeout
	print("[uitest] 出牌回执=%d（0 = 服务端接受）" % last_ack_result)
	var accepted := last_ack_result

	# 再连续出 6 张牌，确认整局能持续跑（不是只开个局）
	var played := 0
	for i in 6:
		await get_tree().create_timer(2.0).timeout
		if not match_active:
			break
		var slot := -1
		for candidate in hand_buttons.size():
			# CardView 是自绘控件，没有 disabled 属性；用可见性判断就行
			if hand_buttons[candidate].visible:
				slot = candidate
				break
		if slot < 0:
			continue
		_on_hand_slot_pressed(slot)
		var base2: Vector2 = board.bases[0] if board.bases.size() > 0 else Vector2(400, 400)
		var spot := base2 + Vector2(100 + (i % 3) * 60, 100 + (i % 2) * 60)
		var screen2: Vector2 = board.world_to_screen(spot)
		var ev2 := InputEventMouseButton.new()
		ev2.button_index = MOUSE_BUTTON_LEFT
		ev2.pressed = true
		ev2.position = screen2
		Input.parse_input_event(ev2)
		played += 1

	await get_tree().create_timer(1.0).timeout
	print("[uitest] 连续出牌=%d 局内=%s 剩余=%ss 我的基地=%d 部队=%d" % [
		played, str(match_active), str(snapshot.get("remaining", 0)),
		int(snapshot.get("bases", [0])[0]),
		board.snapshot.get("units", []).size()])
	get_tree().quit(0 if accepted == 0 and match_active else 1)


# ---------------------------------------------------------------- 搭建

func _build_theme() -> void:
	var custom := Theme.new()
	var system_font := SystemFont.new()
	# Windows 上一定有雅黑；没有的话 Godot 会往后找，实在没有就退回默认字体
	system_font.font_names = PackedStringArray([
		"Microsoft YaHei UI", "Microsoft YaHei", "SimHei", "Noto Sans CJK SC",
		"Source Han Sans SC", "WenQuanYi Micro Hei",
	])
	custom.default_font = system_font
	custom.default_font_size = 20
	ui_font = system_font
	theme = custom


func _build_board() -> void:
	board = BoardScript.new()
	board.name = "Board"
	add_child(board)
	board.font = ui_font
	board.play_requested.connect(_on_play_requested)


func _build_hud() -> void:
	# ---- 底部工具栏：资源 / 基地 / 建筑 / 计时 / 功能按钮 都在这里 ----
	var bar := _pixel_panel(self, "Toolbar", Vector2(0, TOOLBAR_TOP),
		Vector2(1600, 900 - TOOLBAR_TOP))
	hud_nodes.append(bar)

	var line := ColorRect.new()
	line.color = Color(0.28, 0.36, 0.5, 0.9)
	line.position = Vector2(0, TOOLBAR_TOP)
	line.size = Vector2(1600, 2)
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(line)
	hud_nodes.append(line)

	# 资源：人 / 物 / 能 / 科 / 领（人力显示 上限）
	var res_icons := ["man", "mat", "energy", "tech", "land"]
	for i in 5:
		var icon := TextureRect.new()
		icon.texture = Art.load_ui("icon_" + str(res_icons[i]))
		icon.position = Vector2(18 + i * 148, 790)
		icon.size = Vector2(16, 16)
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		add_child(icon)
		hud_nodes.append(icon)
		var label := _make_label("", 21, RES_COLORS[i])
		label.position = Vector2(38 + i * 148, 786)
		label.size = Vector2(144, 28)
		add_child(label)
		resource_labels.append(label)
		hud_nodes.append(label)

	base_label = _make_label("", 19, Color(0.9, 0.94, 1.0))
	base_label.position = Vector2(20, 822)
	base_label.size = Vector2(260, 26)
	add_child(base_label)
	hud_nodes.append(base_label)

	building_label = _make_label("", 19, Color(0.8, 0.88, 1.0))
	building_label.position = Vector2(280, 822)
	building_label.size = Vector2(200, 26)
	add_child(building_label)
	hud_nodes.append(building_label)

	weather_label = _make_label("", 19, Color(0.75, 0.82, 0.95))
	weather_label.position = Vector2(20, 850)
	weather_label.size = Vector2(700, 26)
	add_child(weather_label)
	hud_nodes.append(weather_label)

	hover_label = _make_label("", 18, Color(0.62, 0.72, 0.9))
	hover_label.position = Vector2(20, 872)
	hover_label.size = Vector2(700, 24)
	add_child(hover_label)
	hud_nodes.append(hover_label)

	timer_label = _make_label("", 28, Color(0.95, 0.95, 1.0))
	timer_label.position = Vector2(980, 790)
	timer_label.size = Vector2(150, 40)
	timer_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	add_child(timer_label)
	hud_nodes.append(timer_label)

	status_label = _make_label("", 17, Color(0.7, 0.78, 0.92))
	status_label.position = Vector2(760, 826)
	status_label.size = Vector2(380, 60)
	status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(status_label)
	hud_nodes.append(status_label)

	# ---- 右侧按钮：成长树 / 拆除 / 修复 / 总部支援 + 缩放 ----
	growth_button = Button.new()
	growth_button.text = " 成长树"
	growth_button.icon = Art.load_ui("icon_tree")
	growth_button.add_theme_constant_override("icon_max_width", 18)
	growth_button.position = Vector2(1152, 786)
	growth_button.size = Vector2(96, 34)
	growth_button.toggle_mode = true
	_style_card_button(growth_button)
	growth_button.add_theme_font_size_override("font_size", 16)
	growth_button.toggled.connect(_on_growth_toggled)
	add_child(growth_button)
	hud_nodes.append(growth_button)

	demolish_button = Button.new()
	demolish_button.text = " 拆除"
	demolish_button.icon = Art.load_ui("icon_build")
	demolish_button.add_theme_constant_override("icon_max_width", 18)
	demolish_button.position = Vector2(1256, 786)
	demolish_button.size = Vector2(76, 34)
	demolish_button.toggle_mode = true
	_style_card_button(demolish_button)
	demolish_button.add_theme_font_size_override("font_size", 16)
	demolish_button.toggled.connect(_on_demolish_toggled)
	add_child(demolish_button)
	hud_nodes.append(demolish_button)

	repair_button = Button.new()
	repair_button.text = " 修复"
	repair_button.icon = Art.load_ui("icon_energy")
	repair_button.add_theme_constant_override("icon_max_width", 18)
	repair_button.position = Vector2(1340, 786)
	repair_button.size = Vector2(76, 34)
	repair_button.toggle_mode = true
	_style_card_button(repair_button)
	repair_button.add_theme_font_size_override("font_size", 16)
	repair_button.toggled.connect(_on_repair_toggled)
	add_child(repair_button)
	hud_nodes.append(repair_button)

	hq_button = Button.new()
	hq_button.text = " 总部支援"
	hq_button.icon = Art.load_ui("icon_flag")
	hq_button.add_theme_constant_override("icon_max_width", 18)
	hq_button.position = Vector2(1424, 786)
	hq_button.size = Vector2(156, 34)
	_style_card_button(hq_button)
	hq_button.add_theme_font_size_override("font_size", 16)
	hq_button.disabled = true
	hq_button.pressed.connect(_on_hq_pressed)
	add_child(hq_button)
	hud_nodes.append(hq_button)

	tower_button = Button.new()
	tower_button.text = " 造塔"
	tower_button.icon = Art.load_ui("icon_build")
	tower_button.add_theme_constant_override("icon_max_width", 18)
	tower_button.position = Vector2(440, 836)
	tower_button.size = Vector2(104, 34)
	tower_button.toggle_mode = true
	_style_card_button(tower_button)
	tower_button.add_theme_font_size_override("font_size", 16)
	tower_button.toggled.connect(_on_tower_button_toggled)
	add_child(tower_button)
	hud_nodes.append(tower_button)

	territory_button = Button.new()
	territory_button.text = "领地"
	territory_button.position = Vector2(1424, 830)
	territory_button.size = Vector2(156, 34)
	territory_button.toggle_mode = true
	_style_card_button(territory_button)
	territory_button.add_theme_font_size_override("font_size", 16)
	territory_button.toggled.connect(_on_territory_toggled)
	add_child(territory_button)
	hud_nodes.append(territory_button)

	for i in 3:
		var button := Button.new()
		button.text = ["＋", "－", "全图"][i]
		button.position = Vector2(1152 + i * 72, 830)
		button.size = Vector2(64, 34)
		_style_card_button(button)
		button.add_theme_font_size_override("font_size", 16)
		button.pressed.connect(_on_zoom_pressed.bind(i))
		add_child(button)
		hud_nodes.append(button)

	# ---- 手牌：居中，鼠标不在附近时只露出上半部分 ----
	hand_panel = Control.new()
	hand_panel.name = "Hand"
	hand_panel.position = Vector2(HAND_LEFT, TOOLBAR_TOP - HAND_PEEK)
	hand_panel.size = Vector2(HAND_SPAN, HAND_PEEK)
	hand_panel.clip_contents = true   # 收起时把下半张牌裁掉，别盖住工具栏
	hand_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(hand_panel)
	hud_nodes.append(hand_panel)

	for slot in 4:
		var view := CardViewScript.new()
		view.slot = slot
		view.size = Vector2(CARD_WIDTH, CARD_HEIGHT)
		view.position = Vector2(slot * HAND_STEP, 0)
		view.base_position = view.position
		view.pressed.connect(_on_hand_slot_pressed)
		view.right_clicked.connect(_on_hand_slot_discard)
		hand_panel.add_child(view)
		hand_buttons.append(view)

	deck_pile = Control.new()
	deck_pile.name = "DeckPile"
	deck_pile.position = Vector2(1216, TOOLBAR_TOP - 74)
	deck_pile.size = Vector2(80, 74)
	deck_pile.mouse_filter = Control.MOUSE_FILTER_IGNORE
	deck_pile.draw.connect(_draw_deck_pile)
	add_child(deck_pile)
	hud_nodes.append(deck_pile)

	event_label = _make_label("", 21, Color(1.0, 0.9, 0.45))
	event_label.position = Vector2(0, 548)
	event_label.size = Vector2(1600, 30)
	event_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(event_label)
	hud_nodes.append(event_label)

	hint_label = _make_label("", 19, Color(0.7, 0.78, 0.95))
	hint_label.position = Vector2(0, 574)
	hint_label.size = Vector2(1600, 28)
	hint_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(hint_label)
	hud_nodes.append(hint_label)

	toast_label = _make_label("", 22, Color(1.0, 0.55, 0.55))
	toast_label.position = Vector2(560, 496)
	toast_label.size = Vector2(480, 34)
	toast_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(toast_label)
	hud_nodes.append(toast_label)

func _set_hud_visible(value: bool) -> void:
	for node in hud_nodes:
		if is_instance_valid(node):
			node.visible = value
	if not value and structure_panel != null:
		structure_panel.visible = false
		rally_mode = false
	if value:
		# 回到对局：手牌先收成"只露一半"
		hand_expanded = false
		if hand_panel != null:
			hand_panel.position.y = TOOLBAR_TOP - HAND_PEEK
			hand_panel.size.y = HAND_PEEK

# 像素风 9-slice 样式：贴图是 16×16 的按钮 / 24×24 的面板，四边各留 4 / 8 像素
func _pixel_box(name: String, margin: int, pad: int = 8) -> StyleBox:
	var texture: Texture2D = Art.load_ui(name)
	if texture == null:
		var flat := StyleBoxFlat.new()
		flat.bg_color = Color(0.13, 0.18, 0.29)
		flat.border_color = Color(0.28, 0.42, 0.66)
		flat.set_border_width_all(2)
		return flat
	var box := StyleBoxTexture.new()
	box.texture = texture
	box.texture_margin_left = margin
	box.texture_margin_right = margin
	box.texture_margin_top = margin
	box.texture_margin_bottom = margin
	box.content_margin_left = pad
	box.content_margin_right = pad
	box.content_margin_top = 4
	box.content_margin_bottom = 4
	return box


func _style_card_button(button: Button, gold := false) -> void:
	var prefix := "button_gold" if gold else "button"
	button.add_theme_stylebox_override("normal", _pixel_box(prefix, 4))
	button.add_theme_stylebox_override("hover", _pixel_box(prefix + "_hover", 4))
	button.add_theme_stylebox_override("pressed", _pixel_box("button_pressed", 4))
	button.add_theme_stylebox_override("disabled", _pixel_box("button_disabled", 4))
	button.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	button.add_theme_color_override("font_color", Color(0.9, 0.94, 1.0))
	button.add_theme_color_override("font_pressed_color", Color(1, 1, 1))
	button.add_theme_color_override("font_disabled_color", Color(0.45, 0.5, 0.6))
	button.add_theme_color_override("font_hover_color", Color(1, 1, 1))


# 像素面板：给工具栏 / 构筑页 / 结果页做底
func _pixel_panel(parent: Node, name: String, position: Vector2, size: Vector2,
		dark := true) -> NinePatchRect:
	var rect := NinePatchRect.new()
	rect.name = name
	var texture: Texture2D = Art.load_ui("panel_dark" if dark else "panel")
	if texture != null:
		rect.texture = texture
		rect.patch_margin_left = 8
		rect.patch_margin_right = 8
		rect.patch_margin_top = 8
		rect.patch_margin_bottom = 8
	rect.position = position
	rect.size = size
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(rect)
	return rect

func _build_deck_panel() -> void:
	deck_panel = Control.new()
	deck_panel.name = "DeckPanel"
	deck_panel.size = Vector2(1600, 900)
	deck_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(deck_panel)

	var background := ColorRect.new()
	background.color = Color("#070a12")
	background.position = Vector2.ZERO
	background.size = Vector2(1600, 900)
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	deck_panel.add_child(background)
	_pixel_panel(deck_panel, "DeckFrame", Vector2(56, 16), Vector2(760, 760))
	_pixel_panel(deck_panel, "DeckInfo", Vector2(856, 300), Vector2(680, 220))

	var title := _make_label("构筑你的卡组", 44, Color(1.0, 0.85, 0.45))
	title.position = Vector2(80, 30)
	title.size = Vector2(600, 60)
	deck_panel.add_child(title)

	deck_subtitle = _make_label("从自己的 10 张卡池里选 8 张。手牌 4 张循环，打出去的那张回到牌堆末尾。",
			20, Color(0.65, 0.72, 0.9))
	deck_subtitle.position = Vector2(80, 108)
	deck_subtitle.size = Vector2(900, 40)
	deck_panel.add_child(deck_subtitle)

	# 阵营在开始页已经选过了：这里只显示当前阵营，避免选两次
	deck_faction_title = _make_label("当前阵营", 18, Color(0.7, 0.78, 0.95))
	deck_faction_title.position = Vector2(880, 318)
	deck_faction_title.size = Vector2(300, 26)
	deck_panel.add_child(deck_faction_title)

	deck_faction_label = _make_label("", 22, Color(1.0, 0.85, 0.45))
	deck_faction_label.position = Vector2(880, 346)
	deck_faction_label.size = Vector2(420, 34)
	deck_panel.add_child(deck_faction_label)

	deck_faction_passive = _make_label("", 16, Color(0.75, 0.82, 0.95))
	deck_faction_passive.position = Vector2(880, 382)
	deck_faction_passive.size = Vector2(420, 52)
	deck_faction_passive.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	deck_panel.add_child(deck_faction_passive)

	var change_faction := Button.new()
	change_faction.text = "换阵营"
	change_faction.position = Vector2(880, 442)
	change_faction.size = Vector2(180, 44)
	_style_card_button(change_faction)
	change_faction.add_theme_font_size_override("font_size", 18)
	change_faction.pressed.connect(_on_change_faction_pressed)
	deck_panel.add_child(change_faction)

	# 局外 DIY：兵营默认兵种
	var unit_row := Control.new()
	unit_row.position = Vector2(80, 146)
	unit_row.size = Vector2(1200, 44)
	unit_row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	deck_panel.add_child(unit_row)
	var unit_hint := _make_label("兵营默认兵种：", 18, Color(0.72, 0.8, 0.95))
	unit_hint.position = Vector2(0, 10)
	unit_hint.size = Vector2(160, 26)
	unit_row.add_child(unit_hint)
	for i in UNIT_CHOICES.size():
		var unit_button := Button.new()
		unit_button.text = UNIT_CHOICE_NAMES[i]
		unit_button.position = Vector2(150 + i * 96, 0)
		unit_button.size = Vector2(88, 40)
		unit_button.toggle_mode = true
		_style_card_button(unit_button)
		unit_button.add_theme_font_size_override("font_size", 15)
		unit_button.pressed.connect(_on_unit_choice.bind(i))
		unit_row.add_child(unit_button)
		unit_buttons.append(unit_button)

	# 用普通 Control 手动摆位：GridContainer 会覆盖子节点 position，卡牌选中抬手会跳
	deck_grid = Control.new()
	deck_grid.position = Vector2(80, 196)
	deck_grid.size = Vector2(760, 400)
	deck_grid.mouse_filter = Control.MOUSE_FILTER_IGNORE
	deck_panel.add_child(deck_grid)

	deck_count_label = _make_label("已选 0 / 8", 26, Color(0.85, 0.9, 1.0))
	deck_count_label.position = Vector2(80, 762)
	deck_count_label.size = Vector2(240, 40)
	deck_panel.add_child(deck_count_label)

	var clear_button := Button.new()
	clear_button.text = "清空"
	clear_button.position = Vector2(600, 750)
	clear_button.size = Vector2(120, 64)
	_style_card_button(clear_button)
	clear_button.add_theme_font_size_override("font_size", 20)
	clear_button.pressed.connect(_on_clear_deck)
	deck_panel.add_child(clear_button)

	starter_button = Button.new()
	starter_button.text = "一键选推荐"
	starter_button.position = Vector2(332, 750)
	starter_button.size = Vector2(220, 64)
	_style_card_button(starter_button)
	starter_button.add_theme_font_size_override("font_size", 20)
	starter_button.pressed.connect(_on_apply_starter)
	deck_panel.add_child(starter_button)

	deck_confirm = Button.new()
	deck_confirm.text = "开始对局"
	deck_confirm.position = Vector2(1240, 750)
	deck_confirm.size = Vector2(280, 64)
	_style_card_button(deck_confirm, true)
	deck_confirm.add_theme_font_size_override("font_size", 26)
	deck_confirm.disabled = true
	deck_confirm.pressed.connect(_on_confirm_deck)
	deck_panel.add_child(deck_confirm)

	deck_panel.visible = false


func _build_result_panel() -> void:
	result_panel = Control.new()
	result_panel.name = "ResultPanel"
	result_panel.size = Vector2(1600, 900)
	result_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(result_panel)

	var background := ColorRect.new()
	background.color = Color(0.02, 0.03, 0.06, 0.94)
	background.position = Vector2.ZERO
	background.size = Vector2(1600, 900)
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	result_panel.add_child(background)
	_pixel_panel(result_panel, "ResultFrame", Vector2(420, 210), Vector2(760, 470))

	result_label = _make_label("", 72, Color(1.0, 0.85, 0.45))
	result_label.position = Vector2(0, 250)
	result_label.size = Vector2(1600, 90)
	result_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	result_panel.add_child(result_label)

	result_detail = _make_label("", 28, Color(0.85, 0.9, 1.0))
	result_detail.position = Vector2(0, 350)
	result_detail.size = Vector2(1600, 40)
	result_detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	result_panel.add_child(result_detail)

	result_bases = _make_label("", 24, Color(0.7, 0.78, 0.95))
	result_bases.position = Vector2(0, 420)
	result_bases.size = Vector2(1600, 40)
	result_bases.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	result_panel.add_child(result_bases)

	rematch_button = Button.new()
	rematch_button.text = "再来一局"
	rematch_button.position = Vector2(560, 560)
	rematch_button.size = Vector2(220, 64)
	_style_card_button(rematch_button, true)
	rematch_button.add_theme_font_size_override("font_size", 24)
	rematch_button.pressed.connect(_on_direct_rematch)
	result_panel.add_child(rematch_button)

	change_deck_button = Button.new()
	change_deck_button.text = "重新构筑"
	change_deck_button.position = Vector2(820, 560)
	change_deck_button.size = Vector2(220, 64)
	_style_card_button(change_deck_button)
	change_deck_button.add_theme_font_size_override("font_size", 24)
	change_deck_button.pressed.connect(_on_rematch)
	result_panel.add_child(change_deck_button)

	result_panel.visible = false

func _make_label(text: String, font_size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.7))
	label.add_theme_constant_override("shadow_offset_x", 2)
	label.add_theme_constant_override("shadow_offset_y", 2)
	return label


func _connect_server() -> void:
	net = NetScript.new()
	net.name = "Net"
	add_child(net)
	net.connected.connect(_on_connected)
	net.disconnected.connect(_on_disconnected)
	net.message_received.connect(_on_message)
	print("[client] 连接 %s:%d" % [server_host, server_port])
	net.connect_to(server_host, server_port)
	if shot_path != "":
		_take_screenshot()


# 开发用：--shot=<绝对路径> --shot-delay=6 自动截图并退出（录 demo / 验收用）
func _take_screenshot() -> void:
	await get_tree().create_timer(shot_delay).timeout
	await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	var err := image.save_png(shot_path)
	print("[client] 截图 %s (err=%d)" % [shot_path, err])
	get_tree().quit()


# ---------------------------------------------------------------- 网络回调

func _on_connected() -> void:
	status_label.text = "已连接服务端，等待卡池…"
	conn_title.text = "已连接服务端"
	conn_detail.text = "等待卡表下发…"
	print("[client] 已连接服务端")


func _on_disconnected() -> void:
	match_active = false
	status_label.text = "连接断开"
	print("[client] 连接断开")
	# 关键：没连上服务端时画面本来就是空的（地图和卡表都来自服务端），
	# 必须明确告诉玩家"现在缺的是什么"，否则看起来就是"游戏打不开"。
	# 开始页自己会显示连接状态，不要再盖一层全屏提示
	if menu_panel != null and menu_panel.visible:
		menu_status.text = "未连接服务端 · 正在重试…（先启动 run-demo.cmd）"
		reconnect_delay = 2.5
		return
	conn_overlay.visible = true
	conn_title.text = "未连接服务端"
	conn_detail.text = ("正在重试 %s:%d …\n\n"
		+ "先启动服务端：双击 run-demo.cmd\n"
		+ "（或运行 .\\demo.ps1 -Port %d）") % [server_host, server_port, server_port]
	reconnect_delay = 2.5


# ---------------------------------------------------------------- 开始页 / 暂停

const TERRAIN_LEGEND := """地形属性
  平地 · 无影响
  道路 · 移速 +25%
  森林 · 移速 -30%（伏击好地方）
  高地 · 站在上面伤害 +25%，移速 -15%
  水域 · 不可通行
  岩石 · 不可通行

地图资源点
  矿脉 · 物资 +2.5/秒　　能源井 · 能源 +2/秒
  高战略点 · 5 分钟后点亮，物资+能源双倍（开局是暗的）

操作
  点手牌选卡 → 点战场放置（建筑/法术；塔只能盖在自家领地的空地上）
  滚轮缩放 · 右键拖动 · 工具栏「全图」回到全局
  「拆除」拆自家建筑/塔 · 「成长树」打开升级页
  ESC 暂停 / 退出
"""


func _build_menu() -> void:
	menu_panel = Control.new()
	menu_panel.name = "Menu"
	menu_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	menu_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(menu_panel)

	var background := ColorRect.new()
	background.color = Color("#070a12")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	menu_panel.add_child(background)

	var title := _make_label("KINGDOM FIGHT", 64, Color(1.0, 0.85, 0.45))
	title.position = Vector2(120, 90)
	title.size = Vector2(900, 80)
	menu_panel.add_child(title)

	var subtitle := _make_label("四人实时 RTS + 卡牌构筑 ｜ 四家联盟抢一颗星球",
			24, Color(0.7, 0.78, 0.95))
	subtitle.position = Vector2(124, 170)
	subtitle.size = Vector2(900, 40)
	menu_panel.add_child(subtitle)

	var start := Button.new()
	start.text = "开始游戏"
	start.position = Vector2(124, 250)
	start.size = Vector2(320, 76)
	_style_card_button(start)
	start.add_theme_font_size_override("font_size", 30)
	start.pressed.connect(_on_menu_start)
	menu_panel.add_child(start)

	var quit := Button.new()
	quit.text = "退出"
	quit.position = Vector2(124, 346)
	quit.size = Vector2(320, 60)
	_style_card_button(quit)
	quit.add_theme_font_size_override("font_size", 22)
	quit.pressed.connect(_on_menu_quit)
	menu_panel.add_child(quit)

	var legend := _make_label(TERRAIN_LEGEND, 18, Color(0.72, 0.8, 0.92))
	legend.position = Vector2(1052, 250)
	legend.size = Vector2(520, 520)
	menu_panel.add_child(legend)

	# 阵营选择：点哪个就在右边列出它的 7 张牌
	menu_status = _make_label("正在连接服务端…", 18, Color(0.6, 0.68, 0.85))
	menu_status.position = Vector2(124, 214)
	menu_status.size = Vector2(700, 30)
	menu_panel.add_child(menu_status)

	menu_card_preview = _make_label("", 18, Color(0.85, 0.9, 1.0))
	menu_card_preview.position = Vector2(520, 250)
	menu_card_preview.size = Vector2(500, 520)
	menu_panel.add_child(menu_card_preview)


func _build_pause_panel() -> void:
	pause_panel = Control.new()
	pause_panel.name = "Pause"
	pause_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	pause_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	pause_panel.visible = false
	add_child(pause_panel)

	var dim := ColorRect.new()
	dim.color = Color(0.02, 0.03, 0.06, 0.9)
	dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	pause_panel.add_child(dim)

	var title := _make_label("已暂停", 52, Color(1.0, 0.85, 0.45))
	title.position = Vector2(0, 260)
	title.size = Vector2(1600, 70)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	pause_panel.add_child(title)

	var resume := Button.new()
	resume.text = "继续游戏"
	resume.position = Vector2(640, 380)
	resume.size = Vector2(320, 64)
	_style_card_button(resume)
	resume.add_theme_font_size_override("font_size", 24)
	resume.pressed.connect(_on_pause_resume)
	pause_panel.add_child(resume)

	var to_menu := Button.new()
	to_menu.text = "返回主菜单"
	to_menu.position = Vector2(640, 460)
	to_menu.size = Vector2(320, 64)
	_style_card_button(to_menu)
	to_menu.add_theme_font_size_override("font_size", 24)
	to_menu.pressed.connect(_on_pause_menu)
	pause_panel.add_child(to_menu)

	var quit := Button.new()
	quit.text = "退出游戏"
	quit.position = Vector2(640, 540)
	quit.size = Vector2(320, 64)
	_style_card_button(quit)
	quit.add_theme_font_size_override("font_size", 24)
	quit.pressed.connect(_on_menu_quit)
	pause_panel.add_child(quit)


func _build_structure_panel() -> void:
	structure_panel = StructurePanelScript.new()
	structure_panel.unit_chosen.connect(_on_unit_chosen)
	structure_panel.rally_requested.connect(_on_rally_requested)
	structure_panel.closed.connect(func() -> void:
		structure_panel.visible = false
		rally_mode = false)
	add_child(structure_panel)


func _open_structure(target: Dictionary) -> void:
	if structure_panel == null:
		return
	var kind := int(target.get("kind", 0))
	var index := int(target.get("index", -1))
	if kind == 0:
		var own: Array = []
		for b in snapshot.get("buildings", []):
			if int(b.owner) == my_player:
				own.append(b)
		if index < 0 or index >= own.size():
			return
		structure_panel.show_building(own[index], index, unit_table)
	else:
		for t in snapshot.get("towers", []):
			if int(t.get("id", -1)) == index:
				structure_panel.show_tower(t, unit_table, 280.0)
				break


func _on_unit_chosen(building_index: int, unit_type: int) -> void:
	if not match_active or building_index < 0:
		return
	var payload := PackedByteArray()
	payload.append(building_index)
	payload.append(unit_type)
	net.send(C2S_SET_UNIT, payload)


func _on_rally_requested(building_index: int) -> void:
	rally_index = building_index
	rally_mode = true
	structure_panel.visible = false
	hint_label.text = "点地图指定这个兵营的攻击区域（出兵后先往那儿走）"


func _refresh_structure_panel() -> void:
	if structure_panel == null or not structure_panel.visible:
		return
	# 重新读一次快照，换兵种后按钮状态要立刻更新
	if structure_panel.has_method("show_building"):
		pass
func _build_hover_label() -> void:
	hover_label = _make_label("", 18, Color(0.62, 0.72, 0.9))
	hover_label.position = Vector2(24, 668)
	hover_label.size = Vector2(900, 26)
	add_child(hover_label)


func _on_menu_start() -> void:
	if cards.is_empty() or not (net != null and net.is_socket_open()):
		menu_status.text = "正在连接服务端…"
		if net == null or not net.is_socket_open():
			_connect_server()
		return
	menu_panel.visible = false
	_build_deck_grid()
	deck_panel.visible = true
	_set_hud_visible(false)


# 开始页的阵营按钮：选了就更新右边的卡牌预览
func _build_menu_factions() -> void:
	for button in menu_faction_buttons:
		button.queue_free()
	menu_faction_buttons.clear()
	for i in 4:
		var button := Button.new()
		button.text = "%s · %s" % [ALLIANCE_NAMES[i], ALLIANCE_PASSIVE[i]]
		button.position = Vector2(124, 420 + i * 62)
		button.size = Vector2(370, 54)
		button.toggle_mode = true
		_style_card_button(button)
		button.add_theme_font_size_override("font_size", 17)
		button.pressed.connect(_on_menu_faction.bind(i))
		menu_panel.add_child(button)
		menu_faction_buttons.append(button)
	_on_menu_faction(my_alliance)


func _on_menu_faction(index: int) -> void:
	my_alliance = index
	for i in menu_faction_buttons.size():
		menu_faction_buttons[i].set_pressed_no_signal(i == index)
	_update_menu_preview()


func _update_menu_preview() -> void:
	if menu_card_preview == null:
		return
	if cards.is_empty():
		menu_card_preview.text = ""
		return
	var pool_size := 0
	for id in cards.keys():
		if int(cards[id].alliance) == my_alliance:
			pool_size += 1
	var lines: Array = ["%s · 卡池（%d 张选 %d）" % [
		ALLIANCE_NAMES[my_alliance], pool_size, deck_required], ""]
	for id in cards.keys():
		var info: Dictionary = cards[id]
		if int(info.alliance) != my_alliance:
			continue
		var kind_names := ["单位", "法术", "建筑"]
		var kind: String = kind_names[int(info.kind)] if int(info.kind) < 3 else "?"
		lines.append("  [%s] %s   %s" % [kind, str(info.name), _cost_text(info.costs)])
	menu_card_preview.text = "\n".join(lines)


func _on_menu_quit() -> void:
	get_tree().quit()


func _on_pause_resume() -> void:
	pause_panel.visible = false


func _on_pause_menu() -> void:
	pause_panel.visible = false
	match_active = false
	net.disconnect_peer()
	result_panel.visible = false
	deck_panel.visible = false
	_set_hud_visible(false)
	menu_panel.visible = true


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	if key.keycode == KEY_M:
		if audio != null:
			var on: bool = audio.toggle()
			_show_toast("音效：%s" % ("开" if on else "关"))
			get_viewport().set_input_as_handled()
			return
	if key.keycode == KEY_ESCAPE:
		if menu_panel != null and menu_panel.visible:
			return
		if growth_panel != null and growth_panel.visible:
			_on_growth_close()
			get_viewport().set_input_as_handled()
			return
		pause_panel.visible = not pause_panel.visible
		get_viewport().set_input_as_handled()


func _build_connection_overlay() -> void:
	conn_overlay = Control.new()
	conn_overlay.name = "ConnOverlay"
	conn_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	conn_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(conn_overlay)

	var dim := ColorRect.new()
	dim.color = Color(0.02, 0.03, 0.06, 0.94)
	dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	conn_overlay.add_child(dim)

	conn_title = _make_label("正在连接服务端…", 42, Color(1.0, 0.84, 0.42))
	conn_title.position = Vector2(0, 330)
	conn_title.size = Vector2(1600, 60)
	conn_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	conn_overlay.add_child(conn_title)

	conn_detail = _make_label("", 22, Color(0.76, 0.83, 0.95))
	conn_detail.position = Vector2(0, 410)
	conn_detail.size = Vector2(1600, 160)
	conn_detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	# 一上来就把"要怎么启动服务端"写清楚：地图和卡表都来自服务端，
	# 连不上时画面本来就是空的，不写清楚就会被当成"游戏打不开"。
	conn_detail.text = ("目标 %s:%d\n\n先启动服务端：双击 run-demo.cmd\n"
		+ "（或运行 .\\demo.ps1 -Port %d）") % [server_host, server_port, server_port]
	conn_overlay.add_child(conn_detail)


func _on_message(cmd: int, payload: PackedByteArray) -> void:
	match cmd:
		S2C_WELCOME:
			var b := _reader(payload)
			my_player = b.get_u8()
			var ai_count := b.get_u8()
			b.get_u8()  # human_count
			b.get_u8()  # players
			deck_required = b.get_u8()
			b.get_u8()  # hand_size
			b.get_u8()  # pool_size
			build_stamp = _read_text(b)
			board.my_player = my_player
			conn_overlay.visible = false
			status_label.text = "已连接 · %d 人混战 · build %s" % [4, build_stamp]
		S2C_MAP:
			var b := _reader(payload)
			var map_cols := b.get_u16()
			var map_rows := b.get_u16()
			b.get_u16()  # tile_size
			base_max_hp = []
			for i in 4:
				base_max_hp.append(b.get_u16())
			var base_positions := []
			for i in 4:
				base_positions.append(Vector2(b.get_16(), b.get_16()))
			alliances = []
			for i in 4:
				alliances.append(b.get_u8())
			var tile_bytes: PackedByteArray = b.get_data(map_cols * map_rows)[1]
			board.set_map(map_cols, map_rows, tile_bytes, base_positions, base_max_hp, alliances)
			print("[client] 地图 %dx%d，联盟 %s" % [map_cols, map_rows, str(alliances)])
		S2C_CARD_POOL:
			_parse_card_pool(payload)
			if not auto_deck:
				_load_profile()
			if show_deck_on_start:
				show_deck_on_start = false
				_on_menu_start()
			if menu_panel != null and menu_panel.visible:
				# 还在开始页：只更新阵营预览，别把构筑界面顶上来
				_build_menu_factions()
				_update_menu_preview()
				menu_status.text = "已连接服务端 · 选好阵营后点「开始游戏」"
			else:
				_build_deck_grid()
				deck_panel.visible = true
				_set_hud_visible(false)
			print("[client] 收到卡池 %d 张" % cards.size())
		S2C_MATCH_START:
			var start := _reader(payload)
			start.get_u8()  # 我的座位
			var total_seconds := start.get_u16()
			var hq_unlock := start.get_u16()
			deck_panel.visible = false
			result_panel.visible = false
			match_active = true
			selected_slot = -1
			hq_targeting = false
			board.set_placement(false, -1)
			status_label.text = "%s · 总部支援 %d 分后解锁" % [
				ALLIANCE_NAMES[my_alliance], int(hq_unlock / 60)]
			_set_hud_visible(true)
			print("[client] 开局：%d 秒，总部支援 %d 秒解锁" % [total_seconds, hq_unlock])
			# 第一次玩的引导：不然不知道要先点手牌再点地图
			_show_toast("点下方手牌 → 点地图放置")
			if selftest_mode:
				_run_selftest()
		S2C_SNAPSHOT:
			_on_snapshot(payload)
		S2C_MATCH_OVER:
			_on_match_over(payload)
		S2C_HQ_ACK:
			var ack := _reader(payload)
			var result := ack.get_u8()
			hq_targeting = false
			if result != 0:
				_show_toast("总部支援失败：%s" % _play_result_text(result))
		S2C_UNIT_TABLE:
			var ut := _reader(payload)
			unit_table.clear()
			for i in ut.get_u8():
				var utype := ut.get_u8()
				var tt := ut.get_u16()
				var cm := ut.get_u16()
				var cmat := ut.get_u16()
				var uname := _read_text(ut)
				unit_table[utype] = {"type": utype, "train": tt, "manpower": cm,
					"materiel": cmat, "name": uname}
		S2C_SET_UNIT_ACK:
			var uack := _reader(payload)
			if uack.get_u8() == 0:
				_show_toast("已切换训练兵种")
				_refresh_structure_panel()
			else:
				_show_toast("切换失败")
		S2C_SET_RALLY_ACK:
			var rack2 := _reader(payload)
			if rack2.get_u8() == 0:
				_show_toast("已指定攻击区域")
			else:
				_show_toast("指定失败")
		S2C_BUILD_TOWER_ACK:
			var tack := _reader(payload)
			var tresult := tack.get_u8()
			if tresult == 0:
				audio.play("place")
				_show_toast("塔已开工")
				tower_build_mode = false
				if tower_button != null:
					tower_button.set_pressed_no_signal(false)
				board.set_placement(false, -1)
				hint_label.text = ""
			else:
				_show_toast(_play_result_text(tresult))
		S2C_TERRITORY:
			var town_reader := _reader(payload)
			var town_len := town_reader.get_u16()
			var town_mask := PackedByteArray()
			town_mask.resize(town_len)
			for i in town_len:
				town_mask[i] = town_reader.get_u8()
			if board != null:
				board.set_territory_mask(town_mask)
				board.queue_redraw()
		S2C_DISCARD_ACK:
			var dack2 := _reader(payload)
			dack2.get_u8()
			var dres2 := dack2.get_u8()
			if dres2 == 0:
				audio.play("click", -8.0)
			else:
				hand_signature = ""
				for v in hand_buttons:
					v.visible = true
				_show_toast("弃不了：%s" % _play_result_text(dres2))
		S2C_REPAIR_ACK:
			var rack := _reader(payload)
			rack.get_u8()
			rack.get_u16()
			var rresult := rack.get_u8()
			if rresult == 0:
				audio.play("build_done", -4.0)
				_show_toast("已修复")
			else:
				_show_toast("修不了：%s" % _play_result_text(rresult))
		S2C_UPGRADE_ACK:
			var uack := _reader(payload)
			var upgrade_id := uack.get_u8()
			var level := uack.get_u8()
			var uresult := uack.get_u8()
			if uresult == 0:
				audio.play("upgrade")
				_show_toast("%s 升到 Lv%d" % [UPGRADE_NAMES[upgrade_id], level])
			else:
				_show_toast("升级失败：%s" % _play_result_text(uresult))
		S2C_DEMOLISH_ACK:
			var dack := _reader(payload)
			dack.get_u8()   # kind
			dack.get_u16()  # 建筑下标 / 塔 id
			var dresult := dack.get_u8()
			if dresult == 0:
				audio.play("place", -10.0)
				_show_toast("已拆除")
			else:
				_show_toast("拆不了：%s" % _play_result_text(dresult))
		S2C_PLAY_ACK:
			var b := _reader(payload)
			var slot := b.get_u8()
			var result := b.get_u8()
			last_ack_result = result
			if selftest_mode or uitest_mode:
				print("[test] 出牌回执 slot=%d result=%d" % [slot, result])
			if result == 0:
				audio.play("place")
			else:
				# 被拒：把被 ghost 藏起来的手牌重新显示出来
				hand_signature = ""
				for v in hand_buttons:
					v.visible = true
				_show_toast(_play_result_text(result))
				selected_slot = -1
				_refresh_hand_selection()
		S2C_ERROR:
			var b := _reader(payload)
			status_label.text = "服务端错误：" + _read_text(b)
		_:
			pass


func _reader(payload: PackedByteArray) -> StreamPeerBuffer:
	var buffer := StreamPeerBuffer.new()
	buffer.big_endian = true
	buffer.data_array = payload
	return buffer


func _read_text(buffer: StreamPeerBuffer) -> String:
	var length := buffer.get_u16()
	if length <= 0:
		return ""
	var raw: PackedByteArray = buffer.get_data(length)[1]
	return raw.get_string_from_utf8()


func _parse_card_pool(payload: PackedByteArray) -> void:
	var buffer := _reader(payload)
	var count := buffer.get_u16()
	for i in count:
		var id := buffer.get_u16()
		var alliance := buffer.get_u8()  # 这张卡只属于这个阵营
		var kind := buffer.get_u8()
		var costs := [buffer.get_u16(), buffer.get_u16(), buffer.get_u16(), buffer.get_u16()]
		var radius := buffer.get_u16()
		var building := int(buffer.get_u8())
		var spell := int(buffer.get_u8())
		if building == 255:
			building = -1
		var name := _read_text(buffer)
		cards[id] = {"id": id, "name": name, "costs": costs, "kind": kind,
			"alliance": alliance, "radius": radius, "building": building, "spell": spell}


# "兵10 食8" 这种紧凑写法，卡面放得下
func _cost_text(costs: Array) -> String:
	var parts := []
	for i in 4:
		if i >= costs.size():
			continue
		if int(costs[i]) > 0:
			parts.append("%s%d" % [RES_NAMES[i], int(costs[i])])
	return " ".join(parts)


func _can_afford(costs: Array) -> bool:
	var res: Array = snapshot.get("res", [0, 0, 0, 0])
	for i in 4:
		if i < costs.size() and int(costs[i]) > int(res[i]):
			return false
	return true


# ---------------------------------------------------------------- 构筑界面

func _build_deck_grid() -> void:
	for child in deck_grid.get_children():
		child.queue_free()
	card_buttons.clear()
	selected_cards.clear()
	_refresh_deck_faction_header()
	deck_count_label.text = "已选 0 / %d" % deck_required
	if deck_subtitle != null:
		deck_subtitle.text = "从自己的 10 张卡池里选 %d 张。同卡最多 2 张，右键减一张。" % deck_required
	deck_confirm.disabled = true

	# 只显示自己阵营的 10 张 —— 直接复用竖版 CardView，和手牌长得一样
	var index := 0
	for id in cards.keys():
		var info: Dictionary = cards[id]
		if int(info.alliance) != my_alliance:
			continue
		var view := CardViewScript.new()
		view.slot = id
		view.lift_amount = 0.0
		var col := index % 5
		var row := index / 5
		view.position = Vector2(col * 146, row * 196)
		view.base_position = view.position
		view.pressed.connect(_on_deck_card_pressed)
		view.right_clicked.connect(_on_deck_card_remove)
		view.set_card(info)
		deck_grid.add_child(view)
		card_buttons[id] = view
		index += 1

	if OS.get_cmdline_user_args().has("--deck-demo") and card_buttons.size() >= 4:
		var demo_ids: Array = card_buttons.keys()
		_on_deck_card_pressed(demo_ids[0])
		_on_deck_card_pressed(demo_ids[0])   # 同一张带 2 张
		_on_deck_card_pressed(demo_ids[1])
		_on_deck_card_pressed(demo_ids[2])
		_on_deck_card_pressed(demo_ids[3])
	if auto_deck and card_buttons.size() >= deck_required:
		await get_tree().create_timer(0.4).timeout
		_on_apply_starter()
		await get_tree().create_timer(0.2).timeout
		_on_confirm_deck()


# 构筑界面点卡：选中/取消（CardView 自己不做 toggle，状态放在 selected_cards 里）
func _on_deck_card_pressed(id: int) -> void:
	audio.play("click", -12.0)
	var copies := 0
	for card_id in selected_cards:
		if int(card_id) == id:
			copies += 1
	if copies >= 2:
		_show_toast("同一张卡最多带 2 张（右键可以减一张）")
		return
	if selected_cards.size() >= deck_required:
		_show_toast("卡组上限 %d 张" % deck_required)
		return
	selected_cards.append(id)
	_refresh_deck_selection()


func _on_deck_card_remove(id: int) -> void:
	for i in range(selected_cards.size() - 1, -1, -1):
		if int(selected_cards[i]) == id:
			selected_cards.remove_at(i)
			break
	audio.play("click", -14.0)
	_refresh_deck_selection()


func _refresh_deck_selection() -> void:
	for id in card_buttons.keys():
		var view = card_buttons[id]
		if view == null or not view.has_method("set_selected"):
			continue
		var copies := 0
		for card_id in selected_cards:
			if int(card_id) == int(id):
				copies += 1
		view.set_copies(copies)
		view.set_selected(copies > 0)
	deck_count_label.text = "已选 %d / %d" % [selected_cards.size(), deck_required]
	deck_count_label.add_theme_color_override("font_color",
		Color(1.0, 0.85, 0.35) if selected_cards.size() == deck_required else Color(0.85, 0.9, 1.0))
	deck_confirm.disabled = selected_cards.size() != deck_required


func _on_unit_choice(index: int) -> void:
	preferred_unit = index
	for i in unit_buttons.size():
		unit_buttons[i].set_pressed_no_signal(i == index)
	audio.play("click", -10.0)


# 局外 DIY：把上次选的阵营 / 兵营默认兵种存到 user://profile.json
func _load_profile() -> void:
	var path := "user://profile.json"
	if not FileAccess.file_exists(path):
		return
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return
	var data = JSON.parse_string(file.get_as_text())
	if typeof(data) != TYPE_DICTIONARY:
		return
	if data.has("alliance"):
		my_alliance = clampi(int(data["alliance"]), 0, ALLIANCE_NAMES.size() - 1)
	if data.has("preferred_unit"):
		preferred_unit = int(data["preferred_unit"])
	for i in unit_buttons.size():
		unit_buttons[i].set_pressed_no_signal(i == preferred_unit)


func _save_profile() -> void:
	var file := FileAccess.open("user://profile.json", FileAccess.WRITE)
	if file == null:
		return
	file.store_string(JSON.stringify({
		"alliance": my_alliance,
		"preferred_unit": preferred_unit,
	}))

func _refresh_deck_faction_header() -> void:
	if deck_faction_label != null:
		deck_faction_label.text = ALLIANCE_NAMES[my_alliance]
	if deck_faction_passive != null:
		deck_faction_passive.text = ALLIANCE_PASSIVE[my_alliance]


func _refresh_menu_faction_buttons() -> void:
	for i in menu_faction_buttons.size():
		menu_faction_buttons[i].set_pressed_no_signal(i == my_alliance)
	_update_menu_preview()

func _on_change_faction_pressed() -> void:
	audio.play("click")
	deck_panel.visible = false
	menu_panel.visible = true
	_refresh_menu_faction_buttons()

func _on_confirm_deck() -> void:
	if selected_cards.size() != deck_required:
		return
	var payload := PackedByteArray()
	payload.append(selected_cards.size())
	for id in selected_cards:
		payload.append((id >> 8) & 0xFF)
		payload.append(id & 0xFF)
	payload.append(my_alliance)  # 联盟随卡组一起提交
	# 局外 DIY：再跟一个字节表示兵营默认兵种（255 = 用阵营默认）
	payload.append(preferred_unit if preferred_unit >= 0 else 255)
	net.send(C2S_DECK, payload)
	_save_profile()
	status_label.text = "已提交 %s 卡组，等待开局…" % ALLIANCE_NAMES[my_alliance]


# ---------------------------------------------------------------- 对局

func _sfx_build_completed(snap: Dictionary) -> void:
	for building in snap.get("buildings", []):
		if int(building.owner) != my_player:
			continue
		if int(building.get("progress", 100)) < 100:
			continue
		var key := "%d:%d:%d" % [int(building.owner), int(building.x), int(building.y)]
		if not _ready_buildings.has(key):
			_ready_buildings[key] = true
			audio.play("build_done", -8.0)

func _on_snapshot(payload: PackedByteArray) -> void:
	var snap := SnapshotParser.parse(payload)
	if not snap.get("ok", false):
		push_warning("快照解析失败：%s" % snap.get("error", "unknown"))
		return

	snapshot = snap
	board.set_snapshot(snap)
	timer_label.text = "%d:%02d" % [snap["remaining"] / 60, snap["remaining"] % 60]
	_sfx_build_completed(snap)
	_refresh_player_hud(snap)
	if (place_demo_pending or spell_demo_pending) and not hand_seen.is_empty():
		var want_kind := 2 if place_demo_pending else 1   # 2=建筑牌 1=法术牌
		for slot in hand_seen.size():
			var info2: Dictionary = cards.get(hand_seen[slot], {})
			if int(info2.get("kind", 0)) != want_kind:
				continue
			selected_slot = slot
			board.set_placement(true, int(info2["kind"]), int(info2.get("building", -1)),
				int(info2.get("radius", 0)), int(info2.get("spell", 0)))
			_refresh_hand_selection()
			break
		place_demo_pending = false
		spell_demo_pending = false
		# 手牌里没有对应类型的牌时，直接用演示参数画预览
		if selected_slot < 0:
			board.set_placement(true, want_kind, -1,
				200 if want_kind == 1 else 0, 1 if want_kind == 1 else 0)
		if board != null and my_player < board.bases.size():
			demo_hover_pos = (board.bases[my_player] as Vector2) + Vector2(180, 90)
			board.forced_hover = demo_hover_pos
			board.hover = demo_hover_pos
			board.queue_redraw()
	if ghost_demo_pending and not hand_seen.is_empty():
		ghost_demo_pending = false
		var demo_ghost := _spawn_ghost(int(hand_seen[0]))
		print("[ghost-demo] 出牌残影演示：card=%d" % int(hand_seen[0]))
		_fly_ghost(demo_ghost, hand_panel.position + Vector2(0, 0), Vector2(800, 360), 5.0, 0.5, 0.2, 0.0)
	if show_growth_on_start and growth_panel != null and not growth_panel.visible:
		show_growth_on_start = false
		growth_panel.visible = true
		if hover_label != null:
			hover_label.visible = false
		if growth_button != null:
			growth_button.set_pressed_no_signal(true)
	if growth_panel != null and growth_panel.visible:
		_refresh_growth_panel()
	if OS.get_cmdline_user_args().has("--structure-demo") and structure_panel != null and not structure_panel.visible:
		_open_structure({"kind": 0, "index": 0})
	if OS.get_cmdline_user_args().has("--territory") and board != null and not board.show_territory:
		board.show_territory = true
		if territory_button != null:
			territory_button.set_pressed_no_signal(true)
		board.queue_redraw()
	if OS.get_cmdline_user_args().has("--fit") and board != null:
		board.reset_zoom()
	if not got_first_snapshot:
		got_first_snapshot = true
		# 开局把镜头对准自己的基地（之前默认停在整张地图中心，容易"找不到自己"）
		var my_bases: Array = snap.get("bases", [])
		if my_player < my_bases.size():
			board.focus_on(board.bases[my_player] if board.bases.size() > my_player else Vector2(board.FIELD_W, board.FIELD_H) * 0.5)
		print("[client] 首个快照 tick=%d 人=%d 物=%d 能=%d 科=%d bases=%s" % [
			snap["tick"], snap["res"][0], snap["res"][1], snap["res"][2], snap["res"][3],
			str(snap["bases"])])
	_refresh_hand()


func _refresh_player_hud(snap: Dictionary) -> void:
	var res: Array = snap.get("res", [0, 0, 0, 0, 0])
	var cap: int = int(snap.get("manpower_cap", 0))
	for i in resource_labels.size():
		if i >= res.size():
			continue
		if i == 0:
			resource_labels[i].text = "%s %d/%d" % [RES_NAMES[i], int(res[i]), cap]
		else:
			resource_labels[i].text = "%s %d" % [RES_NAMES[i], int(res[i])]

	# 自己的基地血量 / 建筑数 / 计时
	var bases: Array = snap.get("bases", [])
	var my_hp := int(bases[my_player]) if my_player < bases.size() else 0
	var my_max := int(base_max_hp[my_player]) if my_player < base_max_hp.size() else 0
	if base_label != null:
		base_label.text = "基地 %d / %d" % [my_hp, my_max]

	var buildings := 0
	for building in snap.get("buildings", []):
		if int(building.owner) == my_player:
			buildings += 1
	var building_cap := int(snap.get("building_cap", 0))
	var towers := 0
	for tower in snap.get("towers", []):
		if int(tower.owner) == my_player:
			towers += 1
	if building_label != null:
		building_label.text = "建筑 %d / %d · 塔 %d / %d" % [
			buildings, building_cap, towers, int(snap.get("tower_cap", 0))]

	if timer_label != null:
		var left := int(snap.get("remaining", 0))
		timer_label.text = "%d:%02d" % [left / 60, left % 60]

	_refresh_weather(snap)
	_refresh_hq_button()

const WEATHER_NAMES := ["晴朗", "降雨", "浓雾", "风暴"]
const WEATHER_EFFECT := ["无影响", "移速 -10%", "索敌 / 塔射程 -25%", "移速 -5%"]


func _refresh_weather(snap: Dictionary) -> void:
	var weather := int(snap.get("weather", 0))
	var phase := int(snap.get("day_phase", 0))
	var night: bool = phase >= 55
	var text := "%s · %s · %s" % [
		"夜晚" if night else "白天",
		WEATHER_NAMES[weather] if weather < WEATHER_NAMES.size() else "?",
		WEATHER_EFFECT[weather] if weather < WEATHER_EFFECT.size() else "",
	]
	if night:
		text += " · 视野 -15%"
	weather_label.text = text

	# 随机事件横幅
	var event := int(snap.get("event", 0))
	var event_left := int(snap.get("event_left", 0))
	if event == 1:
		var who := int(snap.get("event_target", -1))
		event_label.text = "随机事件 · 补给空投" if event_left > 0 else ""
	elif event == 2:
		event_label.text = "随机事件 · 矿脉爆发（产量翻倍 %d 秒）" % event_left
	else:
		event_label.text = ""
	event_label.modulate = Color(1.0, 0.9, 0.45)


# 总部支援按钮：0 没解锁 / 1 可用 / 2 领地不足 / 3 冷却
func _refresh_hq_button() -> void:
	if hq_button == null:
		return
	var state := int(snapshot.get("hq_state", 0))
	var territory := int(snapshot.get("res", [0, 0, 0, 0, 0])[4]) if snapshot.has("res") else 0
	hq_button.disabled = state != 1
	match state:
		0: hq_button.text = "总部支援 · 未解锁"
		1: hq_button.text = "总部支援 · 点击瞄准"
		2: hq_button.text = "总部支援 · 领地 %d/30" % territory
		3: hq_button.text = "总部支援 · 冷却中"
		_: hq_button.text = "总部支援"


func _on_hq_pressed() -> void:
	if not match_active or int(snapshot.get("hq_state", 0)) != 1:
		return
	hq_targeting = true
	selected_slot = -1
	board.set_placement(false, -1)
	hint_label.text = "点击地图任意位置呼叫对地打击"


# 成长树整页：拆到 scripts/growth_page.gd（界面自包含，main 只负责喂数据）
func _build_growth_panel() -> void:
	growth_panel = GrowthPageScript.new()
	growth_panel.visible = false
	growth_panel.upgrade_requested.connect(_on_upgrade_pressed)
	growth_panel.closed.connect(_on_growth_close)
	add_child(growth_panel)


func _refresh_growth_panel() -> void:
	if growth_panel == null:
		return
	var ups: Array = snapshot.get("upgrades", [])
	var tech := int(snapshot.get("res", [0, 0, 0, 0, 0])[3]) if snapshot.has("res") else 0
	growth_panel.refresh(ups, tech)

func _on_upgrade_pressed(index: int) -> void:
	if not match_active:
		return
	var payload := PackedByteArray()
	payload.append(index)
	net.send(C2S_UPGRADE, payload)


func _on_tower_button_toggled(pressed: bool) -> void:
	tower_build_mode = pressed
	if pressed:
		selected_slot = -1
		board.set_placement(true, 2, 4, 0, 0)   # kind=建筑(2) + tower 标记(4)
		hint_label.text = "点地图造塔：领地内空地，%d 物 %d 能" % [70, 30]
	else:
		board.set_placement(false, -1)
		hint_label.text = ""
	_refresh_hand_selection()


func _on_territory_toggled(pressed: bool) -> void:
	if board != null:
		board.show_territory = pressed
		board.queue_redraw()


func _on_growth_toggled(pressed: bool) -> void:
	growth_panel.visible = pressed
	# 悬停提示是最后建的，会盖在整页上面，打开时先收起来
	if hover_label != null:
		hover_label.visible = not pressed
	if pressed:
		_refresh_growth_panel()


func _on_growth_close() -> void:
	growth_panel.visible = false
	if hover_label != null:
		hover_label.visible = true
	if growth_button != null:
		growth_button.set_pressed_no_signal(false)


func _on_repair_toggled(pressed: bool) -> void:
	repair_mode = pressed
	if pressed:
		demolish_mode = false
		if demolish_button != null:
			demolish_button.set_pressed_no_signal(false)
		selected_slot = -1
		_refresh_hand_selection()
		board.set_placement(false, -1)
		hint_label.text = "修复模式：点你家受损的建筑/塔（每 20 点血 1 物资）"
	else:
		hint_label.text = ""


func _on_demolish_toggled(pressed: bool) -> void:
	demolish_mode = pressed
	if pressed:
		repair_mode = false
		if repair_button != null:
			repair_button.set_pressed_no_signal(false)
		selected_slot = -1
		_refresh_hand_selection()
		board.set_placement(false, -1)
		hint_label.text = "拆除模式：点你自己的建筑或塔"
	else:
		hint_label.text = ""


func _on_zoom_pressed(index: int) -> void:
	match index:
		0: board.zoom_by(1.25)
		1: board.zoom_by(0.8)
		_: board.reset_zoom()


# 手牌刷新：只做"数据 + 布局滑移"。出牌/抽牌的飞行由 _fly_ghost 负责。
func _refresh_hand() -> void:
	var hand: Array = snapshot.get("hand", [])
	var res: Array = snapshot.get("res", [0, 0, 0, 0, 0])
	var signature := str(hand) + "|" + str(res)
	if signature == hand_signature:
		return
	var previous: Array = hand_seen.duplicate()
	hand_seen = hand.duplicate()
	hand_signature = signature

	for slot in hand_buttons.size():
		var view = hand_buttons[slot]
		if slot >= hand.size():
			view.visible = false
			continue
		view.visible = true
		var id: int = hand[slot]
		if not cards.has(id):
			continue
		var info: Dictionary = cards[id]
		view.set_card(info)
		view.set_selected(slot == selected_slot)
		var affordable: bool = _can_afford(info.costs)
		var target := _slot_position(slot)
		var from_slot: int = previous.find(id)
		if from_slot >= 0 and from_slot != slot and from_slot < hand_buttons.size():
			# 老牌被挤到新槽位：从旧槽位滑过去
			view.position = _slot_position(from_slot)
			view.modulate = Color(1, 1, 1) if affordable else Color(0.6, 0.62, 0.7)
			_slide_hand_card(view, target, 0.13)
		elif from_slot < 0:
			# 新抽的牌：淡入 + 从右边滑进来
			view.position = target + Vector2(46, 0)
			view.modulate = Color(1, 1, 1, 0)
			var tween := create_tween().set_parallel(true)
			tween.tween_property(view, "position", target, 0.16).set_trans(Tween.TRANS_QUAD)
			tween.tween_property(view, "modulate",
				Color(1, 1, 1) if affordable else Color(0.6, 0.62, 0.7), 0.16)
		else:
			view.modulate = Color(1, 1, 1) if affordable else Color(0.6, 0.62, 0.7)
	if deck_pile != null:
		deck_pile.queue_redraw()


func _restore_hand_slot_soon(view: Control) -> void:
	# 残影飞出去的同时把源手牌藏起来；0.3 秒后无论牌堆有没有变都恢复显示
	await get_tree().create_timer(0.3).timeout
	if view != null and is_instance_valid(view):
		view.visible = true


func _slot_position(slot: int) -> Vector2:
	return Vector2(slot * 136, 0)


func _slide_hand_card(view: Control, target: Vector2, duration: float) -> void:
	var tween := create_tween()
	tween.tween_property(view, "position", target, duration).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)

# 牌堆：一小摞牌 + 剩余张数（牌组 6 张，手里 4 张，剩下 2 张在循环里）
func _draw_deck_pile() -> void:
	if deck_pile == null:
		return
	var hand_count := int(snapshot.get("hand", []).size())
	var remaining: int = max(0, deck_required - hand_count)
	# 像素风牌堆：三层错位卡背 + 金色菱形徽记，全部硬边
	for i in 3:
		var offset := Vector2(i * 3, -i * 3)
		var rect := Rect2(offset + Vector2(6, 2), Vector2(54, 60))
		deck_pile.draw_rect(rect, Color("#131a26"), true)
		deck_pile.draw_rect(Rect2(rect.position, rect.size), Color("#3d4f70"), false, 2.0)
		# 点阵纹理
		for y in range(int(rect.position.y) + 6, int(rect.position.y + rect.size.y) - 6, 4):
			for x in range(int(rect.position.x) + 6, int(rect.position.x + rect.size.x) - 6, 4):
				deck_pile.draw_rect(Rect2(x, y, 2, 2), Color("#222d42"), true)
	# 中央金色菱形
	var center := Vector2(33, 32)
	for step in range(0, 9):
		var half := 9 - step
		deck_pile.draw_rect(Rect2(center.x - half, center.y - 9 + step, half * 2 + 1, 1),
			Color("#f0c05a") if step < 3 else Color("#a88034"), true)
	deck_pile.draw_string(ThemeDB.fallback_font, Vector2(2, 70), "牌堆 %d" % remaining,
		HORIZONTAL_ALIGNMENT_LEFT, 80, 13, Color(0.75, 0.85, 1.0))

# ---- 飞行残影 --------------------------------------------------------------
# 出牌/弃牌不再让手牌节点自己飞（坐标系会乱），而是复制一张牌在屏幕空间飞过去，
# 手牌节点只负责"布局滑移"。这样位置一定对得上点击的地方。

func _spawn_ghost(card_id: int) -> Control:
	var ghost := CardViewScript.new()
	ghost.mouse_filter = Control.MOUSE_FILTER_IGNORE
	ghost.lift_amount = 0.0
	ghost.size = Vector2(132, 184)
	ghost.z_index = 100
	if cards.has(card_id):
		ghost.set_card(cards[card_id])
	add_child(ghost)
	return ghost


func _fly_ghost(ghost: Control, from: Vector2, to: Vector2, duration: float,
		end_scale: float, end_rotation: float, end_alpha: float) -> void:
	ghost.position = from
	ghost.pivot_offset = ghost.size * 0.5
	# 控制点抬高，走一条先上扬再落下的弧线
	var control := from.lerp(to, 0.5) + Vector2(0, -120)
	var tween := create_tween().set_parallel(true)
	tween.tween_method(func(t: float) -> void:
		var a := from.lerp(control, t)
		var b := control.lerp(to, t)
		ghost.position = a.lerp(b, t), 0.0, 1.0, duration).set_trans(Tween.TRANS_SINE)
	tween.tween_property(ghost, "scale", Vector2(end_scale, end_scale), duration)
	tween.tween_property(ghost, "rotation", end_rotation, duration)
	tween.tween_property(ghost, "modulate:a", end_alpha, duration)
	tween.finished.connect(func() -> void: ghost.queue_free())

func _on_hand_slot_discard(slot: int) -> void:
	if not match_active:
		return
	var hand: Array = snapshot.get("hand", [])
	if slot < 0 or slot >= hand.size():
		return
	var card_id := int(hand[slot])
	var view: Control = hand_buttons[slot]
	var from: Vector2 = hand_panel.position + view.position
	view.visible = false
	_restore_hand_slot_soon(view)
	var ghost := _spawn_ghost(card_id)
	_fly_ghost(ghost, from, deck_pile.position + Vector2(52, 34), 0.26, 0.45, -0.18, 0.0)
	_shake_deck()
	var payload := PackedByteArray()
	payload.append(slot)
	net.send(C2S_DISCARD, payload)


# 牌堆"抖一下"：抽牌/弃牌/洗牌时给个反馈
func _shake_deck() -> void:
	if deck_pile == null:
		return
	var base := deck_pile.position
	var tween := create_tween()
	tween.tween_property(deck_pile, "position", base + Vector2(5, -3), 0.05)
	tween.tween_property(deck_pile, "position", base + Vector2(-5, 2), 0.05)
	tween.tween_property(deck_pile, "position", base + Vector2(3, 1), 0.05)
	tween.tween_property(deck_pile, "position", base, 0.05)


func _on_hand_slot_pressed(slot: int) -> void:
	if not match_active:
		return
	if structure_panel != null:
		structure_panel.visible = false
	rally_mode = false
	if tower_build_mode:
		tower_build_mode = false
		if tower_button != null:
			tower_button.set_pressed_no_signal(false)
	if selected_slot == slot:
		selected_slot = -1
		board.set_placement(false, -1)
		hint_label.text = ""
	else:
		selected_slot = slot
		var hand: Array = snapshot.get("hand", [])
		var kind := 0
		var building := -1
		var radius := 0
		var spell := 0
		if slot < hand.size() and cards.has(hand[slot]):
			kind = int(cards[hand[slot]]["kind"])
			building = int(cards[hand[slot]].get("building", -1))
			radius = int(cards[hand[slot]].get("radius", 0))
			spell = int(cards[hand[slot]].get("spell", 0))
		board.set_placement(true, kind, building, radius, spell)
		if kind == 2:
			if building == 4:
				hint_label.text = "点地图放塔：自家领地内、不能和其它塔重叠"
			else:
				hint_label.text = "点地图放建筑：绿色格子才能放（不能重叠）"
		elif kind == 1:
			hint_label.text = "点地图放法术：格子显示作用范围"
		else:
			hint_label.text = "点地图投放部队（只能放基地周围）"
	_refresh_hand_selection()


func _refresh_hand_selection() -> void:
	for slot in hand_buttons.size():
		hand_buttons[slot].set_selected(slot == selected_slot)
		if selected_slot < 0:
			hint_label.text = ""


func _on_play_requested(world_pos: Vector2) -> void:
	if not match_active:
		return
	# 修复模式：花钱把自家受损建筑/塔补满
	if repair_mode:
		var target: Dictionary = board.pick_structure(world_pos)
		if target.is_empty():
			_show_toast("这里没有你的建筑/塔")
			return
		var payload := PackedByteArray()
		payload.append(int(target.kind))
		var ridx := int(target.index)
		payload.append((ridx >> 8) & 0xFF)
		payload.append(ridx & 0xFF)
		net.send(C2S_REPAIR, payload)
		return

	# 拆除模式：点自家建筑/塔就拆掉
	if demolish_mode:
		var target: Dictionary = board.pick_structure(world_pos)
		if target.is_empty():
			_show_toast("这里没有你的建筑")
			return
		var payload := PackedByteArray()
		payload.append(int(target.kind))
		var didx := int(target.index)
		payload.append((didx >> 8) & 0xFF)
		payload.append(didx & 0xFF)
		net.send(C2S_DEMOLISH, payload)
		_show_toast("拆除中…")
		return
	# 总部支援优先：进入瞄准模式后，点哪儿就打哪儿
	if hq_targeting:
		hq_targeting = false
		var payload := PackedByteArray()
		var hx := int(round(world_pos.x))
		var hy := int(round(world_pos.y))
		payload.append((hx >> 8) & 0xFF)
		payload.append(hx & 0xFF)
		payload.append((hy >> 8) & 0xFF)
		payload.append(hy & 0xFF)
		net.send(C2S_HQ, payload)
		audio.play("explosion")
		board.shake(1.0)  # 总部支援：整屏震一下
		hint_label.text = ""
		_refresh_hq_button()
		return
	if tower_build_mode:
		if not board.placement_ok_at(world_pos):
			_show_toast("这里不能造塔：要领地内的平地/道路，且不能和别的建筑重叠")
			return
		var tpay := PackedByteArray()
		var tx := int(round(world_pos.x))
		var ty := int(round(world_pos.y))
		tpay.append((tx >> 8) & 0xFF)
		tpay.append(tx & 0xFF)
		tpay.append((ty >> 8) & 0xFF)
		tpay.append(ty & 0xFF)
		net.send(C2S_BUILD_TOWER, tpay)
		return
	# 指定攻击区域：下一次点击地图就是这个兵营的目标点
	if rally_mode and rally_index >= 0:
		rally_mode = false
		var rpay := PackedByteArray()
		rpay.append(rally_index)
		var rx := int(round(world_pos.x))
		var ry := int(round(world_pos.y))
		rpay.append((rx >> 8) & 0xFF)
		rpay.append(rx & 0xFF)
		rpay.append((ry >> 8) & 0xFF)
		rpay.append(ry & 0xFF)
		net.send(C2S_SET_RALLY, rpay)
		hint_label.text = ""
		return
	# 没选牌、也没在任何模式里：点建筑/塔 = 打开详情面板
	if selected_slot < 0:
		var picked: Dictionary = board.pick_structure(world_pos)
		if not picked.is_empty():
			_open_structure(picked)
		return
	# 建筑先在本地判一次：地形不对/和别人重叠就别发给服务端了
	var hand_now: Array = snapshot.get("hand", [])
	if selected_slot < hand_now.size() and cards.has(hand_now[selected_slot]):
		var info: Dictionary = cards[hand_now[selected_slot]]
		if int(info.get("kind", 0)) == 2 and not board.placement_ok_at(world_pos):
			_show_toast("这里放不下：要平地/道路，且不能和已有建筑重叠")
			return
	_send_play(selected_slot, int(round(world_pos.x)), int(round(world_pos.y)))
	last_acked_slot = selected_slot
	selected_slot = -1
	board.set_placement(false, -1)
	hint_label.text = ""
	_refresh_hand_selection()


func _send_play(slot: int, x: int, y: int) -> void:
	var payload := PackedByteArray()
	payload.append(slot)
	payload.append((x >> 8) & 0xFF)
	payload.append(x & 0xFF)
	payload.append((y >> 8) & 0xFF)
	payload.append(y & 0xFF)
	net.send(C2S_PLAY, payload)
	# 出牌动画：复制一张牌从手牌位置飞到点击处（手牌节点自己不动）
	if slot >= 0 and slot < hand_buttons.size():
		var hand_now: Array = snapshot.get("hand", [])
		if slot < hand_now.size():
			var played_id := int(hand_now[slot])
			var view: Control = hand_buttons[slot]
			var from: Vector2 = hand_panel.position + view.position
			view.visible = false
			_restore_hand_slot_soon(view)
			var ghost := _spawn_ghost(played_id)
			var board_point: Vector2 = board.world_to_screen(Vector2(float(x), float(y)))
			_fly_ghost(ghost, from, board_point - Vector2(66, 92), 0.26, 0.55, 0.18, 0.0)


# --bot：演示用自动出牌（不会作弊，走的还是同一条 C2S_Play 通道）
# 鼠标靠近手牌时抽上来，离开后缩回去（只露上半部分）
func _update_hand_peek() -> void:
	if hand_panel == null or not hand_panel.visible:
		return
	var mouse := get_global_mouse_position()
	var peek_y := TOOLBAR_TOP - HAND_PEEK
	var full_y := TOOLBAR_TOP - CARD_HEIGHT
	var zone := Rect2(HAND_LEFT - 12.0, peek_y - 8.0, HAND_SPAN + 24.0,
		TOOLBAR_TOP - peek_y + 18.0)
	var want := zone.has_point(mouse)
	if want == hand_expanded:
		return
	hand_expanded = want
	var tween := create_tween().set_parallel(true)
	tween.tween_property(hand_panel, "position:y", full_y if want else peek_y, 0.14) \
		.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tween.tween_property(hand_panel, "size:y", CARD_HEIGHT if want else HAND_PEEK, 0.14)

func _process(delta: float) -> void:
	# 连不上就自动重试：服务端可能比客户端晚起来十几秒（WSL 里要编译）
	if reconnect_delay > 0.0:
		reconnect_delay -= delta
		if reconnect_delay <= 0.0:
			reconnect_delay = 0.0
			print("[client] 重试连接 %s:%d" % [server_host, server_port])
			_connect_server()

	# 手牌悬停展开
	_update_hand_peek()
	# 截图演示：把鼠标位置钉在基地旁边（headless 没有真实鼠标）
	if demo_hover_pos.x >= 0.0 and board != null:
		board.hover = demo_hover_pos
		board.queue_redraw()

	# 鼠标下的地块属性（水/林/山…），做"每个砖块说明自己的效果"
	if hover_label != null and match_active and board != null:
		var info: String = board.tile_info_at(board.hover)
		hover_label.text = ("地形：" + info) if info != "" else ""

	if not bot_mode or not match_active:
		return
	bot_timer -= delta
	if bot_timer > 0.0:
		return
	bot_timer = 1.0

	var hand: Array = snapshot.get("hand", [])
	var my_buildings := 0
	for b in snapshot.get("buildings", []):
		if b.owner == my_player:
			my_buildings += 1

	# 前 3 个建筑优先：这样演示里能看到经济区，而不是一上来就送兵
	var choice := -1
	if my_buildings < 3:
		for slot in hand.size():
			var id: int = hand[slot]
			if cards.has(id) and int(cards[id]["kind"]) == 2 and _can_afford(cards[id]["costs"]):
				choice = slot
				break
	if choice < 0:
		var affordable := []
		for slot in hand.size():
			var id: int = hand[slot]
			if cards.has(id) and _can_afford(cards[id]["costs"]):
				affordable.append(slot)
		if affordable.is_empty():
			return
		choice = affordable[randi() % affordable.size()]

	var base := Vector2(280, 280)
	if board.bases.size() > my_player:
		base = board.bases[my_player]
	var angle := randf() * TAU
	var distance := randf_range(120.0, 380.0)
	var x := int(clampf(base.x + cos(angle) * distance, 90.0, 2310.0))
	var y := int(clampf(base.y + sin(angle) * distance, 90.0, 1510.0))
	_send_play(choice, x, y)


func _on_match_over(payload: PackedByteArray) -> void:
	var buffer := _reader(payload)
	var winner := buffer.get_u8()
	var reason := buffer.get_u8()
	var bases := []
	for i in 4:
		bases.append(buffer.get_u16())

	match_active = false
	board.set_placement(false, -1)
	selected_slot = -1
	result_panel.visible = true
	if winner == 255:
		audio.play("defeat")
	elif winner == my_player:
		audio.play("victory")
	else:
		audio.play("defeat")

	var reason_text := "基地被摧毁" if reason == 1 else "时间到，按基地血量判定"
	var headline := "平局"
	if winner != 255:
		headline = "胜利" if winner == my_player else "失败 · %s 获胜" % PLAYER_NAMES[winner]

	var parts := []
	for p in 4:
		parts.append("%s %d" % [PLAYER_NAMES[p], bases[p]])
	result_label.text = headline
	result_detail.text = reason_text
	result_bases.text = "    ".join(parts)
	status_label.text = "对局结束"


# 一键选推荐卡组：给第一次玩的人一个"能打、能运营、有技能"的起点
func _on_clear_deck() -> void:
	selected_cards.clear()
	audio.play("click")
	_refresh_deck_selection()


func _on_apply_starter() -> void:
	if cards.is_empty():
		return
	# 从自己阵营的池子里挑 3 兵 + 2 建筑 + 1 法术（各阵营池子结构一致）
	var units := []
	var buildings := []
	var spells := []
	for id in cards.keys():
		var info: Dictionary = cards[id]
		if int(info.alliance) != my_alliance:
			continue
		match int(info.kind):
			0: units.append(id)
			1: spells.append(id)
			2: buildings.append(id)

	# 推荐卡组：4 建筑 + 防御塔 + 工程站 + 2 法术（正好 8 张）
	var picks: Array = []
	picks.append_array(buildings.slice(0, 4))
	picks.append_array(spells.slice(0, 2))
	# 补到卡组上限：用卡池里剩下的建筑/法术凑满
	for id in cards.keys():
		if picks.size() >= deck_required:
			break
		if int(cards[id].alliance) != my_alliance:
			continue
		if picks.has(id):
			continue
		# 优先补建筑，其次法术；同卡最多 2 张
		var kind_value := int(cards[id].kind)
		if kind_value != 2 and kind_value != 1:
			continue
		var already := 0
		for picked in picks:
			if int(picked) == int(id):
				already += 1
		if already >= 2:
			continue
		picks.append(id)
	picks = picks.slice(0, deck_required)

	selected_cards.clear()
	for id in picks:
		if card_buttons.has(id):
			selected_cards.append(id)
	_refresh_deck_selection()


# 同一副卡组直接再开一局（服务端 C2S_Rematch）
func _on_direct_rematch() -> void:
	result_panel.visible = false
	_set_hud_visible(true)
	status_label.text = "再来一局…"
	net.send(C2S_REMATCH, PackedByteArray())


func _on_rematch() -> void:
	result_panel.visible = false
	deck_panel.visible = true
	_set_hud_visible(false)
	status_label.text = "重新构筑卡组"
	selected_cards.clear()
	_refresh_deck_selection()


# 自检：走一遍「选牌 → 鼠标点击 → 服务端回执」的真实链路（合成一个鼠标事件）
func _run_selftest() -> void:
	await get_tree().create_timer(1.0).timeout
	if not match_active:
		print("[selftest] 对局没开始，跳过")
		return
	_on_hand_slot_pressed(0)
	# 点在自己基地旁边（降落点是随机的，不能写死坐标）
	var base := Vector2(280, 280)
	if board.bases.size() > my_player:
		base = board.bases[my_player]
	var screen_position: Vector2 = board.world_to_screen(base + Vector2(120, 120))
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = true
	event.position = screen_position
	Input.parse_input_event(event)
	print("[selftest] 已合成点击 %s，等回执…" % str(screen_position))


func _play_result_text(result: int) -> String:
	match result:
		2: return "手牌序号不对"
		3: return "资源不够"
		4: return "不能放在这里"
		5: return "单位太多了"
		6: return "建筑已满"
		7: return "总部支援还没解锁"
		8: return "总部支援冷却中"
		_: return "出牌失败"


# ---------------------------------------------------------------- 杂项

func _show_toast(message: String) -> void:
	toast_label.text = message
	var tween := create_tween()
	toast_label.modulate = Color(1, 1, 1, 1)
	tween.tween_interval(1.2)
	tween.tween_property(toast_label, "modulate", Color(1, 1, 1, 0), 0.6)


func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode >= KEY_1 and event.keycode <= KEY_4:
			_on_hand_slot_pressed(event.keycode - KEY_1)
		elif event.keycode == KEY_ESCAPE:
			selected_slot = -1
			board.set_placement(false, -1)
			_refresh_hand_selection()
