using System;
using System.Collections.Generic;
using System.Linq;

namespace TuneLab.Agent.Models;

internal enum AgentEndpointFamily
{
    OpenAIChatCompletions,
    AnthropicMessages,
    GeminiGenerateContent,
    Ollama,
}

internal sealed record AgentProviderDefinition(
    string Id,
    string Name,
    AgentEndpointFamily Family,
    string BaseUrl,
    string? ApiKeyUrl = null,
    bool NoApiKeyRequired = false,
    string? ModelsApiUrl = null);

internal static class AgentProviderCatalog
{
    public const string CustomProviderId = "custom";

    public static IReadOnlyList<AgentProviderDefinition> Providers => sProviders;

    public static AgentProviderDefinition DefaultProvider => sProviders[0];

    public static AgentProviderDefinition Resolve(string? id)
        => Find(id) ?? DefaultProvider;

    public static AgentProviderDefinition? Find(string? id)
        => string.IsNullOrWhiteSpace(id) ? null : sProviders.FirstOrDefault(p => p.Id == id);

    public static string FamilyId(AgentEndpointFamily family) => family switch
    {
        AgentEndpointFamily.AnthropicMessages => "anthropic-messages",
        AgentEndpointFamily.GeminiGenerateContent => "google-generate-content",
        AgentEndpointFamily.Ollama => "ollama-chat",
        _ => "openai-chat-completions",
    };

    public static AgentEndpointFamily ParseFamily(string? value)
        => value switch
        {
            "anthropic-messages" => AgentEndpointFamily.AnthropicMessages,
            "google-generate-content" => AgentEndpointFamily.GeminiGenerateContent,
            "ollama-chat" => AgentEndpointFamily.Ollama,
            _ => AgentEndpointFamily.OpenAIChatCompletions,
        };

