#pragma once
#include "ark/assistant.hpp"
#include <string>
#include <vector>
namespace ark {
// Claude Code CLI session over its stream-json control protocol. ARK runs the
// user's installed, unmodified CLI and serves the role's tools as an in-process
// SDK MCP server. The CLI authenticates as the user configured it; ARK never
// reads, stores or supplies Claude credentials.
void validateAssistantClaudeCodeConfig(const AssistantConfig&);
bool assistantClaudeCodeAvailable(const AssistantConfig&);
// "claude" resolves to the native CLI on PATH, in ~/.local/bin or in the newest
// VS Code extension; an explicit path must name the native executable.
std::string assistantClaudeCodeExecutable(const AssistantConfig&);
// Arguments after the executable. Built-in tools, slash commands, session
// persistence and other MCP servers are disabled; only role tools are allowed.
// Authentication options are left untouched.
std::vector<std::string> assistantClaudeCodeArguments(const AssistantConfig&,const arkjson::Value& roleTools,const std::string& systemPromptFile);
// False only for a parent Claude Code session marker; credentials pass unchanged.
bool assistantClaudeCodeInheritsEnvironment(const std::string& name);
// initialize control request only: no user message or inference. Returns the
// CLI's model catalog and auth route, never the account e-mail.
arkjson::Value assistantClaudeCodeProbe(const AssistantConfig&);
std::vector<std::string> assistantClaudeCodeModels(const AssistantConfig&);
AssistantResult runAssistantClaudeCode(const AssistantConfig&,const arkjson::Value& input,
    const arkjson::Value& tools,const AssistantTool&,const AssistantEvents& events={},
    const std::atomic_bool* cancel=nullptr,const AssistantInbox& inbox={},
    const AssistantHistory& history={},const AssistantRetryHooks& hooks={});
}
