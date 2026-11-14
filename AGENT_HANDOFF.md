# 当前任务交接

更新时间：2026-11-15。持续目标 ACTIVE。分支 cubase_16_full_ara。

## 当前阶段与 Git 检查点

- 原目标不变：按 docs/TuneLab_Cubase16_S3Plus_路线B实施计划.md 的 M0→M6 交付 VSTi3 + ARA Bridge、进程内钢琴窗、合成/时间/音符/歌词关联、工程恢复与发布材料，最终 S3+ 尚未完成。
- M0–M3 已完成自主实施与非 GUI 验收并推送。M3 功能提交 32433224c724b404866ef113f2ad4b5ec7fbac7f，文档补记 ae7cf2bb2cad11379c52b510d9362b81cf0a2f74，均已在此前实际确认远端一致。
- M4 双容器钢琴窗已完成自主实施、非 GUI 验收和阶段检查点；功能提交 78c7f251deb71e3b6cc1e2d413dd6fd57d724f2a 已推送 origin/cubase_16_full_ara，ls-remote 实际确认同 SHA，上游同名。当前仅补记已确认的 Git 状态。
- M5–M6 未开始。Cubase Key Editor 原生歌词通道尚未证实；不得把 TuneLab 内部歌词输入、ARA 音符导入或自有消息当作原生歌词同步。
- 正式 M4 逐项验收：docs/cubase-m4-validation.md。历史 M2/M3 逐项证据：docs/cubase-m2-validation.md、docs/cubase-m3-validation.md。

## M4 已完成实现

- 两个容器沿用同一 EditorView/PianoEditorSession；局部动作和工具栏、五种钢琴工具及快捷键、量化、剪贴板、既有参数区与歌词/音素编辑。操作进行中限制工具切换/撤销。
- 每个内嵌视图拥有 EditorSelectionState，弱键保存对象选择；音符/颤音/连续与分段锚点的渲染、拖动、复制、删除、粘贴及脚本读写/失败回滚显式传递该状态。独立版默认保留模型旗标语义；同文档仍共享撤销历史，各视图时间/音高轴独立。
- 右键升降八度使用实例动作集合，局部临时菜单不订阅全局 Keymap.Changed。脚本菜单使用实例 Scope，不安装全局工程提供器。
- EmbeddedScriptInputs 在插件根内复用现有属性控件，确认/取消/重置、响应式 schema、焦点与 Tab 范围；关闭/片段切换取消，工程替换后拒绝旧结果，错误在插件内显示。批量歌词也使用此入口，复用原 LyricInput.ApplyLyrics 的分词/发音/跳过延音符逻辑，不强转内嵌根为 Window。
- 私有 ABI16：IPlugViewContentScaleSupport、挂载前比例/尺寸、IPlugFrame.resizeView、失败回退、跨包 ScaleView/ScalePairView。托管布局按 host factor / Root.RenderScaling，不更改宿主 DPI 策略。
- 显式清理订阅、计时器、属性控件、动作、试听和 Root；作用域停用与表单取消避免关闭后的续体执行。原生 removed/共享 lease 保持已有关闭重试及 owner 生命周期。
- 未改冻结 TuneLab.SDK/Foundation 公共 API、既有引擎必需接口或第三方源码；私有数据/脚本选择参数均可选，原入口默认行为保留。

## 最终验证（M4）

- m4-solution-build.log：Windows Debug 解决方案构建 PASS，0 错误。
- m4-full-tests.log：主测试 855/855 PASS。
- m4-legacy-tests.log：legacy 兼容 13/13 PASS。
- m4-selection-managed-build.log：最新托管集成程序集构建 PASS。
- m4-selection-lyrics-tests.log：选择/歌词/脚本/锚点/动作相关 94/94 PASS，含同文档双选择、音符/颤音剪贴板与撤销、锚点重建和脚本回滚。
- m4-final-host-verification.log：完整 verify-host 退出0；ABI16；双 validator 各47/0；实际插件包的运行时、ARA/状态/时间/缓存/视图及跨包所有权、缩放协议通过。CMake ALL 目标复制最新 managed 资源。
- 限定格式处理、原 BOM 保留和 git diff --check 已执行。保留原 Cbor NU1902 和既有编译器警告，没有无关升级。
- 修复过的构建问题：Windows max 宏冲突、表单所在程序集无法访问既有内部控件（移入主程序集）、测试构造前置条件及新歌词入口 using。最终均已通过，未放宽断言。

## 未验证事项与证据边界

所有真实 Cubase/V7、GUI、中文/日文 IME、宿主快捷键、菜单、DPI/点击命中、反复开关与资源观测、硬件、安装和试听均 SKIPPED（用户决定，未执行，需人工验证）。不启动、不等待、不作为 blocked，不记为 PASS。视图探针不创建 HWND；数据/委托测试不创建控件，不冒充真实双窗口或 Cubase S3+ 验收。

本机声库仅 fu2_ning2_na4 用于测试，不随插件分发。M3试听仍是一秒音符加尾音缓存，不是无限延音/连续实时演唱；M3 既有边界与音频证据见其正式验收记录。

## 当前修改与保留项

M4 已提交的修改涉及 TuneLab/Data 的选择与相关编辑入口、TuneLab/Hosting 的视图/表单/配对租约、脚本上下文与句柄、钢琴窗/参数区/歌词输入、integrations/cubase 的 managed/native 视图缩放和协议探针，以及阶段测试、计划和本手册。

本阶段新增文件包括：TuneLab/Data/EditorSelectionState.cs、TuneLab/Hosting/EmbeddedScriptInputs.cs、TuneLab/UI/MainWindow/Editor/PianoWindow/PianoScrollView/PianoScrollViewLyrics.cs、integrations/cubase/managed/EditorToolbar.cs、tests/TuneLab.Tests/CubaseScriptScopeTests.cs、tests/TuneLab.Tests/EditorSelectionStateTests.cs、docs/cubase-m4-validation.md。上述均已纳入 M4 功能检查点 78c7f251deb71e3b6cc1e2d413dd6fd57d724f2a 并推送。本次文档补记没有新增实施变化。

不得 stage 用户原有 .gitignore 修改、.codegraph/、out/ 或三份参考文档：docs/Cubase16_SVS_接入静态分析.md、docs/S3+_交付与接口清单分析.md、docs/TuneLab_接入Cubase16_方案分析.md。模型、生成音频/工程、日志不分发。

## 下一步顺序

1. M4 已完成且功能提交已推送确认；没有剩余 M4 自主实施任务，人工项携带 SKIPPED 正常收尾。
2. 本次只补记检查点文档，推送并确认 HEAD/远端一致后进入 M5，不再给 M4 增加范围。
3. M5 按原计划核对 M0 的宿主歌词证据，落实已证实通道及音符/歌词关联、冲突和增量重合成；不虚构协议，不默认降级 S3+。人工项始终非阻塞。
4. 每阶段同样完成记录→检查 diff→提交→推送→确认，之后才进入下一阶段。

## 必须遵守的约束与运行环境

- CodeGraph 优先；主仓与 third_party SDK 各自索引。不得初始化用户未决定的索引。
- 本机16GB：仅原生 Windows 工具链和 Windows 目标。禁止任何 WSL 调用、列举、探测、启动或间接包装；不进行本地 Linux/其他目标构建。
- 所有构建/测试/打包前台 PowerShell，2>&1 | Tee-Object -FilePath build.log，立即记录并检查 LASTEXITCODE，失败读日志。不并行占用多个构建进程；native 使用 --parallel 1 -- /nodeReuse:false。
- Qt offscreen 禁止；真实 GUI 按用户决定全部跳过，不能用 headless/offscreen 代替 PASS。
- 冻结公共 ABI；新增 TuneLab API 必须可选且兼容既有引擎；不做无关重构或扩大阶段范围。
- 不猜额度/订阅。最近可靠查询 primary used56%，仍允许使用，未触发交接阈值。仅阈值触发后才委派 Luna 整理手册；当前手册由当前 Agent 更新。
- 核心路径：out/cubase/m0/native；managed 项目 integrations/cubase/managed/TuneLab.Cubase.Managed.csproj；两插件位于 out/cubase/m0/native/VST3/Debug/。
- 验证前设置 TUNELAB_DATA_DIR=out/cubase/m2/data；本机测试状态 out/cubase/m2/fu2_ning2_na4.state。verify-host.ps1 -AraProjectState 该文件执行全套非 GUI 协议；运行日志位于仓库根，未加入 Git。

## 历史实现与验证资料（当前阶段以以上记录为准）

## 已完成实现

M3已完成自主实施/非GUI验收（2026-11-14）：生产ARA modification拥有独立EditorDocument，clone深复制；TLARA001按SDK筛选器归档/部分恢复/持久ID重映射及活动managed handle回滚已接入。Editor角色选择经实际Bridge组件/SDK ConnectionProxy/私有令牌关联到IPlugView，覆盖双连接顺序、断连与重连；真实窗口仍SKIPPED。

source note内容、版本1身份匹配、musical context tempo/meter与region秒/quarter映射已实现；source音符/tempo经私有ABI进入TuneLab工程，v2 state保存baseline/current ID/原点，旧v1保持兼容。三方合并已接入更新回调，保留歌词、固定音素与用户覆盖；Busy保护未提交编辑，并由现有编辑轮询重新读取最新managed状态后重试，region销毁使待重试安全失效。核心托管18/18、完整verify-host、双validator各47/0及Busy/销毁夹具通过。最新日志见下文各补记。

source身份与计数器已通过TLARA002按筛选器归档恢复和新文档ID重映射。Bridge已支持区域合成、变速、裁剪/偏移/重叠、活动host/editor更新、宿主编辑通知及合成完成通知；immutable Plan保证音频线程不释放worker。真实本机声库CLI路径和双validator通过，证据见下文最新补记。

当前私有ABI16（M4增加视图缩放）：ARA源数据/tempo/拍号原点、版本化关联/归档、区域合成/试听、source-local播放位置及显式配对均已实现。跨包共享文档与视图lease、唯一正式输出、配对码UI、PeerMissing意图保存恢复，以及多region源时间重投影有对应自动证据。M3验收记录见docs/cubase-m3-validation.md；解决方案build、主843/843、legacy13/13、双validator各47/0和本机SVS CLI回归通过。功能检查点32433224c724b404866ef113f2ad4b5ec7fbac7f已提交推送并经远端确认。M4已开始，当前工具栏变更尚未提交。真实GUI/Cubase保持SKIPPED，不称实机S3+通过。

M1钢琴窗与CLR基础保持：每实例ProjectDocument、局部动作/撤销/剪贴板、共享controller/view文档所有权；包内hostfxr/nethost及Avalonia嵌入根，显式视图清理。

M2包括：

