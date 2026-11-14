# M5 宿主歌词通道实施与证据

## 阶段最终审计（2026-11-15）

M5 自主实施与非 GUI 验收完成；阶段提交420c11aea36b52d84c26609853942fbce744f58f已推送origin/cubase_16_full_ara，ls-remote确认一致。以下早期“尚未接入/下一步”段落为实施历史，以本表为当前状态。M6 随后进入交付审计。真实 Cubase S3+ 仍未实机验证。

| 原定验收项 | 结果与证据 |
|---|---|
| 有依据的宿主文本入口与标明来源的夹具 | PASS：标准 VST3 kTextTypeID/NoteExpressionTextEvent；队列与实际插件 SDK 夹具；不宣称 Cubase 已供给 |
| runtime noteId 到持久音符关联、修订失效 | PASS：NoteOn 起点/pitch → baseline stable ID；歧义拒绝；seek、状态恢复、配对与来源换版测试；m5-instrument-lyrics-final.log、m5-source-only-archive-green.log |
| 复制、删除、撤销及重开 | PASS：复制不继承来源 ID、删除撤销恢复关联、宿主复制/删除/撤销快照与重开；m5-lyric-transactions-tests.log，37/37 |
| 文本、空值、延音与语言边界 | PASS：Unicode/CJK/日文/英文原样传递，空文本清除，延音“-”保留给现有引擎解释；不伪造语言或音素事件 |
| 覆盖冲突及结果反馈 | PASS：保留本地词/发音/固定音素，baseline 推进、重复无写入；状态与配对实例隔离；GUI 视觉 SKIPPED |
| 歌词重合成及未修改范围缓存 | PASS：实际 Instrument→Bridge→fu2_ning2_na4 非零有限音频与变化；未改乐句对象/提交版本/样本/波形缓存保留，冷暖渲染最大误差0；m5-real-cache-retention.log |
| 版本拒绝、克隆、部分归档与来源身份恢复 | PASS：错误版本不破坏当前文档；带歌词实际 ARA clone/partial restore 及 managed baseline；source-only RED→GREEN并接受新ID99；m5-source-only-archive-green.log |
| 公共 ABI、已有引擎及独立版回归 | PASS：SDK/Foundation 公共表未修改，无新增引擎必需 API；私有 ABI21；主测试890/890、legacy13/13 |
| 最终构建及协议检查 | PASS：m5-solution-build.log，0错误；m5-abi21-host-verification.log退出0，双validator各47/0；git diff --check通过 |
| Cubase Key Editor 实际供给、实机、GUI、试听 | SKIPPED（用户决定，未执行，需人工验证），不作为阶段检查点阻塞，不记为 PASS |

本阶段未增加模型分发；fu2_ning2_na4 只用于本机测试。完整目标保持 ACTIVE。M6 负责原计划中的部署、许可、用户材料和发布候选检查。


日期：2026-11-15。状态：IN PROGRESS，未完成阶段验收、未提交推送。M4 功能检查点 78c7f251 与文档补记 f3c38723 已推送并确认。

## 最新状态：标准事件已接入（2026-11-15）

NoteTextIngress 已接入实际 InstrumentProbe.process：音频线程固定分配绑定 runtime noteId 到 Note On 的宿主起点/pitch，把关联附到已复制文本；同采样点文本先于Note On可解析，跨块文本沿用原始起点。seek/loop不连续、播放状态改变、文档修订、Note Off使关联失效；live键盘与重复活动ID不写工程。文档替换及配对目标/状态变化提高epoch，队列旧事件不进入新文档。无匹配不猜测稳定ID。

处理器仅捕获；33ms SDK Windows主线程Timer发送插件内部Lyrics.Request，控制线程排空后经Lyrics.Event调用控制器和ABI17，音频回调不调用CLR/消息/锁。丢失时丢弃不完整前缀并记录，失败保留待重试项。同步歌词产生的工程快照回声不错误作废同批后续歌词。控制器通过INoteExpressionController只声明bus0的16通道kTextTypeID，不声明未定义phoneme协议，不用数值转换伪造文本。

