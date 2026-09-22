#include "ark/source_tube.hpp"
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
const V& field(const V& j,const char* k){auto p=j.find(k);if(!p)throw std::runtime_error(std::string("Source package missing ")+k);return *p;}
double number(const V& j,const char* k){auto& p=field(j,k);if(p.t!=V::Num||!std::isfinite(p.num))throw std::runtime_error("Invalid source bend number");return p.num;}
Vec3 vector(const V& j,const char* k){auto& p=field(j,k);if(p.t!=V::Arr||p.arr.size()!=3)throw std::runtime_error("Invalid source vector");for(auto& n:p.arr)if(n.t!=V::Num||!std::isfinite(n.num))throw std::runtime_error("Non-finite source vector");return {p.arr[0].num,p.arr[1].num,p.arr[2].num};}
Mesh mesh(const V& j){
    Mesh m;auto& p=field(j,"pos");auto& t=field(j,"idx");if(p.t!=V::Arr||t.t!=V::Arr||p.arr.empty()||t.arr.empty()||p.arr.size()%3||t.arr.size()%3||p.arr.size()>6000000||t.arr.size()>6000000)throw std::runtime_error("Invalid source mesh buffers");
    m.pos.reserve(p.arr.size());for(auto& v:p.arr){if(v.t!=V::Num||!std::isfinite(v.num)||std::fabs(v.num)>1e8)throw std::runtime_error("Invalid source mesh position");m.pos.push_back((f32)v.num);}
    m.idx.reserve(t.arr.size());for(auto& v:t.arr){if(v.t!=V::Num||v.num<0||v.num!=std::floor(v.num)||v.num>=m.vertCount())throw std::runtime_error("Invalid source mesh triangle");m.idx.push_back((u32)v.num);}
    m.computeNormals();return m;
}
}
SourceTubePreview buildSourceTubePreview(const V& package){
    if(field(package,"format").asStr()!="ark-source-tube"||number(package,"version")!=1)throw std::runtime_error("Unsupported source package");
    SourceTubePreview out;out.report=field(package,"report");out.holes=field(package,"holes");
    auto& verified=field(out.report,"source_projection_verified");if(verified.t!=V::Bool||!verified.b)throw std::runtime_error("Source projection is not verified");
    out.straight=mesh(field(package,"straight"));const auto& rigid=field(package,"rigid");const auto& bends=field(package,"bends");
    if(rigid.t!=V::Arr||bends.t!=V::Arr||bends.arr.empty()||bends.arr.size()>32||rigid.arr.size()!=bends.arr.size()+1)throw std::runtime_error("Invalid source span chain");
    if(out.holes.t!=V::Arr||out.holes.arr.size()>10000||number(out.report,"opening_count")!=(double)out.holes.arr.size())throw std::runtime_error("Invalid source opening inventory");
    for(auto& hole:out.holes.arr){double index=number(hole,"segment");if(index<0||index>=rigid.arr.size()||index!=std::floor(index))throw std::runtime_error("Invalid opening owner");vector(hole,"source_center");vector(hole,"flat_center");}
    for(auto& part:rigid.arr)out.rigid.push_back(mesh(part));
    for(auto& j:bends.arr){SourceBend b;b.start=vector(j,"start");b.axis=vector(j,"axis");b.outward=vector(j,"outward");b.widthAxis=vector(j,"width_axis");b.length=number(j,"length");b.width=number(j,"width");b.wall=number(j,"wall");b.neutralRadius=number(j,"neutral_radius");b.angle=number(j,"angle");
        if(b.width<=0||b.wall<=0||b.neutralRadius<=b.wall/2||b.length<=0||b.angle<=0||b.angle>K_PI||std::fabs(b.length-b.neutralRadius*b.angle)>1e-6||std::fabs(b.axis.len()-1)>1e-8||std::fabs(b.outward.len()-1)>1e-8||std::fabs(b.widthAxis.len()-1)>1e-8||std::fabs(std::fabs(b.widthAxis.dot(b.axis))-1)>1e-8||!b.axis.cross(b.outward).approx({1,0,0},1e-8))throw std::runtime_error("Invalid finite hinge frame");
        out.bends.push_back(b);
    }return out;
}
std::vector<Mat4> sourceFoldTransforms(const SourceTubePreview& preview,double progress){
    if(!std::isfinite(progress))throw std::runtime_error("Non-finite source progress");
    progress=clamp(progress,0.,1.);
    std::vector<Mat4> matrices{Mat4{}};
    for(size_t i=0;i<preview.bends.size();++i){auto& b=preview.bends[i];double phase=clamp(progress*preview.bends.size()-i,0.,1.),angle=b.angle*phase;
        if(angle<1e-12){matrices.push_back(matrices.back());continue;}
        double radius=b.length/angle;Vec3 endpoint=b.start+Vec3{radius*std::sin(angle),0,0}-b.outward*(radius*2*std::pow(std::sin(angle/2),2));
        Mat4 joint=Mat4::translation(endpoint)*Mat4::axisAngle(b.axis,angle)*Mat4::translation(-(b.start+Vec3{b.length,0,0}));matrices.push_back(matrices.back()*joint);
    }return matrices;
}
Mesh sourceHingeMesh(const SourceBend& b,double phase){
    if(!std::isfinite(phase))throw std::runtime_error("Non-finite source hinge phase");
    phase=clamp(phase,0.,1.);double angle=b.angle*phase;
    const int count=32;Mesh mesh;mesh.pos.reserve((count+1)*12);mesh.idx.reserve(count*24+12);
    for(int i=0;i<=count;++i){double s=b.length*i/count,theta=angle*i/count;
        for(auto corner:std::vector<Vec2>{{-b.wall/2,-b.width/2},{b.wall/2,-b.width/2},{b.wall/2,b.width/2},{-b.wall/2,b.width/2}}){
            Vec3 p;
            if(angle<1e-12)p=b.start+Vec3{s,0,0}+b.outward*corner.x+b.widthAxis*corner.y;
            else{double radius=b.length/angle;double along=(radius+corner.x)*std::sin(theta);double normalOffset=corner.x*std::cos(theta)-radius*2*std::pow(std::sin(theta/2),2);p=b.start+Vec3{along,0,0}+b.outward*normalOffset+b.widthAxis*corner.y;}
            mesh.addVertex(p);
        }
    }
    for(int i=0;i<count;++i)for(int k=0;k<4;++k){u32 a=i*4+k,an=i*4+(k+1)%4,next=a+4,nn=an+4;mesh.addTri(a,an,nn);mesh.addTri(a,nn,next);}
    mesh.addTri(0,2,1);mesh.addTri(0,3,2);u32 n=count*4;mesh.addTri(n,n+1,n+2);mesh.addTri(n,n+2,n+3);meshOrientOutward(mesh);mesh.computeNormals();return mesh;
}
}
