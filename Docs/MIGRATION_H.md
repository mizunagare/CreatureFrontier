# H盘项目迁移记录（2026-10-31）

## 有效位置与代码

当前有效项目为 `H:\myGame`，工程入口 `H:\myGame\CreatureFrontier.uproject`。虚幻引擎本体仍在 `D:\Epic\UE_5.8`，F盘既有可运行构建没有移动。后续构建、测试和Git操作均使用H盘项目目录；旧历史文档／测试日志中的D盘路径保留当时含义，不做全局替换。

原26,574个文件、约40.6GB完整复制，逐文件SHA-256全部一致，包括Git历史、源码、资产、未提交改动、构建缓存、开发证据和存档。完整清单及日志位于项目外 `H:\myGame-MigrationEvidence-20261031`。迁移前编辑器施工测试自行结束，未在活动测试写档期间复制。

所有开发中代码已经保存并推送至 `mizunagare/CreatureFrontier` 的 `master`，提交 `c3a896c3322cf4e08f46c8c3e990ae6f423ec2bb`；通过远端 `ls-remote` 核对一致，记录 `Git-Verified.csv`。该提交包含尚待Windows和图形验收的墙顶施工进度，准确范围见 `SHELTER_PROGRESS.md`，不是将开发中内容标为完成。

## H盘验证

- 重新生成工程文件退出0：`ProjectFiles-Console.log`。
- 初次Editor构建退出0：`Build-H-Console.log`，82.53秒，但后续核验发现迁移的Makefile缓存仍包含D盘绝对输出路径，因此该记录不能证明H盘模块已更新。
- H盘 `-CFSmokeTest` 启动与捕捉／工作／跟随／死亡检查通过，退出0：`Smoke-H.log`；这是无图形基础验证，不替代完整玩家体验或Windows打包验收。
- 中文静态检查818条文字、27条资产脚本提示通过；未新增玩家文案。
- 原3047份 `.sav` 全部与迁移前SHA-256一致；自动测试没有覆盖普通玩家存档。

### 构建缓存路径纠正

纠正后Editor构建退出0，90.00秒；日志没有旧项目路径，H盘模块SHA-256为 `712ABA1348791E11D7D5800F53BDD8DB1BA43CA4A2520EB7828D0DF903D53A1B`。新进程 `Saved/StarterShelterQA/20261031-111225-16109061` 完整链通过，明确包含 `collision=3 supportReject=1`；六张1280×720截图已逐张目视，中文可读、箱内22→16→10→0及开放棚屋可见。长提示仍使用既有省略，灰盒不代表最终品质。Windows该版本尚未验证。

后续墙顶测试的 `Saved/Build-StarterShelter-Geometry-20261031.log` 明确记录输出仍在D盘，H盘模块指纹仍为旧的129160B3；该轮旧模块的行为通过不能证明新增碰撞门槛通过。已将H盘Editor的 `Intermediate/Build/Win64/x64/CreatureFrontierEditor/Development/Makefile.bin` 原地改名保留为 `.migration-backup-20261031`，再使用显式 `-Project=H:\myGame\CreatureFrontier.uproject` 重新生成构建缓存。新日志为 `Saved/Build-H-CacheRepair-20261031.log`；构建与新模块运行结果须单独核验。没有删除D盘内容。Game目标的迁移缓存也须在首次新构建前检查，不能仅凭工作目录或退出0认定迁移成功。

## D盘清理未执行

用户明确要求先推送所有代码再清理D盘。目录搬移最初遇到Git只读对象后，D盘Git元数据已从完整H盘副本恢复；D盘旧项目数据仍在。工具随后两次拦截清理命令，最新一次发生在用户明确授权、远端核验和存档核验全部通过之后；命令未启动，未进行自动递归删除，也没有绕过限制改用其他工具删除。

因此不能声称D盘空间已释放，也没有建立 `D:\myGame` 到H盘的目录链接。D盘旧副本仍是普通文件夹，可在确认H盘工程打开正常后，由用户在资源管理器中手动删除 **仅 `D:\myGame`**。不要删除整个D盘，不要删除 `D:\Epic`，也不要误删H盘主项目。清空回收站前可再次检查H盘存档和GitHub提交；若保留在回收站，空间可能尚未真正释放。

`H:\myGame-D-backup-20261031` 只包含首次目录搬移时的部分Git元数据，不是完整项目备份，不作为恢复或开发入口。完整有效项目在 `H:\myGame`，完整旧副本仍在 `D:\myGame`。
