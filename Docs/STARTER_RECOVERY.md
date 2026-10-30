# 冒险返家后的休息、治疗与补药

## 真实来源与普通操作

输入为 `Saved/StarterRescueQA/20261030-224713-96a1390d/StarterRescueDone-generated.sav`，SHA-256：`12668FC8502C956E8F392A300E07F91A233C452963C49497EE539163C8B4C5EB`。它是原倒地伙伴实际救起并安全返家的完成档，不是满资源夹具。玩家生命79.2、伙伴42／90生命及约35体力，药品0、材料0、食物2、种子1、捕捉器7；原农圃与穿戴装备保留，伙伴经验12、亲密3、避战开启，首领目标未完成。

本次连续链复用原规则：

1. 在原采集台附近T让原伙伴休息。伙伴实际走近设施，在220厘米内按原每秒6生命／12体力恢复，不在野外直接回血。保存真正休息任务中途档。
2. 玩家实际走到原再生灌木，M按共享采集冷却实采10材料，原节点14→4；玩家采集不增加伙伴经验。
3. 返回原设施附近，按两次2，每次5材料制作1药，共支付10材料得到2药。H消耗1药将玩家恢复到100生命；再按H，原满血拒绝且保留余1药。
4. 原伙伴满生命／体力恢复后F归队，保持避战。原农圃、粮种、装备、伙伴身份／昵称／经验和旅程进度不变；双方实际位置、生命、体力、补给存读一致。

材料账为10真实采出＝2药×5成本；药品账为2制出＝1治疗＋1备用。没有生成资源、改配方、提高恢复速度、关闭碰撞、暂停野生生态或瞬移角色。普通游戏规则与玩家文案未修改，仅新增从真实来源衔接原玩法的验证入口；引擎绑定／固定导航路线驱动仍不等于完整人类游玩验收。

## 编辑器证据

模块 SHA-256：`3EB3C1EA816C62D14D2E702346C9192C882C09E75E0DD90AB39E39F6B1F4D7E9`，`Saved/Build-StarterRecovery-20261030.log` 编译退出0。

- `Saved/StarterRecoveryQA/20261030-225603-946074ec/`：1280×720整链，原T休息、源节点实扣10、两次制作／一次治疗／满血拒绝、家园和补给保留及存读通过。玩家706.1厘米、伙伴564.6厘米、野生生物2033.0厘米；最终玩家100、伙伴90／90生命及100体力、材料0、药品1。Resting／Gathered／Resupplied／Recovered四张新图全部逐张目视，所测中文字体、换行、既定省略和边界可读；摄像机近距离伙伴取景仍是验证灰盒，不是最终镜头品质。
- `Saved/StarterRecoveryQA/20261030-225638-fbb54fa5/`：从自身T休息中途档独立进程恢复，先核对双方源位置和原设施休息任务，再采料、制药、治疗、满血拒绝及归队存读通过，不重复下达T。

## Windows与剩余范围

Windows新包与自身整链／中途恢复已通过当前限定验收，旧包保留。因为C盘不能容纳同时新增的暂存／归档，使用E盘新暂存目录和C盘新归档目录，未删除旧构建。首次 `Saved/Package-StarterRecovery-20261030.log` 在编译后烹饪阶段退出25：Zen服务创建Windows烹饪oplog返回 `Insufficient Storage (507)`，引擎报无法建立oplog，不能算打包成功。已读取本机 `CookCommandlet.cpp` 的 `SkipZenStore` 分支和AutomationTool的附加烹饪参数处理，第二次调用仅追加 `-AdditionalCookerOptions=-SkipZenStore`，改用本次松散文件烹饪后打包，不修改项目或全局设置、不清理旧缓存；第二次日志独立保留为 `Saved/Package-StarterRecovery-Loose-20261030.log`，BuildCookRun成功、退出0。

Windows入口：`C:\CreatureFrontierBuilds\Windows-StarterRecovery\CreatureFrontier.exe`；内部程序 SHA-256为 `D3F3439C5CE55C49F0760C1FA31587D5E4DDA44A6E6C767BB2925A3A465DB631`。

- `Saved/StarterRecoveryQA/20261030-230045-41e21610/`：Windows960×540整链通过，玩家701.2厘米、伙伴393.9厘米、野生生物1645.8厘米；原T设施休息、M采10／源节点14→4、原2制作两药／消耗10材料、H治疗100生命／留1药及满血拒绝、伙伴90／90生命与100体力归队、原家园／补给及双方位置存读均通过。四张新图全部逐张目视，中文字体、换行、省略和边界可读。
- `Saved/StarterRecoveryQA/20261030-230122-0d5a920e/`：同一Windows自身T休息中途档独立恢复、位置和休息目标核对、实际采料制药及治疗存读通过，无图形预览。
- `Saved/FarmJourneyQA/20261030-230152-9411df1d/`：同一Windows原旅程推进、拒绝、产出、存读／迁移和农业五查询回归通过。
- `Saved/StarterRescueQA/20261030-230209-b2777ec1/`：同一Windows从原首轮真实倒地现场重新执行已修复的救援链，有限3药、两原地图锚点、避战、安全返家与存读回归通过；没有重试此前未修复的普通接敌链。
- `Saved/StarterRecovery-Root-Smoke-20261030.log`：普通根入口无渲染启动退出0，仅启动退出范围。

797条中文文字／27条脚本提示、脚本语法及差异空白检查通过；未改展示资产字段，36份原资产／地图哈希未变。四次恢复专项16项原槽核对通过，见Windows整链目录 `SlotsReview.csv`。源救援档哈希保持原样，普通玩家档不在写入范围。Windows真实恢复完成档为 `Saved/StarterRecoveryQA/20261030-230045-41e21610/StarterRecoveryDone-generated.sav`，SHA-256：`71A416859B2823D9C0C2F9CFA84B934BC027CBC9A8F56034E51029F02E992D0B`。

除首轮Zen存储空间导致打包失败外，本轮没有恢复玩法失败运行；不把不同程序、未执行图形预览或旧受控首领记录混作本轮完整体验验收。当前各盘剩余空间有限，后续重要打包前应先核对容量，不能擅删旧构建或用户缓存。

复核入口：

```powershell
& Scripts/Test-StarterJourney.ps1 -Recovery -ReadInputPath '<真实StarterRescueDone-generated.sav绝对路径>' -Preview -Resolution '1280x720'
& Scripts/Test-StarterJourney.ps1 -Recovery -Resume -ReadInputPath '<自身StarterRecoveryMid-generated.sav绝对路径>'
```

Recovery与农业、装备、接敌及救援模式互斥，必须提供真实输入；只写独立专项槽并在保留生成档后恢复原槽，普通玩家存档不在写入范围。新增断言核对现有药品目录仍为5材料／1药，不用硬编码成本替换普通配方。

尚未完成下一次真实出行、首领击败／捕捉／领奖或长期经营。避战仍保持开启，不把满生命和一瓶备用药宣称为首领战斗准备充分；有限灌木仍需后续再生／可持续采集。下一项优先改善受伤返家时的普通恢复提示，让玩家自行发现这些现有操作，而不是继续堆叠恢复测试矩阵。
