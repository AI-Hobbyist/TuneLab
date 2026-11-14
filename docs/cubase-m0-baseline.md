# Cubase 集成 M0 技术基线

日期：2026-11-14。状态：M0 自主实施与自动验证完成；真实宿主/GUI 验证 SKIPPED（用户决定，未执行，需人工验证）。

## 工具链与依赖

| 项目 | 已核实基线 |
|---|---|
| Cubase | D:/Program Files/Steinberg/Cubase 16，16.0.5.174 |
| V7 插件 | C:/Program Files/Common Files/VST3/VOCALOID7VSTi.vst3 与 VOCALOID7Bridge.vst3 |
| ARA SDK | a2b1aac1d1d5c4eed387db85a9c0cdb7d460254c，示例报告 2.3.0 |
| VST3 SDK | 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96，CMake 3.8.1 |
| Native | VS 18 2026，MSVC 19.51.36260，Windows SDK 10.0.26100 |
| Managed | net8.0，Avalonia.Desktop 11.3.15，hostfxr 8.0.26，host pack 8.0.6 |

third_party 中 SDK 为本机符号链接；构建脚本提供依赖入口，不分发这些目录中的生成物。

## 已验证范围

- verify-sdk.ps1：示例插件和 CLI 宿主构建、ContentReading、PlaybackRendering，退出 0。
- verify-host.ps1：自有 VSTi3 和 Bridge 标准 bundle、各 47 项 validator 通过、hostfxr 调用 ABI 1、state 标记正常往返与截断/未知版本拒绝，退出 0。
- verify-bridge.ps1：属性更新、内容通知、modification 克隆、EditorView 协议通知及最小 archive 往返，退出 0。
- Avalonia 嵌入根入口仅编译通过，没有初始化或创建 GUI 窗口。

探针目前输出静音，state/archive 仅保存版本标记；尚无 TuneLab 编辑内容、声音合成和 UI 附着。validator 不能证明实际 Cubase 接入；此前发现缺 InitDll 后已补 SDK Windows 模块入口，并通过 ARA CLI 加载。

## 歌词接口调查

本地 ARAInterface.h 没有 kARAContentTypeLyrics。宿主内部 ARAContentLyricsEntries 名称不足以建立外部调用契约。

本地 VST3 pluginterfaces/vst/ivstnoteexpression.h 提供标准候选：NoteExpressionTextEvent，字段为 typeId、noteId、textLen 和 UTF-16 text；预定义 kTextTypeID 和 kPhonemeTypeID。音素类型说明仍标为 TODO，不能假定声库音素编码。头文件中 kPhoneticTypeID 注释与枚举命名不一致，实现须以实际枚举为准。

下一步核对 INoteExpressionController 的文本类型声明方式，并构造标准事件测试；宿主临时 text 指针必须在回调有效期内复制到预分配队列，不能交给异步线程保留。标准事件适配通过只证明适配逻辑；Cubase Key Editor 是否供给事件和完整歌词编辑事务仍需人工验证，按用户决定 SKIPPED，不阻塞独立实施。

## 引擎候选

用户已明确选定 fu2_ning2_na4，仅用于本机测试，不随插件或测试资产分发。路径为 D:/AI-Tools/OpenUtau/Singers/fu2_ning2_na4-DiffSinger/fu2_ning2_na4。以现有 DiffSinger 1.0.0 扩展、20 sampling steps、保留当前运行设备配置建立后续引擎对照；本阶段未运行声库渲染，渲染性能和确定性尚未测量。

本机 APPDATA/TuneLab/Extensions/DiffSinger/manifest.json 声明 DiffSinger 1.0.0、voice、sdk-version 1.0、win-x64，入口 DiffSingerForTuneLab.DiffSingerVoiceEngine；目录包含 ONNX Runtime 和 OpenUtau.Core。它可作为现有扩展兼容接入候选，尚未选定可分发声库或完成版本/许可核对，不能记歌声渲染通过。优先复用现有 legacy compat 路径，不修改冻结公共 ABI。

## M0 检查点

