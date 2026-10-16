#include "ark/agent.hpp"
#include "ark/mesh_model.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
using namespace ark;
using V=arkjson::Value;
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
V json(const char* source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Invalid public performance fixture");return value;}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing ")+key);return *found;}
void apply(AgentSession& session,V commands,bool success=true){
    V args=V::makeObj();args.set("expected_revision",V::makeNum(session.revision));args.set("commands",commands);
    const auto result=session.call("doc_apply",args);
    if(success&&field(result,"isError").b)throw std::runtime_error(result.dump());
    check(field(result,"isError").b!=success,"Invalid geometry mutation was accepted");
}
void healthy(const Document& document){for(const auto& result:document.lastResults)check(result.ok,result.error.c_str());check(document.evalBodies.size()==document.features.size(),"A valid fixture lost an evaluated feature");}
using Cache=std::unordered_map<u64,Document::EvaluationCacheEntry>;
void reused(const Document& document,const Cache& before,std::initializer_list<u64> ids){
    for(u64 id:ids){const auto old=before.find(id),current=document.evaluationCache.find(id);check(old!=before.end()&&current!=document.evaluationCache.end(),"Missing expected cache entry");check(current->second.generation==old->second.generation&&current->second.body==old->second.body,"Unrelated mutation replaced immutable cached geometry");}
}
void reevaluated(const Document& document,const Cache& before,std::initializer_list<u64> ids){
    for(u64 id:ids){const auto old=before.find(id),current=document.evaluationCache.find(id);check(old!=before.end()&&current!=document.evaluationCache.end(),"Missing dependent cache entry");check(current->second.generation!=old->second.generation&&current->second.body!=old->second.body,"A real dependency mutation reused stale evaluated geometry");}
}
const Body& body(const Document& document,u64 id){for(const auto& candidate:document.evalBodies)if(candidate.id==id)return candidate;throw std::runtime_error("Missing evaluated body");}
Mesh quad(double z){Mesh mesh;for(Vec3 p:std::initializer_list<Vec3>{{0,0,z},{2,0,z},{2,2,z},{0,2,z}})mesh.addVertex(p);mesh.idx={0,1,2,0,2,3};mesh.computeNormals();return mesh;}
V addMesh(const Mesh& mesh){V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("mesh_body"));command.set("name",V::makeStr("Retained triangle source"));command.set("mesh",meshToPayload(mesh));return command;}

void meshCacheAndAtomicity(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    V commands=json(R"([{"op":"feature.add","type":"box","name":"Unrelated stock","p":{"dx":4,"dy":4,"dz":4}}])");commands.arr.push_back(addMesh(quad(1)));apply(session,commands);
    commands=json(R"([{"op":"feature.add","type":"mesh_smooth","name":"Driven smooth","inputs":[2],"p":{"iterations":1,"lambda":0.5,"pin_boundary":1}},{"op":"feature.add","type":"mesh_offset","name":"Driven offset","inputs":[3],"p":{"distance":0.25}},{"op":"feature.add","type":"mesh_edit","name":"Retained projection","inputs":[4],"native":{"op":"shrinkwrap"},"p":{"max_distance":5}}])");
    V edit=field(commands.arr[2],"native");edit.set("source_topology",V::makeStr(meshTopologyFingerprint(quad(1))));edit.set("reference_mesh",meshToPayload(quad(0)));commands.arr[2].set("native",edit);apply(session,commands);healthy(session.document);
    const auto initial=session.document.evaluationCache;
    const auto shape=body(session.document,5).nativeData.dump();
    apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"dx":9}}])"));
    reused(session.document,initial,{2,3,4,5});reevaluated(session.document,initial,{1});
    check(session.document.recomputeEvaluated==1&&session.document.recomputeReused==4,"Unrelated upstream edit reran an explicit mesh chain");
    check(body(session.document,5).nativeData.dump()==shape,"Unrelated edit altered retained projection evidence");
    const auto beforeSource=session.document.evaluationCache;
    commands=json(R"([{"op":"feature.edit","id":2}])");commands.arr[0].set("mesh",meshToPayload(quad(2)));apply(session,commands);
    reused(session.document,beforeSource,{1});reevaluated(session.document,beforeSource,{2,3,4,5});
    check(std::abs(body(session.document,4).baked->pos[2]-2.25)<1e-6,"Input coordinate mutation did not reach mesh offset geometry");
    check(std::abs(field(body(session.document,5).nativeData,"maximum_vertex_displacement_mm").num-2.25)<1e-6,"Input mutation did not reach retained shrinkwrap evidence");
    const auto beforeFailure=documentToJson(session.document,true,false);const auto cache=session.document.evaluationCache;const auto revision=session.revision;
    apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"dx":12}},{"op":"feature.edit","id":5,"p":{"max_distance":0.01}}])"),false);
    check(session.revision==revision&&documentToJson(session.document,true,false)==beforeFailure,"Failed dependent mesh edit published a partial batch");reused(session.document,cache,{1,2,3,4,5});
}

