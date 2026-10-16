#include "ark/occt_solid.hpp"
#include "../src/occt/common.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Face.hxx>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using V=arkjson::Value;namespace native=ark::occt;namespace fs=std::filesystem;
int checks=0;
void check(bool value,const std::string& reason){++checks;if(!value)throw std::runtime_error(reason);}
const V& field(const V& value,const char* name){const auto* found=value.find(name);check(found,std::string("Missing synthetic field: ")+name);return *found;}
V& edit(V& value,const char* name){for(auto& row:value.obj)if(row.first==name)return row.second;throw std::runtime_error(std::string("Missing mutable synthetic field: ")+name);}
V json(const char* source){V value;check(arkjson::parse(source,value),"synthetic JSON parses");return value;}
template<class F>void rejects(F action,const char* expected){try{action();}catch(const std::exception& e){check(std::string(e.what()).find(expected)!=std::string::npos,std::string("Unexpected rejection: ")+e.what());return;}throw std::runtime_error(std::string("Expected rejection: ")+expected);}
void environment(const std::string& value){
#ifdef _WIN32
    _putenv_s("ARK_NATIVE_PROFILE_DIRECTORY",value.c_str());
#else
    if(value.empty())unsetenv("ARK_NATIVE_PROFILE_DIRECTORY");else setenv("ARK_NATIVE_PROFILE_DIRECTORY",value.c_str(),1);
#endif
}
struct TimingFixture {
    fs::path directory;std::string previous;
    TimingFixture(){if(const char* old=std::getenv("ARK_NATIVE_PROFILE_DIRECTORY"))previous=old;directory=fs::temp_directory_path()/("ark-validated-snapshot-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(directory),"create owned synthetic timing directory");const auto text=directory.u8string();environment(std::string(text.begin(),text.end()));}
    ~TimingFixture(){environment(previous);std::error_code error;if(directory.parent_path()==fs::temp_directory_path(error)&&!error&&directory.filename().string().rfind("ark-validated-snapshot-",0)==0){for(const auto& file:fs::directory_iterator(directory,error))if(file.is_regular_file())fs::remove(file.path(),error);fs::remove(directory,error);}}
    V record(const std::function<void()>& action,const char* operation){std::set<fs::path> before;for(const auto& file:fs::directory_iterator(directory))before.insert(file.path());{native::NativeProfileRequest request;request.operation(operation);action();request.succeeded();}V trace;size_t created=0;for(const auto& file:fs::directory_iterator(directory))if(!before.count(file.path())){check(arkjson::parse(native::readFile(file.path()),trace),"timing JSON parses");++created;}check(created==1,"one native request emits one isolated timing record");return trace;}
};
size_t calls(const V& trace,const char* name){for(const auto& phase:field(trace,"phases").arr)if(field(phase,"phase").str==name)return (size_t)field(phase,"calls").num;return 0;}
void parity(const V& original,const V& restored){check(field(original,"brep_base64").str==field(restored,"brep_base64").str,"original serialized B-rep bytes are retained");check(field(original,"topology").dump()==field(restored,"topology").dump(),"all validated aliases, descriptors, vertices and digest retain representation");check(field(original,"mesh").dump()==field(restored,"mesh").dump(),"fresh mesh and picking inventory retain exact producer output");for(const char* name:{"kernel","kernel_version","brep_valid","solids","faces","volume_mm3","area_mm2","bounds_mm","display_mesh_authoritative","manufacturing_authoritative","self_intersections_checked"})check(field(field(original,"report"),name).dump()==field(field(restored,"report"),name).dump(),std::string("fresh report parity: ")+name);check(!field(field(restored,"report"),"derivation_replayed").b,"validated saved design state does not claim recipe replay");}
V boxRequest(){return json(R"({"operation":"model_solid","request":{"op":"box","width":12,"depth":15,"height":8}})");}
void oneValidatedShape(){
    const auto request=boxRequest();const auto original=native::modelSolidNative(field(request,"request"));TimingFixture timing;V restored;
    auto saved=original;edit(saved,"report").set("volume_mm3",V::makeNum(999999));edit(saved,"report").set("area_mm2",V::makeNum(1));edit(saved,"report").set("bounds_mm",json("[999,999,999,1000,1000,1000]"));saved.set("mesh",V::makeObj());
    const auto trace=timing.record([&]{restored=native::restoreNativeResult(request,saved);},"restore_native_result");parity(original,restored);
    check(calls(trace,"readBrep")==1&&calls(trace,"brep.decode")==1&&calls(trace,"brep.read")==1&&calls(trace,"brep.validate")==1,"restoration decodes, reads and BRepChecks its exact B-rep once");
    check(calls(trace,"report.validate")==0&&calls(trace,"report.reuse_validation")==1,"only private proof consumes the prior same-shape validity check");
    check(calls(trace,"volume.integrate")==1&&calls(trace,"areaProperties")==1&&calls(trace,"bounds")==1&&calls(trace,"mesh.tessellate")==1&&calls(trace,"completeSelectionReferences")==1,"exact measurements, mesh and reference completion remain fresh and unchanged");
    const auto parsed=native::readBrep(field(original,"brep_base64").str);V measured;const auto publicTrace=timing.record([&]{measured=native::shapeReport(parsed,true);},"model_solid");check(calls(publicTrace,"report.validate")==1&&calls(publicTrace,"volume.integrate")==1&&calls(publicTrace,"report.reuse_validation")==0,"public shapeReport validates and remeasures arbitrary caller shapes");check(field(measured,"volume_mm3").dump()==field(field(original,"report"),"volume_mm3").dump(),"public exact signed-volume measurement matches the private path");
    auto reversed=parsed;reversed.Reverse();rejects([&]{(void)native::shapeReport(reversed,true);},"positive-volume solid");
    TopoDS_Face invalid;BRep_Builder builder;builder.MakeFace(invalid);rejects([&]{(void)native::shapeReport(invalid,false);},"B-rep validation");
    const auto again=timing.record([&]{restored=native::restoreNativeResult(request,saved);},"restore_native_result");check(calls(again,"brep.validate")==1&&calls(again,"volume.integrate")==1,"another request receives no process cache of shapes, measurements or validity");parity(original,restored);
    auto bad=original;bad.set("brep_base64",V::makeStr(native::base64Encode("invalid synthetic B-rep")));edit(bad,"topology").set("brep_sha256",V::makeStr(native::sha256("invalid synthetic B-rep")));rejects([&]{(void)native::restoreNativeResult(request,bad);},"Invalid native B-rep payload");
    bad=original;edit(bad,"topology").set("brep_sha256",V::makeStr(std::string(64,'0')));rejects([&]{(void)native::restoreNativeResult(request,bad);},"B-rep digest mismatch");
    bad=original;edit(bad,"report").set("volume_mm3",V::makeNum(-1));rejects([&]{(void)native::restoreNativeResult(request,bad);},"signed-volume contract");
    bad=original;auto& rows=edit(edit(bad,"topology"),"references");rows.arr.push_back(rows.arr.front());rejects([&]{(void)native::restoreNativeResult(request,bad);},"duplicate reference name");
}
V sequence(const char* op,const V& a,const V& b){V envelope=V::makeObj(),request=V::makeObj(),pieces=V::makeArr();envelope.set("operation",V::makeStr("model_boolean_sequence"));request.set("schema",V::makeNum(1));request.set("op",V::makeStr(op));int index=1;for(const auto* source:{&a,&b}){V piece=V::makeObj();piece.set("brep_base64",field(*source,"brep_base64"));piece.set("topology",field(*source,"topology"));piece.set("namespace",V::makeStr("feature."+std::to_string(index++)));pieces.arr.push_back(piece);}request.set("pieces",pieces);envelope.set("request",request);return envelope;}
void sequenceSnapshots(){
    const auto a=native::shapeResult(BRepPrimAPI_MakeBox(10,10,10).Shape(),true),b=native::shapeResult(BRepPrimAPI_MakeBox(gp_Pnt(5,0,0),10,10,10).Shape(),true);
    V saved;
    for(const char* op:{"union","cut","common"}){V request=V::makeObj();request.set("op",V::makeStr(op));request.set("input_brep",field(a,"brep_base64"));request.set("input_topology",field(a,"topology"));request.set("tool_brep",field(b,"brep_base64"));request.set("tool_topology",field(b,"topology"));saved=native::modelSolidNative(request);const auto restored=native::restoreNativeResult(sequence(op,a,b),saved);parity(saved,restored);check(std::abs(field(field(restored,"report"),"volume_mm3").num-(std::string(op)=="union"?1500:500))<1e-8,"aggregate envelope restores actual final Boolean geometry without replaying prefixes");}
    auto valid=sequence("common",a,b);auto pieces=field(field(valid,"request"),"pieces");pieces.arr[0].obj.erase(std::remove_if(pieces.arr[0].obj.begin(),pieces.arr[0].obj.end(),[](const auto& row){return row.first=="topology";}),pieces.arr[0].obj.end());pieces.arr[1].set("frame",json("[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]"));edit(valid,"request").set("pieces",pieces);parity(saved,native::restoreNativeResult(valid,saved));
    for(int kind=0;kind<13;++kind){auto invalid=valid;auto& request=edit(invalid,"request");auto rows=field(request,"pieces");
        if(kind==0)request.set("schema",V::makeNum(2));if(kind==1)request.set("op",V::makeStr("mesh_union"));if(kind==2)request.set("ignored",V::makeBool(true));
        if(kind==3)rows.arr.pop_back();if(kind==4)while(rows.arr.size()<17)rows.arr.push_back(rows.arr.front());
        if(kind==5)rows.arr[0].set("namespace",V::makeStr(""));if(kind==6)rows.arr[0].set("brep_base64",V::makeNum(1));if(kind==7)rows.arr[0].set("topology",V{});if(kind==8)rows.arr[0].set("path",V::makeStr("ignored"));
        if(kind==9)rows.arr[1].set("frame",json("[1,0,0]"));if(kind==10){auto matrix=field(rows.arr[1],"frame");matrix.arr[0]=V::makeNum(-1);rows.arr[1].set("frame",matrix);}if(kind==11){auto matrix=field(rows.arr[1],"frame");matrix.arr[0]=V::makeNum(2);rows.arr[1].set("frame",matrix);}if(kind==12)request.obj.push_back({"schema",V::makeNum(1)});
        request.set("pieces",rows);rejects([&]{(void)native::restoreNativeResult(invalid,saved);},"Saved native result rejected");
    }
}
}
int main(){try{oneValidatedShape();sequenceSnapshots();std::cout<<"032D validated snapshot: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"032D validated snapshot failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
