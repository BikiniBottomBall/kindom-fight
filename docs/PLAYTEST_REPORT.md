# 试玩自检报告

> 这份报告记录"这个 demo 到底能不能玩完一局"的实际证据，命令、输出、截图都可复现。
> 每次大改之后应该重跑一遍。

## 0. 最近一次复核（2026-11-17）

环境：Windows 11 + MSYS2 UCRT64 g++ 15.2 / Godot 4.7.2（Steam 版）。
**服务端这次直接跑在 Windows 上**（`poller=select`），不再需要 WSL ——
`demo.ps1` 注释里"本机 Device Guard 会拦未签名 exe"的说法已经过时，新编出来的 exe 能正常跑。
WSL 现在只是可选的另一条路径（那条走真正的 epoll）。

| 套件 | 命令 | 结果 |
|---|---|---|
| C++ 单测 | `powershell -ExecutionPolicy Bypass -File build.ps1 test` | **130 / 130 通过** |
| 服务端端到端 | `./build.sh smoke` | 20 秒内 401 个快照，逐字段对齐；出牌 9 接受 / 1 被拒 |
| 节奏跑分 | `kingdom_fight_bench --matches 12 --shipping --check` | 平均 405.5 秒、超时 8.3%（91.7% 打到只剩一家），门禁通过 |
| 确定性 | 同种子跑两遍 `diff` | 完全一致（逐字节） |
| 客户端协议 | `bash tools/run_godot_tests.sh` | 51 / 51 + 56 / 56 + 美术加载正常 |
| 整局（`--fast`） | 服务端 `--fast` + 客户端 `--autodeck --bot` | 165.1 秒打出 `base_destroyed`，客户端 stderr 干净 |

## 1. 自动化测试

| 套件 | 命令 | 结果 |
|---|---|---|
| C++ 单测 | `wsl bash -lc "cd /mnt/d/Code/DSH/*Game* && ./build.sh test"` | **130 / 130 通过** |
| 客户端协议 | `bash tools/run_godot_tests.sh` | **51 / 51 通过**（帧格式 / 粘包 / 黄金向量） |
| 客户端快照 | 同上 | **56 / 56 通过**（逐字段断言） |
| 美术加载 | 同上 | 8 单位 / 6 砖块 / 基地 全部命中 |

## 2. 手动流程自动验收（`--uitest`）

模拟真人操作：**开始页 → 构筑界面选 8 张 → 开始对局 → 连出 7 张牌**。

```
[uitest] 卡池=true 张数=40 开始页可见=true
[uitest] 构筑界面可见=true 卡牌按钮=10
[uitest] 已选=8 确认禁用=false
[uitest] 开局=true
[test] 出牌回执 slot=0 result=0
[uitest] 出牌回执=0（0 = 服务端接受）
...（连续 6 次全部 result=0）
[uitest] 连续出牌=6 局内=true 剩余=163s 我的基地=6500
```

## 3. 整局试玩（`--fast` 180 秒 + hard AI）

命令：

```powershell
# Windows 本机直接跑（也可以走 WSL 里的 ./build.sh）
.\build\bin\kingdom_fight_server.exe --port 9935 --fast --ai-level hard
godot --path client/godot -- --autodeck --bot --port=9935 --shot=build/shots/result.png --shot-delay=185
```

结果：**正常打到结算页**，无脚本报错，客户端日志：

```
[audio] 已加载 8 个音效
[client] 收到卡池 40 张
[client] 地图 240x160，联盟 [0, 1, 2, 3]
[client] 开局：180 秒，总部支援 60 秒解锁
[client] 首个快照 tick=0 ...
```

结算页（`build/shots/result.png`）：**胜利** · 时间到按基地血量判定 · 我 6500 / P1 5200 / P2 5200 / P3 0，
按钮不重叠、可点击。

## 4. 覆盖到的系统

| 系统 | 验证方式 |
|---|---|
| 开局镜头对准自己基地 | 截图（基地在画面中间） |
| 卡牌抽/出动画 + 牌堆计数 | 截图 + `--uitest` 出牌回执 |
| 工地建造时间 + 进度条 | `BaseExposedUntilTowerIsBuilt`（塔工地完工才开火）+ 快照 `progress` 字段断言 |
| 工程兵加速 / 清障 / 维修 | 加速：`EngineersSpeedUpConstruction`（有工程兵的工地完工更快）；维修：`RepairCostsMaterielAndRestoresHp`；清障只有手测 |
| 手动修复 | 单测 `RepairCostsMaterielAndRestoresHp` |
| 领地扩张 + 建筑上限 | `NodesGetCapturedWhenArmiesMarch`（占下片区后上限必须 > 6）+ 快照 `territory` / `building_cap` |
| 拆除释放槽位 | `DemolishFreesTowerSlot`（槽位满了拒绝、按 id 拆掉后立刻能再造） |
| 防御塔放置规则 | 单测 `TowerNeedsOwnTerritoryAndOpenGround` |
| 成长树（科技点升级） | 单测 `GrowthTreeBranchesAreExclusive` + 整页截图 |
| AI 难度 | `--ai-level hard` 整局跑通（本报告第 3 节） |
| 结算与重开 | 结算截图；`再来一局` 走 `C2S_Rematch` 通道（`smoke_rts.py` 也覆盖了这一条） |
| 音效 | `[audio] 已加载 8 个音效`；缺少 wav 时静音不报错 |

## 5. 已知问题（不影响"能玩完一局"，但记下来）

1. 工程兵只清森林/岩石、不填水，也不会规划"往哪清"（就近清）。
2. 局外 DIY 只保存一套配置（`user://profile.json`），没有多套方案。
3. `main.gd` 仍有 **2349 行**；成长树页已拆出，构筑界面/结算页待拆。
4. 4 人局里 AI 之间会互相消耗，有时玩家"躺着赢"（hard 难度也只是更频繁决策，不是更强策略）。
5. **测试缺口**：工程兵清障（每 4 秒清一格森林/岩石）只有手测，没有单测。
   加速、拆除释放槽位、领地抬上限三条已经补上（`EngineersSpeedUpConstruction` /
   `DemolishFreesTowerSlot` / `NodesGetCapturedWhenArmiesMarch`）。

## 6. 复现清单

```powershell
# 1) 清干净
Get-Process | ? { $_.ProcessName -like "*godot*" } | Stop-Process -Force
# 2) 服务端（本机，hard AI，180 秒一局）
.\build\bin\kingdom_fight_server.exe --port 9935 --fast --ai-level hard
# 3) 客户端（机器人代打，185 秒截图）
godot --path client/godot -- --autodeck --bot --port=9935 --shot=build/shots/result.png --shot-delay=185
```
