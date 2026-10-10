#pragma once
#include "ark/json.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <stdexcept>

namespace ark {
enum class AssistantAuthMode { ApiKey, ChatGptPlan };
// Configuration contains an environment-variable NAME, never an API key.
struct AssistantConfig {
    AssistantAuthMode authMode=AssistantAuthMode::ApiKey;
    std::string endpoint="https://api.openai.com/v1/responses";
    std::string model; // Explicit user choice; no implicit paid model.
    std::string reasoningEffort; // Empty: provider default; explicit values are model-dependent.
    std::string apiKeyEnv="OPENAI_API_KEY"; // Empty only for unauthenticated local servers.
    unsigned maxRounds=12;
    unsigned timeoutSeconds=90;
    std::size_t maxResponseBytes=4*1024*1024;
    std::size_t maxConversationBytes=8*1024*1024;
};
// Provider-reported usage only. Missing/invalid usage never becomes zero-cost
// evidence. Cached/reasoning counts are subsets, not extra tokens to add.
struct AssistantUsage {
    std::uint64_t inputTokens=0, outputTokens=0, totalTokens=0;
    std::uint64_t cachedInputTokens=0, reasoningTokens=0;
    unsigned reportedResponses=0, missingResponses=0;
};
void accumulateAssistantUsage(AssistantUsage& total,const AssistantUsage& delta);
std::string assistantUsageLabel(const AssistantUsage& usage);
struct AssistantEvent {
    enum Kind { Status, Text, ToolStart, ToolEnd, Usage } kind=Status;
    std::string text; // Tool events contain only tool name; never credentials.
    AssistantUsage usage; // Usage events: cumulative snapshot for this run.
};
struct AssistantResult {
    arkjson::Value conversation=arkjson::Value::makeArr();
    std::string text;
    unsigned rounds=0;
    AssistantUsage usage;
};
// A bounded turn yielded after real tool receipts. Hosts may continue the
// retained, protocol-complete history; this is not a request to replay writes.
struct AssistantRoundLimit : std::runtime_error {
    unsigned rounds;
    explicit AssistantRoundLimit(unsigned value):std::runtime_error("Assistant reached its round limit; previously applied changes remain in the document. Continue from retained receipts after inspecting the current revision."),rounds(value){}
};
struct AssistantTransportError : std::runtime_error {
    explicit AssistantTransportError(const std::string& detail):std::runtime_error(detail){}
};
using AssistantTool=std::function<arkjson::Value(const std::string&,const arkjson::Value&)>;
using AssistantEvents=std::function<void(const AssistantEvent&)>;
// Optional host messages consumed before each provider request. Return an array
// of Responses input messages, or null/empty when there is no new input. The
// messages become part of the retained conversation, including on continuation.
using AssistantInbox=std::function<arkjson::Value()>;
// Successful input/output/tool-history checkpoints, also retained by a host if
// a later round fails. A checkpoint is conversation evidence, not run success.
using AssistantHistory=std::function<void(const arkjson::Value&)>;
// Injectable offline transport. Native transport reads credentials internally,
// only after validation. Neither requests nor callbacks contain the API key.
using AssistantTransport=std::function<arkjson::Value(const AssistantConfig&,const arkjson::Value&,const std::atomic_bool*)>;

void validateAssistantConfig(const AssistantConfig& config);
bool assistantKeyAvailable(const AssistantConfig& config);
arkjson::Value assistantFunctionTools(const arkjson::Value& mcpTools);
arkjson::Value assistantRequest(const AssistantConfig& config,const arkjson::Value& conversation,const arkjson::Value& mcpTools);
// input: user prompt string, or complete Responses input array including history.
// Synchronous: call on a worker. Host serializes tool callback on its UI/session thread.
// Throws on cancellation, invalid configuration/response, transport or round limit.
// Prior successful document mutations are NOT rolled back when a later round fails.
AssistantResult runAssistant(const AssistantConfig& config,const arkjson::Value& input,
    const arkjson::Value& mcpTools,const AssistantTool& callTool,
    const AssistantEvents& events={},const std::atomic_bool* cancel=nullptr,
    const AssistantTransport& transport={},const AssistantInbox& inbox={},
    const AssistantHistory& history={});
}