- PluginServices初始化现有/legacy引擎；组件ALC与插件共享所属宿主契约，compat和旧SDK进入私有托管包。插件内选择引擎/歌手，稳定ID、不可用提示、撤销/重做及保存恢复；公共SDK/Foundation ABI未改。
- CubaseProjectState以TLCUBASE/version1封装原NativeProjectFile CBOR。Native文档copy/restore/callback、controller连接重发、IMessage快照、processor验证/缓存/恢复闭环。恢复抑制回声，活动视图UI线程detach/rebind。
- 私有Project/AudioGraph不注册Standalone音轨；独立版默认行为保留。SnapshotRenderer、固定租约、请求取消、输出率转换、latest-only RenderWorker及原生AudioCache。音频process不调用CLR/分配/锁/回收缓存。
- MIDI16通道、SPSC、64槽held-note身份/力度/游标、独立preview worker/每pitch代次缓存、样本偏移NoteOff、float/double混音、无输出flush。原曲播放按宿主sample position，offline setActive非音频等待。
- VSTi3标准Bypass（ParamID0，可自动化、离散两态），逐样本队列处理；component state后附可选TLVSTP01/12字节参数尾部，旧状态默认false，坏版本/截断拒绝。editor快照消息只恢复工程，不清宿主参数。Bridge暂不暴露该参数。
- 非音频transport消息反馈工程/试听准备状态及耗时，私有callback接内嵌状态栏；HostTimeMailbox一致采集当前块sample/rate、音乐位置、tempo、拍号/bar及flags。尚未将其写成完整速度表。

## 已验证（M2历史基线，M3见首段与最终检查点）

- 解决方案Debug构建PASS：m2-solution-build.log。
- 主测试818/818：m2-full-tests.log；legacy13/13：m2-legacy-tests.log。
- verify-host.ps1退出0，双validator各47/0；ABI/三个目录、state、文档/视图协议、缓存/MIDI/时间并发、参数及预览夹具：m2-host-verification.log。
- 真实VST3 SDK宿主+DiffSinger fu2_ning2_na4：48k32-bit/44.1k64-bit歌声、MIDI、Bypass样本128–255静音、参数恢复及editor更新保留、state/seek-loop/双实例PASS：processor-verification.log。
- 两线程同时调用真实整曲/预览合成：144000/96000帧，peak0.302906/0.234372，finite/nonzero，concurrent-render-verification.log。
- 真实最新代次worker/旧率拒绝/空状态失效：m2-render-worker-final.log。
- 新增渲染测试原用基础SynchronizationContext，Project构造合并通知的时基回调落到ThreadPool使全量测试崩溃。已仅修测试为PumpableSynchronizationContext并显式DrainAll覆盖触发点；原全量命令最终818/818。未改生产断言，临时[DEBUG-m2-thread]代码清除，诊断日志归档out/cubase/m2/context-post.log。
- clang-format及git diff --check执行PASS。保留原Cbor NU1902及原编译器警告，未做无关升级。

## 未验证与实际限制

真实Cubase/V7/GUI、中文状态栏布局、IME/DPI/焦点/窗口恢复、硬件MIDI、安装、试听全部SKIPPED（用户决定，未执行，需人工验证），不是PASS，不等待、不作为blocked。

试听是当前声源/歌词的一秒note加尾音缓存，不是无限延音/实时连续歌唱。原生cache miss起音40–109ms是在已有引擎/磁盘缓存的本机测量，不是首次冷启动指标。tuning数值保留但未应用。真实引擎并发调用、确定性复音夹具与真实Cubase调度分别记录，不互相冒充。

完整tempo表/变速重合成、ARA对象/region/selection/archive/角色与显式配对已完成M3自主实施/非GUI验收；M4双容器工具与生命周期，M5原生歌词协议，M6部署/许可/最终回归。Cubase原生歌词协议仍未证实，不得把自有消息或手动输入称为Key Editor歌词同步。Bridge区域渲染和归档实现已在上文M3进度记录，不再是M0仅静态探针。

## Git与工作区

M3功能提交32433224已完成并确认推送；本次仅补记检查点状态。不得把用户预先存在的以下内容stage：.gitignore、.codegraph/、out/、三份未跟踪参考文档（Cubase16_SVS_接入静态分析.md、S3+_交付与接口清单分析.md、TuneLab_接入Cubase16_方案分析.md）。所有模型、测试生成工程/音频、日志均不分发。没有改动third_party源码或冻结公共API文件。

## 下一步顺序

1. M3功能检查点32433224已提交推送且远端确认；本次仅补记检查点文档。
2. 按原计划开始M4，先CodeGraph核对双容器编辑工具、状态呈现及所有权/释放，再执行范围内最小必要实现和非GUI验证；不得跳到M5。
3. 每阶段实施/自动验收完成后更新计划与本手册、检查diff、独立提交推送确认。全部真实GUI/宿主/人工项SKIPPED，不等待、不作为blocked。

## 环境与约束

VS18/2026 MSVC19.51，WindowsSDK10.0.26100；.NET8，hostpack8.0.6/hostfxr8.0.26，Avalonia11.3.15。Cubase16.0.5.174与V7仅静态读取；不启动。third_party SDK索引/链接保留。

所有build/test/package前台PowerShell经Tee-Object写build.log并立即检查LASTEXITCODE；CMake --parallel 1 -- /nodeReuse:false。正常Qt真实窗口才可作为GUI证据，禁止offscreen。现阶段GUI默认SKIPPED，不以headless截图冒充。

本机声库fu2_ning2_na4仅测试，不随插件分发；隔离数据out/cubase/m2/data、基线state为out/cubase/m2/fu2_ning2_na4.state。不改变用户设备/CUDA/引擎配置。新增TuneLab引擎API必须可选，现有引擎保留行为。

额度接口最近已用49%，未触发阈值；不得猜测额度。仅阈值触发时按用户规则委派GPT-6 Luna整理交接，正常开发不换模型。

## M3 非空归档夹具补记（2026-11-14）

新增 integrations/cubase/native/ara_state_probe.cpp 与 CMake 目标 tunelab_ara_state_probe。夹具直接调用生产工厂与真实 ARA C 接口；从本机基线 state 读取非空工程，通过 SDK 对象创建/clone/store/restore 验证文档独立、完整及部分归档、持久 ID 重映射。第二条记录为非法 CBOR 时恢复失败，第一条待写状态不落地，原对象均保持原值。使用真实托管 CBOR 校验，不新增生产测试入口。

编译 PASS；运行命令 out/cubase/m0/native/Debug/tunelab_ara_state_probe.exe out/cubase/m2/fu2_ning2_na4.state PASS，日志 m3-ara-state-probe.log。无 GUI、无声库打包、不声称实际 Cubase 通过；活动视图恢复仍未实现。CMake 为夹具复制私有 managed 运行时到构建目录 Resources/managed，遵循现有模块路径解析。当前 M3 未完成，修改未提交/推送。下一最小实施：把 Bridge 编辑器绑定到所选 modification，并完善活动文档恢复；随后对象/tempo/渲染/配对。

## M3 EditorView 角色选择解析补记（2026-11-14）

生产 doCreateEditorView 改为创建 BridgeEditorView；selectedDocument 从 SDK 当前 effective playback regions 解析首个 region 对应 modification 的独立 EditorDocument，空选择返回空。不跨 host editing 周期持有 region 原始指针，避免对象删除后的悬空引用。实际 VST3 EditorView 与该角色的连接仍未实现，不能把角色解析称为窗口绑定完成。

ara_state_probe 新增真实 ARA editor role、context/sequence/region 创建、选择通知、所选文档身份断言及删除 region 后空选择断言，全部 PASS。setEditorOpen 仅协议状态，不创建 GUI；真实 GUI/Cubase 仍 SKIPPED。Bridge 实际插件重新构建 PASS，VST3 validator 47/0（m3-bridge-editor-role-build.log）。clang-format 已应用；M3 修改未提交/推送。可靠额度接口本次已用50%。下一步为 companion API processor/controller 关联与实际视图文档切换，必须处理 proxy、连接先后及销毁，不能用裸地址消息冒充宿主标准协议。

## M3 已创建托管文档恢复补记（2026-11-14）

doRestoreObjectsFromArchive 不再因 editor handle 非零而直接拒绝。全量解码及 payload 校验后，先通过 CopyDocumentState 捕获所有已创建托管文档；再调用现有 RestoreDocumentState（已有视图时由该接口 detach/rebind，未开视图时保持 pending）；全部成功才更新 native state。某条调用失败，包含失败对象在内逆序恢复原 managed 状态；回滚失败明确日志报错，不静默成功。

新夹具实际创建两个 managed 文档但不创建 GUI，验证非空/空项目替换、非法 CBOR 无变更，并在第二个文档的私有 ABI 函数指针边界注入一次失败，验证第一个已经替换的真实 managed 文档与 native 快照恢复到原值。该注入只在测试程序，不新增生产测试 API。编译/夹具 PASS，实际 Bridge validator 47/0，日志 m3-ara-live-restore.log。真实活动窗口恢复仍 SKIPPED，不以无视图 handle 测试冒充 GUI PASS。M3 未完成、未提交推送；下一步仍为 ARA editor role 到实际 VST3 view 的关联和文档切换。

## M3 原生视图文档切换补记（2026-11-14）

核对 vst3sdk CodeGraph ConnectionProxy/ComponentBase：连接对端仅可 notify，不能强转 peer。已为 EditorView 增加 setDocument：未 attach 时仅替换所有权；attach 后先创建目标 managed view，成功后移除旧 view，再提交文档切换；空文档清除子视图并保留宿主容器。目标创建失败保留原视图；重复 attached 拒绝，避免空选择容器重复 attach。

view_protocol_probe 用私有 ABI 回调夹具验证 A→B→空→A、目标创建失败保留旧视图、父容器保持 attach、文档不提前销毁与最终释放数量，PASS（m3-view-switch.log）。无真实 HWND/GUI，不冒充视觉或实际 managed 窗口切换验证。ARA 角色到控制器的消息关联仍未接入；setDocument 是该连接所需的视图能力，不代表整条绑定完成。M3 继续进行，未提交推送。

## M3 控制器文档关联消息补记（2026-11-14）

新增 editor_binding.h：插件模块内递增实例令牌、互斥保护的弱文档注册表、RAII 注销。令牌不是地址、不存入项目；只支持当前路线要求的同进程同插件模块关联。EditorController 的 Bridge 路径接收私有 TuneLab.ARA.EditorBinding 消息及 token 属性，解析文档并调用所有存活视图 setDocument；未知令牌拒绝，0 表示清空。通过 EditorView::Reference 弱生命周期引用避免保存已销毁 view 指针；后续 createView 采用已关联文档。Instrument 不接受该消息，原有工程/参数路径保留。

view_protocol_probe 验证两实例令牌独立、空选择不串文档、注册注销后旧令牌失效，连同原有视图切换测试 PASS。Bridge 编译/validator 47/0（m3-editor-binding.log）。本轮尚未直接测试控制器消息接收行为；更未完成 processor 侧消息发送与 ARA 角色开闭/选择回调连接，不能称完整链路通过。下一最小任务：接入这些发送与生命周期路径，并用 SDK ConnectionProxy 夹具验证连接先后、选择变化和销毁。GUI 保持 SKIPPED。M3 未完成，修改未提交推送。

## M3 processor 选择通知发送补记（2026-11-14）

BridgeProbe 现持 EditorBinding。绑定 ARA 后为 BridgeEditorView 接 selectionChanged 回调，选择变更更新弱文档注册表并发送 TuneLab.ARA.EditorBinding/token。processor connect 在已绑定时重发；控制器第一次收到关联后重发 EditorOpen，处理连接先后导致首次 open 消息丢失的情况，避免重复互发递归。EditorController 按所有存活视图的实际 isAttached 状态发送 EditorOpen；EditorView attached/removed 成功后通知，弱 callback 在 controller 析构时清除，避免悬空捕获。BridgeEditorView::setOpen 控制 SDK setEditorOpen；doNotifySelection 回调选择，DocumentController::didEndEditing 刷新（SDK 删除 region 会清空选择）。processor 析构先清除角色 callback。

