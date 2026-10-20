# 构建步骤清单（S0 → S18）

> 每一步都有**验收标准**。验收不通过就不进入下一步——这是防止 12 个月项目烂尾的唯一办法。
> 完成定义（DoD）：① 编译通过 ② `ctest` 全绿 ③ CHANGELOG 加一行 ④ 一次 commit。

---

## 阶段 M1：纯逻辑层（不碰网络、不碰 Godot）

目标：**终端里 4 个 AI 打完一整局。** 做完这一步，你就"有游戏了"。

| # | 做什么 | 产出文件 | 验收标准 | 工时 |
|---|---|---|---|---|
| **S0** | 工程地基 | | | 4h |
| S0.1 | 仓库与目录骨架、`.gitignore`、README 空壳 | 目录树 | `git log` 有第一个 commit | 0.5h |
| S0.2 | CMake + Ninja 构建，跑通 hello | `CMakeLists.txt`、`src/main.cpp` | `cmake --build build` 成功，能运行 | 1.5h |
| S0.3 | 接入 GoogleTest，一个 `EXPECT_TRUE` 通过 | `tests/CMakeLists.txt`、`tests/smoke_test.cpp` | `ctest` 显示 1 passed | 2h |
| **S1** | 核心数据模型 | `src/logic/types.h` | | 5h |
| | 区域（5 个，邻接表）、军队（兵力+兵种构成）、玩家（金币/AP/控制区域）、卡牌定义（12 张，数据驱动） | `region.*`、`army.*`、`player.*`、`card_def.*` | 能构造 4 人初始局面并打印成表 | 5h |
| **S2** | 回合驱动与协议内部结构 | `src/logic/command.h`、`event.h`、`game_state.*`、`turn.*` | | 6h |
| | 定义「指令（意图）」与「事件（事实）」两种结构；实现 `提交 → 结算 → 产出事件序列` 的主循环 | 同上 | 传入 4 份空指令，连续跑 12 回合不崩，事件序列可打印 | 6h |
| **S3** | 资源产出与建筑 | `src/logic/economy.*` | 单测：金币/兵源按公式增长，建造完工后产出改变 | 5h |
| **S4** | 移动与合法性校验 | `src/logic/orders.*` | 单测：非邻接区域移动被拒；对无兵区域进攻被拒；AP 不足被拒；**校验函数是纯函数** | 6h |
| **S5** | 战斗结算 | `src/logic/combat.*` | 10 个单测：克制方向正确、兵力相等时骰子决定、伤亡不产生负数、战败方全灭、战力公式边界（0 兵） | 8h |
| **S6** | 占领 / 淘汰 / 胜利判定 | `src/logic/victory.*` | 单测覆盖三种胜利条件（3 区域 / 全淘汰 / 12 回合分最高） | 4h |
| **S7** | ⭐ 确定性验证 | `tests/determinism_test.cpp` | **同一指令序列 + 同一种子，两次结算产出的事件序列逐字节相同** | 3h |
| **S8** | 战报输出 | `src/logic/report.*` | 打印一局的完整战报，人能读懂（这是 Godot 客户端未来的数据形状） | 3h |
| **S9** | AI | `src/ai/` | 随机合法 AI + 简单启发式 AI；4 个 AI 自动打完一局，10 秒内 | 10h |
| **S10** | 终端 CLI | `client/cli/` | **你作为 1 号玩家，和 3 个 AI 打完 3 局** | 6h |

**M1 合计 ≈ 60h ≈ 13 周（5h/周）**
**⭐ 里程碑：S10 完成 = 你有一个能自己玩的完整游戏，可以开始调平衡（M0 的纸上推演从此换成真机推演）。**

---

## 阶段 M2：网络层（关键路径，决定你能不能投简历）

目标：**4 个终端客户端真正联机打完一局，断线能重连。**

| # | 做什么 | 产出文件 | 验收标准 | 工时 |
|---|---|---|---|---|
| **S11** | 传输与协议分层 | `src/net/frame.h`、`src/net/protocol.h` | 「帧怎么跑」与「帧里装什么」两个抽象分开；有单元测试 | 4h |
| **S12** | epoll Reactor | `src/net/reactor.*`、`acceptor.*`、`connection.*`、`buffer.*` | 单线程 echo 服务端，`nc` 或 python 脚本能收发 1000 条 | 12h |
| **S13** | 编解码与粘包 | `src/net/codec.*` | 单测：一次读到半帧、一次读到三帧、超长帧被拒 | 6h |
| **S14** | 心跳与超时踢人 | `src/net/heartbeat.*`、`src/common/timer_wheel.*` | 客户端不发心跳 30 秒后被断开，服务端 fd 正确释放 | 6h |
| **S15** | 会话与登录 | `src/server/session.*` | 相同 token 重登会踢掉旧连接 | 5h |
| **S16** | 房间服务 | `src/room/` | actor-per-room + SPSC 队列；4 人加入 → 准备 → 开局 | 12h |
| **S17** | ⭐ 状态裁剪下发 | `src/server/broadcast.*` | 每个连接收到**不同字节**（私有手牌不同）；公共战报相同 | 6h |
| **S18** | 断线重连 | `src/server/reconnect.*` | 杀进程重启客户端后，快照 + 增量恢复，能继续打完 | 9h |

