#include "ark/model.hpp"
#include "ark/references.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ark {
bool isMechanicalMate(const std::string& type){return type=="chain"||type=="gear"||type=="shaft"||type=="rack_pinion"||type=="cam";}
namespace {
constexpr double kAngularWeight=10.0;
constexpr double kPositionTolerance=1e-5;
constexpr double kAngleToleranceDeg=1e-4;

Vec3 origin(const Mat4& m){return {m.m[12],m.m[13],m.m[14]};}
Vec3 axis(const Mat4& m){return m.transformDir({0,0,1});}
bool finiteFrame(const Mat4& m){
    for(double v:m.m)if(!std::isfinite(v))return false;
    const Vec3 x=m.transformDir({1,0,0}),y=m.transformDir({0,1,0}),z=axis(m);
    return std::fabs(x.len()-1)<1e-6&&std::fabs(y.len()-1)<1e-6&&
           std::fabs(z.len()-1)<1e-6&&std::fabs(x.dot(y))<1e-6&&
           std::fabs(x.dot(z))<1e-6&&std::fabs(y.dot(z))<1e-6&&
           x.cross(y).dot(z)>1-1e-6;
}
Mat4 localReference(const Document& doc,const AssemblyNode& node,
                    const std::string& name,double fraction){
    if(name=="origin"||name=="Z"||name=="XY")return Mat4::identity();
    if(name=="X")return Mat4::rotY(K_HALF_PI);
    if(name=="Y")return Mat4::rotX(-K_HALF_PI);
    if(!node.feature)throw std::runtime_error("Semantic reference requires a part instance");
    return resolveGeometryReference(doc,node.feature,name,fraction).frame;
}
AssemblyMotionRelation prepareMotionRelation(const Document& doc,const AssemblyMate& mate,
        const AssemblyNode& a,const AssemblyNode& b,const Mat4& worldA,const Mat4& worldB){
    if(a.fixed||b.fixed)throw std::runtime_error("MECHANICAL_FIXED_ENDPOINT: moving endpoints cannot be fixed");
    if(a.pattern.active||b.pattern.active)throw std::runtime_error("MECHANICAL_PATTERN_UNSUPPORTED: coupling requires individual instances");
    AssemblyMotionRelation relation;relation.mate=mate;
    relation.localA=localReference(doc,a,mate.reference_a,mate.fraction_a);relation.localB=localReference(doc,b,mate.reference_b,mate.fraction_b);
    if(!finiteFrame(relation.localA)||!finiteFrame(relation.localB))throw std::runtime_error("MECHANICAL_INVALID_AXIS: reference frames must be rigid");
    const Mat4 fa=worldA*relation.localA,fb=worldB*relation.localB;const double dot=axis(fa).dot(axis(fb));
    const ExprEnv env=doc.buildEnv();
    auto number=[&](const char* key,double fallback,bool required=false){const auto found=mate.motion.find(key);if(found==mate.motion.end()){if(required)throw std::runtime_error(std::string("MECHANICAL_FIELD_REQUIRED: ")+key);return fallback;}bool ok=false;const double value=evalExpr(found->second,env,&ok);if(!ok||!std::isfinite(value)||std::fabs(value)>1e6)throw std::runtime_error(std::string("MECHANICAL_INVALID_EXPRESSION: ")+key);return value;};
    for(const auto& field:mate.motion){const auto& key=field.first;const bool allowed=(mate.type=="chain"||mate.type=="gear")?(key=="teeth_a"||key=="teeth_b"||key=="phase_deg"):mate.type=="rack_pinion"?(key=="pitch_radius"||key=="direction"):mate.type=="cam"?(key=="lift"||key=="phase_deg"):false;if(!allowed)throw std::runtime_error("MECHANICAL_UNSUPPORTED_FIELD: "+key);}
    bool valueOk=false;const double legacy=evalExpr(mate.value,env,&valueOk);if(!valueOk||std::fabs(legacy)>1e-12)throw std::runtime_error("Mechanical mates use motion fields; value must be zero");
    if(mate.type=="chain"||mate.type=="gear"||mate.type=="shaft"){
        if(std::fabs(std::fabs(dot)-1)>1e-6)throw std::runtime_error("MECHANICAL_AXES_NOT_PARALLEL: chain/gear/shaft require parallel semantic axes");
        if(mate.type=="shaft"){const auto gap=origin(fb)-origin(fa);if((gap-axis(fa)*gap.dot(axis(fa))).len()>kPositionTolerance)throw std::runtime_error("MECHANICAL_SHAFT_NOT_COAXIAL: shaft references must share an axis");}
        else{const double teethA=number("teeth_a",0,true),teethB=number("teeth_b",0,true);if(teethA<1||teethB<1||teethA>10000||teethB>10000||std::fabs(teethA-std::round(teethA))>1e-9||std::fabs(teethB-std::round(teethB))>1e-9)throw std::runtime_error("MECHANICAL_INVALID_TEETH: counts must be integers 1..10000");relation.teethA=teethA;relation.teethB=teethB;relation.ratio=teethA/teethB;relation.phaseRad=deg2rad(number("phase_deg",0));}
        relation.ratio*=dot<0?-1:1;if(mate.type=="gear")relation.ratio*=-1;
    }else{
        relation.translational=true;if(std::fabs(dot)>1e-6)throw std::runtime_error("MECHANICAL_AXES_NOT_ORTHOGONAL: radial cam/rack travel must be perpendicular to rotation axis");
        if(mate.type=="cam"){relation.lift=number("lift",0,true);if(relation.lift<0)throw std::runtime_error("MECHANICAL_INVALID_LIFT: lift must be nonnegative");relation.phaseRad=deg2rad(number("phase_deg",0));}
        else{relation.ratio=number("pitch_radius",0,true);const double direction=number("direction",1);if(relation.ratio<=0||(direction!=1&&direction!=-1))throw std::runtime_error("MECHANICAL_INVALID_RACK: positive pitch_radius and direction ±1 required");relation.ratio*=direction;}
    }
    return relation;
}
void validateMechanicalGraph(const Document& doc){
    std::unordered_map<u64,u64> incoming;std::unordered_map<u64,std::vector<u64>> children;std::unordered_set<u64> followers;
    for(const auto& mate:doc.mates)if(isMechanicalMate(mate.type)){if(!incoming.emplace(mate.b,mate.a).second)throw std::runtime_error("MECHANICAL_MULTIPLE_DRIVERS: a driven instance can have only one incoming coupling");children[mate.a].push_back(mate.b);if(mate.type=="cam"||mate.type=="rack_pinion")followers.insert(mate.b);}
    for(const auto& mate:doc.mates)if(isMechanicalMate(mate.type)&&followers.count(mate.a))throw std::runtime_error("MECHANICAL_FOLLOWER_AS_DRIVER: a translating follower cannot drive a rotational coupling");
    std::unordered_map<u64,int> colour;std::function<void(u64)> visit=[&](u64 node){if(colour[node]==1)throw std::runtime_error("MECHANICAL_CYCLE_UNSUPPORTED: cyclic coupling graphs are outside this motion domain");if(colour[node]==2)return;colour[node]=1;const auto found=children.find(node);if(found!=children.end())for(u64 child:found->second)visit(child);colour[node]=2;};for(const auto& item:children)visit(item.first);
}
struct PreparedMate {
    const AssemblyMate* mate=nullptr;
    Mat4 a,b;
    double target=0;
    bool limited=false;
    double minimum=0,maximum=0;
};
void evaluateLimits(const AssemblyMate& mate,const ExprEnv& env,bool& limited,double& minimum,double& maximum){
    limited=mate.limits.enabled;if(!limited)return;
    if(mate.type!="revolute"&&mate.type!="prismatic")throw std::runtime_error("ASSEMBLY_INVALID_JOINT_LIMITS: limits require revolute or prismatic");
    auto number=[&](const std::string& expression){bool ok=false;const double value=evalExpr(expression,env,&ok);
        if(!ok||!std::isfinite(value)||std::fabs(value)>1e6)throw std::runtime_error("ASSEMBLY_INVALID_JOINT_LIMITS: bounds must evaluate within +/-1e6");return value;};
    minimum=number(mate.limits.min);maximum=number(mate.limits.max);
    if(minimum>maximum)throw std::runtime_error("ASSEMBLY_INVALID_JOINT_LIMITS: min must not exceed max");
    if(mate.type=="revolute"&&maximum-minimum>=360)throw std::runtime_error("ASSEMBLY_INVALID_JOINT_LIMITS: revolute interval must span less than 360 degrees; multi-turn limits are unsupported");
}
PreparedMate preparedConstraint(const AssemblyMotionRelation& constraint){
    return {&constraint.mate,constraint.localA,constraint.localB,constraint.target,constraint.limited,constraint.minimum,constraint.maximum};
}
double jointCoordinate(const PreparedMate& mate,const std::unordered_map<u64,Mat4>& world){
    const Mat4 a=world.at(mate.mate->a)*mate.a,b=world.at(mate.mate->b)*mate.b;
    if(mate.mate->type=="prismatic")return (origin(b)-origin(a)).dot(axis(a));
    const Vec3 xa=a.transformDir({1,0,0}),xb=b.transformDir({1,0,0});
    double angle=rad2deg(std::atan2(axis(a).dot(xa.cross(xb)),xa.dot(xb)));
    // A sub-turn interval identifies one physical angular branch, including
    // intervals which cross the atan2 +/-180-degree cut. No turn count is inferred.
    if(mate.limited)angle+=360*std::round(((mate.minimum+mate.maximum)*.5-angle)/360);
    return angle;
}
double limitError(double position,double minimum,double maximum){return std::max({0.,minimum-position,position-maximum});}
double limitRoundoff(double position,double minimum,double maximum){return 64*std::numeric_limits<double>::epsilon()*
    std::max({1.,std::fabs(position),std::fabs(minimum),std::fabs(maximum)});}
bool satisfiedLimit(const AssemblyMateResidual& r){return std::isfinite(r.limitError)&&
    (!r.limited||(std::isfinite(r.coordinate)&&r.limitError<=limitRoundoff(r.coordinate,r.min,r.max)));}
struct MateState {Vec3 pa,pb,za,zb;double dot=0;};
MateState state(const PreparedMate& m,const std::unordered_map<u64,Mat4>& world){
    const Mat4 a=world.at(m.mate->a)*m.a,b=world.at(m.mate->b)*m.b;
    MateState s{origin(a),origin(b),axis(a),axis(b)};
    s.dot=clamp(s.za.dot(s.zb),-1.0,1.0);
    return s;
}
void append(Vec3 v,std::vector<double>& out){out.push_back(v.x);out.push_back(v.y);out.push_back(v.z);}
void residual(const PreparedMate& m,const std::unordered_map<u64,Mat4>& world,
              std::vector<double>& out){
    const auto s=state(m,world);
    const Vec3 d=s.pb-s.pa,c=s.za.cross(s.zb);
    const auto& type=m.mate->type;
    if(type=="concentric"||type=="prismatic"){
        append(d-s.za*d.dot(s.za),out);
        if(type=="prismatic"){
            const Mat4 a=world.at(m.mate->a)*m.a,b=world.at(m.mate->b)*m.b;
            for(const Vec3 direction:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})
                append((b.transformDir(direction)-a.transformDir(direction))*kAngularWeight,out);
        }else append(c*kAngularWeight,out);
    }else if(type=="coincident"||type=="revolute"||type=="rigid"){
        append(d,out);
        if(type=="rigid"){
            const Mat4 a=world.at(m.mate->a)*m.a,b=world.at(m.mate->b)*m.b;
            for(const Vec3 direction:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})
                append((b.transformDir(direction)-a.transformDir(direction))*kAngularWeight,out);
        }else{append(c*kAngularWeight,out);out.push_back(kAngularWeight*(1-s.dot));}
    }else if(type=="contact"){
        out.push_back(d.dot(s.za));
        append(c*kAngularWeight,out);
        out.push_back(kAngularWeight*(1+s.dot));
    }else if(type=="distance"){
        out.push_back(d.dot(s.za)-m.target);
    }else if(type=="angle"){
        if(m.target<1e-10||K_PI-m.target<1e-10){
            append(c*kAngularWeight,out);
            out.push_back(kAngularWeight*(1-(m.target<1e-10?1:-1)*s.dot));
        }else out.push_back(kAngularWeight*(std::atan2(c.len(),s.dot)-m.target));
    }
}
bool solveLinear(std::vector<double> a,std::vector<double> b,std::vector<double>& x,int n){
    x.assign(n,0);
    for(int k=0;k<n;++k){
        int pivot=k;for(int i=k+1;i<n;++i)if(std::fabs(a[i*n+k])>std::fabs(a[pivot*n+k]))pivot=i;
        if(std::fabs(a[pivot*n+k])<1e-20)return false;
        if(pivot!=k){for(int j=k;j<n;++j)std::swap(a[k*n+j],a[pivot*n+j]);std::swap(b[k],b[pivot]);}
        const double inv=1/a[k*n+k];
        for(int i=k+1;i<n;++i){const double f=a[i*n+k]*inv;
            for(int j=k+1;j<n;++j)a[i*n+j]-=f*a[k*n+j];b[i]-=f*b[k];}
    }
    for(int i=n-1;i>=0;--i){double v=b[i];for(int j=i+1;j<n;++j)v-=a[i*n+j]*x[j];x[i]=v/a[i*n+i];}
    return true;
}
int matrixRank(const std::vector<double>& j,int rows,int cols){
    if(!rows||!cols)return 0;
    std::vector<double> q=j;double largest=0;
    for(int c=0;c<cols;++c){double norm=0;for(int r=0;r<rows;++r)norm+=q[r*cols+c]*q[r*cols+c];largest=std::max(largest,std::sqrt(norm));}
    const double threshold=std::max(1e-9,largest*1e-6);int rank=0;
    for(int step=0;step<std::min(rows,cols);++step){
        int pivot=-1;double best=0;
        for(int c=step;c<cols;++c){double v=0;for(int r=0;r<rows;++r)v+=q[r*cols+c]*q[r*cols+c];
            if(v>best){best=v;pivot=c;}}
        if(pivot<0||std::sqrt(best)<threshold)break;
        for(int r=0;r<rows;++r)std::swap(q[r*cols+step],q[r*cols+pivot]);
        const double scale=1/std::sqrt(best);
        for(int r=0;r<rows;++r)q[r*cols+step]*=scale;
        for(int c=step+1;c<cols;++c){double dot=0;for(int r=0;r<rows;++r)dot+=q[r*cols+step]*q[r*cols+c];
            for(int r=0;r<rows;++r)q[r*cols+c]-=dot*q[r*cols+step];}
        ++rank;
    }
    return rank;
}
double squared(const std::vector<double>& v){double n=0;for(double x:v)n+=x*x;return n;}
AssemblyMateResidual measuredResidual(const PreparedMate& mate,const std::unordered_map<u64,Mat4>& world){
    const auto s=state(mate,world);const Vec3 gap=s.pb-s.pa;
    AssemblyMateResidual r;r.id=mate.mate->id;r.type=mate.mate->type;
    const double theta=rad2deg(std::atan2(s.za.cross(s.zb).len(),s.dot));
    if(r.type=="concentric"||r.type=="prismatic"){
        r.positionMm=(gap-s.za*gap.dot(s.za)).len();r.angleDeg=std::min(theta,180-theta);
        if(r.type=="prismatic"){
            const Mat4 a=world.at(mate.mate->a)*mate.a,b=world.at(mate.mate->b)*mate.b;
            double trace=0;for(const Vec3 direction:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})trace+=a.transformDir(direction).dot(b.transformDir(direction));
            r.angleDeg=rad2deg(std::acos(clamp((trace-1)*.5,-1.,1.)));
        }
    }
    else if(r.type=="coincident"||r.type=="revolute"||r.type=="rigid"){
        r.positionMm=gap.len();r.angleDeg=theta;
        if(r.type=="rigid"){
            const Mat4 a=world.at(mate.mate->a)*mate.a,b=world.at(mate.mate->b)*mate.b;
            double trace=0;for(const Vec3 direction:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})
                trace+=a.transformDir(direction).dot(b.transformDir(direction));
            r.angleDeg=rad2deg(std::acos(clamp((trace-1)*.5,-1.,1.)));
        }
    }else if(r.type=="contact"){r.positionMm=std::fabs(gap.dot(s.za));r.angleDeg=180-theta;}
    else if(r.type=="distance"){r.valueError=gap.dot(s.za)-mate.target;r.positionMm=std::fabs(r.valueError);}
    else{r.valueError=theta-rad2deg(mate.target);r.angleDeg=std::fabs(r.valueError);}
    if(r.type=="revolute"||r.type=="prismatic"){
        r.hasCoordinate=true;r.coordinate=jointCoordinate(mate,world);r.limited=mate.limited;r.min=mate.minimum;r.max=mate.maximum;
        if(r.limited)r.limitError=limitError(r.coordinate,r.min,r.max);
    }
    return r;
}
bool satisfiedResidual(const AssemblyMateResidual& r){return std::isfinite(r.positionMm)&&std::isfinite(r.angleDeg)&&
    r.positionMm<=kPositionTolerance&&r.angleDeg<=kAngleToleranceDeg&&satisfiedLimit(r);}
