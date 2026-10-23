#include "ark/agent.hpp"
#include "ark/model.hpp"
#include "ark/serialize.hpp"
#include "ark/history_authoring.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
namespace {
unsigned checks=0;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
V json(const char* text){V v;check(arkjson::parse(text,v),"fixture JSON");return v;}
const V& field(const V& v,const char* key){auto* p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing ")+key+" in "+v.dump());return *p;}
V accepted(const V& r){check(!field(r,"isError").b,r.dump());return field(r,"structuredContent");}
const Body& body(const Document& d,u64 id){for(const auto& b:d.evalBodies)if(b.id==id)return b;throw std::runtime_error("Missing body");}
double area(const Document& d,u64 id){return body(d,id).nativeData.find("report")->find("area_mm2")->num;}
void closeTo(double a,double b,const char* why){check(std::abs(a-b)<1e-5,why);}
V canonical(V v){for(auto& x:v.obj)x.second=canonical(x.second);for(auto& x:v.arr)x=canonical(x);std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});return v;}
void apply(AgentSession& s,const char* commands){V q=V::makeObj();q.set("commands",json(commands));q.set("expected_revision",V::makeNum((double)s.revision));accepted(s.call("doc_apply",q));}
void run(){
    AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    apply(s,R"([
      {"op":"feature.add","type":"bspline_surface","name":"Half skin","p":{"u_degree":1,"v_degree":1},"native":{"poles":[[[0,0,0],[0,10,0]],[[20,0,0],[20,10,0]]]}},
      {"op":"feature.add","type":"surface_edit","name":"Symmetric skin","inputs":[1],"native":{"op":"mirror","plane":"XZ","continuity":"G1"}},
      {"op":"feature.add","type":"bspline_surface","name":"Cutter","p":{"u_degree":1,"v_degree":1},"native":{"poles":[[[8,0,-5],[8,10,-5]],[[8,0,5],[8,10,5]]]}},
      {"op":"feature.add","type":"surface_edit","name":"Cut skin","inputs":[1,3],"native":{"op":"trim_tool","tool_kind":"surface","keep_point":[12,5,0]}},
      {"op":"feature.add","type":"curve","name":"First rail","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[20,0,0]}]}},
      {"op":"feature.add","type":"curve","name":"Second rail","native":{"start":[0,10,3],"segments":[{"kind":"line","to":[20,10,3]}]}},
      {"op":"feature.add","type":"surface_edit","name":"Ruled skin","inputs":[5,6],"native":{"op":"ruled","reverse_second":false}},
      {"op":"feature.add","type":"curve","name":"Spatial boundary","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[10,0,0]},{"kind":"line","to":[10,10,2]},{"kind":"line","to":[0,10,0]},{"kind":"line","to":[0,0,0]}]}},
      {"op":"feature.add","type":"surface_fill","name":"Spatial fill","inputs":[8],"native":{"boundary_curve":8}}
    ])");
    closeTo(area(s.document,2),400,"mirror exact area");closeTo(area(s.document,4),120,"trim exact kept area");closeTo(area(s.document,7),20*std::sqrt(109.),"ruled exact area");check(area(s.document,9)>100,"nonplanar closed-wire fill");
    const auto consumed=consumedFeatureInputs(s.document);check(consumed.count(1)&&!consumed.count(3),"trim consumes its source while retaining the reference cutter");
    const auto snapshot=documentToJson(s.document);const auto revision=s.revision;
    for(const char* commands:{
        R"([{"op":"feature.add","type":"surface_fill","native":{"boundary_curve":8}}])",
        R"([{"op":"feature.add","type":"surface_edit","inputs":[1,3],"native":{"op":"trim_tool","tool_kind":"surface","keep_point":[8,5,0]}}])",
        R"([{"op":"feature.add","type":"surface_edit","inputs":[5,5],"native":{"op":"ruled","reverse_second":false}}])",
        R"([{"op":"feature.add","type":"surface_edit","inputs":[1],"native":{"op":"mirror","plane":"XY","continuity":"G1"}}])"
    }){V q=V::makeObj();q.set("commands",json(commands));q.set("expected_revision",V::makeNum((double)s.revision));check(field(s.call("doc_apply",q),"isError").b,"invalid operation rejected");check(s.revision==revision&&documentToJson(s.document)==snapshot,"rejection preserves complete drawing");}
    Document restored;std::string error;check(documentFromJson(snapshot,restored,&error),error);restored.recompute();
    for(u64 id:{2,4,7,9}){closeTo(area(restored,id),area(s.document,id),"native result survives reopen/recompute");check(canonical(restored.find(id)->nativeSpec).dump()==canonical(s.document.find(id)->nativeSpec).dump(),"source recipe survives reopen");check(restored.find(id)->inputs==s.document.find(id)->inputs,"dependencies survive reopen");}
    apply(s,R"([{"op":"feature.edit","id":1,"native":{"poles":[[[0,0,0],[0,10,0]],[[24,0,0],[24,10,0]]]}}])");
    closeTo(area(s.document,2),480,"mirror regenerates after source edit");closeTo(area(s.document,4),160,"trim regenerates after source edit");
    V parameters=json(R"({"name":"Command mirror","source":1,"plane":"XZ","continuity":"G1"})");parameters.set("expected_revision",V::makeNum((double)s.revision));
    V discovery=json(R"({"topic":"commands","command":"surface_mid_mirror"})");discovery.set("parameters",parameters);
    const auto unchanged=documentToJson(s.document);const auto prepared=field(accepted(s.call("agent_onboard",discovery)),"prepared_call");check(field(prepared,"tool").str=="doc_preview","mirror command prepares ordinary preview");
    const auto preview=accepted(s.call("doc_preview",field(prepared,"arguments")));check(documentToJson(s.document)==unchanged,"command discovery and preview preserve drawing");V publish=V::makeObj();publish.set("candidate_id",field(preview,"candidate_id"));publish.set("expected_revision",V::makeNum((double)s.revision));accepted(s.call("doc_apply",publish));closeTo(area(s.document,s.document.features.back().id),480,"prepared mirror publishes exact dependent result");
}
}
int main(){try{run();std::cout<<"Surfacing workflow: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
