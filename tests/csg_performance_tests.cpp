#include "ark/mesh.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/mesh_query.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ark;
namespace {
int checks=0;
void check(bool condition,const char* label){++checks;if(!condition)throw std::runtime_error(label);}
Mesh box(double size,const Mat4& frame) {
    Mesh mesh;for(auto p:{Vec3{0,0,0},Vec3{size,0,0},Vec3{size,size,0},Vec3{0,size,0},Vec3{0,0,size},Vec3{size,0,size},Vec3{size,size,size},Vec3{0,size,size}})mesh.addVertex(frame.transformPoint(p));
    mesh.idx={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7};return mesh;
}
double volume(const Mesh& mesh){double v=0;auto p=[&](u32 i){return Vec3{mesh.pos[i*3],mesh.pos[i*3+1],mesh.pos[i*3+2]};};for(size_t i=0;i<mesh.idx.size();i+=3)v+=p(mesh.idx[i]).dot(p(mesh.idx[i+1]).cross(p(mesh.idx[i+2])))/6;return v;}
}
int main(){try {
    cpu_detect();
    auto start=std::chrono::steady_clock::now();
    for(const auto& frame:{Mat4::identity(),Mat4::translation({19,-8,11})*Mat4::rotX(.23)}) {
        auto a=box(10,frame),b=box(4,frame*Mat4::translation({3,3,3}));
        for(auto op:{CsgOp::Union,CsgOp::Subtract,CsgOp::Intersect}) {
            auto result=meshBoolean(a,b,op);
            const double expected=op==CsgOp::Union?1000:op==CsgOp::Subtract?936:64;
            check(std::abs(volume(result)-expected)<expected*1e-5,"Contained box Boolean preserves independent analytic volume");
            auto report=analyzeMesh(result);check(report.closedOriented(),"Contained box Boolean stays closed and oriented");
            MeshBVH bvh(result);auto cast=[&](double x,double y){return bvh.raycast(result,{frame.transformPoint({x,y,20}),frame.transformDir({0,0,-1})}).hit;};
            check(cast(5,5),"Ray hits material over the inner box");
            check(cast(1,1)==(op!=CsgOp::Intersect),"Outer material matches Boolean operation");
        }
    }
    // Oversized inputs are rejected before polygon allocation/splitting. This
    // is a resource contract, not a claim that arbitrary mesh CSG is watertight.
    auto oversized=box(1,Mat4::identity());oversized.idx.resize(3000003,0);
    bool rejected=false;
    try{meshBoolean(oversized,box(1,Mat4::identity()),CsgOp::Union);}
    catch(const std::runtime_error& error){rejected=std::string(error.what()).find("bounded work budget")!=std::string::npos;}
    check(rejected,"Input triangle budget fails explicitly before expensive work");
    std::cout<<checks<<" CSG acceleration checks passed in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"s\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
