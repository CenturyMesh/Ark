#include "ark/assembly.hpp"
#include "ark/model.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include "ark/sketch.hpp"
#include "ark/spatial_sketch.hpp"
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr int maxCopies=32;
constexpr double connectionTolerance=1e-6;
[[noreturn]] void fail(const std::string& message){throw std::runtime_error("ASSEMBLY_PATTERN_"+message);}
const V& need(const V& value,const char* key){auto item=value.find(key);if(!item)fail(std::string("FIELD_REQUIRED: ")+key);return *item;}
std::string text(const V& value,const char* key,const char* fallback=nullptr){auto item=value.find(key);if(!item){if(fallback)return fallback;fail(std::string("FIELD_REQUIRED: ")+key);}if(item->t!=V::Str||item->str.empty()||item->str.size()>2048)fail(std::string("INVALID_TEXT: ")+key);return item->str;}
std::string expression(const V& value){if(value.t==V::Num&&std::isfinite(value.num))return value.dump();if(value.t==V::Str&&!value.str.empty()&&value.str.size()<=2048)return value.str;fail("INVALID_EXPRESSION: expected a finite number or expression");}
u64 identity(const V& value){if(value.t!=V::Num||!std::isfinite(value.num)||value.num<1||value.num>9007199254740990.||value.num!=std::floor(value.num))fail("INVALID_ID: bound source ID required");return (u64)value.num;}
double scalar(const V& value,const ExprEnv& env,const char* key){bool ok=false;double number=evalExpr(expression(value),env,&ok);if(!ok||!std::isfinite(number)||std::abs(number)>1e6)fail(std::string("INVALID_EXPRESSION: ")+key);return number;}
double number(const V& value,const ExprEnv& env,const char* key,double fallback){auto item=value.find(key);return item?scalar(*item,env,key):fallback;}
bool boolean(const V& value,const char* key,bool fallback=false){auto item=value.find(key);if(!item)return fallback;if(item->t!=V::Bool)fail(std::string("INVALID_BOOLEAN: ")+key);return item->b;}
Vec3 vector(const V& value,const ExprEnv& env,const char* key){if(value.t!=V::Arr||value.arr.size()!=3)fail(std::string("INVALID_VECTOR: ")+key);return {scalar(value.arr[0],env,key),scalar(value.arr[1],env,key),scalar(value.arr[2],env,key)};}
Vec3 unit(Vec3 v,const char* key){if(v.len()<1e-9)fail(std::string("DEGENERATE_DIRECTION: ")+key);return v.normalized();}
Mat4 tangentFrame(Vec3 position,Vec3 tangent,Vec3 normal){
    const Vec3 x=unit(tangent,"tangent"),z=unit(normal-x*normal.dot(x),"normal parallel to tangent"),y=z.cross(x);
    Mat4 frame;const double values[]{x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,position.x,position.y,position.z,1};std::copy(std::begin(values),std::end(values),frame.m);return frame;
}
void validateFrame(const Mat4& frame){
    for(double value:frame.m)if(!std::isfinite(value)||std::abs(value)>1e6)fail("FRAME_LIMIT: non-finite or out-of-bounds placement");
    auto x=frame.transformDir({1,0,0}),y=frame.transformDir({0,1,0}),z=frame.transformDir({0,0,1});
    if(std::abs(x.len()-1)+std::abs(y.len()-1)+std::abs(z.len()-1)+std::abs(x.dot(y))+std::abs(y.dot(z))+std::abs(x.dot(z))+std::abs(x.cross(y).dot(z)-1)>1e-6)fail("NONRIGID_FRAME: placement must be right handed and rigid");
}
std::string canonical(std::string kind){if(kind=="pattern-driven"||kind=="pattern_driven_component")return "pattern_driven";if(kind=="sketch-driven"||kind=="sketch_driven_component")return "sketch_driven";if(kind=="curve-driven"||kind=="curve_driven_component")return "curve_driven";if(kind=="chain-component"||kind=="chain_component")return "chain";return kind;}
const Feature& feature(const Document& doc,u64 id){const Feature* found=nullptr;for(const auto& item:doc.features)if(item.id==id){if(found)fail("AMBIGUOUS_FEATURE");found=&item;}if(!found)fail("SOURCE_MISSING: feature #"+std::to_string(id));if(found->suppressed)fail("SOURCE_SUPPRESSED: feature #"+std::to_string(id));return *found;}
const AssemblyNode& node(const Document& doc,u64 id){const AssemblyNode* found=nullptr;for(const auto& item:doc.assembly)if(item.id==id){if(found)fail("AMBIGUOUS_NODE");found=&item;}if(!found)fail("SOURCE_MISSING: node #"+std::to_string(id));if(!found->feature)fail("SOURCE_GROUP: source must be a part occurrence");return *found;}
u64 bindFeature(const Document& doc,const V& value){if(value.t==V::Num){const auto id=identity(value);feature(doc,id);return id;}if(value.t!=V::Str||value.str.empty())fail("INVALID_SOURCE_FEATURE");u64 found=0;for(const auto& item:doc.features)if(item.name==value.str){if(found)fail("AMBIGUOUS_SOURCE_FEATURE: "+value.str);found=item.id;}if(!found)fail("SOURCE_MISSING: feature "+value.str);feature(doc,found);return found;}
u64 bindNode(const Document& doc,const V& value){if(value.t==V::Num){const auto id=identity(value);node(doc,id);return id;}if(value.t!=V::Str||value.str.empty())fail("INVALID_SOURCE_NODE");u64 found=0;for(const auto& item:doc.assembly)if(item.name==value.str){if(found)fail("AMBIGUOUS_SOURCE_NODE: "+value.str);found=item.id;}if(!found)fail("SOURCE_MISSING: node "+value.str);node(doc,found);return found;}
void keys(const V& value,const std::set<std::string>& allowed){if(value.t!=V::Obj)fail("SOURCE_OBJECT_REQUIRED");std::set<std::string> seen;for(const auto& field:value.obj){if(!allowed.count(field.first))fail("UNKNOWN_FIELD: "+field.first);if(!seen.insert(field.first).second)fail("DUPLICATE_FIELD: "+field.first);}}
int count(const AssemblyPattern& pattern,const ExprEnv& env,int inferred=0){
    if(pattern.count.empty()){if(inferred<2||inferred>maxCopies)fail("COUNT_LIMIT: source must provide 2..32 stable slots");return inferred;}
    const double value=scalar(V::makeStr(pattern.count),env,"count");if(value<2||value>maxCopies||std::abs(value-std::round(value))>1e-6)fail("COUNT_LIMIT: count must be an integer 2..32");if(inferred&&std::round(value)!=inferred)fail("SOURCE_COUNT_MISMATCH: omit count or match the source slot count");return (int)std::round(value);
}
void skipValidation(const AssemblyPattern& pattern,int copies){std::set<int> seen;for(int slot:pattern.skips)if(slot<=0||slot>=copies||!seen.insert(slot).second)fail("INVALID_SKIP: unique non-seed slot in current source range required");}
bool skipped(const AssemblyPattern& pattern,int slot){return std::find(pattern.skips.begin(),pattern.skips.end(),slot)!=pattern.skips.end();}

