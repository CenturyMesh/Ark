#include "ark/agent.hpp"
#include "ark/agent_activity.hpp"
#include "ark/assistant.hpp"
#include "ark/iteration_history.hpp"
#include "ark/usage.hpp"
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
    {auto empty=args;empty.set("query",V::makeStr(""));accepted(s.call("context_graph",empty));}
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
void bisectorGraph(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    apply(s,R"([{"op":"plane.set","name":"Lower","p":{"oz":2}},{"op":"plane.set","name":"Upper","p":{"oz":12}},{"op":"plane.set","name":"Middle","support":{"kind":"bisector","first":"Lower","second":"Upper"}}])");
    const auto args=json(R"({"keys":["planes:Middle"],"depth":1})");
    const auto graph=accepted(s.call("context_graph",args));
    check(edge(graph,"planes:Middle","planes:Lower","uses_plane")&&edge(graph,"planes:Middle","planes:Upper","uses_plane"),"bisector string anchors retain both authored plane dependencies");
    s.document.planes.back().support.set("second",V::makeStr("Lower"));
    const auto changed=accepted(s.call("context_graph",args));
    check(!field(changed,"index_reused").b&&field(changed,"source_sha256").str!=field(graph,"source_sha256").str&&!edge(changed,"planes:Middle","planes:Upper","uses_plane"),"in-place bisector retarget invalidates graph and removes the old dependency");
    s.document.planes.back().support.set("second",V::makeStr("Upper"));
    Feature cable;cable.type=FeatureType::CableRoute;cable.name="Format probe";cable.nativeSpec=json(R"({"route_mode":"spline","start":{"point":[0,0,0]},"end":{"point":[80,0,10]},"waypoints":[[30,5,8]],"profile":{"type":"ribbon","width":4,"thickness":0.4,"up":[0,0,1]},"bend_radius":8})");s.document.addFeature(cable);
    auto saved=json(documentToJson(s.document,false,false));Document restored;std::string error;
    check(field(saved,"version").num==46&&documentFromJson(saved.dump(),restored,&error),"mixed cable and bisector authoring saves and reopens at format46");
    saved.set("version",V::makeNum(45));check(!documentFromJson(saved.dump(),restored,&error)&&error.find("Bisector planes require document version 46")!=std::string::npos,"format45 cable contract cannot conceal a downgraded bisector");
    s.document.planes.pop_back();saved=json(documentToJson(s.document,false,false));check(field(saved,"version").num==45&&documentFromJson(saved.dump(),restored,&error),"existing format45 cable-only source stays readable and does not upgrade unnecessarily");
}
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
V publish(AgentSession& s,V args){args.set("expected_revision",V::makeNum(s.revision));const auto preview=accepted(s.call("parametric_program",args));V a=V::makeObj();a.set("candidate_id",field(preview,"candidate_id"));a.set("expected_revision",V::makeNum(s.revision));return accepted(s.call("doc_apply",a));}
std::string featureKey(const AgentSession& s,const std::string& name){for(const auto& f:s.document.features)if(f.name==name)return "feature:"+std::to_string(f.id);throw std::runtime_error("missing feature "+name);}
void programGraph(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    apply(s,R"([{"op":"bom.initialize"},{"op":"param.set","name":"pg","expr":3},{"op":"param.set","name":"only","expr":1},{"op":"material.set","name":"Oak","density":0.7},{"op":"material.set","name":"Pine","density":0.5}])");
    publish(s,json(R"({"action":"prepare","mode":"install","namespace":"pg","program":{"format":"ark.parametric-program.v1","name":"Graph probe","parameters":{"w":{"default":10,"min":1},"finish":{"choices":["Oak","Pine"],"default":"Oak"}},"body":[
        {"emit":{"op":"plane.set","name":"top","parent":"XY","p":{"oz":"{w}"}}},{"emit":{"op":"sketch.set","name":"profile","sketch":{"plane":"top","entities":[{"kind":"circle","name":"c","dof":["0","0","{w}"]}]}}},{"emit":{"op":"material.set","name":"skin","density":1.2}},
        {"emit":{"op":"feature.add","type":"box","name":"Body","material":"skin","p":{"dx":"{w}","dy":4,"dz":2}}},{"emit":{"op":"feature.add","type":"box","name":"Trim {@finish}","material":"{@finish}","p":{"dx":2,"dy":2,"dz":2}}}]}})"));
    const auto digest=field(s.document.parametricPrograms.arr[0],"program_sha256").str;const auto body=featureKey(s,"pg / Body"),trim=featureKey(s,"pg / Trim Oak");
    const auto graph=accepted(s.call("context_graph",json(R"({"keys":["program:pg"],"depth":1,"limit":16})")));const auto* program=node(graph,"program:pg");
    check(program&&field(*program,"kind").str=="program"&&field(*program,"name").str=="Graph probe","an installed program binding is a graph node");
    const auto& facts=field(*program,"facts");check(field(facts,"namespace").str=="pg"&&field(facts,"program_sha256").str==digest&&field(field(facts,"values"),"w").num==10&&field(field(facts,"values"),"finish").str=="Oak","binding facts carry namespace, source digest and resolved values");
    check(edge(graph,"program:pg",body.c_str(),"owns")&&edge(graph,"program:pg",trim.c_str(),"owns")&&edge(graph,"program:pg","sketches:pg__profile","owns")&&edge(graph,"program:pg","planes:pg__top","owns")&&edge(graph,"program:pg","materials:pg__skin","owns"),"ownership edges reach owned features, sketches, planes and materials");
    check(!edge(graph,"program:pg","params:pg","uses_parameter")&&!edge(graph,"program:pg","params:only","uses_parameter")&&!edge(graph,"program:pg","materials:Oak","uses_material"),"namespace, values and authority text are not document references");
    check(node(accepted(s.call("context_graph",json(R"({"query":"Graph probe","depth":0})"))),"program:pg"),"query discovers the binding by program name");
    const auto backlinks=accepted(s.call("context_graph",json(("{\"keys\":[\""+body+"\"],\"depth\":1}").c_str())));check(node(backlinks,"program:pg"),"an owned feature leads back to its program");
    V known=json(R"({"keys":["program:pg"],"depth":0})");known.set("known_scope",field(graph,"scope"));known.set("known_fingerprints",field(graph,"fingerprints"));
    check(!field(*node(accepted(s.call("context_graph",known)),"program:pg"),"changed").b,"an unchanged binding keeps its fingerprint");
    publish(s,json(R"({"action":"prepare","mode":"configure","namespace":"pg","values":{"w":12,"finish":"Pine"}})"));const auto configured=accepted(s.call("context_graph",known));const auto* row=node(configured,"program:pg");
    check(row&&field(*row,"changed").b&&field(field(field(*row,"facts"),"values"),"w").num==12&&field(field(field(*row,"facts"),"values"),"finish").str=="Pine","configured values change the binding fingerprint and facts");
    check(edge(accepted(s.call("context_graph",json(R"({"keys":["program:pg"],"depth":1,"limit":16})"))),"program:pg",featureKey(s,"pg / Trim Pine").c_str(),"owns"),"ownership follows the regenerated objects");
    // A detach changes only the binding; the authoring stamp must still invalidate the index.
    publish(s,json(R"({"action":"prepare","mode":"detach","namespace":"pg"})"));const auto detached=accepted(s.call("context_graph",json(R"({"query":"Body","depth":0})")));
    check(!field(detached,"index_reused").b&&field(detached,"source_sha256").str==iterationSha256(documentToJson(s.document,false,false))&&!node(detached,"program:pg"),"binding-only change rebuilds the graph");
    check(field(s.call("context_graph",json(R"({"keys":["program:pg"]})")),"isError").b,"a detached binding is no longer a graph key");
    s.document.designVariants=json(R"([{"name":"A","namespace":"pg","program_sha256":"0000000000000000000000000000000000000000000000000000000000000000","values":{"w":10}}])");
    const auto variants=accepted(s.call("context_graph",json(R"({"query":"Body","depth":0})")));check(!field(variants,"index_reused").b&&field(variants,"source_sha256").str==iterationSha256(documentToJson(s.document,false,false)),"saved variants join the authoring stamp");
}
void pagedGraph078B(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    for(int i=0;i<9;++i){Feature f;f.type=i?FeatureType::Transform:FeatureType::Box;f.name="Paged fixture "+std::to_string(i);if(i)f.inputs={(u64)i};s.document.addFeature(f);}
    const auto authoring=documentToJson(s.document);size_t evaluated=0;auto observer=setEvaluationObserver([&](u64,const std::string&,size_t,size_t,bool,bool){++evaluated;});
    auto args=json(R"({"query":"Paged","depth":0,"limit":2,"history_limit":0})");auto first=accepted(s.call("context_graph",args));check(field(first,"partial").b&&field(field(first,"page"),"neighborhood_nodes").num==9,"Page census includes omitted query roots");
    std::set<std::string> seen;auto page=first;int pages=0;for(;;){++pages;for(const auto& n:field(page,"nodes").arr)check(seen.insert(field(n,"key").str).second,"Continuation repeated a node");if(field(page,"next_call").isNull())break;page=accepted(s.call("context_graph",field(page,"next_call")));check(pages<10,"Graph continuation failed to terminate");}
    check(pages==5&&seen.size()==9&&!field(page,"partial").b,"Continuation omitted query roots or kept a false has_more");
    const auto boundary=accepted(s.call("context_graph",json(R"({"keys":["feature:5"],"depth":0,"limit":1})")));check(field(boundary,"boundary_edges").arr.size()==2&&field(boundary,"frontier").arr.size()==2&&field(field(boundary,"page"),"depth_boundary_nodes").num==2&&!field(boundary,"expand_call").isNull(),"Depth boundary and executable expansion are not visible");
    auto delta=json(R"({"keys":["feature:5"],"depth":0,"limit":1})");delta.set("known_scope",field(boundary,"scope"));delta.set("known_fingerprints",field(boundary,"fingerprints"));const auto quiet=accepted(s.call("context_graph",delta));check(!node(quiet,"feature:5")->find("facts")&&field(quiet,"boundary_edges").dump()==field(boundary,"boundary_edges").dump(),"Delta facts erased continuation edges");check(node(quiet,"feature:5")->find("geometry_domain"),"Delta graph omitted geometry classification");
    const auto outgoing=accepted(s.call("context_graph",json(R"({"keys":["feature:5"],"depth":2,"direction":"outgoing"})")));check(node(outgoing,"feature:3")&&node(outgoing,"feature:4")&&!node(outgoing,"feature:6"),"Outgoing traversal followed backlinks");
    const auto incoming=accepted(s.call("context_graph",json(R"({"keys":["feature:5"],"depth":2,"direction":"incoming"})")));check(node(incoming,"feature:7")&&!node(incoming,"feature:4"),"Incoming traversal followed forward links");
    auto altered=field(first,"next_call");altered.set("direction",V::makeStr("incoming"));check(field(s.call("context_graph",altered),"isError").b,"Changed selection accepted a cursor");
    altered=field(first,"next_call");V cursor=field(altered,"cursor");cursor.set("offset",V::makeNum(1000));altered.set("cursor",cursor);check(field(s.call("context_graph",altered),"isError").b,"Out-of-range cursor offset accepted");
    s.document.features[0].name="Changed in place";check(field(s.call("context_graph",field(first,"next_call")),"isError").b,"Source edit at same revision accepted a stale cursor");s.document.features[0].name="Paged fixture 0";
    auto revisionArgs=field(first,"next_call");revisionArgs.obj.erase(std::remove_if(revisionArgs.obj.begin(),revisionArgs.obj.end(),[](const auto& e){return e.first=="expected_revision";}),revisionArgs.obj.end());++s.revision;check(field(s.call("context_graph",revisionArgs),"isError").b,"Cursor itself failed to bind observed revision");--s.revision;
    setEvaluationObserver(observer);check(!evaluated&&authoring==documentToJson(s.document),"Graph paging evaluated or changed authoring");
    AgentSession wide;for(int i=0;i<90;++i){Feature f;f.type=i?FeatureType::Transform:FeatureType::Box;f.name="Wide";if(i)f.inputs={1};wide.document.addFeature(f);}const auto clipped=accepted(wide.call("context_graph",json(R"({"keys":["feature:1"],"depth":0,"limit":1})")));check(field(field(clipped,"page"),"frontier_truncated").b&&field(clipped,"frontier").arr.size()==64,"Wide boundary must be explicitly bounded");
    auto expansion=accepted(wide.call("context_graph",field(clipped,"expand_call")));std::set<std::string> expanded;for(;;){for(const auto& n:field(expansion,"nodes").arr)expanded.insert(field(n,"key").str);if(field(expansion,"next_call").isNull())break;expansion=accepted(wide.call("context_graph",field(expansion,"next_call")));}check(expanded.size()==90,"Truncated frontier lost its unlisted neighbors from executable expansion");
}
void directedEdgePages078B(){
    auto edgeIdentity=[](const V& edge){return field(edge,"from").str+"\n"+field(edge,"relation").str+"\n"+field(edge,"to").str+"\n"+field(edge,"basis").str;};
    auto collect=[&](AgentSession& session,V args,size_t nodeCount,const std::set<std::string>& expected){
        std::set<std::string> nodes,edges;size_t chunks=0,edgeOnly=0;auto receipt=accepted(session.call("context_graph",args));const auto first=receipt;
        for(;;){++chunks;check(chunks<1000,"Edge continuation failed to terminate");const auto& page=field(receipt,"page");check(field(receipt,"edges").arr.size()+field(receipt,"boundary_edges").arr.size()<=128,"Combined edge response exceeded its bound");
            if(field(page,"edge_offset").num>0){++edgeOnly;check(field(receipt,"nodes").arr.empty()&&field(page,"returned").num==0,"Edge continuation duplicated node facts");}
            for(const auto& row:field(receipt,"nodes").arr)check(nodes.insert(field(row,"key").str).second,"Continuation repeated a node across edge pages");for(const char* section:{"edges","boundary_edges"})for(const auto& edge:field(receipt,section).arr)edges.insert(edgeIdentity(edge));
            check(field(page,"has_more").b==!field(receipt,"next_call").isNull(),"Page completion disagrees with executable continuation");if(field(receipt,"next_call").isNull())break;receipt=accepted(session.call("context_graph",field(receipt,"next_call")));}
        check(nodes.size()==nodeCount&&edges==expected&&edgeOnly>0,"Directed/dense paging lost authored nodes or incident edge identities");
        auto bad=field(first,"next_call");V cursor=field(bad,"cursor");cursor.set("edge_offset",V::makeNum(1000000));bad.set("cursor",cursor);check(field(session.call("context_graph",bad),"isError").b,"Invalid edge continuation offset accepted");
        if(!session.document.features.empty()){const auto name=session.document.features[0].name;session.document.features[0].name+=" changed";check(field(session.call("context_graph",field(first,"next_call")),"isError").b,"Source edit accepted a stale edge continuation");session.document.features[0].name=name;}
    };
    AgentSession incoming;incoming.suppressFailureCapture();incoming.document.setParam("driver","3");std::set<std::string> incomingEdges;
    for(int i=1;i<=205;++i){Feature f;f.type=FeatureType::Box;f.name="Incoming leaf "+std::to_string(i);f.p["dx"]="driver";incoming.document.addFeature(f);incomingEdges.insert("feature:"+std::to_string(i)+"\nuses_parameter\nparams:driver\nexpression_identifier");}
    collect(incoming,json(R"({"keys":["params:driver"],"direction":"incoming","depth":1,"limit":7,"history_limit":0})"),206,incomingEdges);
    AgentSession outgoing;outgoing.suppressFailureCapture();std::set<std::string> outgoingEdges;std::string expression;
    for(int i=0;i<205;++i){const auto name="p"+std::to_string(i);outgoing.document.setParam(name,"1");if(!expression.empty())expression+='+';expression+=name;outgoingEdges.insert("feature:1\nuses_parameter\nparams:"+name+"\nexpression_identifier");}Feature source;source.type=FeatureType::Box;source.name="Outgoing root";source.p["dx"]=expression;outgoing.document.addFeature(source);
    collect(outgoing,json(R"({"keys":["feature:1"],"direction":"outgoing","depth":1,"limit":7,"history_limit":0})"),206,outgoingEdges);
    AgentSession dense;dense.suppressFailureCapture();std::set<std::string> denseEdges;
    for(int i=1;i<=32;++i){Feature f;f.type=i<=16?FeatureType::Box:FeatureType::NativeSolid;f.name="Dense "+std::to_string(i);if(i>16){f.nativeSpec=json(R"({"op":"union"})");for(int j=1;j<=16;++j){f.inputs.push_back(j);denseEdges.insert("feature:"+std::to_string(i)+"\ndepends_on\nfeature:"+std::to_string(j)+"\nauthored_selector");}}dense.document.addFeature(f);}
    collect(dense,json(R"({"query":"Dense","depth":0,"limit":32,"history_limit":0})"),32,denseEdges);
}
void auditedPaging078B(){
    const auto actor=json(R"({"kind":"agent","id":"context-page-reader","role":"reviewer"})");
    auto populate=[](AgentSession& s){s.suppressFailureCapture();s.suppressIterationHistory();for(int i=0;i<10;++i){Feature f;f.type=FeatureType::Box;f.name="Audit node "+std::to_string(i);s.document.addFeature(f);}};
    AgentSession s;populate(s);const auto source=documentToJson(s.document,false,false);const auto revision=s.revision;
    auto args=json(R"({"query":"Audit","depth":0,"limit":2,"history_limit":3})");args.set("actor",actor);auto page=accepted(s.call("context_graph",args));const auto scope=field(page,"scope").str,hash=field(page,"source_sha256").str;
    check(s.document.agentActivity&&scope==s.document.agentActivity->id&&s.document.bomDocumentId.empty(),"First audited graph receipt did not bind the identity used to record that read");
    UsageReceipt usage;usage.id="context-page-usage";usage.source="offline-test";usage.provenance="synthetic";usage.missingResponses=1;std::string error;check(addUsageReceipt(s.document,usage,&error)==UsageRecordResult::Added,error.c_str());
    auto feedback=json(R"({"ratings":[{"activity_id":"","utility":5,"difficulty":1,"action":"Audit graph inspection","issue":"Audit page history now has feedback"}]})");feedback.obj.front().second.arr.front().set("activity_id",field(page,"activity_id"));feedback.set("actor",actor);accepted(s.call("tool_feedback",feedback));
    std::set<std::string> nodes;bool historySeen=false;size_t reads=0;for(;;){++reads;for(const auto& row:field(page,"nodes").arr)check(nodes.insert(field(row,"key").str).second,"Audited pagination repeated a node");historySeen|=!field(page,"history").arr.empty();if(field(page,"next_call").isNull())break;auto next=field(page,"next_call");next.set("actor",actor);page=accepted(s.call("context_graph",next));check(field(page,"scope").str==scope&&field(page,"source_sha256").str==hash&&field(page,"revision").num==revision,"Audit/usage bookkeeping invalidated a graph continuation");}
    check(nodes.size()==10&&reads==5&&historySeen&&agentActivitySerial(s.document)>=reads&&s.document.agentActivity->entries.size()==reads&&s.revision==revision&&documentToJson(s.document,false,false)==source,"Agent-attributed graph reads changed authoring or duplicated/lost their audit actions");
    AgentSession batched;populate(batched);const auto batchSource=documentToJson(batched.document,false,false);V query=V::makeObj();query.set("tool",V::makeStr("context_graph"));query.set("args",json(R"({"query":"Audit","depth":0,"limit":2})"));V queries=V::makeArr();queries.arr.push_back(query);V batch=V::makeObj();batch.set("queries",queries);batch.set("actor",actor);const auto receipt=accepted(batched.call("query_batch",batch));const auto graph=accepted(field(receipt,"results").arr.front());check(batched.document.agentActivity&&batched.document.agentActivity->entries.size()==1,"Audited batch recorded duplicate inner read actions");auto next=field(graph,"next_call");next.set("actor",actor);const auto continued=accepted(batched.call("context_graph",next));check(field(continued,"scope").str==field(graph,"scope").str&&field(continued,"source_sha256").str==field(graph,"source_sha256").str&&batched.document.agentActivity->entries.size()==2&&documentToJson(batched.document,false,false)==batchSource,"First audited batch invalidated its nested graph continuation or changed source");
}
void addExchange(V& history,const char* id,const char* name,const V& receipt){V call=json(R"({"type":"function_call","arguments":"{}"})"),output=json(R"({"type":"function_call_output"})");call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(name));output.set("call_id",V::makeStr(id));output.set("output",V::makeStr(receipt.dump()));history.arr.push_back(call);history.arr.push_back(output);}
void graphRequests(){
    V history=json(R"([{"role":"developer","content":"Read-only reviewer. Preserve exact face references."},{"role":"user","content":"Inspect suspension; keep wheelbase 240 mm."}])");
    V graph=json(R"({"schema":"ark.context-graph.v1","scope":"public-project","revision":7,"source_sha256":"abc","nodes":[{"key":"feature:1","fingerprint":"fp1","facts":{}}],"edges":[],"history":[],"feedback_required":true,"activity_id":"rated-read"})");
    V rows=field(graph,"nodes"),facts=V::makeObj();facts.set("description",V::makeStr("Suspension dimensions "+std::string(1200,'x')));rows.arr[0].set("facts",facts);graph.set("nodes",rows);
    {V first=graph;first.set("feedback_required",V::makeBool(false));first.set("next_call",json(R"({"cursor":{"offset":12}})"));V second=first;second.set("next_call",V{});V navigation=V::makeArr();addExchange(navigation,"navigation-before","context_graph",first);addExchange(navigation,"navigation-after","context_graph",second);check(assistant_context::graphProject(navigation).dump()==navigation.dump(),"Overlapping graph facts erased a distinct continuation");}
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
int main(){try{preciseStamp();parallelStamps();bisectorGraph();projectGraph();programGraph();pagedGraph078B();directedEdgePages078B();auditedPaging078B();graphRequests();std::cout<<checks<<" context graph checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
