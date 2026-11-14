# M3 ARA Bridge 自主验收记录

日期：2026-11-14。分支：cubase_16_full_ara。对应路线 B 实施计划 M3。

M3实施与非GUI自动验收完成，待提交/推送Git检查点；M4–M6未开始。真实Cubase/V7、GUI、安装、硬件和听感统一为 **SKIPPED（用户决定，未执行，需人工验证）**。本记录不表示真实Cubase S3+验收通过。

## 要求与证据

| M3要求 | 实现与自动证据 |
|---|---|
| 工厂/角色 | ARA SDK factory、playback/editor renderer/editor view；仅声明基本Timestretch。双VST3 validator各47/0；实际组件editor-only输入转发、playback替换输入、combined加试听 |
| 对象/隔离 | source/context/modification/region分层；modification拥有独立EditorDocument，clone深复制并新建端点；实际SDK创建/更新/销毁、Busy未提交编辑及region销毁重试取消通过 |
| Tempo/拍号 | sync points转换seconds/quarters；source/playback offset、裁剪与duration ratio；私有MeterOriginTick及宿主bar编号。负位置、非零原点、分段速度与变拍号通过；独立版默认原点0 |
| 多region | 各region只读构建投影；宿主未覆盖字段跟随更新，用户覆盖/手工音符/表达锚点按源秒重投影。恒速及分段速度/不同原点、A→B→A与持久化往返、歌词/音素/删除保留通过 |
| Selection/视图 | 实际Bridge选区经模块令牌、本组件IConnectionPoint到controller/IPlugView；真实SDK ConnectionProxy覆盖两种连接顺序、重连/断连。视图创建回调为明确无GUI替身 |
| 播放位置 | 音频线程HostTimeMailbox；控制线程按所选region转换source秒，再用当前/待恢复文档tempo转换局部quarter。实际Bridge两种连接顺序、无效context及分段pending时钟通过 |
| Archive/恢复 | TLCUBASE v2关联baseline/current IDs及原点；TLARA002保存modification state、source notes/IDs/counter，兼容旧格式。部分归档/ID重映射/重复恢复/截断与坏版本拒绝/事务回滚通过 |
| 播放 | immutable Plan、控制线程回收、各region后台缓存、offline非音频等待。actual Bridge+真实本机声库覆盖float/double、mono/stereo、0.5/1/2倍率、裁剪/偏移/重叠/seek、移动/编辑及并发Plan替换 |
| 试听 | 当前声源/歌词的单音缓存经editor renderer输出；actual Bridge editor-only/combined、直接region/sequence、输入转发、松键/no-output flush/停处理/移除分配通过；不是无限实时延音 |
| 宿主通知 | 用户编辑通知modification；合成完成在SDK宿主轮询通知region样本失效，恢复不回声。实际对象/flags/合并和六组合成完成通知通过；先发布样本再报告成功 |
| 显式配对/路由 | BCL-only注册表跨真实插件ALC；明确Instrument/Bridge端点，Bridge唯一正式输出，Instrument工程/MIDI抑制。实际双包/native atomic/实际Instrument声库静音、删除侧仍静音、解除恢复通过 |
| 同一文档 | Bridge所有者提供视图lease；Instrument proxy转发标准操作，不建第二份可写工程/撤销栈。跨包实际controller/IPlugView+无GUI owner替身覆盖同owner、resize/focus、保活、关闭失败重试/释放约束 |
| 配对入口/恢复 | Bridge当前实例码、Instrument粘贴绑定，两侧解除；缺失时仅显示配对控件。TLVSTP02保存Bypass/requiresPair；恢复PeerMissing/静音，旧peer尚存亦需显式重绑，复制实例不抢占。注册表8/8及实际component state通过；按钮实窗交互SKIPPED |

## 最终自动结果

所有构建/测试采用前台PowerShell Tee-Object保存build.log并立即检查退出码，结果归档为以下本机日志。

| 检查 | 结果/日志 |
|---|---|
| dotnet build TuneLab.sln -c Debug | PASS，0 errors；m3-solution-build.log |
| dotnet test tests/TuneLab.Tests/TuneLab.Tests.csproj | PASS，843/843；m3-full-tests.log |
| dotnet test legacy/compat/TuneLab.Hosting.Compat.Legacy.Tests/TuneLab.Hosting.Compat.Legacy.Tests.csproj | PASS，13/13；m3-legacy-tests.log |
| pwsh integrations/cubase/verify-host.ps1 -AraProjectState out/cubase/m2/fu2_ning2_na4.state | PASS，ABI15、双validator各47/0、archive/selection/跨包等协议；m3-source-playhead-host.log |
| tunelab_runtime_probe.exe 实际双包 --peer | PASS，含最终新增分段pending时钟；m3-source-playhead-clock.log |
| tunelab_ara_state_probe.exe out/cubase/m2/fu2_ning2_na4.state --synthesis | PASS，actual Bridge区域/试听/归档/transport；m3-source-playhead-ara-synthesis.log |
| tunelab_processor_probe.exe 实际Instrument与本机声库 | PASS，配对state/重绑/复制及原M2回归；m3-pair-intent-processor.log |
| 相关时间/合并/文档测试 | PASS，21/21；m3-region-reprojection-tests.log |
| 配对注册表 | PASS，8/8；m3-pair-intent-tests.log |
| scoped格式化、git diff --check | PASS；保留既有NU1902和编译器警告 |

声库fu2_ning2_na4仅本机测试，模型/音频/测试工程不分发。冻结TuneLab.SDK/Foundation及third_party源码未改，已有引擎无需新增必填API。

## 契约与证据边界

- 首个ARA bar signature对应bar 1并向前外推；MeterOriginTick是私有可选metadata，旧数据默认0。内部Project/TimeSignatureManager可选偏移不改变独立版默认行为。
- note ID匹配带版本，歧义分配新身份；手工note关联为0；用户删除不因重复快照复活。未关联v1工程不被宿主导入强行替换，未提交编辑返回Busy后在控制线程重试。
- 配对只保存需要Bridge的输出归属意图，不保存运行时GUID或跨工程猜配。重开后复制Bridge当前选区新码，Instrument明确重绑或解除；符合缺少可靠宿主工程身份时的显式绑定边界。
- 音频线程只读取native缓存/atomic/mailbox，CLR/消息/视图/合成等待和回收在控制或工作线程。跨线程端点通知置待发标记，在下次控制线程请求发送。
- source-position回调仅持有weak region/clock，以当前或待恢复编辑工程速度表解释源秒。CLI测试不证明真实窗口中的播放线、布局或焦点。

## 跳过项及后续阶段

真实Cubase Extension添加/扫描、Lower Zone/VST3窗口、中文/IME/DPI/焦点、配对码复制粘贴/重绑、小节线/播放线、事件拖动/复制/Make Permanent、宿主保存重开、安装卸载/听感全部SKIPPED（用户决定，未执行，需人工验证）。不记PASS，也不阻塞阶段检查点。

M4继续双容器工具/状态呈现/资源生命周期，M5实现有证据的宿主歌词通道，M6部署/许可/交付。当前ARA notes只带音高/时间/时长；TuneLab内手工歌词可保存并在宿主更新时保留，尚不是Cubase Key Editor原生歌词同步。

Git检查点仅纳入M3源码、测试、计划和本记录；保留原.gitignore、.codegraph/、out/及三份未跟踪参考文档。