ara_state_probe 现验证真实角色选择回调和删除 region 后编辑事务完成通知空文档，PASS；view_protocol_probe 回归 PASS（m3-selection-notification.log）。实际 Bridge 编译及 validator 47/0（m3-binding-sender-build.log）。生产发送/接收代码均已接入，但完整 ConnectionProxy 链路、断连清理及 GUI 仍未验证，不能称 M3 编辑绑定已验收完成。下一最小任务：ConnectionProxy 协议夹具覆盖连接顺序、消息响应和销毁；补 controller/processor disconnect 清理与必要失败处理。M3 未完成，修改未提交/推送；GUI 仍 SKIPPED。

## M3 实际组件 ConnectionProxy 验证补记（2026-11-14）

ara_state_probe 现编译链接真实 bridge_probe.cpp，通过真实 GetPluginFactory 创建 IComponent/IEditController，使用 SDK HostApplication/双 ConnectionProxy，而非伪造 processor/controller。验证 processor 先连接和 controller 先连接两种顺序；真实 ARA editor role 的 notifySelection 经生产令牌消息到达实际 controller 的 IPlugView，createView 私有 ABI 替身收到所选文档真实 handle。仅窗口创建/销毁替身化，无 HWND/GUI。断开 controller 连接断言已销毁所选文档子视图；断连后 notifySelection 不重建视图。完整归档/活文档回滚/角色选择及这两种代理顺序一起 PASS（m3-connection-proxy.log）。

生产修正：Bridge createView 在未选 ARA 文档时使用空容器，不创建独立插件工程；Instrument 仍使用原文档。EditorController disconnect 清除视图文档、ARA 文档引用与关联标志；processor disconnect 关闭角色并清除弱文档选择。双插件 Debug 编译、validator 各47/0、git diff --check PASS。真实 Cubase/GUI 仍 SKIPPED，不以 ABI 视图替身证明视觉正确。

M3 尚未完成，未提交/推送。下一最小任务：扩充重连及多实例同时存活验证、实际 managed 视图未验证状态记录；然后按计划接 source 内容、region 几何与 musical context tempo 导入和 Bridge 角色渲染/配对。不要重复实现已通过的生产选择消息链路。

## M3 重连与 musical context tempo 补记（2026-11-14）

真实 Bridge/ConnectionProxy 夹具追加断连后重连：保持父容器 attached，重新连接并发送 host selection，所选 managed 文档 handle 再次驱动视图创建，PASS（m3-reconnect.log；也含在后续全夹具中）。

新增 ara_timeline.h：ARA tempo sync points 原样保留，以秒/quarter 双坐标分段线性映射，首尾段按 ARA 规则外推，支持 quarter 0 对应非零 song seconds。拒绝不足两个点、非有限/不递增坐标、缺 quarter-zero 的完整 map。生产 BridgeMusicalContext 在 didAddMusicalContextToDocument 和 doUpdateMusicalContextContent 用 HostContentReader 读取完整 tempo entries；无内容时 timeline 为空，错误时 invalidTimeline 标记并日志，不伪造默认速度。

夹具新增完整 ARAContentAccessControllerInterface 的 tempo 回调：创建 musical context 时读取，改变 host sync points 后 updateMusicalContextContent 替换映射，并断言 reader 全部释放。验证两段不同速度、前后外推、非零原点及多个 quarter 正反往返。m3-context-tempo.log 编译/全夹具 PASS；首次编译因测试 lambda 自动推导匿名枚举与 ARAContentGrade ABI 不符失败，已显式返回 ARAContentGrade 修正。

该 tempo 快照尚未传给 TuneLab 工程速度表/合成，拍号尚未接入；不得据此声称时间同步闭环完成。下一最小任务：处理拍号与 region 坐标，并将 host context 映射实际应用到所选 mod 的 TuneLab 编辑/渲染数据。M3 未完成、未提交/推送，GUI/Cubase 仍 SKIPPED。

## M3 region 坐标映射补记（2026-11-14）

新增生产 BridgePlaybackRegion，经 doCreatePlaybackRegion 创建，直接读取 SDK 当前 region 属性，不缓存旧几何。modificationSecondsAtSongSeconds 对实际播放范围采用左闭右开，映射到 source/modification 秒位置；songQuarterAtModificationSeconds 经当前 region sequence 的 BridgeMusicalContext tempo map 转换到宿主 quarter。空长度、范围外或无 tempo 时不返回伪造坐标。尚未启用/宣称音频 time-stretch，也未将该换算接到最终音符/渲染。

真实 C ARA 对象夹具更新 region 属性验证源偏移0.25、裁剪0.5、播放偏移14、tempo变更后的quarter位置、移动到10秒后新映射、边界排除及零长度，再恢复非空region运行原消息/归档/回滚测试，PASS（m3-region-mapping.log）。clang-format/git diff --check PASS。verify-host.ps1 新增可选 -AraProjectState 参数，以显式工程state运行该完整夹具；不把本机声库路径写入默认命令。

M3 未完成、未提交推送。下一最小任务：补拍号内容及接 managed 的 host context/源音符导入，使当前已验证时间与region映射进入实际编辑和合成。额度可靠查询仍为已用50%，未触发交接阈值；GUI/Cubase 仍 SKIPPED。

## M3 musical context 拍号补记（2026-11-14）

BridgeMusicalContext 读取 tempo 后追加 HostContentReader<BarSignatures>；AraTimeline 同一快照保存宿主原始 numerator/denominator/quarter position。校验正拍号、有限位置、严格递增且变化落在前一拍号的整小节边界；缺拍号保持空列表，不伪造4/4。完整上下文校验失败时 invalidTimeline 标记，避免半份新tempo与旧meter混用。

C ContentAccess 夹具升级为按类型创建/销毁独立 reader；验证第一拍号position=-4被保留、后续3/4读取、变更7/8替换，以及所有reader释放。编译/全ARA夹具 PASS（m3-context-meter.log），clang-format/git diff --check PASS。还未将tempo/meter应用到TuneLab工程/合成，不能称完整时间同步。

主仓 CodeGraph 核对 TimeSignatureManager：CorrectStatusFrom(0) 固定第一拍号Pos=0；TempoManager 使用 MusicTheory.RESOLUTION tick与BPM，时间原点也由现有转换快照管理。ARA保留song seconds/quarter-zero以及首小节位置，因此接 managed 时必须显式保存宿主原点偏移，不能直接将非零quarter的首拍号写成barIndex0便宣称等价。保持公共SDK及独立版行为，使用宿主私有映射/元数据。下一最小实施为私有host context数据传递与源note导入，处理这个偏移及同mod多region上下文。M3未完成、未提交推送；GUI仍SKIPPED。

## M3 source note 内容读取补记（2026-11-14）

生产 BridgeAudioSource 经 doCreateAudioSource/didAddAudioSourceToDocument 创建并读取 HostContentReader<Notes>，doUpdateAudioSourceContent 更新全量快照和 contentRevision。保留完整 ARAContentNote（frequency、pitchNumber、volume、source秒起点、attack/note/signal duration），区分 notesAvailable 与 invalidNotes。无内容或错误均清旧快照，避免后续使用旧音符；不把音高/起点当永久身份、不折叠同起点同音高。标准 ARA note 没有歌词字段，本路径没有实现歌词同步。

真实 ContentAccess C接口夹具提供两个同音同起点不同持续时间事件，验证两者保留；更新删除/移动后快照替换且revision递增；空但available与unavailable两种状态区分，reader释放，PASS。现共享内容reader计数名为contentReaders。完整原归档/活文档回滚/实际Bridge代理选择/断连重连/tempo/meter/region测试一并PASS（m3-source-notes.log）。clang-format/git diff --check PASS。

此步骤只读到原生source快照，尚未写进managed工程，也未完成跨revision音符身份匹配/用户编辑覆盖保留。下一最小任务为私有ARA数据传递及实际TuneLab音符/tempo/meter映射；同时保留source与每mod独立编辑、每region几何/上下文边界。M3未完成、未提交推送；GUI/Cubase仍SKIPPED。

## M3 source note 身份匹配补记（2026-11-14）

新增 ara_note_identity.h，araNoteMatchingVersion=1。BridgeAudioSource 保留与 notes 平行的 noteIds，ID在source persistent ID域内递增、不回收。刷新时先按完整ARAContentNote字段匹配并逐次消费重复事件；未匹配事件按唯一一对一、仅一个语义字段组变化匹配（start、pitch/frequency、note/signal duration、attack、volume）。所有候选计数先算完再分配，避免遍历顺序消除歧义。歧义或多组变化分配新ID，不任意迁移用户覆盖。无内容/错误清当前快照和IDs，next ID继续递增。

完整ARA夹具验证同音同起点不同duration拥有不同ID、无变化更新保留、同起点事件重排按完整内容保留、删掉一个重复起点音符后另一音符移动能唯一保留ID；额外歧义重复移动两个事件均得到不同新ID，PASS（m3-note-identity.log）。clang-format/git diff --check PASS。ID映射尚未进入archive，也未应用到managed工程，不能称编辑覆盖保留/重开身份恢复已完成。下一任务仍为私有ARA数据传递及实际TuneLab模型更新，并把匹配版本/来源快照映射纳入持久化。M3未完成、未提交推送，GUI仍SKIPPED。

## M3 托管 ARA 工程转换基础补记（2026-11-14）

新增 TuneLab/Hosting/CubaseAraScore.cs（全internal，无公共ABI变化）：私有tempo/source-note/region DTO，创建真实NativeProjectFile/MidiPartInfo/NoteInfo与TempoInfo。把source秒经region关系映到host quarter，再减去source zero的host quarter，生成source-local tick；BPM按region时间比例转换，使原生后续按region放置时保留源秒时长。输出显式 SourceZeroSongSeconds/Quarter 和平行NoteIds，不把ID塞进现有冻结NoteInfo。复制选定engine/singer标识，新源歌词为空，不假造歌词同步；尚未支持覆盖合并/拍号原点应用。

新增 CubaseAraScoreTests：使用真正 TempoSnapshot 换算生成的工程数据，验证跨速度变化的音符起止秒、非零song origin、两种region比例、重复同音起点但不同ID与duration、声源标识、重复ID及空region拒绝。3/3 PASS（m3-managed-ara-score.log）；首次编译缺MusicTheory namespace、随后测试IEnumerable未物化为IReadOnlyList，均已按日志修正。dotnet format whitespace 仅作用于新增两文件，git diff --check执行。

该转换函数尚未被原生ARA回调调用，不能称source→TuneLab实时同步已完成。当前生成新source工程，尚未把用户编辑/歌词/固定音素等覆盖合并，也未保存projection metadata进archive。下一最小任务：私有托管ABI接入、所选mod首次导入，再实现更新合并与归档元数据。M3未完成、未提交推送；GUI仍SKIPPED。

## M3 私有ABI 3与首次源导入补记（2026-11-14）

私有 managed ABI 从2升3（NativeEntry/GetAbiVersion 与 native managedAbiVersion），新增 BuildAraProjectState 和明确顺序布局的tempo/note/region输入结构；native静态断言16/32/32字节。入口按容量查询/复制返回既有TLCUBASE+CBOR项目，不新增公共SDK或引擎契约。NativeAra.cs可从可选模板取声源标识；当前首次导入调用不提供模板，用户在插件中选择声源。