Vec3 rotationVector(const Mat4& r){
    const double trace=r.m[0]+r.m[5]+r.m[10];double w,x,y,z;
    if(trace>0){const double s=2*std::sqrt(trace+1);w=s/4;x=(r.m[6]-r.m[9])/s;y=(r.m[8]-r.m[2])/s;z=(r.m[1]-r.m[4])/s;}
    else if(r.m[0]>r.m[5]&&r.m[0]>r.m[10]){const double s=2*std::sqrt(1+r.m[0]-r.m[5]-r.m[10]);w=(r.m[6]-r.m[9])/s;x=s/4;y=(r.m[4]+r.m[1])/s;z=(r.m[8]+r.m[2])/s;}
    else if(r.m[5]>r.m[10]){const double s=2*std::sqrt(1+r.m[5]-r.m[0]-r.m[10]);w=(r.m[8]-r.m[2])/s;x=(r.m[4]+r.m[1])/s;y=s/4;z=(r.m[9]+r.m[6])/s;}
    else{const double s=2*std::sqrt(1+r.m[10]-r.m[0]-r.m[5]);w=(r.m[1]-r.m[4])/s;x=(r.m[8]+r.m[2])/s;y=(r.m[9]+r.m[6])/s;z=s/4;}
    if(w<0){w=-w;x=-x;y=-y;z=-z;}const Vec3 v{x,y,z};const double n=v.len();return n<1e-14?Vec3{}:v*(2*std::atan2(n,w)/n);
}
}

