#include "ark/assistant_credentials.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "ark/assistant_connectors.hpp"
#include "ark/agent_team.hpp"
#include "ark/agent.hpp"
#include "ark/project_memory_tools.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
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
        const auto& functions=request.find("tools")->arr;auto expectedSchema=*tools.arr[0].find("inputSchema");expectedSchema.set("properties",V::makeObj());check(functions[0].find("function")->find("parameters")->dump()==expectedSchema.dump(),"Ollama makes the empty property map explicit without closing the schema");
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
void testOllamaEmptyPropertySchemas(){
    const auto catalog=AgentSession::tools();const auto original=catalog.dump();const auto functions=assistantFunctionTools(catalog);
    const auto request=assistantRequest(configured("ollama"),json(R"([{"role":"user","content":"Inspect the current document"}])"),catalog);
    const auto& wire=request.find("tools")->arr;check(wire.size()==functions.arr.size(),"Ollama retains every model-visible function, excluding host-only usage receipts");
    unsigned adapted=0;
    for(std::size_t i=0;i<functions.arr.size();++i){
        const auto& schema=*functions.arr[i].find("parameters");const auto& parameters=*wire[i].find("function")->find("parameters");
        auto expected=schema;if(schema.find("type")&&schema.find("type")->asStr()=="object"&&!schema.find("properties")){expected.set("properties",V::makeObj());++adapted;}
        check(parameters.dump()==expected.dump(),"Ollama preserves every declared constraint and property while adapting absent maps");
        check(parameters.find("properties")&&parameters.find("properties")->t==V::Obj,"every discovered Ollama function has an object property map");
    }
    check(adapted>0&&catalog.dump()==original,"actual no-argument tools are covered and canonical MCP discovery is unchanged");
    auto other=assistantRequest(configured("claude-chat"),json(R"([{"role":"user","content":"Inspect"}])"),tools);
    check(other.find("tools")->arr[0].find("function")->find("parameters")->dump()==tools.arr[0].find("inputSchema")->dump(),"other provider schemas are unchanged");
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
    const auto grokBuild=assistantExternalMcpConfig("grok-build",exe,pipe,configured("grok"));check(grokBuild==codex+"enabled = true\n","Grok Build exports the documented enabled stdio MCP table, absolute executable and actual pipe only");
    auto planForGrok=configured("openai");planForGrok.authMode=AssistantAuthMode::ChatGptPlan;check(assistantExternalMcpConfig("grok-build",exe,pipe,planForGrok)==grokBuild,"Grok Build export keeps CLI account ownership independent of Assistant API or plan selection");
    const std::string quotedExe="C:\\ARK \"quoted\"\\ark-mcp.exe";check(assistantExternalMcpConfig("grok-build",quotedExe,pipe).find(V::makeStr(quotedExe).dump())!=std::string::npos,"Grok Build TOML escapes quoted Windows executable paths");
    rejects([&]{assistantExternalMcpConfig("grok-build","ark-mcp.exe",pipe);},"Grok Build refuses relative executable paths");rejects([&]{assistantExternalMcpConfig("grok-build",exe,"pipe with spaces");},"Grok Build refuses an invalid active endpoint");rejects([&]{assistantExternalMcpConfig("grok-build","C:/ARK/${OTHER}/ark-mcp.exe",pipe);},"Grok Build refuses command placeholders that the client would expand");
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
void testSavedKeysAndClaudeRoutes(){
    auto deepseek=assistantProviderPreset("deepseek");check(deepseek.endpoint=="https://api.deepseek.com/chat/completions"&&deepseek.apiKeyEnv=="DEEPSEEK_API_KEY"&&deepseek.protocol==AssistantProtocol::ChatCompletions&&deepseek.model.empty(),"DeepSeek preset uses its Chat Completions endpoint without choosing a model");
    auto claude=configured("claude");const auto request=assistantRequest(claude,json(R"([{"role":"user","content":"Inspect"}])"),tools);
    check(request.find("cache_control")&&request.find("cache_control")->find("type")->str=="ephemeral"&&request.find("system")->arr.back().find("cache_control"),"first-party Messages requests cache the stable prefix and the growing tail");
    auto compatible=claude;compatible.provider="custom";compatible.endpoint="https://messages.example.invalid/v1/messages";check(!assistantRequest(compatible,json(R"([{"role":"user","content":"Inspect"}])"),tools).find("cache_control"),"other Messages endpoints receive no Anthropic cache fields");
    const auto command=assistantExternalMcpConfig("claude-code-cli","C:/ARK/ark-mcp.exe","ark-studio-7-1-2");
    check(command.find("claude mcp add --transport stdio --scope user ark -- \"C:/ARK/ark-mcp.exe\" --connect ark-studio-7-1-2")!=std::string::npos&&command.find("claude mcp remove ark --scope user")==0,"Claude Code export is one user-scope stdio command with options before --");
#ifdef _WIN32
    const auto directory=std::filesystem::temp_directory_path()/("ark-api-keys-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);_putenv_s("ARK_API_KEY_DIRECTORY","");}} cleanup{directory};
    _putenv_s("ARK_API_KEY_DIRECTORY",directory.string().c_str());
    auto keyed=claude;keyed.apiKeyEnv="ARK_TEST_UNSET_KEY_VARIABLE";const std::string key="sk-ant-test-0123456789abcdef";
    check(!assistantApiKeyStored(keyed)&&!assistantKeyAvailable(keyed),"no saved key and no environment variable means no credential");
    storeAssistantApiKey(keyed,key);check(assistantApiKeyStored(keyed)&&assistantKeyAvailable(keyed)&&loadAssistantApiKey(keyed)==key,"a pasted key is available immediately without an environment variable or restart");
    std::string bytes;for(const auto& entry:std::filesystem::directory_iterator(directory)){std::ifstream input(entry.path(),std::ios::binary);bytes+=std::string((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());}
    check(!bytes.empty()&&bytes.find(key)==std::string::npos,"the saved key is DPAPI-protected, never plaintext on disk");
    auto discovery=keyed;discovery.endpoint="https://api.anthropic.com/v1/models";check(loadAssistantApiKey(discovery)==key,"model discovery on the same provider host uses the saved key");
    auto otherHost=keyed;otherHost.endpoint="https://proxy.example.invalid/v1/messages";check(!assistantApiKeyStored(otherHost)&&loadAssistantApiKey(otherHost).empty(),"a saved key is never released to another host");
    auto otherProvider=keyed;otherProvider.provider="claude-chat";check(loadAssistantApiKey(otherProvider).empty(),"a saved key is bound to its provider");
    rejects([&]{storeAssistantApiKey(keyed,"sk ant with spaces");},"keys with spaces or line breaks are rejected");
    auto local=keyed;local.endpoint="http://127.0.0.1:9/v1/messages";rejects([&]{storeAssistantApiKey(local,key);},"keys are never saved for a non-HTTPS endpoint");
    check(removeAssistantApiKey(keyed)&&!assistantApiKeyStored(keyed)&&loadAssistantApiKey(keyed).empty(),"forgetting the key removes it");
    auto customDeepSeek=assistantProviderPreset("custom");customDeepSeek.endpoint="https://api.deepseek.com/chat/completions";customDeepSeek.protocol=AssistantProtocol::ChatCompletions;const auto presetDeepSeek=assistantProviderPreset("deepseek");const std::string deepKey="sk-deepseek-test-0123456789";
    storeAssistantApiKey(customDeepSeek,deepKey);check(assistantApiKeyStored(presetDeepSeek)&&loadAssistantApiKey(presetDeepSeek)==deepKey,"the DeepSeek preset reuses a key saved for a custom DeepSeek endpoint");
    auto customElsewhere=customDeepSeek;customElsewhere.endpoint="https://proxy.example.invalid/chat/completions";check(loadAssistantApiKey(customElsewhere).empty(),"a custom connector on another host never receives that key");
    check(removeAssistantApiKey(presetDeepSeek)&&!assistantApiKeyStored(customDeepSeek),"forgetting through the preset removes the shared key");
    storeAssistantApiKey(presetDeepSeek,deepKey);check(loadAssistantApiKey(customDeepSeek)==deepKey,"a custom DeepSeek endpoint reuses the preset's saved key");removeAssistantApiKey(presetDeepSeek);
#endif
}
void testPromptCacheKey(){
    auto openai=configured("openai");V first=json(R"([{"role":"developer","content":"Static guide"},{"role":"user","content":"First turn"}])"),later=first;later.arr.push_back(json(R"({"role":"user","content":"Later turn with new evidence"})"));
    const auto a=assistantRequest(openai,first,tools),b=assistantRequest(openai,later,tools);
    check(a.find("prompt_cache_key")&&a.find("prompt_cache_key")->str.rfind("ark-",0)==0&&a.find("prompt_cache_key")->str==b.find("prompt_cache_key")->str,"OpenAI requests keep one prompt cache key while the history grows");
    check(a.find("prompt_cache_key")->str.find("First turn")==std::string::npos&&a.find("prompt_cache_key")->str.size()==36,"prompt cache key is an opaque digest, not request text");
    const V readOnly=json(R"([{"name":"doc_info","description":"Inspect current document","inputSchema":{"type":"object"}}])");check(assistantRequest(openai,first,readOnly).find("prompt_cache_key")->str!=a.find("prompt_cache_key")->str,"roles with different tool catalogs use different cache keys");
    auto plan=openai;plan.authMode=AssistantAuthMode::ChatGptPlan;check(!assistantRequest(plan,first,tools).find("prompt_cache_key"),"ChatGPT plan Responses requests stay unkeyed (keyed plan runs reported no cached tokens)");
    for(auto config:{configured("grok"),configured("custom")})check(!assistantRequest(config,first,tools).find("prompt_cache_key"),"other Responses providers receive no OpenAI-specific cache field");
}
void testResponsesCallIdPreflight(){
    auto base=json(R"([{"role":"user","content":"Synthetic request."},{"type":"function_call","call_id":"valid_call","name":"doc_info","arguments":"{}"},{"type":"function_call_output","call_id":"valid_call","output":"sk_FAKE_HISTORY_SECRET"}])");
    const std::string boundary(64,'b');base.arr[1].set("call_id",V::makeStr(boundary));base.arr[2].set("call_id",V::makeStr(boundary));
    for(bool plan:{false,true}){
        auto config=configured("openai");if(plan)config.authMode=AssistantAuthMode::ChatGptPlan;
        const auto valid=assistantRequest(config,base,tools);
        check(valid.find("input")->arr[1].find("call_id")->str==boundary&&valid.find("input")->arr[2].find("call_id")->str==boundary,"valid external 64-character IDs are preserved exactly");
        for(int variant=0;variant<7;++variant){auto malformed=base;std::size_t expectedIndex=1;
            if(variant==0)malformed.arr[1].set("call_id",V::makeStr(""));
            if(variant==1)malformed.arr[1].set("call_id",V::makeStr(std::string(65,'s')));
            if(variant==2)malformed.arr[1].set("call_id",V::makeNum(123));
            if(variant==3){auto& fields=malformed.arr[1].obj;fields.erase(std::remove_if(fields.begin(),fields.end(),[](const auto& pair){return pair.first=="call_id";}),fields.end());}
            if(variant==4)malformed.arr[1].set("call_id",V::makeStr("private\nprovider_id"));
            if(variant==5)malformed.arr[1].set("call_id",V::makeStr("ark_memory_prefetch_"+std::string(64,'a')));
            if(variant==6){malformed.arr[2].set("call_id",V::makeStr(std::string(65,'r')));expectedIndex=2;}
            const auto before=malformed.dump();unsigned requests=0;std::string failure;
            try{runAssistant(config,malformed,tools,{}, {},nullptr,[&](const auto&,const V&,const auto*){++requests;return json(R"({"status":"completed","output":[]})");});}catch(const std::exception& e){failure=e.what();}
            check(requests==0&&failure.find("input["+std::to_string(expectedIndex)+"].call_id")!=std::string::npos&&failure.find("64")!=std::string::npos,"malformed Responses IDs reject with the exact parameter before any transport invocation");
            check(failure.find("sk_FAKE_HISTORY_SECRET")==std::string::npos&&failure.find("private")==std::string::npos&&failure.find(std::string(65,'s'))==std::string::npos&&malformed.dump()==before,"preflight failure reveals no raw IDs or tool contents and preserves original history");
        }
    }
    V memoryTools=V::makeArr();appendProjectMemoryTools(memoryTools);
    auto recall=[](const auto&,const auto&){return json(R"({"structuredContent":{"note":"Synthetic private historical data."}})");};
    const auto fresh=projectMemoryAssistantInput(V::makeStr("Synthetic task"),memoryTools,recall);auto legacy=fresh;
    const auto legacyId="ark_memory_prefetch_"+iterationSha256(legacy.arr[1].find("arguments")->str+"\n"+legacy.arr[2].find("output")->str);
    legacy.arr[1].set("call_id",V::makeStr(legacyId));legacy.arr[2].set("call_id",V::makeStr(legacyId));
    for(int variant=0;variant<7;++variant){auto malformed=legacy;
        if(variant==0)malformed.arr[1].set("name",V::makeStr("external_tool"));
        if(variant==1)malformed.arr[1].set("arguments",V::makeStr("{\"operation\":\"wakeup\",\"scope\":\"shared\"}"));
        if(variant==2)malformed.arr[2].set("output",V::makeStr("tampered private receipt"));
        if(variant==3)malformed.arr[1].set("external_metadata",V::makeStr("external provenance"));
        if(variant==4)malformed.arr[2].set("call_id",V::makeStr("unrelated_result"));
        if(variant==5)malformed.arr.pop_back();
        if(variant==6)malformed.arr.insert(malformed.arr.begin()+2,json(R"({"role":"assistant","content":"Nonadjacent external result."})"));
        const auto before=malformed.dump();std::string failure;try{assistantRequest(configured("openai"),malformed,memoryTools);}catch(const std::exception& e){failure=e.what();}
        check(failure.find("input[1].call_id")!=std::string::npos&&failure.find("historical data")==std::string::npos&&malformed.dump()==before,"legacy recovery rejects forged provenance, changed payload, incomplete and nonadjacent pairs without rewriting");
    }
    auto colliding=legacy;colliding.arr.push_back(fresh.arr[1]);colliding.arr.back().set("name",V::makeStr("doc_info"));colliding.arr.push_back(fresh.arr[2]);const auto before=colliding.dump();
    std::string failure;try{assistantRequest(configured("openai"),colliding,memoryTools);}catch(const std::exception& e){failure=e.what();}
    check(failure.find("collide")!=std::string::npos&&colliding.dump()==before,"legacy recovery refuses an ID already owned by another retained pair");
    auto claimed=base;claimed.arr[1].set("call_id",*fresh.arr[1].find("call_id"));claimed.arr[2].set("call_id",*fresh.arr[1].find("call_id"));const auto claimedBefore=claimed.dump();failure.clear();
    try{projectMemoryAssistantInput(claimed,memoryTools,recall);}catch(const std::exception& e){failure=e.what();}
    check(failure.find("collide")!=std::string::npos&&claimed.dump()==claimedBefore,"new prefetch refuses a host ID already claimed by an unrelated external pair");
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
#include "local_models_tests.inl"
int main(int argc,char** argv){try{
    if(argc==5&&std::string(argv[1])=="--export-mcp"){std::cout<<assistantExternalMcpConfig(argv[2],argv[3],argv[4]);return 0;}
    if(argc==3&&std::string(argv[1])=="--local-agent-smoke"){liveLocalModelSmoke(argv[2]);return 0;}
    if(argc==3&&std::string(argv[1])=="--local-agent-basic-smoke"){liveLocalModelSmoke(argv[2],true);return 0;}
    // Explicit opt-in read-only network smoke. Default test stays fully offline.
    if(argc==3&&std::string(argv[1])=="--catalog"){const auto ids=assistantProviderModels(assistantProviderPreset(argv[2]));V models=V::makeArr();for(const auto& id:ids)models.arr.push_back(V::makeStr(id));std::cout<<models.dump()<<'\n';return 0;}
    testLocalModelCapabilities();testChat();testOllamaEmptyPropertySchemas();testCompleteBeforeExecution();testBoundedOutcomes();testMessages();testPresetsCatalogAndExport();testResponsesReplayCustody();testPromptCacheKey();testSavedKeysAndClaudeRoutes();testResponsesCallIdPreflight();testMixedProviderTeam();std::cout<<"Provider connector contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
