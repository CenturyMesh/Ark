#include "ark/assembly_authoring.hpp"
#include "ark/iteration_history.hpp"
#include "ark/skeleton.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
std::atomic<u64> sourceOpenAttempts{0},sourceFilesRead{0},sourceBytesRead{0};
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void keys(const V& v,std::initializer_list<const char*> allowed){require(v.t==V::Obj,"Assembly authoring requires an object");for(const auto& p:v.obj){bool found=false;for(auto key:allowed)found|=p.first==key;require(found,"Unknown assembly authoring field: "+p.first);}}
std::string text(const V& v,const char* key){auto p=v.find(key);require(p&&p->t==V::Str&&!p->str.empty()&&p->str.size()<=4096,std::string("Missing assembly authoring text: ")+key);return p->str;}
u64 id(const V& v){require(v.t==V::Num&&std::isfinite(v.num)&&v.num>=1&&v.num<=9007199254740990.&&v.num==std::floor(v.num),"Expected positive assembly authoring ID");return (u64)v.num;}
bool node(const Document& d,u64 n){return std::any_of(d.assembly.begin(),d.assembly.end(),[&](const auto& item){return item.id==n;});}
std::string expression(const V& v){require((v.t==V::Num&&std::isfinite(v.num))||(v.t==V::Str&&!v.str.empty()&&v.str.size()<=2048),"Configuration parameter must be a finite number or expression");return v.t==V::Str?v.str:v.dump();}
void configuration(const Document& d,const V& v){
    keys(v,{"name","parameters","visibility"});require(text(v,"name").size()<=128,"Configuration name exceeds 128 characters");
    if(auto p=v.find("parameters")){require(p->t==V::Obj&&p->obj.size()<=1024,"Configuration parameters require a bounded object");for(const auto& item:p->obj){require(std::any_of(d.params.begin(),d.params.end(),[&](const auto& p){return p.name==item.first;}),"Configuration names a missing parameter: "+item.first);expression(item.second);}}
    if(auto p=v.find("visibility")){require(p->t==V::Obj&&p->obj.size()<=4096,"Configuration visibility requires a bounded object");for(const auto& item:p->obj){require(!item.first.empty()&&item.first.find_first_not_of("0123456789")==std::string::npos&&item.first.size()<=15,"Configuration visibility keys must be node IDs");const auto n=std::stoull(item.first);require(node(d,n)&&std::to_string(n)==item.first&&item.second.t==V::Bool,"Configuration visibility names a missing node or invalid boolean");}}
}
void selection(const Document& d,const V& v){if(v.t==V::Null)return;require(v.t==V::Arr&&v.arr.size()<=4096,"Selective loading roots require null or an array");std::set<u64> seen;for(const auto& item:v.arr){auto n=id(item);require(node(d,n)&&seen.insert(n).second,"Selective loading root is missing or duplicated");}}
void external(const Document& d,const V& v){
    keys(v,{"feature","path","source_sha256","source_revision"});auto key=v.find("feature");require(key,"External reference needs feature");auto feature=d.find(id(*key));require(feature&&feature->type==FeatureType::NativeSolid&&(featureOperation(*feature)=="imported_brep"||featureOperation(*feature)=="imported_cad"),"External reference requires a source-owned imported exact definition");
    auto hash=text(v,"source_sha256");require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos,"External reference needs a lowercase SHA-256");auto source=feature->nativeSpec.find("package");auto original=source?source->find("source_sha256"):nullptr;require(original&&original->t==V::Str&&original->str==hash,"External pin must match the imported definition source hash");
    auto path=std::filesystem::u8path(text(v,"path"));require(path.is_absolute(),"External reference path must be absolute");require(text(v,"source_revision").size()<=256,"External source revision exceeds 256 characters");
}
std::string fileDigest(const std::string& path){++sourceOpenAttempts;std::ifstream stream(std::filesystem::u8path(path),std::ios::binary);require((bool)stream,"EXTERNAL_REFERENCE_MISSING: "+path);stream.seekg(0,std::ios::end);auto length=stream.tellg();require(length>=0&&length<=256*1024*1024,"EXTERNAL_REFERENCE_SIZE: source exceeds 256 MiB");stream.seekg(0);std::string bytes((size_t)length,'\0');stream.read(bytes.data(),length);require((bool)stream,"EXTERNAL_REFERENCE_READ_FAILED: "+path);++sourceFilesRead;sourceBytesRead+=(u64)length;return iterationSha256(bytes);}
V list(const V& value){return value.t==V::Null?V::makeArr():value;}
}
AssemblyExternalIo assemblyExternalIo(){return {sourceOpenAttempts.load(),sourceFilesRead.load(),sourceBytesRead.load()};}
void validateAssemblyAuthoring(const Document& d){
    require(d.assemblyConfigurations.t==V::Null||(d.assemblyConfigurations.t==V::Arr&&d.assemblyConfigurations.arr.size()<=128),"Configurations must be an array of at most 128 recipes");std::set<std::string> names;for(const auto& v:d.assemblyConfigurations.arr){configuration(d,v);require(names.insert(text(v,"name")).second,"Duplicate configuration name");}
    selection(d,d.assemblyLoadSelection);require(d.assemblyExternalReferences.t==V::Null||(d.assemblyExternalReferences.t==V::Arr&&d.assemblyExternalReferences.arr.size()<=4096),"External references require a bounded array");std::set<u64> seen;for(const auto& v:d.assemblyExternalReferences.arr){external(d,v);require(seen.insert(id(*v.find("feature"))).second,"Duplicate external feature pin");}
}
void verifyAssemblyExternalReferences(const Document& d){const auto loaded=assemblyLoadedFeatures(d);for(const auto& v:d.assemblyExternalReferences.arr){external(d,v);if(loaded.count(id(*v.find("feature"))))require(fileDigest(text(v,"path"))==text(v,"source_sha256"),"EXTERNAL_REFERENCE_DRIFT: source bytes changed; explicitly import and repin a new revision");}}
bool applyAssemblyAuthoringCommand(Document& d,const V& command,std::string& error){
    const auto* op=command.find("op");if(!op||op->t!=V::Str)return false;const auto& name=op->str;
    if(name!="configuration.set"&&name!="configuration.apply"&&name!="configuration.remove"&&name!="assembly.load"&&name!="assembly.external.set"&&name!="assembly.external.remove")return false;
    try{
        if(name=="configuration.set"){
            keys(command,{"op","name","parameters","visibility"});V recipe=V::makeObj();for(const auto& p:command.obj)if(p.first!="op")recipe.set(p.first,p.second);configuration(d,recipe);auto rows=list(d.assemblyConfigurations);auto key=text(recipe,"name");bool replaced=false;for(auto& row:rows.arr)if(text(row,"name")==key){row=recipe;replaced=true;break;}if(!replaced)rows.arr.push_back(recipe);require(rows.arr.size()<=128,"Configuration capacity exceeded");d.assemblyConfigurations=std::move(rows);
        }else if(name=="configuration.apply"||name=="configuration.remove"){
            keys(command,{"op","name"});auto key=text(command,"name");auto rows=list(d.assemblyConfigurations);auto at=std::find_if(rows.arr.begin(),rows.arr.end(),[&](const auto& row){return text(row,"name")==key;});require(at!=rows.arr.end(),"Configuration not found: "+key);
            if(name=="configuration.remove"){rows.arr.erase(at);d.assemblyConfigurations=std::move(rows);}else{
                configuration(d,*at);Document candidate=d.authoringCopy();if(auto p=at->find("parameters"))for(const auto& item:p->obj){auto value=expression(item.second);setSkeletonInput(candidate,item.first,value);candidate.setParam(item.first,value);}require(candidate.parameterError.empty(),candidate.parameterError);
                d.params=std::move(candidate.params);d.skeleton=std::move(candidate.skeleton);d.parameterError.clear();if(auto p=at->find("visibility"))for(const auto& item:p->obj)for(auto& n:d.assembly)if(n.id==std::stoull(item.first))n.visible=item.second.b;
            }
        }else if(name=="assembly.load"){
            keys(command,{"op","roots"});auto p=command.find("roots");require(p,"Selective loading needs roots (null resets to all)");selection(d,*p);d.assemblyLoadSelection=*p;
        }else if(name=="assembly.external.set"){
            keys(command,{"op","feature","path","source_sha256","source_revision"});V recipe=V::makeObj();for(const auto& p:command.obj)if(p.first!="op")recipe.set(p.first,p.second);external(d,recipe);require(fileDigest(text(recipe,"path"))==text(recipe,"source_sha256"),"EXTERNAL_REFERENCE_DRIFT: source bytes differ from pin");auto rows=list(d.assemblyExternalReferences);const auto feature=id(*recipe.find("feature"));bool replaced=false;for(auto& row:rows.arr)if(id(*row.find("feature"))==feature){row=recipe;replaced=true;break;}if(!replaced)rows.arr.push_back(recipe);d.assemblyExternalReferences=std::move(rows);
        }else{keys(command,{"op","feature"});auto p=command.find("feature");require(p,"External remove needs feature");auto feature=id(*p);auto rows=list(d.assemblyExternalReferences);auto at=std::find_if(rows.arr.begin(),rows.arr.end(),[&](const auto& row){return id(*row.find("feature"))==feature;});require(at!=rows.arr.end(),"External feature pin not found");rows.arr.erase(at);d.assemblyExternalReferences=std::move(rows);}
        validateAssemblyAuthoring(d);error.clear();
    }catch(const std::exception& e){error=e.what();}return true;
}
std::unordered_set<u64> assemblyOccurrenceDependencies(const Document& d,u64 nodeId){
    // A semantic occurrence reference consumes every stable pattern slot and
    // its exact ancestor frame. Cyclic node/sketch/feature associations reject.
    static thread_local std::set<std::pair<const Document*,u64>> active;
    const auto identity=std::make_pair(&d,nodeId);require(active.insert(identity).second,"SELECTIVE_DEPENDENCY_CYCLE: occurrence "+std::to_string(nodeId));
    struct Scope{decltype(active)& nodes;std::pair<const Document*,u64> key;~Scope(){nodes.erase(key);}} scope{active,identity};
    const AssemblyNode* source=nullptr;for(const auto& n:d.assembly)if(n.id==nodeId)source=&n;require(source,"Selective loading occurrence dependency missing");
    std::unordered_set<u64> result;if(source->feature)result.insert(source->feature);
    if(source->pattern.active){Feature semantic;semantic.nativeSpec=source->pattern.source;auto dependencies=assemblyFeatureDependencies(d,semantic);result.insert(dependencies.begin(),dependencies.end());}
    if(source->parent){auto dependencies=assemblyOccurrenceDependencies(d,source->parent);result.insert(dependencies.begin(),dependencies.end());}
    return result;
}
std::unordered_set<u64> assemblyLoadedFeatures(const Document& d){
    std::unordered_set<u64> loaded;if(d.assemblyLoadSelection.t==V::Null){for(const auto& f:d.features)loaded.insert(f.id);return loaded;}selection(d,d.assemblyLoadSelection);
    std::unordered_map<u64,std::vector<u64>> children;std::unordered_map<u64,const AssemblyNode*> nodes;for(const auto& n:d.assembly){nodes[n.id]=&n;if(n.parent)children[n.parent].push_back(n.id);}
    std::unordered_set<u64> selected;std::function<void(u64)> visit=[&](u64 n){if(!selected.insert(n).second)return;const auto found=nodes.find(n);require(found!=nodes.end(),"Selective loading node missing");if(found->second->feature)loaded.insert(found->second->feature);for(auto child:children[n])visit(child);};for(const auto& root:d.assemblyLoadSelection.arr)visit(id(root));
    // Mates retain their full source dependency closure; unloading never drops
    // a constraint or changes its reference authority.
    for(const auto& mate:d.mates)for(auto n:{mate.a,mate.b}){auto found=nodes.find(n);require(found!=nodes.end(),"Mate endpoint missing during selective loading");if(found->second->feature)loaded.insert(found->second->feature);}
    // Every authored pattern still expands to its complete occurrence inventory,
    // even outside the display selection. Retain its guide/point/alternate and
    // linked-pattern sources, without loading unrelated seed definitions.
    for(const auto& n:d.assembly)if(n.pattern.active){Feature semantic;semantic.nativeSpec=n.pattern.source;for(auto feature:assemblyFeatureDependencies(d,semantic))loaded.insert(feature);}
    std::vector<u64> pending(loaded.begin(),loaded.end());for(size_t at=0;at<pending.size();++at){const auto* f=d.find(pending[at]);require(f,"Selective loading feature dependency missing");for(auto input:assemblyFeatureDependencies(d,*f))if(loaded.insert(input).second)pending.push_back(input);}
    return loaded;
}
arkjson::Value inspectAssemblyAuthoring(const Document& d,u64 revision){
    validateAssemblyAuthoring(d);verifyAssemblyExternalReferences(d);auto loaded=assemblyLoadedFeatures(d);V out=V::makeObj(),definitions=V::makeArr(),pins=list(d.assemblyExternalReferences);size_t verified=0;for(auto& pin:pins.arr){const bool active=loaded.count(id(*pin.find("feature")))!=0;pin.set("verification",V::makeStr(active?"verified":"deferred_until_loaded"));verified+=active;}for(const auto& f:d.features){V row=V::makeObj();row.set("feature",V::makeNum((double)f.id));row.set("loaded",V::makeBool(loaded.count(f.id)!=0));definitions.arr.push_back(row);}out.set("revision",V::makeNum((double)revision));out.set("configurations",list(d.assemblyConfigurations));out.set("load_roots",d.assemblyLoadSelection);out.set("definitions",definitions);out.set("loaded_definitions",V::makeNum((double)loaded.size()));out.set("total_definitions",V::makeNum((double)d.features.size()));out.set("external_references",pins);out.set("external_sources_verified",V::makeBool(verified==pins.arr.size()));out.set("verified_external_sources",V::makeNum((double)verified));out.set("deferred_external_sources",V::makeNum((double)(pins.arr.size()-verified)));out.set("selective_loading_scope",V::makeStr("selected node subtrees and transitive semantic feature/sketch/datum/placed-instance sources; all mate endpoints and pattern inventory sources retained; unselected evaluated geometry/cache released; unloaded external file verification deferred to reload"));return out;
}
bool isAssemblyNodeLoaded(const Document& d,u64 nodeId){
    if(d.assemblyLoadSelection.t==V::Null)return true;
    for(const auto& mate:d.mates)if(mate.a==nodeId||mate.b==nodeId)return true;
    std::unordered_map<u64,u64> parents;for(const auto& n:d.assembly)parents[n.id]=n.parent;
    std::unordered_set<u64> visited;for(u64 at=nodeId;at;){if(!visited.insert(at).second)return false;for(const auto& root:d.assemblyLoadSelection.arr)if(root.t==V::Num&&(u64)root.num==at)return true;const auto found=parents.find(at);if(found==parents.end())return false;at=found->second;}return false;
}
}