bool resolveAssembly(const Document& d,std::vector<AssemblyPlacement>& placements,
                     std::string& error,AssemblyDiagnostics* diagnostics,bool reportUnsatisfied){
    placements.clear();error.clear();if(diagnostics)*diagnostics={};
    auto fail=[&](const std::string& e){error=e;placements.clear();if(diagnostics)*diagnostics={};return false;};
    if(d.assembly.size()>4096)return fail("Assembly capacity exceeds 4096 nodes");
    std::unordered_map<u64,const AssemblyNode*> byId;
    std::unordered_map<u64,Mat4> world,rigid,parentFrame;
    std::unordered_map<u64,bool> visibility;
    std::set<u64> features;for(auto& f:d.features)features.insert(f.id);
    for(auto& n:d.assembly){
        if(!n.id||!byId.emplace(n.id,&n).second)return fail("Duplicate/zero assembly ID");
        if(n.feature&&!features.count(n.feature))return fail("Assembly references missing feature #"+std::to_string(n.feature));
    }
    ExprEnv env=d.buildEnv();std::set<u64> active;
    std::function<bool(u64,int)> resolve=[&](u64 id,int depth){
        if(world.count(id))return true;
        if(depth>64||active.count(id)){error="Assembly cycle or depth exceeds 64";return false;}
        auto found=byId.find(id);if(found==byId.end()){error="Assembly parent not found";return false;}
        auto& n=*found->second;active.insert(id);Mat4 parent;
        if(n.parent){auto p=byId.find(n.parent);if(p==byId.end()||p->second->feature){error="Parent must be an assembly group";return false;}
            if(!resolve(n.parent,depth+1))return false;
            parent=world[n.parent];}
        for(auto& kv:n.p)if(kv.first!="tx"&&kv.first!="ty"&&kv.first!="tz"&&kv.first!="rx"&&kv.first!="ry"&&kv.first!="rz"){error="Unsupported assembly transform: "+kv.first;return false;}
        auto number=[&](const char* key){auto p=n.p.find(key);if(p==n.p.end())return 0.;bool ok=false;f64 value=evalExpr(p->second,env,&ok);if(!ok||!std::isfinite(value)||std::fabs(value)>1e6)error=std::string("Invalid assembly expression: ")+key;return value;};
        Vec3 t{number("tx"),number("ty"),number("tz")};f64 rx=number("rx"),ry=number("ry"),rz=number("rz");
        if(!error.empty())return false;
        rigid[id]=Mat4::translation(t)*Mat4::rotZ(deg2rad(rz))*Mat4::rotY(deg2rad(ry))*Mat4::rotX(deg2rad(rx));
        parentFrame[id]=parent;world[id]=parent*rigid[id];
        visibility[id]=n.visible&&(!n.parent||visibility[n.parent]);active.erase(id);return true;
    };
    for(auto& n:d.assembly)if(!resolve(n.id,0)){placements.clear();return false;}
    if(d.mates.size()>256)return fail("Assembly mate capacity exceeds 256");
    std::unordered_set<u64> mated;
    std::unordered_set<u64> mateIds;
    std::vector<PreparedMate> mates;mates.reserve(d.mates.size());
    for(const auto& mate:d.mates){
        if(!mate.id||!mateIds.insert(mate.id).second)return fail("Duplicate/zero mate ID");
        auto a=byId.find(mate.a),b=byId.find(mate.b);
        if(a==byId.end()||b==byId.end()||mate.a==mate.b)return fail("Mate references invalid assembly instance IDs");
        if(!a->second->feature||!b->second->feature)return fail(isMechanicalMate(mate.type)?"MECHANICAL_GROUP_ENDPOINT_UNSUPPORTED: use individual part instances and shaft coupling for attached rotating parts":"Mates require part instances, not groups");
        if(mate.type!="concentric"&&mate.type!="coincident"&&mate.type!="revolute"&&mate.type!="prismatic"&&mate.type!="rigid"&&mate.type!="contact"&&mate.type!="distance"&&mate.type!="angle"&&!isMechanicalMate(mate.type))return fail("Unsupported assembly mate type: "+mate.type);
        if(!std::isfinite(mate.fraction_a)||!std::isfinite(mate.fraction_b)||
           mate.fraction_a<0||mate.fraction_a>1||mate.fraction_b<0||mate.fraction_b>1)return fail("Invalid mate reference fraction");
        if(isMechanicalMate(mate.type)){if(mate.limits.enabled)return fail("ASSEMBLY_INVALID_JOINT_LIMITS: limits require revolute or prismatic");continue;}
        if(!mate.motion.empty())return fail("Motion fields require a mechanical mate");
        bool ok=false;double target=evalExpr(mate.value,env,&ok);
        if(!ok||!std::isfinite(target)||std::fabs(target)>1e6)return fail("Invalid assembly mate value #"+std::to_string(mate.id));
        if(mate.type=="angle"&&(target<0||target>180))return fail("Mate angle must be between 0 and 180 degrees");
        if(mate.type!="angle"&&mate.type!="distance"&&std::fabs(target)>1e-12)return fail("Only distance and angle mates accept nonzero values");
        PreparedMate prepared;prepared.mate=&mate;prepared.target=mate.type=="angle"?deg2rad(target):target;
        try{evaluateLimits(mate,env,prepared.limited,prepared.minimum,prepared.maximum);
            prepared.a=localReference(d,*a->second,mate.reference_a,mate.fraction_a);
            prepared.b=localReference(d,*b->second,mate.reference_b,mate.fraction_b);}
        catch(const std::exception& e){return fail("Mate #"+std::to_string(mate.id)+" reference: "+e.what());}
        if(!finiteFrame(prepared.a)||!finiteFrame(prepared.b))return fail("Mate reference is not a finite rigid frame");
        mates.push_back(prepared);mated.insert(mate.a);mated.insert(mate.b);
    }
    std::unordered_map<u64,std::vector<u64>> children;
    for(const auto& n:d.assembly)if(n.parent)children[n.parent].push_back(n.id);
    std::function<bool(u64)> groupAnchored=[&](u64 id)->bool{
        if(byId.at(id)->fixed)return true;
        auto found=children.find(id);if(found==children.end())return false;
        for(u64 child:found->second)if(groupAnchored(child))return true;
        return false;
    };
    std::unordered_map<u64,bool> crossOnly;
    for(u64 id:mated)crossOnly[id]=true;
    std::unordered_set<u64> crossGroups;
    for(const auto& prepared:mates){
        const auto* left=byId.at(prepared.mate->a);const auto* right=byId.at(prepared.mate->b);
        if(left->parent==right->parent){crossOnly[prepared.mate->a]=false;crossOnly[prepared.mate->b]=false;}
        else{if(left->parent)crossGroups.insert(left->parent);if(right->parent)crossGroups.insert(right->parent);}
    }
    struct Mover{u64 id;bool group;int depth;};
    std::vector<Mover> moving;
    for(const auto& n:d.assembly){
        if(!n.feature||!mated.count(n.id)||n.fixed)continue;
        if(crossOnly[n.id]&&n.parent)continue;
        moving.push_back({n.id,false,0});
    }
    std::function<int(u64)> depthOf=[&](u64 id)->int{u64 parent=byId.at(id)->parent;return parent?depthOf(parent)+1:0;};
    for(u64 group:crossGroups)if(!groupAnchored(group))moving.push_back({group,true,depthOf(group)});
    std::stable_sort(moving.begin(),moving.end(),[](const Mover& a,const Mover& b){
        if(a.group!=b.group)return !a.group;
        return a.depth>b.depth;
    });
    if(moving.size()>64)return fail("Assembly mate solve exceeds 64 moving instances");
    // Every connected mate component must have an explicit fixed instance.
    // A cross-parent component is also anchored when a parent group contains a fixed descendant.
    std::unordered_set<u64> seen;
    for(u64 id:mated){
        if(seen.count(id))continue;std::vector<u64> queue{id};seen.insert(id);bool anchored=false,cross=false;
        for(size_t k=0;k<queue.size();++k){u64 at=queue[k];anchored|=byId.at(at)->fixed;
            // Cross-parent mates use the immediate parent as a rigid carrier.
            // Its distinct pin instances are therefore physical graph peers;
            // otherwise every intermediate pin pair in a real linkage would
            // incorrectly appear to be a separate ungrounded component.
            const u64 carrier=byId.at(at)->parent;
            if(carrier&&crossGroups.count(carrier)){
                std::vector<u64> stack=children[carrier];
                for(size_t i=0;i<stack.size();++i){const u64 member=stack[i];
                    if(byId.at(member)->feature&&seen.insert(member).second)queue.push_back(member);
                    const auto nested=children.find(member);if(nested!=children.end())stack.insert(stack.end(),nested->second.begin(),nested->second.end());}
            }
            for(const auto& mate:d.mates){if(isMechanicalMate(mate.type))continue;u64 next=mate.a==at?mate.b:(mate.b==at?mate.a:0);
                if(!next)continue;
                if(byId.at(mate.a)->parent!=byId.at(mate.b)->parent)cross=true;
                if(seen.insert(next).second)queue.push_back(next);}}
        if(cross)for(u64 at:queue)if(u64 parent=byId.at(at)->parent)anchored|=groupAnchored(parent);
        if(!anchored)return fail("Mate component has no fixed anchor (node #"+std::to_string(id)+")");
    }
    const auto authored=world;
    const int columns=(int)moving.size()*6;
    auto rigidDelta=[&](const Mat4& base,const std::vector<double>& x,size_t k){
        Vec3 dt{x[6*k],x[6*k+1],x[6*k+2]};Vec3 rv{x[6*k+3],x[6*k+4],x[6*k+5]};
        const Vec3 o=origin(base);const double angle=rv.len();
        Mat4 rotation=angle>1e-14?Mat4::axisAngle(rv/angle,angle):Mat4::identity();
        return Mat4::translation(o+dt)*rotation*Mat4::translation(-o);
    };
    auto partDescendants=[&](u64 group){std::vector<u64> parts,stack;
        auto found=children.find(group);if(found!=children.end())stack=found->second;
        for(size_t i=0;i<stack.size();++i){u64 id=stack[i];if(byId.at(id)->feature)parts.push_back(id);
            auto next=children.find(id);if(next!=children.end())stack.insert(stack.end(),next->second.begin(),next->second.end());}
        return parts;};
    auto posed=[&](const std::vector<double>& x){auto placed=authored;
        for(size_t k=0;k<moving.size();++k)if(!moving[k].group)placed[moving[k].id]=rigidDelta(authored.at(moving[k].id),x,k)*authored.at(moving[k].id);
        for(size_t k=0;k<moving.size();++k)if(moving[k].group){Mat4 delta=rigidDelta(authored.at(moving[k].id),x,k);
            for(u64 part:partDescendants(moving[k].id))placed[part]=delta*placed[part];}
        return placed;};
    auto evaluate=[&](const std::vector<double>& x){auto posedWorld=posed(x);std::vector<double> r;
        for(const auto& mate:mates)residual(mate,posedWorld,r);return r;};
    auto jacobian=[&](const std::vector<double>& x,int rows){std::vector<double> j((size_t)rows*columns,0);
        for(int c=0;c<columns;++c){double h=c%6<3?1e-4:1e-6;auto plus=x,minus=x;
            plus[c]+=h;minus[c]-=h;auto rp=evaluate(plus),rm=evaluate(minus);
            for(int row=0;row<rows;++row)j[(size_t)row*columns+c]=(rp[row]-rm[row])/(2*h);}
        return j;};
    std::vector<double> x(columns,0);
    if(columns){
        // Seed full-frame attachment including the exact 180-degree twist pole.
        for(const auto& mate:mates)if(mate.mate->type=="rigid"||mate.mate->type=="prismatic"){
            const u64 movable=byId.at(mate.mate->b)->fixed?mate.mate->a:mate.mate->b;
            auto at=std::find_if(moving.begin(),moving.end(),[&](const Mover& mover){return !mover.group&&mover.id==movable;});
            if(at==moving.end())continue;
            const bool moveB=movable==mate.mate->b;
            const double axial=mate.mate->type=="prismatic"?jointCoordinate(mate,authored):0;
            const Mat4 target=authored.at(moveB?mate.mate->a:mate.mate->b)*(moveB?mate.a:mate.b)*
                Mat4::translation({0,0,moveB?axial:-axial})*(moveB?mate.b:mate.a).inverted();
            const Vec3 dt=origin(target)-origin(authored.at(movable)),rv=rotationVector(target*authored.at(movable).inverted());
            auto trial=x;const size_t offset=6*(size_t)(at-moving.begin());
            trial[offset]=dt.x;trial[offset+1]=dt.y;trial[offset+2]=dt.z;
            trial[offset+3]=rv.x;trial[offset+4]=rv.y;trial[offset+5]=rv.z;
            if(squared(evaluate(trial))<squared(evaluate(x)))x=std::move(trial);
        }
        // Parallel/opposed axes can start at the wrong exact pole. There the
        // first derivative of a 180-degree flip vanishes, so seed that branch.
        for(const auto& mate:mates){
            const auto s=state(mate,authored);
            const bool needOpposed=mate.mate->type=="contact"||
                (mate.mate->type=="angle"&&K_PI-mate.target<1e-10);
            const bool needAligned=mate.mate->type=="coincident"||mate.mate->type=="revolute"||mate.mate->type=="rigid"||
                (mate.mate->type=="angle"&&mate.target<1e-10);
            if(!((needOpposed&&s.dot>0.95)||(needAligned&&s.dot<-.95)))continue;
            const u64 movable=byId.at(mate.mate->b)->fixed?mate.mate->a:mate.mate->b;
            auto at=std::find_if(moving.begin(),moving.end(),[&](const Mover& mover){return !mover.group&&mover.id==movable;});
            if(at==moving.end()){u64 parent=byId.at(movable)->parent;
                if(parent)at=std::find_if(moving.begin(),moving.end(),[&](const Mover& mover){return mover.group&&mover.id==parent;});}
            if(at==moving.end())continue;
            const Mat4 reference=authored.at(mate.mate->a)*mate.a;
            Vec3 direction=reference.transformDir({1,0,0}).normalized();
            auto trial=x;size_t offset=6*(size_t)(at-moving.begin());
            trial[offset+3]=direction.x*K_PI;trial[offset+4]=direction.y*K_PI;
            trial[offset+5]=direction.z*K_PI;
            if(squared(evaluate(trial))<squared(evaluate(x)))x=std::move(trial);
        }
        for(int phase=0;phase<2;++phase){
        const bool lockRotation=phase==0;
        double damping=1e-3;
        for(int iteration=0;iteration<100;++iteration){
            const auto r=evaluate(x);const double cost=squared(r);
            if(!std::isfinite(cost))return fail("Non-finite assembly mate residual");
            if(cost<1e-16)break;
            const int rows=(int)r.size();const auto j=jacobian(x,rows);
            std::vector<double> h((size_t)columns*columns,0),g(columns,0);
            for(int row=0;row<rows;++row)for(int c=0;c<columns;++c){double v=j[(size_t)row*columns+c];g[c]+=v*r[row];
                for(int q=0;q<=c;++q)h[(size_t)c*columns+q]+=v*j[(size_t)row*columns+q];}
            for(int c=0;c<columns;++c)for(int q=0;q<c;++q)h[(size_t)q*columns+c]=h[(size_t)c*columns+q];
            bool improved=false;
            for(int trial=0;trial<12;++trial){auto regularized=h;
                for(int c=0;c<columns;++c)regularized[(size_t)c*columns+c]+=damping*(1+h[(size_t)c*columns+c]);
                std::vector<double> step,rhs=g;for(double& v:rhs)v=-v;
                if(!solveLinear(regularized,rhs,step,columns))return fail("Assembly mate linear solve failed");
                if(lockRotation)for(size_t k=0;k<moving.size();++k){step[6*k+3]=0;step[6*k+4]=0;step[6*k+5]=0;}
                auto candidate=x;for(int c=0;c<columns;++c)candidate[c]+=step[c];
                const double nextCost=squared(evaluate(candidate));
                if(std::isfinite(nextCost)&&nextCost<cost){x=std::move(candidate);damping=std::max(1e-12,damping*0.25);improved=true;break;}
                damping=std::min(1e12,damping*10);
            }
            if(!improved)break;
        }
        }
        world=posed(x);
    }
    AssemblyDiagnostics result;
    if(columns){const auto r=evaluate(x);result.rank=matrixRank(jacobian(x,(int)r.size()),(int)r.size(),columns);}
    result.freeDofs=columns-result.rank;
    auto carried=[&](u64 id){u64 parent=byId.at(id)->parent;while(parent){
        for(const auto& mover:moving)if(mover.group&&mover.id==parent)return true;
        parent=byId.at(parent)->parent;}return false;};
    for(const auto& n:d.assembly)if(n.feature&&!n.fixed&&!mated.count(n.id)&&!carried(n.id)){
        result.unconstrainedNodes.push_back(n.id);result.freeDofs+=6;}
    for(const auto& mate:mates){const auto s=state(mate,world);const auto r=measuredResidual(mate,world);
        result.maxPositionMm=std::max(result.maxPositionMm,r.positionMm);
        result.maxAngleDeg=std::max(result.maxAngleDeg,r.angleDeg);
        result.mates.push_back(r);
        result.frames.push_back({mate.mate->id,s.pa,s.za,s.pb,s.zb});
        if(!satisfiedResidual(r))
            if(!reportUnsatisfied)return fail(!satisfiedLimit(r)?
                "ASSEMBLY_JOINT_LIMIT_VIOLATION: pose exceeds mate #"+std::to_string(r.id)+" limits":"Unsatisfied assembly mate #"+std::to_string(r.id));
    }
    try{validateMechanicalGraph(d);for(const auto& mate:d.mates)if(isMechanicalMate(mate.type))prepareMotionRelation(d,mate,*byId.at(mate.a),*byId.at(mate.b),world.at(mate.a),world.at(mate.b));}catch(const std::exception& e){return fail(e.what());}
    if(!expandAssemblyPatterns(d,parentFrame,rigid,world,visibility,placements,error,&result.patternReports))return fail(error);
    if(diagnostics)*diagnostics=std::move(result);
    return true;
}