**M2 合计 ≈ 60h ≈ 12 周。累计 ≈ 25 周 ≈ 6 个月**（Godot 支线不占关键路径）
**⭐ 里程碑：S18 完成 = 可以开始投简历。此时项目已经能讲：epoll、协议、粘包、并发模型、状态裁剪、重连。**

---

## 阶段 M3~M5：从"能跑"到"能打"

| 阶段 | 内容 | 工时 | 何时做 |
|---|---|---|---|
| M3 | AI 托管/补位/难度档；超时自动托管 | 20h | S18 之后 |
| M4 | 内容扩充：区域 5、卡牌 20+、建筑 3 种、平衡调整 | 30h | 有人陪你玩之后 |
| M5 | MySQL 战绩 + Redis 排行 + 压测报告 + Docker Compose + Prometheus | 40h | 面试前 2 个月 |
| 支线 | Godot 客户端（护栏：≤70h，周末，不进关键路径） | 70h | S12 之后随时 |
| 支线 | 读 muduo 源码并写笔记 | 15h | 可与 M5 并行 |

---

## 三条执行纪律

1. **顺序不可跳跃。** 不在 S7（确定性）通过之前碰网络层；不在 S10（终端能玩）之前碰 Godot。
2. **每个 S 都是独立可提交的。** 一周只推进一小步也没关系，但要有一行 CHANGELOG。这个 CHANGELOG 就是面试时讲"演进过程"的素材。
3. **卡住超过 3 小时就降级，不要死磕。** 例：战斗公式算不平 → 先写死一个简单公式跑通流程，标 `// TODO: balance`，继续推进。完美主义是 5h/周的人最大的敌人。

---

## 已完成（第一批：S0 → S1 → S2）

现状：**S0 / S1 / S2 全部完成，35 个单元测试全绿，demo 可跑完 12 回合并正常判定胜者。**

| 交付 | 位置 |
|---|---|
| 离线构建脚本（g++，无 cmake 也能编） | `build.ps1` |
| CMake 构建（CI / WSL 用） | `CMakeLists.txt` + `.github/workflows/ci.yml` |
| 零依赖测试框架（本机无外网，拉不到 GoogleTest） | `tests/test_framework.h` |
| 数据模型：兵种/军队/领地邻接/12 张卡 | `src/logic/{types,region,card_def}.h` |
| 回合驱动：同时决策 → 结算 → 事件流 | `src/logic/{command,event,engine}.h` |
| 纯文本战报 | `src/logic/report.h` |

**明确未实现、且会显式报 `NotImplemented` 的缝隙**（不静默丢弃，避免"看起来对了但其实没算"）：
经济产出、建筑完工、移动/进攻、战斗结算。

下一步顺序：**S3 经济 → S4 合法性校验 → S5 战斗结算**。

---

## 环境踩坑记录（给以后的自己）

1. **本机无外网**：HTTPS 到 GitHub / PyPI 全部 TLS 失败，无代理端口。所以没有 cmake、没有 GoogleTest，
   改用 `g++ + build.ps1` + 自带测试框架。CMake 留到 CI/WSL 验证。
2. **Windows PowerShell 5.1 读无 BOM 的 UTF-8 会乱码**，`.ps1` 里出现中文会导致解析报错。
   → `build.ps1` 全文只用 ASCII。
3. **PowerShell 的 `$ErrorActionPreference='Stop'` 会把 g++ 的 stderr 警告变成致命错误**。
   → 脚本改成 `Continue`，靠 `$LASTEXITCODE` 显式判断。
4. 运行脚本需要绕执行策略：`powershell -ExecutionPolicy Bypass -File build.ps1 test`。
5. **git 自带的 ssh 读不到 `~/.ssh/known_hosts`**（`D:\Git\usr\bin\ssh.exe`，用户名含非 ASCII 时 HOME 解析不一致），
   表现为 `Host key verification failed` —— 但认证其实是通的，很容易误判成"key 没加"。
   修法（已配好）：`git config --global core.sshCommand "C:/Windows/System32/OpenSSH/ssh.exe"`。
6. **本机 HTTPS 到 GitHub 不通**（TLS 握手被重置，且 TLS1.2 也不行），只能用 SSH remote：`git@github.com:...`。
7. 新建仓库时勾了 README → 首次推送被拒（`fetch first`）。
   处理：`git rebase -X theirs origin/main`，保留对方的 Initial commit，冲突文件采用本地版本，历史保持线性。