BridgePlaybackRegion::importInitialScore 在已有非空state时保持编辑数据；否则从原生source notes/IDs、context tempo和region构建输入，调用真实CLR入口生成state。已有managed handle则经既有RestoreDocumentState接收，全部成功后提交native state；selectedDocument 在首次host选择时调用。该路径未启动GUI或加载SVS模型。

完整 verify-host.ps1 -AraProjectState out/cubase/m2/fu2_ning2_na4.state 退出0（m3-abi3-host-verification.log）：所有native目标按ABI3重建，两个实际插件validator各47/0，宿主ABI报告3，原托管目录及两bundle目录运行时/文档状态协议通过。ARA夹具新断言首次导入state非空、区别于空项目、CBOR校验成功、托管CopyDocumentState与native字节相同，重复选择不覆盖已导入state。原归档/回滚/身份/时间/代理连接测试继续PASS。

C++ clang-format和新增NativeAra.cs的dotnet format whitespace执行，git diff --check执行。当前原生→CLR首次source score导入已接入；source更新/tempo变化后的合并、手工歌词与固定音素覆盖保留、投影/身份元数据归档、拍号在实际工程中的原点显示、Bridge渲染/配对仍未完成。无真实GUI/合成端到端新证据；GUI/Cubase仍SKIPPED。M3未完成、未提交推送。下一最小任务为持久化导入baseline并以其合并更新，不能在每次host回调重建覆盖用户编辑。

## M3 ARA 三方合并器补记（2026-11-14）

核对NativeProjectFile和CubaseProjectState：通用native工程只带project/editor/export，暂无ARA元数据；CubaseDocumentContext SaveState也尚未携带ARA映射。未改公共NoteInfo，也未将内部ID写进传给引擎的Properties。

新增全internal CubaseAraMerge：输入导入baseline、当前编辑投影（ID0为手工新增音符）和新host投影。按source note ID匹配，Pos/Dur/Pitch仅在当前值仍等于baseline时更新host新值；当前歌词、pronunciation、固定音素双列表/BodyOffset、properties及part声源/曲线/效果保留。host删除移除对应导入事件；用户已删除的baseline事件不因重复host快照复活；新host ID添加、手工note保留。输出按Pos稳定排序并同步ID列表；输入通过真实CBOR深复制，避免改变其他mod或baseline；tempo使用新host数据。

新增CubaseAraMergeTests：同起点重复源音符、host删除/移动/延长/新增、保留歌词和固定音素与用户pitch覆盖、深复制隔离、用户删除/手工新增保留。与CubaseAraScoreTests合计5/5 PASS（m3-ara-merge.log）；首次测试编译单个params new(...)类型推导失败，已明确CubaseAraSourceNote类型修正。dotnet format whitespace只格式化新增两文件，git diff --check执行。

合并器尚未由NativeAra/原生回调调用；当前实时路径仍是首次导入，不宣称更新合并已交付。下一最小任务：在Cubase私有state容器中版本化保存baseline/当前ID映射/原点，再在CubaseDocumentContext跟踪实际note对象关联，接更新入口。保存恢复必须保留该映射，不能仅保存在内存并冒充重开通过。M3未完成、未提交推送；GUI仍SKIPPED。

## M3 v2 ARA 元数据与实际文档关联持久化（2026-11-14）

CubaseProjectState 保持无ARA时输出原v1；有CubaseAraMetadata时输出v2：CBOR两元素数组（原native project CBOR字节、私有JSON元数据字节）。DecodeEnvelope读v1/v2，Decode继续返回NativeProjectFile供既有渲染使用。原生project_state_stream同时接受1/2，未知版本测试改用3/uint.MaxValue，仍拒绝未知版本。公共NativeProjectFile/SDK未扩展、独立版格式未改变。

元数据Version1/MatchingVersion1，保存SourceId、v1 baseline state、baseline/current note ID数组、source-zero宿主秒/quarter。验证版本、非递归baseline、关联长度/唯一性、当前非零ID属于baseline、有限原点；baseline必须含source MIDI part。拒绝把含ARA关联的v2再嵌入baseline。

CubaseDocumentContext RestoreState读完整envelope，按恢复后真实INote对象绑定ID；SaveState按当前对象导出ID（新建音符0），删除不误配、undo原对象可恢复ID。NativeAra首次构建现在附baseline/ID/原点metadata；当前SourceId在原生调用中仍为空，原生source ID分配器恢复未接入，不能称完整重开后源更新已通过。

17/17相关托管测试PASS（m3-ara-metadata.log）：v1兼容、v2中文source ID/基线/原点往返、非法关联/未知匹配版本/嵌套baseline拒绝，实际CubaseDocumentContext歌词修改/手工增note/删除/undo/保存后新文档恢复保留映射。全verify-host退出0、双validator各47/0、ABI3/三个运行时目录PASS（m3-v2-state-host-verification.log）。实际ARA archive新增v2 metadata状态往返，清native state后restore、managed copy一致，PASS（m3-v2-ara-archive.log）。clang-format/dotnet format whitespace及git diff --check执行PASS。

下一最小任务：NativeAra根据template envelope baseline/current IDs调用三方合并；补原生source ID与分配器恢复，接source/tempo/region变化更新且保存来源信息。当前实时路径仍首次导入，不宣称实时更新合并完成。M3仍未完成、未提交推送；GUI/Cubase仍SKIPPED。

## M3 ARA 更新合并实际接入（2026-11-14）

CubaseAraMerge.UpdateState 从v2 envelope恢复baseline/current IDs，合并incoming projection，保存新的host baseline/current IDs和原点。没有关联的已有v1工程明确拒绝覆盖。NativeAra.BuildAraProjectState调用该方法；BridgePlaybackRegion改为synchronizeScore，对已有v2传入当前state作为模板，对v1保留；生成字节相同则不恢复文档。源码/tempo/region变化在现有didEndEditing刷新所选region时会走新路径，不再仅首次导入。

新增私有RestoreAraDocumentState（普通RestoreDocumentState保持原语义）：已有未提交编辑时返回Busy7，避免source更新覆盖正在拖动/输入的事务。私有ABI升4，两侧GetAbiVersion/managedAbiVersion同步，输入结构16/32/32保持。当前Busy后需后续选择/host编辑通知再触发，自动重试未实现，不能声称这一边界已经闭环。

18/18相关托管测试PASS（m3-update-merge.log）：持久化v2经过连续移动/变调更新保留歌词，更新baseline后重复输入字节不变。原生实际ARA source修改pitch/frequency后endEditing触发CLR合并，native字节变化且managed Copy一致；重复source内容通知state不变。全verify-host退出0、ABI4、双validator各47/0、三个运行时目录及原协议回归PASS（m3-source-update-host-verification.log）。未以无GUI测试宣称真实编辑窗口通过。

clang-format/dotnet format whitespace限定当前修改文件，git diff --check执行。下一最小任务：Busy更新自动重试与源SourceId/ID分配器归档恢复；再继续多region上下文、拍号应用、Bridge角色渲染/显式配对。SourceId生产元数据当前仍为空，不能以托管测试手填SourceId冒充真实source重开更新已通过。M3未完成、未提交推送；GUI/Cubase仍SKIPPED。

## M3 Busy自动重试与region销毁补记（2026-11-14）

EditorDocument 新增retryPendingHostUpdate，现有managed钢琴窗transport轮询所调用的onPositionRequested先复制并执行该函数，允许成功后自行清除。BridgePlaybackRegion::synchronizeScore读取当前真实managed CopyDocumentState再合并（仅已有v2；首次空native state不把managed空v1当作已有用户工程），RestoreAraDocumentState返回Busy时注册弱region生命周期+弱document回调。下一次UI/data线程轮询重新构建、重新合并最新编辑，不缓存待覆盖的旧结果；成功、非Busy失败或region失效后清注册。不增加后台/音频线程调用CLR。

ARA夹具在私有ABI边界注入一次Busy，验证原state不变且排队；调用真实native轮询入口后CLR更新完成、managed与native一致、注册清除。再制造Busy并真实destroy region，轮询安全取消且原文档不变。原ARA/身份/归档/代理/时间测试及view_protocol回归PASS（m3-busy-retry.log）。两插件重建、validator各47/0（m3-busy-retry-plugins.log）。真实窗口手势/计时器视觉流程仍SKIPPED；未冒称实际未提交拖动测试通过。clang-format/git diff --check PASS。M3未完成、未提交推送。

## M3 原生source身份快照编解码（2026-11-14）

新增 ara_source_identity.h：TLASRC01头、匹配算法版本、64位分配计数器、事件数量，每条记录显式小端序保存ID及完整ARAContentNote字段（含float frequency/volume与double时间），不序列化C++ struct padding。校验ID非零/唯一/不大于计数器、长度和事件值/顺序；拒绝未知版本、截断、尾随数据。ara_note_identity.h 新ID分配检测uint64耗尽，防止溢出产生保留ID0。

ARA夹具用IDs7/41、计数器42往返原始字段；恢复后移动事件保留41，新增事件43；所有截断、未知匹配版本、尾随字节和低于已有ID的计数器拒绝。编译/全原生ARA夹具PASS（m3-source-identity-codec.log），clang-format/git diff --check PASS。

目前仅完成source快照wire codec，尚未挂入真实ARA doStore/doRestore；不能称source重开身份恢复已交付。下一最小任务：版本化archive bundle区分source与mod记录，按SDK source筛选器保存与ID重映射恢复；恢复时以保存的原始事件和ID匹配host当前snapshot，保留host实时内容/几何，不用archive旧音符覆盖host。处理partial restore不修改未请求source及多mod共享source的身份一致性。生产SourceId元数据仍未填充。M3未完成、未提交推送；GUI/Cubase仍SKIPPED。

## M3 TLARA002 source/mod归档实际接入（2026-11-14）

新增ara_archive_bundle.h：TLARA002分别包含v1记录表格式的modification与source两块，明确长度；source payload为TLASRC01。兼容旧TLARA001及M0 marker。生产doStore分别按SDK getAudioModificationsToStore/getAudioSourcesToStore筛选；mod-only不额外保存source。doRestore完整解码后按getAudioSourceToRestoreStateWithID处理source PID重映射，预先用保存事件/IDs匹配host当前notes，计数器取保存值与当前值的max后继续分配。managed mod恢复全部成功后才提交source IDs/counter；不替换host当前notes、属性或几何。

实际ARA夹具在导入前种子IDs7/41、counter42；保存全archive后新建独立ARA文档source PID=reopened-source，host当前事件已移动并新增。source/mod PID同时重映射恢复，得到7/41/43、counter43，host新事件/移动位置和PID保留，mod v2编辑state恢复。mod-only恢复未修改source sentinel IDs/counter；无效mod CBOR未造成source部分提交。全量与partial source筛选、旧格式兼容及新bundle每一截断拒绝，全部PASS（m3-source-archive-bundle.log）。实际Bridge构建/validator47/0（m3-source-bundle-plugin-build.log），真实ARA SDK TestHost PropertyUpdates/ContentUpdates/ModificationCloning/Archiving/EditorView协议退出0（m3-source-bundle-sdk-host.log）。GUI仍SKIPPED。

source持久身份的权威目前在ARA bundle source记录及SDK对象关联；托管JSON的SourceId字段仍可为空，未声称该字段被生产填充。未增加另一私有ABI或冻结SDK成员。下一最小任务为恢复后新文档的后续source→managed更新回归，再继续多region/拍号应用和Bridge角色渲染/显式配对。M3未完成，修改未提交推送；clang-format/git diff --check PASS。

