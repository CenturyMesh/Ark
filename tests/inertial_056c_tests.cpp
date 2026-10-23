#include "ark/inertial_simulation.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace ark;
static int checks=0;
static void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
static void near(double a,double b,double tol,const char* text){if(std::abs(a-b)>tol)std::cerr<<text<<": "<<a<<" vs "<<b<<" tolerance "<<tol<<'\n';check(std::abs(a-b)<=tol,text);}
template<class F>static void rejects(F f,const char* text){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,text);}
static Mesh cube(){Mesh m;for(auto p:{Vec3{-50,-50,-50},Vec3{50,-50,-50},Vec3{50,50,-50},Vec3{-50,50,-50},Vec3{-50,-50,50},Vec3{50,-50,50},Vec3{50,50,50},Vec3{-50,50,50}})m.addVertex(p);m.idx={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7};return m;}
static InertialBody body(){InertialBody b;b.mass=2;b.inertia={1./300,0,0,0,1./300,0,0,0,1./300};return b;}
static void cases(const std::string& backend){
    auto shape=prepareInertialShape(cube(),{});check(shape.vertices.size()==8&&shape.normals.size()==3&&shape.edges.size()==3,"compact convex topology");
    InertialOptions o;o.backend=backend;o.duration=.5;o.outputFps=10;o.step=1./240;o.contacts=false;auto a=body();a.position={0,0,2};auto fall=simulateInertial({shape},{a},{},o);auto end=fall.frames.back()[0];near(end.position.z,2-.5*9.80665*.25,2e-5,"analytic free fall");near(end.velocity.z,-9.80665*.5,2e-5,"gravity velocity");
    o.gravity={};a.position={};a.force={4,0,0};auto force=simulateInertial({shape},{a},{},o);near(force.frames.back()[0].position.x,.25,1e-5,"F=ma displacement");near(force.frames.back()[0].velocity.x,1,1e-5,"F=ma velocity");
    a.force={};a.torque={0,0,.01};auto torque=simulateInertial({shape},{a},{},o);near(torque.frames.back()[0].omega.z,1.5,2e-5,"torque over inertia");auto pose=inertialDisplayPose(torque.frames.back()[0]);near(pose.m[0],std::cos(.375),2e-5,"integrated rigid orientation");
    a.fixed=true;auto fixed=simulateInertial({shape},{a},{},o);near(fixed.frames.back()[0].position.len(),0,0,"fixed position");near(fixed.frames.back()[0].omega.len(),0,0,"fixed orientation");
    a=body();auto b=body();a.position={-.099,0,0};b.position={.099,0,0};a.velocity={1,0,0};b.velocity={-1,0,0};o.contacts=true;o.restitution=1;o.friction=0;o.duration=.2;auto collision=simulateInertial({shape},{a,b},{},o);const auto& pair=collision.frames.back();near(pair[0].velocity.x+pair[1].velocity.x,0,2e-4,"collision momentum");near(pair[0].velocity.x,-1,2e-3,"elastic collision restitution");near(pair[1].velocity.x,1,2e-3,"opposite impulse");
    a=body();b=body();o.duration=.025;auto coincident=simulateInertial({shape},{a,b},{},o);const auto& overlapping=coincident.frames.back();near((overlapping[0].velocity+overlapping[1].velocity).len(),0,2e-4,"coincident pair conserves momentum");
    o.contacts=false;o.ground=true;o.groundZ=0;o.gravity={0,0,-9.80665};o.restitution=0;o.friction=.6;o.duration=.7;a=body();a.position={0,0,.12};a.velocity={.2,0,0};auto ground=simulateInertial({shape},{a},{},o);const auto& resting=ground.frames.back()[0];check(resting.position.z>.049&&resting.position.z<.052,"ground support within 1mm");check(std::abs(resting.velocity.x)<.15,"Coulomb friction reduces sliding");
    std::cout<<backend<<" "<<ground.diagnostics.dump()<<'\n';
    o.ground=false;o.gravity={};o.contacts=false;o.duration=.1;a=body();b=body();a.fixed=true;b.position={.1,0,0};b.force={0,1,0};InertialJoint hinge;hinge.a=0;hinge.b=1;hinge.anchorA={.05,0,0};hinge.anchorB={-.05,0,0};hinge.axisA=hinge.axisB={0,0,1};hinge.hinge=true;auto joint=simulateInertial({shape},{a,b},{hinge},o);check(joint.diagnostics.find("max_sampled_joint_drift_mm")->num<.3,"hinge anchor closure");
    if(backend=="gpu"){auto repeat=simulateInertial({shape},{a,b},{hinge},o);check(repeat.diagnostics.find("gpu_mesh_cache_hit")->b&&repeat.diagnostics.find("gpu_mesh_uploaded_bytes")->num==0,"GPU geometry cached between runs");}
}
int main(int argc,char** argv){try{
    auto bad=cube();bad.idx.pop_back();rejects([&]{prepareInertialShape(bad,{});},"malformed triangles reject");bad=cube();bad.flip();rejects([&]{prepareInertialShape(bad,{});},"inverted mesh rejects");bad=cube();bad.pos[0]=30;bad.pos[1]=30;bad.pos[2]=30;rejects([&]{prepareInertialShape(bad,{});},"concave mesh rejects without hull fallback");
    auto shape=prepareInertialShape(cube(),{});auto b=body();InertialOptions o;o.backend="cpu";b.mass=0;rejects([&]{simulateInertial({shape},{b},{},o);},"zero mass rejects");b=body();b.inertia[0]=-1;rejects([&]{simulateInertial({shape},{b},{},o);},"indefinite inertia rejects");
    b=body();b.orientation={0,0,0,2};rejects([&]{simulateInertial({shape},{b},{},o);},"nonunit orientation rejects");b=body();o.duration=15;o.step=15./14400;o.outputFps=119;rejects([&]{simulateInertial({shape},{b},{},o);},"actual frame-aligned step capacity");
    AgentSession s;arkjson::Value args;arkjson::parse(R"({"simulation":{"duration_s":1},"expected_revision":999})",args);auto r=s.call("physics_inspect",args);check(r.find("isError")->b&&s.revision==0,"stale source rejects without mutation");
    cases("cpu");auto cap=inertialCapabilities();std::cout<<cap.dump()<<'\n';bool required=argc>1&&std::string(argv[1])=="--require-gpu";check(!required||cap.find("gpu_available")->b,"hardware GPU required");if(cap.find("gpu_available")->b)cases("gpu");std::cout<<checks<<" inertial checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
