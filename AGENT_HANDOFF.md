# 当前任务交接

更新时间：2026-11-14。

## 任务与完成状态

用户已授权执行并重建持续目标，当前进入 M0。已成功配置 ARA_Examples（输出 out/cubase/m0/ara），工具链为 VS 18 2026、MSVC 19.51.36260、Windows SDK 10.0.26100、VST3 SDK 3.8.1。third_party 两个 SDK 是指向 E 盘的符号链接，部分 Git 操作遇到访问限制，普通文件读取与 CMake 配置可用。

原构建会话 90760 已主动 Ctrl+C 终止，原因是多个 MSBuild 节点无编译输出。已在正常权限下以前台单节点、/nodeReuse:false 重新构建，当前会话 3234，目标 ARATestPlugInVST3、ARATestHost、validator，日志 build.log。已实际编译生成 ARA_PlugIn_Library、ARATestPlugInCommon、base、pluginterfaces 等库，完整构建尚未返回结果；接续先轮询 3234。SDK 存在 C4819/C4865 等警告，未修改第三方代码。out/ 是本次生成的未跟踪构建目录，不能提交。M0 尚未完成，未提交实施阶段。

最新用户决定：全部需要打开软件、GUI 窗口、实机或用户配合的验证默认跳过，标记 **SKIPPED（用户决定，未执行，需人工验证）**。包括可自动操作的真窗口测试；不启动、不重复申请许可、不等待结果，不阻塞阶段推进、提交、推送或交付。非 GUI 自动检查仍执行。计划已同步更新全文适用规则、阶段检查点、M4/M5 和验收矩阵；跳过不等于 PASS。本政策覆盖下文先前的真实宿主验证安排。

用户要求仅编写 TuneLab 深度集成 Cubase 16 的 S3+ 路线 B 计划书，明确需要 VSTi3 和 ARA Bridge，并让钢琴窗内嵌 VST3 界面。本次计划已写入 `docs/TuneLab_Cubase16_S3Plus_路线B实施计划.md`，未开始产品实施。

已查阅 docs 三份集成参考、命名规范，并优先使用主仓和 third_party 两个 SDK 各自的 CodeGraph。针对索引未展示的确切接口补读头文件。关键结论：本地 ARA 内容枚举没有 kARAContentTypeLyrics；EditorRenderer 是音频试听角色，GUI 通过 IPlugView；歌词协议需 M0 在真实宿主核实。

## 文件与 Git

- 当前分支：cubase_16_full_ara。用户已授权提交计划书与交接手册并推送同名远端分支；本记录随提交保存，最终提交及推送结果以 Git 分支和上游状态为准。
- 初版计划和本文件已在 7c581054 提交并推送；本次修改两份文档以记录默认跳过实机验证政策。
- 任务开始时已有 `.gitignore` 修改、未跟踪 `.codegraph/` 和三份集成参考 Markdown；均保留，不纳入任何提交。
- 实施分阶段提交推送规则已写入计划，但本次只有计划编写，不执行 M0–M6。

## 验证与未验证

- 模型调查：deepseek贝壳 README 要求再分发另行确认，不打包。fu2_ning2_na4-DiffSinger/fu2_ning2_na4 已含匹配 44100/hop512/128mel dsvocoder 与 ONNX，force_on_cpu=false；许可未完成，不进入分发。新增 native/.clang-format 并使用 VS 自带 clang-format 仅格式化本次 native 源码，diff 空白检查通过。格式化后尚需回归验证；M0 报告已记范围和证据缺口，仍未完成检查点。

- 已找到本机实际声库：只读筛选 Configs/ExtensionSettings.json 的 DiffSinger 配置，voicebank_dirs=D:/AI-Tools/OpenUtau/Singers，存在多个模型。deepseek贝壳 配置 44100Hz/hop512/128mel，外部 vocoder pc_nsf_hifigan_44.1k_hop512_128bin_2025.02，无模型内 dsvocoder。下一步核对其 README 条款和共享 vocoder；不要再报告本机无声库，不修改用户配置或模型。M0 仍未完成。

- M0 基线已补 ABI、线程、唯一输出方、NoteExpressionController 声明与文本事件语义。已核对 DiffSinger 扩展 LICENSE 为 MIT；默认 Voices/Vocoders 未发现模型，尚需查其已配置额外模型目录，不能冒称可分发声库已确定。该输入缺口不阻止独立 UI/协议工作；M0 验收记录仍需如实保留未完成项。

