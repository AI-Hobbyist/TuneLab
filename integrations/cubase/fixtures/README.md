# 无声库交付测试工程

本目录由 probes/FixtureBuilder 使用 TuneLab 真实序列化器及标准 MIDI 写入生成，不包含引擎、歌手模型或个人数据。

| 文件 | 用途 |
|---|---|
| tempo-lyrics.tlpx | TuneLab 原生工程；打开后明确选择自己的已安装引擎与歌手 |
| tempo-lyrics.mid | SMF0、480 PPQ，包含音符、速度、拍号及 UTF-8 lyric meta event；供宿主导入后人工检查 |
| tempo-lyrics.state | 私有 TLCUBASE v2 状态夹具，source 为 delivery-fixture，稳定音符 ID 为 11/22/33；仅供 CLI/协议测试，不是 Cubase 工程 |

工程从 120 BPM、4/4 开始，在 tick 3840（quarter 8）变为 90 BPM、3/4。音符表如下；时间均为工程 tick，分辨率 480 tick/quarter。

| 稳定 ID | 起点 | 时长 | MIDI 音高 | 歌词 |
|---|---:|---:|---:|---|
| 11 | 480 | 480 | 60 | 啊 |
| 22 | 3840 | 480 | 62 | 好 |
| 33 | 4320 | 480 | 62 | - |

声源留空，静音不视为合成通过。使用前在内嵌插件中选择本机已安装歌手；本项目仅授权 fu2_ning2_na4 用于本机测试，候选包不分发它。第二、第三音符的连音或延音发音由所选引擎解释。

MIDI lyric meta event 是文件数据，不能用它证明 Cubase 会发送标准 VST3 歌词事件。实际 .cpr 需后续在 Cubase 中导入、关联 Bridge、选择歌手并保存；该操作本轮 SKIPPED（用户决定，未执行，需人工验证）。没有伪造 .cpr 文件，也没有将 .state 改名为宿主工程。

重新生成：从仓库根目录构建 integrations/cubase/probes/FixtureBuilder/FixtureBuilder.csproj，再用其 TuneLab.Tests.dll 运行，唯一参数为本目录。构建/运行均须遵循前台 PowerShell Tee-Object build.log 及退出码检查规则。生成器自行检查 .tlpx 和 .state 的歌词/身份往返；MIDI 结构另作独立检查。
