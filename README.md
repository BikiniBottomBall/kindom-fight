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
| S3 | 资源产出与建筑 | ✅ |
| S4 | 招募 / 调动与合法性校验 | ✅ |
| S5 | 战斗结算（确定性公式，无骰子） | ✅ |
| S6 | 占领 / 淘汰 / 胜负判定 | ✅ |
| S7 | 确定性验证 | ✅ 同种子同指令 → 事件流逐字节一致 |
| S8 | 战报（事件流 + 纯文本渲染） | ✅ |
| S9 | AI（单机对手 / 补位 / 托管，一个模块三处复用） | ✅ 启发式版，能打完整局 |
| S10 | 终端客户端（人工操作） | ✅ `client/cli`，你 vs AI |
| S11 | 传输与协议分层（自定义二进制帧，带 seq 幂等位） | ✅ |
| S12 | epoll Reactor（Linux）+ select（Windows），跨平台 poller 抽象 | ✅ 双平台都实测过 |
| S13 | 编解码 / 粘包半包 / 超长帧拒绝 | ✅ 12 个单测 + 真 TCP 冒烟 |
| S14 | 心跳与超时踢人 | ⬜ |
| S15 | 会话与登录 | ⬜ |
| S16 | 房间（4 人加入 / 开局） | ⬜ |
| S17 | 按人裁剪状态下发 | ⬜ |
| S18 | 断线重连（快照 + 增量 + seq 去重） | ⬜ |

**已知问题（下一步要解决的第一件事）**：4 个 AI 对局平均 **3 回合就结束**，而设计目标是 8–12 回合。
这不是 bug，是数值问题——一次成功进攻的收益（+1 领地 ≈ +2 金/回合永久收益）远高于发育
（+1~2 兵/回合），所以"快攻"在数学上就是最优解。实测把胜利门槛从 3 块提到 5 块，对局只从
3 回合延长到 5 回合，说明瓶颈在**征服速度本身**，不在门槛。详见
[docs/02-game-design.md](docs/02-game-design.md) §9。解决方式不是继续拍脑袋调数值，而是先做
**自动对局统计**（跑 10k 局看分布），再动参数。

完整步骤与验收标准见 [docs/03-build-plan.md](docs/03-build-plan.md)。

## 构建

### Windows（本机已验证，离线可用）

本机没有 cmake，用 g++ 直接编译：

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1        # 编译全部目标
powershell -ExecutionPolicy Bypass -File build.ps1 test   # 编译并跑单测
powershell -ExecutionPolicy Bypass -File build.ps1 run    # AI 自打一局（看战报）
powershell -ExecutionPolicy Bypass -File build.ps1 play   # 你亲自打一局
```

依赖：MSYS2 UCRT64 g++（已在本机 PATH 中）。无第三方库。

### CMake（Linux / WSL / CI）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/kingdom_fight        # AI 自打
./build/kingdom_fight_cli    # 你亲自打
```

## 玩一局（终端客户端）

```powershell
.\build\bin\kingdom_fight_cli.exe              # 默认 4 人
.\build\bin\kingdom_fight_cli.exe --seed 7     # 换一个牌序
.\build\bin\kingdom_fight_cli.exe --players 2  # 2 人局
```

你是 P0，其他位置由 AI 补。每回合你有 2 点行动点：

| 指令 | 作用 | 消耗 |
|---|---|---|
| `b <区域>` | 建造（4 金，等级 +1，每级 +1 金/回合，上限 2 级） | 1 AP |
| `r <区域> <s\|h\|r> <数量>` | 招募（1 金/兵，受该区域招募上限限制） | 1 AP |
| `m <起点> <终点> <数量>` | 调动（仅自己相邻的两块领地） | 1 AP |
| `a <起点> <终点> <数量>` | 进攻（相邻的敌方/中立领地） | 1 AP |
| `t <手牌序号> <区域>` | 暗置战术卡，该区域开战时给己方加战力 | 0 AP |
| `c` / `done` / `help` / `quit` | 清空指令 / 提交结算 / 说明 / 退出 | — |

结算阶段所有玩家**同时**生效：你先看到自己的指令被接受，然后一次性看到产出、建造、
战术揭晓、战斗、占领的完整战报。

## 目录

```
src/
├── common/       config.h（可调参数）、rng.h（确定性随机）
├── logic/        纯逻辑层：零网络、零数据库、零线程，可单测
│   ├── types       兵种与军队（三角克制）
│   ├── region      5 个区域 + 邻接表（节点图，不是格子）
│   ├── card_def    10 张卡的数据驱动目录（兵种 / 战术 / 经济）
│   ├── command     指令 = 玩家意图
│   ├── event       事件 = 服务端判定后的事实（serialize/describe）
│   ├── combat      确定性战斗公式（无骰子）
│   ├── engine      同时决策的回合循环
│   └── report      纯文本战报
├── ai/           simple_ai：一个模块三处复用（单机对手 / 补位 / 托管）
├── net/          网络层（S11–S13）
│   ├── protocol    帧格式 + 大端序读写 + 编码
│   ├── decoder     粘包/半包状态机（12 个单测）
│   ├── socket      跨平台 socket RAII（Windows SOCKET 用 intptr_t 装）
│   ├── poller      epoll / select 双实现，上层只认接口
│   └── channel     socket + 读缓冲 + 写队列
└── main.cpp      AI 自打 demo
client/cli/       终端客户端：你 vs AI（S10）
tools/            echo 服务端 + 网络冒烟脚本
tests/            78 个单测（自带极简框架，零依赖）
docs/             设计稿、路线图、步骤清单、ADR
```

