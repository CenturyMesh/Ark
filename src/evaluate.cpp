// ARK-Point :: evaluate.cpp â€” feature-history recompute pipeline.
#include "ark/model.hpp"
#include "ark/history_authoring.hpp"
#include "ark/assembly_authoring.hpp"
#include "ark/serialize.hpp"
#include <cctype>
#include "ark/mesh_model.hpp"
#include "ark/source_tube.hpp"
#include "ark/sketch.hpp"
#include "ark/datum_authoring.hpp"
#include "ark/prism_cut.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/native_features.hpp"
#include "ark/suspension.hpp"
#include "ark/native_backend.hpp"
#include "ark/skeleton.hpp"
#include <chrono>
#include <exception>
#include <future>
#include <set>

namespace ark {
namespace {thread_local EvaluationObserver evaluationObserver;}
EvaluationObserver setEvaluationObserver(EvaluationObserver observer){auto previous=std::move(evaluationObserver);evaluationObserver=std::move(observer);return previous;}
static void evaluationProgress(u64 id,const std::string& name,size_t index,size_t total,bool completed,bool reused){if(evaluationObserver)try{evaluationObserver(id,name,index,total,completed,reused);}catch(...){/* An observer cannot alter geometric acceptance. */}}

static f64 nowMs() {
    using namespace std::chrono;
    return duration<f64,std::milli>(steady_clock::now().time_since_epoch()).count();
}
// Only audited operations with explicit feature inputs may cross the ordered
// publication boundary. Sketch/datum/instance consumers retain a serial barrier.
static bool explicitDependencies(const Feature& f){
    if(f.suppressed)return false;
    if(f.type==FeatureType::Box||f.type==FeatureType::Sphere||f.type==FeatureType::Cylinder||f.type==FeatureType::Cone||f.type==FeatureType::Torus||f.type==FeatureType::MeshBody)return f.inputs.empty();
    if(f.p.count("sketch")||f.p.count("plane"))return false;
    std::function<bool(const arkjson::Value&)> local=[](const arkjson::Value&){return true;};
    local=[&](const arkjson::Value& value){for(const auto& pair:value.obj){if(pair.first=="sketch"||pair.first=="source_sketch"||pair.first=="instances"||pair.first=="node"||pair.first=="source_instance"||pair.first=="alternate_node")return false;if(pair.first=="plane"&&(pair.second.t!=arkjson::Value::Str||(pair.second.str!="XY"&&pair.second.str!="XZ"&&pair.second.str!="YZ")))return false;if(!local(pair.second))return false;}for(const auto& item:value.arr)if(!local(item))return false;return true;};
    if(!local(f.nativeSpec))return false;
    // Mesh evaluation consumes only the authored payload, expression environment,
    // tessellation options and explicit input bodies; it has no Document access.
    if(isMeshModelFeature(f.type))return f.type==FeatureType::MeshNet?f.inputs.empty():f.inputs.size()==1;
    auto inlineProfile=[&](const char* key){const auto* value=f.nativeSpec.find(key);return value&&value->t==arkjson::Value::Obj;};
    if(f.type==FeatureType::BSplineSurface)return f.inputs.empty()&&f.nativeSpec.find("poles");
    if(f.type==FeatureType::SurfaceFill||f.type==FeatureType::ExtrudeSurface||f.type==FeatureType::RevolveSurface)return inlineProfile("profile");
    if(f.type==FeatureType::Thicken||f.type==FeatureType::OffsetSurface)return f.inputs.size()==1;
    if(f.type==FeatureType::Sew)return f.inputs.size()>=2&&f.inputs.size()<=16;
    if(f.type==FeatureType::Sweep){
        if(!inlineProfile("path"))return false;
        if(const auto* sections=f.nativeSpec.find("sections")){
            if(sections->t!=arkjson::Value::Arr)return false;
            for(const auto& station:sections->arr){const auto* section=station.find("section");if(!section||section->t!=arkjson::Value::Obj)return false;}
        }else if(!inlineProfile("section"))return false;
        if(const auto* orientation=f.nativeSpec.find("orientation"))if(const auto* guide=orientation->find("guide");guide&&guide->t!=arkjson::Value::Obj)return false;
        return true;
    }
    if(f.type==FeatureType::SurfaceEdit){
        const auto* operation=f.nativeSpec.find("op");if(!operation||operation->t!=arkjson::Value::Str)return false;
        static const std::set<std::string> edits{"extract_face","trim_plane","trim_wire","repair","untrim","extend","mutual_trim"};
        return edits.count(operation->str)!=0;
    }
    if(f.type!=FeatureType::NativeSolid)return false;
    std::string op=f.get("op");if(op.empty())if(auto value=f.nativeSpec.find("op"))op=value->str;
    static const std::set<std::string> pure{"box","sphere","cylinder","cone","torus","union","cut","common","hole","fillet","chamfer","transform","linear_pattern","circular_pattern","mirror","shell","draft","split_plane","curve_pattern","hole_pattern","vertex_pull","rib","gusset","mount_pad","boss","screw_support","nut_seat","lid_lip","lid_groove","split_surface","split_tool"};return pure.count(op)!=0;
}
static unsigned threadSetting(const char* name,unsigned fallback,unsigned maximum){if(const char* value=std::getenv(name)){char* end=nullptr;long requested=std::strtol(value,&end,10);if(!end||*end||requested<1||(unsigned long)requested>maximum)throw std::runtime_error(std::string(name)+" has an invalid thread budget");return (unsigned)requested;}return fallback;}
struct NativeBudgetScope {int previous;explicit NativeBudgetScope(unsigned budget):previous(setNativeThreadBudget((int)budget)){}~NativeBudgetScope(){setNativeThreadBudget(previous);}};
struct OuterParallelScope {bool previous;explicit OuterParallelScope(bool value):previous(setOuterParallelRegion(value)){}~OuterParallelScope(){setOuterParallelRegion(previous);}};
static size_t valueMemory(const arkjson::Value& value){size_t bytes=sizeof(value)+value.str.size()+64;for(const auto& child:value.arr)bytes+=valueMemory(child);for(const auto& pair:value.obj)bytes+=sizeof(pair.first)+pair.first.size()+valueMemory(pair.second);return bytes;}
// Actual retained allocation. Payloads are shared immutable storage, so a job
// that reads an input does not duplicate it; the bound is not a worst case
// for a JSON mesh expansion that scheduled evaluation never performs.
static size_t valueMemory(const NativePayload& value){return value.allocatedBytes();}

static std::string featureParameterError(const Feature& f,const ExprEnv& env){
    std::string error;
        for(auto& kv:f.p){
            if(kv.first=="shape"||kv.first=="section"||kv.first=="sketch"||kv.first=="plane"||kv.first=="name"||kv.first=="op"||
               (f.type==FeatureType::ParametricSurface&&(kv.first=="x"||kv.first=="y"||kv.first=="z")))continue;
            bool ok=false; f64 value=evalExpr(kv.second,env,&ok);
            if(!ok || std::fabs(value)>1e8)error="Invalid expression for '"+kv.first+"': "+kv.second;
        }
        auto positive=[&](std::initializer_list<const char*> keys){for(auto key:keys)if(f.p.count(key)&&f.num(env,key)<=0)error=std::string(key)+" must be positive";};
        switch(f.type){
            case FeatureType::Box:positive({"dx","dy","dz"});break;
            case FeatureType::Cylinder:positive({"r","h"});break;
            case FeatureType::Sphere:positive({"r"});break;
            case FeatureType::Cone:positive({"rb","h"});if(f.num(env,"rt")<0)error="rt cannot be negative";break;
            case FeatureType::Torus:positive({"R","r"});if(f.num(env,"r",2)>=f.num(env,"R",8))error="torus requires R > r";break;
            case FeatureType::SketchCut:positive({"depth"});break;
            case FeatureType::Extrude:positive({"w","d","r","height"});break;
            case FeatureType::Revolve:positive({"w","d","r","angle"});break;
            case FeatureType::LinearPattern:if(f.num(env,"count",3)<1||f.num(env,"count",3)>256||std::floor(f.num(env,"count",3))!=f.num(env,"count",3))error="count must be an integer in 1..256";break;
            case FeatureType::ParametricSurface:for(auto key:{"nu","nv"})if(f.p.count(key)&&std::floor(f.num(env,key))!=f.num(env,key))error="surface subdivisions must be integers";break;
            default:break;
        }
    return error;
}
static bool makeProfile(const Feature& f, const ExprEnv& env, Document& doc,
                        SketchProfile& prof, std::string& err, std::vector<SketchProfile>* holes=nullptr) {
    std::string sname=f.get("sketch");
    if(!sname.empty()){
        NamedSketch* ns=doc.findSketch(sname);
        if(!ns){ err="sketch '"+sname+"' not found"; return false; }
        if(!ns->error.empty()){err=ns->error;return false;}
        try{
            const Mat4 frame=resolvePlane(doc,ns->sk.plane)*Mat4::translation({f.num(env,"ox"),f.num(env,"oy"),f.num(env,"oz")});
            if(holes&&sketchProfileComponents(ns->sk)>1){
                // 046B: one region (outer loop minus its direct holes) per mesh body.
                auto regions=sketchProfileRegions(ns->sk,frame);
                if(regions.size()!=1){std::string outers;for(const auto& region:regions)outers+=(outers.empty()?"":"; ")+region.outerName;err="Profile has "+std::to_string(regions.size())+" separate regions (outer loops: "+outers+"); a mesh extrude publishes one body. Extrude each region separately, or use native_solid extrude with native.regions to select one";return false;}
                prof=std::move(regions.front().outer);*holes=std::move(regions.front().holes);return true;
            }
            auto profiles=sketchProfiles(ns->sk,frame);
            if(profiles.size()!=1){err=holes?"Extrude requires one outer loop; use sketch_cut for multiple openings":"Mesh revolve requires one closed loop; use revolve_surface with p.solid=1 for profile regions with holes";return false;}
            prof=std::move(profiles.front());return true;
        }catch(const std::exception& e){err=e.what();if(!holes&&err.find("Nested sketch loops")!=std::string::npos)err="Mesh revolve requires one closed loop; use revolve_surface with p.solid=1 for profile regions with holes";return false;}
    }

    prof.frame = resolvePlane(doc,f.get("plane","XY"))*Mat4::translation({f.num(env,"ox"),f.num(env,"oy"),f.num(env,"oz")});
    std::string shape=f.get("shape","rect");
    f64 cx=f.num(env,"cx",0), cy=f.num(env,"cy",0);
    if(shape=="rect"){
        f64 w=tmax(f.num(env,"w",10),1e-9), d=tmax(f.num(env,"d",10),1e-9);
        Vec2 a{cx-w/2,cy-d/2}, b{cx+w/2,cy+d/2};
        prof.segs={
            {ProfileSeg::Line,a,{b.x,a.y}},
            {ProfileSeg::Line,{b.x,a.y},b},
            {ProfileSeg::Line,b,{a.x,b.y}},
            {ProfileSeg::Line,{a.x,b.y},a},
        };
    } else if(shape=="polygon"){
        int n=(int)clamp(f.num(env,"n",6),3.0,64.0);
        f64 r=tmax(f.num(env,"r",5),1e-9);
        std::vector<Vec2> pts;
        for(int i=0;i<n;++i){
            f64 t=K_2PI*(f64)i/n;
            pts.push_back({cx+r*std::cos(t), cy+r*std::sin(t)});
        }
        for(int i=0;i<n;++i)
            prof.segs.push_back({ProfileSeg::Line,pts[i],pts[(i+1)%n]});
    } else { // circle: 4 arcs
        f64 r=tmax(f.num(env,"r",5),1e-9);
        Vec2 c{cx,cy};
        for(int q=0;q<4;++q)
            prof.segs.push_back({ProfileSeg::Arc,{},{},c,r,
                                 (f64)q*K_HALF_PI,(f64)(q+1)*K_HALF_PI});
    }
    err.clear();
    return true;
}

static Body evalFeatureBody(const Feature& f, const ExprEnv& env, Document& doc,
                            std::vector<Body>& work, FeatureResult& res,
                            const TessOptions& topt) {
    auto findInput=[&](u64 id)->Body*{
        for(auto&w:work) if(w.id==id) return &w;
        return nullptr;
    };

    if(f.type==FeatureType::Suspension){
        if(!f.inputs.empty())throw std::runtime_error("Suspension uses authored hardpoints, not feature inputs");
        Body body=evaluateSuspensionFeature(f,env);res.tris=body.baked?body.baked->triCount():0;return body;
    }
    if(isNativeFeature(f.type)){
        Body body=evaluateNativeFeature(f,env,doc,work);
        res.tris=body.baked?body.baked->triCount():0;
        return body;
    }
    bool exactInput=false;
    for(u64 input:f.inputs)for(const Body& source:work)
        if(source.id==input&&source.nativeData.find("brep_base64"))exactInput=true;
    if(exactInput && (f.type==FeatureType::Transform||f.type==FeatureType::Boolean||f.type==FeatureType::LinearPattern)){
        Body body=evaluateExactLegacyOperation(f,env,doc,work);
        res.tris=body.baked?body.baked->triCount():0;
        return body;
    }
    if(exactInput && f.type==FeatureType::SketchCut){
        res.ok=false;res.error="Exact native B-rep needs an exact solid operation";return {};
    }
    switch(f.type){
        case FeatureType::SourceTube: {
            try{auto preview=buildSourceTubePreview(f.sourceData);Body body;body.baked=std::make_unique<Mesh>(std::move(preview.straight));res.tris=body.baked->triCount();return body;}
            catch(const std::exception& e){res.ok=false;res.error=e.what();return {};}
        }
        case FeatureType::NotchedTube: {
            try{auto preview=buildNotchPreview(notchRequestForFeature(f,env));Body body;body.baked=std::make_unique<Mesh>(std::move(preview.straight));res.tris=body.baked->triCount();return body;}
            catch(const std::exception& e){res.ok=false;res.error=e.what();return {};}
        }
        case FeatureType::Tube: {
            auto tube=buildTube(f,env);if(!tube.error.empty()){res.ok=false;res.error=tube.error;return {};}
            Body body;body.baked=std::make_unique<Mesh>(std::move(tube.mesh));res.tris=body.baked->triCount();return body;
        }
        case FeatureType::ParametricSurface: {
            int nu=(int)f.num(env,"nu",96), nv=(int)f.num(env,"nv",48);
            if(nu<3||nv<3||nu>256||nv>256){res.ok=false;res.error="surface resolution must be 3..256";return {};}
            bool wrapU=f.num(env,"wrap_u",0)!=0,wrapV=f.num(env,"wrap_v",0)!=0;
            f64 u0=f.num(env,"u0",0),u1=f.num(env,"u1",K_2PI);
            f64 v0=f.num(env,"v0",0),v1=f.num(env,"v1",K_2PI);
            if(u1<=u0||v1<=v0){res.ok=false;res.error="surface domains must increase";return {};}
            Body out;Mesh m;
            int cols=nu+(wrapU?0:1),rows=nv+(wrapV?0:1);
            m.pos.reserve((size_t)cols*rows*3);m.idx.reserve((size_t)nu*nv*6);
            ExprEnv local=env;
            auto point=[&](f64 u,f64 v,Vec3& p){
                local.vars["u"]=u;local.vars["v"]=v;bool ok=true,k=false;
                p.x=evalExpr(f.get("x"),local,&k);ok &= k;
                p.y=evalExpr(f.get("y"),local,&k);ok &= k;
                p.z=evalExpr(f.get("z"),local,&k);ok &= k;
                return ok && std::fabs(p.x)<1e8 && std::fabs(p.y)<1e8 && std::fabs(p.z)<1e8;
            };
            for(int j=0;j<rows;++j)for(int i=0;i<cols;++i){
                Vec3 p;f64 u=lerp(u0,u1,(f64)i/nu),v=lerp(v0,v1,(f64)j/nv);
                if(!point(u,v,p)){res.ok=false;res.error="invalid/non-finite surface expression";return {};}
                // Periodic indexing must not silently bridge non-periodic endpoints.
                Vec3 seam;
                if((wrapU&&i==0&&(!point(u1,v,seam)||!p.approx(seam,1e-6))) ||
                   (wrapV&&j==0&&(!point(u,v1,seam)||!p.approx(seam,1e-6)))){
                    res.ok=false;res.error="surface periodic seam does not match";return {};
                }
                m.addVertex(p);
            }
            for(int j=0;j<nv;++j)for(int i=0;i<nu;++i){
                u32 a=j*cols+i,b=j*cols+(i+1)%cols;
                u32 c=((j+1)%rows)*cols+(i+1)%cols,d=((j+1)%rows)*cols+i;
                m.addTri(a,b,c);m.addTri(a,c,d);
            }
            if(wrapU&&wrapV)meshOrientOutward(m);
            m.computeNormals();res.tris=m.triCount();out.baked=std::make_unique<Mesh>(std::move(m));return out;
        }
        case FeatureType::Box:
            return makeBox({ f.num(env,"dx",10), f.num(env,"dy",10), f.num(env,"dz",10) });
        case FeatureType::Cylinder:
            return makeCylinder(tmax(f.num(env,"r",5),1e-9), tmax(f.num(env,"h",10),1e-9));
        case FeatureType::Sphere:
            return makeSphere(tmax(f.num(env,"r",5),1e-9));
        case FeatureType::Cone:
            return makeCone(tmax(f.num(env,"rb",5),1e-9), tmax(f.num(env,"rt",0.0),0.0),
                            tmax(f.num(env,"h",10),1e-9));
        case FeatureType::Torus:
            return makeTorus(tmax(f.num(env,"R",8),1e-9), tmax(f.num(env,"r",2),1e-9));

        case FeatureType::Extrude: {
            SketchProfile prof; std::string perr; std::vector<SketchProfile> holes;
            if(!makeProfile(f,env,doc,prof,perr,&holes)){ res.ok=false; res.error=perr; return {}; }
            Body body=extrudeProfile(prof,tmax(f.num(env,"height",8),1e-9),f.num(env,"both",0)!=0.0);
            if(!holes.empty()){
                // Holes are full-depth openings of this same prism: reuse the
                // validated prismatic path (area x height volume, closed mesh).
                for(auto& hole:holes)hole.frame=body.profile.frame;Body opened;
                try{if(!tryPrismaticCut(body,holes,body.pd1.x,opened)){res.ok=false;res.error="Extrusion with profile holes requires a rigid sketch frame";return {};}}
                catch(const std::exception& e){res.ok=false;res.error=std::string("Extrusion with profile holes: ")+e.what();return {};}
                res.tris=opened.baked->triCount();return opened;
            }
            auto mesh=body.tessellate(topt);
            auto audit=analyzeMesh(mesh);
            if(!audit.closedOriented()||!(audit.signedVolume>0)){res.ok=false;res.error="Extrusion failed closed mesh/positive volume validation; check profile and mesh precision at this placement";return {};}
            res.tris=mesh.triCount();body.baked=std::make_unique<Mesh>(std::move(mesh));return body;
        }
        case FeatureType::SketchCut: {
            if(f.inputs.size()!=1){res.ok=false;res.error="sketch_cut requires one base feature";return {};}
            auto base=findInput(f.inputs[0]);auto source=doc.find(f.inputs[0]);
            if(!base||!source){res.ok=false;res.error="sketch_cut base missing";return {};}
            if(source->type==FeatureType::SourceTube||source->type==FeatureType::SourceSheet){res.ok=false;res.error="Imported source geometry stays read-only";return {};}
            auto ns=doc.findSketch(f.get("sketch"));if(!ns){res.ok=false;res.error="Cut sketch not found";return {};}
            if(!ns->error.empty()){res.ok=false;res.error=ns->error;return {};}
            auto profiles=sketchProfiles(ns->sk,resolvePlane(doc,ns->sk.plane)*Mat4::translation({0,0,f.num(env,"offset")}));
            Body prismatic;
            if(tryPrismaticCut(*base,profiles,f.num(env,"depth",10),prismatic)){res.tris=prismatic.baked->triCount();return prismatic;}
            Mesh mesh=base->tessellate(topt);
            for(auto& profile:profiles){auto cutter=extrudeProfile(profile,f.num(env,"depth",10));mesh=meshBoolean(mesh,cutter.tessellate(topt),CsgOp::Subtract);}
            if(!mesh.triCount()){res.ok=false;res.error="Cut removed the entire body";return {};}
            auto audit=analyzeMesh(mesh);
            if(!audit.closedOriented()||!(audit.signedVolume>0)){res.ok=false;res.error="General sketch cut failed closed mesh/positive volume validation; use disjoint through-openings in an extrusion or change the operation";return {};}
            meshOrientOutward(mesh);mesh.computeNormals();Body body;body.baked=std::make_unique<Mesh>(std::move(mesh));res.tris=body.baked->triCount();return body;
        }
        case FeatureType::Revolve: {
            SketchProfile prof; std::string perr;
            if(!makeProfile(f,env,doc,prof,perr)){ res.ok=false; res.error=perr; return {}; }
            return revolveProfile(prof, clamp(f.num(env,"angle",360),1e-6,360.0));
        }

        case FeatureType::Transform: {
            Body* src = f.inputs.empty()?nullptr:findInput(f.inputs[0]);
            if(!src){ res.ok=false; res.error="transform: input missing"; return {}; }
            Mat4 M = Mat4::translation({f.num(env,"tx"),f.num(env,"ty"),f.num(env,"tz")})
                   * Mat4::rotZ(deg2rad(f.num(env,"rz")))
                   * Mat4::rotY(deg2rad(f.num(env,"ry")))
                   * Mat4::rotX(deg2rad(f.num(env,"rx")));
            return transformBody(*src,M);
        }

        case FeatureType::Boolean: {
            if(f.inputs.size()<2){ res.ok=false; res.error="boolean: need 2 inputs"; return {}; }
            Body *A=nullptr,*B=nullptr;
            for(auto&w:work){ if(w.id==f.inputs[0])A=&w; else if(w.id==f.inputs[1])B=&w; }
            if(!A||!B){ res.ok=false; res.error="boolean: inputs missing"; return {}; }
            Mesh ma=A->tessellate(topt), mb=B->tessellate(topt);
            CsgOp op = f.opInt==1?CsgOp::Subtract : f.opInt==2?CsgOp::Intersect : CsgOp::Union;
            Body out; out.id=f.id;
            Mesh combined=meshBoolean(ma,mb,op);meshOrientOutward(combined);combined.computeNormals();
            out.baked=std::make_unique<Mesh>(std::move(combined));
            res.tris=out.baked->triCount();
            return out;
        }

        case FeatureType::MeshBody:
        case FeatureType::MeshWeld:
        case FeatureType::MeshSmooth:
        case FeatureType::MeshDecimate:
        case FeatureType::MeshOffset:
        case FeatureType::MeshEdit:
        case FeatureType::MeshNet:
        case FeatureType::MeshFill: {
            Body body = evaluateMeshFeature(f, env, work, topt);
            res.tris = body.baked->triCount();
            return body;
        }
        case FeatureType::LinearPattern: {
            Body* src = f.inputs.empty()?nullptr:findInput(f.inputs[0]);
            if(!src || (!src->baked && src->faces.empty())){
                res.ok=false; res.error="pattern: input missing"; return {}; }
            int n=(int)clamp(f.num(env,"count",3),1.0,256.0);
            Vec3 dir={f.num(env,"dx",20),f.num(env,"dy",0),f.num(env,"dz",0)};
            Body acc; acc.id=f.id;Mesh combined;
            Mesh base=src->tessellate(topt);
            for(int k=0;k<n;++k){
                Mesh part; part.pos=base.pos; part.idx=base.idx;
                Mat4 M=Mat4::translation(dir*(f64)k);
                size_t nv=part.vertCount();
                std::vector<Vec3> in(nv),outv(nv);
                for(size_t i=0;i<nv;++i) in[i]={part.pos[i*3],part.pos[i*3+1],part.pos[i*3+2]};
                transform_points_f32(M,in.data(),outv.data(),nv);
                for(size_t i=0;i<nv;++i){ part.pos[i*3]=(f32)outv[i].x; part.pos[i*3+1]=(f32)outv[i].y; part.pos[i*3+2]=(f32)outv[i].z; }
                combined.append(part);
            }
            combined.weld(1e-7*tmax(src->bbox().diagonal(),1.0));
            meshOrientOutward(combined);combined.computeNormals();acc.baked=std::make_unique<Mesh>(std::move(combined));
            res.tris=acc.baked->triCount();
            return acc;
        }
        // Native features and Suspension return before this portable switch.
        // Preserve the explicit error below for any unsupported future type.
        default:break;
    }
    res.ok=false; res.error="unknown feature type";
    return {};
}

static bool deferredSketch(const Document& document,const NamedSketch& sketch);
static bool deferredDatum(const Document& document,const std::string& name);
static void refreshDrivenSketches(Document& document, bool require){
    ExprEnv env=document.buildEnv();
    for(auto& sketch:document.sketches){
        if(deferredSketch(document,sketch)){sketch.error.clear();sketch.residual=0;continue;}
        bool projects=false;
        for(const auto& entity:sketch.sk.ents) if(entity.kind==SketchEnt::Project) projects=true;
        if(!projects) continue;
        DrivenSketchStatus status=bakeSketchProjects(document,sketch);
        if(!status.error.empty()){sketch.error=status.error;sketch.residual=INFINITY;continue;}
        if(status.waiting){
            if(require){sketch.error="Project source is not evaluated yet";sketch.residual=INFINITY;}
            continue;
        }
        evaluateSketch(sketch,env);
    }
}

// Collect semantic authoring inputs without touching evaluated bodies. Named
// point -> datum -> sketch/feature chains must be checked before cache reuse or
// native evaluation, including when their producer appears later in history.
struct AuthoringSources {
    using V=arkjson::Value;
    const Document& document;std::set<u64> features;std::set<std::string> visiting,done;bool named=false,sweepProfiles=false;
    explicit AuthoringSources(const Document& doc):document(doc){}
    void producer(const V& id){
        if(id.t!=V::Num||id.num<1||id.num>9007199254740990.||id.num!=std::floor(id.num))throw std::runtime_error("Associative producer requires a stable numeric feature ID");
        const auto* source=document.find((u64)id.num);if(!source)throw std::runtime_error("Associative source feature not found");
        if(source->suppressed)throw std::runtime_error("Associative source feature is suppressed");features.insert(source->id);
    }
    void occurrence(const V& id){
        if(id.t==V::Obj){const auto* node=id.find("node");if(!node)throw std::runtime_error("Associative occurrence object requires a node");occurrence(*node);return;}
        if(id.t==V::Str){u64 found=0;size_t count=0;for(const auto& node:document.assembly)if(node.name==id.str){found=node.id;++count;}if(count!=1)throw std::runtime_error("Associative occurrence name is missing or ambiguous");occurrence(V::makeNum((double)found));return;}
        if(id.t!=V::Num||id.num<1||id.num>9007199254740990.||id.num!=std::floor(id.num))throw std::runtime_error("Associative occurrence requires a stable numeric node ID");
        auto sources=assemblyOccurrenceDependencies(document,(u64)id.num);features.insert(sources.begin(),sources.end());named=true;
    }
    bool enter(const std::string& key){named=true;if(done.count(key))return false;if(!visiting.insert(key).second)throw std::runtime_error("Cyclic associative authoring dependency: "+key);return true;}
    void leave(const std::string& key){visiting.erase(key);done.insert(key);}
    void datum(const std::string& name){
        if(name=="XY"||name=="XZ"||name=="YZ")return;const auto key="datum:"+name;if(!enter(key))return;
        const DatumPlane* definition=nullptr;for(const auto& d:document.planes)if(d.name==name)definition=&d;
        if(!definition)throw std::runtime_error("Associative construction datum not found: "+name);validateDatumDefinition(*definition);
        if(definition->support.isNull())datum(definition->parent);
        else {
            const auto& support=definition->support;for(const char* p:{"feature","second_feature"})if(auto id=support.find(p))producer(*id);
            for(const char* p:{"plane","axis","point","a","b","c","start","end","first","second","x_axis"})if(auto ref=support.find(p))anchor(*ref);
        }leave(key);
    }
    void sketch(const std::string& name){
        const auto key="sketch:"+name;if(!enter(key))return;const NamedSketch* source=nullptr;for(const auto& s:document.sketches)if(s.name==name)source=&s;
        if(!source)throw std::runtime_error("Associative sketch not found: "+name);datum(source->sk.plane);
        for(const auto& e:source->sk.ents)if(e.kind==SketchEnt::Project){if(!e.sourceSketch.empty())sketch(e.sourceSketch);else if(e.sourceFeature)producer(V::makeNum((double)e.sourceFeature));if(e.sourceInstance)occurrence(V::makeNum((double)e.sourceInstance));}
        leave(key);
    }
    void anchor(const V& ref){
        if(ref.t==V::Str){datum(ref.str);return;}if(ref.t!=V::Obj)return;
        if(auto p=ref.find("datum")){if(p->t!=V::Str)throw std::runtime_error("Datum anchor name must be a string");datum(p->str);}
        if(auto p=ref.find("sketch")){if(p->t!=V::Str)throw std::runtime_error("Sketch anchor name must be a string");sketch(p->str);}
        if(auto p=ref.find("feature"))producer(*p);
        for(const char* key:{"node","source_instance"})if(auto p=ref.find(key))occurrence(*p);
    }
    void profile(const V& value){
        if(value.t==V::Obj){native(value);return;}if(value.t!=V::Str)return;named=true;for(const auto& s:document.sketches)if(s.name==value.str){sketch(s.name);return;}
        for(const auto& d:document.planes)if(d.name==value.str&&d.kind=="point"){datum(d.name);return;}
        const Feature* source=nullptr;for(const auto& f:document.features)if(f.name==value.str){if(source)throw std::runtime_error("Ambiguous associative curve name: "+value.str);source=&f;}
        if(source){producer(V::makeNum((double)source->id));return;}
        throw std::runtime_error("Associative profile/curve source not found: "+value.str);
    }
    void native(const V& value){
        if(value.t==V::Arr){for(const auto& child:value.arr)native(child);return;}if(value.t!=V::Obj)return;
        if(const auto* type=value.find("type");type&&type->t==V::Str&&type->str=="on_axis")if(const auto* axis=value.find("axis");axis&&axis->t==V::Str)datum(axis->str);
        for(const auto& kv:value.obj){
            if((kv.first=="feature"||kv.first=="second_feature"||kv.first=="source_feature"||kv.first=="alternate_feature"||kv.first=="curve"||kv.first=="support")&&kv.second.t==V::Num)producer(kv.second);
            else if(kv.first=="node"||kv.first=="source_instance"||kv.first=="alternate_node")occurrence(kv.second);
            else if(kv.first=="instances"){if(kv.second.t!=V::Arr)throw std::runtime_error("Associative instances must be an array");for(const auto& id:kv.second.arr)occurrence(id);}
            else if(kv.first=="start_ref"||kv.first=="to_ref"||kv.first=="through_ref"||kv.first=="point_ref"||kv.first=="normal_ref")anchor(kv.second);
            else if(kv.first=="datum") {if(kv.second.t==V::Str)datum(kv.second.str);}
            else if(kv.first=="plane") {if(kv.second.t==V::Str)datum(kv.second.str);else if(kv.second.t==V::Obj)anchor(kv.second);}
            else if(kv.first=="sketch"||kv.first=="source_sketch") {if(kv.second.t==V::Str)sketch(kv.second.str);}
            else if(kv.first=="sections"||kv.first=="guides") {if(kv.second.t==V::Arr)for(const auto& name:kv.second.arr)profile(name);}
            // A construction-frame profile's section="round"/"rect" is
            // its cross-section enum. Only a sweep interprets section/path
            // strings as named CAD resources, including station objects.
            else if(sweepProfiles&&(kv.first=="path"||kv.first=="section")&&kv.second.t==V::Str)profile(kv.second);
            else if(sweepProfiles&&kv.first=="guide"&&kv.second.t==V::Str)profile(kv.second);
            else native(kv.second);
        }
    }
    void collect(const Feature& feature){
        sweepProfiles=feature.type==FeatureType::Sweep;
        for(u64 id:feature.inputs)producer(V::makeNum((double)id));
        if(auto p=feature.p.find("plane");p!=feature.p.end())datum(p->second);
        if(auto p=feature.p.find("sketch");p!=feature.p.end()&&!p->second.empty())sketch(p->second);
        native(feature.nativeSpec);
    }
};

std::unordered_set<u64> assemblyFeatureDependencies(const Document& document,const Feature& feature){
    AuthoringSources sources(document);sources.collect(feature);return {sources.features.begin(),sources.features.end()};
}

void validateFeatureHistoryOrder(const Document& document){
    std::unordered_map<u64,size_t> indices;for(size_t i=0;i<document.features.size();++i)indices[document.features[i].id]=i;
    for(size_t i=0;i<document.features.size();++i)if(!document.features[i].suppressed){
        AuthoringSources sources(document);sources.collect(document.features[i]);
        for(u64 source:sources.features){const auto found=indices.find(source);if(found==indices.end()||found->second>=i)
            throw std::runtime_error("Forward or cyclic associative feature dependency: "+document.features[i].name+" -> "+std::to_string(source));}
    }
}

static bool deferredSources(const Document& document,const AuthoringSources& sources){
    if(!document.historyTip&&document.assemblyLoadSelection.isNull())return false;
    const auto loaded=assemblyLoadedFeatures(document);
    for(u64 source:sources.features)if(!featureWithinHistory(document,source)||!loaded.count(source))return true;
    return false;
}
static bool deferredSketch(const Document& document,const NamedSketch& sketch){
    if(!document.historyTip&&document.assemblyLoadSelection.isNull())return false;
    try{AuthoringSources sources(document);sources.sketch(sketch.name);return deferredSources(document,sources);}catch(...){return false;}
}
static bool deferredDatum(const Document& document,const std::string& name){
    if(!document.historyTip&&document.assemblyLoadSelection.isNull())return false;
    try{AuthoringSources sources(document);sources.datum(name);return deferredSources(document,sources);}catch(...){return false;}
}

const std::vector<FeatureResult>& Document::recompute() {
    NativeDocumentResultScope nativeResults(pendingNativeGeometry);
    f64 t0=nowMs();
    refreshSkeleton(*this);
    verifyAssemblyExternalReferences(*this);
    if(historyTip&&!find(historyTip))throw std::runtime_error("Feature history tip was not found");
    const auto loadedFeatures=assemblyLoadedFeatures(*this);
    std::vector<bool> activeFeatures;bool withinHistory=true;
    for(const auto& feature:features){activeFeatures.push_back(withinHistory&&loadedFeatures.count(feature.id));if(feature.id==historyTip)withinHistory=false;}
    // Cache custody is process-local and never serialized. Audited literal/input
    // consumers use explicit sources; unaudited/named consumers retain history.
    arkjson::Value authored;if(!arkjson::parse(documentToJson(*this,false,false),authored))throw std::runtime_error("Cannot serialize evaluation dependencies");
    // Cache only the named authoring context actually reachable from a feature.
    // Substring matching intentionally over-approximates references (never drops
    // an ambiguous dependency). Projected/support geometry keeps a full context.
    bool geometricSupports=false,instanceProjection=false;
    for(const auto& p:planes)geometricSupports|=p.support.t==arkjson::Value::Obj;
    for(const auto& sk:sketches)for(const auto& e:sk.sk.ents)if(e.kind==SketchEnt::Project){geometricSupports=true;instanceProjection|=e.sourceInstance!=0;}
    for(const auto& f:features)instanceProjection|=f.type==FeatureType::Curve&&f.nativeSpec.find("source_instance")!=nullptr;
    auto occurrenceBoundCable=[](const Feature& feature){
        if(feature.type!=FeatureType::CableRoute)return false;
        std::function<bool(const arkjson::Value&)> contains=[&](const arkjson::Value& v){if(v.t==arkjson::Value::Obj){if(v.find("node"))return true;for(const auto& item:v.obj)if(contains(item.second))return true;}if(v.t==arkjson::Value::Arr)for(const auto& child:v.arr)if(contains(child))return true;return false;};return contains(feature.nativeSpec);
    };
    auto scopedContext=[&](const Feature& feature,const std::string& own){
        std::string context;std::set<std::string> included;bool changed=true;
        while(changed){changed=false;for(const char* kind:{"sketches","planes"})if(const auto* rows=authored.find(kind))for(const auto& row:rows->arr){
            const auto* name=row.find("name");if(!name||name->str.empty())continue;const std::string key=std::string(kind)+":"+name->str;const auto quoted=arkjson::Value::makeStr(name->str).dump();const auto reference=quoted.substr(1,quoted.size()-2);
            if(!included.count(key)&&(geometricSupports||own.find(reference)!=std::string::npos||context.find(reference)!=std::string::npos)){included.insert(key);context+=row.dump();changed=true;}
        }}
        // Frame mass/stock reports consume material data; ordinary solid geometry
        // does not depend on appearance, BOM or unrelated material declarations.
        if(feature.type==FeatureType::ConstructionFrame)if(const auto* m=authored.find("materials"))context+=m->dump();
        if(instanceProjection||occurrenceBoundCable(feature))for(const char* kind:{"assembly","mates"})if(const auto* v=authored.find(kind))context+=v->dump();
        return context;
    };
    std::unordered_map<u64,EvaluationCacheEntry> nextCache;
    std::unordered_map<u64,std::string> keys;
    std::string upstream;
    size_t featureIndex=0;recomputeReused=0;recomputeEvaluated=0;
    lastResults.clear();
    evalBodies.clear();
    refreshParams();
    std::string manufacturingError;
    if(!validateManufacturingRoutes(*this,manufacturingError))parameterError=manufacturingError;
    if(parameterError.empty())try{validateNativeComponentCustody(*this);}catch(const std::exception& error){parameterError=error.what();}
    ExprEnv env=buildEnv();
    for(auto& ns:sketches){
        for(auto& entity:ns.sk.ents)if(entity.kind==SketchEnt::Project){entity.projectionBrep.clear();entity.projectionSamples.clear();entity.projectionLength=0;entity.dof.clear();entity.expressions.clear();entity.segments.clear();}
        if(deferredSketch(*this,ns)){ns.error.clear();ns.residual=0;continue;}
        evaluateSketch(ns,env);
    }
    std::vector<Body> work;
    evaluatingBodies=&work;
    struct EvaluationScope {Document& doc;~EvaluationScope(){doc.evaluatingBodies=nullptr;}} evaluationScope{*this};
    // Sketch-only associations can resolve before the first feature. Feature-backed
    // sources remain waiting until their producer crosses the publication boundary.
    refreshDrivenSketches(*this,false);
    std::vector<bool> explicitInputs;for(const auto& f:features)explicitInputs.push_back(explicitDependencies(f));
    const unsigned cores=std::max(1u,std::thread::hardware_concurrency());
    const unsigned cpuBudget=std::min(cores,threadSetting("ARK_CPU_THREADS",std::min(cores,32u),64));
    const unsigned requestedWorkers=threadSetting("ARK_FEATURE_THREADS",std::min(cpuBudget,4u),16);

    parallelFeatures=0;peakParallelFeatures=0;dependencyEdges=0;parallelContextFeaturesCopied=0;
    std::unordered_map<u64,size_t> indices;for(size_t i=0;i<features.size();++i)indices[features[i].id]=i;
    std::vector<std::vector<size_t>> dependencies(features.size());
    std::vector<std::string> authoringDependencyErrors(features.size());
    for(size_t i=0;i<features.size();++i){
        if(activeFeatures[i]&&!features[i].suppressed)try{
            AuthoringSources sources(*this);sources.collect(features[i]);if(sources.named)explicitInputs[i]=false;
            for(u64 source:sources.features)if(std::find(features[i].inputs.begin(),features[i].inputs.end(),source)==features[i].inputs.end())explicitInputs[i]=false;
            for(u64 source:sources.features){const auto found=indices.find(source);if(found==indices.end()||found->second>=i)throw std::runtime_error("Forward or cyclic associative feature dependency: "+features[i].name+" -> "+std::to_string(source));}
        }catch(const std::exception& error){authoringDependencyErrors[i]=error.what();explicitInputs[i]=false;}
        for(u64 input:features[i].inputs){auto found=indices.find(input);if(found==indices.end()||found->second>=i){explicitInputs[i]=false;continue;}dependencies[i].push_back(found->second);}
        if(!explicitInputs[i]){dependencies[i].clear();for(size_t j=0;j<i;++j)dependencies[i].push_back(j);}
        dependencyEdges+=dependencies[i].size();
    }
    // Semantic source discovery can turn otherwise independent operations into
    // ordered barriers. Budget only the final audited set, after that discovery.
    const size_t eligible=(size_t)std::count(explicitInputs.begin(),explicitInputs.end(),true);
    featureWorkers=eligible>=4?std::min(cpuBudget,requestedWorkers):1;
    nativeThreadsPerWorker=std::min(threadSetting("ARK_NATIVE_THREADS",8,64),std::max(1u,cpuBudget/featureWorkers));
    NativeBudgetScope ownerBudget(nativeThreadsPerWorker);
    auto keyFor=[&](size_t index){
        const auto& f=features[index];auto source=authored.find("features")->arr[index];
        source.obj.erase(std::remove_if(source.obj.begin(),source.obj.end(),[](const auto& v){return v.first=="appearance"||v.first=="part";}),source.obj.end());
        // A feature that survived semantic source discovery cannot read named
        // sketches/datums/occurrences. Unrelated global projected geometry must
        // not add those resources to this feature's otherwise explicit key.
        std::string own=source.dump(),context=explicitInputs[index]?std::string{}:scopedContext(f,own),dependenciesText=own+context,key=dependenciesText;
        for(const auto& param:params){
            if(param.name.empty())continue;size_t pos=0;bool used=false;
            while((pos=dependenciesText.find(param.name,pos))!=std::string::npos){auto ident=[](char c){return std::isalnum((unsigned char)c)||c=='_';};if((pos==0||!ident(dependenciesText[pos-1]))&&(pos+param.name.size()==dependenciesText.size()||!ident(dependenciesText[pos+param.name.size()]))){used=true;break;}pos+=param.name.size();}
            if(used)key+=param.name+"="+arkjson::Value::makeNum(param.value).dump();
        }
        if(!explicitInputs[index]){key+=upstream;for(const char* name:{"assembly","mates"})if(auto value=authored.find(name))key+=value->dump();if(geometricSupports)for(const auto& p:params)key+=p.name+"="+arkjson::Value::makeNum(p.value).dump();}
        for(u64 input:f.inputs)key+=keys[input];
        for(double n:{tessOpt.tol,tessOpt.maxEdge,tessOpt.minAngleDeg,(double)tessOpt.maxLevels})key+=':'+arkjson::Value::makeNum(n).dump();return key;
    };
    std::vector<std::string> plannedKeys(features.size());std::vector<u64> plannedGenerations(features.size());
    for(size_t i=0;i<features.size();++i){if(!activeFeatures[i])continue;plannedKeys[i]=keyFor(i);auto old=evaluationCache.find(features[i].id);plannedGenerations[i]=old!=evaluationCache.end()&&old->second.key==plannedKeys[i]?old->second.generation:++evaluationGeneration;if(!features[i].suppressed){keys[features[i].id]=std::to_string(features[i].id)+":"+std::to_string(plannedGenerations[i])+";";upstream+=keys[features[i].id];}}
    upstream.clear();
    struct Evaluation {Body body;FeatureResult result;};
    struct Activity {std::atomic<unsigned> active{0},peak{0};void enter(){unsigned n=++active,old=peak.load();while(n>old&&!peak.compare_exchange_weak(old,n)){};}void leave(){--active;}};
    auto activity=std::make_shared<Activity>();
    std::vector<std::future<Evaluation>> jobs(features.size());std::vector<bool> published(features.size(),false);size_t outstanding=0,outstandingBytes=0;std::vector<size_t> jobBytes(features.size(),0);
    std::vector<std::unique_ptr<Evaluation>> completed(features.size());
    const size_t contextBytes=valueMemory(authored)+features.size()*sizeof(Feature),payloadBudget=128u*1024u*1024u;
    struct Drain {std::vector<std::future<Evaluation>>& jobs;~Drain(){for(auto& job:jobs)if(job.valid())job.wait();}} drain{jobs};
    auto harvest=[&](size_t current){
        for(size_t i=current;i<jobs.size();++i)if(jobs[i].valid()&&jobs[i].wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            completed[i]=std::make_unique<Evaluation>(jobs[i].get());
            const auto& body=completed[i]->body;size_t retained=sizeof(Evaluation)+body.faces.size()*sizeof(Face)+valueMemory(body.nativeData);
            if(body.baked)retained+=sizeof(Mesh)+(body.baked->pos.capacity()+body.baked->nrm.capacity())*sizeof(f32)+body.baked->idx.capacity()*sizeof(u32);
            const size_t charge=std::max(jobBytes[i],retained);outstandingBytes+=charge-jobBytes[i];jobBytes[i]=charge;
            // Finished outputs keep their slot and charge until publication.
            // Otherwise looking beyond owner-only rows could accumulate a
            // document-sized queue of completed, unpublished geometry.
        }
    };
    auto inputBody=[&](u64 id)->const Body*{
        for(const auto& b:work)if(b.id==id)return &b;
        auto found=indices.find(id);if(found==indices.end()||!activeFeatures[found->second]||features[found->second].suppressed)return nullptr;size_t index=found->second;
        if(completed[index])return completed[index]->result.ok?&completed[index]->body:nullptr;
        auto cached=evaluationCache.find(id);if(cached!=evaluationCache.end()&&cached->second.key==plannedKeys[index])return cached->second.body.get();return nullptr;
    };
    // An unpublished feature whose audited explicit inputs are all evaluated
    // may run on the pool. bytes receives its input charge for the budget.
    auto workerReady=[&](size_t i,size_t& bytes){
        if(!activeFeatures[i])return false;
        // Only owned, audited explicit-input jobs cross owner-only rows.
        // Named sketch/datum/occurrence consumers stay on the owner thread.
        if(!explicitInputs[i]||jobs[i].valid()||completed[i]||published[i])return false;
        bool ready=true;for(size_t dependency:dependencies[i])ready&=inputBody(features[dependency].id)!=nullptr;if(!ready)return false;
        for(u64 id:features[i].inputs){
            const Body* body=inputBody(id);if(!body)return false;
            // Mesh workers copy an already evaluated triangle payload. Keep
            // lazy analytic tessellation on the owner thread until audited.
            if(isMeshModelFeature(features[i].type)){ready&=body->baked&&body->baked->triCount();continue;}
            if(!body->nativeData.find("brep_base64")){auto source=indices.find(id);if(source==indices.end())ready=false;else{auto type=features[source->second].type;ready&=type==FeatureType::Box||type==FeatureType::Cylinder||type==FeatureType::Sphere||type==FeatureType::Cone||type==FeatureType::Torus;}}
        }
        if(!ready)return false;
        const auto& key=plannedKeys[i];auto cached=evaluationCache.find(features[i].id);if(cached!=evaluationCache.end()&&cached->second.key==key)return false;
        bytes=contextBytes;for(u64 id:features[i].inputs){const auto* body=inputBody(id);bytes+=sizeof(Body)+body->faces.size()*sizeof(Face)+valueMemory(body->nativeData);if(body->baked)bytes+=(body->baked->pos.size()+body->baked->nrm.size()+body->baked->idx.size())*sizeof(float);}
        return bytes<=payloadBudget&&outstandingBytes<=payloadBudget-bytes;
    };
    auto dispatch=[&](size_t i,size_t bytes){
        {
            // A task owns all mutable evaluation context. Shared cache bodies are
            // immutable; lazy native promotion changes only these input copies.
            Document snapshot;
            // Audited tasks cannot consume named sketches/datums/occurrences.
            // Retain the complete explicit ancestor recipes (native promotion
            // may inspect them), rather than copying every unrelated feature
            // once for each scheduled task. Preserve original history order.
            std::set<size_t> contextFeatures;
            std::function<void(size_t)> includeSource=[&](size_t index){if(!contextFeatures.insert(index).second)return;for(u64 id:features[index].inputs){auto source=indices.find(id);if(source!=indices.end())includeSource(source->second);}};
            includeSource(i);snapshot.features.reserve(contextFeatures.size());
            for(size_t index:contextFeatures)snapshot.features.push_back(features[index]);
            parallelContextFeaturesCopied+=snapshot.features.size();
            snapshot.params=params;snapshot.materials=materials;snapshot.tessOpt=tessOpt;
            std::vector<Body> inputs;for(u64 id:features[i].inputs)inputs.push_back(*inputBody(id));
            const Feature feature=features[i];const unsigned nativeBudget=nativeThreadsPerWorker;
            auto task=std::make_shared<std::packaged_task<Evaluation()>>([snapshot=std::move(snapshot),inputs=std::move(inputs),feature,env,nativeBudget,activity,pending=pendingNativeGeometry]()mutable{
                NativeDocumentResultScope nativeResults(std::move(pending));
                NativeBudgetScope budget(nativeBudget);Evaluation value;value.result.id=feature.id;double started=nowMs();activity->enter();
                try{value.result.error=featureParameterError(feature,env);value.result.ok=value.result.error.empty();if(value.result.ok){snapshot.evaluatingBodies=&inputs;value.body=evalFeatureBody(feature,env,snapshot,inputs,value.result,snapshot.tessOpt);value.body.id=feature.id;}}catch(const std::exception& e){value.result.ok=false;value.result.error=e.what();}
                activity->leave();value.result.ms=nowMs()-started;return value;
            });
            jobs[i]=task->get_future();ThreadPool::get().submit([task]{(*task)();});++outstanding;++parallelFeatures;jobBytes[i]=bytes;outstandingBytes+=bytes;
        }
    };
    // While the owner waits for a pool job it evaluates nothing itself, so
    // every feature worker may be busy; otherwise one slot stays the owner's.
    auto schedule=[&](size_t current,bool ownerWaiting){
        if(featureWorkers<2||!parameterError.empty())return;
        harvest(current);
        for(size_t i=current+1;i<features.size()&&outstanding<(ownerWaiting?featureWorkers:featureWorkers-1);++i){size_t bytes=0;if(workerReady(i,bytes))dispatch(i,bytes);}
    };
    for(auto&f:features){
        const size_t index=featureIndex++;
        if(!activeFeatures[index]){FeatureResult inactive;inactive.id=f.id;lastResults.push_back(inactive);continue;}
        schedule(index,false);std::string cacheKey=plannedKeys[index];
        FeatureResult fr; fr.id=f.id;
        const auto previousCache=evaluationCache.find(f.id);const bool cachedFeature=previousCache!=evaluationCache.end()&&previousCache->second.key==cacheKey;
        evaluationProgress(f.id,f.name,index,features.size(),false,cachedFeature);
        f64 tf=nowMs();
        if(f.suppressed){
            fr.ok=true;fr.ms=nowMs()-tf;lastResults.push_back(fr);refreshDrivenSketches(*this,false);continue;
        }
        bool blocked=false;
        for(u64 input:f.inputs){if(const Feature* source=find(input);source&&source->suppressed){fr.ok=false;fr.error="source feature is suppressed";blocked=true;break;}}
        if(blocked){fr.ms=nowMs()-tf;lastResults.push_back(std::move(fr));refreshDrivenSketches(*this,false);continue;}
        fr.error=!authoringDependencyErrors[index].empty()?authoringDependencyErrors[index]:parameterError.empty()?featureParameterError(f,env):parameterError;
        fr.ok=fr.error.empty();
        // An audited feature of the owner's own runs on the pool as well, and
        // the owner keeps dispatching later ready features whenever a job
        // finishes instead of blocking all scheduling while it evaluates.
        // Publication below still follows document order.
        if(fr.ok&&!cachedFeature&&featureWorkers>=2&&parameterError.empty()&&!completed[index]&&!jobs[index].valid()&&outstanding<featureWorkers){size_t bytes=0;if(workerReady(index,bytes))dispatch(index,bytes);}
        while(fr.ok&&!completed[index]&&jobs[index].valid()&&jobs[index].wait_for(std::chrono::milliseconds(2))!=std::future_status::ready)schedule(index,true);
        Body b;bool measuredByWorker=false;
        try{if(fr.ok){auto cached=evaluationCache.find(f.id);if(cached!=evaluationCache.end()&&cached->second.key==cacheKey){b=*cached->second.body;fr.tris=cached->second.tris;++recomputeReused;}else if(completed[index]){b=std::move(completed[index]->body);fr=std::move(completed[index]->result);measuredByWorker=true;completed[index].reset();--outstanding;outstandingBytes-=jobBytes[index];++recomputeEvaluated;}else if(jobs[index].valid()){auto value=jobs[index].get();--outstanding;outstandingBytes-=jobBytes[index];b=std::move(value.body);fr=std::move(value.result);measuredByWorker=true;++recomputeEvaluated;}else{OuterParallelScope nested(outstanding>0);NativeBudgetScope budget(!explicitInputs[index]&&outstanding==0?std::min(threadSetting("ARK_NATIVE_THREADS",8,64),cpuBudget):nativeThreadsPerWorker);activity->enter();try{b=evalFeatureBody(f,env,*this,work,fr,tessOpt);}catch(...){activity->leave();throw;}activity->leave();++recomputeEvaluated;}}}
        catch(const std::exception& e){fr.ok=false;fr.error=e.what();}
        // A completed worker retains its actual evaluation duration; publication
        // wait time alone can be near zero even for an expensive operation.
        if(!measuredByWorker)fr.ms=nowMs()-tf;
        if(fr.ok && b.faces.empty() && !b.baked){
            fr.ok=false; fr.error="feature produced empty body";
        }
        if(fr.ok){
            b.id=f.id;
            const u64 generation=plannedGenerations[index];
            if(cachedFeature)nextCache[f.id]=previousCache->second;
            else nextCache[f.id]={std::move(cacheKey),std::make_shared<const Body>(b),generation,fr.tris};
            keys[f.id]=std::to_string(f.id)+":"+std::to_string(generation)+";";upstream+=keys[f.id];
            work.push_back(std::move(b));
        } else {
            b=Body{};
        }
        published[index]=true;lastResults.push_back(std::move(fr));
        evaluationProgress(f.id,f.name,index,features.size(),true,cachedFeature);
        refreshDrivenSketches(*this,false);
    }
    refreshDrivenSketches(*this,true);
    ExprEnv canvasEnv=buildEnv();
    for(const auto& canvas:canvases){
        bool known=canvas.plane=="XY"||canvas.plane=="XZ"||canvas.plane=="YZ";
        for(const auto& plane:planes) if(plane.name==canvas.plane) known=true;
        if(!known){if(parameterError.empty())parameterError="Canvas plane was not found";continue;}
        if(!deferredDatum(*this,canvas.plane))try{resolvePlane(*this,canvas.plane);}catch(const std::exception& e){if(parameterError.empty())parameterError=e.what();}
        auto lengthOk=[&](const std::string& expr){bool ok=false;double value=evalExpr(expr,canvasEnv,&ok);return ok&&value>0.&&value<=1e6;};
        if(!lengthOk(canvas.width)||!lengthOk(canvas.height)){if(parameterError.empty())parameterError="Canvas width and height must be positive millimetres";}
    }
    try{for(auto& plane:planes)if(!deferredDatum(*this,plane.name))resolvePlane(*this,plane.name);}
    catch(const std::exception& e){if(parameterError.empty())parameterError=e.what();}
    for(auto& ns:sketches)if(ns.error.empty()&&!deferredSketch(*this,ns)){
        try{resolvePlane(*this,ns.sk.plane);}catch(const std::exception& e){ns.error=e.what();ns.residual=INFINITY;}
    }
    evaluatingBodies=nullptr;
    evalBodies=std::move(work);
    for(auto& job:jobs)if(job.valid())job.wait();
    peakParallelFeatures=activity->peak.load();
    evaluationCache=std::move(nextCache);
    // Unloaded definitions retain authored recipes, never derived native
    // buffers. These two auxiliary caches otherwise outlive evalBodies.
    for(auto it=referenceFrameCache.begin();it!=referenceFrameCache.end();)if(!evaluationCache.count(it->first))it=referenceFrameCache.erase(it);else ++it;
    for(auto it=centroidCache.begin();it!=centroidCache.end();)if(!evaluationCache.count(it->first))it=centroidCache.erase(it);else ++it;
    recomputeMs=nowMs()-t0;
    return lastResults;
}

} // namespace ark
