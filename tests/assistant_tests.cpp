#include "ark/assistant.hpp"
#include "ark/assistant_connectors.hpp"
#include "ark/io.hpp"
#include "ark/agent.hpp"
#include "ark/onboarding.hpp"
#include "ark/chatgpt_auth.hpp"
#include <algorithm>
#include <limits>
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>
using V=arkjson::Value;
static int checks=0;
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static V json(const std::string& s){V v;if(!arkjson::parse(s,v))throw std::runtime_error("bad test JSON");return v;}
static V& member(V& value,const char* key){for(auto& item:value.obj)if(item.first==key)return item.second;throw std::runtime_error("missing mutable test member");}
template<class F>static void fails(F action,const char* why){bool caught=false;try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
static V toolResponse(const char* id="call_1",const char* name="doc_info",const char* args="{}"){
    V response=json(R"({"status":"completed","output":[{"type":"reasoning","id":"rs_1","summary":[],"encrypted_content":"opaque-reasoning"}]})");
    V call=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(name));call.set("arguments",V::makeStr(args));
    V output=*response.find("output");output.arr.push_back(call);response.set("output",output);return response;
}
static V finalResponse(){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Done."}]}]})");}
#include "assistant_context_tests.inl"
static void visionChecks(){
    const std::string png="iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC";
    auto receipt=json(R"({"isError":false,"structuredContent":{"visual_evidence":{"revision":7,"current_revision":7,"view":"front","feature_id":19,"occurrence_id":23,"copy":1,"reference":"face.front","label":"before"}},"content":[{"type":"image","mimeType":"image/png"}]})");member(receipt,"content").arr[0].set("data",V::makeStr(png));
    const auto tools=json(R"([{"name":"snapshot","description":"Visual inspection","inputSchema":{"type":"object"}}])");
    for(const char* provider:{"openai","grok","ollama","claude"}){auto config=ark::assistantProviderPreset(provider);config.model="offline-vision";int requests=0,calls=0;
        const auto run=ark::runAssistant(config,V::makeStr("Inspect selected CAD appearance"),tools,[&](auto&,auto&){++calls;return receipt;},{},nullptr,[&](const auto&,const V& request,auto*){
            if(++requests==1){if(config.protocol==ark::AssistantProtocol::Responses)return toolResponse("visual","snapshot",R"({"expected_revision":7})");if(config.protocol==ark::AssistantProtocol::ChatCompletions)return json(R"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"visual","type":"function","function":{"name":"snapshot","arguments":"{\"expected_revision\":7}"}}]}}]})");return json(R"({"type":"message","role":"assistant","stop_reason":"tool_use","content":[{"type":"tool_use","id":"visual","name":"snapshot","input":{"expected_revision":7}}]})");}
            const auto& rows=request.find(config.protocol==ark::AssistantProtocol::Responses?"input":"messages")->arr;const auto& parts=rows.back().find("content")->arr;
            const V* image=nullptr;for(const auto& part:parts)if(part.find("type")&&(part.find("type")->str=="input_image"||part.find("type")->str=="image_url"||part.find("type")->str=="image"))image=&part;
            check(image!=nullptr,"actual provider request includes multimodal image content");
            if(config.protocol==ark::AssistantProtocol::Messages)check(image->find("source")->find("media_type")->str=="image/png"&&image->find("source")->find("data")->str==png,"Claude receives actual base64 pixels in native image source");
            else{const auto url=config.protocol==ark::AssistantProtocol::Responses?image->find("image_url")->str:image->find("image_url")->find("url")->str;check(url=="data:image/png;base64,"+png,"Responses/Chat local/remote connectors receive identical actual PNG bytes");}
            check(request.dump().find("face.front")!=std::string::npos&&request.dump().find("occurrence_id")!=std::string::npos,"image retains semantic reference and selected CAD identity metadata");
            if(config.protocol==ark::AssistantProtocol::Responses)return finalResponse();if(config.protocol==ark::AssistantProtocol::ChatCompletions)return json(R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"Done."}}]})");return json(R"({"type":"message","role":"assistant","stop_reason":"end_turn","content":[{"type":"text","text":"Done."}]})");
        });check(requests==2&&calls==1&&run.text=="Done.","vision tool/provider loop completes offline");
    }
    ark::AssistantConfig config;config.model="offline-vision";
    for(int variant=0;variant<5;++variant){auto invalid=receipt;if(variant==0)invalid.set("isError",V::makeBool(true));if(variant==1)member(member(invalid,"structuredContent"),"visual_evidence").set("current_revision",V::makeNum(8));if(variant==2)member(invalid,"content").arr[0].set("data",V::makeStr("not an image"));if(variant==3)member(invalid,"structuredContent").set("visual_evidence",V{});if(variant==4)member(member(invalid,"structuredContent"),"visual_evidence").set("stale",V::makeBool(true));int requests=0;
        auto run=ark::runAssistant(config,V::makeStr("Reject invalid/stale pixels"),tools,[&](auto&,auto&){return invalid;},{},nullptr,[&](auto&,const V& request,auto*){if(++requests==1)return toolResponse("invalid","snapshot",R"({"expected_revision":7})");check(request.dump().find("input_image")==std::string::npos,"error/stale/malformed/missing provenance supplies no image observation");check(request.dump().find(png)==std::string::npos,"rejected image bytes are removed from text tool result too");return finalResponse();});
    }
    int requests=0;auto repeated=ark::runAssistant(config,V::makeStr("Reuse the same snapshot"),tools,[&](auto&,auto&){return receipt;},{},nullptr,[&](auto&,auto&,auto*){++requests;if(requests<4)return toolResponse(requests==1?"a":requests==2?"b":"c","snapshot",R"({"expected_revision":7})");return finalResponse();});
    int images=0;for(const auto& item:repeated.conversation.arr)if(const auto* content=item.find("content");content&&content->t==V::Arr)for(const auto& part:content->arr)if(part.find("type")&&part.find("type")->str=="input_image")++images;check(images==1,"unchanged image plus metadata deduplicates without another host/provider image observation");
    requests=0;int captures=0;auto changing=ark::runAssistant(config,V::makeStr("Keep latest before/after views"),tools,[&](auto&,auto&){auto next=receipt;member(member(next,"structuredContent"),"visual_evidence").set("label",V::makeStr("view-"+std::to_string(++captures)));return next;},{},nullptr,[&](auto&,auto&,auto*){++requests;if(requests<4)return toolResponse(requests==1?"d":requests==2?"e":"f","snapshot",R"({"expected_revision":7})");return finalResponse();});images=0;for(const auto& item:changing.conversation.arr)if(const auto* content=item.find("content");content&&content->t==V::Arr)for(const auto& part:content->arr)if(part.find("type")&&part.find("type")->str=="input_image")++images;check(images==2&&changing.conversation.dump().find("view-1")!=std::string::npos,"latest two image observations retain before/after pixels and all old metadata");
    auto external=json(R"([{"role":"user","content":[{"type":"input_image","image_url":"C:/private/secret.png"}]}])");fails([&]{ark::assistantRequest(config,external,tools);},"model text/path is never fetched as an image");
    std::atomic_bool cancelled=false;requests=0;fails([&]{ark::runAssistant(config,V::makeStr("Cancel snapshot"),tools,[&](auto&,auto&){cancelled=true;return receipt;},{},&cancelled,[&](auto&,auto&,auto*){++requests;return toolResponse("cancel","snapshot");});},"cancellation after capture stops image delivery");check(requests==1,"cancelled capture sends no image request");
    ark::Image pixel;pixel.w=1;pixel.h=1;pixel.px={255,0,0};const auto encoded=ark::encodePNG(pixel);check(encoded.size()>24&&encoded[0]==137,"PNG memory encoding produces actual image bytes");
    ark::AgentSession session;auto created=session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Vision box","p":{"dx":10,"dy":8,"dz":4}}]})"));check(!created.find("isError")->b,"disposable snapshot geometry created");auto actual=session.call("snapshot",json(R"({"id":1,"view":"right","expected_revision":1,"label":"after"})"));check(!actual.find("isError")->b&&actual.find("content")->arr.size()==2&&actual.find("content")->arr[1].find("data")->str.rfind("iVBOR",0)==0,"public MCP snapshot returns actual inline PNG without requiring path");check(actual.find("structuredContent")->find("visual_evidence")->find("feature_id")->num==1,"snapshot identity records selected stable CAD ID");auto stale=session.call("snapshot",json(R"({"id":1,"expected_revision":0})"));check(stale.find("isError")->b&&stale.find("content")->arr.size()==1,"stale revision rejects before returning any pixels");
    auto preview=session.call("doc_preview",json(R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":20}}]})"));check(!preview.find("isError")->b&&preview.find("structuredContent")->find("candidate_cached")->b,"disposable evaluated preview candidate retained");V capture=json(R"({"id":1,"view":"front","expected_revision":1})");capture.set("candidate_id",*preview.find("structuredContent")->find("candidate_id"));auto previewImage=session.call("snapshot",capture);check(!previewImage.find("isError")->b&&!previewImage.find("structuredContent")->find("visual_evidence")->find("committed")->b&&session.revision==1,"prepared candidate snapshot renders actual uncommitted pixels without changing document");session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"changed","expr":1}]})"));capture.set("expected_revision",V::makeNum(2));auto expired=session.call("snapshot",capture);check(expired.find("isError")->b&&expired.find("content")->arr.size()==1,"changed baseline invalidates prepared preview pixels explicitly");
}
static void payloadAndPerformanceChecks(){
    ark::AgentSession session;ark::AssistantConfig config;config.model="offline-payload";config.reasoningEffort="high";
    config.onboardingPack=ark::agentOnboardingPack({"overview"});auto tools=ark::AgentSession::tools();
    const auto fullPack=ark::agentOnboardingPack({"overview","manufacturing","components","sketch","solids","surfacing","sheet","frames","materials","assembly","motion"});
    const auto receipt=session.call("agent_onboard",json(R"({"topic":"surfacing"})"));
    int rounds=0;std::uint64_t beforeRequest=0,afterRequest=0,beforeInitial=0,afterInitial=0,compactReceiptBytes=0;std::vector<ark::AssistantPerformance> measured;
    auto run=ark::runAssistant(config,V::makeStr("Create a curved enclosure from the functional design intent"),tools,[&](auto&,auto&){std::this_thread::sleep_for(std::chrono::milliseconds(3));return receipt;},
        [&](const auto& event){if(event.kind==ark::AssistantEvent::Usage||event.kind==ark::AssistantEvent::ToolEnd)measured.push_back(event.performance);},nullptr,
        [&](auto&,const V& request,auto*){std::this_thread::sleep_for(std::chrono::milliseconds(8));
            if(++rounds==1){afterInitial=request.dump().size();auto legacy=*request.find("input");legacy.arr.front().set("content",V::makeStr("ARK_PREBAKED_GUIDE_V1\n"+fullPack));beforeInitial=ark::assistantRequest(config,legacy,tools).dump().size();return toolResponse("guide_curves","agent_onboard",R"({"topic":"surfacing"})");}
            const auto& history=request.find("input")->arr;V compact;check(arkjson::parse(history.back().find("output")->str,compact),"compact actual MCP receipt is valid JSON");
            check(!compact.find("content")&&compact.find("structuredContent")->dump()==receipt.find("structuredContent")->dump()&&!compact.find("isError")->b,"only duplicated MCP text is removed and the exact structured receipt survives");
            auto legacy=*request.find("input");legacy.arr.back().set("output",V::makeStr(receipt.dump()));beforeRequest=ark::assistantRequest(config,legacy,tools).dump().size();afterRequest=request.dump().size();compactReceiptBytes=compact.dump().size();return finalResponse();});
    check(beforeRequest>afterRequest&&receipt.dump().size()>compactReceiptBytes,"removing a duplicated MCP receipt reduces the actual next request without discarding evidence");
    check(beforeInitial>afterInitial&&fullPack.size()>config.onboardingPack.size(),"a lazily loaded core guide lowers initial context while all tool schemas remain advertised");
    check(run.performance.providerRequests==2&&run.performance.toolCalls==1&&run.performance.providerMilliseconds>=8&&run.performance.toolMilliseconds>=1,"provider and tool timings are measured independently from real callback boundaries");
    check(run.performance.requestBytes==afterInitial+afterRequest&&run.performance.lastRequestBytes==afterRequest&&run.performance.maxRequestBytes==std::max(afterInitial,afterRequest),"request byte totals and peak count the exact serialized requests once");
    check(measured.size()==3&&measured[0].providerRequests==1&&measured[1].toolCalls==1&&measured[2].providerRequests==2&&measured[2].toolCalls==1,"existing Usage and ToolEnd events carry cumulative measured performance without extra requests");
    V report=V::makeObj();report.set("tools",V::makeNum(ark::assistantFunctionTools(tools).arr.size()));report.set("tool_schema_bytes",V::makeNum(run.performance.lastToolSchemaBytes));report.set("full_pack_bytes",V::makeNum(fullPack.size()));report.set("core_pack_bytes",V::makeNum(config.onboardingPack.size()));report.set("initial_request_full_pack",V::makeNum(beforeInitial));report.set("initial_request_core_pack",V::makeNum(afterInitial));report.set("receipt_bytes_full",V::makeNum(receipt.dump().size()));report.set("receipt_bytes_compact",V::makeNum(compactReceiptBytes));report.set("next_request_full_receipt",V::makeNum(beforeRequest));report.set("next_request_compact_receipt",V::makeNum(afterRequest));std::cout<<"ASSISTANT_PAYLOAD_BENCHMARK "<<report.dump()<<'\n';
    // Images, annotations and distinct text are not duplicate evidence.
    auto preserve=[&](V original){int calls=0;ark::runAssistant(config,V::makeStr("Preserve tool evidence"),tools,[&](auto&,auto&){return original;},{},nullptr,[&](auto&,const V& request,auto*){if(++calls==1)return toolResponse("evidence","doc_info");V retained;arkjson::parse(request.find("input")->arr.back().find("output")->str,retained);check(retained.dump()==original.dump(),"nonduplicate MCP content and metadata stay complete");return finalResponse();});};
    auto distinct=receipt;auto content=*receipt.find("content");content.arr[0].set("text",V::makeStr("Additional measured evidence"));distinct.set("content",content);preserve(distinct);
    auto annotated=receipt;content=*receipt.find("content");content.arr[0].set("annotations",json(R"({"audience":["assistant"]})"));annotated.set("content",content);preserve(annotated);
    auto visual=receipt;content=*receipt.find("content");content.arr[0].set("text",V::makeStr("Additional visual evidence"));content.arr.push_back(json(R"({"type":"image","mimeType":"image/png","data":"offline-preserved-image"})"));visual.set("content",content);preserve(visual);
    const auto failure=session.call("doc_apply",json(R"({"commands":[{"op":"feature.edit","id":"Missing part","p":{"dx":20}}]})"));rounds=0;
    ark::runAssistant(config,V::makeStr("Retain atomic rejection evidence"),tools,[&](auto&,auto&){return failure;},{},nullptr,[&](auto&,const V& request,auto*){if(++rounds==1)return toolResponse("rejection","doc_apply",R"({"commands":[]})");V retained;arkjson::parse(request.find("input")->arr.back().find("output")->str,retained);check(retained.find("isError")->b&&retained.find("structuredContent")->dump()==failure.find("structuredContent")->dump(),"compaction preserves complete actual rejection diagnostics");return finalResponse();});
    ark::AssistantPerformance interrupted;bool caught=false;
    try{ark::runAssistant(config,V::makeStr("Measure interrupted request"),tools,{},[&](const auto& event){if(event.kind==ark::AssistantEvent::Usage)interrupted=event.performance;},nullptr,[](auto&,auto&,auto*)->V{std::this_thread::sleep_for(std::chrono::milliseconds(3));throw std::runtime_error("offline interrupted transport");});}catch(const ark::AssistantTransportError&){caught=true;}
    check(caught&&interrupted.providerRequests==1&&interrupted.providerMilliseconds>=1&&interrupted.requestBytes>0&&interrupted.toolCalls==0,"failed provider requests retain measured latency and bytes without inventing token usage");
}
static void nestedReceiptChecks(){
    ark::AgentSession session;ark::AssistantConfig config;config.model="offline-batch-payload";const auto tools=ark::AgentSession::tools();
    const auto batch=session.call("query_batch",json(R"({"queries":[{"tool":"doc_info","args":{}},{"tool":"materials_inspect","args":{}},{"tool":"sketch_inspect","args":{"name":"Missing"}}]})"));
    check(!batch.find("isError")->b&&batch.find("structuredContent")->find("results")->arr.size()==3,"actual query_batch supplies nested public MCP receipts");
    const auto publicBytes=batch.dump();for(const auto& row:batch.find("structuredContent")->find("results")->arr)check(row.find("content")->arr[0].find("text")->str==row.find("structuredContent")->dump()&&row.find("elapsed_ms"),"public query_batch contract retains canonical text, structured data and timing");
    auto execute=[&](const V& original,bool mixed){int rounds=0;std::size_t compactBytes=0,beforeRequest=0,afterRequest=0;
        ark::runAssistant(config,V::makeStr("Inspect batch receipts"),tools,[&](auto&,auto&){return original;},{},nullptr,[&](auto&,const V& request,auto*){
            if(++rounds==1)return toolResponse("batch","query_batch",R"({"queries":[{"tool":"doc_info","args":{}}]})");
            V compact;check(arkjson::parse(request.find("input")->arr.back().find("output")->str,compact),"nested compact receipt serializes valid provider JSON");check(!compact.find("content"),"canonical outer escaped copy is removed before nested receipts change");
            const auto& oldRows=original.find("structuredContent")->find("results")->arr;const auto& rows=compact.find("structuredContent")->find("results")->arr;
            check(compact.find("structuredContent")->find("revision")->dump()==original.find("structuredContent")->find("revision")->dump()&&compact.find("isError")->dump()==original.find("isError")->dump(),"batch revision and error semantics survive compaction");
            for(std::size_t i=0;i<rows.size();++i){check(rows[i].find("structuredContent")->dump()==oldRows[i].find("structuredContent")->dump()&&rows[i].find("isError")->dump()==oldRows[i].find("isError")->dump()&&rows[i].find("elapsed_ms")->dump()==oldRows[i].find("elapsed_ms")->dump(),"every nested result retains exact data, errors and measured elapsed time");if(!mixed||i==2)check(!rows[i].find("content"),"plain duplicate nested MCP text is removed");}
            if(mixed){const auto& content=rows[0].find("content")->arr;check(content.size()==2&&content[0].find("text")->str=="Unique batch evidence"&&content[1].find("data")->str=="offline-preserved-image"&&content[1].find("mimeType")->str=="image/png","distinct nested text and image bytes survive beside a removed duplicate");const auto& annotated=rows[1].find("content")->arr;check(annotated.size()==1&&annotated[0].find("annotations")&&annotated[0].find("text")->str==oldRows[1].find("structuredContent")->dump(),"annotated duplicate keeps its content and annotation semantics");}
            compactBytes=compact.dump().size();auto legacy=*request.find("input");legacy.arr.back().set("output",V::makeStr(original.dump()));beforeRequest=ark::assistantRequest(config,legacy,tools).dump().size();afterRequest=request.dump().size();return finalResponse();
        });check(compactBytes<original.dump().size()&&afterRequest<beforeRequest,"nested compaction measurably reduces actual serialized model context");
        if(!mixed){V report=V::makeObj();report.set("receipt_bytes_full",V::makeNum(original.dump().size()));report.set("receipt_bytes_compact",V::makeNum(compactBytes));report.set("next_request_full_receipt",V::makeNum(beforeRequest));report.set("next_request_compact_receipt",V::makeNum(afterRequest));std::cout<<"ASSISTANT_QUERY_BATCH_PAYLOAD_BENCHMARK "<<report.dump()<<'\n';}
    };
    execute(batch,false);check(batch.dump()==publicBytes,"internal model compaction never changes the public MCP value");
    auto mixed=batch;auto& rows=member(member(mixed,"structuredContent"),"results").arr;member(rows[0],"content").arr.push_back(json(R"({"type":"text","text":"Unique batch evidence"})"));member(rows[0],"content").arr.push_back(json(R"({"type":"image","mimeType":"image/png","data":"offline-preserved-image"})"));auto annotated=member(rows[1],"content").arr[0];annotated.set("annotations",json(R"({"audience":["assistant"]})"));member(rows[1],"content").arr.push_back(annotated);member(mixed,"content").arr[0].set("text",V::makeStr(mixed.find("structuredContent")->dump()));execute(mixed,true);
}
static void transientHttpRetryChecks(){
    ark::AssistantConfig config;config.model="offline-retry";auto tools=ark::AgentSession::tools();
    auto counted=[](V response){response.set("usage",json(R"({"input_tokens":10,"output_tokens":2,"total_tokens":12})"));return response;};
    std::vector<std::uint64_t> delays;ark::AssistantRetryHooks hooks;hooks.jitterMilliseconds=[](){return 0u;};hooks.wait=[&](auto delay,const auto*){delays.push_back(delay);};
    int attempts=0,writes=0,checkpoints=0;std::string failedRequest;ark::AgentSession session;std::vector<std::string> retryMessages;V retained;hooks.checkpoint=[&]{++checkpoints;};
    auto recovered=ark::runAssistant(config,V::makeStr("Commit one parameter, then finish the response"),tools,[&](auto& name,auto& args){++writes;return session.call(name,args);},[&](const auto& event){if(event.providerRetry)retryMessages.push_back(event.text);},nullptr,
        [&](auto&,const V& request,auto*){switch(++attempts){case 1:return counted(toolResponse("committed_before_503","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":10}]})"));case 2:failedRequest=request.dump();throw ark::AssistantHttpError(503,true,2500,"server_is_overloaded");default:check(request.dump()==failedRequest,"retry resends the byte-identical failed request without consuming history/inbox");return counted(finalResponse());}}, {},[&](const V& history){retained=history;},hooks);
    check(attempts==3&&writes==1&&session.revision==1&&session.document.getParam("width")==10&&recovered.rounds==2,"transient retry never replays previously executed CAD tools or becomes another assistant round");
    check(delays==std::vector<std::uint64_t>{2500}&&checkpoints==1&&retryMessages.size()==3&&retryMessages.front().find("HTTP 503 / retry 1/3")==0&&retryMessages.back().find("Connection recovered")==0,"server minimum delay, host checkpoint and visible retry/recovery statuses are honored");
    check(recovered.usage.totalTokens==24&&recovered.usage.reportedResponses==2&&recovered.usage.missingResponses==1&&recovered.performance.providerRequests==3&&recovered.performance.toolCalls==1,"actual transport attempts are counted while failed-attempt usage remains unknown and successful usage is counted once");
    int callOutputs=0;for(const auto& item:recovered.conversation.arr)if(item.find("type")&&item.find("type")->str=="function_call_output")++callOutputs;check(callOutputs==1,"failed HTTP attempts append no duplicated call receipts");
    attempts=0;delays.clear();ark::AssistantUsage exhaustedUsage;ark::AssistantPerformance exhaustedPerformance;bool exhausted=false;
    try{ark::runAssistant(config,V::makeStr("Bound retries"),tools,{},[&](const auto& event){if(event.kind==ark::AssistantEvent::Usage){exhaustedUsage=event.usage;exhaustedPerformance=event.performance;}},nullptr,[&](auto&,auto&,auto*)->V{++attempts;throw ark::AssistantHttpError(503,true);},{},{},hooks);}catch(const ark::AssistantTransportError&){exhausted=true;}
    check(exhausted&&attempts==4&&delays==std::vector<std::uint64_t>({1000,2000,4000})&&exhaustedUsage.missingResponses==4&&exhaustedPerformance.providerRequests==4,"exactly three exponential retries stop after four failed attempts without a turn/token budget");
    for(unsigned status:{400u,401u,403u,429u,501u}){attempts=0;delays.clear();fails([&]{ark::runAssistant(config,V::makeStr("Do not retry action-required HTTP"),tools,{}, {},nullptr,[&](auto&,auto&,auto*)->V{++attempts;throw ark::AssistantHttpError(status,true);},{},{},hooks);},"nontransient status stays a terminal transport failure");check(attempts==1&&delays.empty(),"invalid input/auth/quota and unrelated HTTP statuses are not automatically repeated");}
    for(const char* code:{"insufficient_quota","subscription_sharing_usage_limit_exceeded","route_not_enabled","model_not_found"}){attempts=0;delays.clear();fails([&]{ark::runAssistant(config,V::makeStr("Do not retry quota/configuration"),tools,{}, {},nullptr,[&](auto&,auto&,auto*)->V{++attempts;throw ark::AssistantHttpError(503,true,{},code);},{},{},hooks);},"action-required code overrides transient status");check(attempts==1&&delays.empty(),"quota/configuration never retries even on a misleading 503");}
    for(unsigned status:{500u,502u,504u}){attempts=0;delays.clear();auto result=ark::runAssistant(config,V::makeStr("Retry an eligible pre-response server failure"),tools,{}, {},nullptr,[&](auto&,auto&,auto*){if(++attempts==1)throw ark::AssistantHttpError(status,true);return counted(finalResponse());},{},{},hooks);check(attempts==2&&result.usage.missingResponses==1,"explicit 500/502/504 pre-response failures recover with one bounded retry");}
    for(auto delay:{std::uint64_t{30001},std::numeric_limits<std::uint64_t>::max()}){attempts=0;delays.clear();bool deferred=false;try{ark::runAssistant(config,V::makeStr("Respect long server wait"),tools,{}, {},nullptr,[&](auto&,auto&,auto*)->V{++attempts;throw ark::AssistantHttpError(503,true,delay);},{},{},hooks);}catch(const ark::AssistantTransportError& error){deferred=std::string(error.what()).find("deferred")!=std::string::npos;}check(deferred&&attempts==1&&delays.empty(),"a valid Retry-After above the supported maximum is deferred rather than clamped to an early retry");}
    std::atomic_bool cancelled=false;attempts=0;delays.clear();auto cancelHooks=hooks;cancelHooks.wait=[&](auto delay,const auto*){delays.push_back(delay);cancelled=true;};
    fails([&]{ark::runAssistant(config,V::makeStr("Cancel recovery"),tools,{}, {},&cancelled,[&](auto&,auto&,auto*)->V{++attempts;throw ark::AssistantHttpError(503,true);},{},{},cancelHooks);},"cancellation interrupts retry backoff");check(attempts==1&&delays.size()==1,"cancelled backoff sends no next provider request");
    cancelled=false;std::atomic_bool waiting=false;attempts=0;const auto began=std::chrono::steady_clock::now();
    std::jthread cancelDuringWait([&](std::stop_token stop){while(!waiting&&!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(1));if(stop.stop_requested())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));cancelled=true;});
    fails([&]{ark::runAssistant(config,V::makeStr("Cancel the real short-poll wait"),tools,{},[&](const auto& event){if(event.providerRetry&&event.text.find("HTTP 503 / retry")==0)waiting=true;},&cancelled,[&](auto&,auto&,auto*)->V{++attempts;throw ark::AssistantHttpError(503,true);});},"default retry waiter observes cancellation");
    check(attempts==1&&std::chrono::steady_clock::now()-began<std::chrono::seconds(1),"default bounded wait cancels promptly without sleeping through its delay");
    attempts=0;delays.clear();writes=0;V interruptedHistory;ark::AgentSession interrupted;
    fails([&]{ark::runAssistant(config,V::makeStr("Do not replay an incomplete output stream"),tools,[&](auto& name,auto& args){++writes;return interrupted.call(name,args);},{},nullptr,[&](auto&,auto&,auto*){if(++attempts==1)return counted(toolResponse("before_stream_failure","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":10}]})"));throw ark::AssistantTransportError("stream interrupted after output began");},{},[&](const V& history){interruptedHistory=history;},hooks);},"stream failure requires explicit retained-history recovery");
    check(attempts==2&&writes==1&&interrupted.revision==1&&delays.empty()&&interruptedHistory.dump().find("function_call_output")!=std::string::npos,"incomplete streaming or ambiguous transport failures never auto retry or erase prior tool receipts");
    attempts=0;delays.clear();fails([&]{ark::runAssistant(config,V::makeStr("No string-matched retries"),tools,{}, {},nullptr,[&](auto&,auto&,auto*)->V{++attempts;throw std::runtime_error("HTTP 503 text alone");},{},{},hooks);},"ordinary errors are not classified by message text");check(attempts==1&&delays.empty(),"only typed pre-output HTTP errors can enter retry recovery");
    const ark::AssistantHttpError sanitized(503,true,{},"unsafe\ncredential", "unsafe request id", "unsafe\rlabel");check(std::string(sanitized.what()).find("credential")==std::string::npos&&sanitized.errorCode.empty()&&sanitized.requestId.empty(),"HTTP diagnostics contain bounded machine fields and never raw error bodies");
}
int main(int argc,char** argv){try{
    ark::AssistantConfig config;config.model="test-model";
    V tools=json(R"([{"name":"doc_info","description":"Read document","inputSchema":{"type":"object"}}])");
    if(argc==3&&(std::string(argv[1])=="--local-http"||std::string(argv[1])=="--cancel-local-http")){
        config.endpoint=argv[2];config.apiKeyEnv="";config.timeoutSeconds=2;
        std::atomic_bool cancel=false;bool doCancel=std::string(argv[1])=="--cancel-local-http";
        std::jthread trigger([&]{if(doCancel){std::this_thread::sleep_for(std::chrono::milliseconds(150));cancel=true;}});
        auto result=ark::runAssistant(config,V::makeStr("offline transport test"),tools,[](auto&,auto&){return V::makeObj();},{},&cancel);
        std::cout<<result.text<<'\n';return 0;
    }
    auto converted=ark::assistantFunctionTools(tools);
    check(converted.arr.size()==1,"tool count");check(converted.arr[0].find("strict")->b==false,"non-strict extensible geometry schema");
    check(converted.arr[0].find("parameters")->dump()==tools.arr[0].find("inputSchema")->dump(),"preserve schema");
    V conversation=json(R"([{"role":"user","content":"Inspect"}])");
    auto request=ark::assistantRequest(config,conversation,tools);
    check(!request.find("store")->b,"no stored response");check(!request.find("parallel_tool_calls")->b,"serial tools");
    check(request.find("include")->arr[0].str=="reasoning.encrypted_content","request portable reasoning");
    check(request.find("instructions")->str.find("agent_onboard")!=std::string::npos,"onboarding instruction");
    const auto policy=request.find("instructions")->str;
    check(policy.find("For an open-ended design brief")!=std::string::npos&&policy.find("coherent editable concept")!=std::string::npos&&policy.find("explicitly unspecified")!=std::string::npos,"open design starts with editable concept geometry instead of mandatory fabrication decisions");
    check(policy.find("Manufacturing and component-library checks are conditional")!=std::string::npos&&policy.find("When the user asks for fabrication readiness")!=std::string::npos&&policy.find("User-specified dimensions, materials, process and functional constraints apply from the beginning")!=std::string::npos,"concept freedom preserves explicit production requests and user constraints");
    check(policy.find("do not force catalogue searches, load calculations or production-route decisions before creating the concept")!=std::string::npos&&policy.find("native fillet/chamfer")!=std::string::npos&&policy.find("Static guides contain no live document state")!=std::string::npos,"concept planning permits curved authoring while retaining revision and evidence authority");
    check(policy.find("actual image pixels")!=std::string::npos&&policy.find("from its path alone")!=std::string::npos,"snapshot observation requires actual supplied pixels");
    check(request.dump().find("OPENAI_API_KEY")==std::string::npos,"credential configuration excluded from request");
    check(request.find("reasoning")==nullptr,"provider default reasoning is omitted");
    auto plan=config;plan.authMode=ark::AssistantAuthMode::ChatGptPlan;plan.model="gpt-6.1-sol";plan.reasoningEffort="low";
    auto planRequest=ark::assistantRequest(plan,conversation,tools);check(planRequest.find("reasoning")->find("effort")->str=="low","explicit reasoning effort is sent");
    check(planRequest.find("tools")->arr.size()==1&&planRequest.find("tools")->arr[0].find("type")->str=="namespace","ChatGPT tools are namespaced");
    plan.reasoningEffort="none";check(ark::assistantRequest(plan,conversation,tools).find("reasoning")->find("effort")->str=="none","plan model-effort support is deferred to account metadata");
    plan.reasoningEffort="ultra";ark::validateAssistantConfig(plan);check(ark::assistantRequest(plan,conversation,tools).find("reasoning")->find("effort")->str=="ultra","account-advertised plan ultra passes through when explicit");
    auto apiEffort=config;apiEffort.model="gpt-6.1-sol";apiEffort.reasoningEffort="none";fails([&]{ark::validateAssistantConfig(apiEffort);},"API-key known model rejects unsupported none effort");
    apiEffort.reasoningEffort="ultra";fails([&]{ark::validateAssistantConfig(apiEffort);},"API-key route rejects nonpublic ultra effort");
    plan.reasoningEffort="low";
    auto invalid=config;invalid.model="";fails([&]{ark::validateAssistantConfig(invalid);},"model required");
    for(auto url:{"http://example.com/v1/responses","http://localhost.evil/v1/responses","https://user:pass@example.com/api","https:///api","https://example.com/api?key=x","https://example.com/api\r\nInjected:x"}){
        invalid=config;invalid.endpoint=url;fails([&]{ark::validateAssistantConfig(invalid);},"reject invalid endpoint");
    }
    for(auto url:{"http://localhost:1234/v1/responses","http://127.0.0.1/v1/responses","http://[::1]:1234/v1/responses"}){
        invalid=config;invalid.endpoint=url;fails([&]{ark::validateAssistantConfig(invalid);},"no credentials on HTTP");
        invalid.apiKeyEnv="";ark::validateAssistantConfig(invalid);check(ark::assistantKeyAvailable(invalid),"unauthenticated local connector");
    }
    invalid=config;invalid.apiKeyEnv="ARK_ASSISTANT_TEST_UNSET_5438D71F";check(!ark::assistantKeyAvailable(invalid),"presence-only missing credential check");
    invalid=config;invalid.maxRounds=33;fails([&]{ark::validateAssistantConfig(invalid);},"round bound");
    auto duplicate=tools;duplicate.arr.push_back(tools.arr[0]);fails([&]{ark::assistantFunctionTools(duplicate);},"duplicate tool rejected");
    int transports=0,calls=0;std::vector<ark::AssistantEvent::Kind> events;
    auto result=ark::runAssistant(config,V::makeStr("Inspect"),tools,[&](auto& name,auto& args){++calls;check(name=="doc_info"&&args.t==V::Obj,"tool dispatch");return json(R"({"revision":42})");},[&](const auto& e){events.push_back(e.kind);},nullptr,[&](const auto&,const auto& body,const auto*){
        ++transports;
        if(transports==1)return toolResponse();
        auto history=body.find("input");check(history->arr.size()==4,"history includes prompt reasoning call result");
        check(history->arr[1].find("encrypted_content")->str=="opaque-reasoning","reasoning roundtrip");
        check(history->arr[3].find("call_id")->str=="call_1","result call ID");
        check(history->arr[3].find("output")->str==R"({"revision":42})","actual tool result propagated");return finalResponse();
    });
    check(calls==1&&transports==2&&result.rounds==2,"bounded two round loop");check(result.text=="Done.","final text");
    check(result.conversation.arr.size()==5,"retain complete history");check(events.size()==7,"status/tool/text and usage events");
    check(result.usage.reportedResponses==0&&result.usage.missingResponses==2,"missing usage is explicitly unknown");
    check(ark::assistantUsageLabel(result.usage).find("unavailable")!=std::string::npos,"unknown usage label");
    auto runWith=[&](V response){return ark::runAssistant(config,V::makeStr("test"),tools,[&](auto&,auto&){++calls;return V{};},{},nullptr,[&](auto&,auto&,auto*){return response;});};
    V reasoningOnly=json(R"({"status":"completed","output":[{"type":"reasoning","id":"rs_no_action","summary":[]}],"usage":{"input_tokens":5940,"output_tokens":20,"total_tokens":5960,"output_tokens_details":{"reasoning_tokens":20}}})");
    ark::AssistantUsage noActionUsage;std::string noActionError;calls=0;
    try{ark::runAssistant(plan,V::makeStr("Create a model"),tools,[&](auto&,auto&){++calls;return V{};},[&](const auto& event){if(event.kind==ark::AssistantEvent::Usage)noActionUsage=event.usage;},nullptr,[&](auto&,auto&,auto*){return reasoningOnly;});}catch(const std::runtime_error& error){noActionError=error.what();}
    check(noActionError.find("without a visible reply or ARK tool call")!=std::string::npos&&noActionUsage.totalTokens==5960&&calls==0,"reasoning-only completed response reports billed usage and clear no-action error");
    int optionalNamespaceCalls=0,planRounds=0;
    auto optionalNamespace=ark::runAssistant(plan,V::makeStr("Inspect"),tools,[&](auto& name,auto&){check(name=="doc_info","optional namespace preserves tool identity");++optionalNamespaceCalls;return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){return ++planRounds==1?toolResponse("call_optional","doc_info"):finalResponse();});
    check(optionalNamespaceCalls==1&&optionalNamespace.text=="Done.","documented optional namespace still dispatches unique advertised ARK tool");
    int priorCalls=0,priorRounds=0;std::string priorError;
    try{ark::runAssistant(config,V::makeStr("Edit"),tools,[&](auto&,auto&){++priorCalls;return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){return ++priorRounds==1?toolResponse("call_prior","doc_info"):reasoningOnly;});}catch(const std::runtime_error& error){priorError=error.what();}
    check(priorCalls==1&&priorError.find("Earlier ARK tool calls may have changed")!=std::string::npos,"no final visible reply warns about prior tool calls");
    // Reproduce the direct-plan wire shape: completed output items arrive before
    // a terminal response whose output array is empty. Carry the recovered tool
    // call through the real AgentSession and continue with its actual result.
    ark::AgentSession disposable;V applyTools=json(R"([{"name":"doc_apply","description":"Atomic document edit","inputSchema":{"type":"object"}}])");
    auto doneEvent=[](int index,V item){V event=V::makeObj();event.set("type",V::makeStr("response.output_item.done"));event.set("output_index",V::makeNum(index));event.set("item",std::move(item));return std::string("data: ")+event.dump()+"\n\n";};
    auto terminalEvent=[](int in,int out){V usage=V::makeObj(),response=V::makeObj(),event=V::makeObj();usage.set("input_tokens",V::makeNum(in));usage.set("output_tokens",V::makeNum(out));usage.set("total_tokens",V::makeNum(in+out));response.set("status",V::makeStr("completed"));response.set("output",V::makeArr());response.set("usage",usage);event.set("type",V::makeStr("response.completed"));event.set("response",response);return std::string("data: ")+event.dump()+"\n\n";};
    V doneReason=json(R"({"type":"reasoning","id":"rs_box","summary":[],"encrypted_content":"offline-opaque"})");
    V boxCall=json(R"({"type":"function_call","namespace":"ark","name":"doc_apply","call_id":"call_box"})");
    boxCall.set("arguments",V::makeStr(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Offline box","p":{"dx":10,"dy":12,"dz":8}}]})"));
    const V firstWire=ark::parseChatGptSse(doneEvent(0,doneReason)+doneEvent(1,boxCall)+terminalEvent(100,10));
    V finalMessage=json(R"({"type":"message","role":"assistant","content":[{"type":"output_text","text":"Box created."}]})");
    const V secondWire=ark::parseChatGptSse(doneEvent(0,finalMessage)+terminalEvent(20,5));
    int wireRounds=0,wireCalls=0;std::vector<ark::AssistantUsage> wireUsage;
    auto modeled=ark::runAssistant(plan,V::makeStr("Create a box"),applyTools,[&](const std::string& name,const V& args){++wireCalls;check(name=="doc_apply","recovered tool identity");return disposable.call(name,args);},[&](const ark::AssistantEvent& event){if(event.kind==ark::AssistantEvent::Usage)wireUsage.push_back(event.usage);},nullptr,[&](const ark::AssistantConfig&,const V& request,const std::atomic_bool*){
        if(++wireRounds==1)return firstWire;
        const auto& history=request.find("input")->arr;check(history.size()==4&&history.back().find("type")->str=="function_call_output","continuation carries recovered call and tool result");
        V returned;check(arkjson::parse(history.back().find("output")->str,returned)&&!returned.find("isError")->b,"real doc_apply succeeded before continuation");return secondWire;
    });
    check(wireCalls==1&&disposable.revision==1&&disposable.document.features.size()==1&&disposable.document.features[0].name=="Offline box","streamed tool item creates exactly one analytic box");
    check(modeled.text=="Box created."&&wireRounds==2&&modeled.usage.totalTokens==135&&modeled.usage.reportedResponses==2&&wireUsage.size()==2&&wireUsage[0].totalTokens==110&&wireUsage[1].totalTokens==135,"full SSE-to-document chain returns text and counts each provider response once");
    calls=0;fails([&]{runWith(toolResponse("call_a","shell_execute"));},"unknown tool rejected");check(calls==0,"unknown tool not executed");
    auto mixed=toolResponse();V outputs=*mixed.find("output");auto bad=toolResponse("call_bad","shell_execute");outputs.arr.push_back(bad.find("output")->arr.back());mixed.set("output",outputs);
    fails([&]{runWith(mixed);},"whole response validated before dispatch");check(calls==0,"no partial dispatch before invalid tool");
    auto incomplete=toolResponse();incomplete.set("status",V::makeStr("incomplete"));fails([&]{runWith(incomplete);},"incomplete rejected");check(calls==0,"incomplete tool not executed");
    auto failed=toolResponse();failed.set("error",json(R"({"message":"provider problem"})"));fails([&]{runWith(failed);},"provider error rejected");
    transports=0;calls=0;
    ark::runAssistant(config,V::makeStr("test"),tools,[&](auto&,auto&){++calls;return V{};},{},nullptr,[&](auto&,const auto& body,auto*){
        if(++transports==1)return toolResponse("call_malformed","doc_info","[]");
        V error;check(arkjson::parse(body.find("input")->arr.back().find("output")->str,error)&&error.find("isError")->b,"malformed args reported back");return finalResponse();
    });check(calls==0,"malformed args not dispatched");
    transports=0;
    ark::runAssistant(config,V::makeStr("test"),tools,[](auto&,auto&)->V{throw std::runtime_error("sensitive error detail");},{},nullptr,[&](auto&,const auto& body,auto*){
        if(++transports==1)return toolResponse();
        auto data=body.find("input")->arr.back().find("output")->str;
        check(data.find("sensitive")==std::string::npos&&data.find("isError")!=std::string::npos,"callback exceptions sanitized");return finalResponse();
    });
    transports=0;
    ark::runAssistant(config,V::makeStr("test"),tools,[](auto&,auto&){return json(R"({"isError":true,"structuredContent":{"error":"Stale revision","revision":17}})");},{},nullptr,[&](auto&,const auto& body,auto*){
        if(++transports==1)return toolResponse();
        V data;check(arkjson::parse(body.find("input")->arr.back().find("output")->str,data),"tool error is JSON");
        check(data.find("isError")->b&&data.find("structuredContent")->find("revision")->num==17,"tool error and revision preserved");return finalResponse();
    });
    std::atomic_bool cancel=true;transports=0;
    fails([&]{ark::runAssistant(config,V::makeStr("test"),tools,{}, {},&cancel,[&](auto&,auto&,auto*){++transports;return finalResponse();});},"early cancellation");check(transports==0,"cancel before network");
    cancel=false;calls=0;fails([&]{ark::runAssistant(config,V::makeStr("test"),tools,[&](auto&,auto&){++calls;return V{};},{},&cancel,[&](auto&,auto&,auto*){cancel=true;return toolResponse();});},"cancel after response");check(calls==0,"no mutation after cancellation");
    invalid=config;invalid.maxRounds=1;
    fails([&]{ark::runAssistant(invalid,V::makeStr("test"),tools,[](auto&,auto&){return V::makeObj();},{},nullptr,[&](auto&,auto&,auto*){return toolResponse();});},"round exhaustion explicit");
    fails([&]{runWith(toolResponse(""));},"missing call ID rejected");
    fails([&]{ark::runAssistant(config,result.conversation,tools,[](auto&,auto&){return V{};},{},nullptr,[&](auto&,auto&,auto*){return toolResponse();});},"reused historical call ID rejected");
    invalid=config;invalid.maxResponseBytes=1024;
    fails([&]{ark::runAssistant(invalid,V::makeStr("test"),tools,{}, {},nullptr,[&](auto&,auto&,auto*){auto huge=finalResponse();huge.set("padding",V::makeStr(std::string(1024,'x')));return huge;});},"response byte bound");
    invalid=config;invalid.maxConversationBytes=1024;
    fails([&]{ark::assistantRequest(invalid,conversation,tools);},"conversation byte bound");
    // Usage across tool rounds includes reasoning and cache only as subsets.
    transports=0;std::vector<ark::AssistantUsage> snapshots;
    auto counted=ark::runAssistant(config,V::makeStr("count"),tools,[](auto&,auto&){return V::makeObj();},
        [&](const auto& e){if(e.kind==ark::AssistantEvent::Usage)snapshots.push_back(e.usage);},nullptr,
        [&](auto&,auto&,auto*){
            V response=++transports==1?toolResponse():finalResponse();
            response.set("usage",json(transports==1?
                R"({"input_tokens":100,"output_tokens":40,"total_tokens":140,"input_tokens_details":{"cached_tokens":80},"output_tokens_details":{"reasoning_tokens":30}})":
                R"({"input_tokens":150,"output_tokens":20,"total_tokens":170,"input_tokens_details":{"cached_tokens":100},"output_tokens_details":{"reasoning_tokens":10}})"));
            return response;
        });
    check(counted.usage.totalTokens==310&&counted.usage.inputTokens==250&&counted.usage.outputTokens==60,"sum provider usage over tool rounds");
    check(counted.usage.cachedInputTokens==180&&counted.usage.reasoningTokens==40,"cache and reasoning subsets");
    check(counted.usage.reportedResponses==2&&!counted.usage.missingResponses,"complete usage evidence");
    check(snapshots.size()==2&&snapshots[0].totalTokens==140&&snapshots[1].totalTokens==310,"usage events are cumulative snapshots");
    check(ark::assistantUsageLabel(counted.usage).find("partial")==std::string::npos,"complete label");
    ark::AssistantUsage session;ark::accumulateAssistantUsage(session,counted.usage);
    ark::accumulateAssistantUsage(session,result.usage);
    check(session.totalTokens==310&&session.missingResponses==2&&ark::assistantUsageLabel(session).find("partial")!=std::string::npos,"mixed provider support yields partial lower bound");
    for(auto malformed:{R"({"input_tokens":-1,"output_tokens":2,"total_tokens":1})",
                        R"({"input_tokens":1.5,"output_tokens":2,"total_tokens":3.5})",
                        R"({"input_tokens":1,"output_tokens":2,"total_tokens":4})",
                        R"({"input_tokens":1,"output_tokens":2,"total_tokens":3,"input_tokens_details":{"cached_tokens":5}})",
                        R"({"input_tokens":1,"output_tokens":2,"total_tokens":3,"output_tokens_details":{"reasoning_tokens":3}})",
                        R"({"input_tokens":1e30,"output_tokens":2,"total_tokens":1e30})"}){
        auto response=finalResponse();response.set("usage",json(malformed));auto r=runWith(response);
        check(r.text=="Done."&&r.usage.missingResponses==1&&!r.usage.reportedResponses&&!r.usage.totalTokens,"malformed usage does not invent tokens or block modeling");
    }
    auto zero=finalResponse();zero.set("usage",json(R"({"input_tokens":0,"output_tokens":0,"total_tokens":0})"));
    check(runWith(zero).usage.reportedResponses==1,"reported zero differs from absent usage");
    auto incompleteCounted=finalResponse();incompleteCounted.set("status",V::makeStr("incomplete"));
    incompleteCounted.set("usage",json(R"({"input_tokens":10,"output_tokens":5,"total_tokens":15})"));
    ark::AssistantUsage partial;
    fails([&]{ark::runAssistant(config,V::makeStr("count"),tools,{},
        [&](const auto& e){if(e.kind==ark::AssistantEvent::Usage)partial=e.usage;},nullptr,
        [&](auto&,auto&,auto*){return incompleteCounted;});},"incomplete response remains rejected");
    check(partial.totalTokens==15,"usage from incomplete response survives via event");
    requestContextChecks();payloadAndPerformanceChecks();nestedReceiptChecks();transientHttpRetryChecks();visionChecks();std::cout<<checks<<" assistant checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"assistant test failed: "<<e.what()<<'\n';return 1;}}
