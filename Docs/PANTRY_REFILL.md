# 农业产出回补室内食槽（限定整合交付）

## 玩家流程与范围

沿用上一真实照料档：食槽剩2、背包粮0／种子1，原农夫仍在屋南原农圃Work。瞄准3米内农圃F2存入原1种子，伙伴按原效率播种／推进6工序／收获，O取出1食物；玩家实际绕行返屋，瞄准食槽U存入，把储粮补至3。保留原农夫和另一只伙伴的休息指派、身份与家园。

本项复用正常农业、容器事务、指派、移动和存档实现，只增加显式整合验证入口`CFPantryRefillTest`／`CFPantryRefillRead`及`Scripts/Test-PantryRefill.ps1`。没有修改普通经济、生产速度、碰撞或存档格式，没有生成物资、修改饥饿、传送玩家或暂停生态，也不按G手工推进生长。

## Editor证据

程序SHA256：`2719F21AFF3DD25D98394CF7E24B017251282D230F1FE64260D294FF5B71AE50`。

来源`Saved/StarterPantryQA/20261031-183150-53775291/StarterPantryDone-generated.sav`，哈希`4649E8AB63898C0E5D2461A5C911F3A77EAC0F2B9CE77108140334AE6D5F82F2`。

`Saved/PantryRefillQA/20261031-184740-72321f98`的1280×720完整图形链通过：种子1→0、正值生长约1.026工序时保存、伙伴自然收获1、农圃储粮1→0／背包粮0→1、玩家实走返屋存入、食槽2→3／背包粮1→0、原农夫经验40→41。原玩家伤势79.2／药品1、另一伙伴经验18／普通休息、三设施／七模块及资源不变，最终身份／位置／库存／任务存读通过。Growing／Harvested／Restocked三图已逐张目视，生长进度、背包粮1、3/30储粮与操作可读，无文字越界；方块农圃／食槽、程序化伙伴和窗口过曝仍非最终品质。

同程序自身生长中途档独立进程恢复`Saved/PantryRefillQA/20261031-184834-344b897e`通过，只续工，不再次F2付种。原Crop逻辑／旧档迁移关联`Saved/FarmRegressionQA/20261031-184904-b1ff065e`通过，旧逻辑夹具不当作真实移动或图形证据。

## Windows证据

程序SHA256：`ED2AE35FF540E0917E076D2DCBA6099479B33B8F11EA3BE7002DA66700B063EF`；入口`F:\CreatureFrontierBuilds\Windows-PantryRefill\CreatureFrontier.exe`。Development原生构建通过（`Saved/Build-PantryRefill-Windows.log`），36源资产哈希未变，复用DE57649E已验收烹饪数据，28不可变硬链接／19独立文件，见候选目录`StageEvidence.csv`。不是本轮完整重新烹饪或最终分发包，不能原地改写共享文件，旧构建保留。

自身真实来源`Saved/StarterPantryQA/20261031-183632-d2ca9b0d/StarterPantryDone-generated.sav`，哈希`95DF91CF9086A5CFFBF7A06CD5378E16361D27697EA8310B538314DE862CE8F4`。`Saved/PantryRefillQA/20261031-185029-bfe95af9`的960×540完整图形链通过，原玩家生命28／药品3保持，种子1→0／原农夫经验40→41／食槽2→3，另一伙伴原休息、家园、资源和其他物资保持。三张图已逐张目视，幼苗正值进度、收获后背包1粮和屋内3/30储粮可读，无越界；灰盒品质限制与Editor相同。

同程序自身生长中途档独立进程恢复`Saved/PantryRefillQA/20261031-185114-a88b0daa`通过，不再次存种；原Crop逻辑／迁移关联`Saved/FarmRegressionQA/20261031-185133-ef85e2ce`通过。

## 限制与后续

本项只补回一份上一轮吃掉的食物，剩余种子为0，不是永续农业或实体自动运输；不能把玩家取粮／步行／存粮称作伙伴搬运。本轮只增加整合验证，不改普通规则，存档格式仍38。下一主线应接真实补给的用途或补种来源，不继续扩同一收获矩阵。