// Inspect stored producer dependencies before native evaluation. A path that
// depends on the very occurrences it places cannot be a driving source.
void validateSourceDependencies(const Document& doc,u64 source,u64 target){
    std::unordered_set<u64> active,done;
    std::function<void(u64)> visit=[&](u64 id){if(done.count(id))return;if(!active.insert(id).second)fail("SOURCE_FEATURE_CYCLE");const auto& f=feature(doc,id);
        auto occurrence=[&](const V& value){const u64 n=bindNode(doc,value);if(n==target)fail("DEPENDENCY_CYCLE: source consumes its patterned occurrence");fail("SOURCE_ASSEMBLY_DEPENDENCY_UNSUPPORTED: use an independent feature-local guide");};
        for(u64 input:f.inputs)visit(input);
        if(const auto* instances=f.nativeSpec.find("instances")){if(instances->t!=V::Arr)fail("INVALID_INSTANCE_DEPENDENCY");for(const auto& instance:instances->arr)occurrence(instance.t==V::Obj?need(instance,"node"):instance);}
        if(const auto* instance=f.nativeSpec.find("source_instance"))occurrence(*instance);
        const auto sketch=f.p.find("sketch");if(sketch!=f.p.end())for(const auto& sk:doc.sketches)if(sk.name==sketch->second)for(const auto& e:sk.sk.ents){if(e.sourceInstance)occurrence(V::makeNum((double)e.sourceInstance));if(e.sourceFeature)visit(e.sourceFeature);}
        active.erase(id);done.insert(id);
    };visit(source);
}

