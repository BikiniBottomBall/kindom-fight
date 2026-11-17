# 战场渲染 + 点选落点。
#
# 这个文件只做「把服务端给的快照画出来」和「把点击位置换算成世界坐标」。
# 它不知道一张牌能不能出、也不知道谁赢 —— 那是服务端的事（ADR 护栏二）。
#
# 地图是 2400×1600 的世界坐标，窗口只有 1600×900，所以这里用 draw_set_transform
# 做一次统一的缩放 + 居中，鼠标坐标用同一个公式反算回去。
extends Node2D

const Art = preload("res://scripts/art.gd")

signal play_requested(world_pos: Vector2)

const FIELD_W := 4800.0
const FIELD_H := 3200.0
const VIEW_W := 1600.0
const VIEW_H := 900.0
const TILE := 20.0
const TOWER_RANGE := 280.0   # 和服务器 kTowerRange 保持一致
const kTickHz := 20.0
const PLACE_RADIUS := 640.0

const PLAYER_COLORS := [
	Color("#4a9eff"),  # P0 蓝
	Color("#ff6b6b"),  # P1 红
	Color("#4ade80"),  # P2 绿
	Color("#fbbf24"),  # P3 黄
]
const PLAYER_NAMES := ["我", "P1", "P2", "P3"]

# 地形配色（和 src/rts/terrain.h 的 Tile 顺序一致）
const TILE_COLORS := [
	Color("#1b2436"),  # Plain
	Color("#2d3b55"),  # Road
	Color("#1f3a2a"),  # Forest
	Color("#3d3520"),  # Hill
	Color("#16304a"),  # Water
	Color("#2b2b36"),  # Rock
]

const UNIT_RADIUS := [7.0, 7.0, 10.0, 8.0, 7.0, 12.0, 15.0]
const UNIT_NAMES := ["militia", "spear", "shield", "rider", "archer", "siege", "behemoth", "engineer"]
const BUILDING_NAMES := ["depot", "reactor", "barracks", "lab", "tower", "workshop"]

var snapshot: Dictionary = {}
var my_player := 0
var selected_kind := -1  # 0 = 单位/建筑（只能放基地周围），1 = 法术（随便放）
var placement_active := false
var font: Font
var hover := Vector2(-1, -1)
var preview_radius := 0      # 建筑占地半径 / 法术作用半径（像素）
var preview_spell := 0       # 0 无 / 1 火 / 2 治疗 / 3 冰 / 4 集结 / 5 开路
var forced_hover := Vector2(-1, -1)   # 截图演示用：钉住预览位置
var territory_mask := PackedByteArray()   # 60×40，每格 = 4×4 地板格（服务器下发）
var show_territory := false                # 工具栏「领地」按钮：把自己的领地铺在地图上
const MASK_COLS := 60
const MASK_ROWS := 40
const FOOTPRINT_TILES := 4

var tiles := PackedByteArray()
var cols := 30
var rows := 20
var bases: Array = []        # Vector2 × 4
var base_max_hp: Array = []  # 每家的基地满血（先锋军团更高）
var alliances: Array = []    # 每家的联盟编号

var _scale := 0.5625
var _offset := Vector2.ZERO
# 镜头：整张图塞进窗口的"适配比例" × 缩放倍数，再按中心点平移。
# 大地图如果一直用适配比例，单位会小到看不清 —— 所以默认放大，靠拖动看全局。
var _fit_scale := 0.3
const MIN_ZOOM := 1.0   # 最小缩放：1.0 = 整张地图刚好铺满视野（可以看全图）
const MAX_ZOOM := 4.0
var _zoom := 2.6
var _center := Vector2(1800, 1200)
var _view_rect := Rect2(12, 56, 1576, 724)
var _dragging := false
# 两个快照之间的插值进度（0~1）：服务端 20Hz，屏幕 60Hz
var _prev_units: Array = []
var _interp := 1.0
# 美术资源（没有就用程序画的形状兜底）
var _unit_art := {}
var _building_art := {}
var _node_art := {}
var _base_art: Texture2D = null
var _terrain_tex: ImageTexture = null
# 战斗反馈
var _fx: Array = []
var _shake := 0.0
var _prev_buildings := {}
var _prev_towers := {}
var _prev_base_hp := {}


func _ready() -> void:
	# 顶部留血条、底部留手牌，地图画在中间那块里（否则大地图会顶到 HUD 底下）
	const top := 10.0        # 顶部不再有血条
	# 底部让出「工具栏 + 手牌常驻露出的那一截」，这样不悬停时地图完全不会被手牌挡住
	const bottom := 120.0 + 72.0
	const available_w := VIEW_W - 24.0
	const available_h := VIEW_H - top - bottom
	_view_rect = Rect2(12.0, top, available_w, available_h)
	_fit_scale = min(available_w / FIELD_W, available_h / FIELD_H)
	_center = Vector2(FIELD_W * 0.5, FIELD_H * 0.5)
	_update_camera()
	set_process(true)
	_unit_art = Art.load_named("units", UNIT_NAMES)
	_building_art = Art.load_named("buildings", BUILDING_NAMES)
	_node_art = Art.load_named("nodes", ["materiel", "energy", "rich"])
	_base_art = Art.load_texture("base")
	print("[board] 美术：单位%d 建筑%d 资源点%d 基地%s" % [
		_unit_art.size(), _building_art.size(), _node_art.size(),
		"有" if _base_art != null else "无"])


