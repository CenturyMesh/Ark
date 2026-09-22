#include "ark/agent.hpp"
#include "ark/compute.hpp"
#include <iostream>
using namespace ark;using V=arkjson::Value;
int failures=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';++failures;}}while(0)
V parse(const char* text){V j;CHECK(arkjson::parse(text,j));return j;}
bool bad(const V& r){return r.find("isError")->b;}
int main(int argc,char** argv){
    Feature tube;tube.type=FeatureType::Tube;tube.p={{"od","30"},{"wall","2"},{"tail","120"},{"chord_tol","0.02"}};
    auto straight=buildTube(tube,{});CHECK(straight.error.empty());CHECK(analyzeMesh(straight.mesh).closedOriented());
    CHECK(std::fabs(straight.centerlineLength-120)<1e-12);
    CHECK(std::fabs(analyzeMesh(straight.mesh).signedVolume/(straight.centerlineLength*straight.sectionArea)-1)<.005);
    tube.bends={{"100","60","90","0"}};auto bend=buildTube(tube,{});
    CHECK(bend.error.empty());CHECK(bend.frames.back().point.approx({160,180,0},1e-10));
    CHECK(bend.frames.back().tangent.approx({0,1,0},1e-10));
    CHECK(std::fabs(bend.centerlineLength-(220+30*K_PI))<1e-10);
    auto audit=analyzeMesh(bend.mesh);CHECK(audit.closedOriented());CHECK(audit.components==1);CHECK(audit.signedVolume>0);
    CHECK(std::fabs(audit.signedVolume/(bend.sectionArea*bend.centerlineLength)-1)<.006);
    tube.bends[0].angle="-90";auto negative=buildTube(tube,{});CHECK(negative.frames.back().point.approx({160,-180,0},1e-10));
    tube.bends[0].roll="90";tube.bends[0].angle="90";auto rolled=buildTube(tube,{});CHECK(rolled.frames.back().point.approx({160,0,180},1e-10));
    tube.p["section"]="rect";tube.p["width"]="40";tube.p["height"]="20";
    tube.bends.push_back({"100","80","90","90"});auto spatial=buildTube(tube,{});CHECK(spatial.error.empty());CHECK(analyzeMesh(spatial.mesh).closedOriented());
    for(auto& frame:spatial.frames){CHECK(std::fabs(frame.tangent.dot(frame.normal))<1e-10);CHECK(std::fabs(frame.normal.len()-1)<1e-10);}
    tube.bends[0].radius="10";CHECK(!buildTube(tube,{}).error.empty());tube.bends[0].radius="60";
    tube.p["wall"]="10";CHECK(!buildTube(tube,{}).error.empty());tube.p["wall"]="2";
    tube.bends[0].feed="-5";CHECK(!buildTube(tube,{}).error.empty());
    AgentSession s;
    CHECK(!bad(s.call("doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"box","name":"Shared beam","p":{"dx":100,"dy":20,"dz":30}},{"op":"assembly.add","name":"Frame","p":{"tx":10,"rz":90}},{"op":"assembly.add","name":"Beam A","parent":1,"feature":1,"p":{"tx":100}},{"op":"assembly.add","name":"Beam B","parent":1,"feature":1,"p":{"tx":200}}]})"))));
    std::vector<AssemblyPlacement> placements;std::string error;CHECK(resolveAssembly(s.document,placements,error));CHECK(placements.size()==2);
    CHECK(placements[0].world.transformPoint({0,0,0}).approx({10,100,0},1e-9));CHECK(placements[1].world.transformPoint({0,0,0}).approx({10,200,0},1e-9));
    auto info=s.call("assembly_info",V::makeObj());CHECK(info.find("structuredContent")->find("bom")->arr[0].find("quantity")->num==2);
    auto mesh=s.call("mesh_inspect",V::makeObj());CHECK(mesh.find("structuredContent")->find("triangles")->num==24);
    auto instance=s.call("mesh_inspect",parse(R"({"node":2})"));CHECK(instance.find("structuredContent")->find("triangles")->num==12);
    auto saved=documentToJson(s.document);Document replay;CHECK(documentFromJson(saved,replay,&error));CHECK(documentToJson(replay)==saved);
    CHECK(bad(s.call("doc_apply",parse(R"({"commands":[{"op":"assembly.edit","id":1,"parent":2}]})"))));CHECK(documentToJson(s.document)==saved);
    CHECK(bad(s.call("doc_apply",parse(R"({"commands":[{"op":"feature.remove","id":1}]})"))));CHECK(documentToJson(s.document)==saved);
    CHECK(bad(s.call("doc_apply",parse(R"({"commands":[{"op":"assembly.edit","id":2,"feature":999}]})"))));
    CHECK(!bad(s.call("doc_apply",parse(R"({"commands":[{"op":"assembly.edit","id":1,"visible":false}]})"))));
    CHECK(bad(s.call("mesh_inspect",V::makeObj())));CHECK(!bad(s.call("doc_undo",V::makeObj())));
    CHECK(!bad(s.call("doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"tube","p":{"od":30,"wall":2,"tail":120},"bends":[{"feed":100,"radius":60,"angle":90,"roll":0}]}]})"))));
    auto report=s.call("tube_inspect",parse(R"({"id":2})"));CHECK(!bad(report));CHECK(std::fabs(report.find("structuredContent")->find("centerline_length_mm")->num-(220+30*K_PI))<1e-9);
    saved=documentToJson(s.document);CHECK(documentFromJson(saved,replay,&error));replay.recompute();CHECK(replay.lastResults.back().ok);
    CHECK(replay.features.back().bends[0].feed=="100");
    CHECK(bad(s.call("doc_apply",parse(R"({"commands":[{"op":"feature.edit","id":2,"bends":[{"feed":100,"radius":3,"angle":90}]}]})"))));CHECK(documentToJson(s.document)==saved);
    std::vector<float> points(9003),cpu,gpu;for(size_t i=0;i<points.size();++i)points[i]=(float)(i*.031-200);
    Mat4 matrix=Mat4::translation({5,-8,9})*Mat4::rotZ(.27);
    CHECK(transformPacked(points,cpu,matrix,ComputeMode::CPU));
    for(size_t i=0;i<points.size();i+=3){auto p=matrix.transformPoint({points[i],points[i+1],points[i+2]});CHECK(std::fabs(cpu[i]-p.x)<.0001);}
    bool ran=transformPacked(points,gpu,matrix,ComputeMode::GPU);
    if(argc>1&&std::string(argv[1])=="--require-gpu")CHECK(ran);
    if(ran){CHECK(cpu.size()==gpu.size());double maxError=0;for(size_t i=0;i<cpu.size();++i)maxError=std::max(maxError,std::fabs((double)cpu[i]-gpu[i]));CHECK(maxError<.0001);std::cout<<"GPU parity max error: "<<maxError<<" mm\n";}
    else std::cout<<"GPU unavailable: CPU fallback verified\n";
    std::vector<float> empty,output;CHECK(transformPacked(empty,output,matrix,ComputeMode::CPU)&&output.empty());
    CHECK(!transformPacked(std::vector<float>{1},output,matrix,ComputeMode::CPU));
    std::cout<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;
}
