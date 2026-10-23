#include "ark/inertial_simulation.hpp"
#include "ark/physics.hpp"
#include "ark/history_authoring.hpp"
#include "ark/iteration_history.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/geometry_storage_032d.hpp"
#include <map>
#include <set>
#include <chrono>
#include <stdexcept>
#include <numeric>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#endif

namespace ark {
namespace inertial {
using V=arkjson::Value;
using Tensor=std::array<double,9>;
using Quat=std::array<double,4>;
void require(bool test,const std::string& reason){if(!test)throw std::runtime_error("SIMULATION: "+reason);}
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Vec3 xyz(const V& v){require(v.t==V::Arr&&v.arr.size()==3,"expected xyz vector");for(auto& n:v.arr)require(n.t==V::Num&&std::isfinite(n.num),"nonfinite vector");return {v.arr[0].num,v.arr[1].num,v.arr[2].num};}
double num(const V& v,const char* key,double fallback){auto p=v.find(key);if(!p)return fallback;require(p->t==V::Num&&std::isfinite(p->num),std::string(key)+" must be finite");return p->num;}
bool flag(const V& v,const char* key,bool fallback){auto p=v.find(key);if(!p)return fallback;require(p->t==V::Bool,std::string(key)+" must be boolean");return p->b;}
V jvec(Vec3 v){V r=V::makeArr();for(double n:{v.x,v.y,v.z})r.arr.push_back(V::makeNum(n));return r;}
void fields(const V& v,std::initializer_list<const char*> names){require(v.t==V::Obj,"expected an object");for(auto& field:v.obj)require(std::any_of(names.begin(),names.end(),[&](const char* n){return field.first==n;}),"unsupported field: "+field.first);}
Vec3 rotate(Quat q,Vec3 v){Vec3 u{q[0],q[1],q[2]};return v+2*u.cross(u.cross(v)+q[3]*v);}
Quat conjugate(Quat q){return {-q[0],-q[1],-q[2],q[3]};}
Quat multiply(Quat a,Quat b){Vec3 av{a[0],a[1],a[2]},bv{b[0],b[1],b[2]},v=a[3]*bv+b[3]*av+av.cross(bv);return {v.x,v.y,v.z,a[3]*b[3]-av.dot(bv)};}
Quat normalized(Quat q){double n=std::sqrt(std::inner_product(q.begin(),q.end(),q.begin(),0.));require(n>1e-15&&std::isfinite(n),"invalid orientation");for(auto& v:q)v/=n;return q;}
Quat rotationStep(Vec3 omega,double dt){double angle=omega.len()*dt;if(angle<1e-12)return {0,0,0,1};Vec3 v=omega.normalized()*std::sin(angle*.5);return {v.x,v.y,v.z,std::cos(angle*.5)};}
Vec3 product(const Tensor& t,Vec3 p){return {t[0]*p.x+t[1]*p.y+t[2]*p.z,t[3]*p.x+t[4]*p.y+t[5]*p.z,t[6]*p.x+t[7]*p.y+t[8]*p.z};}
Tensor inverse(const Tensor& a){
    for(double n:a)require(std::isfinite(n),"nonfinite inertia");
    double scale=std::max({std::abs(a[0]),std::abs(a[4]),std::abs(a[8])});
    require(scale>0,"zero inertia");for(int i=0;i<3;++i)for(int j=0;j<3;++j)require(std::abs(a[i*3+j]-a[j*3+i])<=scale*1e-8,"inertia must be symmetric");
    double det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    require(a[0]>0&&a[0]*a[4]-a[1]*a[3]>0&&det>scale*scale*scale*1e-12,"inertia must be positive definite and well-conditioned");
    return {(a[4]*a[8]-a[5]*a[7])/det,(a[2]*a[7]-a[1]*a[8])/det,(a[1]*a[5]-a[2]*a[4])/det,(a[5]*a[6]-a[3]*a[8])/det,(a[0]*a[8]-a[2]*a[6])/det,(a[2]*a[3]-a[0]*a[5])/det,(a[3]*a[7]-a[4]*a[6])/det,(a[1]*a[6]-a[0]*a[7])/det,(a[0]*a[4]-a[1]*a[3])/det};
}
Vec3 inertiaWorld(const InertialBody& b,Vec3 v,bool invert){return b.fixed?Vec3{}:rotate(b.orientation,product(invert?inverse(b.inertia):b.inertia,rotate(conjugate(b.orientation),v)));}
double invMass(const InertialBody& b){return b.fixed?0:1/b.mass;}
Vec3 point(const InertialBody& b,Vec3 v){return b.position+rotate(b.orientation,v);}
double effective(const InertialBody& b,Vec3 r,Vec3 n){return invMass(b)+n.dot(inertiaWorld(b,r.cross(n),true).cross(r));}
struct Contact {int a=0,b=-1;Vec3 normal,point;double depth=0,target=0;};
std::pair<double,double> project(const InertialShape& s,const InertialBody& b,Vec3 n){double lo=INFINITY,hi=-INFINITY;for(auto p:s.vertices){double d=point(b,p).dot(n);lo=std::min(lo,d);hi=std::max(hi,d);}return {lo,hi};}
Vec3 support(const InertialShape& s,const InertialBody& b,Vec3 n){double hi=project(s,b,n).second;Vec3 sum;unsigned count=0;for(auto p:s.vertices){auto w=point(b,p);if(hi-w.dot(n)<1e-6){sum+=w;++count;}}return sum/(double)count;}
bool clippedSegment(const InertialShape& s,const InertialBody& b,Vec3 p,Vec3 q,Vec3& loPoint,Vec3& hiPoint){
    double lo=0,hi=1;Vec3 delta=q-p;
    for(auto local:s.normals){auto n=rotate(b.orientation,local);auto bounds=project(s,b,n);double start=p.dot(n),slope=delta.dot(n);if(std::abs(slope)<1e-12){if(start<bounds.first-1e-7||start>bounds.second+1e-7)return false;continue;}double x=(bounds.first-start)/slope,y=(bounds.second-start)/slope;if(x>y)std::swap(x,y);lo=std::max(lo,x);hi=std::min(hi,y);if(lo>hi)return false;}
    loPoint=p+delta*lo;hiPoint=p+delta*hi;return true;
}
Vec3 intersectionWitness(const InertialShape& sa,const InertialBody& a,const InertialShape& sb,const InertialBody& b){
    std::vector<Vec3> points;auto addPoint=[&](Vec3 p){for(auto previous:points)if((previous-p).len()<1e-6)return;points.push_back(p);};
    auto add=[&](const InertialShape& source,const InertialBody& body,const InertialShape& other,const InertialBody& target){for(auto edge:source.segments){Vec3 p,q;if(clippedSegment(other,target,point(body,source.vertices[edge[0]]),point(body,source.vertices[edge[1]]),p,q)){addPoint(p);addPoint(q);}}};add(sa,a,sb,b);add(sb,b,sa,a);require(!points.empty(),"SAT contact lacks a valid intersection witness; reduce time step/check mesh");Vec3 sum;for(auto p:points)sum+=p;return sum/(double)points.size();
}
bool contact(const InertialShape& sa,const InertialBody& a,const InertialShape& sb,const InertialBody& b,Contact& c){
    c.depth=INFINITY;
    auto axis=[&](Vec3 n){double length=n.len();if(length<1e-7)return true;n=n/length;auto pa=project(sa,a,n),pb=project(sb,b,n);double d1=pb.second-pa.first,d2=pa.second-pb.first;if(d1<0||d2<0)return false;if(std::min(d1,d2)<c.depth){c.depth=std::min(d1,d2);c.normal=d1<d2?n:-n;}return true;};
    for(auto n:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})if(!axis(n))return false;
    for(auto n:sa.normals)if(!axis(rotate(a.orientation,n)))return false;
    for(auto n:sb.normals)if(!axis(rotate(b.orientation,n)))return false;
    for(auto ea:sa.edges)for(auto eb:sb.edges)if(!axis(rotate(a.orientation,ea).cross(rotate(b.orientation,eb))))return false;
    c.point=intersectionWitness(sa,a,sb,b);return true;
}
std::vector<Contact> contacts(const std::vector<InertialShape>& shapes,const std::vector<InertialBody>& bodies,const InertialOptions& options,double dt){
    std::vector<Contact> list;
    for(size_t i=0;i<bodies.size();++i){const auto& a=bodies[i];if(options.ground&&!a.fixed){auto p=support(shapes[a.shape],a,{0,0,-1});if(p.z<options.groundZ){Contact c;c.a=(int)i;c.point=p;c.depth=options.groundZ-p.z;c.normal={0,0,1};list.push_back(c);}}
        if(options.contacts)for(size_t j=i+1;j<bodies.size();++j){const auto& b=bodies[j];if(a.fixed&&b.fixed)continue;Contact c;c.a=(int)i;c.b=(int)j;if(contact(shapes[a.shape],a,shapes[b.shape],b,c))list.push_back(c);}}
    for(auto& c:list){const auto& a=bodies[c.a];Vec3 v=a.velocity+a.omega.cross(c.point-a.position);if(c.b>=0){const auto& b=bodies[c.b];v=v-b.velocity-b.omega.cross(c.point-b.position);}double vn=v.dot(c.normal);c.target=std::max(-options.restitution*std::min(vn,0.),.15*std::max(c.depth-1e-5,0.)/dt);}
    return list;
}
void integrate(std::vector<InertialBody>& bodies,const InertialOptions& options,double dt){for(auto& b:bodies){if(b.fixed)continue;Vec3 acceleration=options.gravity+b.force/b.mass;
    // Exact constant-force translation and midpoint gyroscopic angular step.
    b.position+=b.velocity*dt+acceleration*(.5*dt*dt);b.velocity+=acceleration*dt;
    Vec3 alpha=inertiaWorld(b,b.torque-b.omega.cross(inertiaWorld(b,b.omega,false)),true);InertialBody mid=b;mid.omega=b.omega+alpha*(.5*dt);mid.orientation=normalized(multiply(rotationStep(b.omega,.5*dt),b.orientation));Vec3 midAlpha=inertiaWorld(mid,mid.torque-mid.omega.cross(inertiaWorld(mid,mid.omega,false)),true);b.omega+=midAlpha*dt;
    b.orientation=normalized(multiply(rotationStep(mid.omega,dt),b.orientation));}}
void solve(std::vector<InertialBody>& bodies,const std::vector<Contact>& list,const std::vector<InertialJoint>& joints,const InertialOptions& options,double dt){
    std::vector<unsigned> degree(bodies.size(),0);for(auto& c:list){++degree[c.a];if(c.b>=0)++degree[c.b];}for(auto& j:joints){++degree[j.a];++degree[j.b];}
    for(unsigned it=0;it<options.iterations;++it){auto next=bodies;for(size_t i=0;i<bodies.size();++i){const auto& a=bodies[i];if(a.fixed)continue;Vec3 dv,dw;
        for(auto& c:list){if(c.a!=(int)i&&c.b!=(int)i)continue;int other=c.a==(int)i?c.b:c.a;Vec3 n=c.a==(int)i?c.normal:-c.normal,ra=c.point-a.position;const InertialBody* b=other>=0?&bodies[other]:nullptr;Vec3 rb=b?c.point-b->position:Vec3{};Vec3 relative=a.velocity+a.omega.cross(ra)-(b?b->velocity+b->omega.cross(rb):Vec3{});double denom=effective(a,ra,n)+(b?effective(*b,rb,n):0);if(denom<=1e-18)continue;
            double relax=1./std::max(degree[i],other>=0?degree[other]:1u);double impulse=std::max(0.,(c.target-relative.dot(n))/denom)*relax;Vec3 p=n*impulse;Vec3 tangential=relative-n*relative.dot(n);double speed=tangential.len();if(speed>1e-9){Vec3 t=tangential/speed;double td=effective(a,ra,t)+(b?effective(*b,rb,t):0);p=p-t*std::min(options.friction*impulse,speed/td*relax);}dv+=p*invMass(a);dw+=inertiaWorld(a,ra.cross(p),true);
        }
        for(auto& j:joints){if(j.a!=i&&j.b!=i)continue;bool first=j.a==i;size_t other=first?j.b:j.a;const auto& b=bodies[other];Vec3 ra=rotate(a.orientation,first?j.anchorA:j.anchorB),rb=rotate(b.orientation,first?j.anchorB:j.anchorA);Vec3 error=b.position+rb-a.position-ra;Vec3 relative=a.velocity+a.omega.cross(ra)-b.velocity-b.omega.cross(rb);double relax=.6/std::max(degree[i],degree[other]);
            for(auto n:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}){double denom=effective(a,ra,n)+effective(b,rb,n);if(denom>1e-18){Vec3 p=n*((.15*error.dot(n)/dt-relative.dot(n))/denom*relax);dv+=p*invMass(a);dw+=inertiaWorld(a,ra.cross(p),true);}}
            if(j.hinge){Vec3 aa=rotate(a.orientation,first?j.axisA:j.axisB),ab=rotate(b.orientation,first?j.axisB:j.axisA);Vec3 target=aa.cross(ab)*(.15/dt),relativeW=a.omega-b.omega;Vec3 t1=aa.cross(std::abs(aa.x)<.8?Vec3{1,0,0}:Vec3{0,1,0}).normalized(),t2=aa.cross(t1);for(auto n:{t1,t2}){double denom=n.dot(inertiaWorld(a,n,true)+inertiaWorld(b,n,true));if(denom>1e-18)dw+=inertiaWorld(a,n*((target-relativeW).dot(n)/denom*relax),true);}}
        }next[i].velocity+=dv;next[i].omega+=dw;
    }bodies=std::move(next);}
}
void validate(const std::vector<InertialShape>& shapes,const std::vector<InertialBody>& bodies,const std::vector<InertialJoint>& joints,const InertialOptions& o){
    require(!bodies.empty()&&bodies.size()<=128,"select 1..128 rigid bodies");require(o.backend=="gpu"||o.backend=="cpu","backend must be gpu or cpu; no silent fallback");
    require(std::isfinite(o.duration)&&o.duration>0&&o.duration<=60&&std::isfinite(o.step)&&o.step>=1e-5&&o.step<=1./30&&o.duration/o.step<=14400,"duration/step exceeds supported 14400 steps");require(o.outputFps>=1&&o.outputFps<=120&&o.duration*o.outputFps<=1800,"output capacity is 1801 frames");require(o.iterations>=1&&o.iterations<=64,"solver_iterations must be 1..64");require(finite(o.gravity)&&o.gravity.len()<=100&&std::isfinite(o.groundZ),"invalid gravity/ground");require(std::isfinite(o.restitution)&&o.restitution>=0&&o.restitution<=1&&std::isfinite(o.friction)&&o.friction>=0&&o.friction<=2,"invalid contact coefficients");
    unsigned scheduled=0;double previous=0;size_t samples=(size_t)std::ceil(o.duration*o.outputFps);require((samples+1)*bodies.size()<=65536,"simulation frame storage exceeds 65536 body states");for(size_t i=1;i<=samples;++i){double target=std::min(o.duration,(double)i/o.outputFps);scheduled+=(unsigned)std::ceil((target-previous)/o.step-1e-9);previous=target;}require(scheduled<=14400,"frame-aligned schedule exceeds 14400 steps");
    for(auto& b:bodies){require(b.shape<shapes.size()&&b.mass>=1e-9&&b.mass<=1e12&&std::isfinite(b.mass),"invalid shape/mass (supported kg: 1e-9..1e12)");auto inv=inverse(b.inertia);for(double n:inv)require(std::isfinite((float)n),"inertia exceeds GPU numeric range");for(double n:b.inertia)require(std::isfinite((float)n),"inertia exceeds GPU numeric range");for(int d:{0,4,8})require((float)inv[d]>0&&(float)b.inertia[d]>0,"inertia underflows GPU precision");double norm=std::inner_product(b.orientation.begin(),b.orientation.end(),b.orientation.begin(),0.);require(std::isfinite(norm)&&std::abs(norm-1)<1e-8,"orientation must be a unit quaternion");require(finite(b.position)&&finite(b.velocity)&&finite(b.omega)&&finite(b.force)&&finite(b.torque),"nonfinite body inputs");require(b.position.len()<1e4&&b.velocity.len()<1e4&&b.omega.len()<1e4&&b.force.len()<1e12&&b.torque.len()<1e12,"body inputs exceed solver range");require(!b.fixed||(b.velocity.len()==0&&b.omega.len()==0),"fixed bodies cannot move");}
    size_t vertices=0;for(auto& s:shapes){require(s.vertices.size()>=4&&s.vertices.size()<=4096&&!s.normals.empty()&&!s.edges.empty()&&!s.segments.empty()&&s.normals.size()<=16384&&s.edges.size()<=24576&&s.segments.size()<=24576,"invalid collision shape");vertices+=s.vertices.size();for(auto v:s.vertices)require(finite(v)&&v.len()<1e4,"nonfinite/out-of-range mesh vertex");for(auto list:{&s.normals,&s.edges})for(auto v:*list)require(finite(v)&&std::abs(v.len()-1)<1e-6,"invalid mesh direction");for(auto edge:s.segments)require(edge[0]<s.vertices.size()&&edge[1]<s.vertices.size()&&edge[0]!=edge[1],"invalid mesh edge");}require(vertices<=131072,"collision mesh capacity exceeded");
    double totalWork=0;if(o.contacts)for(size_t i=0;i<bodies.size();++i)for(size_t j=i+1;j<bodies.size();++j){if(bodies[i].fixed&&bodies[j].fixed)continue;const auto& a=shapes[bodies[i].shape];const auto& b=shapes[bodies[j].shape];double clipWork=(double)a.segments.size()*b.normals.size()*b.vertices.size()+(double)b.segments.size()*a.normals.size()*a.vertices.size();double work=(double)(a.edges.size()*b.edges.size()+a.normals.size()+b.normals.size()+3)*(a.vertices.size()+b.vertices.size())+clipWork*(1+2.*(a.segments.size()+b.segments.size()));require(work<=20000000,"convex collision complexity exceeds 20M support operations per pair; use explicit simpler simulation solids");totalWork+=work;}require(totalWork<=40000000&&totalWork*scheduled<=5000000000.,"total collision work exceeds supported dispatch/run capacity; select fewer bodies or a shorter study");
    for(auto& j:joints)require(j.a<bodies.size()&&j.b<bodies.size()&&j.a!=j.b&&finite(j.anchorA)&&finite(j.anchorB)&&(!j.hinge||(std::abs(j.axisA.len()-1)<1e-6&&std::abs(j.axisB.len()-1)<1e-6)),"invalid joint");require(joints.size()<=128,"joint capacity exceeded");
}
}
using namespace inertial;

