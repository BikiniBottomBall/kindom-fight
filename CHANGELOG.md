# 进度记录（面试时讲"演进过程"的素材）

格式：`S<编号> 一句话做了什么 —— 验证方式`

## S0 工程地基
- 目录骨架、`.gitignore`、`build.ps1`（本机无 cmake，用 g++ 直编）、`CMakeLists.txt`（留给 CI/WSL）。
- 自带极简测试框架 `tests/test_framework.h`（约 80 行，零依赖）：本机离线拉不到 GoogleTest。
- 验证：`powershell -ExecutionPolicy Bypass -File build.ps1 test` 全绿。

## S1 核心数据模型
- `logic/types.h`：兵种（长枪/盾卫/骑手）+ 三角克制 + 军队（每种兵的数量）。
- `logic/region.h`：5 个区域组成环，节点 + 邻接表，不是格子（设计稿四条红线）。
- `logic/card_def.h`：12 张卡的数据驱动目录（兵种/战术/经济/阴谋）。
- 验证：初始局面能打印成表，区域邻接与卡牌目录有单测覆盖。

## S2 回合驱动
- `logic/command.h`：指令 = 玩家意图（不是事实）。
- `logic/event.h`：事件 = 服务端判定后的事实，带 `serialize()`（决定论比对用）与 `describe()`（战报）。
- `logic/engine.h/.cpp`：同时决策 → 提交 → 结算 → 产出事件；未提交者按超时托管处理。
- 结算顺序固定：产出 → 完工 → 指令 → 战斗 → 胜负。
- 胜负判定：控制 3 块领地 / 对手全灭 / 12 回合分数最高（同分取 id 小者）。
- 验证：4 份空指令连跑 12 回合不崩、正常结束；同种子同指令两次结算事件流逐字节相同。
- 未实现（预留缝隙，明确报 `NotImplemented` 而不是静默丢弃）：经济产出、建造、移动/进攻、战斗结算。

## S3 经济
- 每块被占领的区域每回合产出 `gold_yield + 建筑等级` 金币；`troop_yield` 作为该区域的**每回合招募上限**。
- 建造：花 4 金，等级 +1（上限 2），每级 +1 金币产出。
- 抽牌：每回合 1 张，手牌上限 5，牌库空了自动把弃牌堆洗回来（用引擎内的确定性 RNG）。
- 校验：`BuildingMaxed` / `NotEnoughGold` / `RecruitLimit` / `NotEnoughAp`。
- 验证：产出、建造、金币不足、满级、招募上限各有单测。

## S4 招募 / 调动
- 招募：花 1 金/兵，受区域 `troop_yield` 上限约束（每回合每区域独立计数）。
- 调动：只能在自己的相邻区域间进行，按 `长枪→盾卫→骑手` 固定顺序抽兵（顺序固定才有决定论）。
- 指令执行顺序：发育类（建造/招募/暗置）按玩家顺序即时执行 → 调动按（起点区, 玩家）排序 →
  进攻按（目标区, **轮转玩家顺序**）排序（固定顺序会让后手永远捡残兵，是系统性偏差）。
- 验证：转移正确、打敌方区域被拒、进攻超出兵力被拒、AP 消耗正确。

## S5 战斗结算（确定性，无骰子）
- `战力 = Σ 各兵种数量 × 对"敌方主力兵种"的克制系数`（125 / 100 / 80）。
- 空城直接占领无伤亡；战力相同判守方胜（抑制无脑早攻）；败方全灭；胜方按战力比 × 50% 伤亡（夹在 5%~95%）。
- 战术卡：决策阶段暗置（不花金币、不花 AP，消耗手牌），结算时揭晓并给己方 +X% 战力；目标区域没打仗则白扣一张。
- 验证：`tests/test_combat.cpp` 11 个用例，含克制方向、平手判守方、加成可翻盘、胜方不会同归于尽、结算可复现。

## S6 占领 / 淘汰 / 胜负
- 攻方胜 → 区域易主、存活部队进驻；守方全灭。
- 淘汰：无领地且无兵力。胜利：控制 3 块 / 只剩一人 / 回合用尽分数最高（同分取 id 小者）。
- 中立领地（中枢）开局带 8 民兵，不是白送。

## S9 AI（提前做，因为它是平衡工具的前提）
- `src/ai/simple_ai.cpp`：预判必胜且损失不过半才进攻；**家里至少留 1 兵**
  （第一版全压导致老家被邻居白捡，对局变成纯快攻）。
- 一个模块三处复用：单机对手 / 匹配补位 / 掉线托管。

## S10 终端客户端（你 vs AI）
- `client/cli/main.cpp`：地图表、手牌、指令解析（`b/r/m/a/t/c/done/help/quit`）、
  回合结算战报、终局面；支持 `--seed` 与 `--players 2~4`。
