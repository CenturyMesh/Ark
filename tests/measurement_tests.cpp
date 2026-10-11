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
    std::cout<<"Measurement units, exact native picks, snap, rejection and read-only custody passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
