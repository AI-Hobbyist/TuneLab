# M6 发布候选实施与证据

## 最终自主验收（2026-11-15）

M6原定自主实施、自动验证及交付材料已完成。功能提交1619711d42c15781608fdb4bbed02d78a01bc5e1已推送确认，正式提交ZIP已重建并完成最终验证，见cubase-delivery.md。后文IN PROGRESS/待办为历史过程，不覆盖本节；真实Cubase、GUI、安装和试听按用户决定SKIPPED，不宣称实机S3+通过。

| 原M6项目 | 证据 |
|---|---|
| Windows双插件与运行时部署 | package.ps1生成完整Release双bundle与ZIP；nethost同一现存8.0.26 NuGet包的DLL/头文件/许可；依赖系统.NET8 x64和VC++，不安装额外运行时 |
| 重定位及完整性 | m6-package-dependency-verification.log退出0：ZIP新目录解压、369文件SHA256、PE64、所有deps.json声明runtime/native资产、手册/许可/fixture、双validator47/0、跨包CLR |
| 实际包内SVS | m6-package-real-lyrics.log：fu2_ning2_na4，实际包内Instrument标准SDK歌词→状态与新音频，旧cache静音，波形平均绝对变化0.0337584；非试听 |
| 实际包内处理器回归 | m6-package-processor-regression.log：控制器恢复/无效状态/seek-loop/64–512块/多实例/旁路/配对/44.1k双精度；冷预览51ms，均为该次CLI测量 |
| 主测试/兼容/构建 | m6-final-main-isolation-tests.log，892/892；m6-final-legacy-tests.log，13/13；m6-final-solution-build.log，0错误 |
| 工程与文档 | 主手册稳定锚点cubase；fixture真实tlpx/state往返、独立SMF解析；人工A01–A16清单、依赖/许可/升级卸载说明随包 |
| 版本恢复与原阶段回归 | M0–M5正式记录及M5 ABI21全host回归；公共SDK/Foundation表未改，未新增引擎必需API |
| 安装/升级/卸载和真实宿主 | 操作说明和数据保护边界已写；实机步骤全部SKIPPED（用户决定，未执行，需人工验证），不是PASS |

### 最后全量测试中的失败与修复

m6-final-main-parallel-failure.log保留891通过/1失败：ExtensionWriteCommandsTests枚举FormatsManager.mEntries时，ProjectOpenCommandTests的静态初始化注册内建格式，发生Collection was modified。仅把这两个测试类标为同一FormatRegistry集合，保留生产格式注册器。最终以原默认运行方式通过全部892项，没有跳过测试或屏蔽断言。

