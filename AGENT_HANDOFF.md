# 当前任务交接

更新时间：2026-11-14。持续目标 ACTIVE，分支 cubase_16_full_ara。

## 当前阶段与完成状态

M0 已完成并推送 f2f51b2a。M1 自主实施与非 GUI 自动验证完成，本次准备单独检查点提交并推送；须实际确认上游同步后再进入 M2。M2–M6 尚未开始。正式计划：docs/TuneLab_Cubase16_S3Plus_路线B实施计划.md；M0 证据：docs/cubase-m0-baseline.md。

M1 实现：双 VST3 controller 返回 HWND IPlugView；首次附着加载 CLR，包内 Contents/Resources/managed 定位托管程序集和 nethost.dll；构建每次复制托管输出，避免包内旧副本。私有 ABI 提供文档创建/销毁、视图创建/销毁、尺寸与焦点。CLR 进程常驻，不尝试卸载。

每实例持独立 ProjectDocument；视图绑定所属 MIDI part，重复获取保留编辑对象与历史。视图/控制器共享原生文档所有权，关闭视图不销毁 controller 文档。局部 EditorActionCollection 保留独立版全局 facade；钢琴窗动作、撤销/重做及剪贴板快捷键路由本视图。波形显隐、参数高度局部保存；用户偏好仍共享。关闭视图主动退订、停计时器、释放图像/嵌入根；成功清理后移除句柄。未更改 SDK/Foundation 公共 API。

## 验证结果与证据边界

- 相关 ActionCommands/KeymapDispatch/EditorActionCollection/CubaseDocumentContext 回归：59/59 PASS，m1-regression.log。
- legacy compat 测试：13/13 PASS，已实际 restore/test，不以无测试输出的 no-restore 返回值作为证据。
- verify-host.ps1：退出 0；双 validator 各 47/0；ABI 1、双文档释放、state 正常往返与拒绝未知版本/截断、三处 runtime 入口解析/文档释放 PASS，host-verification.log。
- view_protocol_probe：平台/无效父句柄、尺寸/焦点转发、移除失败重试、重复移除及所有权 PASS。使用测试回调，不创建 HWND，不证明真实 GUI。
- 托管集成构建 PASS。git diff --check PASS。存在原有 Cbor 8.0.0 NU1902 和编译警告，未扩大范围修复。
- 真实 Cubase/V7/GUI、IME、DPI、窗口焦点、关闭再打开、错误线程析构、安装：全部 SKIPPED（用户决定，未执行，需人工验证）。不得记 PASS，不等待或作为 blocked。

## 尚未完成与实际下一步

M1 还需本轮检查点提交/推送确认。随后开始 M2：先用现有引擎和指定声库跑本机非 GUI 歌声渲染，建立输出/缓存/取消基线；实现双插件内引擎与歌手选择入口、实例 SoundSource 选择和稳定标识恢复；接通 MIDI/参数/宿主时间、合成缓存与音频输出、完整 component state。再按计划进入 M3 ARA 数据/渲染、M4 嵌入体验、M5 歌词协议、M6 部署交付。

双插件当前仍为静音探针，state/archive 只有版本标记。控制器文档尚未与 processor/ARA 文档关联，属于后续闭环工作。Cubase Key Editor 歌词实际供给未证实：VST3 NoteExpressionTextEvent 是候选；不存在可据以承诺公开协议的 kARAContentTypeLyrics。不能用自有消息或模拟事件宣称宿主链路通过。

用户明确要求插件内可选引擎与歌手，VSTi3 与 ARA Bridge 均实现；复用引擎声库枚举，修改当前实例/part，保存稳定标识、显示缺失/错误，不硬编码唯一声库。新增 API 必须可选，通过能力检测或 DIM 默认体，不影响已有引擎加载合成，保持已发布 ABI。

本机测试声库：D:/AI-Tools/OpenUtau/Singers/fu2_ning2_na4-DiffSinger/fu2_ning2_na4，仅本机测试不分发；已有 APPDATA TuneLab Extensions 中 DiffSinger 1.0.0 legacy sdk 1.0，保留运行设备设置，自带 44100/hop512/128mel vocoder。无需再次询问声库。

## Git 与工作区

M1 修改集中于 TuneLab/Hosting、Input、PianoWindow/清理入口、integrations/cubase、相关测试及计划/本手册。预存 .gitignore 修改、未跟踪 .codegraph/、三份 docs 参考均保留且不提交；out/ 为构建产物不提交。提交号、推送状态以 git log/status/rev-parse 的检查点结果为准。

## 必须遵守

优先主仓及 third_party SDK 各自 CodeGraph。阶段按 M0→M6，单独提交推送确认再开始下一阶段。禁止 codex/ 前缀。保留用户修改，不扩大范围，不改冻结 SDK/Foundation 已发布签名。所有 GUI/实机验收跳过并注明；禁止 Qt offscreen。所有构建测试打包在前台 PowerShell Tee-Object 保存 build.log，并立即检查退出码。MSBuild 使用单节点 /nodeReuse:false；正常网络/进程通信需要时可申请相应执行权限。可靠额度最近检查周池已用 46%，未达预警；不得猜测后续额度。