    static readonly AgentProviderDefinition[] sProviders =
    [
        new("custom", "Custom OpenAI Compatible", AgentEndpointFamily.OpenAIChatCompletions, "https://api.openai.com/v1"),
        new("openai", "OpenAI", AgentEndpointFamily.OpenAIChatCompletions, "https://api.openai.com/v1", "https://platform.openai.com/api-keys"),
        new("anthropic", "Anthropic", AgentEndpointFamily.AnthropicMessages, "https://api.anthropic.com", "https://console.anthropic.com/settings/keys"),
        new("gemini", "Gemini", AgentEndpointFamily.GeminiGenerateContent, "https://generativelanguage.googleapis.com", "https://aistudio.google.com/app/apikey"),
        new("deepseek", "深度求索 DeepSeek", AgentEndpointFamily.OpenAIChatCompletions, "https://api.deepseek.com", "https://platform.deepseek.com/api_keys"),
        new("openrouter", "OpenRouter", AgentEndpointFamily.OpenAIChatCompletions, "https://openrouter.ai/api/v1", "https://openrouter.ai/settings/keys", ModelsApiUrl: "https://openrouter.ai/api/v1/models"),
        new("silicon", "硅基流动 SiliconFlow", AgentEndpointFamily.OpenAIChatCompletions, "https://api.siliconflow.cn/v1", "https://cloud.siliconflow.cn/account/ak"),
        new("zhipu", "智谱 Zhipu", AgentEndpointFamily.OpenAIChatCompletions, "https://open.bigmodel.cn/api/paas/v4/", "https://bigmodel.cn/usercenter/proj-mgmt/apikeys"),
        new("moonshot", "月之暗面 Moonshot", AgentEndpointFamily.OpenAIChatCompletions, "https://api.moonshot.cn", "https://platform.moonshot.cn/console/api-keys"),
        new("dashscope", "阿里云百炼 Bailian", AgentEndpointFamily.OpenAIChatCompletions, "https://dashscope.aliyuncs.com/compatible-mode/v1/", "https://bailian.console.aliyun.com/?apiKey=1#/api-key"),
        new("doubao", "豆包 Doubao", AgentEndpointFamily.OpenAIChatCompletions, "https://ark.cn-beijing.volces.com/api/v3/", "https://console.volcengine.com/ark/region:ark+cn-beijing/apiKey"),
        new("groq", "Groq", AgentEndpointFamily.OpenAIChatCompletions, "https://api.groq.com/openai", "https://console.groq.com/keys"),
        new("mistral", "Mistral", AgentEndpointFamily.OpenAIChatCompletions, "https://api.mistral.ai", "https://console.mistral.ai/api-keys"),
        new("together", "Together", AgentEndpointFamily.OpenAIChatCompletions, "https://api.together.ai", "https://api.together.ai/settings/api-keys"),
        new("fireworks", "Fireworks", AgentEndpointFamily.OpenAIChatCompletions, "https://api.fireworks.ai/inference", "https://fireworks.ai/account/api-keys"),
        new("nvidia", "NVIDIA", AgentEndpointFamily.OpenAIChatCompletions, "https://integrate.api.nvidia.com", "https://build.nvidia.com/settings/api-keys"),
        new("perplexity", "Perplexity", AgentEndpointFamily.OpenAIChatCompletions, "https://api.perplexity.ai/", "https://www.perplexity.ai/settings/api"),
        new("modelscope", "魔搭 ModelScope", AgentEndpointFamily.OpenAIChatCompletions, "https://api-inference.modelscope.cn/v1/", "https://modelscope.cn/my/myaccesstoken"),
        new("github", "Github Models", AgentEndpointFamily.OpenAIChatCompletions, "https://models.github.ai/inference", "https://github.com/settings/personal-access-tokens"),
        new("yi", "零一万物 Yi", AgentEndpointFamily.OpenAIChatCompletions, "https://api.lingyiwanwu.com", "https://platform.lingyiwanwu.com/apikeys"),
        new("baichuan", "百川智能 Baichuan", AgentEndpointFamily.OpenAIChatCompletions, "https://api.baichuan-ai.com", "https://platform.baichuan-ai.com/console/apikey"),
        new("stepfun", "阶跃星辰 StepFun", AgentEndpointFamily.OpenAIChatCompletions, "https://api.stepfun.com", "https://platform.stepfun.com/account-info/api-key"),
        new("minimax", "MiniMax", AgentEndpointFamily.OpenAIChatCompletions, "https://api.minimaxi.com/v1/", "https://platform.minimaxi.com/user-center/basic-information/interface-key"),
        new("minimax-global", "MiniMax Global", AgentEndpointFamily.OpenAIChatCompletions, "https://api.minimax.io/v1/", "https://platform.minimax.io/user-center/basic-information/interface-key"),
        new("hunyuan", "腾讯混元 Hunyuan", AgentEndpointFamily.OpenAIChatCompletions, "https://api.hunyuan.cloud.tencent.com", "https://console.cloud.tencent.com/hunyuan/api-key"),
        new("baidu-cloud", "百度智能云千帆", AgentEndpointFamily.OpenAIChatCompletions, "https://qianfan.baidubce.com/v2/", "https://console.bce.baidu.com/qianfan/ais/console/applicationConsole/application"),
        new("cerebras", "Cerebras AI", AgentEndpointFamily.OpenAIChatCompletions, "https://api.cerebras.ai/v1", "https://cloud.cerebras.ai/platform/"),
        new("zai", "智谱 Z.AI", AgentEndpointFamily.OpenAIChatCompletions, "https://api.z.ai/api/paas/v4/", "https://z.ai/manage-apikey/apikey-list"),
        new("longcat", "LongCat", AgentEndpointFamily.OpenAIChatCompletions, "https://api.longcat.chat/openai", "https://longcat.chat"),
        new("poe", "Poe", AgentEndpointFamily.OpenAIChatCompletions, "https://api.poe.com/v1/", "https://poe.com/api_key"),
        new("aihubmix", "AiHubMix", AgentEndpointFamily.OpenAIChatCompletions, "https://aihubmix.com/v1", "https://aihubmix.com/token"),
        new("302ai", "302.AI", AgentEndpointFamily.OpenAIChatCompletions, "https://api.302.ai", "https://dash.302.ai/apis/list"),
        new("dmxapi", "DMXAPI", AgentEndpointFamily.OpenAIChatCompletions, "https://www.dmxapi.cn", "https://www.dmxapi.cn/token"),
        new("aionly", "AIOnly", AgentEndpointFamily.OpenAIChatCompletions, "https://api.aiionly.com", "https://aiionly.com"),
        new("new-api", "New API", AgentEndpointFamily.OpenAIChatCompletions, "http://localhost:3000"),
        new("ollama", "Ollama", AgentEndpointFamily.Ollama, "http://localhost:11434", NoApiKeyRequired: true),
        new("lmstudio", "LM Studio", AgentEndpointFamily.OpenAIChatCompletions, "http://localhost:1234", NoApiKeyRequired: true),
        new("gpustack", "GPUStack", AgentEndpointFamily.OpenAIChatCompletions, "http://localhost:80", NoApiKeyRequired: true),
        new("ovms", "OpenVINO Model Server", AgentEndpointFamily.OpenAIChatCompletions, "http://localhost:8000/v3/", NoApiKeyRequired: true),
    ];
}
