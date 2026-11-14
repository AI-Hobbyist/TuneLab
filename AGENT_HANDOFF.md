# 当前任务交接

更新时间：2026-11-15。分支cubase_16_full_ara。M0–M6自主实施、非GUI验收、独立提交推送及正式源码对应包均已完成，本轮授权工作正常收尾。真实Cubase S3+实机验收未通过验证，全部人工步骤按用户决定SKIPPED。

## 已完成

- 路线B、VSTi3+ARA Bridge、进程内.NET/Avalonia钢琴窗、引擎/歌手选择、显式配对与单一输出owner、时间/音符/歌词/版本化工程和ARA归档。SDK/Foundation冻结公共ABI未改，无新增引擎必需API。
- 标准VST3文本入口、持久音符匹配、覆盖冲突、来源修订失效、实际本机fu2_ning2_na4重合成；持久RenderSession与region worker/generation隔离，未改乐句cache保留。私有ABI21。
- M6：Windows Release打包/ZIP/hash/版本/许可/依赖资产检查、主手册第18章、无模型MIDI/tlpx/state夹具、A01–A16人工清单及升级卸载说明。
- 资源路径宿主缺陷已RED→GREEN最小修复，ResourcesFolder按TuneLab.dll目录定位，保留普通独立版布局、可执行根和用户数据路径。
- 最后全量暴露格式表注册/枚举测试竞态，只将ProjectOpenCommandTests与ExtensionWriteCommandsTests归入FormatRegistry集合，生产格式注册器未改，正常默认调度完整测试通过。

## 最新验证

正式交付矩阵docs/cubase-delivery.md，M6逐项证据docs/cubase-m6-validation.md；M0/M2–M5记录仍有效。

- m6-final-main-isolation-tests.log：892/892通过；m6-final-legacy-tests.log：13/13；m6-final-solution-build.log：0错误。
- 原失败m6-final-main-parallel-failure.log：891/1；全局串行试验m6-final-main-serial-tests.log：94失败，未当PASS。最终通过来自默认调度+精确集合隔离，没有跳过用例。
- m6-package-rc04.log及m6-package-dependency-verification.log：Release打包/解压重定位，369文件哈希/PE64/手册/许可/夹具，deps.json全部runtime/native资产，双validator47/0和跨包CLR协议通过。
- m6-package-real-lyrics.log：实际包内Instrument SDK歌词更新到新音频，旧cache静音，波形变化0.0337584，有限非零。
- m6-package-processor-regression.log：实际包内Instrument状态/seek-loop/64–512块/格式/旁路/配对/多实例通过；该次CLI冷预览51ms。
- M5真实Bridge配对/归档/来源身份恢复RED→GREEN及真实未改乐句cache、冷暖误差0见M5日志与正式记录。
- 主手册15项检查、native/state往返和独立MIDI解析通过；未生成.cpr。

## Git与最后操作

M6功能提交1619711d42c15781608fdb4bbed02d78a01bc5e1已推送origin/cubase_16_full_ara，git ls-remote已确认同SHA。正式包从该提交构建；本手册与交付记录为随后文档补记，随同名分支交付。M5功能420c11ae及文档ed882bff此前也已推送确认。

最终ZIP：out/cubase/packages/m6-final-1619711d/TuneLab-Cubase-win-x64.zip；SHA256 11991d563abc8ce8261b347279ddaf8de4237f4b898e933913830cb251ec0eb8；51411806字节。ZIP旁有.sha256，包内manifest列369文件和精确依赖/SDK提交。m6-final-package-build.log、m6-final-package-verification.log、m6-final-package-real-lyrics.log均退出0；两个validator各47/0、重定位与完整依赖/CLR及本机真实歌声通过。

没有剩余自主实施或新开发阶段。仅docs/cubase-manual-checklist.md的人工项继续SKIPPED（用户决定，未执行）；以后收到用户明确指令才执行，不自动重启目标或要求当前用户操作。完整结果及A01–A16边界在docs/cubase-delivery.md。

保留原有.gitignore修改、.codegraph/、out/及三份未跟踪参考文档，不stage：docs/Cubase16_SVS_接入静态分析.md、docs/S3+_交付与接口清单分析.md、docs/TuneLab_接入Cubase16_方案分析.md。实际状态以git status为准。

## 未验证与约束

- 所有Cubase/V7启动、真实GUI/IME/DPI、安装/升级/卸载实机、硬件、宿主长时播放、保存重开和试听，均SKIPPED（用户决定，未执行，需人工验证）；不作为blocked、不等待、不冒称PASS。以后仅在用户明确要求时按docs/cubase-manual-checklist.md执行。SDK夹具与MIDI meta歌词不能证明Cubase原生供给。
- nethost/DLL/头文件/许可来自同一本机已有NuGet Host.win-x64 8.0.26，要求另装系统.NET8 x64与VC++；不安装运行时，不分发测试声库。
- 只用Windows原生工具链，禁止任何WSL调用/探测及本地非Windows构建。16GB内存，dotnet -m:1 /nodeReuse:false，native --parallel 1。
- CodeGraph优先。构建/测试/打包前台PowerShell Tee-Object build.log并立即检查LASTEXITCODE。GUI验证严禁Qt offscreen或伪造真实窗口结论。
- 不扩大范围、不机会重构、保留用户修改；每阶段独立commit/push确认。可靠额度最近used58%，无需Luna阈值交接。普通开发不使用子agent。
