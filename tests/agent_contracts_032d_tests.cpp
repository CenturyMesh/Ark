#include "ark/agent.hpp"
#include "ark/native_backend.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace ark;using V=arkjson::Value;size_t checks=0;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing receipt field: ")+key);return *found;}
V json(const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid public synthetic fixture JSON");return value;}
V accepted(V value){check(!field(value,"isError").b,value.dump());return field(value,"structuredContent");}
V call(AgentSession& s,const char* tool,const char* args){return accepted(s.call(tool,json(args)));}
V apply(AgentSession& s,V commands,const char* detail="changes"){V args=V::makeObj();args.set("expected_revision",V::makeNum(s.revision));args.set("commands",commands);args.set("detail",V::makeStr(detail));return accepted(s.call("doc_apply",args));}
void setup(AgentSession& s){s.suppressIterationHistory();s.suppressFailureCapture();V commands=V::makeArr();for(int i=0;i<48;++i){V command=json(R"({"op":"feature.add","type":"box","p":{"dx":4,"dy":5,"dz":6}})");command.set("name",V::makeStr("Fixture block "+std::to_string(i)));commands.arr.push_back(command);}apply(s,commands);}
void bytes(const char* label,const V& full,const V& compact){check(compact.dump().size()<full.dump().size(),std::string(label)+" must reduce actual serialized JSON bytes on the identical document");std::cout<<label<<" full_bytes="<<full.dump().size()<<" compact_bytes="<<compact.dump().size()<<'\n';}
void unchanged(const AgentSession& s,const std::string& before,u64 revision){check(documentToJson(s.document)==before&&s.revision==revision,"Read or rejected operation must retain authoring and observed revision");}
void rejects(AgentSession& s,const char* tool,const char* args,const char* diagnostic){const auto before=documentToJson(s.document);const auto revision=s.revision;auto result=s.call(tool,json(args));check(field(result,"isError").b,result.dump());check(result.dump().find(diagnostic)!=std::string::npos,std::string("Expected diagnostic ")+diagnostic+": "+result.dump());unchanged(s,before,revision);}
void compactDiscovery(){
    AgentSession s;setup(s);const auto before=documentToJson(s.document);const auto revision=s.revision;
    const auto full=call(s,"doc_info","{}"),compact=call(s,"doc_info",R"({"detail":"revision"})");
    for(const char* key:{"revision","units","feature_count","recompute_ms","features_reused","features_evaluated","parameter_error","history_tip"})check(field(full,key).dump()==field(compact,key).dump(),std::string("Status projection must preserve ")+key);
    check(!compact.find("features")&&!compact.find("sketches")&&!compact.find("drawings"),"Revision receipt excludes object inventories");bytes("doc_info",full,compact);
    const auto onboard=call(s,"agent_onboard",R"({"topic":"overview"})"),guide=call(s,"agent_onboard",R"({"topic":"overview","detail":"guide"})");
    check(onboard.find("document")&&!guide.find("document")&&!field(guide,"document_state_included").b,"Guide-only onboarding explicitly excludes document state");
    for(const auto& item:onboard.obj)if(item.first!="document"&&item.first!="detail"&&item.first!="document_state_included")check(field(guide,item.first.c_str()).dump()==item.second.dump(),"Guide preserves complete topic rules and authority/limits");bytes("onboarding",onboard,guide);unchanged(s,before,revision);
    rejects(s,"doc_info",R"({"detail":"revision","ids":[1]})","require detail design");rejects(s,"agent_onboard",R"({"detail":"guide","commands":[]})","Unknown agent_onboard field");
}
void batchContracts(){
    AgentSession s;setup(s);const auto before=documentToJson(s.document);const auto revision=s.revision;
    V definition;for(const auto& tool:AgentSession::tools().arr)if(field(tool,"name").str=="query_batch")definition=tool;
    const auto& choices=field(field(field(field(field(field(definition,"inputSchema"),"properties"),"queries"),"items"),"properties"),"tool");std::set<std::string> names;for(const auto& name:field(choices,"enum").arr)names.insert(name.str);
    check(names.count("object_context")&&names.count("agent_onboard")&&names.count("native_inspect"),"Discovery publishes bounded context/onboarding and exact geometry reads");
    check(!names.count("query_batch")&&!names.count("doc_apply")&&!names.count("export_step")&&!names.count("doc_save"),"Discovered batch allowlist excludes recursion and writes");
    const auto batch=call(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{"detail":"revision"},"fields":["revision","feature_count"]},{"tool":"agent_onboard","args":{"topic":"overview","detail":"guide"},"fields":["topic","workflow","document_state_included"]},{"tool":"object_context","args":{"ids":[1],"limit":1},"fields":["revision","objects","partial","evidence_limits"]},{"tool":"eval_expr","args":{"expr":"unknown_parameter"}}]})");
    const auto& rows=field(batch,"results").arr;check(rows.size()==4&&field(batch,"revision").num==revision,"Read batch retains a single observed revision");
    check(field(field(rows[0],"structuredContent"),"revision").num==revision&&field(rows[0],"structuredContent").obj.size()==2,"fields returns exactly the requested top-level result names");
    check(field(rows[3],"isError").b&&field(rows[3],"structuredContent").find("error"),"Individual query errors preserve the real error envelope");
    for(const auto& row:rows){check(row.find("elapsed_ms")&&row.find("content"),"Each row retains elapsed time and text envelope");if(!field(row,"isError").b)check(field(field(row,"content").arr[0],"text").str==field(row,"structuredContent").dump(),"Projection updates text and structured content consistently");}unchanged(s,before,revision);
    rejects(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{},"fields":["misspelled"]}]})","available fields");
    rejects(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{},"fields":["revision","revision"]}]})","distinct nonempty");
    rejects(s,"query_batch",R"({"queries":[{"tool":"agent_onboard","args":{"topic":"overview"}}]})","explicit detail:guide");
    rejects(s,"query_batch",R"({"queries":[{"tool":"query_batch","args":{"queries":[]}}]})","recursive batches");
    rejects(s,"query_batch",R"({"queries":[{"tool":"doc_apply","args":{"commands":[]}}]})","tool is not allowed");
    rejects(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{},"path":"revision"}]})","accepted: tool, args, fields");
}
void previewAndAtomicity(){
    AgentSession s;setup(s);const auto before=documentToJson(s.document);const auto revision=s.revision;
    V args=json(R"({"detail":"design","commands":[{"op":"feature.edit","id":1,"p":{"dx":9}}],"checks":[{"id":"positive","query":{"tool":"eval_expr","args":{"expr":"2+3"},"path":"value"},"op":"eq","value":5}]})");args.set("expected_revision",V::makeNum(revision));const auto full=accepted(s.call("doc_preview",args));args.set("detail",V::makeStr("changes"));const auto compact=accepted(s.call("doc_preview",args));
    check(field(full,"valid").b&&field(compact,"valid").b&&field(compact,"candidate_cached").b,"Compact preview remains an actually evaluated retained candidate");
    for(const char* key:{"created_ids","created_node_ids","created_mate_ids","sheet_edits","geometry_recomputed"})check(field(field(full,"candidate_result"),key).dump()==field(field(compact,"candidate_result"),key).dump(),"Compact candidate keeps complete apply receipts");
    const auto& conditions=field(field(compact,"candidate_result"),"conditions");check(field(conditions,"passed").b,"Compact preview retains successful measured postconditions");
    check(!field(compact,"candidate_result").find("features")&&field(field(compact,"after"),"partial").b,"Changes preview has no hidden full candidate inventory");bytes("preview",full,compact);unchanged(s,before,revision);
    const auto committed=accepted(s.call("doc_apply",field(compact,"apply")));check(field(committed,"applied_prepared").b&&!field(committed,"geometry_recomputed").b,"Compact candidate commits checked geometry without a second recompute");check(s.document.find(1)->p.at("dx")=="9","Prepared apply publishes the actual proposed recipe");
    bool measured=false;for(const auto& body:s.document.evalBodies)if(body.id==1){const auto bounds=body.tessellate(s.document.tessOpt).computeBBox();measured=bounds.valid()&&std::abs(bounds.mx.x-bounds.mn.x-9)<1e-6;}check(measured,"Prepared apply publishes the actual evaluated target geometry");
    const auto after=documentToJson(s.document);const auto appliedRevision=s.revision;auto stale=s.call("doc_apply",field(full,"apply"));check(field(stale,"isError").b,"Old full preview remains revision guarded");unchanged(s,after,appliedRevision);
    V invalid=json(R"({"detail":"changes","commands":[{"op":"feature.edit","id":1,"p":{"dx":10}}],"checks":[{"query":{"tool":"eval_expr","args":{"expr":"1"},"path":"value"},"op":"eq","value":2}]})");invalid.set("expected_revision",V::makeNum(s.revision));check(field(s.call("doc_apply",invalid),"isError").b,"Compact apply cannot bypass failed measured checks");unchanged(s,after,appliedRevision);
}
void assemblyMeasurement(){
    AgentSession s;setup(s);apply(s,json(R"([{"op":"assembly.add","name":"Left","feature":1,"p":{"tx":0}},{"op":"assembly.add","name":"Right","feature":1,"p":{"tx":30}}])"));
    const auto before=documentToJson(s.document);const auto revision=s.revision;const auto full=call(s,"assembly_info",R"({"measure":{"a":{"node":"Left"},"b":{"node":"Right"}}})"),compact=call(s,"assembly_info",R"({"detail":"result","measure":{"a":{"node":"Left"},"b":{"node":"Right"}}})");
    check(field(full,"measure").dump()==field(compact,"measure").dump()&&field(field(compact,"measure"),"distance_mm").num==30,"Compact assembly measurement preserves measured distance and both frames");
    check(!compact.find("features")&&!compact.find("instances")&&!compact.find("bom")&&!compact.find("mass_checked"),"Result-only wrapper omits unrelated inventories and aggregate evidence");bytes("assembly_measure",full,compact);unchanged(s,before,revision);
    rejects(s,"assembly_info",R"({"detail":"result","measure":{"a":{"node":"Left"},"b":{"node":"Right"}},"interference":true})","does not accept");
    rejects(s,"assembly_info",R"({"detail":"result"})","exactly one");
}
#if defined(ARK_CONTRACTS_NATIVE_TESTS)
void nativeProperties(){AgentSession s;s.suppressIterationHistory();s.suppressFailureCapture();apply(s,json(R"([{"op":"feature.add","type":"native_solid","name":"Exact fixture","native":{"op":"box"},"p":{"width":4,"depth":5,"height":6}}])"));const auto before=documentToJson(s.document);const auto revision=s.revision;const auto full=call(s,"native_inspect",R"({"id":1})"),compact=call(s,"native_inspect",R"({"id":1,"detail":"properties"})");for(const char* key:{"brep_valid","volume_mm3","area_mm2","bounds_mm","display_mesh_authoritative","self_intersections_checked","manufacturing_authoritative"})check(field(field(full,"report"),key).dump()==field(field(compact,"report"),key).dump(),"Native compact report preserves exact measurements and authority flags");check(std::abs(field(field(compact,"report"),"volume_mm3").num-120)<1e-9,"Exact synthetic solid has the authored measured volume");check(!compact.find("topology")&&!compact.find("holes")&&!field(compact,"topology_included").b,"Properties mode explicitly omits topology and hole/operation inventories");bytes("native_properties",full,compact);unchanged(s,before,revision);}
#endif
}
int main(){try{compactDiscovery();batchContracts();previewAndAtomicity();assemblyMeasurement();
#if defined(ARK_CONTRACTS_NATIVE_TESTS)
nativeProperties();
#endif
std::cout<<"032D agent contract checks passed: "<<checks<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"032D agent contract failure: "<<error.what()<<'\n';return 1;}}
