#pragma once
#include "ark/assistant.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace ark {
enum class ClaudeAuthPhase071 { Idle, Login, Checking, Ready, NotConnected, Failed, Cancelled };
struct ClaudeAuthState071 {
    ClaudeAuthPhase071 phase=ClaudeAuthPhase071::Idle;
    bool running=false,interactiveLogin=false,loggedIn=false,sdkReady=false;
    bool externalCredentialOverride=false,renewedSavedLogin=false;
    int loginExitCode=-1;
    unsigned generation=0;
    std::string authMethod,message;
};
struct ClaudeAuthProcessResult071 {int exitCode=-1;bool cancelled=false;std::string output;};
// Offline process seams. The production path executes only the configured
// official native CLI; none of these calls reads a credential store or prompts
// a model. Status output is transient and never copied into a UI receipt.
using ClaudeAuthRunner071=std::function<ClaudeAuthProcessResult071(const std::string&,const std::vector<std::string>&,bool,const std::atomic_bool*)>;
using ClaudeAuthProbe071=std::function<arkjson::Value(const AssistantConfig&,const std::atomic_bool*)>;
struct ClaudeAuthDependencies071 {ClaudeAuthRunner071 run;ClaudeAuthProbe071 probe;};
class AssistantClaudeAuth071 {
public:
    explicit AssistantClaudeAuth071(ClaudeAuthDependencies071 dependencies={});
    ~AssistantClaudeAuth071();
    AssistantClaudeAuth071(const AssistantClaudeAuth071&)=delete;
    AssistantClaudeAuth071& operator=(const AssistantClaudeAuth071&)=delete;
    // Interactive login is permitted only after an explicit human action.
    // Caller must defer while an assistant/team CAD task is in flight.
    bool start(const AssistantConfig&,bool interactiveLogin);
    ClaudeAuthState071 snapshot() const;
    void cancel() noexcept;
    void shutdown() noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
