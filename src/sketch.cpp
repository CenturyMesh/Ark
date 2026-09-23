#include "ark/sketch.hpp"
#include <algorithm>
#include <stdexcept>

namespace ark {
namespace {
double expressionValue(const std::string& expression,const ExprEnv& env){
    bool ok=false;double value=evalExpr(expression,env,&ok);
    if(!ok||!std::isfinite(value)||std::abs(value)>1e7)throw std::runtime_error("Invalid sketch/plane expression: "+expression);
    return value;
}
double distanceToSegment(Vec2 p,Vec2 a,Vec2 b){
    auto d=b-a;double t=d.lensq()>0?clamp((p-a).dot(d)/d.lensq(),0.,1.):0.;
    return (p-(a+d*t)).len();
}
void cubicPoints(Vec2 a,Vec2 b,Vec2 c,Vec2 d,double tol,std::vector<Vec2>& out,int depth=0){
    // A Bezier curve is contained in the convex hull of its controls. Distance
    // to the chord segment is convex, so this bounds curve-to-polyline error.
    if(std::max(distanceToSegment(b,a,d),distanceToSegment(c,a,d))<=tol){
        if(out.size()>=2048)throw std::runtime_error("Curve exceeds 2048 samples; increase tolerance or split sketch");
        out.push_back(d);return;
    }
    if(depth>=24)throw std::runtime_error("Spline subdivision failed to meet tolerance");
    Vec2 ab=(a+b)*.5,bc=(b+c)*.5,cd=(c+d)*.5,abc=(ab+bc)*.5,bcd=(bc+cd)*.5,m=(abc+bcd)*.5;
    cubicPoints(a,ab,abc,m,tol,out,depth+1);cubicPoints(m,bcd,cd,d,tol,out,depth+1);
}
bool intersects(Vec2 a,Vec2 b,Vec2 c,Vec2 d){
    constexpr double eps=1e-7;
    if(std::max(a.x,b.x)+eps<std::min(c.x,d.x)||std::max(c.x,d.x)+eps<std::min(a.x,b.x)||
       std::max(a.y,b.y)+eps<std::min(c.y,d.y)||std::max(c.y,d.y)+eps<std::min(a.y,b.y))return false;
    auto sign=[](double v,double e){return v>e?1:v< -e?-1:0;};
    double ab=eps*std::max((b-a).len(),1.),cd=eps*std::max((d-c).len(),1.);
    return sign((b-a).cross(c-a),ab)*sign((b-a).cross(d-a),ab)<=0 &&
           sign((d-c).cross(a-c),cd)*sign((d-c).cross(b-c),cd)<=0;
}
bool inside(Vec2 p,const std::vector<Vec2>& poly){
    bool result=false;for(size_t i=0,j=poly.size()-1;i<poly.size();j=i++){
        auto a=poly[i],b=poly[j];if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)result=!result;
    }return result;
}
arkjson::Value vectorValue(Vec3 p){auto v=arkjson::Value::makeArr();for(auto n:{p.x,p.y,p.z})v.arr.push_back(arkjson::Value::makeNum(n));return v;}
}

void evaluateSketch(NamedSketch& ns,const ExprEnv& env){
    ns.error.clear();ns.residual=0;
    try{
        for(auto& e:ns.sk.ents)for(size_t i=0;i<e.expressions.size();++i)if(!e.expressions[i].empty())e.dof[i]=expressionValue(e.expressions[i],env);
        for(auto& c:ns.sk.cons){
            if(!c.expression.empty())c.val=expressionValue(c.expression,env);
            if(!c.expression2.empty())c.val2=expressionValue(c.expression2,env);
            if((c.type==SketchCon::Radius&&c.val<=0)||(c.type==SketchCon::DistPP&&c.val<0))throw std::runtime_error("Invalid dimension target");
        }
        SketchDiagnostics diagnostics;ns.residual=solveSketch(ns.sk,nullptr,200,&diagnostics);
        ns.rank=diagnostics.rank;ns.freeDofs=diagnostics.variables-diagnostics.rank;
        if(!std::isfinite(ns.residual)||ns.residual>1e-6)throw std::runtime_error("Conflicting constraints or solver failed to converge");
        for(auto& e:ns.sk.ents){
            for(auto x:e.dof)if(!std::isfinite(x)||std::abs(x)>1e7)throw std::runtime_error("Sketch coordinate outside finite bounds");
            sampleSketchEntity(e,ns.sk.tolerance); // validate even unconsumed sketches
        }
    }catch(const std::exception& ex){ns.error=ex.what();ns.residual=INFINITY;}
}