| 原定项目 | 结果 |
|---|---|
| 工具链与 SDK 基线 | 已记录版本及 commit |
| 双插件发现与工厂 | 两个标准 bundle，validator 各 47/0；自有 Bridge ARA CLI 可加载 |
| C ABI smoke | 实际 native→hostfxr→managed 返回 ABI 1 |
| 基础状态往返 | VST3 内存流正常/截断/未知版本测试通过；ARA 空文档标记归档通过 |
| Avalonia 嵌入试验 | 嵌入根 API 编译通过；窗口/线程/IME 实机行为 SKIPPED |
| 歌词协议证据 | 公开文本事件候选已定位，未证实 Cubase Key Editor 供给；保留 M5 技术缺口 |
| 引擎和声库 | DiffSinger 1.0.0；用户选定 fu2_ning2_na4 仅本机测试，自带匹配 vocoder，不分发模型；实际歌声渲染留待 M2 |
| 线程与输出路由设计 | 本节明确约定；尚未实现完整调度 |

本阶段证明基础构建和协议接入路径可执行，不代表 S3/S3+ 产品可用。后续阶段继续实现完整 UI、工程、合成与宿主同步。

### 已确定的实施契约

- 原生到托管入口采用 cdecl、UnmanagedCallersOnly，ABI 查询返回 1。字符串使用 UTF-8 加显式字节长度，实例以不透明句柄标识；M1 扩展前须固定错误码、句柄和释放方。
- 普通 VST3 UI 与 Extension UI 共用 Avalonia 嵌入根；根创建、尺寸、焦点与销毁只在指定 UI 线程执行。M0 仅证明 API 可编译，真实附着按用户决定跳过。
- ARA 控制线程遵守宿主对象生命周期；宿主临时内容必须复制，合成工作线程只消费快照；音频回调不调用 CLR、不等待合成。
- 独立 VSTi3 自有文档；ARA 事件由其指定 renderer 输出。配对模式显式绑定文档并指定唯一输出方，不使用加载顺序猜测关联，不用 IConnectionPoint 发现任意插件。
- Note Expression 的类型枚举与信息由 INoteExpressionController 的 getNoteExpressionCount/getNoteExpressionInfo 在 UI-thread/Connected 阶段声明。数值与字符串转换接口不是任意文本的持久化接口；歌词文本由 ProcessData 事件中的 UTF-16 数据接收，不能从转换方法伪造宿主歌词。
- 文本事件按 noteId 关联一次播放中的音符；不能将运行时 noteId 当作跨工程稳定身份。全文编辑、离线静态音符和歌词车道事务是否可通过同一路径获取仍未证实。

### 引擎基线核对结果

deepseek贝壳 的随包 README 明确要求声库再分发另行确认，因此本集成不打包该模型。本机另一模型 fu2_ning2_na4-DiffSinger/fu2_ning2_na4 含自带 dsvocoder/vocoder.yaml 和对应 ONNX，44100Hz/hop512/128mel，force_on_cpu=false；可用于后续本机模型可用性检查，声库许可未核实前同样不进入分发包。M0 固定 DiffSinger 扩展作为适配目标、模型保持用户外部资产；实际声库加载、合成与确定性测试留在引擎接入验证，未记 PASS。

进一步只读核对 ExtensionSettings.json 中 DiffSinger 的 voicebank_dirs，配置指向 D:/AI-Tools/OpenUtau/Singers。该目录存在多个 DiffSinger 模型；例如 deepseek贝壳 含 acoustic.onnx、dsconfig.yaml、character.yaml、音素与语言字典，采样率 44100、hop 512、128 mel bins，指定 pc_nsf_hifigan_44.1k_hop512_128bin_2025.02 vocoder。该例没有内置 dsvocoder 目录，需继续核对实际共享 vocoder 和 README 使用条款；目前仅确认本机模型存在，不授权重新分发，不代表渲染通过。

选用本机 DiffSinger 1.0.0 作为首个既有扩展接入候选。扩展 LICENSE 为 MIT，附带 OpenUtau/G2P 等第三方声明；该许可不自动覆盖模型权重和声库。按其随包文档，只读检查默认 APPDATA/DiffSingerForTuneLab/Voices 和 Vocoders，未发现 dsconfig.yaml 或 vocoder.yaml。不能宣称已选定可分发测试声库或已完成歌声渲染。

后续先检查扩展已配置的额外模型目录；若仍无可用模型，记录声库输入缺口并继续 UI、文档适配、缓存及协议工作，不下载许可不明模型，也不以静音或正弦波替代歌声合成验收。保持已有 GPU/DirectML 能力，不为测试便利擅自切换配置。

完成检查点提交推送后进入 M1：实例上下文、确定性生命周期、钢琴窗嵌入和非 GUI 隔离验证。保留歌词供给及实机 UI 缺口；所有真实 Cubase/V7/GUI 验收均 SKIPPED（用户决定，未执行，需人工验证）。
