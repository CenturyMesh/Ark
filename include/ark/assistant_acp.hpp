#pragma once
#include "ark/assistant.hpp"
#include <chrono>
namespace ark {
// Newline-delimited complete JSON-RPC packets, with a bounded cancellable poll.
// This interface never exposes the CLI's credential contents to ARK.
class AssistantAcpChannel {
public:
    virtual ~AssistantAcpChannel()=default;
    virtual void send(const arkjson::Value& packet)=0;
    virtual std::optional<arkjson::Value> receive(std::chrono::milliseconds wait)=0;
};
void validateAssistantAcpConfig(const AssistantConfig&);
bool assistantAcpAvailable(const AssistantConfig&);
// initialize only: no authenticate/browser, session/new or model inference.
// Returns negotiated SDK-MCP/image support and the CLI's actual modelState.
arkjson::Value assistantAcpProbe(const AssistantConfig&);
std::vector<std::string> assistantAcpModels(const AssistantConfig&);
AssistantResult runAssistantAcp(const AssistantConfig&,const arkjson::Value& input,
    const arkjson::Value& tools,const AssistantTool&,const AssistantEvents& events={},
    const std::atomic_bool* cancel=nullptr,const AssistantInbox& inbox={},
    const AssistantHistory& history={},const AssistantRetryHooks& hooks={});
}