std::vector<Vec2> sampleSketchEntity(const SketchEnt& e,double tolerance){
    if(!std::isfinite(tolerance)||tolerance<.0001||tolerance>5)throw std::runtime_error("Sketch tolerance must be 0.0001..5 mm");
    auto pt=[&](size_t i){return Vec2{e.dof.at(2*i),e.dof.at(2*i+1)};};
    if(e.kind==SketchEnt::Pnt)return {pt(0)};
    if(e.kind==SketchEnt::Line){if((pt(1)-pt(0)).len()<1e-8)throw std::runtime_error("Zero-length sketch line");return {pt(0),pt(1)};}
    std::vector<Vec2> out;
    if(e.kind==SketchEnt::Circle){
        double r=e.dof.at(2);if(r<=1e-8||!std::isfinite(r))throw std::runtime_error("Circle radius must be positive");
        double a=std::acos(clamp(1-tolerance/r,-1.,1.));int n=(int)std::ceil(K_PI/std::max(a,1e-12));n=std::max(12,((n+3)/4)*4);
        if(n>2044)throw std::runtime_error("Circle exceeds sampling limit; increase tolerance");
        for(int i=0;i<n;++i){double t=K_2PI*i/n;out.push_back(pt(0)+Vec2{r*std::cos(t),r*std::sin(t)});}out.push_back(out.front());return out;
    }
    auto segments=sketchSplineSegments(e);out.push_back(segments.front()[0]);
    for(auto& segment:segments)cubicPoints(segment[0],segment[1],segment[2],segment[3],tolerance,out);
    return out;
}

std::vector<std::array<Vec2,4>> sketchSplineSegments(const SketchEnt& e){
    if(e.kind!=SketchEnt::Spline)throw std::runtime_error("Expected spline entity");
    auto pt=[&](size_t i){return Vec2{e.dof.at(2*i),e.dof.at(2*i+1)};};
    size_t n=e.dof.size()/2;
    if(n<(e.closed?3u:2u)||n>64||e.dof.size()%2||!std::isfinite(e.tension)||e.tension<0||e.tension>1)throw std::runtime_error("Spline requires 2..64 anchors (closed: 3 minimum), tension 0..1");
    for(size_t i=1;i<n;++i)if((pt(i)-pt(i-1)).len()<1e-7)throw std::runtime_error("Consecutive spline anchors coincide");
    if(e.closed&&(pt(0)-pt(n-1)).len()<1e-7)throw std::runtime_error("Closed spline wraps automatically; do not repeat first anchor");
    auto anchor=[&](int i){if(e.closed)return pt((size_t)((i+(int)n)%(int)n));if(i<0)return pt(0)*2-pt(1);if(i>=(int)n)return pt(n-1)*2-pt(n-2);return pt((size_t)i);};
    std::vector<std::array<Vec2,4>> segments;
    for(int i=0;i<(int)n-(e.closed?0:1);++i){
        Vec2 p0=anchor(i-1),p1=anchor(i),p2=anchor(i+1),p3=anchor(i+2);
        double t01=std::sqrt((p1-p0).len()),t12=std::sqrt((p2-p1).len()),t23=std::sqrt((p3-p2).len());
        Vec2 m1=((p2-p1)+((p1-p0)/t01-(p2-p0)/(t01+t12))*t12)*(1-e.tension);
        Vec2 m2=((p2-p1)+((p3-p2)/t23-(p3-p1)/(t12+t23))*t12)*(1-e.tension);
        segments.push_back({p1,p1+m1/3,p2-m2/3,p2});
    }return segments;
}