# 把镜头对准一个世界坐标（开局对准自己的基地，不然你连自己在哪都看不见）
func focus_on(point: Vector2) -> void:
	_center = point
	_update_camera()
	queue_redraw()


func zoom_by(factor: float) -> void:
	var center_before := screen_to_world(_view_rect.position + _view_rect.size * 0.5)
	_zoom = clampf(_zoom * factor, MIN_ZOOM, MAX_ZOOM)
	_update_camera()
	_center += center_before - screen_to_world(_view_rect.position + _view_rect.size * 0.5)
	_update_camera()


func reset_zoom() -> void:
	_zoom = MIN_ZOOM
	_center = Vector2(FIELD_W * 0.5, FIELD_H * 0.5)
	_update_camera()


func _update_camera() -> void:
	_scale = _fit_scale * _zoom
	var view_center := _view_rect.position + _view_rect.size * 0.5
	var half := (_view_rect.size * 0.5) / _scale
	if half.x * 2.0 >= FIELD_W:
		_center.x = FIELD_W * 0.5
	else:
		_center.x = clampf(_center.x, half.x, FIELD_W - half.x)
	if half.y * 2.0 >= FIELD_H:
		_center.y = FIELD_H * 0.5
	else:
		_center.y = clampf(_center.y, half.y, FIELD_H - half.y)
	_offset = view_center - _center * _scale
	queue_redraw()


func set_map(map_cols: int, map_rows: int, map_tiles: PackedByteArray, base_positions: Array,
		base_max: Array, map_alliances: Array) -> void:
	cols = map_cols
	rows = map_rows
	tiles = map_tiles
	bases = base_positions
	base_max_hp = base_max
	alliances = map_alliances
	_build_terrain_texture()
	queue_redraw()


func set_snapshot(value: Dictionary) -> void:
	# 服务端 20Hz、屏幕 60Hz：不插值的话单位是"跳"着走的，看起来像快进。
	_prev_units = snapshot.get("units", [])
	_detect_fx(value)
	snapshot = value
	_interp = 0.0
	queue_redraw()


# ---- 战斗反馈 --------------------------------------------------------------
# 客户端自己对比前后两帧快照，做出"受击闪白 / 建造完成 / 建筑爆炸"这些表现。

func _detect_fx(next: Dictionary) -> void:
	var previous := snapshot
	if previous.is_empty():
		_prev_buildings.clear()
		_prev_towers.clear()
		_prev_base_hp.clear()
		return

	var old_units := {}
	for unit in previous.get("units", []):
		old_units[int(unit.id)] = int(unit.hp)
	for unit in next.get("units", []):
		var key := int(unit.id)
		if not old_units.has(key):
			continue
		if int(unit.hp) < int(old_units[key]):
			_spawn_fx(Vector2(unit.x, unit.y), 0.32, 0)

	for building in next.get("buildings", []):
		var bkey := "%d:%d:%d" % [int(building.owner), int(building.x), int(building.y)]
		var hp := int(building.hp)
		var progress := int(building.get("progress", 100))
		if _prev_buildings.has(bkey):
			var old: Dictionary = _prev_buildings[bkey]
			if hp < int(old.hp):
				_spawn_fx(Vector2(building.x, building.y), 0.35, 0)
			if int(old.progress) < 100 and progress >= 100:
				_spawn_fx(Vector2(building.x, building.y), 0.6, 1)
		_prev_buildings[bkey] = {"hp": hp, "progress": progress}

	for tower in next.get("towers", []):
		var tkey := "%d:%d" % [int(tower.owner), int(tower.id)]
		if _prev_towers.has(tkey) and int(tower.hp) < int(_prev_towers[tkey]):
			_spawn_fx(Vector2(tower.x, tower.y), 0.35, 0)
		_prev_towers[tkey] = int(tower.hp)

	var bases_next: Array = next.get("bases", [])
	for p in bases_next.size():
		var hp := int(bases_next[p])
		if _prev_base_hp.has(p) and hp < int(_prev_base_hp[p]) and p < bases.size():
			_spawn_fx(bases[p], 0.7, 2)
		_prev_base_hp[p] = hp


func _spawn_fx(pos: Vector2, life: float, kind: int) -> void:
	_fx.append({"pos": pos, "ttl": life, "max": life, "kind": kind})


func shake(amount: float) -> void:
	_shake = max(_shake, amount)


func _draw_fx() -> void:
	for fx in _fx:
		var t: float = 1.0 - float(fx.ttl) / float(fx.max)
		var alpha: float = clampf(float(fx.ttl) / float(fx.max), 0.0, 1.0)
		var pos: Vector2 = fx.pos
		match int(fx.kind):
			0:
				var radius := lerpf(5.0, 20.0, t)
				draw_arc(pos, radius, 0.0, TAU, 18, Color(1.0, 0.92, 0.7, alpha * 0.9), 2.0, true)
			1:
				var radius1 := lerpf(10.0, 48.0, t)
				draw_arc(pos, radius1, 0.0, TAU, 32, Color(0.55, 0.95, 1.0, alpha), 3.0, true)
				draw_arc(pos, radius1 * 0.6, 0.0, TAU, 24, Color(0.85, 1.0, 1.0, alpha * 0.8), 2.0, true)
			2:
				var radius2 := lerpf(20.0, 120.0, t)
				draw_arc(pos, radius2, 0.0, TAU, 40, Color(1.0, 0.55, 0.25, alpha), 4.0, true)
				draw_circle(pos, lerpf(8.0, 30.0, t), Color(1.0, 0.7, 0.35, alpha * 0.25))


