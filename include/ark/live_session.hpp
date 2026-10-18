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
    LiveSessionBridge();
    ~LiveSessionBridge();
    LiveSessionBridge(const LiveSessionBridge&) = delete;
    LiveSessionBridge& operator=(const LiveSessionBridge&) = delete;
    bool start(Dispatch dispatch, std::string name = {});
    void stop() noexcept;
    std::string endpoint() const;
    std::string lastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Throws on transport failure; NEVER falls back to another document. A timeout
// after dispatch has an unknown outcome: inspect revision before retrying writes.
arkjson::Value callLiveSession(const std::string& endpoint, const std::string& tool,
                              const arkjson::Value& args, unsigned timeoutMs = 30000);
}
