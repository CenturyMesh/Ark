#include "ark/agent.hpp"
#include "ark/native_backend.hpp"
#include "ark/units.hpp"
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static V json(const char* text){V v;check(arkjson::parse(text,v),"fixture JSON");return v;}
int main(){try{
    DisplayUnits units;check(units.distance(25.4)=="25.4 mm"&&units.weight(1000)=="1000 g"&&units.capacity(1000000)=="1 L","SI display defaults");
    units.length="in";units.mass="lb";units.volume="mL";check(units.distance(25.4)=="1 in"&&units.weight(453.59237)=="1 lb"&&units.capacity(1000)=="1 mL","independent unit conversion");
    units.length="cm";check(units.area(100)=="1 cm2"&&units.distance(-30)=="-3 cm","squared area and signed components");
    auto restored=DisplayUnits::fromJson(units.json());check(restored.json().dump()==units.json().dump(),"settings roundtrip");bool rejected=false;auto invalid=units.json();invalid.set("volume",V::makeStr("gallons"));try{DisplayUnits::fromJson(invalid);}catch(...){rejected=true;}check(rejected,"unknown units explicitly rejected");
    AgentSession session;auto response=session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","name":"Measurement fixture","type":"native_solid","native":{"op":"box"},"p":{"width":20,"depth":10,"height":10}}]})"));check(!response.find("isError")->b,"exact box fixture");
    const auto authored=documentToJson(session.document);const auto brep=*session.document.evalBodies.front().nativeData.find("brep_base64");
    auto pick=[&](const char* origin,double radius){V args=json(R"({"direction":[0,0,-1]})");args.set("origin",json(origin));args.set("brep",brep);args.set("snap_radius_mm",V::makeNum(radius));V request=V::makeObj();request.set("operation",V::makeStr("measurement_pick"));request.set("request",args);return nativeWorkerRequest(request);};
    auto hit=pick("[5,5,30]",0);auto point=*hit.find("point_mm");check(std::abs(point.arr[0].num-5)<1e-10&&std::abs(point.arr[1].num-5)<1e-10&&std::abs(point.arr[2].num-10)<1e-10&&hit.find("authority")->str=="exact_brep","native ray hits original surface");
    hit=pick("[0.1,0.1,30]",1);point=*hit.find("point_mm");check(hit.find("snap")->str=="Vertex"&&point.arr[0].num==0&&point.arr[1].num==0&&point.arr[2].num==10,"acquisition snaps to original exact vertex");
    rejected=false;try{pick("[-10,-10,30]",0);}catch(...){rejected=true;}check(rejected,"miss rejects rather than inventing a mesh intersection");
    check(documentToJson(session.document)==authored&&session.revision==1,"measurement leaves authored model and revision unchanged");
    auto inspect=[&](V args){V r=V::makeObj();r.set("operation",V::makeStr("measurement_pick"));r.set("request",args);return nativeWorkerRequest(r);};
    V args=json(R"({"action":"entity","kind":"edge","index":0})");args.set("brep",brep);auto edge=inspect(args);check(edge.find("type")->str=="Line"&&std::abs(edge.find("length_mm")->num-10)<1e-7,"exact box edge length");
    args.set("placement",json("[1,0,0,0,0,1,0,0,0,0,1,0,30,0,0,1]"));auto moved=inspect(args);V pair=json(R"({"action":"between"})");pair.set("brep",*edge.find("brep"));pair.set("other_brep",*moved.find("brep"));auto distance=inspect(pair);check(std::abs(distance.find("distance_mm")->num-30)<1e-7,"world occurrence distance");
    args.set("index",V::makeNum(9999));rejected=false;try{inspect(args);}catch(...){rejected=true;}check(rejected,"foreign subshape index rejects");args.set("index",V::makeNum(0));args.set("placement",json("[2,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]"));rejected=false;try{inspect(args);}catch(...){rejected=true;}check(rejected,"scaled occurrence is not silently normalized");
    V section=json(R"({"action":"section","origin":[0,0,5],"normal":[0,0,1]})");section.set("brep",brep);auto cut=inspect(section);check(cut.find("mesh")&&cut.find("cap")&&std::abs(cut.find("section_area_mm2")->num-200)<1e-6,"box section cap area");
    section.set("normal",json("[0,0,-1]"));check(std::abs(inspect(section).find("section_area_mm2")->num-200)<1e-6,"flipped section");section.set("origin",json("[0,0,-20]"));check(!inspect(section).find("mesh"),"outside section can be empty");
    check(documentToJson(session.document)==authored&&session.revision==1,"entity and section inspection are read-only");
    response=session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"feature.add","name":"Round fixture","type":"native_solid","native":{"op":"cylinder"},"p":{"radius":7,"height":20}}]})"));check(!response.find("isError")->b,"cylinder fixture");
    auto round=*session.document.evalBodies.back().nativeData.find("brep_base64");args=json(R"({"action":"entity","kind":"face","index":0})");args.set("brep",round);auto cylinder=inspect(args);check(cylinder.find("type")->str=="Cylinder"&&std::abs(cylinder.find("diameter_mm")->num-14)<1e-7,"cylindrical face diameter");
    args.set("kind",V::makeStr("edge"));auto circle=inspect(args);check(circle.find("diameter_mm")&&std::abs(circle.find("diameter_mm")->num-14)<1e-7,"circular edge diameter");
    auto solid=[&](V model){V r=V::makeObj();r.set("operation",V::makeStr("model_solid"));r.set("request",model);return nativeWorkerRequest(r);};
    auto inner=solid(json(R"({"op":"cylinder","radius":3,"height":20})"));V hollow=json(R"({"op":"cut"})");hollow.set("input_brep",round);hollow.set("tool_brep",*inner.find("brep_base64"));auto tube=solid(hollow);section=json(R"({"action":"section","origin":[0,0,10],"normal":[0,0,-1]})");section.set("brep",*tube.find("brep_base64"));auto cap=inspect(section);check(std::abs(cap.find("section_area_mm2")->num-40*K_PI)<1e-5,"section preserves hollow bore");
    const auto& mesh=*cap.find("cap");const auto& positions=mesh.find("pos")->arr;const auto& indices=mesh.find("idx")->arr;double area=0;for(size_t i=0;i<indices.size();i+=3){Vec3 p[3];for(int k=0;k<3;++k){size_t v=(size_t)indices[i+k].num*3;p[k]={positions[v].num,positions[v+1].num,positions[v+2].num};}area+=(p[1]-p[0]).cross(p[2]-p[0]).len()*.5;auto center=(p[0]+p[1]+p[2])/3.;check(center.x*center.x+center.y*center.y>8.9,"cap triangle filled the hole");}check(std::abs(area-40*K_PI)<.3,"cap tessellation area matches hollow exact section");
    std::cout<<"Measurement units, exact native picks, snap, rejection and read-only custody passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
