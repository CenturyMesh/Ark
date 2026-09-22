#include "ark/notch.hpp"
#include "ark/model.hpp"
#include "ark/mesh_analysis.hpp"
#include <map>
#include <array>
#include <stdexcept>
namespace ark {
namespace {
using Polygon=std::vector<Vec3>;using Polygons=std::vector<Polygon>;
struct Halfspace{Vec3 n;double d;};
constexpr double clipTolerance=1e-9;
Vec3 normal(const Polygon& p){for(size_t i=1;i+1<p.size();++i){auto n=(p[i]-p[0]).cross(p[i+1]-p[0]);if(n.len()>1e-12)return n.normalized();}return {};}
Polygon clip(const Polygon& p,const Halfspace& h,bool inside){
    Polygon out;if(p.empty())return out;bool any=false;
    for(auto v:p){double d=h.n.dot(v)-h.d;any|=inside?d<=clipTolerance:d>clipTolerance;}
    if(!any)return out;
    auto keep=[&](double d){return inside?d<=clipTolerance:d>=-clipTolerance;};
    Vec3 a=p.back();double da=h.n.dot(a)-h.d;bool ka=keep(da);
    for(auto b:p){double db=h.n.dot(b)-h.d;bool kb=keep(db);
        if(ka!=kb){double t=da/(da-db);out.push_back(a+(b-a)*t);}if(kb)out.push_back(b);
        a=b;da=db;ka=kb;
    }
    Polygon clean;for(auto v:out)if(clean.empty()||(v-clean.back()).len()>clipTolerance)clean.push_back(v);
    if(clean.size()>1&&(clean.front()-clean.back()).len()<clipTolerance)clean.pop_back();
    if(clean.size()<3||normal(clean).len()<.5)return {};
    return clean;
}
Polygons subtract(const Polygon& p,const std::vector<Halfspace>& planes,bool keepOpposite=false){
    if(keepOpposite)for(auto& h:planes){bool same=true;for(auto v:p)same&=std::fabs(h.n.dot(v)-h.d)<=clipTolerance;
        if(same&&normal(p).dot(h.n)<-.99)return {p};}
    Polygon rest=p;Polygons outside;
    for(auto& h:planes){auto part=clip(rest,h,false);if(!part.empty())outside.push_back(std::move(part));rest=clip(rest,h,true);if(rest.empty())break;}
    return outside;
}
std::vector<Halfspace> boxPlanes(Vec3 lo,Vec3 hi){return {{{-1,0,0},-lo.x},{{1,0,0},hi.x},{{0,-1,0},-lo.y},{{0,1,0},hi.y},{{0,0,-1},-lo.z},{{0,0,1},hi.z}};}
Vec3 vec(const arkjson::Value& p){return {p.arr[0].num,p.arr[1].num,p.arr[2].num};}
Polygons prism(const arkjson::Value& j){
    std::vector<Vec3> p;for(auto& v:j.find("cutter_polygon")->arr)p.push_back(vec(v));Vec3 delta=vec(*j.find("cutter_extrusion"));
    for(size_t i=0;i<4;++i)p.push_back(p[i]+delta);
    Polygons faces={{p[0],p[1],p[2],p[3]},{p[4],p[5],p[6],p[7]}};
    for(int i=0;i<4;++i)faces.push_back({p[i],p[(i+1)%4],p[(i+1)%4+4],p[i+4]});
    Vec3 center;for(auto v:p)center+=v;center*=1./8;
    for(auto& f:faces)if(normal(f).dot(f[0]-center)<0)std::reverse(f.begin(),f.end());
    return faces;
}
Mesh mesh(const Polygons& polygons){
    // Insert all coplanar edge split points before triangulation, preserving
    // shared topology without the legacy mesh CSG's relative healing tolerance.
    Mesh m;std::vector<Vec3> vertices;std::map<std::array<long long,3>,u32> ids;
    auto vertex=[&](Vec3 p){std::array<long long,3> key{std::llround(p.x*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)};auto found=ids.find(key);if(found!=ids.end())return found->second;
        u32 id=(u32)vertices.size();vertices.push_back(p);ids[key]=id;m.addVertex(p);return id;};
    for(auto& p:polygons)for(auto v:p)vertex(v);
    size_t edgeVertices=vertices.size();
    for(auto& polygon:polygons){if(polygon.size()<3)continue;Polygon refined;
        for(size_t i=0;i<polygon.size();++i){Vec3 a=polygon[i],b=polygon[(i+1)%polygon.size()],edge=b-a;double length2=edge.lensq();if(length2<1e-18)continue;
            std::vector<std::pair<double,Vec3>> splits{{0,a}};
            for(size_t k=0;k<edgeVertices;++k){Vec3 p=vertices[k];double t=(p-a).dot(edge)/length2;
                if(t>1e-9&&t<1-1e-9&&(p-a-edge*t).len()<1e-8)splits.push_back({t,p});}
            std::sort(splits.begin(),splits.end(),[](auto& a,auto& b){return a.first<b.first;});
            for(auto& s:splits)if(refined.empty()||(s.second-refined.back()).len()>1e-8)refined.push_back(s.second);
        }
        if(refined.size()<3)continue;
        Vec3 center;for(auto v:refined)center+=v;center*=1./refined.size();u32 c=vertex(center);
        for(size_t i=0;i<refined.size();++i){u32 a=vertex(refined[i]),b=vertex(refined[(i+1)%refined.size()]);
            if(a!=b&&(refined[i]-center).cross(refined[(i+1)%refined.size()]-center).len()>1e-12)m.addTri(c,a,b);}
    }
    m.computeNormals();return m;
}
}
arkjson::Value notchRequestForFeature(const Feature& f,const ExprEnv& env){
    using V=arkjson::Value;V request=V::makeObj(),list=V::makeArr();
    const std::pair<const char*,double> defaults[]={{"width_mm",40},{"height_mm",30},{"wall_mm",2},{"length_mm",600},{"hinge_offset_mm",1}};
    for(auto& kv:defaults){bool ok=true;auto p=f.p.find(kv.first);double n=p==f.p.end()?kv.second:evalExpr(p->second,env,&ok);if(!ok)throw std::runtime_error(std::string("Invalid stock expression: ")+kv.first);request.set(kv.first,V::makeNum(n));}
    for(auto& point:f.notches){bool a=false,b=false;double station=evalExpr(point.station,env,&a),angle=evalExpr(point.angle,env,&b);if(!a||!b)throw std::runtime_error("Invalid bend point expression");
        V j=V::makeObj();j.set("station_mm",V::makeNum(station));j.set("angle_deg",V::makeNum(angle));j.set("hinge_face",V::makeStr(point.face));list.arr.push_back(j);}
    request.set("notches",list);return request;
}
NotchPreview buildNotchPreview(const arkjson::Value& request){
    NotchPreview result;result.plan=planNotchedBlank(request);auto& stock=*result.plan.find("stock");
    double w=stock.find("width_mm")->num,h=stock.find("height_mm")->num,t=stock.find("wall_mm")->num,l=stock.find("length_mm")->num;
    std::vector<Vec3> o={{0,0,0},{0,h,0},{0,h,w},{0,0,w}},in={{0,t,t},{0,h-t,t},{0,h-t,w-t},{0,t,w-t}};
    Polygons boundary;Vec3 end{l,0,0};
    for(int i=0;i<4;++i){int next=(i+1)%4;
        boundary.push_back({o[i],o[next],o[next]+end,o[i]+end});
        boundary.push_back({in[i],in[i]+end,in[next]+end,in[next]});
        boundary.push_back({o[i],in[i],in[next],o[next]});
        boundary.push_back({o[i]+end,o[next]+end,in[next]+end,in[i]+end});
    }
    auto outer=boxPlanes({0,0,0},{l,h,w}),cavity=boxPlanes({-1,t,t},{l+1,h-t,w-t});
    std::vector<Polygon> hingeCaps;
    for(auto& j:result.plan.find("notches")->arr){
        auto faces=prism(j);std::vector<Halfspace> planes;for(auto& face:faces){auto n=normal(face);planes.push_back({n,n.dot(face[0])});}
        Polygons next;for(auto& face:boundary){auto pieces=subtract(face,planes,true);next.insert(next.end(),pieces.begin(),pieces.end());}
        for(auto face:faces){for(auto& p:outer)face=clip(face,p,true);if(face.empty())continue;
            auto pieces=subtract(face,cavity);for(auto p:pieces){std::reverse(p.begin(),p.end());next.push_back(std::move(p));}}
        boundary=std::move(next);
        double station=j.find("station_mm")->num;result.stations.push_back(station);result.angles.push_back(deg2rad(j.find("angle_deg")->num));
        result.hingePoints.push_back(vec(*j.find("hinge_axis_point")));result.hingeAxes.push_back(vec(*j.find("hinge_axis_direction")));
        Mat4 frame;for(int i=0;i<16;++i)frame.m[i]=j.find("frame_column_major")->arr[i].num;
        bool side=j.find("hinge_face")->str=="left"||j.find("hinge_face")->str=="right";double breadth=side?h:w;
        Polygon cap;for(auto p:std::vector<Vec3>{{station,0,0},{station,t,0},{station,t,breadth},{station,0,breadth}})cap.push_back(frame.transformPoint(p));hingeCaps.push_back(cap);
    }
    result.straight=mesh(boundary);
    for(size_t i=0;i<=result.stations.size();++i){Polygons pieces;
        for(auto face:boundary){if(i>0)face=clip(face,{{-1,0,0},-result.stations[i-1]},true);if(i<result.stations.size())face=clip(face,{{1,0,0},result.stations[i]},true);if(!face.empty())pieces.push_back(face);}
        if(i>0){auto cap=hingeCaps[i-1];std::reverse(cap.begin(),cap.end());pieces.push_back(cap);}if(i<hingeCaps.size())pieces.push_back(hingeCaps[i]);
        result.segments.push_back(mesh(pieces));
    }
    auto audit=analyzeMesh(result.straight);double expected=result.plan.find("validation")->find("remaining_volume_mm3")->num;
    if(!audit.closedOriented()||std::fabs(audit.signedVolume-expected)>std::max(1e-4,expected*1e-6))throw std::runtime_error("Straight preview failed topology/float mesh volume checks");
    for(auto& segment:result.segments)if(!analyzeMesh(segment).closedOriented())throw std::runtime_error("Bend segment preview failed topology checks");
    return result;
}
std::vector<Mat4> notchFoldTransforms(const NotchPreview& preview,double progress){
    if(!std::isfinite(progress))throw std::runtime_error("Fold progress must be finite");
    progress=clamp(progress,0.,1.);
    std::vector<Mat4> transforms{Mat4{}};size_t count=preview.stations.size();
    for(size_t i=0;i<count;++i){double phase=clamp(progress*count-i,0.,1.);Vec3 p=preview.hingePoints[i];
        transforms.push_back(transforms.back()*Mat4::translation(p)*Mat4::axisAngle(preview.hingeAxes[i],preview.angles[i]*phase)*Mat4::translation(-p));}
    return transforms;
}
}
