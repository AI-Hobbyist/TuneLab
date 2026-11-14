# 当前任务交接

更新时间：2026-11-14。持续目标已激活，分支 cubase_16_full_ara。

## 阶段状态

M0 自主实施与自动验证完成；真实 Cubase/V7/GUI 项全部 SKIPPED（用户决定，未执行，需人工验证），不作为阶段门槛。正式计划 docs/TuneLab_Cubase16_S3Plus_路线B实施计划.md，证据与限制 docs/cubase-m0-baseline.md。M1 尚未开始，必须确认 M0 检查点推送成功再实施。

## 已完成与验证

- integrations/cubase 下有独立 VSTi3、ARA Bridge 标准 bundle 探针；静音，仅发现/生命周期，不是产品。
- verify-sdk.ps1：SDK 示例构建、内容读取与播放渲染通过。
- verify-host.ps1：两插件 validator 各 47/0、hostfxr ABI 1、state 正常往返及截断/未知版本拒绝，通过。
- verify-bridge.ps1：属性、内容通知、modification 克隆、EditorView 协议通知、空文档版本标记归档，通过。
- Avalonia 11.3.15 嵌入根入口仅编译通过，没有创建窗口。所有测试在前台 Tee-Object 记录，退出码均 0。
- 原 ARA 示例自动部署故障已通过 ARA_SETUP_DEBUGGING=OFF 解决；缺 InitDll 已补 SDK dllmain；无活跃构建会话。
- 用户已明确选择 fu2_ning2_na4 仅本机测试，不分发。实际目录 D:/AI-Tools/OpenUtau/Singers/fu2_ning2_na4-DiffSinger/fu2_ning2_na4，自带 vocoder；使用已有 DiffSinger 1.0.0 扩展，保留运行设备配置。

## 未实现与证据缺口

完整编辑器附着、多实例、工程数据归档、引擎合成、缓存/播放、歌词同步、部署尚未实现。当前 archive/state 只有版本标记。VST3 NoteExpressionTextEvent 是公开候选，Cubase Key Editor 供给未证实；禁止虚构 kARAContentTypeLyrics 或用模拟事件宣称真实链路通过。实际声库渲染与确定性未测，后续 M2 执行，不重复询问声库选择。

## Git 与文件

M0 探针进展提交 4f6a884b 已推送；当前 M0 收尾修改包含私有 managed_abi.h、host_probe、计划和基线记录，本文件随正式检查点提交。最终提交号和上游同步以 Git 为准。开始时已有 .gitignore 修改、未跟踪 .codegraph/ 和三份 docs 集成参考，均保留，不提交。out/ 是本次构建产物，不提交。

## 下一步

1. 检查 diff/空白并提交推送 M0 正式检查点，确认 HEAD 与上游一致。
2. 读取 M1 所涉及 Editor/PianoWindow/工程上下文的 CodeGraph，先建立实例隔离与确定性释放，不进行全仓重构。
3. 实现后按阶段运行非 GUI 验证，更新记录，单独提交推送，再进入 M2。

## 必须遵守

用户最新要求：若补充 TuneLab API，必须作为可选能力，不影响已有引擎。优先可选接口，已有插件实现接口新增成员给 DIM 默认体；能力检测后启用，验证未实现新 API 的旧引擎仍可加载合成。遵守公共 ABI 演进登记，不改已发布签名。

优先主仓及 SDK 各自 CodeGraph；冻结 SDK/Foundation 公共 ABI；保留独立版行为和用户修改。所有 GUI/实机验证默认跳过，不启动、不反复申请、不等待、不作为 blocked；跳过不记 PASS。禁止 Qt offscreen。构建/测试/打包前台 PowerShell、Tee-Object build.log、立即检查退出码。阶段按序检查点提交推送；分支禁止 codex/ 前缀。SDK 目录是 E 盘符号链接，受限环境部分进程通信/网络需正常权限，已验证使用单节点 MSBuild /nodeReuse:false。
