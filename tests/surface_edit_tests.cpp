#include "ark/agent.hpp"
#include "ark/model.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
void check(bool ok,const std::string& text){if(!ok)throw std::runtime_error(text);}
V parse(const char* text){V v;check(arkjson::parse(text,v),"fixture JSON");return v;}
V call(AgentSession& s,const char* name,const V& args){auto response=s.call(name,args);check(!response.find("isError")->b,response.dump());return *response.find("structuredContent");}
V call(AgentSession& s,const char* name,const char* args){return call(s,name,parse(args));}
double area(const AgentSession& s,u64 id){for(const auto& body:s.document.evalBodies)if(body.id==id)return body.nativeData.find("report")->find("area_mm2")->num;throw std::runtime_error("No body");}
int main(){try{
    AgentSession s;call(s,"doc_apply",R"({"commands":[{"op":"feature.add","name":"Block","type":"native_solid","native":{"op":"box"},"p":{"width":20,"depth":10,"height":5}},{"op":"feature.add","name":"Skin","type":"surface_edit","inputs":["Block"],"native":{"op":"extract_face","face":"z_max"}},{"op":"feature.add","name":"Trimmed","type":"surface_edit","inputs":["Skin"],"native":{"op":"trim_plane","origin":[8,0,0],"normal":[1,0,0],"keep":"positive"}},{"op":"feature.add","name":"Wall","type":"thicken","inputs":["Trimmed"],"p":{"thickness":2}}]})");
    check(std::abs(area(s,2)-200)<1e-7,"extraction keeps exact face area");check(std::abs(area(s,3)-120)<1e-7,"trimmed surface area");
    auto ref=call(s,"reference_inspect",R"({"id":3,"reference":"surface"})");check(!ref.obj.empty(),"trim output semantic face");
    const auto before=s.revision;const auto snapshot=documentToJson(s.document);
    for(const char* bad:{R"({"commands":[{"op":"feature.add","type":"surface_edit","inputs":["Skin"],"native":{"op":"trim_plane","origin":[30,0,0],"normal":[1,0,0],"keep":"positive"}}]})",R"({"commands":[{"op":"feature.add","type":"surface_edit","inputs":["Block"],"native":{"op":"extract_face","face":"missing"}}]})",R"({"commands":[{"op":"feature.add","type":"surface_edit","inputs":["Block"],"native":{"op":"trim_plane","origin":[10,0,0],"normal":[1,0,0],"keep":"positive"}}]})"}){
        auto response=s.call("doc_apply",parse(bad));check(response.find("isError")->b,"unsupported trim/face rejected");check(s.revision==before&&documentToJson(s.document)==snapshot,"failure preserves document/history");
    }
    Document restored;std::string error;check(documentFromJson(snapshot,restored,&error),error);restored.recompute();check(restored.evalBodies.size()==4,"surface recompute roundtrip");check(restored.features[2].nativeSpec.find("op")->str=="trim_plane","surface program roundtrip");
    V parsed;check(arkjson::parse(snapshot,parsed),"saved JSON");check(parsed.find("version")->num>=26,"surface format version");
    const auto path=std::filesystem::temp_directory_path()/"ark-surface-edit-roundtrip.step";V args=V::makeObj();args.set("id",V::makeNum(3));args.set("path",V::makeStr(path.string()));auto exported=call(s,"export_step",args);check(std::filesystem::exists(path),"exact surface STEP export");std::filesystem::remove(path);
    call(s,"doc_apply",R"({"commands":[{"op":"feature.add","type":"mesh_weld","name":"Explicit display sample","inputs":["Trimmed"],"p":{"epsilon":0.00001,"sample":1}}]})");
    auto mesh=call(s,"mesh_inspect",R"({"id":5})");check(mesh.find("mesh_model")!=nullptr,"explicit hybrid mesh conversion");
    AgentSession curved;
    call(curved,"doc_apply",R"({"commands":[{"op":"feature.add","type":"bspline_surface","name":"Warp","native":{"poles":[[[0,0,0],[0,10,0]],[[20,0,0],[20,10,4]]]},"p":{"u_degree":1,"v_degree":1}},{"op":"feature.add","type":"surface_edit","name":"Cut warp","inputs":["Warp"],"native":{"op":"trim_plane","origin":[8,0,0],"normal":[1,0,0],"keep":"positive"}},{"op":"feature.add","type":"surface_edit","name":"Extracted warp","inputs":["Cut warp"],"native":{"op":"extract_face","face":"surface"}}]})");
    check(std::abs(area(curved,2)-area(curved,3))<1e-7,"nonanalytic semantic face extraction does not require an analytic coordinate frame");
    AgentSession sphere;call(sphere,"doc_apply",R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Sphere","native":{"op":"sphere"},"p":{"radius":10}},{"op":"feature.add","type":"surface_edit","inputs":["Sphere"],"native":{"op":"extract_face","face":"surface"}}]})");
    check(std::abs(area(sphere,2)-400*K_PI)<1e-6,"spherical solid face extraction preserves exact area without invented axis");
    std::cout<<"Surface extraction, trim and hybrid tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
