#include "ark/agent.hpp"
#include "ark/assistant.hpp"
#include "ark/assistant_connectors.hpp"
#include "ark/serialize.hpp"
#include "ark/onboarding.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
using namespace ark;using V=arkjson::Value;int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
V json(const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("fixture JSON");return v;}
const V& field(const V& v,const char* key){auto p=v.find(key);if(!p)throw std::runtime_error(std::string("missing ")+key);return *p;}
V accepted(const V& v){if(field(v,"isError").b)throw std::runtime_error(v.dump());return field(v,"structuredContent");}
V apply(AgentSession& s,const char* commands){V args=V::makeObj();args.set("expected_revision",V::makeNum(s.revision));args.set("commands",json(commands));args.set("detail",V::makeStr("changes"));return accepted(s.call("doc_apply",args));}
V preview(AgentSession& s,const char* commands){V args=V::makeObj();args.set("expected_revision",V::makeNum(s.revision));args.set("commands",json(commands));args.set("detail",V::makeStr("changes"));return accepted(s.call("doc_preview",args));}
void packedRetention(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();apply(s,R"([{"op":"feature.add","type":"box","name":"Base","p":{"dx":2,"dy":3,"dz":4}},{"op":"feature.add","type":"box","name":"Leaf","p":{"dx":4,"dy":5,"dz":6}}])");
    // Public allocation stress: numeric display-channel storage, no private CAD
    // geometry or claim that this synthetic payload is manufacturing evidence.
    V payload=V::makeObj(),mesh=V::makeObj(),pos=V::makeArr(),idx=V::makeArr();
    for(int i=0;i<900000;++i){pos.arr.push_back(V::makeNum(i%3));idx.arr.push_back(V::makeNum(i%3));}
    mesh.set("pos",std::move(pos));mesh.set("idx",std::move(idx));payload.set("mesh",std::move(mesh));
    Body body=*s.document.evaluationCache.at(1).body;body.nativeData=std::move(payload);
    check(body.nativeData.packedMesh()&&body.nativeData.maximumRetainedBytes()>128u*1024u*1024u&&body.nativeData.allocatedBytes()<128u*1024u*1024u,"packed fixture distinguishes actual allocation from hypothetical expansion");
    s.document.evaluationCache.at(1).body=std::make_shared<const Body>(body);s.document.evalBodies[0]=body;
    const auto candidate=preview(s,R"([{"op":"feature.edit","id":2,"p":{"dx":7}}])");check(field(candidate,"candidate_cached").b,"independent leaf preview is cached");
    check(field(candidate,"candidate_additional_bytes").num<field(candidate,"candidate_budget_bytes").num,"preview exposes measured additional retention");
    apply(s,R"([{"op":"feature.edit","id":1,"p":{"dx":9}}])");
    check(s.preparedRecipe(field(candidate,"candidate_id").str).t==V::Obj,"former live packed geometry remains retained within actual budget");
    auto stale=s.call("doc_apply",field(candidate,"apply"));check(field(stale,"isError").b,"retention does not authorize stale publication");
    // Expanding shared geometry later is charged at the next tool boundary.
    (void)body.nativeData.value();accepted(s.call("doc_info",json(R"({"detail":"revision"})")));
    bool evicted=false;try{s.preparedRecipe(field(candidate,"candidate_id").str);}catch(...){evicted=true;}check(evicted,"later lazy expansion is recharged and evicts over-budget receipt");
}
void refreshAndCommit(){
    AgentSession s;s.suppressFailureCapture();apply(s,R"([{"op":"feature.add","type":"box","name":"Target","p":{"dx":2,"dy":3,"dz":4}},{"op":"feature.add","type":"box","name":"Peer","p":{"dx":4,"dy":5,"dz":6}}])");
    const auto candidate=preview(s,R"([{"op":"feature.edit","id":1,"p":{"dx":7}}])");apply(s,R"([{"op":"feature.edit","id":2,"p":{"dx":9}}])");
    V args=field(candidate,"apply");args.set("expected_revision",V::makeNum(s.revision));const auto before=documentToJson(s.document);check(field(s.call("doc_apply",args),"isError").b&&documentToJson(s.document)==before,"plain stale candidate rejects atomically");
    args.set("refresh_candidate",V::makeBool(true));args.set("detail",V::makeStr("invalid"));check(field(s.call("doc_apply",args),"isError").b&&documentToJson(s.document)==before,"invalid refresh detail rejects before publication");args.set("detail",V::makeStr("changes"));const auto result=accepted(s.call("doc_apply",args));
    check(field(result,"refresh_applied_atomically").b&&field(result,"applied_prepared").b,"explicit refresh and commit returns both receipts");check(s.document.find(1)->p.at("dx")=="7"&&s.document.find(2)->p.at("dx")=="9","refresh preserves unrelated peer edit and publishes checked target");
    V undo=V::makeObj();undo.set("expected_revision",V::makeNum(s.revision));accepted(s.call("doc_undo",undo));check(s.document.find(1)->p.at("dx")=="2"&&s.document.find(2)->p.at("dx")=="9","atomic refresh is one undoable publication preserving peer history");undo.set("expected_revision",V::makeNum(s.revision));accepted(s.call("doc_redo",undo));check(s.document.find(1)->p.at("dx")=="7","atomic refresh redo restores actual checked edit");
    const auto conflict=preview(s,R"([{"op":"feature.edit","id":1,"p":{"dy":8}}])");apply(s,R"([{"op":"feature.edit","id":1,"p":{"dz":12}}])");args=field(conflict,"apply");args.set("expected_revision",V::makeNum(s.revision));args.set("refresh_candidate",V::makeBool(true));const auto frozen=documentToJson(s.document);const auto rev=s.revision;
    check(field(s.call("doc_apply",args),"isError").b&&documentToJson(s.document)==frozen&&s.revision==rev,"changed dependency rejects refresh-and-commit without mutation");
}
void scopedBom(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();apply(s,R"([{"op":"feature.add","type":"box","name":"Specialist part","p":{"dx":2,"dy":3,"dz":4},"part":{"part_number":"PART-A","description":"Public selected fixture","category":"fastener.screw"}},{"op":"feature.add","type":"box","name":"Other part","p":{"dx":4,"dy":5,"dz":6}}])");
    const auto before=documentToJson(s.document);const auto part=accepted(s.call("bom_inspect",json(R"({"ids":["Specialist part"]})")));
    check(field(part,"items").arr.size()==1&&field(part,"fastener_quantity").num==1&&field(part,"partial").b&&!field(part,"global_review").b,"selected BOM inspection is explicitly partial");
    V args=json(R"({"expected_revision":1,"ids":[1],"scope":"concept","pending_notes":"Material and fastening unresolved.","fastening_review":"Local connections inspected; no hardware qualification."})");
    const auto receipt=accepted(s.call("bom_review",args));check(!field(receipt,"global_review").b&&field(receipt,"selected_feature_ids").arr.size()==1,"selected definition review succeeds after selected inspection");check(!field(s.bomCompletionCheck(),"allowed").b,"partial review cannot satisfy global completion");
    args.obj.erase(std::remove_if(args.obj.begin(),args.obj.end(),[](const auto& x){return x.first=="ids";}),args.obj.end());check(field(s.call("bom_review",args),"isError").b,"global review still requires every current item page");
    check(field(s.call("bom_inspect",json(R"({"ids":[999]})")),"isError").b&&field(s.call("bom_inspect",json(R"({"ids":[1,1]})")),"isError").b,"unknown and duplicate scoped definitions reject");
    auto bad=s.call("object_context",json(R"({"type":"cable"})"));check(field(bad,"isError").b&&bad.dump().find("box")!=std::string::npos,"unknown type lists actual valid alternatives");check(documentToJson(s.document)==before,"scoped review and diagnostics preserve authoring");
}
void proactiveContext(){
    V history=json(R"([{"role":"user","content":"Keep the 240 mm wheelbase and all source references."}])");
    // Past the proactive bound (512 KB) and the recency window kept verbatim (256 KB).
    for(int i=0;i<48;++i){V call=json(R"({"type":"function_call","name":"doc_info","arguments":"{}"})"),output=json(R"({"type":"function_call_output"})"),data=json(R"({"revision":3,"isError":false})");const auto id="context-"+std::to_string(i);call.set("call_id",V::makeStr(id));output.set("call_id",V::makeStr(id));data.set("inventory",V::makeStr(std::string(12000,'x')));output.set("output",V::makeStr(data.dump()));history.arr.push_back(call);history.arr.push_back(output);}
    const auto original=history.dump();AssistantConfig config;config.model="offline-workflow";bool projected=false;
    const auto request=assistantRequest(config,history,AgentSession::tools(),&projected);check(projected&&request.dump().find("Keep the 240 mm wheelbase")!=std::string::npos,"history is proactively compacted below the transport ceiling and preserves user constraints");check(history.dump()==original,"request compaction leaves full host evidence unchanged");
    auto result=runAssistant(config,history,AgentSession::tools(),{}, {},nullptr,[](const auto&,const V&,auto*){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Review only."}]}],"usage":{"input_tokens":100,"output_tokens":2,"total_tokens":102}})");});
    check(result.performance.projectedRequests==1&&result.performance.lastOriginalHistoryBytes==original.size()&&result.performance.lastToolResultBytes>0&&result.performance.lastMessageBytes>0&&result.performance.lastInstructionBytes>0,"request metrics separate original history, messages, results, instructions and schema bytes");
    auto messages=assistantProviderPreset("claude");messages.model="offline-message-context";auto other=runAssistant(messages,history,AgentSession::tools(),{}, {},nullptr,[](const auto&,const V&,auto*){return json(R"({"type":"message","role":"assistant","content":[{"type":"text","text":"Read-only review."}],"stop_reason":"end_turn","usage":{"input_tokens":100,"output_tokens":2}})");});check(other.performance.lastInstructionBytes>0&&other.performance.lastToolResultBytes>0,"Messages protocol measures system instructions and tool-result content blocks separately");
    std::cout<<"context original_history_bytes="<<original.size()<<" projected_input_bytes="<<result.performance.lastInputBytes<<" schema_bytes="<<result.performance.lastToolSchemaBytes<<'\n';
}
void authoringBootstrap(){
    const auto pack=agentAuthoringOnboardingPack();V parsed;if(!arkjson::parse(pack,parsed))throw std::runtime_error("authoring bootstrap pack JSON");
    check(field(parsed,"topics").arr.size()==3,"initial authoring pack supplies overview, sketch and solids without unrelated specialist topics");
    bool sketch=false,solids=false;for(const auto& guide:field(parsed,"topics").arr){check(!guide.find("document")&&!field(guide,"document_state_included").b,"bootstrap documentation contains no document snapshot");if(field(guide,"topic").str=="sketch")sketch=guide.find("example_doc_apply")!=nullptr;if(field(guide,"topic").str=="solids"){solids=guide.find("example_doc_apply")!=nullptr;check(!guide.find("example_doc_apply")->find("expected_revision"),"bootstrap examples do not bake a document revision");}}
    check(sketch&&solids,"initial writer knows actual editable sketch and native authoring examples");
    AssistantConfig config;config.model="offline-authoring-bootstrap";config.onboardingPack=pack;int requests=0;
    auto run=runAssistant(config,V::makeStr("Create a coherent editable concept in this new file."),AgentSession::tools(),{}, {},nullptr,[&](const auto&,const V& request,const auto*){++requests;const auto body=request.dump();check(body.find("ARK_PREBAKED_GUIDE_V1")!=std::string::npos&&body.find("Solid profile")!=std::string::npos&&body.find("example_doc_apply")!=std::string::npos,"very first provider request already contains the core authoring contracts");return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Initial contract inspected; no geometry claim."}]}]})");});
    check(requests==1&&run.rounds==1,"bootstrap evidence requires no onboarding discovery round or paid transport");
}
#ifdef ARK_CONTRACTS_NATIVE_TESTS
void invalidProfileReportsOwnedCause(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    const auto before=documentToJson(session.document);
    V args=json(R"({"expected_revision":0,"commands":[{"op":"sketch.set","name":"Invalid ellipse","sketch":{"plane":"XY","entities":[{"kind":"recipe","name":"Oval","recipe":{"type":"ellipse","center":[0,0],"major":20,"minor":30,"angle":0}}]}}]})");
    const auto sketchOnly=session.call("doc_apply",args);check(field(sketchOnly,"isError").b,"invalid ellipse fails at the owned sketch predicate");const auto sketchError=field(field(sketchOnly,"structuredContent"),"error").str;
    auto commands=field(args,"commands");commands.arr.push_back(json(R"({"op":"feature.add","type":"native_solid","name":"Dependent extrusion","native":{"op":"extrude"},"p":{"sketch":"Invalid ellipse","depth":12}})"));args.set("commands",commands);
    const auto dependent=session.call("doc_apply",args);check(field(dependent,"isError").b,"dependent native extrusion rejects invalid profile atomically");const auto nativeError=field(field(dependent,"structuredContent"),"error").str;
    const auto start=sketchError.find(": ");check(start!=std::string::npos&&nativeError.find(sketchError.substr(start+2))!=std::string::npos,"native profile rejection retains the exact underlying sketch error instead of only a missing/invalid label");
    check(session.revision==0&&documentToJson(session.document)==before,"profile diagnostic improvement preserves source and revision after both rejections");
}
bool visibleStaticGuide(const V& value,V& found,int depth=0){
    if(depth>24)return false;
    if(value.t==V::Obj){
        const auto* topic=value.find("topic"),*mode=value.find("detail"),*example=value.find("example_doc_apply");
        if(topic&&topic->asStr()=="solids"&&mode&&mode->asStr()=="guide"&&example){found=value;return true;}
        for(const auto& entry:value.obj)if(visibleStaticGuide(entry.second,found,depth+1))return true;
    }else if(value.t==V::Arr){for(const auto& item:value.arr)if(visibleStaticGuide(item,found,depth+1))return true;}
    else if(value.t==V::Str){const auto offset=value.str.find('{');V parsed;if(offset!=std::string::npos&&arkjson::parse(value.str.substr(offset),parsed)&&visibleStaticGuide(parsed,found,depth+1))return true;}
    return false;
}
void retainedGuideAuthorsNativeCheckpoint(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    V history=json(R"([{"role":"user","content":"Create an editable circular profile of radius 20 mm and extrude it 12 mm. Preserve those explicit dimensions."}])");
    auto exchange=[&](const std::string& id,const std::string& name,V args,V receipt){V request=V::makeObj(),output=V::makeObj();request.set("type",V::makeStr("function_call"));request.set("call_id",V::makeStr(id));request.set("name",V::makeStr(name));request.set("arguments",V::makeStr(args.dump()));output.set("type",V::makeStr("function_call_output"));output.set("call_id",V::makeStr(id));output.set("output",V::makeStr(receipt.dump()));history.arr.push_back(request);history.arr.push_back(output);};
    const V guideArgs=json(R"({"topic":"solids","detail":"guide"})");const V guideEnvelope=session.call("agent_onboard",guideArgs);const V originalGuide=accepted(guideEnvelope);exchange("loaded-native-guide","agent_onboard",guideArgs,guideEnvelope);
    for(int i=0;i<10;++i){V receipt=V::makeObj();receipt.set("revision",V::makeNum(0));receipt.set("historical_inventory",V::makeStr(std::string(14000,'x')));exchange("earlier-read-"+std::to_string(i),"doc_info",V::makeObj(),receipt);}
    const auto original=history.dump();check(original.size()>96*1024,"native guide continuation crosses the actual request-projection threshold");
    AssistantConfig config;config.model="offline-retained-contract";int rounds=0,writes=0;V retained;
    auto responseCall=[](const std::string& id,const char* name,const V& args){V response=json(R"({"status":"completed","output":[]})"),call=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(name));call.set("arguments",V::makeStr(args.dump()));auto output=*response.find("output");output.arr.push_back(call);response.set("output",output);return response;};
    auto run=runAssistant(config,history,AgentSession::tools(),[&](const auto& name,const V& args){if(name=="doc_apply")++writes;return session.call(name,args);},{},nullptr,[&](const auto&,const V& request,const auto*){
        ++rounds;V current;check(visibleStaticGuide(request,current)&&current.dump()==originalGuide.dump(),"complete native contract remains readable after projection and subsequent tool calls");retained=current;
        if(rounds==1)return responseCall("fresh-required-revision","doc_info",json(R"({"detail":"revision"})"));
        if(rounds==2){V commands=field(retained,"example_doc_apply");commands.set("expected_revision",V::makeNum(session.revision));commands.set("detail",V::makeStr("changes"));return responseCall("create-from-retained-guide","doc_apply",commands);}
        return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Editable profile and native extrusion created; fabrication is unqualified."}]}]})");
    });
    check(rounds==3&&writes==1&&session.revision==1&&session.document.sketches.size()==1&&session.document.features.size()==1,"retained contract reaches one atomic editable native checkpoint without rediscovery");
    const auto native=accepted(session.call("native_inspect",json(R"({"id":1,"detail":"properties"})")));const auto& report=field(native,"report");check(field(report,"brep_valid").b&&std::abs(field(report,"volume_mm3").num-std::acos(-1.)*400*12)<1e-6,"checkpoint produced the exact requested native volume");
    for(size_t i=0;i<history.arr.size();++i)check(run.conversation.arr[i].dump()==history.arr[i].dump(),"native continuation preserves every original host message and receipt");check(history.dump()==original,"native execution does not mutate caller history");
    std::cout<<"RETAINED_NATIVE_CHECKPOINT original_history_bytes="<<original.size()<<" offline_provider_responses="<<rounds<<" writes="<<writes<<" native_volume_mm3="<<field(report,"volume_mm3").num<<" paid_model_calls=0\n";
}
void exactNativeRefresh(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();apply(s,R"([{"op":"feature.add","type":"native_solid","name":"Exact target","native":{"op":"box"},"p":{"width":2,"depth":3,"height":4}},{"op":"feature.add","type":"native_solid","name":"Exact peer","native":{"op":"box"},"p":{"width":4,"depth":5,"height":6}}])");
    V args=json(R"({"expected_revision":1,"detail":"changes","commands":[{"op":"feature.edit","id":1,"p":{"width":7}}],"checks":[{"query":{"tool":"native_inspect","args":{"id":1,"detail":"properties"},"path":"report.brep_valid"},"op":"true"}]})");
    const auto candidate=accepted(s.call("doc_preview",args));check(field(candidate,"candidate_cached").b,"exact native checked preview is retained");apply(s,R"([{"op":"feature.edit","id":2,"p":{"width":9}}])");
    args=field(candidate,"apply");args.set("expected_revision",V::makeNum(s.revision));args.set("refresh_candidate",V::makeBool(true));const auto receipt=accepted(s.call("doc_apply",args));check(field(receipt,"refresh_applied_atomically").b,"native refresh rechecks and publishes atomically");
    const auto target=accepted(s.call("native_inspect",json(R"({"id":1,"detail":"properties"})"))),peer=accepted(s.call("native_inspect",json(R"({"id":2,"detail":"properties"})")));
    check(field(field(target,"report"),"brep_valid").b&&std::abs(field(field(target,"report"),"volume_mm3").num-84)<1e-8,"published native target preserves checked exact volume");check(std::abs(field(field(peer,"report"),"volume_mm3").num-270)<1e-8,"unrelated exact peer edit survives refresh");
}
#endif
}
int main(){try{packedRetention();refreshAndCommit();scopedBom();proactiveContext();authoringBootstrap();
#ifdef ARK_CONTRACTS_NATIVE_TESTS
invalidProfileReportsOwnedCause();retainedGuideAuthorsNativeCheckpoint();exactNativeRefresh();
#endif
std::cout<<checks<<" workflow checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
