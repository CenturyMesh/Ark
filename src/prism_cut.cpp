#include "ark/prism_cut.hpp"
#include "ark/mesh_analysis.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <mapbox/earcut.hpp>

namespace ark {
namespace {
using Loop=std::vector<Vec2>;
constexpr double eps=1e-7;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
double area(const Loop& loop){
    double sum=0;auto origin=loop.front();
    for(size_t i=0;i<loop.size();++i)sum+=(loop[i]-origin).cross(loop[(i+1)%loop.size()]-origin);
    return sum*.5;
}
Loop sampleLoop(const SketchProfile& profile){
    require(!profile.segs.empty()&&profile.segs.size()<=2048,"Prismatic cut profile has invalid segment count.");
    auto endpoint=[](const ProfileSeg& s,bool end){return s.type==ProfileSeg::Line?(end?s.p1:s.p0):s.c+Vec2{std::cos(end?s.a1:s.a0),std::sin(end?s.a1:s.a0)}*s.r;};
    Loop result;
    for(size_t i=0;i<profile.segs.size();++i){
        const auto& s=profile.segs[i];auto end=endpoint(s,true),next=endpoint(profile.segs[(i+1)%profile.segs.size()],false);
        double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,end.len(),next.len()});
        require((end-next).len()<=roundoff,"Prismatic cut profile segments do not form a closed continuous loop.");
        if(s.type==ProfileSeg::Line)result.push_back(s.p0);
        else {
            require(std::isfinite(s.r)&&s.r>0&&std::isfinite(s.a0)&&std::isfinite(s.a1),"Prismatic profile arc is invalid.");
            for(int j=0;j<48;++j){double t=lerp(s.a0,s.a1,(double)j/48);result.push_back(s.c+Vec2{std::cos(t),std::sin(t)}*s.r);}
        }
        require(result.size()<=2048,"Prismatic cut loop exceeds 2048 sampled vertices.");
    }
    return result;
}
double segmentDistance(Vec2 p,Vec2 a,Vec2 b){auto ab=b-a;return (p-(a+ab*clamp((p-a).dot(ab)/ab.lensq(),0.,1.))).len();}
bool intersect(Vec2 a,Vec2 b,Vec2 c,Vec2 d){
    if(std::max(a.x,b.x)+eps<std::min(c.x,d.x)||std::max(c.x,d.x)+eps<std::min(a.x,b.x)||
       std::max(a.y,b.y)+eps<std::min(c.y,d.y)||std::max(c.y,d.y)+eps<std::min(a.y,b.y))return false;
    auto sign=[](double v,double tolerance){return v>tolerance?1:v< -tolerance?-1:0;};
    double e1=eps*(b-a).len(),e2=eps*(d-c).len();
    return sign((b-a).cross(c-a),e1)*sign((b-a).cross(d-a),e1)<=0&&
           sign((d-c).cross(a-c),e2)*sign((d-c).cross(b-c),e2)<=0;
}
bool inside(Vec2 p,const Loop& loop){
    bool in=false;for(size_t i=0,j=loop.size()-1;i<loop.size();j=i++){
        auto a=loop[i],b=loop[j];if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)in=!in;
    }return in;
}
void validateLoop(const Loop& loop,double& clearance){
    require(loop.size()>=3&&loop.size()<=2048,"Prismatic cut loop must have 3..2048 sampled vertices.");
    for(auto p:loop)require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1e7&&std::abs(p.y)<=1e7,"Prismatic cut coordinates exceed finite bounds.");
    for(size_t i=0;i<loop.size();++i){
        auto a=loop[i],b=loop[(i+1)%loop.size()];double length=(b-a).len();
        require(length>eps,"Prismatic cut has a degenerate sampled edge.");clearance=std::min(clearance,length);
        // Adjacent edges may share one endpoint, but cannot backtrack over it.
        auto c=loop[(i+2)%loop.size()];auto ab=b-a,bc=c-b;
        require(!(std::abs(ab.cross(bc))<=eps*std::min(ab.len(),bc.len())&&ab.dot(bc)<0),"Prismatic cut profile backtracks over an adjacent edge.");
        for(size_t j=i+1;j<loop.size();++j){
            if(j==i+1||(i==0&&j+1==loop.size()))continue;
            auto c2=loop[j],d=loop[(j+1)%loop.size()];
            require(!intersect(a,b,c2,d),"Prismatic cut profile intersects or touches itself.");
            clearance=std::min({clearance,segmentDistance(a,c2,d),segmentDistance(b,c2,d),segmentDistance(c2,a,b),segmentDistance(d,a,b)});
        }
    }
    require(std::abs(area(loop))>1e-10,"Prismatic cut profile has zero area.");
}
void separate(const Loop& a,const Loop& b,double& clearance){
    for(size_t i=0;i<a.size();++i)for(size_t j=0;j<b.size();++j){
        auto a0=a[i],a1=a[(i+1)%a.size()],b0=b[j],b1=b[(j+1)%b.size()];
        require(!intersect(a0,a1,b0,b1),"Prismatic cut loops intersect or touch.");
        clearance=std::min({clearance,segmentDistance(a0,b0,b1),segmentDistance(a1,b0,b1),segmentDistance(b0,a0,a1),segmentDistance(b1,a0,a1)});
    }
}
bool rigid(const Mat4& frame){
    auto x=frame.transformDir({1,0,0}),y=frame.transformDir({0,1,0}),z=frame.transformDir({0,0,1}),p=frame.transformPoint({0,0,0});
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::abs(x.lensq()-1)<1e-9&&std::abs(y.lensq()-1)<1e-9&&std::abs(z.lensq()-1)<1e-9&&
        std::abs(x.dot(y))<1e-9&&std::abs(x.dot(z))<1e-9&&std::abs(y.dot(z))<1e-9&&x.cross(y).dot(z)>1-1e-9;
}
void retainBoundaryVertices(std::vector<u32>& triangles,const std::vector<Vec2>& points){
    // Earcut may omit collinear boundary vertices while eliminating bridges.
    // Walls must keep every supplied vertex. Split incident cap triangles at
    // those same original coordinates; never weld, move, or invent a point.
    std::vector<u32> regular;
    for(size_t i=0;i<triangles.size();i+=3){
        for(int j=0;j<3;++j)require(triangles[i+j]<points.size(),"Prismatic cap index out of bounds.");
        auto a=points[triangles[i]],b=points[triangles[i+1]],c=points[triangles[i+2]];
        auto ab=b-a,ac=c-a;double cross=ab.cross(ac);
        double rounding=64*std::numeric_limits<double>::epsilon()*std::max({1.,a.len(),b.len(),c.len()})*(ab.len()+ac.len());
        // A bridge may contain a numerically zero-area triangle after rigid
        // frame projection. Its boundary vertices are retained below; no
        // original point or edge is removed from the final cap/wall mesh.
        if(std::abs(cross)<=rounding)continue;
        regular.insert(regular.end(),triangles.begin()+i,triangles.begin()+i+3);
    }
    triangles=std::move(regular);
    std::vector<bool> used(points.size(),false);for(auto i:triangles)used[i]=true;
    for(u32 p=0;p<(u32)points.size();++p){
        bool inserted=false;size_t previous=triangles.size();
        for(size_t i=0;i<previous;i+=3)for(int edge=0;edge<3;++edge){
            auto a=triangles[i+edge],b=triangles[i+(edge+1)%3],c=triangles[i+(edge+2)%3];
            if(p==a||p==b||p==c)continue;
            auto ab=points[b]-points[a],ap=points[p]-points[a];double length=ab.len();
            double t=ap.dot(ab)/ab.lensq();
            double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,points[a].len(),points[b].len(),points[p].len()});
            if(t<=0||t>=1||std::abs(ab.cross(ap))>roundoff*length)continue;
            triangles[i]=a;triangles[i+1]=p;triangles[i+2]=c;
            triangles.push_back(p);triangles.push_back(b);triangles.push_back(c);inserted=true;break;
        }
        require(triangles.size()<=12*points.size(),"Prismatic cap refinement exceeded its bounded triangle budget.");
        require(used[p]||inserted,"Prismatic triangulation omitted a boundary vertex which cannot be preserved.");
    }
}
}

