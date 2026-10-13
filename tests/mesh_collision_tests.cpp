#include "ark/mesh_collision.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using ark::Mesh;using ark::Vec3;using ark::Mat4;using V=arkjson::Value;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
template<class F>void rejects(F&& call,const char* message){try{call();}catch(const std::exception&){return;}throw std::runtime_error(message);}
Mesh cube(double side){Mesh result;for(Vec3 p:std::initializer_list<Vec3>{{0,0,0},{side,0,0},{side,side,0},{0,side,0},{0,0,side},{side,0,side},{side,side,side},{0,side,side}})result.addVertex(p);for(ark::u32 index:{0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5})result.idx.push_back(index);return result;}
Mesh triangle(Vec3 a,Vec3 b,Vec3 c){Mesh result;result.addVertex(a);result.addVertex(b);result.addVertex(c);result.addTri(0,1,2);return result;}
V inspect(const Mesh& a,const Mesh& b,const Mat4& bWorld=Mat4::identity()){return ark::inspectMeshCollision(a,Mat4::identity(),b,bWorld);}
void separatedAndTransformed(){
    const Mesh a=cube(10);const std::string unchanged=std::to_string(a.pos.size())+":"+std::to_string(a.idx.size());
    const V separated=inspect(a,a,Mat4::translation({15,0,0}));check(separated.find("status")->str=="separated"&&!separated.find("aabb_candidate")->b&&!separated.find("boundary_contact")->b,"disjoint closed cubes separated");check(separated.find("containment_checked")->b&&!separated.find("interference")->b,"closed mesh separation excludes containment");check(separated.find("min_distance_mm")->t==V::Null&&!separated.find("minimum_distance_computed")->b,"unmeasured distance is not invented");check(unchanged==std::to_string(a.pos.size())+":"+std::to_string(a.idx.size()),"inspection changes no input arrays");
    const Mat4 rotated=Mat4::translation({20,0,0})*Mat4::rotZ(.7);check(inspect(a,a,rotated).find("status")->str=="separated","rotated explicit placement honored");
    const Mesh ta=triangle({0,0,0},{2,0,0},{0,2,0}),tb=triangle({2,2,0},{2,.75,0},{.75,2,0});const V candidate=inspect(ta,tb);check(candidate.find("aabb_candidate")->b&&candidate.find("candidate_triangle_pairs")->num==1&&!candidate.find("boundary_contact")->b,"overlapping triangle bounds do not imply surface collision");check(candidate.find("status")->str=="surface_separated"&&!candidate.find("containment_checked")->b&&candidate.find("interference")->t==V::Null,"open-mesh containment remains unknown");
}
void contactAndCrossing(){
    const Mesh a=cube(10);V touching=inspect(a,a,Mat4::translation({10,0,0}));check(touching.find("boundary_contact")->b&&touching.find("status")->str=="surface_intersection","face-touching cubes have triangle boundary contact");check(touching.find("interference")->t==V::Null&&touching.find("common_volume_mm3")->t==V::Null,"boundary contact does not invent common volume or intrusion");
    V overlapping=inspect(a,a,Mat4::translation({5,0,0}));check(overlapping.find("surface_intersection")->b&&overlapping.find("boundary_contact_pairs")->num>0,"crossing cube boundaries found");
    const Mesh horizontal=triangle({0,0,0},{2,0,0},{0,2,0}),vertical=triangle({.5,.5,-1},{.5,.5,1},{.5,1.5,1});check(inspect(horizontal,vertical).find("surface_intersection")->b,"triangle edge pierces face interior");
    const Mesh coplanar=triangle({.5,-.5,0},{2,.5,0},{.5,2,0});check(inspect(horizontal,coplanar).find("surface_intersection")->b,"coplanar triangle contact handled");
    const Mesh nearParallelA=triangle({-100000,0,0},{100000,0,0},{-100000,-1,0}),nearParallelB=triangle({-200000,.002,0},{200000,-.002,0},{-200000,.003,0});check(inspect(nearParallelA,nearParallelB).find("surface_intersection")->b,"long almost-parallel coplanar edges cross without tolerance inflation");
    const Mesh justAbove=triangle({.1,.1,.0000005},{1,.1,.0000005},{.1,1,.0000005});const V toleranceContact=inspect(horizontal,justAbove);check(toleranceContact.find("boundary_contact")->b&&toleranceContact.find("contact_tolerance_mm")->num==1e-6,"fixed tolerance disclosed on near contact");
}
void nestedClosedMeshContainment(){
    const Mesh outer=cube(10),inner=cube(2);const Mat4 pose=Mat4::translation({3,3,3})*Mat4::rotZ(.3);V contained=inspect(outer,inner,pose);check(contained.find("aabb_candidate")->b&&!contained.find("boundary_contact")->b&&contained.find("status")->str=="interference","nested closed bodies collide without boundary crossing");check(contained.find("containment_checked")->b&&contained.find("interference")->b&&contained.find("b_in_a")->find("status")->str=="contained","oriented winding certifies tested containment domain");check(contained.find("authority")->str=="indexed_triangle_mesh"&&!contained.find("exact_geometry")->b&&!contained.find("trajectory_checked")->b,"mesh authority and static scope truthful");
    Mesh reversed=outer;reversed.flip();check(inspect(reversed,inner,pose).find("interference")->b,"globally reversed coherent orientation retains containment");
    Mesh open=outer;open.idx.resize(open.idx.size()-3);V unknown=inspect(open,inner,pose);check(!unknown.find("containment_checked")->b&&unknown.find("interference")->t==V::Null,"open mesh cannot certify a solid interior");
    Mesh nonmanifold=outer;nonmanifold.addTri(0,2,1);unknown=inspect(nonmanifold,inner,pose);check(!unknown.find("containment_checked")->b&&unknown.find("b_in_a")->find("status")->str=="unknown_open_or_nonmanifold_mesh","nonmanifold topology leaves containment unknown");
    Mesh disconnected=inner;disconnected.append(inner,Mat4::translation({20,0,0}));V partial=inspect(outer,disconnected,Mat4::translation({3,3,3}));check(partial.find("interference")->b&&partial.find("b_in_a")->find("status")->str=="partially_contained_components","all source components receive containment checks");
}
void invalidAndCapacity(){
    Mesh a=cube(1),invalid=a;invalid.idx[0]=100;rejects([&]{inspect(a,invalid);},"invalid index accepted");invalid=a;invalid.pos[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{inspect(a,invalid);},"nonfinite vertex accepted");invalid=a;invalid.idx.pop_back();rejects([&]{inspect(a,invalid);},"incomplete triangle accepted");invalid=a;invalid.idx[1]=invalid.idx[0];rejects([&]{inspect(a,invalid);},"degenerate triangle accepted");rejects([&]{inspect(a,a,Mat4::scale(2,1,1));},"nonrigid transform accepted");Mat4 bad;bad.m[12]=std::numeric_limits<double>::infinity();rejects([&]{inspect(a,a,bad);},"nonfinite transform accepted");
    Mesh tooMany;tooMany.pos={0,0,0,1,0,0,0,1,0};tooMany.idx.resize(300003);for(size_t i=0;i<tooMany.idx.size();++i)tooMany.idx[i]=(ark::u32)(i%3);rejects([&]{inspect(a,tooMany);},"input capacity silently truncated");
    Mesh dense;dense.pos={0,0,0,1,0,0,0,1,0};for(size_t i=0;i<710;++i)dense.addTri(0,1,2);rejects([&]{inspect(dense,dense);},"candidate-pair capacity silently truncated");
}
}
int main(){try{separatedAndTransformed();contactAndCrossing();nestedClosedMeshContainment();invalidAndCapacity();std::cout<<"mesh collision tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<"mesh collision: "<<e.what()<<'\n';return 1;}}
