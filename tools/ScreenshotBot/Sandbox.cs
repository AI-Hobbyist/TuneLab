using System;
using System.IO;
using System.Text;

namespace TuneLab.ScreenshotBot;

// 隔离的用户数据沙盒：截图不能读到真实环境（会带上个人设置与已装的第三方插件），也不能写坏它。
// 靠 TUNELAB_DATA_DIR 把 PathManager 的数据根整体改指到这里，每次运行重建。
internal static class Sandbox
{
    // 文档用的演示扩展：tools/ScreenshotBot/demo-plugins 下的示例插件（中性命名、无第三方内容）。
    // 真实环境里装的是各家第三方插件，其名称不该出现在手册插图里，故一律走沙盒里的这两个。
    static readonly (string Folder, string InstallName)[] DemoExtensions =
    [
        ("DemoVoice", "示例声源"),
        ("DemoInstrument", "示例乐器"),
    ];

    public static void Prepare(string dataDir, string repoRoot)
    {
        if (Directory.Exists(dataDir))
            Directory.Delete(dataDir, true);
        Directory.CreateDirectory(Path.Combine(dataDir, "Configs"));
        Directory.CreateDirectory(Path.Combine(dataDir, "Extensions"));
        Directory.CreateDirectory(Path.Combine(dataDir, "Scripts"));

        foreach (var (folder, installName) in DemoExtensions)
        {
            string source = Path.Combine(repoRoot, "tools", "ScreenshotBot", "demo-plugins", "out", folder);
            if (!Directory.Exists(source))
            {
                Console.WriteLine($"[warn] demo plugin not built: {source} (run shoot.ps1)");
                continue;
            }
            CopyDirectory(source, Path.Combine(dataDir, "Extensions", installName));
        }

        WriteDemoScript(Path.Combine(dataDir, "Scripts"));
    }

    static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.GetFiles(source))
        {
            // pdb 不必进沙盒，其余（dll / deps.json / manifest / Introduction）照搬。
            if (Path.GetExtension(file).Equals(".pdb", StringComparison.OrdinalIgnoreCase))
                continue;
            File.Copy(file, Path.Combine(target, Path.GetFileName(file)), true);
        }
    }

    // 脚本页需要库里有东西；写一个**真能跑**的最小脚本（按 Resources/ScriptDoc 的口径：集合用方法、
    // 标量用裸属性）。写错 API 的示例比没有示例更坏——它会被人照着抄。
    static void WriteDemoScript(string scriptsDir)
    {
        File.WriteAllText(Path.Combine(scriptsDir, "选中音符升八度.js"), """
        // 把钢琴窗里选中的音符整体升一个八度。
        const part = tl.currentPart();
        if (!part)
            throw new Error("先在钢琴窗里打开一个片段");
        for (const note of part.selectedNotes())
            note.pitch += 12;
        """, new UTF8Encoding(false));
    }

    // 音频文件放哪：公共音乐目录（路径里没有用户名，可以出现在插图上）优先，不可写则退回沙盒。
    static string PickAudioDir(string sandboxRoot)
    {
        try
        {
            string common = Environment.GetFolderPath(Environment.SpecialFolder.CommonMusic);
            if (!string.IsNullOrEmpty(common))
            {
                string dir = Path.Combine(common, "TuneLab");
                Directory.CreateDirectory(dir);
                return dir;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("[warn] cannot use public music folder: " + ex.Message);
        }
        return Path.Combine(sandboxRoot, "audio");
    }

    // 演示用音频：写一段带包络的 16-bit 单声道 WAV，供音频轨与波形截图使用。
    public static string WriteDemoAudio(string sandboxRoot, double seconds = 4.0, int sampleRate = 44100)
    {
        string dir = PickAudioDir(sandboxRoot);
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, "参考音频.wav");
        int count = (int)(seconds * sampleRate);
        var samples = new short[count];
        for (int i = 0; i < count; i++)
        {
            double t = (double)i / sampleRate;
            // 三个音的琶音 + 每音一个衰减包络，波形看上去像真实素材而不是一条直线。
            double freq = t < seconds / 3 ? 220 : t < seconds * 2 / 3 ? 277.18 : 329.63;
            double phase = t % (seconds / 3);
            double env = Math.Exp(-3 * phase) * (1 - Math.Exp(-80 * phase));
            double v = Math.Sin(2 * Math.PI * freq * t) * 0.6 * env
                     + Math.Sin(2 * Math.PI * freq * 2 * t) * 0.15 * env;
            samples[i] = (short)(Math.Clamp(v, -1, 1) * short.MaxValue);
        }

        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        int dataBytes = count * 2;
        writer.Write(Encoding.ASCII.GetBytes("RIFF"));
        writer.Write(36 + dataBytes);
        writer.Write(Encoding.ASCII.GetBytes("WAVEfmt "));
        writer.Write(16);
        writer.Write((short)1);            // PCM
        writer.Write((short)1);            // mono
        writer.Write(sampleRate);
        writer.Write(sampleRate * 2);      // byte rate
        writer.Write((short)2);            // block align
        writer.Write((short)16);           // bits
        writer.Write(Encoding.ASCII.GetBytes("data"));
        writer.Write(dataBytes);
        foreach (var s in samples)
            writer.Write(s);
        return path;
    }
}
