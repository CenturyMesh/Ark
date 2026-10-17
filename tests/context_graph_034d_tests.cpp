#include "ark/agent.hpp"
#include "ark/agent_activity.hpp"
#include "ark/assistant.hpp"
#include "ark/iteration_history.hpp"
#include "../src/assistant_context.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
namespace {
using namespace ark;using V=arkjson::Value;int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
V json(const std::string& text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("fixture JSON");return v;}
const V& field(const V& v,const char* key){const auto* p=v.find(key);if(!p)throw std::runtime_error(std::string("missing ")+key);return *p;}
V accepted(const V& v){if(field(v,"isError").b)throw std::runtime_error(v.dump());return field(v,"structuredContent");}
V apply(AgentSession& s,const char* commands){V args=V::makeObj();args.set("expected_revision",V::makeNum(s.revision));args.set("commands",json(commands));args.set("detail",V::makeStr("changes"));return accepted(s.call("doc_apply",args));}
const V* node(const V& graph,const std::string& key){for(const auto& row:field(graph,"nodes").arr)if(field(row,"key").str==key)return &row;return nullptr;}
void preciseStamp(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    apply(s,R"([{"op":"material.set","name":"Precision probe","density":1.0000001}])");
    const auto args=json(R"({"keys":["materials:Precision probe"],"depth":0})");
    auto first=accepted(s.call("context_graph",args));
    apply(s,R"([{"op":"material.set","name":"Precision probe","density":1.0000002}])");
    auto second=accepted(s.call("context_graph",args));
    check(!field(second,"index_reused").b&&field(first,"source_sha256").str!=field(second,"source_sha256").str,"sub-micro density edit invalidates the stamp through normal publication");
    check(field(field(*node(second,"materials:Precision probe"),"facts"),"density").num==1.0000002,"graph returns the exact current density without decimal truncation");
    s.document.materials[0].densityGcm3=1.0000003;const auto revision=s.revision;
    auto direct=accepted(s.call("context_graph",args));check(!field(direct,"index_reused").b&&field(direct,"source_sha256").str==iterationSha256(documentToJson(s.document,false,false))&&s.revision==revision,"in-place small density change has the hash of actual saved authoring");
    PlaneCanvas canvas;canvas.name="Opacity probe";canvas.path="synthetic-not-loaded.png";canvas.opacity=.5000001;s.document.canvases.push_back(canvas);
    auto before=accepted(s.call("context_graph",args));s.document.canvases[0].opacity=.5000002;
    auto after=accepted(s.call("context_graph",args));check(!field(after,"index_reused").b&&field(before,"source_sha256").str!=field(after,"source_sha256").str&&field(after,"source_sha256").str==iterationSha256(documentToJson(s.document,false,false)),"sub-micro canvas opacity change invalidates the saved source hash");
    const auto serializations=contextGraphAuthoringSerializations();auto quiet=accepted(s.call("context_graph",args));check(field(quiet,"index_reused").b&&contextGraphAuthoringSerializations()==serializations,"precise unchanged authoring still avoids full serialization");
}
void parallelStamps(){
    const auto start=contextGraphAuthoringSerializations();std::vector<std::future<std::string>> readers;
    for(int i=0;i<8;++i)readers.push_back(std::async(std::launch::async,[i]{AgentSession s;Material material;material.name="Parallel material";material.densityGcm3=i+1;s.document.materials.push_back(material);auto args=json(R"({"keys":["materials:Parallel material"],"depth":0})");const auto graph=accepted(s.call("context_graph",args));const auto hash=field(graph,"source_sha256").str;for(int n=0;n<32;++n){const auto next=accepted(s.call("context_graph",args));if(!field(next,"index_reused").b||field(next,"source_sha256").str!=hash||field(field(*node(next,"materials:Parallel material"),"facts"),"density").num!=i+1)throw std::runtime_error("Independent session graph cache crossed concurrent authoring");}return hash;}));
    std::set<std::string> hashes;for(auto& reader:readers)hashes.insert(reader.get());check(hashes.size()==8,"independent concurrent sessions retain distinct current graph sources");check(contextGraphAuthoringSerializations()==start+8,"atomic serialization diagnostic counts exactly one build per concurrent session");
}
bool edge(const V& graph,const char* from,const char* to,const char* relation){for(const auto& row:field(graph,"edges").arr)if(field(row,"from").str==from&&field(row,"to").str==to&&field(row,"relation").str==relation)return true;return false;}
void projectGraph(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    apply(s,R"([{"op":"bom.initialize"},{"op":"param.set","name":"travel","expr":20},{"op":"param.set","name":"span","expr":"travel*2"},{"op":"feature.add","type":"box","name":"Suspension base","p":{"dx":"span","dy":4,"dz":3}},{"op":"feature.add","type":"transform","name":"Suspension placed","inputs":[1],"p":{"tx":5}},{"op":"feature.add","type":"box","name":"Unrelated antenna"},{"op":"assembly.add","name":"Suspension group","feature":0},{"op":"assembly.add","name":"Suspension occurrence","feature":2,"parent":1}])");
    const auto originalScope=s.document.bomDocumentId;
    const auto saved=documentToJson(s.document);const auto rev=s.revision;int evaluated=0;
    auto previous=setEvaluationObserver([&](u64,const std::string&,size_t,size_t,bool,bool){++evaluated;});
    const auto serializedBefore=contextGraphAuthoringSerializations();
    const auto graph=accepted(s.call("context_graph",json(R"({"keys":["feature:2"],"depth":2,"limit":12})")));
    const auto serializedOnce=contextGraphAuthoringSerializations();
    const auto reused=accepted(s.call("context_graph",json(R"({"keys":["feature:2"],"depth":2,"limit":12})")));setEvaluationObserver(previous);
    check(evaluated==0&&documentToJson(s.document)==saved&&s.revision==rev,"graph reads neither evaluate geometry nor mutate saved authoring/revision");
    check(field(reused,"index_reused").b&&!field(graph,"index_reused").b,"unchanged authoring reuses the session index");
    check(serializedOnce==serializedBefore+1&&contextGraphAuthoringSerializations()==serializedOnce,"unchanged authoring does not serialize the document again");
    const auto originalName=s.document.features[0].name;s.document.features[0].name=originalName+" edited";
    const auto renamed=accepted(s.call("context_graph",json(R"({"keys":["feature:1"],"depth":0})")));
    check(!field(renamed,"index_reused").b&&field(renamed,"source_sha256").str!=field(graph,"source_sha256").str&&field(*node(renamed,"feature:1"),"name").str==originalName+" edited","in-place edit rebuilds the graph without a revision change");
    s.document.features[0].name=originalName;for(auto& parameter:s.document.params)if(parameter.name=="span")parameter.expr="travel*4";
    const auto retargeted=accepted(s.call("context_graph",json(R"({"keys":["params:span"],"depth":0})")));
    check(!field(retargeted,"index_reused").b&&field(*node(retargeted,"params:span"),"facts").find("expr")->str=="travel*4","in-place parameter edit is visible in the rebuilt graph");
    for(auto& parameter:s.document.params)if(parameter.name=="span")parameter.expr="travel*2";
    const auto restoredAuthoring=accepted(s.call("context_graph",json(R"({"keys":["feature:2"],"depth":2,"limit":12})")));
    check(!field(restoredAuthoring,"index_reused").b&&field(restoredAuthoring,"source_sha256").str==field(graph,"source_sha256").str,"restored authoring recovers the original source hash");
    const auto quiet=contextGraphAuthoringSerializations();accepted(s.call("context_graph",json(R"({"keys":["feature:2"],"depth":2,"limit":12})")));
    check(contextGraphAuthoringSerializations()==quiet,"restored authoring reuses the index without serialization");
    check(node(graph,"feature:1")&&node(graph,"params:span")&&node(graph,"assembly:2")&&!node(graph,"feature:3"),"selected neighborhood includes dependencies and backlinks while excluding unrelated objects");
    check(edge(graph,"feature:2","feature:1","depends_on")&&edge(graph,"feature:1","params:span","uses_parameter")&&edge(graph,"assembly:2","feature:2","places"),"typed authored/expression links preserve direction");
    check(!field(graph,"manufacturing_qualified").b&&!field(graph,"partial").b,"navigation graph makes no manufacturing claim");
    V deltaArgs=json(R"({"keys":["feature:2"],"depth":2})");deltaArgs.set("known_scope",field(graph,"scope"));deltaArgs.set("known_fingerprints",field(graph,"fingerprints"));
    auto delta=accepted(s.call("context_graph",deltaArgs));for(const auto& row:field(delta,"nodes").arr)check(!field(row,"changed").b&&!row.find("facts"),"unchanged delta omits only facts and retains graph identity");
    auto cascade=accepted(s.call("context_graph",json(R"({"keys":["params:span"],"depth":1})")));check(node(cascade,"params:travel"),"parameter cascades are discoverable");
    apply(s,R"([{"op":"feature.edit","id":3,"p":{"dx":6}}])");delta=accepted(s.call("context_graph",deltaArgs));check(!field(delta,"index_reused").b&&field(delta,"source_sha256").str!=field(graph,"source_sha256").str&&!field(*node(delta,"feature:2"),"changed").b,"unrelated edit invalidates source index while preserving unchanged node facts");
    apply(s,R"([{"op":"param.set","name":"span","expr":"travel*3"}])");delta=accepted(s.call("context_graph",deltaArgs));check(field(*node(delta,"params:span"),"changed").b,"edited parameter facts invalidate their fingerprint");
    const auto current=documentToJson(s.document);const auto currentRev=s.revision;
    for(const char* invalid:{R"({"query":"***"})",R"({"keys":["feature:999"]})",R"({"keys":["feature:1","feature:1"]})",R"({"depth":3})",R"({"limit":0})",R"({"expected_revision":0})",R"({"keys":["feature:1","feature:2"],"limit":1})",R"({"unknown":true})"})check(field(s.call("context_graph",json(invalid)),"isError").b,"invalid or stale graph requests reject explicitly");
    s.document.bomDocumentId="another-document";check(field(s.call("context_graph",deltaArgs),"isError").b,"cross-document fingerprints reject");s.document.bomDocumentId=originalScope;
    check(documentToJson(s.document)==current&&s.revision==currentRev,"all rejected queries preserve source and revision");
    Document reopened;std::string error;check(documentFromJson(documentToJson(s.document),reopened,&error),"existing save format reopens without graph persistence changes");AgentSession restored;restored.document=std::move(reopened);restored.revision=s.revision;
    const auto before=accepted(s.call("context_graph",json(R"({"keys":["feature:2"],"depth":2})"))),after=accepted(restored.call("context_graph",json(R"({"keys":["feature:2"],"depth":2})")));
    check(field(before,"fingerprints").dump()==field(after,"fingerprints").dump()&&field(before,"scope").dump()==field(after,"scope").dump(),"saved authoring reconstructs stable scoped graph after reopen");
    s.document.agentActivity=std::make_shared<AgentActivityLedger>();s.document.agentActivity->id="public-ledger";s.document.agentActivity->unrecordedActions=2;
    s.document.agentActivity->entries.push_back(json(R"({"id":"public-error","utc":"2026-10-17T10:00:00Z","tool":"doc_apply","is_error":true,"error_code":"REVISION_CONFLICT","error":"Suspension revision changed","actor":{"id":"reviewer","role":"reviewer"},"feedback":{"provenance":"agent-self-report","action":"Inspected suspension","issue":"Need smaller context"}})"));
    const auto historical=accepted(s.call("context_graph",json(R"({"query":"Suspension","history_limit":1})")));
    check(field(historical,"history").arr.size()==1&&field(field(historical,"history").arr[0],"feedback_provenance").str=="agent-self-report"&&field(historical,"history_capture_gap").num==2,"historical issues preserve attribution and capture gaps");
    check(historical.dump().find("unresolved")==std::string::npos,"a historical complaint is not declared currently unresolved");
    V queries=json(R"({"queries":[{"tool":"context_graph","args":{"keys":["feature:2"]}}]})");check(!field(s.call("query_batch",queries),"isError").b,"graph discovery participates in read-only batch dispatch");
    AgentSession large;large.suppressIterationHistory();V commands=V::makeArr();for(int i=0;i<96;++i){V command=json(R"({"op":"feature.add","type":"box","p":{"dx":20,"dy":10,"dz":5}})");command.set("name",V::makeStr(i==0?"Chosen suspension":"Unrelated part "+std::to_string(i)));commands.arr.push_back(command);}V args=V::makeObj();args.set("commands",commands);accepted(large.call("doc_apply",args));
    const auto inventory=accepted(large.call("doc_info",json(R"({"detail":"design"})"))),selected=accepted(large.call("context_graph",json(R"({"keys":["feature:1"],"depth":1})")));
    check(node(selected,"feature:1")&&field(*node(selected,"feature:1"),"name").str=="Chosen suspension"&&field(selected,"nodes").arr.size()==1,"matched retrieval identifies the requested feature");
    check(selected.dump().size()<inventory.dump().size(),"bounded navigation response is smaller for the matched 96-part fixture");
    std::cout<<"GRAPH_RETRIEVAL design_bytes="<<inventory.dump().size()<<" selected_graph_bytes="<<selected.dump().size()<<" parts=96\n";
}
void addExchange(V& history,const char* id,const char* name,const V& receipt){V call=json(R"({"type":"function_call","arguments":"{}"})"),output=json(R"({"type":"function_call_output"})");call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(name));output.set("call_id",V::makeStr(id));output.set("output",V::makeStr(receipt.dump()));history.arr.push_back(call);history.arr.push_back(output);}
void graphRequests(){
    V history=json(R"([{"role":"developer","content":"Read-only reviewer. Preserve exact face references."},{"role":"user","content":"Inspect suspension; keep wheelbase 240 mm."}])");
    V graph=json(R"({"schema":"ark.context-graph.v1","scope":"public-project","revision":7,"source_sha256":"abc","nodes":[{"key":"feature:1","fingerprint":"fp1","facts":{}}],"edges":[],"history":[],"feedback_required":true,"activity_id":"rated-read"})");
    V rows=field(graph,"nodes"),facts=V::makeObj();facts.set("description",V::makeStr("Suspension dimensions "+std::string(1200,'x')));rows.arr[0].set("facts",facts);graph.set("nodes",rows);
    for(int i=0;i<6;++i){const auto id="graph-"+std::to_string(i);addExchange(history,id.c_str(),"context_graph",graph);}
    V delta=graph;rows=field(delta,"nodes");rows.arr[0].obj.erase(std::remove_if(rows.arr[0].obj.begin(),rows.arr[0].obj.end(),[](auto& row){return row.first=="facts";}),rows.arr[0].obj.end());delta.set("nodes",rows);addExchange(history,"graph-delta","context_graph",delta);
    const auto unrated=assistant_context::graphProject(history);check(unrated.dump()==history.dump(),"unrated graph reads preserve exact evidence");
    addExchange(history,"rating","tool_feedback",json(R"({"isError":false,"structuredContent":{"rated_ids":["rated-read"]}})"));
    addExchange(history,"failed-write","doc_apply",json(R"({"isError":true,"structuredContent":{"error":"Revision conflict","candidate_id":"keep-candidate"}})"));
    const auto original=history.dump();const auto projected=assistant_context::graphProject(history);
    check(projected.dump().size()<original.size()&&history.dump()==original,"graph dedup reduces request bytes while retaining original host history");
    check(projected.arr[0].dump()==history.arr[0].dump()&&projected.arr[1].dump()==history.arr[1].dump(),"role and user constraints remain exact");
    check(projected.dump().find("Suspension dimensions")!=std::string::npos&&projected.dump().find("keep-candidate")!=std::string::npos&&projected.dump().find("Revision conflict")!=std::string::npos,"latest delta receives exact facts and failed write receipts remain");
    for(const char* changed:{"scope","source_sha256"}){V isolated=json(R"([])"),other=graph;other.set("feedback_required",V::makeBool(false));V first=other;other.set(changed,V::makeStr("different"));addExchange(isolated,"one","context_graph",first);addExchange(isolated,"two","context_graph",other);check(assistant_context::graphProject(isolated).dump()==isolated.dump(),"different source or scope cannot cover an older graph read");}
    V broken=history;broken.arr.pop_back();check(assistant_context::graphProject(broken).dump()==broken.dump(),"unpaired exchanges cannot be hidden by graph projection");
    AssistantConfig config;config.model="offline-context";bool compact=false;const auto request=assistantRequest(config,history,AgentSession::tools(),&compact);check(compact&&request.dump().find("Suspension dimensions")!=std::string::npos,"automatic wire projection also operates below the generic history threshold");
    int rounds=0,writes=0,inspections=0;bool rejected=false;
    auto response=[](const char* id,const char* name,const char* args){V out=json(R"({"status":"completed","output":[]})"),call=json(R"({"type":"function_call"})");call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(name));call.set("arguments",V::makeStr(args));V outputs=V::makeArr();outputs.arr.push_back(call);out.set("output",outputs);return out;};
    const auto run=runAssistant(config,history,AgentSession::tools(),[&](const std::string& name,const V&){if(name=="doc_info")++inspections;else ++writes;return json(R"({"isError":false,"structuredContent":{"revision":7}})");},{},nullptr,[&](auto&,const V& outgoing,auto*){
        switch(++rounds){case 1:return response("premature","doc_apply",R"({"expected_revision":7,"commands":[]})");case 2:rejected=outgoing.dump().find("Read current doc_info successfully")!=std::string::npos;return response("current","doc_info",R"({"detail":"revision"})");case 3:return response("allowed","doc_apply",R"({"expected_revision":7,"commands":[]})");default:return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Reviewed"}]}]})");}
    });
    check(rejected&&inspections==1&&writes==1&&rounds==4,"graph projection requires fresh successful document inspection before a remaining write");
    for(size_t i=0;i<history.arr.size();++i)check(run.conversation.arr[i].dump()==history.arr[i].dump(),"graph continuation keeps every original host message and receipt");
    std::cout<<"GRAPH_DEDUP original_history_bytes="<<original.size()<<" projected_history_bytes="<<projected.dump().size()<<" provider_request_bytes="<<request.dump().size()<<" calls=7 paid_model_calls=0\n";
}
}
int main(){try{preciseStamp();parallelStamps();projectGraph();graphRequests();std::cout<<checks<<" context graph checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
