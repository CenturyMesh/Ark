#pragma once
#include "ark/assistant.hpp"
#include <vector>

namespace ark {
// Versioned synthetic source only. Reference operations qualify the harness;
// they are never included in a live model's prompt.
struct AgentBenchmarkCase {
    std::string id,category,brief;
    unsigned version=1;
    arkjson::Value baseline=arkjson::Value::makeArr();
    arkjson::Value reference=arkjson::Value::makeArr();
    arkjson::Value checks=arkjson::Value::makeArr();
};
struct AgentBenchmarkSuite {
    std::string id;
    unsigned version=1;
    std::vector<AgentBenchmarkCase> cases;
};
struct AgentBenchmarkConfig {
    AssistantConfig assistant;
    unsigned repetitions=3,deadlineSeconds=120;
    std::vector<std::string> strategies={"Single","Focused","Parallel"};
    std::vector<std::string> caseIds;
    AssistantTransport offlineTransport; // Test seam only; --live rejects it.
};
AgentBenchmarkSuite parseAgentBenchmarkSuite(const arkjson::Value& value);
AgentBenchmarkConfig parseAgentBenchmarkConfig(const arkjson::Value& value);
// Evaluates every supplied baseline and reference with the production C++ CAD
// executor and returns all checks, including failed ones. No provider calls.
arkjson::Value preflightAgentBenchmark(const AgentBenchmarkSuite& suite);
// --live is a host decision, never inferred from a provider preset. The default
// is an offline scripted transport, not evidence about any LLM's performance.
arkjson::Value runAgentBenchmark(const AgentBenchmarkSuite& suite,
    const AgentBenchmarkConfig& config,bool live=false,
    const std::function<void(const arkjson::Value&)>& receipt={});
// Separate pixel-delivery smoke. No automatic visual understanding/quality pass.
arkjson::Value runAgentVisionSmoke(const arkjson::Value& syntheticCommands,
    const AgentBenchmarkConfig& config,bool live=false,const std::string& screenshotPath={});
}
