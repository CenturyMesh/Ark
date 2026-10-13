#include "ark/sketch.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
struct Schema { const char* json; };
const std::map<std::string,Schema>& schemas(){
    static const std::map<std::string,Schema> result={
        {"corner_rectangle",{R"({"type":"corner_rectangle","first":[0,0],"second":[60,40]})"}},
        {"center_rectangle",{R"({"type":"center_rectangle","center":[0,0],"corner":[30,20]})"}},
        {"three_point_rectangle",{R"({"type":"three_point_rectangle","first":[0,0],"second":[60,0],"third":[60,40]})"}},
        {"three_point_center_rectangle",{R"({"type":"three_point_center_rectangle","center":[0,0],"side_midpoint":[30,0],"corner":[30,20]})"}},
        {"parallelogram",{R"({"type":"parallelogram","first":[0,0],"second":[60,0],"third":[75,40]})"}},
        {"straight_slot",{R"({"type":"straight_slot","start":[-25,0],"end":[25,0],"width":20})"}},
        {"center_slot",{R"({"type":"center_slot","center":[0,0],"end":[25,0],"width":20})"}},
        {"arc_slot",{R"({"type":"arc_slot","center":[0,0],"radius":40,"a0":0,"a1":120,"width":12})"}},
        {"three_point_arc_slot",{R"({"type":"three_point_arc_slot","start":[40,0],"through":[28.2842712475,28.2842712475],"end":[0,40],"width":12})"}},
        {"polygon",{R"({"type":"polygon","center":[0,0],"radius":30,"sides":6,"angle":0,"inscribed":false})"}},
        {"perimeter_circle",{R"({"type":"perimeter_circle","first":[30,0],"second":[0,30],"third":[-30,0]})"}},
        {"ellipse",{R"({"type":"ellipse","center":[0,0],"major":40,"minor":20,"angle":0})"}},
        {"partial_ellipse",{R"({"type":"partial_ellipse","center":[0,0],"major":40,"minor":20,"angle":0,"a0":0,"a1":180})"}},
        {"parabola",{R"({"type":"parabola","vertex":[0,0],"focal":10,"angle":0,"t0":-30,"t1":30})"}},
        {"conic",{R"({"type":"conic","start":[0,0],"control":[30,40],"end":[60,0],"weight":0.7071067811865476})"}},
        {"style_spline",{R"({"type":"style_spline","controls":[[0,0],[20,40],[40,-20],[60,0]]})"}},
        {"equation2d",{R"JSON({"type":"equation2d","x":"40*cos(t)","y":"20*sin(t)","t0":0,"t1":"2*pi","samples":64,"tolerance":0.01})JSON"}},
        {"point",{R"({"type":"point","point":[0,0]})"}},
        {"centerline",{R"({"type":"centerline","start":[-30,0],"end":[30,0]})"}},
        {"midpoint_line",{R"({"type":"midpoint_line","center":[0,0],"end":[30,0]})"}},
        {"route",{R"({"type":"route","points":[[0,0],[40,0],[40,30],[80,30]]})"}},
        {"belt",{R"({"type":"belt","first":[-40,0],"second":[40,0],"radius1":20,"radius2":30})"}},
        {"text",{R"({"type":"text","origin":[0,0],"text":"ARK","height":20,"spacing":1.2,"angle":0})"}}
    };return result;
}
bool scalar(const V& v){return (v.t==V::Num&&std::isfinite(v.num)&&std::abs(v.num)<=1e7)||(v.t==V::Str&&!v.str.empty()&&v.str.size()<=2048);}
bool pair(const V& v){return v.t==V::Arr&&v.arr.size()==2&&scalar(v.arr[0])&&scalar(v.arr[1]);}
double value(const V& v,const ExprEnv& env){bool ok=v.t==V::Num;double result=ok?v.num:evalExpr(v.str,env,&ok);if(!ok||!std::isfinite(result)||std::abs(result)>1e7)throw std::runtime_error("Invalid entity recipe expression");return result;}
V xy(Vec2 p){V a=V::makeArr();a.arr={V::makeNum(p.x),V::makeNum(p.y)};return a;}
V nativeLine(Vec2 a,Vec2 b){if((b-a).len()<1e-8)throw std::runtime_error("Recipe has a zero-length edge");V r=V::makeObj(),p=V::makeArr();p.arr={xy(a),xy(b)};r.set("kind",V::makeStr("line"));r.set("points",p);return r;}
V nativeArc(Vec2 center,double radius,double a0,double a1){V r=V::makeObj();r.set("kind",V::makeStr("arc"));r.set("center",xy(center));r.set("radius",V::makeNum(radius));r.set("a0",V::makeNum(a0));r.set("a1",V::makeNum(a1));return r;}
Vec2 bezier(const std::vector<Vec2>& controls,const std::vector<double>& weights,double t){std::vector<Vec3> work;for(size_t i=0;i<controls.size();++i){double w=weights.empty()?1:weights[i];work.push_back({controls[i].x*w,controls[i].y*w,w});}for(size_t n=work.size();n>1;--n)for(size_t i=0;i+1<n;++i)work[i]=work[i]*(1-t)+work[i+1]*t;return {work[0].x/work[0].z,work[0].y/work[0].z};}
double chordDistance(Vec2 p,Vec2 a,Vec2 b){auto d=b-a;double t=d.lensq()>0?clamp((p-a).dot(d)/d.lensq(),0.,1.):0;return (p-a-d*t).len();}
// Positive rational Bernstein weights retain the convex-hull property.
void sampleBezier(const std::vector<Vec2>& c,const std::vector<double>& weights,double tol,std::vector<Vec2>& samples,unsigned depth=0){
    double error=0;for(Vec2 p:c)error=std::max(error,chordDistance(p,c.front(),c.back()));
    if(error<=tol){if(samples.size()>=8192)throw std::runtime_error("Recipe sample capacity exceeded");samples.push_back(c.back());return;}
    if(depth>=24)throw std::runtime_error("Bezier recipe cannot meet display tolerance");
    std::vector<Vec3> row;for(size_t i=0;i<c.size();++i){double w=weights.empty()?1:weights[i];row.push_back({c[i].x*w,c[i].y*w,w});}
    std::vector<Vec3> left{row.front()},right{row.back()};for(size_t n=row.size();n>1;--n){for(size_t i=0;i+1<n;++i)row[i]=(row[i]+row[i+1])*.5;left.push_back(row[0]);right.push_back(row[n-2]);}std::reverse(right.begin(),right.end());
    auto split=[&](const std::vector<Vec3>& h){std::vector<Vec2> p;std::vector<double> w;for(auto v:h){p.push_back({v.x/v.z,v.y/v.z});w.push_back(v.z);}sampleBezier(p,w,tol,samples,depth+1);};split(left);split(right);
}
}
V sketchRecipeDefaults(const std::string& type){auto it=schemas().find(type);if(it==schemas().end())throw std::runtime_error("Unknown sketch entity recipe type: "+type);V result;arkjson::parse(it->second.json,result);return result;}
std::vector<std::string> sketchRecipeTypes(){std::vector<std::string> types;for(auto& item:schemas())types.push_back(item.first);return types;}
bool validateSketchEntityRecipe(const V& recipe,std::string& error){
    auto fail=[&](const std::string& message){error=message;return false;};if(recipe.t!=V::Obj)return fail("Entity recipe must be an object");const V* type=recipe.find("type");if(!type||type->t!=V::Str||!schemas().count(type->str))return fail("Unknown entity recipe type");auto defaults=sketchRecipeDefaults(type->str);
    for(const auto& field:recipe.obj){const V* expected=defaults.find(field.first.c_str());if(!expected)return fail("Unknown "+type->str+" recipe field: "+field.first);const V& supplied=field.second;if(field.first=="type")continue;
        if(field.first=="text"){if(supplied.t!=V::Str||supplied.str.empty()||supplied.str.size()>64)return fail("Sketch text requires 1..64 characters");for(unsigned char c:supplied.str)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c==' '||c=='-'))return fail("Builtin engraving font supports A-Z, 0-9, spaces and hyphens");}
        else if(field.first=="inscribed"){if(supplied.t!=V::Bool)return fail("inscribed must be boolean");}
        else if(field.first=="controls"||field.first=="points"){if(supplied.t!=V::Arr||supplied.arr.size()<2||supplied.arr.size()>(field.first=="controls"?26u:64u))return fail("controls/points require 2..26/64 coordinate pairs");for(auto& p:supplied.arr)if(!pair(p))return fail("Recipe points require [x,y] expressions");}
        else if(expected->t==V::Arr){if(!pair(supplied))return fail("Recipe coordinate requires [x,y] expressions");}
        else if(!scalar(supplied))return fail("Recipe scalar requires a finite number or expression");
    }
    for(auto& field:defaults.obj)if(!recipe.find(field.first.c_str()))return fail("Missing "+type->str+" recipe field: "+field.first);return true;
}
void evaluateSketchEntityRecipe(SketchEnt& entity,const ExprEnv& env,double displayTolerance){
    std::string error;if(entity.kind!=SketchEnt::Recipe||!validateSketchEntityRecipe(entity.entityRecipe,error))throw std::runtime_error(error.empty()?"Expected recipe entity":error);
    if(!std::isfinite(displayTolerance)||displayTolerance<.0001||displayTolerance>5)throw std::runtime_error("Recipe display tolerance outside 0.0001..5 mm");
    const V& recipe=entity.entityRecipe;const std::string type=recipe.find("type")->str;V geometry=V::makeArr(),report=V::makeObj();std::vector<Vec2> samples,handles;
    auto n=[&](const char* key){return value(*recipe.find(key),env);};auto p=[&](const char* key){const auto& a=recipe.find(key)->arr;return Vec2{value(a[0],env),value(a[1],env)};};
    auto positive=[&](const char* key){double x=n(key);if(x<=1e-6)throw std::runtime_error(std::string(key)+" must exceed 1e-6 mm");return x;};
    auto line=[&](Vec2 a,Vec2 b){geometry.arr.push_back(nativeLine(a,b));if(samples.empty())samples.push_back(a);samples.push_back(b);};
    auto loop=[&](const std::vector<Vec2>& points){for(size_t i=0;i<points.size();++i)line(points[i],points[(i+1)%points.size()]);handles=points;};
    auto arc=[&](Vec2 center,double radius,double a0,double a1){if(radius<=1e-6||std::abs(a1-a0)<=1e-9||std::abs(a1-a0)>360.+1e-8)throw std::runtime_error("Recipe arc radius/sweep is degenerate");geometry.arr.push_back(nativeArc(center,radius,a0,a1));const unsigned count=std::max(2u,(unsigned)std::ceil(std::abs(deg2rad(a1-a0))/(2*std::acos(clamp(1-displayTolerance/radius,-1.,1.)))));if(count>8192)throw std::runtime_error("Recipe arc sample capacity exceeded");for(unsigned i=samples.empty()?0u:1u;i<=count;++i){double a=deg2rad(a0+(a1-a0)*i/count);samples.push_back(center+Vec2{std::cos(a),std::sin(a)}*radius);}};
    auto curve=[&](const std::vector<Vec2>& controls,const std::vector<double>& weights=std::vector<double>{}){if(controls.size()<2||std::none_of(controls.begin()+1,controls.end(),[&](Vec2 point){return (point-controls.front()).len()>1e-8;}))throw std::runtime_error("Bezier recipe has coincident controls and zero length");V item=V::makeObj(),points=V::makeArr(),w=V::makeArr();item.set("kind",V::makeStr("bezier"));for(auto a:controls)points.arr.push_back(xy(a));item.set("points",points);if(!weights.empty()){for(double x:weights)w.arr.push_back(V::makeNum(x));item.set("weights",w);}geometry.arr.push_back(item);if(samples.empty())samples.push_back(controls.front());sampleBezier(controls,weights,displayTolerance,samples);handles=controls;};
    report.set("geometry_authority",V::makeStr("exact_native_primitives"));report.set("display_tolerance_mm",V::makeNum(displayTolerance));report.set("display_error_bound_verified",V::makeBool(true));
    if(type=="corner_rectangle"||type=="center_rectangle"){
        Vec2 a=type=="corner_rectangle"?p("first"):p("center")*2-p("corner"),b=type=="corner_rectangle"?p("second"):p("corner");if(std::abs(a.x-b.x)<=1e-6||std::abs(a.y-b.y)<=1e-6)throw std::runtime_error("Rectangle needs nonzero width and height");loop({a,{b.x,a.y},b,{a.x,b.y}});
    }else if(type=="three_point_center_rectangle"){
        Vec2 center=p("center"),u=p("side_midpoint")-center;if(u.len()<=1e-6)throw std::runtime_error("Center rectangle needs a distinct side midpoint");Vec2 y{-u.y/u.len(),u.x/u.len()},v=y*(p("corner")-center).dot(y);if(v.len()<=1e-6)throw std::runtime_error("Center rectangle needs nonzero perpendicular half height");loop({center-u-v,center+u-v,center+u+v,center-u+v});handles={center,p("side_midpoint"),p("corner")};
    }else if(type=="three_point_rectangle"||type=="parallelogram"){
        Vec2 a=p("first"),b=p("second"),c=p("third"),side=c-b;if((b-a).len()<=1e-6)throw std::runtime_error("Rectangle first side is degenerate");if(type=="three_point_rectangle"){Vec2 x=(b-a).normalized(),y{-x.y,x.x};side=y*(c-b).dot(y);c=b+side;}if(std::abs((b-a).cross(side))<=1e-8)throw std::runtime_error("Rectangle/parallelogram has zero area");loop({a,b,c,a+side});
    }else if(type=="straight_slot"||type=="center_slot"){
        Vec2 a=type=="straight_slot"?p("start"):p("center")*2-p("end"),b=p("end");double r=positive("width")*.5;if((b-a).len()<=1e-6)throw std::runtime_error("Slot needs distinct cap centres");Vec2 x=(b-a).normalized(),y{-x.y,x.x};double angle=rad2deg(std::atan2(x.y,x.x));line(a+y*r,b+y*r);arc(b,r,angle+90,angle-90);line(b-y*r,a-y*r);arc(a,r,angle-90,angle-270);handles={a,b};
    }else if(type=="arc_slot"||type=="three_point_arc_slot"){
        Vec2 center;double r,a0,a1;if(type=="arc_slot"){center=p("center");r=positive("radius");a0=n("a0");a1=n("a1");}else{Vec2 s=p("start"),m=p("through"),e=p("end");auto a=arcThroughThreePoints({s.x,s.y,0},{m.x,m.y,0},{e.x,e.y,0});center={a.center.x,a.center.y};r=a.radius;a0=rad2deg(std::atan2(s.y-center.y,s.x-center.x));a1=a0+rad2deg(a.sweep)*(a.normal.z<0?-1.:1.);}
        double h=positive("width")*.5;if(r<=h+1e-6||std::abs(a1-a0)>=360.-1e-7)throw std::runtime_error("Arc slot needs width below diameter and open centre arc");double sign=a1>a0?1.:-1.;auto radial=[&](double a){return Vec2{std::cos(deg2rad(a)),std::sin(deg2rad(a))};};arc(center,r+h,a0,a1);arc(center+radial(a1)*r,h,a1,a1+sign*180);arc(center,r-h,a1,a0);arc(center+radial(a0)*r,h,a0+180,a0+180+sign*180);handles={center,center+radial(a0)*r,center+radial(a1)*r};
    }else if(type=="polygon"){
        double sides=n("sides"),r=positive("radius");if(sides!=std::floor(sides)||sides<3||sides>64)throw std::runtime_error("Polygon sides must be integer 3..64");if(recipe.find("inscribed")->b)r/=std::cos(K_PI/sides);Vec2 center=p("center");std::vector<Vec2> points;for(unsigned i=0;i<(unsigned)sides;++i){double a=deg2rad(n("angle"))+K_2PI*i/sides;points.push_back(center+Vec2{std::cos(a),std::sin(a)}*r);}loop(points);
    }else if(type=="perimeter_circle"){
        Vec2 a=p("first"),b=p("second"),c=p("third");auto circle=arcThroughThreePoints({a.x,a.y,0},{b.x,b.y,0},{c.x,c.y,0});arc({circle.center.x,circle.center.y},circle.radius,0,360);handles={a,b,c};
    }else if(type=="ellipse"||type=="partial_ellipse"){
        Vec2 center=p("center");double major=positive("major"),minor=positive("minor"),angle=deg2rad(n("angle"));if(major<minor)throw std::runtime_error("Ellipse major radius must be >= minor radius");double a0=type=="ellipse"?0:n("a0"),a1=type=="ellipse"?360:n("a1");if(std::abs(a1-a0)<=1e-9||std::abs(a1-a0)>360.+1e-8)throw std::runtime_error("Ellipse sweep must be in (0,360] degrees");Vec2 x{std::cos(angle),std::sin(angle)},y{-x.y,x.x};auto at=[&](double a){return center+x*(major*std::cos(a))+y*(minor*std::sin(a));};unsigned count=(unsigned)std::ceil(std::abs(a1-a0)/90);for(unsigned i=0;i<count;++i){double start=deg2rad(a0+(a1-a0)*i/count),end=deg2rad(a0+(a1-a0)*(i+1)/count),middle=(start+end)*.5,w=std::cos((end-start)*.5);curve({at(start),center+(at(middle)-center)/w,at(end)},{1,w,1});}handles={center,center+x*major,center+y*minor};
    }else if(type=="parabola"){
        Vec2 vertex=p("vertex");double f=positive("focal"),a=n("t0"),b=n("t1"),angle=deg2rad(n("angle"));if(b<=a+1e-7)throw std::runtime_error("Parabola t1 must exceed t0");Vec2 x{std::cos(angle),std::sin(angle)},y{-x.y,x.x};auto at=[&](double t){return vertex+x*(t*t/(4*f))+y*t;};Vec2 start=at(a),end=at(b),tangent=x*(a/(2*f))+y;curve({start,start+tangent*((b-a)*.5),end});handles={vertex,start,end};
    }else if(type=="conic"){
        double w=positive("weight");if(w>1e6)throw std::runtime_error("Conic weight exceeds supported domain");Vec2 start=p("start"),control=p("control"),end=p("end");if((end-start).len()<=1e-6||std::abs((control-start).cross(end-start))<=1e-10*(control-start).len()*(end-start).len())throw std::runtime_error("Conic requires distinct endpoints and a noncollinear control");curve({start,control,end},{1,w,1});report.set("conic_class",V::makeStr(w<1-1e-10?"elliptic":w>1+1e-10?"hyperbolic":"parabolic"));
    }else if(type=="style_spline"){
        std::vector<Vec2> controls;for(auto& pair:recipe.find("controls")->arr)controls.push_back({value(pair.arr[0],env),value(pair.arr[1],env)});curve(controls);
    }else if(type=="equation2d"){
        V spec=V::makeObj();spec.set("op",V::makeStr("equation"));for(const char* key:{"x","y","t0","t1","samples","tolerance"})spec.set(key,*recipe.find(key));spec.set("z",V::makeNum(0));V request=sampleEquationCurve(spec,env),polyline=V::makeObj(),points=V::makeArr();for(const auto& point:request.find("points")->arr){Vec2 point2{point.arr[0].num,point.arr[1].num};samples.push_back(point2);points.arr.push_back(xy(point2));}polyline.set("kind",V::makeStr("polyline"));polyline.set("points",points);geometry.arr.push_back(polyline);handles={samples.front(),samples.back()};report=*request.find("equation_evidence");report.set("geometry_authority",V::makeStr("authored_adaptive_polyline"));report.set("display_error_bound_verified",V::makeBool(false));
    }else if(type=="point"){
        samples={p("point")};handles=samples;entity.construction=true;report.set("geometry_authority",V::makeStr("analytic_point"));
    }else if(type=="centerline"||type=="midpoint_line"){
        Vec2 b=p("end"),a=type=="centerline"?p("start"):p("center")*2-b;line(a,b);handles={a,b};if(type=="centerline")entity.construction=true;
    }else if(type=="route"){
        for(auto& point:recipe.find("points")->arr)handles.push_back({value(point.arr[0],env),value(point.arr[1],env)});for(size_t i=1;i<handles.size();++i)line(handles[i-1],handles[i]);report.set("route_kind",V::makeStr("editable_planar_polyline"));
    }else if(type=="belt"){
        Vec2 a=p("first"),b=p("second"),chord=b-a;double r1=positive("radius1"),r2=positive("radius2"),distance=chord.len();if(distance<=std::abs(r1-r2)+1e-6||distance<=r1+r2+1e-6)throw std::runtime_error("Belt pulleys must be separate and have two external tangents");double axis=rad2deg(std::atan2(chord.y,chord.x)),offset=rad2deg(std::acos((r1-r2)/distance));auto radial=[&](double angle){return Vec2{std::cos(deg2rad(angle)),std::sin(deg2rad(angle))};};double upper=axis+offset,lower=axis-offset;line(a+radial(upper)*r1,b+radial(upper)*r2);arc(b,r2,upper,lower);line(b+radial(lower)*r2,a+radial(lower)*r1);arc(a,r1,lower,upper-360);handles={a,b};report.set("belt_kind",V::makeStr("open_external_tangent_belt"));
    }else if(type=="text"){
        // A built-in seven-segment engraving font is deterministic and editable.
        // It is explicitly stroke geometry, not an invented installed font.
        const std::map<char,unsigned> glyphs={{'0',0x3f},{'1',0x06},{'2',0x5b},{'3',0x4f},{'4',0x66},{'5',0x6d},{'6',0x7d},{'7',0x07},{'8',0x7f},{'9',0x6f},{'A',0x77},{'B',0x7c},{'C',0x39},{'D',0x5e},{'E',0x79},{'F',0x71},{'G',0x3d},{'H',0x76},{'I',0x06},{'J',0x1e},{'K',0x76},{'L',0x38},{'M',0x37},{'N',0x54},{'O',0x3f},{'P',0x73},{'Q',0x67},{'R',0x50},{'S',0x6d},{'T',0x78},{'U',0x3e},{'V',0x3e},{'W',0x3e},{'X',0x76},{'Y',0x6e},{'Z',0x5b},{'-',0x40},{' ',0}};
        const Vec2 starts[7]={{0,1},{.6,1},{.6,.5},{.6,0},{0,0},{0,.5},{0,.5}},ends[7]={{.6,1},{.6,.5},{.6,0},{0,0},{0,.5},{0,1},{.6,.5}};
        Vec2 origin=p("origin");double height=positive("height"),spacing=positive("spacing"),angle=deg2rad(n("angle"));Vec2 x{std::cos(angle),std::sin(angle)},y{-x.y,x.x};unsigned index=0;for(char letter:recipe.find("text")->str){if(letter>='a'&&letter<='z')letter-=32;auto glyph=glyphs.find(letter);if(glyph==glyphs.end())throw std::runtime_error("Builtin engraving font supports A-Z, 0-9, spaces and hyphens");for(unsigned bit=0;bit<7;++bit)if(glyph->second&(1u<<bit)){auto at=[&](Vec2 local){return origin+x*((local.x+index*.8*spacing)*height)+y*(local.y*height);};Vec2 a=at(starts[bit]),b=at(ends[bit]);geometry.arr.push_back(nativeLine(a,b));samples.push_back(a);samples.push_back(b);}++index;}if(samples.empty())throw std::runtime_error("Sketch text has no strokes");handles={origin};entity.construction=true;report.set("font",V::makeStr("ARK builtin seven-segment stroke"));report.set("disconnected_strokes",V::makeBool(true));report.set("profile_qualification",V::makeStr("engraving_paths_only"));
    }
    for(Vec2 sample:samples)if(!std::isfinite(sample.x)||!std::isfinite(sample.y)||std::abs(sample.x)>1e7||std::abs(sample.y)>1e7)throw std::runtime_error("Recipe coordinate exceeds finite domain");
    entity.dof.clear();for(auto handle:handles){entity.dof.push_back(handle.x);entity.dof.push_back(handle.y);}entity.expressions.clear();entity.segments.clear();entity.recipeGeometry=std::move(geometry);entity.recipeSamples=std::move(samples);entity.recipeReport=std::move(report);
}
}
