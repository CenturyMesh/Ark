#pragma once
#include "ark/assistant.hpp"
#include <vector>
namespace ark {
struct AssistantProviderPreset {
    std::string id,label,endpoint,apiKeyEnv;
    AssistantProtocol protocol=AssistantProtocol::Responses;
};
std::vector<AssistantProviderPreset> assistantProviderPresets();
AssistantConfig assistantProviderPreset(const std::string& id);
std::string assistantProtocolName(AssistantProtocol protocol);
AssistantProtocol assistantProtocolFromName(const std::string& name);
// Protocol adapters retain one canonical Responses-style history inside ARK.
// Provider-specific thinking replay is kept only on its originating route.
arkjson::Value assistantAdaptRequest(const AssistantConfig&,const arkjson::Value& responsesRequest);
arkjson::Value assistantNormalizeResponse(const AssistantConfig&,const arkjson::Value& providerResponse);
// Read-only GET /models, no inference. Returns actual IDs without choosing one.
std::vector<std::string> assistantProviderModels(const AssistantConfig&,const AssistantTransport& offlineTransport={});
// Read-only Ollama /api/show metadata for the chosen installed model; no inference
// or downloads. Restricted to credential-free loopback, even with an injected transport.
arkjson::Value assistantLocalModelInfo(const AssistantConfig&,const AssistantTransport& offlineTransport={});
std::vector<std::string> assistantLocalModels(const AssistantConfig&,const AssistantTransport& offlineTransport={});
// Pure export: never edits a client's global configuration or reads credentials.
// targets: codex, grok-build, claude-code, claude-desktop, opencode.
std::string assistantExternalMcpConfig(const std::string& target,const std::string& executablePath,
    const std::string& pipeEndpoint,const AssistantConfig& provider={});
}