- 新增 docs/cubase-m0-baseline.md 整理当前证据及剩余项。发现 VST3 标准 NoteExpressionTextEvent（kTextTypeID/kPhonemeTypeID、noteId、UTF-16）可作为歌词候选通道，尚未证明 Cubase 歌词车道会供给；下一步实现标准事件契约探针。APPDATA 已安装 DiffSinger 1.0.0（旧 SDK 1.0）候选引擎，需走现有 compat 并核对声库/许可，未完成歌声渲染基线。

- state_probe 已实际运行通过：正常往返成功、截断和未知版本拒绝，verify-host.ps1 退出 0（会话 95592 已结束）；两个插件 validator 仍 47/0。SDK HEAD：ARA a2b1aac1d1d5c4eed387db85a9c0cdb7d460254c；VST3 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96。V7 双插件实际存在 C:/Program Files/Common Files/VST3/VOCALOID7Bridge.vst3 与 VOCALOID7VSTi.vst3。下一步整理 M0 契约、依赖/引擎基线与剩余验收，不重复已有探针。

- Avalonia.Desktop 11.3.15 与 EmbeddableControlRoot 最小构造入口已 dotnet build 通过，0 warning/error；未初始化 GUI 或创建窗口。新增 probe_state.h，为两插件加入带版本标记的 VST3 state 读写，尚待编译及显式往返测试；仅空探针状态，不是 TuneLab 工程。下一步验证 state 的正常/截断/未知版本，再完成 M0 基线报告与检查点。

- 最新 M0：加入 8 字节带版本的空探针文档 archive marker；verify-host.ps1 和 verify-bridge.ps1 均完整通过（退出 0），Bridge 测试包含 Archiving。该归档只验证宿主流写入/读回及版本识别，不保存 TuneLab 编辑或 ARA 对象内容，不能当作完整工程恢复。下一步补 VST3 state 基础往返、Avalonia 嵌入根编译试验及 M0 静态协议/引擎基线记录。无活动构建会话。

- 模块入口修复已验证通过：verify-host.ps1 + 自有 Bridge 的 ARATestHost PropertyUpdates/ContentUpdates/ModificationCloning/EditorView 均退出 0；最后一项仅协议通知，不创建窗口。SDK/原生 host 调用链成功。新增 verify-bridge.ps1 固化四项，脚本待独立运行。M0 仍缺状态往返、静态契约/引擎基线及嵌入根编译试验，尚未提交阶段。

- 自有 Bridge 的 ARA 宿主测试暴露缺失 InitDll（VST3Loader.cpp:153 initBinaryFunc 断言），会话 77189 已终止，退出码 3。已为两个插件补 SDK dllmain.cpp 与 module_lifecycle.cpp；修正后待重建。下一步 verify-host.ps1，再运行 ARATestHost -test PropertyUpdates ContentUpdates ModificationCloning EditorView（仅协议通知，无 GUI）。此结果说明 validator 通过不能替代实际 ARA 入口加载。

- 最新 verify-host.ps1 完整通过，退出码 0：补齐四个文档回调和三个 ARA IID 定义后，Bridge 成功生成标准 bundle，validator 可发现 ARA Main Factory Class。两插件验证均为 47 passed / 0 failed，hostfxr 输出 ABI 1。尚需 ARA 命令行宿主对象/角色测试、状态往返、静态歌词契约与引擎基线，M0 未完成。

- Bridge 构建最新失败已缩小到 ProbeDocumentController 尚为抽象类（C2259，build.log 约 164 行起列出缺失虚方法）。已修复前置 CMake C 语言未启用、ARA 类别宏错误加命名空间两项。下一步读取编译器列出的纯虚方法并补最小正确实现；不能将 Bridge 记为已生成。VSTi3 validator 仍为 47/0。无活跃构建会话。

- 已补 bridge_probe.cpp：独立 processor/controller/factory ID、ARA Main Factory Class、IPlugInEntryPoint/2 和角色绑定；静音仅 M0 探针。首次配置报 ARA_API 重复目标，已移除手工 add_subdirectory(ARA_API)，由 ARA_Library 自行引入。修正后尚未重建；下一步执行 verify-host.ps1，处理 Bridge 编译/validator 结果。ARA 文档与 Bridge 源码仍未通过构建，不能记 PASS。

- 新增 native/ara_document.h：自有 ARA factory 元数据、最小 DocumentController，归档尚未实现时显式返回 false。尚未链接编译；下一步补 Bridge 的 VST3 processor + IPlugInEntryPoint/2 + IMainFactory 注册，并链接 ARA_PlugIn_Library。不要把当前头文件当作可加载 Bridge。

- 最新 verify-host.ps1 完整运行通过：修正 CMake project VERSION 0.1.0 后，TuneLabInstrumentProbe 标准 VST3 bundle 成功生成，构建时 validator 47 passed / 0 failed，原生 hostfxr 再次输出 ABI 1。日志 host-verification.log / build.log。仍只是静音身份探针，ARA Bridge、状态往返和完整集成尚未实施。

