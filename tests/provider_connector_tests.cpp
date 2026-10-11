#include "ark/assistant_connectors.hpp"
#include "ark/agent_team.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
using namespace ark;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
V json(const char* source){V value;check(arkjson::parse(source,value),"test JSON");return value;}
template<class F> void rejects(F action,const char* reason){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,reason);}
const V tools=json(R"([{"name":"doc_info","description":"Inspect current document","inputSchema":{"type":"object"}},{"name":"doc_apply","description":"Atomic checked CAD write","inputSchema":{"type":"object"}}])");
V finalChat(){return json(R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"Finished with actual evidence."}}],"usage":{"prompt_tokens":20,"completion_tokens":5,"total_tokens":25}})");}
V multiChat(){return json(R"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":"Inspect then apply the checked change.","reasoning_content":"Retained local reasoning.","tool_calls":[{"id":"inspect_1","type":"function","function":{"name":"doc_info","arguments":"{}"}},{"id":"write_2","type":"function","function":{"name":"doc_apply","arguments":"{\"expected_revision\":7,\"commands\":[{\"op\":\"param.set\",\"name\":\"width\",\"expr\":30}]}"}}]}}],"usage":{"prompt_tokens":10,"completion_tokens":5,"total_tokens":15,"prompt_tokens_details":{"cached_tokens":3},"completion_tokens_details":{"reasoning_tokens":2}}})");}
AssistantConfig configured(const std::string& id){auto config=assistantProviderPreset(id);config.model="explicit-offline-model";return config;}
V messagesCalls();
void testChat(){
    auto config=configured("ollama");int requests=0,executions=0;V retained;
    auto run=runAssistant(config,V::makeStr("Edit my document"),tools,[&](const std::string& name,const V& args){++executions;if(executions==1)check(name=="doc_info","tools execute in response order");else check(name=="doc_apply"&&args.find("expected_revision")->num==7,"revision survives chat conversion");return json(R"({"isError":false,"structuredContent":{"revision":8}})");},{},nullptr,
      [&](const auto&,const V& request,const auto*){
        check(request.find("messages")&&!request.find("input")&&!request.find("include")&&!request.find("instructions"),"chat request uses only its protocol");
        const auto& functions=request.find("tools")->arr;check(functions[0].find("function")->find("parameters")->dump()==tools.arr[0].find("inputSchema")->dump(),"chat keeps extensible tool schema");
        if(++requests==1)return multiChat();
        const auto& messages=request.find("messages")->arr;check(messages.size()==5,"one assistant multi-call message and two tool results");
        const auto& assistant=messages[2];check(assistant.find("tool_calls")->arr.size()==2&&assistant.find("reasoning_content")->str=="Retained local reasoning.","multi-call batch and reasoning replay remain intact");
        check(messages[3].find("tool_call_id")->str=="inspect_1"&&messages[4].find("tool_call_id")->str=="write_2","tool receipts retain exact IDs");return finalChat();
      },{},[&](const V& history){retained=history;});
    check(requests==2&&executions==2&&run.usage.totalTokens==40&&run.usage.cachedInputTokens==3&&run.usage.reasoningTokens==2&&run.usage.reportedResponses==2,"provider usage counted once including valid details");
    check(retained.dump()==run.conversation.dump()&&run.text.find("Finished")!=std::string::npos,"normalized history and final reply retained");
    auto grok=configured("grok");const auto request=assistantRequest(grok,run.conversation,tools);check(request.find("input")&&!request.find("include")&&request.dump().find("ark_chat_reasoning")==std::string::npos&&request.dump().find("write_2")!=std::string::npos,"switching provider retains actual receipts without foreign thinking");
    int replayWrites=0;rejects([&]{runAssistant(config,run.conversation,tools,[&](auto&,auto&){++replayWrites;return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){return multiChat();});},"old call IDs cannot replay a write");check(replayWrites==0,"replayed response executes no tools");
    auto explicitEffort=config;explicitEffort.reasoningEffort="high";check(assistantRequest(explicitEffort,json(R"([{"role":"user","content":"Inspect"}])"),tools).find("reasoning_effort")->str=="high","chat effort is passed explicitly rather than downgraded");
}
void testCompleteBeforeExecution(){auto config=configured("claude-chat");
    for(int variant=0;variant<6;++variant){int calls=0;AssistantUsage usage;V outcome;
        rejects([&]{runAssistant(config,V::makeStr("Check invalid envelope"),tools,[&](auto&,auto&){++calls;return V::makeObj();},[&](const auto& event){if(event.kind==AssistantEvent::Usage){usage=event.usage;outcome=event.providerOutcome;}},nullptr,[&](auto&,auto&,auto*){
            auto response=multiChat();V choices=*response.find("choices"),entry=choices.arr[0],reply=*entry.find("message"),toolCalls=*reply.find("tool_calls");
            if(variant==0)entry.set("finish_reason",V::makeStr("length"));
            if(variant==1){toolCalls.arr[1].set("id",V::makeStr("inspect_1"));reply.set("tool_calls",toolCalls);}
            if(variant==2){V fn=*toolCalls.arr[1].find("function");fn.set("name",V::makeStr("shell"));toolCalls.arr[1].set("function",fn);reply.set("tool_calls",toolCalls);}
            if(variant==3)entry.set("finish_reason",V::makeStr("stop"));
            if(variant==4){toolCalls.arr[1].set("type",V::makeStr("custom"));reply.set("tool_calls",toolCalls);}
            if(variant==5){V fn=*toolCalls.arr[1].find("function");fn.set("arguments",V::makeStr("{"));toolCalls.arr[1].set("function",fn);reply.set("tool_calls",toolCalls);}
            entry.set("message",reply);choices.arr[0]=entry;response.set("choices",choices);return response;
        });},"invalid/incomplete chat response rejects");check(calls==0&&usage.totalTokens==15,"entire output checked before first tool while usage remains measured");
        check(outcome.find("protocol")->str=="chat-completions","categorical provider outcome keeps actual protocol");
        if(variant==0)check(outcome.find("stop_reason")->str=="length"&&outcome.find("normalization")->str=="incomplete_finish","provider truncation retained distinctly from malformed envelope");
        else if(variant==2)check(outcome.find("response_status")->str=="completed","completed provider envelope does not bypass host tool allowlist");
        else check(outcome.find("normalization")->str=="invalid_envelope"&&outcome.find("response_status")->str=="incomplete","invalid envelope classified and no partial calls retained");
    }
    int rounds=0;auto missing=runAssistant(config,V::makeStr("Unknown usage is unknown"),tools,{}, {},nullptr,[&](auto&,auto&,auto*){++rounds;auto response=finalChat();response.set("usage",V{});return response;});check(rounds==1&&missing.usage.reportedResponses==0&&missing.usage.missingResponses==1,"missing usage never becomes a zero-cost receipt");
    auto explicitEffort=config;explicitEffort.reasoningEffort="high";rejects([&]{validateAssistantConfig(explicitEffort);},"Claude compatibility cannot silently ignore effort");
}
void testBoundedOutcomes(){
    auto config=configured("ollama");auto response=multiChat();V choices=*response.find("choices"),entry=choices.arr.front();entry.set("finish_reason",V::makeStr("sk_FAKE_PROVIDER_BODY_SECRET"));choices.arr.front()=entry;response.set("choices",choices);auto rejected=assistantNormalizeResponse(config,response);
    const auto* outcome=rejected.find("ark_provider_outcome");check(outcome&&outcome->dump().size()<256&&outcome->find("stop_reason")->str=="unknown"&&outcome->dump().find("SECRET")==std::string::npos,"unrecognized provider stop reason never enters categorical diagnostics");check(rejected.find("output")->arr.empty(),"unrecognized terminal marker retains no executable output");
    V eventOutcome;int calls=0;std::string failure;
    try{runAssistant(config,V::makeStr("Incomplete receipt"),tools,[&](auto&,auto&){++calls;return V::makeObj();},[&](const auto& event){if(event.kind==AssistantEvent::Usage)eventOutcome=event.providerOutcome;},nullptr,[&](auto&,auto&,auto*){return response;});}catch(const std::exception& error){failure=error.what();}
    check(calls==0&&eventOutcome.find("stop_reason")->str=="unknown"&&failure.find("SECRET")==std::string::npos&&failure.find("stop_reason=unknown")!=std::string::npos,"runtime failure and usage event report bounded diagnostics without provider-body text");
    auto native=configured("claude");auto truncated=messagesCalls();truncated.set("stop_reason",V::makeStr("max_tokens"));auto messages=assistantNormalizeResponse(native,truncated);check(messages.find("ark_provider_outcome")->find("stop_reason")->str=="max_tokens"&&messages.find("output")->arr.empty(),"native Messages truncation keeps stop reason without executing tools");
}
V messagesCalls(){return json(R"({"type":"message","role":"assistant","stop_reason":"tool_use","content":[{"type":"thinking","thinking":"Signed provider thinking.","signature":"offline_signature"},{"type":"text","text":"Check both tools."},{"type":"tool_use","id":"claude_read","name":"doc_info","input":{}},{"type":"tool_use","id":"claude_write","name":"doc_apply","input":{"expected_revision":7,"commands":[]}}],"usage":{"input_tokens":10,"output_tokens":4,"cache_read_input_tokens":3,"cache_creation_input_tokens":2}})");}
V messagesFinal(){return json(R"({"type":"message","role":"assistant","stop_reason":"end_turn","content":[{"type":"text","text":"Done."}],"usage":{"input_tokens":15,"output_tokens":5}})");}
void testMessages(){auto config=configured("claude");int requests=0,calls=0;
    const auto run=runAssistant(config,V::makeStr("Inspect assembly"),tools,[&](auto&,auto&){++calls;return json(R"({"isError":false})");},{},nullptr,[&](auto&,const V& request,auto*){
        check(request.find("system")&&request.find("max_tokens")->num==8192&&request.find("tools")->arr[0].find("input_schema"),"Messages request uses required native contracts");
        check(!request.find("parallel_tool_calls")&&!request.find("reasoning_effort")&&!request.find("include"),"Messages has no ignored OpenAI controls");
        if(++requests==1)return messagesCalls();const auto& entries=request.find("messages")->arr;check(entries.size()==3,"Messages merges two tool receipts into one user turn");const auto& assistant=entries[1].find("content")->arr;check(assistant[0].find("signature")->str=="offline_signature"&&assistant[2].find("id")->str=="claude_read"&&assistant[3].find("id")->str=="claude_write","signed thinking and both calls replay intact");const auto& results=entries[2].find("content")->arr;check(results.size()==2&&results[0].find("tool_use_id")->str=="claude_read"&&results[1].find("tool_use_id")->str=="claude_write","native tool receipt IDs match exactly");return messagesFinal();
    });check(calls==2&&requests==2&&run.usage.totalTokens==39&&run.usage.cachedInputTokens==3&&run.usage.reportedResponses==2,"Messages usage counts uncached, cache-read and creation once");
    auto ollama=configured("ollama");auto switched=assistantRequest(ollama,run.conversation,tools);check(switched.dump().find("offline_signature")==std::string::npos&&switched.dump().find("claude_write")!=std::string::npos,"mixed provider transcript retains tools and excludes foreign signatures");
    int badCalls=0;rejects([&]{runAssistant(config,V::makeStr("Incomplete native message"),tools,[&](auto&,auto&){++badCalls;return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){auto response=messagesCalls();response.set("stop_reason",V::makeStr("max_tokens"));return response;});},"Messages truncation rejects before tools");check(badCalls==0,"no tools on native truncation");
    auto invalidUsage=runAssistant(config,V::makeStr("Do not mask invalid usage with cache tokens"),tools,{}, {},nullptr,[&](auto&,auto&,auto*){auto response=messagesFinal();response.set("usage",json(R"({"input_tokens":-1,"output_tokens":4,"cache_creation_input_tokens":2})"));return response;});check(!invalidUsage.usage.reportedResponses&&invalidUsage.usage.missingResponses==1,"cache counts cannot make a negative original usage valid");
}
void testPresetsCatalogAndExport(){
    for(const auto& preset:assistantProviderPresets()){const auto config=assistantProviderPreset(preset.id);check(config.model.empty()&&config.provider==preset.id,"presets do not choose a model or read a credential");check(assistantProtocolFromName(assistantProtocolName(config.protocol))==config.protocol,"protocol names roundtrip");}
    auto local=assistantProviderPreset("ollama");check(local.apiKeyEnv.empty()&&local.protocol==AssistantProtocol::ChatCompletions,"local Ollama is credential-free chat");
    auto models=assistantProviderModels(local,[&](const auto& config,const V& request,auto*){check(request.find("method")->str=="GET"&&config.endpoint=="http://127.0.0.1:11434/v1/models"&&config.apiKeyEnv.empty(),"catalog uses read-only derived local GET");return json(R"({"data":[{"id":"actual-model:7b"},{"id":"actual-model:7b"},{"id":"actual-model:14b"}]})");});check(models.size()==2&&models.front()=="actual-model:7b"&&local.model.empty(),"catalog IDs deduplicate without selecting one");
    auto zen=assistantProviderPreset("opencode");assistantProviderModels(zen,[&](const auto& config,auto&,auto*){check(config.endpoint=="https://opencode.ai/zen/v1/models"&&config.apiKeyEnv.empty(),"known public Zen model catalog reads no API key");return json(R"({"data":[]})");});
    auto invalid=local;invalid.model="explicit";invalid.apiKeyEnv="OPENAI_API_KEY";rejects([&]{validateAssistantConfig(invalid);},"HTTP loopback must not transmit credentials");invalid=local;invalid.model="explicit";invalid.endpoint="http://remote.invalid/v1/chat/completions";rejects([&]{validateAssistantConfig(invalid);},"HTTP remote rejects");
    const std::string exe="C:\\Program Files\\ARK\\ark-mcp.exe",pipe="ark-studio-actual-123";
    const auto codex=assistantExternalMcpConfig("codex",exe,pipe);check(codex.find("[mcp_servers.ark]")!=std::string::npos&&codex.find("--connect")!=std::string::npos&&codex.find(pipe)!=std::string::npos,"Codex export binds actual pipe/executable");
    for(const char* target:{"claude-code","claude-desktop","opencode"}){V exported;check(arkjson::parse(assistantExternalMcpConfig(target,exe,pipe),exported),"client export is valid JSON");const auto key=std::string(target)=="opencode"?"mcp":"mcpServers";check(exported.find(key)->find("ark")&&exported.dump().find(pipe)!=std::string::npos,"external client connects exact active window");}
    auto selected=configured("ollama");V external;check(arkjson::parse(assistantExternalMcpConfig("opencode",exe,pipe,selected),external),"OpenCode local provider JSON");check(external.find("provider")->find("ark-ollama")->find("options")->find("baseURL")->str=="http://127.0.0.1:11434/v1"&&external.find("model")->str=="ark-ollama/explicit-offline-model","external Ollama uses OpenCode MCP plus actual chosen model");
    selected=configured("grok");arkjson::parse(assistantExternalMcpConfig("opencode",exe,pipe,selected),external);check(external.find("provider")->find("ark-grok")->find("options")->find("apiKey")->str=="{env:XAI_API_KEY}"&&external.find("provider")->find("ark-grok")->find("npm")->str=="@ai-sdk/openai","external Grok references env and Responses SDK only");
    selected=configured("openai");selected.authMode=AssistantAuthMode::ChatGptPlan;arkjson::parse(assistantExternalMcpConfig("opencode",exe,pipe,selected),external);check(!external.find("provider")&&!external.find("model")&&external.find("mcp"),"plan/account route exports MCP only without silently switching to an API key");
    rejects([&]{assistantExternalMcpConfig("codex","ark-mcp.exe",pipe);},"export refuses guessed relative executable");rejects([&]{assistantExternalMcpConfig("codex",exe,"pipe with spaces");},"export refuses invalid endpoint");
}
void testResponsesReplayCustody(){auto openai=configured("openai");V response=json(R"({"status":"completed","output":[{"type":"reasoning","id":"reason_source","encrypted_content":"opaque_source_payload","summary":[]},{"type":"function_call","call_id":"actual_read","name":"doc_info","arguments":"{}"}],"usage":{"input_tokens":5,"output_tokens":3,"total_tokens":8}})");
    V transcript=json(R"([{"role":"user","content":"Inspect"}])");const auto retained=assistantNormalizeResponse(openai,response);for(const auto& item:retained.find("output")->arr)transcript.arr.push_back(item);transcript.arr.push_back(json(R"({"type":"function_call_output","call_id":"actual_read","output":"{\"revision\":7}"})"));
    auto same=assistantRequest(openai,transcript,tools);check(same.dump().find("opaque_source_payload")!=std::string::npos&&same.dump().find("ark_protocol")==std::string::npos,"same Responses route replays reasoning with no ARK wire metadata");
    for(auto config:{configured("grok"),configured("custom")}){const auto changed=assistantRequest(config,transcript,tools);check(changed.dump().find("opaque_source_payload")==std::string::npos&&changed.dump().find("actual_read")!=std::string::npos,"foreign Responses route excludes encrypted reasoning while preserving tool receipts");}
    auto changedModel=openai;changedModel.model="different-explicit-model";check(assistantRequest(changedModel,transcript,tools).dump().find("opaque_source_payload")==std::string::npos,"reasoning is not replayed to a different model");
    auto plan=openai;plan.authMode=AssistantAuthMode::ChatGptPlan;check(assistantRequest(plan,transcript,tools).dump().find("opaque_source_payload")==std::string::npos,"API and ChatGPT plan reasoning custody stay separate");
}
void testMixedProviderTeam(){AgentTeam team;AgentTeamConfig config;config.maxWorkers=3;std::array<std::atomic<int>,3> requests{};std::atomic<int> inspections=0;
    const char* providers[]={"grok","ollama","claude"};
    for(int i=0;i<3;++i){AgentTeamRole role;role.id="provider"+std::to_string(i);role.label=role.id;role.assistant=configured(providers[i]);role.assistant.model+="-"+std::to_string(i);role.canWrite=true;
        role.transport=[&,i](const AssistantConfig& connector,const V& request,const auto*){check(connector.provider==providers[i]&&connector.model=="explicit-offline-model-"+std::to_string(i),"mixed team keeps each role provider/model");const int count=++requests[i];const std::string id="role_inspect_"+std::to_string(i);
            if(connector.protocol==AssistantProtocol::Responses){check(request.find("input")&&!request.find("include"),"Grok role uses Responses without OpenAI encrypted request option");if(count==1){V response=json(R"({"status":"completed","output":[{"type":"function_call","name":"doc_info","arguments":"{}"}]})");V output=*response.find("output");output.arr[0].set("call_id",V::makeStr(id));response.set("output",output);return response;}return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Evidence inspected."}]}]})");}
            if(connector.protocol==AssistantProtocol::ChatCompletions){check(request.find("messages")&&!request.find("input"),"local role uses Chat Completions");if(count==1){V response=json(R"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"type":"function","function":{"name":"doc_info","arguments":"{}"}}]}}]})");V choices=*response.find("choices"),entry=choices.arr[0],msg=*entry.find("message"),calls=*msg.find("tool_calls");calls.arr[0].set("id",V::makeStr(id));msg.set("tool_calls",calls);entry.set("message",msg);choices.arr[0]=entry;response.set("choices",choices);return response;}return finalChat();}
            check(request.find("system")&&request.find("max_tokens"),"Claude role uses native Messages");if(count==1){V response=json(R"({"type":"message","role":"assistant","stop_reason":"tool_use","content":[{"type":"tool_use","name":"doc_info","input":{}}]})");V content=*response.find("content");content.arr[0].set("id",V::makeStr(id));response.set("content",content);return response;}return messagesFinal();
        };config.roles.push_back(role);
    }
    team.start(config,"Inspect this same drawing",tools,[&](const std::string& name,const V&){check(name=="doc_info","mixed provider team inspection allowlist");++inspections;return json(R"({"isError":false,"structuredContent":{"revision":7}})");});check(team.waitIdleFor(std::chrono::seconds(5)),"mixed team completes bounded offline task");const auto snapshot=team.snapshot(true);check(inspections==3&&snapshot.roles.size()==3,"each provider role inspects exactly once");for(int i=0;i<3;++i)check(requests[i]==2&&snapshot.roles[i].state==AgentTeamState::Idle&&snapshot.roles[i].conversation.dump().find("role_inspect_"+std::to_string(i))!=std::string::npos,"mixed roles keep independent normalized histories and finish successfully");
}
}
int main(int argc,char** argv){try{
    // Explicit opt-in read-only network smoke. Default test stays fully offline.
    if(argc==3&&std::string(argv[1])=="--catalog"){const auto ids=assistantProviderModels(assistantProviderPreset(argv[2]));V models=V::makeArr();for(const auto& id:ids)models.arr.push_back(V::makeStr(id));std::cout<<models.dump()<<'\n';return 0;}
    testChat();testCompleteBeforeExecution();testBoundedOutcomes();testMessages();testPresetsCatalogAndExport();testResponsesReplayCustody();testMixedProviderTeam();std::cout<<"Provider connector contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
