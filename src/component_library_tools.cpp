#include "ark/component_library_tools.hpp"
#include "ark/component_library.hpp"
#include "ark/component_sources.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
const V& need(const V& v,const char* key){const auto* item=v.find(key);if(!item)throw std::runtime_error(std::string("Missing component library field: ")+key);return *item;}
std::string text(const V& v,const char* key){const auto& item=need(v,key);if(item.t!=V::Str||item.str.empty()||item.str.size()>4096)throw std::runtime_error(std::string("Invalid component library text: ")+key);return item.str;}
std::string utf8(const fs::path& path){const auto bytes=path.u8string();return std::string(bytes.begin(),bytes.end());}
V payload(const V& response){if(need(response,"isError").b)throw std::runtime_error(need(need(response,"structuredContent"),"error").str);return need(response,"structuredContent");}
void keys(const V& args,std::initializer_list<const char*> allowed){if(args.t!=V::Obj)throw std::runtime_error("Component library arguments must be an object");for(const auto& item:args.obj){bool found=false;for(const char* key:allowed)found|=item.first==key;if(!found)throw std::runtime_error("Unknown component library argument: "+item.first);}}
std::string root(const V& args){
    if(args.find("root"))return utf8(fs::absolute(fs::u8path(text(args,"root"))).lexically_normal());
    if(const char* value=std::getenv("ARK_COMPONENT_LIBRARY");value&&*value)return utf8(fs::absolute(fs::u8path(value)).lexically_normal());
    if(const char* value=std::getenv("LOCALAPPDATA");value&&*value)return utf8(fs::u8path(value)/"ARK"/"components");
    if(const char* value=std::getenv("HOME");value&&*value)return utf8(fs::u8path(value)/".local"/"share"/"ark"/"components");
    throw std::runtime_error("Configure root or ARK_COMPONENT_LIBRARY for the reusable component database");
}
std::shared_ptr<ComponentLibrary> library(const V& args){
    const auto directory=root(args);static std::mutex mutex;static std::map<std::string,std::shared_ptr<ComponentLibrary>> cache;
    std::lock_guard<std::mutex> lock(mutex);auto found=cache.find(directory);if(found!=cache.end())return found->second;
    auto created=std::make_shared<ComponentLibrary>(ComponentLibraryConfig{directory});if(cache.size()>=4)cache.erase(cache.begin());cache.emplace(directory,created);return created;
}
int bodySelection(const V& args){
    bool all=false;if(auto selected=args.find("all_bodies")){if(selected->t!=V::Bool)throw std::runtime_error("all_bodies must be boolean");all=selected->b;}
    if(all&&args.find("body_index"))throw std::runtime_error("Choose body_index or all_bodies, never both");if(all)return -2;
    if(auto selected=args.find("body_index")){if(selected->t!=V::Num||!std::isfinite(selected->num)||selected->num<0||selected->num>10000||selected->num!=std::floor(selected->num))throw std::runtime_error("body_index must be an explicit zero-based integer");return (int)selected->num;}return -1;
}
ComponentValidator validator(){return [](const std::string& path,const std::string& hash,int body){V request=V::makeObj();request.set("operation",V::makeStr("import_component"));request.set("path",V::makeStr(path));request.set("body_index",V::makeNum(body));auto native=nativeWorkerRequest(request);if(need(native,"source_sha256").str!=hash)throw std::runtime_error("Immutable STEP snapshot changed during native validation");
    // Tessellation is disposable display data and is not a library authority.
    V data=V::makeObj();for(const char* key:{"brep_base64","report","topology","package"})data.set(key,need(native,key));V receipt=V::makeObj();receipt.set("source_sha256",need(native,"source_sha256"));receipt.set("body_index",need(native,"body_index"));receipt.set("native_data",data);return receipt;};}