var placement_building := -1   # 正在放置的建筑类型（4 = 防御塔，用来画攻击范围）

func pick_structure(world_point: Vector2) -> Dictionary:
	var best := {}
	var best_distance := 70.0 * 70.0
	var building_index := 0
	for b in snapshot.get("buildings", []):
		if int(b.owner) != my_player:
			continue
		var pos := Vector2(b.x, b.y)
		var d := pos.distance_squared_to(world_point)
		if d < best_distance:
			best_distance = d
			best = {"kind": 0, "index": building_index}
		building_index += 1
	for t in snapshot.get("towers", []):
		if int(t.owner) != my_player:
			continue
		var pos2 := Vector2(t.x, t.y)
		var d2 := pos2.distance_squared_to(world_point)
		if d2 < best_distance:
			best_distance = d2
			best = {"kind": 1, "index": int(t.get("id", 0))}
	return best


func set_placement(active: bool, kind: int, building: int = -1,
		radius: int = 0, spell: int = 0) -> void:
	placement_active = active
	selected_kind = kind
	placement_building = building if active else -1
	preview_radius = radius if active else 0
	preview_spell = spell if active else 0
	queue_redraw()


# 建筑能不能放在这个点：地形可建 + 不和已有建筑/塔/基地重叠
func placement_ok_at(point: Vector2) -> bool:
	if cols <= 0 or rows <= 0:
		return false
	var is_tower := placement_building == 4
	return _building_ok_at(point, is_tower, is_tower)


func set_territory_mask(bytes: PackedByteArray) -> void:
	territory_mask = bytes


func _cell_owned(col: int, row: int) -> bool:
	if col < 0 or col >= MASK_COLS or row < 0 or row >= MASK_ROWS:
		return false
	var index := row * MASK_COLS + col
	if index >= territory_mask.size():
		return false
	return territory_mask[index] != 0


# 和服务器同一个算法：4×4 对齐块，整块合法才算能放
func footprint_origin(point: Vector2) -> Vector2i:
	var col := int(point.x / TILE) - (FOOTPRINT_TILES / 2 - 1)
	var row := int(point.y / TILE) - (FOOTPRINT_TILES / 2 - 1)
	return Vector2i(clampi(col, 0, cols - FOOTPRINT_TILES), clampi(row, 0, rows - FOOTPRINT_TILES))


func _block_of_structure(point: Vector2) -> Rect2i:
	var origin := footprint_origin(point)
	return Rect2i(origin.x, origin.y, FOOTPRINT_TILES, FOOTPRINT_TILES)


func _building_ok_at(point: Vector2, needs_territory: bool, check_cap: bool) -> bool:
	var origin := footprint_origin(point)
	# 1) 16 格地形都得能建
	for dr in FOOTPRINT_TILES:
		for dc in FOOTPRINT_TILES:
			if not _tile_buildable(origin.x + dc, origin.y + dr):
				return false
	# 2) 塔必须"这一块里至少 6 格是自己的"（掩码已经按这个规则烘过）
	if needs_territory:
		var cell_col := origin.x / FOOTPRINT_TILES
		var cell_row := origin.y / FOOTPRINT_TILES
		if not _cell_owned(cell_col, cell_row):
			return false
	# 3) 不与已有建筑/塔的块相交
	var mine := Rect2i(origin.x, origin.y, FOOTPRINT_TILES, FOOTPRINT_TILES)
	for building in snapshot.get("buildings", []):
		if mine.intersects(_block_of_structure(Vector2(building.x, building.y))):
			return false
	for tower in snapshot.get("towers", []):
		if mine.intersects(_block_of_structure(Vector2(tower.x, tower.y))):
			return false
	# 4) 基地留空（6×6 格）
	for base in bases:
		var base_origin := Vector2i(int((base as Vector2).x / TILE) - 3, int((base as Vector2).y / TILE) - 3)
		if mine.intersects(Rect2i(base_origin.x, base_origin.y, 6, 6)):
			return false
	# 5) 塔数量上限
	if check_cap:
		var tower_count := 0
		for tower in snapshot.get("towers", []):
			if int(tower.owner) == my_player:
				tower_count += 1
		if tower_count >= int(snapshot.get("tower_cap", 0)):
			return false
	return true


func _tile_buildable(col: int, row: int) -> bool:
	if col < 0 or col >= cols or row < 0 or row >= rows:
		return false
	var value: int = int(tiles[row * cols + col])
	# 0 平地 / 1 道路可建；森林、丘陵、水、岩石都不行（和服务器一致）
	return value == 0 or value == 1


func tile_info_at(point: Vector2) -> String:
	if cols <= 0 or rows <= 0 or tiles.size() < cols * rows:
		return ""
	var col := int(point.x / TILE)
	var row := int(point.y / TILE)
	if col < 0 or col >= cols or row < 0 or row >= rows:
		return ""
	const NAMES := [
		"平地 · 无影响",
		"道路 · 移速 +25%",
		"森林 · 移速 -30%",
		"高地 · 伤害 +25% / 移速 -15%",
		"水域 · 不可通行",
		"岩石 · 不可通行",
	]
	var tile := int(tiles[row * cols + col])
	return NAMES[tile] if tile < NAMES.size() else ""


func screen_to_world(point: Vector2) -> Vector2:
	return (point - _offset) / _scale


func world_to_screen(point: Vector2) -> Vector2:
	return point * _scale + _offset


