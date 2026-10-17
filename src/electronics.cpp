#include "ark/electronics.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr double eps=1e-8;
const V& need(const V& object,const char* key){const V* v=object.find(key);if(!v)throw std::runtime_error(std::string("Electronics needs ")+key);return *v;}
const V& array(const V& object,const char* key,size_t minimum,size_t maximum){const V& v=need(object,key);if(v.t!=V::Arr||v.arr.size()<minimum||v.arr.size()>maximum)throw std::runtime_error(std::string("Electronics ")+key+" has invalid size");return v;}
std::string word(const V& object,const char* key){const V& v=need(object,key);if(v.t!=V::Str||v.str.empty()||v.str.size()>128)throw std::runtime_error(std::string("Electronics ")+key+" must be a name");return v.str;}
double number(const V& value,const ExprEnv& env,const char* label){
    if(value.t==V::Num&&std::isfinite(value.num)&&std::abs(value.num)<=1e6)return value.num;
    if(value.t==V::Str&&value.str.size()<=2048){bool ok=false;double x=evalExpr(value.str,env,&ok);if(ok&&std::isfinite(x)&&std::abs(x)<=1e6)return x;}
    throw std::runtime_error(std::string("Invalid electronics number: ")+label);
}
double normalizedNumber(const V& object,const char* key){const V& value=need(object,key);if(value.t!=V::Num||!std::isfinite(value.num))throw std::runtime_error(std::string("Invalid normalized electronics number: ")+key);return value.num;}
double get(const V& object,const ExprEnv& env,const char* key,double fallback){const V* v=object.find(key);return v?number(*v,env,key):fallback;}
double positive(const V& object,const ExprEnv& env,const char* key,double upper=1e5){double x=number(need(object,key),env,key);if(x<=0||x>upper)throw std::runtime_error(std::string(key)+" must be positive and bounded");return x;}
Vec2 p2(const V& value,const ExprEnv& env){if(value.t!=V::Arr||value.arr.size()!=2)throw std::runtime_error("Expected [x,y]");return {number(value.arr[0],env,"x"),number(value.arr[1],env,"y")};}
Vec3 p3(const V& value,const ExprEnv& env){if(value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error("Expected [x,y,z]");return {number(value.arr[0],env,"x"),number(value.arr[1],env,"y"),number(value.arr[2],env,"z")};}
V point(Vec2 p){V a=V::makeArr();a.arr.push_back(V::makeNum(p.x));a.arr.push_back(V::makeNum(p.y));return a;}
V point(Vec3 p){V a=V::makeArr();a.arr.push_back(V::makeNum(p.x));a.arr.push_back(V::makeNum(p.y));a.arr.push_back(V::makeNum(p.z));return a;}
double cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
double orientation(Vec2 a,Vec2 b,Vec2 c){return cross(b-a,c-a);}
double length(Vec2 a){return std::sqrt(a.x*a.x+a.y*a.y);}
double length(Vec3 a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
double distance(Vec2 a,Vec2 b){return length(a-b);}
double pointSegment(Vec2 p,Vec2 a,Vec2 b){Vec2 v=b-a;double d=v.x*v.x+v.y*v.y;if(d<eps)return distance(p,a);double t=std::clamp(((p.x-a.x)*v.x+(p.y-a.y)*v.y)/d,0.,1.);return distance(p,a+v*t);}
bool segmentsCross(Vec2 a,Vec2 b,Vec2 c,Vec2 d){
    double o1=orientation(a,b,c),o2=orientation(a,b,d),o3=orientation(c,d,a),o4=orientation(c,d,b);
    if(((o1>eps&&o2<-eps)||(o1<-eps&&o2>eps))&&((o3>eps&&o4<-eps)||(o3<-eps&&o4>eps)))return true;
    return (std::abs(o1)<=eps&&pointSegment(c,a,b)<=eps)||(std::abs(o2)<=eps&&pointSegment(d,a,b)<=eps)||(std::abs(o3)<=eps&&pointSegment(a,c,d)<=eps)||(std::abs(o4)<=eps&&pointSegment(b,c,d)<=eps);
}
double segmentDistance(Vec2 a,Vec2 b,Vec2 c,Vec2 d){if(segmentsCross(a,b,c,d))return 0;return std::min({pointSegment(a,c,d),pointSegment(b,c,d),pointSegment(c,a,b),pointSegment(d,a,b)});}
bool inside(const std::vector<Vec2>& poly,Vec2 p){bool yes=false;for(size_t i=0,j=poly.size()-1;i<poly.size();j=i++){const auto& a=poly[i];const auto& b=poly[j];if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)yes=!yes;}return yes;}
double edgeDistance(const std::vector<Vec2>& poly,Vec2 p){double best=1e300;for(size_t i=0;i<poly.size();++i)best=std::min(best,pointSegment(p,poly[i],poly[(i+1)%poly.size()]));return best;}
double edgeDistance(const std::vector<Vec2>& poly,Vec2 a,Vec2 b){double best=1e300;for(size_t i=0;i<poly.size();++i)best=std::min(best,segmentDistance(a,b,poly[i],poly[(i+1)%poly.size()]));return best;}
void withinBoard(const std::vector<Vec2>& outline,Vec2 a,Vec2 b,double radius,double margin,const std::string& label){
    if(!inside(outline,a)||!inside(outline,b)||edgeDistance(outline,a,b)+eps<radius+margin)
        throw std::runtime_error(label+" violates board outline or edge clearance");
}
void withinBoard(const std::vector<Vec2>& outline,Vec2 center,double radius,double margin,const std::string& label){withinBoard(outline,center,center,radius,margin,label);}
struct Copper {std::string net,layer,label;int kind=0;Vec2 a{},b{};double radius=0,hx=0,hy=0;}; // kind 0 disc, 1 segment, 2 rectangular pad
double copperGap(const Copper& a,const Copper& b){
    auto discRect=[](Vec2 center,double radius,const Copper& rectangle){double dx=std::max(std::abs(center.x-rectangle.a.x)-rectangle.hx,0.);double dy=std::max(std::abs(center.y-rectangle.a.y)-rectangle.hy,0.);return std::sqrt(dx*dx+dy*dy)-radius;};
    auto segmentRect=[&](const Copper& segment,const Copper& rectangle){
        Vec2 c=rectangle.a;double x=rectangle.hx,y=rectangle.hy;
        std::array<Vec2,4> corners{{{c.x-x,c.y-y},{c.x+x,c.y-y},{c.x+x,c.y+y},{c.x-x,c.y+y}}};
        if(std::abs(segment.a.x-c.x)<=x&&std::abs(segment.a.y-c.y)<=y)return -segment.radius;
        if(std::abs(segment.b.x-c.x)<=x&&std::abs(segment.b.y-c.y)<=y)return -segment.radius;
        double best=1e300;for(int i=0;i<4;++i)best=std::min(best,segmentDistance(segment.a,segment.b,corners[i],corners[(i+1)%4]));return best-segment.radius;
    };
    if(a.kind==2&&b.kind==2){double dx=std::max(std::abs(a.a.x-b.a.x)-a.hx-b.hx,0.);double dy=std::max(std::abs(a.a.y-b.a.y)-a.hy-b.hy,0.);return std::sqrt(dx*dx+dy*dy);}
    if(a.kind==2)return b.kind==1?segmentRect(b,a):discRect(b.a,b.radius,a);
    if(b.kind==2)return a.kind==1?segmentRect(a,b):discRect(a.a,a.radius,b);
    if(a.kind==0&&b.kind==0)return distance(a.a,b.a)-a.radius-b.radius;
    if(a.kind==0)return pointSegment(a.a,b.a,b.b)-a.radius-b.radius;
    if(b.kind==0)return pointSegment(b.a,a.a,a.b)-a.radius-b.radius;
    return segmentDistance(a.a,a.b,b.a,b.b)-a.radius-b.radius;
}
V normalizePcb(const V& spec,const ExprEnv& env){
    const V& outlineSpec=array(spec,"outline",3,128);std::vector<Vec2> outline;V outlineJson=V::makeArr();
    for(const V& coordinate:outlineSpec.arr){Vec2 p=p2(coordinate,env);if(std::abs(p.x)>10000||std::abs(p.y)>10000)throw std::runtime_error("PCB coordinates must be within 10 m");outline.push_back(p);outlineJson.arr.push_back(point(p));}
    double twiceArea=0;for(size_t i=0;i<outline.size();++i){if(distance(outline[i],outline[(i+1)%outline.size()])<1e-5)throw std::runtime_error("PCB outline has a zero-length edge");twiceArea+=cross(outline[i],outline[(i+1)%outline.size()]);}
    if(twiceArea<=eps)throw std::runtime_error("PCB outline must be counterclockwise and have positive area");
    for(size_t i=0;i<outline.size();++i)for(size_t j=i+1;j<outline.size();++j){
        if(j==i+1||(i==0&&j==outline.size()-1))continue;
        if(segmentsCross(outline[i],outline[(i+1)%outline.size()],outline[j],outline[(j+1)%outline.size()]))throw std::runtime_error("PCB outline self-intersects");
    }
    const double thickness=positive(spec,env,"thickness",20);
    const V* rulesSpec=spec.find("rules");if(rulesSpec&&rulesSpec->t!=V::Obj)throw std::runtime_error("PCB rules must be an object");V empty=V::makeObj();const V& rules=rulesSpec?*rulesSpec:empty;
    const double clearance=get(rules,env,"clearance",.2),edge=get(rules,env,"edge_clearance",.2),minWidth=get(rules,env,"min_trace_width",.15),minDrill=get(rules,env,"min_drill",.2),minRing=get(rules,env,"min_annular_ring",.1);
    if(clearance<0||clearance>10||edge<0||edge>10||minWidth<=0||minWidth>10||minDrill<=0||minDrill>10||minRing<=0||minRing>10)throw std::runtime_error("PCB design rules outside supported range");
    V normalized=V::makeObj(),normalizedRules=V::makeObj();normalized.set("kind",V::makeStr("pcb"));normalized.set("outline",outlineJson);normalized.set("thickness",V::makeNum(thickness));
    normalizedRules.set("clearance",V::makeNum(clearance));normalizedRules.set("edge_clearance",V::makeNum(edge));normalizedRules.set("min_trace_width",V::makeNum(minWidth));normalizedRules.set("min_drill",V::makeNum(minDrill));normalizedRules.set("min_annular_ring",V::makeNum(minRing));normalized.set("rules",normalizedRules);
    V layerRows=V::makeArr();std::set<std::string> layerNames;
    if(const V* source=spec.find("layers")){
        if(source->t!=V::Arr||source->arr.empty()||source->arr.size()>8)throw std::runtime_error("PCB needs 1..8 layers");
        for(const V& item:source->arr){if(item.t!=V::Obj)throw std::runtime_error("PCB layer must be an object");std::string name=word(item,"name");if(!layerNames.insert(name).second)throw std::runtime_error("Duplicate PCB layer");double z=get(item,env,"z",name=="bottom"?0:thickness),copper=get(item,env,"copper_thickness",.035);if(z<0||z>thickness||copper<=0||copper>.5)throw std::runtime_error("PCB layer depth or copper thickness is invalid");V row=V::makeObj();row.set("name",V::makeStr(name));row.set("z",V::makeNum(z));row.set("copper_thickness",V::makeNum(copper));layerRows.arr.push_back(row);}
    }else for(const auto& [name,z]:std::vector<std::pair<std::string,double>>{{"top",thickness},{"bottom",0}}){V row=V::makeObj();row.set("name",V::makeStr(name));row.set("z",V::makeNum(z));row.set("copper_thickness",V::makeNum(.035));layerRows.arr.push_back(row);layerNames.insert(name);}
    normalized.set("layers",layerRows);
    std::vector<Copper> copper;V pads=V::makeArr(),traces=V::makeArr(),vias=V::makeArr(),holes=V::makeArr(),components=V::makeArr();std::set<std::string> padNames,componentRefs;
    if(const V* items=spec.find("pads")){
        if(items->t!=V::Arr||items->arr.size()>256)throw std::runtime_error("PCB pad limit is 256");
        for(const V& item:items->arr){if(item.t!=V::Obj)throw std::runtime_error("PCB pad must be an object");std::string name=word(item,"name"),net=word(item,"net"),layer=word(item,"layer");if(!padNames.insert(name).second||!layerNames.count(layer))throw std::runtime_error("Duplicate pad or unknown layer");double x=number(need(item,"x"),env,"x"),y=number(need(item,"y"),env,"y"),w=positive(item,env,"width",100),h=positive(item,env,"height",100);Vec2 center{x,y};std::array<Vec2,4> corners{{{x-w/2,y-h/2},{x+w/2,y-h/2},{x+w/2,y+h/2},{x-w/2,y+h/2}}};for(size_t side=0;side<4;++side)withinBoard(outline,corners[side],corners[(side+1)%4],0,edge,"Pad "+name);copper.push_back({net,layer,"pad "+name,2,center,{},0,w/2,h/2});V row=V::makeObj();for(auto [k,v]:std::vector<std::pair<const char*,std::string>>{{"name",name},{"net",net},{"layer",layer}})row.set(k,V::makeStr(v));for(auto [k,v]:std::vector<std::pair<const char*,double>>{{"x",x},{"y",y},{"width",w},{"height",h}})row.set(k,V::makeNum(v));pads.arr.push_back(row);}
    }
    if(const V* items=spec.find("traces")){
        if(items->t!=V::Arr||items->arr.size()>128)throw std::runtime_error("PCB trace limit is 128");
        size_t total=0;for(const V& item:items->arr){if(item.t!=V::Obj)throw std::runtime_error("PCB trace must be an object");std::string net=word(item,"net"),layer=word(item,"layer");if(!layerNames.count(layer))throw std::runtime_error("Unknown trace layer");double width=positive(item,env,"width",20);if(width+eps<minWidth)throw std::runtime_error("PCB trace below minimum width");const V& points=array(item,"points",2,64);total+=points.arr.size();if(total>512)throw std::runtime_error("PCB total trace point limit is 512");std::vector<Vec2> path;V pathJson=V::makeArr();for(const V& source:points.arr){Vec2 p=p2(source,env);path.push_back(p);pathJson.arr.push_back(point(p));}for(size_t i=1;i<path.size();++i){if(distance(path[i-1],path[i])<1e-5)throw std::runtime_error("PCB trace has a zero-length segment");withinBoard(outline,path[i-1],path[i],width/2,edge,"Trace "+net);copper.push_back({net,layer,"trace "+net,1,path[i-1],path[i],width/2});}V row=V::makeObj();row.set("net",V::makeStr(net));row.set("layer",V::makeStr(layer));row.set("width",V::makeNum(width));row.set("points",pathJson);traces.arr.push_back(row);}
    }
    if(const V* items=spec.find("vias")){
        if(items->t!=V::Arr||items->arr.size()>128)throw std::runtime_error("PCB via limit is 128");
        for(const V& item:items->arr){if(item.t!=V::Obj)throw std::runtime_error("PCB via must be an object");std::string net=word(item,"net");Vec2 p{number(need(item,"x"),env,"x"),number(need(item,"y"),env,"y")};double diameter=positive(item,env,"diameter",20),drill=positive(item,env,"drill",20);if(drill+eps<minDrill||(diameter-drill)/2+eps<minRing)throw std::runtime_error("PCB via drill or annular ring is invalid");withinBoard(outline,p,diameter/2,edge,"Via");for(const auto& layer:layerNames)copper.push_back({net,layer,"via "+net,0,p,{},diameter/2});V row=V::makeObj();row.set("net",V::makeStr(net));row.set("x",V::makeNum(p.x));row.set("y",V::makeNum(p.y));row.set("diameter",V::makeNum(diameter));row.set("drill",V::makeNum(drill));vias.arr.push_back(row);}
    }
    if(const V* items=spec.find("holes")){
        if(items->t!=V::Arr||items->arr.size()>128)throw std::runtime_error("PCB hole limit is 128");
        for(const V& item:items->arr){if(item.t!=V::Obj)throw std::runtime_error("PCB hole must be an object");Vec2 p{number(need(item,"x"),env,"x"),number(need(item,"y"),env,"y")};double diameter=positive(item,env,"diameter",100);if(diameter+eps<minDrill)throw std::runtime_error("PCB hole below minimum drill");withinBoard(outline,p,diameter/2,edge,"Mounting hole");for(const V& prior:holes.arr){Vec2 earlier{prior.find("x")->num,prior.find("y")->num};if(distance(p,earlier)+eps<(diameter+prior.find("diameter")->num)/2+clearance)throw std::runtime_error("PCB mounting holes overlap or violate clearance");}for(const Copper& element:copper)if(copperGap(element,{"",element.layer,"mounting hole",0,p,{},diameter/2})+eps<clearance)throw std::runtime_error("Mounting hole violates copper clearance: "+element.label);V row=V::makeObj();row.set("x",V::makeNum(p.x));row.set("y",V::makeNum(p.y));row.set("diameter",V::makeNum(diameter));holes.arr.push_back(row);}
    }
    for(size_t i=0;i<copper.size();++i)for(size_t j=i+1;j<copper.size();++j)if(copper[i].net!=copper[j].net&&copper[i].layer==copper[j].layer&&copperGap(copper[i],copper[j])+eps<clearance)throw std::runtime_error("PCB copper clearance: "+copper[i].label+" versus "+copper[j].label);
    if(const V* items=spec.find("components")){
        if(items->t!=V::Arr||items->arr.size()>128)throw std::runtime_error("PCB component limit is 128");
        for(const V& item:items->arr){if(item.t!=V::Obj)throw std::runtime_error("PCB component must be an object");std::string ref=word(item,"ref");if(!componentRefs.insert(ref).second)throw std::runtime_error("Duplicate component ref");Vec2 p{number(need(item,"x"),env,"x"),number(need(item,"y"),env,"y")};double w=positive(item,env,"width",100),h=positive(item,env,"height",100),height=get(item,env,"height_mm",1);if(height<=0||height>100)throw std::runtime_error("Invalid component height");std::array<Vec2,4> corners{{{p.x-w/2,p.y-h/2},{p.x+w/2,p.y-h/2},{p.x+w/2,p.y+h/2},{p.x-w/2,p.y+h/2}}};for(size_t side=0;side<4;++side)withinBoard(outline,corners[side],corners[(side+1)%4],0,0,"Component "+ref);V row=V::makeObj();row.set("ref",V::makeStr(ref));row.set("value",V::makeStr(item.find("value")?item.find("value")->asStr():""));row.set("package",V::makeStr(item.find("package")?item.find("package")->asStr():""));row.set("x",V::makeNum(p.x));row.set("y",V::makeNum(p.y));row.set("width",V::makeNum(w));row.set("height",V::makeNum(h));row.set("height_mm",V::makeNum(height));components.arr.push_back(row);}
    }
    normalized.set("pads",pads);normalized.set("traces",traces);normalized.set("vias",vias);normalized.set("holes",holes);normalized.set("components",components);
    // Connectivity is evaluated on the copper geometry, including all layer
    // occurrences of each via. Disconnected net islands remain editable.
    std::vector<size_t> parent(copper.size());for(size_t i=0;i<parent.size();++i)parent[i]=i;
    auto root=[&](size_t x){while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;};
    auto join=[&](size_t a,size_t b){a=root(a);b=root(b);if(a!=b)parent[b]=a;};
    for(size_t i=0;i<copper.size();++i)for(size_t j=i+1;j<copper.size();++j)if(copper[i].net==copper[j].net&&copper[i].layer==copper[j].layer&&copperGap(copper[i],copper[j])<=eps)join(i,j);
    // Via occurrences touch one continuous plated barrel across layers.
    for(const V& item:vias.arr){std::vector<size_t> indices;for(size_t k=0;k<copper.size();++k)if(copper[k].label=="via "+item.find("net")->str&&copper[k].a.x==item.find("x")->num&&copper[k].a.y==item.find("y")->num)indices.push_back(k);for(size_t k=1;k<indices.size();++k)join(indices[0],indices[k]);}
    std::map<std::string,std::set<size_t>> islands;for(size_t i=0;i<copper.size();++i)islands[copper[i].net].insert(root(i));
    V nets=V::makeArr();bool complete=true;for(const auto& [name,parts]:islands){V row=V::makeObj(),pins=V::makeArr();for(const V& pad:pads.arr)if(pad.find("net")->str==name)pins.arr.push_back(*pad.find("name"));row.set("name",V::makeStr(name));row.set("pads",pins);row.set("islands",V::makeNum((double)parts.size()));row.set("connected",V::makeBool(parts.size()==1));nets.arr.push_back(row);complete&=parts.size()==1;}
    std::map<std::pair<std::string,std::string>,std::vector<std::string>> grouped;for(const V& component:components.arr)grouped[{component.find("value")->str,component.find("package")->str}].push_back(component.find("ref")->str);
    V bom=V::makeArr();for(const auto& [key,refs]:grouped){V row=V::makeObj(),names=V::makeArr();for(const auto& ref:refs)names.arr.push_back(V::makeStr(ref));row.set("value",V::makeStr(key.first));row.set("package",V::makeStr(key.second));row.set("quantity",V::makeNum((double)refs.size()));row.set("refs",names);bom.arr.push_back(row);}
    V report=V::makeObj();report.set("drc_checked",V::makeBool(true));report.set("drc_scope",V::makeStr("outline, same-layer copper clearance, trace width, drill and annular ring, mounting-hole clearance"));report.set("drc_limits",V::makeStr("Solder mask, impedance, thermal, component courtyard and fabrication panelization are not checked"));report.set("connectivity_checked",V::makeBool(true));report.set("connectivity_complete",V::makeBool(complete));report.set("nets",nets);report.set("bom",bom);report.set("board_area_mm2",V::makeNum(twiceArea/2));report.set("layer_count",V::makeNum((double)layerRows.arr.size()));report.set("pad_count",V::makeNum((double)pads.arr.size()));report.set("trace_count",V::makeNum((double)traces.arr.size()));report.set("via_count",V::makeNum((double)vias.arr.size()));report.set("component_count",V::makeNum((double)components.arr.size()));report.set("physical_mass_supported",V::makeBool(false));report.set("fabrication_qualified",V::makeBool(false));normalized.set("report",report);return normalized;
}
struct Obstacle {Vec3 lo,hi;std::string name;};
Vec3 endpoint(const V& source,const ExprEnv& env,std::string& name){
    if(source.t==V::Arr)return p3(source,env);
    if(source.t!=V::Obj)throw std::runtime_error("Cable endpoint must be a point or named point");
    name=word(source,"name");return p3(need(source,"point"),env);
}
bool segmentClear(Vec3 a,Vec3 b,const std::vector<Obstacle>& obstacles,double radius){
    for(const Obstacle& box:obstacles){
        const Vec3 lo=box.lo-Vec3{radius,radius,radius},hi=box.hi+Vec3{radius,radius,radius};double low=0,high=1;bool miss=false;
        for(int axis=0;axis<3;++axis){double p=axis==0?a.x:axis==1?a.y:a.z,d=axis==0?b.x-a.x:axis==1?b.y-a.y:b.z-a.z,l=axis==0?lo.x:axis==1?lo.y:lo.z,h=axis==0?hi.x:axis==1?hi.y:hi.z;if(std::abs(d)<1e-12){if(p<l||p>h){miss=true;break;}continue;}double x=(l-p)/d,y=(h-p)/d;if(x>y)std::swap(x,y);low=std::max(low,x);high=std::min(high,y);if(low>high){miss=true;break;}}
        if(!miss)return false;
    }
    return true;
}
double cableLength(const std::vector<Vec3>& path,double bendRadius){
    std::vector<double> lengths,tangents(path.size(),0);double straight=0;for(size_t i=1;i<path.size();++i){double segment=length(path[i]-path[i-1]);if(segment<1e-5)throw std::runtime_error("Cable has a zero-length segment");lengths.push_back(segment);straight+=segment;}
    for(size_t i=1;i+1<path.size();++i){Vec3 incoming=path[i]-path[i-1],outgoing=path[i+1]-path[i];double a=length(incoming),b=length(outgoing);double dot=std::clamp((incoming.x*outgoing.x+incoming.y*outgoing.y+incoming.z*outgoing.z)/(a*b),-1.,1.);double theta=std::acos(dot);if(theta<1e-7)continue;if(theta>3.13)throw std::runtime_error("Cable cannot reverse direction at a waypoint");double tangent=bendRadius*std::tan(theta/2);if(tangent>=.49*std::min(a,b))throw std::runtime_error("Cable bend radius does not fit adjacent segments");tangents[i]=tangent;straight+=bendRadius*theta-2*tangent;}
    for(size_t i=0;i<lengths.size();++i)if(tangents[i]+tangents[i+1]>=lengths[i]-1e-5)throw std::runtime_error("Cable bend radius does not fit adjacent segments");
    return straight;
}
bool cableClear(const std::vector<Vec3>& path,const std::vector<Obstacle>& obstacles,double radius){for(size_t i=1;i<path.size();++i)if(!segmentClear(path[i-1],path[i],obstacles,radius))return false;return true;}
V normalizeCable(const V& spec,const ExprEnv& env){
    std::string startName,endName;Vec3 start=endpoint(need(spec,"start"),env,startName),end=endpoint(need(spec,"end"),env,endName);
    auto bounded=[](Vec3 p){if(std::abs(p.x)>10000||std::abs(p.y)>10000||std::abs(p.z)>10000)throw std::runtime_error("Cable coordinates must stay within 10 m");};bounded(start);bounded(end);
    double radius=positive(spec,env,"radius",100),bendRadius=get(spec,env,"bend_radius",radius*3);if(bendRadius<radius||bendRadius>1e5)throw std::runtime_error("Cable bend radius must be at least its radius and bounded");
    std::vector<Obstacle> obstacles;V boxes=V::makeArr();
    if(const V* source=spec.find("obstacles")){if(source->t!=V::Arr||source->arr.size()>64)throw std::runtime_error("Cable obstacle limit is 64");size_t index=0;for(const V& item:source->arr){if(item.t!=V::Obj)throw std::runtime_error("Cable obstacle must be an object");Obstacle box{p3(need(item,"min"),env),p3(need(item,"max"),env),item.find("name")?word(item,"name"):"obstacle."+std::to_string(index++)};bounded(box.lo);bounded(box.hi);if(box.hi.x<=box.lo.x||box.hi.y<=box.lo.y||box.hi.z<=box.lo.z)throw std::runtime_error("Cable obstacle bounds must increase");V row=V::makeObj();row.set("name",V::makeStr(box.name));row.set("min",point(box.lo));row.set("max",point(box.hi));boxes.arr.push_back(row);obstacles.push_back(box);}}
    bool autoRoute=false;if(const V* value=spec.find("auto_route")){if(value->t!=V::Bool)throw std::runtime_error("auto_route must be boolean");autoRoute=value->b;}
    std::vector<Vec3> path{start};V waypoints=V::makeArr();if(const V* source=spec.find("waypoints")){if(source->t!=V::Arr||source->arr.size()>32)throw std::runtime_error("Cable waypoint limit is 32");for(const V& item:source->arr){std::string name;Vec3 value=endpoint(item,env,name);bounded(value);path.push_back(value);V row=V::makeObj();row.set("name",V::makeStr(name));row.set("point",point(value));waypoints.arr.push_back(row);}}
    if(autoRoute&&!waypoints.arr.empty())throw std::runtime_error("auto_route cannot also accept authored waypoints");
    path.push_back(end);
    if(autoRoute&&!cableClear(path,obstacles,radius)){
        std::vector<std::vector<Vec3>> candidates;double margin=radius+bendRadius+1;
        double high=std::max(start.z,end.z),low=std::min(start.z,end.z),left=std::min(start.x,end.x),right=std::max(start.x,end.x),front=std::min(start.y,end.y),back=std::max(start.y,end.y);
        for(const auto& obstacle:obstacles){high=std::max(high,obstacle.hi.z);low=std::min(low,obstacle.lo.z);left=std::min(left,obstacle.lo.x);right=std::max(right,obstacle.hi.x);front=std::min(front,obstacle.lo.y);back=std::max(back,obstacle.hi.y);}
        for(double z:{high+margin,low-margin})candidates.push_back({start,{start.x,start.y,z},{end.x,end.y,z},end});
        for(double x:{left-margin,right+margin})candidates.push_back({start,{x,start.y,start.z},{x,end.y,end.z},end});
        for(double y:{front-margin,back+margin})candidates.push_back({start,{start.x,y,start.z},{end.x,y,end.z},end});
        double best=1e300;for(auto& candidate:candidates){try{for(Vec3 p:candidate)bounded(p);if(!cableClear(candidate,obstacles,radius))continue;double length=cableLength(candidate,bendRadius);if(length<best){best=length;path=candidate;}}catch(const std::exception&){}}
        if(best==1e300)throw std::runtime_error("No bounded obstacle-avoiding cable route found; add waypoints");
    }
    if(!cableClear(path,obstacles,radius))throw std::runtime_error("Cable centerline intersects an obstacle at the specified radius");
    double routedLength=cableLength(path,bendRadius);if(routedLength>100000)throw std::runtime_error("Cable route exceeds 100 m limit");
    V output=V::makeObj(),points=V::makeArr(),report=V::makeObj();for(Vec3 p:path)points.arr.push_back(point(p));output.set("kind",V::makeStr("cable_route"));output.set("start_name",V::makeStr(startName));output.set("end_name",V::makeStr(endName));output.set("path",points);output.set("radius",V::makeNum(radius));output.set("bend_radius",V::makeNum(bendRadius));output.set("obstacles",boxes);output.set("waypoints",waypoints);report.set("start_name",V::makeStr(startName));report.set("end_name",V::makeStr(endName));report.set("path",points);report.set("route_length_mm",V::makeNum(routedLength));report.set("obstacle_prefilter_checked",V::makeBool(true));report.set("exact_obstacle_checked",V::makeBool(false));report.set("auto_routed",V::makeBool(autoRoute));report.set("route_scope",V::makeStr("static authored AABB obstacles; no dynamic motion, clip, connector or thermal validation"));report.set("physical_mass_supported",V::makeBool(true));output.set("report",report);return output;
}
#include "electronics_cables_035b.inl"
} // namespace