**最重要的分层原则：`src/logic/` 不许 include 任何 IO / 线程 / 数据库头。**
它能被单测、能被 AI 直接驱动、能在没有网络时反复试玩 —— 这是整个项目能做完的根本原因。

客户端也只做两件事：**渲染状态** 和 **把人的意图翻译成 `Command`**。
合法性判定全在引擎里——把 `client/` 整个删掉，服务端依然完全正确（ADR 0002 护栏二）。

## 网络层（S11–S13）

帧格式（大端序，13 字节头）：

```
+--------+---------+-------+-------+------------+-----------+
| magic  | version |  cmd  |  seq  | payload_len|  payload  |
| 0x4B46 |  1 字节 | 2字节 | 4字节 |   4 字节   |  N 字节   |
+--------+---------+-------+-------+------------+-----------+
```

- **`seq` 不是装饰**：断线重连后客户端会重发"未确认的指令"，服务端靠它去重（S18 的地基）。
- **传输与协议解耦**：以后加 WebSocket（网页 demo）只写一个新的编解码外壳，业务代码零改动。
- **丢帧策略是 fail-fast**：magic 不对 / 版本不支持 / 声明长度超 64KB → 直接断开，
  不做"跳过垃圾字节重新同步"。游戏协议不需要那种宽容，容忍只会扩大攻击面。

poller 两套实现，上层代码完全相同：

| 平台 | 实现 | 说明 |
|---|---|---|
| **Linux** | **epoll（水平触发 LT）** | 面试考点。没用 ET：ET 要求每次事件必须读到 EAGAIN，漏读一次这条连接就永久饿死，且是偶发难复现的那种 bug。先用 LT 保证正确，压测阶段再对比 ET 的收益 |
| Windows | select | 本机开发用，也是 POSIX 兜底。O(n) 拷贝 + O(n) 内核遍历、受 `FD_SETSIZE` 限制 —— 这正是 epoll 存在的理由 |

`KF_POLLER=select` 可强制走 select，所以在 Linux 上也能做两种实现的**同组用例对比**。

验收（真 TCP，不是 mock）：

```bash
./build.sh                          # Linux / WSL：编译 + 78 个单测
./tools/smoke_net.sh                # 完整帧 / 半包 / 粘包 三个用例（epoll）
./tools/smoke_net.sh 19528 select   # 同一组用例，select 后端再跑一遍
```

Windows 侧：`build.ps1` 编译后 `build\bin\kingdom_fight_echo.exe --port 9531` 起服务端，
用任意 TCP 客户端发帧即可验证。

## 设计文档

- [01 蓝图与路线图](docs/01-blueprint-and-roadmap.md) —— 为什么做这个项目、面试怎么讲
- [02 游戏设计稿](docs/02-game-design.md) —— 规则、四条红线、机制到后端考点的映射
- [03 构建步骤清单](docs/03-build-plan.md) —— S0→S18，每步都有验收标准
- [ADR 0001 不引入游戏引擎](docs/adr/0001-no-game-engine.md) —— 自研边界
- [ADR 0002 客户端用 Godot 4.5](docs/adr/0002-godot-client.md) —— 为什么可以、代价与护栏

## 密钥与安全（硬约束）

**任何 API Key / Token / 私钥都不许进这个仓库，包括历史提交。**

- 本项目当前**不调用任何外部 API**（AI 是本地启发式代码，不是大模型），所以仓库里不需要任何密钥。
- 将来如果要接入大模型（例如用 LLM 当对手），密钥只能放环境变量或未跟踪的 `.env`：

  ```powershell
  setx DEEPSEEK_API_KEY "..."   # 只存在本机环境变量里，不要写进任何文件
  ```

两道防线：

1. `.gitignore` 已屏蔽 `.env` / `.env.*` / `*.key` / `*.pem` / `secrets.*` / `credentials.*` 等。
2. `.githooks/pre-commit` 会扫描**暂存区新增行**，命中的是"变量名 + 真实值"的形式
   （`sk-…`、`DEEPSEEK_API_KEY=<16 位以上>`、`Bearer <token>`、私钥头）。
   只写变量名不会误报——文档里提名字是正常的，值不行。
   每台机器启用一次：

   ```powershell
   git config core.hooksPath .githooks
   ```

已经进过历史的密钥无法靠删除文件消除（`git log` 里还在），必须用 `git filter-repo` 重写历史并**立刻在服务商后台吊销那把 key**。所以防线要放在提交前，而不是提交后。

## 三条执行纪律

1. **逻辑先于网络，网络先于界面。** S7（确定性）没过不许碰网络层，S10（终端能玩）没过不许碰 Godot。
2. **每个 S 一次 commit + 一行 CHANGELOG。** 见 [CHANGELOG.md](CHANGELOG.md)。
3. **卡住超过 3 小时就降级。** 先跑通流程，标 `// TODO: balance`，继续推进。
