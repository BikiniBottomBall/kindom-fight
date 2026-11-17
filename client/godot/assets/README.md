# 美术资源目录（像素风）

所有图都是**可选**的：`res://assets/` 下存在同名 `.png` 就用图，不存在就用程序画的色块。
你可以一张一张替换，不用等整套齐活。

> 占位图是脚本生成的：`python tools/gen_art.py`（地形/兵种/建筑/基地/资源点）和
`python tools/gen_cards.py`（40 张卡面 + 40 个徽章）。都是 Pillow 纯代码画。
> **替换完必须在 Godot 里打开一次项目生成 `.import`**，否则运行时读不到：
> `godot --headless --editor --quit --path client/godot`

## 约定

- 工程已设置 **Nearest 过滤**（`textures/canvas_textures/default_texture_filter=0`），像素不会被糊。
- 透明背景；单位朝右画（暂未做左右翻转）。
- 尺寸不要求严格一致，客户端按"绘制尺寸"缩放。

| 目录 / 文件 | 建议画布 | 绘制尺寸（世界像素） | 说明 |
|---|---|---|---|
| `assets/units/<type>.png` | 32×32 | 40×40 | militia / spear / shield / rider / archer / siege / behemoth / engineer |
| `assets/buildings/<type>.png` | 96×96 | 96×96 | depot / reactor / barracks / lab / tower / workshop |
| `assets/base.png` | 160×160 | 120×120 | 四家共用，**不染色**，外面套玩家色描边 |
| `assets/nodes/<kind>.png` | 48×48 | 52×52 | materiel / energy / rich |
| `assets/tiles/<tile>.png` | 20×20 | 每格 20 世界像素 | plain / road / forest / hill / water / rock |
| `assets/cards/face_<id>.png` | 124×172 | 竖版卡面（`id` = 卡牌 id，0~39；绘制尺寸 132×184） | 卡面：圆角卡身 + 阵营边框 + 顶部类型条 + 中间画框插画；名字/费用由客户端画在底部 |
| `assets/cards/emblem_<id>.png` | 48×48 | 备用图标 | 同一张牌的标志图形（卡面中间画框用的就是它放大版） |

## 地形砖块是怎么画上去的

地形不是逐格绘制（240×160 = 38400 格，逐格会掉帧）。客户端在**开局时把整张地图烘成一张贴图**：

- 每格烘 `10×10` 像素 → 整张贴图 2400×1600，一次 draw call；
- 有砖块美术就贴美术（先缩到 10×10），没有就用纯色；
- `board.gd` 里的 `TILE_BAKE` / `TILE_ART_NAMES` 控制这个映射。

改砖块细节时注意：一格在屏幕上大约 11~12 像素（默认缩放），所以**不要把关键信息画在 10 像素以下**。

## 卡牌美术的约定

- 文件名用**卡牌 id**（`face_7.png`），不是中文名——换名字不会丢图。
- 卡面是**竖版 124×172**（客户端画成 132×184）：圆角卡身 + 顶部类型条 + 中间画框 + 底部文字区。
- **底部那条约 56 像素高的区域要留空**：客户端会把名字和费用画在上面。
- 手牌和构筑界面用的是同一个卡面，所以卡面在两种场景下都要可读（默认缩放约 1:1）。
- 想改某张牌的图案：只改 `tools/gen_cards.py` 里对应名字的绘制分支，或者直接替换 `emblem_<id>.png`。

## 音效（规划中）

`assets/sfx/<name>.wav`：click / place / hit / explosion / victory / defeat。
没有音频文件时游戏静音运行，不报错。

## 自检

```powershell
godot --path client/godot -- --autodeck --bot --shot=build/shots/art.png --shot-delay=10
```
截图里能看到你的图就说明路径和命名对了；看不到先检查：
1. 有没有 `.import`（没导入）；
2. 目录名/文件名大小写（Linux 下敏感）；
3. 图是不是全透明。