struct Sample {Vec3 point,tangent;bool hasTangent=false;};
struct Edge {std::string reference;double length=0;Sample first,last;bool reverse=false;};
struct Guide {const Body* body=nullptr;std::vector<Edge> edges;double length=0;bool closed=false;double closureGap=0;};
std::vector<V> query(const Body& body,const std::vector<V>& queries){
    const auto& brep=need(body.nativeData,"brep_base64");const auto& topology=need(body.nativeData,"topology");V worker=V::makeObj(),request=V::makeObj(),array=V::makeArr();for(const auto& q:queries)array.arr.push_back(q);
    // Own the full exact source identity: cache reuse never relies on a node ID,
    // document address, revision, tessellation or an approximate fingerprint.
    struct Cache {std::string brep,topology;std::map<std::string,std::vector<V>> responses;size_t bytes=0;};
    static thread_local std::vector<Cache> caches;const std::string topologyIdentity=topology.dump(),queryIdentity=array.dump();Cache* cache=nullptr;size_t bytes=0;for(auto& item:caches){bytes+=item.bytes;if(item.brep==brep.str&&item.topology==topologyIdentity)cache=&item;}
    if(cache){auto hit=cache->responses.find(queryIdentity);if(hit!=cache->responses.end())return hit->second;}
    request.set("brep",brep);request.set("topology",topology);request.set("queries",array);worker.set("operation",V::makeStr("geometry_query"));worker.set("request",request);
    auto response=nativeWorkerRequest(worker);const auto& results=need(response,"results");if(results.t!=V::Arr||results.arr.size()!=queries.size())fail("INVALID_NATIVE_RESPONSE");for(const auto& result:results.arr)if(!need(result,"ok").b)fail("SOURCE_REFERENCE: "+text(result,"error"));
    const size_t identityBytes=brep.str.size()+topologyIdentity.size(),responseBytes=results.dump().size()+queryIdentity.size();
    if(identityBytes+responseBytes<=16u*1024u*1024u){if(bytes+identityBytes+responseBytes>16u*1024u*1024u||(!cache&&caches.size()>=8)){caches.clear();cache=nullptr;}if(!cache){caches.push_back({brep.str,topologyIdentity,{},identityBytes});cache=&caches.back();}if(cache->responses.size()>=64){cache->responses.clear();cache->bytes=identityBytes;}cache->responses[queryIdentity]=results.arr;cache->bytes+=responseBytes;}
    return results.arr;
}
V curveQuery(const std::string& reference,double fraction){V q=V::makeObj();q.set("action",V::makeStr("curve"));q.set("reference",V::makeStr(reference));q.set("fraction",V::makeNum(fraction));return q;}
Sample sample(const V& result,const ExprEnv& env){const auto& samples=need(result,"samples");if(samples.t!=V::Arr||samples.arr.size()!=1)fail("INVALID_NATIVE_SAMPLE");const auto& item=samples.arr[0];Sample out;out.point=vector(need(item,"point_mm"),env,"point_mm");out.hasTangent=boolean(item,"tangent_defined");if(out.hasTangent)out.tangent=unit(vector(need(item,"tangent"),env,"tangent"),"tangent");return out;}
void checkSourceAccuracy(const Body& body,const V& source,const ExprEnv& env,V& report){
    const auto* geometryReport=body.nativeData.find("report");if(!geometryReport)return;
    bool approximate=false;double observed=0,reached=0;
    for(const char* key:{"helix_observed_max_error_mm","equation_observed_max_error_mm"})if(auto value=geometryReport->find(key)){approximate=true;observed=std::max(observed,scalar(*value,env,key));}
    if(auto value=geometryReport->find("helix_kernel_tolerance_reached_mm"))reached=scalar(*value,env,"kernel_tolerance");
    if(approximate){if(!source.find("max_source_error"))fail("SOURCE_ERROR_BOUND_REQUIRED: fitted source needs explicit max_source_error in mm");const double bound=number(source,env,"max_source_error",0);if(bound<=0||bound>1)fail("SOURCE_ERROR_BOUND: max_source_error must be in (0,1] mm");if(observed>bound||reached>bound)fail("SOURCE_ERROR_EXCEEDED: measured source fit exceeds the declared bound");report.set("source_observed_error_mm",V::makeNum(observed));report.set("source_kernel_fit_error_mm",V::makeNum(reached));report.set("source_max_error_mm",V::makeNum(bound));report.set("source_between_checkpoints_bound_verified",V::makeBool(false));}
    report.set("source_approximation",V::makeBool(approximate));report.set("placement_authority",V::makeStr("native_BRep_arc_length_queries"));
}
Guide guide(const Document& doc,const AssemblyNode& target,const ExprEnv& env,V& report){
    const auto& source=target.pattern.source;const auto id=identity(need(source,"feature"));validateSourceDependencies(doc,id,target.id);
    auto& mutableDoc=const_cast<Document&>(doc);auto& bodies=doc.evaluatingBodies?*const_cast<std::vector<Body>*>(doc.evaluatingBodies):mutableDoc.evalBodies;ensureNativeBody(mutableDoc,id,bodies);
    Guide result;for(const auto& body:bodies)if(body.id==id)result.body=&body;if(!result.body||!result.body->nativeData.find("brep_base64"))fail("SOURCE_NOT_EXACT: native B-rep guide required");checkSourceAccuracy(*result.body,source,env,report);
    const auto reference=text(source,"reference");std::vector<std::string> names;
    if(reference=="path"){
        if(feature(doc,id).type!=FeatureType::Curve&&feature(doc,id).type!=FeatureType::IntersectionCurve&&feature(doc,id).type!=FeatureType::Bridge)fail("PATH_REQUIRES_CURVE: use one exact semantic edge for a solid");
        const auto& refs=need(need(result.body->nativeData,"topology"),"references");for(const auto& ref:refs.arr)if(text(ref,"kind")=="edge"&&text(ref,"status")=="bound")names.push_back(text(ref,"name"));
    }else names.push_back(reference);
    if(names.empty()||names.size()>32)fail("PATH_EDGE_LIMIT: guide must contain 1..32 exact edges");
    std::vector<V> queries;for(const auto& name:names){queries.push_back(curveQuery(name,0));queries.push_back(curveQuery(name,1));}const auto results=query(*result.body,queries);
    for(size_t i=0;i<names.size();++i){Edge edge;edge.reference=names[i];edge.first=sample(results[i*2],env);edge.last=sample(results[i*2+1],env);edge.length=scalar(need(results[i*2],"length_mm"),env,"length_mm");if(edge.length<=1e-9)fail("ZERO_LENGTH_GUIDE");result.edges.push_back(edge);result.length+=edge.length;}
    // Native curve registries retain edge order. Accept only one connected,
    // unbranched traversal; reversal preserves the selected exact edge.
    for(size_t i=1;i<result.edges.size();++i){auto& edge=result.edges[i];const auto& previous=result.edges[i-1];const Vec3 end=previous.reverse?previous.first.point:previous.last.point;
        const bool atStart=(end-edge.first.point).len()<=connectionTolerance,atEnd=(end-edge.last.point).len()<=connectionTolerance;
        if(atStart==atEnd)fail("GUIDE_DISCONNECTED_OR_AMBIGUOUS: native edge order does not define one path");edge.reverse=atEnd;
        for(size_t earlier=0;earlier+1<i;++earlier){const auto& a=result.edges[earlier];const Vec3 begin=edge.reverse?edge.last.point:edge.first.point;if((begin-a.first.point).len()<=connectionTolerance||(begin-a.last.point).len()<=connectionTolerance)fail("GUIDE_BRANCH: shared non-adjacent endpoint");}
    }
    const Vec3 first=result.edges.front().first.point;const auto& last=result.edges.back();const Vec3 end=last.reverse?last.first.point:last.last.point;result.closureGap=(end-first).len();result.closed=result.closureGap<=connectionTolerance;
    if(boolean(source,"closed")!=result.closed)fail("CLOSURE_MISMATCH: declared closed must match native endpoints");report.set("guide_length_mm",V::makeNum(result.length));report.set("guide_edges",V::makeNum((double)result.edges.size()));report.set("guide_closed",V::makeBool(result.closed));report.set("guide_closure_gap_mm",V::makeNum(result.closureGap));report.set("arc_length_inversion_tolerance_mm",V::makeNum(1e-8));return result;
}
std::vector<Sample> stations(const Guide& guide,const std::vector<double>& distances,const ExprEnv& env){
    std::vector<V> queries;std::vector<bool> reverse;
    for(double distance:distances){if(distance<0||distance>guide.length+connectionTolerance)fail("SPACING_OVERRUN: requested station exceeds guide length");double walked=0;const Edge* selected=&guide.edges.back();for(const auto& edge:guide.edges){if(distance<=walked+edge.length+1e-10){selected=&edge;break;}walked+=edge.length;}double fraction=std::clamp((distance-walked)/selected->length,0.,1.);queries.push_back(curveQuery(selected->reference,selected->reverse?1-fraction:fraction));reverse.push_back(selected->reverse);}
    const auto rows=query(*guide.body,queries);std::vector<Sample> out;for(size_t i=0;i<rows.size();++i){auto value=sample(rows[i],env);if(reverse[i])value.tangent=-value.tangent;out.push_back(value);}return out;
}
std::vector<Vec3> sketchPoints(const Document& doc,const AssemblyNode& target,const ExprEnv& env){
    const auto& source=target.pattern.source;const auto& names=need(source,"points");if(names.t!=V::Arr||names.arr.size()<2||names.arr.size()>32)fail("POINT_COUNT: points needs 2..32 ordered named points");std::map<std::string,Vec3> available;
    if(const auto* sketchName=source.find("sketch")){
        const NamedSketch* selected=nullptr;for(const auto& sk:doc.sketches)if(sk.name==sketchName->str){if(selected)fail("AMBIGUOUS_SKETCH");selected=&sk;}if(!selected)fail("SOURCE_MISSING: sketch "+sketchName->str);auto evaluated=*selected;evaluateSketch(evaluated,env);if(!evaluated.error.empty()||evaluated.residual>1e-6)fail("INVALID_SKETCH: "+evaluated.error);const auto frame=resolvePlane(doc,selected->sk.plane);
        for(const auto& point:evaluated.sk.ents)if(point.kind==SketchEnt::Pnt&&!point.name.empty()&&point.dof.size()==2){if(available.count(point.name))fail("AMBIGUOUS_POINT: "+point.name);available[point.name]=frame.transformPoint({point.dof[0],point.dof[1],0});}
    }else{
        const auto id=identity(need(source,"feature"));validateSourceDependencies(doc,id,target.id);const auto& spatial=feature(doc,id);if(spatial.type!=FeatureType::Curve||!spatial.nativeSpec.find("segments"))fail("SPATIAL_POINTS_REQUIRE_AUTHORED_CURVE: start/segment handles required");const auto solved=solveSpatialCurve(spatial.nativeSpec,doc);const auto& handles=need(solved.diagnostics,"handles");for(const auto& handle:handles.arr)available[text(handle,"name")]=vector(need(handle,"point_mm"),env,"point_mm");
    }
    std::set<std::string> seen;std::vector<Vec3> points;for(const auto& requested:names.arr){if(requested.t!=V::Str||requested.str.empty()||!seen.insert(requested.str).second)fail("INVALID_POINT_NAME: unique named point required");auto name=requested.str;if(name.rfind("point.",0)==0)name=name.substr(6);auto found=available.find(name);if(found==available.end())fail("SOURCE_POINT_MISSING: "+requested.str);points.push_back(found->second);}for(size_t i=0;i<points.size();++i)for(size_t j=0;j<i;++j)if((points[i]-points[j]).len()<=connectionTolerance)fail("COINCIDENT_POINTS: distinct source stations required");return points;
}
}

