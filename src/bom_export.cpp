#include "ark/bom.hpp"
#include "ark/agent.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/step_export.hpp"
#include "ark/io.hpp"
#include "ark/iteration_history.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace ark {
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
std::string text(const V& v,const char* key){const auto* p=v.find(key);return p?p->asStr():"";}
std::string pathText(const fs::path& path){auto s=path.u8string();return {s.begin(),s.end()};}
std::string bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);require((bool)in,"Cannot read exported file");return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& path,const V& value){std::ofstream out(path,std::ios::binary);auto s=value.dump();out.write(s.data(),(std::streamsize)s.size());out.flush();require((bool)out,"Cannot write batch export manifest");}
void publish(const fs::path& from,const fs::path& to){
#ifdef _WIN32
    require(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"Batch destination exists or atomic publication failed");
#elif defined(__linux__)
    require(syscall(SYS_renameat2,AT_FDCWD,from.c_str(),AT_FDCWD,to.c_str(),1)==0,"Batch destination exists or atomic publication failed");
#else
    throw std::runtime_error("Atomic no-replace batch publication is unavailable on this platform");
#endif
}
}
V exportBomParts(AgentSession& session,const V& args){
    require(args.t==V::Obj,"BOM batch export requires object arguments");
    for(const auto& entry:args.obj)require(entry.first=="path"||entry.first=="expected_revision"||entry.first=="formats"||entry.first=="features"||entry.first=="procurement"||entry.first=="pose","Unknown batch export field: "+entry.first);
    const auto* expected=args.find("expected_revision");require(expected&&expected->t==V::Num&&expected->num==(double)session.revision,"Revision conflict; refresh BOM before batch export");
    const auto requested=text(args,"path");require(!requested.empty()&&requested.size()<=4096,"Choose a new local output directory");
    const auto destination=fs::absolute(fs::u8path(requested)).lexically_normal();require(!fs::exists(destination),"Batch destination already exists; choose a new directory");
    require(fs::is_directory(destination.parent_path()),"Batch destination parent directory does not exist");
    require(!session.document.bomDocumentId.empty(),"Initialize persistent BOM identity before batch export");
    const auto bom=inspectBom(session.document,session.revision);const u64 revision=session.revision;
    std::vector<std::string> formats;const auto* values=args.find("formats");require(values&&values->t==V::Arr&&!values->arr.empty()&&values->arr.size()<=6,"Choose 1..6 formats");
    for(const auto& value:values->arr){require(value.t==V::Str&&(value.str=="step"||value.str=="iges"||value.str=="stl"||value.str=="obj"||value.str=="fbx"||value.str=="dxf"),"Format must be step, iges, stl, obj, fbx or dxf");require(std::find(formats.begin(),formats.end(),value.str)==formats.end(),"Duplicate batch format");formats.push_back(value.str);}
    const auto procurement=args.find("procurement")?text(args,"procurement"):"all";require(procurement=="all"||procurement=="make"||procurement=="buy","Procurement filter must be all, make or buy");
    const auto pose=args.find("pose")?text(args,"pose"):"formed";require(pose=="formed"||pose=="flat","Pose must be formed or flat");
    std::set<u64> selected;if(const auto* list=args.find("features")){require(list->t==V::Arr&&!list->arr.empty()&&list->arr.size()<=4096,"features must contain 1..4096 counted definition IDs");for(const auto& id:list->arr){require(id.t==V::Num&&std::isfinite(id.num)&&id.num>=1&&id.num<=9007199254740991.&&id.num==std::floor(id.num),"Invalid selected feature ID");require(selected.insert((u64)id.num).second,"Duplicate selected definition ID");}}
    std::set<u64> counted;std::vector<V> rows;
    for(const auto& row:bom.find("items")->arr){const u64 id=(u64)row.find("feature")->num;counted.insert(id);if(!selected.empty()&&!selected.count(id))continue;if(procurement!="all"&&text(*row.find("metadata"),"procurement")!=procurement)continue;rows.push_back(row);}
    for(u64 id:selected)require(counted.count(id)!=0,"Selected definition is not a counted BOM part: "+std::to_string(id));require(!rows.empty(),"No BOM parts match the export selection");
    const auto staging=destination.parent_path()/(".ark-batch-"+newBomDocumentId());require(fs::create_directory(staging),"Cannot create batch staging directory");
    struct Cleanup{fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{staging};
    V files=V::makeArr();
    for(const auto& row:rows){const u64 id=(u64)row.find("feature")->num;const auto* feature=session.document.find(id);require(feature!=nullptr,"BOM feature disappeared");
        std::string name=text(*row.find("metadata"),"part_number");if(name.empty())name=text(row,"name");for(char& c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))c='_';if(name.size()>100)name.resize(100);name="part-"+std::to_string(id)+"-"+name;
        for(const auto& format:formats){const auto file=name+"-"+format+"."+format;V request=V::makeObj();request.set("id",V::makeNum((double)id));request.set("path",V::makeStr(pathText(staging/file)));std::string tool;
            const bool sheet=feature->type==FeatureType::SheetMetal||feature->type==FeatureType::SourceSheet;
            if(format=="step"||format=="iges"){tool=format=="step"?"export_step":"export_iges";request.set("pose",V::makeStr(sheet?pose:"formed"));}
            else if(format=="dxf"){require(sheet,"Flat DXF requires an authored sheet definition");tool="export_flat_dxf";}
            else{require(pose!="flat"||!sheet,"Flat mesh export is unavailable; use STEP, IGES or DXF");tool="export_mesh";request.set("format",V::makeStr(format));}
            V receipt;
            if(format=="step"&&!isNativeFeature(feature->type)&&feature->type!=FeatureType::Tube&&feature->type!=FeatureType::SourceTube&&feature->type!=FeatureType::NotchedTube){
                ensureNativeBody(session.document,id,session.document.evalBodies);const V* brep=nullptr;for(const auto& body:session.document.evalBodies)if(body.id==id)brep=body.nativeData.find("brep_base64");require(brep&&brep->t==V::Str,"Part has no exact STEP representation");receipt=exportNativeBrep(brep->str,pathText(staging/file));
            }else{auto result=session.call(tool,request);require(!result.find("isError")->b,"Batch export failed for "+file+": "+text(*result.find("structuredContent"),"error"));receipt=*result.find("structuredContent");}
            require(fs::is_regular_file(staging/file),"Exporter did not produce a file");V item=V::makeObj();item.set("feature",V::makeNum((double)id));item.set("part_id",*row.find("part_id"));item.set("quantity",*row.find("quantity"));item.set("path",V::makeStr(file));item.set("format",V::makeStr(format));item.set("file_sha256",V::makeStr(iterationSha256(bytes(staging/file))));item.set("pose",V::makeStr(sheet?pose:"definition_local"));item.set("authority",V::makeStr(format=="step"||format=="iges"?"exact_brep":format=="dxf"?"authored_flat_pattern":"display_mesh"));V validation=V::makeObj();for(const char* key:{"iges_roundtrip_bounds_error_mm","iges_roundtrip_area_error_mm2","solid_topology_preserved","units","authority"})if(const auto* value=receipt.find(key))validation.set(key,*value);if(!validation.obj.empty())item.set("exchange_validation",validation);files.arr.push_back(item);
        }
    }
    V supporting=V::makeArr();for(const auto& entry:fs::directory_iterator(staging)){const auto file=pathText(entry.path().filename());bool primary=false;for(const auto& row:files.arr)primary|=text(row,"path")==file;if(!primary){V row=V::makeObj();row.set("path",V::makeStr(file));row.set("file_sha256",V::makeStr(iterationSha256(bytes(entry.path()))));supporting.arr.push_back(row);}}
    require(revision==session.revision,"Document changed during batch export");write(staging/"bom.json",bom);V manifest=V::makeObj();manifest.set("schema",V::makeStr("ark.bom-export.v1"));manifest.set("revision",V::makeNum((double)revision));manifest.set("document_id",*bom.find("document_id"));manifest.set("authoring_sha256",*bom.find("authoring_sha256"));manifest.set("bom_file",V::makeStr("bom.json"));manifest.set("bom_file_sha256",V::makeStr(iterationSha256(bytes(staging/"bom.json"))));manifest.set("files",files);manifest.set("supporting_files",supporting);manifest.set("definition_count",V::makeNum((double)rows.size()));manifest.set("occurrence_transforms_baked",V::makeBool(false));write(staging/"manifest.json",manifest);publish(staging,destination);
    manifest.set("path",V::makeStr(pathText(destination)));manifest.set("document_unchanged",V::makeBool(true));return manifest;
}
}
