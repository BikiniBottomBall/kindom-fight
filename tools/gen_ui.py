"""生成像素风 UI 组件（9-slice 面板、按钮四态、图标、卡背）。

风格：深色石板 + 1px 亮边 + 2px 点阵纹理，不描圆角，全部硬边。
输出到 client/godot/assets/ui/，程序里用 Art.load_ui() 取。

用法：python tools/gen_ui.py
"""
from __future__ import annotations

import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "client", "godot", "assets", "ui")

# 调色板
BLACK = (8, 11, 17)
DARK = (19, 26, 38)
PANEL = (30, 40, 58)
PANEL_HI = (42, 56, 80)
EDGE_LO = (86, 108, 150)
EDGE_HI = (140, 166, 210)
GOLD = (240, 192, 90)
GOLD_DARK = (168, 128, 52)
CYAN = (111, 212, 255)
RED = (232, 96, 96)
GREEN = (120, 214, 132)
WHITE = (236, 242, 255)
GREY = (120, 132, 152)


def png(path: str, width: int, height: int, pixels: list[list[tuple[int, int, int, int]]]) -> None:
    raw = b""
    for y in range(height):
        raw += b"\x00"
        for x in range(width):
            color = pixels[y][x]
            r, g, b = color[0], color[1], color[2]
            a = color[3] if len(color) > 3 else 255
            raw += bytes((r, g, b, a))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    body = zlib.compress(raw, 9)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", header))
        handle.write(chunk(b"IDAT", body))
        handle.write(chunk(b"IEND", b""))


def blank(w: int, h: int, color=(0, 0, 0, 0)):
    return [[color for _ in range(w)] for _ in range(h)]


def rect(px, x, y, w, h, color):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= yy < len(px) and 0 <= xx < len(px[0]):
                px[yy][xx] = color


def border(px, color_lo, color_hi):
    w, h = len(px[0]), len(px)
    for x in range(w):
        px[0][x] = color_lo
        px[h - 1][x] = color_lo
    for y in range(h):
        px[y][0] = color_lo
        px[y][w - 1] = color_lo
    # 内侧高光：左上亮，右下暗
    for x in range(1, w - 1):
        px[1][x] = color_hi
    for y in range(1, h - 1):
        px[y][1] = color_hi


def panel(size: int = 24, fill=PANEL, hi=EDGE_HI, lo=EDGE_LO) -> list[list]:
    px = blank(size, size, fill)
    border(px, BLACK, hi)
    # 第二层：更细的亮边
    for x in range(2, size - 2):
        px[2][x] = lo
    for y in range(2, size - 2):
        px[y][2] = lo
    # 点阵纹理：每 4px 一个暗点，像老式贴图
    for y in range(3, size - 3):
        for x in range(3, size - 3):
            if (x + y) % 4 == 0:
                px[y][x] = DARK
    # 四角挖一个像素，做出"切角"感
    for cx, cy in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1)):
        px[cy][cx] = BLACK
    return px


def button(fill, edge, inner_hi=None, inset=False) -> list[list]:
    size = 16
    px = blank(size, size, fill)
    border(px, BLACK, edge)
    for x in range(2, size - 2):
        px[2][x] = edge if not inset else BLACK
    for y in range(2, size - 2):
        px[y][2] = edge if not inset else BLACK
    if inset:
        for x in range(2, size - 2):
            px[size - 3][x] = edge
        for y in range(2, size - 2):
            px[y][size - 3] = edge
    if inner_hi is not None:
        for x in range(3, size - 3):
            px[3][x] = inner_hi
    for cx, cy in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1)):
        px[cy][cx] = BLACK
    return px