最新证据：m5-text-association-probe.log包含运行时关联和既有10万次SPSC；m5-instrument-lyrics-final.log在实际Instrument二进制中验证process→复制→主线程消息→controller→CLR状态，两个音符/不同runtime与source ID/宿主缓冲区修改/空排空、真实无窗口WM_TIMER消费、文档替换和seek拒绝。输入是SDK夹具，不是Cubase采集。m5-ingress-host-verification.log完整verify-host退出0、ABI17、双validator各47/0、所有现有非GUI协议/双包转发通过；之后最新epoch保护的目标构建/实际插件探针也退出0（m5-ingress-final-build.log）。

首次关联测试发现Note Off后错误回看同块更早Note On（退出21），已限制同偏移补查为后续事件并通过原断言；控制器初次构建缺少标准头文件已修正。未放宽测试。新增探针已接verify-host的AraProjectState路径，dotnet构建限制为单节点/不复用。

下一步：Bridge播放计划安全复用渲染worker、真实未修改范围缓存验证，以及原计划M5剩余事务审计。下文早期“尚未接入”记录是实施历史；本节及后续补记为最新状态。Cubase原生Key Editor是否提供此标准事件仍SKIPPED（用户决定，未执行，需人工验证），不作为阻塞，不冒称S3+通过。

### 持久渲染会话基础与 RenderWorker 接入，ABI20（2026-11-15）

原快照渲染每次创建并销毁Project，导致既有引擎会话缓存无法跨修订保留。新增CubaseRenderSession，固定在创建它的数据线程，持有私有Project、DataDocument和泵。结构签名沿用完整序列化器，忽略且仅忽略音符Lyric/Pronunciation；这两者变化通过现有属性setter/Commit传给引擎，保留工程、音符和引擎管线。失效范围仍由引擎决定，不增加必需SDK接口。其余结构/时基/引擎配置变化重建；改变带固定音素音符的歌词时也重建，避免setter清除导入音素。worker不保留撤销历史。

原CubaseSnapshotRenderer的单次Render/RenderPreview入口保留，内部用一次性会话；取消/失败清理半应用工程，并在原数据线程泵完在途任务。新增私有CreateRenderSession/DestroyRenderSession/RenderSessionSnapshot，由原生RenderWorker在线程内创建并复用、同线程释放，旧RenderSnapshot ABI入口仍可使用。公开SDK和独立版引擎必需接口不变。

m5-retained-render-session-tests.log：新会话/既有快照/预览10/10 PASS。覆盖歌词修订保持实际工程与音符对象、结果与冷渲染一致（内建空引擎夹具，不能证明真实音频缓存命中）、结构变化重建、固定音素保持、预取消及坏状态清理。首次测试编译缺少Data扩展方法using已修正，未改变断言。m5-session-abi-host.log完整verify-host退出0、ABI20、双validator各47/0。m5-render-session-protocol.log验证实际CLR句柄线程归属、错误线程释放拒绝、正常/重复释放、失效句柄输出清零。

m5-retained-session-real-svs.log：实际Instrument在新RenderWorker→持久会话路径的fu2_ning2_na4歌词事件重合成退出0，旧缓存立即静音、新generation有限非零，前后平均绝对变化0.0337584。此例修改单音符，证明新路径输出正确，不证明未修改片段被复用。

重要未完成：AraPlayback.add目前仍为新播放计划创建新RenderWorker，因此Bridge跨计划复用尚未接通。下一步须让旧计划继续以其固定generation读取，避免复用worker后旧计划读到新修订的音频，再落实对应worker/会话生命周期与真实缓存保留证据。不能把本轮基础实施称为完整增量合成已交付；M5仍进行中、未提交推送。

### 实际配对音频与来源修订保护，ABI19（2026-11-15）

新增paired_instrument_fixture.h及ARA探针--paired-lyrics/--paired-lyrics-revision模式。探针拥有实际Bridge工厂/SDK文档，动态加载实际Instrument DLL和controller；标准文本进入Instrument.process，生产消息/配对代理调用所属BridgeAudioModification，再进入实际Bridge playback renderer和本机fu2_ning2_na4。两个modification只配对其一；歌词修改更新所属状态、稳定source ID11及状态行数据，未配对clone保持不变，ARA modification与region完成通知正常，Bridge前后实际音频有限非零，平均绝对变化0.0337584。证据m5-paired-real-lyrics.log，退出0。没有创建GUI或试听，输入仍是SDK夹具。