bool buildAssemblyMotion(const Document& document,AssemblyMotionProgram& program,std::string& error){
    program={};if(!resolveAssembly(document,program.baseline,error))return false;
    try{
        std::unordered_map<u64,const AssemblyNode*> nodes;for(const auto& node:document.assembly)nodes[node.id]=&node;
        std::unordered_map<u64,Mat4> world;for(const auto& placement:program.baseline)if(placement.copy==0)world[placement.node]=placement.world;
        auto retainAxis=[&](u64 node,const Mat4& local){auto existing=program.localAxes.find(node);if(existing==program.localAxes.end()){program.localAxes[node]=local;return;}const Mat4 a=world.at(node)*existing->second,b=world.at(node)*local;const auto gap=origin(b)-origin(a);if(axis(a).dot(axis(b))<1-1e-6||(gap-axis(a)*gap.dot(axis(a))).len()>kPositionTolerance)throw std::runtime_error("MECHANICAL_INCONSISTENT_NODE_AXIS: use one oriented semantic axis per moving instance");};
        for(const auto& mate:document.mates){const auto& a=*nodes.at(mate.a);const auto& b=*nodes.at(mate.b);
            if(isMechanicalMate(mate.type)){auto relation=prepareMotionRelation(document,mate,a,b,world.at(mate.a),world.at(mate.b));retainAxis(mate.a,relation.localA);retainAxis(mate.b,relation.localB);program.relations.push_back(std::move(relation));}
            else{AssemblyMotionRelation constraint;constraint.mate=mate;constraint.localA=localReference(document,a,mate.reference_a,mate.fraction_a);constraint.localB=localReference(document,b,mate.reference_b,mate.fraction_b);bool ok=false;const auto env=document.buildEnv();constraint.target=evalExpr(mate.value,env,&ok);if(!ok)throw std::runtime_error("Invalid geometric mate target");if(mate.type=="angle")constraint.target=deg2rad(constraint.target);evaluateLimits(mate,env,constraint.limited,constraint.minimum,constraint.maximum);program.geometricConstraints.push_back(std::move(constraint));}
        }
        if(program.relations.empty())throw std::runtime_error("MECHANICAL_NO_COUPLINGS: add chain, gear, shaft, rack_pinion or cam mates");
        return true;
    }catch(const std::exception& exception){error=exception.what();program={};return false;}
}