struct Environment {
    const char* name;std::string previous;
    Environment(const char* key,const char* value):name(key),previous(std::getenv(key)?std::getenv(key):""){set(value);}
    void set(const char* value){
#ifdef _WIN32
        _putenv_s(name,value);
#else
        if(*value)setenv(name,value,1);else unsetenv(name);
#endif
    }
    ~Environment(){set(previous.c_str());}
};
void meshWorkerInputs(){
    Environment cpu("ARK_CPU_THREADS","4"),workers("ARK_FEATURE_THREADS","4");
    Document document;Feature unrelated;unrelated.type=FeatureType::Box;document.addFeature(unrelated);
    Feature source;source.type=FeatureType::MeshBody;source.sourceData=meshToPayload(quad(0));document.addFeature(source);document.recompute();healthy(document);
    for(int i=0;i<4;++i){Feature offset;offset.type=FeatureType::MeshOffset;offset.name="Independent mesh branch "+std::to_string(i);offset.inputs={2};offset.p["distance"]=std::to_string(i+1);document.addFeature(offset);}
    document.recompute();healthy(document);
    check(document.dependencyEdges==4,"Pure mesh branches acquired unrelated history dependencies");
    for(u64 id=3;id<=6;++id)check(std::abs(body(document,id).baked->pos[2]-(id-2))<1e-6,"Parallel mesh input snapshot changed authored offset");
    if(std::thread::hardware_concurrency()>1)check(document.parallelFeatures>0,"Ready mesh-body inputs could not enter audited worker snapshots");
}

