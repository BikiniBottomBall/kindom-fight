# kingdom_fight · 4 人混战抢地游戏（服务端优先）

> 3-4 名领导者争夺 5 块领地：发育解锁卡牌，用卡牌运营军队，最后只有一人胜出。
> **这是一个服务端项目，不是游戏客户端项目** —— 客户端只是壳（见 [ADR 0002](docs/adr/0002-godot-client.md)）。

C++20 · 回合制 · 同时决策 · 服务端权威 · 本机离线可构建（零外部依赖）

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| S0 | 工程地基（构建 + 测试框架） | ✅ |
| S1 | 核心数据模型（领地 / 军队 / 卡牌） | ✅ |
| S2 | 回合驱动（同时决策 → 结算 → 事件流） | ✅ |
| S3 | 资源产出与建筑 | ⬜ |
| S4 | 移动与合法性校验 | ⬜ |
| S5 | 战斗结算 | ⬜ |
| S6 | 占领 / 淘汰 / 胜负判定 | 🟡 部分（三种胜利条件已实现，无战斗支撑） |
| S7 | 确定性验证 | 🟡 地基版（同种子同指令 → 事件流逐字节一致） |
| S8–S10 | 战报 / AI / 终端客户端 | ⬜ |
| S11–S18 | 网络层（epoll / 协议 / 房间 / 重连） | ⬜ |

完整步骤与验收标准见 [docs/03-build-plan.md](docs/03-build-plan.md)。

## 构建

### Windows（本机已验证，离线可用）

本机没有 cmake，用 g++ 直接编译：

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1        # 编译 demo + 测试
powershell -ExecutionPolicy Bypass -File build.ps1 test   # 编译并跑单测
powershell -ExecutionPolicy Bypass -File build.ps1 run    # 编译并跑 demo
```

依赖：MSYS2 UCRT64 g++（已在本机 PATH 中）。无第三方库。

### CMake（Linux / WSL / CI）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/kingdom_fight
```

## 目录

```
src/
├── common/       config.h（可调参数）、rng.h（确定性随机）
├── logic/        纯逻辑层：零网络、零数据库、零线程，可单测
│   ├── types       兵种与军队（三角克制）
│   ├── region      5 个区域 + 邻接表（节点图，不是格子）
│   ├── card_def    12 张卡的数据驱动目录
│   ├── command     指令 = 玩家意图
│   ├── event       事件 = 服务端判定后的事实（serialize/describe）
│   ├── engine      同时决策的回合循环
│   └── report      纯文本战报
└── main.cpp      临时 demo（S10 会被 client/cli 替掉）
tests/            35 个单测（自带极简框架，零依赖）
docs/             设计稿、路线图、步骤清单、ADR
```

**最重要的分层原则：`src/logic/` 不许 include 任何 IO / 线程 / 数据库头。**
它能被单测、能被 AI 直接驱动、能在没有网络时反复试玩 —— 这是整个项目能做完的根本原因。

## 设计文档

- [01 蓝图与路线图](docs/01-blueprint-and-roadmap.md) —— 为什么做这个项目、面试怎么讲
- [02 游戏设计稿](docs/02-game-design.md) —— 规则、四条红线、机制到后端考点的映射
- [03 构建步骤清单](docs/03-build-plan.md) —— S0→S18，每步都有验收标准
- [ADR 0001 不引入游戏引擎](docs/adr/0001-no-game-engine.md) —— 自研边界
- [ADR 0002 客户端用 Godot 4.5](docs/adr/0002-godot-client.md) —— 为什么可以、代价与护栏

## 三条执行纪律

1. **逻辑先于网络，网络先于界面。** S7（确定性）没过不许碰网络层，S10（终端能玩）没过不许碰 Godot。
2. **每个 S 一次 commit + 一行 CHANGELOG。** 见 [CHANGELOG.md](CHANGELOG.md)。
3. **卡住超过 3 小时就降级。** 先跑通流程，标 `// TODO: balance`，继续推进。
