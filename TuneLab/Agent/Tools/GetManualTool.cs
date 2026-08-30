using System;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using TuneLab.Docs;

namespace TuneLab.Agent;

// 「怎么操作」类问题的按需文档（渐进式披露，同 get_script_api）：用户手册随包发布，模型不常驻它，
// 需要时先看目录/检索、再取一章。回答界面用法必须**以手册为依据**而不是凭印象——手册与软件同版本，
// 模型的记忆不是。
//
// 与自省类工具的分工：list_settings / list_keybindings 报的是「此刻这台机器上是什么值、在哪一行」，
// 手册讲的是「这个功能是什么、怎么配合别的功能用」。问「颤音怎么画」查手册，问「我的颤音快捷键是啥」查注册表。
internal sealed class GetManualTool : IAgentTool
{
    public string Name => "get_manual";

    public string Description =>
        "Look up the bundled TuneLab user manual - the authoritative source for how the editor is operated " +
        "(UI areas, tools and their mouse gestures, sidebar pages, settings, files, extensions). " +
        "Call with no arguments for the table of contents, with `query` to search, or with `section` to read one chapter in full. " +
        "Use this before answering any \"how do I ...\" / \"where is ...\" question about the editor instead of relying on memory. " +
        "For live per-machine values (current shortcut bindings, current setting values) use the list_* tools instead.";

    public string ParametersJsonSchema => """
        {
          "type": "object",
          "properties": {
            "section": {
              "type": "string",
              "description": "Chapter id (or a title/subheading substring) to read in full, e.g. \"parameters\". Omit to get the table of contents."
            },
            "query": {
              "type": "string",
              "description": "Keyword to search across the manual. Returns matching lines with the chapter they live in."
            }
          },
          "additionalProperties": false
        }
        """;

    public Task<string> ExecuteAsync(string argumentsJson, CancellationToken cancellationToken)
    {
        if (!ManualLibrary.IsAvailable)
            return Task.FromResult("The user manual is not bundled with this build (Resources/Manual is missing). "
                + "Answer from the introspection tools instead, and say that the manual is unavailable.");

        string? section = null, query = null;
        // 无参调用（取目录）是常态，故参数 JSON 可能是空串 / "{}" / null——都不该当错误。
        if (!string.IsNullOrWhiteSpace(argumentsJson))
        {
            try
            {
                using var doc = JsonDocument.Parse(argumentsJson);
                section = doc.RootElement.GetStringOrNull("section");
                query = doc.RootElement.GetStringOrNull("query");
            }
            catch (Exception ex) { return Task.FromResult("Error: invalid arguments — " + ex.Message); }
        }

        if (!string.IsNullOrWhiteSpace(section))
            return Task.FromResult(ReadSection(section!));

        if (!string.IsNullOrWhiteSpace(query))
            return Task.FromResult(SearchManual(query!));

        return Task.FromResult(Toc());
    }

    static string Header()
    {
        var sb = new StringBuilder();
        sb.Append("TuneLab user manual (language: ").Append(ManualLibrary.Language).Append(')');
        if (!ManualLibrary.IsCurrentLanguage)
            sb.Append(" - no edition in the current UI language, so this is a fallback edition; "
                + "answer the user in their own language regardless of the manual's language");
        sb.AppendLine(".");
        return sb.ToString();
    }

    static string Toc()
    {
        var sb = new StringBuilder();
        sb.Append(Header());
        sb.AppendLine("Chapters (id — title (subsections)). Read one with section=<id>, or search with query=<keyword>:");
        sb.AppendLine();
        sb.Append(ManualLibrary.BuildToc());
        return sb.ToString();
    }

    static string ReadSection(string key)
    {
        var found = ManualLibrary.Find(key);
        if (found == null)
        {
            var sb = new StringBuilder();
            sb.Append("No chapter matches \"").Append(key).AppendLine("\". Available chapters:");
            sb.Append(ManualLibrary.BuildToc());
            return sb.ToString();
        }

        return Header() + "\n" + ManualLibrary.ForModel(found);
    }

    static string SearchManual(string query)
    {
        var hits = ManualLibrary.Search(query);
        var sb = new StringBuilder();
        sb.Append(Header());
        if (hits.Count == 0)
        {
            sb.Append("No line matches \"").Append(query).AppendLine("\". Chapters available:");
            sb.Append(ManualLibrary.BuildToc());
            return sb.ToString();
        }

        sb.Append("Lines matching \"").Append(query).AppendLine("\" (read the whole chapter with section=<id> for context):");
        sb.AppendLine();
        foreach (var group in hits.GroupBy(h => h.SectionId))
        {
            sb.Append("[").Append(group.Key).Append("] ").AppendLine(group.First().SectionTitle);
            foreach (var hit in group)
                sb.Append("  ").AppendLine(hit.Line);
        }
        return sb.ToString();
    }
}