## M3 重复恢复身份漂移修复与新文档后续更新（2026-11-14）

新增真实ARA重复restore断言复现问题：archive后新增的host note首次恢复分配43，重复恢复又分配新ID。RED日志m3-source-repeat-restore-red.log；初次异常展开时测试夹具未清理region且未uninitializeARA，额外SDK断言掩盖原失败。已仅修fixture析构按region→mod/source→sequence/context清理、适配正在editing状态，并用FactoryScope保证异常时uninitialize，不禁用SDK断言。

生产新增restoreAraSourceIdentity：先匹配archive事件/ID；对于不属于archive ID集合、但current已有ID大于archive分配域上限的post-archive事件，保留其current ID；只提交实际结果的最高counter，避免临时分配造成重复restore增长。保留host current事件。真实新文档反复恢复保持7/41/43和counter43，GREEN（m3-source-repeat-restore-green.log）。

新文档继续创建真实ARA context/sequence/region，调用生产synchronizeScore并创建新managed文档；原host移动/新增进入v2工程，CopyDocumentState与native一致。之后host再次变调，source IDs仍7/41/43、managed状态继续变化。全原生ARA夹具及实际Bridge validator47/0通过（m3-post-reopen-update.log），clang-format/git diff --check PASS。该项是CLI新对象/新文档恢复，不冒称真实Cubase UI重开。

source归档/ID重映射/重开后继续更新现有非GUI证据已建立；不再把其列为未实现。M3下一最小任务：多region上下文与拍号实际应用、Bridge各角色音频输出、显式配对；M3仍未完成、未提交推送，GUI仍SKIPPED。

## M3 Bridge角色音频基础（2026-11-14）

优先查ARA_SDK CodeGraph与ARAInterface.h：playback renderer add/removePlaybackRegion仅在companion未激活/非render-state调用；model endEditing仍可与render并发，region几何/内容更新必须通过快照发布。kARAEditorRendererRole且无playback职责时必须透传input并叠加试听；playback职责则替换input。

修正实际BridgeProbe::process原先无条件32-bit清零：editor-only按bus/channel透传输入，缺输入置零并传播silenceFlags；playback或combined当前无cache混音时输出零。canProcessSampleSize现在支持float/double，避免64-bit访问32-bit union。未添加音频线程CLR调用、分配或锁。

真实GetPluginFactory创建组件绑定ARA角色，分别跑playback-only/editor-only/combined三种角色×float/double，activate/setup/process/cleanup；断言editor-only逐样本输入相等、空playback不泄漏input、无输入静音。原ARA归档/恢复/Busy/身份/更新夹具一起PASS，实际Bridge validator47/0（m3-render-role-audio.log）。首次编译角色initializer_list枚举与位或结果推导不一致，已用显式std::array<ARAPlugInInstanceRoleFlags,3>修正。

当前尚未实现assigned region的歌声cache混音与editor audition，不能称Bridge音频闭环完成。下一最小任务：复用RenderWorker准备各assigned region的source-local缓存，按region播放offset/crop混音；遵守inactive列表更改与并发model更新快照契约，offline等待使用非音频激活阶段。M3未完成、未提交推送，GUI仍SKIPPED。clang-format/git diff --check执行PASS。

## M3 区域播放准备：无副作用谱面快照（2026-11-14）

已将 BridgePlaybackRegion::projectState 从 synchronizeScore 中拆出：按当前 region 的时间映射和最新 managed 文档生成候选工程，不替换共享 modification 的 state，不清除未提交编辑的重试回调。原同步路径复用此快照，再执行原有 Busy 保护和文档提交。供后续 assigned-region 渲染缓存使用；尚未接入 Bridge 合成混音，不宣称播放闭环完成。

验证：前台构建 tunelab_ara_state_probe 并执行真实私有托管 ABI/ARA 协议夹具退出 0；新增断言验证投影不改共享文档、不清除或替换 pending retry。原归档、恢复、Busy、角色音频和连接代理回归继续通过。日志 m3-region-projection.log。clang-format 与 git diff --check 通过。未启动 GUI。

M3 仍在实施，未提交、未推送；下一最小任务是将区域快照接入非音频 RenderWorker，并按宿主 sample position、region 偏移/裁剪混音，随后覆盖活动更新与角色试听。公共 ABI 未变。人工验收保持 SKIPPED（用户决定，未执行，需人工验证）。

## M3 Bridge 区域歌声缓存与实际组件输出（2026-11-14）

新增 integrations/cubase/native/ara_playback.h，Bridge setActive 在非音频线程收集 assigned playback regions，调用 projectState，并为区域提交 RenderWorker；offline 激活等待结果并显式返回失败，realtime 异步准备。process 根据 projectTimeSamples 读取缓存，执行 source offset、半开 region crop、重叠求和、mono/stereo、float/double；不在 process 分配、锁、调用 CLR 或释放 worker。editor-only 继续转发输入，playback/combo 取代输入。停用后回收 worker。

真实本机 fu2_ning2_na4 无 GUI 验证通过：与单独 RenderWorker 输出逐 sample 比较区域偏移/裁剪/两区域求和，seek、mono/float 与 stereo/double；实际 Bridge factory 创建组件、通过 ARA playbackRendererInterface 分配区域、offline setActive 合成，32/64 位 process 有限且非零，区域前后全静音。Bridge validator 47/0。最终日志 m3-assigned-region-final.log，插件构建/validator 日志 m3-region-playback.log。最初编译缺 ProcessContext 完整定义，补 ivstprocesscontext.h 后通过。clang-format 已应用，git diff --check 通过。

真实声库测试使用 tunelab_ara_state_probe.exe <state> --synthesis；必须在进程启动前设置 TUNELAB_DATA_DIR 到 out/cubase/m2/data。未带 --synthesis 的原协议测试不要求本机声库；不分发模型。

明确剩余：当前仅激活时固定区域/工程快照，活动期间 host/editor 修改尚不更新播放缓存；editor renderer audition、显式配对、meter 工程应用仍待实施。factory 未声明 time stretch；当前不等长 modification/playback duration 会显式拒绝激活，正确变速合成仍需后续完成。新文件未 stage；M3全部 WIP 未提交推送，M4–M6未开始。真实 Cubase/GUI/实机仍 SKIPPED（用户决定，未执行，需人工验证）。下一步先完善区域工程的变速渲染与活动更新发布，不进入 M4。

## M3 区域变速合成（2026-11-14）

新增私有 ABI5 BuildAraRenderState，源工程快照仍用于编辑/归档；渲染副本通过 tempo BPM / playbackScale 将整个时间轴映射到播放时长，保留音符 pitch/歌词/声源和原编辑数据，不对歌声波形重采样。原生 source offset 同时乘 scale，按实际播放时间裁剪；factory 声明基础 Timestretch，未声明 reflecting-tempo 等其他 transformation。撤除原不等长区域拒绝，接入实际 Bridge 激活合成。SDK/Foundation ABI 未改，无需现有引擎新增 API。

验证：CubaseAraScoreTests 6/6，覆盖 0.5/1/2 倍时长、跨 tempo 边界起止秒、pitch/声源及输入不变；真实 fu2_ning2_na4 的 actual Bridge ARA assigned-region offline float/double × 三种时长（六次）均有 finite/nonzero 歌声、区域前后静音，原 source offset/overlap/seek/mono/cache 与完整状态夹具通过。日志 m3-render-scale-tests.log、m3-render-scale-synthesis.log，插件构建/validator m3-render-scale-native.log。并未进行听感或输出基频测量，真实 Cubase/GUI仍 SKIPPED。

私有 ABI 升级后需两个插件、所有原生工具和托管包一起部署，旧 ABI4 原生二进制不可混用。当前仍仅激活时快照；下一最小任务是活动期间 host/editor 更新的不可变播放快照发布，随后 audition、配对与 meter 应用。M3未完成未提交推送，目标ACTIVE。

ABI5完整 verify-host.ps1 已退出0，两个插件 validator 各47/0、所有native工具重建、协议夹具及三个部署目录的runtime/document验证通过；日志 m3-abi5-host-verification.log。git diff --check通过。

## M3 活动宿主更新与区域快照发布（2026-11-14）

AraPlayback 改为 immutable Plan + SC atomic active pointer/readers；控制线程 beginUpdate/add/publish，音频线程仅取固定 Plan。旧 Plan/RenderWorker 仅在读取计数为0时由控制线程回收，停用清除；scratch 仅在 inactive prepare 分配。ProbeDocumentController 的 didEndEditing 在刷新编辑选择后通知弱生命周期观察者，实际 Bridge 激活时重建/发布 assigned-region 工程、几何和合成缓存，销毁组件会撤销观察者。区域无可用工程或准备失败时发布空播放，不继续播放陈旧区域。

验证日志 m3-active-region-synthesis.log：真实本机 fu2_ning2_na4，实际 Bridge 的 offline active 状态中移动区域，旧位置静音、新位置有限非零，覆盖0.5/1/2倍时长×float/double共六组；原归档/恢复/Busy/身份/绑定及偏移裁剪重叠/seek回归通过。12次控制线程快照替换与音频线程并行处理14016块通过（finite样本/释放与取消未崩溃，不是30分钟性能结论）。插件 validator47/0，构建日志m3-active-region-build.log。最初MSVC拒绝lambda内捕获局部constexpr作array模板参数，已在测试数组指定512后通过。

剩余证据缺口：宿主音符/tempo变化经同一 didEndEditing 重建路径，当前实际组件音频断言直接覆盖的是区域移动；尚未对更新前后歌声的音高/发音作数值比较。插件内 settled-edit 回调还没有调度到宿主允许的线程，不能在CLR/UI回调直接遍历ARA对象，下一最小任务是正确接入此调度和未提交编辑保护。editor audition、显式配对、meter工程应用继续待实施。M3未完成，所有M3改动未提交/推送；目标ACTIVE，M4–M6未开始。真实Cubase/GUI/实机仍SKIPPED（用户决定，未执行，需人工验证）。

## M3 编辑完成后的区域合成刷新（2026-11-14）

源码确认 PluginUiRuntime.Initialize 在宿主STA/UI线程 SetupWithoutStarting，使用Cubase消息循环，未创建独立UI线程。EditorDocument新增去重、弱生命周期、多订阅者状态观察；仅状态确实改变后通知。Bridge为assigned modification订阅同一刷新回调，校验bind所在线程；正常settled-edit在宿主主线程重建不可变播放快照，宿主编辑事务期间合并到didEndEditing。未增加消息窗口、后台线程或独立版行为变化。错误线程事件记录诊断并拒绝访问ARA；现有UI本身已要求同一Dispatcher线程。

验证：actual Bridge活动合成测试通过私有BuildAraRenderState/RestoreDocumentState构造不同工程快照，并调用生产EditorDocument::onStateChanged边界，随后process样本相对旧输出确实变化（差值>1e-6），覆盖float/double × 0.5/1/2倍时长；这证明回调至合成输出路径，不等同真实GUI编辑。日志m3-editor-refresh-synthesis.log。多观察者、重复订阅/相同快照抑制、观察者销毁及清理在view_protocol_probe通过。完整verify-host退出0、双validator各47/0、三目录runtime/document回归通过：m3-editor-refresh-host.log。clang-format/git diff --check通过。

