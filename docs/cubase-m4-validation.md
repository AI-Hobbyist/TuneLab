# M4 双容器钢琴窗验证记录

日期：2026-11-15。状态：自主实施与本阶段非 GUI 验收完成；Git 检查点待执行。分支：cubase_16_full_ara。

## 要求与证据

| 原计划要求 | 实现 | 证据及边界 |
|---|---|---|
| VSTi3 与 ARA Extension 共用编辑器 | 两个容器都使用 EditorView/PianoEditorSession；配对通过所属文档的共享 lease 创建视图 | 实际插件包的运行时、ARA 选择绑定及跨包协议通过；真实 Cubase 容器交互 SKIPPED |
| 完整钢琴窗编辑工具 | 五种既有工具及 1–5 快捷键、量化、局部撤销/重做/剪贴板；复用歌词、音素、音高、颤音和参数区操作；乐器声源不提供颤音工具 | 编译和实际数据事务回归通过；按钮、手势和显示效果 SKIPPED |
| 多视图共享文档而选择/缩放独立 | 每个内嵌视图创建 EditorSelectionState；音符、颤音、连续/分段锚点、绘制、拖动、菜单和脚本显式传递；各自时间/音高轴；共享同一文档撤销历史 | 同文档两份选择的复制/删除/粘贴/撤销、脚本回滚、锚点重建通过；真实双窗口交互 SKIPPED |
| 保留独立版与引擎接口 | 未传选择上下文时沿用模型旗标；既有引擎无新增必需 API；冻结 SDK/Foundation 和第三方源码未改 | 原脚本 API、锚点脏区及完整测试通过；PublicApiAnalyzers 构建无错误 |
| 焦点、IME、键盘归属 | 原生 onFocus 转发；文字输入不触发钢琴动作；未处理 IPlugView 按键保留 SDK 的返回宿主路径；表单期间禁用背景、Tab 循环，焦点指向活动表单 | 源码核对和协议焦点转发通过；真实宿主快捷键/中文日文 IME SKIPPED |
| DPI/缩放 | IPlugViewContentScaleSupport；挂载前比例和尺寸；IPlugFrame.resizeView；拒绝非法值和失败尺寸回退；布局比例为宿主绝对比例 / Root.RenderScaling | 挂载前、帧拒绝、托管失败回退及跨包 ScaleView/ScalePairView 探针通过；真实 DPI、命中和候选框位置 SKIPPED |
| 菜单与模态归属 | 升降八度使用局部动作集合；脚本菜单使用实例 Scope；脚本参数及批量歌词使用根内表单，错误在插件内显示 | 不再把内嵌根强转 Window；确认等待、取消、关闭/工程替换保护，以及批量歌词延音/单次撤销通过；实际表单交互 SKIPPED |
| 确定性释放 | 显式解绑视图/选择/缩放/计时器订阅，清理动作和属性控件，停用作用域、取消待确认表单；原生移除失败可重试，旧共享 lease 保持 owner 至成功关闭 | 原生/跨包引用所有权、端点移除、关闭重试协议通过；真实窗口资源观测 SKIPPED |

选择状态用弱键保存，不把已删除对象额外保活；它不写入工程模型旗标。连续自动化默认值换算及分段锚点重建显式传递选择上下文，保持编辑目标和新对象的本视图选中态。脚本读写及失败回滚使用相同上下文；默认入口继续使用原模型选择。

批量歌词复用原 LyricInput.ApplyLyrics 的分词、发音和跳过延音符逻辑。此功能是 TuneLab 内部编辑，不是 Cubase Key Editor 原生歌词同步；后者仍归 M5，完整 S3+ 目标未完成。

## 最终非 GUI 验证

所有构建/测试在 Windows 前台 PowerShell 执行，经 Tee-Object 保存 build.log 并立即检查退出码，按次保存以下日志；没有调用或探测 WSL，没有本地非 Windows 构建。

| 检查 | 最终结果 | 日志 |
|---|---|---|
| dotnet build TuneLab.sln -c Debug --no-restore -m:1 /nodeReuse:false | PASS，0 错误 | m4-solution-build.log |
| 主测试项目完整回归 | 855/855 PASS | m4-full-tests.log |
| Legacy 兼容测试完整回归 | 13/13 PASS | m4-legacy-tests.log |
| 最新托管集成程序集构建 | PASS，0 错误 | m4-selection-managed-build.log |
| 局部选择/歌词/脚本/锚点/动作相关回归 | 94/94 PASS | m4-selection-lyrics-tests.log |
| 完整 verify-host，含实际插件包和跨包协议 | PASS，退出0；ABI16；双 validator 各47/0 | m4-final-host-verification.log |
| 格式与差异检查 | 已检查新增及修改代码，限定格式处理，git diff --check PASS | 当前 Git diff |

完整宿主命令：设置 TUNELAB_DATA_DIR 为 out/cubase/m2/data，然后运行
`pwsh -NoProfile -File integrations/cubase/verify-host.ps1 -AraProjectState out/cubase/m2/fu2_ning2_na4.state`。
CMake 使用 Visual Studio 18 2026、x64、Windows SDK 10.0.26100，原生构建单并发。包内 managed 资源由 ALL 复制目标更新。本机 fu2_ning2_na4 声库和生成工程/音频/日志不随插件分发。

## 诊断与修正

- 原生 numeric_limits::max 与 Windows 宏冲突，已用括号形式修复，随后构建/探针通过。
- 输入表单放在集成程序集时无法访问既有内部属性控件，已移入主程序集复用现有友元边界，没有扩大可见性。
- 新增测试的构造前置条件及歌词入口缺失 using 已修正；最终回归通过，没有放宽业务守卫。
- 保留既有 Cbor NU1902 及原编译器警告，未做无关依赖升级；撤回格式器产生的无关空白变化并保留原 BOM。

## 人工状态与交付边界

全部真实 Cubase/V7、Windows Qt/Avalonia 窗口、Lower Zone/独立 Extension 窗口、中文/日文 IME、宿主快捷键、DPI、菜单、焦点、点击命中、反复开关和资源观测均为 **SKIPPED（用户决定，未执行，需人工验证）**。不启动、不等待、不作为阻塞，不记为 PASS。

原生视图探针只调用测试回调，不创建 HWND、不初始化 GUI；数据测试不创建控件。它们证明已覆盖的协议、编辑事务和所有权，不证明真实窗口显示或最终 Cubase S3+。

下一步仅执行 M4 Git 检查点：更新计划/交接 → 检查 diff → 单独提交并推送 → 确认远端。完成后按原顺序进入 M5，不再给 M4 增加范围。