bool tryPrismaticCut(const Body& base,const std::vector<SketchProfile>& cutters,double depth,Body& out){
    if(base.kind!="extrude"&&base.kind!="prism_cut")return false;
    if(!rigid(base.profile.frame))return false;
    require(std::isfinite(depth)&&depth>0,"Prismatic cut depth must be positive and finite.");
    double height=base.pd1.x;require(std::isfinite(height)&&height>0,"Prismatic base thickness must be positive and finite.");
    require(!cutters.empty()&&cutters.size()+base.prismHoles.size()<=128,"Prismatic cut requires 1..128 total openings.");
    auto inverse=base.profile.frame.inverted();std::vector<Mat4> localFrames;
    // Check the entire operation's applicability before inspecting its polygons.
    for(auto& cutter:cutters){
        if(!rigid(cutter.frame))return false;
        auto local=inverse*cutter.frame;auto z=local.transformDir({0,0,1});
        if(std::abs(z.x)>1e-9||std::abs(z.y)>1e-9||std::abs(std::abs(z.z)-1)>1e-9)return false;
        double start=local.transformPoint({0,0,0}).z,end=start+depth*z.z;
        require(std::isfinite(start)&&std::isfinite(end),"Prismatic cut depth interval is not finite.");
        // Only floating-point transform roundoff, not a geometric cutting margin.
        double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,height,std::abs(start),std::abs(end)});
        if(std::min(start,end)>roundoff||std::max(start,end)<height-roundoff)return false;
        localFrames.push_back(local);
    }
    std::vector<Loop> loops;loops.push_back(sampleLoop(base.profile));
    loops.insert(loops.end(),base.prismHoles.begin(),base.prismHoles.end());
    for(size_t i=0;i<cutters.size();++i){
        auto loop=sampleLoop(cutters[i]);for(auto& point:loop){auto local=localFrames[i].transformPoint({point.x,point.y,0});point={local.x,local.y};}loops.push_back(std::move(loop));
    }
    size_t vertices=0;for(auto& loop:loops){vertices+=loop.size();require(vertices<=8192,"Prismatic cut exceeds 8192 total sampled vertices.");}
    double clearance=std::numeric_limits<double>::infinity();
    for(auto& loop:loops)validateLoop(loop,clearance);
    if(area(loops[0])<0)std::reverse(loops[0].begin(),loops[0].end());
    for(size_t i=1;i<loops.size();++i){
        separate(loops[0],loops[i],clearance);
        require(inside(loops[i].front(),loops[0]),"Prismatic opening lies outside the stock profile.");
        for(size_t j=1;j<i;++j){
            separate(loops[i],loops[j],clearance);
            require(!inside(loops[i].front(),loops[j])&&!inside(loops[j].front(),loops[i]),"Prismatic openings may not nest or duplicate existing holes.");
        }
        if(area(loops[i])>0)std::reverse(loops[i].begin(),loops[i].end());
    }
    std::vector<std::vector<std::array<double,2>>> polygon;std::vector<Vec2> points;
    double netArea=0,perimeter=0;
    for(auto& loop:loops){
        polygon.emplace_back();netArea+=area(loop);
        for(size_t i=0;i<loop.size();++i){auto p=loop[i];polygon.back().push_back({p.x,p.y});points.push_back(p);perimeter+=(loop[(i+1)%loop.size()]-p).len();}
    }
    require(netArea>1e-10,"Prismatic openings leave no stock area.");
    auto caps=mapbox::earcut<u32>(polygon);
    require(!caps.empty()&&caps.size()%3==0,"Prismatic cap triangulation failed.");
    retainBoundaryVertices(caps,points);
    double capArea=0;
    for(size_t i=0;i<caps.size();i+=3){
        auto a=caps[i],b=caps[i+1],c=caps[i+2];require(a<points.size()&&b<points.size()&&c<points.size(),"Prismatic cap contains an invalid index.");
        double twice=(points[b]-points[a]).cross(points[c]-points[a]);
        require(std::isfinite(twice)&&std::abs(twice)>1e-16,"Prismatic cap contains a degenerate triangle.");
        if(twice<0)std::swap(caps[i+1],caps[i+2]);
        capArea+=std::abs(twice)*.5;
    }
    require(std::abs(capArea-netArea)<=std::max(1e-9,netArea*1e-10),"Prismatic cap area disagrees with the independent polygon area.");
    Mesh mesh;mesh.pos.reserve(points.size()*6);mesh.idx.reserve(caps.size()*2+points.size()*6);
    double quantization=0;
    for(double z:{0.,height})for(auto p:points){
        auto exact=base.profile.frame.transformPoint({p.x,p.y,z});auto index=mesh.vertCount();mesh.addVertex(exact);
        Vec3 stored{mesh.pos[index*3],mesh.pos[index*3+1],mesh.pos[index*3+2]};quantization=std::max(quantization,(stored-exact).len());
    }
    require(quantization*8<clearance,"Prismatic features are too small for float mesh precision at this placement.");
    auto n=(u32)points.size();
    for(size_t i=0;i<caps.size();i+=3){auto a=caps[i],b=caps[i+1],c=caps[i+2];mesh.addTri(a,c,b);mesh.addTri(a+n,b+n,c+n);}
    u32 offset=0;
    for(auto& loop:loops){
        for(u32 i=0;i<(u32)loop.size();++i){auto a=offset+i,b=offset+(i+1)%(u32)loop.size();mesh.addTri(a,b,b+n);mesh.addTri(a,b+n,a+n);}offset+=(u32)loop.size();
    }
    auto report=analyzeMesh(mesh);
    if(!report.closedOriented()||report.components!=1)throw std::runtime_error("Prismatic cut failed mesh topology validation (boundary="+std::to_string(report.boundaryEdges)+", nonmanifold="+std::to_string(report.nonManifoldEdges)+", winding="+std::to_string(report.inconsistentEdges)+", degenerate="+std::to_string(report.degenerateTriangles)+").");
    double expected=netArea*height;
    // Volume rounding bound derives from measured f64->f32 vertex displacement
    // and analytic surface area. No fixture-dependent tolerance or healing.
    double volumeTolerance=std::max(1e-9,expected*2e-10)+4*quantization*(2*netArea+perimeter*height);
    require(report.signedVolume>0&&std::abs(report.signedVolume-expected)<=volumeTolerance,"Prismatic cut volume disagrees with polygon area times thickness.");
    mesh.computeNormals();Body result;result.kind="prism_cut";result.profile=base.profile;result.pd1=base.pd1;
    result.prismHoles.assign(loops.begin()+1,loops.end());result.baked=std::make_unique<Mesh>(std::move(mesh));out=std::move(result);return true;
}
}