- verify-host.ps1 已完整执行通过，退出码 0。最新新增 instrument_probe.cpp 和 VST3 CMake 目标 TuneLabInstrumentProbe，独立 processor/controller ID、Instrument|Synth 分类、立体声输出与 MIDI 输入；仅 M0 静音发现探针，不具备歌声合成/GUI。新增目标尚未构建，下一步运行 verify-host.ps1（现在同时构建该目标）并处理编译/validator 结果，再实现 ARA Bridge 自有身份探针。未完成 M0，未提交阶段。

- hostfxr 原生探针已配置、构建、运行通过，退出码 0，实际输出 Managed ABI version: 1；使用 host pack 8.0.6 头文件和 hostfxr 8.0.26。新增 verify-host.ps1 固化调用链，脚本自身待完整执行。计划 M0 状态已更新为 IN PROGRESS，未达到完整检查点，不提前进入 M1。

- 托管最小入口已在正常网络权限下 dotnet build 成功，0 warning / 0 error；之前沙箱 NuGet DNS 错误已排除。新增 native/CMakeLists.txt 与 host_probe.cpp，用 hostfxr 加载程序集并调用 cdecl GetAbiVersion；原生探针尚未构建运行。下一步以安装的 .NET Host pack include 配置构建，并传 hostfxr.dll、runtimeconfig.json、程序集绝对路径运行，预期 Managed ABI version: 1。

- verify-sdk.ps1 已完整运行，退出码 0，配置、示例插件/宿主构建及 ContentReading/PlaybackRendering 全部通过。新增 integrations/cubase/managed 最小 net8.0 托管入口（GetAbiVersion，cdecl UnmanagedCallersOnly），用于后续 hostfxr 探针；尚不包含 UI/引擎。

- 最新通过：ARATestHost 的 ContentReading 与 PlaybackRendering 两项在示例插件上执行，退出码 0，实际读取 5 个音符并渲染 44100Hz 区域。仅证明 SDK 示例链路，不是 TuneLab/Cubase 产品验收。新增 integrations/cubase/verify-sdk.ps1 固化禁用部署的构建与这两项命令行检查；该脚本本身尚未完整运行。日志仍为 build.log。

- 最新 M0 结果：以 `-DARA_SETUP_DEBUGGING=OFF` 重新配置，成功生成 ARATestPlugIn.vst3（传统单文件形式）与 ARATestHost.exe，未部署到系统目录。多目标构建在最后请求顶层 validator.vcxproj 时失败（MSB1009）；已有 bin/Debug/validator.exe 可执行。单独 validator 检测到 ARA factory 和处理器/控制器，结果 47 passed、1 failed，另报传统单文件不是标准 bundle；日志 build.log。下一步定位失败测试并区分示例自身行为与插件目标要求，建立规范 bundle 的自有探针。无活跃构建会话。

- 会话 3234 已结束，退出码 1。build.log 显示插件构建后测试为 0 failed，但 SDK 自动部署步骤创建 `C:\Program Files\Common Files\VST3` 目录参数错误而失败（约第 566 行）。不能记完整构建 PASS。下一步检查 ARA 示例自身的自动部署配置，禁用系统目录链接/部署后重建；不要执行其删除系统已安装示例的命令。当前无正在运行的构建会话，本条覆盖上文的中间状态。

- 已读取 `D:\Program Files\Steinberg\Cubase 16\Cubase 16.exe` 文件版本：16.0.5.174。
- V7 参考位置由用户指定为 `C:\Program Files\VOCALOID7\Editor`；未运行或动态分析。
- 未编译、测试、安装、启动任何宿主或插件。真实 UI、歌词链路、音频、多实例、归档等产品能力未验证。
- 没有实施失败项；宿主歌词协议是计划中的待验证技术项，不是本次文档任务的阻塞。
- 文档已执行 diff 空白检查；未运行与文档无关的构建或测试。

## 下一步

本次无剩余实施任务。等待用户审阅计划；只有用户后续授权实施后才开始 M0。先复核计划和实际 Git 状态，保留既有修改。

必须遵守：优先使用各自 CodeGraph；不扩大范围；冻结 SDK/Foundation 公共 ABI；真实窗口 GUI 验证且禁止 Qt offscreen；所有构建/测试/打包前台 PowerShell 配合 Tee-Object 和立即检查退出码；人工项记 MANUAL/PENDING 或用户决定的 SKIPPED，不作为 blocked 条件；分阶段自动验证后更新记录、检查 diff、提交、推送并确认，再进入下一阶段。分支不得使用 codex/ 前缀。