arkjson::Value inspectAssemblyMotion(const AssemblyMotionProgram& program,u64 driver,double speedRpm,double timeSeconds,double angleDegrees){
    using V=arkjson::Value;
    if(!std::isfinite(speedRpm)||std::fabs(speedRpm)>100000||!std::isfinite(timeSeconds)||timeSeconds<0||timeSeconds>3600||!std::isfinite(angleDegrees)||std::fabs(angleDegrees)>1e6)throw std::runtime_error("MECHANICAL_INVALID_INPUT: finite speed ±100000 rpm, time 0..3600 s and angle ±1e6 degrees required");
    std::unordered_map<u64,double> angle,speed,displacement,velocity;
    std::unordered_map<u64,Mat4> baseline;for(const auto& placement:program.baseline)if(placement.copy==0)baseline[placement.node]=placement.world;
    if(!program.localAxes.count(driver)||!baseline.count(driver))throw std::runtime_error("MECHANICAL_DRIVER_NOT_COUPLED: driver must be a coupled rotational instance");
    for(const auto& relation:program.relations)if(relation.translational&&relation.mate.b==driver)throw std::runtime_error("MECHANICAL_TRANSLATIONAL_DRIVER_UNSUPPORTED: drive a rotational instance");
    angle[driver]=deg2rad(angleDegrees)+speedRpm*2*K_PI/60*timeSeconds;speed[driver]=speedRpm*2*K_PI/60;
    auto bounded=[&](u64 node){if(angle.count(node)&&(!std::isfinite(angle.at(node))||std::fabs(angle.at(node))>1e9||!std::isfinite(speed.at(node))||std::fabs(speed.at(node))>1e9))throw std::runtime_error("MECHANICAL_RESOURCE_LIMIT: angular position/speed exceeds 1e9 rad or rad/s");if(displacement.count(node)&&(!std::isfinite(displacement.at(node))||std::fabs(displacement.at(node))>1e6||!std::isfinite(velocity.at(node))||std::fabs(velocity.at(node))>1e9))throw std::runtime_error("MECHANICAL_RESOURCE_LIMIT: translation exceeds 1e6 mm or velocity exceeds 1e9 mm/s");};
    // Propagate exact scalar coupling equations in both directions for angular
    // gears/chain/shafts, then forward to bounded analytic translating followers.
    for(size_t pass=0;pass<=program.relations.size();++pass){bool progress=false;
        for(const auto& relation:program.relations){const u64 a=relation.mate.a,b=relation.mate.b;
            if(angle.count(a)&&!angle.count(b)&&!displacement.count(b)){
                if(!relation.translational){angle[b]=relation.ratio*angle[a]+relation.phaseRad;speed[b]=relation.ratio*speed[a];}
                else if(relation.mate.type=="rack_pinion"){displacement[b]=relation.ratio*angle[a];velocity[b]=relation.ratio*speed[a];}
                else{displacement[b]=relation.lift/2*(std::cos(relation.phaseRad)-std::cos(angle[a]+relation.phaseRad));velocity[b]=relation.lift/2*std::sin(angle[a]+relation.phaseRad)*speed[a];}
                bounded(b);progress=true;
            }else if(!relation.translational&&angle.count(b)&&!angle.count(a)){angle[a]=(angle[b]-relation.phaseRad)/relation.ratio;speed[a]=speed[b]/relation.ratio;bounded(a);progress=true;}
        }
        if(!progress)break;
    }
    std::unordered_map<u64,Mat4> world=baseline;
    for(const auto& item:angle){const Mat4 frame=baseline.at(item.first)*program.localAxes.at(item.first);const Vec3 pivot=origin(frame),direction=axis(frame);world[item.first]=Mat4::translation(pivot)*Mat4::axisAngle(direction,std::remainder(item.second,2*K_PI))*Mat4::translation(pivot*-1)*baseline.at(item.first);}
    for(const auto& item:displacement){const Mat4 frame=baseline.at(item.first)*program.localAxes.at(item.first);world[item.first]=Mat4::translation(axis(frame)*item.second)*baseline.at(item.first);}
    double maxPosition=0,maxAngle=0;
    for(const auto& constraint:program.geometricConstraints){const auto prepared=preparedConstraint(constraint);const auto r=measuredResidual(prepared,world);
        maxPosition=std::max(maxPosition,r.positionMm);maxAngle=std::max(maxAngle,r.angleDeg);
        if(!satisfiedResidual(r))throw std::runtime_error("MECHANICAL_STATIC_CONSTRAINT_VIOLATION: motion violates geometric mate #"+std::to_string(constraint.mate.id));
    }
    V result=V::makeObj(),instances=V::makeArr(),relations=V::makeArr();
    for(const auto& placement:program.baseline){V row=V::makeObj(),matrix=V::makeArr();row.set("node",V::makeNum((double)placement.node));row.set("feature",V::makeNum((double)placement.feature));row.set("name",V::makeStr(placement.name));row.set("copy",V::makeNum(placement.copy));row.set("visible",V::makeBool(placement.visible));const bool rotating=angle.count(placement.node),translating=displacement.count(placement.node);row.set("moving",V::makeBool(rotating||translating));for(double value:(rotating||translating?world.at(placement.node):placement.world).m)matrix.arr.push_back(V::makeNum(value));row.set("world_column_major",matrix);
        if(rotating){row.set("angle_deg",V::makeNum(rad2deg(angle.at(placement.node))));row.set("speed_rpm",V::makeNum(speed.at(placement.node)*60/(2*K_PI)));}if(translating){row.set("displacement_mm",V::makeNum(displacement.at(placement.node)));row.set("velocity_mm_s",V::makeNum(velocity.at(placement.node)));}instances.arr.push_back(row);
    }
    for(const auto& relation:program.relations){V row=V::makeObj();row.set("id",V::makeNum((double)relation.mate.id));row.set("type",V::makeStr(relation.mate.type));row.set("a",V::makeNum((double)relation.mate.a));row.set("b",V::makeNum((double)relation.mate.b));if(relation.mate.type!="cam")row.set("ratio",V::makeNum(relation.ratio));row.set("ratio_scope",V::makeStr(relation.mate.type=="rack_pinion"?"mm per input radian":relation.mate.type=="cam"?"harmonic lift law":"signed driven angular speed / driver angular speed"));if(relation.teethA){row.set("teeth_a",V::makeNum(relation.teethA));row.set("teeth_b",V::makeNum(relation.teethB));}if(relation.mate.type=="cam")row.set("lift_mm",V::makeNum(relation.lift));row.set("phase_deg",V::makeNum(rad2deg(relation.phaseRad)));const bool active=angle.count(relation.mate.a);row.set("active",V::makeBool(active));if(active){double residualValue=0;if(!relation.translational)residualValue=angle.at(relation.mate.b)-relation.ratio*angle.at(relation.mate.a)-relation.phaseRad;else{const double expected=relation.mate.type=="cam"?relation.lift/2*(std::cos(relation.phaseRad)-std::cos(angle.at(relation.mate.a)+relation.phaseRad)):relation.ratio*angle.at(relation.mate.a);residualValue=displacement.at(relation.mate.b)-expected;}row.set(relation.translational?"residual_mm":"residual_rad",V::makeNum(residualValue));if(std::fabs(residualValue)>1e-7)throw std::runtime_error("MECHANICAL_COUPLING_RESIDUAL: inconsistent relation");}relations.arr.push_back(row);}
    result.set("driver",V::makeNum((double)driver));result.set("time_s",V::makeNum(timeSeconds));result.set("speed_rpm",V::makeNum(speedRpm));result.set("instances",instances);result.set("relations",relations);result.set("max_static_position_residual_mm",V::makeNum(maxPosition));result.set("max_static_angle_residual_deg",V::makeNum(maxAngle));result.set("static_constraints_verified",V::makeBool(true));result.set("document_unchanged",V::makeBool(true));result.set("model",V::makeStr("ideal kinematic coupling graph; cam is a harmonic lift law relative to authored phase-zero pose"));result.set("tooth_contact_checked",V::makeBool(false));result.set("chain_collision_checked",V::makeBool(false));result.set("cam_contact_checked",V::makeBool(false));result.set("dynamics_checked",V::makeBool(false));return result;
}