来源换版夹具先保留runtime ID502的歌词，再把source ID11从原位置移开、在原位置创建同音高source ID12（显式身份只用于验证适配器，不冒充宿主身份匹配算法）。修复前旧排队文本错误写入新音符，m5-paired-source-revision-red.log退出1且明确断言失败。原因是输入epoch只覆盖整状态重载/配对目标，没有来源baseline几何/身份修订。

ABI19新增NotifyPairSourceChange：比较已验证状态的host baseline IDs、位置/时长/pitch、tempo/meter/来源原点，忽略歌词/发音和本地几何覆盖；有实质变化才经既有端点通知通道发布修订，不改变输出所有权。原生PairEndpoint原子通知计数加入音频捕获/控制消费的epoch；ARA synchronizeScore、共享工程应用及成功归档modification恢复均发布差异。普通歌词变化不会清掉同批后续事件；新建托管handle的初始载入也不误作新的host变化。

原失败夹具转GREEN：旧文本不改变换版状态，新的Note On/runtime ID503可正确更新source ID12，移动后的source ID11保留“你”。m5-paired-source-revision-green.log退出0，未关闭歌词或放宽断言。相关52/52通过（m5-source-revision-tests.log）；完整verify-host退出0、ABI19、双validator各47/0、现有归档/rollback/配对/消息回归通过（m5-source-revision-host.log）。本轮原生/托管构建记录m5-source-revision-native-build.log、m5-source-revision-managed-build.log。

重跑：使用out/cubase/m5/fu2_ning2_na4-lyrics.state作为ARA探针首参数，第二参数--paired-lyrics-revision，数据目录仍out/cubase/m2/data；前台Tee-Object和退出码规则照常。此测试不纳入不具备私有声库的默认CI。普通--paired-lyrics也可仅重跑配对音频路径。

仍未完成按音符/片段范围复用合成结果的增量渲染；M5剩余修改/删除/复制/撤销、版本拒绝与归档关联组合按原验收逐项审计。来源换版夹具覆盖上述明确场景，不将其扩张为所有可能宿主事务都已验证。M5未完成、未提交推送，M6未开始。

### ABI18 歌词结果与界面状态（2026-11-15）

CubaseLyricStatus区分等待、无变化、应用、用户冲突、无唯一关联、溢出、编辑Busy重试、失败重试及格式/长度拒绝。现有插件状态行经原33ms轮询显示歌词结果；不把未应用记为成功。队列溢出、非法输入及缺失NoteOn关联经主线程私有消息记录；状态不修改工程数据、撤销历史或引擎API。新私有ReportDocumentLyricStatus/GetDocumentLyricStatus入口由ABI18严格匹配。

配对双方通过BCL-only lyric-status.v1共享逻辑owner的状态，其他实例隔离，端点移除清理共享状态。仍存活的文档在端点过期后保存本地诊断，不能因诊断失败拒绝音乐数据更新；状态在首次绑定端点时迁移。重复宿主文本若当前歌词仍不同，报告冲突但不修改快照；用户把歌词改为宿主值后，重复事件恢复“无变化”，不会永久卡在冲突提示。

m5-lyric-status-tests.log：相关38/38 PASS。m5-lyric-status-host-verification.log：完整verify-host退出0、ABI18、双validator各47/0、实际双包跨ALC状态共享/失联后无旧状态、原生诊断不改变音乐快照/回调次数均通过。首次该回归在旧端点文档处退出34，已定位并修复诊断对有效歌词写入的影响，原断言保留。真实界面文字/布局/交互继续SKIPPED，不以纯文案测试冒充GUI验收。

另有实际Instrument回归复现本地编辑快照使待处理歌词丢失（m5-editor-echo-red.log，退出90）。已区分宿主完整restore与本地editor提交：只有宿主restore和配对目标变化作废输入epoch，本地提交保留等待当前编辑结束的歌词，随后由baseline合并保护用户覆盖。原夹具转GREEN且整状态替换/seek拒绝仍通过（m5-editor-echo-green.log、m5-editor-echo-fix-build.log）；不是通过删除旧断言解决。完整宿主回归在此修复前通过，修复后针对实际Instrument路径验证。

