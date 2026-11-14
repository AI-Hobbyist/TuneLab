# TuneLab 深度集成 Cubase 16 的 S3+ 路线 B 实施计划

日期：2026-11-14。状态：实施中，M0–M2已完成并推送，M3自主实施与非GUI验收完成且功能检查点已推送确认；下一阶段M4尚未开始。

本计划交付两个插件：**TuneLab VST3 Instrument（VSTi3）**和 **TuneLab ARA Bridge**。采用路线 B，在 Cubase 进程内通过原生 C++ 插件壳加载 .NET 与 Avalonia，复用 TuneLab 钢琴窗，使其既能嵌入 VST3 插件界面，也能通过 ARA 工作流进入 Cubase Extension 编辑器。最终目标是 S3+，包括 Cubase Key Editor 歌词输入同步、合成播放、编辑和工程恢复。

本计划最初按“仅编写计划书”建立，用户随后已授权按阶段持续实施。当前进度与验证见本文进度记录及 AGENT_HANDOFF.md；安装、软件启动和真实 GUI 验证遵循下述跳过政策。

## 1 目标与范围

### 用户决定的默认验证政策

所有需要打开软件、真实 GUI 窗口、实机操作或用户配合的验证，默认跳过，统一记录为 **SKIPPED（用户决定，未执行，需人工验证）**。包括 Cubase、VOCALOID、TuneLab、GUI 测试窗口和安装器交互，即使可以自动操作也不默认启动。此规则适用于全文，覆盖下文所有“必须验证”“真实宿主验收”和相关阶段依赖，直到用户明确要求执行对应验证。

不重复询问启动许可，不等待人工结果，不以这些跳过项阻塞阶段完成、进入下一阶段、提交、推送或任务交付。各阶段完成实施与非 GUI 自动验证后正常收尾，并附需人工验证清单。跳过项不记 PASS，不宣称已通过真实 Cubase 的 S3+ 验收。

无需 GUI 的源码/SDK 核对、静态分析、编译、命令行 validator、单元测试、协议夹具和打包检查仍执行。缺少真实采集样本时可用明确标注的合成夹具验证适配逻辑，不能据此宣称宿主链路已通过；缺少实际实现所需协议时记录具体技术缺口，继续其他可自主完成工作。

### 1.1 最终交付

| 交付物 | 职责 | 完成条件 |
|---|---|---|
| TuneLab VSTi3 | 乐器轨加载、MIDI 输入、音频输出、参数和插件状态 | Cubase 可扫描、加载、演奏、播放、导出和恢复工程 |
| TuneLab ARA Bridge | ARA 文档与片段接入、上下文同步、Extension 编辑、归档、指定角色的音频渲染 | Extensions 可发现，宿主对象生命周期正确，Lower Zone 可编辑 |
| 共享托管运行层 | TuneLab 工程、编辑事务、合成调度、声库与扩展加载 | 多实例隔离，复用现有 SDK，不启动独立 TuneLab 应用 |
| 内嵌钢琴窗 | 音符、歌词、音素、音高、颤音及现有参数编辑 | VST3 窗口与 Extension 两种容器均可操作，中文输入正常 |
| 宿主歌词接入 | Cubase Key Editor 的歌词及对应音符进入 TuneLab | 更改宿主歌词后，对应音符、显示与实际发音一致 |
| 工程与部署 | 版本化状态、安装包、测试工程、用户文档 | 重开工程不丢编辑内容，升级与卸载可验证 |

S3 = VSTi3 + ARA Bridge + 内嵌编辑器；S3+ = S3 + 经真实 Cubase 验证的宿主歌词输入通道。仅在 TuneLab 窗口输入歌词、导入 MIDI/歌词文件或通过自有插件互传歌词，均不能代替 S3+ 的最后一项。

### 1.2 支持边界

- 首期只支持 Windows x64、用户指定的 Cubase 16；以 16.0.5.174 建立首个验证基线。
- VSTi3 与 ARA Bridge 是两个独立可发现的插件身份，采用不同且稳定的 class ID。可共享运行库，但不能只交付一个普通乐器插件。
- 路线 B 固定为进程内运行。外进程编辑器、跨进程窗口强行父化、整套 C++ 钢琴窗重写均不作为默认替代路线。
- 保留 TuneLab 独立版现有行为；仅做嵌入、多实例和宿主音频接入所必需的调整。
- 插件反向改写 Cubase 歌词/音符、与 Cubase 完全统一的撤销栈、其他 DAW、macOS、VST2、新声库训练均不在本期承诺内。
- 基于开源 SVS 补齐必要能力属于实施候选，但必须先证明现有 TuneLab 引擎或适配器无法满足目标；不预设替换现有引擎。

## 2 参考基线与证据裁决

### 2.1 输入资料

| 资料 | 用途 |
|---|---|
| [Cubase 静态分析](Cubase16_SVS_接入静态分析.md) | 宿主扫描、Extension、歌词相关二进制线索 |
| [S3+ 交付与接口分析](S3+_交付与接口清单分析.md) | 交付范围与验收场景 |
| [TuneLab 接入方案分析](TuneLab_接入Cubase16_方案分析.md) | 路线 B、现有工程及合成模型 |
| [术语表](naming-glossary.md) | 音素固定、合成参数、时间与位置等命名 |
| [SDK 演进约束](sdk-api-evolution.md) | 冻结公共 ABI 与新增接口规则 |
| [插件开发指南](plugin-development.zh-CN.md) | 引擎会话、快照、音频段和声库扩展 |

本机参考位置以用户本次指定为准，覆盖旧资料中的 C 盘 Cubase 路径：

