# M2 VSTi3 自主验收记录

日期：2026-11-14。分支：cubase_16_full_ara。对应路线 B 实施计划 M2。功能检查点9b1ec32917c89f659ac4dbfd621fd56ebe5b8886已推送同名远端，HEAD/上游一致已确认。

本阶段自主实施与非 GUI 自动验收完成。真实 Cubase/V7、GUI、硬件 MIDI、试听及安装均为 **SKIPPED（用户决定，未执行，需人工验证）**。本记录不表示完整 S3+ 或 ARA Bridge 产品链路通过；M3–M6 仍按原顺序实施。

## 实现与证据

| M2 要求 | 当前实现 | 自动证据 |
|---|---|---|
| MIDI 与参数 | 16 通道输入、固定容量 SPSC、独立 held-note 身份/力度/游标、样本偏移 Note Off、float/double 输出；标准可自动化 Bypass | 真实 VST3 SDK 宿主发 MIDI67，指定歌手输出非静音；样本37松键及零样本无输出 flush；Bypass 样本128–255静音、其余与原缓存完全一致 |
| 现有歌声引擎 | 复用现有 voice 会话、旧引擎兼容层及用户安装的声库；不新增引擎必填 API | 本机 DiffSinger 1.0.0 + fu2_ning2_na4；legacy 13/13；组件 ALC 与插件共享所属宿主契约 |
| 引擎/歌手选择 | 插件内枚举与选择、目标 part 事务、不可用提示、撤销/重做 | 检查更换合成管线、另一实例管线不变、标识保存恢复；真实窗口操作 SKIPPED |
| 合成与缓存 | 私有 snapshot Project/AudioGraph、输出率转换、异步 latest-only worker、请求取消、代次与采样率拒绝、原生不可变缓存 | 真实 worker 新代次48k结果通过，旧率拒绝及 invalidate静音；原生缓存并发发布；预取消/重复释放拒绝 |
| 现场预览 | 所选声源与当前歌词的一秒音符预合成及尾音缓存；后台 worker 生成、音频线程混音 | 真实48k/32-bit与44.1k/64-bit歌声；夹具验证同音多ID/力度混音、前导静音游标、先松键不复活、忽略取消的旧结果不可读、关闭取消/join |
| 宿主播放与离线处理 | 按宿主 sample position 读整曲缓存；offline setActive 在非音频线程等待准备；停止/空状态静音 | 真实插件 kOffline 连续process；64/128/256/512样本块非单调seek/loop逐样本对照；双实例交替处理不串状态/声音 |
| 完整 component state | 既有 NativeProjectFile CBOR 的 TLCUBASE/version1 envelope；可选 TLVSTP01 参数尾部；controller/processor快照消息与恢复 | 字段往返、无效/截断/未知版本保留原状态；controller连接重发；恢复Bypass且编辑快照不清参数；文档提交/撤销/重做通知，恢复不回声 |
| 准备状态 | 非音频消息及私有callback反馈工程/试听未准备、合成中、就绪、取消、失败及最近准备耗时 | 文本7/7；真实插件准备完成与空状态重置消息；窗口布局/CJK SKIPPED |
| 当前块时间契约 | 明确请求并一致采集sample/rate、quarter-note、tempo、拍号、bar与有效标记 | 10万次并发快照；真实137.5 BPM、7/8、负bar及撤销有效标记检查 |

## 最终验证

- dotnet build TuneLab.sln -c Debug：PASS，m2-solution-build.log。
- 主测试：818/818 PASS，m2-full-tests.log。
- legacy compat：13/13 PASS，m2-legacy-tests.log。
- verify-host.ps1：PASS；双 VST3 validator 各47通过、0失败；ABI与源码/两个包目录解析、文档生命周期、完整state framing、视图协议、缓存、MIDI、时间、参数及预览夹具通过，m2-host-verification.log。
- verify-processor.ps1：真实插件/引擎 PASS，processor-verification.log。48k/32-bit整曲 peak0.302906；第二实例44.1k/64-bit peak0.304965。
- native concurrent_render_probe：整曲144000帧/peak0.302906，MIDI69预览96000帧/peak0.234372，两路48k、finite、nonzero，concurrent-render-verification.log。
- native render_worker_probe：真实最新代次发布、旧率拒绝及空状态失效 PASS，m2-render-worker-final.log。
- git diff --check：PASS。没有修改 TuneLab.SDK/Foundation 的已发布公共面。

数据目录与声库仅本机测试使用，位于 out/cubase/m2/data 与用户已安装目录；模型、缓存、生成工程、日志不随插件提交或分发。基线输入保存在 out/cubase/m2/fu2_ning2_na4.state。真实引擎输出与先前 CLI 基线的采样率、时长和振幅度量对应；本阶段没有以该度量对照声称逐样本音色等价。

原生预览缓存 miss 起音曾测得40–109 ms，环境已有引擎/磁盘缓存，不作为首次模型加载或固定低延迟承诺。一秒音符加尾音的缓存试听不等于无限延音或实时连续演唱。VST3 tuning数值目前保留但未应用；未暴露宿主参数的引擎专属曲线仍通过内嵌现有编辑器处理。

## 回归中修正的问题

全量测试最初因新增渲染测试使用基础 SynchronizationContext 而崩溃：Project 构造结束的合并通知会排队时基重建，基础 Post 把它送到没有数据上下文的线程池。最小修复仅在测试夹具使用现有 PumpableSynchronizationContext，并显式 DrainAll 覆盖该构造回调。原始全量命令重跑818/818通过。没有放松生产数据线程断言；临时堆栈记录代码已清除。

## 证据边界与后续阶段

- 真实 SDK 宿主加载的是实际插件并调用实际引擎；确定性预览夹具使用假渲染回调，两类证据分别记录，均不代替 Cubase 实机。
- 完整速度表、变速重合成、ARA对象/region/selection/archive/角色输出与显式配对归 M3；当前块信息没有被当作完整速度表覆盖工程。
- 双容器完整工具、IME/DPI及释放工作归 M4。现有真实窗口验收继续 SKIPPED。
- Cubase Key Editor 原生歌词协议仍未证实，归 M5；没有以自有消息或手工导入冒充宿主歌词同步。
- 发布部署、许可、完整长期性能与最终回归归 M6。安装和真实宿主验收继续 SKIPPED，保留验收清单。
- 预先存在的 Cbor NU1902 与编译器警告仍记录，未做无关依赖升级。
