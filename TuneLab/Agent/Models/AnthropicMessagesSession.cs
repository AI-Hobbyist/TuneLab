using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;

namespace TuneLab.Agent.Models;

internal sealed class AnthropicMessagesSession : IAgentModelSession, IAgentThinkingLevelSession
{
    public const string ApiVersion = "2023-06-01";

    public AnthropicMessagesSession(string baseUrl, string apiKey, string model, double temperature, int maxTokens, AgentModality supportedInput)
    {
        mEndpoint = baseUrl.TrimEnd('/') + "/v1/messages";
        mModel = model;
        mTemperature = temperature;
        mMaxTokens = maxTokens > 0 ? maxTokens : 4096;
        mSupportedInput = supportedInput;
        mSupportsThinkingLevel = IsAdaptiveThinkingModel(model);
        mHttp = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
        if (!string.IsNullOrEmpty(apiKey))
            mHttp.DefaultRequestHeaders.Add("x-api-key", apiKey);
        mHttp.DefaultRequestHeaders.Add("anthropic-version", ApiVersion);
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
        var messages = new JsonArray();
        string? system = null;
        foreach (var message in request.Messages)
        {
            if (message.Role == AgentRole.System)
            {
                system = string.IsNullOrEmpty(system) ? message.Content : system + "\n\n" + message.Content;
                continue;
            }
            messages.Add(BuildMessage(message));
        }

        var body = new JsonObject
        {
            ["model"] = mModel,
            ["max_tokens"] = mMaxTokens,
            ["temperature"] = mTemperature,
            ["messages"] = messages,
        };
        if (!string.IsNullOrEmpty(system))
            body["system"] = system;
        if (mSupportsThinkingLevel && ThinkingLevel != AgentThinkingLevel.Auto)
        {
            body["thinking"] = new JsonObject { ["type"] = "adaptive" };
            body["output_config"] = new JsonObject { ["effort"] = ThinkingLevelId(ThinkingLevel) };
        }

        if (request.Tools.Count > 0)
        {
            var tools = new JsonArray();
            foreach (var tool in request.Tools)
            {
                tools.Add(new JsonObject
                {
                    ["name"] = tool.Name,
                    ["description"] = tool.Description,
                    ["input_schema"] = JsonNode.Parse(tool.ParametersJsonSchema),
                });
            }
            body["tools"] = tools;
        }

        return body;
    }

    static JsonObject BuildMessage(AgentMessage message)
    {
        if (message.Role == AgentRole.Tool)
        {
            return new JsonObject
            {
                ["role"] = "user",
                ["content"] = new JsonArray
                {
                    new JsonObject
                    {
                        ["type"] = "tool_result",
                        ["tool_use_id"] = message.ToolCallId ?? string.Empty,
                        ["content"] = message.Content ?? string.Empty,
                    },
                },
            };
        }

        var role = message.Role == AgentRole.Assistant ? "assistant" : "user";
        var obj = new JsonObject { ["role"] = role };
        var content = new JsonArray();

        if (message.Parts is { Count: > 0 } parts)
        {
            foreach (var part in parts)
            {
                if (part.Kind == AgentContentKind.Image && part.Data is { Length: > 0 })
                {
                    content.Add(new JsonObject
                    {
                        ["type"] = "image",
                        ["source"] = new JsonObject
                        {
                            ["type"] = "base64",
                            ["media_type"] = string.IsNullOrEmpty(part.MediaType) ? "image/png" : part.MediaType,
                            ["data"] = Convert.ToBase64String(part.Data),
                        },
                    });
                }
                else if (!string.IsNullOrEmpty(part.Text))
                {
                    content.Add(new JsonObject { ["type"] = "text", ["text"] = part.Text });
                }
            }
        }
        else if (!string.IsNullOrEmpty(message.Content))
        {
            content.Add(new JsonObject { ["type"] = "text", ["text"] = message.Content });
        }

        if (message.Role == AgentRole.Assistant && message.ToolCalls is { Count: > 0 } calls)
        {
            foreach (var call in calls)
            {
                content.Add(new JsonObject
                {
                    ["type"] = "tool_use",
                    ["id"] = call.Id,
                    ["name"] = call.Name,
                    ["input"] = JsonNode.Parse(call.ArgumentsJson),
                });
            }
        }

        obj["content"] = content;
        return obj;
    }

    static AgentModelReply ParseReply(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        var text = new StringBuilder();
        var reasoning = new StringBuilder();
        var toolCalls = new List<AgentToolCall>();
        if (root.TryGetProperty("content", out var content) && content.ValueKind == JsonValueKind.Array)
        {
            foreach (var part in content.EnumerateArray())
            {
                var type = part.TryGetProperty("type", out var t) && t.ValueKind == JsonValueKind.String ? t.GetString() : null;
                if (type == "text" && part.TryGetProperty("text", out var textPart) && textPart.ValueKind == JsonValueKind.String)
                    text.Append(textPart.GetString());
                else if (type == "thinking" && part.TryGetProperty("thinking", out var thinkingPart) && thinkingPart.ValueKind == JsonValueKind.String)
                    reasoning.Append(thinkingPart.GetString());
                else if (type == "tool_use")
                    toolCalls.Add(new AgentToolCall
                    {
                        Id = GetString(part, "id") ?? string.Empty,
                        Name = GetString(part, "name") ?? string.Empty,
                        ArgumentsJson = part.TryGetProperty("input", out var input) ? input.GetRawText() : "{}",
                    });
            }
        }

        return new AgentModelReply
        {
            Content = text.Length > 0 ? text.ToString() : null,
            Reasoning = reasoning.Length > 0 ? reasoning.ToString() : null,
            ToolCalls = toolCalls,
            Usage = ParseUsage(root),
        };
    }

    static AgentTokenUsage? ParseUsage(JsonElement root)
    {
        if (!root.TryGetProperty("usage", out var usage) || usage.ValueKind != JsonValueKind.Object)
            return null;
        var input = GetInt(usage, "input_tokens");
        var output = GetInt(usage, "output_tokens");
        return new AgentTokenUsage { PromptTokens = input, CompletionTokens = output, TotalTokens = input + output };
    }

    static string? GetString(JsonElement obj, string name)
        => obj.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    static int GetInt(JsonElement obj, string name)
        => obj.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Number ? value.GetInt32() : 0;

    static bool IsAdaptiveThinkingModel(string model)
    {
        var id = model.ToLowerInvariant();
        return id.Contains("sonnet-5")
            || id.Contains("fable-5")
            || id.Contains("mythos")
            || id.Contains("opus-4-6")
            || id.Contains("opus-4-7")
            || id.Contains("opus-4-8")
            || id.Contains("sonnet-4-6");
    }

    static string ThinkingLevelId(AgentThinkingLevel level) => level switch
    {
        AgentThinkingLevel.Minimal => "low",
        AgentThinkingLevel.Low => "low",
        AgentThinkingLevel.Medium => "medium",
        AgentThinkingLevel.High => "high",
        _ => "medium",
    };

    public void Dispose() => mHttp.Dispose();

    readonly HttpClient mHttp;
    readonly string mEndpoint;
    readonly string mModel;
    readonly double mTemperature;
    readonly int mMaxTokens;
    readonly AgentModality mSupportedInput;
    readonly bool mSupportsThinkingLevel;
}
