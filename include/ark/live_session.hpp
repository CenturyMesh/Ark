#pragma once
#include "ark/json.hpp"
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

namespace ark {
struct AgentTeamSnapshot;
// Human/local IPC inspection only; never part of AgentSession/agent discovery.
bool isStudioDiagnosticsTool(const std::string& tool) noexcept;
arkjson::Value studioDiagnosticsToolSchema();
arkjson::Value studioTeamDiagnostics(const AgentTeamSnapshot& snapshot,
                                    const arkjson::Value& args);
// Local current-user IPC only. The dispatcher must return promptly, posting work
// to the document's owning thread. Complete with AgentSession::call's tool result.
// Check cancellation before executing queued work; a running geometry operation
// is not interrupted. Completion remains safe after stop/destruction.
class LiveSessionBridge {
public:
    using Completion = std::function<void(arkjson::Value)>;
    using Dispatch = std::function<void(const std::string&, const arkjson::Value&,
                                        Completion, std::stop_token)>;
    // Optional short text describing the call in progress (for example the
    // feature being evaluated). Called from the bridge thread; must be thread-safe.
    using Progress = std::function<std::string()>;
    LiveSessionBridge();
    ~LiveSessionBridge();
    LiveSessionBridge(const LiveSessionBridge&) = delete;
    LiveSessionBridge& operator=(const LiveSessionBridge&) = delete;
    bool start(Dispatch dispatch, std::string name = {}, Progress progress = {});
    void stop() noexcept;
    std::string endpoint() const;
    std::string lastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Throws on transport failure; NEVER falls back to another document. A timeout
// after dispatch has an unknown outcome: inspect revision before retrying writes.
// timeoutMs bounds the whole call (1..660000); connecting is bounded at 30 s.
// onProgress, when given, asks Studio for progress notes while the call runs
// ({elapsed_ms, message?} about every 2 s); Studio builds without it send none.
arkjson::Value callLiveSession(const std::string& endpoint, const std::string& tool,
                              const arkjson::Value& args, unsigned timeoutMs = 660000,
                              const std::function<void(const arkjson::Value&)>& onProgress = {});
}
