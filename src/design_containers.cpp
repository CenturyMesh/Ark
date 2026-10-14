#include "ark/design_containers.hpp"
#include "ark/serialize.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
using Keys=std::set<std::string>;
const char* const collections[]={"params","planes","sketches","materials","features","assembly","mates"};
[[noreturn]] void reject(const std::string& code,const std::string& message){throw std::runtime_error(code+": "+message);}
const V& field(const V& v,const char* key){static const V empty;const auto* p=v.find(key);return p?*p:empty;}
void erase(V& v,const char* key){v.obj.erase(std::remove_if(v.obj.begin(),v.obj.end(),[&](const auto& kv){return kv.first==key;}),v.obj.end());}
void canonical(V& v){if(v.t==V::Obj){for(auto& kv:v.obj)canonical(kv.second);std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(v.t==V::Arr)for(auto& row:v.arr)canonical(row);}
std::string canonicalText(V v){canonical(v);return v.dump();}
std::string digest(const V& v){return iterationSha256(canonicalText(v));}
u64 id(const V& v){if(v.t!=V::Num||!std::isfinite(v.num)||v.num<0||v.num>9007199254740991.0||std::floor(v.num)!=v.num)reject("CONTAINER_INVALID_ID","Expected an exact nonnegative integer ID");return (u64)v.num;}
V number(u64 value){return V::makeNum((double)value);}
V sourceRoot(const Document& doc){V root;if(!arkjson::parse(documentToJson(doc,false,false),root))reject("CONTAINER_SOURCE","Unable to serialize authored source");erase(root,"design_container_checkout");for(const auto* collection:collections)if(!root.find(collection))root.set(collection,V::makeArr());root.set("next_feature_id",number(doc.nextFeatureId_));root.set("next_assembly_id",number(doc.nextAssemblyId_));root.set("next_mate_id",number(doc.nextMateId_));return root;}
std::string rowKey(const char* collection,const V& row){return std::string(collection)+":"+((std::string(collection)=="features"||std::string(collection)=="assembly"||std::string(collection)=="mates")?std::to_string(id(field(row,"id"))):field(row,"name").asStr());}
using Rows=std::map<std::string,V>;
const V* findRow(const Rows& source,const std::string& key);
Rows rows(const V& root){Rows out;for(const auto* collection:collections)for(const auto& row:field(root,collection).arr)out.emplace(rowKey(collection,row),row);return out;}
Keys keySet(const V& value){Keys result;for(const auto& row:value.arr){if(row.t!=V::Str)reject("CONTAINER_MANIFEST_INVALID","Ownership entries must be strings");if(!result.insert(row.str).second)reject("CONTAINER_MANIFEST_INVALID","Duplicate ownership entry");}return result;}
V keyArray(const Keys& keys){V out=V::makeArr();for(const auto& key:keys)out.arr.push_back(V::makeStr(key));return out;}
bool numericCollection(const std::string& key){return key.rfind("features:",0)==0||key.rfind("assembly:",0)==0||key.rfind("mates:",0)==0;}

// The source graph is deliberately conservative: authored expression tokens
// bind parameters; named native/sketch/plane references bind source objects.
// Exact numeric feature/occurrence reference fields bind their stable IDs.
struct Graph {
    Rows all;
    std::map<std::string,Keys> names;
    std::map<std::string,Keys> edges;
    explicit Graph(const V& root):all(rows(root)){
        for(const auto& kv:all){const auto name=field(kv.second,"name").asStr();if(!name.empty())names[name].insert(kv.first);}
        for(const auto& kv:all){Keys refs;scan(kv.second,"",refs);if(kv.first.rfind("mates:",0)==0)for(const auto* endpoint:{"a","b"}){const auto node="assembly:"+std::to_string(id(field(kv.second,endpoint)));if(all.count(node))refs.insert(node);}refs.erase(kv.first);edges[kv.first]=std::move(refs);}
    }
    void named(const std::string& text,Keys& refs) const {
        auto found=names.find(text);if(found!=names.end())refs.insert(found->second.begin(),found->second.end());
        for(size_t at=0;at<text.size();){if(!(std::isalpha((unsigned char)text[at])||text[at]=='_')){++at;continue;}const auto start=at++;while(at<text.size()&&(std::isalnum((unsigned char)text[at])||text[at]=='_'))++at;const auto key="params:"+text.substr(start,at-start);if(all.count(key))refs.insert(key);}
    }
    void scan(const V& value,const std::string& key,Keys& refs) const {
        if(key=="name"||key=="id"||key=="part"||key=="mesh"||key=="appearance"||key=="component_metadata"||key=="package"||key=="topology"||key=="source_topology")return;
        if(value.t==V::Str){named(value.str,refs);return;}
        if(value.t==V::Num){const bool feature=key=="feature"||key=="second_feature"||key=="source_feature"||key=="alternate_feature"||key=="inputs"||key=="curve";const bool node=key=="node"||key=="source_instance"||key=="alternate_node"||key=="instances"||key=="parent";if(feature||node){const auto k=std::string(feature?"features:":"assembly:")+std::to_string(id(value));if(all.count(k))refs.insert(k);}return;}
        if(value.t==V::Arr){for(const auto& row:value.arr)scan(row,key,refs);return;}
        if(value.t==V::Obj)for(const auto& kv:value.obj)scan(kv.second,kv.first,refs);
    }
    Keys closure(Keys seed) const {std::vector<std::string> pending(seed.begin(),seed.end());for(size_t i=0;i<pending.size();++i){const auto it=edges.find(pending[i]);if(it==edges.end())continue;for(const auto& ref:it->second)if(seed.insert(ref).second)pending.push_back(ref);}return seed;}
};

u64 containerId(const Document& doc,const V& args){const auto& selection=args.find("node")?field(args,"node"):field(args,"container");u64 result=0;if(selection.t==V::Num)result=id(selection);else if(selection.t==V::Str){for(const auto& node:doc.assembly)if(node.name==selection.str){if(result)reject("CONTAINER_AMBIGUOUS","Container name is ambiguous; use its ID");result=node.id;}}else reject("CONTAINER_REQUIRED","Pass an existing assembly group as node ID or unique name");for(const auto& node:doc.assembly)if(node.id==result){if(node.feature)reject("CONTAINER_NOT_GROUP","A container must be an existing feature=0 assembly group");return result;}reject("CONTAINER_MISSING","Assembly group not found");}
void expected(u64 revision,const V& args){const auto* observed=args.find("expected_revision");if(!observed)reject("CONTAINER_REVISION_REQUIRED","Read doc_info and pass expected_revision");if(id(*observed)!=revision)reject("CONTAINER_REVISION_CONFLICT","The observed document revision is stale");}
struct Checkout {V root,manifest;Keys editable,readonly;};
std::set<u64> occurrenceFeatures(const Document& doc,const AssemblyNode& node){std::set<u64> result;if(node.feature)result.insert(node.feature);if(node.pattern.active)for(int slot:assemblyPatternCopySlots(doc,node)){const auto feature=assemblyPatternCopyFeature(doc,node,slot);if(feature)result.insert(feature);}return result;}
Checkout makeCheckout(const Document& doc,u64 revision,u64 group){
    if(doc.designContainerCheckout.t==V::Obj)reject("CONTAINER_NESTED_CHECKOUT","Checkout from the owning source document, not from another checkout");
    if(doc.skeleton.t==V::Obj)reject("CONTAINER_SKELETON_BOUNDARY","Skeleton ownership is global; isolated checkout of that source is not supported");
    const V source=sourceRoot(doc);Graph graph(source);std::set<u64> nodes{group};bool changed=true;
    while(changed){changed=false;for(const auto& node:doc.assembly)if(nodes.count(node.parent)&&nodes.insert(node.id).second)changed=true;}
    Keys owned;for(const auto& node:doc.assembly)if(nodes.count(node.id)){owned.insert("assembly:"+std::to_string(node.id));for(u64 feature:occurrenceFeatures(doc,node))owned.insert("features:"+std::to_string(feature));}
    for(const auto& node:doc.assembly)if(!nodes.count(node.id))for(u64 feature:occurrenceFeatures(doc,node))if(owned.count("features:"+std::to_string(feature)))reject("CONTAINER_SHARED_DEFINITION","Feature "+std::to_string(feature)+" has an occurrence outside this group; choose a container containing every occurrence or author a separate definition");
    for(const auto& mate:doc.mates){const bool a=nodes.count(mate.a)!=0,b=nodes.count(mate.b)!=0;if(a!=b)reject("CONTAINER_CROSS_BOUNDARY_MATE","Mate "+std::to_string(mate.id)+" crosses the group boundary; checkout cannot drop or freeze that constraint");if(a&&b)owned.insert("mates:"+std::to_string(mate.id));}
    // Parent groups supply the exact authored frame without exporting siblings.
    Keys retained=graph.closure(owned);
    for(const auto& key:retained)if(key.rfind("assembly:",0)==0&&!nodes.count(id(field(graph.all.at(key),"id")))){const auto& row=graph.all.at(key);if(id(field(row,"feature")))reject("CONTAINER_CROSS_BOUNDARY_INSTANCE","An occurrence reference leaves this group; isolate its producer explicitly before checkout");}
    // Outside constraints can solve an ancestor frame through another sibling.
    // Without that full solve, a child cannot claim the same occurrence pose.
    for(const auto& mate:doc.mates)if(!nodes.count(mate.a)&&!nodes.count(mate.b))for(u64 endpoint:{mate.a,mate.b}){
        std::set<u64> seen;while(endpoint&&seen.insert(endpoint).second){const auto key="assembly:"+std::to_string(endpoint);if(retained.count(key))reject("CONTAINER_ANCESTOR_MATE_BOUNDARY","Outside mate "+std::to_string(mate.id)+" controls a retained ancestor frame");const auto* row=findRow(graph.all,key);if(!row)break;endpoint=id(field(*row,"parent"));}
    }
    Keys outside;for(const auto& kv:graph.all)if(!owned.count(kv.first)){
        if(numericCollection(kv.first)||!retained.count(kv.first))outside.insert(kv.first);
    }
    outside=graph.closure(outside);
    // Drawings and canvas source dependencies remain parent-owned and are never
    // created/copied as a side effect of isolated design work.
    for(const auto* collection:{"drawings","canvases"})for(const auto& row:field(source,collection).arr){Keys refs;graph.scan(row,"",refs);const auto closure=graph.closure(refs);outside.insert(closure.begin(),closure.end());}
    for(const auto& kv:graph.all)if((kv.first.rfind("features:",0)==0&&!owned.count(kv.first))||(!numericCollection(kv.first)&&!retained.count(kv.first))){const auto closure=graph.closure({kv.first});for(const auto& key:owned)if(key.rfind("features:",0)==0&&closure.count(key))reject("CONTAINER_EXTERNAL_CONSUMER","Outside source "+kv.first+" consumes an editable definition; expand the container or remove that dependency explicitly");}
    Checkout checkout;checkout.editable=owned;
    for(const auto& key:retained)if(!numericCollection(key)&&!outside.count(key))checkout.editable.insert(key);
    for(const auto& key:retained)if(!checkout.editable.count(key))checkout.readonly.insert(key);
    checkout.root=source;
    for(const auto* collection:collections){V subset=V::makeArr();for(const auto& row:field(source,collection).arr)if(retained.count(rowKey(collection,row)))subset.arr.push_back(row);checkout.root.set(collection,subset);}
    erase(checkout.root,"drawings");erase(checkout.root,"canvases");erase(checkout.root,"skeleton");
    // No audit pointer/usage/history crosses documents. The child session starts
    // its own audit when it is opened and edited.
    V manifest=V::makeObj();manifest.set("format",V::makeStr("ark.design-container.v1"));manifest.set("container_node",number(group));manifest.set("source_revision",number(revision));manifest.set("source_sha256",V::makeStr(digest(source)));manifest.set("baseline_sha256",V::makeStr(digest(checkout.root)));manifest.set("baseline",checkout.root);manifest.set("editable",keyArray(checkout.editable));manifest.set("readonly",keyArray(checkout.readonly));checkout.manifest=manifest;
    return checkout;
}

const V* findRow(const Rows& source,const std::string& key){auto it=source.find(key);return it==source.end()?nullptr:&it->second;}
void guardWorkspace(const V& manifest,const V& current){
    std::string error;if(!validateDesignContainerManifest(manifest,error))reject("CONTAINER_MANIFEST_INVALID",error);
    const auto& baseline=field(manifest,"baseline");const Rows old=rows(baseline),now=rows(current);const Keys editable=keySet(field(manifest,"editable")),readonly=keySet(field(manifest,"readonly"));
    for(const auto& key:readonly){const auto* a=findRow(old,key),*b=findRow(now,key);if(!a||!b||canonicalText(*a)!=canonicalText(*b))reject("CONTAINER_READONLY_DEPENDENCY","External source "+key+" must remain unchanged");}
    const auto group=id(field(manifest,"container_node"));std::set<u64> nodes{group};bool changed=true;while(changed){changed=false;for(const auto& row:field(current,"assembly").arr)if(nodes.count(id(field(row,"parent")))&&nodes.insert(id(field(row,"id"))).second)changed=true;}
    const auto* rootNode=findRow(now,"assembly:"+std::to_string(group));const auto* oldRoot=findRow(old,"assembly:"+std::to_string(group));if(!rootNode||!oldRoot||id(field(*rootNode,"feature"))||canonicalText(field(*rootNode,"parent"))!=canonicalText(field(*oldRoot,"parent")))reject("CONTAINER_ROOT_BOUNDARY","The checked-out root group and its original parent are retained");
    for(const auto& row:field(current,"assembly").arr){const auto key=rowKey("assembly",row);if(!readonly.count(key)&&!nodes.count(id(field(row,"id"))))reject("CONTAINER_NODE_BOUNDARY","Editable/new occurrences must remain descendants of the checked-out group");}
    for(const auto& row:field(current,"mates").arr)if(!nodes.count(id(field(row,"a")))||!nodes.count(id(field(row,"b"))))reject("CONTAINER_MATE_BOUNDARY","Every child mate endpoint must remain in the editable group");
    if(!field(current,"drawings").arr.empty()||!field(current,"canvases").arr.empty()||field(current,"skeleton").t==V::Obj)reject("CONTAINER_UNSUPPORTED_SOURCE","Drawings, canvases and global Skeleton source cannot be introduced through a container merge");
    for(const auto& kv:old)if(!editable.count(kv.first)&&!readonly.count(kv.first))reject("CONTAINER_MANIFEST_INVALID","A baseline source has no declared ownership");
    for(const auto& kv:now)if(!old.count(kv.first)&&numericCollection(kv.first)){
        const auto value=id(field(kv.second,"id"));const char* counter=kv.first.rfind("features:",0)==0?"next_feature_id":kv.first.rfind("assembly:",0)==0?"next_assembly_id":"next_mate_id";
        if(value<id(field(baseline,counter)))reject("CONTAINER_ID_REUSE","New objects must use IDs at or above the checkout's retained next-ID counter");
    }
    for(const auto* counter:{"next_feature_id","next_assembly_id","next_mate_id"})if(current.find(counter)&&id(field(current,counter))<id(field(baseline,counter)))reject("CONTAINER_ID_REUSE","Next-ID counters cannot move backwards");
}

V command(const char* op){V out=V::makeObj();out.set("op",V::makeStr(op));return out;}
void append(V& commands,V c){if(commands.arr.size()>=128)reject("CONTAINER_COMMAND_LIMIT","Merge exceeds the existing 128-command atomic recipe limit; make a smaller coherent checkout edit");commands.arr.push_back(std::move(c));}
using IdMap=std::map<u64,u64>;
u64 mapped(u64 value,const IdMap& mapping){const auto it=mapping.find(value);return it==mapping.end()?value:it->second;}
void remap(V& value,const std::string& key,const IdMap& features,const IdMap& nodes){
    if(value.t==V::Num){const bool f=key=="feature"||key=="second_feature"||key=="source_feature"||key=="alternate_feature"||key=="inputs"||key=="curve";const bool n=key=="node"||key=="source_instance"||key=="alternate_node"||key=="instances"||key=="parent";if(f||n)value=number(mapped(id(value),f?features:nodes));}
    else if(value.t==V::Obj){for(auto& kv:value.obj)if(kv.first!="mesh"&&kv.first!="part"&&kv.first!="package"&&kv.first!="component_metadata")remap(kv.second,kv.first,features,nodes);}
    else if(value.t==V::Arr)for(auto& row:value.arr)remap(row,key,features,nodes);
}
bool equal(const V& a,const V& b){return canonicalText(a)==canonicalText(b);}
void noRemovedParams(const V& before,const V& after){for(const auto& kv:before.obj)if(!after.find(kv.first.c_str()))reject("CONTAINER_UNSUPPORTED_PARAMETER_REMOVAL","Existing placement/datum parameter keys cannot be removed by the current edit contract");}
V featureCommand(const V& row,const V* before){V c=command(before?"feature.edit":"feature.add");for(const auto& kv:row.obj){if(kv.first=="id"){if(before)c.set("id",kv.second);}else if(kv.first=="op"){if(!before)c.set("mode",kv.second);}else if(kv.first=="type"||kv.first=="inputs"){if(!before)c.set(kv.first,kv.second);}else if(!before||!equal(field(*before,kv.first.c_str()),kv.second))c.set(kv.first,kv.second);}if(before){for(const auto* key:{"part","material","appearance","manufacturing"})if(before->find(key)&&!row.find(key))c.set(key,V{});if(before->find("suppressed")&&!row.find("suppressed"))c.set("suppressed",V::makeBool(false));V removed=V::makeArr();for(const auto& kv:field(*before,"p").obj)if(!field(row,"p").find(kv.first.c_str()))removed.arr.push_back(V::makeStr(kv.first));if(!removed.arr.empty())c.set("p_remove",removed);}return c;}
}

