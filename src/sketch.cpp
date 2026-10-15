#include "ark/sketch.hpp"
#include "ark/references.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/assembly.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <mapbox/earcut.hpp>

namespace ark {
void resolveSketchCorners(Sketch& sketch,const ExprEnv& env);
Vec3 AuthoredArc::point(double fraction) const {
    const double angle=sweep*fraction;
    return center+(xAxis*std::cos(angle)+normal.cross(xAxis)*std::sin(angle))*radius;
}
Vec3 AuthoredArc::tangent(double fraction) const {
    const double angle=sweep*fraction;
    return (xAxis*(-std::sin(angle))+normal.cross(xAxis)*std::cos(angle))*(sweep<0?-1.:1.);
}
namespace {
void finiteArcPoint(Vec3 point){
    for(double value:{point.x,point.y,point.z})if(!std::isfinite(value)||std::abs(value)>1e7)throw std::runtime_error("Arc input outside finite supported bounds");
}
double positiveArcAngle(Vec3 xAxis,Vec3 yAxis,Vec3 radial){
    double angle=std::atan2(yAxis.dot(radial),xAxis.dot(radial));
    if(angle<=0)angle+=2*K_PI;
    return angle;
}
}
AuthoredArc arcThroughThreePoints(Vec3 start,Vec3 through,Vec3 end){
    finiteArcPoint(start);finiteArcPoint(through);finiteArcPoint(end);
    const Vec3 a=through-start,b=end-start,n=a.cross(b);
    const double la=a.len(),lb=b.len(),lc=(end-through).len(),ln=n.len();
    if(std::min({la,lb,lc})<=1e-8)throw std::runtime_error("Three-point arc needs three distinct points");
    if(ln<=1e-10*la*lb)throw std::runtime_error("Three-point arc points are collinear or numerically degenerate");
    AuthoredArc arc;arc.center=start+(b.cross(n)*a.lensq()+n.cross(a)*b.lensq())/(2*n.lensq());
    finiteArcPoint(arc.center);arc.radius=(start-arc.center).len();arc.normal=n/ln;arc.xAxis=(start-arc.center)/arc.radius;
    arc.sweep=positiveArcAngle(arc.xAxis,arc.normal.cross(arc.xAxis),end-arc.center);
    const double middle=positiveArcAngle(arc.xAxis,arc.normal.cross(arc.xAxis),through-arc.center);
    if(!(arc.radius>1e-8&&arc.sweep>1e-10&&arc.sweep<2*K_PI-1e-10&&middle<arc.sweep))throw std::runtime_error("Three-point arc construction is degenerate");
    return arc;
}
AuthoredArc arcFromStartTangent(Vec3 start,Vec3 end,Vec3 tangent){
    finiteArcPoint(start);finiteArcPoint(end);finiteArcPoint(tangent);
    const Vec3 chord=end-start;const double length=chord.len(),tangentLength=tangent.len();
    if(length<=1e-8)throw std::runtime_error("Tangent arc endpoints coincide");
    if(tangentLength<=1e-12)throw std::runtime_error("Tangent arc start direction is zero");
    tangent=tangent/tangentLength;
    const Vec3 perpendicular=chord-tangent*chord.dot(tangent);const double span=perpendicular.len();
    if(span<=1e-10*length)throw std::runtime_error("Tangent arc endpoint is on the tangent line; use an explicit line");
    AuthoredArc arc;arc.radius=chord.lensq()/(2*span);arc.center=start+perpendicular*(arc.radius/span);
    finiteArcPoint(arc.center);arc.xAxis=(start-arc.center)/arc.radius;arc.normal=arc.xAxis.cross(tangent).normalized();
    arc.sweep=positiveArcAngle(arc.xAxis,arc.normal.cross(arc.xAxis),end-arc.center);
    if(!(arc.radius>1e-8&&arc.sweep>1e-10&&arc.sweep<2*K_PI-1e-10))throw std::runtime_error("Tangent arc construction is degenerate");
    return arc;
}
bool validateSketchArcRecipe(const arkjson::Value& recipe,std::string& error){
    using V=arkjson::Value;auto fail=[&](const char* text){error=text;return false;};
    if(recipe.t!=V::Obj)return fail("Arc recipe must be an object");
    const auto type=recipe.find("type");if(!type||type->t!=V::Str||(type->str!="three_point"&&type->str!="tangent"))return fail("Arc recipe type must be three_point or tangent");
    for(const auto& field:recipe.obj)if(field.first!="type"&&field.first!="start"&&field.first!="end"&&field.first!=(type->str=="three_point"?"through":"tangent"))return fail("Unknown arc recipe field");
    for(const char* key:{"start","end",type->str=="three_point"?"through":"tangent"}){
        auto coordinates=recipe.find(key);if(!coordinates||coordinates->t!=V::Arr||coordinates->arr.size()!=2)return fail("Arc recipe coordinates must be [x,y]");
        for(const auto& coordinate:coordinates->arr)if(!((coordinate.t==V::Num&&std::isfinite(coordinate.num)&&std::abs(coordinate.num)<=1e7)||(coordinate.t==V::Str&&!coordinate.str.empty()&&coordinate.str.size()<=2048)))return fail("Arc recipe coordinates must be finite numbers or nonempty expressions");
    }
    return true;
}
namespace {
double expressionValue(const std::string& expression,const ExprEnv& env){
    bool ok=false;double value=evalExpr(expression,env,&ok);
    if(!ok||!std::isfinite(value)||std::abs(value)>1e7)throw std::runtime_error("Invalid sketch/plane expression: "+expression);
    return value;
}
void evaluateArcRecipe(SketchEnt& entity,const ExprEnv& env){
    if(entity.arcRecipe.t!=arkjson::Value::Obj)return;
    std::string error;if(entity.kind!=SketchEnt::Arc||!validateSketchArcRecipe(entity.arcRecipe,error))throw std::runtime_error(error.empty()?"Only an arc accepts a construction recipe":error);
    auto point=[&](const char* key){const auto& values=entity.arcRecipe.find(key)->arr;auto value=[&](size_t i){return values[i].t==arkjson::Value::Str?expressionValue(values[i].str,env):values[i].num;};return Vec3{value(0),value(1),0};};
    const Vec3 start=point("start"),end=point("end");
    const auto arc=entity.arcRecipe.find("type")->str=="three_point"?arcThroughThreePoints(start,point("through"),end):arcFromStartTangent(start,end,point("tangent"));
    const double a0=rad2deg(std::atan2(start.y-arc.center.y,start.x-arc.center.x));
    entity.dof={arc.center.x,arc.center.y,arc.radius,a0,a0+rad2deg(arc.sweep)*(arc.normal.z<0?-1.:1.)};
    entity.expressions.clear();
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
bool drivenKind(SketchEnt::Kind kind){return kind==SketchEnt::Project||kind==SketchEnt::Offset;}
void assignPolyline(SketchEnt& entity,const std::vector<Vec2>& points){
    std::vector<Vec2> unique;unique.reserve(points.size());
    for(const auto& point:points){if(!std::isfinite(point.x)||!std::isfinite(point.y)||std::abs(point.x)>1e7||std::abs(point.y)>1e7)throw std::runtime_error("Sketch coordinate outside finite bounds");if(!unique.empty()&&(point-unique.back()).len()<1e-8)continue;unique.push_back(point);}
    if(unique.size()<2)throw std::runtime_error("Projected or offset geometry is degenerate");
    if(unique.size()>65)throw std::runtime_error("Projected or offset geometry exceeds 64 segments");
    entity.segments.clear();entity.dof.clear();entity.expressions.clear();
    entity.dof.push_back(unique.front().x);entity.dof.push_back(unique.front().y);
    for(size_t i=1;i<unique.size();++i){SketchEnt::WireSeg segment;segment.kind=SketchEnt::WireSeg::Line;entity.segments.push_back(segment);entity.dof.push_back(unique[i].x);entity.dof.push_back(unique[i].y);}
}
std::vector<Vec2> offsetPolyline(const std::vector<Vec2>& source,double distance){
    if(source.size()<2||!std::isfinite(distance)||std::abs(distance)<1e-9||std::abs(distance)>1e6)throw std::runtime_error("Offset distance must be a nonzero finite length");
    bool closed=(source.front()-source.back()).len()<1e-6&&source.size()>=4;
    std::vector<Vec2> points=source;if(closed)points.pop_back();
    const int count=(int)points.size();if(count<2)throw std::runtime_error("Offset source is degenerate");
    auto left=[](Vec2 direction){double length=direction.len();if(length<1e-8)throw std::runtime_error("Offset source is degenerate");return Vec2{-direction.y/length,direction.x/length};};
    auto at=[&](int index){if(closed){int wrapped=(index%count+count)%count;return points[(size_t)wrapped];}return points[(size_t)std::clamp(index,0,count-1)];};
    std::vector<Vec2> moved((size_t)count);
    for(int i=0;i<count;++i){
        if(!closed&&(i==0||i==count-1)){Vec2 normal=i==0?left(points[1]-points[0]):left(points[(size_t)count-1]-points[(size_t)count-2]);moved[(size_t)i]=points[(size_t)i]+normal*distance;continue;}
        Vec2 previous=left(points[(size_t)i]-at(i-1)),next=left(at(i+1)-points[(size_t)i]),miter=previous+next;
        double denom=miter.dot(previous);
        if(miter.len()<1e-8||std::abs(denom)<0.25)moved[(size_t)i]=points[(size_t)i]+previous*distance;
        else moved[(size_t)i]=points[(size_t)i]+miter*(distance/denom);
    }
    if(closed)moved.push_back(moved.front());
    return moved;
}
void bakeSketchOffsets(Sketch& sketch,const ExprEnv& env){
    for(size_t index=0;index<sketch.ents.size();++index){
        SketchEnt& entity=sketch.ents[index];if(entity.kind!=SketchEnt::Offset)continue;
        const SketchEnt* source=nullptr;size_t sourceIndex=0;
        for(size_t i=0;i<sketch.ents.size();++i)if(sketch.ents[i].name==entity.sourceName){if(source)throw std::runtime_error("Ambiguous offset source");source=&sketch.ents[i];sourceIndex=i;}
        if(!source)throw std::runtime_error("Offset source was not found");
        if(sourceIndex>=index)throw std::runtime_error("Offset source must be an earlier entity");
        if(drivenKind(source->kind)&&source->dof.empty())continue;
        if(source->kind==SketchEnt::Pnt)throw std::runtime_error("Offset source must be a curve");
        double distance=expressionValue(entity.distance,env);
        if(!std::isfinite(distance)||std::abs(distance)<1e-9||std::abs(distance)>1e6)throw std::runtime_error("Offset distance must be a nonzero finite length");
        if(source->kind==SketchEnt::Circle||source->kind==SketchEnt::Arc){
            const bool circle=source->kind==SketchEnt::Circle;const double a0=circle?0:source->dof.at(3),a1=circle?360:source->dof.at(4),sweep=a1-a0;
            const double radius=source->dof.at(2)-(sweep>0?distance:-distance);if(radius<=1e-8)throw std::runtime_error("Sketch offset collapses the circle/arc radius");
            const Vec2 center{source->dof[0],source->dof[1]};auto point=[&](double degrees){const double a=deg2rad(degrees);return center+Vec2{radius*std::cos(a),radius*std::sin(a)};};
            const Vec2 start=point(a0);entity.dof={start.x,start.y};entity.segments.clear();entity.expressions.clear();const int count=(int)std::ceil(std::abs(sweep)/180.);
            if(count<1||count>2)throw std::runtime_error("Offset arc requires one turn or less");for(int i=1;i<=count;++i){const auto end=circle&&i==count?start:point(a0+sweep*i/count);SketchEnt::WireSeg segment;segment.kind=SketchEnt::WireSeg::Arc;segment.sweep=sweep>0?1:-1;entity.segments.push_back(segment);entity.dof.insert(entity.dof.end(),{end.x,end.y,radius});}
        }else{
            auto moved=offsetPolyline(sampleSketchEntity(*source,sketch.tolerance,&sketch),distance);const bool closed=(moved.front()-moved.back()).len()<1e-6;
            for(size_t i=1;i<moved.size();++i)for(size_t j=i+2;j<moved.size();++j)if(!(closed&&i==1&&j==moved.size()-1)&&intersects(moved[i-1],moved[i],moved[j-1],moved[j]))throw std::runtime_error("Sketch offset self-intersects or collapses; reduce distance");
            assignPolyline(entity,moved);
        }
    }
}
Vec2 onSketch(const Mat4& inverse,Vec3 world){Vec3 local=inverse.transformPoint(world);return {local.x,local.y};}
const Body* evaluatedBody(const Document& document,u64 feature){
    const auto& bodies=document.evaluatingBodies?*document.evaluatingBodies:document.evalBodies;
    for(const auto& body:bodies)if(body.id==feature)return &body;
    return nullptr;
}

}

arkjson::Value projectGeometry(Document& document,u64 feature,const std::string& reference,const std::string& plane,u64 sourceInstance,int sourceCopy,const std::string& mode,double displayTolerance){
    using V=arkjson::Value;
    if(sourceCopy<0||sourceCopy>31||(!sourceInstance&&sourceCopy))throw std::runtime_error("Projection copy requires a selected assembly instance");
    auto& bodies=document.evaluatingBodies?*document.evaluatingBodies:document.evalBodies;
    ensureNativeBody(document,feature,bodies);
    const Body* source=evaluatedBody(document,feature);if(!source)throw std::runtime_error("Projection source must be evaluated before its consumer");
    auto brep=source->nativeData.find("brep_base64"),topology=source->nativeData.find("topology");
    if(!brep||brep->t!=V::Str||!topology)throw std::runtime_error("Projection requires an exact source and its semantic topology registry");
    V request=V::makeObj();request.set("input_brep",*brep);request.set("input_topology",*topology);request.set("reference",V::makeStr(reference));
    request.set("mode",V::makeStr(plane.empty()?"3d":mode));request.set("display_tolerance",V::makeNum(displayTolerance));
    auto matrix=[](Mat4 frame){V values=V::makeArr();for(double value:frame.m)values.arr.push_back(V::makeNum(value));return values;};
    if(!plane.empty())request.set("plane_frame",matrix(resolvePlane(document,plane)));
    if(sourceInstance){
        std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(document,placements,error))throw std::runtime_error(error);
        const AssemblyPlacement* selected=nullptr;for(const auto& placement:placements)if(placement.node==sourceInstance&&placement.copy==sourceCopy){if(selected)throw std::runtime_error("Ambiguous projection instance copy");selected=&placement;}
        if(!selected||selected->feature!=feature)throw std::runtime_error("Projection instance does not own the selected source feature/copy");
        request.set("source_frame",matrix(selected->world));
    }
    V worker=V::makeObj();worker.set("operation",V::makeStr("project_reference"));worker.set("request",request);return nativeWorkerRequest(worker);
}

arkjson::Value projectSketchGeometry(Document& document,const std::string& sourceSketch,const std::string& sourceEntity,const std::string& plane,const std::string& mode,double displayTolerance){
    using V=arkjson::Value;V request=V::makeObj();request.set("input_profile",sketchEntityNativeProfile(document,sourceSketch,sourceEntity));request.set("mode",V::makeStr(plane.empty()?"3d":mode));request.set("display_tolerance",V::makeNum(displayTolerance));
    if(!plane.empty()){V frame=V::makeArr();for(double value:resolvePlane(document,plane).m)frame.arr.push_back(V::makeNum(value));request.set("plane_frame",frame);}
    V worker=V::makeObj();worker.set("operation",V::makeStr("project_reference"));worker.set("request",request);return nativeWorkerRequest(worker);
}

void evaluateSketch(NamedSketch& ns,const ExprEnv& env){
    ns.error.clear();ns.residual=0;
    try{
        for(auto& e:ns.sk.ents){for(size_t i=0;i<e.expressions.size();++i)if(!e.expressions[i].empty())e.dof[i]=expressionValue(e.expressions[i],env);evaluateArcRecipe(e,env);if(e.kind==SketchEnt::Recipe)evaluateSketchEntityRecipe(e,env,ns.sk.tolerance);}
        for(auto& c:ns.sk.cons){
            if(!c.expression.empty())c.val=expressionValue(c.expression,env);
            if(!c.expression2.empty())c.val2=expressionValue(c.expression2,env);
            if((c.type==SketchCon::Radius&&c.val<=0)||(c.type==SketchCon::DistPP&&c.val<0))throw std::runtime_error("Invalid dimension target");
        }
        resolveSketchCorners(ns.sk,env);
        bakeSketchOffsets(ns.sk,env);
        SketchDiagnostics diagnostics;ns.residual=solveSketch(ns.sk,nullptr,200,&diagnostics);
        bakeSketchOffsets(ns.sk,env);
        resolveSketchCorners(ns.sk,env);
        ns.rank=diagnostics.rank;ns.freeDofs=diagnostics.variables-diagnostics.rank;
        if(!std::isfinite(ns.residual)||ns.residual>1e-6){
            std::string message="Conflicting constraints or solver failed to converge";
            // Diagnose the attempted candidate, never change or remove a
            // relation to make a failed solve appear successful.
            const auto report=diagnoseSketch(ns.sk);int shown=0;
            for(const auto& row:report.find("constraints")->arr)if(row.find("violated")->b){
                if(shown++==4){message+="; more candidates in sketch_diagnose diagnostics";break;}
                message+=(shown==1?": ":"; ")+std::string("constraint ")+std::to_string((int)row.find("index")->num+1);
                if(auto* id=row.find("id"))message+=" ["+id->str+"]";
                message+=" "+row.find("type")->str;
                for(const auto& entity:row.find("entities")->arr)if(!entity.find("name")->str.empty())message+=" / "+entity.find("name")->str;
            }
            message+=". Review targets and driven expressions; no constraints were removed";
            throw std::runtime_error(message);
        }
        for(auto& e:ns.sk.ents){
            for(auto x:e.dof)if(!std::isfinite(x)||std::abs(x)>1e7)throw std::runtime_error("Sketch coordinate outside finite bounds");
            if(drivenKind(e.kind)&&e.dof.empty())continue;
            sampleSketchEntity(e,ns.sk.tolerance,&ns.sk); // validate even unconsumed sketches
        }
    }catch(const std::exception& ex){ns.error=ex.what();ns.residual=INFINITY;}
}

DrivenSketchStatus bakeSketchProjects(Document& document,NamedSketch& sketch){
    DrivenSketchStatus status;
    bool projects=false;for(const auto& entity:sketch.sk.ents)projects|=entity.kind==SketchEnt::Project;
    if(!projects)return status;
    Mat4 plane=Mat4::identity(),inverse=Mat4::identity();
    try{plane=resolvePlane(document,sketch.sk.plane);inverse=plane.inverted();}
    catch(const std::exception& error){status.error=error.what();return status;}
    for(auto& entity:sketch.sk.ents){
        if(entity.kind!=SketchEnt::Project)continue;
        if(!entity.sourceSketch.empty()){
            size_t sourceIndex=document.sketches.size(),targetIndex=document.sketches.size();
            for(size_t i=0;i<document.sketches.size();++i){if(document.sketches[i].name==entity.sourceSketch)sourceIndex=i;if(document.sketches[i].name==sketch.name)targetIndex=i;}
            if(sourceIndex>=targetIndex){status.error="Projection source sketch must precede its target; self references and cycles reject";return status;}
            const auto& source=document.sketches[sourceIndex];
            const SketchEnt* selected=nullptr;for(const auto& item:source.sk.ents)if(item.name==entity.sourceEntity)selected=&item;
            if(selected&&selected->kind==SketchEnt::Project&&selected->projectionBrep.empty()){
                if(!source.error.empty()){status.error=source.error;return status;}
                status.waiting=true;continue;
            }
        }
        const Feature* feature=entity.sourceSketch.empty()?document.find(entity.sourceFeature):nullptr;
        if(entity.sourceSketch.empty()){
        if(!feature){status.error="Project source was not found";return status;}
        if(feature->suppressed){status.error="Project source is suppressed";return status;}
        if(!evaluatedBody(document,feature->id)){
            bool attempted=false;for(const auto& result:document.lastResults)if(result.id==feature->id)attempted=true;
            if(attempted){status.error="Project source is not evaluated yet";return status;}
            status.waiting=true;continue;
        }
        }
        try{
            auto projected=entity.sourceSketch.empty()?projectGeometry(document,feature->id,entity.reference,sketch.sk.plane,entity.sourceInstance,entity.sourceCopy,entity.projectionMode,sketch.sk.tolerance):projectSketchGeometry(document,entity.sourceSketch,entity.sourceEntity,sketch.sk.plane,"normal",sketch.sk.tolerance);
            entity.projectionBrep=projected.find("local_brep_base64")->str;entity.projectionLength=projected.find("report")->find("length_mm")->num;
            entity.projectionSamples.clear();
            for(const auto& point:projected.find("polyline_world")->arr){auto local=onSketch(inverse,{point.arr[0].num,point.arr[1].num,point.arr[2].num});if(!std::isfinite(local.x)||!std::isfinite(local.y)||std::abs(local.x)>1e7||std::abs(local.y)>1e7)throw std::runtime_error("Projected sketch coordinate exceeds its finite domain");entity.projectionSamples.push_back(local);}
            if(entity.projectionSamples.size()<2)throw std::runtime_error("Exact projection has no display samples");
            const auto a=entity.projectionSamples.front(),b=entity.projectionSamples.back();entity.dof={a.x,a.y,b.x,b.y};entity.expressions.clear();entity.segments={{SketchEnt::WireSeg::Line,1}};
        }catch(const std::exception& error){status.error=error.what();return status;}
    }
    return status;
}

namespace {
void appendArc(Vec2 center,double radius,double a0Deg,double a1Deg,double tolerance,std::vector<Vec2>& out,bool includeStart){
    if(!(radius>1e-8)||!std::isfinite(radius))throw std::runtime_error("Arc radius must be positive");
    double sweep=a1Deg-a0Deg;
    if(!(std::abs(sweep)>1e-9&&std::abs(sweep)<=360.+1e-6))throw std::runtime_error("Arc sweep must be in (0, 360] degrees");
    double step=std::acos(clamp(1-tolerance/radius,-1.,1.));
    int n=(int)std::ceil(std::abs(deg2rad(sweep))/std::max(step,1e-12));
    n=std::max(2,n);
    if(n>2044)throw std::runtime_error("Arc exceeds sampling limit; increase tolerance");
    for(int i=includeStart?0:1;i<=n;++i){
        double t=deg2rad(a0Deg+sweep*i/n);
        out.push_back(center+Vec2{radius*std::cos(t),radius*std::sin(t)});
    }
}
}

const char* sketchEntityKindName(SketchEnt::Kind kind){
    switch(kind){
        case SketchEnt::Pnt: return "point";
        case SketchEnt::Line: return "line";
        case SketchEnt::Circle: return "circle";
        case SketchEnt::Spline: return "spline";
        case SketchEnt::Arc: return "arc";
        case SketchEnt::Wire: return "wire";
        case SketchEnt::Project: return "project";
        case SketchEnt::Offset: return "offset";
        case SketchEnt::Recipe: return "recipe";
    }
    return "unknown";
}

int sketchVertexCount(const SketchEnt& entity){
    if(entity.kind==SketchEnt::Recipe)return (int)entity.dof.size()/2;
    if(entity.kind==SketchEnt::Project||entity.kind==SketchEnt::Offset) return entity.segments.empty()?0:1+(int)entity.segments.size();
    if(entity.kind==SketchEnt::Wire) return 1+(int)entity.segments.size();
    if(entity.kind==SketchEnt::Line) return 2;
    if(entity.kind==SketchEnt::Spline) return (int)entity.dof.size()/2;
    return 1;
}

int sketchVertexDof(const SketchEnt& entity,int vertex){
    if(entity.kind!=SketchEnt::Wire&&entity.kind!=SketchEnt::Project&&entity.kind!=SketchEnt::Offset) return vertex*2;
    if(vertex<=0) return 0;
    int index=2;
    for(int i=0;i<vertex-1&&i<(int)entity.segments.size();++i){
        auto kind=entity.segments[(size_t)i].kind;
        index+=kind==SketchEnt::WireSeg::Line?2:kind==SketchEnt::WireSeg::Arc?3:6;
    }
    if(vertex-1<(int)entity.segments.size()&&entity.segments[(size_t)(vertex-1)].kind==SketchEnt::WireSeg::Cubic) index+=4;
    return index;
}

bool sketchMinorArc(Vec2 start,Vec2 end,double radius,int sweep,Vec2& center,double& a0Deg,double& a1Deg){
    if((sweep!=1&&sweep!=-1)||!(radius>1e-8)||!std::isfinite(radius)) return false;
    Vec2 chord=end-start;double length=chord.len();
    if(length<1e-8||length>radius*2+1e-6) return false;
    Vec2 left{-chord.y/length,chord.x/length},mid=(start+end)*.5;
    double half=length*.5,h2=radius*radius-half*half;
    if(h2<-1e-8) return false;
    double h=h2<=0?0:std::sqrt(h2);
    center=mid-left*(h*(double)sweep);
    auto degrees=[&](Vec2 p){return rad2deg(std::atan2(p.y-center.y,p.x-center.x));};
    double a0=degrees(start),a1=degrees(end);
    Vec2 bulge=h<1e-8?mid+left*((double)sweep*radius):center+(mid-center)*(radius/std::max(h,1e-12));
    double bulgeAngle=degrees(bulge);
    auto wrap=[&](double d){while(d<=-360)d+=360;while(d>360)d-=360;return d;};
    auto contains=[&](double delta){double target=wrap(bulgeAngle-a0);if(delta>=0){if(target<0)target+=360;return target>=-1e-4&&target<=delta+1e-4;}if(target>0)target-=360;return target<=1e-4&&target>=delta-1e-4;};
    double direct=wrap(a1-a0),other=direct>0?direct-360:direct+360;
    double chosen=std::abs(direct)<=180.+1e-3?direct:other;
    if(!contains(chosen)) chosen=contains(direct)?direct:other;
    if(!(std::abs(chosen)>1e-6&&std::abs(chosen)<=180.+1e-3)) return false;
    a0Deg=a0;a1Deg=a0+chosen;return true;
}

std::vector<SketchCurve> sketchPresentedCurves(const Sketch& sketch,const SketchEnt& entity){
    std::vector<SketchCurve> curves;
    bool used=false;
    for(const auto& corner:sketch.corners)if(corner.entity==entity.name&&!entity.name.empty())used=true;
    if(!used||(entity.kind!=SketchEnt::Wire&&entity.kind!=SketchEnt::Project&&entity.kind!=SketchEnt::Offset))return curves;
    if(entity.segments.empty()||entity.dof.size()<4)throw std::runtime_error("Fillet corner is not between two edges");
    std::vector<Vec2> points;
    std::vector<SketchEnt::WireSeg::Kind> kinds;
    auto at=[&](int index){return Vec2{entity.dof.at((size_t)index),entity.dof.at((size_t)index+1)};};
    Vec2 cursor=at(0);int dof=2;points.push_back(cursor);
    for(const auto& segment:entity.segments){
        if(segment.kind==SketchEnt::WireSeg::Line){cursor=at(dof);dof+=2;}
        else if(segment.kind==SketchEnt::WireSeg::Arc){cursor=at(dof);dof+=3;}
        else if(segment.kind==SketchEnt::WireSeg::Cubic){cursor=at(dof+4);dof+=6;}
        else throw std::runtime_error("Wire segment kind must be line, arc or cubic");
        points.push_back(cursor);kinds.push_back(segment.kind);
    }
    const bool repeated=points.size()>=4&&(points.front()-points.back()).len()<1e-6;
    const int unique=repeated?(int)points.size()-1:(int)points.size();
    const bool closed=repeated;
    if(unique<2)throw std::runtime_error("Fillet corner is not between two edges");
    struct Cut{bool on=false;bool chamfer=false;double t=0;Vec2 incoming,outgoing,center;double radius=0,a0=0,a1=0;};
    std::vector<Cut> cuts((size_t)unique);
    for(const auto& corner:sketch.corners){
        if(corner.entity!=entity.name)continue;
        const char* unfit=corner.chamfer?"Chamfer distance does not fit the corner":"Fillet radius does not fit the corner";
        if(corner.corner<0||corner.corner>=unique)throw std::runtime_error("Fillet corner is not between two edges");
        if(!closed&&(corner.corner==0||corner.corner==unique-1))throw std::runtime_error("Fillet corner is not between two edges");
        const int previous=closed?(corner.corner+unique-1)%unique:corner.corner-1;
        const int next=corner.corner;
        if(previous<0||next>=(int)kinds.size()||kinds[(size_t)previous]!=SketchEnt::WireSeg::Line||kinds[(size_t)next]!=SketchEnt::WireSeg::Line)
            throw std::runtime_error("Fillet needs two straight edges");
        const Vec2 vertex=points[(size_t)corner.corner];
        const Vec2 before=points[(size_t)previous];
        const Vec2 after=points[(size_t)(corner.corner+1)];
        Vec2 towardPrevious=before-vertex,towardNext=after-vertex;
        const double previousLength=towardPrevious.len(),nextLength=towardNext.len();
        if(previousLength<1e-8||nextLength<1e-8)throw std::runtime_error(unfit);
        towardPrevious=towardPrevious*(1/previousLength);towardNext=towardNext*(1/nextLength);
        const double alpha=std::acos(clamp(towardPrevious.dot(towardNext),-1.,1.));
        const Vec2 bisector=towardPrevious+towardNext;
        if(bisector.len()<1e-6||alpha<1e-4||K_PI-alpha<1e-4)throw std::runtime_error(unfit);
        const double size=corner.solved;
        const double trim=corner.chamfer?size:size/std::tan(alpha*0.5);
        if(!(trim>1e-9)||trim>=previousLength-1e-8||trim>=nextLength-1e-8)throw std::runtime_error(unfit);
        Cut cut;cut.on=true;cut.chamfer=corner.chamfer;cut.t=trim;
        cut.incoming=vertex+towardPrevious*trim;cut.outgoing=vertex+towardNext*trim;
        if(!corner.chamfer){
            const Vec2 center=vertex+bisector.normalized()*(size/std::sin(alpha*0.5));
            auto degrees=[&](Vec2 p){return rad2deg(std::atan2(p.y-center.y,p.x-center.x));};
            const double a0=degrees(cut.incoming),a1=degrees(cut.outgoing);
            auto wrap=[&](double d){while(d<=-180)d+=360;while(d>180)d-=360;return d;};
            const double sweep=wrap(a1-a0);
            if(!(std::abs(sweep)>1e-4&&std::abs(sweep)<=180.+1e-3))throw std::runtime_error(unfit);
            cut.center=center;cut.radius=size;cut.a0=a0;cut.a1=a0+sweep;
        }
        cuts[(size_t)corner.corner]=cut;
    }
    const int edges=(int)kinds.size();
    for(int edge=0;edge<edges;++edge){
        if(kinds[(size_t)edge]!=SketchEnt::WireSeg::Line)continue;
        const double length=(points[(size_t)edge+1]-points[(size_t)edge]).len();
        const double startTrim=cuts[(size_t)edge].on?cuts[(size_t)edge].t:0;
        const int endCorner=closed&&edge==edges-1?0:(edge+1<unique?edge+1:-1);
        const double endTrim=endCorner>=0&&cuts[(size_t)endCorner].on?cuts[(size_t)endCorner].t:0;
        if(startTrim+endTrim>0&&startTrim+endTrim>=length-1e-8){
            const bool chamfer=(cuts[(size_t)edge].on&&cuts[(size_t)edge].chamfer)||(endCorner>=0&&cuts[(size_t)endCorner].on&&cuts[(size_t)endCorner].chamfer);
            throw std::runtime_error(chamfer?"Chamfer distance does not fit the corner":"Fillet radius does not fit the corner");
        }
    }
    auto emitLine=[&](Vec2 a,Vec2 b){if((b-a).len()<1e-8)return;SketchCurve curve;curve.kind=SketchCurve::Line;curve.a=a;curve.b=b;curves.push_back(curve);};
    for(int edge=0;edge<edges;++edge){
        const int endCorner=closed&&edge==edges-1?0:(edge+1<unique?edge+1:-1);
        const Vec2 from=cuts[(size_t)edge].on?cuts[(size_t)edge].outgoing:points[(size_t)edge];
        const Vec2 to=endCorner>=0&&cuts[(size_t)endCorner].on?cuts[(size_t)endCorner].incoming:points[(size_t)edge+1];
        emitLine(from,to);
        if(endCorner>=0&&cuts[(size_t)endCorner].on){
            const Cut& cut=cuts[(size_t)endCorner];
            if(cut.chamfer){SketchCurve line;line.kind=SketchCurve::Line;line.a=cut.incoming;line.b=cut.outgoing;line.corner=endCorner;curves.push_back(line);}
            else{SketchCurve arc;arc.kind=SketchCurve::Arc;arc.a=cut.incoming;arc.b=cut.outgoing;arc.center=cut.center;arc.radius=cut.radius;arc.a0=cut.a0;arc.a1=cut.a1;arc.corner=endCorner;curves.push_back(arc);}
        }
    }
    if(curves.empty())throw std::runtime_error("Fillet corner is not between two edges");
    return curves;
}

void resolveSketchCorners(Sketch& sketch,const ExprEnv& env){
    if(sketch.corners.size()>64)throw std::runtime_error("Sketch corner capacity exceeded");
    for(auto& corner:sketch.corners){
        const SketchEnt* found=nullptr;
        for(const auto& entity:sketch.ents)if(entity.name==corner.entity){
            if(found)throw std::runtime_error("Ambiguous sketch entity");
            found=&entity;
        }
        if(!found)throw std::runtime_error("Sketch entity not found");
        if(found->kind!=SketchEnt::Wire)throw std::runtime_error("Fillet a wire corner");
        const double size=expressionValue(corner.size,env);
        if(!(size>0)||!std::isfinite(size)||size>1e6)throw std::runtime_error(corner.chamfer?"Chamfer distance must be a positive length":"Fillet radius must be a positive length");
        corner.solved=size;
    }
    for(const auto& entity:sketch.ents){
        bool used=false;for(const auto& corner:sketch.corners)if(corner.entity==entity.name)used=true;
        if(used)(void)sketchPresentedCurves(sketch,entity);
    }
}

std::vector<Vec2> sampleSketchEntity(const SketchEnt& e,double tolerance,const Sketch* owner){
    if(!std::isfinite(tolerance)||tolerance<.0001||tolerance>5)throw std::runtime_error("Sketch tolerance must be 0.0001..5 mm");
    if(e.kind==SketchEnt::Project&&!e.projectionBrep.empty())return e.projectionSamples;
    if(e.kind==SketchEnt::Recipe){if(e.recipeSamples.empty())throw std::runtime_error("Entity recipe is not evaluated");return e.recipeSamples;}
    auto pt=[&](size_t i){return Vec2{e.dof.at(2*i),e.dof.at(2*i+1)};};
    auto at=[&](int i){return Vec2{e.dof.at((size_t)i),e.dof.at((size_t)i+1)};};
    if(e.kind==SketchEnt::Pnt)return {pt(0)};
    if(e.kind==SketchEnt::Line){if((pt(1)-pt(0)).len()<1e-8)throw std::runtime_error("Zero-length sketch line");return {pt(0),pt(1)};}
    std::vector<Vec2> out;
    if(e.kind==SketchEnt::Circle){
        double r=e.dof.at(2);if(r<=1e-8||!std::isfinite(r))throw std::runtime_error("Circle radius must be positive");
        double a=std::acos(clamp(1-tolerance/r,-1.,1.));int n=(int)std::ceil(K_PI/std::max(a,1e-12));n=std::max(12,((n+3)/4)*4);
        if(n>2044)throw std::runtime_error("Circle exceeds sampling limit; increase tolerance");
        for(int i=0;i<n;++i){double t=K_2PI*i/n;out.push_back(pt(0)+Vec2{r*std::cos(t),r*std::sin(t)});}out.push_back(out.front());return out;
    }
    if(e.kind==SketchEnt::Arc){
        if(e.dof.size()!=5)throw std::runtime_error("Arc needs center, radius and two angles");
        appendArc({e.dof[0],e.dof[1]},e.dof[2],e.dof[3],e.dof[4],tolerance,out,true);return out;
    }
    if(e.kind==SketchEnt::Wire||e.kind==SketchEnt::Project||e.kind==SketchEnt::Offset){
        if(owner){
            bool trimmed=false;
            for(const auto& corner:owner->corners)if(corner.entity==e.name&&!e.name.empty())trimmed=true;
            if(trimmed){
                const auto presented=sketchPresentedCurves(*owner,e);
                std::vector<Vec2> trimmedSamples;
                for(const auto& curve:presented){
                    if(trimmedSamples.empty())trimmedSamples.push_back(curve.a);
                    if(curve.kind==SketchCurve::Line){if((curve.b-trimmedSamples.back()).len()>1e-9)trimmedSamples.push_back(curve.b);}
                    else appendArc(curve.center,curve.radius,curve.a0,curve.a1,tolerance,trimmedSamples,false);
                }
                return trimmedSamples;
            }
        }
        if((e.kind==SketchEnt::Project||e.kind==SketchEnt::Offset)&&(e.dof.empty()||e.segments.empty()))
            throw std::runtime_error(e.kind==SketchEnt::Project?"Project source is not evaluated yet":"Offset source is not resolved");
        if(e.segments.empty()||e.segments.size()>64)throw std::runtime_error("Wire needs 1..64 segments");
        Vec2 cursor=at(0);out.push_back(cursor);int dof=2;
        for(const auto& segment:e.segments){
            if(segment.kind==SketchEnt::WireSeg::Line){
                Vec2 end=at(dof);if((end-cursor).len()<1e-8)throw std::runtime_error("Zero-length sketch line");
                out.push_back(end);cursor=end;dof+=2;
            }else if(segment.kind==SketchEnt::WireSeg::Arc){
                Vec2 end=at(dof);double radius=e.dof.at((size_t)dof+2);Vec2 center;double a0=0,a1=0;
                if(!sketchMinorArc(cursor,end,radius,segment.sweep,center,a0,a1))throw std::runtime_error("Wire arc chord does not fit the radius");
                appendArc(center,radius,a0,a1,tolerance,out,false);cursor=end;dof+=3;
            }else if(segment.kind==SketchEnt::WireSeg::Cubic){
                cubicPoints(cursor,at(dof),at(dof+2),at(dof+4),tolerance,out);cursor=at(dof+4);dof+=6;
            }else throw std::runtime_error("Wire segment kind must be line, arc or cubic");
        }
        if((size_t)dof!=e.dof.size())throw std::runtime_error("Wire degrees of freedom do not match segments");
        return out;
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

std::vector<std::vector<Vec2>> sampleSketchEntityPaths(const SketchEnt& entity,double tolerance,const Sketch* owner){
    if(entity.kind==SketchEnt::Recipe&&entity.entityRecipe.find("type")&&entity.entityRecipe.find("type")->str=="text"){
        std::vector<std::vector<Vec2>> paths;for(size_t i=1;i<entity.recipeSamples.size();i+=2)paths.push_back({entity.recipeSamples[i-1],entity.recipeSamples[i]});return paths;
    }
    return {sampleSketchEntity(entity,tolerance,owner)};
}

std::vector<SketchProfile> sketchProfiles(const Sketch& sketch,const Mat4& frame){
    std::vector<std::vector<Vec2>> curves,loops;size_t samples=0;
    for(auto& e:sketch.ents)if(!e.construction&&e.kind!=SketchEnt::Pnt){auto p=sampleSketchEntity(e,sketch.tolerance,&sketch);samples+=p.size();if(samples>4096)throw std::runtime_error("Sketch exceeds 4096 profile samples");curves.push_back(std::move(p));}
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

Mesh sketchRegionMesh(const Sketch& sketch,const Mat4& frame){
    std::vector<size_t> entities;std::vector<std::vector<Vec2>> samples,loops;size_t count=0;
    for(size_t i=0;i<sketch.ents.size();++i){const auto& e=sketch.ents[i];if(e.construction||e.kind==SketchEnt::Pnt)continue;
        try{auto points=sampleSketchEntity(e,sketch.tolerance,&sketch);count+=points.size();if(count>4096)return {};if(points.size()>1){entities.push_back(i);samples.push_back(std::move(points));}}catch(const std::exception&){}
    }
    std::vector<bool> used(samples.size());
    for(size_t seed=0;seed<samples.size();++seed){if(used[seed])continue;std::vector<size_t> component{seed};used[seed]=true;
        for(size_t at=0;at<component.size();++at)for(size_t i=0;i<samples.size();++i)if(!used[i]){
            const auto& a=samples[component[at]];const auto& b=samples[i];bool joins=false;
            for(Vec2 p:{a.front(),a.back()})for(Vec2 q:{b.front(),b.back()})joins|=(p-q).len()<=1e-6;
            if(joins){used[i]=true;component.push_back(i);}
        }
        Sketch subset=sketch;subset.ents.clear();subset.cons.clear();for(size_t i:component)subset.ents.push_back(sketch.ents[entities[i]]);
        try{for(const auto& profile:sketchProfiles(subset,frame)){std::vector<Vec2> loop;for(const auto& edge:profile.segs)loop.push_back(edge.p0);loops.push_back(std::move(loop));}}catch(const std::exception&){}
    }
    // Touching/crossing boundaries have no unambiguous region presentation.
    std::vector<bool> invalid(loops.size());
    for(size_t a=0;a<loops.size();++a)for(size_t b=a+1;b<loops.size();++b)
        for(size_t i=0;i<loops[a].size();++i)for(size_t j=0;j<loops[b].size();++j)
            if(intersects(loops[a][i],loops[a][(i+1)%loops[a].size()],loops[b][j],loops[b][(j+1)%loops[b].size()]))invalid[a]=invalid[b]=true;
    std::vector<int> parent(loops.size(),-1),depth(loops.size());
    auto area=[](const std::vector<Vec2>& loop){double value=0;for(size_t i=0;i<loop.size();++i)value+=loop[i].cross(loop[(i+1)%loop.size()]);return std::abs(value);};
    for(size_t i=0;i<loops.size();++i)if(!invalid[i]){double smallest=1e300;for(size_t j=0;j<loops.size();++j)if(i!=j&&!invalid[j]&&inside(loops[i].front(),loops[j])&&area(loops[j])<smallest){smallest=area(loops[j]);parent[i]=(int)j;}}
    for(size_t i=0;i<loops.size();++i)for(int p=parent[i];p>=0;p=parent[(size_t)p])++depth[i];
    Mesh mesh;
    for(size_t i=0;i<loops.size();++i)if(!invalid[i]&&depth[i]%2==0){std::vector<std::vector<std::array<double,2>>> polygon;
        auto ring=[&](size_t at){std::vector<std::array<double,2>> values;for(Vec2 p:loops[at])values.push_back({p.x,p.y});polygon.push_back(std::move(values));};
        ring(i);for(size_t j=0;j<loops.size();++j)if(!invalid[j]&&parent[j]==(int)i)ring(j);
        const u32 base=(u32)mesh.vertCount();for(const auto& points:polygon)for(const auto& p:points){Vec3 world=frame.transformPoint({p[0],p[1],0});mesh.pos.insert(mesh.pos.end(),{(float)world.x,(float)world.y,(float)world.z});}
        for(u32 index:mapbox::earcut<u32>(polygon))mesh.idx.push_back(base+index);
    }
    return mesh;
}

arkjson::Value inspectSketch(const Document& doc,const NamedSketch& ns,bool samples){
    using V=arkjson::Value;V r=V::makeObj(),entities=V::makeArr();auto frame=resolvePlane(doc,ns.sk.plane);
    r.set("name",V::makeStr(ns.name));r.set("plane",V::makeStr(ns.sk.plane));r.set("tolerance_mm",V::makeNum(ns.sk.tolerance));
    r.set("residual",V::makeNum(ns.residual));r.set("remaining_dofs",V::makeNum(ns.freeDofs));r.set("jacobian_rank",V::makeNum(ns.rank));
    r.set("constraint_status",V::makeStr(ns.error.empty()?(ns.freeDofs?"underconstrained":"locally_fully_constrained"):"failed"));r.set("error",V::makeStr(ns.error));
    r.set("diagnosis",diagnoseSketch(ns.sk));
    for(auto& e:ns.sk.ents){V item=V::makeObj(),dof=V::makeArr(),world=V::makeArr(),local=V::makeArr();
        item.set("name",V::makeStr(e.name));item.set("kind",V::makeStr(sketchEntityKindName(e.kind)));
        item.set("construction",V::makeBool(e.construction));for(auto x:e.dof)dof.arr.push_back(V::makeNum(x));item.set("resolved_dof",dof);
        if(e.arcRecipe.t==V::Obj){item.set("recipe",e.arcRecipe);item.set("geometry_authority",V::makeStr("analytic_arc"));item.set("driven",V::makeBool(true));item.set("editable",V::makeBool(true));}
        if(e.kind==SketchEnt::Recipe){item.set("recipe",e.entityRecipe);item.set("recipe_report",e.recipeReport);item.set("geometry_authority",e.recipeReport.find("geometry_authority")?*e.recipeReport.find("geometry_authority"):V::makeStr("unresolved"));item.set("driven",V::makeBool(true));item.set("editable",V::makeBool(true));}
        if(e.kind==SketchEnt::Project){item.set("geometry_authority",V::makeStr(e.projectionBrep.empty()?"unresolved":"exact_brep"));item.set("associative",V::makeBool(true));item.set("editable",V::makeBool(false));item.set("feature",V::makeNum(e.sourceFeature));item.set("reference",V::makeStr(e.reference));}
        auto points=sampleSketchEntity(e,ns.sk.tolerance,&ns.sk);item.set("sample_count",V::makeNum(points.size()));
        if(samples){for(auto p:points){local.arr.push_back(vectorValue({p.x,p.y,0}));world.arr.push_back(vectorValue(frame.transformPoint({p.x,p.y,0})));}item.set("local",local);item.set("world",world);}entities.arr.push_back(item);
    }r.set("entities",entities);
    try{auto profiles=sketchProfiles(ns.sk,frame);r.set("profile_ready",V::makeBool(true));r.set("loops",V::makeNum(profiles.size()));}
    catch(const std::exception& e){r.set("profile_ready",V::makeBool(false));r.set("profile_error",V::makeStr(e.what()));}
    r.set("solid_certified",V::makeBool(false));return r;
}

arkjson::Value measureSketchEntity(const Document&,const NamedSketch& sketch,const std::string& entity){
    using V=arkjson::Value;
    const SketchEnt* found=nullptr;
    for(const auto& candidate:sketch.sk.ents) if(candidate.name==entity){
        if(found) throw std::runtime_error("Ambiguous sketch entity");
        found=&candidate;
    }
    if(!found) throw std::runtime_error("Sketch entity not found");
    const auto samples=sampleSketchEntity(*found,sketch.sk.tolerance,&sketch.sk);
    double xmin=1e300,ymin=1e300,xmax=-1e300,ymax=-1e300,length=0;
    for(const auto& p:samples){xmin=std::min(xmin,p.x);ymin=std::min(ymin,p.y);xmax=std::max(xmax,p.x);ymax=std::max(ymax,p.y);}
    for(size_t i=1;i<samples.size();++i) length+=(samples[i]-samples[i-1]).len();
    Vec2 startTangent{},endTangent{};
    if(found->kind==SketchEnt::Line){
        Vec2 a{found->dof.at(0),found->dof.at(1)},b{found->dof.at(2),found->dof.at(3)};
        length=(b-a).len();startTangent=endTangent=(b-a).normalized();
        xmin=std::min(a.x,b.x);xmax=std::max(a.x,b.x);ymin=std::min(a.y,b.y);ymax=std::max(a.y,b.y);
    }else if(found->kind==SketchEnt::Circle){
        double r=found->dof.at(2);Vec2 c{found->dof[0],found->dof[1]};
        length=K_2PI*r;startTangent=endTangent={0,1};
        xmin=c.x-r;xmax=c.x+r;ymin=c.y-r;ymax=c.y+r;
    }else if(found->kind==SketchEnt::Arc){
        double r=found->dof.at(2),sweep=deg2rad(found->dof[4]-found->dof[3]),sign=sweep>=0?1.:-1.;
        length=std::abs(sweep)*r;
        auto tangent=[&](double degrees){double t=deg2rad(degrees);return Vec2{-std::sin(t)*sign,std::cos(t)*sign};};
        startTangent=tangent(found->dof[3]);endTangent=tangent(found->dof[4]);
    }else if(found->kind==SketchEnt::Pnt){
        length=0;xmin=xmax=found->dof.at(0);ymin=ymax=found->dof.at(1);
    }else if(samples.size()>=2){
        startTangent=(samples[1]-samples[0]).normalized();
        endTangent=(samples.back()-samples[samples.size()-2]).normalized();
    }
    auto pair=[&](Vec2 p){V a=V::makeArr();a.arr.push_back(V::makeNum(p.x));a.arr.push_back(V::makeNum(p.y));return a;};
    V result=V::makeObj(),box=V::makeArr();
    result.set("name",V::makeStr(entity));result.set("kind",V::makeStr(sketchEntityKindName(found->kind)));
    if(found->arcRecipe.t==V::Obj)result.set("recipe",found->arcRecipe);
    result.set("length_mm",V::makeNum(length));
    if(found->kind==SketchEnt::Project&&!found->projectionBrep.empty()){result.set("length_mm",V::makeNum(found->projectionLength));result.set("length_authority",V::makeStr("exact_brep"));result.set("bbox_authority",V::makeStr("display_samples"));}
    for(double value:{xmin,ymin,xmax,ymax}) box.arr.push_back(V::makeNum(value));
    result.set("bbox_mm",box);result.set("start_tangent",pair(startTangent));result.set("end_tangent",pair(endTangent));
    return result;
}
}
