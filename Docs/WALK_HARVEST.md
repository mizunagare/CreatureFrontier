# 徒步采料接回伙伴施工

后续 `-WalkSupply` 已把本链的楼板存8、楼梯存9／补3改为实际徒步开箱及原E／回车输入，并处理缺料等待伙伴挡路，见 `Docs/WALK_SUPPLY.md`。下方三处直接库存事务的边界描述保留本首轮版本含义；未启用新选项仍运行原夹具，不回写旧程序验证结论。

## 实现与范围

`Scripts/Test-UpperDeckConstruction.ps1 -WalkHarvest` 在原二层施工整链中增加可选徒步采料段，复用既有导航、角色移动输入、慢行按键和按住M采集组件，不增加经济、背包或伙伴任务系统。

从真实37遮棚完成档恢复，同一伙伴、原箱和23个保留模块不变。原屋顶拆除返5材料；玩家按完整导航路径走到正常资源点，通过引擎注册的M按下／松开委托采15份，共20材料，再实际走回家园，接原楼板8份与楼梯12份施工、双方上下楼及存读。采集逐批核对节点储量与背包增量，按真实游戏时间逐帧检查玩家、伙伴连续位移，保留碰撞、正常制动、共享采集冷却和原伙伴AI。路径停滞五秒、输入中止、不可达或守恒异常均失败并留档，不瞬移重试。

该选项不能与 `-Read` 组合：携料中途恢复不重复采料。脚本保护原测试槽并恢复；失败和生成档保留在各自证据目录。`Run.csv`、`Results.csv` 增加 `WalkHarvest` 字段，协议标记保留英文。

## 复验

```powershell
& ./Scripts/Test-UpperDeckConstruction.ps1 -ReadInputPath D:/myGame/Saved/PlatformConstructionQA/20261030-101226-0a501af8/PlatformDone-generated.sav -StairWork -SupplyPause -WalkHarvest
```

独立Windows验证使用相同参数，另加 `-BuildRoot E:/CreatureFrontierBuilds/Windows-Walk-Harvest`。输入SHA-256为 `9278731B8C8336864FB58FA098A3F7CE687509BC7301A165B5F588A1892DF29D`。

## 验证记录

编辑器构建成功，模块SHA-256 `FDC745A43407F75BD0DFA951BFEA59C08DF33FCA485EF1D68917E6B10B570530`。`Saved/UpperDeckConstructionQA/20261030-142423-3fb938ae/` 整链退出0，徒步采料往返2933.1厘米、15份产出，缺料等待五秒不耗施工体力，补料后楼梯完成，25模块／0材料／1种子、经验8／亲密9及原身份存读通过。此首次记录早于脚本CSV新增字段，以实际启动命令与日志中的 `CF_UPPER_DECK_WALK_HARVEST PASS` 为模式证据，不追写历史CSV。

独立Windows包位于 `E:/CreatureFrontierBuilds/Windows-Walk-Harvest/CreatureFrontier.exe`，内部程序SHA-256 `9E5EFB96261B5376ED39D6EBE66A01D1DB9CA86D99F491FE6A618493316FA1AD`，UAT退出0。`Saved/UpperDeckConstructionQA/20261030-144449-9a716a3b/` 自身整链退出0，徒步2932.9厘米，15份采集、返家、原伙伴施工／补料、上下楼及存读通过；不是沿用编辑器结果。

同一Windows包使用自身 `UpperDeckStairMid-generated.sav` 独立恢复，`Saved/UpperDeckConstructionQA/20261030-144558-c9bc06aa/` 退出0，携3份／玩家保留3份、原位置、缺料等待补给和完工返回通过。三个进程的21次测试槽恢复逐项复核；源码中文检查686条及资产脚本提示27条通过。普通根程序无专项参数的空渲染启动／退出检查正常退出0，日志为 `Saved/WalkHarvest-Root-Smoke-20261030.log`，不当作持续游玩证据。脚本语法通过，`-WalkHarvest -Read` 实际在启动前中文拒绝。没有新增自动CI。

## 尚未完成

只在采料往返段移除固定观察站位及直接采集调用；初始拆顶、后续排工地、存料／补箱仍使用原受控夹具。M为引擎绑定委托，不是操作系统键鼠验收。无关野生AI仍隔离，不代表自然风险、任意房型、全程连续自然游玩、长期经营或最终品质。没有新增玩家文案或资产字段，不用历史截图代替新程序图形验收。下一主线把同一家园的建造、开箱补料与照料也接入连续行走及原输入。
