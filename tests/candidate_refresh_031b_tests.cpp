#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "ark/native_backend.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace ark;
using V=arkjson::Value;
size_t checks=0;
void check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
const V& field(const V& value,const char* key){const auto* item=value.find(key);if(!item)throw std::runtime_error(std::string("Missing result field: ")+key);return *item;}
V json(const std::string& text){V value;check(arkjson::parse(text,value),"Public candidate fixture parses");return value;}
V accepted(const V& response){check(!field(response,"isError").b,response.dump());return field(response,"structuredContent");}
V commands(const char* text){return json(text);}
V batch(AgentSession& session,V changes){V args=V::makeObj();args.set("commands",std::move(changes));args.set("expected_revision",V::makeNum(session.revision));return args;}
V apply(AgentSession& session,const char* changes){return accepted(session.call("doc_apply",batch(session,commands(changes))));}
V preview(AgentSession& session,const char* changes){const auto before=documentToJson(session.document);const auto revision=session.revision;auto result=accepted(session.call("doc_preview",batch(session,commands(changes))));check(field(result,"valid").b&&field(result,"candidate_cached").b,"A valid fixture must retain actual checked geometry");check(session.revision==revision&&documentToJson(session.document)==before,"Preparing a candidate must preserve live document and history");return result;}
V candidateArgs(AgentSession& session,const V& candidate){V args=V::makeObj();args.set("candidate_id",field(candidate,"candidate_id"));args.set("expected_revision",V::makeNum(session.revision));return args;}
V refresh(AgentSession& session,const V& candidate){const auto before=documentToJson(session.document);const auto revision=session.revision;auto result=accepted(session.call("doc_preview",candidateArgs(session,candidate)));check(field(result,"valid").b&&field(result,"candidate_cached").b,"Safe refresh must prepare checked geometry");check(field(result,"candidate_id").str!=field(candidate,"candidate_id").str,"Explicit refresh must issue a fresh candidate ID");check(session.revision==revision&&documentToJson(session.document)==before,"Refresh must not publish geometry or rewrite peer history");const auto& receipt=field(result,"refresh");check(field(receipt,"source_candidate_id").str==field(candidate,"candidate_id").str&&field(receipt,"current_revision").num==revision,"Refresh must expose the original candidate and current observed revision");return result;}
void rejected(AgentSession& session,const char* tool,V args,const char* reason){
    const auto before=documentToJson(session.document);const auto revision=session.revision;const auto cache=session.document.evaluationCache;
    const auto response=session.call(tool,args);check(field(response,"isError").b,"Unsafe or stale candidate action must reject");
    check(response.dump().find(reason)!=std::string::npos,std::string("Wrong rejection for ")+reason+": "+response.dump());
    check(session.revision==revision&&documentToJson(session.document)==before,"Rejected candidate action must preserve live authoring, revision and peer history");
    for(const auto& [id,entry]:cache){const auto found=session.document.evaluationCache.find(id);check(found!=session.document.evaluationCache.end()&&found->second.body==entry.body&&found->second.generation==entry.generation,"Rejected candidate action must preserve immutable evaluated cache ownership");}
}
void conflict(AgentSession& session,const V& candidate){rejected(session,"doc_preview",candidateArgs(session,candidate),"CANDIDATE_REFRESH_CONFLICT");}
const Feature& named(const Document& document,const char* name){const Feature* result=nullptr;for(const auto& feature:document.features)if(feature.name==name){check(!result,"Fixture feature name must remain unambiguous");result=&feature;}check(result!=nullptr,std::string("Missing fixture feature ")+name);return *result;}
const Body& body(const Document& document,u64 id){for(const auto& value:document.evalBodies)if(value.id==id)return value;throw std::runtime_error("Missing evaluated candidate body");}
// These portable fixtures use the legacy watertight mesh route. Its analytic
// face proxy can over-bound; measure the actual geometry consumed by that route.
AABB meshBounds(const Document& document,u64 id){return body(document,id).tessellate(document.tessOpt).computeBBox();}
void extent(const Document& document,const char* name,Vec3 expected){const auto& feature=named(document,name);const auto bounds=meshBounds(document,feature.id);check(bounds.valid(),"Committed geometry must have measured bounds");const auto size=bounds.mx-bounds.mn;auto text=[](Vec3 value){return "["+std::to_string(value.x)+","+std::to_string(value.y)+","+std::to_string(value.z)+"]";};check(size.approx(expected,1e-6),std::string("Committed body dimensions for ")+name+": actual="+text(size)+" expected="+text(expected));}
struct EvaluationWatch {
    size_t started=0;EvaluationObserver previous;
    EvaluationWatch(){previous=setEvaluationObserver([this](u64,const std::string&,size_t,size_t,bool complete,bool){if(!complete)++started;});}
    ~EvaluationWatch(){setEvaluationObserver(std::move(previous));}
};
void commitChecked(AgentSession& session,const V& candidate){EvaluationWatch watch;const auto result=accepted(session.call("doc_apply",field(candidate,"apply")));check(field(result,"applied_prepared").b&&!field(result,"geometry_recomputed").b&&field(result,"features_evaluated").num==0,"Prepared apply must publish checked geometry without a second recompute");check(watch.started==0,"Prepared apply must not enter another feature evaluation traversal");}
void setup(AgentSession& session){session.suppressFailureCapture();apply(session,R"([{"op":"feature.add","type":"box","name":"Anchor","p":{"dx":2,"dy":3,"dz":4}}])");}

