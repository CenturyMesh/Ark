// ARK-Point :: evaluate.cpp â€” feature-history recompute pipeline.
#include "ark/model.hpp"
#include "ark/source_tube.hpp"
#include <chrono>
#include <exception>

namespace ark {

static f64 nowMs() {
    using namespace std::chrono;
    return duration<f64,std::milli>(steady_clock::now().time_since_epoch()).count();
}

// Sketch -> profile: chain line entities into one closed loop (order- and
// direction-agnostic); a lone circle becomes a full-circle arc profile.
static bool profileFromSketch(const Sketch& sk, const Feature& f, const ExprEnv& env,
                              SketchProfile& prof, std::string& err) {
    prof.frame = Mat4::translation({f.num(env,"ox"), f.num(env,"oy"), f.num(env,"oz")});

    AABB bb;
    for(auto&e:sk.ents){
        if(e.kind==SketchEnt::Circle){
            bb.grow(Vec3{e.dof[0]-e.dof[2], e.dof[1]-e.dof[2], 0});
            bb.grow(Vec3{e.dof[0]+e.dof[2], e.dof[1]+e.dof[2], 0});
        } else if(e.kind==SketchEnt::Line){
            bb.grow(Vec3{e.dof[0],e.dof[1],0}); bb.grow(Vec3{e.dof[2],e.dof[3],0});
        }
    }
    f64 tol = bb.valid()? 1e-6*tmax(bb.diagonal(),1.0) : 1e-6;

    std::vector<size_t> lines, circs;
    for(size_t i=0;i<sk.ents.size();++i){
        if(sk.ents[i].kind==SketchEnt::Line) lines.push_back(i);
        else if(sk.ents[i].kind==SketchEnt::Circle) circs.push_back(i);
    }

    if(circs.size()==1 && lines.empty()){
        auto& c=sk.ents[circs[0]].dof;
        Vec2 ctr{c[0],c[1]}; f64 r=c[2];
        if(r<=tol){ err="sketch '"+f.get("sketch")+"': circle radius ~ 0"; return false; }
        for(int q=0;q<4;++q)
            prof.segs.push_back({ProfileSeg::Arc,{},{},ctr,r,
                                 (f64)q*K_HALF_PI,(f64)(q+1)*K_HALF_PI});
        return true;
    }
    if(lines.empty()){ err="sketch has no line/circle geometry"; return false; }
    if(!circs.empty()){
        err="sketch mixes lines and circles; inner loops not supported yet";
        return false;
    }

    // greedy nearest-endpoint chaining
    size_t n=lines.size();
    std::vector<bool> used(n,false);
    Vec2 cur{}, first{};
    bool have=false;
    for(size_t k=0;k<n;++k){
        i32 best=-1; bool flip=false; f64 bestd=tol;
        for(size_t i=0;i<n;++i){
            if(used[i]) continue;
            auto&d=sk.ents[lines[i]].dof;
            Vec2 s{d[0],d[1]}, t{d[2],d[3]};
            if(!have){ best=(i32)i; break; }
            f64 d1=(cur-s).len(), d2=(cur-t).len();
            if(d1<bestd){ bestd=d1; best=(i32)i; flip=false; }
            if(d2<bestd){ bestd=d2; best=(i32)i; flip=true;  }
        }
        if(best<0){ err="sketch lines form multiple disjoint loops"; return false; }
        used[(size_t)best]=true;
        auto&d=sk.ents[lines[(size_t)best]].dof;
        Vec2 s{d[0],d[1]}, t{d[2],d[3]};
        if(flip) std::swap(s,t);
        if((t-s).len()<=tol) continue;                 // degenerate segment
        prof.segs.push_back({ProfileSeg::Line,s,t});
        if(!have){ first=s; have=true; }
        cur=t;
    }
    if(prof.segs.empty()){ err="sketch has no usable profile segments"; return false; }
    if((first-cur).len()>tol)
        prof.segs.push_back({ProfileSeg::Line,cur,first});   // explicit closure
    return true;
}

static bool makeProfile(const Feature& f, const ExprEnv& env, Document& doc,
                        SketchProfile& prof, std::string& err) {
    std::string sname=f.get("sketch");
    if(!sname.empty()){
        NamedSketch* ns=doc.findSketch(sname);
        if(!ns){ err="sketch '"+sname+"' not found"; return false; }
        return profileFromSketch(ns->sk,f,env,prof,err);
    }

    prof.frame = Mat4::identity();
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
            Body out; out.baked=std::make_unique<Mesh>();auto& m=*out.baked;
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
            m.computeNormals();res.tris=m.triCount();return out;
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
            SketchProfile prof; std::string perr;
            if(!makeProfile(f,env,doc,prof,perr)){ res.ok=false; res.error=perr; return {}; }
            return extrudeProfile(prof, tmax(f.num(env,"height",8),1e-9),
                                  f.num(env,"both",0)!=0.0);
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
            out.baked=std::make_unique<Mesh>(meshBoolean(ma,mb,op));
            meshOrientOutward(*out.baked);
            out.baked->computeNormals();
            res.tris=out.baked->triCount();
            return out;
        }

