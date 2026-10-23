#include "ark/rubber_dynamics.hpp"
#include "ark/native_backend.hpp"
#include "ark/iteration_history.hpp"
#include <array>
#include <chrono>
#include <limits>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#endif

namespace ark {
namespace rubber059c {
using V=arkjson::Value;
constexpr unsigned maxNodes=8192,maxLinks=65536,maxColors=128,maxSteps=14400,maxPulleys=8;
void require(bool ok,const std::string& reason) {
    if(!ok)throw std::runtime_error("RUBBER_SIMULATION: "+reason);
}
bool finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool bounded(Vec3 p,double limit) {return finite(p)&&std::abs(p.x)<=limit&&std::abs(p.y)<=limit&&std::abs(p.z)<=limit;}
double invMass(const RubberNode& n) {return n.fixed?0:1/n.mass;}
void cancel(const NativeRequestControl& c) {
    if(c.cancelled&&c.cancelled())throw std::runtime_error("NATIVE_CANCELLED: rubber simulation cancelled");
}
struct Link {RubberLink value;Vec3 initialDirection;size_t originalIndex=0;};
struct Topology {std::vector<Link> links;std::vector<unsigned> offsets;};
// Greedy edge colouring is stable in input order. Every writable endpoint occurs
// at most once per colour, so a GPU dispatch needs no float atomics or reductions.
Topology topology(const std::vector<RubberNode>& nodes,const std::vector<RubberLink>& links) {
    std::vector<std::array<uint64_t,2>> used(nodes.size());
    std::vector<std::vector<Link>> colors;
    for(size_t i=0;i<links.size();++i) {
        const auto& link=links[i];
        unsigned c=0;
        while(c<maxColors&&((used[link.a][c/64]|used[link.b][c/64])&(uint64_t(1)<<(c%64))))++c;
        require(c<maxColors,"constraint graph needs more than 128 colours; reduce node degree");
        used[link.a][c/64]|=uint64_t(1)<<(c%64);used[link.b][c/64]|=uint64_t(1)<<(c%64);
        if(c==colors.size())colors.emplace_back();
        colors[c].push_back({link,(nodes[link.a].position-nodes[link.b].position).normalized(),i});
    }
    Topology out;out.offsets.push_back(0);
    for(const auto& color:colors) {out.links.insert(out.links.end(),color.begin(),color.end());out.offsets.push_back((unsigned)out.links.size());}
    return out;
}
size_t stepCount(const RubberDynamicsOptions& o) {
    size_t count=0,samples=(size_t)std::ceil(o.duration*o.outputFps);double previous=0;
    for(size_t i=1;i<=samples;++i) {double t=std::min(o.duration,double(i)/o.outputFps);count+=(size_t)std::ceil((t-previous)/o.step-1e-10);previous=t;}
    return count;
}
void validate(const std::vector<RubberNode>& nodes,const std::vector<RubberLink>& links,const RubberDynamicsOptions& o) {
    require(!nodes.empty()&&nodes.size()<=maxNodes,"node count must be 1..8192");
    require(links.size()<=maxLinks,"link count exceeds 65536");
    require(o.backend=="cpu"||o.backend=="gpu","backend must be cpu or gpu; there is no silent fallback");
    require(std::isfinite(o.duration)&&o.duration>=1e-5&&o.duration<=60,"duration must be 1e-5..60 seconds");
    require(std::isfinite(o.step)&&o.step>=1e-5&&o.step<=1.0/30,"step must be 1e-5..1/30 seconds");
    require(o.iterations>=1&&o.iterations<=64,"iterations must be 1..64");
    require(o.outputFps>=1&&o.outputFps<=120,"outputFps must be 1..120");
    require(bounded(o.gravity,1e6),"gravity must be finite and bounded by 1e6 m/s^2");
    require(std::isfinite(o.groundZ)&&std::abs(o.groundZ)<=1000,"groundZ must be within +/-1000 m");
    require(std::isfinite(o.friction)&&o.friction>=0&&o.friction<=2,"friction must be 0..2");
    require(std::isfinite(o.damping)&&o.damping>=0&&o.damping<=1000,"damping must be 0..1000 s^-1");
    require(std::isfinite(o.contactRadius)&&o.contactRadius>=0&&o.contactRadius<=10,"contactRadius must be 0..10 m");
    require(o.pulleys.size()<=maxPulleys,"at most 8 analytic pulleys are supported");
    for(const auto& p:o.pulleys) {
        require(bounded(p.center,1000)&&finite(p.axis)&&std::abs(p.axis.len()-1)<1e-8,"pulley needs a finite centre and unit axis");
        require(std::isfinite(p.radius)&&p.radius>=1e-6&&p.radius<=1000,"pulley radius must be 1e-6..1000 m");
        require(std::isfinite(p.angularVelocity)&&std::abs(p.angularVelocity)<=10000,"pulley angularVelocity must be within +/-10000 rad/s");
    }
    for(const auto& n:nodes) {
        require(bounded(n.position,1000)&&bounded(n.velocity,10000)&&bounded(n.force,1e12),"node position, velocity or force is nonfinite/out of range");
        require(std::isfinite(n.mass)&&n.mass>=1e-9&&n.mass<=1e9,"every node needs mass in 1e-9..1e9 kg, including anchors");
        require(!n.fixed||n.velocity.lensq()==0,"fixed anchors require zero initial velocity");
    }
    std::set<std::pair<uint32_t,uint32_t>> unique;
    for(const auto& l:links) {
        require(l.a<nodes.size()&&l.b<nodes.size()&&l.a!=l.b,"link endpoints must be distinct valid node indices");
        require(std::isfinite(l.restLength)&&l.restLength>=1e-6&&l.restLength<=1000,"link restLength must be 1e-6..1000 m");
        require(std::isfinite(l.compliance)&&l.compliance>=0&&l.compliance<=1000,"link compliance must be 0..1000 m/N");
        require((nodes[l.a].position-nodes[l.b].position).len()>=1e-9,"initial link endpoints are coincident");
        require(unique.emplace(std::min(l.a,l.b),std::max(l.a,l.b)).second,"duplicate link endpoints are ambiguous; combine their stiffness explicitly");
    }
    const size_t steps=stepCount(o),frames=(size_t)std::ceil(o.duration*o.outputFps)+1;
    require(steps<=maxSteps,"sample-aligned integration exceeds 14400 steps; reduce duration or output rate");
    require(frames*nodes.size()<=4000000,"requested frame storage exceeds 4000000 node samples");
    require(double(steps)*(nodes.size()*(double(o.iterations)+1)+links.size()*double(o.iterations))<=2e9,"requested solver work exceeds 2e9 node/link visits");
}
Vec3 perpendicular(Vec3 axis) {
    return axis.cross(std::abs(axis.x)<.8?Vec3{1,0,0}:Vec3{0,1,0}).normalized();
}
Vec3 pulleyNormal(Vec3 p,const RubberPulley& pulley,double& radius) {
    Vec3 d=p-pulley.center;d-=pulley.axis*d.dot(pulley.axis);radius=d.len();
    return radius>1e-12?d/radius:perpendicular(pulley.axis);
}
struct Contact {double correction=0;};
void projectContacts(Vec3& p,const RubberDynamicsOptions& o,Contact* contact) {
    if(o.ground) {
        double depth=o.groundZ+o.contactRadius-p.z;
        if(depth>0) {p.z+=depth;contact[0].correction+=depth;}
    }
    for(size_t j=0;j<o.pulleys.size();++j) {
        double r;Vec3 n=pulleyNormal(p,o.pulleys[j],r);double depth=o.pulleys[j].radius+o.contactRadius-r;
        if(depth>0) {p+=n*depth;contact[j+1].correction+=depth;}
    }
}
void contactVelocity(Vec3& v,Vec3 normal,Vec3 surfaceVelocity,double correction,double dt,double friction) {
    Vec3 relative=v-surfaceVelocity;double vn=relative.dot(normal);
    // Zero restitution. The normal projection and any remaining inward normal
    // velocity supply the Coulomb impulse bound; no friction in free flight.
    double normalDelta=correction/dt+std::max(0.0,-vn);
    if(vn<0)relative-=normal*vn;
    Vec3 tangent=relative-normal*relative.dot(normal);double speed=tangent.len();
    if(speed>1e-12)relative-=tangent*(std::min(speed,friction*normalDelta)/speed);
    v=relative+surfaceVelocity;
}
void finishVelocity(Vec3& v,Vec3 p,const RubberDynamicsOptions& o,const Contact* contact,double dt) {
    if(o.ground&&p.z<=o.groundZ+o.contactRadius+1e-7)
        contactVelocity(v,{0,0,1},{},contact[0].correction,dt,o.friction);
    for(size_t j=0;j<o.pulleys.size();++j) {
        const auto& pulley=o.pulleys[j];double radius;Vec3 n=pulleyNormal(p,pulley,radius);
        if(radius<=pulley.radius+o.contactRadius+1e-7)
            contactVelocity(v,n,pulley.axis.cross(n)*(pulley.radius*pulley.angularVelocity),contact[j+1].correction,dt,o.friction);
    }
}
RubberDynamicsResult runCpu(const std::vector<RubberNode>& nodes,const Topology& graph,const RubberDynamicsOptions& o) {
    const auto started=std::chrono::steady_clock::now();const auto control=captureNativeRequestControl();
    std::vector<Vec3> positions,velocities,predicted;std::vector<double> weights;
    for(const auto& n:nodes) {positions.push_back(n.position);velocities.push_back(n.velocity);weights.push_back(invMass(n));}
    RubberDynamicsResult out;out.frames.push_back(positions);out.times.push_back(0);
    const size_t stride=o.pulleys.size()+1;std::vector<Contact> contacts(nodes.size()*stride);
    std::vector<double> lambda(graph.links.size());double time=0,lastDt=0;size_t steps=0,samples=(size_t)std::ceil(o.duration*o.outputFps);
    for(size_t sample=1;sample<=samples;++sample) {
        const double target=std::min(o.duration,double(sample)/o.outputFps);
        while(time<target-1e-12) {
            cancel(control);double dt=std::min(o.step,target-time),decay=std::exp(-o.damping*dt);
            std::fill(lambda.begin(),lambda.end(),0);std::fill(contacts.begin(),contacts.end(),Contact{});
            for(size_t i=0;i<nodes.size();++i)if(!nodes[i].fixed) {
                velocities[i]=velocities[i]*decay+(o.gravity+nodes[i].force*weights[i])*dt;
                positions[i]+=velocities[i]*dt;
            }
            predicted=positions;
            for(unsigned iteration=0;iteration<o.iterations;++iteration) {
                for(size_t j=0;j<graph.links.size();++j) {
                    const auto& link=graph.links[j];const auto& l=link.value;
                    double wa=weights[l.a],wb=weights[l.b];if(wa+wb==0)continue;
                    Vec3 d=positions[l.a]-positions[l.b];double length=d.len();
                    Vec3 n=length>1e-12?d/length:link.initialDirection;
                    double alpha=l.compliance/(dt*dt),dl=(-(length-l.restLength)-alpha*lambda[j])/(wa+wb+alpha);
                    lambda[j]+=dl;if(wa)positions[l.a]+=n*(wa*dl);if(wb)positions[l.b]-=n*(wb*dl);
                }
                for(size_t i=0;i<nodes.size();++i)if(!nodes[i].fixed)projectContacts(positions[i],o,contacts.data()+i*stride);
            }
            for(size_t i=0;i<nodes.size();++i)if(!nodes[i].fixed) {
                // Equivalent to (solved-previous)/dt, but preserves the
                // integrated velocity instead of subtracting nearby positions
                // in free flight (particularly important for GPU float32).
                velocities[i]+=(positions[i]-predicted[i])/dt;finishVelocity(velocities[i],positions[i],o,contacts.data()+i*stride,dt);
                require(finite(positions[i])&&finite(velocities[i]),"CPU solver became nonfinite; reduce step/check material and loads");
            }
            time+=dt;lastDt=dt;++steps;
        }
        out.frames.push_back(positions);out.times.push_back(time);
    }
    out.finalVelocities=std::move(velocities);out.diagnostics=V::makeObj();
    out.finalLinkForces_N.resize(graph.links.size());
    for(size_t j=0;j<graph.links.size();++j)out.finalLinkForces_N[graph.links[j].originalIndex]=-lambda[j]/(lastDt*lastDt);
    out.diagnostics.set("backend",V::makeStr("CPU reference / float64"));
    out.diagnostics.set("steps",V::makeNum(double(steps)));
    out.diagnostics.set("transfer_inclusive_ms",V::makeNum(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()));
    return out;
}
}

#include "rubber_gpu_059c.inl"

arkjson::Value rubberDynamicsCapabilities() {
    using namespace rubber059c;auto result=V::makeObj();result.set("gpu_available",V::makeBool(false));
#ifdef _WIN32
    auto& g=gpu();std::lock_guard lock(g.mutex);
    try {g.init();result.set("gpu_available",V::makeBool(true));result.set("adapter",V::makeStr(g.adapter));}
    catch(const std::exception& e) {result.set("reason",V::makeStr(e.what()));}
#else
    result.set("reason",V::makeStr("GPU backend requires Windows Direct3D11; select cpu explicitly for the portable reference solver"));
#endif
    result.set("model",V::makeStr("XPBD compliant distance-link network; uncalibrated reduced rubber approximation"));
    result.set("max_nodes",V::makeNum(maxNodes));result.set("max_links",V::makeNum(maxLinks));result.set("max_steps",V::makeNum(maxSteps));
    result.set("max_constraint_colors",V::makeNum(maxColors));result.set("max_pulleys",V::makeNum(maxPulleys));
    result.set("contacts",V::makeStr("discrete particle/plane and particle/infinite-cylinder; Coulomb friction; zero restitution"));
    result.set("volume_preservation",V::makeBool(false));result.set("self_collision",V::makeBool(false));
    result.set("continuous_collision_detection",V::makeBool(false));result.set("silent_fallback",V::makeBool(false));
    result.set("qualified",V::makeBool(false));return result;
}
RubberDynamicsResult simulateRubber(const std::vector<RubberNode>& nodes,const std::vector<RubberLink>& links,const RubberDynamicsOptions& options) {
    using namespace rubber059c;validate(nodes,links,options);cancel(captureNativeRequestControl());
    Topology graph=topology(nodes,links);RubberDynamicsResult out;
    if(options.backend=="gpu") {
#ifdef _WIN32
        out=runGpu(nodes,graph,options);
#else
        throw std::runtime_error("RUBBER_SIMULATION: GPU unavailable on this platform; select cpu explicitly");
#endif
    } else out=runCpu(nodes,graph,options);
    double maxStretch=0,maxGroundPenetration=0,maxPulleyPenetration=0,maxTension=0,maxCompression=0;
    for(double force:out.finalLinkForces_N) {
        require(std::isfinite(force),"nonfinite link force estimate");maxTension=std::max(maxTension,force);maxCompression=std::max(maxCompression,-force);
    }
    for(const auto& frame:out.frames) {
        for(const auto& l:links)maxStretch=std::max(maxStretch,std::abs((frame[l.a]-frame[l.b]).len()/l.restLength-1));
        for(size_t i=0;i<frame.size();++i)if(!nodes[i].fixed) {
            if(options.ground)maxGroundPenetration=std::max(maxGroundPenetration,options.groundZ+options.contactRadius-frame[i].z);
            for(const auto& p:options.pulleys) {double r;pulleyNormal(frame[i],p,r);maxPulleyPenetration=std::max(maxPulleyPenetration,p.radius+options.contactRadius-r);}
        }
    }
    auto energy=[&](const std::vector<Vec3>& positions,const std::vector<Vec3>& velocities) {
        double total=0;for(size_t i=0;i<nodes.size();++i)if(!nodes[i].fixed)total+=.5*nodes[i].mass*velocities[i].lensq()-nodes[i].mass*options.gravity.dot(positions[i]);
        for(const auto& l:links)if(l.compliance>0) {double c=(positions[l.a]-positions[l.b]).len()-l.restLength;total+=.5*c*c/l.compliance;}
        return total;
    };
    std::vector<Vec3> initialVelocities;for(const auto& n:nodes)initialVelocities.push_back(n.velocity);
    double initialEnergy=energy(out.frames.front(),initialVelocities),finalEnergy=energy(out.frames.back(),out.finalVelocities);
    require(std::isfinite(initialEnergy)&&std::isfinite(finalEnergy)&&std::isfinite(maxStretch),"nonfinite diagnostics; reduce loads/check material");
    auto& d=out.diagnostics;d.set("model",V::makeStr("XPBD compliant distance-link network"));
    d.set("constraint_colors",V::makeNum(double(graph.offsets.size()-1)));d.set("node_count",V::makeNum(double(nodes.size())));d.set("link_count",V::makeNum(double(links.size())));
    d.set("initial_energy_J",V::makeNum(initialEnergy));d.set("final_energy_J",V::makeNum(finalEnergy));
    d.set("energy_scope",V::makeStr("kinetic + compliant link strain + gravity; excludes work of constant forces and moving pulleys"));
    d.set("max_sampled_relative_link_strain",V::makeNum(maxStretch));
    d.set("final_max_tensile_link_force_N",V::makeNum(maxTension));d.set("final_max_compressive_link_force_N",V::makeNum(maxCompression));
    d.set("link_force_model",V::makeStr("last-step XPBD -lambda/dt^2 in input link order; reduced-network forces, not continuum stress"));
    d.set("max_sampled_ground_penetration_m",V::makeNum(maxGroundPenetration));
    d.set("max_sampled_pulley_penetration_m",V::makeNum(maxPulleyPenetration));
    d.set("geometry_modified",V::makeBool(false));d.set("qualified",V::makeBool(false));d.set("volume_preservation",V::makeBool(false));
    d.set("self_collision",V::makeBool(false));d.set("continuous_collision_detection",V::makeBool(false));
    d.set("frame_storage_bytes",V::makeNum(double(out.frames.size()*nodes.size()*sizeof(Vec3))));
    return out;
}
}