- 客户端不含任何规则常量：所有数字都从 `GameState` / `CardDef` 读；
  校验只做"少让人白按一次键"级别的检查，合法性仍全在引擎里。
- 中文按字节宽度对齐会错位，所以自己算显示宽度（CJK 记 2 列）再补齐。
- 指令回显按类型显示有效字段（建造只显示区域，不再显示无意义的 `→-1`）。

## 本轮修掉的 bug
- **行动点发放时机错了**：原本在 `begin_turn`（结算开头）发放，导致玩家提交指令时
  看到的是上一回合剩下的 `AP 0`，AI 也会隔一回合不动一次。改为在 `end_turn` 发放
  "下一回合的 AP"，并加了两条回归测试锁住（`ActionPointsAreReadyForTheUpcomingTurn`、
  `AiSeesActionPointsEveryTurn`）。
- 开局 `ap = 0`：AI/客户端在第一回合提交前读到 0 点，白白空过一回合。

## 实测发现（未解决，见 docs/02-game-design.md §9）
- 4 个 AI 对局 **3 回合就结束**（目标 8~12 回合）。胜利门槛从 3 块提到 5 块，对局也只从 3 回合延长到 5 回合
  → 瓶颈是"征服速度"，不是门槛。原因是进攻收益（+1 领地 = 永久 +2 金/回合）远高于发育收益（+1~2 兵/回合）。
- 下一步：参数搬进运行时 `GameConfig` → 做自动对局统计 → 用数据验证假设，而不是继续拍脑袋调数值。

## S11 传输与协议分层
- `src/net/protocol.h`：13 字节头 + 大端序，`magic | version | cmd | seq | payload_len | payload`。
- **传输与协议解耦**：长度前缀（TCP）只是外壳，以后接 WebSocket 只换编解码层。
- **`seq` 是幂等地基**：断线重连后客户端重发未确认指令，服务端靠它去重。
- 单帧 payload 上限 64KB，超限直接判非法帧（防止"声明 2GB"式攻击）。

## S12 epoll Reactor + 跨平台 poller
- `src/net/socket.*`：socket RAII 封装。Windows 的 SOCKET 是 UINT_PTR，所以句柄统一用
  `intptr_t`；`INVALID_SOCKET` 展开成 ~0，转成 intptr_t 正好 -1，两个平台共用一个哨兵。
- `src/net/poller.*`：poller 抽象 + **两套实现**
  - Linux：`epoll`（**水平触发 LT**）。没用 ET：ET 要求每次必须读到 EAGAIN，
    漏读一次该连接就永久饿死，且偶发难复现。先保证正确，压测阶段再对比 ET 收益。
  - Windows：`select`（也是 POSIX 兜底）。O(n) 拷贝/遍历 + FD_SETSIZE 限制 —— epoll 存在的理由。
  - `KF_POLLER=select` 可强制降级，便于在 Linux 上做两种实现的同组用例对比。
- `src/net/channel.*`：socket + 帧解码器 + 写队列；非阻塞 send 只写出一部分也不丢数据。
- `tools/echo_server.cpp`：不玩牌，只验证"能收能发能正确关闭"。

## S13 编解码（粘包 / 半包 / 超长帧）
- `src/net/decoder.*` + `src/net/byte_buffer.h`：缓冲 + 状态机切帧。
- 12 个单测：一次三帧、逐字节喂、payload 中间断开、magic 错、版本错、
  声明 2GB、失败后不复活、64KB 边界。
- **双平台实测**（这是本轮的验收重点）：

  | 平台 | poller | 单测 | 真 TCP 冒烟 |
  |---|---|---|---|
  | Windows | select | 78/78 | 完整帧 / 半包 / 粘包 全 PASS |
  | WSL Ubuntu（g++ 15.2） | **epoll(LT)** | 78/78 | 三个用例全 PASS |
  | WSL Ubuntu（KF_POLLER=select） | select | — | 三个用例全 PASS |

- 新增 `build.sh`（Linux/WSL 构建）与 `tools/smoke_net.sh`（网络冒烟，python3 当客户端）。

## 跨平台踩坑记录
- `size_t` 在 Windows 是 `unsigned long long`、Linux 是 `unsigned long`，
  测试框架的 `to_debug` 写具体重载会在某个平台上歧义 → 改成整型模板。
- Linux 下 `TCP_NODELAY` 需要 `<netinet/tcp.h>`；`select` 的 `timeval*` 不是 const。
- Windows 下 `WIN32_LEAN_AND_MEAN` / `NOMINMAX` 重复定义会产生警告 → 加 `#ifndef` 守卫。

## 质量基线
- 78 个单元测试全绿（Windows + Linux 双平台）；事件流在"同种子 + 同指令"下逐字节可复现。
