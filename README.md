# Creature Frontier

原创第一人称生物捕捉 RPG 原型。玩法重点是捕捉伙伴，并与伙伴共同生活、工作和战斗。

## Windows 独立原型

本机打包位置：`D:\myGame\Builds\Windows\CreatureFrontier.exe`。
双击即可进入测试地图，不需要先打开 Unreal Editor。复制到其他机器时需复制整个
`Builds\Windows` 文件夹，不能只复制根目录的启动 EXE。
这是 Development 灰盒原型，尚未完成正式美术、内容与性能优化。

重新打包和验证（在项目根目录执行）：

```powershell
.\Scripts\Package-Windows.ps1
.\Scripts\Test-Packaged.ps1
```

测试脚本直接运行打包后的程序，验证捕捉/工作、背包制作、伙伴成长照料、跨进程存档、导航绕障碍和近战遮挡。
每个测试必须出现日志成功标记，单看进程退出码不能判定通过。
构建产物与测试日志分别在 `Builds/Windows`、`Saved/PackagedTests`，均不提交 Git。

## 当前状态

Windows 开发环境已部署并验证：Unreal Engine 5.8、Visual Studio C++ 工具链、
.NET SDK 与 Git LFS 均可用。`CreatureFrontierEditor / Win64 / Development`
已成功编译，编辑器无界面启动验证通过。

工程骨架包含第一人称 Pawn、数据驱动的生物定义、可复制的生命/驯服/活动状态，
按生命值修正成功率的捕捉组件、基础野生宠物追击 AI，以及可直接游玩的灰盒测试地图。

## 原型操作

- `WASD`：移动
- 鼠标：视角
- `Space`：跳跃
- 鼠标左键：攻击准星指向的宠物
- 鼠标右键：捕捉准星指向的宠物（宠物生命值越低，成功率越高）
- `F`：召回所有伙伴跟随，并自动攻击玩家周边的野生宠物
- `E`：靠近出生点后方的工作台（8 米内），分配伙伴采集；到达后每 3 秒产出 1 资源
- `R`：重新开始当前关卡（玩家倒地后也可使用）
- `Esc`：退出游戏（退出前需自行按 F5 保存）
- `F5`：保存当前进度；`F9`：读取进度（包括重新打开游戏之后）
- `1`：工作台 8 米内消耗 3 材料制作 1 个捕捉装置
- `2`：工作台 8 米内消耗 5 材料制作 1 份治疗药
- `H`：使用 1 份治疗药恢复最多 40 生命（满血或倒地不消耗）
- `T`：基地工作台 8 米内，让存活伙伴返回基地休息，恢复生命和体力
- `P`：瞄准自己的受伤/倒地伙伴，消耗 1 份药恢复 40 生命；满血不消耗

伙伴成长：升到下一级需要 `当前等级 × 100` 经验，最高 20 级。
每升一级增加 20 生命上限和 2 点攻击，当前生命增加最多 20。
击败野生生物时，附近存活伙伴获得 25 × 敌人等级的经验；伙伴完成最后一击则获得 50 × 敌人等级。
敌人倒地只结算一次奖励。工作成功产出材料也会获得 1 经验。

工作每秒消耗 1.5 体力，伙伴攻击消耗 3 体力；耗尽会停止并休息。
基地休息每秒恢复 6 生命和 12 体力，野外原地休息只恢复 4 体力。
倒地伙伴不能自然复活，需先用 P 救治。F 恢复跟随/协战，E 重新分配工作。
野生生物会攻击距离更近的玩家或伙伴，注意伙伴生命。等级、经验、体力和休息分配都随存档保存。

背包含材料、捕捉装置与治疗药，每类最多 999。开局有 6 材料、5 装置和 1 份药。
每次有效捕捉尝试消耗 1 个装置，成功和失败均消耗；对倒地/已驯服目标、
超出距离、隔墙目标或冷却中的点击不消耗。伙伴工作持续提供制作所需材料。
背包随 F5/F9 保存和恢复，旧版存档会迁移材料并补充初始装置/药。

HUD 显示玩家生命、资源、伙伴数量、工作数量和准星目标的生命/捕捉概率。
宠物生命归零后无法捕捉；请先削弱再捕捉。当前迭代以单机为测试目标，
伙伴跟随、追击和前往工作台使用已烘焙的 Recast 导航网格，每 0.4 秒更新目标。
宠物近战必须满足三维距离和视线检查，墙体阻挡伤害。当前导航覆盖灰盒地面，
尚未包含动态建造后的网格更新、跳跃/攀爬或复杂群体避让；联机权威校验仍待完善。

修改测试地图障碍后，通过 Unreal 的 Build Paths 重建导航，或者执行：

```powershell
& 'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\myGame\CreatureFrontier.uproject' -run=pythonscript `
  -script='D:\myGame\Content\Python\build_navigation.py' -nullrhi -unattended -nop4 -nosound
```

`-CFNavTest` 在实际游戏世界验证方块绕行、跟随到达、远处工作台到达/产出，
并检查宠物隔墙不能伤害玩家、无遮挡近战能够伤害玩家。

存档保存玩家位置/视角/生命/资源，以及所有宠物的位置、生命、归属、数据定义、
活动状态与工作点。重复读档替换现有宠物，不会叠加伙伴。
存档位于 `Saved/SaveGames/FrontierSlot.sav`，是当前地图的单机快照；
手动按 F9 恢复，暂不自动加载。读取不兼容地图或版本时会保留当前世界并显示错误提示。
`R` 重开不删除磁盘存档，仍可通过 F9 恢复。

跨进程存档测试可分别添加 `-CFSaveWrite` 和 `-CFSaveRead` 启动游戏；
使用独立的 `FrontierAutomationSlot`，不覆盖玩家的 `FrontierSlot`。
`-CFInventoryTest` 验证制作扣费、资源不足、治疗上限、捕捉消耗和背包读档，
使用独立的 `FrontierInventoryAutomation` 测试槽。
`-CFCareTest` 验证战斗经验/升级、敌人伤害伙伴、倒地救治、基地休息和成长状态读档，
使用 `FrontierCareAutomation` 测试槽。

玩法冒烟测试（运行约 6 秒后自动退出，日志中应出现 `CF_SMOKE PASS`）：

```powershell
& 'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\myGame\CreatureFrontier.uproject' /Game/Maps/L_Prototype `
  -game -nullrhi -nosound -unattended -nop4 -CFSmokeTest -log
```

打开工程后载入的默认地图为 `/Game/Maps/L_Prototype`，点击工具栏 **Play** 即可测试。

## 打开项目

直接双击 `CreatureFrontier.uproject`，或者使用生成的 `CreatureFrontier.sln` 调试。

命令行编译：

```powershell
& "D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat" `
  CreatureFrontierEditor Win64 Development `
  -Project="D:\myGame\CreatureFrontier.uproject" -WaitMutex
```

运行 `powershell -ExecutionPolicy Bypass -File .\Scripts\Check-Environment.ps1` 可检查依赖。
详细范围与里程碑见 `Docs/GAME_DESIGN.md` 和 `Docs/ROADMAP.md`。