- Cubase：`D:\Program Files\Steinberg\Cubase 16`；已读取主程序文件版本为 `16.0.5.174`，尚未运行验证。
- VOCALOID 7：`C:\Program Files\VOCALOID7\Editor`。用于后续观察安装布局和实际工作流；VSTi/Bridge bundle 的实际路径在 M0 只读核实，不假定它们一定放在 Editor 目录。
- 本仓 SDK：`third_party/ARA_SDK`、`third_party/vst3sdk`。实施优先查询各目录自身 CodeGraph，不再以旧分析中的 E 盘副本作为构建基准。
- TuneLab 当前项目文件声明 .NET 8 与 Avalonia 11.3.15。M0 记录实际 SDK commit、子模块状态、工具链与运行时版本，禁止仅凭参考资料标注版本锁定依赖。

### 2.2 必须修正的参考假设

| 参考假设 | 已核实的事实或实施约束 |
|---|---|
| ARA EditorRenderer 提供 UI 绘制面 | 本地 `ARAEditorRendererInterface` 管理编辑试听的 region/sequence，属于音频预览角色。实际原生 GUI 由 VST3 `IPlugView` 承载；EditorView 提供选择、隐藏轨等通知 |
| 可直接读取 `kARAContentTypeLyrics` | 本地 `ARAInterface.h` 的内容类型枚举没有该项。Cubase 内部 `ARAContentLyricsEntries` 字符串不能证明第三方插件可调用的公开歌词协议存在 |
| 自定义 IConnectionPoint 消息可兜底读取宿主歌词 | 自有消息只能在双方实现协议时工作；不能让 Cubase 自动发送其歌词。必须先确认真实发送方、接口、事件格式与关联规则 |
| 普通 VST3 不能内嵌钢琴窗 | `IPlugView::attached` 接收宿主父窗口，普通插件窗口即可承载钢琴窗；这与 ARA Lower Zone 嵌入是两项独立验收 |
| 插件卸载意味着卸载 CLR | 路线 B 按进程级运行时、实例级资源释放设计；不能把 CLR 卸载重载作为普通实例生命周期要求，ALC 也不是运行时或崩溃隔离 |
| ARA PlaybackRegion 与 TuneLab Part 永远一一对应 | 需要处理同一 source/modification 的多个 region、裁剪、复制与不同播放偏移，不可把对象身份压成单个时间区间 |
| VST3 仍必须采用旧 GPL/商业许可 | 当前本地 VST3 LICENSE 为 MIT；分发按锁定版本实际许可核对，不沿用旧资料结论 |