func _process(delta: float) -> void:
	# 视角只靠"右键拖动 / 滚轮缩放 / 点小地图"，不再用 WASD 平移

	# 插值推进：一个快照周期（50ms）里把 _interp 从 0 推到 1
	if _interp < 1.0:
		_interp = minf(1.0, _interp + delta * float(kTickHz))
		queue_redraw()

	# 特效与震屏都按时间衰减
	if not _fx.is_empty():
		for fx in _fx:
			fx.ttl = float(fx.ttl) - delta
		_fx = _fx.filter(func(item): return float(item.ttl) > 0.0)
		queue_redraw()
	if _shake > 0.0:
		_shake = maxf(0.0, _shake - delta * 2.2)
		queue_redraw()

	var mouse := forced_hover if forced_hover.x >= 0.0 else screen_to_world(get_global_mouse_position())
	if mouse != hover:
		hover = mouse
		queue_redraw()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	match key.keycode:
		KEY_EQUAL, KEY_KP_ADD:
			zoom_by(1.2)
		KEY_MINUS, KEY_KP_SUBTRACT:
			zoom_by(0.83)
		KEY_0:
			reset_zoom()
		_:
			return
	get_viewport().set_input_as_handled()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		# 滚轮缩放（以光标为中心）
		if mb.pressed and (mb.button_index == MOUSE_BUTTON_WHEEL_UP or
				mb.button_index == MOUSE_BUTTON_WHEEL_DOWN):
			var before := screen_to_world(mb.position)
			var factor := 1.15 if mb.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.15
			_zoom = clampf(_zoom * factor, MIN_ZOOM, MAX_ZOOM)
			_update_camera()
			_center += before - screen_to_world(mb.position)
			_update_camera()
			get_viewport().set_input_as_handled()
			return
		# 右键 / 中键拖动视角
		if mb.button_index == MOUSE_BUTTON_RIGHT or mb.button_index == MOUSE_BUTTON_MIDDLE:
			_dragging = mb.pressed
			get_viewport().set_input_as_handled()
			return
		if mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
			if placement_active:
				play_requested.emit(screen_to_world(mb.position))
				get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and _dragging:
		var mm := event as InputEventMouseMotion
		_center -= mm.relative / _scale
		_update_camera()
		get_viewport().set_input_as_handled()


# ---------------------------------------------------------------- 绘制

func _draw() -> void:
	# 地图是 3:2，窗口是 16:9，两侧会有留白 —— 先铺一层深色底，别露出默认灰
	draw_rect(Rect2(0, 0, VIEW_W, VIEW_H), Color("#0a0d14"), true)
	# 整个世界坐标缩放进 1600×900 的窗口；鼠标换算用同一个 _scale / _offset
	var shake_offset := Vector2.ZERO
	if _shake > 0.001:
		var seconds := Time.get_ticks_msec() * 0.001
		var amp := 16.0 * _shake
		shake_offset = Vector2(sin(seconds * 47.0) * amp, cos(seconds * 39.0) * amp)
	draw_set_transform(_offset + shake_offset, 0.0, Vector2(_scale, _scale))
	_draw_terrain()
	_draw_nodes()
	_draw_placement()
	_draw_bases()
	_draw_towers()
	_draw_buildings()
	_draw_units()
	_draw_fx()
	_draw_effects()

	# 夜晚：整张地图压一层冷色，配合"视野 -15%"的规则
	var day_phase := int(snapshot.get("day_phase", 0))
	if day_phase >= 55:
		draw_rect(Rect2(0, 0, FIELD_W, FIELD_H), Color(0.04, 0.07, 0.18, 0.30), true)


	# 镜头放大后，世界会画到 HUD 底下。
	# 这里补四块底色把它盖住：视野区只有 _view_rect 那么大。
	# 注意：必须先把变换重置回屏幕坐标（以前是小地图函数顺手做的，删小地图时要自己补上）。
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	var background := Color("#0a0d14")
	draw_rect(Rect2(0, 0, VIEW_W, _view_rect.position.y), background, true)
	draw_rect(Rect2(0, _view_rect.position.y + _view_rect.size.y, VIEW_W,
			VIEW_H - _view_rect.position.y - _view_rect.size.y), background, true)
	draw_rect(Rect2(0, 0, _view_rect.position.x, VIEW_H), background, true)
	draw_rect(Rect2(_view_rect.position.x + _view_rect.size.x, 0,
			VIEW_W - _view_rect.position.x - _view_rect.size.x, VIEW_H), background, true)


func _draw_terrain() -> void:
	# 砖块现在 20 像素，一张图 240×160 = 38400 格，逐格 draw_rect 会掉帧。
	# 所以整张地形烘成一张贴图（每格 1 像素，Nearest 放大），只有 1 次绘制调用。
	if _terrain_tex != null:
		draw_texture_rect(_terrain_tex, Rect2(0, 0, FIELD_W, FIELD_H), false)
	else:
		draw_rect(Rect2(0, 0, FIELD_W, FIELD_H), TILE_COLORS[0], true)

	# 领地可视化：把自家领地按 4×4 格铺成半透明绿色
	if show_territory and territory_mask.size() >= 60 * 40:
		for cell_row in 40:
			for cell_col in 60:
				if territory_mask[cell_row * 60 + cell_col] == 0:
					continue
				var cell_rect := Rect2(cell_col * FOOTPRINT_TILES * TILE,
					cell_row * FOOTPRINT_TILES * TILE,
					FOOTPRINT_TILES * TILE, FOOTPRINT_TILES * TILE)
				draw_rect(cell_rect, Color(0.30, 1.0, 0.65, 0.18), true)
				draw_rect(cell_rect, Color(0.45, 1.0, 0.75, 0.45), false, 1.5)

	# 地图边框
	draw_rect(Rect2(0, 0, FIELD_W, FIELD_H), Color(0.35, 0.45, 0.7, 0.5), false, 4.0)