bool buildAssemblyManipulation(const Document& document,AssemblyManipulationProgram& program,std::string& error){
    program={};if(!resolveAssembly(document,program.baseline,error,&program.diagnostics))return false;
    try{
        std::unordered_map<u64,const AssemblyNode*> nodes;
        std::unordered_map<u64,Mat4> world;
        std::unordered_map<u64,std::vector<u64>> children;
        for(const auto& placement:program.baseline)if(!placement.copy)world[placement.node]=placement.world;
        for(const auto& node:document.assembly){nodes[node.id]=&node;if(node.parent)children[node.parent].push_back(node.id);
            if(node.feature){program.rigidCluster[node.id]=node.id;if(node.fixed)program.fixedNodes.push_back(node.id);if(node.pattern.active)program.patternNodes.push_back(node.id);}}
        const ExprEnv env=document.buildEnv();std::unordered_map<u64,Mat4> authored;
        std::function<Mat4(u64)> authoredFrame=[&](u64 id){
            if(!id)return Mat4::identity();auto known=authored.find(id);if(known!=authored.end())return known->second;
            const auto& node=*nodes.at(id);const Mat4 parent=authoredFrame(node.parent);
            auto number=[&](const char* key){const auto found=node.p.find(key);if(found==node.p.end())return 0.;bool ok=false;const double value=evalExpr(found->second,env,&ok);if(!ok)throw std::runtime_error("MANIPULATION_INVALID_BASELINE: invalid transform expression");return value;};
            const Mat4 frame=parent*Mat4::translation({number("tx"),number("ty"),number("tz")})*
                Mat4::rotZ(deg2rad(number("rz")))*Mat4::rotY(deg2rad(number("ry")))*Mat4::rotX(deg2rad(number("rx")));
            authored[id]=frame;if(node.feature){program.parentFrames[id]=parent;program.authoredWorld[id]=frame;}return frame;
        };
        for(const auto& node:document.assembly)authoredFrame(node.id);
        std::function<u64(u64)> cluster=[&](u64 id){u64& parent=program.rigidCluster.at(id);return parent==id?id:parent=cluster(parent);};
        for(const auto& mate:document.mates){AssemblyMotionRelation constraint;
            if(isMechanicalMate(mate.type))constraint=prepareMotionRelation(document,mate,*nodes.at(mate.a),*nodes.at(mate.b),world.at(mate.a),world.at(mate.b));
            else{constraint.mate=mate;constraint.localA=localReference(document,*nodes.at(mate.a),mate.reference_a,mate.fraction_a);
                constraint.localB=localReference(document,*nodes.at(mate.b),mate.reference_b,mate.fraction_b);
                bool ok=false;constraint.target=evalExpr(mate.value,env,&ok);if(!ok)throw std::runtime_error("MANIPULATION_INVALID_BASELINE: invalid mate target");if(mate.type=="angle")constraint.target=deg2rad(constraint.target);
                evaluateLimits(mate,env,constraint.limited,constraint.minimum,constraint.maximum);}
            program.constraints.push_back(std::move(constraint));
            if(mate.type=="rigid"){const u64 a=cluster(mate.a),b=cluster(mate.b);if(a!=b)program.rigidCluster[b]=a;}
        }
        auto descendants=[&](u64 id){std::vector<u64> parts,queue{id};for(size_t i=0;i<queue.size();++i){const auto& node=*nodes.at(queue[i]);if(node.feature)parts.push_back(node.id);const auto found=children.find(node.id);if(found!=children.end())queue.insert(queue.end(),found->second.begin(),found->second.end());}return parts;};
        // Match the static solver's cross-parent rigid carriers. A link can own
        // its body and multiple explicitly placed pin instances in one group.
        std::unordered_map<u64,bool> crossOnly;std::set<u64> crossGroups;
        for(const auto& constraint:program.constraints)if(!isMechanicalMate(constraint.mate.type)){
            const auto& m=constraint.mate;
            for(u64 id:{m.a,m.b})if(!crossOnly.count(id))crossOnly[id]=true;
            if(nodes.at(m.a)->parent==nodes.at(m.b)->parent)crossOnly[m.a]=crossOnly[m.b]=false;
            else for(u64 id:{m.a,m.b})if(nodes.at(id)->parent)crossGroups.insert(nodes.at(id)->parent);
        }
        for(u64 group:crossGroups){const auto parts=descendants(group);bool anchored=nodes.at(group)->fixed;
            for(u64 id:parts)anchored|=nodes.at(id)->fixed;
            if(anchored){for(u64 id:parts)if(std::find(program.fixedNodes.begin(),program.fixedNodes.end(),id)==program.fixedNodes.end())program.fixedNodes.push_back(id);}
            else{u64 representative=0;for(u64 id:parts){if(crossOnly.count(id)&&!crossOnly.at(id))continue;
                if(!representative)representative=cluster(id);else program.rigidCluster[cluster(id)]=cluster(representative);}}
        }
        for(auto& item:program.rigidCluster)item.second=cluster(item.first);
        auto fixed=[&](u64 id){return std::find(program.fixedNodes.begin(),program.fixedNodes.end(),id)!=program.fixedNodes.end();};
        for(const auto& candidate:program.constraints){const auto& mate=candidate.mate;
            if(mate.type!="concentric"&&mate.type!="coincident"&&mate.type!="revolute"&&mate.type!="prismatic")continue;
            auto samePair=[&](const AssemblyMate& other){return (other.a==mate.a&&other.b==mate.b)||(other.a==mate.b&&other.b==mate.a);};
            // All constraints between the same endpoints form one joint bundle;
            // their actual residuals still determine whether a drive is allowed.
            auto side=[&](u64 endpoint){std::vector<u64> queue{endpoint};std::unordered_set<u64> seen{endpoint};
                if(nodes.at(mate.a)->parent!=nodes.at(mate.b)->parent&&nodes.at(endpoint)->parent){
                    const auto parts=descendants(nodes.at(endpoint)->parent);bool anchored=false;
                    for(u64 part:parts)anchored|=nodes.at(part)->fixed;
                    if(!anchored)for(u64 part:parts)if(seen.insert(part).second)queue.push_back(part);
                }
                for(size_t i=0;i<queue.size();++i){
                    // Every visited rigid carrier contributes all of its pins,
                    // not only the group at the selected joint. This is the
                    // physical graph used by the static anchor audit as well.
                    for(const auto& member:program.rigidCluster)if(member.second==program.rigidCluster.at(queue[i])&&seen.insert(member.first).second)queue.push_back(member.first);
                    for(const auto& constraint:program.constraints){const auto& edge=constraint.mate;
                        if(isMechanicalMate(edge.type)||samePair(edge))continue;
                        const u64 next=edge.a==queue[i]?edge.b:edge.b==queue[i]?edge.a:0;
                        if(next&&seen.insert(next).second)queue.push_back(next);
                    }
                }
                std::sort(queue.begin(),queue.end());return queue;
            };
            const auto left=side(mate.a),right=side(mate.b);
            auto anchored=[&](const std::vector<u64>& side){for(u64 id:side)if(fixed(id))return true;return false;};
            const bool leftFixed=anchored(left),rightFixed=anchored(right);
            if(!leftFixed&&!rightFixed)continue;
            bool constrained=leftFixed&&rightFixed;
            bool moveB=leftFixed;
            if(constrained){
                // Grounded loop joints expose a stable world pivot for dragging.
                // An internal floating joint does not pick a moving side by guess.
                const bool aFixed=fixed(mate.a),bFixed=fixed(mate.b);
                if(aFixed==bFixed)continue;moveB=aFixed;
            }
            std::vector<u64> moving=moveB?right:left;
            if(!constrained&&std::binary_search(moving.begin(),moving.end(),moveB?mate.a:mate.b))continue;
            for(const auto& constraint:program.constraints)if(isMechanicalMate(constraint.mate.type)){
                const bool a=std::binary_search(moving.begin(),moving.end(),constraint.mate.a),b=std::binary_search(moving.begin(),moving.end(),constraint.mate.b);
                if(a!=b)constrained=true;
            }
            if(constrained){
                std::vector<u64> component{mate.a,mate.b};std::unordered_set<u64> seen{mate.a,mate.b};
                for(size_t k=0;k<component.size();++k){const u64 at=component[k];
                    for(const auto& edge:program.constraints){const u64 next=edge.mate.a==at?edge.mate.b:edge.mate.b==at?edge.mate.a:0;
                        if(next&&seen.insert(next).second)component.push_back(next);}
                    for(const auto& item:program.rigidCluster)if(item.second==program.rigidCluster.at(at)&&seen.insert(item.first).second)component.push_back(item.first);
                }
                moving.clear();for(u64 id:component)if(!fixed(id))moving.push_back(id);std::sort(moving.begin(),moving.end());
            }
            bool unsupported=false;for(u64 id:moving)unsupported|=nodes.at(id)->pattern.active||fixed(id);
            if(unsupported)continue;
            const Mat4 frame=world.at(moveB?mate.a:mate.b)*(moveB?candidate.localA:candidate.localB);
            AssemblyManipulationJoint joint;joint.mate=mate.id;joint.type=mate.type;joint.mode=mate.type=="prismatic"?"translate":"rotate";
            joint.constrained=constrained;joint.movingEndpoint=moveB?mate.b:mate.a;
            joint.origin=origin(frame);joint.axis=axis(frame).normalized();joint.movingNodes=moving;
            joint.coordinateSign=moveB?1:-1;joint.limited=candidate.limited;joint.min=candidate.minimum;joint.max=candidate.maximum;
            const auto prepared=preparedConstraint(candidate);
            if(mate.type=="revolute"||mate.type=="prismatic")joint.position=jointCoordinate(prepared,world);
            program.joints.push_back(joint);
            if(mate.type=="concentric"){joint.mode="translate";program.joints.push_back(std::move(joint));}
        }
        return true;
    }catch(const std::exception& e){error=e.what();program={};return false;}
}

bool selectAssemblyManipulationJoint(const AssemblyManipulationProgram& program,u64 driver,u64 mate,
                                    const std::string& mode,AssemblyManipulationJoint& joint,std::string& error){
    error.clear();joint={};
    auto fail=[&](const char* message){error=message;return false;};
    if(mode!="rotate"&&mode!="translate")return fail("MANIPULATION_INVALID_MODE: mode must be rotate or translate");
    if(!program.rigidCluster.count(driver))return fail("MANIPULATION_INVALID_DRIVER: select an individual assembly part");
    if(std::find(program.fixedNodes.begin(),program.fixedNodes.end(),driver)!=program.fixedNodes.end())return fail("MANIPULATION_FIXED_DRIVER: grounded part cannot be moved");
    if(std::find(program.patternNodes.begin(),program.patternNodes.end(),driver)!=program.patternNodes.end())return fail("MANIPULATION_PATTERN_UNSUPPORTED: select an individual unpatterned instance");
    // Prefer the nearest joint of the selected rigid component. Selecting a
    // wheel drives its axle; selecting its rigidly attached handlebar steers.
    std::unordered_map<u64,int> distance;std::vector<u64> queue{program.rigidCluster.at(driver)};distance[queue[0]]=0;
    for(size_t i=0;i<queue.size();++i)for(const auto& edge:program.constraints){if(isMechanicalMate(edge.mate.type))continue;
        const u64 a=program.rigidCluster.at(edge.mate.a),b=program.rigidCluster.at(edge.mate.b);
        const u64 next=a==queue[i]?b:b==queue[i]?a:0;
        if(next&&!distance.count(next)){distance[next]=distance.at(queue[i])+1;queue.push_back(next);}}
    int best=100000;bool ambiguous=false;
    for(const auto& candidate:program.joints){
        if(candidate.mode!=mode||(mate&&candidate.mate!=mate)||
           !std::binary_search(candidate.movingNodes.begin(),candidate.movingNodes.end(),driver))continue;
        const auto found=std::find_if(program.constraints.begin(),program.constraints.end(),[&](const AssemblyMotionRelation& edge){return edge.mate.id==candidate.mate;});
        const u64 endpoint=candidate.movingEndpoint;
        const auto depth=distance.find(program.rigidCluster.at(endpoint));if(depth==distance.end())continue;
        if(depth->second<best){joint=candidate;best=depth->second;ambiguous=false;}
        else if(depth->second==best){
            const bool equivalent=joint.movingNodes==candidate.movingNodes&&joint.origin.approx(candidate.origin,kPositionTolerance)&&joint.axis.approx(candidate.axis,1e-8);
            if(equivalent){if(candidate.mate<joint.mate)joint=candidate;}else ambiguous=true;
        }
    }
    if(!joint.mate)return fail("MANIPULATION_NO_GROUNDED_JOINT: requires a grounded revolute/prismatic/coincident/concentric joint with individual unpatterned instances");
    if(ambiguous&&!mate){joint={};return fail("MANIPULATION_AMBIGUOUS_JOINT: specify a mate ID for the desired degree of freedom");}
    return true;
}

