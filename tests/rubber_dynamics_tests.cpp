#include "ark/rubber_dynamics.hpp"
#include "ark/native_backend.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ark;
namespace {
int checks=0;
void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
void near(double a,double b,double tolerance,const char* text) {
    if(std::abs(a-b)>tolerance)std::cerr<<text<<": "<<a<<" versus "<<b<<", tolerance "<<tolerance<<'\n';
    check(std::abs(a-b)<=tolerance,text);
}
template<class F>void rejects(F fn,const char* expected) {
    try {fn();}catch(const std::exception& e){check(std::string(e.what()).find(expected)!=std::string::npos,expected);return;}
    check(false,expected);
}
RubberNode node(Vec3 p,bool fixed=false){RubberNode n;n.position=p;n.fixed=fixed;return n;}
RubberDynamicsOptions options(const std::string& backend) {
    RubberDynamicsOptions o;o.backend=backend;o.gravity={};o.ground=false;o.damping=0;o.duration=.1;return o;
}
void cases(const std::string& backend) {
    const double precision=backend=="gpu"?3e-4:1e-10;
    auto o=options(backend);o.duration=.5;o.gravity={0,0,-9.80665};
    auto fall=simulateRubber({node({0,0,2})},{},o);double steps=120,dt=o.step;
    near(fall.frames.back()[0].z,2-9.80665*dt*dt*steps*(steps+1)/2,precision,"semi-implicit free fall displacement");
    near(fall.finalVelocities[0].z,-9.80665*.5,precision,"free fall velocity");
    near(fall.times.back(),.5,1e-12,"actual final sample time");check(fall.frames.size()==16,"requested output frame count");

    o=options(backend);RubberNode anchor=node({.123456789,.234567891,.345678912},true);
    auto fixed=simulateRubber({anchor},{},o);check(fixed.frames.back()[0]==anchor.position,"fixed anchor preserves authored float64 coordinates");
    near(fixed.finalVelocities[0].len(),0,0,"fixed anchor velocity");
    auto prestress=simulateRubber({node({0,0,0},true),node({1.2,0,0},true)},{{0,1,1,.01}},o);
    near(prestress.finalLinkForces_N[0],20,precision*10,"two fixed endpoints retain elastic pretension");

    // The isolated linear spring has an independently computable implicit Euler
    // solution, so this checks the physical compliance rather than its code path.
    std::vector<RubberNode> spring{node({0,0,0},true),node({1.2,0,0})};
    std::vector<RubberLink> links{{0,1,1,.01}};
    auto elastic=simulateRubber(spring,links,o);double x=.2,v=0;
    for(int i=0;i<24;++i){v=(v-o.step*100*x)/(1+o.step*o.step*100);x+=o.step*v;}
    near(elastic.frames.back()[1].x,1+x,precision,"spring displacement matches implicit linear oscillator");
    near(elastic.finalVelocities[1].x,v,precision,"spring velocity matches implicit linear oscillator");
    near(elastic.finalLinkForces_N[0],100*x,precision*10,"XPBD tension agrees with linear spring constitutive force");
    check(elastic.frames.back()[1].x<1.2,"elastic stretch restores toward rest");
    o.iterations=1;auto single=simulateRubber(spring,links,o);
    near(single.frames.back()[1].x,elastic.frames.back()[1].x,precision,"isolated compliance does not change with solver iterations");
    links[0].compliance=.1;auto soft=simulateRubber(spring,links,o);
    check(soft.frames.back()[1].x>elastic.frames.back()[1].x,"higher compliance means lower stiffness");
    spring[1].position={.8,0,0};links[0].compliance=.01;
    auto compressed=simulateRubber(spring,links,o);
    check(compressed.frames.back()[1].x>.8&&compressed.finalLinkForces_N[0]<0,"compression restores length and reports a negative link force");

    o=options(backend);o.duration=2;o.damping=10;spring[1].position={1,0,0};spring[1].force={1,0,0};links[0].compliance=.01;
    auto equilibrium=simulateRubber(spring,links,o);o.step=1./120;auto coarse=simulateRubber(spring,links,o);
    near(equilibrium.frames.back()[1].x,1.01,.0001,"loaded spring approaches force/stiffness extension");
    near(coarse.frames.back()[1].x,equilibrium.frames.back()[1].x,.0001,"loaded spring remains comparable at half integration rate");
    near(equilibrium.finalLinkForces_N[0],1,.01,"loaded spring reports tension balancing the external force");
    near(equilibrium.diagnostics.find("final_max_tensile_link_force_N")->num,equilibrium.finalLinkForces_N[0],0,"tension diagnostics use final constraint estimate");

    // Disconnected springs plus a shared anchor reorder during colouring.
    // Different rest lengths make the original public link order observable.
    o=options(backend);o.duration=o.step;
    std::vector<RubberNode> ordered{node({0,0,0},true),node({1.1,0,0}),node({0,1.2,0}),node({0,0,1.3})};
    std::vector<RubberLink> orderedLinks{{0,1,1,.01},{0,2,1,.02},{1,3,1.5,.03},{0,3,1,.04}};
    auto forces=simulateRubber(ordered,orderedLinks,o);
    check(forces.finalLinkForces_N.size()==orderedLinks.size(),"force receipt preserves all input links");
    for(size_t i=0;i<orderedLinks.size();++i) {
        const auto& l=orderedLinks[i];double expected=((forces.frames.back()[l.a]-forces.frames.back()[l.b]).len()-l.restLength)/l.compliance;
        near(forces.finalLinkForces_N[i],expected,.02,"force receipt preserves original order after graph colouring");
    }

    o=options(backend);o.duration=.2;std::vector<RubberNode> pair{node({-.6,0,0}),node({.6,0,0})};pair[1].mass=3;
    pair[0].velocity={.2,.1,0};pair[1].velocity={-.1,.05,0};
    auto momentum=simulateRubber(pair,{{0,1,1,.005}},o);
    near((momentum.finalVelocities[0]+momentum.finalVelocities[1]*3-(pair[0].velocity+pair[1].velocity*3)).len(),0,precision*4,"internal elasticity conserves linear momentum");

    o=options(backend);o.ground=true;o.gravity={0,0,-9.80665};o.friction=.5;
    auto sliding=node({0,0,0});sliding.velocity={1,0,0};auto ground=simulateRubber({sliding},{},o);
    near(ground.frames.back()[0].z,0,precision,"ground projection prevents penetration");
    near(ground.finalVelocities[0].z,0,precision,"zero-restitution normal velocity");
    near(ground.finalVelocities[0].x,1-.5*9.80665*.1,precision,"Coulomb sliding impulse follows mu times normal load");
    o.friction=0;auto frictionless=simulateRubber({sliding},{},o);
    near(frictionless.finalVelocities[0].x,1,precision,"frictionless ground preserves tangent velocity");
    o.contactRadius=.02;sliding.position.z=.02;auto inflated=simulateRubber({sliding},{},o);
    near(inflated.frames.back()[0].z,.02,precision,"explicit contact radius offsets ground");

    o=options(backend);o.gravity={-9.80665,0,0};o.friction=.8;o.pulleys={{{0,0,0},{0,0,1},1,2}};
    auto belt=simulateRubber({node({1,0,0})},{},o);
    check(belt.finalVelocities[0].y>.3,"rotating pulley friction drives a contacting node");
    check(belt.frames.back()[0].len()>=1-precision,"pulley prevents radial penetration");
    o.pulleys[0].axis={0,1,0};auto turned=simulateRubber({node({1,0,0})},{},o);
    check(turned.finalVelocities[0].z<-.3,"pulley axis rotates the surface traction direction");
    o.friction=0;auto slip=simulateRubber({node({1,0,0})},{},o);
    near(slip.finalVelocities[0].z,0,precision,"rotating pulley has no traction when friction is zero");

    // Sample boundaries may introduce a final short integration step. They must
    // represent the requested times instead of labelling an overshot state.
    o=options(backend);o.duration=.107;o.outputFps=29;o.step=.006;
    auto timings=simulateRubber({node({})},{},o);
    near(timings.times.back(),.107,1e-12,"non-divisible duration uses actual final time");
    near(timings.times[1],1./29,1e-12,"non-divisible sample uses actual requested time");

    if(backend=="gpu") {
        o=options(backend);spring[1].force={};auto first=simulateRubber(spring,links,o),again=simulateRubber(spring,links,o);
        check(again.diagnostics.find("gpu_constraint_cache_hit")->b,"exact constraint cache hit");
        near(again.diagnostics.find("gpu_constraint_uploaded_bytes")->num,0,0,"cached constraints transfer zero bytes");
        links[0].compliance*=2;auto changed=simulateRubber(spring,links,o);
        check(!changed.diagnostics.find("gpu_constraint_cache_hit")->b,"material change invalidates constraint cache");
        check(changed.diagnostics.find("gpu_constraint_sha256")->str!=first.diagnostics.find("gpu_constraint_sha256")->str,"cache identity includes compliance");
        auto invalidLinks=links;invalidLinks[0].compliance=1e-100;
        rejects([&]{simulateRubber(spring,invalidLinks,o);},"underflows GPU");
        std::vector<RubberNode> tiny{node({999,0,0}),node({999.000001,0,0})};
        rejects([&]{simulateRubber(tiny,{{0,1,1e-6,.01}},o);},"precision is insufficient");
    }
    std::cout<<backend<<" pulley "<<belt.diagnostics.dump()<<'\n';
}
void networkParity(bool gpu) {
    constexpr unsigned width=16,height=16;std::vector<RubberNode> nodes;std::vector<RubberLink> links;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)nodes.push_back(node({.025*x,.025*y,.4},y==0));
    for(auto& n:nodes)n.mass=.01;
    auto link=[&](unsigned a,unsigned b){links.push_back({a,b,(nodes[a].position-nodes[b].position).len(),1e-4});};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        unsigned i=y*width+x;if(x+1<width)link(i,i+1);if(y+1<height)link(i,i+width);
        if(x+1<width&&y+1<height){link(i,i+width+1);link(i+1,i+width);}
    }
    auto o=options("cpu");o.gravity={0,0,-9.80665};o.duration=.2;o.damping=.1;o.ground=true;
    auto reference=simulateRubber(nodes,links,o);
    check(reference.frames.back().back().z<.4,"anchored shear-linked network deforms under gravity");
    check(reference.diagnostics.find("constraint_colors")->num>1,"connected network uses multiple independent constraint colors");
    std::cout<<"network cpu "<<reference.diagnostics.dump()<<'\n';
    if(!gpu)return;
    o.backend="gpu";auto compute=simulateRubber(nodes,links,o);double error=0;
    for(size_t f=0;f<reference.frames.size();++f)for(size_t i=0;i<nodes.size();++i)error=std::max(error,(reference.frames[f][i]-compute.frames[f][i]).len());
    near(error,0,3e-5,"coupled 256-node CPU/GPU sampled position agreement");
    for(unsigned x=0;x<width;++x)check(compute.frames.back()[x]==nodes[x].position,"network anchors remain exactly fixed");
    auto cached=simulateRubber(nodes,links,o);check(cached.diagnostics.find("gpu_constraint_cache_hit")->b,"coupled topology cache reused");
    double repeat=0;for(size_t f=0;f<compute.frames.size();++f)for(size_t i=0;i<nodes.size();++i)repeat=std::max(repeat,(compute.frames[f][i]-cached.frames[f][i]).len());
    near(repeat,0,0,"same-adapter GPU dispatch order is deterministic");
    std::cout<<"network gpu "<<compute.diagnostics.dump()<<'\n';std::cout<<"network gpu cached "<<cached.diagnostics.dump()<<" max CPU/GPU difference_m="<<error<<'\n';
}
void invalidInputs() {
    auto o=options("cpu");std::vector<RubberNode> nodes{node({0,0,0}),node({1,0,0})};std::vector<RubberLink> links{{0,1,1,.01}};
    rejects([&]{simulateRubber({},links,o);},"node count");
    auto bad=nodes;bad[0].mass=0;rejects([&]{simulateRubber(bad,links,o);},"mass");
    bad=nodes;bad[0].position.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{simulateRubber(bad,links,o);},"nonfinite");
    bad=nodes;bad[0].fixed=true;bad[0].velocity.x=1;rejects([&]{simulateRubber(bad,links,o);},"zero initial velocity");
    auto broken=links;broken[0].b=2;rejects([&]{simulateRubber(nodes,broken,o);},"endpoints");
    broken=links;broken[0].restLength=-1;rejects([&]{simulateRubber(nodes,broken,o);},"restLength");
    broken=links;broken[0].compliance=-1;rejects([&]{simulateRubber(nodes,broken,o);},"compliance");
    bad=nodes;bad[0].fixed=bad[1].fixed=true;broken=links;broken[0].restLength=.5;broken[0].compliance=0;
    rejects([&]{simulateRubber(bad,broken,o);},"contradict an inextensible");
    broken=links;broken.push_back({1,0,1,.01});rejects([&]{simulateRubber(nodes,broken,o);},"duplicate");
    bad=nodes;bad[1].position=bad[0].position;rejects([&]{simulateRubber(bad,links,o);},"coincident");
    auto invalid=o;invalid.backend="auto";rejects([&]{simulateRubber(nodes,links,invalid);},"no silent fallback");
    invalid=o;invalid.duration=15;invalid.step=15./14400;invalid.outputFps=119;rejects([&]{simulateRubber(nodes,links,invalid);},"sample-aligned");
    invalid=o;invalid.pulleys={{{},{0,0,2},1,0}};rejects([&]{simulateRubber(nodes,links,invalid);},"unit axis");
    invalid=o;invalid.contactRadius=-1;rejects([&]{simulateRubber(nodes,links,invalid);},"contactRadius");
    invalid=o;invalid.duration=1e-12;rejects([&]{simulateRubber(nodes,links,invalid);},"duration");
    bad.assign(8193,node({}));rejects([&]{simulateRubber(bad,{},o);},"node count");
    bad.clear();broken.clear();bad.push_back(node({}));
    for(unsigned i=1;i<=129;++i){bad.push_back(node({double(i),0,0}));broken.push_back({0,i,double(i),.01});}
    rejects([&]{simulateRubber(bad,broken,o);},"128 colours");
    {NativeRequestControlScope scope([]{return true;},30000);rejects([&]{simulateRubber(nodes,links,o);},"NATIVE_CANCELLED");}
    for(const auto* backend:{"cpu","gpu"}) {
        int polls=0;o.backend=backend;NativeRequestControlScope scope([&]{return ++polls>4;},30000);
        if(std::string(backend)=="cpu"||rubberDynamicsCapabilities().find("gpu_available")->b)
            rejects([&]{simulateRubber(nodes,links,o);},"NATIVE_CANCELLED");
    }
}
}
int main(int argc,char** argv) {
    try {
        invalidInputs();cases("cpu");auto cap=rubberDynamicsCapabilities();std::cout<<cap.dump()<<'\n';
        bool gpu=cap.find("gpu_available")->b,required=argc>1&&std::string(argv[1])=="--require-gpu";
        check(!required||gpu,"hardware GPU required");if(gpu)cases("gpu");networkParity(gpu);
        check(!cap.find("self_collision")->b&&!cap.find("qualified")->b,"capabilities disclose model limits");
        std::cout<<checks<<" rubber dynamics checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}
