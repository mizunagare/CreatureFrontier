# 受伤后的恢复操作提示

## 普通玩家变化

第五项目标尚未完成时，原提示新增只读恢复建议，优先于装备制作／区域方位提示：

- 玩家受伤且没药：提示设施附近T让伙伴休息、M采料、设施附近2制药，成本读取原药品目录。
- 玩家受伤且有药：提示H消耗1药治疗自己，以及伙伴可瞄准P用药或在设施附近T休息。
- 玩家已恢复而存活伙伴受伤：提示T设施休息／P治疗／恢复后F跟随；伙伴正在恢复生命或体力时说明继续休息。
- 不再需要上述恢复建议时，返回原装备和水晶高地方位提示。

玩家倒地仍走既有倒地反馈和复活操作，本次不改写该流程；不把恢复建议当作战斗／制作／奖励的新许可条件。已经完成首领目标时仍先提示N领奖，即使受伤也不拦奖励。药品、伙伴休息、资源成本和存档格式38均不改。

## 本轮真实来源与编辑器证据

沿用原真实药品耗尽的安全返家档 `Saved/StarterRescueQA/20261030-224713-96a1390d/StarterRescueDone-generated.sav`，SHA-256：`12668FC8502C956E8F392A300E07F91A233C452963C49497EE539163C8B4C5EB`。复用原恢复驱动，不发新补给：T伙伴休息，玩家M实采10材料、两次2制药、H治疗并满血拒绝、留1药、F归队和存读。

在实际制作后、H前增加MedicineReady取景，断言缺药／有药治疗／恢复探索三种真实提示，既有原料、生命和家园预算继续核对。没有为提示而假造受伤或冻结角色；仍是引擎绑定／固定路线整合验证，不是人类自主游玩。

编辑器模块 SHA-256：`BF94365CBA2094BDBC8971326CE75E9F851B50B95648F7738695ED4B014A1C54`，构建日志 `Saved/Build-RecoveryGuidance-20261030.log` 退出0。

- `Saved/StarterRecoveryQA/20261030-230944-e5a6b713/`：1280×720恢复整链、三提示查询、原料／药品实付、满血用药拒绝和位置／生命／家园存读通过。玩家实走702.3厘米、伙伴559.7厘米、野生生物1987.0厘米。Resting／Gathered／MedicineReady／Resupplied／Recovered五张新图全部逐张目视，所测中文字体、换行、既定省略与边界可读。
- `Saved/StarterRecoveryQA/20261030-231207-9ffd6c08/`：自身T休息中途档独立恢复，原位置／任务核对、三提示与实际采料制药治疗／归队存读通过。

伙伴单独受伤、玩家已满血但伙伴仍在休息以及药品目录缺失的回退分支尚未专项图形验收；不把上述三真实状态扩大为全部动态组合或任意窗口验收。

## Windows与存储处理

交付已实际运行验证的完整Windows暂存构建：`E:\CreatureFrontierBuilds\RecoveryGuidanceCompact\Stage\Windows\CreatureFrontier.exe`，内部程序 SHA-256为 `7E230CEEB6FFA7171007A91502103413B4EB46ACD1087EEADBA6E1224453EA4D`，与本次编译输出哈希一致。独立归档没有完成，不能把本次BuildCookRun宣称为全流程成功。采用本机引擎支持的 `-nodebuginfo`（不拷贝调试符号到暂存）和上轮已核验的 `-AdditionalCookerOptions=-SkipZenStore`。

三次打包处理分别保留日志：

- `Saved/Package-RecoveryGuidance-20261030.log`：本次程序编译、烹饪完成，写C盘容器时空间不足，退出3。C盘新失败暂存仍保留，没有清理旧构建。
- `Saved/Package-RecoveryGuidance-Restage-20261030.log`：复用本次已完成的编译／烹饪，将暂存换到E盘；复制程序到压缩目录时仍报空间不足，退出1。取消该新Win64目录压缩后，复制本次程序并核对完整哈希，不采用截断文件。
- `Saved/Package-RecoveryGuidance-Archive-20261030.log`：`-nocleanstage` 保留已核对内容，暂存命令明确 `STAGE COMMAND COMPLETED`，随后独立归档复制在C盘耗尽时继续重试。已核对并仅停止该次AutomationTool进程，命令退出-1，保留完整E盘暂存。C盘 `RecoveryGuidanceCompact\Windows` 是**不完整归档，不可作为入口**；其中本次程序移至 `D:\myGame\Saved\BuildArtifacts\RecoveryGuidanceIncompleteArchive\CreatureFrontier.exe` 保存并核对哈希，以恢复C盘余量，未动旧游戏程序或存档。

