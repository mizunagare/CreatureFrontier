# 照料暂停边界修复（格式 25，不改字段）

## 目标与发现

推进个体偏好前复查既有关系入口，发现手动喂食完全未检查世界暂停，抚摸仅检查旧暂停菜单布尔值，不能覆盖格式 25 新增的原生命名窗口或其他世界暂停来源。UI-only 输入模式通常阻止物理键输入，但不能作为玩法入口的规则校验；直接调用仍可能扣食物、改饱食度/体力/亲密度与冷却。

本轮仅修复玩家手动 PetCompanion / FeedCompanion 的暂停边界，两者在任何世界暂停状态下均拒绝；暂停前后的距离、饥饿、忙碌、资源与亲密度规则不变。命名窗口自身确认是有意允许的暂停中操作，不扩大禁止到命名提交。不修改存档格式 25，不增加 CI，不宣称偏好系统已实现；自动进食/全部库存和战斗接口的直接调用边界不在此次证据范围。

## 验收方法与风险

扩展既有 CFAffectionTest：在无冷却、饥饿且有食物的有效伙伴上，分别通过 SetGamePaused 和真实 Naming.Open 暂停，独立调用抚摸与喂食，要求两个入口均拒绝，食物/亲密度/饱食度/体力/抚摸冷却完全不变；关闭窗口后原成功照料路径继续通过。原距离/遮挡/威胁/主人/倒下/忙碌/冷却/上限/保存/坏档/15 迁移断言保留。此为真实入口集成测试，不是物理键或输入法验证。

先加测试、后改实现。Editor 负对照 `Saved/CarePause/20261028-081318/Before.log` 出现 `CF_AFFECTION FAIL care=1 gates=1 paused=0 feed=0 save=1 invalid=1 legacy=1 response=1`，证明测试能拒绝旧实现。Editor 进程返回 0 但行为 FAIL，因此绝不以退出码单独判通过。

涉及 CompanionAffection.cpp、CompanionFeeding.cpp、AffectionScenario.cpp。实现使用世界暂停查询，在一切资源/关系变更之前返回。最终证据如下，不使用上一命名包的记录代替。

修复后 Editor 编译通过（仅已有引擎弃用与非首选工具链警告），`Saved/CarePause/20261028-081532/After.log` 为 `CF_AFFECTION PASS care=1 gates=1 paused=1 feed=1 save=1 invalid=1 legacy=1 response=1`。

Windows Development 编译/烘焙/归档已成功：`E:/CreatureFrontierBuilds/Windows-CarePause-Final/CreatureFrontier.exe`；内部 Win64 EXE SHA-256 `8FDCC488ACE29A182BC4B6F36518FDF50B7A76278878F8AD0FCD6B40F68878A1`。旧包未覆盖。完整回归 `Saved/PackagedTests/20261028-081718-91411e9a/` 一次连续执行正常退出：58 类/62 个进程用例，62 份日志均有 PASS 与正常关闭，无 CF_* FAIL / Fatal error / Assertion failed；完成后 EXE 哈希未变。包括三次跟随、短步巡游、命名/农业/运输/房屋/野外工作/探索跨进程恢复与保存/导航。没有重跑历史完整跟随排列矩阵或 14 次真实 24 旧档专项；无存档字段/版本修改，回归保留各场景既有的迁移断言，不代表所有历史版本均有真实样本覆盖，历史真实旧档证据仅属于上一构建。

D3D12 照料专项 `Saved/CarePauseRender/20261028-081738/Affection.log` 行为各项为 1 并正常退出；`petting.png` 是本次新生成的 603252 字节图片，已目视：伙伴和照料反馈显示正常，文字覆在角色上的旧 HUD 问题未修。该截图不是动画全周期或正式美术验收。图形条件为 Adapter 0、1280×720、RenderOffscreen、norhithread、forcerhibypass、60 帧上限、关闭运动模糊/使用 AA 1；不改变正式默认，不代表默认渲染性能或所有输入设备验证。
