# 美术资源加载器（像素风）。
#
# 设计原则：**图是可选的**。assets 下没有同名文件就返回 null，调用方回退到程序画的形状。
# 这样美术可以一张一张替换，不用等整套齐活，也不会因为缺图崩掉。
extends RefCounted

const ROOT := "res://assets/"


static func load_texture(relative_path: String) -> Texture2D:
	var path := ROOT + relative_path + ".png"
	if not ResourceLoader.exists(path):
		return null
	var resource := load(path)
	return resource as Texture2D


static func load_ui(name: String) -> Texture2D:
	return load_texture("ui/" + name)


static func load_card_face(card_id: int) -> Texture2D:
	return load_texture("cards/face_%d" % card_id)


static func load_card_emblem(card_id: int) -> Texture2D:
	return load_texture("cards/emblem_%d" % card_id)


# 一次性把某个目录下的所有图按名字装进字典：{"militia": Texture2D, ...}
static func load_named(relative_dir: String, names: Array) -> Dictionary:
	var out := {}
	for name in names:
		var texture := load_texture("%s/%s" % [relative_dir, name])
		if texture != null:
			out[name] = texture
	return out