InertialShape prepareInertialShape(const Mesh& mesh,Vec3 center){
    require(mesh.pos.size()%3==0&&mesh.idx.size()%3==0&&mesh.triCount()>=4&&mesh.triCount()<=16384,"collision mesh requires 4..16384 triangles");
    InertialShape shape;shape.sourceVertices=mesh.vertCount();std::map<std::array<float,3>,unsigned> unique;std::vector<unsigned> remap;
    for(size_t i=0;i<mesh.vertCount();++i){std::array<float,3> key{mesh.pos[3*i],mesh.pos[3*i+1],mesh.pos[3*i+2]};require(std::all_of(key.begin(),key.end(),[](float n){return std::isfinite(n);}),"nonfinite mesh");auto [it,inserted]=unique.emplace(key,(unsigned)shape.vertices.size());if(inserted)shape.vertices.push_back((Vec3{key[0],key[1],key[2]}-center)/1000.);remap.push_back(it->second);}
    require(shape.vertices.size()>=4&&shape.vertices.size()<=4096,"collision mesh supports 4..4096 unique vertices");
    std::map<std::pair<unsigned,unsigned>,std::vector<Vec3>> edgeFaces;std::map<std::pair<unsigned,unsigned>,int> directed;double volume=0;
    auto direction=[&](std::vector<Vec3>& list,Vec3 v){v=v.normalized();if(std::abs(v.x)>1e-8?v.x<0:std::abs(v.y)>1e-8?v.y<0:v.z<0)v=-v;for(auto n:list)if(n.dot(v)>1-1e-10)return;list.push_back(v);};
    for(size_t i=0;i<mesh.idx.size();i+=3){unsigned ids[3];for(int j=0;j<3;++j){require(mesh.idx[i+j]<remap.size(),"invalid mesh index");ids[j]=remap[mesh.idx[i+j]];}auto a=shape.vertices[ids[0]],b=shape.vertices[ids[1]],c=shape.vertices[ids[2]],cross=(b-a).cross(c-a);require(cross.len()>1e-16,"degenerate collision triangle");Vec3 n=cross.normalized();volume+=a.dot(b.cross(c))/6;
        for(auto v:shape.vertices)require((v-a).dot(n)<=1e-7,"contact simulation requires a convex closed outward mesh; concave surfaces are not replaced with a hull");direction(shape.normals,n);
        for(int j=0;j<3;++j){unsigned x=ids[j],y=ids[(j+1)%3];++directed[{x,y}];edgeFaces[{std::min(x,y),std::max(x,y)}].push_back(n);}}
    require(volume>1e-15,"collision mesh has no positive volume");for(auto& [edge,faces]:edgeFaces){require(faces.size()==2&&directed[{edge.first,edge.second}]==1&&directed[{edge.second,edge.first}]==1,"collision mesh is not closed and consistently oriented");if(faces[0].dot(faces[1])<1-1e-10){direction(shape.edges,shape.vertices[edge.second]-shape.vertices[edge.first]);shape.segments.push_back({edge.first,edge.second});}}
    std::string bytes(reinterpret_cast<const char*>(mesh.pos.data()),mesh.pos.size()*sizeof(float));bytes.append(reinterpret_cast<const char*>(mesh.idx.data()),mesh.idx.size()*sizeof(u32));for(double n:{center.x,center.y,center.z})bytes.append(reinterpret_cast<const char*>(&n),sizeof n);shape.digest=iterationSha256(bytes);return shape;
}
Mat4 inertialDisplayPose(const InertialBody& b){auto q=b.orientation;Mat4 r;auto x=rotate(q,{1,0,0}),y=rotate(q,{0,1,0}),z=rotate(q,{0,0,1});for(int i=0;i<3;++i){auto v=i==0?x:i==1?y:z;r.m[4*i]=v.x;r.m[4*i+1]=v.y;r.m[4*i+2]=v.z;}auto t=b.position*1000-r.transformDir(b.localCenterMm);r.m[12]=t.x;r.m[13]=t.y;r.m[14]=t.z;return r;}

