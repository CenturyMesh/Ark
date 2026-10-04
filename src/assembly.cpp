#include "ark/model.hpp"
#include "ark/references.hpp"
#include <cmath>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace ark {
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
struct PreparedMate {
    const AssemblyMate* mate=nullptr;
    Mat4 a,b;
    double target=0;
};
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
    if(type=="concentric"){
        append(d-s.za*d.dot(s.za),out);
        append(c*kAngularWeight,out);
    }else if(type=="coincident"){
        append(d,out);
        append(c*kAngularWeight,out);
        out.push_back(kAngularWeight*(1-s.dot));
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
        if(!a->second->feature||!b->second->feature)return fail("Mates require part instances, not groups");
        if(a->second->parent!=b->second->parent)return fail("Mates across different assembly parents are unsupported");
        if(mate.type!="concentric"&&mate.type!="coincident"&&mate.type!="contact"&&mate.type!="distance"&&mate.type!="angle")return fail("Unsupported assembly mate type: "+mate.type);
        if(!std::isfinite(mate.fraction_a)||!std::isfinite(mate.fraction_b)||
           mate.fraction_a<0||mate.fraction_a>1||mate.fraction_b<0||mate.fraction_b>1)return fail("Invalid mate reference fraction");
        bool ok=false;double target=evalExpr(mate.value,env,&ok);
        if(!ok||!std::isfinite(target)||std::fabs(target)>1e6)return fail("Invalid assembly mate value #"+std::to_string(mate.id));
        if(mate.type=="angle"&&(target<0||target>180))return fail("Mate angle must be between 0 and 180 degrees");
        if(mate.type!="angle"&&mate.type!="distance"&&std::fabs(target)>1e-12)return fail("Only distance and angle mates accept nonzero values");
        PreparedMate prepared;prepared.mate=&mate;prepared.target=mate.type=="angle"?deg2rad(target):target;
        try{prepared.a=localReference(d,*a->second,mate.reference_a,mate.fraction_a);
            prepared.b=localReference(d,*b->second,mate.reference_b,mate.fraction_b);}
        catch(const std::exception& e){return fail("Mate #"+std::to_string(mate.id)+" reference: "+e.what());}
        if(!finiteFrame(prepared.a)||!finiteFrame(prepared.b))return fail("Mate reference is not a finite rigid frame");
        mates.push_back(prepared);mated.insert(mate.a);mated.insert(mate.b);
    }
    std::vector<u64> moving;
    for(const auto& n:d.assembly)if(n.feature&&mated.count(n.id)&&!n.fixed)moving.push_back(n.id);
    if(moving.size()>64)return fail("Assembly mate solve exceeds 64 moving instances");
    // Every connected mate component must have an explicit fixed instance.
    std::unordered_set<u64> seen;
    for(u64 id:mated){
        if(seen.count(id))continue;std::vector<u64> queue{id};seen.insert(id);bool anchored=false;
        for(size_t k=0;k<queue.size();++k){u64 at=queue[k];anchored|=byId.at(at)->fixed;
            for(const auto& mate:d.mates){u64 next=mate.a==at?mate.b:(mate.b==at?mate.a:0);
                if(next&&seen.insert(next).second)queue.push_back(next);}}
        if(!anchored)return fail("Mate component has no fixed anchor (node #"+std::to_string(id)+")");
    }
    const auto authored=world;
    const int columns=(int)moving.size()*6;
    auto posed=[&](const std::vector<double>& x){auto placed=authored;
        for(size_t k=0;k<moving.size();++k){const Mat4& base=authored.at(moving[k]);
            Vec3 dt{x[6*k],x[6*k+1],x[6*k+2]};Vec3 rv{x[6*k+3],x[6*k+4],x[6*k+5]};
            const Vec3 o=origin(base);const double angle=rv.len();
            Mat4 rotation=angle>1e-14?Mat4::axisAngle(rv/angle,angle):Mat4::identity();
            placed[moving[k]]=Mat4::translation(o+dt)*rotation*Mat4::translation(-o)*base;
        }return placed;};
    auto evaluate=[&](const std::vector<double>& x){auto posedWorld=posed(x);std::vector<double> r;
        for(const auto& mate:mates)residual(mate,posedWorld,r);return r;};
    auto jacobian=[&](const std::vector<double>& x,int rows){std::vector<double> j((size_t)rows*columns,0);
        for(int c=0;c<columns;++c){double h=c%6<3?1e-4:1e-6;auto plus=x,minus=x;
            plus[c]+=h;minus[c]-=h;auto rp=evaluate(plus),rm=evaluate(minus);
            for(int row=0;row<rows;++row)j[(size_t)row*columns+c]=(rp[row]-rm[row])/(2*h);}
        return j;};
    std::vector<double> x(columns,0);
    if(columns){
        // Parallel/opposed axes can start at the wrong exact pole. There the
        // first derivative of a 180-degree flip vanishes, so seed that branch.
        for(const auto& mate:mates){
            const auto s=state(mate,authored);
            const bool needOpposed=mate.mate->type=="contact"||
                (mate.mate->type=="angle"&&K_PI-mate.target<1e-10);
            const bool needAligned=mate.mate->type=="coincident"||
                (mate.mate->type=="angle"&&mate.target<1e-10);
            if(!((needOpposed&&s.dot>0.95)||(needAligned&&s.dot<-.95)))continue;
            const u64 movable=byId.at(mate.mate->b)->fixed?mate.mate->a:mate.mate->b;
            auto at=std::find(moving.begin(),moving.end(),movable);
            if(at==moving.end())continue;
            const Mat4 reference=authored.at(mate.mate->a)*mate.a;
            Vec3 direction=reference.transformDir({1,0,0}).normalized();
            auto trial=x;size_t offset=6*(size_t)(at-moving.begin());
            trial[offset+3]=direction.x*K_PI;trial[offset+4]=direction.y*K_PI;
            trial[offset+5]=direction.z*K_PI;
            if(squared(evaluate(trial))<squared(evaluate(x)))x=std::move(trial);
        }
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
                auto candidate=x;for(int c=0;c<columns;++c)candidate[c]+=step[c];
                const double nextCost=squared(evaluate(candidate));
                if(std::isfinite(nextCost)&&nextCost<cost){x=std::move(candidate);damping=std::max(1e-12,damping*0.25);improved=true;break;}
                damping=std::min(1e12,damping*10);
            }
            if(!improved)break;
        }
        world=posed(x);
    }
    AssemblyDiagnostics result;
    if(columns){const auto r=evaluate(x);result.rank=matrixRank(jacobian(x,(int)r.size()),(int)r.size(),columns);}
    result.freeDofs=columns-result.rank;
    for(const auto& n:d.assembly)if(n.feature&&!n.fixed&&!mated.count(n.id)){
        result.unconstrainedNodes.push_back(n.id);result.freeDofs+=6;}
    for(const auto& mate:mates){const auto s=state(mate,world);const Vec3 d=s.pb-s.pa;
        AssemblyMateResidual r;r.id=mate.mate->id;r.type=mate.mate->type;
        const double theta=rad2deg(std::acos(s.dot));
        if(r.type=="concentric"){
            r.positionMm=(d-s.za*d.dot(s.za)).len();r.angleDeg=std::min(theta,180-theta);
        }else if(r.type=="coincident"){
            r.positionMm=d.len();r.angleDeg=theta;
        }else if(r.type=="contact"){
            r.positionMm=std::fabs(d.dot(s.za));r.angleDeg=180-theta;
        }else if(r.type=="distance"){
            r.valueError=d.dot(s.za)-mate.target;r.positionMm=std::fabs(r.valueError);
        }else{
            r.valueError=theta-rad2deg(mate.target);r.angleDeg=std::fabs(r.valueError);
        }
        result.maxPositionMm=std::max(result.maxPositionMm,r.positionMm);
        result.maxAngleDeg=std::max(result.maxAngleDeg,r.angleDeg);
        result.mates.push_back(r);
        result.frames.push_back({mate.mate->id,s.pa,s.za,s.pb,s.zb});
        if(!std::isfinite(r.positionMm)||!std::isfinite(r.angleDeg)||
           r.positionMm>kPositionTolerance||r.angleDeg>kAngleToleranceDeg)
            if(!reportUnsatisfied)return fail("Unsatisfied assembly mate #"+std::to_string(r.id));
    }
    auto number=[&](const std::string& text,const char* label){bool ok=false;double value=evalExpr(text,env,&ok);if(!ok||!std::isfinite(value)||std::fabs(value)>1e6)error=std::string("Invalid assembly pattern expression: ")+label;return value;};
    for(auto& n:d.assembly)if(n.feature){
        int copies=1;
        if(n.pattern.active){
            if(n.pattern.type!="linear"&&n.pattern.type!="circular")return fail("Unsupported instance pattern: "+n.pattern.type);
            double raw=number(n.pattern.count,"count");if(!error.empty())return fail(error);
            if(std::fabs(raw-std::round(raw))>1e-6||raw<2||raw>32)return fail("Instance pattern count must be an integer from 2 to 32");
            copies=(int)std::round(raw);
            if(n.pattern.type=="linear"){double dx=number(n.pattern.dx,"dx"),dy=number(n.pattern.dy,"dy"),dz=number(n.pattern.dz,"dz");if(!error.empty())return fail(error);if(dx*dx+dy*dy+dz*dz<=1e-18)return fail("Linear instance pattern spacing is zero");}
            else{double step=number(n.pattern.step,"step");if(!error.empty())return fail(error);if(!(std::fabs(step)>0&&std::fabs(step)<=360))return fail("Circular instance pattern step must be in (0, 360] degrees");if(n.pattern.axis!="X"&&n.pattern.axis!="Y"&&n.pattern.axis!="Z")return fail("Circular instance pattern axis must be X, Y or Z");}
        }
        Mat4 authored0=parentFrame[n.id]*rigid[n.id];
        Mat4 carry=world[n.id]*authored0.inverted();
        for(int copy=0;copy<copies;++copy){
            Mat4 step=Mat4::identity();
            if(copy){
                if(n.pattern.type=="linear"){double dx=number(n.pattern.dx,"dx"),dy=number(n.pattern.dy,"dy"),dz=number(n.pattern.dz,"dz");step=Mat4::translation({dx*copy,dy*copy,dz*copy});}
                else{Vec3 axis=n.pattern.axis=="X"?Vec3{1,0,0}:n.pattern.axis=="Y"?Vec3{0,1,0}:Vec3{0,0,1};step=Mat4::axisAngle(axis,deg2rad(number(n.pattern.step,"step")*copy));}
                if(!error.empty())return fail(error);
            }
            placements.push_back({n.id,n.feature,n.name,carry*(parentFrame[n.id]*step*rigid[n.id]),visibility[n.id],copy});
        }
    }
    if(diagnostics)*diagnostics=std::move(result);
    return true;
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
    for(const auto& row:diagnostics.mates)if(row.id==extra.id){
        satisfied=satisfied&&row.positionMm<=kPositionTolerance&&row.angleDeg<=kAngleToleranceDeg;break;}
    return true;
}
}