bool requiresElectronics035B(const Feature& feature){
    if(feature.type!=FeatureType::CableRoute||feature.nativeSpec.t!=V::Obj)return false;
    const auto& spec=feature.nativeSpec;
    return spec.find("route_mode")||spec.find("profile")||spec.find("start_tangent")||spec.find("end_tangent")||cableMetadata035B(spec);
}

arkjson::Value electronicsRequest(const Feature& feature,const ExprEnv& env){
    if(feature.nativeSpec.t!=arkjson::Value::Obj)throw std::runtime_error("Electronics feature needs a native program");
    V authored=feature.nativeSpec;
    if(feature.type==FeatureType::PCB){
        if(auto value=feature.p.find("thickness");value!=feature.p.end())authored.set("thickness",V::makeStr(value->second));
        V rules=authored.find("rules")?*authored.find("rules"):V::makeObj();
        for(const char* key:{"clearance","edge_clearance","min_trace_width","min_drill","min_annular_ring"})if(auto value=feature.p.find(key);value!=feature.p.end())rules.set(key,V::makeStr(value->second));
        authored.set("rules",rules);return normalizePcb(authored,env);
    }
    if(feature.type==FeatureType::CableRoute){
        for(const char* key:{"radius","bend_radius"})if(auto value=feature.p.find(key);value!=feature.p.end())authored.set(key,V::makeStr(value->second));
        if(const auto* profile=authored.find("profile")){V numeric=*profile;for(const char* key:{"width","thickness"})if(auto value=feature.p.find(key);value!=feature.p.end())numeric.set(key,V::makeStr(value->second));authored.set("profile",numeric);}
        if(authored.find("route_mode")||authored.find("profile")||authored.find("start_tangent")||authored.find("end_tangent"))return normalizeSplineCable035B(authored,env);
        V result=normalizeCable(authored,env);
        if(cableMetadata035B(authored)){V report=*result.find("report");report.set("endpoints",cableEndpoints035B(authored,env));result.set("report",report);}
        return result;
    }
    throw std::runtime_error("Unsupported electronics feature");
}

