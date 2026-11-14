# TuneLab Cubase Windows x64 候选包

本包包含 TuneLabInstrumentProbe.vst3（VSTi3）和 TuneLabBridgeProbe.vst3（ARA Bridge）。当前保留开发阶段的 Probe 名称及既有插件 ID，两个插件必须使用同一套发行文件。路线 B 的钢琴窗与引擎在宿主进程内运行。

## 当前验证状态

已完成非 GUI 的 VST3/ARA/托管协议、歌词关联与工程状态测试，并使用本机声库完成歌声合成与增量缓存验证。Cubase 16 原生 Key Editor 是否发送标准歌词文本事件尚未实机证实。Cubase 启动、真实窗口、IME/DPI、安装、长时实机播放及试听均为 SKIPPED（用户决定，未执行，需人工验证）。不能将此候选包描述为已通过真实 Cubase S3+ 验收。

## 依赖与安装

- Windows x64、Cubase 16，以及系统安装的 Microsoft.NETCore.App 8.0 x64 运行时。包内 nethost 用于发现系统运行时，不包含完整 .NET 运行时。
- 原生 Release 插件需要匹配的 Microsoft Visual C++ x64 运行库。引擎、声库与其额外依赖由用户单独安装，本包不分发 fu2_ning2_na4 或任何其他测试模型。
- 手工安装时先关闭 Cubase，再将两个完整 .vst3 目录复制到 Windows VST3 插件目录，例如 C:\Program Files\Common Files\VST3。保留 Contents/Resources/managed，不能只复制内部二进制。
- 打开 Cubase 后检查乐器列表及 Extensions 中的 Bridge，按插件中的配对控件选择对应实例。配对后共享同一文档，只有指定 owner 输出工程音频；缺失 peer 时先明确重新绑定或解除配对。
- 在内嵌 TuneLab 的引擎/歌手选择控件中选择已安装资源。现有引擎无需实现新增必需 API。插件通常使用用户 TuneLab 数据目录；命令行测试可通过 TUNELAB_DATA_DIR 指向隔离的测试目录。

上述安装与宿主操作是待人工执行的说明，本轮未执行。完整编辑用法见包内 managed/Resources/Manual 的 TuneLab 用户手册；Cubase 特有验收清单见仓库实施计划 A01–A16。

## 升级、卸载与工程

升级前备份宿主工程并关闭 Cubase。备份原来的两个插件目录，再一起替换成同一候选包的两个完整目录；不要混用新旧托管文件。遇到恢复问题时保留工程和日志，并恢复成备份的匹配插件对。

卸载只移除这两个插件目录。不要删除用户 TuneLab 数据、扩展、声库或工程。依赖系统安装的 .NET 与 VC++ 运行库不由本包卸载。候选包没有自动安装器、更新服务或修改注册表的步骤。

歌词通道接收标准 VST3 文本事件，并按音符起点、音高与来源基线匹配。未唯一匹配时拒绝写入；本地词、发音与固定音素修改发生冲突时优先保留本地编辑。空文本清除歌词，Unicode 与延音标记交由所选引擎解释。直接在 TuneLab 输入歌词不等于已经验证 Cubase 原生歌词同步。

## 开发构建

从仓库根目录用 PowerShell 运行 integrations/cubase/package.ps1；默认 Release、win-x64，前台构建日志写入 build.log，生成新的 out/cubase/packages/rc-时间戳目录、ZIP 和逐文件 SHA256 清单。不覆盖已有包，不安装插件，不启动 GUI。当前构建工具为 Visual Studio 18 2026、CMake 3.25+、.NET SDK 8+。nethost 与头文件、LICENSE.TXT、THIRD-PARTY-NOTICES.TXT 必须来自同一 Microsoft.NETCore.App.Host.win-x64 NuGet 包，默认使用本机已有的 8.0.26；可用 HostPackRoot 和 HostPackVersion 指定对应包目录和版本，脚本不下载或安装运行时。

包中的 LICENSE.txt、THIRD-PARTY-NOTICES.txt 和 licenses 目录提供 TuneLab、托管依赖及 VST3/ARA 声明。引擎和模型由其各自许可约束。manifest.json 记录源提交、构建配置、工作区是否有未提交修改及校验信息；它不是签名或真实宿主验收证明。