#include "inertial_gpu.inl"

V inertialCapabilities(){V result=V::makeObj();result.set("module",V::makeStr("inertial_dynamics"));result.set("gpu_available",V::makeBool(false));
#ifdef _WIN32
    auto& g=gpu();std::lock_guard lock(g.mutex);try{g.init();result.set("gpu_available",V::makeBool(true));result.set("adapter",V::makeStr(g.adapter));result.set("backend",V::makeStr("D3D11 hardware compute / float32"));}catch(const std::exception& e){result.set("reason",V::makeStr(e.what()));}
#else
    result.set("reason",V::makeStr("GPU backend requires Windows Direct3D11; choose cpu explicitly for the portable reference solver"));
#endif
    result.set("max_bodies",V::makeNum(128));result.set("max_steps",V::makeNum(14400));result.set("collision_model",V::makeStr("convex_closed_mesh_SAT_discrete_impulses"));result.set("silent_fallback",V::makeBool(false));return result;
}
V inertialSimulationSchema(){V schema;arkjson::parse(R"({"type":"object","additionalProperties":false,"properties":{"action":{"enum":["capabilities","run"]},"backend":{"enum":["gpu","cpu"],"default":"gpu"},"duration_s":{"type":"number","exclusiveMinimum":0,"maximum":60},"step_s":{"type":"number","minimum":0.00001,"maximum":0.0333333333333},"output_fps":{"type":"integer","minimum":1,"maximum":120},"solver_iterations":{"type":"integer","minimum":1,"maximum":64},"gravity_m_s2":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"ground_z_mm":{"type":"number"},"restitution":{"type":"number","minimum":0,"maximum":1},"friction":{"type":"number","minimum":0,"maximum":2},"contacts":{"type":"boolean"},"assembly_joints":{"type":"boolean","description":"True preserves supported unlimited revolute CAD mates. False explicitly simulates detached bodies; unsupported joints never silently disappear."},"include_frames":{"type":"boolean"},"bodies":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","additionalProperties":false,"properties":{"id":{"type":["integer","string"]},"node":{"type":"integer","minimum":1},"copy":{"type":"integer","minimum":0},"fixed":{"type":"boolean"},"velocity_m_s":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"angular_velocity_rad_s":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"force_N":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"torque_Nm":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}}},"anyOf":[{"required":["id"]},{"required":["node"]}]}}}})",schema);return schema;}
InertialResult simulateInertial(const std::vector<InertialShape>& shapes,const std::vector<InertialBody>& initial,const std::vector<InertialJoint>& joints,const InertialOptions& options){
    validate(shapes,initial,joints,options);InertialResult out;
    auto control=captureNativeRequestControl();if(control.cancelled&&control.cancelled())throw std::runtime_error("NATIVE_CANCELLED: inertial simulation cancelled");
    if(options.backend=="gpu"){
#ifdef _WIN32
        out=runGpu(shapes,initial,joints,options);
#else
        throw std::runtime_error("SIMULATION: GPU backend unavailable on this platform; cpu must be explicitly selected");
#endif
    }else{auto start=std::chrono::steady_clock::now();auto bodies=initial;out.frames.push_back(bodies);out.times.push_back(0);double time=0;unsigned steps=0;size_t frames=(size_t)std::ceil(options.duration*options.outputFps);
        for(size_t sample=1;sample<=frames;++sample){double target=std::min(options.duration,(double)sample/options.outputFps);while(time<target-1e-12){if(control.cancelled&&control.cancelled())throw std::runtime_error("NATIVE_CANCELLED: inertial simulation cancelled");double dt=std::min(options.step,target-time);integrate(bodies,options,dt);auto list=contacts(shapes,bodies,options,dt);solve(bodies,list,joints,options,dt);time+=dt;++steps;for(auto& b:bodies)require(finite(b.position)&&finite(b.velocity)&&finite(b.omega),"CPU solver became nonfinite");}out.frames.push_back(bodies);out.times.push_back(time);}
        out.diagnostics=V::makeObj();out.diagnostics.set("backend",V::makeStr("CPU reference / float64"));out.diagnostics.set("steps",V::makeNum(steps));out.diagnostics.set("transfer_inclusive_ms",V::makeNum(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));}
    double maxDrift=0;for(auto& frame:out.frames)for(auto& j:joints)maxDrift=std::max(maxDrift,(point(frame[j.a],j.anchorA)-point(frame[j.b],j.anchorB)).len());
    auto energy=[&](const std::vector<InertialBody>& bodies){double total=0;for(auto& b:bodies)if(!b.fixed)total+=.5*b.mass*b.velocity.dot(b.velocity)+.5*b.omega.dot(inertiaWorld(b,b.omega,false))-b.mass*options.gravity.dot(b.position);return total;};
    require(std::isfinite(energy(initial))&&std::isfinite(energy(out.frames.back()))&&std::isfinite(maxDrift),"nonfinite simulation diagnostics");out.diagnostics.set("initial_energy_J",V::makeNum(energy(initial)));out.diagnostics.set("final_energy_J",V::makeNum(energy(out.frames.back())));out.diagnostics.set("max_sampled_joint_drift_mm",V::makeNum(maxDrift*1000));out.diagnostics.set("continuous_collision_detection",V::makeBool(false));out.diagnostics.set("geometry_modified",V::makeBool(false));out.diagnostics.set("qualified",V::makeBool(false));return out;
}
namespace inertial {
Quat rotation(const Mat4& m){
    // Branch-stable matrix-to-quaternion conversion for proper rigid transforms.
    Vec3 x{m.m[0],m.m[1],m.m[2]},y{m.m[4],m.m[5],m.m[6]},z{m.m[8],m.m[9],m.m[10]};require(std::abs(x.len()-1)<1e-7&&std::abs(y.len()-1)<1e-7&&std::abs(z.len()-1)<1e-7&&std::abs(x.dot(y))<1e-7&&x.cross(y).dot(z)>.999999,"simulation placements must be proper rigid transforms");
    double trace=m.m[0]+m.m[5]+m.m[10];Quat q;
    if(trace>0){double s=2*std::sqrt(trace+1);q={(m.m[6]-m.m[9])/s,(m.m[8]-m.m[2])/s,(m.m[1]-m.m[4])/s,.25*s};}
    else if(m.m[0]>m.m[5]&&m.m[0]>m.m[10]){double s=2*std::sqrt(1+m.m[0]-m.m[5]-m.m[10]);q={.25*s,(m.m[4]+m.m[1])/s,(m.m[8]+m.m[2])/s,(m.m[6]-m.m[9])/s};}
    else if(m.m[5]>m.m[10]){double s=2*std::sqrt(1+m.m[5]-m.m[0]-m.m[10]);q={(m.m[4]+m.m[1])/s,.25*s,(m.m[9]+m.m[6])/s,(m.m[8]-m.m[2])/s};}
    else{double s=2*std::sqrt(1+m.m[10]-m.m[0]-m.m[5]);q={(m.m[8]+m.m[2])/s,(m.m[9]+m.m[6])/s,.25*s,(m.m[1]-m.m[4])/s};}return normalized(q);
}
bool fixedNode(const Document& d,u64 id){std::set<u64> seen;while(id){require(seen.insert(id).second,"assembly parent cycle");auto it=std::find_if(d.assembly.begin(),d.assembly.end(),[&](auto& n){return n.id==id;});require(it!=d.assembly.end(),"missing assembly parent");if(it->fixed)return true;id=it->parent;}return false;}
}
V inspectInertial(AgentSession& session,const V& args){
    fields(args,{"simulation","expected_revision"});
    auto request=args.find("simulation");require(request&&request->t==V::Obj,"simulation object is required");const V& input=*request;
    fields(input,{"action","backend","duration_s","step_s","output_fps","solver_iterations","gravity_m_s2","restitution","friction","ground_z_mm","contacts","bodies","include_frames","assembly_joints"});
    if(auto p=input.find("action")){require(p->t==V::Str&&(p->str=="capabilities"||p->str=="run"),"action must be capabilities or run");if(p->str=="capabilities")return inertialCapabilities();}
    const auto revision=args.find("expected_revision");require(revision&&revision->t==V::Num&&revision->num==(double)session.revision,"expected_revision must match the current document");
    InertialOptions options;options.duration=num(input,"duration_s",1);options.step=num(input,"step_s",1./240);double fps=num(input,"output_fps",30),iterations=num(input,"solver_iterations",16);require(fps==std::floor(fps)&&fps>=1&&fps<=120&&iterations==std::floor(iterations)&&iterations>=1&&iterations<=64,"invalid integer output_fps or solver_iterations");options.outputFps=(unsigned)fps;options.iterations=(unsigned)iterations;
    if(auto p=input.find("backend")){require(p->t==V::Str,"backend must be a string");options.backend=p->str;}if(auto p=input.find("gravity_m_s2"))options.gravity=xyz(*p);options.restitution=num(input,"restitution",.1);options.friction=num(input,"friction",.5);options.ground=input.find("ground_z_mm")!=nullptr;options.groundZ=num(input,"ground_z_mm",0)/1000.;options.contacts=flag(input,"contacts",true);bool includeFrames=flag(input,"include_frames",false);
    const auto& doc=session.document;std::vector<AssemblyPlacement> placements;AssemblyDiagnostics assembly;std::string error;
    if(!doc.assembly.empty())require(resolveAssembly(doc,placements,error,&assembly),error);else{auto consumed=consumedFeatureInputs(doc),active=activeFeatureIds(doc);for(auto& f:doc.features)if(active.count(f.id)&&!consumed.count(f.id)){AssemblyPlacement p;p.feature=f.id;p.name=f.name;placements.push_back(p);}}
    std::vector<std::pair<AssemblyPlacement,V>> selected;
    if(auto list=input.find("bodies")){require(list->t==V::Arr&&!list->arr.empty()&&list->arr.size()<=128,"bodies must select 1..128 occurrences");std::set<std::tuple<u64,u64,int>> identities;for(auto& row:list->arr){fields(row,{"id","node","copy","fixed","velocity_m_s","angular_velocity_rad_s","force_N","torque_Nm"});require(row.find("id")||row.find("node"),"body requires id or node");std::vector<AssemblyPlacement> found;for(auto& p:placements){bool match=true;if(auto id=row.find("id")){const auto* f=doc.find(p.feature);match=f&&((id->t==V::Str&&id->str==f->name)||(id->t==V::Num&&id->num==(double)p.feature));}if(auto node=row.find("node"))match=match&&node->t==V::Num&&node->num==(double)p.node;if(auto copy=row.find("copy"))match=match&&copy->t==V::Num&&copy->num==p.copy;if(match)found.push_back(p);}require(found.size()==1,"body selector must name exactly one placed occurrence; use node/copy");auto p=found.front();require(identities.emplace(p.feature,p.node,p.copy).second,"duplicate body selection");selected.push_back({p,row});}}
    else for(auto& p:placements)if(p.visible)selected.push_back({p,V::makeObj()});
    require(!selected.empty()&&selected.size()<=128,"select 1..128 bodies");require(!includeFrames||(std::ceil(options.duration*options.outputFps)+1)*selected.size()<=32768,"requested frame response is too large; lower output_fps or select fewer bodies");std::vector<InertialBody> bodies;std::vector<InertialShape> shapes;std::map<u64,std::pair<size_t,InertialBody>> definitions;
    for(auto& [placement,row]:selected){auto it=definitions.find(placement.feature);if(it==definitions.end()){V query=V::makeObj();query.set("id",V::makeNum((double)placement.feature));V properties=inspectPhysics(session,query);require(properties.find("mass_checked")&&properties.find("mass_checked")->b,"exact mass/density missing for "+placement.name+": "+properties.dump());InertialBody base;base.feature=placement.feature;base.mass=properties.find("mass_kg")->num;base.localCenterMm=xyz(*properties.find("center_mm"));auto tensor=properties.find("inertia_kg_m2");require(tensor&&tensor->arr.size()==9,"missing exact inertia");for(size_t i=0;i<9;++i)base.inertia[i]=tensor->arr[i].num;auto body=std::find_if(doc.evalBodies.begin(),doc.evalBodies.end(),[&](auto& b){return b.id==placement.feature;});require(body!=doc.evalBodies.end(),"missing evaluated body");Mesh mesh=body->baked?*body->baked:body->tessellate(TessOptions{});base.shape=shapes.size();shapes.push_back(prepareInertialShape(mesh,base.localCenterMm));it=definitions.emplace(placement.feature,std::make_pair(base.shape,base)).first;}
        auto body=it->second.second;body.node=placement.node;body.copy=placement.copy;body.name=placement.name;body.position=placement.world.transformPoint(body.localCenterMm)/1000.;body.orientation=rotation(placement.world);body.fixed=flag(row,"fixed",fixedNode(doc,placement.node));if(auto p=row.find("velocity_m_s"))body.velocity=xyz(*p);if(auto p=row.find("angular_velocity_rad_s"))body.omega=xyz(*p);if(auto p=row.find("force_N"))body.force=xyz(*p);if(auto p=row.find("torque_Nm"))body.torque=xyz(*p);require(!body.fixed||(body.velocity.len()==0&&body.omega.len()==0),"fixed bodies cannot have prescribed velocity");bodies.push_back(body);}
    bool useJoints=flag(input,"assembly_joints",true);std::vector<InertialJoint> joints;
    if(useJoints){
        std::map<u64,const AssemblyNode*> nodes;for(const auto& node:doc.assembly)nodes.emplace(node.id,&node);
        std::set<u64> carriers;for(const auto& mate:doc.mates)if(!isMechanicalMate(mate.type)){const auto* a=nodes.at(mate.a);const auto* b=nodes.at(mate.b);if(a->parent!=b->parent){if(a->parent)carriers.insert(a->parent);if(b->parent)carriers.insert(b->parent);}}
        for(const auto& body:bodies)if(body.node){const auto* node=nodes.at(body.node);require(!node->pattern.active,"patterned dynamic bodies are unsupported; assembly_joints:false explicitly detaches bodies");for(u64 parent=node->parent;parent;parent=nodes.at(parent)->parent)require(!carriers.count(parent),"selected body belongs to an unsupported group-carrier joint; assembly_joints:false explicitly detaches bodies");}
    }
    if(useJoints)for(auto& mate:doc.mates){auto a=std::find_if(bodies.begin(),bodies.end(),[&](auto& b){return b.node==mate.a&&b.copy==0;}),b=std::find_if(bodies.begin(),bodies.end(),[&](auto& x){return x.node==mate.b&&x.copy==0;});if(a==bodies.end()&&b==bodies.end())continue;require(a!=bodies.end()&&b!=bodies.end(),"include both endpoints of each simulation joint");const auto nodeA=std::find_if(doc.assembly.begin(),doc.assembly.end(),[&](auto& n){return n.id==mate.a;}),nodeB=std::find_if(doc.assembly.begin(),doc.assembly.end(),[&](auto& n){return n.id==mate.b;});require(nodeA!=doc.assembly.end()&&nodeB!=doc.assembly.end()&&!nodeA->pattern.active&&!nodeB->pattern.active&&nodeA->parent==nodeB->parent,"group-carrier and patterned dynamic joints are unsupported; assembly_joints:false explicitly detaches bodies");require(mate.type=="revolute"&&!mate.limits.enabled,"only unlimited revolute CAD mates currently have dynamic constraints; assembly_joints:false explicitly runs detached bodies");auto frame=std::find_if(assembly.frames.begin(),assembly.frames.end(),[&](auto& f){return f.id==mate.id;});require(frame!=assembly.frames.end(),"missing current joint frames");InertialJoint j;j.a=(size_t)(a-bodies.begin());j.b=(size_t)(b-bodies.begin());j.anchorA=rotate(conjugate(a->orientation),frame->originA/1000.-a->position);j.anchorB=rotate(conjugate(b->orientation),frame->originB/1000.-b->position);j.axisA=rotate(conjugate(a->orientation),frame->axisA).normalized();j.axisB=rotate(conjugate(b->orientation),frame->axisB).normalized();j.hinge=true;joints.push_back(j);}
    auto result=simulateInertial(shapes,bodies,joints,options);V out=V::makeObj();out.set("revision",V::makeNum((double)session.revision));out.set("source_sha256",V::makeStr(iterationSha256(documentToJson(doc,false,false))));out.set("inputs",input);out.set("diagnostics",result.diagnostics);out.set("body_count",V::makeNum((double)bodies.size()));out.set("unique_meshes",V::makeNum((double)shapes.size()));out.set("joint_count",V::makeNum((double)joints.size()));out.set("mass_authority",V::makeStr("native_exact_CAD_with_declared_density"));out.set("contact_authority",V::makeStr("derived_convex_display_mesh_discrete_SAT"));out.set("limits",V::makeStr("Rigid bodies and unlimited revolute joints. Convex closed meshes only; concave meshes and unsupported mates reject. Discrete contacts can tunnel at high speed; no CCD, deformation, FEA, fatigue or manufacturing qualification. CPU prepares exact mass/meshes; GPU performs integration, contacts and constraint solve. Playback never edits CAD."));
    auto frameJson=[&](size_t index){V frame=V::makeObj(),rows=V::makeArr();frame.set("time_s",V::makeNum(result.times[index]));for(auto& body:result.frames[index]){V row=V::makeObj();row.set("id",V::makeNum((double)body.feature));row.set("node",V::makeNum((double)body.node));row.set("copy",V::makeNum(body.copy));row.set("mass_kg",V::makeNum(body.mass));row.set("center_mm",jvec(body.position*1000));row.set("velocity_m_s",jvec(body.velocity));row.set("angular_velocity_rad_s",jvec(body.omega));row.set("fixed",V::makeBool(body.fixed));V pose=V::makeArr();for(double v:inertialDisplayPose(body).m)pose.arr.push_back(V::makeNum(v));row.set("pose",pose);rows.arr.push_back(row);}frame.set("bodies",rows);return frame;};
    out.set("initial",frameJson(0));out.set("final",frameJson(result.frames.size()-1));if(includeFrames){require(result.frames.size()*bodies.size()<=32768,"requested frame response is too large; lower output_fps or select fewer bodies");V frames=V::makeArr();for(size_t i=0;i<result.frames.size();++i)frames.arr.push_back(frameJson(i));out.set("frames",std::move(frames));}return out;
}

} // namespace ark
