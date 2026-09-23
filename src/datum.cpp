#include "ark/sketch.hpp"
#include "ark/references.hpp"
#include <functional>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
double scalar(const V& v,const ExprEnv& env) {
    bool ok=v.t==V::Num; double n=ok?v.num:v.t==V::Str?evalExpr(v.str,env,&ok):0;
    if(!ok||!std::isfinite(n)||std::abs(n)>1e7)throw std::runtime_error("Invalid datum expression");
    return n;
}
std::string text(const V& v,const char* key) {
    auto p=v.find(key);if(!p||p->t!=V::Str||p->str.empty())throw std::runtime_error(std::string("Missing datum ")+key);return p->str;
}
u64 identifier(const V& v,const char* key) {
    auto p=v.find(key);if(!p||p->t!=V::Num||p->num<1||p->num>9007199254740990.||p->num!=std::floor(p->num))throw std::runtime_error("Invalid datum producer");return (u64)p->num;
}
Mat4 axes(Vec3 origin,Vec3 x,Vec3 y,Vec3 z) {
    Mat4 m=Mat4::identity();for(int j=0;j<3;++j){auto v=j==0?x:j==1?y:z;m.m[j*4]=v.x;m.m[j*4+1]=v.y;m.m[j*4+2]=v.z;}
    m.m[12]=origin.x;m.m[13]=origin.y;m.m[14]=origin.z;return m;
}
// Evaluate the authored piecewise line/circle centerline analytically. No
// sampled mesh, tessellation index, or nearest-triangle projection is used.
Mat4 tubeStation(const Document& doc,u64 id,double station) {
    const Feature* source=nullptr;for(auto& f:doc.features)if(f.id==id)source=&f;
    if(!source||source->type!=FeatureType::Tube||station<0)throw std::runtime_error("tube_path needs an authored tube and nonnegative station");
    if(doc.evaluatingBodies){bool ready=false;for(auto& b:*doc.evaluatingBodies)ready|=b.id==id;if(!ready)throw std::runtime_error("Datum support must precede its consuming feature");}
    const auto env=doc.buildEnv();auto n=[&](const std::string& s){return scalar(V::makeStr(s),env);};
    Vec3 p{},t{1,0,0},normal{0,1,0},bn=normal;
    double at=0;
    auto result=[&]{return axes(p,normal,t.cross(normal).normalized(),t);};
    auto line=[&](double length){if(station<=at+length){p+=t*(station-at);return true;}p+=t*length;at+=length;return false;};
    for(auto& bend:source->bends){
        const double feed=n(bend.feed),r=n(bend.radius),a=n(bend.angle),roll=n(bend.roll);
        if(feed<0||r<=0||std::abs(a)<.001||std::abs(a)>180)throw std::runtime_error("Invalid authored tube path");
        if(line(feed))return result();
        bn=Mat4::axisAngle(t,deg2rad(roll)).transformDir(bn).normalized();
        const Vec3 inward=bn*(a<0?-1.:1.),axis=t.cross(inward).normalized(),center=p+inward*r;
        const double target=deg2rad(std::abs(a)),angle=std::min(target,(station-at)/r);
        const Mat4 rotation=Mat4::axisAngle(axis,angle);
        p=center+(t*std::sin(angle)-inward*std::cos(angle))*r;
        t=rotation.transformDir(t).normalized();normal=rotation.transformDir(normal).normalized();
        if(station<=at+r*target)return result();
        bn=rotation.transformDir(bn).normalized();at+=r*target;
    }
    if(line(n(source->get("tail","100"))))return result();
    throw std::runtime_error("Datum station exceeds tube centerline length");
}
}
Mat4 resolvePlane(const Document& doc,const std::string& name) {
    const auto env=doc.buildEnv();std::set<std::string> visiting;
    std::function<Mat4(const std::string&)> resolve=[&](const std::string& key)->Mat4 {
        if(key=="XY")return Mat4::identity();
        if(key=="XZ")return Mat4::rotX(deg2rad(90));
        if(key=="YZ")return Mat4::rotZ(K_HALF_PI)*Mat4::rotX(K_HALF_PI);
        if(!visiting.insert(key).second)throw std::runtime_error("Cyclic construction datum: "+key);
        const DatumPlane* datum=nullptr;for(auto& p:doc.planes)if(p.name==key)datum=&p;
        if(!datum)throw std::runtime_error("Construction datum not found: "+key);
        if(datum->kind!="plane"&&datum->kind!="axis"&&datum->kind!="frame")throw std::runtime_error("Datum kind must be plane, axis or frame");
        for(auto& kv:datum->p)if(kv.first!="ox"&&kv.first!="oy"&&kv.first!="oz"&&kv.first!="rx"&&kv.first!="ry"&&kv.first!="rz")throw std::runtime_error("Unknown datum parameter: "+kv.first);
        Mat4 base;
        if(datum->support.t==V::Obj){
            const auto& s=datum->support;auto kind=text(s,"kind");auto feature=identifier(s,"feature");
            if(kind=="tube_path") {auto station=s.find("station");if(!station)throw std::runtime_error("tube_path needs station");base=tubeStation(doc,feature,scalar(*station,env));}
            else {
                double fraction=.5;if(auto p=s.find("fraction"))fraction=scalar(*p,env);
                auto first=resolveGeometryReference(doc,feature,text(s,"reference"),fraction);base=first.frame;
                if(kind=="midplane"){
                    auto other=s.find("second_feature")?identifier(s,"second_feature"):feature;
                    auto second=resolveGeometryReference(doc,other,text(s,"second_reference"),fraction);
                    Vec3 a=base.transformDir({0,0,1}),b=second.frame.transformDir({0,0,1});
                    if(first.kind!="face"||second.kind!="face"||std::abs(std::abs(a.dot(b))-1)>1e-10)throw std::runtime_error("Midplane needs two parallel planar face references");
                    Vec3 p=base.transformPoint({}),q=second.frame.transformPoint({});
                    auto mid=p+a*((q-p).dot(a)*.5);base.m[12]=mid.x;base.m[13]=mid.y;base.m[14]=mid.z;
                } else if(kind=="face") {if(first.kind!="face")throw std::runtime_error("Face datum requires a planar face");}
                else if(kind=="normal_to_curve") {if(first.kind!="edge"&&first.kind!="axis")throw std::runtime_error("Normal datum requires an edge or axis");}
                else throw std::runtime_error("Unsupported construction datum support kind");
            }
        }else base=resolve(datum->parent);
        auto value=[&](const char* k){auto it=datum->p.find(k);return it==datum->p.end()?0.:scalar(V::makeStr(it->second),env);};
        auto frame=base*Mat4::translation({value("ox"),value("oy"),value("oz")})*
            Mat4::rotZ(deg2rad(value("rz")))*Mat4::rotY(deg2rad(value("ry")))*Mat4::rotX(deg2rad(value("rx")));
        visiting.erase(key);return frame;
    };return resolve(name);
}
}