bool validateDesignContainerManifest(const V& manifest,std::string& error){
    try{
        if(manifest.isNull())return true;
        if(manifest.t!=V::Obj||field(manifest,"format").asStr()!="ark.design-container.v1")reject("CONTAINER_MANIFEST_INVALID","Unsupported checkout manifest");
        const Keys fields{"format","container_node","source_revision","source_sha256","baseline_sha256","baseline","editable","readonly"};for(const auto& kv:manifest.obj)if(!fields.count(kv.first))reject("CONTAINER_MANIFEST_INVALID","Unknown checkout manifest field");
        if(!id(field(manifest,"container_node")))reject("CONTAINER_MANIFEST_INVALID","Container ID must be positive");id(field(manifest,"source_revision"));
        for(const auto* key:{"source_sha256","baseline_sha256"}){const auto& value=field(manifest,key);if(value.t!=V::Str||value.str.size()!=64||value.str.find_first_not_of("0123456789abcdef")!=std::string::npos)reject("CONTAINER_MANIFEST_INVALID","Invalid SHA-256 digest");}
        const auto& baseline=field(manifest,"baseline");if(baseline.t!=V::Obj||baseline.find("design_container_checkout")||baseline.find("agent_activity")||baseline.find("usage")||baseline.find("iteration_history")||digest(baseline)!=field(manifest,"baseline_sha256").str)reject("CONTAINER_MANIFEST_INVALID","Retained baseline hash/source is invalid");
        if(field(manifest,"editable").t!=V::Arr||field(manifest,"readonly").t!=V::Arr)reject("CONTAINER_MANIFEST_INVALID","Ownership arrays are required");const auto editable=keySet(field(manifest,"editable")),readonly=keySet(field(manifest,"readonly"));const auto all=rows(baseline);for(const auto& key:editable)if(!all.count(key)||readonly.count(key))reject("CONTAINER_MANIFEST_INVALID","Invalid/overlapping editable source");for(const auto& key:readonly)if(!all.count(key))reject("CONTAINER_MANIFEST_INVALID","Unknown readonly source");if(editable.size()+readonly.size()!=all.size())reject("CONTAINER_MANIFEST_INVALID","Every baseline source needs ownership");
        const auto key="assembly:"+std::to_string(id(field(manifest,"container_node")));const auto* group=findRow(all,key);if(!group||!editable.count(key)||id(field(*group,"feature")))reject("CONTAINER_MANIFEST_INVALID","Editable group identity is missing");
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

void validateDesignContainerWorkspace(const Document& doc){if(doc.designContainerCheckout.t==V::Obj)guardWorkspace(doc.designContainerCheckout,sourceRoot(doc));}
void validateDesignContainerTransition(const Document& before,const Document& after){if(before.designContainerCheckout.t==V::Obj&&!equal(before.designContainerCheckout,after.designContainerCheckout))reject("CONTAINER_MANIFEST_CHANGED","Keep the retained checkout manifest unchanged while editing");validateDesignContainerWorkspace(after);}

V inspectDesignContainers(const Document& doc,u64 revision,const V& args){
    V out=V::makeObj(),groups=V::makeArr();out.set("revision",number(revision));out.set("authority",V::makeStr("assembly_group"));
    for(const auto& node:doc.assembly)if(!node.feature){V group=V::makeObj();group.set("container",number(node.id));group.set("name",V::makeStr(node.name));group.set("parent",number(node.parent));groups.arr.push_back(group);}out.set("containers",groups);
    if(doc.designContainerCheckout.t==V::Obj){validateDesignContainerWorkspace(doc);out.set("workspace",doc.designContainerCheckout);out.set("isolated_document",V::makeBool(true));}
    if(args.find("node")||args.find("container")){const auto checkout=makeCheckout(doc,revision,containerId(doc,args));V scope=V::makeObj();scope.set("editable",keyArray(checkout.editable));scope.set("readonly",keyArray(checkout.readonly));scope.set("features",number(field(checkout.root,"features").arr.size()));scope.set("instances",number(field(checkout.root,"assembly").arr.size()));scope.set("source_sha256",field(checkout.manifest,"source_sha256"));out.set("scope",scope);}
    return out;
}

V checkoutDesignContainer(const Document& doc,u64 revision,const V& args){
    expected(revision,args);const auto checkout=makeCheckout(doc,revision,containerId(doc,args));V root=checkout.root;root.set("design_container_checkout",checkout.manifest);root.set("version",number(38));Document child;std::string error;if(!documentFromJson(root.dump(),child,&error))reject("CONTAINER_CHECKOUT_INVALID",error);validateDesignContainerWorkspace(child);
    V out=V::makeObj();out.set("revision",number(revision));out.set("container",field(checkout.manifest,"container_node"));out.set("manifest",checkout.manifest);out.set("isolated_document",V::makeBool(true));out.set("editable_feature_count",number(child.features.size()-std::count_if(child.features.begin(),child.features.end(),[&](const Feature& f){return checkout.readonly.count("features:"+std::to_string(f.id));})));out.set("closure_feature_count",number(child.features.size()));
    if(const auto* path=args.find("path")){if(path->t!=V::Str||path->str.empty())reject("CONTAINER_PATH_REQUIRED","A nonempty checkout file path is required");const auto target=std::filesystem::path(reinterpret_cast<const char8_t*>(path->str.c_str()));if(target.extension()!=".ark")reject("CONTAINER_PATH_EXTENSION","Checkout uses an explicit .ark document file");std::error_code ec;if(std::filesystem::exists(target,ec))reject("CONTAINER_PATH_EXISTS","Checkout cannot overwrite an existing project or checkout");if(ec)reject("CONTAINER_PATH_INVALID",ec.message());
        static std::atomic<u64> serial{0};const auto unique=iterationSha256(path->str+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+std::to_string(++serial));auto staging=target;staging+=".checkout-"+unique+".ark";const auto stagingBytes=staging.u8string();const std::string stagingName(stagingBytes.begin(),stagingBytes.end());
        if(!saveDocument(child,stagingName.c_str()))reject("CONTAINER_SAVE_FAILED","Unable to save the isolated child document");std::filesystem::create_hard_link(staging,target,ec);std::error_code cleanup;std::filesystem::remove(staging,cleanup);if(ec)reject("CONTAINER_PUBLISH_FAILED","Atomic exclusive checkout publication failed: "+ec.message());out.set("path",*path);}
    else out.set("document",root);
    return out;
}

V proposeDesignContainerMerge(const Document& doc,u64 revision,const V& args){
    expected(revision,args);if(doc.designContainerCheckout.t==V::Obj)reject("CONTAINER_MERGE_TARGET","Apply checkout changes to the owning source document");
    Document child;std::string error;if(const auto* path=args.find("path")){if(path->t!=V::Str||path->str.empty()||!loadDocument(path->str.c_str(),child,&error))reject("CONTAINER_LOAD_FAILED",error.empty()?"Unable to load child .ark":error);}else if(const auto* root=args.find("document")){if(!documentFromJson(root->dump(),child,&error))reject("CONTAINER_LOAD_FAILED",error);}else reject("CONTAINER_PATH_REQUIRED","Pass path to an edited checkout .ark or its authored document");
    const auto& manifest=child.designContainerCheckout;if(manifest.t!=V::Obj)reject("CONTAINER_MANIFEST_REQUIRED","Child document has no retained checkout provenance");if(!validateDesignContainerManifest(manifest,error))reject("CONTAINER_MANIFEST_INVALID",error);
    if(id(field(manifest,"source_revision"))!=revision)reject("CONTAINER_REVISION_CONFLICT","The source changed since checkout; create a fresh checkout and explicitly transfer intended edits");if(digest(sourceRoot(doc))!=field(manifest,"source_sha256").str)reject("CONTAINER_SOURCE_CONFLICT","Source authoring differs from the retained checkout baseline");
    const auto group=id(field(manifest,"container_node"));const auto authoritative=makeCheckout(doc,revision,group);if(!equal(authoritative.manifest,manifest))reject("CONTAINER_SCOPE_CONFLICT","Child baseline/ownership differs from the source-defined checkout scope");validateDesignContainerWorkspace(child);
    const auto current=sourceRoot(child),baseline=field(manifest,"baseline"),parent=sourceRoot(doc);const auto childHash=digest(current);if(const auto* reviewed=args.find("expected_child_sha256");reviewed&&(reviewed->t!=V::Str||reviewed->str!=childHash))reject("CONTAINER_CHILD_CONFLICT","The child source changed after review; inspect its current proposal before merging");const Rows old=rows(baseline),now=rows(current),parentRows=rows(parent);const auto editable=keySet(field(manifest,"editable"));
    // Retained history order is authoritative. Adding features appends to the
    // owning history; changing existing order is not silently approximated.
    for(const auto* collection:{"features","assembly","mates"}){std::vector<std::string> wanted,actual;for(const auto& row:field(baseline,collection).arr){auto key=rowKey(collection,row);if(now.count(key))wanted.push_back(key);}for(const auto& row:field(current,collection).arr){auto key=rowKey(collection,row);if(old.count(key))actual.push_back(key);}if(wanted!=actual)reject("CONTAINER_HISTORY_ORDER","Retained object order changed; this merge contract does not reorder parent source");}
    for(const auto& kv:now)if(!old.count(kv.first)&&parentRows.count(kv.first))reject("CONTAINER_NAMESPACE_CONFLICT","New source collides with parent-owned "+kv.first);
    for(const auto* collection:{"features","assembly"}){std::set<std::string> outsideNames;for(const auto& row:field(parent,collection).arr)if(!old.count(rowKey(collection,row)))outsideNames.insert(field(row,"name").asStr());for(const auto& row:field(current,collection).arr)if(outsideNames.count(field(row,"name").asStr()))reject("CONTAINER_NAMESPACE_CONFLICT","A child name collides with an outside source; rename explicitly");}
    IdMap featureIds,nodeIds,mateIds;V maps=V::makeObj();
    for(const auto* collection:{"features","assembly","mates"}){u64 next=std::string(collection)=="features"?doc.nextFeatureId_:std::string(collection)=="assembly"?doc.nextAssemblyId_:doc.nextMateId_;auto& mapping=std::string(collection)=="features"?featureIds:std::string(collection)=="assembly"?nodeIds:mateIds;V mapRows=V::makeArr();for(const auto& row:field(current,collection).arr){const auto childId=id(field(row,"id"));const bool existing=old.count(rowKey(collection,row))!=0;mapping[childId]=existing?childId:next++;V entry=V::makeObj();entry.set("child",number(childId));entry.set("source",number(mapping[childId]));mapRows.arr.push_back(entry);}maps.set(collection,mapRows);}
    V commands=V::makeArr();std::vector<std::string> commandKeys;
    // Remove references before their definitions. No outside objects participate.
    for(const auto* collection:{"mates","assembly","features","sketches","planes","materials","params"})for(auto it=field(baseline,collection).arr.rbegin();it!=field(baseline,collection).arr.rend();++it){const auto key=rowKey(collection,*it);if(editable.count(key)&&!now.count(key)){if(std::string(collection)=="params")reject("CONTAINER_UNSUPPORTED_PARAMETER_REMOVAL","The current command contract has no parameter remove operation");const std::string op=std::string(collection)=="features"?"feature.remove":std::string(collection)=="assembly"?"assembly.remove":std::string(collection)=="mates"?"mate.remove":std::string(collection)=="sketches"?"sketch.remove":std::string(collection)=="planes"?"plane.remove":"material.remove";V c=command(op.c_str());c.set(numericCollection(key)?"id":"name",field(*it,numericCollection(key)?"id":"name"));append(commands,c);commandKeys.push_back(key);}}
    for(const auto* collection:{"params","materials","planes","sketches","features","assembly","mates"})for(const auto& original:field(current,collection).arr){const auto key=rowKey(collection,original);const auto* before=findRow(old,key);if(before&&equal(*before,original))continue;if(before&&!editable.count(key))reject("CONTAINER_READONLY_DEPENDENCY",key);V row=original;remap(row,"",featureIds,nodeIds);V c;
        if(std::string(collection)=="features"){
            if(before){if(!equal(field(*before,"type"),field(original,"type"))||!equal(field(*before,"inputs"),field(original,"inputs"))||!equal(field(*before,"op"),field(original,"op")))reject("CONTAINER_UNSUPPORTED_FEATURE_REWRITE","Retained feature type, inputs and Boolean mode are immutable in this merge contract");if(!equal(field(*before,"source"),field(original,"source")))reject("CONTAINER_READONLY_SOURCE","Imported source payload cannot be replaced by a container edit");}c=featureCommand(row,before);
        }else if(std::string(collection)=="assembly"){
            if(before)noRemovedParams(field(*before,"p"),field(original,"p"));c=command(before?"assembly.edit":"assembly.add");for(const auto& kv:row.obj)if(kv.first!="id"||before)c.set(kv.first,kv.second);if(before&&!row.find("pattern"))c.set("pattern",V{});
        }else if(std::string(collection)=="mates"){
            if(before)for(const auto* optional:{"motion","limits"})if(before->find(optional)&&!row.find(optional))reject("CONTAINER_UNSUPPORTED_MATE_REWRITE","Removing retained mate motion/limits is outside this scoped merge contract; edit that constraint in the owning document explicitly");
            row.set("a",number(mapped(id(field(original,"a")),nodeIds)));row.set("b",number(mapped(id(field(original,"b")),nodeIds)));c=command(before?"mate.edit":"mate.add");for(const auto& kv:row.obj)if(kv.first!="id"||before)c.set(kv.first,kv.second);
        }else if(std::string(collection)=="sketches"){
            c=command("sketch.set");c.set("name",field(row,"name"));erase(row,"name");c.set("sketch",row);
        }else{const char* op=std::string(collection)=="params"?"param.set":std::string(collection)=="planes"?"plane.set":"material.set";c=command(op);for(const auto& kv:row.obj)c.set(kv.first,kv.second);if(std::string(collection)=="materials"&&before&&!row.find("physical"))c.set("physical",V{});if(std::string(collection)=="planes"&&before){noRemovedParams(field(*before,"p"),field(original,"p"));if(before->find("support")&&!row.find("support"))c.set("support",V{});}}
        append(commands,c);commandKeys.push_back(key);
    }
    // Source arrays retain history order, whereas a creation or rebinding can
    // require a producer whose array row comes later. Replay actual producer
    // commands first; retire deleted source after its consumers are changed.
    // Creation sequence edges preserve the explicit ID map and feature order.
    const Graph previousGraph(baseline),currentGraph(current);std::map<std::string,size_t> pending;std::vector<Keys> prerequisites(commandKeys.size());for(size_t i=0;i<commandKeys.size();++i)pending.emplace(commandKeys[i],i);
    for(size_t i=0;i<commandKeys.size();++i){const auto& key=commandKeys[i];if(now.count(key)){const auto dependencies=currentGraph.edges.find(key);if(dependencies!=currentGraph.edges.end())for(const auto& producer:dependencies->second)if(pending.count(producer))prerequisites[i].insert(producer);}else for(const auto& entry:previousGraph.edges)if(entry.second.count(key)&&pending.count(entry.first))prerequisites[i].insert(entry.first);}
    for(const auto* collection:{"features","assembly","mates"}){std::string preceding;for(const auto& row:field(current,collection).arr){const auto key=rowKey(collection,row);if(!old.count(key)){if(!preceding.empty())prerequisites[pending.at(key)].insert(preceding);preceding=key;}}}
    V ordered=V::makeArr();while(!pending.empty()){bool progressed=false;for(size_t i=0;i<commandKeys.size();++i){const auto& key=commandKeys[i];if(!pending.count(key))continue;bool ready=true;for(const auto& prerequisite:prerequisites[i])if(pending.count(prerequisite)){ready=false;break;}if(ready){ordered.arr.push_back(commands.arr[i]);pending.erase(key);progressed=true;}}if(!progressed)reject("CONTAINER_RECIPE_DEPENDENCY","The scoped recipe has a source/creation-order cycle; preserve explicit producer history and review that dependency");}commands=std::move(ordered);
    V out=V::makeObj();out.set("expected_revision",number(revision));out.set("container",number(group));out.set("source_sha256",field(manifest,"source_sha256"));out.set("child_sha256",V::makeStr(childHash));out.set("commands",commands);out.set("id_map",maps);out.set("changed",V::makeBool(!commands.arr.empty()));out.set("publication",V::makeStr("owning_session_doc_apply"));out.set("outside_source_preserved",V::makeBool(true));return out;
}
}
