# Cubase 候选包依赖与许可记录

本表依据本地已使用源码、NuGet 元数据和许可原文核对，不把编译工具或本机测试声库当作发行依赖。每个候选包的精确托管依赖 ID/版本/type 与原生 SDK 提交以 manifest.json 为准，二进制完整性以其 SHA256 清单为准。

| 组件 | 本轮版本或来源 | 包内声明及部署方式 |
|---|---|---|
| TuneLab | 当前分支源提交，应用版本2.1.1；私有集成ABI21，原生插件候选版本0.1.0 | MIT，LICENSE.txt；公共SDK/Foundation ABI保持原约定 |
| VST3 SDK | 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96；实际validator报告VST3 3.8.0 | MIT；licenses/VST3-LICENSE.txt及base/interfaces/public-sdk许可证。未使用VSTGUI |
| ARA API | 65ec5c43b943a48cb5446f448a0492db6af8534b | Apache-2.0；licenses/ARA-API-LICENSE.txt |
| ARA Library | d18a6a5e489816316be84a9de0eaf7307bc1abe4 | Apache-2.0；licenses/ARA-Library-LICENSE.txt |
| nethost及对应头文件 | Microsoft.NETCore.App.Host.win-x64/8.0.26，本机已有NuGet包 | NuSpec声明MIT；DLL、LICENSE.TXT、THIRD-PARTY-NOTICES.TXT来自同一包，分别作为nethost.dll和licenses/dotnet-*随包发布 |
| .NET运行时 | runtimeconfig要求Microsoft.NETCore.App 8.0 x64，按系统运行时策略解析补丁版本 | 系统先决条件，不随候选包分发或卸载；SDK/宿主包不代替完整运行时 |
| VC++ x64运行库 | Windows原生Release工具链所需 | 系统先决条件，不复制开发机系统DLL，不随本包卸载 |
| Avalonia及托管依赖 | 如Avalonia11.3.15、AvaloniaEdit11.2.0；完整精确列表见manifest.managedLibraries及各bundle的.deps.json | THIRD-PARTY-NOTICES.txt含组件声明和许可证全文；其中LAME/CUETools等LGPL组件保留原声明，不将整个包一概称MIT |
| 默认钢琴SF2 | UprightPianoKW.sf2，既有TuneLab资源 | CC0，原有THIRD-PARTY-NOTICES.txt记载；用于内置钢琴资源，不是用户歌手模型 |
| 合成引擎和用户声库 | 用户另行安装 | 不随本候选包分发，按各自许可使用；fu2_ning2_na4仅获授权用于本机测试 |

rc01/rc02曾复制系统.NET安装目录的通用许可文件。rc03起改为从实际打包的nethost NuGet包取得精确声明，避免混用SDK安装许可与分发组件许可。打包脚本核对该包的NuSpec名称/版本，并要求原始许可文件存在；不会为了生成候选包而下载另一套运行时。

候选包不含Cubase/VOCALOID二进制、Steinberg或Celemony产品声库，不复制其私有实现或宿主内部内存。品牌与接口名称用于说明兼容目标。真实宿主发现和歌词协议仍按人工验收清单记录SKIPPED，许可文件存在或SDK validator通过不等于获得厂商产品认证。
