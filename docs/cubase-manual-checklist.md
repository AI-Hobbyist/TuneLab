# Cubase 16 候选包人工验收清单

全部人工项当前均为 **SKIPPED（用户决定，未执行，需人工验证）**，不作为自主实施检查点或交付受阻条件。此表保留原实施计划 A01–A16，供以后明确要求实机验证时使用；没有将自动夹具结果登记为人工 PASS。

记录模板：候选包 manifest 源提交、包 SHA256、Cubase/Windows 版本、引擎/歌手、采样率/块大小、输入工程、步骤、预期、实际、状态与证据路径。先备份工程，不在卸载测试中删除用户数据。使用包内 fixtures 的无声库输入后再选择合法的本机声库。

| 项 | 后续人工操作及观察目标 | 当前状态 |
|---|---|---|
| A01 | 安装完整匹配插件对，检查 Instrument 与 Extensions 发现 | SKIPPED |
| A02 | VST3 窗口与 Extension 容器操作真实钢琴窗 | SKIPPED |
| A03 | 编辑音符/歌词/音素/音高/参数并撤销重做，检查所属文档 | SKIPPED |
| A04 | Key Editor 改两种歌词，核对实际事件、关联、显示与试听 | SKIPPED |
| A05 | 变速、拍号、region 偏移/裁剪及采样对齐；排除声库辅音提前量 | SKIPPED |
| A06 | 按原计划采样率/块大小预热后连续播放30分钟，记录欠载和最坏回调耗时 | SKIPPED |
| A07 | 记录首次/增量合成和预览延迟，检查未就绪与修订状态 | SKIPPED |
| A08 | 前后seek、循环、停止尾音及新区域数据 | SKIPPED |
| A09 | 实时录制、离线导出、Make Permanent 对齐比较，按引擎确定性判定 | SKIPPED |
| A10 | 至少10次保存重开，覆盖逆序恢复、缺声库、旧状态及损坏输入 | SKIPPED |
| A11 | 至少4对实例，复制轨道并关闭其中一对，检查隔离与单一输出owner | SKIPPED |
| A12 | 20次视图开关及实例重建，检查HWND/订阅/任务释放 | SKIPPED |
| A13 | 100/125/150/200%缩放、跨屏、中日IME、菜单快捷键与命中 | SKIPPED |
| A14 | 引擎失败、取消、删除region、关闭工程和移除Bridge后的诊断与释放 | SKIPPED |
| A15 | 独立版打开、编辑、播放、保存和加载插件 | SKIPPED |
| A16 | 干净Windows的运行时依赖、升级卸载及用户工程保护 | SKIPPED |

已执行的自动证据单列在 cubase-m2-validation.md 至 cubase-m6-validation.md；部署包哈希、SDK validator、CLR 协议和序列化验证仅证明其实际覆盖的内容。真实 Cubase 原生歌词供给仍待 A04 验证。
