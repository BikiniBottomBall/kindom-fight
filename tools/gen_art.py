"""生成占位美术资源（像素风），输出到 client/godot/assets/。

为什么要脚本生成：现在的图是我"程序画"的占位，方便你之后用 Aseprite 一张张替换 ——
文件名和尺寸都固定，替换时不用改代码。跑一次就能重建全套占位图。

用法：
    python tools/gen_art.py
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "client" / "godot" / "assets"

OUTLINE = (18, 20, 26, 255)


def save(img: Image.Image, *parts: str) -> None:
    path = ART.joinpath(*parts)
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path)
    print(f"  {path.relative_to(ROOT)}  {img.width}x{img.height}")


def rect(draw, box, color):
    draw.rectangle(box, fill=color)


def unit_sprite(name: str, size=32) -> Image.Image:
    """单位：一具小身体 + 各自的武器剪影，颜色区分兵种。"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = size // 2

    palettes = {
        "militia":  ((196, 214, 240), (120, 150, 200), (230, 236, 245)),
        "spear":    ((210, 226, 250), (90, 130, 190), (240, 246, 255)),
        "shield":   ((232, 220, 190), (120, 140, 210), (255, 230, 160)),
        "rider":    ((220, 180, 150), (150, 90, 60), (255, 210, 170)),
        "archer":   ((190, 230, 190), (70, 140, 90), (225, 245, 210)),
        "siege":    ((190, 190, 200), (110, 110, 125), (235, 200, 120)),
        "behemoth": ((200, 150, 200), (120, 60, 120), (245, 190, 245)),
        "engineer": ((240, 220, 150), (200, 160, 40), (255, 245, 200)),
    }
    body, dark, accent = palettes.get(name, ((220, 220, 220), (110, 110, 110), (255, 255, 255)))

    # 影子
    rect(d, (cx - 7, size - 6, cx + 7, size - 4), (0, 0, 0, 90))

    if name == "rider":
        # 马：横条身体 + 腿
        rect(d, (cx - 10, 16, cx + 8, 22), dark + (255,))
        rect(d, (cx + 8, 12, cx + 13, 18), dark + (255,))
        for lx in (cx - 8, cx - 3, cx + 3):
            rect(d, (lx, 22, lx + 2, 28), dark + (255,))
        rect(d, (cx - 6, 9, cx + 1, 16), body + (255,))  # 骑手
        rect(d, (cx - 6, 7, cx - 1, 10), accent + (255,))
        return img

    if name == "siege":
        rect(d, (cx - 11, 14, cx + 11, 24), dark + (255,))       # 车体
        rect(d, (cx - 8, 8, cx + 6, 16), (90, 70, 50, 255))      # 炮管
        rect(d, (cx + 5, 9, cx + 14, 13), accent + (255,))
        for wx in (cx - 8, cx + 4):
            img_draw_circle(d, wx + 2, 26, 4, (60, 60, 70, 255))
        return img

    if name == "behemoth":
        rect(d, (cx - 11, 10, cx + 11, 27), dark + (255,))
        rect(d, (cx - 8, 7, cx + 8, 12), body + (255,))
        rect(d, (cx - 9, 4, cx - 5, 9), accent + (255,))  # 角
        rect(d, (cx + 5, 4, cx + 9, 9), accent + (255,))
        rect(d, (cx - 4, 14, cx - 1, 17), (250, 250, 250, 255))
        rect(d, (cx + 1, 14, cx + 4, 17), (250, 250, 250, 255))
        return img

    # 通用人形
    rect(d, (cx - 5, 12, cx + 5, 25), body + (255,))          # 身体
    rect(d, (cx - 5, 25, cx - 1, 30), dark + (255,))          # 腿
    rect(d, (cx + 1, 25, cx + 5, 30), dark + (255,))
    rect(d, (cx - 4, 7, cx + 4, 13), accent + (255,))         # 头/头盔

    if name in ("militia", "spear"):
        rect(d, (cx + 5, 6, cx + 7, 30), (150, 170, 200, 255))  # 长杆
        rect(d, (cx + 5, 4, cx + 7, 8), (230, 240, 255, 255))
    elif name == "shield":
        rect(d, (cx - 11, 12, cx - 6, 24), (180, 200, 240, 255))
        rect(d, (cx - 11, 12, cx - 6, 14), accent + (255,))
    elif name == "archer":
        img_draw_arc(d, cx + 6, 16, 8, -60, 60, (210, 230, 190, 255))
    elif name == "engineer":
        rect(d, (cx + 4, 12, cx + 10, 15), (200, 200, 210, 255))  # 扳手
        rect(d, (cx + 9, 9, cx + 12, 18), (200, 200, 210, 255))
    return img