接口依据：[本地 ARA 定义](../third_party/ARA_SDK/ARA_API/ARAInterface.h)、[ARA 与 VST3 绑定](../third_party/ARA_SDK/ARA_API/ARAVST3.h)、[VST3 GUI 接口](../third_party/vst3sdk/pluginterfaces/gui/iplugview.h)。上游对应资料：[ARA API](https://github.com/Celemony/ARA_API)、[IPlugView](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/master/gui/iplugview.h)、[VST3 许可](https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt)。

本计划采用上述校正后的契约。原参考资料保留为研究记录，不直接作为实现头文件或已通过的宿主验收。

## 3 路线 B 架构

```text
Cubase 16 进程
  ├─ TuneLab VSTi3 原生模块
  │    ├─ VST3 Processor / Controller / IPlugView
  │    └─ 原生音频缓存与实时输出
  ├─ TuneLab ARA Bridge 原生模块
  │    ├─ ARA Factory / DocumentController / 对象与角色绑定
  │    ├─ IPlugView → Extension 内嵌钢琴窗
  │    └─ ARA 播放与编辑试听输出
  └─ 共享运行层
       ├─ hostfxr → 托管入口和版本化 C ABI
       ├─ 每文档/实例的数据、编辑事务、归档与绑定
       ├─ Avalonia 内嵌根 → 现有 PianoWindow
       └─ 合成工作线程 → 不可变音频缓存 → 原生输出
```

### 3.1 模块职责与最小修改面

以下新增目录是建议组织，M0 后固定；不要求先重构出覆盖全仓的 TuneLab.Core。

| 模块 | 建议位置或现有入口 | 必需工作 |
|---|---|---|
| 原生插件与运行时入口 | 新增 `integrations/cubase/native/` | 两个插件目标、hostfxr、C ABI、VST3/ARA 适配、原生缓存 |
| 托管嵌入入口 | 新增 `integrations/cubase/managed/` | 运行时初始化、实例上下文、UI 创建、模型和合成适配 |
| 钢琴窗嵌入 | `TuneLab/UI/MainWindow/Editor/PianoWindow/` 及必要 Editor 依赖 | 复用控件，注入编辑上下文，补齐确定性解除订阅和视图释放 |
| 数据与合成 | 现有工程、会话、快照、音频段实现 | 适配宿主时间与渲染请求，剥离已确认的单实例或设备依赖 |
| 自动验证与宿主夹具 | 新增 `tests/cubase/` | ABI、时间、状态、角色、双实例与真实宿主回归 |
| 打包与记录 | 新增集成构建脚本及 docs 文档 | 固定版本、可复现打包、证据与兼容矩阵 |

CodeGraph 已显示 `PianoWindow` 有 `IDependency` 输入，并复用编辑动作、参数区域和视图状态。复用该入口，但须在 M1 审计静态状态、全局动作路由、播放头、订阅与对话框依赖，不能仅把独立版 MainWindow 父化后宣布嵌入完成。

### 3.2 VSTi3 与 Bridge 的音频归属

- **独立 VSTi3 模式**：VSTi3 自有文档，处理现场 MIDI 与已编辑内容，通过乐器轨输出。
- **ARA 事件模式**：Bridge 根据宿主赋予的 playback/editor renderer 角色输出事件音频与试听，支持离线渲染及 Make Extension Permanent；不能用永久静音的 Bridge 通过最终验收。
- **配对模式**：VSTi3 与 Bridge 显式绑定同一逻辑文档。每个播放区域只允许一个正式输出方，状态中记录绑定和路由，不以名称、加载顺序或“最近一个实例”猜测配对。
- 配对恢复必须处理宿主先恢复任一插件、复制轨道、重复打开工程及删除一侧。M0/M3 验证宿主能提供的身份上下文；无法可靠自动关联时提供显式绑定，不静默跨工程连接。
- 不把 VST3 IConnectionPoint 当作跨插件发现总线。共享运行库中的注册与数据交换采用自有明确契约；组件/控制器分离情况下遵守 VST3 状态与通信边界。

### 3.3 托管运行时与线程

运行时初始化在非音频路径完成，扫描阶段避免创建 UI、加载声库或启动合成。部署携带与 hostfxr 入口匹配的运行时配置和依赖；M0 验证独立部署及已加载 CLR 的宿主进程场景。不得调用独立应用退出逻辑结束 Cubase。

| 执行域 | 允许工作 | 禁止工作 |
|---|---|---|
| 原生实时音频回调 | 读已发布缓存、有限混音、处理预分配事件队列 | CLR 调用、合成、分配、阻塞锁、磁盘、网络、等待 UI |
| 宿主协议线程 | 按 SDK 约束处理 ARA 生命周期、复制有效期受限的数据 | 把宿主临时指针交给异步线程长期保存 |
| 数据与调度线程 | 编辑事务、快照、修订号、取消失效任务、状态序列化 | 与音频回调同步互等 |
| 合成工作线程 | G2P、音高/音素计算、推理、重采样、缓存发布 | 修改正在被音频线程读取的缓存 |
| UI 线程 | 现有钢琴窗、输入、布局、编辑动作 | 合成阻塞、嵌套启动独立应用主循环 |

M0 确认 Avalonia dispatcher、STA、宿主窗口线程与消息循环的兼容方式。C ABI 只传带长度的数据、明确单位的值和不透明句柄，规定 UTF-8、版本、结构大小、调用线程、释放方、错误返回及回调有效期；托管异常不得越过 native 边界。

### 3.4 钢琴窗嵌入

优先使用 Avalonia 可嵌入根和本进程子 HWND，对接 `IPlugView` 的 attached、removed、尺寸、焦点和缩放接口。宿主父窗口归宿主所有，不改其窗口过程或全局 DPI 策略来迁就插件。

VSTi3 插件窗口与 ARA Extension 复用同一编辑视图实现；关闭视图只销毁视图，不销毁文档或停止宿主播放。多视图共享文档时保持独立选择/缩放状态，编辑通过同一文档事务提交。

必须验证中文/日文 IME 候选框、复制粘贴、Delete、撤销重做、空格与宿主快捷键、菜单和模态窗口归属、DPI 变化、缩放命中测试及频繁开关。未处理的按键按 VST3 契约返回宿主，避免双重执行或吞掉快捷键。

不采用参考资料中的“EditorRenderer 位图绘制面”假设，不把离屏截图作为真实嵌入证据。

## 4 宿主内容与开源 SVS 能力

### 4.1 S3+ 歌词通道验证顺序

以下第 2–4 步涉及打开软件的操作保留为人工验证步骤，默认 SKIPPED，不作为 M0/M5 的推进门槛。优先通过源码、SDK、现有资料和静态分析核对契约。

1. 核对本地 VST3/ARA 头文件和官方扩展契约，枚举歌词、音素、语言及 note 关联相关事件；区分宿主内部类型和可供第三方插件使用的协议。
2. 在真实 Cubase 创建最小 MIDI part，输入两种不同歌词并修改、删除、移动、复制音符。用最小原生探针记录实际收到的接口、事件类型、内容、时间和关联标识。
3. 对照 VOCALOID 7 的 VSTi 与 Bridge 工作流，确认歌词由哪一侧接收、是否需要 Bridge 绑定以及歌词变化如何使合成失效。观察其行为，不复制其私有实现或把其内部消息名当作自有协议。
4. 证明“Cubase 歌词修改 → 探针收到数据 → 正确音符关联 → TuneLab 模型变化 → 发音变化”的完整链路，形成 M0 协议证据与 M5 产品验收。
5. 若只能证明内部符号存在，记录“宿主歌词协议未证实”。继续独立可执行的 VSTi3/ARA 工作，S3+ 项保持未完成；如确需厂商接口、私有扩展或新增辅助组件，提交影响与范围供用户决定，不默认降级目标。

不能通过新增一条仅自有插件识别的消息、手工复制歌词或改写宿主内部内存，宣称已经打通 Key Editor 歌词输入。

### 4.2 复用与自行实现清单

| 能力 | 优先复用 | 必要时自行实现的最小范围 | 验证 |
|---|---|---|---|
| 文本到音素 | 现有 voice 扩展的 G2P | 所选开源 SVS 的适配、语言/音素集映射 | 中日英用例，空歌词与延音 |
| 音符到歌声 | `IVoiceSynthesisSession` 与现有音频段 | 宿主调度、缓存、增量失效、取消与结果提交 | 同一输入与原引擎离线结果对照 |
| 现场 MIDI 预览 | 所选引擎已有能力 | 经验证需要的预合成/缓存试听适配 | 记录启动延迟、Note Off、连续音与复音行为 |
| 钢琴窗编辑 | TuneLab 现有控件与编辑事务 | 嵌入容器、实例上下文、宿主焦点适配 | 真窗口编辑与撤销 |
| 宿主上下文 | VST3/ARA 标准接口 | 时间、对象身份、区域映射及已证实的歌词协议适配 | 变速、复制、裁剪与重开 |
| 工程持久化 | TuneLab 现有模型序列化 | VST3 state、ARA archive 包装与关联恢复 | 全字段往返、旧版迁移 |

M0 选定一个可合法用于测试和分发验证的开源 SVS 引擎及声库，记录版本、许可证、输入约束和确定性。现有材料未指定具体引擎，不能预先承诺所有声库都支持实时演唱。现场按键预览与已知乐句的完整歌声合成分别验收，不能用普通提示音替代歌声合成能力。

新增适配优先留在宿主内部或现有 voice 插件实现内。冻结的 TuneLab.SDK/Foundation 公共面不因适配便利而修改；确需新增公共 API 时按演进规则处理，破坏性变更先取得维护者同意。

用户补充约束：确需补充 TuneLab API 时，新增能力必须可选，不影响已有引擎加载、合成及既有行为。优先新增独立可选能力接口；对已有插件实现接口增加成员时提供 DIM 默认实现，调用前检测能力，不把新能力设为所有引擎必填契约。保留旧引擎工作路径，并通过未实现新接口的引擎回归验证兼容性。仍按 PublicAPI.Unshipped、SDK 版本周期和演进文档登记新增公共面，不更改已发布签名。

插件内引擎与歌手选择（用户明确要求，M2 交付）：VSTi3 和 ARA Bridge 的插件界面均提供引擎与歌手/声库选择入口，复用现有引擎枚举及声库列表能力，选择只修改当前实例/当前编辑 part 的 SoundSource。更换引擎后刷新该引擎的可用歌手列表；加载失败、声库缺失等状态在插件内可见。保存并恢复稳定的引擎/声库标识，不随插件分发本机声库。自动验证覆盖选择落到目标 part、另一实例不受影响、切换后的合成失效及状态往返；Cubase 中实际点击、界面和歌声试听按用户规则记录 SKIPPED，需人工验证。当前本机歌声测试使用 fu2_ning2_na4，不将其写死为产品默认或唯一选项。

### 4.3 时间和对象模型

- Cubase 是宿主工程速度、拍号、播放位置和 region 范围的权威。TuneLab 保留音乐位置，向合成会话提供其契约要求的秒时间，音频缓存采用明确采样率的 sample index。
- 明确 quarter-note beat、PPQ tick、秒、sample 的单位和原点；不得把 beat 和 tick 混用。支持分段速度积分及工程起始偏移；线性速度变化以宿主实际供给语义实现。
- ARA source、modification、playback region 分开建模。保留 source offset、playback offset、裁剪范围和稳定身份，覆盖同源多区域及复制后的编辑隔离。
- 宿主提供稳定 note ID 时直接映射；不提供时使用带版本的事件匹配，并对重复同音同起点用例验证。不能只用音高和开始时间作为永久 ID。
- 工程编辑、速度、声库、采样率等改变均递增渲染修订。旧任务完成后只有修订匹配才可发布，避免撤销后旧音频覆盖新状态。

### 4.4 编辑所有权与撤销

宿主导入内容与 TuneLab 用户覆盖分层保存。宿主再次发送变化时，不无提示清空用户固定音素、曲线或局部歌词覆盖；标出受影响项，提供接受宿主值或保留本地覆盖的明确操作。

原始歌词、语言、延音和音素来源分别保存，不按空格/连字符盲目切分所有语言，不把宿主音素字符串当作所有声库通用编码。已确认的宿主变更和 TuneLab 编辑事务分别进入对应历史；插件本地撤销复用既有事务系统，不伪称能撤销宿主编辑。

## 5 音频与持久化契约

### 5.1 音频正确性

实时回调读取按绝对 sample 地址定位的原生缓存，而非只依靠顺序环形缓冲；seek、循环、宿主随机块请求和离线导出都必须可定位。缓存未就绪时输出受控静音并在非实时 UI 显示准备状态，累计 underrun 指标，不能把持续静音算作正常播放。

离线导出和 Make Permanent 必须等待所需内容完成，或通过宿主支持的错误路径明确失败；不得成功返回缺块静音。离线处理允许的等待、取消与超时由 M2/M3 的宿主行为验证确定，不把实时线程限制机械套用到离线任务。

插件延迟只报告确定的音频处理延迟；异步合成准备时间单独显示，不伪装成可由 PDC 解决的固定延迟。覆盖停止后的尾音、循环接缝、采样率变化、最大块大小及不同浮点格式；不支持的格式明确拒绝协商。

### 5.2 状态归属

| 模式/数据 | 权威保存位置 |
|---|---|
| 独立 VSTi3 的完整可编辑文档 | VST3 component state；controller 还原展示状态 |
| ARA 文档、关联和 TuneLab 编辑数据 | ARA archive，支持宿主请求的部分归档/恢复 |
| 配对 VSTi3 的参数、关联和输出设置 | VST3 state；不维护会与 ARA 冲突的第二份可写文档 |
| UI 布局 | 对应实例的版本化视图状态 |
| 可再生成音频 | 可丢弃缓存，不作为编辑数据唯一载体 |
| 声库和引擎 | 稳定标识、版本和所需配置；不默认嵌入受限声库文件 |

归档保存音符、歌词、固定音素、音高、颤音、参数、声库引用、宿主映射、覆盖来源及 schema 版本。恢复采用完整快照替换，不能让半恢复文档参与播放；缺声库仍保留编辑内容并明确提示。未知新版本或损坏数据保留原始内容并报错，不静默改成空工程覆盖保存。

## 6 按序实施里程碑

本节是未来实施顺序，本次不执行。状态均为 NOT STARTED。M0 的歌词调查有边界；未取得宿主协议证据时，记录该项并推进已具备依据的独立部分，不能据此改写 S3+ 定义。

| 阶段 | 工作与产物 | 自动验证要求 | 需人工验证，默认 SKIPPED | 粗估人周 |
|---|---|---|---|---|
| M0 契约与可行性 | 固定工具链；双插件探针；hostfxr/Avalonia 嵌入试验；歌词协议证据；引擎与声库基线；冻结接口及音频路由设计 | 原生 SDK 示例/探针构建、C ABI smoke、基础 state 往返；协议报告区分事实与未证实项 | 指定 Cubase 中识别双插件；原生/托管视图显示；最小歌词链路探测；V7 对照 | 2–3 |
| M1 共享运行层与嵌入骨架 | 托管入口、实例隔离、最小编辑上下文、VST3 内嵌现有钢琴窗 | 生命周期、两实例隔离、编辑事务、视图关闭不销毁文档、既有相关回归 | VST3 真窗口输入中文、缩放、关闭再打开；无独立 TuneLab 主窗口 | 3–4 |
| M2 VSTi3 完整闭环 | MIDI 与参数、歌声合成、缓存、宿主播放、离线导出、完整 component state | VST3 validator、时序/缓存/取消、状态往返、引擎对照 | Cubase 可演奏、播放、导出、保存重开；seek/loop 无陈旧片段 | 4–6 |
| M3 ARA Bridge 闭环 | 工厂和角色、对象映射、tempo、selection、archive、播放/试听、显式配对与路由 | ARA 示例宿主/测试夹具；对象生命周期、部分恢复、关联恢复、角色输出隔离 | Extension 可添加；事件移动/裁剪/复制；Make Permanent；删除任一插件不串实例 | 4–6 |
| M4 双容器钢琴窗 | VSTi3 与 Lower Zone 共用编辑器；完整编辑工具；焦点、IME、DPI、菜单与确定性释放 | 非 GUI 编辑事务及资源所有权回归 | 真窗口反复开关及资源释放；VST3、Lower Zone、宿主独立 Extension 窗口均可编辑；中文正常 | 3–4 |
| M5 S3+ 宿主歌词 | 实现 M0 有依据的协议；歌词/音符关联、语言与延音、覆盖冲突、增量重合成 | 已有采集样本或标明来源的合成夹具；修改/删除/复制/撤销关联；错误版本拒绝 | Cubase Key Editor 修改歌词，正确音符的 TuneLab 显示和发音同步；保存重开保持 | 3–5 |
| M6 发布候选 | 安装升级卸载、运行时部署、版本/许可清单、用户手册、测试工程、完整回归 | 干净环境部署检查、全量必要测试、打包与归档迁移回归 | 完整用户工作流、长时播放、多实例、多屏、重开与导出对照 | 3–4 |

合计约 **22–32 人周**，为工作量区间而非日历承诺；假设复用现有钢琴窗与至少一个可用引擎，不包括厂商协议等待或另写合成算法。M0 结束后按真实证据修订估算。M1–M4 是 S3+ 的组成阶段，不视为用户已同意发布降级产品。

### 6.1 每阶段检查点

每阶段完成所需自动验证后：更新本计划进度 → 检查 git status 与 git diff → 只提交本阶段改动 → 推送当前同名分支 → 确认远端提交成功 → 才进入下一阶段。不得把多个阶段积累成一个最终提交；不纳入用户预先存在的改动。

用户已决定全部开软件、人工和实机验证默认 **SKIPPED（用户决定，未执行，需人工验证）**。阶段检查点只要求完成本阶段实施和非 GUI 自动验证；跳过项不阻止进入下一阶段，不触发 blocked、不重复申请启动许可，也不等待人工结果。交付分别列明实现状态、自动验证结果和人工验证清单。缺少宿主协议属于具体技术缺口，不能靠虚构协议或模拟事件把它消除。

后续依赖某项真实宿主行为时，只推进已有证据支撑的工作；涉及新组件、路线变化或公共 ABI 破坏时说明具体影响再请求决策。每阶段达到既定标准即结束，不追加相邻功能扩大验收范围。

### 6.2 构建和验证纪律

所有编译、测试、打包在前台 PowerShell 执行，完整输出通过 `2>&1 | Tee-Object -FilePath "build.log"` 保存，立即保存并检查 `$LASTEXITCODE`；失败读取日志定位根因。每次结果归档到对应阶段记录后再覆盖 build.log。

实施阶段按修改范围运行 `dotnet build TuneLab.sln -c Debug`、主测试项目、legacy compat 测试及新增原生测试。SDK 公共面变化时执行样例插件 Rebuild、Pack、Install 三步；安装前保证相关应用已关闭。仅文档变更不触发无关构建。

GUI 验证当前默认跳过。只有用户后续明确要求执行时，才采用真实 Windows 窗口：构建 → 打开真实窗口 → 等待稳定 → 操作 → 检查必要截图 → 记录 → 关闭。严禁 Qt offscreen；本集成的 Avalonia/headless 截图也不能代替 Cubase 内嵌窗口验收。届时资源压力测试分批执行并清理窗口，不以离屏方式替代。

## 7 验收矩阵

**A01–A16 中所有开软件、真实窗口和实机部分均默认 SKIPPED（用户决定，未执行，需人工验证），不是阶段或交付门槛。** 下表保留操作目标供后续人工验证。每项内无需 GUI 的自动检查仍执行并单独记录，例如 A05 时间换算、A10 序列化往返、A16 打包和许可检查。

以下为拟定验收目标，数值不是当前已测结果。M0 固定基准机器、声库、工程和测量方法后记录，不在后续阶段任意加码。

| 编号 | 场景 | 判定与证据 |
|---|---|---|
| A01 | 双插件安装和发现 | VSTi3 可列入乐器；ARA Bridge 可列入 Extensions；不依赖厂商硬编码扫描路径 |
| A02 | 两种内嵌容器 | VST3 插件窗口和 Lower Zone 都能操作真实 TuneLab 钢琴窗；截图记录窗口及宿主版本 |
| A03 | 编辑与撤销 | 音符、歌词、音素、音高、颤音和参数编辑往返正确；撤销作用域清楚 |
| A04 | 宿主歌词 | 两种不同发音歌词在 Key Editor 修改后，事件日志、模型、UI 与输出发音全部对应；不是手工导入 |
| A05 | 时间换算 | 固定/变速、拍号、region 偏移与裁剪测试；数学映射到 sample 的舍入误差不超过 1 sample，不将声库辅音提前量当作同步误差 |
| A06 | 实时稳定 | 44.1/48 kHz、64/128/256/512 sample 块；预热后已准备缓存连续播放 30 分钟，无插件缓存欠载或阻塞；记录最坏回调耗时 |
| A07 | 合成准备 | 记录首次/增量合成耗时与现场预览延迟；未就绪明确显示，旧修订不得覆盖新结果 |
| A08 | seek 与循环 | 前后跳转、循环和停止尾音正确，无跨区域旧数据；首次未就绪与稳态分别记录 |
| A09 | 离线一致 | 同一引擎/声库/工程的实时录制、离线导出和 Make Permanent 对齐后比较；确定性引擎按数值误差判定，非确定性引擎在 M0 定义可复验容差 |
| A10 | 保存恢复 | 自有 state 与 ARA archive 全编辑字段往返一致；至少 10 次保存重开，覆盖逆序恢复、缺声库、旧 schema、损坏状态 |
| A11 | 多实例 | 至少 4 对实例，复制轨道及关闭其中一对后，剩余实例编辑、关联、输出和保存不受影响；配对无双重出声 |
| A12 | 视图生命周期 | 20 次视图打开关闭、实例移除重建；无残留 HWND、活动订阅或合成任务；区分进程级 runtime 常驻与实例泄漏 |
| A13 | 输入与 DPI | 100/125/150/200% 缩放；跨屏、中文/日文 IME、菜单、快捷键及鼠标命中一致，无方框字 |
| A14 | 异常与释放 | 引擎失败、取消、删除 region、关闭工程、移除 Bridge 后错误可诊断；无已销毁宿主对象回调 |
| A15 | 独立版回归 | 原有打开、编辑、播放、保存及插件加载行为不因嵌入适配退化 |
| A16 | 部署与许可 | 无开发机绝对路径依赖；依赖、运行时、引擎与声库许可清单完整；升级卸载不删除用户工程 |

验收记录至少包含阶段、commit、二进制版本、环境、输入工程、步骤、期望、实际、PASS/FAIL/MANUAL/PENDING/SKIPPED 及证据位置。测试夹具只证明适配逻辑；宿主接入结论必须来自真实 Cubase。

## 8 技术决策与范围控制

| 发现 | 处置 |
|---|---|
| 宿主没有已证实的歌词公开协议 | 保留 S3+ 未完成项，继续独立工作；列出需要厂商确认的精确接口问题，不自行增加内存注入或辅助程序 |
| Avalonia 内嵌出现焦点/IME/线程问题 | 在嵌入适配层修复并重测，不默认重写钢琴窗或退回外进程路线 |
| 当前引擎无法低延迟演唱 | 分离预览与乐句合成证据；提出最小缓存/适配方案。需更换引擎或新增合成算法时先报告成本与范围 |
| 全局状态影响多实例 | 只拆分已证实冲突的状态，不借机全面重构项目 |
| ARA 与 VSTi3 恢复关系不明确 | 使用明确身份、恢复阶段与显式绑定，禁止按加载顺序自动串联 |
| 需要公共 ABI 破坏 | 按项目规则停止该变更并请求维护者决策，其他独立工作继续 |
| 真窗口或人工验收 | 默认 SKIPPED（用户决定，未执行，需人工验证）；不启动软件、不等待用户、不阻塞阶段或交付，保留步骤 |

## 9 当前进度与下一步

| 阶段 | 当前状态 |
|---|---|
| M0 契约与可行性 | 自主实施/自动验证完成，f2f51b2a已推送；见 cubase-m0-baseline.md |
| M1 共享运行层与嵌入骨架 | 自主实施/自动验证完成，1cb004e1已推送；真实GUI SKIPPED |
| M2 VSTi3完整闭环 | 自主实施与本阶段非GUI自动验收完成；9b1ec329已推送且HEAD/上游一致，见 [M2验收记录](cubase-m2-validation.md) |
| M3 ARA Bridge闭环 | IN PROGRESS；source音符/tempo、身份归档、角色选择、区域合成/活动更新/通知及编辑试听已有CLI证据；跨包配对契约通过，实例绑定/路由/共享文档/UI/恢复、meter原点和最终覆盖待实施；GUI SKIPPED |
| M4–M6 | NOT STARTED，按原顺序推进 |

M2交付包括：插件内引擎/歌手选择与实例隔离、既有及legacy歌声引擎复用、完整版本化component state和Bypass参数状态、controller文档快照消息/恢复、异步代次缓存、整曲宿主播放和离线process、MIDI短音歌声缓存试听、样本级NoteOff/Bypass、float/double、状态反馈，以及当前块时间信息接收。

主测试818/818、legacy13/13、双validator各47/0；真实插件/声库的32/64-bit声音、state/seek-loop/双实例和并发调用证据已记录。全量回归发现并修正了新增测试夹具的同步上下文错误，没有放宽生产线程断言。详细命令、结果及证据边界见M2验收记录。

所有真实Cubase/V7、GUI、实机、硬件MIDI、安装及试听继续 **SKIPPED（用户决定，未执行，需人工验证）**。一秒音符加尾音的缓存试听不等于无限延音或实时连续演唱；当前块tempo不等于完整速度表。完整tempo/ARA对象映射归M3，双容器完整工具归M4，已证实宿主歌词链路归M5，发布与最终性能回归归M6。S3+目标保持未完成，不以M2通过冒充最终交付。

下一步：M2功能检查点与文档补记均已推送并确认远端一致；M3已通过非GUI归档/选择消息链路，并接入source音符、tempo及更新合并和v2关联元数据；Busy重试已接入并通过非GUI夹具，继续完成原生source身份恢复、多region/拍号应用、角色渲染和显式配对，真实窗口仍SKIPPED。继续优先使用主仓和third_party各自CodeGraph；不得把原有.gitignore改动、.codegraph、三份参考文档或out测试产物纳入阶段提交。

M3补记（2026-11-14）：已提供不替换共享编辑文档的 region 工程快照，保留 pending retry；原 synchronizeScore 复用候选快照后提交。新增无 GUI 回归及既有 ARA 状态夹具通过，日志 m3-region-projection.log。区域 RenderWorker/混音尚未接入，M3仍 IN PROGRESS，未形成阶段提交。

M3补记（2026-11-14）：Bridge 已按激活时 assigned-region 快照接入 RenderWorker 与 process 混音；真实本机声库的区域 crop/offset/overlap/seek/mono/stereo，以及实际 ARA 分配区域后的 Bridge float/double offline 输出通过（m3-assigned-region-final.log），Bridge validator 47/0。仅激活快照，活动更新、变速合成、editor audition/配对仍未完成；M3保持 IN PROGRESS，未提交推送。真实 Cubase验证仍 SKIPPED。

M3补记（2026-11-14）：区域变速合成已通过私有 ABI5 接入（BPM / 播放时长比、source offset × 同比），保持编辑/归档数据和音高字段；factory 声明基础 Timestretch。时间映射6/6和 actual Bridge float/double × 0.5/1/2 倍时长真实声库 CLI 测试通过，见 m3-render-scale-tests.log、m3-render-scale-synthesis.log。活动缓存更新/试听/配对/meter仍未完成，M3保持 IN PROGRESS。

M3补记（2026-11-14）：活动宿主编辑已通过 didEndEditing 的弱生命周期观察者重建播放快照；AraPlayback immutable Plan/原子发布/读者计数保证旧worker在非音频线程回收。actual Bridge激活期间区域移动，0.5/1/2时长×float/double六组真实声库验证通过；12次并发发布/14016音频块通过，validator47/0，日志m3-active-region-synthesis.log。插件内编辑的宿主线程调度、试听/配对/meter仍待完成，M3保持IN PROGRESS、未提交推送。

M3补记（2026-11-14）：确认Avalonia嵌入使用宿主STA线程后，已通过弱观察者接入settled-edit播放刷新，并校验宿主线程/编辑事务；actual Bridge callback→重新合成→process音频变化六组测试通过（m3-editor-refresh-synthesis.log）。多观察者/重复抑制/销毁清理及完整verify-host通过，双validator各47/0（m3-editor-refresh-host.log）。真实GUI仍SKIPPED；宿主dirty通知、editor audition、配对和meter应用继续实施，M3未完成未提交推送。

M3补记（2026-11-14）：modification编辑通知及合成完成的playback region样本失效通知已接入SDK宿主轮询；正确对象/flags、重复合并、恢复无回声及actual Bridge完成通知六组真实声库CLI测试通过。RenderWorker保证先发布samples再置成功，完整回归另见交接最新记录。editor audition/配对/meter仍待完成，M3保持IN PROGRESS，未提交推送，GUI仍SKIPPED。


M3补记（2026-11-14）：嵌入钢琴键按下/释放和关闭视图停止经私有ABI6接到所属EditorDocument，独立版默认AudioEngine试听路径保留。当前输入回调已接入，ARA editor renderer音频消费者仍待实施，不能称试听完成；后续配对/meter仍在M3范围内。公共ABI未变，GUI仍SKIPPED，阶段未提交推送。


M3补记（2026-11-14）：ARA editor renderer已接入ABI6键盘歌声缓存试听（直接region/sequence、editor-only与combined、松键/no-output flush/停处理/移除分配停止）；actual Bridge真实fu2_ning2_na4三组CLI测试及完整verify-host通过，双validator各47/0，日志m3-editor-audition-final.log和m3-editor-audition-host.log。下一步显式配对/单一正式输出、meter原点与多region最终验收；M3未完成未提交推送，GUI仍SKIPPED。

M3补记（2026-11-14）：实际双插件包证明CLR组件文档注册表分离，新增BCL-only AppDomain进程配对注册及私有ABI7，实际跨包Pair/query/peer removal/unlink通过m3-pair-cross-bundle.log。当前只完成注册/所有权契约，native实例/音频路由/共享工程/UI/恢复仍待接入，不宣称配对产品闭环。M3未完成未提交推送，GUI仍SKIPPED。


M3补记（2026-11-14）：私有ABI8把配对路由callback发布到native atomic；Instrument实例/ARA modification端点RAII及clone独立已接入。实际Instrument+真实声库验证Bind后工程与轨道MIDI静音、peer释放保持静音、显式Unlink恢复同cache；跨实际双包native callback及5项registry测试通过，详细日志见AGENT_HANDOFF最新条目。共享文档/UI/持久化仍待完成，M3未完成未提交推送。


M3补记（2026-11-14）：私有ABI9共享文档read/apply已接实际Bridge modification所有者，跨包BCL委托及版本化状态字节互通；非法CBOR/Busy保留/clone隔离、registry6/6及完整verify-host通过（m3-shared-document-host.log），双validator各47/0。Instrument视图/共享撤销、配对UI/恢复仍待接入，不能称配对产品闭环；M3未完成未提交推送，GUI仍SKIPPED。


M3补记（2026-11-14）：私有ABI10共享视图lease/provider和Bridge EditorDocument保活已接入；7项registry测试、跨实际双包的native协议（同owner、resize/focus、端点移除保活/关闭重试）及完整verify-host通过，双validator各47/0，日志m3-shared-view-cross-bundle.log/m3-shared-view-host.log。尚未切换Instrument实际IPlugView，下一步proxy文档/controller绑定，再配对UI/恢复；M3仍未完成未提交推送，GUI仍SKIPPED。

### M3 进度补记：标准视图代理（2026-11-14）

私有ABI11已将本地配对文档/视图proxy接入标准视图Create/Destroy/Resize/Focus，不创建第二份工程。跨两个实际插件包的非GUI协议夹具确认同一Bridge owner、文档释放约束、缩放/焦点、端点移除保活与关闭重试；完整verify-host通过，双validator各47/0。证据：m3-view-proxy-host.log、m3-view-proxy-cross-bundle.log。owner创建/关闭回调为协议替身，真实窗口未验证。Instrument controller按配对状态选择proxy尚待实现，M3未完成且未提交/推送，人工项继续SKIPPED（用户决定，未执行，需人工验证）。

### M3 进度补记：controller 配对选择（2026-11-14）

Instrument controller已根据Pairing.State选择Bridge文档proxy，重复同一身份通知不重建，缺失对端状态为空容器，解除配对恢复独立文档；processor在Bind/Unlink后主动回传含owner的状态。实际controller/IPlugView跨包无GUI协议及完整verify-host通过，双validator各47/0。实际Instrument+fu2_ning2_na4验证主动回传、配对单一正式输出及32/64位合成/状态/Bypass回归通过。日志m3-controller-pair-host.log、m3-controller-pair-processor.log。配对UI、身份/路由持久化、对端删除主动通知和meter/多region仍待实施；真实GUI仍SKIPPED，M3未完成未提交推送。

### M3 进度补记：对端移除主动通知（2026-11-14）

PairEndpoint已复用输出通知发布配对状态：注册消息线程即时通知，跨线程仅置待发标记，后续控制线程请求发送；失败保留重试。实际Instrument删除对端后无需额外Request即获status2并保持静音，解除后恢复输出；跨双包即时/跨线程/失败重试协议、真实本机声库32/64位回归及完整verify-host通过，双validator各47/0。日志m3-peer-notification-host.log、m3-peer-notification-cross-bundle.log、m3-peer-notification-processor.log。跨线程无消息时通过下次请求/重开查询更新；真实GUI仍SKIPPED。配对UI/关联持久化、meter原点及多region上下文仍未完成，M3保持进行中、未提交推送。

### M3 进度补记：显式配对栏（2026-11-14）

内嵌视图已增加配对栏：Bridge显示当前文档配对码，Instrument粘贴后显式绑定，两侧可解除，状态明确正式输出归属。私有ABI12把文档关联到对应端点，Bridge选区注册当前modification端点；异步UI按钮避免事件中同步销毁当前视图。托管/完整verify-host及实际Bridge+本机声库CLI合成回归通过，双validator各47/0（m3-pair-ui-host.log、m3-pair-ui-ara-synthesis.log）。真实窗口交互、复制粘贴、中文布局及焦点仍SKIPPED，不能称配对UI实机通过。配对码为当前进程标识，配对意图/输出设置持久化与重开后的显式重新绑定仍待完成；之后meter原点、多region及M3验收。阶段未完成、未提交推送。

### M3 进度补记：配对意图保存恢复（2026-11-14）

TLVSTP02新增配对意图，与Bypass同属版本化component参数；旧P01/无尾部状态可读。私有ABI13恢复为PeerMissing并保持Instrument静音，即使旧Bridge尚存也不自动重连或抢占；显式输入当前Bridge码后才能重绑。缺失状态显示配对/解除栏，隐藏独立乐谱，解决原空容器无法恢复绑定的问题。实际Instrument+本机声库验证保存/恢复、旧peer断开、显式重绑、复制到第二实例保持静音通过；注册表8/8及完整verify-host通过，双validator各47/0（m3-pair-intent-tests.log、m3-pair-intent-host.log、m3-pair-intent-processor.log）。无可靠宿主工程身份时按本计划采用显式重新绑定，不持久化runtime GUID或猜测跨工程关系。真实Cubase重开与恢复UI均SKIPPED；meter原点/多region上下文及M3最终验收仍待完成，阶段未提交推送。

### M3 进度补记：宿主拍号相位（2026-11-14）

SDK首bar signature对应bar 1并向前外推；私有ABI14已将宿主拍号送入managed投影。新增私有MeterOriginTick保存音源局部tick 0相对小节起点的偏移和宿主bar编号；Project/TimeSignatureManager仅增加默认0的内部构造支持，独立版原行为不变。文档恢复和正式合成应用偏移，三方合并保留歌词并更新宿主拍号；渲染副本保留该metadata。相关托管19/19、完整verify-host及实际Bridge本机声库CLI回归通过，双validator各47/0（m3-meter-tests.log、m3-meter-host.log、m3-meter-ara-synthesis.log）。真实GUI小节线与Cubase交互仍SKIPPED。多region/不同context的共享编辑和投影语义及M3逐项验收仍待完成，阶段未提交推送。

### M3 进度补记：多region编辑重投影（2026-11-14）

复现了跨context更换tempo时用户覆盖/手工音符及表达锚点仍用旧tick导致源秒漂移（1.25→2.5秒）。CubaseAraMerge已按source秒比较基线并重投影用户几何/表达/裁剪，宿主未覆盖字段仍跟随incoming；同tempo保留原表示。恒速及分段速度、不同原点、保存后的A→B→A往返等21项相关测试通过，完整verify-host和actual Bridge本机声库CLI回归通过，双validator各47/0（m3-region-reprojection-tests.log、m3-region-reprojection-host.log、m3-region-reprojection-ara-synthesis.log）。真实Cubase多region UI仍SKIPPED。下一步M3逐项最终审计与完整回归，尚未完成阶段或提交推送。

### M3 最终自主验收（2026-11-14）

M3逐项审计与必要回归完成，见[cubase-m3-validation.md](cubase-m3-validation.md)。最后补齐Bridge source-local播放位置：mailbox、region源秒、当前/待恢复文档tempo换算，私有ABI15；实际Bridge双连接顺序/失效context和分段pending时钟通过。解决方案build PASS，主测试843/843、legacy13/13，双validator各47/0，actual Bridge/Instrument本机声库回归通过。人工项仍SKIPPED。M3实施/非GUI验收完成，提交推送检查点待执行并确认后才进入M4；完整S3+目标未完成。

### M3 Git检查点（2026-11-14）

功能提交32433224c724b404866ef113f2ad4b5ec7fbac7f已独立提交并推送至origin/cubase_16_full_ara，ls-remote实际返回相同SHA，上游同名关系已确认。M3完成自主实施/非GUI验收及阶段检查点；当前只补记文档，随后按顺序进入M4。保留原.gitignore、.codegraph、out及三份参考文档。人工项继续SKIPPED，完整S3+目标未完成。