歌词/音符/引擎歌手提交沿同一settled-state通知，歌词发音变化尚未单独做音频比较；真实Cubase和GUI仍SKIPPED（用户决定，未执行，需人工验证）。下一最小任务为ARA编辑数据变更的宿主通知/dirty记录及editor renderer audition，然后显式配对/meter应用。M3仍IN PROGRESS，未提交/推送，目标ACTIVE，M4–M6未开始。

## M3 ARA 宿主编辑通知与合成完成通知（2026-11-14）

BridgeAudioModification 为独立文档注册弱生命周期观察者；用户settled-state变化在宿主主线程排队notifyAudioModificationContentChanged(everythingIsAffected)，由SDK合并并在宿主notifyModelUpdates轮询时发送。数据属于modification，不误报notifyDocumentDataChanged；host导入/正常归档恢复不触发此编辑通知。多个playback renderer不重复标记修改所属对象。

合成完成通过willNotifyModelUpdates中的弱观察者检查当前AraPlayback所有worker成功，按修订向assigned playback region排队samplesAreAffected通知一次，使宿主更新音频缓存；不从合成/音频线程调用ARA。每次重建重置完成通知标志。RenderWorker的lastResult成功写入调整到cache.publish之后，确保观察成功时样本已发布。

协议夹具已提供真实ARAModelUpdateControllerInterface，验证通知不抢跑宿主poll、对象hostRef/全范围/flags正确、重复相同快照和重复poll无重复通知、归档恢复不回声。actual Bridge的初次及编辑后合成完成会各发送一次区域通知，六组float/double×0.5/1/2时长真实声库验证通过；日志m3-completion-notify-synthesis.log。后续完整重建及发布顺序回归结果另记。clang-format与git diff --check通过。

M3继续IN PROGRESS，所有M3工作尚未提交/推送；下一最小任务为editor renderer audition，然后显式配对、meter工程应用与M3逐项验收。真实Cubase/GUI/实机全部SKIPPED（用户决定，未执行，需人工验证），CLI host回调证据不能冒充Cubase保存优化或缓存行为通过。目标ACTIVE，M4–M6未开始。

最终发布顺序回归全部退出0：完整verify-host（双validator各47/0、所有工具与三目录runtime）m3-notifications-host.log；真实worker最新代次/旧率拒绝peak0.302906通过m3-notifications-worker.log；真实ARA通知/活动更新/变速/并发回归通过m3-notifications-final-synthesis.log（本次并发25179块）。M3未完成未提交推送。


## M3 嵌入钢琴键试听输入（2026-11-14）

PianoRoll新增实例局部PreviewKey/StopPreview委托；未注入时保留独立版AudioEngine.PlayKeySample。CubasePianoView/PianoEditorSession为嵌入视图注入回调，鼠标释放与视图Dispose发停止，避免走全局独立版设备；没有改变独立版默认路径。私有ABI6 SetDocumentKeyPreviewCallback按document handle登记/移除原生callback，文档销毁清理登记；EditorDocument注册原生按下/松开异常边界并持有可选keyPreview消费者。

当前只接入输入，不声称ARA试听出声：Bridge editor renderer尚未绑定keyPreview消费者。下一步用assigned editor region/document映射注册消费者，复用已有SVS缓存预览；音频线程消费无锁命令，处理松键、切换声源、关闭视图/实例及editor-only转发/combined叠加。

view_protocol_probe新增按下pitch64、松开、解除消费者后不再回调；runtime_probe验证真实ABI注册/解除、零handle及已销毁handle拒绝。完整verify-host结果另补。clang-format及局部dotnet format whitespace执行；git diff --check通过，未整文件格式化旧UI。实现脚本最初因系统GBK默认读取UTF8中文文件中止，已显式UTF8续写核对；后续托管构建通过。未运行GUI/离屏验证。

M3保持IN PROGRESS，未提交推送，公共SDK/Foundation API未改；两插件/托管包需同步ABI6部署，旧ABI5不可混用。真实Cubase/GUI继续SKIPPED（用户决定，未执行，需人工验证）。

ABI6完整verify-host已退出0：双validator各47/0，所有native工具重建、view按下/释放边界及三目录真实runtime回调注册/解除/无效handle验证通过；日志m3-key-preview-host.log。未启动GUI。M3仍未完成未提交推送。


## M3 ARA editor renderer 歌声试听出声（2026-11-14）

新增ara_key_preview.h：复用MidiPreviewPlayer/MidiPreviewWorker，宿主线程lazy创建及更新声源工程，音频线程通过原子latest-key命令读取/推送预览事件，无CLR/锁/堆分配/worker回收；同noteId=0单音拖键，0命令松键。命令CAS更新允许setProcessing(false)停止与UI按键安全交接。prepare/clear仅inactive分配/释放。

BridgeEditorRenderer按assigned直接region或region sequence匹配modification文档，响应私有ABI6 key callback。只在匹配时取projectState、选择所属引擎/歌手，播放当前歌词的一秒缓存预览；每文档更改停止旧预览，移除region/sequence停止，旧文档观察者随当前文档切换失效。Bridge editor-only转发input后加试听，combined在正式playback后加试听，playback-only不响应；无输出处理松键，停处理及停用清理。modification销毁清除回调，避免EditorDocument被视图保留后引用已销毁ARA对象。

真实本机fu2_ning2_na4 CLI测试通过：actual Bridge editor-only float(直接region)/double(sequence)及combined double，验证idle输入/角色静音、歌声有限非零、释放立即停止且不复活、无输出release flush、setProcessing重启无残音、移除assignment停止及未分配文档不出声。原区域合成/变速/活动更新/通知/归档回归通过，日志m3-editor-audition-final.log；完整verify-host退出0、双validator各47/0、三目录runtime及协议夹具通过m3-editor-audition-host.log。日志PASS文案仍含editor-only，第三次调用实际assigned roles为playback|editor，按不同基线断言。未跑真实GUI，未做听感验证。

首次夹具knownRoles只声明editor，SDK按规则自动启用未知playback，导致idle输入被替换，报Idle audition altered input；已只修夹具knownRoles为全部已知角色，保留生产语义，重跑通过。无SDK/third_party修改，公共API未变。clang-format/git diff --check通过。

仍需显式配对/单正式输出方、meter工程原点、多region上下文最终覆盖及M3验收。预览是当前声源/歌词的一秒缓存，不是无限延音；VSTi内嵌键盘消费需后续UI阶段核对，当前此消费者覆盖ARA角色。M3保持IN PROGRESS未提交推送，目标ACTIVE，M4–M6未开始，GUI/实机继续SKIPPED（用户决定，未执行，需人工验证）。

## M3 显式配对跨包契约基础（2026-11-14）

实际两个VST3包目录经ManagedRuntime同时装载，实测managed入口不同，owner document在peer registry不可见（m3-pair-runtime-topology.log）。因此不能把单程序集static registry当进程共享，也不能交换各组件ALC中的TuneLab类型/本地文档handle。

新增CubasePairRegistry：AppDomain命名slot，仅保存BCL Dictionary<Guid,Tuple<byte,Guid>>，每端点新runtime GUID；显式Pair/Unpair，无按名称/加载顺序自动选择。配对模型以Bridge为logical owner及正式区域输出方；Instrument失去Bridge后PeerMissing且输出所有权仍false，须显式解绑或重绑，避免自动播旧score。禁止抢占活跃配对。控制线程锁不进入音频process。

私有ABI7（非SDK）新增RegisterPairEndpoint/PairEndpoints/GetPairEndpointState/UnpairEndpoint/ReleasePairEndpoint，固定16-byte Guid.TryWriteBytes顺序。实际两个插件包入口已验证：默认独立→显式配对→两边查询一致单owner→释放Bridge后Instrument失联且无输出权→跨上下文显式解绑。日志m3-pair-cross-bundle.log。

这仅是配对注册与所有权契约，尚未把端点绑定到实际native实例、UI显式操作、共享工程读写、归档恢复或process输出开关；不能据模型标志称双插件已不会重复出声。下一最小任务是端点RAII生命周期及native控制线程读取路由后发布atomic音频开关，再接文档state与UI/持久化。拍号原点、多region最终覆盖及M3验收仍待实施。

CubasePairRegistryTests初次3/3，覆盖独立注册、角色/抢占、失联/重绑、真实单独AssemblyLoadContext互通；新增四对隔离测试结果另记。完整构建一度在ara_state_probe的bridge_probe.obj遇LNK1163 COMDAT选择无效，保存失败日志后只删除并重编该out生成对象，目标链接通过（m3-pair-contract-rebuild.log），未改变生产编译选项/逻辑。私有ABI7两个插件包已重建，完整verify-host结果另补。clang-format和局部dotnet format已执行。

M3保持IN PROGRESS，所有M3修改未提交/推送；目标ACTIVE，M4–M6未开始，公共SDK/Foundation未改，实机/GUI继续SKIPPED（用户决定，未执行，需人工验证）。

配对契约最终验证：CubasePairRegistryTests 4/4（含四对隔离）m3-explicit-pair-registry.log；实际双包ABI7互通m3-pair-cross-bundle.log退出0；修复构建产物后完整verify-host退出0，双validator各47/0、三目录runtime/所有协议夹具通过m3-pair-contract-host-final.log；git diff --check通过。无剩余运行进程，M3未完成未提交推送。


## M3 实例配对端点与实际VSTi输出路由（2026-11-14）

新增native pair_endpoint.h，控制线程注册/解除并由RAII释放端点，私有ABI8 SetPairOutputCallback通过BCL-only共享delegate表把路由变化发布到native atomic<bool>。process只读取每块固定bool，不调用CLR/注册表、不取锁。Instrument ensureRuntime懒注册独立实例端点，terminate/析构释放；Bridge端点归属于具体BridgeAudioModification，pairEndpoint()懒注册，clone得到不同runtime token，随modification销毁释放。

Instrument私有组件/控制器消息新增TuneLab.Pairing.Request/State、Bind(bridge GUID16字节)、Unlink；这是自有实例UI契约，不是Cubase跨插件发现协议。配对以Bridge为正式输出，VSTi跳过工程cache，也清空/抑制轨道MIDI预览，避免同份MIDI另生成歌声。Bridge释放后VSTi保持静音；显式解绑后恢复原cache。独立模式原有MIDI、Bypass和工程播放保留。

验证：PairRegistryTests5/5（新增callback初始/配对/失联/解绑/解除注册）；完整verify-host退出0（m3-pair-route-host.log），双validator各47/0，modification clone端点独立及三目录runtime通过。后续Instrument MIDI门控修改单独重建、validator47/0（m3-pair-route-instrument-build.log）。实际加载Instrument插件+fu2_ning2_na4 processor验证独立有声→Bind静音→MIDI输入及连续64块静音→companion端点释放仍静音→Unlink逐样本恢复原cache通过；原Bypass、MIDI、state、seek/loop、双实例32/64-bit回归通过，日志m3-pair-route-final-processor.log。实际双包独立ALC中的原生atomic输出回调互通通过m3-pair-route-cross-bundle.log。桥端真实ARADocument/modification端点与clone验证在state probe；本次processor companion是实际托管endpoint，尚未把两个真实插件的完整UI/共享文档一起跑通。

真实processor首次两次在旧return48退出，诊断received=1 song=7(Busy) preview=0 elapsed=49604：此前编辑state更新的整曲任务仍执行，夹具误要求立即song=0。已仅让夹具限时30s轮询此真实任务，最终仍断言song=0/preview=0和耗时>0；未修改生产合成或放宽结果。最终真实运行通过。cold preview本次52ms，不是首次冷启动或听感结论。

