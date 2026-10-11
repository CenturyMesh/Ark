#pragma once
#include "ark/json.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <stdexcept>
#include <optional>

namespace ark {
enum class AssistantAuthMode { ApiKey, ChatGptPlan };
enum class AssistantProtocol { Responses, ChatCompletions, Messages };
// Configuration contains an environment-variable NAME, never an API key.
struct AssistantConfig {
    AssistantAuthMode authMode=AssistantAuthMode::ApiKey;
    AssistantProtocol protocol=AssistantProtocol::Responses;
    std::string provider="openai"; // Preset identity; not credentials or a model choice.
    std::string endpoint="https://api.openai.com/v1/responses";
    std::string model; // Explicit user choice; no implicit paid model.
    std::string reasoningEffort; // Empty: provider default; explicit values are model-dependent.
    std::string apiKeyEnv="OPENAI_API_KEY"; // Empty only for unauthenticated local servers.
    unsigned maxOutputTokens=8192; // Required per-response max_tokens for Messages; not a run budget.
    unsigned maxRounds=0; // Zero: continue until completion/cancel; optional bounded test/host mode.
    std::string onboardingPack; // Static source-owned guide, inserted once per conversation.
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
// Measured local wall time and serialized request bytes, separate from provider
// token receipts. Tool time includes the host's queue/serialization wait.
struct AssistantPerformance {
    std::uint64_t providerRequests=0,providerMilliseconds=0,toolCalls=0,toolMilliseconds=0;
    std::uint64_t requestBytes=0,inputBytes=0,toolSchemaBytes=0,maxRequestBytes=0;
    std::uint64_t lastProviderMilliseconds=0,lastToolMilliseconds=0;
    std::uint64_t lastRequestBytes=0,lastInputBytes=0,lastToolSchemaBytes=0;
};
void accumulateAssistantPerformance(AssistantPerformance& total,const AssistantPerformance& delta);
struct AssistantEvent {
    enum Kind { Status, Text, ToolStart, ToolEnd, Usage } kind=Status;
    std::string text; // Tool events contain only tool name; never credentials.
    AssistantUsage usage; // Usage events: cumulative snapshot for this run.
    AssistantPerformance performance; // Usage/ToolEnd: cumulative measured snapshot for this run.
    bool providerRetry=false; // Status: transient transport recovery, suitable for the visible log.
    // Usage: bounded categorical transport/envelope evidence; no provider body,
    // model-generated arguments, text, request IDs or credentials.
    arkjson::Value providerOutcome;
};
struct AssistantResult {
    arkjson::Value conversation=arkjson::Value::makeArr();
    std::string text;
    unsigned rounds=0;
    AssistantUsage usage;
    AssistantPerformance performance;
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
// Typed HTTP rejection before any usable response output. Never use this for
// incomplete streams, timeouts or failures after output has begun.
struct AssistantHttpError : std::runtime_error {
    unsigned statusCode;
    bool safeToRetry;
    std::optional<std::uint64_t> retryAfterMilliseconds;
    std::string errorCode,requestId,label,errorParam,failureCategory;
    AssistantHttpError(unsigned status,bool retryable,std::optional<std::uint64_t> retryAfter={},
        std::string code={},std::string id={},std::string source="Assistant provider",std::string param={},std::string category="http_failure");
};
std::optional<std::uint64_t> assistantRetryAfterMilliseconds(const std::string& header,std::int64_t nowUnix=-1);
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
// Host pause gate plus deterministic offline wait/jitter seams. Runtime policy
// remains three retries for pre-output HTTP 500/502/503/504; no turn/token budget.
struct AssistantRetryHooks {
    std::function<void()> checkpoint;
    std::function<void(std::uint64_t,const std::atomic_bool*)> wait;
    std::function<unsigned()> jitterMilliseconds;
};

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
    const AssistantHistory& history={},const AssistantRetryHooks& retryHooks={});
}