def card_back(w: int = 132, h: int = 184) -> list[list]:
    px = blank(w, h, DARK)
    border(px, BLACK, EDGE_LO)
    rect(px, 3, 3, w - 6, h - 6, PANEL)
    # 棋盘格底纹
    for y in range(5, h - 5):
        for x in range(5, w - 5):
            if ((x // 6) + (y // 6)) % 2 == 0:
                px[y][x] = PANEL_HI
    # 中央菱形徽记
    cx, cy = w // 2, h // 2
    for dy in range(-22, 23):
        span = 22 - abs(dy)
        for dx in range(-span, span + 1):
            if abs(dx) == span or abs(dy) == 22:
                px[cy + dy][cx + dx] = GOLD_DARK
    for dy in range(-16, 17):
        span = 16 - abs(dy)
        rect(px, cx - span, cy + dy, span * 2 + 1, 1, GOLD)
    # 四角铆钉
    for rx, ry in ((10, 10), (w - 12, 10), (10, h - 12), (w - 12, h - 12)):
        rect(px, rx, ry, 3, 3, EDGE_HI)
    return px


def icon(kind: str, size: int = 16) -> list[list]:
    px = blank(size, size)
    c = WHITE
    if kind == "man":       # 人头
        rect(px, 5, 2, 6, 5, c)
        rect(px, 4, 8, 8, 6, c)
        rect(px, 6, 3, 2, 2, DARK)
        rect(px, 9, 3, 2, 2, DARK)
    elif kind == "mat":     # 箱子
        rect(px, 2, 5, 12, 9, c)
        rect(px, 2, 5, 12, 2, GOLD)
        rect(px, 7, 5, 2, 9, DARK)
    elif kind == "energy":  # 闪电
        for i, (x, y, w, h) in enumerate(((8, 1, 4, 4), (6, 5, 4, 4), (4, 9, 4, 6))):
            rect(px, x, y, w, h, CYAN if i % 2 == 0 else WHITE)
    elif kind == "tech":    # 烧瓶
        rect(px, 6, 2, 4, 4, c)
        rect(px, 4, 6, 8, 3, c)
        rect(px, 3, 9, 10, 5, c)
        rect(px, 5, 10, 6, 3, CYAN)
    elif kind == "land":    # 旗子
        rect(px, 3, 2, 2, 12, c)
        rect(px, 5, 3, 8, 5, GREEN)
        rect(px, 5, 3, 8, 1, WHITE)
    elif kind == "base":    # 基地
        rect(px, 2, 8, 12, 6, c)
        rect(px, 4, 4, 8, 4, EDGE_HI)
        rect(px, 7, 5, 2, 3, DARK)
    elif kind == "build":   # 锤子
        rect(px, 9, 2, 3, 9, c)
        rect(px, 5, 2, 8, 4, GOLD)
        rect(px, 8, 11, 3, 3, c)
    elif kind == "tree":    # 树
        rect(px, 7, 12, 2, 3, GOLD_DARK)
        rect(px, 4, 5, 8, 8, GREEN)
        rect(px, 6, 2, 4, 4, GREEN)
        rect(px, 6, 8, 2, 2, DARK)
    elif kind == "flag":    # 支援旗
        rect(px, 3, 2, 2, 13, c)
        for y in range(3, 9):
            rect(px, 5, y, 9 - (y - 3) // 2, 1, RED)
    elif kind == "plus":
        rect(px, 6, 3, 4, 10, c)
        rect(px, 3, 6, 10, 4, c)
    elif kind == "minus":
        rect(px, 3, 6, 10, 4, c)
    elif kind == "map":
        rect(px, 2, 3, 12, 10, c)
        rect(px, 3, 4, 10, 8, DARK)
        rect(px, 5, 6, 3, 3, GREEN)
        rect(px, 9, 8, 2, 2, GOLD)
    elif kind == "clock":
        rect(px, 3, 3, 10, 10, c)
        rect(px, 4, 4, 8, 8, DARK)
        rect(px, 7, 5, 2, 5, c)
        rect(px, 8, 8, 4, 2, c)
    elif kind == "close":
        for i in range(8):
            rect(px, 3 + i, 4 + i, 2, 2, c)
            rect(px, 10 - i, 4 + i, 2, 2, c)
    return px


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    png(os.path.join(OUT, "panel.png"), 24, 24, panel())
    png(os.path.join(OUT, "panel_dark.png"), 24, 24, panel(fill=DARK, hi=EDGE_LO, lo=DARK))
    png(os.path.join(OUT, "button.png"), 16, 16, button((48, 62, 88), (120, 146, 190)))
    png(os.path.join(OUT, "button_hover.png"), 16, 16, button((66, 86, 120), (170, 200, 240)))
    png(os.path.join(OUT, "button_pressed.png"), 16, 16,
        button((22, 30, 44), (96, 120, 162), inset=True))
    png(os.path.join(OUT, "button_disabled.png"), 16, 16,
        button((24, 28, 38), (52, 60, 76)))
    png(os.path.join(OUT, "button_gold.png"), 16, 16,
        button((74, 56, 26), (226, 176, 74), inner_hi=(255, 214, 120)))
    png(os.path.join(OUT, "button_gold_hover.png"), 16, 16,
        button((96, 72, 32), (255, 206, 96), inner_hi=(255, 232, 160)))
    png(os.path.join(OUT, "card_back.png"), 132, 184, card_back())
    png(os.path.join(OUT, "divider.png"), 16, 4,
        [[(0, 0, 0, 0)] * 16 if y != 1 else [EDGE_LO] * 16 for y in range(4)])
    for name in ("man", "mat", "energy", "tech", "land", "base", "build",
                 "tree", "flag", "plus", "minus", "map", "clock", "close"):
        png(os.path.join(OUT, f"icon_{name}.png"), 16, 16, icon(name))
    print(f"[gen_ui] -> {OUT}")


if __name__ == "__main__":
    main()
