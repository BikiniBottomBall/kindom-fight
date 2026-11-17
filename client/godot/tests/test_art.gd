extends SceneTree

func _init() -> void:
	var Art = load("res://scripts/art.gd")
	print("exists militia: ", ResourceLoader.exists("res://assets/units/militia.png"))
	var units = Art.load_named("units", ["militia", "spear", "shield", "rider", "archer", "siege", "behemoth", "engineer"])
	print("loaded units: ", units.size(), " keys=", units.keys())
	var tiles = Art.load_named("tiles", ["plain", "road", "forest", "hill", "water", "rock"])
	print("loaded tiles: ", tiles.size())
	var base = Art.load_texture("base")
	print("base: ", base)
	quit()
