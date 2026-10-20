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
