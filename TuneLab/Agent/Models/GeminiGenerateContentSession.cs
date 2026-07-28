using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;

namespace TuneLab.Agent.Models;

internal sealed class GeminiGenerateContentSession : IAgentModelSession, IAgentThinkingLevelSession
{
    public GeminiGenerateContentSession(string baseUrl, string apiKey, string model, double temperature, int maxTokens, AgentModality supportedInput)
    {
        var modelPath = model.StartsWith("models/", StringComparison.Ordinal) ? model : "models/" + model;
        mEndpoint = baseUrl.TrimEnd('/') + "/v1beta/" + modelPath + ":generateContent";
        if (!string.IsNullOrEmpty(apiKey))
            mEndpoint += "?key=" + Uri.EscapeDataString(apiKey);
        mTemperature = temperature;
        mMaxTokens = maxTokens;
        mSupportedInput = supportedInput;
        mSupportsThinkingLevel = IsThinkingLevelModel(model);
        mHttp = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
    }

    public AgentModality SupportedInput => mSupportedInput;
    public bool SupportsThinkingLevel => mSupportsThinkingLevel;
    public AgentThinkingLevel ThinkingLevel { get; set; } = AgentThinkingLevel.Auto;

    public async Task<AgentModelReply> SendAsync(AgentModelRequest request, CancellationToken cancellationToken)
    {
        var body = BuildRequestBody(request).ToJsonString();
        using var content = new StringContent(body, Encoding.UTF8, "application/json");
        using var response = await mHttp.PostAsync(mEndpoint, content, cancellationToken);
        var text = await response.Content.ReadAsStringAsync(cancellationToken);
        if (!response.IsSuccessStatusCode)
            throw new Exception(string.Format("Model request failed ({0}): {1}", (int)response.StatusCode, text));
        return ParseReply(text);
    }

    JsonObject BuildRequestBody(AgentModelRequest request)
    {
        var contents = new JsonArray();
        string? system = null;
        var toolCallNames = new Dictionary<string, string>();
        foreach (var message in request.Messages)
        {
            if (message.Role == AgentRole.System)
            {
                system = string.IsNullOrEmpty(system) ? message.Content : system + "\n\n" + message.Content;
                continue;
            }
            contents.Add(BuildContent(message, toolCallNames, mThoughtSignatures));
        }

        var generationConfig = new JsonObject { ["temperature"] = mTemperature };
        if (mMaxTokens > 0)
            generationConfig["maxOutputTokens"] = mMaxTokens;
        if (mSupportsThinkingLevel && ThinkingLevel != AgentThinkingLevel.Auto)
        {
            generationConfig["thinkingConfig"] = new JsonObject
            {
                ["thinkingLevel"] = ThinkingLevelId(ThinkingLevel),
            };
        }

        var body = new JsonObject
        {
            ["contents"] = contents,
            ["generationConfig"] = generationConfig,
        };
        if (!string.IsNullOrEmpty(system))
            body["systemInstruction"] = new JsonObject { ["parts"] = new JsonArray { new JsonObject { ["text"] = system } } };

        if (request.Tools.Count > 0)
        {
            var declarations = new JsonArray();
            foreach (var tool in request.Tools)
            {
                declarations.Add(new JsonObject
                {
                    ["name"] = tool.Name,
                    ["description"] = tool.Description,
                    ["parameters"] = ToGeminiSchema(tool.ParametersJsonSchema),
                });
            }
            body["tools"] = new JsonArray { new JsonObject { ["functionDeclarations"] = declarations } };
        }

        return body;
    }

    static JsonNode? ToGeminiSchema(string jsonSchema)
    {
        var node = JsonNode.Parse(jsonSchema);
        return SanitizeGeminiSchema(node);
    }

