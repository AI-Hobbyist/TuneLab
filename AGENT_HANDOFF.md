# 当前任务交接

更新时间：2026-11-15。持续目标 ACTIVE。分支 cubase_16_full_ara。

## 当前阶段

按 docs/TuneLab_Cubase16_S3Plus_路线B实施计划.md 的 M0→M6 交付 VSTi3 + ARA Bridge、路线B进程内钢琴窗、合成/时间/音符/歌词关联、工程恢复与发布材料。M0–M4 已完成自主实施、非 GUI 验收及提交推送。M5 自主实施与非 GUI 验收及独立提交/推送检查点已完成，M6 进入部署与交付材料审计。真实 Cubase S3+ 未实机验证。

## 已完成与已验证

- M5：标准 VST3 kTextTypeID 文本入口，有界 UTF16 队列、主线程消费，运行时 NoteOn 起点/pitch 到 ARA baseline 持久ID关联，歧义/旧修订拒绝。
- 歌词属性事务、单次settled通知、本地词/发音/固定音素覆盖冲突保护；跨包配对代理、owner状态共享与实例隔离；状态行展示已实现，视觉未验证。
- CubaseRenderSession 保留工程和引擎会话；Bridge按region生命周期复用worker，每份计划固定generation，旧计划不读取新修订音频。
- fu2_ning2_na4本机CLI验证实际Instrument→Bridge歌词更新与音频变化；未改乐句的AudioSegment/CommitVersion/样本/波形引用保留，冷暖最大采样误差0。
- 复制不继承source ID、删除撤销恢复关联、重开更新、未知版本保持原状态；带歌词ARA clone/partial restore和managed baseline一致性。
- source-only身份恢复接纳旧队列文本的缺陷已RED复现并修复；私有ABI21 InvalidatePairSource，成功提交时使受影响owner旧文本失效，fresh runtime note505仍更新新ID99。
- SDK/Foundation冻结公共ABI未修改；没有新增既有引擎必需接口，没有分发测试模型。

## 验证记录

正式逐项审计：docs/cubase-m5-validation.md。日志在out/cubase/m5（本地，不纳入版本库）。

- m5-solution-build.log：Windows解决方案构建0错误。
- m5-full-tests.log：主测试890/890通过。
- m5-legacy-tests.log：旧引擎兼容13/13通过。
- m5-abi21-host-verification.log：完整verify-host退出0，ABI21、双validator各47/0，实际双包/CLR/ARA/歌词协议通过。
- m5-source-only-archive-red.log退出1→m5-source-only-archive-green.log退出0；后者包含带歌词归档组合与fresh ID99验证。
- m5-real-cache-retention.log：真实声库片段缓存及冷暖等价通过；可重复探针integrations/cubase/probes/RenderCacheProbe。
- m5-retained-region-synthesis.log：worker复用、旧计划隔离、并发替换和实际Bridge输出回归通过。
- git diff --check通过。已有编译/依赖警告未作为本阶段修复范围。

## Git 状态与下一步

M5功能提交 420c11aea36b52d84c26609853942fbce744f58f 已推送 origin/cubase_16_full_ara，并由git ls-remote确认同SHA、上游同名。本次文档补记仅记录已核实检查点。M6下一最小任务：核对现有打包/部署入口与依赖许可、按原验收补齐离线部署检查和使用文档；完成后再独立commit/push。

保留且不stage原有.gitignore修改、.codegraph/、out/及三份用户参考文档：docs/Cubase16_SVS_接入静态分析.md、docs/S3+_交付与接口清单分析.md、docs/TuneLab_接入Cubase16_方案分析.md。实际文件状态以git status为准。

## 未验证事项及约束

- 所有开软件、真实Cubase/V7、GUI、安装/硬件/试听及用户配合验收一律SKIPPED（用户决定，未执行，需人工验证）；不启动、不等待、不作为blocked、不冒充PASS。
- 只支持本机Windows原生工具链；禁止任何WSL探测/启动及本地非Windows构建。16GB内存，dotnet -m:1 /nodeReuse:false，native --parallel 1。
- CodeGraph优先，主仓及third_party各自索引。不要初始化索引。所有构建/测试/打包前台PowerShell 2>&1 | Tee-Object build.log，立即检查LASTEXITCODE并读失败日志。
- 不扩大范围、不机会重构、不改冻结公共ABI。可选补充API不得破坏已有引擎。每阶段独立commit/push确认才进入下一阶段。
- 普通开发未授权subagent；可靠额度<=2%时仅交接文档可委派Luna。最近可靠used58%，ordinaryUsageAllowed=true，无阈值交接。
- M6下一步按原计划核对部署脚本、运行时/依赖许可、发行包和手册，继续保留真实宿主协议证据缺口。
