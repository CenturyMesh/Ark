// ARK-Point :: api.cpp â€” C ABI implementation over Document.
#include "ark/api.h"
#include "ark/model.hpp"
#include "ark/io.hpp"
#include "ark/json.hpp"
#include "ark/serialize.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>

using namespace ark;
using arkjson::Value;

// ---------------------------------------------------------------------------
struct ArkDoc {
    Document doc;
    std::string lastErr;
};

static Feature mkFeature(FeatureType t, const char* name){
    Feature f; f.type=t; f.name=name;
    return f;
}

extern "C" {

ArkDoc* ark_doc_create(){ auto d=new ArkDoc(); return d; }
void    ark_doc_destroy(ArkDoc* d){ delete d; }

int ark_param_set(ArkDoc* d,const char* n,const char* e){ if(!d||!n||!e) return -1; d->doc.setParam(n,e); return 0; }
double ark_param_get(ArkDoc* d,const char* n){ return (d&&n)?d->doc.getParam(n):0; }
int ark_param_remove(ArkDoc* d,const char* n){ return (d&&n&&d->doc.removeParam(n))?0:-1; }

uint64_t ark_add_box(ArkDoc* d,double x,double y,double z){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Box,"box");
    f.p={ {"dx",std::to_string(x)},{"dy",std::to_string(y)},{"dz",std::to_string(z)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_cylinder(ArkDoc* d,double r,double h){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Cylinder,"cyl");
    f.p={ {"r",std::to_string(r)},{"h",std::to_string(h)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_sphere(ArkDoc* d,double r){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Sphere,"sph");
    f.p={ {"r",std::to_string(r)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_cone(ArkDoc* d,double rb,double rt,double h){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Cone,"cone");
    f.p={ {"rb",std::to_string(rb)},{"rt",std::to_string(rt)},{"h",std::to_string(h)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_torus(ArkDoc* d,double R,double r){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Torus,"torus");
    f.p={ {"R",std::to_string(R)},{"r",std::to_string(r)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_extrude(ArkDoc* d,const char* shape,double w,double dd,double r,int sides,double height){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Extrude,"extrude");
    f.p={ {"shape", shape?shape:"rect"}, {"w",std::to_string(w)},{"d",std::to_string(dd)},{"r",std::to_string(r)},{"n",std::to_string(sides)},{"height",std::to_string(height)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_revolve(ArkDoc* d,const char* shape,double w,double dd,double r,int sides,double angleDeg){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Revolve,"revolve");
    f.p={ {"shape", shape?shape:"rect"}, {"w",std::to_string(w)},{"d",std::to_string(dd)},{"r",std::to_string(r)},{"n",std::to_string(sides)},{"angle",std::to_string(angleDeg)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_transform(ArkDoc* d,uint64_t in,
                           double tx,double ty,double tz,
                           double rx,double ry,double rz){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Transform,"xform");
    f.inputs={in};
    f.p={ {"tx",std::to_string(tx)},{"ty",std::to_string(ty)},{"tz",std::to_string(tz)},{"rx",std::to_string(rx)},{"ry",std::to_string(ry)},{"rz",std::to_string(rz)} };
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_boolean(ArkDoc* d,uint64_t a,uint64_t b,int op){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::Boolean,op==1?"cut":op==2?"intersect":"union");
    f.inputs={a,b}; f.opInt=op;
    return d->doc.addFeature(std::move(f));
}
uint64_t ark_add_pattern(ArkDoc* d,uint64_t in,double dx,double dy,double dz,int count){
    if(!d) return 0;
    Feature f=mkFeature(FeatureType::LinearPattern,"pattern");
    f.inputs={in};
    f.p={ {"dx",std::to_string(dx)},{"dy",std::to_string(dy)},{"dz",std::to_string(dz)},{"count",std::to_string(count)} };
    return d->doc.addFeature(std::move(f));
}

uint64_t ark_feature_add(ArkDoc* d,const char* typeName){
    if(!d||!typeName) return 0;
    Feature f;
    if(!featureTypeFromName(typeName,f.type)) return 0;
    f.name=typeName;
    return d->doc.addFeature(std::move(f));
}

int ark_feature_remove(ArkDoc* d,uint64_t id){if(!d)return -1;try{return d->doc.remove(id)?0:-1;}catch(const std::exception& e){d->lastErr=e.what();return -1;}}
int ark_feature_move(ArkDoc* d,uint64_t id,int delta){ return (d&&d->doc.reorderMove(id,delta))?0:-1; }

int ark_feature_set_param(ArkDoc* d,uint64_t id,const char* key,const char* expr){
    if(!d||!key||!expr||!*key) return -1;
    Feature* f=d->doc.find(id);
    if(!f) return 1;
    f->p[key]=expr;
    return 0;
}

static bool sketchJsonsToSketch(const char* entitiesJson,const char* constraintsJson,
                                Sketch& sk,std::string& err){
    std::string combined="{\"entities\":";
    combined += (entitiesJson&&*entitiesJson)? entitiesJson : "[]";
    combined += ",\"constraints\":";
    combined += (constraintsJson&&*constraintsJson)? constraintsJson : "[]";
    combined += "}";
    return sketchFromJson(combined,sk,&err);
}

int ark_sketch_set(ArkDoc* d,const char* name,const char* entitiesJson,const char* constraintsJson){
    return ark_sketch_set_owned(d,0,name,entitiesJson,constraintsJson);
}
int ark_sketch_set_owned(ArkDoc* d,uint64_t ownerFeature,const char* name,const char* entitiesJson,const char* constraintsJson){
    if(!d||!name||!*name) return -1;
    Sketch sk; std::string err;
    if(!sketchJsonsToSketch(entitiesJson,constraintsJson,sk,err)) return -2;
    try{d->doc.setSketch(name,std::move(sk),ownerFeature);d->doc.recompute();return 0;}
    catch(const std::exception& e){d->lastErr=e.what();return -3;}
}

char* ark_sketch_json(ArkDoc* d,const char* name){
    if(!d||!name) return nullptr;
    NamedSketch* ns=d->doc.findSketch(name);
    if(!ns) return nullptr;
    std::string s=sketchToJson(ns->sk);
    char* out=(char*)std::malloc(s.size()+1);
    if(!out) return nullptr;
    std::memcpy(out,s.c_str(),s.size()+1);
    return out;
}

int ark_sketch_remove(ArkDoc* d,const char* name){
    return (d&&name&&d->doc.removeSketch(name))?0:-1;
}

int ark_recompute(ArkDoc* d){
    if(!d) return -1;
    auto& res=d->doc.recompute();
    for(auto&r:res) if(!r.ok) return 1;
    return 0;
}

int ark_body_count(ArkDoc* d){ return d?(int)d->doc.evalBodies.size():0; }
uint32_t ark_body_tris(ArkDoc* d,int i){
    if(!d||i<0||(size_t)i>=d->doc.evalBodies.size()) return 0;
    Body& b=d->doc.evalBodies[(size_t)i];
    Mesh m=b.tessellate(d->doc.tessOpt);
    return (u32)m.triCount();
}

static bool mergedMesh(ArkDoc* d,int idx,Mesh& out){
    if(!d) return false;
    if(idx<0){
        TessOptions o=d->doc.tessOpt;
        for(auto&b:d->doc.evalBodies){
            Mesh m=b.tessellate(o);
            out.append(m);
        }
        return true;
    }
    if((size_t)idx>=d->doc.evalBodies.size()) return false;
    out=d->doc.evalBodies[(size_t)idx].tessellate(d->doc.tessOpt);
    return true;
}

int ark_export_stl(ArkDoc* d,const char* path,int idx){
    Mesh m; if(!mergedMesh(d,idx,m)) return -1;
    return exportSTL(path,m)?0:-1;
}
int ark_export_obj(ArkDoc* d,const char* path,int idx){
    Mesh m; if(!mergedMesh(d,idx,m)) return -1;
    return exportOBJ(path,m)?0:-1;
}
int ark_render_png(ArkDoc* d,const char* path,int w,int h){
    Mesh m; if(!mergedMesh(d,-1,m)) return -1;
    SnapCam cam; cam.dist=m.computeBBox().diagonal()*2.2;
    Image img=renderSnapshot(m,w,h,cam);
    return writePNG(path,img)?0:-1;
}

char* ark_doc_json(ArkDoc* d){
    if(!d) return nullptr;
    auto J=Value::makeObj();
    Value ps=Value::makeArr();
    for(auto&p:d->doc.params){
        Value v=Value::makeObj();
        v.set("name",Value::makeStr(p.name));
        v.set("expr",Value::makeStr(p.expr));
        v.set("value",Value::makeNum(p.value));
        ps.arr.push_back(std::move(v));
    }
    J.set("params",std::move(ps));

    Value sks=Value::makeArr();
    for(auto&ns:d->doc.sketches){
        Value v=Value::makeObj();
        v.set("name",Value::makeStr(ns.name));
        v.set("residual",Value::makeNum(ns.residual));
        v.set("entities",Value::makeNum((double)ns.sk.ents.size()));
        v.set("constraints",Value::makeNum((double)ns.sk.cons.size()));
        sks.arr.push_back(std::move(v));
    }
    J.set("sketches",std::move(sks));

    Value fs=Value::makeArr();
    for(auto&f:d->doc.features){
        Value v=Value::makeObj();
        v.set("id",Value::makeNum((double)f.id));
        v.set("type",Value::makeStr(featureTypeName(f.type)));
        v.set("name",Value::makeStr(f.name));
        Value in=Value::makeArr();
        for(u64 id:f.inputs) in.arr.push_back(Value::makeNum((double)id));
        v.set("inputs",std::move(in));
        fs.arr.push_back(std::move(v));
    }
    J.set("features",std::move(fs));
    J.set("bodies",Value::makeNum((double)d->doc.evalBodies.size()));
    J.set("recomputeMs",Value::makeNum(d->doc.recomputeMs));

    std::string s=J.dump();
    char* out=(char*)std::malloc(s.size()+1);
    if(!out) return nullptr;
    std::memcpy(out,s.c_str(),s.size()+1);
    return out;
}

int ark_doc_save(ArkDoc* d,const char* path){
    return (d&&saveDocument(d->doc,path))?0:-1;
}
int ark_doc_load(ArkDoc* d,const char* path){
    if(!d) return -1;
    std::string err;
    if(!loadDocument(path,d->doc,&err)){ d->lastErr=err; return -2; }
    d->doc.recompute();
    return 0;
}

void ark_free(char* p){ std::free(p); }

double ark_eval(const char* expr){
    if(!expr) return 0;
    bool ok=false;
    return evalExpr(expr,ExprEnv{},&ok);
}

} // extern "C"