    static JsonNode? SanitizeGeminiSchema(JsonNode? node)
    {
        if (node is JsonArray array)
        {
            var next = new JsonArray();
            foreach (var item in array)
                next.Add(SanitizeGeminiSchema(item));
            return next;
        }

        if (node is not JsonObject obj)
            return node?.DeepClone();

        var result = new JsonObject();
        foreach (var kv in obj)
        {
            if (IsUnsupportedGeminiSchemaKey(kv.Key))
                continue;

            // Gemini does not accept JSON Schema union type arrays. Preserve the
            // non-null type and mark it nullable when the source allowed null.
            if (kv.Key == "type" && kv.Value is JsonArray typeArray)
            {
                string? nonNullType = null;
                bool nullable = false;
                foreach (var item in typeArray)
                {
                    var type = item?.GetValue<string>();
                    if (type == "null")
                        nullable = true;
                    else if (nonNullType == null)
                        nonNullType = type;
                }
                if (!string.IsNullOrEmpty(nonNullType))
                    result["type"] = nonNullType;
                if (nullable)
                    result["nullable"] = true;
                continue;
            }

            result[kv.Key] = SanitizeGeminiSchema(kv.Value);
        }

        return result;
    }

    static bool IsUnsupportedGeminiSchemaKey(string key)
        => key is "$schema"
            or "$id"
            or "additionalProperties"
            or "unevaluatedProperties"
            or "patternProperties"
            or "allOf"
            or "anyOf"
            or "oneOf"
            or "not"
            or "default"
            or "examples";

    static JsonObject BuildContent(AgentMessage message, Dictionary<string, string> toolCallNames, IReadOnlyDictionary<string, string> thoughtSignatures)
    {
        var role = message.Role == AgentRole.Assistant ? "model" : "user";
        var parts = new JsonArray();

        if (message.Role == AgentRole.Tool)
        {
            var response = new JsonObject
            {
                ["name"] = message.ToolCallId != null && toolCallNames.TryGetValue(message.ToolCallId, out var name) ? name : "tool",
                ["response"] = new JsonObject { ["content"] = message.Content ?? string.Empty },
            };
            if (!string.IsNullOrEmpty(message.ToolCallId))
                response["id"] = message.ToolCallId;
            parts.Add(new JsonObject { ["functionResponse"] = response });
        }
        else
        {
            if (message.Parts is { Count: > 0 } contentParts)
            {
                foreach (var part in contentParts)
                {
                    if (part.Kind == AgentContentKind.Image && part.Data is { Length: > 0 })
                    {
                        parts.Add(new JsonObject
                        {
                            ["inlineData"] = new JsonObject
                            {
                                ["mimeType"] = string.IsNullOrEmpty(part.MediaType) ? "image/png" : part.MediaType,
                                ["data"] = Convert.ToBase64String(part.Data),
                            },
                        });
                    }
                    else if (!string.IsNullOrEmpty(part.Text))
                    {
                        parts.Add(new JsonObject { ["text"] = part.Text });
                    }
                }
            }
            else if (!string.IsNullOrEmpty(message.Content))
            {
                parts.Add(new JsonObject { ["text"] = message.Content });
            }

            if (message.Role == AgentRole.Assistant && message.ToolCalls is { Count: > 0 } calls)
            {
                foreach (var call in calls)
                {
                    toolCallNames[call.Id] = call.Name;
                    var functionCall = new JsonObject
                    {
                        ["name"] = call.Name,
                        ["args"] = JsonNode.Parse(call.ArgumentsJson),
                    };
                    if (!string.IsNullOrEmpty(call.Id))
                        functionCall["id"] = call.Id;
                    var partObject = new JsonObject { ["functionCall"] = functionCall };
                    if (!string.IsNullOrEmpty(call.Id))
                        partObject["thoughtSignature"] = thoughtSignatures.TryGetValue(call.Id, out var thoughtSignature)
                            ? thoughtSignature
                            : "skip_thought_signature_validator";
                    parts.Add(partObject);
                }
            }
        }

        return new JsonObject { ["role"] = role, ["parts"] = parts };
    }