新专用目录曾启用NTFS压缩；确认大程序复制存在空间压力后，将E盘新Win64目录和C盘不完整归档的Paks目录取消压缩，并将E盘新主ucas取消压缩。文件内容不变。最终交付以完整暂存的运行结果为准，非不完整归档，也不以压缩目录存在视作可运行证据。

Windows当前验证：

- `Saved/StarterRecoveryQA/20261030-232531-0346c736/`：960×540整链通过，玩家696.1厘米、伙伴408.4厘米、野生生物1587.4厘米；原10材料采集与两药制作、治疗／满血拒绝、三提示查询、家园与位置／生命存读通过。最终双方满生命／伙伴满体力、材料0、药1、食物2、种子1，伙伴避战保持，首领未完成。五张新图已全部逐张目视，中文字体、换行、省略和边界可读。
- `Saved/StarterRecoveryQA/20261030-232602-dc8fe97e/`：同一Windows自身休息中途档独立恢复、源位置／任务、三提示和恢复整链存读通过，无图形预览。
- `Saved/FarmJourneyQA/20261030-232647-0b11b49b/`：原旅程推进、拒绝、产出、存读／迁移及农业五查询回归通过；完成首领目标时仍先N领奖，不被恢复建议拦截。此受控记录不是本轮首领实战证据。
- `Saved/StarterPrepQA/20261030-232713-b329b7f6/`：原真实农业档的装备准备整链与五装备提示回归通过；健康状态下原准备建议未被恢复建议遮掉。
- `Saved/RecoveryGuidance-Root-Smoke-20261030.log`：完整暂存普通根入口无渲染启动退出0，仅启动退出范围。

805条中文文字／27条资产脚本提示、开发脚本语法与差异空白检查通过，36份原资产／地图哈希未变。四次当前恢复运行16项专项槽恢复通过，见Windows图形整链目录 `SlotsReview.csv`；源真实返家档哈希未变，普通玩家档不在测试写入范围。Windows真实完成档为 `Saved/StarterRecoveryQA/20261030-232531-0346c736/StarterRecoveryDone-generated.sav`，SHA-256：`6B81CE1221863F06F2023C9390726AB362D0A89B85E5D9F41F56DDDA02C34916`。

本轮启动时C盘只有约28MB，直接无损压缩调试符号也因空间不足失败。将自己上一轮生成的 `C:\CreatureFrontierBuilds\Windows-StarterRecovery\CreatureFrontier\Binaries\Win64\CreatureFrontier.pdb` 移至 `D:\myGame\Saved\BuildSymbols\Windows-StarterRecovery\CreatureFrontier.pdb` 并核对哈希；系统未允许创建原路径符号链接，所以调试符号现位于备份路径，不宣称原符号路径仍存在。旧游戏可执行文件仍在原处，SHA-256保持 `D3F3439C5CE55C49F0760C1FA31587D5E4DDA44A6E6C767BB2925A3A465DB631`，普通玩家存档未动。

该备份符号、Windows-PlacedHabitat／StarterPrep／StarterFarm／StarterJourney／Orientation符号、C盘Staging-PlacedHabitat符号与E盘Staging-StarterRecovery／Windows-Walk-Supply／Windows-Walking-Home符号做了NTFS无损压缩，内容哈希核对保留。归巢旧包符号与原暂存副本也核对一致。Windows-Crafting-Guidance符号压缩因空间不足未成功，不算成功压缩。一次E盘暂存符号路径缺少Windows子目录而未找到文件，核对实际路径后才压缩，未删除文件或清理用户缓存。新候选不附带调试符号，项目编译输出中的符号仍保留，不能把精简包当作携带完整符号的调试分发。

## 复核与下一步

```powershell
& Scripts/Test-StarterJourney.ps1 -Recovery -ReadInputPath '<真实StarterRescueDone-generated.sav绝对路径>' -Preview -Resolution '1280x720'
& Scripts/Test-StarterJourney.ps1 -Recovery -Resume -ReadInputPath '<自身StarterRecoveryMid-generated.sav绝对路径>'
```

提示只帮助发现已经接通的恢复操作，不等于满生命／1药足以战胜首领。下一主项回到真实恢复完成档的探索与伙伴战斗，评估可行路径、主动指令和撤离条件，不继续堆叠孤立恢复菜单状态。首领完成、完整自然新手体验、长期经营与最终品质仍未完成。
