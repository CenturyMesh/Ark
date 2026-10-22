#include "ark/parametric_components.hpp"
#include "ark/serialize.hpp"
#include "ark/document_container.hpp"
#include "ark/iteration_history.hpp"
#include "ark/history_authoring.hpp"
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
const char* collections[]={"params","planes","sketches","materials","features","assembly","mates"};
const V& get(const V& v,const char* key){static V null;const auto* p=v.find(key);return p?*p:null;}
[[noreturn]] void fail(const std::string& code,const std::string& why){throw std::runtime_error("COMPONENT_"+code+": "+why);}
void erase(V& v,const char* key){v.obj.erase(std::remove_if(v.obj.begin(),v.obj.end(),[&](const auto& p){return p.first==key;}),v.obj.end());}
void canonical(V& v){if(v.t==V::Obj){for(auto& p:v.obj)canonical(p.second);std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(v.t==V::Arr)for(auto& p:v.arr)canonical(p);}
std::string hash(V v){canonical(v);return iterationSha256(v.dump());}
bool digest(const V& v){return v.t==V::Str&&v.str.size()==64&&v.str.find_first_not_of("0123456789abcdef")==std::string::npos;}
u64 id(const V& v){if(v.t!=V::Num||!std::isfinite(v.num)||v.num<1||v.num>9007199254740991.||v.num!=std::floor(v.num))fail("ID","Expected a positive exact integer");return (u64)v.num;}
std::string text(const V& v,const char* key){const auto& p=get(v,key);const size_t limit=std::string(key)=="path"?32768:128;if(p.t!=V::Str||p.str.empty()||p.str.size()>limit)fail("TEXT",std::string(key)+" needs nonempty bounded text");return p.str;}
bool numeric(const std::string& c){return c=="features"||c=="assembly"||c=="mates";}
std::string rowKey(const std::string& c,const V& row){return numeric(c)?std::to_string(id(get(row,"id"))):text(row,"name");}
V authored(const Document& d){V out;if(!arkjson::parse(documentToJson(d,false,false),out))fail("SOURCE","Cannot serialize authored source");return out;}
Document parse(const V& root){Document d;std::string error;if(!documentFromJson(root.dump(),d,&error))fail("SOURCE",error);return d;}
void nameSpace(const std::string& s){if(s.empty()||s.size()>32||!(std::isalpha((unsigned char)s[0])||s[0]=='_')||s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)fail("NAMESPACE","Use an identifier of 1..32 ASCII letters, digits or underscores");}
void ports(const V& p,const char* key,const char* collection){const auto& a=get(p,key);if(a.t!=V::Arr||a.arr.size()>128)fail("PORTS",std::string(key)+" must be an array of at most 128 names");std::set<std::string> seen;for(const auto& n:a.arr){if(n.t!=V::Str||!seen.insert(n.str).second)fail("PORTS","Port names must be distinct strings");bool found=false;for(const auto& row:get(get(p,"document"),collection).arr)found|=get(row,"name").asStr()==n.str;if(!found)fail("PORTS","Missing source port "+n.str);}}
void validatePackage(const V& p){
    if(p.t!=V::Obj||get(p,"format").asStr()!="ark.parametric-component.v1")fail("FORMAT","Expected an ARK parametric component v1");
    const std::set<std::string> allowed{"format","kind","component_id","revision","name","document","parameters","interfaces","parent_sha256"};for(const auto& kv:p.obj)if(!allowed.count(kv.first))fail("FORMAT","Unknown package field "+kv.first);
    const auto kind=text(p,"kind");if(kind!="part"&&kind!="assembly")fail("KIND","Choose part or assembly");text(p,"component_id");text(p,"name");id(get(p,"revision"));
    if(p.find("parent_sha256")&&!digest(get(p,"parent_sha256")))fail("HASH","Invalid previous revision digest");
    const auto& root=get(p,"document");if(root.t!=V::Obj||get(root,"features").arr.empty())fail("SOURCE","A component needs authored geometry");
    const std::set<std::string> rootFields{"format","version","units","params","planes","sketches","materials","features","assembly","mates","next_feature_id","next_assembly_id","next_mate_id"};
    for(const auto& kv:root.obj)if(!rootFields.count(kv.first))fail("UNSUPPORTED_SOURCE","Source field has no portable component contract: "+kv.first);
    // Source programs with encoded/global ownership need their own remapper.
    for(const auto& f:get(root,"features").arr){const auto type=get(f,"type").asStr();if(type=="skeleton"||type=="electronics"||type=="harness"||type=="source_tube"||f.find("source")||f.find("part")||get(f,"native").find("package"))fail("UNSUPPORTED_SOURCE","Imported packages and domain/BOM ownership must be exported through their owning contract");}
    ports(p,"parameters","params");ports(p,"interfaces","planes");
    auto d=parse(root);size_t roots=0;for(const auto& n:d.assembly)if(!n.parent){if(n.feature)fail("ROOT","The component root must be an assembly group");++roots;}
    if(roots!=1)fail("ROOT","A component must have one local root group");
    if(kind=="part"&&d.assembly.size()!=2)fail("KIND","A part has exactly one placed result and its local root");
}
std::string freshId(){static std::atomic<u64> serial{0};return "ark-"+iterationSha256(std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+":"+std::to_string(++serial)).substr(0,32);}
std::string remapTokens(const std::string& s,const V& mapping){std::string out;for(size_t i=0;i<s.size();){if(std::isalpha((unsigned char)s[i])||s[i]=='_'){const auto start=i++;while(i<s.size()&&(std::isalnum((unsigned char)s[i])||s[i]=='_'))++i;const auto token=s.substr(start,i-start);const auto* n=mapping.find(token.c_str());out+=n?n->str:token;}else out+=s[i++];}return out;}
V mapped(const V& maps,const char* c,const V& v){if(v.t==V::Num&&v.num==0)return v;const auto key=v.t==V::Num?std::to_string(id(v)):v.asStr();const auto* p=get(maps,c).find(key.c_str());if(!p)fail("REFERENCE","Unmapped "+std::string(c)+":"+key);return *p;}
void remapValue(V& v,const std::string& key,const V& maps,bool expression=false){
    static const std::set<std::string> opaque{"name","local_name","type","op","kind","reference","reference_a","reference_b","source_entity","entity","face","material_side","format","units","appearance","face_appearances","mesh","topology","part","manufacturing"};
    if(opaque.count(key))return;
    if(v.t==V::Num){
        if(key=="owner_feature"||key=="feature"||key=="second_feature"||key=="source_feature"||key=="alternate_feature"||key=="inputs"||key=="curve")v=mapped(maps,"features",v);
        else if(key=="node"||key=="source_instance"||key=="alternate_node"||key=="instances"||key=="parent")v=mapped(maps,"assembly",v);
    }else if(v.t==V::Str){
        const char* collection=nullptr;
        if(key=="sketch"||key=="source_sketch"||key=="sections"||key=="guides"||key=="profile"||key=="path"||key=="section")collection="sketches";
        if(key=="plane"||key=="datum"||key=="parent"||key=="first"||key=="second")collection="planes";
        if(key=="material")collection="materials";
        if(collection){if(const auto* n=get(maps,collection).find(v.str.c_str())){v=*n;return;}}
        // Expression source is tokenized, never replaced by substring. Enumerated
        // words and geometric selectors remain untouched even for a parameter x.
        static const std::set<std::string> exprKeys{"expr","p","dof","val","val2","value","pitch","ratio","phase","min","max","lower","upper","fraction","u","v","count","dx","dy","dz","step","feed","radius","angle","roll","station","offset","distance","size","height","width","depth","start","end","through","tangent"};
        const auto rewritten=remapTokens(v.str,get(maps,"params"));
        if(expression||exprKeys.count(key))v.str=rewritten;
        else if(rewritten!=v.str)fail("EXPRESSION_FIELD","Parameter identifier occurs in an unsupported source field: "+key);
    }else if(v.t==V::Obj){for(auto& item:v.obj)remapValue(item.second,item.first,maps,expression||key=="p"||key=="dof");}
    else if(v.t==V::Arr)for(auto& item:v.arr)remapValue(item,key,maps,expression);
}
V mappingFor(const V& source,const Document& target,const std::string& ns){
    V maps=V::makeObj();u64 feature=target.nextFeatureId_,node=target.nextAssemblyId_,mate=target.nextMateId_;
    for(const char* c:{"features","assembly","mates","params","planes","materials","sketches"}){V map=V::makeObj();for(const auto& row:get(source,c).arr){const auto key=rowKey(c,row);V value;
        if(numeric(c)){auto& next=std::string(c)=="features"?feature:std::string(c)=="assembly"?node:mate;value=V::makeNum((double)next++);}
        else if(std::string(c)=="sketches"&&row.find("owner_feature"))value=V::makeStr(sketchReference((u64)mapped(maps,"features",get(row,"owner_feature")).num,get(row,"local_name").asStr()));
        else value=V::makeStr(ns+"__"+key);
        if(value.t==V::Str&&value.str.size()>128)fail("NAME_LENGTH","Prefixed source name exceeds 128 bytes");map.set(key,value);
    }maps.set(c,map);}return maps;
}
V installedSource(const V& package,const V& maps,const std::string& ns,const V& overrides){
    if(overrides.t!=V::Obj)fail("OVERRIDES","overrides must map public parameter names to finite numbers");
    for(const auto& kv:overrides.obj){bool exposed=false;for(const auto& n:get(package,"parameters").arr)exposed|=n.asStr()==kv.first;if(!exposed||kv.second.t!=V::Num||!std::isfinite(kv.second.num))fail("OVERRIDES","Only public parameters accept finite numeric overrides: "+kv.first);}
    V source=get(package,"document");
    for(auto& collection:source.obj){if(std::find(std::begin(collections),std::end(collections),collection.first)==std::end(collections))continue;
        for(auto& row:collection.second.arr){const auto original=rowKey(collection.first,row);remapValue(row,"",maps);
            if(collection.first=="mates"){row.set("a",mapped(maps,"assembly",get(row,"a")));row.set("b",mapped(maps,"assembly",get(row,"b")));}
            if(numeric(collection.first)){row.set("id",*get(maps,collection.first.c_str()).find(original.c_str()));if(row.find("name"))row.set("name",V::makeStr(ns+" / "+get(row,"name").asStr()));}
            else row.set("name",*get(maps,collection.first.c_str()).find(original.c_str()));
            if(collection.first=="params")if(const auto* value=overrides.find(original.c_str()))row.set("expr",V::makeStr(value->dump()));
        }
    }return source;
}
u64 rootNode(const V& package,const V& maps){for(const auto& n:get(get(package,"document"),"assembly").arr)if(get(n,"parent").asNum()==0)return id(mapped(maps,"assembly",get(n,"id")));fail("ROOT","No local root");}
V fingerprints(const V& root,const V& maps,const V& package){V result=V::makeObj();const auto localRoot=rootNode(package,maps);for(const auto* c:collections){V values=V::makeObj();for(const auto& m:get(maps,c).obj){const auto wanted=m.second.t==V::Num?std::to_string(id(m.second)):m.second.str;const V* found=nullptr;for(const auto& row:get(root,c).arr)if(rowKey(c,row)==wanted){found=&row;break;}if(!found)fail("LOCAL_EDITS","A retained component object is missing: "+std::string(c)+":"+wanted);V source=*found;if(std::string(c)=="assembly"&&id(get(source,"id"))==localRoot)for(const char* key:{"p","parent","visible","fixed","name"})erase(source,key);values.set(wanted,V::makeStr(hash(source)));}result.set(c,values);}return result;}
V brief(const V& p){V out=p;erase(out,"document");out.set("sha256",V::makeStr(hash(p)));V counts=V::makeObj();for(const auto* c:collections)counts.set(c,V::makeNum(get(get(p,"document"),c).arr.size()));out.set("counts",counts);return out;}
void expected(const AgentSession& s,const V& args){const auto& n=get(args,"expected_revision");if(n.t!=V::Num||n.num!=(double)s.revision)fail("REVISION_CONFLICT","Read the current document revision before preparing or exporting");}
}