namespace {
double frameRotationIncrement(const Mat4& previous,const Mat4& current,const Mat4& local){
    const Mat4 a=previous*local,b=current*local;
    const Vec3 x=a.transformDir({1,0,0}),y=b.transformDir({1,0,0});
    return std::atan2(axis(a).dot(x.cross(y)),x.dot(y));
}
double frameTranslationIncrement(const Mat4& previous,const Mat4& current,const Mat4& local){
    const Mat4 a=previous*local,b=current*local;
    return (origin(b)-origin(a)).dot(axis(a));
}
double couplingIncrementResidual(const AssemblyMotionRelation& relation,
        const AssemblyManipulationProgram& program,const std::unordered_map<u64,Mat4>& previous,
        const std::unordered_map<u64,Mat4>& current){
    const auto& m=relation.mate;
    const double a=frameRotationIncrement(previous.at(m.a),current.at(m.a),relation.localA);
    if(!relation.translational)return frameRotationIncrement(previous.at(m.b),current.at(m.b),relation.localB)-relation.ratio*a;
    const double b=frameTranslationIncrement(previous.at(m.b),current.at(m.b),relation.localB);
    if(m.type=="rack_pinion")return b-relation.ratio*a;
    // A cam phase is relative to the authored grounded rotational coordinate,
    // not reset to zero by each saved pose. This remains reproducible on reload.
    for(const auto& joint:program.constraints)if(joint.mate.type=="revolute"&&(joint.mate.a==m.a||joint.mate.b==m.a)){
        const u64 other=joint.mate.a==m.a?joint.mate.b:joint.mate.a;
        if(std::find(program.fixedNodes.begin(),program.fixedNodes.end(),other)==program.fixedNodes.end())continue;
        const Mat4 local=joint.mate.a==m.a?joint.localA:joint.localB;
        const double alignment=axis(previous.at(m.a)*local).dot(axis(previous.at(m.a)*relation.localA));
        if(std::fabs(std::fabs(alignment)-1)>1e-8)continue;
        const double sign=(joint.mate.b==m.a?1:-1)*alignment;
        const double angle=deg2rad(jointCoordinate(preparedConstraint(joint),previous))*sign;
        return b-relation.lift*.5*(std::cos(angle+relation.phaseRad)-std::cos(angle+a+relation.phaseRad));
    }
    throw std::runtime_error("MANIPULATION_CAM_PHASE_UNRESOLVED: cam drag requires a grounded revolute input coordinate");
}
bool constrainedManipulation(const AssemblyManipulationProgram& program,const AssemblyManipulationJoint& joint,
        double delta,std::unordered_map<u64,Mat4>& world,AssemblyManipulationPreview& preview,std::string& error){
    auto fail=[&](const std::string& message){error=message;return false;};
    const auto selected=std::find_if(program.constraints.begin(),program.constraints.end(),[&](const AssemblyMotionRelation& c){return c.mate.id==joint.mate;});
    AssemblyMotionRelation driven=*selected;
    driven.mate.type=joint.mode=="rotate"?"revolute":"prismatic";
    const double start=jointCoordinate(preparedConstraint(driven),world);
    if(joint.limited){const double target=start+joint.coordinateSign*delta;
        if(limitError(target,joint.min,joint.max)>limitRoundoff(target,joint.min,joint.max))return fail("MANIPULATION_LIMIT_VIOLATION: requested drive exceeds selected joint limits");}
    std::vector<std::vector<u64>> clusters;std::map<u64,size_t> clusterIndex;
    for(u64 node:joint.movingNodes){const u64 id=program.rigidCluster.at(node);
        const auto [at,inserted]=clusterIndex.emplace(id,clusters.size());if(inserted)clusters.emplace_back();clusters[at->second].push_back(node);}
    if(clusters.size()>64)return fail("MANIPULATION_RESOURCE_LIMIT: constrained continuation exceeds 64 moving carriers");
    const int columns=(int)clusters.size()*6;
    // Ratios reduce the step before an angular follower could cross atan2's cut.
    std::unordered_map<u64,double> angularScale,linearScale;
    const u64 drivenCarrier=program.rigidCluster.at(joint.movingEndpoint);
    if(joint.mode=="rotate")angularScale[drivenCarrier]=1;else linearScale[drivenCarrier]=1;
    for(size_t pass=0;pass<program.constraints.size();++pass)for(const auto& relation:program.constraints)if(isMechanicalMate(relation.mate.type)){
        const u64 a=program.rigidCluster.at(relation.mate.a),b=program.rigidCluster.at(relation.mate.b);
        if(!relation.translational){
            if(angularScale.count(a)&&!angularScale.count(b))angularScale[b]=std::fabs(relation.ratio)*angularScale.at(a);
            else if(angularScale.count(b)&&!angularScale.count(a))angularScale[a]=angularScale.at(b)/std::fabs(relation.ratio);
        }else if(relation.mate.type=="rack_pinion"){
            if(angularScale.count(a)&&!linearScale.count(b))linearScale[b]=std::fabs(relation.ratio)*deg2rad(angularScale.at(a));
            else if(linearScale.count(b)&&!angularScale.count(a))angularScale[a]=rad2deg(linearScale.at(b)/std::fabs(relation.ratio));
        }else if(angularScale.count(a)&&!linearScale.count(b))linearScale[b]=relation.lift*.5*deg2rad(angularScale.at(a));
    }
    double scale=1;for(const auto& item:angularScale)scale=std::max(scale,item.second);for(const auto& item:linearScale)scale=std::max(scale,item.second);
    const double required=std::ceil(std::fabs(delta)*scale/2);
    if(required>4096)return fail("MANIPULATION_RESOURCE_LIMIT: requested continuation exceeds 4096 steps");
    const int steps=std::max(1,(int)required);int baselineRank=-1;
    using V=arkjson::Value;preview.couplings=V::makeArr();
    for(int stepIndex=1;stepIndex<=steps;++stepIndex){
        const auto previous=world;const double target=start+joint.coordinateSign*delta*stepIndex/steps;
        auto posed=[&](const std::vector<double>& x){auto candidate=previous;
            for(size_t k=0;k<clusters.size();++k){const Vec3 o=origin(previous.at(clusters[k][0]));
                const Vec3 t{x[6*k],x[6*k+1],x[6*k+2]},r{x[6*k+3],x[6*k+4],x[6*k+5]};const double angle=r.len();
                const Mat4 change=Mat4::translation(o+t)*(angle>1e-14?Mat4::axisAngle(r/angle,angle):Mat4::identity())*Mat4::translation(-o);
                for(u64 node:clusters[k])candidate[node]=change*previous.at(node);}
            return candidate;};
        auto evaluate=[&](const std::vector<double>& x){const auto candidate=posed(x);std::vector<double> r;
            for(const auto& constraint:program.constraints){
                if(isMechanicalMate(constraint.mate.type)){
                    if(std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),constraint.mate.a)||std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),constraint.mate.b))
                        r.push_back(couplingIncrementResidual(constraint,program,previous,candidate)*(constraint.translational?1:kAngularWeight));
                }else residual(preparedConstraint(constraint),candidate,r);
            }
            double difference=jointCoordinate(preparedConstraint(driven),candidate)-target;
            if(joint.mode=="rotate")difference=deg2rad(std::remainder(difference,360.))*kAngularWeight;
            r.push_back(difference);return r;};
        auto jacobian=[&](const std::vector<double>& x,int rows){std::vector<double> j((size_t)rows*columns);
            for(int c=0;c<columns;++c){const double h=c%6<3?1e-4:1e-6;auto plus=x,minus=x;plus[c]+=h;minus[c]-=h;
                const auto a=evaluate(plus),b=evaluate(minus);for(int row=0;row<rows;++row)j[(size_t)row*columns+c]=(a[row]-b[row])/(2*h);}return j;};
        std::vector<double> x(columns);double damping=1e-6;
        if(baselineRank<0){const auto r=evaluate(x);baselineRank=matrixRank(jacobian(x,(int)r.size()),(int)r.size(),columns);}
        for(int iteration=0;iteration<80;++iteration){const auto r=evaluate(x);const double cost=squared(r);
            if(!std::isfinite(cost))return fail("MANIPULATION_NONFINITE_POSE: nonfinite constrained residual");
            if(cost<1e-28)break;
            const int rows=(int)r.size();const auto j=jacobian(x,rows);
            std::vector<double> h((size_t)columns*columns),g(columns);
            for(int row=0;row<rows;++row)for(int c=0;c<columns;++c){const double v=j[(size_t)row*columns+c];g[c]+=v*r[row];for(int q=0;q<=c;++q)h[(size_t)c*columns+q]+=v*j[(size_t)row*columns+q];}
            for(int c=0;c<columns;++c)for(int q=0;q<c;++q)h[(size_t)q*columns+c]=h[(size_t)c*columns+q];
            bool improved=false;
            for(int trial=0;trial<10;++trial){auto regularized=h;for(int c=0;c<columns;++c)regularized[(size_t)c*columns+c]+=damping*(1+h[(size_t)c*columns+c]);
                auto rhs=g;for(double& v:rhs)v=-v;std::vector<double> change;if(!solveLinear(regularized,rhs,change,columns))return fail("MANIPULATION_CONSTRAINT_VIOLATION: constrained linear solve failed");
                auto next=x;for(int c=0;c<columns;++c)next[c]+=change[c];const double nextCost=squared(evaluate(next));
                if(std::isfinite(nextCost)&&nextCost<cost){x=std::move(next);damping=std::max(1e-14,damping*.2);improved=true;break;}damping=std::min(1e12,damping*10);}
            if(!improved)break;
        }
        const auto r=evaluate(x);
        if(squared(r)>1e-16)return fail("MANIPULATION_CONSTRAINT_VIOLATION: no continuous solution for requested joint coordinate");
        if(matrixRank(jacobian(x,(int)r.size()),(int)r.size(),columns)<baselineRank)return fail("MANIPULATION_SINGULAR_POSE: continuation reached a singular constraint branch");
        for(size_t k=0;k<clusters.size();++k){const Vec3 rotation{x[6*k+3],x[6*k+4],x[6*k+5]};if(rotation.len()>deg2rad(15))return fail("MANIPULATION_BRANCH_DISCONTINUITY: passive carrier jumped more than 15 degrees in one continuation step");}
        world=posed(x);
        for(const auto& constraint:program.constraints)if(!isMechanicalMate(constraint.mate.type)){
            const auto measured=measuredResidual(preparedConstraint(constraint),world);
            if(!satisfiedLimit(measured))return fail("MANIPULATION_LIMIT_VIOLATION: continuation exceeds mate #"+std::to_string(constraint.mate.id)+" limits");
            if(!satisfiedResidual(measured))return fail("MANIPULATION_CONSTRAINT_VIOLATION: continuation violates mate #"+std::to_string(constraint.mate.id));
        }else if(std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),constraint.mate.a)||std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),constraint.mate.b)){
            const double measured=couplingIncrementResidual(constraint,program,previous,world);
            if(!std::isfinite(measured)||std::fabs(measured)>1e-7)return fail("MANIPULATION_COUPLING_RESIDUAL: transmission equation is unsatisfied");
            const Mat4 a=world.at(constraint.mate.a)*constraint.localA,b=world.at(constraint.mate.b)*constraint.localB;
            const double dot=axis(a).dot(axis(b));
            if(constraint.translational?std::fabs(dot)>1e-6:std::fabs(std::fabs(dot)-1)>1e-6)return fail("MANIPULATION_COUPLING_AXIS_VIOLATION: semantic coupling axes changed incompatibly");
            if(constraint.mate.type=="shaft"){const Vec3 gap=origin(b)-origin(a);if((gap-axis(a)*gap.dot(axis(a))).len()>kPositionTolerance)return fail("MANIPULATION_COUPLING_AXIS_VIOLATION: shaft axes no longer coaxial");}
            if(stepIndex==steps){V row=V::makeObj();row.set("id",V::makeNum((double)constraint.mate.id));row.set("type",V::makeStr(constraint.mate.type));row.set(constraint.translational?"residual_mm":"residual_rad",V::makeNum(measured));row.set("ratio",V::makeNum(constraint.ratio));preview.couplings.arr.push_back(row);}
        }
    }
    preview.continuationSteps=steps;return true;
}
}