当前文件：pair_endpoint.h新增；CubasePairRegistry.cs、NativePairing.cs、NativeEntry.cs、managed_abi.h/runtime.h、Instrument/ARA实现及processor/runtime/state probes、CMake/tests有M3 WIP；公共SDK/Foundation和third_party未改。clang-format、限定dotnet format、git diff --check通过。尚无配对UI，尚未共享工程/保存配对恢复，因此M3仍IN PROGRESS，全部未提交/推送。下一步继续共享文档与UI/持久化，不进入M4。真实Cubase/GUI/实机继续SKIPPED（用户决定，未执行，需人工验证），目标ACTIVE。


## M3 配对共享文档状态访问（2026-11-14）

私有ABI9新增SetPairDocumentAccess/CopyPairDocumentState/ApplyPairDocumentState。进程级registry仅保存BCL Func<Tuple<int,byte[]>>/Func<byte[],int>委托，以Bridge logical owner解析访问；读写都复制byte[]，不跨ALC传TuneLab对象。未配对或peer已销毁返回InvalidHandle；Busy等所有者结果原样传播，不fallback到旧本地文档。释放端点移除provider，控制线程同步调用并在调用期间保持注册生命周期。

BridgeAudioModification::pairEndpoint懒注册对应文档provider：读已提交native快照，写先验证版本化工程，存在managed handle时经RestoreAraDocumentState保护未提交编辑，成功后用原EditorDocument::onStateChanged通知宿主及缓存。provider限定宿主创建线程，线程错误返回WrongThread；clone文档及provider独立。native PairEndpoint提供copy/apply控制线程方法；音频process完全不调用此通道。

验证：registry6/6覆盖读写副本、Busy、解绑/失联无访问（m3-shared-document-tests.log）。实际ARA modification配对consumer读取基线、写入另一有效工程、错误CBOR拒绝、注入Busy拒绝且原状态不变、clone不变、还原/解绑拒绝通过；完整verify-host退出0，双validator各47/0、三目录runtime及既有协议回归通过m3-shared-document-host.log。实际双包不同ALC的read/write委托互通通过m3-shared-document-cross-bundle.log。非宿主线程拒绝和managed state API返回已写入字节的补验结果另记；无GUI时managed状态可能是等待视图激活的pending state，不宣称实际钢琴窗已切换。

当前仅完整状态访问接口，尚未将Instrument controller/view自动转向Bridge编辑上下文，尚无配对UI/保存恢复/共享撤销历史；不能用全状态镜像冒充共享编辑器完成。下一步优先接实际配对文档/视图路径及owner上下文，再UI和持久化。M3仍IN PROGRESS，全部未提交/推送；M4–M6未开始，公共SDK/Foundation未改，真实Cubase/GUI/实机继续SKIPPED（用户决定，未执行，需人工验证），目标ACTIVE。

共享文档补验已退出0：m3-shared-document-owner-final.log证明非宿主线程read返回4、managed state API读回完整写入字节；构建m3-shared-document-final-build.log通过。clang-format、限定dotnet format、git diff --check通过。M3未完成未提交推送，没有剩余运行进程。


## M3 配对共享视图契约与所有者保活（2026-11-14）

新增CubasePairViews.cs（registry partial）：BCL-only provider(create/destroy/resize/focus/release)及GUID视图lease保存在进程共享slot。Create按logical owner调用其视图工厂；lease缓存所有者回调与其本地view handle，不跨ALC传TuneLab类型。端点移除禁止新建视图，但已有lease仍可resize/focus/destroy；destroy失败保留lease供重试，成功后移除。

新增私有ABI10 SetPairViewProvider/CreatePairView/DestroyPairView/ResizePairView/FocusPairView，以及native pair_view_owner.h：每个registration和每次成功创建各持有一份引用，内部强持有同一EditorDocument；Bridge modification端点懒注册该provider。native create始终调用owner.runtime.createView(owner.handle)，同一文档/历史留在Bridge组件上下文。端点销毁释放registration引用，最后已有视图关闭后才销毁owner文档；失败关闭不减引用。所有操作限定所有者宿主/UI线程，不进入audio process。

验证：PairRegistryTests7/7，覆盖端点移除、两视图、resize/focus、失败关闭重试及最终释放（m3-shared-view-tests.log）。完整verify-host退出0，双validator各47/0及三目录runtime/原有协议回归通过m3-shared-view-host.log。实际两个插件包的ABI10调用链+native PairViewOwner，通过协议替身证明两个请求传相同document handle、GUID lease独立、resize/focus转发、endpoint/本地shared_ptr移除后仍保活、关闭失败返回4不释放、最后关闭释放文档一次；m3-shared-view-cross-bundle.log。替身没有创建HWND/初始化Avalonia，不是GUI证据。构建m3-shared-view-protocol-build.log，clang-format、局部dotnet format及git diff --check通过。

当前尚未把Instrument的实际EditorView切到此契约，故不宣称配对双窗口共享撤销已完成。下一最小任务为NativeEntry的pair-document proxy映射（使现有C++EditorView用标准view入口时转发租约）及controller的Pairing.State切换到owner上下文；不能用两份状态镜像替代共享历史。随后接显式配对UI、持久化、拍号原点与M3验收。M3仍IN PROGRESS，全部M3 WIP未提交/推送，M4–M6未开始，目标ACTIVE。真实Cubase/GUI/实机继续SKIPPED（用户决定，未执行，需人工验证）。

## 2026-11-14 M3 标准视图代理补记

- 私有ABI11新增CreatePairDocumentProxy；本地高位句柄只存端点/lease，不创建ProjectDocument、工程副本或撤销栈。标准CreateView/DestroyView/ResizeView/FocusView先分派proxy，再进入原本地Avalonia路径；线程检查由真实所有者负责。
- DestroyDocument在proxy仍有视图时返回1；关闭失败保留lease和proxy映射供重试，全部视图关闭后才能释放文档proxy。现有lease仍由Bridge所有者保活。
- 托管构建PASS；verify-host全部非GUI协议PASS，两个validator各47/0，私有ABI11及三个managed目录入口解析PASS，日志m3-view-proxy-host.log。跨实际两个插件包的标准视图代理PASS，覆盖同一owner handle、resize/focus、端点消失后保活、关闭4重试及proxy文档释放，日志m3-view-proxy-cross-bundle.log。owner回调为明确的无GUI替身，不是真实Avalonia/Cubase验证。
- scoped dotnet format whitespace/clang-format已执行，git diff --check PASS。原NU1902及既有警告保留。
- 当前M3仍未完成、未提交/推送。下一步controller根据Pairing.State绑定proxy、Bind/Unlink主动回传；随后配对UI/归档恢复等M3剩余项。真实GUI/Cubase仍SKIPPED（用户决定，未执行，需人工验证）。

## 2026-11-14 M3 controller 配对选择补记

- Instrument controller已接Pairing.State：显式配对时创建本地proxy并供IPlugView使用；状态含owner身份，重复相同通知不重建；独立状态恢复原文档，缺失对端状态选择空容器。打开视图时经本组件IConnectionPoint查询当前状态，失败不创建错误的独立视图。
- Instrument processor抽取sendPairingState，Bind/Unlink成功后主动回传endpoint/owner/status/output。视图绑定失败输出诊断并保留已成功的配对操作，重新打开视图可重试。尚未接对端删除的主动UI通知，当前该状态通过查询可达；不能称该生命周期已闭环。
- runtime_probe用实际EditorController及标准IPlugView，经真实三个managed加载上下文接Bridge所有者替身，验证同一owner handle、重复通知不重建、resize/focus、缺失对端切换的关闭失败保留及重试、解除配对（视图先detach，未启动独立GUI）。日志m3-controller-pair-cross-bundle.log。verify-host已纳入跨双包该协议回归。
- 本轮构建PASS，双validator各47/0；完整verify-host含实际Bridge archive/ConnectionProxy与ABI11/跨包controller协议PASS，日志m3-controller-pair-host.log。实际Instrument+fu2_ning2_na4测试PASS：Bind/Unlink主动回传（测试不再用额外Request掩盖）、配对工程/MIDI静音、对端释放保持静音、解除恢复缓存、32/64位歌声、Bypass/state/seek-loop/双实例。日志m3-controller-pair-processor.log。
- clang-format及git diff --check PASS。真实GUI/Cubase仍SKIPPED（用户决定，未执行，需人工验证）；协议替身不证明窗口/撤销交互已实机通过。
- 分支cubase_16_full_ara；M3仍未完成、未提交/推送，M4–M6未开始。下一最小项：配对UI、身份/路由状态恢复、对端删除主动通知，之后meter原点/多region及M3最终验收。保留全部原工作区修改。

## 2026-11-14 M3 对端移除通知补记

- 原生PairEndpoint复用既有输出callback发布statePending。通知注册线程立即回传Pairing.State；其他线程只更新atomic输出/待发标记，下一次控制线程消息/显式请求发送，不跨线程操作视图。失败保留pending，后续控制线程轮询可重试。未新增公共或私有C ABI，仍ABI11。
- Instrument在Pairing.Request/Bind/Unlink建立消息线程通知，在Transport.Request发送待发状态。通知线程绑定来自控制器消息，独立于此前setState/setActive初始化runtime的线程。Bind/Unlink原主动状态回传由该通知路径承担；对端移除同线程可直接进入现有controller缺失状态处理。
- 跨实际插件包测试PASS：初始/配对即时通知、foreign-thread移除不执行UI通知、回到注册线程后发送、失败重试及成功后合并、解除后正式输出恢复。日志m3-peer-notification-cross-bundle.log。
- 实际Instrument+fu2_ning2_na4测试移除后不再发送额外Request，主动获得status2/output0且保持静音PASS；解除后回传status0/output1并恢复缓存，32/64位合成/Bypass/state/seek-loop/双实例回归PASS。日志m3-peer-notification-processor.log。声库仅本机测试，不分发。
- 最终构建PASS，双validator各47/0；完整verify-host含ARA归档/ConnectionProxy/跨包通知及controller协议PASS，日志m3-peer-notification-host.log。clang-format及git diff --check PASS。
- GUI/Cubase仍SKIPPED（用户决定，未执行，需人工验证）。跨线程移除通知在下一次控制线程请求发送；无持续消息时依靠重新打开视图的状态查询，不宣称后台线程可立即关闭真实窗口。
- 当前分支cubase_16_full_ara；M3未完成、未提交/推送，下一最小项配对UI/身份与路由持久化，其后meter原点/多region上下文及M3最终验收；M4–M6未开始。

## 2026-11-14 M3 显式配对界面补记