剩余关联审计须覆盖配对Bridge的source baseline内容版本更换时已排队文本的处理；当前证据覆盖整宿主状态替换/seek/配对目标切换，不把它扩大为所有ARA内容更新场景已通过。未完成M5检查点、未提交推送，M6未开始。

### 真实本机 SVS 歌词重合成（2026-11-15）

processor_probe 新增 --lyrics-synthesis，输入必须是带来源关联及已选择声库的状态。本轮只使用用户指定的fu2_ning2_na4：从既有M2状态提取同一SoundSource，生成单音符source ID11、0.5秒起/1秒长/C4、宿主baseline和当前歌词均为“啊”的本地状态。实际Instrument组件/controller加载该状态，48000Hz/512块离线准备并读取原歌声；在暂停处理时以SDK Note On/runtime ID501和标准文本“你”进入process，控制线程经生产消费者修改状态并调度渲染。

结果：m5-real-lyrics-synthesis.log退出0。事件后保存状态改变；旧generation缓存立即静音；离线激活等待当前generation完成；随后2秒范围的实际process输出非零且全部有限，前后波形平均绝对变化0.0337584。原音符载荷/文本路径仍是SDK构造，不是Cubase录制。波形差异仅作输出变化证据，不证明听感发音；试听始终SKIPPED。没有把渲染等待移到process，也没有分发模型。

本地临时夹具工具位于out/cubase/m5/fixture-writer，借用测试程序集的内部可见性，仅编码状态，不属于产品或分发包。源状态out/cubase/m2/fu2_ning2_na4.state，生成状态out/cubase/m5/fu2_ning2_na4-lyrics.state；TUNELAB_DATA_DIR使用out/cubase/m2/data。夹具编译/生成日志m5-lyrics-fixture-build.log、m5-lyrics-fixture.log；原生目标构建m5-lyrics-synthesis-build.log及限定格式/失败清理后的m5-lyrics-synthesis-final-build.log。首次临时项目相对引用层数错误已修正；未修改产品API来制造测试数据。

重跑命令：设置上述TUNELAB_DATA_DIR后，运行out/cubase/m0/native/Debug/tunelab_processor_probe.exe，依次传入实际Instrument的Contents/x86_64-win/TuneLabInstrumentProbe.vst3、上述关联状态、--lyrics-synthesis；前台Tee-Object写build.log并检查退出码。该模式不在默认verify-host中强制要求本机私有模型。此证据是完整source快照重合成，尚不能称为按音符范围的增量渲染已验收。

## 协议依据

- 本地 VST3 SDK 的 ivstnoteexpression.h 定义 kTextTypeID、NoteExpressionTextEvent，以及 INoteExpressionController 的声明方法。事件包含运行时 noteId、UTF-16 指针和明确的字符单元长度。
- noteId 是一次播放中的关联标识，不能直接当作跨工程的 ARA/source 稳定 ID。
- kPhonemeTypeID 的说明仍为 TODO；本实现不将其或私有类型解释为歌词。
- 本地 ARA SDK 没有公开 kARAContentTypeLyrics。Cubase 内部的 ARAContentLyricsEntries 名称不构成第三方调用契约。

以上基于 M0 固定的本地 SDK 版本和已读取头文件，不来自 Cubase/VOCALOID 实际事件采集。Cubase Key Editor 是否通过该标准文本事件供应歌词、以及完整编辑事务供给，仍未证实。

## 当前实施

新增 note_text_input.h：单音频生产者/单非音频消费者的有界队列，固定分配槽位。接收标准文本事件时，在回调有效期内复制 UTF-16，保存 noteId、sampleOffset、quarter、单调帧计数和文档修订号；不保留宿主指针，不调用 CLR、锁、分配或唤醒系统调用。

默认上限是 256 个 UTF-16 单元、64 个槽位（可排队63项）。超长文本整项拒绝，不截断；验证代理对、内嵌空字符、指针/长度、关联 ID、偏移和位置。空文本作为清空值保留。满队列单独统计 lost，消费者可丢弃不完整前缀后重新协调关联，不能把丢失视为成功应用。

该模块**尚未接入 InstrumentProbe.process 或文档更新消费者**；也尚未向宿主声明插件支持文本类型，不改变现有 MIDI/歌声行为。它是 M5 的接收基础，不是已打通的歌词同步。

## 已验证

