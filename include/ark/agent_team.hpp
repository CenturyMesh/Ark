#pragma once
#include "ark/assistant.hpp"
#include <chrono>
#include <memory>
#include <vector>

namespace ark {
struct AgentTeamRole {
    std::string id, label, instructions;
    AssistantConfig assistant; // Explicit model and effort per role.
    AssistantTransport transport; // Optional offline/local connector per role.
    bool canWrite=false; // Reviewers/designers can propose via team_send.
};
struct AgentTeamConfig {
    std::vector<AgentTeamRole> roles;
    unsigned maxWorkers=3, maxTurnsPerRole=8;
    std::size_t maxEvents=2048, maxMailboxBytes=64*1024;
    std::size_t maxEventTextBytes=4096, maxEventBytes=1024*1024;
};
enum class AgentTeamState { Queued, Working, Idle, Failed, Cancelled };
const char* agentTeamStateName(AgentTeamState state) noexcept;
struct AgentTeamEvent {
    enum Kind { Status, Text, ToolStart, ToolEnd, Usage, Message, Error } kind=Status;
    std::uint64_t sequence=0, elapsedMs=0;
    std::string roleId, text, tool;
    AssistantUsage usage; // Role lifetime snapshot, never a model estimate.
    bool toolError=false; // ToolEnd: actual executor/permission result.
};
struct AgentTeamRoleSnapshot {
    std::string id,label,model,reasoningEffort,status,text,error;
    AgentTeamState state=AgentTeamState::Idle;
    bool canWrite=false;
    unsigned turns=0;
    AssistantUsage usage;
    arkjson::Value conversation=arkjson::Value::makeArr();
};
struct AgentTeamSnapshot {
    bool running=false, paused=false, cancelled=false;
    std::uint64_t droppedEvents=0;
    std::vector<AgentTeamRoleSnapshot> roles;
    std::vector<AgentTeamEvent> events;
    AssistantUsage usage;
};
using AgentTeamEvents=std::function<void(const AgentTeamEvent&)>;

// A bounded pool of real runAssistant conversations. Calls to a shared document
// executor are serialized; the host still marshals them to its owning UI thread.
// All document mutations require the MODEL'S expected_revision, checked by that
// executor; no automatic rebasing/retry. Missing readOnlyHint is treated as write.
// Steering invalidates writes from an already in-flight response until the next
// provider request consumes it. Already executing geometry is not interrupted.
// Observer callbacks may run concurrently, outside locks; exceptions are ignored.
// They may inspect/cancel/steer but must not start, waitIdle or destroy this team.
class AgentTeam {
public:
    AgentTeam();
    ~AgentTeam();
    AgentTeam(const AgentTeam&)=delete;
    AgentTeam& operator=(const AgentTeam&)=delete;
    void start(const AgentTeamConfig& config,const std::string& prompt,
        const arkjson::Value& mcpTools,const AssistantTool& executor,
        const AgentTeamEvents& observer={});
    // Empty target broadcasts; messages reach active roles before their next
    // provider request and schedule a new turn for idle roles. False means no
    // eligible recipient, cancellation, or a configured message/turn limit.
    bool steer(const std::string& text,const std::string& targetId={});
    bool send(const std::string& fromId,const std::string& toId,const std::string& text);
    void pause(bool paused=true);
    void cancel() noexcept;
    // Waits for active/pending turns (including cancellation cleanup), not for
    // persistent idle workers. Do not call on the UI thread serving executor.
    void waitIdle();
    bool waitIdleFor(std::chrono::milliseconds timeout);
    // Conversation history is intentionally opt-in: UI polling must not copy
    // multi-megabyte provider histories. sinceEvent avoids repeated log copies.
    AgentTeamSnapshot snapshot(bool includeConversations=false,std::uint64_t sinceEvent=0) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