# 把地形烘成一张贴图：每格 TILE_BAKE 像素（10×10），有砖块美术就贴美术，没有就用纯色。
# 240×160 格 → 2400×1600 的贴图，1 次 draw call；比逐格绘制快几百倍。
const TILE_ART_NAMES := ["plain", "road", "forest", "hill", "water", "rock"]
const TILE_BAKE := 10


func _build_terrain_texture() -> void:
	_terrain_tex = null
	if tiles.is_empty() or cols <= 0 or rows <= 0:
		return

	var art := Art.load_named("tiles", TILE_ART_NAMES)
	var sources := []
	for name in TILE_ART_NAMES:
		if art.has(name):
			var image: Image = (art[name] as Texture2D).get_image()
			image.convert(Image.FORMAT_RGBA8)
			sources.append(image)
		else:
			sources.append(null)

	var width := cols * TILE_BAKE
	var height := rows * TILE_BAKE
	var canvas := Image.create(width, height, false, Image.FORMAT_RGBA8)

	for row in rows:
		for col in cols:
			var tile: int = tiles[row * cols + col]
			var color: Color = TILE_COLORS[tile] if tile < TILE_COLORS.size() else TILE_COLORS[0]
			canvas.fill_rect(Rect2i(col * TILE_BAKE, row * TILE_BAKE, TILE_BAKE, TILE_BAKE), color)

	# 有砖块美术就盖上去（先按格尺寸缩好，避免每格都 resize）
	var baked := []
	for source in sources:
		if source != null:
			var scaled: Image = (source as Image).duplicate()
			scaled.resize(TILE_BAKE, TILE_BAKE, Image.INTERPOLATE_NEAREST)
			baked.append(scaled)
		else:
			baked.append(null)

	var any_art := false
	for baked_image in baked:
		if baked_image != null:
			any_art = true
	if any_art:
		for row in rows:
			for col in cols:
				var tile: int = tiles[row * cols + col]
				var piece = baked[tile] if tile < baked.size() else null
				if piece == null:
					continue
				canvas.blit_rect(piece, Rect2i(0, 0, TILE_BAKE, TILE_BAKE),
					Vector2i(col * TILE_BAKE, row * TILE_BAKE))

	_terrain_tex = ImageTexture.create_from_image(canvas)

func _draw_placement() -> void:
	if not placement_active:
		return

	# 法术（CardKind.Spell = 1）：按作用半径铺格子
	if selected_kind == 1:
		var spell_radius := maxf(float(preview_radius), 120.0)
		var color := Color("#ff8a5c")
		match preview_spell:
			2: color = Color("#6fe39a")
			3: color = Color("#6fd4ff")
			4: color = Color("#f0c05a")
			5: color = Color("#c9a227")
		_draw_area_tiles(hover, spell_radius, color, true)
		draw_arc(hover, spell_radius, 0.0, TAU, 64, Color(color.r, color.g, color.b, 0.8), 2.0, true)
		return

	# 建筑（CardKind.Building = 2）：固定 4×4 格方块，整块同色（绿=能放 / 红=不能放）
	if selected_kind == 2:
		var origin := footprint_origin(hover)
		var ok := _building_ok_at(hover, placement_building == 4, placement_building == 4)
		var fill := Color("#67e08a") if ok else Color("#ff6b6b")
		for dr in FOOTPRINT_TILES:
			for dc in FOOTPRINT_TILES:
				var rect := Rect2((origin.x + dc) * TILE, (origin.y + dr) * TILE, TILE - 1, TILE - 1)
				draw_rect(rect, Color(fill.r, fill.g, fill.b, 0.24), true)
				draw_rect(rect, Color(fill.r, fill.g, fill.b, 0.45), false, 1.0)
		# 整块外框加粗，形状一眼看出来是固定方块
		draw_rect(Rect2(origin.x * TILE, origin.y * TILE,
			FOOTPRINT_TILES * TILE, FOOTPRINT_TILES * TILE),
			Color(fill.r, fill.g, fill.b, 0.9), false, 2.0)
		if placement_building == 4:  # 防御塔：额外画射程
			draw_circle(hover, TOWER_RANGE, Color(0.45, 0.85, 1.0, 0.07))
			draw_arc(hover, TOWER_RANGE, 0.0, TAU, 64, Color(0.45, 0.85, 1.0, 0.55), 2.0, true)
		return

	# 单位：显示基地可投放半径
	if bases.size() > my_player:
		var base: Vector2 = bases[my_player]
		draw_circle(base, PLACE_RADIUS, Color(0.29, 0.62, 1.0, 0.08))
		draw_arc(base, PLACE_RADIUS, 0.0, TAU, 72, Color(0.29, 0.62, 1.0, 0.5), 3.0, true)
	if hover.x >= 0 and hover.y >= 0:
		draw_circle(hover, 26.0, Color(0.35, 1.0, 0.55, 0.15))
		draw_arc(hover, 26.0, 0.0, TAU, 32, Color(0.35, 1.0, 0.55, 0.8), 3.0, true)