新增 tunelab_note_text_input_probe，数据来源为**按公开 SDK 构造的合成事件夹具**，不是宿主录制样本，不创建 GUI。

| 检查 | 结果 |
|---|---|
| 宿主缓冲区变更后，队列中文/补充平面 UTF-16 不变 | PASS |
| noteId、偏移、quarter、帧计数、修订号和独立终止符 | PASS |
| offset-zero 空文本 flush | PASS |
| 无关联 ID、空指针、超长、破损代理对、内嵌空字符、非法偏移/位置拒绝 | PASS |
| 不将 phoneme 类型当歌词接收 | PASS |
| 队列满时统计丢失、消费者丢弃待处理前缀 | PASS |
| 100000 次 SPSC 传递，载荷与顺序保持 | PASS |

Windows 原生构建退出0：m5-text-input-build.log。探针退出0：m5-text-input-probe.log。命令为 cmake --build out/cubase/m0/native --config Debug --target tunelab_note_text_input_probe --parallel 1 -- /nodeReuse:false，然后运行对应 Debug exe；均经前台 PowerShell Tee-Object 保存 build.log 并检查退出码。clang-format 已用于新增文件，git diff --check 已执行。

## 文档侧歌词事务（2026-11-15）

新增内部 CubaseAraLyrics.UpdateState：调用方必须先解析成持久 source note ID，并提供匹配的 SourceId；不接受将 VST3 runtime noteId 直接用作关联的约定。状态操作支持原样 Unicode、空文本清除和延音标记，不推断未定义的语言/音素编码。

以 ARA 既有 baseline 保存宿主歌词快照；当前歌词、发音或固定音素存在用户覆盖时保留当前值并返回冲突 ID，同时推进宿主 baseline。重复载荷不重复更改；已删除音符不复活，未知/重复 ID 或错误来源拒绝且输入状态不变。ARA 纯音符刷新保留已有宿主歌词 baseline，并克隆传入投影以避免修改调用方数据。不修改冻结公共 API，也不新增引擎必需接口或工程 envelope 版本。

m5-lyrics-state-tests.log：新增歌词状态与现有 ARA merge/project-state 回归共18/18 PASS，Windows dotnet test 前台日志、退出0。覆盖连续更新/保存恢复/ARA刷新、Unicode/空文本/延音、用户歌词/发音/音素覆盖、删除、重复事件、错误来源和未知/重复ID。当前该事务尚未接入原生事件消费者、活动文档和重合成；这些测试不证明宿主歌词接通。

## 后续实施顺序

### 私有 ABI17 与配对文档入口（2026-11-15）

新增 ApplyDocumentLyric：有界UTF-16校验、quarter/pitch定位、持久source ID及明确disposition输出（未变/应用/本地冲突/无唯一关联）。活动文档调用已验证的ApplyHostLyrics；延迟载入状态更新pending快照并通知已有native状态回调。无匹配不记为已应用；非法UTF-16、嵌入NUL、错误句柄和Busy有明确返回，不进入音频回调。

配对文档代理通过BCL-only document-access.v2注册表转发到所属BridgeAudioModification的独立歌词回调；回调核对host thread，调用所属EditorDocument的ABI，不以整工程替换实现歌词编辑。原冻结SDK/既有引擎接口未变，现有内部SetDocumentAccess调用的歌词回调可选。

已验证：m5-lyrics-abi-runtime.log为真实CLR延迟状态/重复/无匹配/非法文本/清空/回调；m5-pair-lyrics-runtime.log为实际两个插件包中分离ALC的代理转发夹具和peer移除拒绝，明确区分dispatch fixture；m5-ara-lyrics-probe.log为真实BridgeAudioModification回调、状态变更、宿主modification通知、重复抑制及WrongThread拒绝，数据为SDK构造的源状态。m5-pair-lyrics-managed-build.log和m5-pair-lyrics-native-build.log构建退出0、双validator各47/0；m5-lyrics-pair-regression.log相关36/36 PASS。

仍未接入InstrumentProbe.process捕获和控制器消费者、运行时noteId→NoteOn起点关联/seek失效、标准INoteExpressionController声明；未验证本轮歌词变化后的真实SVS重新渲染。以上ABI调用/通知不能冒充Cubase Key Editor供给或完整端到端歌声同步。M5未完成、未提交推送。

