#include "ark/sketch.hpp"
#include "ark/references.hpp"
#include <algorithm>
#include <cmath>
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
        assignPolyline(entity,offsetPolyline(sampleSketchEntity(*source,sketch.tolerance),distance));
    }
}
Vec3 referenceOrigin(Document& document,u64 feature,const std::string& name,double fraction){
    return resolveGeometryReference(document,feature,name,fraction).frame.transformPoint({});
}
Vec2 onSketch(const Mat4& inverse,Vec3 world){Vec3 local=inverse.transformPoint(world);return {local.x,local.y};}
const Body* evaluatedBody(const Document& document,u64 feature){
    const auto& bodies=document.evaluatingBodies?*document.evaluatingBodies:document.evalBodies;
    for(const auto& body:bodies)if(body.id==feature)return &body;
    return nullptr;
}
const arkjson::Value* topologyRow(const Body& body,const std::string& name){
    const auto* topology=body.nativeData.find("topology");if(!topology)return nullptr;
    const auto* rows=topology->find("references");if(!rows||rows->t!=arkjson::Value::Arr)return nullptr;
    for(const auto& row:rows->arr)if(const auto* label=row.find("name");label&&label->t==arkjson::Value::Str&&label->str==name)return &row;
    return nullptr;
}
void projectEdge(Document& document,NamedSketch& sketch,SketchEnt& entity,const Mat4& inverse){
    std::vector<Vec3> world;
    for(double fraction:{0.,.25,.5,.75,1.})world.push_back(referenceOrigin(document,entity.sourceFeature,entity.reference,fraction));
    Vec3 chord=world.back()-world.front();double length2=chord.lensq();bool straight=true;
    for(size_t i=1;i+1<world.size();++i){double t=length2>0?clamp((world[i]-world.front()).dot(chord)/length2,0.,1.):0.;if((world[i]-(world.front()+chord*t)).len()>0.05)straight=false;}
    std::vector<Vec2> flat;if(straight)flat={onSketch(inverse,world.front()),onSketch(inverse,world.back())};
    else for(const auto& point:world)flat.push_back(onSketch(inverse,point));
    assignPolyline(entity,flat);
}
void projectFace(Document& document,NamedSketch& sketch,SketchEnt& entity,const Mat4& plane,const Mat4& inverse){
    GeometryReference face=resolveGeometryReference(document,entity.sourceFeature,entity.reference,.5);
    if(face.kind!="face")throw std::runtime_error("Project an edge or a planar face");
    const Body* body=evaluatedBody(document,entity.sourceFeature);if(!body)throw std::runtime_error("Project source is not evaluated yet");
    const arkjson::Value* row=topologyRow(*body,entity.reference);
    if(!row||!row->find("descriptor")||!row->find("descriptor")->find("normal"))throw std::runtime_error("Project a planar face or an edge");
    Vec3 origin=face.frame.transformPoint({}),normal=face.frame.transformDir({0,0,1});
    Vec3 sketchOrigin=plane.transformPoint({}),sketchNormal=plane.transformDir({0,0,1});
    const bool parallel=std::abs(normal.dot(sketchNormal))>0.99;
    struct Segment{Vec3 a,b;};
    std::vector<Segment> boundary;
    const auto* topology=body->nativeData.find("topology");const auto* rows=topology?topology->find("references"):nullptr;
    if(!rows||rows->t!=arkjson::Value::Arr)throw std::runtime_error("Project source has no exact boundary");
    for(const auto& item:rows->arr){
        const auto* kind=item.find("kind");const auto* status=item.find("status");const auto* name=item.find("name");
        if(!kind||kind->str!="edge"||!status||(status->str!="bound"&&status->str!="merged")||!name)continue;
        if(boundary.size()>=64)break;
        try{
            Vec3 a=referenceOrigin(document,entity.sourceFeature,name->str,0),b=referenceOrigin(document,entity.sourceFeature,name->str,1);
            if(std::abs((a-origin).dot(normal))<=0.05&&std::abs((b-origin).dot(normal))<=0.05)boundary.push_back({a,b});
        }catch(const std::exception&){}
    }
    if(boundary.empty())throw std::runtime_error("Projected face has no boundary");
    if(parallel){
        struct Flat{Vec2 a,b;bool used=false;};
        std::vector<Flat> edges;for(const auto& segment:boundary){Vec2 a=onSketch(inverse,segment.a),b=onSketch(inverse,segment.b);if((a-b).len()>=1e-6)edges.push_back({a,b,false});}
        if(edges.empty())throw std::runtime_error("Projected face is degenerate");
        std::vector<Vec2> chain{edges[0].a,edges[0].b};edges[0].used=true;Vec2 cursor=edges[0].b;
        for(size_t step=1;step<edges.size();++step){
            int found=-1;Vec2 next{};
            for(size_t i=0;i<edges.size();++i)if(!edges[i].used){
                if((cursor-edges[i].a).len()<=0.05){found=(int)i;next=edges[i].b;break;}
                if((cursor-edges[i].b).len()<=0.05){found=(int)i;next=edges[i].a;break;}
            }
            if(found<0)break;
            edges[(size_t)found].used=true;chain.push_back(next);cursor=next;
        }
        for(const auto& edge:edges)if(!edge.used)throw std::runtime_error("Projected face has more than one wire");
        if((chain.front()-chain.back()).len()>0.05)throw std::runtime_error("Projected face boundary did not close");
        if((chain.front()-chain.back()).len()>1e-8)chain.push_back(chain.front());
        assignPolyline(entity,chain);return;
    }
    std::vector<Vec3> hits;
    auto side=[&](Vec3 point){return (point-sketchOrigin).dot(sketchNormal);};
    auto add=[&](Vec3 point){for(const auto& old:hits)if((old-point).len()<=0.05)return;hits.push_back(point);};
    for(const auto& segment:boundary){
        double a=side(segment.a),b=side(segment.b);
        if(std::abs(a)<=0.05&&std::abs(b)<=0.05){add(segment.a);add(segment.b);continue;}
        if(a*b<0||std::abs(a)<=0.05||std::abs(b)<=0.05){if(std::abs(a-b)<1e-12)continue;double t=std::clamp(a/(a-b),0.,1.);add(segment.a+(segment.b-segment.a)*t);}
    }
    if(hits.size()!=2)throw std::runtime_error(hits.size()<2?"Face does not meet the sketch plane":"Projected face intersection is not one segment");
    assignPolyline(entity,{onSketch(inverse,hits[0]),onSketch(inverse,hits[1])});
}
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
        bakeSketchOffsets(ns.sk,env);
        SketchDiagnostics diagnostics;ns.residual=solveSketch(ns.sk,nullptr,200,&diagnostics);
        bakeSketchOffsets(ns.sk,env);
        ns.rank=diagnostics.rank;ns.freeDofs=diagnostics.variables-diagnostics.rank;
        if(!std::isfinite(ns.residual)||ns.residual>1e-6)throw std::runtime_error("Conflicting constraints or solver failed to converge");
        for(auto& e:ns.sk.ents){
            for(auto x:e.dof)if(!std::isfinite(x)||std::abs(x)>1e7)throw std::runtime_error("Sketch coordinate outside finite bounds");
            if(drivenKind(e.kind)&&e.dof.empty())continue;
            sampleSketchEntity(e,ns.sk.tolerance); // validate even unconsumed sketches
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
        const Feature* feature=document.find(entity.sourceFeature);
        if(!feature){status.error="Project source was not found";return status;}
        if(feature->suppressed){status.error="Project source is suppressed";return status;}
        if(!evaluatedBody(document,feature->id)){
            bool attempted=false;for(const auto& result:document.lastResults)if(result.id==feature->id)attempted=true;
            if(attempted){status.error="Project source is not evaluated yet";return status;}
            status.waiting=true;continue;
        }
        try{
            const Body* body=evaluatedBody(document,feature->id);
            const arkjson::Value* row=body?topologyRow(*body,entity.reference):nullptr;
            const auto* kind=row?row->find("kind"):nullptr;
            if(kind&&kind->str=="face")projectFace(document,sketch,entity,plane,inverse);
            else if(kind&&kind->str=="edge")projectEdge(document,sketch,entity,inverse);
            else{
                GeometryReference reference=resolveGeometryReference(document,feature->id,entity.reference,.5);
                if(reference.kind=="face")projectFace(document,sketch,entity,plane,inverse);
                else if(reference.kind=="edge")projectEdge(document,sketch,entity,inverse);
                else throw std::runtime_error("Project an edge or a planar face");
            }
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
    }
    return "unknown";
}

int sketchVertexCount(const SketchEnt& entity){
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

std::vector<Vec2> sampleSketchEntity(const SketchEnt& e,double tolerance){
    if(!std::isfinite(tolerance)||tolerance<.0001||tolerance>5)throw std::runtime_error("Sketch tolerance must be 0.0001..5 mm");
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
        item.set("name",V::makeStr(e.name));item.set("kind",V::makeStr(sketchEntityKindName(e.kind)));
        item.set("construction",V::makeBool(e.construction));for(auto x:e.dof)dof.arr.push_back(V::makeNum(x));item.set("resolved_dof",dof);
        auto points=sampleSketchEntity(e,ns.sk.tolerance);item.set("sample_count",V::makeNum(points.size()));
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
    const auto samples=sampleSketchEntity(*found,sketch.sk.tolerance);
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
    result.set("length_mm",V::makeNum(length));
    for(double value:{xmin,ymin,xmax,ymax}) box.arr.push_back(V::makeNum(value));
    result.set("bbox_mm",box);result.set("start_tangent",pair(startTangent));result.set("end_tangent",pair(endTangent));
    return result;
}
}
