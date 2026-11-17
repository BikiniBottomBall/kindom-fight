# -*- coding: utf-8 -*-
"""卡牌美术生成：卡面（手牌背景 170x96）+ 徽章（构筑界面 48x48）。

id 表必须和 src/rts/cards.cpp 一致；改卡表时同步改这里。
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "client" / "godot" / "assets" / "cards"

FACTION_COLORS = [
    (99, 168, 255),   # 先锋 蓝
    (240, 180, 90),   # 商会 橙
    (199, 146, 234),  # 教团 紫
    (139, 224, 139),  # 蜂群 绿
]

# id -> (名字, 类型 0=建筑 1=法术)
CARDS = {
    0: ("物资站", 0), 1: ("能源站", 0), 2: ("兵营", 0), 3: ("研究所", 0),
    4: ("轨道炮击", 1), 5: ("壁垒号令", 1), 6: ("应急治疗", 1),
    7: ("物资站", 0), 8: ("能源站", 0), 9: ("兵营", 0), 10: ("研究所", 0),
    11: ("火雨", 1), 12: ("应急治疗", 1), 13: ("冰霜", 1),
    14: ("物资站", 0), 15: ("能源站", 0), 16: ("兵营", 0), 17: ("研究所", 0),
    18: ("冰霜", 1), 19: ("火雨", 1), 20: ("应急治疗", 1),
    21: ("物资站", 0), 22: ("能源站", 0), 23: ("兵营", 0), 24: ("研究所", 0),
    25: ("战吼", 1), 26: ("应急治疗", 1), 27: ("冰霜", 1),
    28: ("战地救护", 1), 29: ("贸易空投", 1), 30: ("灵能迟滞", 1), 31: ("虫群狂热", 1),
    32: ("工程站", 0), 33: ("工程站", 0), 34: ("工程站", 0), 35: ("工程站", 0),
    36: ("开路", 1), 37: ("开路", 1), 38: ("开路", 1), 39: ("开路", 1),
}


def faction_of(card_id: int) -> int:
    return card_id // 10 if card_id < 28 else [0, 1, 2, 3][card_id % 4]


def emblem(name: str, tone: tuple, size: int = 48) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size // 2
    light = tuple(min(255, v + 60) for v in tone)

    if name == "物资站":
        d.rectangle((c - 14, c - 10, c + 14, c + 12), fill=tone, outline=light)
        d.line((c - 14, c - 2, c + 14, c - 2), fill=light)
        d.line((c - 6, c - 10, c - 6, c + 12), fill=light)
        d.line((c + 6, c - 10, c + 6, c + 12), fill=light)
    elif name == "能源站":
        d.rectangle((c - 12, c - 12, c + 12, c + 12), fill=tone, outline=light)
        d.polygon([(c + 1, c - 10), (c - 6, c + 1), (c, c + 1), (c - 2, c + 10),
                   (c + 7, c - 2), (c + 1, c - 2)], fill=light)
    elif name == "兵营":
        d.polygon([(c - 14, c + 12), (c, c - 12), (c + 14, c + 12)], fill=tone, outline=light)
        d.line((c, c - 12, c, c + 12), fill=light)
        d.rectangle((c + 1, c - 16, c + 13, c - 10), fill=light)
    elif name == "研究所":
        d.rectangle((c - 6, c - 14, c + 6, c - 4), fill=tone, outline=light)
        d.polygon([(c - 6, c - 4), (c + 6, c - 4), (c + 13, c + 12), (c - 13, c + 12)],
                  fill=tone, outline=light)
        d.ellipse((c - 5, c + 3, c - 1, c + 7), fill=light)
        d.ellipse((c + 1, c + 6, c + 5, c + 10), fill=light)
    elif name == "防御塔":
        d.rectangle((c - 9, c - 4, c + 9, c + 14), fill=tone, outline=light)
        d.rectangle((c - 12, c - 12, c + 12, c - 4), fill=tone, outline=light)
        for bx in (-10, -3, 4):
            d.rectangle((c + bx, c - 14, c + bx + 5, c - 8), fill=light)
        d.rectangle((c - 3, c + 3, c + 3, c + 9), fill=light)
    elif name == "工程站":
        d.ellipse((c - 13, c - 13, c + 13, c + 13), outline=light, width=3)
        d.ellipse((c - 4, c - 4, c + 4, c + 4), outline=light, width=2)
        for angle in range(0, 360, 45):
            import math
            x = c + int(11 * math.cos(math.radians(angle)))
            y = c + int(11 * math.sin(math.radians(angle)))
            d.rectangle((x - 2, y - 2, x + 2, y + 2), fill=light)
    elif name == "轨道炮击":
        d.polygon([(c + 14, c - 14), (c + 2, c + 2), (c - 2, c - 2), (c + 10, c - 18)],
                  fill=light)
        d.line((c + 2, c + 2, c - 12, c + 14), fill=light, width=3)
        d.ellipse((c - 16, c + 8, c - 4, c + 20), outline=light, width=2)
    elif name == "火雨":
        for dx, dy in ((-8, -6), (2, -12), (9, -2)):
            d.polygon([(c + dx, c + dy - 8), (c + dx + 6, c + dy + 4),
                       (c + dx - 6, c + dy + 4)], fill=light)
        d.line((c - 14, c + 14, c + 14, c + 14), fill=tone, width=3)
    elif name == "冰霜":
        import math
        for angle in range(0, 360, 60):
            x = c + int(14 * math.cos(math.radians(angle)))
            y = c + int(14 * math.sin(math.radians(angle)))
            d.line((c, c, x, y), fill=light, width=3)
            d.line((x, y, x - int(5 * math.cos(math.radians(angle + 30))),
                    y - int(5 * math.sin(math.radians(angle + 30)))), fill=light, width=2)
        d.ellipse((c - 3, c - 3, c + 3, c + 3), fill=light)
    elif name == "应急治疗":
        d.rectangle((c - 13, c - 13, c + 13, c + 13), fill=tone, outline=light)
        d.rectangle((c - 3, c - 10, c + 3, c + 10), fill=light)
        d.rectangle((c - 10, c - 3, c + 10, c + 3), fill=light)
    elif name in ("壁垒号令", "战吼"):
        d.polygon([(c, c - 14), (c + 12, c - 8), (c + 9, c + 8), (c, c + 15),
                   (c - 9, c + 8), (c - 12, c - 8)], fill=tone, outline=light)
        d.line((c - 4, c, c - 1, c + 5), fill=light, width=3)
        d.line((c - 1, c + 5, c + 6, c - 6), fill=light, width=3)
    elif name == "开路":
        d.line((c - 13, c + 11, c + 13, c - 11), fill=light, width=4)
        d.polygon([(c + 10, c - 14), (c + 16, c - 8), (c + 6, c - 4)], fill=light)
        d.line((c - 14, c + 14, c + 14, c + 14), fill=tone, width=3)
    else:
        d.ellipse((c - 10, c - 10, c + 10, c + 10), fill=tone, outline=light)
    return img


def face(card_id: int) -> Image.Image:
    """竖版卡面 124x172：圆角、阵营边框、顶部类型条、中间画框、底部文字区（留给客户端画）。"""
    name, kind = CARDS[card_id]
    tone = FACTION_COLORS[faction_of(card_id)]
    w, h = 124, 172
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 卡身：圆角 + 阵营色边框 + 深色底
    d.rounded_rectangle((1, 1, w - 2, h - 2), radius=10, fill=(18, 23, 34, 255),
                        outline=tone + (255,), width=2)

    # 顶部类型条（建筑绿 / 法术蓝）
    band = (99, 200, 120) if kind == 0 else (99, 168, 255)
    d.rounded_rectangle((4, 4, w - 5, 19), radius=7, fill=band + (255,))

    # 中间画框 + 大徽章（卡牌的核心视觉）
    d.rounded_rectangle((9, 25, w - 10, 108), radius=6, fill=(12, 16, 24, 255),
                        outline=(44, 54, 74, 255))
    mark = emblem(name, tone, 60)
    img.alpha_composite(mark, ((w - 60) // 2, 40))

    # 底部文字区（名字 + 费用由客户端画）
    d.rounded_rectangle((7, 114, w - 8, h - 8), radius=6, fill=(24, 30, 43, 255),
                        outline=(44, 54, 74, 255))

    OUT.mkdir(parents=True, exist_ok=True)
    return img


def main() -> None:
    print("生成卡牌美术：")
    for card_id in sorted(CARDS):
        name, kind = CARDS[card_id]
        tone = FACTION_COLORS[faction_of(card_id)]
        face(card_id).save(OUT / f"face_{card_id}.png")
        emblem(name, tone).save(OUT / f"emblem_{card_id}.png")
        print(f"  face_{card_id}.png / emblem_{card_id}.png  ({name})")
    print("完成。")


if __name__ == "__main__":
    main()
