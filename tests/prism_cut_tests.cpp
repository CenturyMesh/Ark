#include "ark/prism_cut.hpp"
#include "ark/sketch.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/mesh_query.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ark;
namespace {
int checks=0;
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
template<class F>void rejected(F action,const char* message){bool threw=false;try{action();}catch(const std::runtime_error&){threw=true;}check(threw,message);}
SketchProfile polygon(std::vector<Vec2> points,Mat4 frame=Mat4::identity()){
    SketchProfile p;p.frame=frame;for(size_t i=0;i<points.size();++i)p.segs.push_back({ProfileSeg::Line,points[i],points[(i+1)%points.size()]});return p;
}
SketchProfile rectangle(double x,double y,double w,double h,Mat4 frame=Mat4::identity()){
    return polygon({{x,y},{x+w,y},{x+w,y+h},{x,y+h}},frame);
}
SketchProfile circle(double x,double y,double r,Mat4 frame){
    Sketch sketch;SketchEnt e;e.kind=SketchEnt::Circle;e.dof={x,y,r};sketch.ents.push_back(e);sketch.tolerance=.01;return sketchProfiles(sketch,frame).front();
}
double polygonArea(const SketchProfile& p){double result=0;for(auto& seg:p.segs)result+=seg.p0.cross(seg.p1);return std::abs(result)*.5;}
void verify(const Body& body,double expected){
    check(body.kind=="prism_cut"&&bool(body.baked),"cut records prism metadata and mesh");
    auto report=analyzeMesh(*body.baked);check(report.closedOriented()&&report.components==1,"cut is one closed oriented indexed mesh");
    check(std::abs(report.signedVolume-expected)<=std::max(1e-5,expected*2e-6),"independent area-times-height volume");
}
bool hit(const Body& body,Vec2 p){auto& mesh=*body.baked;MeshBVH bvh(mesh);auto frame=body.profile.frame;return bvh.raycast(mesh,{frame.transformPoint({p.x,p.y,40}),frame.transformDir({0,0,-1})}).hit;}
}
int main(){try{
    auto frame=Mat4::translation({23,-17,41})*Mat4::rotZ(.37)*Mat4::rotX(.62);
    auto outer=rectangle(-50,-30,100,60,frame);auto base=extrudeProfile(outer,6);
    auto cutFrame=frame*Mat4::translation({0,0,-1});auto a=circle(-30,0,4,cutFrame),b=circle(30,0,4,cutFrame),slot=rectangle(-10,-4,20,8,cutFrame);
    Body first,second,third;check(tryPrismaticCut(base,{a},8,first),"first hole applies");
    check(tryPrismaticCut(first,{b},8,second),"repeated hole applies");check(tryPrismaticCut(second,{slot},8,third),"third hole applies");
    double expected=(6000-polygonArea(a)-polygonArea(b)-160)*6;verify(third,expected);check(third.prismHoles.size()==3,"all prior openings retained");
    check(!hit(third,{-30,0})&&!hit(third,{30,0})&&!hit(third,{0,0}),"rays pass through all three cuts");check(hit(third,{0,15})&&hit(third,{-45,0}),"rays retain material between cuts");
    Body simultaneous;check(tryPrismaticCut(base,{a,b,slot},8,simultaneous),"batch holes apply");verify(simultaneous,expected);
    check(std::abs(analyzeMesh(*simultaneous.baked).signedVolume-analyzeMesh(*third.baked).signedVolume)<1e-8,"sequential and batched cuts agree");
    auto reverseFrame=frame*Mat4::translation({0,0,7})*Mat4::rotX(K_PI);auto reverse=rectangle(-3,-3,6,6,reverseFrame);Body reversed;
    check(tryPrismaticCut(base,{reverse},8,reversed),"antiparallel cutter works");verify(reversed,(6000-36)*6);check(!hit(reversed,{0,0}),"reverse cutter creates through opening");
    auto angled=rectangle(-3,-3,6,6,cutFrame*Mat4::rotZ(K_PI/4));Body diagonal;
    check(tryPrismaticCut(base,{angled},8,diagonal),"in-plane rotated cutter works");verify(diagonal,(6000-36)*6);
    auto movedFrame=Mat4::translation({-63,24,18})*Mat4::rotY(.4);auto moved=transformBody(second,movedFrame);auto movedSlot=slot;movedSlot.frame=movedFrame*slot.frame;Body movedCut;
    check(tryPrismaticCut(moved,{movedSlot},8,movedCut),"rigid transform preserves repeated cut capability");verify(movedCut,expected);check(!hit(movedCut,{0,0})&&!hit(movedCut,{-30,0}),"transformed cut preserves holes");
    Body sentinel;sentinel.kind="unchanged";
    auto pocket=slot;pocket.frame=frame*Mat4::translation({0,0,1});check(!tryPrismaticCut(base,{pocket},3,sentinel),"blind pocket outside specialization");check(sentinel.kind=="unchanged","unsupported operation leaves output unchanged");
    auto slanted=slot;slanted.frame=cutFrame*Mat4::rotX(.1);check(!tryPrismaticCut(base,{slanted},8,sentinel),"oblique cut outside specialization");
    auto box=makeBox({20,20,20});check(!tryPrismaticCut(box,{slot},8,sentinel),"non-extrusion outside specialization");
    auto outside=rectangle(80,0,5,5,cutFrame);rejected([&]{tryPrismaticCut(base,{outside},8,sentinel);},"outside opening rejected");
    auto touching=rectangle(45,-3,5,6,cutFrame);rejected([&]{tryPrismaticCut(base,{touching},8,sentinel);},"boundary touching opening rejected");
    auto crossing=rectangle(48,-3,5,6,cutFrame);rejected([&]{tryPrismaticCut(base,{crossing},8,sentinel);},"crossing boundary opening rejected");
    auto enclosing=rectangle(-60,-40,120,80,cutFrame);rejected([&]{tryPrismaticCut(base,{enclosing},8,sentinel);},"enclosing stock rejected");
    auto nested=rectangle(-1,-1,2,2,cutFrame);rejected([&]{tryPrismaticCut(base,{slot,nested},8,sentinel);},"nested openings rejected");
    rejected([&]{tryPrismaticCut(first,{a},8,sentinel);},"duplicate existing opening rejected");
    auto bow=polygon({{-4,-4},{4,4},{-4,4},{4,-4}},cutFrame);rejected([&]{tryPrismaticCut(base,{bow},8,sentinel);},"self-intersection rejected");check(sentinel.kind=="unchanged","failed validation leaves output unchanged");
    // Organic contour uses independent dimensions and three differently shaped holes.
    Sketch organic;SketchEnt skin;skin.kind=SketchEnt::Spline;skin.closed=true;skin.dof={-60,0,-48,-20.3,-12,-29,36,-26.1,60,0,42,26.1,0,19.14,-42,27.84};organic.ents.push_back(skin);organic.tolerance=.025;
    auto organicProfile=sketchProfiles(organic,frame).front();auto organicBase=extrudeProfile(organicProfile,6);Body organicCut;
    check(tryPrismaticCut(organicBase,{a,b,slot},8,organicCut),"organic stock accepts mixed through holes");verify(organicCut,(polygonArea(organicProfile)-polygonArea(a)-polygonArea(b)-160)*6);
    check(!hit(organicCut,{-30,0})&&!hit(organicCut,{30,0})&&!hit(organicCut,{0,0})&&hit(organicCut,{0,12}),"organic holes and retained web verified by rays");
    Sketch flow;SketchEnt flowCurve;flowCurve.kind=SketchEnt::Spline;flowCurve.closed=true;flowCurve.dof={-12,-4,9,-6,16,0,9,6,-12,4,-17,0};flow.ents.push_back(flowCurve);flow.tolerance=.025;
    auto flowProfile=sketchProfiles(flow,cutFrame).front();auto mountA=circle(-46,0,4,cutFrame),mountB=circle(46,0,4,cutFrame);Body example;
    check(tryPrismaticCut(organicBase,{mountA,mountB,flowProfile},8,example),"freeform plate example with organic slot and two mounting holes");
    verify(example,(polygonArea(organicProfile)-polygonArea(mountA)-polygonArea(mountB)-polygonArea(flowProfile))*6);
    check(!hit(example,{-46,0})&&!hit(example,{46,0})&&!hit(example,{0,0})&&hit(example,{0,12}),"example rays verify three cutouts and web");
    for(bool reverseWinding:{false,true}){
        auto pts=std::vector<Vec2>{{0,0},{10,0},{10,10},{5,5},{0,10}};if(reverseWinding)std::reverse(pts.begin(),pts.end());
        auto profile=polygon(pts);auto concave=extrudeProfile(profile,2);auto hole=rectangle(4,1,2,2,Mat4::translation({0,0,-1}));Body result;
        check(tryPrismaticCut(concave,{hole},4,result),"concave profile winding accepted");verify(result,(75-4)*2);
    }
    std::cout<<checks<<" prismatic cut checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
