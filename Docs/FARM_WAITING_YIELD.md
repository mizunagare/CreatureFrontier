# 空圃缺种工人安全让行

## 复现、原因与修复目标

旧Editor65F4BCD5在 `Saved/StarterHouseholdQA/20261031-140959-cfe190cd`、旧Windows76CA3AE9在 `20261031-141408-71f5fc2d` 读取同一中途档，均先完成耕作／取粮／新伙伴恢复，再在 `observer-walk phase=8` 卡住。输入SHA256为 `3AE5CCCA944F41B6285DBCF83DA0CF785E7DFC9D0866E6B839026FE1A88D71C6`，来源 `Saved/StarterHouseholdQA/20261031-134502-f30ffa46/StarterHouseholdMid-generated.sav`。玩家约(-933,-670)，工人约(-911,-605)。

静态导航路线经过工人胶囊；空圃缺种工人保留Work任务、正常停在农圃旁，而原安全让行只在Follow和施工缺料等待中调用。已有 `ApproachFarm` 能避开角色占据的接近路径，但不能让已经站好的工人为正面通行的主人侧步。修复目标是原路线通过且农业任务、收获／种子、身份、碰撞与存档保持，不替换测试路线来绕开失败。

## 实现与不变规则

仅在原Farming工作分支、`Crop->IsOccupied()==false`（无种且没有种植作物）时调用既有 `FindOwnerYield(...,true)`，由原 `NavigateTo` 实际移动。

- 主人必须同层近距、具有正面通行输入。复用原节流、有限候选、完整非部分路径、胶囊占据／逐段扫掠和同层检查；无安全位置时不强行移动。
- 不清WorkSite、不改Activity为Follow、不清任务，不改库存、作物、经验、亲密或生命。
- 有种、正在生长、成熟未收获、休息、战斗、野外工作与运输走原分支；不把让行扩成中断生产或强拉休息伙伴。
- 主人通过后原 `CanReachFarm/ApproachFarm` 使工人按原任务接近／等待，既有“农圃通路已恢复”反馈沿用原实现。
- 不改碰撞、角色位置、速度、地图、资产、恢复／农业收益或存档格式。

`-CFFarmYieldTrace` 只增加新让行目标的日志，不是行为开关。脚本增加 `-VerifyWaitingYield`，在原通过条件之外要求实际让行记录；图形模式额外保留Yielding图。截图沿原镜头、不中断移动、不改视角或路线。

## 最终Editor证据

程序SHA256 `1780E9D466932116489D463F79050CE16853CF5450A9389AD5D245BF047EE524`，构建 `Saved/Build-FarmWaitingYield-Preview-20261031.log`。

- 同一旧失败来源，图形 `Saved/StarterHouseholdQA/20261031-143302-34dfa421`、空渲染 `20261031-143419-998f818a` 均按原路径通过，不改5秒停步限制、运动连续性和生态／预算检查。
- 日志 `CF_FARM_WAIT_YIELD`：原工人 `C04DB81A4CABCB2CF76E178284EDCF0B` 从(-911.387,-605.145,40.150)选出旁侧导航目标(-996.739,-576.596,-40)，当时种子0、未种植、经验40；目标Z是原导航地面而非角色中心，不是设置角色位置。
- 最终食物3／种子1、旧经验40／新经验1、生命79.2／110／80保持；两伙伴原Work／Rest、原农圃、七模块／箱子／门和全部资源状态、双方ID／位置共同存读通过。
- Harvested／Yielding／Recovered三张1280×720逐张目视，原库存、农业状态和新伙伴休息文字可读。Yielding时工人靠镜头很近，主要看到叶耳，并非正式动作或镜头品质验收。
- 原WorkCycle逻辑 `Saved/FarmRegressionQA/20261031-143605-34fa8da9` 通过；上一轮休息选址自身中途恢复关联 `Saved/StarterRestAimQA/20261031-143658-62ceea4c` 通过。逻辑夹具不作为真实移动证据。

## 中间记录与当前范围

首轮构建 `Build-FarmWaitingYield-Editor-20261031.log` 暴露角色源码对HomeModule的显式包含依赖，按编译错误补齐；不改建筑实现。B413185A中间程序已在 `20261031-142933-faef1188` 通过同一旧失败来源，之后仅增加QA通行截图钩子，最终证据采用上述1780E9D4重做结果，不混用。

## Windows独立证据

- 入口 `F:\CreatureFrontierBuilds\Windows-FarmWaitingYield\CreatureFrontier.exe`，内部程序SHA256 `BE3CAF2E9AD5B7176FECC41CB56D74FC0C11B0636FCC707212FE5A985AA1FEC6`；构建 `Saved/Build-FarmWaitingYield-Windows-20261031.log`。36份资产哈希不变，原生候选复用上一候选不可变文件，不是重新烹饪归档，勿直接改写共享文件。
- 与新旧程序曾失败的**同一来源3AE5CCCA**对照：空渲染 `Saved/StarterHouseholdQA/20261031-144147-565bb365`、图形 `20261031-144243-537219e1` 均通过，要求让行标记且保留原路线／停步限制。日志中的原工人、侧步目标、种子0／未种植／经验40与Editor一致。
- Windows自己的并行中途档另行读取：`Saved/StarterHouseholdQA/20261031-144354-81766cae` 通过，输入为 `20261031-135053-b448554f/StarterHouseholdMid-generated.sav`、SHA256 `EBCDD5F0A486E0600B9B8EB46DFA4F9453635BDD282C0524E93916E4DAE7ED2F`。此项不启用让行诊断标记要求，说明正常行为不依赖诊断开关；保留Windows玩家28生命／药2，未混用对照来源的79.2／无药状态。
- 原WorkCycle规则 `Saved/FarmRegressionQA/20261031-144438-d9f496f3`、休息选址自身中途恢复 `Saved/StarterRestAimQA/20261031-144532-337c9c54` 通过。
- Harvested／Yielding／Recovered三张1280×720逐张目视，原工作、经验、库存、恢复状态可读；同样有极近叶耳遮挡，不当作最终镜头／动画验收。850条中文文字＋27条脚本提示静态通过。

这只修复已复现的空圃缺种等待通路，不把两端受控结果当作完整自然游玩验收，也不宣称所有休息伙伴、战斗拥堵、窄门和大队伍都会让路。让行中的专用跨进程检查点未新增；原中途档读取、完成后的双任务与位置存读已验证，不虚称所有瞬间都做过冷启动。

复验：`Scripts/Test-StarterHousehold.ps1 -ReadInputPath <上述独立中途档> -Resume -VerifyWaitingYield`；加 `-Preview` 检查三图，加 `-BuildRoot <候选目录>` 选择Windows。专用槽保护恢复及来源指纹沿用原脚本，原失败档与备份保留。
