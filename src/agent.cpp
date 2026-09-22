#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/io.hpp"
#include <stdexcept>
#include <set>
#include <fstream>
namespace ark {
using V=arkjson::Value;
namespace {
const V& field(const V& a,const char* k){auto p=a.find(k);if(!p)throw std::runtime_error(std::string("Missing field: ")+k);return *p;}
std::string str(const V& a,const char* k){auto& p=field(a,k);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Expected nonempty string: ")+k);return p.str;}
u64 id(const V& v){if(v.t!=V::Num||!std::isfinite(v.num)||v.num<1||v.num>9007199254740991.||v.num!=std::floor(v.num))throw std::runtime_error("Expected positive integer id");return (u64)v.num;}
std::string expression(const V& v){if(v.t==V::Str && v.str.size()<=2048)return v.str;if(v.t==V::Num&&std::isfinite(v.num))return v.dump();throw std::runtime_error("Expected expression string or finite number");}
void params(Feature& f,const V& p){if(p.t!=V::Obj)throw std::runtime_error("p must be an object");for(auto& kv:p.obj)f.p[kv.first]=expression(kv.second);}
V result(V data,bool error=false){V r=V::makeObj(),content=V::makeArr(),item=V::makeObj();item.set("type",V::makeStr("text"));item.set("text",V::makeStr(data.dump()));content.arr.push_back(item);r.set("content",content);r.set("structuredContent",data);r.set("isError",V::makeBool(error));return r;}
void validated(Document& d){
    if(d.features.size()>1024||d.params.size()>256||d.sketches.size()>64)throw std::runtime_error("Document capacity exceeded");
    d.recompute();
    if(!d.parameterError.empty())throw std::runtime_error(d.parameterError);
    for(auto& r:d.lastResults)if(!r.ok)throw std::runtime_error("Feature #"+std::to_string(r.id)+": "+r.error);
    for(auto& sk:d.sketches)if(!std::isfinite(sk.residual)||sk.residual>1e-6)throw std::runtime_error("Sketch did not converge: "+sk.name);
}
std::set<u64> consumed(const Document& d){std::set<u64> out;for(auto& f:d.features)for(auto x:f.inputs)out.insert(x);return out;}
Mesh selectedMesh(Document& d,const V& args){
    Mesh m;
    u64 selected=args.find("id")?id(*args.find("id")):0;
    auto hidden=consumed(d);
    for(auto& b:d.evalBodies)if(selected?b.id==selected:!hidden.count(b.id))m.append(b.tessellate(d.tessOpt));
    if(!m.triCount())throw std::runtime_error("No mesh for requested selection");
    return m;
}
}
AgentSession::Cache& AgentSession::meshCache(const V& args){
    if(cacheRevision_!=revision){cache_.clear();cacheRevision_=revision;}
    u64 key=args.find("id")?id(*args.find("id")):0;
    auto it=cache_.find(key);if(it!=cache_.end())return it->second;
    Cache c;c.mesh=selectedMesh(document,args);c.report=analyzeMesh(c.mesh);c.bvh=std::make_unique<MeshBVH>(c.mesh);
    return cache_.emplace(key,std::move(c)).first->second;
}
void AgentSession::remember(){undo_.push_back(documentToJson(document));if(undo_.size()>32)undo_.erase(undo_.begin());redo_.clear();}
V AgentSession::info() const {
    V r=V::makeObj();r.set("revision",V::makeNum((double)revision));r.set("units",V::makeStr("mm"));
    r.set("feature_count",V::makeNum(document.features.size()));r.set("recompute_ms",V::makeNum(document.recomputeMs));
    r.set("parameter_error",V::makeStr(document.parameterError));
    V fs=V::makeArr();auto hidden=consumed(document);
    for(auto& f:document.features){V v=V::makeObj();v.set("id",V::makeNum((double)f.id));v.set("name",V::makeStr(f.name));v.set("type",V::makeStr(featureTypeName(f.type)));v.set("output",V::makeBool(!hidden.count(f.id)));fs.arr.push_back(v);}
    r.set("features",fs);return r;
}
V AgentSession::call(const std::string& tool,const V& args){
    try{
        if(args.t!=V::Obj)throw std::runtime_error("Arguments must be an object");
        if(tool=="doc_info")return result(info());
        if(tool=="doc_get"){V r=info(),j;arkjson::parse(documentToJson(document),j);r.set("document",j);return result(r);}
        if(tool=="eval_expr"){
            bool ok=false;auto v=evalExpr(str(args,"expr"),document.buildEnv(),&ok);
            if(!ok)throw std::runtime_error("Invalid mathematical expression");
            V r=V::makeObj();r.set("value",V::makeNum(v));return result(r);
        }
        if(tool=="doc_apply"){
            if(auto expected=args.find("expected_revision")){
                if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; read doc_get and retry");
            }
            const auto& commands=field(args,"commands");
            if(commands.t!=V::Arr||commands.arr.empty()||commands.arr.size()>128)throw std::runtime_error("commands must contain 1..128 operations");
            Document next;std::string error;
            if(!documentFromJson(documentToJson(document),next,&error))throw std::runtime_error(error);
            V created=V::makeArr();
            for(auto& c:commands.arr){
                auto op=str(c,"op");
                if(op=="param.set")next.setParam(str(c,"name"),expression(field(c,"expr")));
                else if(op=="feature.add"){
                    Feature f;auto type=str(c,"type");
                    if(!featureTypeFromName(type,f.type))throw std::runtime_error("Unknown feature type: "+type);
                    f.name=c.find("name")?str(c,"name"):type;
                    if(auto p=c.find("p"))params(f,*p);
                    if(auto in=c.find("inputs")){if(in->t!=V::Arr)throw std::runtime_error("inputs must be an array");for(auto& x:in->arr)f.inputs.push_back(id(x));}
                    if(auto mode=c.find("mode")){if(mode->t!=V::Num||mode->num<0||mode->num>2||mode->num!=std::floor(mode->num))throw std::runtime_error("mode must be 0, 1 or 2");f.opInt=(int)mode->num;}
                    created.arr.push_back(V::makeNum((double)next.addFeature(std::move(f))));
                }
                else if(op=="feature.edit"){
                    auto* f=next.find(id(field(c,"id")));if(!f)throw std::runtime_error("Feature not found");
                    if(auto p=c.find("p"))params(*f,*p);
                    if(c.find("name"))f->name=str(c,"name");
                }
                else if(op=="feature.remove"){if(!next.remove(id(field(c,"id"))))throw std::runtime_error("Feature not found");}
                else if(op=="sketch.set"){
                    Sketch sk;if(!sketchFromJson(field(c,"sketch").dump(),sk,&error))throw std::runtime_error(error);
                    next.setSketch(str(c,"name"),std::move(sk));
                }
                else throw std::runtime_error("Unknown operation: "+op);
            }
            validated(next); // one evaluation for the entire batch, commit only on success
            remember();document=std::move(next);++revision;
            V r=info();r.set("created_ids",created);return result(r);
        }
        if(tool=="doc_undo"||tool=="doc_redo"){
            auto& source=tool=="doc_undo"?undo_:redo_;auto& destination=tool=="doc_undo"?redo_:undo_;
            if(source.empty())throw std::runtime_error("No history available");
            Document next;std::string error;if(!documentFromJson(source.back(),next,&error))throw std::runtime_error(error);
            validated(next);destination.push_back(documentToJson(document));source.pop_back();document=std::move(next);++revision;return result(info());
        }
        if(tool=="doc_save"){
            auto path=str(args,"path");if(!saveDocument(document,path.c_str()))throw std::runtime_error("Could not save document");
            V r=info();r.set("path",V::makeStr(path));return result(r);
        }
        if(tool=="doc_load"){
            Document next;std::string error;
            if(!loadDocument(str(args,"path").c_str(),next,&error))throw std::runtime_error(error);
            validated(next);remember();document=std::move(next);++revision;return result(info());
        }
        if(tool=="mesh_inspect")return result(meshCache(args).report.json());
        if(tool=="mesh_get"){
            auto& cached=meshCache(args);const Mesh& m=cached.mesh;V r=cached.report.json(),positions=V::makeArr(),indices=V::makeArr();
            // Explicit opt-in: inspection never returns large vertex buffers.
            for(auto x:m.pos)positions.arr.push_back(V::makeNum(x));
            for(auto x:m.idx)indices.arr.push_back(V::makeNum(x));
            r.set("positions",positions);r.set("indices",indices);return result(r);
        }
        if(tool=="mesh_raycast"){
            auto vector=[&](const char* key){auto& a=field(args,key);if(a.t!=V::Arr||a.arr.size()!=3)throw std::runtime_error("Ray vectors require three finite numbers");for(auto& x:a.arr)if(x.t!=V::Num||!std::isfinite(x.num)||std::fabs(x.num)>1e8)throw std::runtime_error("Invalid ray coordinate");return Vec3{a.arr[0].num,a.arr[1].num,a.arr[2].num};};
            Ray ray{vector("origin"),vector("direction")};if(ray.d.len()<1e-15)throw std::runtime_error("Ray direction cannot be zero");
            auto& c=meshCache(args);auto hit=c.bvh->raycast(c.mesh,ray);V r=V::makeObj();r.set("hit",V::makeBool(hit.hit));
            if(hit.hit){r.set("distance_mm",V::makeNum(hit.distance));r.set("triangle",V::makeNum(hit.triangle));
                auto arr=[](Vec3 p){V a=V::makeArr();for(auto x:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(x));return a;};r.set("point",arr(hit.point));r.set("normal",arr(hit.normal));}
            return result(r);
        }
        if(tool=="export_mesh"){
            auto path=str(args,"path"),format=str(args,"format");Mesh m=selectedMesh(document,args);bool ok=false;
            if(format=="stl")ok=exportSTL(path.c_str(),m);
            else if(format=="obj")ok=exportOBJ(path.c_str(),m);
            else throw std::runtime_error("format must be stl or obj");
            if(!ok)throw std::runtime_error("Export failed");
            V r=V::makeObj();r.set("path",V::makeStr(path));r.set("triangles",V::makeNum(m.triCount()));return result(r);
        }
        if(tool=="snapshot"){
            auto path=str(args,"path");Mesh m=selectedMesh(document,args);SnapCam camera;
            camera.dist=std::max(m.computeBBox().diagonal()*2.,1.);
            if(!writePNG(path.c_str(),renderSnapshot(m,1280,800,camera)))throw std::runtime_error("Snapshot failed");
            V r=V::makeObj();r.set("path",V::makeStr(path));return result(r);
        }
        throw std::runtime_error("Unknown tool: "+tool);
    }catch(const std::exception& e){V r=V::makeObj();r.set("error",V::makeStr(e.what()));r.set("revision",V::makeNum((double)revision));return result(r,true);}
}
V AgentSession::tools(){
    struct Def{const char* name;const char* desc;const char* schema;bool read;};
    const Def defs[]={
        {"doc_info","Compact document summary with revision and stable feature IDs.",R"({"type":"object"})",true},
        {"doc_get","Read full parametric document, including expressions and revision.",R"({"type":"object"})",true},
        {"doc_apply","Atomic batch; evaluate once, roll back on any failure. Operations: param.set{name,expr}, feature.add{type,name,p,inputs,mode}, feature.edit{id,p,name}, feature.remove{id}, sketch.set{name,sketch}. Feature types: box{dx,dy,dz}, cylinder{r,h}, sphere{r}, cone{rb,rt,h}, torus{R,r}, extrude{shape,w,d,height,sketch}, revolve{shape,w,d,angle,sketch}, transform{tx,ty,tz,rx,ry,rz}, boolean{mode:0 union/1 cut/2 intersect}, pattern{count,dx,dy,dz}, surface{x,y,z,u0,u1,v0,v1,nu,nv,wrap_u,wrap_v}. Geometry parameters belong in p and accept math expressions. Transforms/patterns take one input, booleans two. Use expected_revision to reject stale writes. Angles in degrees except trig expressions in radians. Surface coordinates use u,v; periodic seams are checked.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0},"commands":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"op":{"type":"string","enum":["param.set","feature.add","feature.edit","feature.remove","sketch.set"]}},"required":["op"]}}},"required":["commands"]})",false},
        {"doc_undo","Undo the previous committed transaction; revision increases.",R"({"type":"object"})",false},
        {"doc_redo","Redo a transaction after undo.",R"({"type":"object"})",false},
        {"eval_expr","Evaluate finite arithmetic and trigonometry with named parameters; trig uses radians, deg() converts degrees.",R"({"type":"object","properties":{"expr":{"type":"string"}},"required":["expr"]})",true},
        {"mesh_inspect","Compact indexed topology audit: bounds, area, edge counts, components and signed volume for closed oriented edges. Does not certify solids or detect self intersections. Omit id to inspect final outputs only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_get","Explicitly retrieve mesh positions and triangle indices; potentially large. Prefer mesh_inspect for reasoning.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1}}})",true},
        {"mesh_raycast","Nearest triangle hit using a cached AABB hierarchy. Direction is normalized; distances in mm. Triangle IDs last for this tessellation/revision only.",R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"direction":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["origin","direction"]})",true},
        {"doc_save","Save .ark or legacy .arkjson parametric document as versioned JSON.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"doc_load","Load .ark/.arkjson into this session, validate and commit atomically.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})",false},
        {"export_mesh","Export final output bodies or a selected id as STL/OBJ; construction inputs are excluded.",R"({"type":"object","properties":{"path":{"type":"string"},"format":{"type":"string","enum":["stl","obj"]},"id":{"type":"integer","minimum":1}},"required":["path","format"]})",false},
        {"snapshot","Render final output meshes to a 1280x800 PNG for visual feedback.",R"({"type":"object","properties":{"path":{"type":"string"},"id":{"type":"integer","minimum":1}},"required":["path"]})",false}
    };
    V list=V::makeArr();for(auto& d:defs){V t=V::makeObj(),schema,annotations=V::makeObj();t.set("name",V::makeStr(d.name));t.set("description",V::makeStr(d.desc));arkjson::parse(d.schema,schema);t.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(d.read));t.set("annotations",annotations);list.arr.push_back(t);}return list;
}
Document makeDemoDocument(){
    Document d;d.setParam("radius","32");d.setParam("tube","11");d.setParam("waves","7");d.setParam("amplitude","0.22");
    Feature f;f.type=FeatureType::ParametricSurface;f.name="Harmonic ring";
    std::string t="tube*(1+amplitude*cos(waves*u+3*v))";
    f.p={{"x","(radius+"+t+"*cos(v))*cos(u)"},{"y",t+"*sin(v)+15"},{"z","(radius+"+t+"*cos(v))*sin(u)"},{"nu","160"},{"nv","64"},{"wrap_u","1"},{"wrap_v","1"}};
    d.addFeature(f);d.recompute();return d;
}
}