bool specializedAssemblyPattern(const AssemblyPattern& pattern){return pattern.active&&((canonical(pattern.type)!="linear"&&canonical(pattern.type)!="circular")||pattern.source.t!=V::Null||!pattern.skips.empty());}

V assemblyPatternToJson(const AssemblyPattern& pattern){
    if(!pattern.active)return V{};V value=V::makeObj();value.set("type",V::makeStr(pattern.type));if(!pattern.count.empty())value.set("count",V::makeStr(pattern.count));
    if(pattern.type=="linear"){value.set("dx",V::makeStr(pattern.dx));value.set("dy",V::makeStr(pattern.dy));value.set("dz",V::makeStr(pattern.dz));}else if(pattern.type=="circular"){value.set("step",V::makeStr(pattern.step));value.set("axis",V::makeStr(pattern.axis));}
    if(pattern.source.t!=V::Null)value.set("source",pattern.source);if(!pattern.skips.empty()){V skips=V::makeArr();for(int copy:pattern.skips)skips.arr.push_back(V::makeNum(copy));value.set("skips",skips);}return value;
}
void readAssemblyPattern(AssemblyPattern& pattern,const V& value){
    if(value.t==V::Null){pattern={};return;}keys(value,{"type","count","dx","dy","dz","step","axis","source","skips"});AssemblyPattern next;next.active=true;next.type=canonical(text(value,"type"));
    if(next.type!="linear"&&next.type!="circular"&&next.type!="pattern_driven"&&next.type!="sketch_driven"&&next.type!="curve_driven"&&next.type!="chain")fail("UNSUPPORTED_TYPE: "+next.type);
    if(auto item=value.find("count"))next.count=expression(*item);else if(next.type!="pattern_driven"&&next.type!="sketch_driven")fail("FIELD_REQUIRED: count");
    auto take=[&](const char* key,std::string& to){if(auto item=value.find(key))to=expression(*item);};take("dx",next.dx);take("dy",next.dy);take("dz",next.dz);take("step",next.step);if(auto axis=value.find("axis")){next.axis=axis->asStr();if(next.axis!="X"&&next.axis!="Y"&&next.axis!="Z")fail("INVALID_AXIS: X/Y/Z required");}
    if(auto source=value.find("source"))next.source=*source;
    if(next.type=="pattern_driven"){keys(next.source,{"node"});need(next.source,"node");}
    else if(next.type=="sketch_driven"){keys(next.source,{"sketch","feature","points"});if(bool(next.source.find("sketch"))==bool(next.source.find("feature")))fail("POINT_SOURCE: choose sketch or spatial feature");if(next.source.find("sketch"))text(next.source,"sketch");const auto& points=need(next.source,"points");if(points.t!=V::Arr||points.arr.size()<2||points.arr.size()>32)fail("POINT_COUNT: 2..32 required");for(const auto& point:points.arr)if(point.t!=V::Str||point.str.empty()||point.str.size()>128)fail("INVALID_POINT_NAME");}
    else if(next.type=="curve_driven"||next.type=="chain"){
        keys(next.source,{"feature","reference","spacing","pitch","margin","alignment","normal","closed","endpoint","max_source_error","alternate_feature","alternate_node","alternate_angle"});need(next.source,"feature");text(next.source,"reference");const auto spacing=text(next.source,"spacing","equal"),alignment=text(next.source,"alignment","tangent"),endpoint=text(next.source,"endpoint","exclude");if(spacing!="equal"&&spacing!="pitch")fail("INVALID_SPACING: equal/pitch required");if(alignment!="fixed"&&alignment!="tangent")fail("INVALID_ALIGNMENT: fixed/tangent required");if(endpoint!="exclude"&&endpoint!="include")fail("INVALID_ENDPOINT: exclude/include required");if(spacing=="pitch")expression(need(next.source,"pitch"));if(alignment=="tangent"){const auto& normal=need(next.source,"normal");if(normal.t!=V::Arr||normal.arr.size()!=3)fail("INVALID_NORMAL: three coordinates required");for(const auto& n:normal.arr)expression(n);}
        for(const char* key:{"pitch","margin","max_source_error","alternate_angle"})if(auto item=next.source.find(key))expression(*item);boolean(next.source,"closed");if(next.type!="chain"&&(next.source.find("alternate_feature")||next.source.find("alternate_node")||next.source.find("alternate_angle")))fail("ALTERNATE_REQUIRES_CHAIN");if(next.source.find("alternate_feature")&&next.source.find("alternate_node"))fail("ALTERNATE_SOURCE: choose alternate_feature or alternate_node");
    }else if(next.source.t!=V::Null)fail("SOURCE_REQUIRES_SPECIALIZED_TYPE");
    if(auto skips=value.find("skips")){if(skips->t!=V::Arr||skips->arr.size()>31)fail("INVALID_SKIPS: at most 31 slots");std::set<int> seen;for(const auto& copy:skips->arr){if(copy.t!=V::Num||copy.num!=std::floor(copy.num)||copy.num<1||copy.num>31||!seen.insert((int)copy.num).second)fail("INVALID_SKIP: unique integer 1..31 required");next.skips.push_back((int)copy.num);}}
    pattern=std::move(next);
}
void bindAssemblyPatternSource(const Document& doc,AssemblyPattern& pattern){
    for(const char* key:{"feature","alternate_feature"})if(auto value=pattern.source.find(key))pattern.source.set(key,V::makeNum((double)bindFeature(doc,*value)));
    for(const char* key:{"node","alternate_node"})if(auto value=pattern.source.find(key))pattern.source.set(key,V::makeNum((double)bindNode(doc,*value)));
}
std::vector<int> assemblyPatternCopySlots(const Document& doc,const AssemblyNode& target){
    const auto env=doc.buildEnv();std::unordered_set<u64> active;std::unordered_map<u64,int> logicalCounts;
    std::function<std::vector<int>(const AssemblyNode&,int)> slots=[&](const AssemblyNode& n,int depth){
        if(depth>64||!active.insert(n.id).second)fail("DEPENDENCY_CYCLE: pattern source graph");std::vector<int> out;int total=1;
        if(n.pattern.active){
            if(n.pattern.type=="pattern_driven"){const auto& source=node(doc,identity(need(n.pattern.source,"node")));if(!source.pattern.active)fail("SOURCE_NOT_PATTERNED");out=slots(source,depth+1);total=count(n.pattern,env,logicalCounts.at(source.id));}
            else if(n.pattern.type=="sketch_driven"){const auto& points=need(n.pattern.source,"points");if(points.t!=V::Arr)fail("POINT_COUNT");total=count(n.pattern,env,(int)points.arr.size());}
            else total=count(n.pattern,env);
            skipValidation(n.pattern,total);
        }
        if(n.pattern.type!="pattern_driven"||!n.pattern.active)for(int i=0;i<total;++i)out.push_back(i);
        out.erase(std::remove_if(out.begin(),out.end(),[&](int i){return skipped(n.pattern,i);}),out.end());logicalCounts[n.id]=total;active.erase(n.id);return out;
    };return slots(target,0);
}
u64 assemblyPatternCopyFeature(const Document& doc,const AssemblyNode& target,int copy){
    if(copy<0||copy>31)fail("INVALID_COPY_SLOT");
    if(target.pattern.active&&target.pattern.type=="chain"&&(copy%2)){
        if(const auto* alternate=target.pattern.source.find("alternate_feature"))return feature(doc,identity(*alternate)).id;
        if(const auto* alternate=target.pattern.source.find("alternate_node"))return node(doc,identity(*alternate)).feature;
    }
    return target.feature;
}
bool validateAssemblyPatternAuthoring(const Document& doc,std::string& error){
    error.clear();try{
        auto structural=doc.authoringCopy();for(auto& n:structural.assembly)n.pattern={};structural.mates.clear();std::vector<AssemblyPlacement> seedPlacements;if(!resolveAssembly(structural,seedPlacements,error))return false;
        const auto env=doc.buildEnv();for(const auto& n:doc.assembly)if(n.pattern.active){
            if(!n.feature)fail("GROUP_UNSUPPORTED");AssemblyPattern parsed;readAssemblyPattern(parsed,assemblyPatternToJson(n.pattern));assemblyPatternCopySlots(doc,n);
            for(const char* key:{"node","alternate_node"})if(auto source=n.pattern.source.find(key)){const auto& bound=node(doc,identity(*source));if(std::string(key)=="alternate_node"&&(bound.id==n.id||bound.pattern.active))fail("INVALID_ALTERNATE_SEED");}
            for(const char* key:{"feature","alternate_feature"})if(auto source=n.pattern.source.find(key)){const auto id=identity(*source);feature(doc,id);if(std::string(key)=="feature")validateSourceDependencies(doc,id,n.id);}
            if(n.pattern.type=="sketch_driven"){
                std::set<std::string> available;
                if(const auto* sketch=n.pattern.source.find("sketch")){
                    const NamedSketch* owner=nullptr;for(const auto& sk:doc.sketches)if(sk.name==sketch->str){if(owner)fail("AMBIGUOUS_SKETCH");owner=&sk;}if(!owner)fail("SOURCE_MISSING: sketch "+sketch->str);for(const auto& e:owner->sk.ents)if(e.kind==SketchEnt::Pnt&&!e.name.empty()){if(e.dof.size()!=2||!available.insert(e.name).second)fail("AMBIGUOUS_POINT: "+e.name);}
                }else{
                    const auto& source=feature(doc,identity(need(n.pattern.source,"feature")));if(source.type!=FeatureType::Curve||!source.nativeSpec.find("segments"))fail("SPATIAL_POINTS_REQUIRE_AUTHORED_CURVE");available.insert("start");const auto& segments=need(source.nativeSpec,"segments");if(segments.t!=V::Arr)fail("INVALID_SPATIAL_SOURCE");for(size_t i=0;i<segments.arr.size();++i){const auto prefix="segment."+std::to_string(i+1);const auto& segment=segments.arr[i];if(segment.find("to")||segment.find("to_ref")||segment.find("points")||segment.find("poles"))available.insert(prefix+".end");if(segment.find("through")||segment.find("through_ref"))available.insert(prefix+".through");for(const char* field:{"points","poles"})if(auto controls=segment.find(field)){if(controls->t!=V::Arr)fail("INVALID_SPATIAL_SOURCE");for(size_t j=0;j<controls->arr.size();++j)available.insert(prefix+(std::string(field)=="points"?".point.":".pole.")+std::to_string(j+1));}}
                }
                std::set<std::string> seen;for(const auto& requested:need(n.pattern.source,"points").arr){auto name=requested.str;if(name.rfind("point.",0)==0)name=name.substr(6);if(!available.count(name))fail("SOURCE_POINT_MISSING: "+name);if(!seen.insert(name).second)fail("INVALID_POINT_NAME: duplicate semantic point");}
            }
            if(n.pattern.type=="curve_driven"||n.pattern.type=="chain"){
                const auto& source=n.pattern.source;const auto& definition=feature(doc,identity(need(source,"feature")));if(text(source,"reference")=="path"&&definition.type!=FeatureType::Curve&&definition.type!=FeatureType::IntersectionCurve&&definition.type!=FeatureType::Bridge)fail("PATH_REQUIRES_CURVE");
                for(const char* key:{"pitch","margin","max_source_error","alternate_angle"})if(auto field=source.find(key)){const double value=scalar(*field,env,key);if(std::string(key)=="pitch"&&value<=1e-9)fail("ZERO_SPACING");if(std::string(key)=="margin"&&value<0)fail("INVALID_MARGIN");if(std::string(key)=="max_source_error"&&(value<=0||value>1))fail("SOURCE_ERROR_BOUND");}
                if(text(source,"alignment","tangent")=="tangent")unit(vector(need(source,"normal"),env,"normal"),"normal");if(boolean(source,"closed")&&text(source,"endpoint","exclude")=="include")fail("CLOSED_ENDPOINT_DUPLICATE");
            }
        }return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}

bool expandAssemblyPatterns(const Document& doc,const std::unordered_map<u64,Mat4>& parentFrames,
    const std::unordered_map<u64,Mat4>& rigid,const std::unordered_map<u64,Mat4>& world,
    const std::unordered_map<u64,bool>& visibility,std::vector<AssemblyPlacement>& placements,
    std::string& error,V* reports){
    placements.clear();if(reports)*reports=V::makeArr();
    try{
        const auto env=doc.buildEnv();std::unordered_map<u64,std::vector<AssemblyPlacement>> resolved;std::unordered_map<u64,int> slotCounts;std::unordered_set<u64> active;V receipts=V::makeArr();
        std::function<void(const AssemblyNode&,int)> expand=[&](const AssemblyNode& n,int depth){
            if(resolved.count(n.id))return;if(depth>64||!active.insert(n.id).second)fail("DEPENDENCY_CYCLE: pattern source graph exceeds 64 levels or cycles");
            if(!n.feature)fail("GROUP_UNSUPPORTED: only part occurrences can carry a pattern");
            std::vector<AssemblyPlacement> copies;V report=V::makeObj();report.set("node",V::makeNum((double)n.id));report.set("type",V::makeStr(n.pattern.active?n.pattern.type:"none"));
            const Mat4 seed=world.at(n.id),parent=parentFrames.at(n.id),local=rigid.at(n.id),carry=seed*(parent*local).inverted();
            auto emit=[&](int slot,const Mat4& delta,u64 definition=0,const Mat4* alternate=nullptr){if(n.pattern.active&&skipped(n.pattern,slot))return;const Mat4 placed=slot==0?seed:carry*parent*delta*(alternate?*alternate:local);validateFrame(placed);copies.push_back({n.id,definition?definition:n.feature,n.name,placed,visibility.at(n.id),slot});};
            if(!n.pattern.active){slotCounts[n.id]=1;emit(0,Mat4{});}
            else if(n.pattern.type=="linear"||n.pattern.type=="circular"){
                const int total=count(n.pattern,env);slotCounts[n.id]=total;skipValidation(n.pattern,total);const double dx=scalar(V::makeStr(n.pattern.dx),env,"dx"),dy=scalar(V::makeStr(n.pattern.dy),env,"dy"),dz=scalar(V::makeStr(n.pattern.dz),env,"dz"),step=scalar(V::makeStr(n.pattern.step),env,"step");
                if(n.pattern.type=="linear"&&Vec3{dx,dy,dz}.len()<=1e-9)fail("ZERO_SPACING: linear spacing is zero");if(n.pattern.type=="circular"&&(std::abs(step)<=0||std::abs(step)>360))fail("INVALID_STEP: circular degrees must be nonzero and <=360");const Vec3 axis=n.pattern.axis=="X"?Vec3{1,0,0}:n.pattern.axis=="Y"?Vec3{0,1,0}:Vec3{0,0,1};if(n.pattern.type=="circular"&&n.pattern.axis!="X"&&n.pattern.axis!="Y"&&n.pattern.axis!="Z")fail("INVALID_AXIS");
                for(int slot=0;slot<total;++slot)emit(slot,n.pattern.type=="linear"?Mat4::translation(Vec3{dx,dy,dz}*slot):Mat4::axisAngle(axis,deg2rad(step*slot)));
            }else if(n.pattern.type=="pattern_driven"){
                const auto& source=node(doc,identity(need(n.pattern.source,"node")));if(!source.pattern.active)fail("SOURCE_NOT_PATTERNED: choose a patterned occurrence");expand(source,depth+1);const auto& sourceCopies=resolved.at(source.id);const int total=count(n.pattern,env,slotCounts.at(source.id));slotCounts[n.id]=total;skipValidation(n.pattern,total);const Mat4 source0=world.at(source.id);
                // World-space deltas retain rotated source parents and solved
                // source mates. Target mates carry the whole inherited pattern.
                const Mat4 targetCarry=seed*(parent*local).inverted();for(const auto& p:sourceCopies)if(!skipped(n.pattern,p.copy)){const Mat4 placed=p.copy?targetCarry*(p.world*source0.inverted())*(parent*local):seed;validateFrame(placed);copies.push_back({n.id,n.feature,n.name,placed,visibility.at(n.id),p.copy});}report.set("source_node",V::makeNum((double)source.id));report.set("source_linked",V::makeBool(true));
            }else if(n.pattern.type=="sketch_driven"){
                const auto points=sketchPoints(doc,n,env);const int total=count(n.pattern,env,(int)points.size());slotCounts[n.id]=total;skipValidation(n.pattern,total);for(int slot=0;slot<total;++slot)emit(slot,Mat4::translation(points[slot]-points[0]));report.set("source_linked",V::makeBool(true));report.set("source_point_count",V::makeNum(total));report.set("source_coordinates",V::makeStr(n.pattern.source.find("sketch")?"solved_named_sketch_points_in_datum_frame":"solved_spatial_curve_named_handles_XYZ"));
            }else if(n.pattern.type=="curve_driven"||n.pattern.type=="chain"){
                const int total=count(n.pattern,env);slotCounts[n.id]=total;skipValidation(n.pattern,total);const auto path=guide(doc,n,env,report);const auto& source=n.pattern.source;const double margin=number(source,env,"margin",0);if(margin<0||margin*2>=path.length)fail("INVALID_MARGIN: positive remaining guide length required");if(path.closed&&margin!=0)fail("CLOSED_MARGIN_UNSUPPORTED: closed paths require zero margin");const bool include=text(source,"endpoint","exclude")=="include";if(path.closed&&include)fail("CLOSED_ENDPOINT_DUPLICATE: closed paths require endpoint exclude");const bool pitch=text(source,"spacing","equal")=="pitch";double spacing=pitch?number(source,env,"pitch",0):(path.length-2*margin)/(path.closed||!include?total:total-1);if(spacing<=1e-9)fail("ZERO_SPACING: positive pitch required");
                if(!path.closed&&margin+spacing*(total-1)>path.length-margin+connectionTolerance)fail("SPACING_OVERRUN: count and pitch exceed the usable guide");if(!path.closed&&!include&&margin+spacing*(total-1)>=path.length-margin-1e-8)fail("ENDPOINT_EXCLUDED: final pitch station reaches the excluded endpoint");if(path.closed&&std::abs(spacing*total-path.length)>connectionTolerance)fail("CLOSED_PITCH_MISMATCH: count times pitch must close the guide within 1e-6 mm");
                std::vector<double> distances;for(int slot=0;slot<total;++slot)distances.push_back(margin+spacing*slot);const auto values=stations(path,distances,env);const bool align=text(source,"alignment","tangent")=="tangent";Vec3 normal=align?unit(vector(need(source,"normal"),env,"normal"),"normal"):Vec3{};if(align&&!values[0].hasTangent)fail("UNDEFINED_TANGENT: choose fixed alignment or a regular guide");Mat4 first=align?tangentFrame(values[0].point,values[0].tangent,normal):Mat4::translation(values[0].point);
                u64 alternateDefinition=0;Mat4 alternateLocal;const Mat4* alternate=nullptr;if(n.pattern.type=="chain"){
                    if(auto id=source.find("alternate_feature"))alternateDefinition=feature(doc,identity(*id)).id;
                    if(auto id=source.find("alternate_node")){const auto& second=node(doc,identity(*id));if(second.id==n.id||second.pattern.active)fail("INVALID_ALTERNATE_SEED: distinct unpatterned occurrence required");alternateDefinition=second.feature;alternateLocal=parent.inverted()*world.at(second.id);alternate=&alternateLocal;}
                    const double angle=number(source,env,"alternate_angle",alternate?0:90);alternateLocal=(alternate?alternateLocal:local)*Mat4::rotX(deg2rad(angle));alternate=&alternateLocal;
                    if(path.closed&&total%2&&alternateDefinition)fail("CLOSED_ALTERNATION: two-seed closed chain requires even count");
                }
                for(int slot=0;slot<total;++slot){
                    if(align&&!values[slot].hasTangent)fail("UNDEFINED_TANGENT: choose fixed alignment or a regular guide");const Mat4 frame=align?tangentFrame(values[slot].point,values[slot].tangent,normal):Mat4::translation(values[slot].point);Mat4 rotation=frame*first.inverted();rotation.m[12]=rotation.m[13]=rotation.m[14]=0;
                    // Rotate about the primary seed origin and retain station
                    // offsets independently of the guide's absolute origin.
                    const Vec3 pivot=local.transformPoint({}),offset=values[slot].point-values[0].point;const Mat4 delta=Mat4::translation(offset+pivot)*rotation*Mat4::translation(-pivot);
                    emit(slot,delta,slot%2?alternateDefinition:0,slot%2?alternate:nullptr);
                }
                report.set("source_linked",V::makeBool(true));report.set("spacing",V::makeStr(pitch?"declared_arc_length_pitch":"equal_arc_length"));report.set("pitch_mm",V::makeNum(spacing));report.set("alignment",V::makeStr(align?"relative_tangent_and_declared_normal":"fixed_seed_orientation"));report.set("sampled_frames",V::makeNum(total));report.set("seed_anchor",V::makeStr("slot0 retains authored/solved seed pose; source stations are relative to station0"));report.set("link_contact_checked",V::makeBool(false));report.set("motion_checked",V::makeBool(false));report.set("manufacturing_qualified",V::makeBool(false));if(path.closed)report.set("closure_pitch_residual_mm",V::makeNum(std::abs(spacing*total-path.length)));
            }else fail("UNSUPPORTED_TYPE: "+n.pattern.type);
            active.erase(n.id);if(n.pattern.active){report.set("emitted_copies",V::makeNum((double)copies.size()));report.set("stable_copy_slots",V::makeBool(true));receipts.arr.push_back(report);}resolved[n.id]=std::move(copies);
        };
        for(const auto& n:doc.assembly)if(n.feature)expand(n,0);for(const auto& n:doc.assembly)if(n.feature){const auto& copies=resolved.at(n.id);if(placements.size()+copies.size()>65536)fail("PLACEMENT_CAP: expanded assembly exceeds 65536 copies");placements.insert(placements.end(),copies.begin(),copies.end());}if(reports)*reports=std::move(receipts);return true;
    }catch(const std::exception& e){error=e.what();placements.clear();if(reports)*reports=V::makeArr();return false;}
}
}