# 以鼠标为圆心，把覆盖到的地板格子逐格画出来
# area_only = true：只表示"作用范围"（法术）；false：逐格判定能不能建
func _draw_area_tiles(center: Vector2, radius: float, color: Color, area_only: bool) -> void:
	if center.x < 0 or center.y < 0:
		return
	var r2 := radius * radius
	var c0 := int((center.x - radius) / TILE) - 1
	var c1 := int((center.x + radius) / TILE) + 1
	var r0 := int((center.y - radius) / TILE) - 1
	var r1 := int((center.y + radius) / TILE) + 1
	for row in range(maxi(r0, 0), mini(r1, rows - 1) + 1):
		for col in range(maxi(c0, 0), mini(c1, cols - 1) + 1):
			var tile_center := Vector2((col + 0.5) * TILE, (row + 0.5) * TILE)
			if tile_center.distance_squared_to(center) > r2:
				continue
			var ok := true
			if not area_only:
				ok = _tile_buildable(col, row) and placement_ok_at(tile_center)
			var fill := color
			var alpha := 0.12 if area_only else (0.22 if ok else 0.18)
			if not area_only and not ok:
				fill = Color("#ff6b6b")
			draw_rect(Rect2(col * TILE, row * TILE, TILE - 1, TILE - 1),
				Color(fill.r, fill.g, fill.b, alpha), true)
			draw_rect(Rect2(col * TILE, row * TILE, TILE - 1, TILE - 1),
				Color(fill.r, fill.g, fill.b, alpha + 0.25), false, 1.0)

func _draw_nodes() -> void:
	var nodes: Array = snapshot.get("nodes", [])
	for node in nodes:
		var pos := Vector2(node.x, node.y)
		var flags := int(node.get("flags", 1))
		var active := (flags & 1) != 0
		var surging := (flags & 2) != 0
		var kind := int(node.kind)
		var zone_radius := float(node.get("radius", 160))
		var owner := int(node.get("owner", -1))
		var color := Color("#f0b45a")
		match kind:
			0: color = Color("#f0b45a")   # 矿脉片区：物资
			1: color = Color("#5eead4")   # 能源井片区
			2: color = Color("#ffd166")   # 高资源片区
			3: color = Color("#c9a8ff")   # 城镇据点
		if not active:
			color = color.darkened(0.55)

		# 片区范围：一大圈，归属了就用归属色描边
		draw_circle(pos, zone_radius, Color(color.r, color.g, color.b, 0.09))
		var outline: Color = PLAYER_COLORS[owner] if owner >= 0 and owner < 4 else color
		draw_arc(pos, zone_radius, 0.0, TAU, 64, Color(outline.r, outline.g, outline.b, 0.8), 2.5)
		# 战略点名字：屏幕字号恒定（世界坐标里按缩放反算），远看也能认出来
		if kind >= 2 and kind < 4:
			var label: String = ["", "", "高资源片区", "城市据点"][kind]
			var label_size := int(15.0 / maxf(_scale, 0.05))
			draw_string(ThemeDB.fallback_font, pos + Vector2(-zone_radius * 0.5, zone_radius + label_size * 1.4),
				label, HORIZONTAL_ALIGNMENT_LEFT, -1, label_size, Color(color.r, color.g, color.b, 0.9))

		# 中心图标
		var icon_radius := 30.0 if kind >= 2 else 22.0
		var node_name: String = ["materiel", "energy", "rich", "town"][kind] if kind >= 0 and kind < 4 else "materiel"
		if _node_art.has(node_name):
			var ntex: Texture2D = _node_art[node_name]
			var tint: Color = Color.WHITE if active else Color(1, 1, 1, 0.35)
			draw_texture_rect(ntex, Rect2(pos - Vector2(26, 26), Vector2(52, 52)), false, tint)
		else:
			draw_circle(pos, icon_radius, color.darkened(0.35))
			draw_arc(pos, icon_radius, 0.0, TAU, 24, color, 2.5)

		# 城镇据点：固定工事 + 中立核心血条 + 被攻破提示
		if kind == 3:
			var core_max := maxf(1.0, float(node.get("core_max", 2400)))
			var core_hp := float(node.get("core_hp", 0))
			# 三处固定工事（左右各一座小屋 + 中间核心）
			for i in 2:
				var offset := Vector2(-96 + i * 192, -46)
				draw_rect(Rect2(pos + offset - Vector2(16, 14), Vector2(32, 28)),
					Color(0.22, 0.18, 0.30, 0.95), true)
				draw_rect(Rect2(pos + offset - Vector2(16, 14), Vector2(32, 28)),
					Color(0.75, 0.66, 0.95), false, 2.0)
			_draw_hp_bar(Vector2(pos.x - 46, pos.y - 86), 92.0, core_hp, core_max,
				Color(0.85, 0.7, 1.0))
			if core_hp <= 0.0:
				draw_string(ThemeDB.fallback_font, Vector2(pos.x - 45, pos.y + 56), "已攻破",
					HORIZONTAL_ALIGNMENT_LEFT, -1, 18, Color(0.7, 1.0, 0.8))
			elif owner < 0:
				draw_string(ThemeDB.fallback_font, Vector2(pos.x - 40, pos.y + 56), "守军中",
					HORIZONTAL_ALIGNMENT_LEFT, -1, 16, Color(0.85, 0.8, 1.0))

		# 归属外圈
		if active and owner >= 0 and owner < 4:
			draw_arc(pos, zone_radius + 6.0, 0.0, TAU, 64, PLAYER_COLORS[owner], 3.0)

		# 占点进度：沿片区外圈画弧
		var capturer := int(node.capturer)
		var progress := int(node.progress)
		if active and capturer >= 0 and capturer < 4 and progress > 0:
			var pulse := 0.55 + 0.45 * sin(Time.get_ticks_msec() * 0.004 * (1.0 + progress / 40.0))
			draw_arc(pos, zone_radius + 12.0, -PI / 2.0,
				-PI / 2.0 + TAU * float(progress) / 100.0, 40,
				PLAYER_COLORS[capturer], 4.0)
			draw_arc(pos, zone_radius + 12.0 + 4.0 * pulse, -PI / 2.0,
				-PI / 2.0 + TAU * float(progress) / 100.0, 40,
				Color(PLAYER_COLORS[capturer].r, PLAYER_COLORS[capturer].g,
					PLAYER_COLORS[capturer].b, 0.35 * pulse), 2.0)
		if surging:
			draw_arc(pos, zone_radius + 20.0, 0.0, TAU, 40, Color(1.0, 0.88, 0.35, 0.9), 3.0)