def img_draw_circle(d, x, y, r, color):
    d.ellipse((x - r, y - r, x + r, y + r), fill=color)


def img_draw_arc(d, x, y, r, start, end, color):
    d.arc((x - r, y - r, x + r, y + r), start, end, fill=color, width=2)


def building_sprite(name: str, size=96) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m = 8

    tones = {
        "depot":    ((120, 86, 46), (180, 132, 72), (240, 190, 110)),
        "reactor":  ((30, 90, 100), (60, 170, 180), (150, 245, 235)),
        "barracks": ((50, 70, 120), (90, 130, 200), (170, 210, 255)),
        "lab":      ((80, 50, 110), (140, 90, 180), (220, 170, 250)),
        "tower":    ((60, 66, 80), (110, 120, 140), (200, 215, 240)),
        "workshop": ((110, 90, 40), (200, 165, 70), (255, 225, 140)),
    }
    dark, mid, light = tones.get(name, ((80, 80, 80), (140, 140, 140), (220, 220, 220)))

    if name == "tower":
        rect(d, (m + 22, 20, size - m - 22, size - m - 6), dark + (255,))
        rect(d, (m + 16, 12, size - m - 16, 24), mid + (255,))
        rect(d, (m + 30, 28, size - m - 30, 44), light + (255,))
        rect(d, (m + 26, size - m - 14, size - m - 26, size - m - 4), mid + (255,))
        return img

    # 通用：底座 + 主体 + 屋顶 + 一点发光
    rect(d, (m + 6, size - m - 18, size - m - 6, size - m - 6), dark + (255,))
    rect(d, (m + 12, m + 20, size - m - 12, size - m - 18), mid + (255,))
    rect(d, (m + 8, m + 12, size - m - 8, m + 24), dark + (255,))
    rect(d, (m + 20, m + 28, size - m - 20, m + 40), light + (255,))

    if name == "depot":
        rect(d, (m + 18, size - m - 30, m + 30, size - m - 20), (90, 64, 34, 255))
        rect(d, (m + 36, size - m - 30, m + 48, size - m - 20), (90, 64, 34, 255))
    elif name == "reactor":
        img_draw_circle(d, size // 2, size // 2 + 6, 12, light + (255,))
        img_draw_circle(d, size // 2, size // 2 + 6, 6, (255, 255, 255, 255))
    elif name == "barracks":
        rect(d, (m + 32, size - m - 26, m + 48, size - m - 10), (40, 50, 70, 255))
        rect(d, (m + 34, m + 6, m + 46, m + 16), (230, 240, 255, 255))  # 旗
    elif name == "lab":
        img_draw_circle(d, size // 2, size // 2 + 4, 10, light + (255,))
        rect(d, (size // 2 - 2, m + 6, size // 2 + 2, m + 18), (210, 230, 255, 255))
    elif name == "workshop":
        rect(d, (m + 26, size - m - 34, m + 34, size - m - 26), (60, 60, 70, 255))
        rect(d, (m + 40, size - m - 34, m + 48, size - m - 26), (60, 60, 70, 255))
        rect(d, (m + 30, m + 34, m + 60, m + 38), (255, 220, 120, 255))
    return img


def base_sprite(size=160) -> Image.Image:
    """主基地：画成灰白色，客户端会按玩家颜色染色。"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size // 2
    rect(d, (c - 46, c - 40, c + 46, c + 44), (210, 214, 224, 255))
    rect(d, (c - 46, c - 40, c + 46, c - 30), (245, 248, 255, 255))
    rect(d, (c - 30, c - 8, c + 30, c + 6), (150, 155, 170, 255))
    img_draw_circle(d, c, c - 18, 10, (255, 255, 255, 255))
    # 四个角塔
    for dx, dy in ((-46, -40), (34, -40), (-46, 32), (34, 32)):
        rect(d, (c + dx, c + dy, c + dx + 12, c + dy + 12), (170, 175, 190, 255))
    return img


def node_sprite(kind: str, size=48) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size // 2
    if kind == "materiel":
        rect(d, (c - 12, c - 8, c + 12, c + 12), (150, 110, 60, 255))
        rect(d, (c - 12, c - 8, c + 12, c - 2), (200, 150, 85, 255))
        rect(d, (c - 6, c + 2, c + 6, c + 8), (235, 195, 120, 255))
    elif kind == "energy":
        img_draw_circle(d, c, c, 14, (40, 130, 140, 255))
        img_draw_circle(d, c, c, 9, (90, 220, 210, 255))
        img_draw_circle(d, c, c, 4, (240, 255, 250, 255))
    else:  # rich：金色双菱
        d.polygon([(c, c - 16), (c + 14, c), (c, c + 16), (c - 14, c)], fill=(230, 180, 60, 255))
        d.polygon([(c, c - 8), (c + 7, c), (c, c + 8), (c - 7, c)], fill=(255, 240, 180, 255))
    return img


def tile_sprite(tile: str, size=20) -> Image.Image:
    """地形砖块：20×20，客户端把它烘成整张地图（10 像素/格）。"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    d = ImageDraw.Draw(img)
    if tile == "plain":
        img = Image.new("RGBA", (size, size), (58, 74, 66, 255))
        d = ImageDraw.Draw(img)
        rect(d, (3, 5, 5, 7), (72, 92, 80, 255))
        rect(d, (12, 12, 14, 14), (70, 88, 78, 255))
    elif tile == "road":
        img = Image.new("RGBA", (size, size), (96, 104, 118, 255))
        d = ImageDraw.Draw(img)
        rect(d, (0, 6, size, 8), (128, 136, 150, 255))
        rect(d, (0, 14, size, 15), (116, 124, 138, 255))
    elif tile == "forest":
        img = Image.new("RGBA", (size, size), (26, 52, 36, 255))
        d = ImageDraw.Draw(img)
        d.polygon([(10, 3), (16, 14), (4, 14)], fill=(34, 74, 48, 255))
        d.polygon([(10, 9), (17, 19), (3, 19)], fill=(30, 88, 54, 255))
        rect(d, (9, 16, 11, 20), (60, 46, 30, 255))
    elif tile == "hill":
        img = Image.new("RGBA", (size, size), (74, 66, 50, 255))
        d = ImageDraw.Draw(img)
        d.polygon([(2, 18), (9, 5), (16, 18)], fill=(104, 92, 68, 255))
        d.polygon([(8, 18), (14, 9), (20, 18)], fill=(122, 108, 80, 255))
    elif tile == "water":
        img = Image.new("RGBA", (size, size), (24, 52, 96, 255))
        d = ImageDraw.Draw(img)
        rect(d, (2, 7, 9, 8), (56, 96, 156, 255))
        rect(d, (11, 13, 18, 14), (56, 96, 156, 255))
    elif tile == "rock":
        img = Image.new("RGBA", (size, size), (46, 44, 52, 255))
        d = ImageDraw.Draw(img)
        d.polygon([(4, 17), (9, 6), (16, 15)], fill=(92, 90, 104, 255))
        d.polygon([(8, 17), (13, 10), (18, 17)], fill=(116, 114, 130, 255))
    return img


def main() -> None:
    print("生成占位美术资源：")
    for tile in ("plain", "road", "forest", "hill", "water", "rock"):
        save(tile_sprite(tile), "tiles", f"{tile}.png")
    for unit in ("militia", "spear", "shield", "rider", "archer", "siege", "behemoth", "engineer"):
        save(unit_sprite(unit), "units", f"{unit}.png")
    for building in ("depot", "reactor", "barracks", "lab", "tower", "workshop"):
        save(building_sprite(building), "buildings", f"{building}.png")
    save(base_sprite(), "base.png")
    for kind in ("materiel", "energy", "rich"):
        save(node_sprite(kind), "nodes", f"{kind}.png")
    print("完成。")


if __name__ == "__main__":
    main()