void independentProposals(){
    AgentSession session;setup(session);
    const auto a=preview(session,R"([{"op":"feature.add","type":"box","name":"Part A","p":{"dx":7,"dy":8,"dz":9}}])");
    const auto b=preview(session,R"([{"op":"feature.add","type":"box","name":"Part B","p":{"dx":4,"dy":5,"dz":6}}])");
    check(field(a,"candidate_id").str!=field(b,"candidate_id").str,"Independent previews must have distinct proposal identities");
    commitChecked(session,b);const auto peerId=named(session.document,"Part B").id;const auto peerBody=session.document.evaluationCache.at(peerId).body;
    rejected(session,"doc_apply",field(a,"apply"),"Revision conflict");
    rejected(session,"doc_apply",candidateArgs(session,a),"Prepared candidate");
    const auto refreshed=refresh(session,a);const auto& work=field(refreshed,"candidate_result");check(field(work,"features_evaluated").num==1&&field(work,"features_reused").num==2,"Refresh must reuse unchanged checked peer bodies and evaluate only the new feature");
    const auto revision=session.revision;commitChecked(session,refreshed);check(session.revision==revision+1&&session.document.features.size()==3,"Fresh prepared commit must publish one revision and retain all independent definitions");
    check(named(session.document,"Part B").id==peerId&&named(session.document,"Part A").id!=peerId,"Refreshing an add must allocate current IDs without overwriting the peer's new body");
    check(session.document.evaluationCache.at(peerId).body==peerBody,"Publishing an independent refreshed proposal must retain peer geometry ownership");
    extent(session.document,"Part A",{7,8,9});extent(session.document,"Part B",{4,5,6});
    rejected(session,"doc_apply",candidateArgs(session,refreshed),"Prepared candidate");
}
void ordinaryPeerCommitAndNamedBatch(){
    AgentSession session;setup(session);apply(session,R"([{"op":"feature.add","type":"box","name":"Peer","p":{"dx":3,"dy":4,"dz":5}}])");
    const auto edit=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dx":6}}])");
    apply(session,R"([{"op":"feature.edit","id":"Peer","p":{"dy":11}}])");const auto fresh=refresh(session,edit);commitChecked(session,fresh);
    extent(session.document,"Anchor",{6,3,4});extent(session.document,"Peer",{3,11,5});
    const auto chain=preview(session,R"([{"op":"feature.add","type":"box","name":"Local source","p":{"dx":2,"dy":3,"dz":4}},{"op":"feature.add","type":"transform","name":"Local child","inputs":["Local source"],"p":{"tx":10}}])");
    apply(session,R"([{"op":"feature.add","type":"box","name":"New peer","p":{"dx":8,"dy":8,"dz":8}}])");const auto peerId=named(session.document,"New peer").id;
    const auto freshChain=refresh(session,chain);commitChecked(session,freshChain);const auto& local=named(session.document,"Local source");const auto& child=named(session.document,"Local child");
    check(local.id!=peerId&&child.inputs==std::vector<u64>{local.id},"Named intra-batch references must resolve to newly allocated current proposal IDs");
    const auto localBox=meshBounds(session.document,local.id),childBox=meshBounds(session.document,child.id);check((childBox.mn-localBox.mn).approx({10,0,0},1e-6),"Refreshed dependent geometry must use its own source rather than a peer that took the old ID");extent(session.document,"New peer",{8,8,8});
}
void targetAndDependencyConflicts(){
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dx":9}}])");apply(session,R"([{"op":"feature.edit","id":1,"p":{"dy":12}}])");conflict(session,candidate);extent(session.document,"Anchor",{2,12,4});}
    {AgentSession session;setup(session);apply(session,R"([{"op":"feature.add","type":"transform","name":"Intermediate","inputs":[1],"p":{"tx":10}}])");const auto candidate=preview(session,R"([{"op":"feature.add","type":"transform","name":"Downstream proposal","inputs":[2],"p":{"ty":5}}])");apply(session,R"([{"op":"feature.edit","id":1,"p":{"dz":7}}])");conflict(session,candidate);extent(session.document,"Intermediate",{2,3,7});}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.add","type":"transform","name":"Dependent proposal","inputs":[1]}])");apply(session,R"([{"op":"feature.remove","id":1}])");conflict(session,candidate);check(session.document.features.empty(),"Rejected refresh must not recreate a peer-removed dependency");}
}
void authoringContextConflicts(){
    {AgentSession session;setup(session);apply(session,R"([{"op":"param.set","name":"Width","expr":"5"}])");const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dx":"Width"}}])");apply(session,R"([{"op":"param.set","name":"Width","expr":"8"}])");conflict(session,candidate);extent(session.document,"Anchor",{2,3,4});}
    {AgentSession session;setup(session);apply(session,R"([{"op":"param.set","name":"BaseWidth","expr":"3"},{"op":"param.set","name":"DrivenWidth","expr":"BaseWidth*2"},{"op":"feature.edit","id":1,"p":{"dx":"DrivenWidth"}}])");const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dz":9}}])");apply(session,R"([{"op":"param.set","name":"BaseWidth","expr":"4"}])");conflict(session,candidate);extent(session.document,"Anchor",{8,3,4});}
    {AgentSession session;setup(session);apply(session,R"([{"op":"sketch.set","name":"Round profile","sketch":{"plane":"XY","entities":[{"kind":"circle","dof":[0,0,2]}]}},{"op":"feature.add","type":"extrude","name":"Sketched source","p":{"sketch":"Round profile","height":4}}])");const auto candidate=preview(session,R"([{"op":"feature.edit","id":"Sketched source","p":{"height":6}}])");apply(session,R"([{"op":"sketch.set","name":"Round profile","sketch":{"plane":"XY","entities":[{"kind":"circle","dof":[0,0,3]}]}}])");conflict(session,candidate);extent(session.document,"Sketched source",{6,6,4});}
    {AgentSession session;setup(session);apply(session,R"([{"op":"sketch.set","name":"Proposed profile","sketch":{"plane":"XY","entities":[{"kind":"circle","dof":[0,0,2]}]}}])");const auto candidate=preview(session,R"([{"op":"feature.add","type":"extrude","name":"Proposed extrusion","p":{"sketch":"Proposed profile","height":4}}])");apply(session,R"([{"op":"sketch.remove","name":"Proposed profile"}])");conflict(session,candidate);check(session.document.sketches.empty()&&session.document.features.size()==1,"Refresh must not recreate a removed named authoring dependency");}
}
void namesAndUnsupportedScopes(){
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.add","type":"box","name":"Reserved name"}])");apply(session,R"([{"op":"feature.add","type":"box","name":"Reserved name"}])");conflict(session,candidate);check(session.document.features.size()==2,"Conflicting added name must not publish duplicate proposed geometry");}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.edit","id":"Anchor","p":{"dx":9}}])");apply(session,R"([{"op":"feature.add","type":"box","name":"Anchor"}])");conflict(session,candidate);check(session.document.features.size()==2,"New name ambiguity must not silently retarget the retained recipe");}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.add","type":"box","name":"Numeric local"},{"op":"feature.add","type":"transform","name":"Numeric child","inputs":[2]}])");apply(session,R"([{"op":"feature.add","type":"box","name":"Peer occupies prior ID"}])");rejected(session,"doc_preview",candidateArgs(session,candidate),"CANDIDATE_REFRESH_UNSUPPORTED");}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"param.set","name":"NewSetting","expr":"7"}])");apply(session,R"([{"op":"feature.add","type":"box","name":"Peer"}])");rejected(session,"doc_preview",candidateArgs(session,candidate),"CANDIDATE_REFRESH_UNSUPPORTED");}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.remove","id":1}])");apply(session,R"([{"op":"feature.add","type":"box","name":"Peer"}])");rejected(session,"doc_preview",candidateArgs(session,candidate),"CANDIDATE_REFRESH_UNSUPPORTED");}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dx":7}}])");V injected=candidateArgs(session,candidate);injected.set("commands",commands(R"([{"op":"feature.edit","id":1,"p":{"dx":99}}])"));rejected(session,"doc_preview",injected,"candidate");extent(session.document,"Anchor",{2,3,4});}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p_remove":["dy"]}])");check(!field(candidate,"refresh_supported").b,"Parameter removal remains previewable but must not claim audited refresh support");apply(session,R"([{"op":"feature.add","type":"box","name":"Peer"}])");rejected(session,"doc_preview",candidateArgs(session,candidate),"CANDIDATE_REFRESH_UNSUPPORTED");extent(session.document,"Anchor",{2,3,4});}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dx":5}},{"op":"feature.edit","id":"Anchor","p":{"dy":6}}])");check(!field(candidate,"refresh_supported").b,"Repeated edits of one target remain previewable but require a new explicit recipe preview");apply(session,R"([{"op":"feature.add","type":"box","name":"Peer"}])");rejected(session,"doc_preview",candidateArgs(session,candidate),"CANDIDATE_REFRESH_UNSUPPORTED");extent(session.document,"Anchor",{2,3,4});}
}
void revisionsBoundariesAndEviction(){
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dz":8}}])");apply(session,R"([{"op":"feature.add","type":"box","name":"Peer"}])");V stale=candidateArgs(session,candidate);stale.set("expected_revision",V::makeNum(session.revision-1));rejected(session,"doc_preview",stale,"Revision conflict");}
    {AgentSession session;setup(session);const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dz":8}}])");V args=V::makeObj();args.set("expected_revision",V::makeNum(session.revision));accepted(session.call("doc_new",args));rejected(session,"doc_preview",candidateArgs(session,candidate),"Prepared candidate");}
    {AgentSession session;setup(session);const auto path=std::filesystem::temp_directory_path()/("ark-candidate-refresh-031b-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".arkjson");struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};const auto raw=path.u8string();const std::string destination(reinterpret_cast<const char*>(raw.data()),raw.size());check(saveDocument(session.document,destination.c_str()),"Synthetic project-boundary fixture saves");const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dz":8}}])");V args=V::makeObj();args.set("path",V::makeStr(destination));args.set("expected_revision",V::makeNum(session.revision));accepted(session.call("doc_load",args));rejected(session,"doc_preview",candidateArgs(session,candidate),"Prepared candidate");extent(session.document,"Anchor",{2,3,4});}
    {AgentSession session;setup(session);std::vector<V> candidates;std::set<std::string> ids;for(int i=0;i<6;++i){auto proposal=preview(session,R"([{"op":"feature.edit","id":1,"p":{"dz":8}}])");check(ids.insert(field(proposal,"candidate_id").str).second,"Retained candidate IDs must never be reused across eviction");check(field(proposal,"expires_in_seconds").num>0&&field(proposal,"expires_in_seconds").num<=300,"Prepared receipt exposes bounded expiry without a wall-clock sleep");candidates.push_back(std::move(proposal));}rejected(session,"doc_preview",candidateArgs(session,candidates.front()),"Prepared candidate");const auto fresh=refresh(session,candidates.back());commitChecked(session,fresh);extent(session.document,"Anchor",{2,3,8});}
}
#ifdef ARK_CONTRACTS_NATIVE_TESTS
std::string canonical(const V& value){std::string bytes,error;check(encodeDocumentValue(value,bytes,&error),error);return bytes;}
void replacementImportBoundary(){
    namespace fs=std::filesystem;const auto directory=fs::temp_directory_path()/("ark-candidate-import-031b-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(directory),"Synthetic import workspace must be new");
    struct Cleanup{fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{directory};
    const auto path=directory/"generated-sheet.step";const auto raw=path.u8string();const std::string destination(reinterpret_cast<const char*>(raw.data()),raw.size());
    const auto generated=nativeWorkerRequest(json(R"({"operation":"model_sheet","request":{"width":60,"leg_a":80,"leg_b":50,"thickness":2,"inner_radius":5,"angle":90,"k_factor":0.5}})"));
    exportNativeBrep(field(generated,"brep_base64").str,destination);
    auto import=[&](AgentSession& session,bool replace){V args=V::makeObj();args.set("path",V::makeStr(destination));args.set("replace",V::makeBool(replace));args.set("k_factor",V::makeNum(.5));args.set("expected_revision",V::makeNum(session.revision));accepted(session.call("import_sheet_step",args));check(session.document.features.size()==1&&session.document.features.front().type==FeatureType::SourceSheet,"Synthetic sheet import must publish a verified source document");};
    {AgentSession session;session.suppressFailureCapture();const auto candidate=preview(session,R"([{"op":"feature.add","type":"box","name":"Old project proposal","p":{"dx":2,"dy":3,"dz":4}}])");import(session,true);rejected(session,"doc_preview",candidateArgs(session,candidate),"Prepared candidate");check(session.document.features.size()==1,"Replacement import must not carry a previous project's proposal into its source document");}
    {AgentSession session;session.suppressFailureCapture();const auto candidate=preview(session,R"([{"op":"feature.add","type":"box","name":"Independent append proposal","p":{"dx":2,"dy":3,"dz":4}}])");import(session,false);const auto source=canonical(session.document.features.front().sourceData);const auto sourceId=session.document.features.front().id;const auto owner=session.document.evaluationCache.at(sourceId).body;const auto fresh=refresh(session,candidate);commitChecked(session,fresh);check(session.document.features.size()==2,"Append refresh must retain the source and add exactly one proposed feature");check(session.document.features.front().type==FeatureType::SourceSheet&&session.document.features.front().id==sourceId,"Append refresh must preserve imported source feature identity");check(canonical(session.document.features.front().sourceData)==source,"Append refresh must preserve every source-package value, array order and exact B-rep byte");check(session.document.evaluationCache.at(sourceId).body==owner,"Independent refreshed append proposal must preserve imported source geometry ownership");extent(session.document,"Independent append proposal",{2,3,4});}
}
void exactIndependentEdits(){
    AgentSession session;session.suppressFailureCapture();
    apply(session,R"([{"op":"feature.add","type":"native_solid","name":"Exact target","native":{"op":"box"},"p":{"width":2,"depth":3,"height":4}},{"op":"feature.add","type":"native_solid","name":"Exact peer","native":{"op":"box"},"p":{"width":4,"depth":5,"height":6}}])");
    const auto candidate=preview(session,R"([{"op":"feature.edit","id":1,"p":{"height":9}}])");
    bool previewMeasured=false;for(const auto& row:field(field(candidate,"after"),"features").arr)if(field(row,"id").num==1){check(std::abs(field(row,"volume_mm3").num-54)<1e-8,"Original exact preview must measure the proposed volume");previewMeasured=true;}check(previewMeasured,"Original exact preview must expose its evaluated target");
    apply(session,R"([{"op":"feature.edit","id":2,"p":{"width":7}}])");const auto peerOwner=session.document.evaluationCache.at(2).body;
    const auto fresh=refresh(session,candidate);commitChecked(session,fresh);
    for(const auto& [id,expected]:std::initializer_list<std::pair<u64,double>>{{1,54},{2,210}}){const auto& data=body(session.document,id).nativeData;const auto& report=field(data,"report");check(field(report,"brep_valid").b&&!field(data,"brep_base64").str.empty(),"Refreshed exact feature must retain actual valid B-rep");check(std::abs(field(report,"volume_mm3").num-expected)<1e-8,"Exact proposal refresh must preserve both intended native volumes");}
    check(session.document.evaluationCache.at(2).body==peerOwner,"Exact peer geometry must survive refreshed proposal publication");
}
void implicitDatumAncestorConflict(){
    AgentSession session;session.suppressFailureCapture();
    apply(session,R"([
      {"op":"feature.add","type":"native_solid","name":"First support","native":{"op":"box"},"p":{"width":4,"depth":4,"height":2}},
      {"op":"feature.add","type":"native_solid","name":"Second support","native":{"op":"box"},"p":{"width":4,"depth":4,"height":10}},
      {"op":"plane.set","name":"Between supports","support":{"kind":"midplane","feature":1,"reference":"z_max","second_feature":2,"second_reference":"z_max"}},
      {"op":"sketch.set","name":"Implicit profile","sketch":{"plane":"Between supports","entities":[{"kind":"circle","dof":[0,0,1]}]}},
      {"op":"feature.add","type":"extrude","name":"Implicit ancestor","p":{"sketch":"Implicit profile","height":3}}
    ])");
    const auto sourceId=named(session.document,"Implicit ancestor").id;
    check(std::abs(meshBounds(session.document,sourceId).mn.z-6)<1e-6,"Synthetic midplane must initially lie between the actual support faces");
    const auto candidate=preview(session,R"([{"op":"feature.add","type":"transform","name":"Implicit downstream proposal","inputs":[3],"p":{"tx":5}}])");
    apply(session,R"([{"op":"feature.edit","id":2,"p":{"height":12}}])");
    check(std::abs(meshBounds(session.document,sourceId).mn.z-7)<1e-6,"Editing the second datum producer must move the actual implicit ancestor geometry");
    conflict(session,candidate);check(session.document.features.size()==3,"Rejected implicit-context refresh must preserve the peer's changed geometry without adding the proposal");
}
#endif
}
int main(){try{independentProposals();ordinaryPeerCommitAndNamedBatch();targetAndDependencyConflicts();authoringContextConflicts();namesAndUnsupportedScopes();revisionsBoundariesAndEviction();
#ifdef ARK_CONTRACTS_NATIVE_TESTS
    replacementImportBoundary();
    exactIndependentEdits();
    implicitDatumAncestorConflict();
#endif
    std::cout<<checks<<" candidate refresh checks passed: independent peer preservation, dependency conflicts, revision guards, scope rejection, boundaries and checked commit\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
