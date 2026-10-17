#include "ark/sketch.hpp"
#include "ark/references.hpp"
#include "ark/datum_authoring.hpp"
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
Vec3 unit(Vec3 v,const char* what) {
    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||v.len()<=K_TOL)
        throw std::runtime_error(std::string("Degenerate construction datum: ")+what);
    return v.normalized();
}
Mat4 normalFrame(Vec3 origin,Vec3 direction,Vec3 guide={}) {
    Vec3 z=unit(direction,"zero axis/normal");
    if(guide.lensq()==0)guide=std::abs(z.x)<=std::abs(z.y)&&std::abs(z.x)<=std::abs(z.z)?Vec3{1,0,0}:
        std::abs(z.y)<=std::abs(z.z)?Vec3{0,1,0}:Vec3{0,0,1};
    Vec3 x=unit(guide-z*guide.dot(z),"guide parallel to axis");
    return axes(origin,x,unit(z.cross(x),"invalid frame"),z);
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
static Mat4 resolveDatumFrame(const Document& doc,const std::string& name,const V* anchor=nullptr) {
    const auto env=doc.buildEnv();std::set<std::string> visiting;
    std::function<Mat4(const std::string&)> resolve;
    std::function<Vec3(const V&)> point;
    auto field=[](const V& value,const char* key)->const V&{auto p=value.find(key);if(!p)throw std::runtime_error(std::string("Missing datum ")+key);return *p;};
    auto namedKind=[&](const std::string& key){for(const auto& d:doc.planes)if(d.name==key)return d.kind;if(key=="Origin")return std::string("point");if(key=="XY"||key=="XZ"||key=="YZ")return std::string("plane");throw std::runtime_error("Construction datum not found: "+key);};
    auto reference=[&](const V& anchor)->GeometryReference{
        double fraction=.5;
        if(auto p=anchor.find("fraction"))fraction=scalar(*p,env);
        if(auto p=anchor.find("endpoint"))fraction=p->str=="start"?0.:1.;
        return resolveGeometryReference(doc,identifier(anchor,"feature"),text(anchor,"reference"),fraction);
    };
    auto plane=[&](const V& anchor)->Mat4 {
        if(anchor.t==V::Str){auto kind=namedKind(anchor.str);if(kind!="plane"&&kind!="frame")throw std::runtime_error("Plane support requires a plane or frame");return resolve(anchor.str);}
        if(auto p=anchor.find("datum")){auto kind=namedKind(p->str);if(kind!="plane"&&kind!="frame")throw std::runtime_error("Plane support requires a plane or frame");return resolve(p->str);}
        auto result=reference(anchor);if(result.kind!="face")throw std::runtime_error("Plane support requires an exact planar face");return result.frame;
    };
    auto axis=[&](const V& anchor)->Mat4 {
        const std::string key=anchor.t==V::Str?anchor.str:anchor.find("datum")?text(anchor,"datum"):"";
        if(!key.empty()){
            const auto kind=namedKind(key);auto f=resolve(key);
            if(auto a=anchor.find("axis")){Vec3 d=a->str=="x"?Vec3{1,0,0}:a->str=="y"?Vec3{0,1,0}:Vec3{0,0,1};return normalFrame(f.transformPoint({}),f.transformDir(d));}
            if(kind!="axis"&&kind!="frame")throw std::runtime_error("Axis support requires an axis/frame or an explicit datum axis");
            return f;
        }
        auto result=reference(anchor);if(result.kind!="axis"&&result.kind!="edge")throw std::runtime_error("Axis support requires an exact edge or axis");return result.frame;
    };
    point=[&](const V& anchor)->Vec3{
        auto local=[&]()->Vec3{auto p=anchor.find("local");return p?Vec3{scalar(p->arr[0],env),scalar(p->arr[1],env),scalar(p->arr[2],env)}:Vec3{};};
        if(anchor.t==V::Arr)return {scalar(anchor.arr[0],env),scalar(anchor.arr[1],env),scalar(anchor.arr[2],env)};
        if(anchor.t==V::Str)return resolve(anchor.str).transformPoint({});
        if(auto p=anchor.find("datum"))return resolve(p->str).transformPoint(local());
        if(anchor.find("feature")){
            auto result=reference(anchor);if(anchor.find("endpoint")&&result.kind!="edge"&&result.kind!="axis")throw std::runtime_error("Endpoint anchor requires an exact curve reference");
            return result.frame.transformPoint(local());
        }
        const auto owner=text(anchor,"sketch"),entity=text(anchor,"entity");
        // The sketch plane may depend on this very datum. Retain the same
        // recursive resolver so this cross-resource cycle fails explicitly.
        const auto* source=static_cast<const NamedSketch*>(nullptr);for(const auto& sk:doc.sketches)if(sk.name==owner)source=&sk;
        if(!source)throw std::runtime_error("Datum source sketch not found: "+owner);
        const Mat4 frame=resolve(source->sk.plane);
        auto evaluated=*source;evaluateSketch(evaluated,env);
        if(!evaluated.error.empty())throw std::runtime_error("Datum source sketch invalid: "+evaluated.error);
        const SketchEnt* selected=nullptr;for(const auto& e:evaluated.sk.ents)if(e.name==entity){if(selected)throw std::runtime_error("Ambiguous datum sketch entity");selected=&e;}
        if(!selected)throw std::runtime_error("Datum source sketch entity not found: "+entity);
        const auto& e=*selected;Vec2 p{};const bool end=text(anchor,"endpoint")=="end";
        if(e.kind==SketchEnt::Pnt){if(e.dof.size()!=2)throw std::runtime_error("Invalid sketch point");p={e.dof[0],e.dof[1]};}
        else if(e.kind==SketchEnt::Arc){if(e.dof.size()<5)throw std::runtime_error("Invalid sketch arc");double a=deg2rad(e.dof[end?4:3]);p={e.dof[0]+e.dof[2]*std::cos(a),e.dof[1]+e.dof[2]*std::sin(a)};}
        else if(e.kind==SketchEnt::Line||e.kind==SketchEnt::Spline||e.kind==SketchEnt::Wire){
            if(e.closed||e.dof.size()<4)throw std::runtime_error("Closed curves have no endpoint anchors");
            if(e.kind==SketchEnt::Wire){auto curves=sketchPresentedCurves(evaluated.sk,e);if(curves.empty())throw std::runtime_error("Invalid sketch wire");if(curves.front().a.approx(curves.back().b,K_TOL))throw std::runtime_error("Closed curves have no endpoint anchors");p=end?curves.back().b:curves.front().a;}
            else {const auto i=end?e.dof.size()-2:0;if(Vec2{e.dof[0],e.dof[1]}.approx({e.dof[e.dof.size()-2],e.dof.back()},K_TOL))throw std::runtime_error("Coincident curve endpoints do not define an open endpoint source");p={e.dof[i],e.dof[i+1]};}
        }else throw std::runtime_error("Datum endpoint needs an authored open line, wire, spline, arc or point");
        return frame.transformPoint({p.x,p.y,0});
    };
    resolve=[&](const std::string& key)->Mat4 {
        const DatumPlane* datum=nullptr;for(auto& p:doc.planes)if(p.name==key)datum=&p;
        if(!datum){
            if(key=="Origin"||key=="XY")return Mat4::identity();
            if(key=="XZ")return Mat4::rotX(deg2rad(90));
            if(key=="YZ")return Mat4::rotZ(K_HALF_PI)*Mat4::rotX(K_HALF_PI);
        }
        if(!visiting.insert(key).second)throw std::runtime_error("Cyclic construction datum: "+key);
        if(!datum)throw std::runtime_error("Construction datum not found: "+key);
        validateDatumDefinition(*datum);
        if(datum->kind!="plane"&&datum->kind!="axis"&&datum->kind!="frame"&&datum->kind!="point")throw std::runtime_error("Datum kind must be plane, axis, frame or point");
        for(auto& kv:datum->p)if(kv.first!="ox"&&kv.first!="oy"&&kv.first!="oz"&&kv.first!="rx"&&kv.first!="ry"&&kv.first!="rz")throw std::runtime_error("Unknown datum parameter: "+kv.first);
        Mat4 base;
        if(datum->support.t==V::Obj){
            const auto& s=datum->support;auto kind=text(s,"kind");
            if(kind=="offset_plane")base=plane(field(s,"plane"))*Mat4::translation({0,0,scalar(field(s,"offset"),env)});
            else if(kind=="angle_plane"){
                auto f=plane(field(s,"plane")),a=axis(field(s,"axis"));const auto n=f.transformDir({0,0,1}),o=a.transformPoint({}),d=a.transformDir({0,0,1});
                if(std::abs(n.dot(d))>K_TOL||std::abs((o-f.transformPoint({})).dot(n))>K_TOL)throw std::runtime_error("Angle-plane axis must lie in the source plane");
                base=Mat4::translation(o)*Mat4::axisAngle(d,deg2rad(scalar(field(s,"angle"),env)))*Mat4::translation(-o)*f;
            }
            else if(kind=="plane_three_points"){
                auto a=point(field(s,"a")),b=point(field(s,"b")),c=point(field(s,"c"));const auto x=unit(b-a,"coincident plane points"),ac=unit(c-a,"coincident plane points");
                auto z=unit(x.cross(ac),"collinear plane points");base=axes(a,x,z.cross(x),z);
            }
            else if(kind=="plane_axis_point"){
                auto a=axis(field(s,"axis"));auto o=a.transformPoint({}),z=a.transformDir({0,0,1}),p=point(field(s,"point"));
                auto delta=unit(p-o,"point on plane axis");auto n=unit(z.cross(delta),"point on plane axis");base=axes(o,z,n.cross(z),n);
            }
            else if(kind=="plane_point_normal"){
                auto a=axis(field(s,"axis"));base=normalFrame(point(field(s,"point")),a.transformDir({0,0,1}),a.transformDir({1,0,0}));
            }
            else if(kind=="axis_points"||kind=="between_points"){
                auto a=point(field(s,"start")),b=point(field(s,"end"));double fraction=0;if(auto p=s.find("fraction"))fraction=scalar(*p,env);
                if(fraction<0||fraction>1)throw std::runtime_error("Datum fraction must be 0..1");
                Vec3 guide{};if(auto p=s.find("x_axis"))guide=axis(*p).transformDir({0,0,1});
                base=normalFrame(a+(b-a)*fraction,b-a,guide);if(auto p=s.find("roll"))base=base*Mat4::rotZ(deg2rad(scalar(*p,env)));
            }
            else if(kind=="axis_planes"){
                auto a=plane(field(s,"first")),b=plane(field(s,"second"));auto n=a.transformDir({0,0,1}),m=b.transformDir({0,0,1});
                auto cross=n.cross(m);unit(cross,"parallel intersection planes");auto p=a.transformPoint({}),q=b.transformPoint({});
                auto origin=(m.cross(cross)*n.dot(p)+cross.cross(n)*m.dot(q))/cross.lensq();base=normalFrame(origin,cross);
            }
            else if(kind=="bisector"){
                auto a=plane(field(s,"first")),b=plane(field(s,"second"));Vec3 n=a.transformDir({0,0,1}),m=b.transformDir({0,0,1}),p=a.transformPoint({}),q=b.transformPoint({});
                std::string side="sum";if(auto chosen=s.find("side")){if(chosen->t!=V::Str||(chosen->str!="sum"&&chosen->str!="difference"))throw std::runtime_error("Bisector side must be sum or difference");side=chosen->str;}
                // Parallel result keeps the first normal. An opposing second normal is not flipped; difference negates the result.
                if(std::abs(std::abs(n.dot(m))-1)<=1e-8){
                    auto mid=p+n*((q-p).dot(n)*.5);Vec3 normal=side=="difference"?-n:n;Vec3 guide=a.transformDir({1,0,0});
                    if(std::abs(guide.dot(normal))>0.999)guide=a.transformDir({0,1,0});base=normalFrame(mid,normal,guide);
                }else{
                    auto cross=n.cross(m);auto dir=unit(cross,"parallel intersection planes");auto origin=(m.cross(cross)*n.dot(p)+cross.cross(n)*m.dot(q))/cross.lensq();
                    Vec3 raw=side=="difference"?n-m:n+m;if(raw.len()<=K_TOL)throw std::runtime_error("Degenerate construction datum: bisector");base=normalFrame(origin,raw,dir);
                }
            }
            else if(kind=="point")base=Mat4::translation(point(field(s,"point")));
            else if(kind=="tube_path") {auto feature=identifier(s,"feature");auto station=s.find("station");if(!station)throw std::runtime_error("tube_path needs station");base=tubeStation(doc,feature,scalar(*station,env));}
            else {
                auto feature=identifier(s,"feature");
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
                else if(kind=="on_curve") {if(first.kind!="edge"&&first.kind!="axis")throw std::runtime_error("on_curve needs an edge or axis");}
                else if(kind=="on_face") {
                    if(first.kind!="face")throw std::runtime_error("on_face needs a face");
                    double u=0,v=0;if(auto p=s.find("u"))u=scalar(*p,env);if(auto p=s.find("v"))v=scalar(*p,env);
                    base=base*Mat4::translation({u,v,0});
                }
                else throw std::runtime_error("Unsupported construction datum support kind");
            }
        }else base=resolve(datum->parent);
        auto value=[&](const char* k){auto it=datum->p.find(k);return it==datum->p.end()?0.:scalar(V::makeStr(it->second),env);};
        auto frame=base*Mat4::translation({value("ox"),value("oy"),value("oz")})*
            Mat4::rotZ(deg2rad(value("rz")))*Mat4::rotY(deg2rad(value("ry")))*Mat4::rotX(deg2rad(value("rx")));
        for(double value:frame.m)if(!std::isfinite(value)||std::abs(value)>1e7)throw std::runtime_error("Construction datum exceeds finite coordinate bounds");
        visiting.erase(key);return frame;
    };return anchor?Mat4::translation(point(*anchor)):resolve(name);
}
Mat4 resolvePlane(const Document& doc,const std::string& name) {return resolveDatumFrame(doc,name);}
Vec3 datumAnchorPoint(const Document& doc,const V& anchor) {
    DatumPlane check;check.name="__anchor_validation";check.kind="point";check.support=V::makeObj();
    check.support.set("kind",V::makeStr("point"));check.support.set("point",anchor);validateDatumDefinition(check);
    auto p=resolveDatumFrame(doc,"",&anchor).transformPoint({});
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1e7)throw std::runtime_error("Datum point anchor exceeds finite coordinate bounds");
    return p;
}
}