        case FeatureType::LinearPattern: {
            Body* src = f.inputs.empty()?nullptr:findInput(f.inputs[0]);
            if(!src || (!src->baked && src->faces.empty())){
                res.ok=false; res.error="pattern: input missing"; return {}; }
            int n=(int)clamp(f.num(env,"count",3),1.0,256.0);
            Vec3 dir={f.num(env,"dx",20),f.num(env,"dy",0),f.num(env,"dz",0)};
            Body acc; acc.id=f.id; acc.baked=std::make_unique<Mesh>();
            Mesh base=src->tessellate(topt);
            for(int k=0;k<n;++k){
                Mesh part; part.pos=base.pos; part.idx=base.idx;
                Mat4 M=Mat4::translation(dir*(f64)k);
                size_t nv=part.vertCount();
                std::vector<Vec3> in(nv),outv(nv);
                for(size_t i=0;i<nv;++i) in[i]={part.pos[i*3],part.pos[i*3+1],part.pos[i*3+2]};
                transform_points_f32(M,in.data(),outv.data(),nv);
                for(size_t i=0;i<nv;++i){ part.pos[i*3]=(f32)outv[i].x; part.pos[i*3+1]=(f32)outv[i].y; part.pos[i*3+2]=(f32)outv[i].z; }
                acc.baked->append(part);
            }
            acc.baked->weld(1e-7*tmax(src->bbox().diagonal(),1.0));
            meshOrientOutward(*acc.baked);
            acc.baked->computeNormals();
            res.tris=acc.baked->triCount();
            return acc;
        }
    }
    res.ok=false; res.error="unknown feature type";
    return {};
}

const std::vector<FeatureResult>& Document::recompute() {
    f64 t0=nowMs();
    lastResults.clear();
    evalBodies.clear();
    refreshParams();
    ExprEnv env=buildEnv();
    for(auto&ns:sketches){                       // sketches solve before features
        int iters=0;
        ns.residual=solveSketch(ns.sk,&iters);
    }
    std::vector<Body> work;
    for(auto&f:features){
        FeatureResult fr; fr.id=f.id;
        f64 tf=nowMs();
        fr.error=parameterError;
        for(auto& kv:f.p){
            if(kv.first=="shape"||kv.first=="section"||kv.first=="sketch"||kv.first=="name"||
               (f.type==FeatureType::ParametricSurface&&(kv.first=="x"||kv.first=="y"||kv.first=="z")))continue;
            bool ok=false; f64 value=evalExpr(kv.second,env,&ok);
            if(!ok || std::fabs(value)>1e8)fr.error="Invalid expression for '"+kv.first+"': "+kv.second;
        }
        auto positive=[&](std::initializer_list<const char*> keys){for(auto key:keys)if(f.p.count(key)&&f.num(env,key)<=0)fr.error=std::string(key)+" must be positive";};
        switch(f.type){
            case FeatureType::Box:positive({"dx","dy","dz"});break;
            case FeatureType::Cylinder:positive({"r","h"});break;
            case FeatureType::Sphere:positive({"r"});break;
            case FeatureType::Cone:positive({"rb","h"});if(f.num(env,"rt")<0)fr.error="rt cannot be negative";break;
            case FeatureType::Torus:positive({"R","r"});if(f.num(env,"r",2)>=f.num(env,"R",8))fr.error="torus requires R > r";break;
            case FeatureType::Extrude:positive({"w","d","r","height"});break;
            case FeatureType::Revolve:positive({"w","d","r","angle"});break;
            case FeatureType::LinearPattern:if(f.num(env,"count",3)<1||f.num(env,"count",3)>256||std::floor(f.num(env,"count",3))!=f.num(env,"count",3))fr.error="count must be an integer in 1..256";break;
            case FeatureType::ParametricSurface:for(auto key:{"nu","nv"})if(f.p.count(key)&&std::floor(f.num(env,key))!=f.num(env,key))fr.error="surface subdivisions must be integers";break;
            default:break;
        }
        fr.ok=fr.error.empty();
        Body b=fr.ok?evalFeatureBody(f,env,*this,work,fr,tessOpt):Body{};
        fr.ms=nowMs()-tf;
        if(fr.ok && b.faces.empty() && !b.baked){
            fr.ok=false; fr.error="feature produced empty body";
        }
        if(fr.ok){
            b.id=f.id;
            work.push_back(std::move(b));
        } else {
            b=Body{};
        }
        lastResults.push_back(std::move(fr));
    }
    evalBodies=std::move(work);
    recomputeMs=nowMs()-t0;
    return lastResults;
}

} // namespace ark
