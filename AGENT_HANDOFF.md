# 当前任务交接

更新时间：2026-11-14。

## 任务与完成状态

用户要求仅编写 TuneLab 深度集成 Cubase 16 的 S3+ 路线 B 计划书，明确需要 VSTi3 和 ARA Bridge，并让钢琴窗内嵌 VST3 界面。本次计划已写入 `docs/TuneLab_Cubase16_S3Plus_路线B实施计划.md`，未开始产品实施。

已查阅 docs 三份集成参考、命名规范，并优先使用主仓和 third_party 两个 SDK 各自的 CodeGraph。针对索引未展示的确切接口补读头文件。关键结论：本地 ARA 内容枚举没有 kARAContentTypeLyrics；EditorRenderer 是音频试听角色，GUI 通过 IPlugView；歌词协议需 M0 在真实宿主核实。

## 文件与 Git

- 当前分支：cubase_16_full_ara。用户已授权提交计划书与交接手册并推送同名远端分支；本记录随提交保存，最终提交及推送结果以 Git 分支和上游状态为准。
- 本次新增：上述计划书、本文件。
- 任务开始时已有 `.gitignore` 修改、未跟踪 `.codegraph/` 和三份集成参考 Markdown；均保留，不纳入任何提交。
- 实施分阶段提交推送规则已写入计划，但本次只有计划编写，不执行 M0–M6。

## 验证与未验证

- 已读取 `D:\Program Files\Steinberg\Cubase 16\Cubase 16.exe` 文件版本：16.0.5.174。
- V7 参考位置由用户指定为 `C:\Program Files\VOCALOID7\Editor`；未运行或动态分析。
- 未编译、测试、安装、启动任何宿主或插件。真实 UI、歌词链路、音频、多实例、归档等产品能力未验证。
- 没有实施失败项；宿主歌词协议是计划中的待验证技术项，不是本次文档任务的阻塞。
- 文档已执行 diff 空白检查；未运行与文档无关的构建或测试。

## 下一步

本次无剩余实施任务。等待用户审阅计划；只有用户后续授权实施后才开始 M0。先复核计划和实际 Git 状态，保留既有修改。

必须遵守：优先使用各自 CodeGraph；不扩大范围；冻结 SDK/Foundation 公共 ABI；真实窗口 GUI 验证且禁止 Qt offscreen；所有构建/测试/打包前台 PowerShell 配合 Tee-Object 和立即检查退出码；人工项记 MANUAL/PENDING 或用户决定的 SKIPPED，不作为 blocked 条件；分阶段自动验证后更新记录、检查 diff、提交、推送并确认，再进入下一阶段。分支不得使用 codex/ 前缀。