    AgentModelReply ParseReply(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        var text = new StringBuilder();
        var toolCalls = new List<AgentToolCall>();
        if (root.TryGetProperty("candidates", out var candidates) && candidates.ValueKind == JsonValueKind.Array && candidates.GetArrayLength() > 0
            && candidates[0].TryGetProperty("content", out var content)
            && content.TryGetProperty("parts", out var parts) && parts.ValueKind == JsonValueKind.Array)
        {
            foreach (var part in parts.EnumerateArray())
            {
                if (part.TryGetProperty("text", out var textPart) && textPart.ValueKind == JsonValueKind.String)
                    text.Append(textPart.GetString());
                if (part.TryGetProperty("functionCall", out var call) && call.ValueKind == JsonValueKind.Object)
                {
                    var name = GetString(call, "name") ?? string.Empty;
                    var id = GetString(call, "id") ?? Guid.NewGuid().ToString("N");
                    if (part.TryGetProperty("thoughtSignature", out var thoughtSignature) && thoughtSignature.ValueKind == JsonValueKind.String)
                    {
                        var signature = thoughtSignature.GetString();
                        if (!string.IsNullOrEmpty(signature))
                            mThoughtSignatures[id] = signature;
                    }
                    toolCalls.Add(new AgentToolCall
                    {
                        Id = id,
                        Name = name,
                        ArgumentsJson = call.TryGetProperty("args", out var args) ? args.GetRawText() : "{}",
                    });
                }
            }
        }

        if (text.Length == 0 && toolCalls.Count == 0)
            throw new Exception("Gemini returned no text or function call. " + DescribeEmptyResponse(root));

        return new AgentModelReply
        {
            Content = text.Length > 0 ? text.ToString() : null,
            ToolCalls = toolCalls,
            Usage = ParseUsage(root),
        };
    }

    static string DescribeEmptyResponse(JsonElement root)
    {
        var details = new List<string>();
        if (root.TryGetProperty("promptFeedback", out var promptFeedback))
            details.Add("promptFeedback=" + promptFeedback.GetRawText());
        if (root.TryGetProperty("candidates", out var candidates) && candidates.ValueKind == JsonValueKind.Array && candidates.GetArrayLength() > 0)
        {
            var candidate = candidates[0];
            if (candidate.TryGetProperty("finishReason", out var finishReason))
                details.Add("finishReason=" + finishReason.GetRawText());
            if (candidate.TryGetProperty("safetyRatings", out var safetyRatings))
                details.Add("safetyRatings=" + safetyRatings.GetRawText());
            if (candidate.TryGetProperty("content", out var content))
                details.Add("content=" + content.GetRawText());
        }
        if (details.Count == 0)
            details.Add("raw=" + root.GetRawText());
        return string.Join("; ", details);
    }

    static AgentTokenUsage? ParseUsage(JsonElement root)
    {
        if (!root.TryGetProperty("usageMetadata", out var usage) || usage.ValueKind != JsonValueKind.Object)
            return null;
        return new AgentTokenUsage
        {
            PromptTokens = GetInt(usage, "promptTokenCount"),
            CompletionTokens = GetInt(usage, "candidatesTokenCount"),
            TotalTokens = GetInt(usage, "totalTokenCount"),
        };
    }

    static string? GetString(JsonElement obj, string name)
        => obj.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    static int GetInt(JsonElement obj, string name)
        => obj.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Number ? value.GetInt32() : 0;

    static bool IsThinkingLevelModel(string model)
    {
        var id = model.StartsWith("models/", StringComparison.Ordinal) ? model["models/".Length..] : model;
        return id.StartsWith("gemini-3", StringComparison.OrdinalIgnoreCase);
    }

    static string ThinkingLevelId(AgentThinkingLevel level) => level switch
    {
        AgentThinkingLevel.Minimal => "minimal",
        AgentThinkingLevel.Low => "low",
        AgentThinkingLevel.Medium => "medium",
        AgentThinkingLevel.High => "high",
        _ => "medium",
    };

    public void Dispose() => mHttp.Dispose();

    readonly HttpClient mHttp;
    readonly string mEndpoint;
    readonly double mTemperature;
    readonly int mMaxTokens;
    readonly AgentModality mSupportedInput;
    readonly bool mSupportsThinkingLevel;
    readonly Dictionary<string, string> mThoughtSignatures = new();
}