曾尝试官方支持的全局串行RunSettings，m6-final-main-serial-tests.log记录94项数据线程/上下文断言失败；该运行配置未保留，不能作为PASS。采用针对实际共享状态的集合隔离后，正常调度通过。集合语义依据[xUnit官方并行说明](https://xunit.net/docs/running-tests-in-parallel)。

本轮完整diff与git diff --check已核对，保留原有.gitignore/索引/out/参考文档。只读SDK提交查询曾被沙盒拒绝，按授权提高权限后读到精确提交；没有修改third_party。已有NU1902与其他原警告如实留在日志，不扩大为依赖升级任务。


状态：IN PROGRESS，2026-11-15。M5功能420c11ae和检查点文档ed882bff均已推送并实际确认远端一致；M6尚未完成、尚未提交。

## 已实现与验证

新增 integrations/cubase/package.ps1：前台Windows x64 Release dotnet publish（framework-dependent）及原生CMake构建，单节点；生成两个完整VST3 bundle、安装升级卸载说明、TuneLab/托管依赖/VST3/ARA许可文本、逐文件SHA256 manifest和ZIP。输出必须新目录，不删除或覆盖已有包；不安装插件或运行Cubase。不复制本机声库，模型不随包分发。

新增 integrations/cubase/verify-package.ps1：检查manifest文件集合/大小/哈希、Windows x64 PE、必要资源/手册/许可、系统.NET8声明，再运行两个SDK validator及跨包CLR协议。CLI夹具按自身可执行文件定位资源，因此在隔离fixture目录放入已校验包的相同managed副本；两个目标bundle本身不变，未借用开发managed目录。

- out/cubase/m6/m6-package-rc01.log：Windows Release构建/发布/打包退出0；包含原生validator各47/0。
- out/cubase/packages/m6-rc01/TuneLab-Cubase-win-x64.zip：本地首个候选ZIP。
- 从ZIP解压至out/cubase/m6/relocated-rc01后，m6-package-relocated-verification.log退出0：358个校验文件、双validator各47/0、包内独立ALC/配对/歌词/状态协议通过，隔离空TUNELAB_DATA_DIR。
- 初次包检查错误地预期user-manual.zh-CN.md，已按实际csproj发布映射改为Resources/Manual/zh-CN.md；文件原本已随包发布。
- 初次跨包夹具因其自身模块相对managed目录未部署而退出20；按真实夹具布局提供包内同字节副本后通过，没有关闭失败路径或改产品行为。

## 尚未完成

- 生产资源路径问题已修复并生成rc02，见下节；不再作为待办。
- 补齐依赖版本/许可清单和用户手册中Cubase相关流程；审核.NET nethost许可及VST3各编译子组件声明。
- 按原M6/A01–A16核对最终必要回归、工程夹具、部署升级卸载说明及最终证据，不另增计划外要求。
- 首包来自ed882bff基线和未提交打包脚本，manifest如实记录sourceTrackedDirty=true。最终包需对应正式检查点源码，不能将首包标记为最终发布。

所有Cubase/V7启动、真实GUI、安装/升级/卸载实机、硬件、长时宿主播放和试听继续SKIPPED（用户决定，未执行，需人工验证），不作为阻塞。重定位包CLI验证只证明其覆盖的文件与协议，不冒充干净Windows实机或真实Cubase验收。完整S3+宿主歌词供给仍未实机验证，持续目标ACTIVE。


## 资源定位修复与 rc02（2026-11-15）

将 PathManager.ResourcesFolder 改为基于 TuneLab.dll 所在目录；仅在程序集没有文件位置的单文件布局沿用可执行根。普通独立版同目录布局不变，ExcutableFolder和用户数据路径未改变；不增加可变资源配置、PluginServices初始化接口或公共SDK成员。

新增HostedResourcePathTests：将真实TuneLab程序集复制到隔离的模拟插件目录，通过独立collectible ALC读Resources/Manual/Translations路径；普通布局也检查。首次测试清理遇到Windows映射DLL占用，已把反射引用隔离在NoInlining helper并有界GC释放，后续RED为真实路径不符，未隐藏失败。

- m6-resource-relocation-red.log：修复前1失败/1通过，断言证实指向宿主目录。
- m6-resource-relocation-green.log：资源与ManualLibrary相关17/17通过。
- m6-resource-full-tests.log：主测试892/892通过，退出0。
- m6-package-rc02.log：重新Windows Release发布/构建/打包退出0。
- rc02补充VST3 base/pluginterfaces/public.sdk许可证、系统.NET LICENSE/ThirdPartyNotices，并在manifest记录所有托管library ID/版本/type及nethost host pack版本；具体许可证适用范围仍按原M6清单核对，不把文件存在等同所有许可义务已审查。
- m6-package-rc02-verification.log：从ZIP解压到新目录后363文件哈希/必要资源/许可/PE64、双validator各47/0、跨包CLR协议通过。

本地最新候选：out/cubase/packages/m6-rc02/TuneLab-Cubase-win-x64.zip。仍是未提交M6源码生成的候选，非最终发行。下一步补用户手册Cubase章节、测试工程/发行验收清单及精确依赖许可核对，再按原M6完成必要回归和提交推送。软件启动、GUI、安装/硬件/长时宿主播放与试听继续SKIPPED，完整目标ACTIVE。


## 手册、交付夹具与精确许可（2026-11-15）

唯一用户手册docs/user-manual.zh-CN.md新增稳定锚点cubase及第18章：候选版安装、引擎/歌手、配对、歌词冲突和升级卸载，并明确真实宿主证据边界。m6-manual-tests.log现有手册检查15/15通过，无GUI截图或实机操作。

新增probes/FixtureBuilder，使用真实原生序列化器生成fixtures/tempo-lyrics.tlpx和关联v2 .state，并生成标准SMF0 .mid；不含声库/用户数据，source=delivery-fixture、ID11/22/33，含120→90BPM和4/4→3/4。m6-fixture-generation.log往返检查通过；m6-midi-fixture-verification.log以独立Python解析SMF头/轨道长度、480PPQ、音符时长、UTF8歌词、速度与拍号通过。未生成或伪造.cpr，实际宿主导入/保存按用户决定SKIPPED。

新增docs/cubase-manual-checklist.md，原A01–A16全部人工项逐项标SKIPPED；新增docs/cubase-dependencies.md记录精确本地SDK提交、NuGet和许可来源。已核对root THIRD-PARTY-NOTICES包含Avalonia/Skia/HarfBuzz/NAudio/MeltySynth/默认SF2等既有分发组件声明。

nethost改为本机已有Microsoft.NETCore.App.Host.win-x64/8.0.26 NuGet包，其NuSpec声明MIT、repository commit ace9703c57db8596ccc3e3efdc7e65b6975c0b2d；DLL/头文件/完整许可均从同一包取，脚本校验名称版本，不下载/安装运行时。此前rc02复制系统.NET通用许可的方式不再用于最终包。

m6-package-rc03.log构建打包退出0；ZIP解压重定位m6-package-rc03-verification.log退出0，368文件、双validator47/0、CLR跨包协议通过。随后新增依赖版本说明文档及manifest原生SDK pin字段，最终包须重生成并用当前检查器验证，不能把rc03直接标最终发行。当前最新候选ZIP仍out/cubase/packages/m6-rc03/TuneLab-Cubase-win-x64.zip。

下一最小任务：完成原M6最终自主验收矩阵和必要回归（含候选包实际歌声CLI），生成最终内容对应包，审计diff并提交推送检查点；确认之后按最终源码重建/记录交付包。不扩大验收范围。M6未完成未提交，所有GUI/安装/实机/试听仍SKIPPED且非阻塞。


## 最终检查点与包

M6功能1619711d42c15781608fdb4bbed02d78a01bc5e1已推送并ls-remote确认一致。正式ZIP out/cubase/packages/m6-final-1619711d/TuneLab-Cubase-win-x64.zip，SHA256 11991d563abc8ce8261b347279ddaf8de4237f4b898e933913830cb251ec0eb8，51411806字节。m6-final-package-build.log、m6-final-package-verification.log、m6-final-package-real-lyrics.log均退出0，369文件/完整依赖/双validator47/0/跨包CLR/实际fu2_ning2_na4歌词重合成通过。本节为已完成事实补记，不改变产品文件。自主交付收尾，人工SKIPPED清单保留；无需启动新开发阶段。
