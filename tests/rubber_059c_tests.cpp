#include "ark/rubber.hpp"
#include "ark/occt_solid.hpp"
#include "ark/media_044b.hpp"
#include "../src/occt/common.hpp"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace ark;using V=arkjson::Value;
static unsigned checks=0;
static void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
static V json(const char* t){V v;check(arkjson::parse(t,v),"fixture JSON");return v;}
static const V& field(const V& v,const char* k){auto* p=v.find(k);if(!p)throw std::runtime_error(std::string("Missing ")+k+" in "+v.dump());return *p;}
static V& edit(V& v,const char* k){for(auto& item:v.obj)if(item.first==k)return item.second;throw std::runtime_error("Missing mutable field");}
static V canonical(V v){if(v.t==V::Obj){for(auto& x:v.obj)x.second=canonical(x.second);std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}if(v.t==V::Arr)for(auto& x:v.arr)x=canonical(x);return v;}
static V accepted(const V& r){check(!field(r,"isError").b,r.dump());return field(r,"structuredContent");}
template<class F>static void rejects(F f,const char* why){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,why);}
static double volume(const V& q){const auto r=occt::modelSolidNative(q);const auto shape=occt::readBrep(field(r,"brep_base64").str);check(BRepCheck_Analyzer(shape).IsValid(),"exact valid solid");check(!field(field(r,"topology"),"references").arr.empty(),"semantic refs");GProp_GProps props;BRepGProp::VolumeProperties(shape,props);return props.Mass();}
static void closeTo(double a,double b,double tol,const char* label){check(std::abs(a-b)<=tol,std::string(label)+" actual="+std::to_string(a)+" expected="+std::to_string(b));}
static void geometry(){const double pi=std::acos(-1.);
    auto belt=json(R"({"op":"rubber_body","kind":"belt","center_distance_mm":120,"radius_mm":30,"thickness_mm":4,"width_mm":20})");closeTo(volume(belt),(240+2*pi*30)*4*20,1e-5,"racetrack analytic volume");belt.set("kind",V::makeStr("track"));closeTo(volume(belt),(240+2*pi*30)*4*20,1e-5,"track analytic volume");
    auto tire=json(R"({"op":"rubber_body","kind":"tire","major_radius_mm":60,"radial_radius_mm":20,"axial_radius_mm":12,"wall_mm":0})");closeTo(volume(tire),2*pi*pi*60*20*12,1e-4,"elliptic torus volume");tire.set("wall_mm",V::makeNum(3));closeTo(volume(tire),2*pi*pi*60*(20*12-17*9),1e-4,"hollow elliptic torus volume");
    auto ball=json(R"({"op":"rubber_body","kind":"ball","radius_mm":60,"wall_mm":3})");closeTo(volume(ball),4*pi/3*(60*60*60-57*57*57),1e-5,"sphere shell volume");
    auto invalid=belt;invalid.set("thickness_mm",V::makeNum(60));rejects([&]{volume(invalid);},"collapsed belt reject");invalid=tire;invalid.set("wall_mm",V::makeNum(12));rejects([&]{volume(invalid);},"collapsed tire cavity reject");invalid=ball;invalid.set("radius_mm",V::makeNum(-1));rejects([&]{volume(invalid);},"negative dimension reject");invalid=ball;invalid.set("typo",V::makeNum(1));rejects([&]{volume(invalid);},"unknown native field reject");
    auto relief=json(R"({"pattern":{"distribution":"grid","around":6,"rows":1},"motif":"spike","height_mm":4,"width_mm":3,"embed_mm":1})");tire.set("relief",relief);check(volume(tire)>2*pi*pi*60*(20*12-17*9),"fused spikes add exact volume");auto pattern=*relief.find("pattern");pattern.set("around",V::makeNum(300));relief.set("pattern",pattern);tire.set("relief",relief);rejects([&]{volume(tire);},"exact count ceiling explicit");
}
static void authoring(){AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
    auto plan=json(R"({"action":"create","expected_revision":0,"kind":"belt","name":"Synthetic belt","dimensions":{"center_distance_mm":120,"radius_mm":30,"thickness_mm":4,"width_mm":20},"appearance":{"pattern":"traction","pattern_scale_mm":5}})");
    const auto baseline=documentToJson(s.document);auto proposed=accepted(s.call("rubber_plan",plan));check(s.revision==0&&documentToJson(s.document)==baseline,"planning read-only");
    auto args=field(field(proposed,"preview"),"arguments");accepted(s.call("doc_preview",args));check(s.revision==0&&documentToJson(s.document)==baseline,"preview read-only");accepted(s.call("doc_apply",args));check(s.revision==1&&s.document.features.size()==1,"publish exact editable body");const u64 id=s.document.features.front().id;
    auto malformed=plan;malformed.set("expected_revision",V::makeNum(0));check(field(s.call("rubber_plan",malformed),"isError").b,"stale plan rejects");
    auto material=json(R"({"commands":[{"op":"material.set","name":"Synthetic rubber","density_kg_m3":1100,"physical":{"young_modulus_Pa":3000000,"poisson_ratio":0.49}},{"op":"feature.edit","id":1,"material":"Synthetic rubber"}]})");edit(material,"commands").arr[1].set("id",V::makeNum((double)id));material.set("expected_revision",V::makeNum((double)s.revision));accepted(s.call("doc_apply",material));
    Document restored;std::string error;check(documentFromJson(documentToJson(s.document),restored,&error),"persistence roundtrip "+error);check(canonical(restored.features.front().nativeSpec).dump()==canonical(s.document.features.front().nativeSpec).dump(),"native recipe preserved: "+restored.features.front().nativeSpec.dump()+" vs "+s.document.features.front().nativeSpec.dump());check(canonical(restored.features.front().appearance).dump()==canonical(s.document.features.front().appearance).dump(),"procedural appearance preserved");
    auto study=json(R"({"rubber":{"include_frames":true,"backend":"cpu","duration_s":0.04,"output_fps":25,"around":16,"rows":4,"ground":false,"translation_mm":[0,0,100]}})");study.set("expected_revision",V::makeNum((double)s.revision));edit(study,"rubber").set("id",V::makeNum((double)id));const auto before=documentToJson(s.document);const auto result=accepted(s.call("physics_inspect",study));check(documentToJson(s.document)==before,"study does not edit CAD");closeTo(field(result,"mass_kg").num,(240+2*std::acos(-1.)*30)*4*20*1.1e-6,1e-9,"native mass authority");check(field(result,"nodes_count").num==64,"requested analysis resolution");check(field(result,"frames").arr.size()==2,"complete requested timeline");const auto& frames=field(result,"frames").arr;const auto& start=field(frames.front(),"positions_mm").arr;const auto& end=field(frames.back(),"positions_mm").arr;double dz=0;for(size_t i=0;i<start.size();++i)dz+=end[i].arr[2].num-start[i].arr[2].num;check(dz<0,"gravity deforms/translates derived network");
    auto stale=study;stale.set("expected_revision",V::makeNum(0));check(field(s.call("physics_inspect",stale),"isError").b,"stale physics reject");auto bad=study;edit(bad,"rubber").set("around",V::makeNum(2));check(field(s.call("physics_inspect",bad),"isError").b,"invalid resolution reject");bad=study;edit(bad,"rubber").set("backend",V::makeStr("automatic"));check(field(s.call("physics_inspect",bad),"isError").b,"no implicit backend fallback");
    accepted(s.call("agent_onboard",json(R"({"topic":"rubber","detail":"guide"})")));auto commands=accepted(s.call("agent_onboard",json(R"({"topic":"commands","query":"rubber"})")));check(commands.dump().find("rubber_body")!=std::string::npos,"command discovery");
    auto invalidCommand=args;invalidCommand.set("expected_revision",V::makeNum((double)s.revision));edit(edit(invalidCommand,"commands").arr[0],"p").set("thickness_mm",V::makeNum(100));check(field(s.call("doc_apply",invalidCommand),"isError").b&&documentToJson(s.document)==before,"invalid geometry rolls back atomically");
}
static void appearances(){for(const auto* name:{"pebble","traction","chevron"}){const int k=appearancePatternKind044B(name);check(k>=8,"new appearance registered");for(int i=-20;i<20;++i)for(int j=-20;j<20;++j){const double u=i*.137,v=j*.113,field=appearancePatternField044B(k,u,v);check(std::isfinite(field)&&field>=0&&field<=1,"bounded continuous appearance field");closeTo(field,appearancePatternField044B(k,u+2,v+2),1e-12,"periodic appearance");}}}
static void otherStudies(){
    for(const char* profile:{R"({"op":"rubber_body","kind":"tire","major_radius_mm":60,"radial_radius_mm":20,"axial_radius_mm":12,"wall_mm":3})",R"({"op":"rubber_body","kind":"ball","radius_mm":60,"wall_mm":3})"}){
        AgentSession s;s.suppressFailureCapture();s.suppressIterationHistory();
        V args=json(R"({"expected_revision":0,"commands":[{"op":"material.set","name":"Synthetic","density_kg_m3":1100,"physical":{"young_modulus_Pa":3000000}},{"op":"feature.add","type":"native_solid","name":"Synthetic","material":"Synthetic"}]})");
        edit(args,"commands").arr[1].set("native",json(profile));accepted(s.call("doc_apply",args));const u64 id=s.document.features.back().id;
        V study=json(R"({"rubber":{"include_frames":true,"backend":"cpu","duration_s":0.00001,"around":16,"rows":4,"ground":false,"gravity_m_s2":[0,0,0]}})");study.set("expected_revision",V::makeNum((double)s.revision));edit(study,"rubber").set("id",V::makeNum((double)id));const auto result=accepted(s.call("physics_inspect",study));const bool ball=field(json(profile),"kind").str=="ball";
        check(field(result,"nodes_count").num==(ball?66:64),"tire/sphere surface topology count");const auto& frames=field(result,"frames").arr;const auto& first=field(frames.front(),"positions_mm").arr;const auto& last=field(frames.back(),"positions_mm").arr;for(size_t i=0;i<first.size();++i)for(size_t c=0;c<3;++c)closeTo(first[i].arr[c].num,last[i].arr[c].num,1e-10,"unloaded network rests");
        edit(study,"rubber").set("include_frames",V::makeBool(false));const auto compact=accepted(s.call("physics_inspect",study));check(!compact.find("frames")&&!compact.find("triangles")&&field(compact,"frame_count").num==2,"agent default can omit large frame payload");
    }
}
int main(){try{geometry();authoring();otherStudies();appearances();std::cout<<checks<<" rubber authoring/native/study checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