- 新增内嵌PairSelector：Bridge显示只读、可选中复制的完整GUID配对码；Instrument提供粘贴/配对，两侧提供解除配对与状态。操作只调用明确端点，不按名称/加载顺序猜配。UI按钮用Dispatcher.Post在当前事件结束后操作，Dispose后排队动作不再执行，避免切换共享文档时同步销毁当前点击处理。
- 私有ABI12新增SetDocumentPairEndpoint（仅本地普通文档，验证真实端点/句柄）；NativeDocumentPairing保存每文档端点且Destroy清理。EditorDocument可先保存端点、initialize后配置；Instrument独立状态与Bridge选区各自接入。Session在既有33ms定时器更新配对状态，未配端点时隐藏，不改变独立版。
- Bridge选区现在调用modification.pairEndpoint注册/配置当前文档；此前生产选区未提供用户可获取的端点。clone仍使用独立runtime GUID。共享proxy视图使用Bridge所有者的配对栏，可从该栏解除关系。
- 配对码用于当前进程实例；现阶段未持久化关联意图/输出设置，不能宣称重开配对恢复完成。下一最小任务按计划的身份边界实现持久化；无可靠跨宿主工程身份时保留显式重新绑定，不以runtime GUID自动关联其他工程。
- 托管构建及完整verify-host PASS，ABI12/三个包目录、非法文档/缺失/过期端点配置、ARA archive/selection、跨包通知/controller协议通过；双validator各47/0。日志m3-pair-ui-managed.log、m3-pair-ui-host.log。
- actual Bridge+本机fu2_ning2_na4 CLI --synthesis PASS：并发Plan替换、裁剪/偏移/重叠/seek、mono/float/stereo/double、editor-only audition/释放/移除分配、六组区域变速及归档恢复，日志m3-pair-ui-ara-synthesis.log。声库不分发。
- scoped dotnet format whitespace及clang-format、git diff --check PASS。一次编辑脚本因系统默认GBK读取UTF8文件中断；核对仅先完成NativeEntry修改后，显式UTF8继续余下编辑，最终构建和回归通过。无丢失工作区修改。
- 真实GUI/Cubase、配对码复制粘贴、文本布局/中文/DPI/焦点/按钮与重建交互仍SKIPPED（用户决定，未执行，需人工验证），非GUI结果不冒充UI PASS。
- 分支cubase_16_full_ara，M3仍未完成、未提交/推送，M4–M6未开始。

## 2026-11-14 M3 配对意图持久化补记

- TLVSTP02/12字节尾部新增requiresPair标记，保留Bypass；兼容无尾部及TLVSTP01，非法版本/布尔/保留位/截断拒绝且不改输出参数。参数读取旧签名保留为忽略配对标记的内部重载。
- Instrument getState保存当前单一输出归属的配对意图；setState解析/验证后恢复意图。私有ABI13 RestorePairIntent接入BCL注册表：仅Instrument，恢复到PeerMissing/静音，使用新缺失标识而非runtime token；即使旧Bridge还在也先解除关系，不自动抢占。旧状态恢复独立模式；editor工程快照不改配对参数。
- 显式Pair/Unlink由controller的IComponentHandler2标记宿主dirty。恢复为缺失状态不据此标脏。缺失对端现在显示配对/解除栏并隐藏独立乐谱、声源及渲染状态，焦点转到配对码输入；这修复了旧空容器无法重新绑定的问题，实际UI交互仍SKIPPED。
- runtime协议夹具在通知PeerMissing前先detach假parent，避免启动真正恢复界面；关闭失败/重试仍通过实际IPlugView.removed覆盖，不能把替身当恢复界面验收。
- 注册表8/8 PASS（m3-pair-intent-tests.log）：恢复不会声明旧存活peer，不发布临时重复输出；复制意图各自缺失；只有显式Pair连接，旧关系不被复制抢占。
- 完整verify-host PASS、ABI13/双包/ARA归档/selection/controller/通知协议与格式v1/v2兼容检查通过；双validator各47/0。日志m3-pair-intent-host.log。
- 真实Instrument+fu2_ning2_na4 PASS：保存配对state、恢复保持静音并断开仍存活Bridge、显式重绑、复制配对state到第二实例保持静音；32/64位歌声/MIDI/Bypass/state/seek-loop/双实例原回归通过，日志m3-pair-intent-processor.log。
- scoped dotnet format whitespace/clang-format及git diff --check PASS。旧NU1902及既有警告未扩大处理。全部真实GUI/Cubase/重开工程操作SKIPPED（用户决定，未执行，需人工验证），没有证明真实Cubase保存重开S3+。
- 分支cubase_16_full_ara，M3仍未完成、未提交/推送，M4–M6未开始。下一最小任务：meter原点/多region上下文、M3逐项最终验收。按计划无可靠宿主工程身份时采用显式重绑，不新增跨工程自动配对。

## 2026-11-14 M3 拍号原点补记

- SDK源码确认ARA首个bar signature位置为bar 1，首拍号向前外推。此前只有native读取验证，managed投影未传拍号，且TimeSignatureManager首段Pos固定0，音源从小节中间开始会错置小节线。
- 私有ABI14新增AraMeterInput(quarter/numerator/denominator，16bytes)，region把context barSignatures与tempo一起送入BuildAraProjectState。CubaseAraScore验证边界，计算source零点所在小节的相位/宿主小节编号，后续变拍号位置正确映射。音符/合成时间原点不移动。
- 私有ARA metadata新增可选MeterOriginTick（旧数据默认0、验证finite），三方合并更新宿主拍号及origin同时保留歌词。渲染副本保留ARA envelope；普通渲染/文档恢复都应用偏移，preview独立单音score仍用自己的0原点。
- 最小内部模型改动：Project新增可选meterOriginTick构造参数，TimeSignatureManager内部SetOriginTick供该构造使用，CorrectStatusFrom首段位置采用该值；默认0使独立版保持原行为，公共SDK/Foundation ABI未改。
- 相关托管19/19 PASS（m3-meter-tests.log）：中途起源/负quarter、宿主bar编号、变拍号位置、歌词保留、渲染copy、独立版0原点；既有文档编辑/删除/undo/重开测试增加-480 tick保存恢复断言并通过。
- 完整verify-host PASS、ABI14/双包/ARA archive/selection与跨包协议通过，双validator各47/0（m3-meter-host.log）。actual Bridge+fu2_ning2_na4 CLI --synthesis区域缓存/变速/裁剪/重叠/seek/32-64位/mono-stereo/editor试听/归档回归PASS（m3-meter-ara-synthesis.log）。这些不证明GUI小节线已实机验证。
- scoped dotnet format whitespace、clang-format及git diff --check PASS。真实Cubase/GUI小节线及宿主交互仍SKIPPED（用户决定，未执行，需人工验证）。
- 分支cubase_16_full_ara，M3未完成、未提交/推送，M4–M6未开始。下一最小任务：同一modification多region/不同context的编辑与渲染投影逐项核对（含用户覆盖/手工音符/拍号），然后M3完整验收及阶段提交推送。不得把现有多region音频重叠夹具当所有上下文语义已通过。

## 2026-11-14 M3 多region源时间重投影补记

- RED实证：同一source的编辑在120→60 BPM region投影后，期望1.25源秒却变为2.5秒（m3-region-reprojection-red.log）。根因Merge替换全局tempo却保留用户覆盖/手工音符/表达数据的旧tick坐标。
- 最小修复集中CubaseAraMerge：使用现有TempoSnapshot，以source秒比较基线字段并投影至incoming context；未覆盖字段仍跟随宿主音符，覆盖/手工内容保持源时间。part锚点/裁剪端点、音高/连续及分段voice/effect参数锚点、vibrato范围一并重投影；Hz、音素秒偏移/时长和其他值保留。对相同tempo map保留原tick表示，避免重复导入抖动。所有操作基于深复制，不修改输入。未增加API/ABI，仍私有ABI14。
- 21/21相关托管测试PASS（m3-region-reprojection-tests.log），覆盖恒速与分段速度/不同song原点、手工音符、用户几何覆盖、表达锚点及颤音、A→B→A完整内容往返、真实UpdateState/encode/decode持久化往返、meterOrigin更新与输入不变；原宿主更新/歌词/删除/undo/拍号测试继续通过。测试最初对无钉死音素的BodyOffset作断言不符合序列化契约，已补合法BodyPhonemes后验证秒偏移保留，未改生产序列化行为。
- 完整verify-host PASS（m3-region-reprojection-host.log），双validator各47/0；actual Bridge+fu2_ning2_na4 CLI --synthesis区域缓存/变速/裁剪/重叠/seek/32-64位/mono-stereo/editor audition及归档回归PASS（m3-region-reprojection-ara-synthesis.log）。源时间/表达锚点的跨context证据来自真实managed合并路径测试，不能称已验证真实Cubase UI多选。
- scoped dotnet format whitespace与git diff --check PASS；冻结SDK/Foundation、third_party未改。
- 当前分支cubase_16_full_ara；M3仍未完成/未提交/未推送。下一最小任务：按计划M3及验收矩阵逐项检查当前生产路径和证据（尤其时间/transport在source-local编辑上下文中的映射、角色/选择/归档/配对），补齐实际缺口，再完成方案/主测试/legacy完整回归及M3独立提交推送。不要因现有夹具绿色直接宣称M3完成，也不得扩大到M4–M6。全部真实GUI/Cubase保持SKIPPED（用户决定，未执行，需人工验证）。

## 2026-11-14 M3 最终自主验收与待提交检查点

- 补齐Bridge source-local播放位置：HostTimeMailbox发布sample/rate；控制线程以weak region/clock转换source秒，私有ABI15 MapDocumentSourcePosition按当前工程或pending state转换quarter。独立Instrument原quarter回调不变；失效对象/context返回未知。
- 最终自动验收：解决方案build PASS/0 errors（m3-solution-build.log）；主843/843（m3-full-tests.log）；legacy13/13（m3-legacy-tests.log）；完整verify-host/ABI15/双validator各47/0（m3-source-playhead-host.log）；最终runtime跨包含分段pending时钟PASS（m3-source-playhead-clock.log）；actual Bridge本机SVS区域/试听/归档/transport PASS（m3-source-playhead-ara-synthesis.log）。
- docs/cubase-m3-validation.md逐项对应M3要求及证据。M3自主实施与非GUI验收完成；全部真实GUI/Cubase人工项SKIPPED，不称S3+实机通过。M4实施中，M5–M6未开始，完整目标ACTIVE。
- 旧exec会话37755已从工具注册表消失；本机完整日志确认解决方案、主测试及legacy命令均报告成功，没有盲目重启。新增分段时钟断言最终明确进入重新编译的probe并输出PASS。
- 当前分支cubase_16_full_ara，M3尚未提交/推送。下一操作：仅stage M3源码/测试/计划/验收记录，排除原.gitignore/.codegraph/out/三份参考文档与本机模型/日志；检查暂存diff与空白，独立提交M3并推送，核实同名远端SHA；更新实际Git状态后才进入M4。
- M4按原计划核对双容器工具、状态呈现/焦点和资源释放。M5原生歌词通道尚未实现；不能把手工歌词保存称为Cubase原生歌词同步。

## 2026-11-14 M3 检查点已推送确认

- M3功能提交32433224c724b404866ef113f2ad4b5ec7fbac7f，消息feat(cubase): complete M3 ARA documents and paired playback。git push --set-upstream origin cubase_16_full_ara成功；git ls-remote refs/heads/cubase_16_full_ara返回相同完整SHA，确认上游同名且提交已到远端。
- 提交63个M3源码/测试/计划/验收文件；git diff --cached --check通过，未纳入原.gitignore/.codegraph/out/三份参考文档。功能提交后工作区仅保留这些原有内容。本次文档补记不涉及产品代码。
- 当前任务状态：M0–M3自主实施和非GUI验收完成；M4–M6未开始，完整目标继续ACTIVE。下一最小实施是原计划M4双容器编辑工具与资源所有权核对。没有M3剩余自主实施；真实Cubase/GUI人工清单仍SKIPPED（用户决定，未执行，需人工验证），不作为受阻。
- 上文分日期过程记录中“进行中/未提交”是历史状态，当前以本检查点和首段摘要为准。
