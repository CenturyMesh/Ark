#include "ark/sheet_preview.hpp"
#include "ark/native_backend.hpp"
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
const V& required(const V& v,const char* key){auto p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing sheet animation ")+key);return *p;}
double num(const V& v,const char* key){auto& p=required(v,key);if(p.t!=V::Num||!std::isfinite(p.num))throw std::runtime_error("Invalid animation number");return p.num;}
Mat4 mat(const V& v){if(v.t!=V::Arr||v.arr.size()!=16)throw std::runtime_error("Invalid sheet animation frame");Mat4 m;for(size_t i=0;i<16;++i){if(v.arr[i].t!=V::Num||!std::isfinite(v.arr[i].num))throw std::runtime_error("Invalid frame number");m.m[i]=v.arr[i].num;}return m;}
}
SheetPreview buildSheetPreview(const V& data,double progress){
    if(!std::isfinite(progress)||progress<0||progress>1)throw std::runtime_error("Sheet animation progress must be in [0,1]");
    const auto& program=required(data,"animation"),panels=required(program,"panels"),bends=required(program,"bends");
    if(panels.t!=V::Arr||bends.t!=V::Arr||panels.arr.size()!=bends.arr.size()+1)throw std::runtime_error("Invalid sheet animation tree");
    const double t=num(program,"thickness_mm");SheetPreview result;result.poses.push_back(mat(required(program,"root_pose")));
    // Root's translation interpolates too, preserving exact native flat pose
    // for legacy L documents whose formed origin was at the first bend.
    for(int axis=12;axis<15;++axis)result.poses[0].m[axis]*=progress;
    for(size_t i=0;i<bends.arr.size();++i){
        const auto& b=bends.arr[i];const double parentN=num(b,"parent_index");if(parentN<0||parentN>(double)i||parentN!=std::floor(parentN))throw std::runtime_error("Invalid animation parent");
        const size_t parent=(size_t)parentN;const double target=deg2rad(num(b,"angle_deg")),phase=clamp(progress*(double)bends.arr.size()-(double)i,0.,1.),angle=target*phase;
        const double alpha=std::abs(angle),sign=target<0?-1.:1.,k=num(b,"k_factor"),ba=num(b,"bend_allowance_mm"),width=num(b,"width_mm"),yn=sign>0?(1-k)*t:k*t;
        const Mat4 h=mat(required(b,"flat_attachment"));Mat4 joint=Mat4::identity();
        if(alpha>1e-12){const double rn=ba/alpha;const Vec3 end{rn*std::sin(alpha),yn+sign*rn*2*std::pow(std::sin(alpha/2),2),0};joint=Mat4::translation(end)*Mat4::rotZ(angle)*Mat4::translation({-ba,-yn,0});}
        result.poses.push_back(result.poses[parent]*h*joint*h.inverted());
        Mesh band;const int n=std::max(1,(int)std::ceil(alpha/deg2rad(2)));
        for(int j=0;j<=n;++j){const double fraction=(double)j/n,phi=fraction*alpha;for(auto yz:{Vec2{0,0},Vec2{t,0},Vec2{t,width},Vec2{0,width}}){double x=fraction*ba,y=yz.x;if(alpha>1e-12){const double r=ba/alpha-sign*(yz.x-yn);x=r*std::sin(phi);y=yz.x+sign*r*2*std::pow(std::sin(phi/2),2);}band.addVertex({x,y,yz.y});}}
        for(int j=0;j<n;++j)for(int edge=0;edge<4;++edge){u32 a=4*j+edge,bx=4*j+(edge+1)%4,c=bx+4,d=a+4;band.addTri(a,bx,c);band.addTri(a,c,d);}
        band.addTri(0,2,1);band.addTri(0,3,2);u32 end=4*n;band.addTri(end,end+1,end+2);band.addTri(end,end+2,end+3);band.computeNormals();result.mesh.append(band,result.poses[parent]*h);
    }
    for(size_t i=0;i<panels.arr.size();++i){V mesh=V::makeObj();mesh.set("mesh",required(panels.arr[i],"flat_mesh"));result.mesh.append(nativeResultMesh(mesh),result.poses[i]);}
    result.mesh.computeNormals();return result;
}
}
