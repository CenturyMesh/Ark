#include "ark/agent_features.hpp"
#include "ark/wood_tools.hpp"
#include "ark/bom.hpp"
#include "ark/agent_activity.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/references.hpp"
#include "ark/sketch.hpp"
#include "ark/physics.hpp"
#include "ark/manufacturing.hpp"
#include "ark/component_library_tools.hpp"
#include "ark/result_path.hpp"
#include "ark/curve_pattern_044b.hpp"
#include <algorithm>
#include <chrono>
#include <vector>
#include <set>
#include <map>
#include <cctype>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
const V& required(const V& a,const char* key){auto p=a.find(key);if(!p)throw std::runtime_error(std::string("Missing field: ")+key);return *p;}
std::string text(const V& a,const char* key){const auto& p=required(a,key);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Expected text: ")+key);return p.str;}
double number(const V& a,const char* key,double fallback=0){auto p=a.find(key);if(!p)return fallback;if(p->t!=V::Num||!std::isfinite(p->num)||std::abs(p->num)>1e12)throw std::runtime_error(std::string("Expected finite number: ")+key);return p->num;}
V arr(Vec3 v){V a=V::makeArr();for(double n:{v.x,v.y,v.z})a.arr.push_back(V::makeNum(n));return a;}
Vec3 vector(const V& a){if(a.t!=V::Arr||a.arr.size()!=3)throw std::runtime_error("Expected three finite coordinates");for(const auto& n:a.arr)if(n.t!=V::Num||!std::isfinite(n.num)||std::abs(n.num)>1e12)throw std::runtime_error("Invalid vector coordinate");return {a.arr[0].num,a.arr[1].num,a.arr[2].num};}
Vec3 unit(Vec3 a){if(a.len()<1e-12)throw std::runtime_error("Direction is degenerate");return a.normalized();}
V matrix(const Mat4& m){V a=V::makeArr();for(double n:m.m)a.arr.push_back(V::makeNum(n));return a;}
Mat4 frame(const V& a){if(a.t!=V::Arr||a.arr.size()!=16)throw std::runtime_error("Frame needs 16 column-major numbers");Mat4 m;for(size_t i=0;i<16;++i){if(a.arr[i].t!=V::Num||!std::isfinite(a.arr[i].num))throw std::runtime_error("Invalid frame");m.m[i]=a.arr[i].num;}if(std::abs(m.m[3])+std::abs(m.m[7])+std::abs(m.m[11])+std::abs(m.m[15]-1)>1e-8)throw std::runtime_error("Frame must be affine");Vec3 x=m.transformDir({1,0,0}),y=m.transformDir({0,1,0}),z=m.transformDir({0,0,1});if(std::abs(x.len()-1)+std::abs(y.len()-1)+std::abs(z.len()-1)+std::abs(x.dot(y))+std::abs(x.dot(z))+std::abs(y.dot(z))+std::abs(x.cross(y).dot(z)-1)>1e-7)throw std::runtime_error("Frame must be rigid and right handed");return m;}
Mat4 axes(Vec3 origin,Vec3 z,Vec3 hint){z=unit(z);Vec3 x=unit(hint-z*hint.dot(z)),y=z.cross(x);Mat4 m=Mat4::identity();const double values[]{x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,origin.x,origin.y,origin.z,1};std::copy(std::begin(values),std::end(values),m.m);return m;}
V payload(const V& r){if(auto e=r.find("isError");e&&e->b){auto p=r.find("structuredContent");throw std::runtime_error(p&&p->find("error")?p->find("error")->str:"Tool failed");}return required(r,"structuredContent");}
u64 featureId(const Document& d,const V& v){u64 found=0;int count=0;for(const auto& f:d.features)if((v.t==V::Str&&f.name==v.str)||(v.t==V::Num&&v.num==(double)f.id)){found=f.id;++count;}if(count!=1)throw std::runtime_error(count?"Ambiguous feature":"Feature not found");return found;}
const AssemblyNode& node(const Document& d,const V& v){const AssemblyNode* found=nullptr;for(const auto& n:d.assembly)if((v.t==V::Str&&n.name==v.str)||(v.t==V::Num&&v.num==(double)n.id)){if(found)throw std::runtime_error("Ambiguous instance");found=&n;}if(!found)throw std::runtime_error("Instance not found");return *found;}
void expected(const AgentSession& s,const V& args){const auto& v=required(args,"expected_revision");if(v.t!=V::Num||v.num!=(double)s.revision)throw std::runtime_error("Revision conflict; read doc_info with detail design and retry");}
V applyArgs(u64 revision,const V& commands){V a=V::makeObj();a.set("expected_revision",V::makeNum((double)revision));a.set("commands",commands);return a;}
void copySession(const AgentSession& s,AgentSession& out){out.document=s.document;out.document.iterationHistory=V{};out.suppressIterationHistory();out.document.evaluatingBodies=nullptr;out.revision=s.revision;}
V geometry(AgentSession& s,const V& args){
    for(const auto& field:args.obj)if(field.first!="id"&&field.first!="queries"&&field.first!="expected_revision")throw std::runtime_error("Unknown geometry_query field: "+field.first+"; put reference filters inside queries[].filter");
    u64 id=featureId(s.document,required(args,"id"));ensureNativeBody(s.document,id,s.document.evalBodies);
    const Body* body=nullptr;for(const auto& b:s.document.evalBodies)if(b.id==id)body=&b;
    if(!body||!body->nativeData.find("brep_base64"))throw std::runtime_error("Query requires exact B-rep geometry");
    V request=V::makeObj(),model=V::makeObj();model.set("brep",*body->nativeData.find("brep_base64"));if(auto topology=body->nativeData.find("topology"))model.set("topology",*topology);model.set("queries",required(args,"queries"));request.set("operation",V::makeStr("geometry_query"));request.set("request",model);V out=nativeWorkerRequest(request);out.set("revision",V::makeNum((double)s.revision));out.set("id",V::makeNum((double)id));return out;
}
V math(const V& args){
    V r=V::makeObj();std::string op=text(args,"op");
    if(op=="convert"){
        const std::map<std::string,std::pair<int,double>> units{{"mm",{0,1}},{"cm",{0,10}},{"m",{0,1000}},{"in",{0,25.4}},{"deg",{1,K_PI/180}},{"rad",{1,1}},{"g",{2,1}},{"kg",{2,1000}}};
        auto from=units.find(text(args,"from")),to=units.find(text(args,"to"));if(from==units.end()||to==units.end()||from->second.first!=to->second.first)throw std::runtime_error("Incompatible or unsupported units");required(args,"value");r.set("value",V::makeNum(number(args,"value")*from->second.second/to->second.second));r.set("unit",to==units.end()?V{}:V::makeStr(to->first));return r;
    }
    if(op=="frame"){r.set("frame",matrix(axes(vector(required(args,"origin")),vector(required(args,"z")),vector(required(args,"x_hint")))));return r;}
    Vec3 a=vector(required(args,"a"));
    if(op=="normalize")r.set("value",arr(unit(a)));
    else if(op=="norm")r.set("value",V::makeNum(a.len()));
    else if(op=="transform_point"||op=="transform_direction"){Mat4 from=frame(required(args,"from_frame")),to=frame(required(args,"to_frame"));auto m=to.inverted()*from;r.set("value",arr(op=="transform_point"?m.transformPoint(a):m.transformDir(a)));}
    else if(op=="project_plane"||op=="line_plane"){
        Vec3 origin=vector(required(args,"origin")),normal=unit(vector(required(args,"normal")));
        if(op=="project_plane")r.set("value",arr(a-normal*(a-origin).dot(normal)));
        else{Vec3 direction=unit(vector(required(args,"direction")));double divisor=direction.dot(normal);if(std::abs(divisor)<1e-12)throw std::runtime_error("Line is parallel to plane; no unique intersection");double distance=(origin-a).dot(normal)/divisor;r.set("value",arr(a+direction*distance));r.set("signed_distance",V::makeNum(distance));}
    }else{
        Vec3 b=vector(required(args,"b"));if(op=="add")r.set("value",arr(a+b));else if(op=="subtract")r.set("value",arr(a-b));else if(op=="cross")r.set("value",arr(a.cross(b)));else if(op=="dot")r.set("value",V::makeNum(a.dot(b)));else if(op=="distance")r.set("value",V::makeNum((a-b).len()));else if(op=="angle")r.set("value",V::makeNum(std::acos(std::clamp(unit(a).dot(unit(b)),-1.,1.))*180/K_PI));else throw std::runtime_error("Unknown math operation");
    }
    r.set("units",V::makeStr(op=="angle"?"deg":op=="normalize"?"dimensionless":op=="dot"||op=="cross"?"caller_coordinates_squared":"caller_coordinates"));return r;
}
const std::set<std::string> queryTools{"context_graph","explode_info","hud_info","object_context","sketch_repair","feature_history","assembly_authoring_info","holes_catalog","cad_inspect","drawing_inspect","collision_inspect","skeleton_inspect","skeleton_compile","reference_navigate","library_get","library_search","library_info","library_sources","library_vendors","activity_info","bom_inspect","fastener_recipe","manufacturing_inspect","physics_inspect","materials_inspect","wood_inspect","doc_info","eval_expr","plane_inspect","sketch_inspect","sketch_diagnose","reference_inspect","native_inspect","assembly_info","assembly_motion","motion_info","geometry_query","math_eval","mesh_inspect","frame_inspect","electronics_inspect","sheet_inspect","suspension_inspect","tube_inspect"};
bool batchTool(const std::string& name){return queryTools.count(name)||name=="agent_onboard";}
// 042D: bounded dataflow inside one batch. {"$ref":"id.path"} in a query's args
// takes an earlier row's value; rows still run once, in order, at one revision.
bool batchReference(const V& value){return value.t==V::Obj&&value.obj.size()==1&&value.obj.front().first=="$ref";}
void batchReferences(const V& value,std::vector<std::string>& out){
    if(batchReference(value)){out.push_back(value.obj.front().second.t==V::Str?value.obj.front().second.str:std::string());return;}
    if(value.t==V::Arr)for(const auto& item:value.arr)batchReferences(item,out);else if(value.t==V::Obj)for(const auto& item:value.obj)batchReferences(item.second,out);
}
bool batchIdentifier(const std::string& id){return !id.empty()&&id.size()<=32&&std::all_of(id.begin(),id.end(),[](char c){return std::isalnum((unsigned char)c)||c=='_'||c=='-';});}
std::pair<std::string,std::string> batchTarget(const std::string& reference){const auto dot=reference.find('.');return {reference.substr(0,dot),dot==std::string::npos?std::string():reference.substr(dot+1)};}
// Resolves every reference against earlier rows' full results; false names the first unresolved one.
bool resolveBatchReferences(V& value,const std::map<std::string,V>& outputs,std::string& failure){
    if(batchReference(value)){
        const auto [id,path]=batchTarget(value.obj.front().second.str);const auto& source=outputs.at(id);
        if(path.empty()){value=source;return true;}
        V resolved;std::string where;if(!selectResultPath(source,path,resolved,where)){failure=value.obj.front().second.str+" is absent at "+where;return false;}
        if(resolved.dump().size()>64*1024){failure=value.obj.front().second.str+" resolves to more than 64 KiB; select fewer values";return false;}
        value=std::move(resolved);return true;
    }
    if(value.t==V::Arr){for(auto& item:value.arr)if(!resolveBatchReferences(item,outputs,failure))return false;}
    else if(value.t==V::Obj){for(auto& item:value.obj)if(!resolveBatchReferences(item.second,outputs,failure))return false;}
    return true;
}
void validateBatchQuery(const V& query,size_t index){
    const std::string prefix="query_batch queries["+std::to_string(index)+"]";
    if(query.t!=V::Obj)throw std::runtime_error(prefix+" must be an object");
    for(const auto& field:query.obj)if(field.first!="tool"&&field.first!="args"&&field.first!="fields"&&field.first!="id"&&field.first!="paths"&&field.first!="max_bytes")throw std::runtime_error(prefix+" unknown field: "+field.first+"; accepted: tool, args, fields, id, paths, max_bytes");
    if(const auto* id=query.find("id");id&&(id->t!=V::Str||!batchIdentifier(id->str)))throw std::runtime_error(prefix+" id must be 1..32 letters, digits, '_' or '-'");
    if(query.find("fields")&&query.find("paths"))throw std::runtime_error(prefix+" accepts fields or paths, not both");
    if(const auto* paths=query.find("paths")){
        if(paths->t!=V::Arr||paths->arr.empty()||paths->arr.size()>32)throw std::runtime_error(prefix+" paths requires 1..32 distinct result paths");
        std::set<std::string> seen;for(const auto& path:paths->arr){if(path.t!=V::Str||!seen.insert(path.str).second)throw std::runtime_error(prefix+" paths requires distinct strings");try{validateResultPath(path.str);}catch(const std::exception& error){throw std::runtime_error(prefix+" "+error.what());}}
    }
    if(const auto* cap=query.find("max_bytes");cap&&(cap->t!=V::Num||!std::isfinite(cap->num)||std::floor(cap->num)!=cap->num||cap->num<256||cap->num>262144))throw std::runtime_error(prefix+" max_bytes must be an integer from 256 to 262144");
    const auto name=text(query,"tool");if(!batchTool(name))throw std::runtime_error(prefix+" tool is not allowed: "+name+"; use the query_batch tool enum; mutation, export and recursive batches are excluded");
    const auto& args=required(query,"args");if(args.t!=V::Obj)throw std::runtime_error(prefix+" args must be an object");
    if(name=="library_vendors"&&(!args.find("operation")||args.find("operation")->asStr()!="list"))throw std::runtime_error(prefix+" library_vendors requires operation:list; policy updates cannot be batched reads");
    if(name=="agent_onboard"){const auto* detail=args.find("detail");if(!detail||detail->t!=V::Str||detail->str!="guide")throw std::runtime_error(prefix+" agent_onboard requires explicit detail:guide; document inventory is excluded from batched onboarding");}
    if(const auto* fields=query.find("fields")){
        if(fields->t!=V::Arr||fields->arr.empty()||fields->arr.size()>32)throw std::runtime_error(prefix+" fields requires 1..32 distinct top-level response names");
        std::set<std::string> seen;for(const auto& field:fields->arr)if(field.t!=V::Str||field.str.empty()||!seen.insert(field.str).second)throw std::runtime_error(prefix+" fields requires distinct nonempty strings; nested paths are not accepted");
    }
}
void batchContent(V& row,const V& data){
    row.set("structuredContent",data);V content=V::makeArr(),item=V::makeObj();item.set("type",V::makeStr("text"));item.set("text",V::makeStr(data.dump()));content.arr.push_back(item);row.set("content",content);
}
// Guessed projection names usually share a word with the real field
// (length_mm -> route_length_mm, layers -> layer_count). Suggestions are hints
// only; the row stays rejected until the caller requests real fields.
V suggestedFields(const std::string& missing,const V& result){
    auto words=[](const std::string& name){std::vector<std::string> out;std::string word;for(char c:name+"_"){if(c=='_'){if(word.size()>=3&&word!="mm"&&word!="deg"&&word!="min"&&word!="max")out.push_back(word);word.clear();}else word+=(char)std::tolower((unsigned char)c);}return out;};
    static const std::map<std::string,std::string> synonyms={{"separation","distance"},{"clearance","distance"},{"gap","distance"},{"centre","center"},{"centroid","center"},{"weight","mass"},{"result","value"},{"output","value"}};
    auto stem=[](const std::string& a,const std::string& b){const size_t n=std::min(a.size(),b.size());return n>=4&&a.compare(0,n,b,0,n)==0;};
    V out=V::makeArr();
    for(const auto& field:result.obj){bool match=false;
        for(auto wanted:words(missing)){if(const auto alias=synonyms.find(wanted);alias!=synonyms.end())wanted=alias->second;for(const auto& candidate:words(field.first))if(stem(wanted,candidate))match=true;}
        if(match&&out.arr.size()<4)out.arr.push_back(V::makeStr(field.first));}
    return out;
}
void projectBatchRow(V& row,const V& query,size_t index){
    const auto* fields=query.find("fields");if(!fields||required(row,"isError").b)return;
    const V full=required(row,"structuredContent");V compact=V::makeObj(),missing=V::makeArr();
    // "structuredContent" names the whole tool payload, not a field inside it.
    if(std::any_of(fields->arr.begin(),fields->arr.end(),[](const V& key){return key.t==V::Str&&key.str=="structuredContent";}))return;
    for(const auto& key:fields->arr){if(const auto* value=full.find(key.str.c_str()))compact.set(key.str,*value);else missing.arr.push_back(key);}
    if(missing.arr.empty()){batchContent(row,compact);return;}
    V available=V::makeArr();std::string names;for(const auto& field:full.obj){available.arr.push_back(V::makeStr(field.first));if(!names.empty())names+=", ";names+=field.first;}
    V suggestions=V::makeObj();std::string hint;for(const auto& key:missing.arr){auto candidates=suggestedFields(key.str,full);if(candidates.arr.empty())continue;std::string list;for(const auto& c:candidates.arr)list+=(list.empty()?"":"|")+c.str;hint+=(hint.empty()?"; did you mean ":", ")+key.str+" -> "+list;suggestions.set(key.str,candidates);}
    V error=V::makeObj(),recovery=V::makeObj();error.set("error_code",V::makeStr("QUERY_BATCH_FIELD_MISSING"));error.set("error",V::makeStr("Requested result field is absent: "+missing.arr.front().str+hint+"; available fields: "+names));if(!suggestions.obj.empty())error.set("suggested_fields",suggestions);error.set("query_index",V::makeNum((double)index));error.set("tool",required(query,"tool"));error.set("requested_fields",*fields);error.set("missing_fields",missing);error.set("available_fields",available);error.set("tool_succeeded",V::makeBool(true));error.set("observed_result",full);
    recovery.set("action",V::makeStr("reuse_observed_result"));recovery.set("hint",V::makeStr("The tool already ran once. Read observed_result and its actual available_fields; the other successful queries need no repeat. To record this evidence (and clear a retained team error), call only the tool/args below once with valid fields or without a fields projection."));recovery.set("tool",required(query,"tool"));recovery.set("args",required(query,"args"));error.set("recovery",recovery);
    row.set("isError",V::makeBool(true));batchContent(row,error);
}
V batchRecovery(const V& query,const char* hint){V recovery=V::makeObj();recovery.set("action",V::makeStr("narrow_paths"));recovery.set("hint",V::makeStr(hint));recovery.set("tool",required(query,"tool"));recovery.set("args",required(query,"args"));return recovery;}
// paths keep only the selected values, keyed by path, plus the observed revision.
void projectBatchPaths(V& row,const V& query,size_t index){
    const auto* paths=query.find("paths");if(!paths||required(row,"isError").b)return;
    const V full=required(row,"structuredContent");V compact=V::makeObj();
    for(const auto& path:paths->arr){V value;std::string where;
        if(!selectResultPath(full,path.str,value,where)){
            V error=V::makeObj(),available=V::makeArr();for(const auto& field:full.obj)available.arr.push_back(V::makeStr(field.first));
            error.set("error_code",V::makeStr("QUERY_BATCH_PATH_MISSING"));error.set("error",V::makeStr("Requested result path is absent: "+path.str+" at "+where));error.set("query_index",V::makeNum((double)index));error.set("tool",required(query,"tool"));error.set("requested_paths",*paths);error.set("available_fields",available);error.set("tool_succeeded",V::makeBool(true));
            error.set("recovery",batchRecovery(query,"The tool already ran. Choose paths from available_fields, or run the tool/args below without a projection."));
            row.set("isError",V::makeBool(true));batchContent(row,error);return;
        }
        compact.set(path.str,value);
    }
    if(const auto* revision=full.find("revision");revision&&!compact.find("revision"))compact.set("revision",*revision);
    batchContent(row,compact);
}
// max_bytes bounds what the row may return; above it the row reports its shape instead of its data.
void capBatchRow(V& row,const V& query,size_t index){
    const auto* cap=query.find("max_bytes");if(!cap||required(row,"isError").b)return;
    const V& data=required(row,"structuredContent");const auto bytes=data.dump().size();if(bytes<=(size_t)cap->num)return;
    V keys=V::makeObj();for(const auto& field:data.obj){V shape=V::makeObj();const auto& value=field.second;shape.set("type",V::makeStr(value.t==V::Obj?"object":value.t==V::Arr?"array":value.t==V::Str?"string":value.t==V::Num?"number":value.t==V::Bool?"boolean":"null"));if(value.t==V::Arr)shape.set("count",V::makeNum((double)value.arr.size()));shape.set("bytes",V::makeNum((double)value.dump().size()));
        // One level of child names, so a narrower path is chosen without guessing.
        if(value.t==V::Obj){V names=V::makeArr();for(const auto& child:value.obj){if(names.arr.size()==32){shape.set("keys_truncated",V::makeBool(true));break;}names.arr.push_back(V::makeStr(child.first));}shape.set("keys",names);}
        keys.set(field.first,shape);}
    V error=V::makeObj();error.set("error_code",V::makeStr("QUERY_BATCH_ROW_TOO_LARGE"));error.set("error",V::makeStr("Result is "+std::to_string(bytes)+" bytes, above max_bytes "+std::to_string((size_t)cap->num)+"; select the needed values with paths."));error.set("query_index",V::makeNum((double)index));error.set("tool",required(query,"tool"));error.set("tool_succeeded",V::makeBool(true));error.set("bytes",V::makeNum((double)bytes));error.set("max_bytes",*cap);error.set("result_keys",keys);
    if(const auto* revision=data.find("revision"))error.set("revision",*revision);
    error.set("recovery",batchRecovery(query,"The tool already ran. Request only the values you need with paths, or raise max_bytes."));
    row.set("isError",V::makeBool(true));batchContent(row,error);
}
V queryValue(AgentSession& s,const V& spec){
    const auto tool=text(spec,"tool");if(!queryTools.count(tool))throw std::runtime_error("Condition requires an allowed read-only query");
    V data=payload(s.call(tool,required(spec,"args")));const std::string path=text(spec,"path");const V* value=&data;size_t start=0;
    while(start<path.size()){
        size_t end=path.find('.',start);std::string key=path.substr(start,end==std::string::npos?end:end-start);
        if(value->t==V::Arr){if(key.empty()||key.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid result array index");size_t index=std::stoull(key);if(index>=value->arr.size())throw std::runtime_error("Result index out of range");value=&value->arr[index];}
        else{const V* parent=value;value=value->find(key.c_str());if(!value){std::string available;for(const auto& field:parent->obj){if(!available.empty())available+=", ";available+=field.first;}throw std::runtime_error("Result path not found: "+path+"; available fields at this level: "+available);}}
        if(end==std::string::npos)break;start=end+1;
    }
    return *value;
}
double metric(AgentSession& s,const V& spec){V v=queryValue(s,spec);if(v.t!=V::Num||!std::isfinite(v.num))throw std::runtime_error("Objective must return a finite scalar");return v.num;}
V preview(AgentSession& s,const V& args){return s.preparePreview(args);}
Mat4 endpoint(AgentSession& s,const V& end){
    int selectors=0;for(const char* key:{"node","feature","plane","frame"})selectors+=end.find(key)?1:0;if(selectors!=1)throw std::runtime_error("Endpoint requires exactly one node, feature, plane or frame");
    if(auto explicitFrame=end.find("frame"))return frame(*explicitFrame);
    if(end.find("plane"))return resolvePlane(s.document,text(end,"plane"));
    u64 id=0;Mat4 placement=Mat4::identity();
    if(auto n=end.find("node")){const auto& selected=node(s.document,*n);id=selected.feature;std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(s.document,placements,error))throw std::runtime_error(error);bool found=false;double copy=number(end,"copy",0);if(copy!=std::floor(copy)||copy<0||copy>31)throw std::runtime_error("Invalid pattern copy");for(const auto& p:placements)if(p.node==selected.id&&p.copy==(int)copy){id=p.feature;placement=p.world;found=true;break;}if(!found)throw std::runtime_error("Placed instance not found");}
    else id=featureId(s.document,required(end,"feature"));
    std::string ref=end.find("reference")?text(end,"reference"):"origin";Mat4 local=ref=="origin"?Mat4::identity():resolveGeometryReference(s.document,id,ref,number(end,"fraction",.5)).frame;
    if(const V* center=end.find("center")){if(center->t!=V::Bool)throw std::runtime_error("center must be boolean");if(center->b){V q=V::makeObj(),qs=V::makeArr(),a=V::makeObj();q.set("action",V::makeStr("properties"));if(ref!="origin")q.set("reference",V::makeStr(ref));qs.arr.push_back(q);a.set("id",V::makeNum((double)id));a.set("queries",qs);V report=geometry(s,a);const V& row=required(report,"results").arr[0];if(!required(row,"ok").b)throw std::runtime_error(text(row,"error"));const V* p=row.find("center_mm");if(!p)p=row.find("area_center_mm");if(!p)throw std::runtime_error("Reference has no area or volume centroid");Vec3 origin=vector(*p);local.m[12]=origin.x;local.m[13]=origin.y;local.m[14]=origin.z;}}
    return placement*local;
}
V placementParameters(const Mat4& m){
    // Assembly's convention is Rz * Ry * Rx; solve the gimbal-lock case explicitly.
    double ry=std::asin(std::clamp(-m.m[2],-1.,1.)),rx=0,rz=0;
    if(std::abs(std::cos(ry))>1e-9){rx=std::atan2(m.m[6],m.m[10]);rz=std::atan2(m.m[1],m.m[0]);}else rz=std::atan2(-m.m[4],m.m[5]);
    V p=V::makeObj();for(auto kv:std::vector<std::pair<const char*,double>>{{"tx",m.m[12]},{"ty",m.m[13]},{"tz",m.m[14]},{"rx",rx*180/K_PI},{"ry",ry*180/K_PI},{"rz",rz*180/K_PI}})p.set(kv.first,V::makeNum(kv.second));return p;
}
V placement(AgentSession& s,const V& args){
    expected(s,args);const auto& selected=node(s.document,required(args,"node"));if(selected.fixed)throw std::runtime_error("Cannot place a grounded instance");for(const auto& mate:s.document.mates)if(mate.a==selected.id||mate.b==selected.id)throw std::runtime_error("Edit mates for a constrained instance");
    Mat4 parentWorld=Mat4::identity();
    if(selected.parent){
        if(!s.document.mates.empty())throw std::runtime_error("Nested placement with mates must be edited through its constraints");
        std::vector<const AssemblyNode*> ancestors;u64 id=selected.parent;
        while(id){if(ancestors.size()>=64)throw std::runtime_error("Assembly parent cycle or depth limit");const auto& parent=node(s.document,V::makeNum((double)id));ancestors.push_back(&parent);id=parent.parent;}
        const auto env=s.document.buildEnv();
        for(auto it=ancestors.rbegin();it!=ancestors.rend();++it){auto value=[&](const char* key){auto expr=(*it)->p.find(key);if(expr==(*it)->p.end())return 0.;bool ok=false;double v=evalExpr(expr->second,env,&ok);if(!ok)throw std::runtime_error("Invalid parent placement expression");return v;};parentWorld=parentWorld*Mat4::translation({value("tx"),value("ty"),value("tz")})*Mat4::rotZ(deg2rad(value("rz")))*Mat4::rotY(deg2rad(value("ry")))*Mat4::rotX(deg2rad(value("rx")));}
    }
    V source=V::makeObj();source.set("node",V::makeNum((double)selected.id));source.set("reference",args.find("reference")?*args.find("reference"):V::makeStr("origin"));
    std::string mode=args.find("mode")?text(args,"mode"):"align";
    V targetSpec=required(args,"target");if(mode=="center"){source.set("center",V::makeBool(true));targetSpec.set("center",V::makeBool(true));}
    Mat4 from=endpoint(s,source),target=endpoint(s,targetSpec);Vec3 offset=args.find("offset")?vector(*args.find("offset")):Vec3{};Vec3 dest=target.transformPoint(offset);
    target.m[12]=dest.x;target.m[13]=dest.y;target.m[14]=dest.z;
    V origin=V::makeObj();origin.set("node",V::makeNum((double)selected.id));Mat4 current=endpoint(s,origin),placed;
    if(mode=="center"){placed=current;Vec3 delta=dest-Vec3{from.m[12],from.m[13],from.m[14]};placed.m[12]+=delta.x;placed.m[13]+=delta.y;placed.m[14]+=delta.z;}
    else if(mode=="align")placed=target*from.inverted()*current;
    else throw std::runtime_error("Placement mode must be center or align");
    V command=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("assembly.edit"));command.set("id",V::makeNum((double)selected.id));command.set("p",placementParameters(parentWorld.inverted()*placed));commands.arr.push_back(command);
    V out=preview(s,applyArgs(s.revision,commands));out.set("world_frame",matrix(placed));out.set("persistent_relation",V::makeBool(false));return out;
}
V pattern(AgentSession& s,const V& args){
    expected(s,args);std::string mode=args.find("mode")?text(args,"mode"):"instances";const bool solid=mode=="solid",pitched=solid&&args.find("pitch_mm");
    double count=number(args,"count",2);if(!pitched&&(count<2||count>(solid?2048:128)||count!=std::floor(count)))throw std::runtime_error(solid?"Solid pattern count must be 2..2048":"Pattern count must be 2..128");
    if(mode=="solid"||mode=="holes"){
        V command=V::makeObj(),commands=V::makeArr(),native=V::makeObj(),inputs=V::makeArr();
        command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("native_solid"));command.set("name",required(args,"name"));
        inputs.arr.push_back(V::makeNum((double)featureId(s.document,required(args,"feature"))));inputs.arr.push_back(V::makeNum((double)featureId(s.document,required(args,"path"))));if(solid&&args.find("support"))inputs.arr.push_back(V::makeNum((double)featureId(s.document,required(args,"support"))));command.set("inputs",inputs);
        native.set("op",V::makeStr(mode=="solid"?"curve_pattern":"hole_pattern"));if(!solid||args.find("reference"))native.set("reference",required(args,"reference"));if(!pitched)native.set("count",V::makeNum(count));native.set("margin_mm",V::makeNum(number(args,"margin_mm",0)));if(solid)curvePatternAgentFields044B(args,native);
        for(const char* key:{"offset","x_hint"})if(auto value=args.find(key))native.set(key,*value);
        if(mode=="holes")for(const char* key:{"normal","radius","depth"})native.set(key,required(args,key));
        command.set("native",native);commands.arr.push_back(command);V out=preview(s,applyArgs(s.revision,commands));out.set("path_linked",V::makeBool(true));out.set("spacing",V::makeStr("arc_length"));if(solid){Feature probe;probe.type=FeatureType::NativeSolid;probe.nativeSpec=native;probe.inputs.resize(inputs.arr.size());out.set("curve_pattern_044b",V::makeBool(curvePattern044BSpec(probe)));}return out;
    }
    V query=V::makeObj(),queries=V::makeArr(),queryArgs=V::makeObj();query.set("action",V::makeStr("curve"));query.set("reference",required(args,"reference"));query.set("count",V::makeNum(count));query.set("margin_mm",V::makeNum(number(args,"margin_mm",0)));queries.arr.push_back(query);queryArgs.set("id",required(args,"path"));queryArgs.set("queries",queries);
    V report=geometry(s,queryArgs);const auto& result=required(report,"results").arr[0];if(!required(result,"ok").b)throw std::runtime_error(text(result,"error"));
    V commands=V::makeArr();std::string prefix=text(args,"name");if(prefix.size()>96)throw std::runtime_error("Pattern name too long");int index=0;
    for(const auto& sample:required(result,"samples").arr){
        Vec3 point=vector(required(sample,"point_mm"));V command=V::makeObj();std::string name=prefix+"_"+std::to_string(++index);
        if(mode=="instances"){
            u64 id=featureId(s.document,required(args,"feature"));command.set("op",V::makeStr("assembly.add"));command.set("feature",V::makeNum((double)id));command.set("name",V::makeStr(name));Mat4 placed=Mat4::translation(point);
            if(const auto* hint=args.find("x_hint")){placed=axes(point,vector(required(sample,"tangent")),vector(*hint));}command.set("p",placementParameters(placed));
        }else if(mode=="points"){
            command.set("op",V::makeStr("plane.set"));command.set("kind",V::makeStr("point"));command.set("name",V::makeStr(name));V p=V::makeObj();p.set("ox",V::makeNum(point.x));p.set("oy",V::makeNum(point.y));p.set("oz",V::makeNum(point.z));command.set("p",p);
        }else throw std::runtime_error("Pattern mode must be instances or points");commands.arr.push_back(command);
    }
    V out=preview(s,applyArgs(s.revision,commands));out.set("spacing",V::makeStr("arc_length"));out.set("path_linked",V::makeBool(false));return out;
}
V solve(AgentSession& s,const V& args){
    expected(s,args);std::string parameter=text(args,"parameter");bool found=false;for(const auto& p:s.document.params)found|=p.name==parameter;if(!found)throw std::runtime_error("Parameter not found");
    const std::string mode=args.find("mode")?text(args,"mode"):"target";if(mode!="target"&&mode!="sensitivity")throw std::runtime_error("Solve mode must be target or sensitivity");if(mode=="target")required(args,"target");
    const double lower=number(args,"lower"),upper=number(args,"upper"),target=number(args,"target"),tolerance=number(args,"tolerance",1e-6);if(!(lower<upper)||tolerance<=0)throw std::runtime_error("Need ordered bounds and positive objective tolerance");
    double requestedBudget=number(args,"max_evaluations",32);if(requestedBudget<3||requestedBudget>64||requestedBudget!=std::floor(requestedBudget))throw std::runtime_error("Evaluation budget must be 3..64");int budget=(int)requestedBudget;
    double seconds=number(args,"max_seconds",30);if(seconds<=0||seconds>120)throw std::runtime_error("Time budget must be 0..120 seconds");auto start=std::chrono::steady_clock::now();V samples=V::makeArr();int evaluations=0;
    auto commands=[&](double x){V command=V::makeObj(),cs=V::makeArr();command.set("op",V::makeStr("param.set"));command.set("name",V::makeStr(parameter));command.set("expr",V::makeNum(x));cs.arr.push_back(command);return cs;};
    auto evaluate=[&](double x){if(evaluations>=budget||std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>seconds)throw std::runtime_error("Design solve resource budget exhausted");AgentSession candidate;copySession(s,candidate);payload(candidate.call("doc_apply",applyArgs(candidate.revision,commands(x))));double value=metric(candidate,required(args,"objective"));V row=V::makeObj();row.set("parameter",V::makeNum(x));row.set("value",V::makeNum(value));samples.arr.push_back(row);++evaluations;return value-target;};
    double a=lower,b=upper,fa=evaluate(a),fb=evaluate(b),x=std::abs(fa)<std::abs(fb)?a:b,residual=std::abs(fa)<std::abs(fb)?fa:fb;
    V out=V::makeObj();
    if(mode=="sensitivity"){out.set("secant_derivative",V::makeNum((fb-fa)/(upper-lower)));out.set("derivative_scope",V::makeStr("finite_interval_including_any_topology_changes"));}
    else if(mode=="target"){
        bool bracket=std::signbit(fa)!=std::signbit(fb);if(std::abs(residual)>tolerance&&!bracket)throw std::runtime_error("Target is not bracketed by bounds");
        while(std::abs(residual)>tolerance&&evaluations<budget&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds){x=a+(b-a)/2;residual=evaluate(x);if(std::signbit(residual)==std::signbit(fa)){a=x;fa=residual;}else{b=x;fb=residual;}if(b-a<1e-12*std::max(1.,std::abs(x)))break;}
        bool converged=std::abs(residual)<=tolerance;out.set("converged",V::makeBool(converged));out.set("residual",V::makeNum(residual));out.set("parameter_value",V::makeNum(x));if(converged)out.set("apply",applyArgs(s.revision,commands(x)));out.set("method",V::makeStr("bounded_bisection"));out.set("continuity_assumed",V::makeBool(true));
    }else throw std::runtime_error("Solve mode must be target or sensitivity");
    out.set("samples",samples);out.set("evaluations",V::makeNum(evaluations));out.set("revision",V::makeNum((double)s.revision));return out;
}
}
bool callAgentFeature(AgentSession& s,const std::string& tool,const V& args,V& out){
    if(tool=="activity_info")out=inspectAgentActivity(s.document,args);
    else if(tool=="tool_feedback"){V ratings=args;ratings.obj.erase(std::remove_if(ratings.obj.begin(),ratings.obj.end(),[](const auto& field){return field.first=="actor";}),ratings.obj.end());out=rateAgentActivity(s.document,ratings,args.find("actor")?*args.find("actor"):V{});out.set("revision",V::makeNum((double)s.revision));}
    else if(tool=="bom_review")out=s.reviewBom(args);
    else if(tool=="bom_inspect"){out=inspectBomPage(s.document,s.revision,args);s.observeBomInspection(out);}
    else if(tool=="bom_export")out=exportBom(s.document,s.revision,args);
    else if(tool=="bom_export_parts")out=exportBomParts(s,args);
    else if(tool=="reference_navigate"){
        for(const auto& entry:args.obj)if(entry.first!="id"&&entry.first!="reference"&&entry.first!="kind")throw std::runtime_error("Unknown reference navigation field");
        const auto reference=text(args,"reference"),kind=args.find("kind")?text(args,"kind"):"all";if(kind!="all"&&kind!="face"&&kind!="edge")throw std::runtime_error("Navigation kind must be all, face or edge");
        V query=V::makeObj(),filter=V::makeObj(),queries=V::makeArr(),request=V::makeObj();query.set("action",V::makeStr("references"));filter.set("adjacent_to",V::makeStr(reference));if(kind!="all")filter.set("kind",V::makeStr(kind));query.set("filter",filter);queries.arr.push_back(query);request.set("id",required(args,"id"));request.set("queries",queries);auto report=geometry(s,request);const auto& row=required(report,"results").arr.front();if(!required(row,"ok").b)throw std::runtime_error(text(row,"error"));out=V::makeObj();out.set("revision",V::makeNum((double)s.revision));out.set("id",required(report,"id"));out.set("reference",V::makeStr(reference));out.set("neighbors",required(row,"matches"));out.set("count",required(row,"count"));out.set("authority",V::makeStr("semantic_exact_topology"));
    }
    else if(tool=="selection_set"){
        expected(s,args);for(const auto& entry:args.obj)if(entry.first!="id"&&entry.first!="reference"&&entry.first!="instance"&&entry.first!="copy"&&entry.first!="expected_revision"&&entry.first!="plane")throw std::runtime_error("Unknown selection field");
        V selection=V::makeObj();
        if(args.find("plane")){
            if(args.find("id")||args.find("reference")||args.find("instance")||args.find("copy"))throw std::runtime_error("Plane selection cannot mix feature references");
            const auto name=text(args,"plane");resolvePlane(s.document,name);const DatumPlane* found=nullptr;for(const auto& datum:s.document.planes)if(datum.name==name)found=&datum;const bool builtin=!found&&(name=="Origin"||name=="XY"||name=="XZ"||name=="YZ");std::string kind=found?found->kind:name=="Origin"?"point":"plane";
            if(!builtin&&!found)throw std::runtime_error("Construction datum not found: "+name);
            selection.set("feature",V::makeNum(0));selection.set("reference",V::makeStr(name));selection.set("kind",V::makeStr(kind));selection.set("instance",V::makeNum(0));selection.set("copy",V::makeNum(0));selection.set("datum",V::makeBool(true));selection.set("builtin",V::makeBool(builtin));
        }else{
            if(!args.find("id")||!args.find("reference"))throw std::runtime_error("Select a persistent named face or edge, or a plane name");
            const u64 id=featureId(s.document,required(args,"id"));const auto reference=text(args,"reference");resolveGeometryReference(s.document,id,reference,.5);std::string kind;
            for(const auto& body:s.document.evalBodies)if(body.id==id)if(const auto* topology=body.nativeData.find("topology"))if(const auto* refs=topology->find("references"))for(const auto& row:refs->arr)if(text(row,"name")==reference)kind=text(row,"kind");
            if(kind!="edge"&&kind!="face")throw std::runtime_error("Select a persistent named face or edge");u64 instance=0;const double copy=number(args,"copy",0);if(copy<0||copy>31||copy!=std::floor(copy))throw std::runtime_error("Invalid occurrence copy");
            if(const auto* requested=args.find("instance")){const auto& occurrence=node(s.document,*requested);instance=occurrence.id;std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(s.document,placements,error))throw std::runtime_error(error);if(std::none_of(placements.begin(),placements.end(),[&](const AssemblyPlacement& p){return p.node==instance&&p.copy==(int)copy&&p.feature==id;}))throw std::runtime_error("Selected occurrence copy does not exist or does not own this feature");}else if(copy)throw std::runtime_error("Copy selection requires an occurrence");
            selection.set("feature",V::makeNum((double)id));selection.set("reference",V::makeStr(reference));selection.set("kind",V::makeStr(kind));selection.set("instance",V::makeNum((double)instance));selection.set("copy",V::makeNum(copy));
        }
        out=V::makeObj();out.set("selection",selection);out.set("revision",V::makeNum((double)s.revision));out.set("view_state_applied",V::makeBool(false));out.set("document_unchanged",V::makeBool(true));
    }
    else if(tool=="fastener_recipe")out=fastenerRecipe(s.document,s.revision,args);
    else if(tool=="manufacturing_propose")out=s.proposeManufacturing(args);
    else if(tool=="manufacturing_inspect")out=inspectManufacturing(s.document,args,s.revision);
    else if(tool=="physics_inspect")out=inspectPhysics(s,args);
    else if(tool=="materials_inspect")out=inspectMaterials(s.document);
    else if(tool=="wood_inspect"){for(const auto& field:args.obj)if(field.first!="expected_revision")throw std::runtime_error("Unknown wood_inspect field: "+field.first);if(const auto* expected=args.find("expected_revision");expected&&(expected->t!=V::Num||expected->num!=(double)s.revision))throw std::runtime_error("Revision conflict; inspect current document before reading wood cutlist");out=inspectWood(s.document,s.revision);}
    else if(tool=="geometry_query")out=geometry(s,args);
    else if(tool=="math_eval")out=math(args);
    else if(tool=="doc_preview")out=preview(s,args);
    else if(tool=="failure_info")out=s.failureInfo(args);
    else if(tool=="failure_export")out=s.failureExport(args);
    else if(tool=="failure_replay")out=s.failureReplay(args);
    else if(tool=="placement_plan")out=placement(s,args);
    else if(tool=="pattern_plan")out=pattern(s,args);
    else if(tool=="design_solve")out=solve(s,args);
    else if(tool=="query_batch"){
        for(const auto& field:args.obj)if(field.first!="queries")throw std::runtime_error("query_batch unknown field: "+field.first+"; accepted: queries");
        const auto& queries=required(args,"queries");if(queries.t!=V::Arr||queries.arr.empty()||queries.arr.size()>128)throw std::runtime_error("Batch needs 1..128 read-only queries");
        V rows=V::makeArr();std::set<std::string> ids;
        // Every structure, path and reference is checked before any row runs.
        for(size_t i=0;i<queries.arr.size();++i){const auto& query=queries.arr[i];validateBatchQuery(query,i);const std::string prefix="query_batch queries["+std::to_string(i)+"]";
            std::vector<std::string> references;batchReferences(required(query,"args"),references);
            for(const auto& reference:references){const auto [id,path]=batchTarget(reference);if(!ids.count(id))throw std::runtime_error(prefix+" $ref must name an earlier query id: "+reference);if(!path.empty())try{validateResultPath(path);}catch(const std::exception& error){throw std::runtime_error(prefix+" "+error.what());}}
            if(const auto* id=query.find("id");id&&!ids.insert(id->str).second)throw std::runtime_error(prefix+" id must be unique in the batch: "+id->str);}
        std::map<std::string,V> outputs;std::set<std::string> unavailable;
        size_t errors=0;for(size_t i=0;i<queries.arr.size();++i){const auto& query=queries.arr[i];auto start=std::chrono::steady_clock::now();const auto& submitted=required(query,"args");
            std::vector<std::string> references;batchReferences(submitted,references);std::string blocked,failure;for(const auto& reference:references)if(unavailable.count(batchTarget(reference).first)){blocked=batchTarget(reference).first;break;}
            V executed=submitted,row;
            if(!blocked.empty()){row=V::makeObj();row.set("isError",V::makeBool(true));V error=V::makeObj();error.set("error_code",V::makeStr("QUERY_BATCH_ROW_SKIPPED"));error.set("error",V::makeStr("Not run: query '"+blocked+"', which this query references, failed or was skipped."));error.set("skipped_due_to",V::makeStr(blocked));batchContent(row,error);}
            else if(!references.empty()&&!resolveBatchReferences(executed,outputs,failure)){row=V::makeObj();row.set("isError",V::makeBool(true));V error=V::makeObj();error.set("error_code",V::makeStr("QUERY_BATCH_REF_UNRESOLVED"));error.set("error",V::makeStr("Not run: "+failure));batchContent(row,error);}
            else row=s.call(text(query,"tool"),executed);
            // References read the full result, before this row's own projection.
            if(const auto* id=query.find("id")){if(required(row,"isError").b)unavailable.insert(id->str);else outputs[id->str]=required(row,"structuredContent");}
            projectBatchRow(row,query,i);projectBatchPaths(row,query,i);capBatchRow(row,query,i);
            row.set("query_index",V::makeNum((double)i));row.set("tool",required(query,"tool"));row.set("args",submitted);if(!references.empty()&&blocked.empty()&&failure.empty())row.set("resolved_args",executed);
            row.set("elapsed_ms",V::makeNum(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));if(required(row,"isError").b)++errors;rows.arr.push_back(row);}
        out=V::makeObj();out.set("results",rows);out.set("revision",V::makeNum((double)s.revision));out.set("error_count",V::makeNum((double)errors));
    }else return false;
    return true;
}
V agentPostconditionsSchema(){
    V schema;arkjson::parse(R"({"type":"array","minItems":1,"maxItems":32,"items":{"type":"object","properties":{"id":{"type":"string","description":"Optional unique condition label, not a feature id."},"query":{"type":"object","properties":{"tool":{"type":"string","description":"Allowed read-only measurement tool."},"args":{"type":"object"},"path":{"type":"string","minLength":1,"description":"Dot path within structuredContent, including array indices. Put path inside query, never beside it."}},"required":["tool","args","path"]},"op":{"type":"string","enum":["ge","le","eq","between","true","false"]},"value":{"type":"number","description":"Numeric target for ge, le or eq."},"min":{"type":"number","description":"Lower numeric limit for between."},"max":{"type":"number","description":"Upper numeric limit for between."},"tolerance":{"type":"number","minimum":0},"parameters":{"type":"array","maxItems":16,"items":{"type":"string"}},"diagnose":{"type":"boolean"}},"required":["query","op"],"description":"Numeric ge/le/eq require value; between requires min and max; true/false require an observed boolean. Unknown or failed measurements reject the atomic write.","examples":[{"id":"valid","query":{"tool":"native_inspect","args":{"id":1},"path":"report.brep_valid"},"op":"true"},{"id":"volume","query":{"tool":"native_inspect","args":{"id":1},"path":"report.volume_mm3"},"op":"ge","value":100}]}})",schema);
    V item=*schema.find("items"),variants;arkjson::parse(R"([{"properties":{"op":{"enum":["ge","le","eq"]}},"required":["value"]},{"properties":{"op":{"enum":["between"]}},"required":["min","max"]},{"properties":{"op":{"enum":["true","false"]}}}])",variants);item.set("anyOf",variants);
    V props=*item.find("properties"),query=*props.find("query"),queryProps=*query.find("properties"),tool=*queryProps.find("tool"),names=V::makeArr();for(const auto& name:queryTools)names.arr.push_back(V::makeStr(name));tool.set("enum",names);queryProps.set("tool",tool);query.set("properties",queryProps);props.set("query",query);item.set("properties",props);schema.set("items",item);
    return schema;
}
V evaluatePostconditions(AgentSession& s,const V& checks){
    if(checks.t!=V::Arr||checks.arr.empty()||checks.arr.size()>32)throw std::runtime_error("checks must contain 1..32 postconditions");
    V report=V::makeObj(),rows=V::makeArr();bool all=true;std::set<std::string> ids;unsigned diagnosticProbes=0;const auto started=std::chrono::steady_clock::now();
    for(size_t index=0;index<checks.arr.size();++index){const auto& check=checks.arr[index];V row=V::makeObj();bool passed=false;
        std::string id=check.find("id")?text(check,"id"):std::to_string(index);if(!ids.insert(id).second)throw std::runtime_error("Duplicate condition id");row.set("id",V::makeStr(id));
        try{
            if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>30)throw std::runtime_error("Postcondition evaluation budget exhausted");
            const auto& query=required(check,"query");row.set("query",query);V actual=queryValue(s,query);row.set("observed",actual);std::string op=text(check,"op");row.set("op",V::makeStr(op));
            const double tolerance=number(check,"tolerance",0);if(tolerance<0)throw std::runtime_error("Condition tolerance must be nonnegative");row.set("tolerance",V::makeNum(tolerance));
            if(op=="true"||op=="false"){if(actual.t!=V::Bool)throw std::runtime_error("Boolean condition requires a checked boolean result");passed=actual.b==(op=="true");}
            else{
                if(actual.t!=V::Num||!std::isfinite(actual.num))throw std::runtime_error("Condition measurement is missing or nonnumeric");
                if(op=="between"){
                    required(check,"min");required(check,"max");double low=number(check,"min"),high=number(check,"max");if(low>high)throw std::runtime_error("Condition limits reversed");passed=actual.num+tolerance>=low&&actual.num-tolerance<=high;row.set("min",V::makeNum(low));row.set("max",V::makeNum(high));row.set("required_delta",V::makeNum(actual.num<low?low-actual.num:actual.num>high?high-actual.num:0));
                }else{
                    required(check,"value");double target=number(check,"value");row.set("target",V::makeNum(target));row.set("required_delta",V::makeNum(target-actual.num));
                    if(op=="ge")passed=actual.num+tolerance>=target;else if(op=="le")passed=actual.num-tolerance<=target;else if(op=="eq")passed=std::abs(actual.num-target)<=tolerance;else throw std::runtime_error("Condition op must be ge, le, eq, between, true or false");
                }
            }
            if(!passed){row.set("suggested_action",V::makeStr("Adjust the measured quantity by required_delta; inspect query references before changing geometry. No parameter direction is inferred."));V parameters=V::makeArr();
                if(auto requested=check.find("parameters")){if(requested->t!=V::Arr||requested->arr.size()>16)throw std::runtime_error("Condition parameters needs up to 16 names");for(const auto& name:requested->arr){if(name.t!=V::Str)throw std::runtime_error("Condition parameter must be a name");bool found=false;for(const auto& p:s.document.params)if(p.name==name.str){V parameter=V::makeObj();parameter.set("name",name);parameter.set("value",V::makeNum(p.value));parameter.set("expression",V::makeStr(p.expr));parameters.arr.push_back(parameter);found=true;}if(!found)throw std::runtime_error("Condition parameter not found: "+name.str);}}
                row.set("adjustment_parameters",parameters);row.set("parameter_influence_verified",V::makeBool(false));
                if(const auto* diagnose=check.find("diagnose");diagnose&&diagnose->t==V::Bool&&diagnose->b&&actual.t==V::Num){
                    V sensitivities=V::makeArr();
                    for(const auto& parameter:parameters.arr){if(diagnosticProbes+2>4||std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>5)break;
                        V sensitivity=V::makeObj();sensitivity.set("parameter",*parameter.find("name"));double value=parameter.find("value")->num,h=1e-4*std::max(1.,std::abs(value));
                        try{auto sample=[&](double x){++diagnosticProbes;AgentSession scratch;copySession(s,scratch);V command=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("param.set"));command.set("name",*parameter.find("name"));command.set("expr",V::makeNum(x));commands.arr.push_back(command);payload(scratch.call("doc_apply",applyArgs(s.revision,commands)));return metric(scratch,query);};double plus=sample(value+h),minus=sample(value-h),derivative=(plus-minus)/(2*h);sensitivity.set("derivative",V::makeNum(derivative));sensitivity.set("step",V::makeNum(h));sensitivity.set("method",V::makeStr("local_central_difference"));sensitivity.set("topology_continuity_certified",V::makeBool(false));if(derivative!=0&&std::isfinite(derivative)&&row.find("required_delta")){double delta=row.find("required_delta")->num/derivative;if(std::isfinite(delta))sensitivity.set("linearized_parameter_delta",V::makeNum(delta));}}
                        catch(const std::exception& e){sensitivity.set("error",V::makeStr(e.what()));}
                        sensitivities.arr.push_back(sensitivity);
                    }row.set("sensitivities",sensitivities);
                }
            }
        }catch(const std::exception& e){passed=false;row.set("error",V::makeStr(e.what()));row.set("suggested_action",V::makeStr("Resolve the missing measurement, density or reference before retrying; unknown evidence does not satisfy a condition."));}
        row.set("passed",V::makeBool(passed));rows.arr.push_back(row);all&=passed;
    }
    report.set("passed",V::makeBool(all));report.set("checks",rows);return report;
}
V agentParametricNativeSchema(){
    V schema;arkjson::parse(R"JSON({"type":"object","properties":{
      "radius":{"type":["number","string"],"description":"Helix radius in mm, >1e-6."},
      "pitch":{"type":["number","string"],"description":"Helix positive axial advance per turn in mm, >1e-6."},
      "turns":{"type":["number","string"],"description":"Helix positive turns, <=100."},
      "start_angle":{"type":["number","string"],"description":"Helix starting radial angle in degrees."},
      "origin":{"type":"array","minItems":3,"maxItems":3,"items":{"type":["number","string"]}},
      "axis":{"type":["string","array"],"description":"Helix XYZ axis; string selectors remain available for other native operations."},
      "x_axis":{"type":"array","minItems":3,"maxItems":3,"items":{"type":["number","string"]},"description":"Nonzero helix radial guide perpendicular to axis."},
      "handedness":{"type":["number","string"],"description":"Helix +1 mathematical positive rotation, or -1; axial rise remains positive."},
      "require_connected":{"type":"boolean","description":"Sew rejects disconnected result components; no arbitrary gap healing."},
      "controls":{"type":"object","additionalProperties":false,"properties":{
        "section_alignment":{"type":"array","minItems":2,"maxItems":32,"items":{"type":"object","additionalProperties":false,"properties":{"reverse":{"type":"boolean"},"seam_edge":{"type":["number","string"],"description":"Zero-based first edge of closed section."},"seam_angle":{"type":["number","string"],"description":"Degrees; only one closed analytic circle supports angular seam."}}},"description":"One alignment per loft section; alignment changes directed wire correspondence."},
        "guide_samples":{"type":["number","string"],"description":"2..129 checkpoints per guide edge; does not prove a global deviation bound."},
        "max_guide_deviation_mm":{"type":["number","string"],"description":"Reject measured guide-to-result checkpoint error above 1e-6..0.05 mm; never heals geometry."},
        "require_ordered_contacts":{"type":"boolean","description":"Verify each guide follows section order and cyclic contact order remains coherent."}
      }}
    }})JSON",schema);
    // The existing UV spline source also calls its top-level array controls.
    // Advertise both source shapes; the operation-specific evaluator validates
    // the chosen shape and keeps loft-only keys out of a spline request.
    V properties=*schema.find("properties"),alternatives=V::makeArr(),uv;
    alternatives.arr.push_back(*properties.find("controls"));
    arkjson::parse(R"({"type":"array","minItems":2,"maxItems":26,"items":{"type":"array","minItems":2,"maxItems":2,"items":{"type":["number","string"]}},"description":"UV surface_spline Bezier controls; 2..26 [u,v] points."})",uv);
    alternatives.arr.push_back(uv);V controls=V::makeObj();controls.set("oneOf",alternatives);
    properties.set("controls",controls);schema.set("properties",properties);return schema;
}
void appendAgentFeatureTools(V& tools){
    appendComponentLibraryTools(tools);
    appendBomTools(tools);
    appendAgentActivityTools(tools);
    struct Definition{const char* name;const char* description;const char* schema;};
    const Definition defs[]={
      {"selection_set","Validate and select a persistent named face/edge, or a construction datum by plane name (Origin, XY, XZ, YZ, or a datum). Face/edge keeps definition/occurrence/copy identity. Plane selection uses plane plus expected_revision and cannot mix id, reference, instance or copy. Requires current expected_revision. Headless validates and returns view_state_applied false; Studio applies the highlight without editing CAD. Use reference_navigate for adjacent jump targets.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"reference":{"type":"string","minLength":1},"instance":{"type":["integer","string"]},"copy":{"type":"integer","minimum":0,"maximum":31},"plane":{"type":"string","minLength":1},"expected_revision":{"type":"integer","minimum":0}},"required":["expected_revision"],"oneOf":[{"required":["id","reference"]},{"required":["plane"]}],"additionalProperties":false})"},
      {"reference_navigate","Jump from a persistent semantic face/edge to exact adjacent named faces/edges on the same feature. Face neighbors share an edge; edge neighbors share a vertex. Returns compact names/descriptors at current revision for follow-up selection, inspection or feature edits, avoiding full geometry buffers.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"reference":{"type":"string","minLength":1},"kind":{"type":"string","enum":["all","face","edge"]}},"required":["id","reference"],"additionalProperties":false})"},
      {"failure_info","List bounded recent local CAD rejection records, or inspect exact failed commands/checks by id. The frozen baseline model is never included in this response. Repeated failures are evidence, not an instruction to retry.",R"({"type":"object","properties":{"id":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":8}},"additionalProperties":false})"},
      {"failure_export","Explicitly export one retained rejection and its private immutable .ark baseline to a new local JSON bundle path. Contains user design data; no network upload. Existing files are preserved. Never exports implicitly.",R"({"type":"object","properties":{"id":{"type":"string"},"path":{"type":"string"}},"required":["id","path"],"additionalProperties":false})"},
      {"failure_replay","Reevaluate recorded exact doc_apply/doc_preview commands against their frozen baseline in an isolated session. Select id or local exported bundle path; expected_revision checks the live document. Report observed rejection and same_error. Never mutates live CAD or applies a proposed fix; invalid bundles and unreplayable prepared-cache requests reject explicitly.",R"({"type":"object","properties":{"id":{"type":"string"},"path":{"type":"string"},"expected_revision":{"type":"integer","minimum":0}},"required":["expected_revision"],"oneOf":[{"required":["id"]},{"required":["path"]}],"additionalProperties":false})"},
      {"manufacturing_propose","Prepare an explicit bounded editable design candidate for a current versioned DFM recommendation. Requires observed expected_revision, feature id, rule_id and rule_version and 1..8 commands. Exact before/after manufacturing reports use identical process/material declarations, expose all rule changes and unresolved checks. Geometry stays uncommitted; accept only after review with doc_apply{candidate_id,expected_revision,accept_dfm:true}. No automatic repair, cost optimization or manufacturing qualification.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0},"id":{"type":["integer","string"]},"rule_id":{"type":"string"},"rule_version":{"type":"integer","minimum":1},"commands":{"type":"array","minItems":1,"maxItems":8,"items":{"type":"object"}},"checks":{"type":"array"}},"required":["expected_revision","id","rule_id","rule_version","commands"],"additionalProperties":false})"},
      {"manufacturing_inspect","Read declared fabrication route/process inputs, current exact B-rep evidence, route-specific design requirements, blockers and unknown checks. Each component includes dfm: versioned laser/bend/FDM/CNC/weld rules, explicit evidence/assumptions and typed recommendations; compatible screens do not qualify manufacture. id selects a feature by id/name; otherwise declared and output definitions. Call manufacturing onboarding for process field contracts. Solid-equivalent filament arithmetic is not slicer consumption. No geometry mutation or manufacturing certification.",R"({"type":"object","properties":{"id":{"type":["integer","string"]}},"additionalProperties":false})"},
      {"physics_inspect","Exact rigid-body mass, uniform-gravity CG, world inertia and instantaneous force/torque/energy balance. id selects one feature; otherwise visible assembly instances or unconsumed solids. Missing density rejects aggregate mass. SI loads and kinematics; positions mm. Optional support polygon checks gravity-only projection, not contact dynamics. axial_test uses declared material modulus and uniform cross-section; no FEA. beam_test: declared prismatic Euler-Bernoulli point-load bending and optional ideal Euler buckling. weld_test: uniform direct load on declared effective throat. These two tests work without aggregate mass; no geometry inference or production qualification.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"gravity":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"reference_point_mm":{"type":"array"},"loads":{"type":"array","maxItems":64,"items":{"type":"object"}},"velocity_m_s":{"type":"array"},"angular_velocity_rad_s":{"type":"array"},"acceleration_m_s2":{"type":"array"},"angular_acceleration_rad_s2":{"type":"array"},"support":{"type":"object","properties":{"origin_mm":{"type":"array"},"normal":{"type":"array"},"polygon_mm":{"type":"array"}},"required":["origin_mm","normal","polygon_mm"]},"axial_test":{"type":"object","properties":{"material":{"type":"string"},"force_N":{"type":"number"},"area_mm2":{"type":"number"},"length_mm":{"type":"number"}},"required":["material","force_N","area_mm2","length_mm"]},"beam_test":{"type":"object","properties":{"material":{"type":"string"},"support":{"enum":["cantilever_tip","simply_supported_center"]},"length_mm":{"type":"number","exclusiveMinimum":0},"second_moment_mm4":{"type":"number","exclusiveMinimum":0},"extreme_fiber_mm":{"type":"number","exclusiveMinimum":0},"force_N":{"type":"number"},"buckling":{"type":"object","properties":{"minimum_second_moment_mm4":{"type":"number","exclusiveMinimum":0},"area_mm2":{"type":"number","exclusiveMinimum":0},"effective_length_factor":{"type":"number","exclusiveMinimum":0},"compression_N":{"type":"number","minimum":0}},"required":["minimum_second_moment_mm4","area_mm2","effective_length_factor","compression_N"]}},"required":["material","support","length_mm","second_moment_mm4","extreme_fiber_mm","force_N"]},"weld_test":{"type":"object","properties":{"effective_throat_mm":{"type":"number","exclusiveMinimum":0},"effective_length_mm":{"type":"number","exclusiveMinimum":0},"force_N":{"type":"number"},"allowable_stress_Pa":{"type":"number","exclusiveMinimum":0}},"required":["effective_throat_mm","effective_length_mm","force_N"]}}})"},
      {"wood_inspect","Read authored Wood blank dimensions, grain and BOM occurrence quantities including hidden copies. Retains supported single-input machining lineage. No cutting allowance, structural or manufacturing qualification is inferred.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})"},
      {"materials_inspect","List document materials, declared density in kg/m3 and g/cm3, SI elastic properties and assigned features. material.set accepts density (g/cm3) OR density_kg_m3, plus optional physical:{young_modulus_Pa,poisson_ratio,yield_strength_Pa,thermal_expansion_per_K}. Zero density means unspecified. Values are caller declarations, not a certified material database.",R"({"type":"object"})"},
      {"geometry_query","Batch 1..128 exact queries on one feature id/name. actions: references (filter kind, surface, oriented normal, unoriented axis, radius_mm, near+within_mm, coaxial_origin, adjacent_to; unique rejects ambiguity), properties (reference?, density_g_cm3?), curve (reference,count?,margin_mm?,fraction?), surface (reference,u?,v?), section (origin,normal), continuity (a,b,fraction_a?,fraction_b?), point_distance(point). Coordinates are feature-local mm; curve fractions use arc length. Results have index and ok; failed queries never invent values.",R"({"type":"object","properties":{"id":{"type":["integer","string"]},"queries":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"action":{"type":"string","enum":["references","properties","curve","surface","section","continuity","point_distance"]}},"required":["action"]}}},"required":["id","queries"]})"},
      {"math_eval","Deterministic vector and rigid-frame math. op add/subtract/dot/cross/distance/angle uses a,b xyz; norm/normalize uses a; frame uses origin,z,x_hint; transform_point/direction uses a,from_frame,to_frame (16 column-major values); project_plane uses a,origin,normal; line_plane also direction. convert uses value,from,to: mm/cm/m/in, deg/rad, g/kg. Incompatible units and degenerate directions reject.",R"({"type":"object","properties":{"op":{"type":"string","enum":["add","subtract","dot","cross","distance","angle","norm","normalize","frame","transform_point","transform_direction","project_plane","line_plane","convert"]},"a":{"type":"array"},"b":{"type":"array"},"origin":{"type":"array"},"normal":{"type":"array"},"direction":{"type":"array"},"z":{"type":"array"},"x_hint":{"type":"array"},"from_frame":{"type":"array"},"to_frame":{"type":"array"},"value":{"type":"number"},"from":{"type":"string"},"to":{"type":"string"}},"required":["op"]})"},
      {"doc_preview","Evaluate commands/checks on an isolated candidate, or explicitly refresh this session's retained candidate_id at the observed current expected_revision without resending commands. Refresh verifies retained targets/dependencies and supported authoring scope, rejects real conflicts, and returns a new candidate for the current document. It never commits or silently rebases an old apply. Candidates expire after 300 seconds; at most four/128 MiB, and project boundaries invalidate them. recipe retains expanded commands; valid previews return candidate_id for an atomic apply without a second geometry evaluation. detail:changes returns compact partial evidence. Live document, revision and undo remain unchanged.",R"({"type":"object","properties":{"expected_revision":{"type":"integer"},"candidate_id":{"type":"string","minLength":1},"detail":{"type":"string","enum":["design","changes"],"description":"changes returns explicitly partial changed-feature summaries; design retains full compact design inventories"},"commands":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object"}},"checks":{"type":"array","minItems":1,"maxItems":32,"items":{"type":"object"}}},"required":["expected_revision"],"oneOf":[{"required":["commands"],"not":{"required":["candidate_id"]}},{"required":["candidate_id"],"not":{"anyOf":[{"required":["commands"]},{"required":["checks"]}]}}]})"},
      {"query_batch","Run 1..128 read-only calls against one revision, once per query. Invalid structure/forbidden tools reject the batch before execution. agent_onboard requires detail:guide; object_context retains its 64-object/256KiB bounds. fields selects 1..32 distinct top-level structuredContent names, not dot paths. paths instead selects nested values (key, key[n], key[a:b], or key[] for each element), keyed by path, plus revision. An optional id lets a later query take a value from this row's full result: put {\"$ref\":\"id.path\"} anywhere in its args; the row echoes args and adds resolved_args. A reference to a failed row skips that row explicitly. max_bytes (256..262144) makes a larger row return its shape instead of its data. Missing fields produce only that row's QUERY_BATCH_FIELD_MISSING error with available_fields, actual observed_result and executable recovery tool/args. Reuse retained evidence; request a fresh read only when needed. Tool/projection errors preserve all other rows. Every row has query_index (zero-based), tool, args, structuredContent/content/isError and elapsed_ms. Outer results/revision/error_count require checking each row. Prefer doc_info detail:revision and native_inspect detail:properties.",R"({"type":"object","properties":{"queries":{"type":"array","minItems":1,"maxItems":128,"items":{"type":"object","properties":{"tool":{"type":"string"},"args":{"type":"object"},"fields":{"type":"array","minItems":1,"maxItems":32,"uniqueItems":true,"items":{"type":"string","minLength":1},"description":"Exact top-level structuredContent names. Missing fields are an individual row error with available_fields and retained observed_result; other rows remain available."},"id":{"type":"string","pattern":"^[A-Za-z0-9_-]{1,32}$","description":"Name later queries use in {\"$ref\":\"id.path\"}."},"paths":{"type":"array","minItems":1,"maxItems":32,"uniqueItems":true,"items":{"type":"string","minLength":1,"maxLength":256},"description":"Nested values to return instead of the whole result; not combined with fields."},"max_bytes":{"type":"integer","minimum":256,"maximum":262144}},"required":["tool","args"],"additionalProperties":false}}},"required":["queries"],"additionalProperties":false})"},
      {"placement_plan","Preview a rigid placement of a free instance. node is name/id; reference defaults origin. target is {node|feature,reference?,copy?,fraction?}, {plane:name} or {frame:16-column-major-values}; mode align matches frames, center aligns area/volume centroids by translation. offset xyz uses target coordinates. Refuses grounded/mated instances and nested placements in mated assemblies. Returns doc_apply payload; use mates for persistent relations.",R"({"type":"object","properties":{"expected_revision":{"type":"integer"},"node":{"type":["integer","string"]},"reference":{"type":"string"},"target":{"type":"object"},"mode":{"type":"string","enum":["align","center"]},"offset":{"type":"array"}},"required":["expected_revision","node","target"]})"},
      {"pattern_plan","Preview equally arc-length spaced instances or point datums along one semantic edge. path is feature id/name; reference is edge name; count 2..128; margin_mm at each end; name is unique prefix. instances requires feature and optional x_hint (local Z follows curve tangent). instances/points are snapshots in path feature coordinates. solid/holes create linked version-19 native features; holes requires feature target, normal,radius,depth and optional offset. solid duplicates the feature with semantic copy.N names. 044B solid (version 47): references[] auto-chains edges into one arc-length path (first edge fixes direction/seam; reverse), no reference uses a whole wire curve feature or spline cable centerline; count 2..2048 or pitch_mm with pitch_mode fit|truncate; start_margin_mm,end_margin_mm, closed loops space without seam duplicate (phase_mm); skip[] station indices; orientation translate|x_hint|frenet|surface; surface uses support feature or the path body faces, support_faces[], project, offset_mm, flip_normal, max_deviation_mm; reference_scheme copies (<=512 names) or compact with named_copies[]. See docs/CURVE_PATTERNS_044B.md. Linked counts/margins/offsets remain editable in native payloads. Returns revision-bound doc_apply commands.",R"({"type":"object","properties":{"expected_revision":{"type":"integer"},"path":{"type":["integer","string"]},"reference":{"type":"string"},"count":{"type":"integer","minimum":2,"maximum":2048},"references":{"type":"array","items":{"type":"string"}},"pitch_mm":{"type":"number"},"pitch_mode":{"type":"string","enum":["fit","truncate"]},"start_margin_mm":{"type":"number"},"end_margin_mm":{"type":"number"},"phase_mm":{"type":"number"},"skip":{"type":"array","items":{"type":"integer"}},"reverse":{"type":"boolean"},"orientation":{"type":"string","enum":["translate","x_hint","frenet","surface"]},"support":{"type":["integer","string"]},"support_faces":{"type":"array","items":{"type":"string"}},"project":{"type":"boolean"},"offset_mm":{"type":"number"},"flip_normal":{"type":"boolean"},"max_deviation_mm":{"type":"number"},"reference_scheme":{"type":"string","enum":["copies","compact"]},"named_copies":{"type":"array","items":{"type":"integer"}},"margin_mm":{"type":"number"},"name":{"type":"string"},"mode":{"type":"string","enum":["instances","points","solid","holes"]},"offset":{"type":"array"},"normal":{"type":"array"},"radius":{"type":"number"},"depth":{"type":"number"},"feature":{"type":["integer","string"]},"x_hint":{"type":"array"}},"required":["expected_revision","path","name"]})"},
      {"design_solve","Read-only scalar parameter target solve by bounded bisection; requires a continuous bracket, aborts invalid geometry. parameter,lower,upper,target,tolerance; objective:{tool,args,path} selects a finite scalar using dot paths including array indices. max_evaluations 3..64 (default32), max_seconds <=120 (default30, checked between operations). Returns apply only on convergence. mode sensitivity returns a finite-interval secant. No global optimum or topology continuity certification.",R"({"type":"object","properties":{"expected_revision":{"type":"integer"},"parameter":{"type":"string"},"lower":{"type":"number"},"upper":{"type":"number"},"target":{"type":"number"},"tolerance":{"type":"number","exclusiveMinimum":0},"objective":{"type":"object"},"max_evaluations":{"type":"integer","minimum":3,"maximum":64},"max_seconds":{"type":"number"},"mode":{"type":"string","enum":["target","sensitivity"]}},"required":["expected_revision","parameter","lower","upper","objective"]})"}
    };
    for(const auto& d:defs){V tool=V::makeObj(),schema,annotations=V::makeObj(),output=V::makeObj();arkjson::parse(d.schema,schema);if(std::string(d.name)=="doc_preview"||std::string(d.name)=="manufacturing_propose"){V props=*schema.find("properties");props.set("checks",agentPostconditionsSchema());schema.set("properties",props);}
        if(std::string(d.name)=="query_batch"){
            V props=*schema.find("properties"),queries=*props.find("queries"),item=*queries.find("items"),fields=*item.find("properties"),name=*fields.find("tool"),names=V::makeArr();for(const auto& allowed:queryTools)names.arr.push_back(V::makeStr(allowed));names.arr.push_back(V::makeStr("agent_onboard"));name.set("enum",names);fields.set("tool",name);item.set("properties",fields);queries.set("items",item);props.set("queries",queries);schema.set("properties",props);
            arkjson::parse(R"({"type":"object","properties":{"results":{"type":"array","items":{"type":"object","properties":{"query_index":{"type":"integer","minimum":0},"tool":{"type":"string"},"args":{"type":"object"},"isError":{"type":"boolean"},"structuredContent":{"type":"object","description":"A result or explicit row error. QUERY_BATCH_FIELD_MISSING includes query_index, tool, requested_fields, missing_fields, available_fields, tool_succeeded:true, observed_result and recovery:{action:reuse_observed_result,tool,args,hint}."},"content":{"type":"array"},"resolved_args":{"type":"object","description":"Arguments actually executed after resolving $ref values."},"elapsed_ms":{"type":"number","minimum":0}},"required":["query_index","tool","args","structuredContent","content","isError","elapsed_ms"]}},"revision":{"type":"integer"},"error_count":{"type":"integer","minimum":0,"maximum":128}},"required":["results","revision","error_count"]})",output);
        }else output.set("type",V::makeStr("object"));
        tool.set("name",V::makeStr(d.name));tool.set("description",V::makeStr(d.description));tool.set("inputSchema",schema);tool.set("outputSchema",output);annotations.set("readOnlyHint",V::makeBool(std::string(d.name)!="failure_export"));tool.set("annotations",annotations);tools.arr.push_back(tool);}
}
}
