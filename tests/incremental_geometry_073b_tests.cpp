#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "ark/assembly_authoring.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;using V=arkjson::Value;size_t checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
V json(const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid incremental fixture");return value;}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing ")+key);return *found;}
V accepted(const V& response){if(field(response,"isError").b)throw std::runtime_error(response.dump());return field(response,"structuredContent");}
V arguments(AgentSession& session,V commands){V args=V::makeObj();args.set("commands",std::move(commands));args.set("expected_revision",V::makeNum(session.revision));args.set("detail",V::makeStr("changes"));return args;}
V apply(AgentSession& session,V commands){return accepted(session.call("doc_apply",arguments(session,std::move(commands))));}
void reject(AgentSession& session,V commands){const auto before=documentToJson(session.document);const auto revision=session.revision;check(field(session.call("doc_apply",arguments(session,std::move(commands))),"isError").b,"Invalid incremental edit accepted");check(documentToJson(session.document)==before&&session.revision==revision,"Rejected edit changed live source/revision");}
const Body& body(const Document& document,u64 id){for(const auto& b:document.evalBodies)if(b.id==id)return b;throw std::runtime_error("Missing fixture body");}
using Cache=std::unordered_map<u64,Document::EvaluationCacheEntry>;
void retained(const Document& document,const Cache& before,std::initializer_list<u64> ids){for(u64 id:ids)check(document.evaluationCache.at(id).body==before.at(id).body&&document.evaluationCache.at(id).generation==before.at(id).generation,"Unrelated body lost cache identity/generation");}
void changed(const Document& document,const Cache& before,std::initializer_list<u64> ids){for(u64 id:ids)check(document.evaluationCache.at(id).body!=before.at(id).body&&document.evaluationCache.at(id).generation!=before.at(id).generation,"Affected geometry reused stale cache owner");}
void near(double value,double expected,const char* why){check(std::abs(value-expected)<1e-6,why);}
void healthy(Document& document){document.recompute();for(const auto& result:document.lastResults)check(result.ok,result.error.c_str());}
#ifdef ARK_INCREMENTAL_GEOMETRY_NATIVE_TESTS
void fixture(AgentSession& session,bool pose){
    session.suppressFailureCapture();session.suppressIterationHistory();auto commands=json(R"([{"op":"feature.add","type":"native_solid","name":"Anchor","native":{"op":"box"},"p":{"width":6,"depth":4,"height":3}},{"op":"feature.add","type":"native_solid","name":"Moving","native":{"op":"box"},"p":{"width":6,"depth":4,"height":3}}])");
    for(int i=0;i<12;++i){auto sketch=json(R"({"op":"sketch.set","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[0,0,4]}]}})");sketch.set("name",V::makeStr("Profile "+std::to_string(i)));commands.arr.push_back(sketch);auto extrude=json(R"({"op":"feature.add","type":"native_solid","native":{"op":"extrude"},"p":{"depth":10}})");extrude.set("name",V::makeStr("Independent "+std::to_string(i)));auto parameters=field(extrude,"p");parameters.set("sketch",V::makeStr("Profile "+std::to_string(i)));extrude.set("p",parameters);commands.arr.push_back(extrude);}
    const auto placement=json(R"([{"op":"assembly.add","name":"Anchor instance","feature":1,"fixed":true},{"op":"assembly.add","name":"Moving instance","feature":2,"p":{"tz":5}},{"op":"mate.add","a":1,"b":2,"type":"distance","reference_a":"origin","reference_b":"origin","value":5}])");for(const auto& command:placement.arr)commands.arr.push_back(command);
    if(pose){const auto projected=json(R"([{"op":"sketch.set","name":"Pose observation","sketch":{"plane":"XZ","entities":[{"kind":"project","name":"face","feature":2,"source_instance":2,"reference":"y_min","mode":"normal"}]}},{"op":"feature.add","type":"native_solid","name":"Pose extrusion","native":{"op":"extrude"},"p":{"sketch":"Pose observation","depth":2}}])");for(const auto& command:projected.arr)commands.arr.push_back(command);}
    apply(session,commands);
}
double movingZ(Document& document){std::vector<AssemblyPlacement> placements;std::string error;check(resolveAssembly(document,placements,error),error.c_str());for(const auto& placement:placements)if(placement.node==2)return placement.world.m[14];throw std::runtime_error("Missing moving occurrence");}
void localMates(){
    AgentSession session;fixture(session,false);const auto before=session.document.evaluationCache;size_t evaluations=0;auto observer=setEvaluationObserver([&](u64,const std::string&,size_t,size_t,bool done,bool){if(!done)++evaluations;});
    const auto edited=apply(session,json(R"([{"op":"mate.edit","id":1,"value":6}])"));setEvaluationObserver(observer);
    check(!field(edited,"geometry_recomputed").b&&field(edited,"features_evaluated").num==0&&field(edited,"recompute_ms").num==0&&evaluations==0,"Pose-only mate edit performed/reported a feature rebuild");
    near(movingZ(session.document),6,"Mate-only edit did not update occurrence placement");for(u64 id=1;id<=14;++id)retained(session.document,before,{id});
    const auto source=documentToJson(session.document);const auto preview=accepted(session.call("doc_preview",arguments(session,json(R"([{"op":"mate.edit","id":1,"value":8}])"))));
    check(field(preview,"valid").b&&field(preview,"candidate_cached").b&&documentToJson(session.document)==source,"Mate preview did not retain isolated valid source");
    const auto& candidate=field(preview,"candidate_result");check(!field(candidate,"geometry_recomputed").b&&field(candidate,"features_evaluated").num==0,"Mate preview reported stale geometry work");
    const auto committed=accepted(session.call("doc_apply",field(preview,"apply")));check(field(committed,"applied_prepared").b&&!field(committed,"geometry_recomputed").b&&field(committed,"features_evaluated").num==0,"Prepared mate publication lost placement-only receipt");near(movingZ(session.document),8,"Prepared mate update omitted mate authoring");
    for(u64 id=1;id<=14;++id)retained(session.document,before,{id});
    reject(session,json(R"([{"op":"mate.edit","id":1,"value":"unknown_dimension"}])"));
    const auto live=documentToJson(session.document);auto stale=arguments(session,json(R"([{"op":"mate.edit","id":1,"value":9}])"));stale.set("expected_revision",V::makeNum(0));check(field(session.call("doc_apply",stale),"isError").b&&documentToJson(session.document)==live,"Mate-only path bypassed revision guard");
    const auto removed=apply(session,json(R"([{"op":"mate.remove","id":1}])"));check(!field(removed,"geometry_recomputed").b&&session.document.mates.empty(),"Mate removal should retain pose-independent shapes");
    const auto added=apply(session,json(R"([{"op":"mate.add","a":1,"b":2,"type":"distance","reference_a":"origin","reference_b":"origin","value":7}])"));check(!field(added,"geometry_recomputed").b,"Mate addition should retain pose-independent shapes");near(movingZ(session.document),7,"Added mate was not solved");
    const auto painted=apply(session,json(R"([{"op":"material.set","name":"Display paint","appearance":{"roughness":0.3}}])"));check(!field(painted,"geometry_recomputed").b&&field(painted,"features_evaluated").num==0&&field(painted,"recompute_ms").num==0,"Appearance receipt exposed stale evaluation counts");
}
void poseAndLocalSketch(){
    AgentSession session;fixture(session,true);auto before=session.document.evaluationCache;
    const auto moved=apply(session,json(R"([{"op":"mate.edit","id":1,"value":7}])"));
    check(field(moved,"geometry_recomputed").b&&field(moved,"features_evaluated").num==1&&field(moved,"features_reused").num==14,"Mate with a pose consumer must regenerate only the dependent extrusion");
    check(session.document.recomputeProjectionBuilds==3&&session.document.recomputeProjectionReused==14,"Simple posed projection should wait for its source and complete once, not rebuild after every unrelated feature");
    for(u64 id=1;id<=14;++id)retained(session.document,before,{id});changed(session.document,before,{15});near(body(session.document,15).bbox().mn.z,7,"Pose-projected extrusion retained old placement");
    before=session.document.evaluationCache;apply(session,json(R"([{"op":"sketch.patch","name":"Profile 0","entity":"rim","r":6}])"));
    check(session.document.recomputeEvaluated==1&&session.document.recomputeReused==14,"Local sketch edit invalidated unrelated sketch/pose branches");changed(session.document,before,{3});retained(session.document,before,{1,2,4,5,6,7,8,9,10,11,12,13,14,15});near(field(*body(session.document,3).nativeData.find("report"),"volume_mm3").num,360*std::acos(-1.),"Changed profile radius did not reach exact extrusion");
    before=session.document.evaluationCache;apply(session,json(R"([{"op":"sketch.patch","name":"Profile 1","entity":"rim","r":5}])"));changed(session.document,before,{4});retained(session.document,before,{3,5,6,7,8,9,10,11,12,13,14,15});check(session.document.recomputeEvaluated==1,"Substring-related Profile1/Profile10 names contaminated exact closure");
    apply(session,json(R"([{"op":"mate.edit","id":1,"reference_a":"z_max","value":5}])"));near(body(session.document,15).bbox().mn.z,8,"Semantic anchor face did not drive projected placement");
    before=session.document.evaluationCache;apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"height":4}}])"));
    changed(session.document,before,{1,15});retained(session.document,before,{2,3,4,5,6,7,8,9,10,11,12,13,14});near(body(session.document,15).bbox().mn.z,9,"Mate endpoint body generation missing from pose-projection/extrusion closure");check(session.document.recomputeEvaluated==2,"Changing anchor face should rebuild only anchor and projected consumer");
    reject(session,json(R"([{"op":"sketch.patch","name":"Profile 0","entity":"rim","r":-1}])"));
    const auto invalidationState=session.document.evaluationCache;reject(session,json(R"([{"op":"mate.edit","id":1,"value":"missing_pose_parameter"}])"));retained(session.document,invalidationState,{3,4,15});
    Document fresh;std::string error;check(documentFromJson(documentToJson(session.document),fresh,&error),"Incremental reference deserialization failed");healthy(fresh);
    for(const auto& original:session.document.evalBodies){const auto& actual=body(fresh,original.id);check(original.nativeData.find("brep_base64")->str==actual.nativeData.find("brep_base64")->str,"Fresh full rebuild differs from incremental exact B-rep");}
}
void latePoseEndpointAndFallback(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    apply(session,json(R"([
      {"op":"feature.add","type":"native_solid","name":"Moving","native":{"op":"box"},"p":{"width":6,"depth":4,"height":3}},
      {"op":"feature.add","type":"native_solid","name":"Unrelated","native":{"op":"box"},"p":{"width":2,"depth":2,"height":2}},
      {"op":"feature.add","type":"native_solid","name":"Late anchor","native":{"op":"box"},"p":{"width":6,"depth":4,"height":3}},
      {"op":"assembly.add","name":"Moving instance","feature":1},
      {"op":"assembly.add","name":"Anchor instance","feature":3,"fixed":true},
      {"op":"mate.add","a":2,"b":1,"type":"distance","reference_a":"z_max","reference_b":"origin","value":5},
      {"op":"sketch.set","name":"Observation","sketch":{"plane":"XZ","entities":[{"kind":"project","name":"face","feature":1,"source_instance":1,"reference":"y_min","mode":"normal"}]}},
      {"op":"feature.add","type":"native_solid","name":"Observed extrusion","native":{"op":"extrude"},"p":{"sketch":"Observation","depth":2}}
    ])"));
    check(session.document.recomputeProjectionBuilds==4&&session.document.recomputeProjectionReused==2,"Pose projection completed before late mate endpoint publication");near(body(session.document,4).bbox().mn.z,8,"Late anchor pose was not resolved before extrusion");
    auto before=session.document.evaluationCache;apply(session,json(R"([{"op":"feature.edit","id":3,"p":{"height":6}}])"));changed(session.document,before,{3,4});retained(session.document,before,{1,2});near(body(session.document,4).bbox().mn.z,11,"Late anchor geometry edit reused stale occurrence frame");
    reject(session,json(R"([{"op":"feature.edit","id":3,"p":{"height":-1}}])"));
    apply(session,json(R"([{"op":"assembly.edit","id":1,"pattern":{"type":"linear","count":2,"dx":20}}])"));check(session.document.recomputeProjectionReused==0,"Patterned assembly must keep conservative projection path");
    apply(session,json(R"([{"op":"sketch.entity.set","name":"Observation","entity":{"kind":"project","name":"face","feature":1,"source_instance":1,"source_copy":1,"reference":"y_min","mode":"normal"}}])"));near(body(session.document,4).bbox().mn.x,20,"Pattern copy edit did not reach real projected geometry");
    Document fresh;std::string error;check(documentFromJson(documentToJson(session.document),fresh,&error),"Complex fallback reference deserialize");healthy(fresh);check(body(fresh,4).nativeData.find("brep_base64")->str==body(session.document,4).nativeData.find("brep_base64")->str,"Complex fallback differs from exact fresh rebuild");
}
void sourcePlanesAndBooleanTargets(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    apply(session,json(R"([
      {"op":"feature.add","type":"native_solid","name":"Target","native":{"op":"box"},"p":{"width":6,"depth":4,"height":3}},
      {"op":"sketch.set","name":"Standalone","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[2,2,1]}]}},
      {"op":"feature.add","type":"native_solid","name":"Unrelated extrusion","native":{"op":"extrude"},"p":{"sketch":"Standalone","depth":5}},
      {"op":"feature.add","type":"native_solid","name":"Joined result","inputs":[1,2],"native":{"op":"union"}},
      {"op":"plane.set","name":"Supported plane","support":{"kind":"face","feature":1,"reference":"z_max"}},
      {"op":"sketch.set","name":"Supported profile","sketch":{"plane":"Supported plane","entities":[{"kind":"circle","name":"rim","dof":[2,2,0.5]}]}},
      {"op":"feature.add","type":"native_solid","name":"Supported extrusion","native":{"op":"extrude"},"p":{"sketch":"Supported profile","depth":2}},
      {"op":"feature.add","type":"native_solid","name":"Face extrusion","inputs":[1],"native":{"op":"extrude","source_face":"z_max"},"p":{"depth":2}}
    ])"));
    const auto before=session.document.evaluationCache;apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"width":7,"height":4}}])"));
    changed(session.document,before,{1,3,4,5});retained(session.document,before,{2});near(body(session.document,4).bbox().mn.z,4,"Supported datum source generation omitted from extrusion key");near(field(*body(session.document,3).nativeData.find("report"),"volume_mm3").num,112+std::acos(-1.),"Boolean merge target dependency lost");near(field(*body(session.document,5).nativeData.find("report"),"volume_mm3").num,56,"Source-face extrusion kept stale target dimensions");
    const auto all=session.document.evaluationCache;apply(session,json(R"([{"op":"plane.set","name":"Unrelated plane","p":{"oz":20}}])"));retained(session.document,all,{1,2,3,4,5});check(session.document.recomputeEvaluated==0,"Unrelated datum edit invalidated named extrusion closure");
    reject(session,json(R"([{"op":"plane.set","name":"Supported plane","support":{"kind":"face","feature":4,"reference":"cap.end"}}])"));
}
void surfaceSketchScope(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();apply(session,json(R"([
      {"op":"sketch.set","name":"SurfaceA","sketch":{"plane":"XY","entities":[{"kind":"line","name":"edge","dof":[1,0,3,0]}]}},
      {"op":"feature.add","type":"extrude_surface","name":"Surface A","p":{"sketch":"SurfaceA","distance":4,"solid":0}},
      {"op":"sketch.set","name":"SurfaceB","sketch":{"plane":"XY","entities":[{"kind":"line","name":"edge","dof":[5,0,8,0]}]}},
      {"op":"feature.add","type":"extrude_surface","name":"Surface B","p":{"sketch":"SurfaceB","distance":6,"solid":0}},
      {"op":"feature.add","type":"revolve_surface","name":"Revolved B","p":{"sketch":"SurfaceB","angle":90,"solid":0},"native":{"axis":"Y"}}
    ])"));const auto before=session.document.evaluationCache;apply(session,json(R"([{"op":"sketch.patch","name":"SurfaceA","entity":"edge","anchor":1,"x":4}])"));changed(session.document,before,{1});retained(session.document,before,{2,3});near(field(*body(session.document,1).nativeData.find("report"),"area_mm2").num,12,"Surface profile change did not update exact area");
}
#endif
}
int main(){try{
#ifdef ARK_INCREMENTAL_GEOMETRY_NATIVE_TESTS
localMates();poseAndLocalSketch();latePoseEndpointAndFallback();sourcePlanesAndBooleanTargets();surfaceSketchScope();
#endif
std::cout<<"073B incremental geometry: "<<checks<<" mate/sketch scope, exact B-rep, cache identity, prepared placement and rejection checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