std::vector<SketchProfile> sketchProfiles(const Sketch& sketch,const Mat4& frame){
    std::vector<std::vector<Vec2>> curves,loops;size_t samples=0;
    for(auto& e:sketch.ents)if(!e.construction&&e.kind!=SketchEnt::Pnt){auto p=sampleSketchEntity(e,sketch.tolerance);samples+=p.size();if(samples>4096)throw std::runtime_error("Sketch exceeds 4096 profile samples");curves.push_back(std::move(p));}
    if(curves.empty())throw std::runtime_error("Sketch has no profile geometry");
    std::vector<bool> used(curves.size(),false);
    for(size_t seed=0;seed<curves.size();++seed){
        if(used[seed])continue;
        auto loop=curves[seed];used[seed]=true;
        while((loop.front()-loop.back()).len()>1e-6){
            int found=-1;bool reverse=false;
            for(size_t i=0;i<curves.size();++i)if(!used[i])for(int end=0;end<2;++end){
                Vec2 p=end?curves[i].back():curves[i].front();
                if((loop.back()-p).len()<=1e-6){if(found>=0)throw std::runtime_error("Ambiguous/branched profile connection");found=(int)i;reverse=end!=0;}
            }
            if(found<0)throw std::runtime_error("Open sketch profile; explicitly close its endpoints");
            auto curve=curves[(size_t)found];if(reverse)std::reverse(curve.begin(),curve.end());
            loop.insert(loop.end(),curve.begin()+1,curve.end());used[(size_t)found]=true;
        }
        loop.pop_back();if(loop.size()<3||loop.size()>2048)throw std::runtime_error("Profile requires 3..2048 sampled vertices");
        double area=0;for(size_t i=0;i<loop.size();++i){auto a=loop[i],b=loop[(i+1)%loop.size()];if((b-a).len()<1e-8)throw std::runtime_error("Degenerate profile edge");area+=a.cross(b);
            for(size_t j=i+1;j<loop.size();++j)if(j!=i+1&&!(i==0&&j==loop.size()-1)&&intersects(a,b,loop[j],loop[(j+1)%loop.size()]))throw std::runtime_error("Self-intersecting or touching sampled profile");
        }
        if(std::abs(area)<1e-8)throw std::runtime_error("Profile has zero area");
        if(area<0)std::reverse(loop.begin(),loop.end());
        for(auto& previous:loops){
            for(size_t i=0;i<loop.size();++i)for(size_t j=0;j<previous.size();++j)if(intersects(loop[i],loop[(i+1)%loop.size()],previous[j],previous[(j+1)%previous.size()]))throw std::runtime_error("Profile loops intersect or touch");
            if(inside(loop.front(),previous)||inside(previous.front(),loop))throw std::runtime_error("Nested sketch loops require separate cut features");
        }loops.push_back(std::move(loop));
    }
    std::vector<SketchProfile> profiles;for(auto& loop:loops){SketchProfile p;p.frame=frame;for(size_t i=0;i<loop.size();++i)p.segs.push_back({ProfileSeg::Line,loop[i],loop[(i+1)%loop.size()]});profiles.push_back(std::move(p));}return profiles;
}

arkjson::Value inspectSketch(const Document& doc,const NamedSketch& ns,bool samples){
    using V=arkjson::Value;V r=V::makeObj(),entities=V::makeArr();auto frame=resolvePlane(doc,ns.sk.plane);
    r.set("name",V::makeStr(ns.name));r.set("plane",V::makeStr(ns.sk.plane));r.set("tolerance_mm",V::makeNum(ns.sk.tolerance));
    r.set("residual",V::makeNum(ns.residual));r.set("remaining_dofs",V::makeNum(ns.freeDofs));r.set("jacobian_rank",V::makeNum(ns.rank));
    r.set("constraint_status",V::makeStr(ns.error.empty()?(ns.freeDofs?"underconstrained":"locally_fully_constrained"):"failed"));r.set("error",V::makeStr(ns.error));
    r.set("diagnosis",diagnoseSketch(ns.sk));
    for(auto& e:ns.sk.ents){V item=V::makeObj(),dof=V::makeArr(),world=V::makeArr(),local=V::makeArr();
        item.set("name",V::makeStr(e.name));item.set("kind",V::makeStr(e.kind==SketchEnt::Spline?"spline":e.kind==SketchEnt::Circle?"circle":e.kind==SketchEnt::Line?"line":"point"));
        item.set("construction",V::makeBool(e.construction));for(auto x:e.dof)dof.arr.push_back(V::makeNum(x));item.set("resolved_dof",dof);
        auto points=sampleSketchEntity(e,ns.sk.tolerance);item.set("sample_count",V::makeNum(points.size()));
        if(samples){for(auto p:points){local.arr.push_back(vectorValue({p.x,p.y,0}));world.arr.push_back(vectorValue(frame.transformPoint({p.x,p.y,0})));}item.set("local",local);item.set("world",world);}entities.arr.push_back(item);
    }r.set("entities",entities);
    try{auto profiles=sketchProfiles(ns.sk,frame);r.set("profile_ready",V::makeBool(true));r.set("loops",V::makeNum(profiles.size()));}
    catch(const std::exception& e){r.set("profile_ready",V::makeBool(false));r.set("profile_error",V::makeStr(e.what()));}
    r.set("solid_certified",V::makeBool(false));return r;
}
}