2026-11-15 活动文档补记：CubaseAraLyrics.ResolveSourceNote 按 baseline 的宿主 quarter 起点、pitch、source origin 和 part anchor 定位持久ID；不读取用户移动/转调后的几何，不将列表顺序当身份，同起点同音高的歧义返回0。CubaseDocumentContext.ApplyHostLyrics 已通过现有数据属性和单次Commit更新实际文档，保留对象/选择/撤销历史；未提交编辑时拒绝更新，冲突仅推进baseline，重复输入无通知。空显式发音交由现有 FinalPronunciation/G2P 决定，避免把自动生成发音误判为用户覆盖。一次 settled StateChanged 接回已有观察者入口，但原生 ingress 尚未调用该方法。

m5-live-lyrics-tests.log：歌词状态、映射、活动文档、ARA merge/project-state 共28/28 PASS，Windows非GUI，退出0。包括来源/缺失/歧义、负来源原点、part锚点、本地几何覆盖、连续歌词/撤销重做、Busy拒绝、冲突通知及重开。没有实际宿主文本或本轮歌声重新渲染证据，私有ABI仍16。

1. 接入实际处理器的捕获和非音频消费者，按 SDK 契约声明 INoteExpressionController 文本类型；数值转换方法不伪造歌词数据。
2. 建立运行时音符关联与稳定文档音符的映射，处理重复/缺失/歧义、seek/loop 与修订失效；不能直接持久化 runtime noteId。
3. 实现歌词更新、空值/延音/语言处理、用户覆盖冲突、编辑通知与增量重合成、版本化保存恢复及相关自动回归。
4. 审计原计划 M5 要求，完成自主实施与自动验收后独立提交推送，确认后才进入 M6。

真实 Cubase/V7、GUI 与用户配合采集均为 **SKIPPED（用户决定，未执行，需人工验证）**，不作为阻塞，不记为 PASS。若只能证明候选通道，继续明确保留“Cubase 原生歌词协议未证实”，不宣称 S3+ 已实现，不通过自有消息或宿主内部内存冒充 Key Editor 歌词同步。


## M5 最新补记：Bridge worker 复用（2026-11-15）

已完成 AraPlayback 按 region 生命周期 token 复用 RenderWorker/managed RenderSession，Entry 固定 generation；Bridge 实际入口传入 region reference token。此项覆盖此前“下一最小任务”中的 worker 复用待办。

m5-bridge-session-build.log：全量原生构建退出0，双 validator 各47/0。m5-retained-region-probe-build.log：定向重建退出0。m5-retained-region-synthesis.log：fu2_ning2_na4 CLI 退出0，新修订完成尚未发布时旧计划 ready=false 且拒绝混音，新计划发布后 ready=true；并发替换、实际 Bridge 合成/格式/裁剪和归档回归通过。git diff --check 通过。

下一步：用真实引擎的片段对象或合成任务证据验证未修改片段缓存保留，再审计 M5 修改/删除/复制/撤销及版本/归档组合。上述通过不作为片段缓存命中证据。M5 仍未完成、未提交；M6 未开始；分支 cubase_16_full_ara，HEAD f3c38723。GUI/Cubase 实机/试听 SKIPPED（用户决定，未执行，需人工验证）。


## M5 真实片段缓存证据（2026-11-15，最新）

新增可重复 CLI 探针 integrations/cubase/probes/RenderCacheProbe，使用已有测试 friend assembly，不修改公共 SDK。加载本机 fu2_ning2_na4，构造同一 part 中两个分离乐句，仅将第一句“啊”改为“你”。第二句的原始 AudioSegment、Samples、CommitVersion=1、输出 sample buffer 和 Waveform 引用全部保留（未修改段 sampleOffset=165854）；工程与 pipeline 对象也保持。改词后整体音频平均绝对变化0.006203532，有限且非零；与冷启动完整重渲染最大采样误差0。

m5-tracked-cache-probe-build.log：构建退出0（既有依赖/漏洞源警告）；m5-real-cache-retention.log：执行退出0。早期临时探针 m5-real-cache-probe.log 同样通过，正式以仓库内探针与最终日志为准。首次 sandbox dotnet format 命名管道访问失败，随后按授权在可用权限下重跑格式、构建和探针全部通过。git diff --check 通过。模型不随探针或插件分发，输入状态不改写。

