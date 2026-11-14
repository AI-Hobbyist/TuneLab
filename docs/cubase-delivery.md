# Cubase 16 集成候选版交付记录

日期：2026-11-15。M0–M6自主实施、非GUI验收和阶段检查点已完成。M6功能提交1619711d42c15781608fdb4bbed02d78a01bc5e1已推送origin/cubase_16_full_ara并核对远端同SHA；本记录为交付补记。按用户要求，所有需打开软件、GUI、实机、安装及试听的验收默认SKIPPED，不作为收尾阻塞，不记为PASS。真实Cubase原生歌词供给与完整S3+实机验收尚未验证。

## 最终包与版本

- 源码提交：1619711d42c15781608fdb4bbed02d78a01bc5e1；分支cubase_16_full_ara。
- 最终本地ZIP：[out/cubase/packages/m6-final-1619711d/TuneLab-Cubase-win-x64.zip](../out/cubase/packages/m6-final-1619711d/TuneLab-Cubase-win-x64.zip)，51411806字节，含两个Release插件及369个清单文件。
- ZIP SHA256：11991d563abc8ce8261b347279ddaf8de4237f4b898e933913830cb251ec0eb8；同目录提供.zip.sha256旁车文件。
- 私有ABI21；原生插件版本0.1.0，Windows x64，nethost包8.0.26；运行时与声库按随包说明另行准备。
- m6-final-package-build.log、m6-final-package-verification.log、m6-final-package-real-lyrics.log均退出0：正式提交重建、ZIP重定位/全部依赖资产/双validator47/0/CLR协议、实际歌声重合成通过。
- manifest.sourceTrackedDirty=true来自保留的原有.gitignore修改；正式包构建前没有未提交产品代码。ZIP不含模型、用户配置或开发输出目录。

## 交付物

- 两个Windows x64 Release插件：TuneLabInstrumentProbe.vst3和TuneLabBridgeProbe.vst3，保留既有ID与Probe名称。
- 路线B进程内钢琴窗、引擎/歌手选择、显式配对与单一输出owner、时间/音符/歌词关联、状态与ARA归档。
- 无模型测试工程：tempo-lyrics.mid、tempo-lyrics.tlpx、tempo-lyrics.state及说明；没有伪造.cpr。
- 安装/升级/卸载说明、主用户手册第18章、人工清单、依赖版本/许可证和SHA256 manifest。
- 可重复构建/检查入口：integrations/cubase/package.ps1、verify-package.ps1及probes中的CLI工具。

## 原验收矩阵的证据边界

| 项 | 自主验证 | 人工部分 |
|---|---|---|
| A01 | 完整bundle、元数据、SDK validator | 安装发现SKIPPED |
| A02 | 视图/配对代理、生命周期协议 | 实际双容器钢琴窗SKIPPED |
| A03 | 编辑事务、撤销、选择隔离及脚本数据路径回归 | GUI交互SKIPPED |
| A04 | 标准SDK文本、稳定ID、冲突、实际包内SVS变化 | Cubase原生供给/显示/试听SKIPPED |
| A05 | 原M3时间映射、变速/拍号/偏移/裁剪自动回归 | 实机对齐SKIPPED |
| A06 | native缓存与块/格式回归 | 宿主30分钟播放和现场耗时SKIPPED |
| A07 | 旧修订拒绝、真实未改片段缓存保留、CLI冷预览51ms（该次测量） | 现场首次/增量与预览体验SKIPPED |
| A08 | 既有实际处理器非顺序seek/loop缓存回归 | 宿主交互SKIPPED |
| A09 | 冷暖渲染最大采样误差0及离线CLI输出 | 实时录制/导出/Make Permanent对照SKIPPED |
| A10 | 版本/损坏拒绝、clone/partial restore/rollback及歌词归档 | 10次宿主保存重开SKIPPED |
| A11 | 文档/配对/输出/clone隔离自动回归 | 4对实际宿主轨道SKIPPED |
| A12 | view lease/owner释放及状态协议 | 20次真实窗口/实例操作SKIPPED |
| A13 | 原生缩放接口与代理协议 | DPI/IME/字体/跨屏SKIPPED |
| A14 | 取消、Busy、无效句柄、missing peer与实例释放回归 | 宿主异常场景SKIPPED |
| A15 | 主测试892/892、legacy13/13 | 独立版真实窗口操作SKIPPED |
| A16 | ZIP重定位、369文件哈希、依赖资产/许可与升级卸载说明 | 干净Windows安装/升级/卸载SKIPPED |

正式证据：cubase-m0-baseline.md、cubase-m2-validation.md、cubase-m3-validation.md、cubase-m4-validation.md、cubase-m5-validation.md、cubase-m6-validation.md及原实施计划。各次自动日志在工作区out/cubase各阶段目录，未将本机声库或个人配置打入包。M6一次全量测试的共享格式表竞态已通过两个测试类集合隔离修复，最终默认调度全量通过；失败日志保留在M6记录中。

## 后续人工工作

仅在用户明确要求时执行cubase-manual-checklist.md。尤其A04须取得真实Cubase事件与发音证据；不能将SDK夹具、手动输入或MIDI lyric meta event当作原生歌词同步。运行时与声库按随包说明另行准备。当前没有要求用户立即操作或等待人工结果。

本轮已授权的自主实施、自动验证、记录、提交推送与本地交付全部完成，没有剩余自主开发阶段。仅人工清单按用户决定保持SKIPPED，不等待人工结果，不冒称真实Cubase S3+通过。
