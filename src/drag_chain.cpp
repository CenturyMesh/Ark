#include "ark/drag_chain.hpp"
#include "ark/agent.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
// 050B native drag chain: recipe authoring, rigid-link kinematics, driven link followers
// and the read-only drag_chain_info tool. See docs/DRAG_CHAIN_050B.md.
namespace ark {
namespace {
using V=arkjson::Value;
constexpr double kOffsetTolerance=1e-3;   // mm: moving-end pin distance from the 2R moving run
constexpr double kParallelTolerance=1e-6; // |dc/dq| must be 1 within this
[[noreturn]] void fail(const std::string& code,const std::string& message){throw std::runtime_error("DRAG_CHAIN_"+code+": "+message);}
void need(bool ok,const char* code,const std::string& message){if(!ok)fail(code,message);}
std::string num(double value){char buf[48];std::snprintf(buf,sizeof buf,"%.6g",value);return buf;}
std::string id(u64 value){return "#"+std::to_string(value);}
void keys(const V& v,std::initializer_list<const char*> allowed,const std::string& what){need(v.t==V::Obj,"INVALID",what+" must be an object");for(const auto& p:v.obj){bool known=false;for(auto key:allowed)known|=p.first==key;need(known,"INVALID","unknown "+what+" field: "+p.first);}}
std::string chainName(const V& v,const char* key="name"){const auto* p=v.find(key);need(p&&p->t==V::Str&&!p->str.empty()&&p->str.size()<=128,"INVALID",std::string(key)+" requires 1..128 characters");for(unsigned char c:p->str)need(c>=32,"INVALID","names cannot contain control characters");return p->str;}
// Authored scalar: a finite number or a parameter expression, stored as given.
void scalarSource(const V& v,const std::string& key){need((v.t==V::Num&&std::isfinite(v.num))||(v.t==V::Str&&!v.str.empty()&&v.str.size()<=2048),"NONFINITE",key+" must be a finite number or parameter expression");}
double scalar(const V& v,const ExprEnv& env,const std::string& key){scalarSource(v,key);if(v.t==V::Num){need(std::fabs(v.num)<=1e6,"NONFINITE",key+" must be within 1e6");return v.num;}bool ok=false;const double x=evalExpr(v.str,env,&ok);need(ok&&std::isfinite(x)&&std::fabs(x)<=1e6,"NONFINITE",key+" = "+v.str+" must evaluate to a finite value within 1e6");return x;}
double expr(const std::string& text,const ExprEnv& env,const std::string& key){bool ok=false;const double x=evalExpr(text,env,&ok);need(ok&&std::isfinite(x)&&std::fabs(x)<=1e6,"NONFINITE",key+" = "+text+" must evaluate to a finite value within 1e6");return x;}
void vectorSource(const V& v,const std::string& key){need(v.t==V::Arr&&v.arr.size()==3,"INVALID",key+" must be [x,y,z]");for(const auto& item:v.arr)scalarSource(item,key);}
Vec3 vector(const V& v,const ExprEnv& env,const std::string& key){vectorSource(v,key);return {scalar(v.arr[0],env,key),scalar(v.arr[1],env,key),scalar(v.arr[2],env,key)};}
void directionSource(const V& v,const std::string& key){if(v.t==V::Str){static const std::set<std::string> axes{"+X","-X","+Y","-Y","+Z","-Z"};need(axes.count(v.str),"INVALID",key+" must be +X,-X,+Y,-Y,+Z,-Z or [x,y,z]");return;}vectorSource(v,key);}
Vec3 direction(const V& v,const ExprEnv& env,const std::string& key){directionSource(v,key);Vec3 out;if(v.t==V::Str){const double s=v.str[0]=='-'?-1:1;out=v.str[1]=='X'?Vec3{s,0,0}:v.str[1]=='Y'?Vec3{0,s,0}:Vec3{0,0,s};}else out=vector(v,env,key);need(out.len()>1e-9,"INVALID",key+" is a zero direction");return out.normalized();}
std::string withChain(const std::exception& e,const std::string& name){std::string text=e.what();const auto at=text.find(": ");if(text.rfind("DRAG_CHAIN_",0)!=0||at==std::string::npos)return "DRAG_CHAIN_INVALID: drag chain '"+name+"': "+text;return text.substr(0,at+2)+"drag chain '"+name+"': "+text.substr(at+2);}
const AssemblyNode* nodeById(const Document& d,u64 key){for(const auto& n:d.assembly)if(n.id==key)return &n;return nullptr;}
u64 occurrence(const Document& d,const V& v,const std::string& what){
    if(v.t==V::Num){need(std::isfinite(v.num)&&v.num>=1&&v.num==std::floor(v.num)&&v.num<=9007199254740990.,"UNKNOWN_OCCURRENCE",what+" must be an occurrence id or unique name");const u64 key=(u64)v.num;need(nodeById(d,key),"UNKNOWN_OCCURRENCE",what+" "+id(key)+" does not exist");return key;}
    need(v.t==V::Str&&!v.str.empty(),"UNKNOWN_OCCURRENCE",what+" must be an occurrence id or unique name");u64 found=0;
    for(const auto& n:d.assembly)if(n.name==v.str){need(!found,"UNKNOWN_OCCURRENCE",what+" name '"+v.str+"' is ambiguous; use the id");found=n.id;}
    need(found,"UNKNOWN_OCCURRENCE",what+" '"+v.str+"' does not exist");return found;
}
// Evaluated chain recipe. Link section expressions live on the link definition's p.
struct Chain {
    std::string name;u64 feature=0,group=0,anchorNode=0,carriage=0,joint=0;std::vector<u64> links;
    double pitch=0,width=0,height=0,innerWidth=0,innerHeight=0,radius=0,margin=0,maxSlack=0,qmin=0,qmax=0;
    bool explicitCount=false,travel=false;Vec3 anchorOrigin,anchorDirection,anchorBend,attach;
};
const char* kSectionKeys[]={"pitch","outer_width","outer_height","inner_width","inner_height"};
void readSection(Chain& c,const Feature& f,const ExprEnv& env){
    need(f.type==FeatureType::NativeSolid&&f.nativeSpec.find("op")&&f.nativeSpec.find("op")->asStr()=="drag_chain_link","LINK_DEFINITION","link definition "+id(f.id)+" must be native_solid op drag_chain_link");
    double* targets[]={&c.pitch,&c.width,&c.height,&c.innerWidth,&c.innerHeight};
    for(int i=0;i<5;++i){const auto found=f.p.find(kSectionKeys[i]);need(found!=f.p.end(),"LINK_DEFINITION","link definition lacks p."+std::string(kSectionKeys[i]));*targets[i]=expr(found->second,env,std::string("link.")+kSectionKeys[i]);}
}
Chain compile(const Document& d,const V& row,const ExprEnv& env,const std::unordered_map<u64,const AssemblyNode*>& nodes){
    Chain c;c.name=row.find("name")->str;
    try{
        auto node=[&](u64 key,const std::string& what){const auto found=nodes.find(key);need(found!=nodes.end(),"UNKNOWN_OCCURRENCE",what+" "+id(key)+" no longer exists");return found->second;};
        c.feature=(u64)row.find("link_feature")->num;const Feature* f=d.find(c.feature);need(f,"LINK_DEFINITION","link definition "+id(c.feature)+" no longer exists");readSection(c,*f,env);
        c.group=(u64)row.find("group")->num;need(node(c.group,"group")->feature==0,"LINK_DRIVEN","group "+id(c.group)+" must be an assembly group");
        for(const auto& item:row.find("links")->arr){const u64 key=(u64)item.num;const auto* n=node(key,"link occurrence");
            need(n->parent==c.group&&n->feature==c.feature&&!n->fixed&&!n->pattern.active&&n->p.empty(),"LINK_DRIVEN","link occurrence "+id(key)+" is driven by the chain: keep it under group "+id(c.group)+" on definition "+id(c.feature)+", unfixed, unpatterned and without p; edit the chain with drag_chain.set");c.links.push_back(key);}
        c.radius=scalar(*row.find("bend_radius"),env,"bend_radius");validateDragChainSection(c.pitch,c.width,c.height,c.innerWidth,c.innerHeight,c.radius);
        const V& anchor=*row.find("anchor");if(const auto* o=anchor.find("occurrence")){c.anchorNode=(u64)o->num;need(node(c.anchorNode,"anchor occurrence")->feature,"UNKNOWN_OCCURRENCE","anchor occurrence "+id(c.anchorNode)+" must be a part occurrence");}
        c.anchorOrigin=vector(*anchor.find("origin"),env,"anchor.origin");c.anchorDirection=direction(*anchor.find("direction"),env,"anchor.direction");c.anchorBend=direction(*anchor.find("bend"),env,"anchor.bend");
        need(std::fabs(c.anchorDirection.dot(c.anchorBend))<=1e-6,"NOT_PARALLEL","anchor.bend must be perpendicular to anchor.direction (dot "+num(c.anchorDirection.dot(c.anchorBend))+")");
        c.anchorBend=(c.anchorBend-c.anchorDirection*c.anchorDirection.dot(c.anchorBend)).normalized();
        const V& moving=*row.find("moving");c.carriage=(u64)moving.find("occurrence")->num;need(node(c.carriage,"moving occurrence")->feature,"UNKNOWN_OCCURRENCE","moving occurrence "+id(c.carriage)+" must be a part occurrence");
        c.attach=moving.find("attach")?vector(*moving.find("attach"),env,"moving.attach"):Vec3{};c.joint=(u64)moving.find("joint")->num;
        const auto mate=std::find_if(d.mates.begin(),d.mates.end(),[&](const AssemblyMate& m){return m.id==c.joint;});need(mate!=d.mates.end(),"UNKNOWN_MATE","driving joint mate #"+std::to_string(c.joint)+" no longer exists");need(mate->type=="prismatic","JOINT_NOT_PRISMATIC","driving joint mate #"+std::to_string(c.joint)+" has type "+mate->type+"; the carriage coordinate q is a prismatic joint in mm");
        c.explicitCount=row.find("link_count_source")->str=="explicit";
        c.margin=row.find("margin")?scalar(*row.find("margin"),env,"margin"):2*c.pitch;need(c.margin>=0,"NONFINITE","margin must be nonnegative");
        c.maxSlack=row.find("max_slack")?scalar(*row.find("max_slack"),env,"max_slack"):std::max(4*c.pitch,K_PI*c.radius);need(c.maxSlack>=0,"NONFINITE","max_slack must be nonnegative");
        if(const auto* travel=row.find("travel")){c.travel=true;c.qmin=scalar(*travel->find("min"),env,"travel.min");c.qmax=scalar(*travel->find("max"),env,"travel.max");need(c.qmin<c.qmax,"INVALID","travel.min must be below travel.max");}
    }catch(const std::exception& e){throw std::runtime_error(withChain(e,c.name));}
    return c;
}
std::unordered_map<u64,const AssemblyNode*> nodeMap(const Document& d){std::unordered_map<u64,const AssemblyNode*> out;for(const auto& n:d.assembly)out[n.id]=&n;return out;}
// World chain plane and the moving-end pin from copy-0 world frames; checks the 2R parallel offset.
DragChainGeometry geometryAt(const Chain& c,const std::unordered_map<u64,Mat4>& world,Vec3& pin,double& carriage){
    auto frame=[&](u64 key,const char* what){const auto found=world.find(key);need(found!=world.end(),"UNKNOWN_OCCURRENCE",std::string(what)+" occurrence "+id(key)+" is not placed");return found->second;};
    const Mat4 anchor=c.anchorNode?frame(c.anchorNode,"anchor"):Mat4::identity();DragChainGeometry g;g.pitch=c.pitch;g.radius=c.radius;g.links=(int)c.links.size();
    g.origin=anchor.transformPoint(c.anchorOrigin);g.direction=anchor.transformDir(c.anchorDirection).normalized();g.bend=anchor.transformDir(c.anchorBend);g.bend=(g.bend-g.direction*g.direction.dot(g.bend)).normalized();
    pin=frame(c.carriage,"moving").transformPoint(c.attach);const Vec3 gap=pin-g.origin,lateral=g.bend.cross(g.direction);
    const double offset=gap.dot(g.bend),side=gap.dot(lateral);carriage=gap.dot(g.direction);
    need(std::fabs(offset-2*c.radius)<=kOffsetTolerance&&std::fabs(side)<=kOffsetTolerance,"NOT_PARALLEL","the moving-end pin is "+num(offset)+" mm along bend and "+num(side)+" mm laterally from the fixed run; the runs must be parallel at 2R = "+num(2*c.radius)+" mm within "+num(kOffsetTolerance)+" mm");
    return g;
}
DragChainLayout layoutAt(const Chain& c,const std::unordered_map<u64,Mat4>& world,double* carriageOut=nullptr){
    try{Vec3 pin;double carriage=0;const auto g=geometryAt(c,world,pin,carriage);if(carriageOut)*carriageOut=carriage;return layoutDragChain(g,carriage);}
    catch(const std::exception& e){throw std::runtime_error(withChain(e,c.name));}
}
V vec(const Vec3& v){V a=V::makeArr();a.arr.push_back(V::makeNum(v.x));a.arr.push_back(V::makeNum(v.y));a.arr.push_back(V::makeNum(v.z));return a;}
V matrix(const Mat4& m){V a=V::makeArr();for(double x:m.m)a.arr.push_back(V::makeNum(x));return a;}
std::unordered_map<u64,Mat4> worldOf(const std::vector<AssemblyPlacement>& placements){std::unordered_map<u64,Mat4> world;for(const auto& p:placements)if(!p.copy)world[p.node]=p.world;return world;}
// Driving joint of a chain in a manipulation program (motion bindings use the same selection).
AssemblyManipulationJoint drivingJoint(const AssemblyManipulationProgram& program,u64 mate){
    const auto joint=std::find_if(program.joints.begin(),program.joints.end(),[&](const AssemblyManipulationJoint& j){return j.mate==mate&&j.mode=="translate";});
    need(joint!=program.joints.end(),"JOINT_NOT_DRIVABLE","mate #"+std::to_string(mate)+" has no grounded moving side; ground its support with assembly.edit fixed:true");
    AssemblyManipulationJoint selected;std::string reason;need(selectAssemblyManipulationJoint(program,joint->movingEndpoint,mate,"translate",selected,reason),"JOINT_NOT_DRIVABLE","mate #"+std::to_string(mate)+": "+reason);return selected;
}
// Copy-0 world frames at joint coordinate q through the same preview solver as assembly.drive.
std::unordered_map<u64,Mat4> worldAtQ(const AssemblyManipulationProgram& program,const AssemblyManipulationJoint& joint,double q){
    if(std::fabs(q-joint.position)<=1e-12)return worldOf(program.baseline);
    AssemblyManipulationPreview preview;std::string reason;need(previewAssemblyManipulation(program,joint.movingEndpoint,joint.mate,"translate",(q-joint.position)*joint.coordinateSign,preview,reason),"JOINT_NOT_DRIVABLE","mate #"+std::to_string(joint.mate)+" cannot reach q = "+num(q)+" mm: "+reason);
    return worldOf(preview.placements);
}
double requiredLength(const Chain& c,double reach){return K_PI*c.radius+reach;}
V layoutJson(const Chain& c,const DragChainLayout& layout,bool poses){
    V out=V::makeObj();const double total=c.links.size()*c.pitch,continuous=dragChainContinuousFixedRun(total,c.radius,layout.carriage);
    out.set("carriage_mm",V::makeNum(layout.carriage));out.set("fixed_run_mm",V::makeNum(layout.fixedRun));out.set("moving_run_mm",V::makeNum(layout.movingRun));
    V loop=V::makeObj();loop.set("tangent_along_run_mm",V::makeNum(layout.fixedRun));loop.set("apex_along_run_mm",V::makeNum(layout.fixedRun+c.radius));loop.set("apex_mm",vec(layout.apex));out.set("loop",loop);
    out.set("continuous_fixed_run_mm",V::makeNum(continuous));out.set("polygon_offset_mm",V::makeNum(layout.fixedRun-continuous));
    V segments=V::makeObj();segments.set("fixed_links",V::makeNum(layout.fixedLinks));segments.set("bend_links",V::makeNum(layout.bendLinks));segments.set("moving_links",V::makeNum(layout.movingLinks));out.set("segments",segments);
    double spacing=0;bool monotonic=true;for(size_t i=0;i+1<layout.pins.size();++i){spacing=std::max(spacing,std::fabs((layout.pins[i+1]-layout.pins[i]).len()-c.pitch));monotonic&=layout.arc[i+1]>layout.arc[i];}
    V checks=V::makeObj();checks.set("max_pin_spacing_error_mm",V::makeNum(spacing));checks.set("arc_length_monotonic",V::makeBool(monotonic));out.set("checks",checks);
    V rows=V::makeArr();const int n=(int)c.links.size();
    for(int i=0;i<n;++i){const bool fixed=i<layout.fixedLinks,movingLink=i>=n-layout.movingLinks;
        if(!poses&&!(i==0||i==n-1||(!fixed&&!movingLink)||i==layout.fixedLinks-1||i==n-layout.movingLinks))continue;
        V row=V::makeObj();row.set("index",V::makeNum(i));row.set("node",V::makeNum((double)c.links[(size_t)i]));row.set("segment",V::makeStr(fixed?"fixed":movingLink?"moving":"bend"));
        row.set("rear_pin_mm",vec(layout.pins[(size_t)i]));row.set("front_pin_mm",vec(layout.pins[(size_t)i+1]));row.set("rear_arc_mm",V::makeNum(layout.arc[(size_t)i]));if(poses)row.set("world_column_major",matrix(layout.links[(size_t)i]));rows.arr.push_back(row);}
    out.set(poses?"links":"links_summary",rows);if(!poses)out.set("links_summary_scope",V::makeStr("first, last, run ends and every bend link; poses:true lists all links with world_column_major"));
    return out;
}
// Joint coordinate q of the driving mate at the resolved static pose.
bool staticQ(const AssemblyDiagnostics& diagnostics,u64 mate,double& q){for(const auto& row:diagnostics.mates)if(row.id==mate&&row.hasCoordinate){q=row.coordinate;return true;}return false;}
}

void validateDragChainSection(double p,double w,double h,double wi,double hi,double r){
    for(double x:{p,w,h,wi,hi,r})need(std::isfinite(x)&&x>0,"NONFINITE","link pitch, widths, heights and bend_radius must be finite and positive");
    need(p<=1000&&w<=1000&&h<=1000&&r<=10000,"GEOMETRY","pitch, outer width and height are limited to 1000 mm and bend_radius to 10000 mm");
    need(wi<w&&hi<h,"GEOMETRY","inner cable space ("+num(wi)+" x "+num(hi)+" mm) must be smaller than the outer section ("+num(w)+" x "+num(h)+" mm)");
    need((w-wi)/4>=0.25,"GEOMETRY","each side wall holds two plate layers: (outer_width-inner_width)/4 = "+num((w-wi)/4)+" mm must be at least 0.25 mm");
    need((h-hi)/2>=0.25,"GEOMETRY","crossbar thickness (outer_height-inner_height)/2 = "+num((h-hi)/2)+" mm must be at least 0.25 mm");
    // Adjacent links rotate about their shared pin by the bend articulation angle; their full-height
    // centre sections (0.3*pitch from each pin) clear only while (H/2)*tan(angle/2) < 0.29*pitch.
    const double limit=std::max(p/(2*std::sin(std::atan(0.58*p/h))),h/2+0.05);need(r>=p,"BEND_TOO_TIGHT","bend_radius "+num(r)+" mm is below the pitch "+num(p)+" mm");
    const double half=std::asin(p/(2*r));need(h/2*std::tan(half)<0.29*p&&r*std::cos(half)-h/2>=0.05,"BEND_TOO_TIGHT","bend_radius "+num(r)+" mm is too tight for pitch "+num(p)+" mm and outer height "+num(h)+" mm: adjacent links would collide in the bend; use at least "+num(std::max(limit,p))+" mm");
}

DragChainLayout layoutDragChain(const DragChainGeometry& g,double c){
    const double p=g.pitch,R=g.radius;const int N=g.links;
    need(std::isfinite(p)&&std::isfinite(R)&&std::isfinite(c)&&p>0&&R>=p&&N>=1&&N<=kMaxDragChainLinks,"GEOMETRY","finite pitch, bend radius >= pitch and 1..400 links are required");
    const double alpha=2*std::asin(p/(2*R));
    // First bend pin after a straight pin x behind the tangent point: |P(phi)-P|=p on the circle.
    auto firstAngle=[&](double x){double lo=0,hi=K_PI/2;for(int i=0;i<200&&hi>lo;++i){const double mid=.5*(lo+hi),s=std::sin(mid/2),d2=4*R*R*s*s+x*x+2*x*R*std::sin(mid);if(mid<=lo||mid>=hi)break;(d2<p*p?lo:hi)=mid;}return .5*(lo+hi);};
    struct Walk {bool feasible=false;int m=0,k=0;double x=0,phi=0,u0=0,carriage=0;};
    // Walk the pins for a bend tangent point at fixedRun: m+1 straight pins, k pins on the circle,
    // then the moving straight. carriage is where pin N then lies; it increases with fixedRun.
    auto walk=[&](double fixedRun){Walk w;w.m=(int)std::floor(fixedRun/p);w.x=fixedRun-w.m*p;if(w.x>=p){++w.m;w.x-=p;}if(w.x<0)w.x=0;
        if(w.m>=N)return w;w.phi=firstAngle(w.x);w.k=1+(int)std::floor((K_PI-w.phi)/alpha+1e-12);if(w.m+w.k>=N)return w;
        const double last=w.phi+(w.k-1)*alpha,h=R*(1+std::cos(last));w.u0=std::max(0.,std::sqrt(std::max(0.,p*p-h*h))-R*std::sin(last));
        w.carriage=fixedRun-(w.u0+(N-(w.m+w.k+1))*p);w.feasible=true;return w;};
    const Walk start=walk(0);
    need(start.feasible,"TOO_SHORT",std::to_string(N)+" links of pitch "+num(p)+" mm cannot pass a bend of radius "+num(R)+" mm and reach the moving run");
    need(start.carriage<=c+1e-9,"RUN_NEGATIVE","the fixed run would be "+num((c-start.carriage)/2)+" mm at carriage coordinate "+num(c)+" mm: the chain is too short for this travel");
    double lo=0,hi=N*p;for(int i=0;i<300;++i){const double mid=.5*(lo+hi);if(mid<=lo||mid>=hi)break;const auto w=walk(mid);(w.feasible&&w.carriage<=c?lo:hi)=mid;}
    const Walk w=walk(lo);
    need(w.feasible&&std::fabs(w.carriage-c)<=1e-6,"RUN_NEGATIVE","the moving run would be "+num(-(c-(w.feasible?w.carriage:lo))/2)+" mm at carriage coordinate "+num(c)+" mm: the chain is too short for this travel");
    const Vec3 A=g.origin,d=g.direction,n=g.bend,axis=n.cross(d),C=A+n*(2*R)+d*c,O=A+d*lo+n*R;const int j0=w.m+w.k+1;
    DragChainLayout out;out.carriage=c;out.fixedRun=lo;out.movingRun=lo-c;out.fixedLinks=w.m;out.movingLinks=N-j0;out.bendLinks=N-out.fixedLinks-out.movingLinks;out.apex=O+d*R;
    out.pins.resize((size_t)N+1);out.arc.resize((size_t)N+1);
    for(int i=0;i<=N;++i){
        if(i<=w.m){out.pins[(size_t)i]=A+d*(i*p);out.arc[(size_t)i]=i*p;}
        else if(i<j0){const double phi=w.phi+(i-w.m-1)*alpha;out.pins[(size_t)i]=O-n*(R*std::cos(phi))+d*(R*std::sin(phi));out.arc[(size_t)i]=lo+R*phi;}
        else{out.pins[(size_t)i]=C+d*((N-i)*p);out.arc[(size_t)i]=lo+K_PI*R+(lo-c)-(N-i)*p;}
    }
    out.links.resize((size_t)N);
    for(int i=0;i<N;++i){const Vec3 x=i<w.m?d:i>=j0?d*-1.:(out.pins[(size_t)i+1]-out.pins[(size_t)i]).normalized(),z=x.cross(axis);Mat4 m;
        m.m[0]=x.x;m.m[1]=x.y;m.m[2]=x.z;m.m[4]=axis.x;m.m[5]=axis.y;m.m[6]=axis.z;m.m[8]=z.x;m.m[9]=z.y;m.m[10]=z.z;const Vec3& o=out.pins[(size_t)i];m.m[12]=o.x;m.m[13]=o.y;m.m[14]=o.z;out.links[(size_t)i]=m;}
    return out;
}

const V* findDragChain(const Document& d,const std::string& name){if(d.dragChains.t!=V::Arr)return nullptr;for(const auto& row:d.dragChains.arr)if(const auto* key=row.find("name");key&&key->t==V::Str&&key->str==name)return &row;return nullptr;}
std::string dragChainOfNode(const Document& d,u64 node,int* index){
    if(d.dragChains.t!=V::Arr)return {};
    for(const auto& row:d.dragChains.arr)if(const auto* links=row.find("links"))for(size_t i=0;i<links->arr.size();++i)if((u64)links->arr[i].num==node){if(index)*index=(int)i;return row.find("name")->str;}
    return {};
}
void validateDragChainStorage(const V& chains){
    if(chains.t==V::Null)return;need(chains.t==V::Arr&&!chains.arr.empty()&&chains.arr.size()<=kMaxDragChains,"CAPACITY","drag_chains must be a nonempty array of at most 16 chains");
    auto identity=[](const V* v,const std::string& what){need(v&&v->t==V::Num&&std::isfinite(v->num)&&v->num>=1&&v->num==std::floor(v->num)&&v->num<=9007199254740990.,"INVALID",what+" must be a positive integer id");};
    std::set<std::string> names;std::set<u64> owned;
    for(const auto& row:chains.arr){
        keys(row,{"name","link_feature","group","links","bend_radius","anchor","moving","link_count_source","margin","travel","max_slack"},"drag chain");const auto name=chainName(row);need(names.insert(name).second,"NAME_CONFLICT","duplicate drag chain "+name);
        identity(row.find("link_feature"),"link_feature");identity(row.find("group"),"group");
        const auto* links=row.find("links");need(links&&links->t==V::Arr&&!links->arr.empty()&&links->arr.size()<=(size_t)kMaxDragChainLinks,"CAPACITY","drag chain "+name+" requires 1..400 link occurrences");
        for(const auto& link:links->arr){identity(&link,"link occurrence");need(owned.insert((u64)link.num).second,"LINK_DRIVEN","occurrence #"+link.dump()+" belongs to more than one drag chain");}
        const auto* source=row.find("link_count_source");need(source&&(source->asStr()=="derived"||source->asStr()=="explicit"),"INVALID","link_count_source must be derived or explicit");
        const auto* radius=row.find("bend_radius");need(radius,"INVALID","bend_radius is required");scalarSource(*radius,"bend_radius");
        const auto* anchor=row.find("anchor");need(anchor,"INVALID","anchor is required");keys(*anchor,{"occurrence","origin","direction","bend"},"anchor");if(anchor->find("occurrence"))identity(anchor->find("occurrence"),"anchor.occurrence");
        need(anchor->find("origin")&&anchor->find("direction")&&anchor->find("bend"),"INVALID","anchor requires origin, direction and bend");vectorSource(*anchor->find("origin"),"anchor.origin");directionSource(*anchor->find("direction"),"anchor.direction");directionSource(*anchor->find("bend"),"anchor.bend");
        const auto* moving=row.find("moving");need(moving,"INVALID","moving is required");keys(*moving,{"occurrence","attach","joint"},"moving");identity(moving->find("occurrence"),"moving.occurrence");identity(moving->find("joint"),"moving.joint");if(moving->find("attach"))vectorSource(*moving->find("attach"),"moving.attach");
        for(const char* key:{"margin","max_slack"})if(const auto* value=row.find(key))scalarSource(*value,key);
        if(const auto* travel=row.find("travel")){keys(*travel,{"min","max"},"travel");need(travel->find("min")&&travel->find("max"),"INVALID","travel requires min and max");scalarSource(*travel->find("min"),"travel.min");scalarSource(*travel->find("max"),"travel.max");}
    }
}

AssemblyFollowers assemblyFollowers(const Document& d){
    AssemblyFollowers out;if(d.dragChains.t!=V::Arr||d.dragChains.arr.empty())return out;
    validateDragChainStorage(d.dragChains);const auto env=d.buildEnv();const auto nodes=nodeMap(d);auto chains=std::make_shared<std::vector<Chain>>();
    for(const auto& row:d.dragChains.arr){chains->push_back(compile(d,row,env,nodes));for(u64 key:chains->back().links)out.nodes.insert(key);}
    for(const auto& c:*chains)need(!out.nodes.count(c.carriage)&&!out.nodes.count(c.anchorNode),"LINK_DRIVEN","drag chain '"+c.name+"' attaches to another chain's link occurrence");
    for(const auto& m:d.mates)need(!out.nodes.count(m.a)&&!out.nodes.count(m.b),"LINK_DRIVEN","mate #"+std::to_string(m.id)+" references a drag chain link occurrence; links are driven by their chain and cannot be mated");
    out.pose=[chains](std::unordered_map<u64,Mat4>& world){for(const auto& c:*chains){const auto layout=layoutAt(c,world);for(size_t i=0;i<c.links.size();++i)world[c.links[i]]=layout.links[i];}};
    return out;
}

V dragChainState(const Document& d,const std::string& name,const std::unordered_map<u64,Mat4>& world){
    const auto* row=findDragChain(d,name);need(row,"NOT_FOUND",name);const auto c=compile(d,*row,d.buildEnv(),nodeMap(d));
    V out=layoutJson(c,layoutAt(c,world),false);out.set("name",V::makeStr(c.name));out.set("link_count",V::makeNum((double)c.links.size()));out.set("pitch_mm",V::makeNum(c.pitch));out.set("bend_radius_mm",V::makeNum(c.radius));out.set("joint",V::makeNum((double)c.joint));return out;
}

namespace {
void removeChain(Document& d,const V& command){
    keys(command,{"op","name"},"drag_chain.remove");const auto key=chainName(command);need(d.dragChains.t==V::Arr,"NOT_FOUND",key);
    auto rows=d.dragChains;auto at=std::find_if(rows.arr.begin(),rows.arr.end(),[&](const V& row){return row.find("name")->str==key;});need(at!=rows.arr.end(),"NOT_FOUND",key);
    std::set<u64> owned{(u64)at->find("group")->num};for(const auto& link:at->find("links")->arr)owned.insert((u64)link.num);const u64 feature=(u64)at->find("link_feature")->num;
    d.assembly.erase(std::remove_if(d.assembly.begin(),d.assembly.end(),[&](const AssemblyNode& n){return owned.count(n.id);}),d.assembly.end());
    const bool used=std::any_of(d.assembly.begin(),d.assembly.end(),[&](const AssemblyNode& n){return n.feature==feature;})||std::any_of(d.features.begin(),d.features.end(),[&](const Feature& f){return std::find(f.inputs.begin(),f.inputs.end(),feature)!=f.inputs.end();});
    if(!used)d.remove(feature);
    rows.arr.erase(at);d.dragChains=rows.arr.empty()?V{}:std::move(rows);
}
void setChain(Document& d,const V& command){
    keys(command,{"op","name","link","bend_radius","anchor","moving","link_count","margin","travel","max_slack"},"drag_chain.set");const auto key=chainName(command);
    need(!d.historyTip,"INVALID","resume full feature history before drag_chain.set adds or edits its link definition");
    const auto env=d.buildEnv();
    try{
        // Link section: stored on the native link definition's p.
        const auto* link=command.find("link");need(link,"INVALID","link{pitch,outer_width,outer_height,inner_width?,inner_height?} is required");keys(*link,{"pitch","outer_width","outer_height","inner_width","inner_height"},"link");
        std::map<std::string,std::string> section;
        for(const char* k:kSectionKeys){const auto* v=link->find(k);const std::string label=std::string("link.")+k;
            if(!v){need(std::string(k).rfind("inner_",0)==0,"INVALID",label+" is required");const std::string outer=section[std::string("outer_")+(k[6]=='w'?"width":"height")];section[k]="("+outer+")*0.8";continue;}
            scalarSource(*v,label);section[k]=v->t==V::Num?v->dump():v->str;}
        double values[5];for(int i=0;i<5;++i)values[i]=expr(section[kSectionKeys[i]],env,std::string("link.")+kSectionKeys[i]);
        const auto* radiusSource=command.find("bend_radius");need(radiusSource,"INVALID","bend_radius is required");const double radius=scalar(*radiusSource,env,"bend_radius");
        validateDragChainSection(values[0],values[1],values[2],values[3],values[4],radius);
        // Anchor and moving end, bound to occurrence ids.
        V record=V::makeObj();record.set("name",V::makeStr(key));record.set("bend_radius",*radiusSource);
        const auto* anchorSource=command.find("anchor");need(anchorSource,"INVALID","anchor{occurrence?,origin,direction,bend} is required");keys(*anchorSource,{"occurrence","origin","direction","bend"},"anchor");
        need(anchorSource->find("origin")&&anchorSource->find("direction")&&anchorSource->find("bend"),"INVALID","anchor requires origin, direction and bend");V anchor=*anchorSource;
        if(const auto* o=anchorSource->find("occurrence")){const u64 node=occurrence(d,*o,"anchor.occurrence");need(nodeById(d,node)->feature,"UNKNOWN_OCCURRENCE","anchor.occurrence must be a part occurrence");anchor.set("occurrence",V::makeNum((double)node));}
        record.set("anchor",anchor);
        const auto* movingSource=command.find("moving");need(movingSource,"INVALID","moving{occurrence,attach?,joint} is required");keys(*movingSource,{"occurrence","attach","joint"},"moving");
        need(movingSource->find("occurrence")&&movingSource->find("joint"),"INVALID","moving requires occurrence and joint");V moving=*movingSource;
        const u64 carriage=occurrence(d,*movingSource->find("occurrence"),"moving.occurrence");need(nodeById(d,carriage)->feature,"UNKNOWN_OCCURRENCE","moving.occurrence must be a part occurrence");moving.set("occurrence",V::makeNum((double)carriage));
        const auto* jointSource=movingSource->find("joint");need(jointSource->t==V::Num&&std::isfinite(jointSource->num)&&jointSource->num>=1&&jointSource->num==std::floor(jointSource->num),"UNKNOWN_MATE","moving.joint must be a numeric mate id");
        const u64 mateId=(u64)jointSource->num;const auto mate=std::find_if(d.mates.begin(),d.mates.end(),[&](const AssemblyMate& m){return m.id==mateId;});need(mate!=d.mates.end(),"UNKNOWN_MATE","moving.joint mate #"+std::to_string(mateId)+" does not exist");
        need(mate->type=="prismatic","JOINT_NOT_PRISMATIC","moving.joint mate #"+std::to_string(mateId)+" has type "+mate->type+"; drive the carriage with a prismatic joint");
        record.set("moving",moving);
        int requestedCount=0;if(const auto* count=command.find("link_count")){need(count->t==V::Num&&std::isfinite(count->num)&&count->num==std::floor(count->num),"INVALID","link_count must be an integer");need(count->num>=1&&count->num<=kMaxDragChainLinks,"CAPACITY","link_count "+count->dump()+" is outside 1..400");requestedCount=(int)count->num;}
        for(const char* k:{"margin","max_slack"})if(const auto* value=command.find(k)){need(scalar(*value,env,k)>=0,"NONFINITE",std::string(k)+" must be nonnegative");record.set(k,*value);}
        if(const auto* travel=command.find("travel")){keys(*travel,{"min","max"},"travel");need(travel->find("min")&&travel->find("max"),"INVALID","travel requires min and max");record.set("travel",*travel);}
        // Existing chain keeps its definition, group and leading link identities.
        V rows=d.dragChains.t==V::Arr?d.dragChains:V::makeArr();auto at=std::find_if(rows.arr.begin(),rows.arr.end(),[&](const V& row){return row.find("name")->str==key;});const bool existing=at!=rows.arr.end();
        need(existing||rows.arr.size()<kMaxDragChains,"CAPACITY","a document holds at most 16 drag chains");
        std::set<u64> otherLinks;for(const auto& row:rows.arr)if(row.find("name")->str!=key)for(const auto& link:row.find("links")->arr)otherLinks.insert((u64)link.num);
        std::set<u64> ownLinks;if(existing)for(const auto& link:at->find("links")->arr)ownLinks.insert((u64)link.num);
        for(u64 node:{carriage,anchor.find("occurrence")?(u64)anchor.find("occurrence")->num:0})need(!otherLinks.count(node)&&!ownLinks.count(node),"LINK_DRIVEN","the anchor and moving occurrences cannot be drag chain links");
        // Probe the carriage over the travel with this chain removed: the joint solver and the
        // assembly.drive preview decide where the moving-end pin goes for each q.
        Chain probe;probe.name=key;probe.pitch=values[0];probe.width=values[1];probe.height=values[2];probe.innerWidth=values[3];probe.innerHeight=values[4];probe.radius=radius;probe.carriage=carriage;probe.joint=mateId;
        probe.anchorNode=anchor.find("occurrence")?(u64)anchor.find("occurrence")->num:0;probe.anchorOrigin=vector(*anchor.find("origin"),env,"anchor.origin");probe.anchorDirection=direction(*anchor.find("direction"),env,"anchor.direction");probe.anchorBend=direction(*anchor.find("bend"),env,"anchor.bend");
        need(std::fabs(probe.anchorDirection.dot(probe.anchorBend))<=1e-6,"NOT_PARALLEL","anchor.bend must be perpendicular to anchor.direction");probe.anchorBend=(probe.anchorBend-probe.anchorDirection*probe.anchorDirection.dot(probe.anchorBend)).normalized();
        probe.attach=moving.find("attach")?vector(*moving.find("attach"),env,"moving.attach"):Vec3{};probe.links.assign(1,0);
        probe.margin=record.find("margin")?scalar(*record.find("margin"),env,"margin"):2*probe.pitch;probe.maxSlack=record.find("max_slack")?scalar(*record.find("max_slack"),env,"max_slack"):std::max(4*probe.pitch,K_PI*radius);
        if(const auto* travel=record.find("travel")){probe.travel=true;probe.qmin=scalar(*travel->find("min"),env,"travel.min");probe.qmax=scalar(*travel->find("max"),env,"travel.max");need(probe.qmin<probe.qmax,"INVALID","travel.min must be below travel.max");}
        struct Sample {double q,c;bool end;};std::vector<Sample> samples;std::string travelSource="none";
        {
            struct Restore {Document& d;V saved;~Restore(){d.dragChains=std::move(saved);}} restore{d,d.dragChains};
            if(existing){V without=rows;without.arr.erase(without.arr.begin()+(at-rows.arr.begin()));d.dragChains=without.arr.empty()?V{}:std::move(without);}
            AssemblyManipulationProgram program;std::string reason;need(buildAssemblyManipulation(d,program,reason),"JOINT_NOT_DRIVABLE","assembly solve failed: "+reason);
            const auto joint=drivingJoint(program,mateId);
            if(!probe.travel&&joint.limited){probe.travel=true;probe.qmin=joint.min;probe.qmax=joint.max;travelSource="joint_limits";}else if(probe.travel)travelSource="explicit";
            if(probe.travel)need(joint.position>=probe.qmin-1e-6&&joint.position<=probe.qmax+1e-6,"TRAVEL_EXCLUDES_POSE","the current joint coordinate q = "+num(joint.position)+" mm lies outside travel ["+num(probe.qmin)+", "+num(probe.qmax)+"]");
            std::vector<std::pair<double,bool>> qs{{joint.position,false}};
            if(probe.travel){qs.push_back({probe.qmin,true});qs.push_back({probe.qmax,true});}
            else{const double step=joint.limited&&joint.position+1>joint.max?-1:1;qs.push_back({joint.position+step,false});}
            for(const auto& [q,end]:qs){const auto world=worldAtQ(program,joint,q);Vec3 pin;double c=0;
                try{(void)geometryAt(probe,world,pin,c);}catch(const std::exception& e){throw std::runtime_error(std::string(e.what())+" at q = "+num(q)+" mm");}samples.push_back({q,c,end});}
            const auto& a=samples[probe.travel?1:0];const auto& b=samples[probe.travel?2:1];const double kappa=(b.c-a.c)/(b.q-a.q);
            need(std::fabs(std::fabs(kappa)-1)<=kParallelTolerance,"CARRIAGE_NOT_DRIVEN","the moving-end pin moves "+num(kappa)+" mm along the run per mm of mate #"+std::to_string(mateId)+"; the joint must translate the moving occurrence along anchor.direction (parallel runs)");
            if(probe.travel)need(std::fabs(samples[0].c-(a.c+kappa*(samples[0].q-a.q)))<=kOffsetTolerance,"CARRIAGE_NOT_DRIVEN","the moving-end pin does not follow mate #"+std::to_string(mateId)+" linearly over the travel");
        }
        double reach=0;for(const auto& s:samples)if(s.end||!probe.travel)reach=std::max(reach,std::fabs(s.c));if(probe.travel)reach=std::max(reach,std::fabs(samples[0].c));
        const double required=requiredLength(probe,reach);
        int count=requestedCount?requestedCount:(int)std::ceil((required+probe.margin)/probe.pitch-1e-9);
        need(count>=1&&count<=kMaxDragChainLinks,"CAPACITY","the chain needs "+std::to_string(count)+" links; 1..400 are supported");
        // The rigid-link solution is the authority for feasibility at every probed pose.
        for(int attempt=0;;++attempt){
            bool feasible=true;std::string failure;
            for(const auto& s:samples){if(!probe.travel&&&s!=&samples[0])continue;DragChainGeometry g;g.pitch=probe.pitch;g.radius=radius;g.links=count;g.origin=Vec3{};g.direction={1,0,0};g.bend={0,1,0};
                try{(void)layoutDragChain(g,s.c);}catch(const std::exception& e){feasible=false;failure=e.what();
                    if(requestedCount){const auto colon=failure.find(": ");const std::string detail=colon==std::string::npos?failure:failure.substr(colon+2);
                        if(failure.find("TOO_SHORT")!=std::string::npos)fail("TOO_SHORT",detail);
                        if(s.end)fail("TRAVEL_RUN_NEGATIVE","at q = "+num(s.q)+" mm "+detail+" (travel "+travelSource+" ["+num(probe.qmin)+", "+num(probe.qmax)+"]); add links or reduce the travel");
                        fail("TOO_SHORT","at the current q = "+num(s.q)+" mm "+detail);}
                    break;}}
            if(feasible)break;need(!requestedCount&&attempt<4&&count<kMaxDragChainLinks,"CAPACITY",failure);++count;
        }
        if(probe.travel&&requestedCount){const double slack=count*probe.pitch-required;need(slack<=probe.maxSlack+1e-9,"TOO_LONG",std::to_string(count)+" links ("+num(count*probe.pitch)+" mm) exceed the "+num(required)+" mm the travel needs by "+num(slack)+" mm, more than max_slack "+num(probe.maxSlack)+" mm: the loop would overhang the stroke; remove links or raise max_slack explicitly");}
        record.set("link_count_source",V::makeStr(requestedCount?"explicit":"derived"));
        // Definition, group and link occurrences. Leading identities are retained on replace.
        u64 feature=existing?(u64)at->find("link_feature")->num:0,group=existing?(u64)at->find("group")->num:0;std::vector<u64> links;if(existing)for(const auto& link:at->find("links")->arr)links.push_back((u64)link.num);
        Feature* definition=feature?d.find(feature):nullptr;
        if(!definition){Feature f;f.type=FeatureType::NativeSolid;std::string base=key+" link",unique=base;for(int k=2;std::any_of(d.features.begin(),d.features.end(),[&](const Feature& x){return x.name==unique;});++k)unique=base+" "+std::to_string(k);
            f.name=unique;f.nativeSpec=V::makeObj();f.nativeSpec.set("op",V::makeStr("drag_chain_link"));arkjson::parse(R"({"base_color":[0.13,0.13,0.14],"roughness":0.62})",f.appearance);feature=d.addFeature(std::move(f));definition=d.find(feature);}
        for(int i=0;i<5;++i)definition->p[kSectionKeys[i]]=section[kSectionKeys[i]];
        if(!group||!nodeById(d,group)){AssemblyNode n;n.id=d.nextAssemblyId_++;n.name=key;n.feature=0;group=n.id;d.assembly.push_back(n);}
        if((int)links.size()>count){std::set<u64> drop(links.begin()+count,links.end());links.resize((size_t)count);d.assembly.erase(std::remove_if(d.assembly.begin(),d.assembly.end(),[&](const AssemblyNode& n){return drop.count(n.id);}),d.assembly.end());}
        need(d.assembly.size()+(size_t)count-links.size()<=4096,"CAPACITY","the links would exceed 4096 assembly nodes");
        while((int)links.size()<count){AssemblyNode n;n.id=d.nextAssemblyId_++;n.parent=group;n.feature=feature;n.name=key+" link "+std::to_string(links.size()+1);links.push_back(n.id);d.assembly.push_back(n);}
        for(auto& n:d.assembly)if(std::find(links.begin(),links.end(),n.id)!=links.end()){n.parent=group;n.feature=feature;}
        record.set("link_feature",V::makeNum((double)feature));record.set("group",V::makeNum((double)group));V ids=V::makeArr();for(u64 link:links)ids.arr.push_back(V::makeNum((double)link));record.set("links",ids);
        if(existing)*at=record;else rows.arr.push_back(record);validateDragChainStorage(rows);d.dragChains=std::move(rows);
    }catch(const std::exception& e){throw std::runtime_error(withChain(e,key));}
}
V chainInfo(Document& d,const V& row,const V* qArg,bool poses){
    const auto c=compile(d,row,d.buildEnv(),nodeMap(d));V out=V::makeObj(),warnings=V::makeArr();
    out.set("name",V::makeStr(c.name));out.set("link_feature",V::makeNum((double)c.feature));out.set("group",V::makeNum((double)c.group));out.set("link_count",V::makeNum((double)c.links.size()));out.set("link_count_source",*row.find("link_count_source"));
    V link=V::makeObj();link.set("pitch_mm",V::makeNum(c.pitch));link.set("outer_width_mm",V::makeNum(c.width));link.set("outer_height_mm",V::makeNum(c.height));link.set("inner_width_mm",V::makeNum(c.innerWidth));link.set("inner_height_mm",V::makeNum(c.innerHeight));out.set("link",link);
    out.set("bend_radius_mm",V::makeNum(c.radius));out.set("anchor",*row.find("anchor"));out.set("moving",*row.find("moving"));
    AssemblyManipulationProgram program;std::string reason;need(buildAssemblyManipulation(d,program,reason),"JOINT_NOT_DRIVABLE","assembly solve failed: "+reason);
    double q=0;const bool known=staticQ(program.diagnostics,c.joint,q);std::unordered_map<u64,Mat4> world=worldOf(program.baseline);
    std::unique_ptr<AssemblyManipulationJoint> joint;try{joint=std::make_unique<AssemblyManipulationJoint>(drivingJoint(program,c.joint));q=joint->position;}catch(const std::exception& e){if(qArg)throw;warnings.arr.push_back(V::makeStr(e.what()));}
    if(qArg){need(qArg->t==V::Num&&std::isfinite(qArg->num)&&std::fabs(qArg->num)<=1e6,"NONFINITE","q must be a finite joint coordinate in mm");world=worldAtQ(program,*joint,qArg->num);q=qArg->num;out.set("evaluated_at_q",V::makeBool(true));}
    if(known||joint)out.set("q_mm",V::makeNum(q));out.set("joint",V::makeNum((double)c.joint));
    const auto layout=layoutAt(c,world);out.set("state",layoutJson(c,layout,poses));
    double qmin=c.qmin,qmax=c.qmax;std::string source=c.travel?"explicit":"none";if(!c.travel&&joint&&joint->limited){qmin=joint->min;qmax=joint->max;source="joint_limits";}
    V travel=V::makeObj();travel.set("source",V::makeStr(source));double reach=std::fabs(layout.carriage);
    if(source!="none"){travel.set("min_q_mm",V::makeNum(qmin));travel.set("max_q_mm",V::makeNum(qmax));if(q<qmin-1e-6||q>qmax+1e-6)warnings.arr.push_back(V::makeStr("q = "+num(q)+" mm lies outside the travel"));
        if(joint)for(const auto& [label,value]:{std::pair<const char*,double>{"at_min",qmin},{"at_max",qmax}}){V end=V::makeObj();end.set("q_mm",V::makeNum(value));
            try{double carriage=0;const auto ends=layoutAt(c,worldAtQ(program,*joint,value),&carriage);end.set("carriage_mm",V::makeNum(carriage));end.set("fixed_run_mm",V::makeNum(ends.fixedRun));end.set("moving_run_mm",V::makeNum(ends.movingRun));end.set("loop_apex_mm",vec(ends.apex));reach=std::max(reach,std::fabs(carriage));}
            catch(const std::exception& e){end.set("error",V::makeStr(e.what()));warnings.arr.push_back(V::makeStr(e.what()));}travel.set(label,end);}}
    else warnings.arr.push_back(V::makeStr("travel unknown: no travel range and an unlimited joint; only the current pose was validated"));
    out.set("travel",travel);
    const double total=c.links.size()*c.pitch,required=requiredLength(c,reach);V budget=V::makeObj();
    budget.set("chain_length_mm",V::makeNum(total));budget.set("bend_length_mm",V::makeNum(K_PI*c.radius));budget.set("required_mm",V::makeNum(required));budget.set("required_scope",V::makeStr(source=="none"?"current pose":"travel ends"));
    budget.set("margin_mm",V::makeNum(c.margin));budget.set("slack_mm",V::makeNum(total-required));budget.set("max_slack_mm",V::makeNum(c.maxSlack));out.set("length_budget",budget);
    if(total-required>c.maxSlack+1e-9)warnings.arr.push_back(V::makeStr("slack exceeds max_slack: the loop overhangs the stroke"));
    if(total-required<c.margin-1e-9)warnings.arr.push_back(V::makeStr("slack below the margin"));
    out.set("warnings",warnings);
    out.set("kinematic_model",V::makeStr("rigid links, pins on the path (fixed straight, semicircle R, moving straight) exactly one pitch apart; fixed-run links static, moving-run links rigid with the carriage, loop at half carriage speed on average"));
    out.set("sag_checked",V::makeBool(false));out.set("dynamics_checked",V::makeBool(false));out.set("cable_fill_checked",V::makeBool(false));return out;
}
}

bool applyDragChainCommand(Document& d,const V& command,std::string& error){
    const auto* op=command.find("op");if(!op||op->t!=V::Str)return false;const auto& name=op->str;
    if(name!="drag_chain.set"&&name!="drag_chain.remove")return false;
    try{if(name=="drag_chain.set")setChain(d,command);else removeChain(d,command);error.clear();}catch(const std::exception& e){error=e.what();}
    return true;
}
bool callDragChainTool(AgentSession& session,const std::string& tool,const V& args,V& out){
    if(tool!="drag_chain_info")return false;
    keys(args,{"name","q","poses","expected_revision"},"drag_chain_info");
    if(const auto* expected=args.find("expected_revision"))need(expected->t==V::Num&&expected->num==(double)session.revision,"REVISION_CONFLICT","the current revision is in this result; re-read drag_chain_info and retry");
    const auto* nameArg=args.find("name");const auto* qArg=args.find("q");const auto* posesArg=args.find("poses");
    if(posesArg)need(posesArg->t==V::Bool,"INVALID","poses must be boolean");
    Document& d=session.document;out=V::makeObj();out.set("revision",V::makeNum((double)session.revision));
    if(!nameArg){need(!qArg&&!posesArg,"INVALID","q and poses require name");V rows=V::makeArr();
        if(d.dragChains.t==V::Arr){std::vector<AssemblyPlacement> placements;std::string error;AssemblyDiagnostics diagnostics;const bool solved=resolveAssembly(d,placements,error,&diagnostics);const auto world=worldOf(placements);
            for(const auto& row:d.dragChains.arr){V item=V::makeObj();item.set("name",*row.find("name"));item.set("link_count",V::makeNum((double)row.find("links")->arr.size()));
                try{need(solved,"INVALID","assembly solve failed: "+error);const auto c=compile(d,row,d.buildEnv(),nodeMap(d));const auto layout=layoutAt(c,world);double q=0;if(staticQ(diagnostics,c.joint,q))item.set("q_mm",V::makeNum(q));
                    item.set("pitch_mm",V::makeNum(c.pitch));item.set("bend_radius_mm",V::makeNum(c.radius));item.set("fixed_run_mm",V::makeNum(layout.fixedRun));item.set("moving_run_mm",V::makeNum(layout.movingRun));item.set("valid",V::makeBool(true));}
                catch(const std::exception& e){item.set("valid",V::makeBool(false));item.set("error",V::makeStr(e.what()));}rows.arr.push_back(item);}}
        out.set("drag_chains",rows);out.set("count",V::makeNum((double)rows.arr.size()));out.set("document_unchanged",V::makeBool(true));return true;}
    need(nameArg->t==V::Str,"INVALID","name must be text");const auto* row=findDragChain(d,nameArg->str);need(row,"NOT_FOUND",nameArg->str);
    const auto info=chainInfo(d,*row,qArg,posesArg&&posesArg->b);for(const auto& item:info.obj)out.set(item.first,item.second);out.set("document_unchanged",V::makeBool(true));return true;
}
void appendDragChainTools(V& tools){
    V tool=V::makeObj(),schema,annotations=V::makeObj();
    arkjson::parse(R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"q":{"type":"number","description":"Hypothetical driving-joint coordinate in mm, solved through the assembly.drive preview; read-only."},"poses":{"type":"boolean","description":"With name: list every link with world_column_major and pin points instead of a summary."},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})",schema);
    tool.set("name",V::makeStr("drag_chain_info"));tool.set("description",V::makeStr("Read-only drag chain (cable carrier) inspection. No arguments lists chains with link count, pitch, bend radius, driving joint coordinate q and run lengths. name returns the link section, anchor and moving end, fixed/moving run lengths, loop tangent and apex, link segment counts, pin-spacing checks, the length budget (chain, bend, required, margin, slack), travel limits with the runs at both ends and warnings; poses:true adds every link pose. q evaluates at a hypothetical joint coordinate without changing the document. Author with doc_apply drag_chain.set/drag_chain.remove (agent_onboard assembly)."));
    annotations.set("readOnlyHint",V::makeBool(true));tool.set("inputSchema",schema);tool.set("annotations",annotations);tools.arr.push_back(tool);
    // Advertise the authoring operations on the doc_apply command schema (doc_preview copies it).
    for(auto& entry:tools.arr)if(entry.find("name")&&entry.find("name")->asStr()=="doc_apply")for(auto& field:entry.obj)if(field.first=="inputSchema")for(auto& p:field.second.obj)if(p.first=="properties")for(auto& c:p.second.obj)if(c.first=="commands")for(auto& i:c.second.obj)if(i.first=="items")for(auto& ip:i.second.obj)if(ip.first=="properties")for(auto& op:ip.second.obj)if(op.first=="op")for(auto& e:op.second.obj)if(e.first=="enum"&&e.second.t==V::Arr){e.second.arr.push_back(V::makeStr("drag_chain.set"));e.second.arr.push_back(V::makeStr("drag_chain.remove"));}
}
void appendDragChainOnboarding(const std::string& topic,V& out){
    if(topic!="assembly"&&topic!="motion")return;
    for(auto& field:out.obj)if(field.first=="tools"&&field.second.t==V::Arr)field.second.arr.push_back(V::makeStr("drag_chain_info"));
    V guide=V::makeObj(),rules=V::makeArr(),example;
    for(const char* line:{
        "A cable carrier (drag chain, energy chain) that follows a carriage is native: doc_apply drag_chain.set{name,link:{pitch,outer_width,outer_height,inner_width?,inner_height?},bend_radius,anchor:{occurrence?,origin:[x,y,z],direction:+X..-Z|[x,y,z],bend:+X..-Z|[x,y,z]},moving:{occurrence,attach?:[x,y,z],joint:prismatic mate id},link_count?,margin?,travel?:{min,max},max_slack?} with expected_revision; drag_chain.remove{name}. Values accept parameter expressions. Never model a drag chain as rigid telescoping segments, min()/max() couplings or one motion channel per link.",
        "anchor.origin is the fixed-end pin (assembly space, or the frame of anchor.occurrence); direction runs from the fixed bracket towards the bend; bend points from the fixed run to the moving run. moving.attach is the moving-end pin in the carriage frame: it must lie 2*bend_radius along bend with no lateral offset, and moving.joint must translate it along direction (parallel runs).",
        "The chain owns one native B-rep link definition (native_solid op drag_chain_link: side plates and crossbars around the inner cable space) and link_count ordinary occurrences under one group: BOM quantity, material, visibility, picking and clearance use them. Without link_count the count is derived from the travel plus margin (default 2 pitches). The travel is travel{min,max}, else the joint limits.",
        "Kinematics: pins lie on the path (fixed straight, semicircle R, moving straight) exactly one pitch apart. Fixed-run links never move, moving-run links move rigidly with the carriage and the loop moves at half the carriage speed: L_fixed=(N*pitch-pi*R+c)/2 on average, c the moving-end pin coordinate along direction. Links are driven followers in every pose, assembly_info, assembly.drive preview and motion.bake frame: bake only the carriage joint.",
        "Rejections are typed and atomic: DRAG_CHAIN_NOT_PARALLEL, DRAG_CHAIN_CARRIAGE_NOT_DRIVEN, DRAG_CHAIN_TRAVEL_RUN_NEGATIVE, DRAG_CHAIN_TRAVEL_EXCLUDES_POSE, DRAG_CHAIN_TOO_SHORT, DRAG_CHAIN_TOO_LONG (slack above max_slack, default max(4 pitches, pi*R)), DRAG_CHAIN_UNKNOWN_OCCURRENCE, DRAG_CHAIN_UNKNOWN_MATE, DRAG_CHAIN_JOINT_NOT_PRISMATIC, DRAG_CHAIN_JOINT_NOT_DRIVABLE, DRAG_CHAIN_BEND_TOO_TIGHT, DRAG_CHAIN_GEOMETRY, DRAG_CHAIN_NONFINITE, DRAG_CHAIN_CAPACITY (1..400 links, 16 chains), DRAG_CHAIN_LINK_DRIVEN, DRAG_CHAIN_NOT_FOUND. A drive or bake that would make a run negative rejects with DRAG_CHAIN_RUN_NEGATIVE.",
        "drag_chain_info{} lists chains; {name} returns runs, loop, segment counts, pin checks, length budget, travel ends and warnings; {name,q} evaluates a hypothetical joint coordinate; poses:true lists every link. Sag, dynamics and cable fill are not checked."})rules.arr.push_back(V::makeStr(line));
    arkjson::parse(R"JSON({"commands":[{"op":"drag_chain.set","name":"Camera cable chain","link":{"pitch":"chain_pitch","outer_width":31.6,"outer_height":21.6,"inner_width":25,"inner_height":17},"bend_radius":38,"anchor":{"occurrence":"Duct","origin":[60,-20,405],"direction":"-Z","bend":"+X"},"moving":{"occurrence":"Carriage","attach":[-24,20,-8],"joint":1}}]})JSON",example);
    guide.set("rules",rules);guide.set("example",example);guide.set("workflow",V::makeStr("assembly_info for the carriage occurrence, its prismatic mate id and limits -> place anchor and attach so the runs are parallel at 2R -> doc_preview then doc_apply drag_chain.set -> drag_chain_info{name} and {name,q} at the travel ends -> bake the carriage joint with motion.bake."));
    out.set("drag_chain",guide);
}
}