std::string uniqueFeatureName(const Document& document,const std::string& requested){std::string name=requested;for(unsigned suffix=2;;++suffix){bool used=false;for(const auto& f:document.features)used|=f.name==name;if(!used)return name;name=requested+" ("+std::to_string(suffix)+")";}}
std::string uniqueInstanceName(const Document& document,const std::string& requested){std::string name=requested;for(unsigned suffix=2;;++suffix){bool used=false;for(const auto& item:document.assembly)used|=item.name==name;if(!used)return name;name=requested+" ("+std::to_string(suffix)+")";}}
V insert(AgentSession& session,ComponentLibrary& database,const V& args){
    const auto& expected=need(args,"expected_revision");if(expected.t!=V::Num||expected.num!=(double)session.revision)throw std::runtime_error("Revision conflict; inspect the CAD document before library insertion");
    const auto componentId=text(args,"id");const auto record=database.get(componentId,true);const auto ownedSource=database.resolveStep(componentId,true);
    const auto& native=need(record,"native_data");V package=need(native,"package");package.set("source_path",V::makeStr(ownedSource));
    V custody=V::makeObj();custody.set("operation",V::makeStr("component_custody"));custody.set("package",package);nativeWorkerRequest(custody);
    const auto& metadata=need(record,"metadata");const std::string requestedName=args.find("name")?text(args,"name"):text(metadata,"name");if(requestedName.size()>128)throw std::runtime_error("Component feature name exceeds 128 characters");
    u64 featureId=0;for(const auto& f:session.document.features)if(f.type==FeatureType::NativeSolid&&featureOperation(f)=="imported_brep")if(auto id=f.nativeSpec.find("component_id"))if(id->t==V::Str&&id->str==componentId){if(featureId)throw std::runtime_error("Document has ambiguous duplicate library definitions");featureId=f.id;}
    const bool reused=featureId!=0;V commands=V::makeArr();Feature definition;
    if(session.document.bomDocumentId.empty()){V initialize=V::makeObj();initialize.set("op",V::makeStr("bom.initialize"));commands.arr.push_back(initialize);}
    if(!reused){definition.type=FeatureType::NativeSolid;definition.name=uniqueFeatureName(session.document,requestedName);definition.manufacturingRoute="purchased";definition.nativeSpec=V::makeObj();definition.nativeSpec.set("op",V::makeStr("imported_brep"));definition.nativeSpec.set("package",package);definition.nativeSpec.set("component_id",V::makeStr(componentId));definition.nativeSpec.set("component_metadata",metadata);V add=V::makeObj();add.set("op",V::makeStr("feature.add"));add.set("type",V::makeStr("native_solid"));add.set("name",V::makeStr(definition.name));add.set("native",definition.nativeSpec);V manufacturing=V::makeObj();manufacturing.set("route",V::makeStr("purchased"));add.set("manufacturing",manufacturing);commands.arr.push_back(add);featureId=session.document.nextFeatureId_;}
    else{const auto* feature=session.document.find(featureId);if(!feature||feature->suppressed)throw std::runtime_error("Existing component definition is suppressed; restore it explicitly before insertion");const auto& existing=need(feature->nativeSpec,"package");if(need(existing,"source_sha256").str!=need(package,"source_sha256").str||need(existing,"brep_sha256").str!=need(package,"brep_sha256").str||need(existing,"body_index").num!=need(package,"body_index").num)throw std::runtime_error("Existing library definition does not match the immutable component artifact");}
    Document capacity;std::string error;if(!documentFromJson(documentToJson(session.document,true,false),capacity,&error))throw std::runtime_error(error);if(!reused)capacity.addFeature(definition);
    V preserved=V::makeArr();
    if(session.document.assembly.empty()){
        // Introducing assembly display must retain every previously visible
        // terminal body in its original coordinates, including unrelated CAD.
        std::set<u64> consumed,seen;for(const auto& f:session.document.features)for(u64 input:f.inputs)consumed.insert(input);
        for(const auto& body:session.document.evalBodies){const auto* feature=session.document.find(body.id);if(!feature||feature->suppressed||consumed.count(body.id)||!seen.insert(body.id).second)continue;
            AssemblyNode prior;prior.id=capacity.nextAssemblyId_++;prior.name=uniqueInstanceName(capacity,feature->name.empty()?"Existing output":feature->name+" instance");prior.feature=body.id;capacity.assembly.push_back(prior);
            V add=V::makeObj();add.set("op",V::makeStr("assembly.add"));add.set("name",V::makeStr(prior.name));add.set("feature",V::makeNum((double)body.id));commands.arr.push_back(add);preserved.arr.push_back(V::makeNum((double)prior.id));
        }
    }
    const auto instanceName=uniqueInstanceName(capacity,args.find("instance_name")?text(args,"instance_name"):requestedName+" instance");if(instanceName.size()>128)throw std::runtime_error("Component instance name exceeds 128 characters");
    V placement=V::makeObj();if(auto p=args.find("p")){keys(*p,{"tx","ty","tz","rx","ry","rz"});for(const auto& item:p->obj){if((item.second.t!=V::Num||!std::isfinite(item.second.num))&&(item.second.t!=V::Str||item.second.str.empty()||item.second.str.size()>2048))throw std::runtime_error("Placement parameters need finite values or expressions");placement.set(item.first,item.second);}}
    V assembly=V::makeObj();assembly.set("op",V::makeStr("assembly.add"));assembly.set("name",V::makeStr(instanceName));assembly.set("feature",V::makeNum((double)featureId));assembly.set("p",placement);commands.arr.push_back(assembly);
    // Saved documents have a 4 MiB load limit; insertion must not author a file
    // that the kernel itself cannot reopen. This proof copies authoring only.
    AssemblyNode node;node.id=capacity.nextAssemblyId_++;node.name=instanceName;node.feature=featureId;for(const auto& item:placement.obj)node.p[item.first]=item.second.t==V::Str?item.second.str:item.second.dump();capacity.assembly.push_back(node);
    if(documentToJson(capacity).size()>4u*1024u*1024u)throw std::runtime_error("Portable component insertion exceeds the 4 MiB .ark document capacity; the immutable component remains in the library");
    V apply=V::makeObj();apply.set("expected_revision",expected);apply.set("commands",commands);auto out=payload(session.call("doc_apply",apply));out.set("component_id",V::makeStr(componentId));out.set("feature_id",V::makeNum((double)featureId));out.set("instance_id",V::makeNum((double)node.id));out.set("preserved_output_instances",preserved);out.set("reused_feature",V::makeBool(reused));out.set("metadata_authority",V::makeStr("caller_declared_component_record"));out.set("manufacturing_qualified",V::makeBool(false));return out;
}
}
bool callComponentLibraryTool(AgentSession& session,const std::string& tool,const V& args,V& out){
    if(tool=="library_sources"){keys(args,{});out=componentSources();return true;}
    if(tool=="library_source_search"){out=searchComponentSource(args);return true;}
    if(tool!="library_ingest"&&tool!="library_search"&&tool!="library_get"&&tool!="library_insert"&&tool!="library_update"&&tool!="library_info"&&tool!="library_fetch")return false;
    if(tool=="library_ingest")keys(args,{"root","path","metadata","body_index","all_bodies"});
    else if(tool=="library_fetch")keys(args,{"root","url","metadata","body_index","all_bodies"});
    else if(tool=="library_search")keys(args,{"root","text","category","standard","thread_diameter_mm","thread_pitch_mm","length_mm","material","manufacturer","part_number","geometry_verified","limit","offset","outer_diameter_mm","inner_diameter_mm","thickness_mm","pin_count","pin_pitch_mm","rated_voltage_v","rated_current_a","strength_class","head_type","drive_type"});
    else if(tool=="library_get")keys(args,{"root","id"});else if(tool=="library_info")keys(args,{"root"});
    else if(tool=="library_update")keys(args,{"root","id","expected_revision","metadata"});else keys(args,{"root","id","expected_revision","name","instance_name","p"});
    auto database=library(args);
    if(tool=="library_ingest")out=database->ingest(text(args,"path"),need(args,"metadata"),validator(),bodySelection(args));
    else if(tool=="library_fetch"){
        const auto url=text(args,"url");validateComponentHttpsUrl(url);V metadata=need(args,"metadata");if(metadata.t!=V::Obj)throw std::runtime_error("Component metadata must be an object");if(auto declared=metadata.find("download_url"))if(declared->t!=V::Str||declared->str!=url)throw std::runtime_error("Declared download_url must match the actual downloaded STEP URL");metadata.set("download_url",V::makeStr(url));if(!metadata.find("source_url"))metadata.set("source_url",V::makeStr(url));
        static std::atomic<unsigned long long> serial{0};const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();const auto candidate=fs::temp_directory_path()/("ark-component-fetch-"+std::to_string(stamp)+"-"+std::to_string(++serial));if(!fs::create_directory(candidate))throw std::runtime_error("Cannot create private STEP download directory");
        struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{candidate};const auto destination=candidate/"source.step";downloadComponentStep(url,utf8(destination));out=database->ingest(utf8(destination),metadata,validator(),bodySelection(args));
    }else if(tool=="library_search"){V query=V::makeObj();for(const auto& item:args.obj)if(item.first!="root")query.set(item.first,item.second);out=database->search(query);}
    else if(tool=="library_get")out=database->get(text(args,"id"));
    else if(tool=="library_info")out=database->info();
    else if(tool=="library_update"){const auto& expected=need(args,"expected_revision");if(expected.t!=V::Num||!std::isfinite(expected.num)||expected.num<1||expected.num>9007199254740991.||expected.num!=std::floor(expected.num))throw std::runtime_error("Library expected_revision must be a positive integer record revision");out=database->update(text(args,"id"),(u64)expected.num,need(args,"metadata"));}
    else out=insert(session,*database,args);
    return true;
}
void appendComponentLibraryTools(V& tools){
    struct Definition {const char* name;const char* description;const char* schema;bool read;};const Definition definitions[]={
      {"library_sources","List component source catalogs, attribution and access requirements. Catalog availability does not attest standards or licenses.",R"({"type":"object","additionalProperties":false})",true},
      {"library_source_search","Discover public KiCad STEP source files by paged repository directory. source kicad, path directory, page and optional query. Does not download, classify geometric standards or edit CAD.",R"({"type":"object","properties":{"source":{"type":"string","enum":["kicad"]},"path":{"type":"string"},"page":{"type":"integer","minimum":1},"query":{"type":"string"}}})",true},
      {"library_fetch","Download one direct public HTTPS STEP URL, bounded64MiB/no redirect/auth, then validate and ingest its immutable source snapshot with caller-declared metadata. Ambiguous sources require body_index or all_bodies:true. download_url records the actual STEP URL; source_url may retain its catalog page. No automatic standard recognition. Does not alter CAD revision.",R"({"type":"object","properties":{"root":{"type":"string"},"url":{"type":"string"},"metadata":{"type":"object"},"body_index":{"type":"integer","minimum":0},"all_bodies":{"type":"boolean"}},"required":["url","metadata"],"additionalProperties":false})",false},
      {"library_ingest","Validate and index one local STEP immutable snapshot. Metadata is caller-declared; no filename/mesh recognition of standards or dimensions. Single body auto-selects; multiple bodies require explicit body_index or all_bodies:true to retain the complete source solid compound. Duplicate same source/body+metadata is idempotent; conflicting metadata requires library_update. root defaults ARK_COMPONENT_LIBRARY or local appdata/ARK/components.",R"({"type":"object","properties":{"root":{"type":"string"},"path":{"type":"string"},"metadata":{"type":"object"},"body_index":{"type":"integer","minimum":0},"all_bodies":{"type":"boolean"}},"required":["path","metadata"],"additionalProperties":false})",false},
      {"library_search","Search persistent indexed component metadata with bounded text/facets; no SQL or FTS language. Declared dimensional/standard fields remain declarations. Exact geometry is separately verified. limit1..100,offset0..1000000.",R"({"type":"object","properties":{"root":{"type":"string"},"text":{"type":"string"},"category":{"type":"string"},"standard":{"type":"string"},"thread_diameter_mm":{"type":"number"},"thread_pitch_mm":{"type":"number"},"length_mm":{"type":"number"},"outer_diameter_mm":{"type":"number"},"inner_diameter_mm":{"type":"number"},"thickness_mm":{"type":"number"},"pin_count":{"type":"integer"},"pin_pitch_mm":{"type":"number"},"rated_voltage_v":{"type":"number"},"rated_current_a":{"type":"number"},"strength_class":{"type":"string"},"head_type":{"type":"string"},"drive_type":{"type":"string"},"material":{"type":"string"},"manufacturer":{"type":"string"},"part_number":{"type":"string"},"geometry_verified":{"type":"boolean"},"limit":{"type":"integer","minimum":1,"maximum":100},"offset":{"type":"integer","minimum":0,"maximum":1000000}},"additionalProperties":false})",true},
      {"library_get","Read a compact immutable component identity, revision, declared metadata and native validation evidence without B-rep or mesh buffers.",R"({"type":"object","properties":{"root":{"type":"string"},"id":{"type":"string"}},"required":["id"],"additionalProperties":false})",true},
      {"library_insert","Insert a library component as exact source-owned native CAD plus a rigid assembly instance. Required expected_revision guards the CAD revision; subsequent insertions reuse the same definition. name/instance_name optional;p rigid tx/ty/tz/rx/ry/rz expressions. The portable version24 document embeds original selected STEP and B-rep, revalidates source/hash/material equivalence and rejects changed library assets. Metadata is not supplier/standard qualification; default purchased route identifies installation scope.",R"({"type":"object","properties":{"root":{"type":"string"},"id":{"type":"string"},"expected_revision":{"type":"integer","minimum":0},"name":{"type":"string"},"instance_name":{"type":"string"},"p":{"type":"object","properties":{"tx":{"type":["number","string"]},"ty":{"type":["number","string"]},"tz":{"type":["number","string"]},"rx":{"type":["number","string"]},"ry":{"type":["number","string"]},"rz":{"type":["number","string"]}},"additionalProperties":false}},"required":["id","expected_revision"],"additionalProperties":false})",false},
      {"library_update","Replace declared component metadata with expected_revision equal to the library record revision, independently of CAD revision. Immutable source geometry and identity remain bound to their hashes. Existing CAD documents retain their insertion metadata snapshot.",R"({"type":"object","properties":{"root":{"type":"string"},"id":{"type":"string"},"expected_revision":{"type":"integer","minimum":1},"metadata":{"type":"object"}},"required":["id","expected_revision","metadata"],"additionalProperties":false})",false},
      {"library_info","Inspect library database schema/statistics and configured storage location without transmitting component geometry.",R"({"type":"object","properties":{"root":{"type":"string"}},"additionalProperties":false})",true}
    };for(const auto& definition:definitions){V item=V::makeObj(),schema,annotations=V::makeObj();if(!arkjson::parse(definition.schema,schema))throw std::runtime_error("Invalid component library schema");item.set("name",V::makeStr(definition.name));item.set("description",V::makeStr(definition.description));item.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(definition.read));item.set("annotations",annotations);tools.arr.push_back(item);}
}
}