bool previewAssemblyManipulation(const AssemblyManipulationProgram& program,u64 driver,u64 mate,
                                const std::string& mode,double delta,
                                AssemblyManipulationPreview& preview,std::string& error){
    preview={};error.clear();
    auto fail=[&](const std::string& message){error=message;preview={};return false;};
    if(!std::isfinite(delta)||std::fabs(delta)>1e6)return fail("MANIPULATION_INVALID_DELTA: finite displacement within +/-1e6 degrees or millimetres required");
    AssemblyManipulationJoint joint;if(!selectAssemblyManipulationJoint(program,driver,mate,mode,joint,error))return false;
    try{
        std::unordered_map<u64,Mat4> baselineWorld;for(const auto& placement:program.baseline)if(!placement.copy)baselineWorld[placement.node]=placement.world;
        // Check the requested continuous displacement before reducing a rotation
        // modulo 360. A complete turn cannot tunnel through an authored stop.
        for(const auto& constraint:program.constraints)if(constraint.limited&&!joint.constrained){
            const bool movingA=std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),constraint.mate.a);
            const bool movingB=std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),constraint.mate.b);
            if(movingA==movingB)continue;
            const auto prepared=preparedConstraint(constraint);const double start=jointCoordinate(prepared,baselineWorld);
            const Vec3 direction=axis(baselineWorld.at(constraint.mate.a)*constraint.localA);
            const double alignment=direction.dot(joint.axis);
            if((constraint.mate.type=="revolute"&&mode!="rotate")||(constraint.mate.type=="prismatic"&&mode!="translate")||std::fabs(std::fabs(alignment)-1)>1e-8){
                if(delta!=0)return fail("MANIPULATION_CONSTRAINT_VIOLATION: requested path is incompatible with limited mate #"+std::to_string(constraint.mate.id));
                continue;
            }
            const double end=start+(movingB?1:-1)*alignment*delta;
            const double roundoff=limitRoundoff(end,constraint.minimum,constraint.maximum);
            if(limitError(end,constraint.minimum,constraint.maximum)>roundoff)return fail("MANIPULATION_LIMIT_VIOLATION: requested drive exceeds mate #"+std::to_string(constraint.mate.id)+" limits");
        }
        const Mat4 change=mode=="rotate"?Mat4::translation(joint.origin)*
            Mat4::axisAngle(joint.axis,deg2rad(std::remainder(delta,360.)))*Mat4::translation(-joint.origin):
            Mat4::translation(joint.axis*delta);
        std::unordered_map<u64,Mat4> world=baselineWorld;
        preview.placements=program.baseline;
        preview.diagnostics.rank=program.diagnostics.rank;preview.diagnostics.freeDofs=program.diagnostics.freeDofs;
        preview.diagnostics.unconstrainedNodes=program.diagnostics.unconstrainedNodes;
        if(joint.constrained&&!constrainedManipulation(program,joint,delta,world,preview,error))return fail(error);
        for(auto& placement:preview.placements){
            if(joint.constrained&&std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),placement.node))placement.world=world.at(placement.node);
            else if(std::binary_search(joint.movingNodes.begin(),joint.movingNodes.end(),placement.node))placement.world=change*placement.world;
            if(!finiteFrame(placement.world))return fail("MANIPULATION_NONFINITE_POSE: preview is not a finite rigid transform");
            if(!placement.copy)world[placement.node]=placement.world;}
        for(u64 id:program.fixedNodes){const auto baseline=std::find_if(program.baseline.begin(),program.baseline.end(),[&](const AssemblyPlacement& row){return row.node==id&&!row.copy;});
            for(int i=0;i<16;++i)if(world.at(id).m[i]!=baseline->world.m[i])return fail("MANIPULATION_FIXED_CONFLICT: drive would move a grounded instance");}
        for(const auto& constraint:program.constraints){
            if(isMechanicalMate(constraint.mate.type))continue; // common rigid carriers preserve their relative pose
            const auto prepared=preparedConstraint(constraint);
            const auto r=measuredResidual(prepared,world);const auto s=state(prepared,world);
            preview.diagnostics.maxPositionMm=std::max(preview.diagnostics.maxPositionMm,r.positionMm);
            preview.diagnostics.maxAngleDeg=std::max(preview.diagnostics.maxAngleDeg,r.angleDeg);
            preview.diagnostics.mates.push_back(r);preview.diagnostics.frames.push_back({r.id,s.pa,s.za,s.pb,s.zb});
            if(!satisfiedResidual(r))return fail("MANIPULATION_CONSTRAINT_VIOLATION: drive violates mate #"+std::to_string(r.id));
        }
        using V=arkjson::Value;preview.commands=V::makeArr();
        for(const auto& placement:preview.placements){if(placement.copy||std::find(program.fixedNodes.begin(),program.fixedNodes.end(),placement.node)!=program.fixedNodes.end())continue;
            const Mat4& authored=program.authoredWorld.at(placement.node);double difference=0;
            for(int i=0;i<16;++i)difference=std::max(difference,std::fabs(placement.world.m[i]-authored.m[i]));
            if(difference<1e-12)continue;
            const Mat4 local=program.parentFrames.at(placement.node).inverted()*placement.world;
            const double cy=std::hypot(local.m[0],local.m[1]),ry=std::atan2(-local.m[2],cy);
            const double rx=cy>1e-10?std::atan2(local.m[6],local.m[10]):0;
            const double rz=cy>1e-10?std::atan2(local.m[1],local.m[0]):std::atan2(-local.m[4],local.m[5]);
            if(std::fabs(local.m[12])>1e6||std::fabs(local.m[13])>1e6||std::fabs(local.m[14])>1e6)return fail("MANIPULATION_POSE_RANGE: parent-local translation exceeds 1e6 mm");
            V command=V::makeObj(),p=V::makeObj();command.set("op",V::makeStr("assembly.edit"));command.set("id",V::makeNum((double)placement.node));
            p.set("tx",V::makeNum(local.m[12]));p.set("ty",V::makeNum(local.m[13]));p.set("tz",V::makeNum(local.m[14]));
            p.set("rx",V::makeNum(rad2deg(rx)));p.set("ry",V::makeNum(rad2deg(ry)));p.set("rz",V::makeNum(rad2deg(rz)));
            command.set("p",p);preview.commands.arr.push_back(std::move(command));
        }
        preview.joint=std::move(joint);preview.delta=delta;return true;
    }catch(const std::exception& e){return fail(e.what());}
}

bool probeAssemblyMate(Document& document,const AssemblyMate& candidate,AssemblyDiagnostics& diagnostics,
                       std::string& error,bool& satisfied){
    AssemblyMate extra=candidate;
    if(!extra.id)extra.id=document.nextMateId_;
    for(const auto& mate:document.mates)if(mate.id==extra.id)extra.id=document.nextMateId_+document.mates.size()+1;
    document.mates.push_back(extra);
    struct Pop{Document& d;~Pop(){d.mates.pop_back();}} pop{document};
    std::vector<AssemblyPlacement> placements;
    if(!resolveAssembly(document,placements,error,&diagnostics,true))return false;
    satisfied=std::isfinite(diagnostics.maxPositionMm)&&std::isfinite(diagnostics.maxAngleDeg)&&
              diagnostics.maxPositionMm<=kPositionTolerance&&diagnostics.maxAngleDeg<=kAngleToleranceDeg;
    for(const auto& row:diagnostics.mates)satisfied=satisfied&&satisfiedResidual(row);
    return true;
}
}