func _draw_bases() -> void:
	if bases.size() < 4:
		return
	var base_hp: Array = snapshot.get("bases", [2000, 2000, 2000, 2000])

	for p in 4:
		var pos: Vector2 = bases[p]
		var color: Color = PLAYER_COLORS[p]
		var hp: int = base_hp[p] if p < base_hp.size() else 0
		if hp <= 0:
			draw_rect(Rect2(pos.x - 50, pos.y - 50, 100, 100), Color(0.3, 0.3, 0.35, 0.5), false, 3.0)
			continue

		if _base_art != null:
			# 基地图不染色（不然白底会被整体染成一片色块），改成"原图 + 彩色描边"
			draw_texture_rect(_base_art, Rect2(pos - Vector2(60, 60), Vector2(120, 120)), false)
			draw_rect(Rect2(pos - Vector2(62, 62), Vector2(124, 124)), color, false, 3.0)
		else:
			draw_rect(Rect2(pos.x - 50, pos.y - 50, 100, 100), color.darkened(0.55), true)
			draw_rect(Rect2(pos.x - 50, pos.y - 50, 100, 100), color, false, 3.0)
		draw_rect(Rect2(pos.x - 25, pos.y - 25, 50, 50), color.darkened(0.15), true)
		if p == my_player:
			draw_arc(pos, 88.0, 0.0, TAU, 48, Color(1, 1, 1, 0.55), 3.0, true)

		_draw_hp_bar(Vector2(pos.x - 80, pos.y - 96), 160.0, hp,
				int(base_max_hp[p]) if p < base_max_hp.size() else 3500, color)
		if font != null:
			draw_string(font, Vector2(pos.x - 60, pos.y - 104), str(hp), HORIZONTAL_ALIGNMENT_CENTER,
					120.0, 34, Color(0.92, 0.96, 1.0))


func _draw_towers() -> void:
	var towers: Array = snapshot.get("towers", [])
	for tower in towers:
		var owner: int = tower.owner
		var color: Color = PLAYER_COLORS[owner] if owner < 4 else Color.WHITE
		var pos := Vector2(tower.x, tower.y)
		if tower.hp <= 0:
			continue

		var progress := int(tower.get("progress", 100))
		draw_rect(Rect2(pos.x - 18, pos.y - 18, 36, 36), color.darkened(0.35), true)
		draw_rect(Rect2(pos.x - 18, pos.y - 18, 36, 36), color, false, 2.0)
		if progress < 100:
			# 工地：画一个还没长满的塔基 + 进度条
			draw_rect(Rect2(pos.x - 12, pos.y - 12, 24, 24), Color(0.35, 0.4, 0.5, 0.8), true)
			_draw_hp_bar(Vector2(pos.x - 30, pos.y - 34), 60.0, progress, 100,
				Color(0.55, 0.85, 1.0))
		else:
			draw_circle(pos, 10.0, color.lightened(0.4))
			_draw_hp_bar(Vector2(pos.x - 30, pos.y - 38), 60.0, tower.hp, tower.max_hp, color)


func _draw_buildings() -> void:
	var buildings: Array = snapshot.get("buildings", [])
	for b in buildings:
		var color: Color = PLAYER_COLORS[b.owner] if b.owner < 4 else Color.WHITE
		var pos := Vector2(b.x, b.y)
		# 建筑按类型上色：物资站橙、能源站青、兵营蓝、研究所紫
		var roof: Color
		match b.type:
			0: roof = Color("#f0b45a")   # 物资站
			1: roof = Color("#5eead4")   # 能源站
			2: roof = Color("#63a8ff")   # 兵营
			3: roof = Color("#c792ea")   # 研究所
			_: roof = Color.WHITE
		draw_rect(Rect2(pos.x - 22, pos.y - 22, 44, 44), color.darkened(0.65), true)
		draw_rect(Rect2(pos.x - 22, pos.y - 22, 44, 44), color, false, 2.0)
		draw_rect(Rect2(pos.x - 14, pos.y - 14, 28, 28), roof.darkened(0.25), true)
		draw_rect(Rect2(pos.x - 14, pos.y - 14, 28, 28), roof, false, 2.0)
		_draw_hp_bar(Vector2(pos.x - 22, pos.y - 32), 44.0, b.hp, b.max, color)

		# 工地：进度条 + 半透明，一眼看出"还没建好"
		var progress := int(b.get("progress", 100))
		if progress < 100:
			draw_rect(Rect2(pos.x - 22, pos.y - 26, 44.0 * progress / 100.0, 4),
				Color(0.45, 0.9, 1.0, 0.9), true)
			draw_rect(Rect2(pos.x - 22, pos.y - 26, 44, 4), Color(0.2, 0.3, 0.45, 0.8), false, 1.0)
			draw_rect(Rect2(pos.x - 22, pos.y - 22, 44, 44), Color(0.1, 0.14, 0.2, 0.22), true)

		# 有美术图就盖在程序画的方块上面（图是 96×96，居中）
		var bname: String = BUILDING_NAMES[int(b.type)] if int(b.type) < BUILDING_NAMES.size() else "depot"
		if _building_art.has(bname):
			draw_texture_rect(_building_art[bname],
				Rect2(pos - Vector2(48, 48), Vector2(96, 96)), false)


