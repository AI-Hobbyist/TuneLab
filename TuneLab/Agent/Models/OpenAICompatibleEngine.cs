using TuneLab.Foundation;
using TuneLab.I18N;
using TuneLab.SDK;
using System.Linq;

namespace TuneLab.Agent.Models;

// 内置的参考模型适配器：对接任何 OpenAI 兼容的 /chat/completions 端点（OpenAI 官方、各云厂商、
// 本地 Ollama / LM Studio / vLLM 等）。本身不含模型——端点/密钥/模型名由用户在设置界面填入。
// 作为内置引擎随宿主开箱即用；新的模型适配器走 PR 加进宿主（agent-model 不开放外部扩展，见 IAgentModelEngine 头注释）。
internal sealed class OpenAICompatibleEngine : IAgentModelEngine
{
    public ObjectConfig GetPropertyConfig(IAgentModelPropertyContext context)
    {
        var selectedProviderId = context.Properties.GetString("provider_id", AgentProviderCatalog.DefaultProvider.Id);
        var provider = AgentProviderCatalog.Resolve(selectedProviderId);
        var family = AgentProviderCatalog.ParseFamily(context.Properties.GetString("endpoint_family", AgentProviderCatalog.FamilyId(provider.Family)));
        var baseUrl = context.Properties.GetString("base_url", provider.BaseUrl);
        var apiKey = context.Properties.GetString("api_key", "");
        var currentModel = context.Properties.GetString("model", DefaultModel(provider.Family));
        var cachedModels = AgentModelDiscoveryService.GetCached(provider.Id, baseUrl, apiKey, family);
        var modelOptions = cachedModels is { Count: > 0 }
            ? cachedModels.Select(m => new ComboBoxItem(PropertyValue.Create(m.Id), m.SupportedInput.HasFlag(AgentModality.Image) ? m.DisplayName + "  [image]" : m.DisplayName)).ToList()
            : [new ComboBoxItem(PropertyValue.Create(DefaultModel(provider.Family)), DefaultModel(provider.Family) + "  (refresh models)")];
        if (!string.IsNullOrEmpty(currentModel) && !modelOptions.Any(o => o.Value.Equals(PropertyValue.Create(currentModel))))
            modelOptions.Insert(0, new ComboBoxItem(PropertyValue.Create(currentModel), currentModel + "  (stored)"));

        var properties = new OrderedMap<PropertyKey, IControllerConfig>();
        properties.Add(("provider_id", "Provider".Tr(this)), ComboBoxConfig.Create(AgentProviderCatalog.Providers.Select(p => new ComboBoxItem(PropertyValue.Create(p.Id), p.Name)).ToList()).WithDefault(PropertyValue.Create(provider.Id)));
        properties.Add(("endpoint_family", "Endpoint".Tr(this)), ComboBoxConfig.Create([
            new ComboBoxItem(PropertyValue.Create(AgentProviderCatalog.FamilyId(AgentEndpointFamily.OpenAIChatCompletions)), "OpenAI Chat Completions"),
            new ComboBoxItem(PropertyValue.Create(AgentProviderCatalog.FamilyId(AgentEndpointFamily.AnthropicMessages)), "Anthropic Messages"),
            new ComboBoxItem(PropertyValue.Create(AgentProviderCatalog.FamilyId(AgentEndpointFamily.GeminiGenerateContent)), "Gemini Generate Content"),
            new ComboBoxItem(PropertyValue.Create(AgentProviderCatalog.FamilyId(AgentEndpointFamily.Ollama)), "Ollama"),
        ]).WithDefault(PropertyValue.Create(AgentProviderCatalog.FamilyId(family))));
        properties.Add(("base_url", "Base URL".Tr(this)), TextBoxConfig.Create(provider.BaseUrl));
        properties.Add(("api_key", "API Key".Tr(this)), TextBoxConfig.Create().WithPassword());
        properties.Add(("model", "Model".Tr(this)), ComboBoxConfig.Create(modelOptions));
        properties.Add(("temperature", "Temperature".Tr(this)), SliderConfig.Linear(1, 0, 2));
        // 0 = 不发送 max_tokens，由服务端用默认上限。
        properties.Add(("max_tokens", "Max Tokens (0=auto)".Tr(this)), SliderConfig.Integer(0, 0, 32768));
        return ObjectConfig.Create(properties);
    }

    public void Init() { }

    public void Destroy() { }

    public IAgentModelSession CreateSession(PropertyObject properties)
    {
        var baseUrl = properties.GetString("base_url", "https://api.openai.com/v1");
        var apiKey = properties.GetString("api_key", "");
        var model = properties.GetString("model", "gpt-4o-mini");
        var providerId = properties.GetString("provider_id", AgentProviderCatalog.DefaultProvider.Id);
        var provider = AgentProviderCatalog.Resolve(providerId);
        var family = AgentProviderCatalog.ParseFamily(properties.GetString("endpoint_family", AgentProviderCatalog.FamilyId(provider.Family)));
        var temperature = properties.GetDouble("temperature", 1);
        var maxTokens = (int)properties.GetDouble("max_tokens", 0);
        var supportedInput = ResolveSupportedInput(provider.Id, baseUrl, apiKey, family, model);
        return family switch
        {
            AgentEndpointFamily.AnthropicMessages => new AnthropicMessagesSession(baseUrl, apiKey, model, temperature, maxTokens, supportedInput),
            AgentEndpointFamily.GeminiGenerateContent => new GeminiGenerateContentSession(baseUrl, apiKey, model, temperature, maxTokens, supportedInput),
            AgentEndpointFamily.Ollama => new OpenAICompatibleSession(baseUrl.TrimEnd('/') + "/v1", apiKey, model, temperature, maxTokens, supportedInput),
            _ => new OpenAICompatibleSession(baseUrl, apiKey, model, temperature, maxTokens, supportedInput),
        };
    }

    static string DefaultModel(AgentEndpointFamily family) => family switch
    {
        AgentEndpointFamily.AnthropicMessages => "claude-3-5-sonnet-latest",
        AgentEndpointFamily.GeminiGenerateContent => "gemini-1.5-flash",
        AgentEndpointFamily.Ollama => "llama3.2",
        _ => "gpt-4o-mini",
    };

    static AgentModality ResolveSupportedInput(string providerId, string baseUrl, string apiKey, AgentEndpointFamily family, string model)
    {
        var cached = AgentModelDiscoveryService.GetCached(providerId, baseUrl, apiKey, family);
        var match = cached?.FirstOrDefault(m => m.Id == model);
        return match?.SupportedInput ?? AgentModelDiscoveryService.InferSupportedInput(model, family);
    }
}
