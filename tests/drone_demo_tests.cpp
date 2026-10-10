#include "ark/agent.hpp"
#include "ark/references.hpp"
#include "../src/occt/common.hpp"
#include <BRepClass3d_SolidClassifier.hxx>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
using namespace ark::occt;
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static V parse(const std::string& s){V v;check(arkjson::parse(s,v),"invalid demo input");return v;}
static V accepted(const V& r){if(r.find("isError")->b)throw std::runtime_error(r.find("structuredContent")->dump());return *r.find("structuredContent");}
static const V& body(ark::Document& d,ark::u64 id){for(auto& b:d.evalBodies)if(b.id==id)return b.nativeData;throw std::runtime_error("Missing demo body");}
static void checkOpenings(ark::Document& d,double pitch){
    const auto hull=readBrep(body(d,12).find("brep_base64")->str),floor=readBrep(body(d,2).find("brep_base64")->str);auto box=bounds(floor);double z=(box[2]+box[5])/2;
    auto state=[&](double x,double y){BRepClass3d_SolidClassifier c(hull,gp_Pnt(x,y,z),1e-7);return c.State();};
    for(double x:{-pitch/2,pitch/2})for(double y:{-39.2,39.2}){check(state(x,y)==TopAbs_OUT,"Real electronics hole missing");check(state(x+3,y)==TopAbs_IN,"Material beside electronics hole missing");}
    for(double y:{-15.,15.})check(state(0,y)==TopAbs_OUT,"Real battery strap slot missing");check(state(15,5)==TopAbs_IN,"Uncut floor material missing");
}
int main(int argc,char** argv){try{
    check(argc==2,"Expected drone .arkops path");auto command=parse(readFile(fs::u8path(argv[1])));ark::AgentSession session;accepted(session.call("doc_apply",*command.find("arguments")));
    check(session.document.features.size()==25&&session.document.assembly.size()==11,"Drone parametric history and assembly");
    for(auto& r:session.document.lastResults)check(r.ok,"Drone feature failed");checkOpenings(session.document,50);
    auto first=accepted(session.call("assembly_info",V::makeObj()));check(first.find("diagnostics")->find("remaining_dofs")->num==4,"Only motor axial rotations should remain free");
    auto axis=ark::resolveGeometryReference(session.document,23,"tool.22.axis");check(std::abs(axis.frame.m[10]-1)<1e-10,"Motor shaft semantic axis");
    const auto saved=ark::documentToJson(session.document,true,false);const double before=body(session.document,12).find("report")->find("volume_mm3")->num;
    accepted(session.call("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"body_width","expr":"90"},{"op":"param.set","name":"mount_pitch","expr":"60"},{"op":"param.set","name":"motor_x","expr":"100"}]})")));
    checkOpenings(session.document,60);check(body(session.document,12).find("report")->find("volume_mm3")->num>before,"Wider hull regenerates actual material");
    auto next=accepted(session.call("assembly_info",V::makeObj()));check(next.find("diagnostics")->find("max_position_residual_mm")->num<1e-5,"Mates survive parameter regeneration");
    accepted(session.call("doc_undo",V::makeObj()));check(ark::documentToJson(session.document,true,false)==saved,"Undo returns exact authored demo intent");
    std::cout<<"Drone replay, exact holes/slots, semantic shaft references, assembly DOFs and dimensional regeneration passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
