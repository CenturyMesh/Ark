#pragma once
#include "ark/assistant.hpp"
#include <chrono>
#include <memory>
#include <vector>

namespace ark {
enum class AgentTeamDiscipline { General, Orchestrator, PartDesigner, PhysicsReviewer };
const char* agentTeamDisciplineName(AgentTeamDiscipline value) noexcept;
struct AgentTeamRole {
    std::string id, label, instructions;
    AssistantConfig assistant; // Explicit model and effort per role.
    AssistantTransport transport; // Optional offline/local connector per role.
    bool canWrite=false;
    AgentTeamDiscipline discipline=AgentTeamDiscipline::General;
};
struct AgentTeamConfig {
    std::vector<AgentTeamRole> roles;
    unsigned maxWorkers=8, maxTurnsPerRole=0; // Zero means no turn budget.
    bool orchestratedStart=false; // Specialists await explicit assignments.
    bool focusedWorkflow=false; // One conversation at a time; the lead starts and delegates specific tasks.
    std::size_t maxEvents=2048, maxMailboxBytes=64*1024;
    std::size_t maxEventTextBytes=4096, maxEventBytes=1024*1024;
    unsigned maxCommandsPerStep=8,maxFeaturesPerStep=8,maxPlacementsPerStep=8,maxProposals=64;
    std::size_t maxDraftSketchBytes=16*1024;
};
enum class AgentTeamState { Queued, Working, Idle, Failed, Cancelled, AwaitingContinuation, Interrupted };
const char* agentTeamStateName(AgentTeamState state) noexcept;
struct AgentTeamEvent {
    enum Kind { Status, Text, ToolStart, ToolEnd, Usage, Message, Error,
        ConstructionStart, ConstructionEnd, Proposal, Yield, ConstructionQueued } kind=Status;
    std::uint64_t sequence=0, elapsedMs=0;
    std::string roleId, text, tool;
    AssistantUsage usage; // Role lifetime snapshot, never a model estimate.
    AssistantPerformance performance;
    bool providerRetry=false;
    bool toolError=false; // ToolEnd: actual executor/permission result.
    std::string stepId,proposalId;
    std::vector<std::string> operations,sketchNames,featureNames;
    std::vector<std::uint64_t> createdIds;
    std::uint64_t expectedRevision=0,documentRevision=0;
    bool hasExpectedRevision=false,hasDocumentRevision=false,draft=false;
    // Actual requested sketch intent only, bounded and uncommitted. UI renders
    // this distinctly as a draft and never uses it as evaluated CAD authority.
    arkjson::Value draftSketchCommands=arkjson::Value::makeArr();
};
struct AgentTeamRoleSnapshot {
    std::string id,label,model,reasoningEffort,status,text,error;
    AgentTeamState state=AgentTeamState::Idle;
    bool canWrite=false;
    unsigned turns=0;
    std::uint64_t stepsApplied=0;
    bool needsInspection=false;
    AgentTeamDiscipline discipline=AgentTeamDiscipline::General;
    AssistantUsage usage;
    AssistantPerformance performance;
    arkjson::Value conversation=arkjson::Value::makeArr();
};
struct AgentTeamProposalSnapshot {
    std::string id,fromId,toId,reason,state;
    std::uint64_t expectedRevision=0;
    std::vector<std::string> operations;
};
struct AgentTeamSnapshot {
    bool running=false, paused=false, cancelled=false;
    std::uint64_t droppedEvents=0;
    std::vector<AgentTeamRoleSnapshot> roles;
    std::vector<AgentTeamEvent> events;
    AssistantUsage usage;
    AssistantPerformance performance;
    std::vector<AgentTeamProposalSnapshot> proposals;
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
    // Empty target selects the lead in focused mode, broadcasts in parallel
    // mode. "all" explicitly broadcasts. Messages reach active roles before their next
    // provider request and schedule a new turn for idle roles. False means no
    // eligible recipient, cancellation, or a configured message/turn limit.
    bool steer(const std::string& text,const std::string& targetId={});
    bool send(const std::string& fromId,const std::string& toId,const std::string& text);
    // Resumes yielded/interrupted roles with retained receipts, never replays
    // old mutations. A successful current doc_info is required before writing.
    bool resume(const std::string& targetId={});
    // Valid only during a team worker callback, empty on other threads. Hosts
    // copy this id onto queued UI/session work for accurate progress attribution.
    static std::string currentRoleId();
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