arkjson::Value exportPcbFabrication(const Feature& feature,const ExprEnv& env,const std::string& pathPrefix){
    if(feature.type!=FeatureType::PCB)throw std::runtime_error("Fabrication preview requires a PCB feature");
    if(pathPrefix.empty()||pathPrefix.size()>1024)throw std::runtime_error("Fabrication path prefix is invalid");
    V board=electronicsRequest(feature,env);
    auto coordinate=[](double mm){return std::to_string((long long)std::llround(mm*100000));};
    auto fixed=[](double value){std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(5)<<value;return out.str();};
    auto xy=[&](const V& p){return std::string("X")+coordinate(p.arr[0].num)+"Y"+coordinate(p.arr[1].num);};
    auto header=[](){return std::string("G04 ARK-Point checked preview; independent CAM review required*\n%FSLAX45Y45*%\n%MOMM*%\n%LPD*%\n");};
    struct File {std::filesystem::path path;std::string bytes;};std::vector<File> files;
    auto outputPath=[&](const std::string& suffix){const std::string utf8=pathPrefix+suffix;return std::filesystem::path(reinterpret_cast<const char8_t*>(utf8.c_str()));};
    {
        std::string gerber=header()+"%ADD10C,0.10000*%\nD10*\n";
        const auto& outline=need(board,"outline").arr;
        gerber+=xy(outline.front())+"D02*\n";for(size_t i=1;i<outline.size();++i)gerber+=xy(outline[i])+"D01*\n";gerber+=xy(outline.front())+"D01*\nM02*\n";
        files.push_back({outputPath(".Edge_Cuts.gbr"),gerber});
    }
    size_t layerIndex=0;for(const V& layer:need(board,"layers").arr){
        std::string name=word(layer,"name"),safe=name;for(char& c:safe)if(!std::isalnum((unsigned char)c)&&c!='-'&&c!='_')c='_';
        std::string apertures,draw;int aperture=10;
        for(const V& pad:need(board,"pads").arr)if(word(pad,"layer")==name){
            apertures+="%ADD"+std::to_string(aperture)+"R,"+fixed(normalizedNumber(pad,"width"))+"X"+fixed(normalizedNumber(pad,"height"))+"*%\n";draw+="D"+std::to_string(aperture)+"*\n";
            V p=V::makeArr();p.arr={*pad.find("x"),*pad.find("y")};draw+=xy(p)+"D03*\n";++aperture;
        }
        for(const V& trace:need(board,"traces").arr)if(word(trace,"layer")==name){
            apertures+="%ADD"+std::to_string(aperture)+"C,"+fixed(normalizedNumber(trace,"width"))+"*%\n";draw+="D"+std::to_string(aperture)+"*\n";
            const auto& path=need(trace,"points").arr;draw+=xy(path.front())+"D02*\n";for(size_t i=1;i<path.size();++i)draw+=xy(path[i])+"D01*\n";++aperture;
        }
        for(const V& via:need(board,"vias").arr){apertures+="%ADD"+std::to_string(aperture)+"C,"+fixed(normalizedNumber(via,"diameter"))+"*%\n";draw+="D"+std::to_string(aperture)+"*\n";V p=V::makeArr();p.arr={*via.find("x"),*via.find("y")};draw+=xy(p)+"D03*\n";++aperture;}
        files.push_back({outputPath(".Layer"+std::to_string(++layerIndex)+"_"+safe+".gbr"),header()+apertures+draw+"M02*\n"});
    }
    auto drillFile=[&](const char* key,const char* suffix){
        const auto& items=need(board,key).arr;std::string out="M48\nMETRIC,TZ\n";std::map<double,int> tools;for(const V& item:items){double diameter=normalizedNumber(item,key==std::string("vias")?"drill":"diameter");if(!tools.count(diameter))tools[diameter]=(int)tools.size()+1;}
        for(const auto& [diameter,id]:tools)out+="T"+std::to_string(id)+"C"+fixed(diameter)+"\n";out+="%\n";
        for(const auto& [diameter,id]:tools){out+="T"+std::to_string(id)+"\n";for(const V& item:items)if(normalizedNumber(item,key==std::string("vias")?"drill":"diameter")==diameter)out+="X"+fixed(normalizedNumber(item,"x"))+"Y"+fixed(normalizedNumber(item,"y"))+"\n";}
        out+="M30\n";files.push_back({outputPath(suffix),out});
    };
    drillFile("vias",".PTH.drl");drillFile("holes",".NPTH.drl");
    V netlist=V::makeObj();netlist.set("format",V::makeStr("ark-pcb-preview"));netlist.set("fabrication_qualified",V::makeBool(false));for(const char* key:{"pads","traces","vias","components","report"})netlist.set(key,need(board,key));files.push_back({outputPath(".netlist.json"),netlist.dump()});
    std::vector<std::filesystem::path> pending;try{
        for(const File& file:files){auto parent=file.path.parent_path();if(!parent.empty())std::filesystem::create_directories(parent);auto temp=file.path;temp+=".pending";std::ofstream stream(temp,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("Cannot write fabrication preview");stream.write(file.bytes.data(),(std::streamsize)file.bytes.size());stream.close();if(!stream)throw std::runtime_error("Cannot finish fabrication preview");pending.push_back(temp);}
        for(size_t i=0;i<files.size();++i){std::error_code error;std::filesystem::rename(pending[i],files[i].path,error);if(error){std::filesystem::remove(files[i].path,error);std::filesystem::rename(pending[i],files[i].path,error);if(error)throw std::runtime_error("Cannot publish fabrication preview");}}
    }catch(...){for(const auto& path:pending){std::error_code error;std::filesystem::remove(path,error);}throw;}
    V result=V::makeObj(),paths=V::makeArr();for(const File& file:files){auto utf=file.path.u8string();paths.arr.push_back(V::makeStr(std::string(utf.begin(),utf.end())));}result.set("files",paths);result.set("drc_checked",V::makeBool(true));result.set("connectivity_complete",*need(board,"report").find("connectivity_complete"));result.set("fabrication_qualified",V::makeBool(false));return result;
}

} // namespace ark
