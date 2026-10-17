#pragma once
#include "ark/json.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <stdexcept>
#include <optional>
#include <vector>
#include <array>
#include <memory>

namespace ark {
enum class AssistantAuthMode { ApiKey, ChatGptPlan, CliAccount };
enum class AssistantProtocol { Responses, ChatCompletions, Messages, Acp };
class AssistantAcpChannel;
enum class AssistantImageMode { InspireDesign, ImageToCAD };
enum class AssistantImageDecoder { Platform, Portable };
const char* assistantImageModeName(AssistantImageMode mode) noexcept;
// User-owned bytes only: no path or remotely fetched image enters provider history.
struct AssistantImageAttachment {
    std::string name,mimeType,base64;
    unsigned width=0,height=0,thumbnailWidth=0,thumbnailHeight=0;
    std::vector<unsigned char> thumbnailRGBA;
};
struct AssistantImageDimensions {std::optional<double> width,height,depth;};
AssistantImageDimensions parseAssistantImageDimensions(const std::array<std::string,3>& fields);
AssistantImageAttachment assistantImageAttachment(const std::vector<unsigned char>& bytes,const std::string& name,
    AssistantImageDecoder decoder=AssistantImageDecoder::Platform);
AssistantImageAttachment loadAssistantImageAttachment(const std::string& path);
arkjson::Value assistantUserMessage(const std::string& text,
    const std::optional<AssistantImageAttachment>& image={},AssistantImageMode mode=AssistantImageMode::InspireDesign,
    const AssistantImageDimensions& dimensions={});
void validateAssistantUserMessage(const arkjson::Value& message);
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
    bool memoryPrefetch=false; // Studio opts in; separate untrusted startup data, not a static guide.
    bool imageInputSupported=true; // Host capability override; false rejects pixels explicitly.
    unsigned timeoutSeconds=90;
    std::size_t maxResponseBytes=4*1024*1024;
    std::size_t maxConversationBytes=8*1024*1024; // Outgoing request bound; original host evidence is retained separately.
    // Offline protocol seam; never persisted. Native ACP uses the CLI's own login.
    std::function<std::unique_ptr<AssistantAcpChannel>()> acpChannelFactory;
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
    std::uint64_t originalHistoryBytes=0,instructionBytes=0,messageBytes=0,toolResultBytes=0,projectedRequests=0;
    std::uint64_t lastOriginalHistoryBytes=0,lastInstructionBytes=0,lastMessageBytes=0,lastToolResultBytes=0;
};
void accumulateAssistantPerformance(AssistantPerformance& total,const AssistantPerformance& delta);
arkjson::Value assistantContextMetrics(const AssistantPerformance&);
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
// One synchronous run's user mailbox. Owns real user image bytes; enqueue is
// atomic and validates the connector/request bound before accepting input.
// UI hosts copy requestVersion onto queued tools, then call current again at
// execution time. Geometry already executing is allowed to finish.
class AssistantSteering {
public:
    AssistantSteering(const AssistantConfig& config,const arkjson::Value& tools,const arkjson::Value& history);
    ~AssistantSteering();
    AssistantSteering(const AssistantSteering&)=delete;
    AssistantSteering& operator=(const AssistantSteering&)=delete;
    bool enqueue(const arkjson::Value& userMessage);
    arkjson::Value inbox(); // FIFO; called immediately before each provider request.
    void checkpoint(const arkjson::Value& history);
    std::uint64_t requestVersion() const;
    bool current(std::uint64_t requestVersion) const;
    bool finish(); // Closes only when all accepted steering was consumed.
    void cancel() noexcept; // Rejects new input; retained pending evidence remains available.
    arkjson::Value pending() const;
    arkjson::Value history() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Explicit readOnlyHint is required. Unknown tools are conservatively writes;
// tool_feedback is an audit receipt and can still resolve earlier activity.
bool assistantToolReadOnly(const arkjson::Value& tools,const std::string& name,const arkjson::Value& args={});
// Local transport preflight while an in-flight history can still have an
// unanswered call batch. Discards only those incomplete calls in the temporary
// validation copy; retained evidence is never rewritten or completed by inference.
void validateAssistantSteeringInput(const AssistantConfig& config,const arkjson::Value& history,const arkjson::Value& tools);
// Injectable offline transport. Native transport reads credentials internally,
// only after validation. Neither requests nor callbacks contain the API key.
using AssistantTransport=std::function<arkjson::Value(const AssistantConfig&,const arkjson::Value&,const std::atomic_bool*)>;
// Host pause gate plus deterministic offline wait/jitter seams. Runtime policy
// remains three retries for pre-output HTTP 500/502/503/504; no turn/token budget.
struct AssistantRetryHooks {
    std::function<void()> checkpoint;
    std::function<void(std::uint64_t,const std::atomic_bool*)> wait;
    std::function<unsigned()> jitterMilliseconds;
    std::function<arkjson::Value()> completionCheck; // Host finalization gate.
    std::function<bool()> requestCurrent; // Steering invalidates old writes/final replies.
    std::function<bool()> acceptCompletion; // Atomic host close; false continues pending input.
    std::function<bool()> inputPending; // Final-only gate; passive teammate context need not wake a finished turn.
};

void validateAssistantConfig(const AssistantConfig& config);
bool assistantKeyAvailable(const AssistantConfig& config);
arkjson::Value assistantFunctionTools(const arkjson::Value& mcpTools);
arkjson::Value assistantRequest(const AssistantConfig& config,const arkjson::Value& conversation,const arkjson::Value& mcpTools,bool* historyProjected=nullptr);
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
