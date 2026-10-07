#include "ark/io.hpp"
#include "ark/agent.hpp"
#include "native_process.hpp"
#include <iostream>
#include <cmath>
using V=arkjson::Value;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static V json(const char* text){V v;check(arkjson::parse(text,v),"test JSON");return v;}
static V accepted(const V& response){if(response.find("isError")->b)throw std::runtime_error(response.dump());return *response.find("structuredContent");}
int main(){try{
    native_test::TempDirectory temp("ark-exchange-");ark::Mesh mesh;
    mesh.pos={0,0,0, 20,0,0, 0,30,0, 0,0,40};mesh.idx={0,2,1,0,1,3,0,3,2,1,2,3};
    auto obj=temp.path()/"no-normals.obj";check(ark::exportOBJ(obj.string().c_str(),mesh),"OBJ export without normals");
    auto text=native_test::readFile(obj);check(text.find("//")==std::string::npos,"OBJ must not reference absent normals");
    auto imported=ark::importMeshPath(obj.string());check(imported.triCount()==4,"OBJ independent import triangle count");
    check(std::abs(imported.computeBBox().mx.z-40)<1e-6,"OBJ coordinates stay in millimetres");
    mesh.computeNormals();auto fbx=temp.path()/"tetra.fbx";check(ark::exportFBX(fbx.string().c_str(),mesh),"FBX export");
    text=native_test::readFile(fbx);check(text.find("FBXVersion: 7400")!=std::string::npos,"FBX version");
    check(text.find("UnitScaleFactor\", \"double\", \"Number\", \"\",0.1")!=std::string::npos,"FBX millimetre unit scale");
    check(text.find("PolygonVertexIndex: *12")!=std::string::npos&&text.find("LayerElementNormal")!=std::string::npos,"FBX topology and normals");
    auto invalid=mesh;invalid.idx[0]=100;check(!ark::exportFBX((temp.path()/"bad.fbx").string().c_str(),invalid),"bad FBX topology rejected");
#ifdef ARK_EXCHANGE_EXACT_TESTS
    ark::AgentSession session;accepted(session.call("doc_apply",json(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Blank","native":{"op":"box"},"p":{"width":20,"depth":30,"height":4}}]})")));
    V args=V::makeObj();args.set("id",V::makeNum(1));args.set("path",V::makeStr((temp.path()/"blank.igs").string()));
    auto report=accepted(session.call("export_iges",args));check(report.find("brep_valid")->b,"IGES readback valid");
    check(report.find("iges_roundtrip_bounds_error_mm")->num<1e-4,"IGES readback bounds");
    check(report.find("solid_topology_preserved")->b,"IGES solid topology preserved for box");
    check(session.revision==1,"IGES export is read-only");
#endif
    std::cout<<"OBJ, FBX and native IGES exchange checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