此项补齐已测歌词单点更新下的真实未修改乐句缓存保留证据，不推及其他结构修改的增量能力。Bridge worker/generation 复用证据见前节。下一最小任务是 M5 修改/删除/复制/撤销、语言/延音、错误版本与归档组合审计及阶段全量回归；M5仍未完成、未提交，M6未开始。Cubase原生歌词供给、GUI和试听仍SKIPPED（用户决定，未执行，需人工验证）。当前分支cubase_16_full_ara、HEAD f3c38723；未产生新的提交或推送。最近可靠额度used57%，允许继续。


## M5 关联事务与全量回归补记（2026-11-15）

新增自动验证：同一 part 复制音符不继承持久 source ID（原音符9、复制音符0）；宿主歌词只更新原音符；删除后撤销恢复原对象及关联；保存重开后更新仍隔离复制音符；未知 envelope 版本999恢复被拒绝且当前状态逐字节不变。另以明确标注的宿主快照夹具覆盖来源复制新ID33、删除旧ID11拒绝迟到更新及撤销快照恢复；中/日/英文、空值及“-”文本原样保存/刷新由既有引擎解释，不引入未经证实的语言协议。

- m5-lyric-transactions-tests.log：关联事务/merge/state 定向37/37通过。
- m5-full-tests.log：主测试集890/890通过，退出0。
- m5-legacy-tests.log：旧引擎兼容13/13通过，退出0。
- m5-final-host-verification.log：完整verify-host退出0，ABI20、双validator各47/0，原生队列/状态/ARA归档与rollback、实际插件歌词及跨包代理回归通过。
- 只对本轮修改测试运行格式检查；git diff --check通过。未改变产品行为或公共SDK。

M5仍未做阶段提交/推送，M6未开始。下一最小任务：补齐带歌词的实际ARA modification归档/clone/partial restore组合证据，并定向核对source-only identity restore与已排队文本的修订失效；不得仅以既有无歌词归档测试替代该组合。语言/延音仅文本与引擎现有语义，不冒称Cubase语言/音素事件已证实。真实Cubase供给/GUI/试听继续SKIPPED（用户决定，未执行，需人工验证）。分支cubase_16_full_ara，HEAD f3c38723保持未变；所有M5修改尚未提交。


## M5 歌词归档与 source-only 修复（2026-11-15，最新）

实际配对插件夹具新增并通过：歌词“你”的 ARA modification 克隆、仅该 modification 的归档、修改为“好”后部分恢复；native 状态与 managed baseline 一致，未选中的两个 modification 保持原状态。m5-lyric-archive-paired.log 退出0。

source-only身份恢复专项复现缺陷：归档把来源ID12恢复为99时，尚未刷新score，旧排队歌词被接纳；m5-source-only-archive-red.log退出1。修复在来源身份列表实际改变的成功提交点，立即通知该source所属的所有已注册Bridge owner，令配对Instrument的输入修订失效。新增私有ABI21 InvalidatePairSource，公共SDK与既有引擎契约不变。相同身份不触发此额外通知。

m5-source-only-archive-green.log退出0：旧队列文本在score刷新前拒绝，刷新后新的runtime noteId505能更新新持久ID99，来源移动保护和真实fu2_ning2_na4 Bridge合成仍通过。m5-source-only-fix-build.log原生构建退出0；m5-abi21-host-verification.log完整verify-host退出0、ABI21、双validator各47/0、既有归档/rollback和跨包协议通过。修改范围格式检查与git diff --check通过。前轮主测试890/890与legacy13/13仍为最近全量结果（此次未改其产品路径）。

当前M5尚未执行阶段检查点，M6未开始。下一最小任务是按M5预定DoD作最终证据/diff审计、整理当前计划状态与交接后单独commit/push并确认远端，再进入M6。不要继续添加计划外要求。原生宿主歌词供给、实机/GUI/试听仍SKIPPED（用户决定，未执行，需人工验证），不作为受阻；不可宣称真实Cubase S3+通过。分支cubase_16_full_ara、HEAD f3c38723，全部M5修改未提交。手册上方ABI20与待办属于历史，以本节ABI21为最新。
