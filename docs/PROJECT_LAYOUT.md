# 项目结构与维护指南

这份文档面向"接手的人"（也包括三个月后的我）。先看这张图，再决定去哪个目录改东西。

```
Cpp_Game_ Work/
├─ src/                     服务端 + 模拟内核（纯 C++20，无第三方依赖）
│  ├─ rts/                  实时对局：模拟、卡牌、地形、AI、协议
│  │  ├─ types.h            **所有可调数值**：单位表、资源产出、卡牌定义、常量
│  │  ├─ sim.cpp/.h         权威模拟：移动、战斗、占点、建造、成长树、协议快照
│  │  ├─ cards.cpp          卡池 / 单位数值 / 升级表
│  │  ├─ terrain.cpp/.h     地图生成（秘密森林 / 双河 / 火山口）+ 连通性保证
│  │  ├─ ai.cpp             内置 AI（建造、升塔、升级、法术、总部支援）
│  │  └─ wire.cpp/.h        二进制协议编解码
│  ├─ net/                  socket / poller / 拆包
│  └─ (旧回合制代码，可忽略)
├─ tools/                   开发与运维脚本
│  ├─ rts_server.cpp        服务端入口（--port / --fast / --no-ai）
│  ├─ rts_bench.cpp         批量对局跑分（平衡性/节奏回归）
│  ├─ serve_wsl.sh          WSL 里编译 + 启动
│  ├─ gen_art.py            生成占位美术（Pillow，像素风）
│  ├─ tick_probe.py         量服务端 tick 速率（排查"游戏变快"）
│  ├─ speed_probe.cpp       量单位真实移速（排查速度和表里不一致）
│  └─ smoke_rts.py          协议级冒烟测试
├─ client/godot/            Godot 4 表现层（只负责画和发意图，不含规则）
│  ├─ scripts/main.gd       总控：连服务器、界面、手牌、成长树、拆除
│  ├─ scripts/board.gd      战场绘制：地形烘焙、单位/建筑、相机、小地图
│  ├─ scripts/card_view.gd  卡牌自绘控件 + 抽牌/出牌动画
│  ├─ scripts/art.gd        美术加载器（缺图自动回退到色块）
│  ├─ scripts/kf_net.gd     网络（收发帧、断线重连）
│  ├─ assets/               **美术资源**：替换这里即可，见下
│  └─ tests/                GDScript 协议测试
├─ tests/                   C++ 单测（130 个，包含模拟/协议/地形/卡牌）
├─ docs/                    设计文档 + ADR + 截图
├─ build*/                  编译产物（已 gitignore）
└─ run-demo.cmd / demo.ps1  一键启动（WSL 服务端 + Godot 客户端）
```

## 我要改 X，该动哪里？

| 想改的东西 | 位置 |
|---|---|
| 单位血量/伤害/速度/造价 | `src/rts/cards.cpp` 的 `kUnitStats` |
| 资源产出速度 | `src/rts/sim.cpp` 顶部的 `kBase*Ticks` / `kDepotTicks` … |
| 卡牌名称/费用/效果 | `src/rts/cards.cpp` 的 `kCards`（id 0~39，每家 10 张） |
| 成长树节点/价格 | `src/rts/cards.cpp` 的 `kTreeNodes`（取用 `tree_node(TreeNode)` / `upgrade_cost_for`） |
| 地图形状/森林密度 | `src/rts/terrain.cpp`（`variant == 0` 是「秘密森林」） |
| AI 行为 | `src/rts/ai.cpp`（建造顺序、升塔、升级、法术、总部支援） |
| 建筑数量上限公式 | `Sim::building_cap` |
| 建造时间/工程兵加成 | `types.h` 的 `kBuildTicks` / `kEngineerBuildBonus` |
| 界面布局 / 文案 | `client/godot/scripts/main.gd` |
| 战场绘制 / 相机 / 小地图 | `client/godot/scripts/board.gd` |
| 音效/音乐 | 见下面「音频」一节 |
| 美术 | `client/godot/assets/`（见 `client/godot/assets/README.md`） |

## 美术：怎么替换

1. 用 Aseprite 画好 PNG，按 `client/godot/assets/README.md` 的命名放进对应目录。
2. **在 Godot 编辑器里打开一次项目**（或跑 `godot --headless --editor --quit --path client/godot`），
   让 Godot 生成 `.import` —— 只丢 PNG 不导入是读不到的（我就踩过这个坑）。
3. 尺寸不用严格一致：客户端会按固定绘制尺寸缩放（Nearest 过滤，像素不会被糊）。

占位图是脚本生成的：`python tools/gen_art.py`，随时可以重跑。

## 音频（下一轮接入）

计划目录 `client/godot/assets/sfx/`，命名：`click/place/hit/explosion/victory/defeat.wav`。
加载器会沿用 `art.gd` 的"缺文件就用默认"思路，所以没有音频也能跑。

## 常用命令

```powershell
# 跑服务端（WSL 编译 + 启动）
wsl bash -lc "cd /mnt/d/Code/DSH/*Game* && ./tools/serve_wsl.sh 9700"

# 跑客户端
godot --path client/godot

# 一键（服务端 + 客户端）
.\run-demo.cmd

# C++ 单测（130）
powershell -ExecutionPolicy Bypass -File build.ps1 test        # Windows（MSYS2 g++，不需要 WSL）
wsl bash -lc "cd /mnt/d/Code/DSH/*Game* && ./build.sh test"    # WSL / Linux

# 服务端端到端（真实 TCP，不需要 Godot）
wsl bash -lc "cd /mnt/d/Code/DSH/*Game* && ./build.sh smoke"

# 客户端协议测试（51 + 56 项）
bash tools/run_godot_tests.sh

# 平衡/节奏跑分（12 局，带节奏门禁）
wsl bash -lc "cd /mnt/d/Code/DSH/*Game* && ./build.sh bench 12"

# 量 tick 速率（排查"游戏像加速"）
wsl bash -lc "cd /mnt/d/Code/DSH/*Game* && python3 tools/tick_probe.py --port 9700"
```

## 约定

- **服务端权威**：客户端不跑任何规则，只发"我要出第几张牌、放在哪、拆哪个、升哪项"。
- **确定性**：模拟里不允许浮点/随机数，全部整数 + 固定种子（同输入必得同结果）。
- **数值集中**：新加常量请放进 `types.h` 或 `cards.cpp` 顶部，不要散落在函数里。
- **一次一功能**：每个功能一个 commit + CHANGELOG 一行，方便回溯。