#ifdef ARK_CONTRACTS_NATIVE_TESTS
double nativeVolume(const Document& document,u64 id){return field(field(body(document,id).nativeData,"report"),"volume_mm3").num;}
void nativeExplicitCache(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    apply(session,json(R"([
      {"op":"param.set","name":"StockHeight","expr":"10"},
      {"op":"param.set","name":"SweepScale","expr":"2"},
      {"op":"feature.add","type":"native_solid","name":"Unrelated exact stock","native":{"op":"box"},"p":{"width":2,"depth":2,"height":2}},
      {"op":"feature.add","type":"native_solid","name":"Support stock","native":{"op":"box"},"p":{"width":60,"depth":50,"height":"StockHeight"}},
      {"op":"feature.add","type":"native_solid","name":"Face-owned boss","inputs":[2],"native":{"op":"boss","source_face":"z_max"},"p":{"cx":30,"cy":25,"radius":3,"height":2}},
      {"op":"feature.add","type":"surface_edit","name":"Support skin","inputs":[2],"native":{"op":"extract_face","face":"z_max"}},
      {"op":"feature.add","type":"surface_fill","name":"Supported inline patch","inputs":[4],"native":{"profile":{"entities":[{"kind":"line","points":[[2,2,"StockHeight"],[8,2,"StockHeight"]]},{"kind":"line","points":[[8,2,"StockHeight"],[8,8,"StockHeight"]]},{"kind":"line","points":[[8,8,"StockHeight"],[2,8,"StockHeight"]]},{"kind":"line","points":[[2,8,"StockHeight"],[2,2,"StockHeight"]]}]},"constraints":[{"edge":0,"support":4,"face":"surface","continuity":2}]}},
      {"op":"feature.add","type":"sweep","name":"Inline law sweep","native":{"section":{"entities":[{"kind":"circle","center":[0,0],"radius":1}]},"path":{"entities":[{"kind":"line","points":[[0,0,0],[0,0,10]]}]},"laws":{"interpolation":"linear","stations":[{"fraction":0,"twist_deg":0,"scale":1},{"fraction":1,"twist_deg":45,"scale":"SweepScale"}]}},"p":{"solid":1}},
      {"op":"feature.add","type":"bspline_surface","name":"Independent inline support","native":{"poles":[[[0,0,0],[0,10,0]],[[10,0,0],[10,10,0]]]},"p":{"u_degree":1,"v_degree":1}},
      {"op":"feature.add","type":"offset_surface","name":"Offset inline support","inputs":[7],"p":{"distance":1}},
      {"op":"feature.add","type":"thicken","name":"Thickened inline support","inputs":[7],"p":{"thickness":1}}
    ])"));healthy(session.document);
    check(session.document.dependencyEdges==5,"Inline native features acquired global upstream barriers");
    const auto initial=session.document.evaluationCache;
    apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"width":3}}])"));
    reused(session.document,initial,{2,3,4,5,6,7,8,9});reevaluated(session.document,initial,{1});
    check(session.document.recomputeEvaluated==1&&session.document.recomputeReused==8,"Unrelated upstream exact edit invalidated inline surface or031B recipe");
    const auto beforeHeight=session.document.evaluationCache;
    apply(session,json(R"([{"op":"param.set","name":"StockHeight","expr":"12"}])"));
    reevaluated(session.document,beforeHeight,{2,3,4,5});reused(session.document,beforeHeight,{1,6,7,8,9});
    check(std::abs(nativeVolume(session.document,3)-(60*50*12+18*std::acos(-1.)))<1e-5,"Face-owned recipe kept stale source volume");
    check(std::abs(field(field(body(session.document,5).nativeData,"report"),"bounds_mm").arr[2].num-12)<1e-6,"Supported surface did not move with its parameter/source");
    const auto beforeLaw=session.document.evaluationCache;
    apply(session,json(R"([{"op":"param.set","name":"SweepScale","expr":"3"}])"));
    reevaluated(session.document,beforeLaw,{6});reused(session.document,beforeLaw,{1,2,3,4,5,7,8,9});
    check(std::abs(nativeVolume(session.document,6)-std::acos(-1.)*10*13./3.)<1e-4,"Expression-bound law reused stale geometry");

    // A genuinely projected occurrence sketch exists elsewhere in the document.
    // It must retain its dependencies without polluting pure native cache keys.
    apply(session,json(R"([{"op":"assembly.add","name":"Observed stock occurrence","feature":2,"fixed":true},{"op":"plane.set","name":"Observation plane","p":{"oz":0}},{"op":"sketch.set","name":"Unrelated projected observation","sketch":{"plane":"Observation plane","entities":[{"kind":"project","name":"edge","feature":2,"reference":"edge.x.ymin.zmax","mode":"normal","source_instance":1}]}}])"));
    const auto beforeNamed=session.document.evaluationCache;
    apply(session,json(R"([{"op":"plane.set","name":"Observation plane","p":{"oz":2}},{"op":"assembly.edit","id":1,"p":{"tx":1}}])"));
    reused(session.document,beforeNamed,{1,2,3,4,5,6,7,8,9});check(session.document.recomputeEvaluated==0,"Unrelated projected sketch/occurrence globally invalidated explicit recipes");
    const auto beforeFailure=documentToJson(session.document,true,false);const auto cache=session.document.evaluationCache;const auto revision=session.revision;
    apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"width":8}},{"op":"feature.edit","id":5,"inputs":[2]}])"),false);
    check(session.revision==revision&&documentToJson(session.document,true,false)==beforeFailure,"Undeclared support published a partial cache or source mutation");reused(session.document,cache,{1,2,3,4,5,6,7,8,9});
}

void namedSweepVeto(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    apply(session,json(R"([{"op":"feature.add","type":"native_solid","name":"Unrelated earlier source","native":{"op":"box"},"p":{"width":2,"depth":2,"height":2}},{"op":"plane.set","name":"Named section plane","p":{"oz":0}},{"op":"sketch.set","name":"Named section","sketch":{"plane":"Named section plane","entities":[{"kind":"circle","dof":[0,0,1]}]}},{"op":"sketch.set","name":"Named path","sketch":{"plane":"XZ","entities":[{"kind":"line","dof":[0,0,0,10]}]}},{"op":"feature.add","type":"sweep","name":"Named-resource sweep","native":{"section":"Named section","path":"Named path"},"p":{"solid":1}}])"));healthy(session.document);
    check(session.document.dependencyEdges==1,"Named sweep lost its semantic publication barrier");
    const auto before=session.document.evaluationCache;
    apply(session,json(R"([{"op":"sketch.set","name":"Named section","sketch":{"plane":"Named section plane","entities":[{"kind":"circle","dof":[0,0,2]}]}}])"));
    reused(session.document,before,{1});reevaluated(session.document,before,{2});
    check(std::abs(nativeVolume(session.document,2)-40*std::acos(-1.))<1e-4,"Named sketch mutation reused stale sweep geometry");
}
#endif
}

int main(){try{meshCacheAndAtomicity();meshWorkerInputs();
#ifdef ARK_CONTRACTS_NATIVE_TESTS
    nativeExplicitCache();namedSweepVeto();
#endif
    std::cout<<"031B evaluation performance: "<<checks<<" cache custody, real dependency, worker snapshot and atomicity checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