std::string parametricComponentHash(const V& p){return hash(p);}
V makeParametricComponent(const Document& d,const V& args){
    V p=V::makeObj();p.set("format",V::makeStr("ark.parametric-component.v1"));p.set("kind",V::makeStr(text(args,"kind")));p.set("name",V::makeStr(text(args,"name")));p.set("component_id",V::makeStr(freshId()));p.set("revision",V::makeNum(1));
    if(const auto* previous=args.find("previous_path")){auto old=loadParametricComponent(previous->asStr());if(get(old,"kind").asStr()!=get(p,"kind").asStr())fail("KIND","Revision cannot change component kind");p.set("component_id",get(old,"component_id"));p.set("revision",V::makeNum((double)id(get(old,"revision"))+1));p.set("parent_sha256",V::makeStr(hash(old)));}
    p.set("parameters",args.find("parameters")?get(args,"parameters"):V::makeArr());p.set("interfaces",args.find("interfaces")?get(args,"interfaces"):V::makeArr());p.set("document",componentSourceClosure(d,args));validatePackage(p);return p;
}
V loadParametricComponent(const std::string& path){V p;std::string error;if(!loadDocumentContainer(path.c_str(),p,&error))fail("LOAD",error);validatePackage(p);const auto ext=std::filesystem::u8path(path).extension().string();if(ext!=(get(p,"kind").str=="part"?".arkprt":".arktop"))fail("EXTENSION","File extension does not match component kind");return p;}
void saveParametricComponent(const V& p,const std::string& path){
    validatePackage(p);const auto target=std::filesystem::u8path(path);if(target.extension()!=(get(p,"kind").str=="part"?".arkprt":".arktop"))fail("EXTENSION","Use .arkprt for a part or .arktop for an assembly");
    if(std::filesystem::exists(target))fail("PATH_EXISTS","Component revisions are immutable; choose a new file");
    auto staging=target;staging+="."+freshId()+".tmp";const auto bytes=staging.u8string();const std::string staged(bytes.begin(),bytes.end());std::string error;
    if(!saveDocumentContainer(p,staged.c_str(),&error))fail("SAVE",error);
    std::error_code ec;std::filesystem::create_hard_link(staging,target,ec);std::error_code cleanup;std::filesystem::remove(staging,cleanup);if(ec)fail("PUBLISH",ec.message());
}
void validateParametricComponents(const V& entries){
    if(entries.isNull())return;if(entries.t!=V::Arr||entries.arr.size()>128)fail("BINDINGS","Expected up to 128 component bindings");std::set<std::string> namespaces,targets;
    for(const auto& e:entries.arr){const auto ns=text(e,"namespace");nameSpace(ns);if(!namespaces.insert(ns).second)fail("BINDINGS","Duplicate namespace");const auto& p=get(e,"package");validatePackage(p);
        if(get(e,"package_sha256").asStr()!=hash(p))fail("HASH","Embedded component digest does not match");
        const auto& maps=get(e,"mapping");const auto& base=get(e,"baseline");if(maps.t!=V::Obj||base.t!=V::Obj)fail("BINDINGS","Mapping and baseline are required");
        for(const auto* c:collections){const auto& m=get(maps,c),&b=get(base,c);if(m.t!=V::Obj||b.t!=V::Obj||m.obj.size()!=get(get(p,"document"),c).arr.size()||m.obj.size()!=b.obj.size())fail("BINDINGS","Incomplete ownership map");for(const auto& row:get(get(p,"document"),c).arr){const auto key=rowKey(c,row);const auto* v=m.find(key.c_str());if(!v||(numeric(c)?v->t!=V::Num:v->t!=V::Str))fail("BINDINGS","Invalid mapped identity");const auto dest=numeric(c)?std::to_string(id(*v)):v->str;if(!targets.insert(std::string(c)+":"+dest).second||!digest(get(b,dest.c_str())))fail("BINDINGS","Overlapping ownership or invalid baseline");}}
        (void)installedSource(p,maps,ns,get(e,"overrides"));
    }
}
bool applyParametricComponentCommand(Document& d,const V& command){
    const auto op=get(command,"op").asStr();if(op!="component.insert"&&op!="component.update"&&op!="component.configure")return false;
    if(d.historyTip||!d.designContainerCheckout.isNull()||!d.skeleton.isNull())fail("TARGET","Resume full history and use the owning project without global Skeleton ownership");
    const auto ns=text(command,"namespace");nameSpace(ns);V entries=d.parametricComponents.isNull()?V::makeArr():d.parametricComponents;
    size_t index=entries.arr.size();for(size_t i=0;i<entries.arr.size();++i)if(get(entries.arr[i],"namespace").asStr()==ns)index=i;
    const bool insert=op=="component.insert",configure=op=="component.configure";
    if(insert?index!=entries.arr.size():index==entries.arr.size())fail("NAMESPACE",insert?"Namespace is already installed":"No installed component in this namespace");
    Document target=d.authoringCopy();
    if(insert&&target.assembly.empty()&&!target.features.empty()){
        target.recompute();for(const auto& result:target.lastResults)if(!result.ok)fail("TARGET_GEOMETRY",result.error);
        const auto consumed=consumedFeatureInputs(target);std::set<u64> seen;
        for(const auto& body:target.evalBodies){const auto* feature=target.find(body.id);if(!feature||feature->suppressed||consumed.count(body.id)||!seen.insert(body.id).second)continue;AssemblyNode prior;prior.id=target.nextAssemblyId_++;prior.feature=body.id;prior.name="Existing / "+feature->name;target.assembly.push_back(prior);}
    }
    V root=authored(target),entry=insert?V::makeObj():entries.arr[index];
    if(!insert){if(get(command,"expected_package_sha256").asStr()!=get(entry,"package_sha256").asStr())fail("STALE_PACKAGE","Inspect the installed revision before updating");if(hash(fingerprints(root,get(entry,"mapping"),get(entry,"package")))!=hash(get(entry,"baseline")))fail("LOCAL_EDITS","Installed recipe was edited locally; export/review a new definition instead of replacing it");}
    V package=configure?get(entry,"package"):get(command,"package");validatePackage(package);
    if(!configure&&get(command,"package_sha256").asStr()!=hash(package))fail("HASH","Reviewed package digest does not match");
    if(!insert&&!configure){const auto& old=get(entry,"package");if(get(old,"component_id").str!=get(package,"component_id").str||get(old,"kind").str!=get(package,"kind").str||id(get(package,"revision"))!=id(get(old,"revision"))+1||get(package,"parent_sha256").asStr()!=hash(old))fail("REVISION_CHAIN","Update must be the next explicit revision of this exact component");
        for(const auto* c:collections){std::set<std::string> a,b;for(const auto& row:get(get(old,"document"),c).arr)a.insert(rowKey(c,row));for(const auto& row:get(get(package,"document"),c).arr)b.insert(rowKey(c,row));if(a!=b)fail("STRUCTURAL_UPDATE","This revision changes source identities; insert it under a new namespace and reconnect explicitly");}
    }
    const V maps=insert?mappingFor(get(package,"document"),target,ns):get(entry,"mapping");
    V overrides=insert?V::makeObj():get(entry,"overrides");if(const auto* values=command.find("overrides")){if(values->t!=V::Obj)fail("OVERRIDES","Expected an object");for(const auto& kv:values->obj)overrides.set(kv.first,kv.second);}
    const auto source=installedSource(package,maps,ns,overrides);
    for(const auto* c:collections){V rows=get(root,c);if(rows.isNull())rows=V::makeArr();for(auto row:get(source,c).arr){const auto key=rowKey(c,row);auto it=std::find_if(rows.arr.begin(),rows.arr.end(),[&](const V& r){return rowKey(c,r)==key;});if(insert){if(it!=rows.arr.end())fail("COLLISION",std::string(c)+":"+key);rows.arr.push_back(row);}else{if(it==rows.arr.end())fail("LOCAL_EDITS","Retained object was removed");if(std::string(c)=="assembly"&&id(get(row,"id"))==rootNode(package,maps))for(const char* k:{"p","parent","visible","fixed","name"}){erase(row,k);if(it->find(k))row.set(k,get(*it,k));}*it=row;}}root.set(c,rows);}
    for(const auto* c:{"features","assembly","mates"}){u64 next=std::string(c)=="features"?d.nextFeatureId_:std::string(c)=="assembly"?d.nextAssemblyId_:d.nextMateId_;for(const auto& r:get(root,c).arr)next=std::max(next,id(get(r,"id"))+1);root.set(std::string(c)=="features"?"next_feature_id":std::string(c)=="assembly"?"next_assembly_id":"next_mate_id",V::makeNum((double)next));}
    // Reparse into scratch before returning to the host's normal exact evaluator.
    // Preserve the session's journal and audit, never import those from a file.
    erase(root,"parametric_components");root.set("version",V::makeNum(std::max(get(root,"version").asNum(),get(source,"version").asNum())));Document next=parse(root);
    next.usage=d.usage;next.agentActivity=d.agentActivity;next.iterationHistory=d.iterationHistory;next.evaluationCache=d.evaluationCache;next.evaluationGeneration=d.evaluationGeneration;
    entry.set("namespace",V::makeStr(ns));entry.set("package",package);entry.set("package_sha256",V::makeStr(hash(package)));entry.set("mapping",maps);entry.set("overrides",overrides);entry.set("baseline",fingerprints(authored(next),maps,package));
    if(insert)entries.arr.push_back(entry);else entries.arr[index]=entry;
    validateParametricComponents(entries);next.parametricComponents=entries;next.componentBindingsToSeal=d.componentBindingsToSeal;next.componentBindingsToSeal.push_back(ns);d=std::move(next);return true;
}
void sealParametricComponentBindings(Document& d){if(d.componentBindingsToSeal.empty())return;const auto root=authored(d);for(auto& e:d.parametricComponents.arr)if(std::find(d.componentBindingsToSeal.begin(),d.componentBindingsToSeal.end(),get(e,"namespace").asStr())!=d.componentBindingsToSeal.end())e.set("baseline",fingerprints(root,get(e,"mapping"),get(e,"package")));d.componentBindingsToSeal.clear();}
bool callParametricComponentTool(AgentSession& s,const std::string& tool,const V& args,V& out){
    if(tool!="parametric_components")return false;const auto action=text(args,"action");
    if(action=="inspect"){out=V::makeObj();out.set("revision",V::makeNum((double)s.revision));if(args.find("path"))out.set("package",brief(loadParametricComponent(text(args,"path"))));else{V entries=V::makeArr();for(const auto& e:s.document.parametricComponents.arr){V row=brief(get(e,"package"));row.set("namespace",get(e,"namespace"));row.set("overrides",get(e,"overrides"));row.set("mapping",get(e,"mapping"));entries.arr.push_back(row);}out.set("components",entries);}return true;}
    expected(s,args);
    if(action=="export"){const auto p=makeParametricComponent(s.document,args);const auto path=text(args,"path");saveParametricComponent(p,path);out=brief(p);out.set("path",V::makeStr(path));return true;}
    if(action!="prepare")fail("ACTION","Use inspect, export or prepare");
    const auto mode=text(args,"mode");if(mode!="insert"&&mode!="update"&&mode!="configure")fail("ACTION","Prepare insert, update or configure");
    V command=V::makeObj();command.set("op",V::makeStr("component."+mode));command.set("namespace",V::makeStr(text(args,"namespace")));
    V package;if(mode!="configure"){package=loadParametricComponent(text(args,"path"));command.set("package",package);command.set("package_sha256",V::makeStr(hash(package)));}
    if(mode!="insert"){bool found=false;for(const auto& e:s.document.parametricComponents.arr)if(get(e,"namespace").asStr()==get(command,"namespace").str){command.set("expected_package_sha256",get(e,"package_sha256"));found=true;}if(!found)fail("NAMESPACE","Installed component not found");}
    if(args.find("overrides"))command.set("overrides",get(args,"overrides"));
    V commands=V::makeArr();commands.arr.push_back(command);V apply=V::makeObj();apply.set("commands",commands);apply.set("expected_revision",V::makeNum((double)s.revision));
    // doc_preview executes the real atomic command on an isolated candidate,
    // including exact geometry. The returned candidate_id freezes what was reviewed.
    const auto preview=s.call("doc_preview",apply);const auto& evaluated=get(preview,"structuredContent");if(get(preview,"isError").b||!get(evaluated,"valid").b||!evaluated.find("candidate_id"))fail("PREVIEW",evaluated.dump());out=evaluated;erase(out,"apply");if(!package.isNull())out.set("package",brief(package));out.set("namespace",get(command,"namespace"));out.set("mode",V::makeStr(mode));out.set("publication",V::makeStr("doc_apply candidate_id + expected_revision"));return true;
}
void appendParametricComponentTools(V& list){V tool=V::makeObj(),schema,annotations=V::makeObj();tool.set("name",V::makeStr("parametric_components"));tool.set("description",V::makeStr("Portable parametric .arkprt parts / .arktop assemblies embedded in .ark projects. inspect reads installed revisions or a file; export writes an immutable new file (previous_path creates the next revision); prepare insert/update/configure evaluates an isolated candidate for explicit doc_apply. Numeric overrides only target public parameters. Updates reject local edits and structural identity changes. No network or automatic source updates."));arkjson::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["inspect","export","prepare"]},"mode":{"type":"string","enum":["insert","update","configure"]},"kind":{"type":"string","enum":["part","assembly"]},"feature":{"type":"integer","minimum":1},"node":{"type":["integer","string"]},"name":{"type":"string"},"namespace":{"type":"string"},"path":{"type":"string"},"previous_path":{"type":"string"},"parameters":{"type":"array","items":{"type":"string"}},"interfaces":{"type":"array","items":{"type":"string"}},"overrides":{"type":"object","additionalProperties":{"type":"number"}},"expected_revision":{"type":"integer","minimum":0}},"required":["action"],"additionalProperties":false})",schema);tool.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(false));tool.set("annotations",annotations);list.arr.push_back(tool);}
}
