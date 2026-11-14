# 当前任务交接

更新时间：2026-11-14。持续目标 ACTIVE。分支 cubase_16_full_ara。

## 阶段与检查点

- M0完成并已推送：f2f51b2a；契约/基线见 docs/cubase-m0-baseline.md。
- M1完成并已推送：1cb004e144e435b7f915f6110c877cb36c2d7873。
- M2自主实施与非GUI验收完成，本文件随M2检查点提交；当前尚待提交/推送确认，确认后才进入M3。
- M3–M6未开始。完整目标仍为路线B S3+，不得标记目标完成。
- 正式计划：docs/TuneLab_Cubase16_S3Plus_路线B实施计划.md。
- M2逐项验收与命令/证据：docs/cubase-m2-validation.md。

## 已完成实现

M1钢琴窗与CLR基础保持：每实例ProjectDocument、局部动作/撤销/剪贴板、共享controller/view文档所有权；包内hostfxr/nethost及Avalonia嵌入根，显式视图清理。

M2包括：

- PluginServices初始化现有/legacy引擎；组件ALC与插件共享所属宿主契约，compat和旧SDK进入私有托管包。插件内选择引擎/歌手，稳定ID、不可用提示、撤销/重做及保存恢复；公共SDK/Foundation ABI未改。
- CubaseProjectState以TLCUBASE/version1封装原NativeProjectFile CBOR。Native文档copy/restore/callback、controller连接重发、IMessage快照、processor验证/缓存/恢复闭环。恢复抑制回声，活动视图UI线程detach/rebind。
- 私有Project/AudioGraph不注册Standalone音轨；独立版默认行为保留。SnapshotRenderer、固定租约、请求取消、输出率转换、latest-only RenderWorker及原生AudioCache。音频process不调用CLR/分配/锁/回收缓存。
- MIDI16通道、SPSC、64槽held-note身份/力度/游标、独立preview worker/每pitch代次缓存、样本偏移NoteOff、float/double混音、无输出flush。原曲播放按宿主sample position，offline setActive非音频等待。
- VSTi3标准Bypass（ParamID0，可自动化、离散两态），逐样本队列处理；component state后附可选TLVSTP01/12字节参数尾部，旧状态默认false，坏版本/截断拒绝。editor快照消息只恢复工程，不清宿主参数。Bridge暂不暴露该参数。
- 非音频transport消息反馈工程/试听准备状态及耗时，私有callback接内嵌状态栏；HostTimeMailbox一致采集当前块sample/rate、音乐位置、tempo、拍号/bar及flags。尚未将其写成完整速度表。

## 已验证

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

完整tempo表/变速重合成、ARA对象/region/selection/archive/角色与显式配对归M3；M4双容器工具与生命周期，M5原生歌词协议，M6部署/许可/最终回归。Cubase原生歌词协议仍未证实，不得把自有消息或手动输入称为Key Editor歌词同步。Bridge processor/archive目前仍为M0探针。

## Git与工作区

M2文件已实现，等待本轮专属提交/推送确认。不得把用户预先存在的以下内容stage：.gitignore、.codegraph/、out/、三份未跟踪参考文档（Cubase16_SVS_接入静态分析.md、S3+_交付与接口清单分析.md、TuneLab_接入Cubase16_方案分析.md）。所有模型、测试生成工程/音频、日志均不分发。没有改动third_party源码或冻结公共API文件。

## 下一步顺序

1. 检查M2 diff/暂存清单，提交M2，推送同名分支并确认HEAD/上游一致。
2. 更新检查点推送状态。完成前不启动M3实现。
3. 用主仓及third_party/ARA_SDK各自CodeGraph核对ara_document.h/bridge_probe.cpp与当前ARA生命周期契约，按原计划开始M3文档/对象模型与归档。
4. 每阶段完成所需自动验证后更新记录、检查diff、独立提交推送确认，再进入下一阶段；不积压已完成阶段。

## 环境与约束

VS18/2026 MSVC19.51，WindowsSDK10.0.26100；.NET8，hostpack8.0.6/hostfxr8.0.26，Avalonia11.3.15。Cubase16.0.5.174与V7仅静态读取；不启动。third_party SDK索引/链接保留。

所有build/test/package前台PowerShell经Tee-Object写build.log并立即检查LASTEXITCODE；CMake --parallel1 -- /nodeReuse:false。正常Qt真实窗口才可作为GUI证据，禁止offscreen。现阶段GUI默认SKIPPED，不以headless截图冒充。

本机声库fu2_ning2_na4仅测试，不随插件分发；隔离数据out/cubase/m2/data、基线state为out/cubase/m2/fu2_ning2_na4.state。不改变用户设备/CUDA/引擎配置。新增TuneLab引擎API必须可选，现有引擎保留行为。

额度接口最近已用49%，未触发阈值；不得猜测额度。仅阈值触发时按用户规则委派GPT-6 Luna整理交接，正常开发不换模型。
