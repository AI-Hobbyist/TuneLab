using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace TuneLab.Agent.Models;

internal sealed record AgentDiscoveredModel(string Id, string DisplayName, AgentModality SupportedInput);

internal static class AgentModelDiscoveryService
{
    public static IReadOnlyList<AgentDiscoveredModel>? GetCached(string providerId, string baseUrl, string apiKey, AgentEndpointFamily family)
        => sCache.TryGetValue(CacheKey(providerId, baseUrl, apiKey, family), out var models) ? models : null;

    public static async Task<IReadOnlyList<AgentDiscoveredModel>> DiscoverAsync(string providerId, string baseUrl, string apiKey, AgentEndpointFamily family, CancellationToken cancellationToken)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };
        var models = family switch
        {
            AgentEndpointFamily.AnthropicMessages => await DiscoverAnthropicAsync(http, baseUrl, apiKey, cancellationToken),
            AgentEndpointFamily.GeminiGenerateContent => await DiscoverGeminiAsync(http, baseUrl, apiKey, cancellationToken),
            AgentEndpointFamily.Ollama => await DiscoverOllamaAsync(http, baseUrl, cancellationToken),
            _ => await DiscoverOpenAIAsync(http, AgentProviderCatalog.Find(providerId)?.ModelsApiUrl, baseUrl, apiKey, cancellationToken),
        };

        var normalized = models
            .Where(m => !string.IsNullOrWhiteSpace(m.Id))
            .GroupBy(m => m.Id)
            .Select(g => g.First())
            .OrderBy(m => m.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        sCache[CacheKey(providerId, baseUrl, apiKey, family)] = normalized;
        return normalized;
    }

    public static AgentModality InferSupportedInput(string modelId, AgentEndpointFamily family)
    {
        var id = modelId.ToLowerInvariant();
        if (family == AgentEndpointFamily.GeminiGenerateContent)
            return AgentModality.Text | AgentModality.Image;
        if (id.Contains("vision") || id.Contains("-vl") || id.Contains("qwen-vl") || id.Contains("omni") || id.Contains("gpt-4o") || id.Contains("claude-3"))
            return AgentModality.Text | AgentModality.Image;
        return AgentModality.Text;
    }

    static async Task<IReadOnlyList<AgentDiscoveredModel>> DiscoverOpenAIAsync(HttpClient http, string? modelsApiUrl, string baseUrl, string apiKey, CancellationToken cancellationToken)
    {
        if (!string.IsNullOrEmpty(apiKey))
            http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", apiKey);
        var url = string.IsNullOrEmpty(modelsApiUrl) ? TrimEnd(baseUrl) + "/models" : modelsApiUrl;
        using var response = await http.GetAsync(url, cancellationToken);
        var text = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
            throw new Exception(string.Format("Model list request failed ({0}): {1}", (int)response.StatusCode, text));

        using var doc = JsonDocument.Parse(text);
        if (!doc.RootElement.TryGetProperty("data", out var data) || data.ValueKind != JsonValueKind.Array)
            return [];
        var result = new List<AgentDiscoveredModel>();
        foreach (var item in data.EnumerateArray())
        {
            var id = GetString(item, "id");
            if (string.IsNullOrEmpty(id))
                continue;
            var modality = ReadOpenRouterModality(item) ?? InferSupportedInput(id, AgentEndpointFamily.OpenAIChatCompletions);
            result.Add(new(id, id, modality));
        }
        return result;
    }

    static async Task<IReadOnlyList<AgentDiscoveredModel>> DiscoverAnthropicAsync(HttpClient http, string baseUrl, string apiKey, CancellationToken cancellationToken)
    {
        if (!string.IsNullOrEmpty(apiKey))
            http.DefaultRequestHeaders.Add("x-api-key", apiKey);
        http.DefaultRequestHeaders.Add("anthropic-version", AnthropicMessagesSession.ApiVersion);
        using var response = await http.GetAsync(TrimEnd(baseUrl) + "/v1/models", cancellationToken);
        var text = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
            throw new Exception(string.Format("Model list request failed ({0}): {1}", (int)response.StatusCode, text));
        using var doc = JsonDocument.Parse(text);
        if (!doc.RootElement.TryGetProperty("data", out var data) || data.ValueKind != JsonValueKind.Array)
            return [];
        return data.EnumerateArray()
            .Select(item => GetString(item, "id"))
            .Where(id => !string.IsNullOrEmpty(id))
            .Select(id => new AgentDiscoveredModel(id!, id!, InferSupportedInput(id!, AgentEndpointFamily.AnthropicMessages)))
            .ToArray();
    }

    static async Task<IReadOnlyList<AgentDiscoveredModel>> DiscoverGeminiAsync(HttpClient http, string baseUrl, string apiKey, CancellationToken cancellationToken)
    {
        var url = TrimEnd(baseUrl) + "/v1beta/models";
        if (!string.IsNullOrEmpty(apiKey))
            url += "?key=" + Uri.EscapeDataString(apiKey);
        using var response = await http.GetAsync(url, cancellationToken);
        var text = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
            throw new Exception(string.Format("Model list request failed ({0}): {1}", (int)response.StatusCode, text));
        using var doc = JsonDocument.Parse(text);
        if (!doc.RootElement.TryGetProperty("models", out var data) || data.ValueKind != JsonValueKind.Array)
            return [];
        return data.EnumerateArray()
            .Select(item => (Id: StripGeminiPrefix(GetString(item, "name")), Display: GetString(item, "displayName")))
            .Where(x => !string.IsNullOrEmpty(x.Id))
            .Select(x => new AgentDiscoveredModel(x.Id!, string.IsNullOrEmpty(x.Display) ? x.Id! : x.Display!, InferSupportedInput(x.Id!, AgentEndpointFamily.GeminiGenerateContent)))
            .ToArray();
    }

    static async Task<IReadOnlyList<AgentDiscoveredModel>> DiscoverOllamaAsync(HttpClient http, string baseUrl, CancellationToken cancellationToken)
    {
        using var response = await http.GetAsync(TrimEnd(baseUrl) + "/api/tags", cancellationToken);
        var text = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
            throw new Exception(string.Format("Model list request failed ({0}): {1}", (int)response.StatusCode, text));
        using var doc = JsonDocument.Parse(text);
        if (!doc.RootElement.TryGetProperty("models", out var data) || data.ValueKind != JsonValueKind.Array)
            return [];
        return data.EnumerateArray()
            .Select(item => GetString(item, "name"))
            .Where(id => !string.IsNullOrEmpty(id))
            .Select(id => new AgentDiscoveredModel(id!, id!, InferSupportedInput(id!, AgentEndpointFamily.Ollama)))
            .ToArray();
    }

    static AgentModality? ReadOpenRouterModality(JsonElement item)
    {
        if (!item.TryGetProperty("architecture", out var architecture) || architecture.ValueKind != JsonValueKind.Object)
            return null;
        if (architecture.TryGetProperty("input_modalities", out var modalities) && modalities.ValueKind == JsonValueKind.Array)
        {
            foreach (var modality in modalities.EnumerateArray())
                if (modality.ValueKind == JsonValueKind.String && modality.GetString() == "image")
                    return AgentModality.Text | AgentModality.Image;
            return AgentModality.Text;
        }
        return null;
    }

    static string? GetString(JsonElement obj, string name)
        => obj.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    static string StripGeminiPrefix(string? name)
        => string.IsNullOrEmpty(name) ? string.Empty : (name.StartsWith("models/", StringComparison.Ordinal) ? name[7..] : name);

    static string TrimEnd(string value) => value.TrimEnd('/');

    static string CacheKey(string providerId, string baseUrl, string apiKey, AgentEndpointFamily family)
        => providerId + "\n" + baseUrl.TrimEnd('/') + "\n" + family + "\n" + apiKey.GetHashCode();

    static readonly Dictionary<string, IReadOnlyList<AgentDiscoveredModel>> sCache = new();
}