func _draw_units() -> void:
	var units: Array = snapshot.get("units", [])
	# 上一帧的位置：用来在两个快照之间做线性插值（60fps 平滑，而不是 20 次/秒的跳格）
	var previous := {}
	for old in _prev_units:
		previous[int(old.id)] = Vector2(old.x, old.y)

	for unit in units:
		var owner: int = unit.owner
		var type_index: int = unit.type
		if type_index < 0 or type_index >= UNIT_RADIUS.size():
			continue
		var pos := Vector2(unit.x, unit.y)
		var key := int(unit.id)
		if previous.has(key):
			pos = (previous[key] as Vector2).lerp(pos, _interp)
		var radius: float = UNIT_RADIUS[type_index]
		var color: Color = PLAYER_COLORS[owner] if owner < 4 else Color.WHITE
		var art_name: String = UNIT_NAMES[type_index]
		if _unit_art.has(art_name):
			var tex: Texture2D = _unit_art[art_name]
			draw_texture_rect(tex, Rect2(pos - Vector2(20, 20), Vector2(40, 40)), false)
			continue

		draw_circle(pos + Vector2(0, 3), radius, Color(0, 0, 0, 0.35))
		draw_circle(pos, radius, color.darkened(0.15))
		draw_arc(pos, radius, 0.0, TAU, 24, color.lightened(0.35), 1.5, true)

		match type_index:
			0:
				draw_circle(pos, radius * 0.42, color.lightened(0.55))
			1:
				draw_colored_polygon(PackedVector2Array([
					pos + Vector2(0, -radius * 0.6),
					pos + Vector2(radius * 0.55, radius * 0.5),
					pos + Vector2(-radius * 0.55, radius * 0.5),
				]), color.lightened(0.55))
			2:
				draw_rect(Rect2(pos.x - radius * 0.45, pos.y - radius * 0.45,
						radius * 0.9, radius * 0.9), color.lightened(0.5), true)
			3:
				draw_colored_polygon(PackedVector2Array([
					pos + Vector2(0, -radius * 0.65),
					pos + Vector2(radius * 0.65, 0),
					pos + Vector2(0, radius * 0.65),
					pos + Vector2(-radius * 0.65, 0),
				]), color.lightened(0.55))
			4:
				draw_arc(pos, radius * 0.5, -PI * 0.5, PI * 0.5, 12, color.lightened(0.6), 2.0, true)
			5:
				draw_rect(Rect2(pos.x - radius * 0.7, pos.y - radius * 0.5,
						radius * 1.4, radius), Color(0.65, 0.5, 0.3), true)
			6:
				draw_circle(pos, radius * 0.55, color.lightened(0.35))
				draw_arc(pos, radius * 0.8, 0.0, TAU, 20, color.lightened(0.7), 2.0, true)

		if unit.flags & 1:
			draw_arc(pos, radius + 2.0, 0.0, TAU, 20, Color(0.5, 0.9, 1.0, 0.9), 2.0, true)
		if unit.flags & 2:
			draw_arc(pos, radius + 4.0, 0.0, TAU, 20, Color(1.0, 0.9, 0.4, 0.9), 2.0, true)

		if unit.hp < unit.max:
			_draw_hp_bar(Vector2(pos.x - 16, pos.y - radius - 12), 32.0, unit.hp, unit.max, color)


func _draw_effects() -> void:
	var effects: Array = snapshot.get("effects", [])
	for effect in effects:
		var color: Color
		match effect.kind:
			1: color = Color(1.0, 0.5, 0.15)
			2: color = Color(0.4, 1.0, 0.5)
			3: color = Color(0.45, 0.85, 1.0)
			4: color = Color(1.0, 0.9, 0.35)
			_: color = Color(1, 1, 1)
		var alpha := float(effect.ttl) / 12.0
		var pos := Vector2(effect.x, effect.y)
		draw_circle(pos, effect.radius, Color(color.r, color.g, color.b, 0.12 * alpha))
		draw_arc(pos, effect.radius, 0.0, TAU, 48, Color(color.r, color.g, color.b, 0.8 * alpha),
				4.0, true)


func _draw_hp_bar(top_left: Vector2, width: float, hp: int, max_hp: int, color: Color) -> void:
	var ratio := 0.0 if max_hp <= 0 else clampf(float(hp) / float(max_hp), 0.0, 1.0)
	draw_rect(Rect2(top_left.x, top_left.y, width, 9.0), Color(0, 0, 0, 0.6), true)
	draw_rect(Rect2(top_left.x, top_left.y, width * ratio, 9.0), color, true)
