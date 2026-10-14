#include "ark/bom.hpp"
#include "ark/assembly_io.hpp"
#include "ark/iteration_history.hpp"
#include "ark/native_features.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
std::string text(const V& v,const char* key){const auto* p=v.find(key);return p?p->asStr():"";}
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
const char* fields[]={"part_number","description","part_revision","category","procurement","manufacturer","manufacturer_part_number","supplier","supplier_part_number","source_url","datasheet_url","standard","specification","representation","sourcing_status"};
bool oneOf(const std::string& value,std::initializer_list<const char*> values){for(const auto* item:values)if(value==item)return true;return false;}
void expected(u64 revision,const V& args){const auto* p=args.find("expected_revision");require(p&&p->t==V::Num&&p->num==(double)revision,"Revision conflict; inspect current doc_info before BOM export");}
void issue(V& list,const std::string& field){list.arr.push_back(V::makeStr(field));}
V effectiveMetadata(const Feature& feature){
    V meta=V::makeObj();
    if(const auto* imported=feature.nativeSpec.find("component_metadata");featureOperation(feature)=="imported_brep"&&imported&&imported->t==V::Obj){
        for(const auto* key:{"description","category","manufacturer","source_url","standard","part_revision"})if(const auto* p=imported->find(key))meta.set(key,*p);
        if(const auto* p=imported->find("part_number"))meta.set("manufacturer_part_number",*p);
        meta.set("procurement",V::makeStr("buy"));meta.set("representation",V::makeStr("exact_imported"));
    }
    if(feature.part.t==V::Obj)for(const auto& [key,value]:feature.part.obj)meta.set(key,value);
    return meta;
}
std::string identity(const Document& d,const std::string& kind,u64 id){return (d.bomDocumentId.empty()?"unassigned":d.bomDocumentId)+"/"+kind+"/"+std::to_string(id);}
V nominalFeature(const std::string& name,const char* op,const V& parameters,const V& inputs=V{}){
    V command=V::makeObj(),native=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("native_solid"));command.set("name",V::makeStr(name));native.set("op",V::makeStr(op));command.set("native",native);command.set("p",parameters);if(inputs.t==V::Arr)command.set("inputs",inputs);return command;
}
V inputs(std::initializer_list<std::string> names){V out=V::makeArr();for(const auto& name:names)out.arr.push_back(V::makeStr(name));return out;}
double dimension(const V& args,const char* key){const auto* p=args.find(key);require(p&&p->t==V::Num&&std::isfinite(p->num)&&p->num>0&&p->num<=10000,std::string("Fastener requires an explicit positive dimension in mm: ")+key);return p->num;}
V numbers(std::initializer_list<std::pair<const char*,double>> values){V out=V::makeObj();for(const auto& [key,value]:values)out.set(key,V::makeNum(value));return out;}
}

bool validBomDocumentId(const std::string& value){return value.size()==36&&value.rfind("ark-",0)==0&&value.substr(4).find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string newBomDocumentId(){std::random_device random;std::string id="ark-";for(int i=0;i<16;++i){const unsigned byte=random()&255;id+="0123456789abcdef"[byte>>4];id+="0123456789abcdef"[byte&15];}return id;}
V validatePartMetadata(const V& value){
    require(value.t==V::Obj&&value.dump().size()<=16384,"part must be an object of at most 16 KiB");V out=V::makeObj();
    for(const auto& [key,item]:value.obj){bool known=false;for(const auto* f:fields)known|=key==f;require(known,"Unknown part metadata field: "+key);require(item.t==V::Str&&item.str.size()<=2048&&item.str.find_first_of("\r\n\t")==std::string::npos&&std::none_of(item.str.begin(),item.str.end(),[](unsigned char c){return c<32;}),"Part metadata must be bounded single-line text: "+key);}
    for(const auto* key:fields)if(const auto* item=value.find(key))out.set(key,*item);
    for(const auto* key:{"source_url","datasheet_url"})if(const auto url=text(out,key);!url.empty())require(url.rfind("https://",0)==0&&url.size()>8&&url.find('@')==std::string::npos&&url.find(' ')==std::string::npos,"Source links require explicit HTTPS URLs without credentials");
    if(out.find("procurement"))require(oneOf(text(out,"procurement"),{"make","buy","unspecified"}),"procurement must be make, buy or unspecified");
    if(out.find("representation"))require(oneOf(text(out,"representation"),{"exact","exact_imported","simplified","envelope","unspecified"}),"Unknown part representation");
    if(out.find("sourcing_status"))require(oneOf(text(out,"sourcing_status"),{"unresolved","candidate","selected"}),"Unknown sourcing status");
    if(text(out,"sourcing_status")=="selected")require(!text(out,"source_url").empty()&&(!text(out,"manufacturer_part_number").empty()||!text(out,"supplier_part_number").empty()),"Selected sourcing requires a source URL and an explicit manufacturer or supplier reference");
    return out;
}
V partMetadataSchema(){
    V schema=V::makeObj(),props=V::makeObj();schema.set("type",V::makeStr("object"));schema.set("additionalProperties",V::makeBool(false));
    for(const auto* key:fields){V item=V::makeObj();item.set("type",V::makeStr("string"));item.set("maxLength",V::makeNum(2048));item.set("pattern",V::makeStr("^[^\\x00-\\x1F]*$"));props.set(key,item);}
    auto enumeration=[&](const char* key,std::initializer_list<const char*> values){V item=*props.find(key),list=V::makeArr();for(const auto* value:values)list.arr.push_back(V::makeStr(value));item.set("enum",list);props.set(key,item);};
    enumeration("procurement",{"make","buy","unspecified"});enumeration("representation",{"exact","exact_imported","simplified","envelope","unspecified"});enumeration("sourcing_status",{"unresolved","candidate","selected"});
    for(const auto* key:{"source_url","datasheet_url"}){V item=*props.find(key);item.set("pattern",V::makeStr("^$|^https://[^@\\s]+$"));props.set(key,item);}
    V selection;arkjson::parse(R"([{"not":{"required":["sourcing_status"],"properties":{"sourcing_status":{"enum":["selected"]}}}},{"required":["source_url"],"properties":{"source_url":{"minLength":9}},"anyOf":[{"required":["manufacturer_part_number"],"properties":{"manufacturer_part_number":{"minLength":1}}},{"required":["supplier_part_number"],"properties":{"supplier_part_number":{"minLength":1}}}]}])",selection);schema.set("anyOf",selection);
    schema.set("properties",props);return schema;
}

V inspectBom(const Document& d,u64 revision){
    V out=V::makeObj(),items=V::makeArr(),occurrences=V::makeArr(),groups=V::makeArr(),issues=V::makeArr(),unplaced=V::makeArr();
    std::map<u64,size_t> quantities;std::vector<AssemblyPlacement> placements;std::string error;
    if(!d.assembly.empty())require(resolveAssembly(d,placements,error),error);
    else{
        std::set<u64> consumed;for(const auto& f:d.features)if(!f.suppressed&&featureConsumesInputBodies(f))for(u64 id:f.inputs)consumed.insert(id);
        for(const auto& body:d.evalBodies)if(const auto* f=d.find(body.id);f&&!f->suppressed&&!consumed.count(f->id)&&!isCurveFeature(f->type)&&f->type!=FeatureType::Suspension){AssemblyPlacement p;p.feature=f->id;p.name=f->name;p.world=Mat4::identity();p.visible=true;placements.push_back(p);}
    }
    for(const auto& node:d.assembly)if(!node.feature){V group=V::makeObj();group.set("id",V::makeStr(identity(d,"node",node.id)));group.set("node",V::makeNum((double)node.id));group.set("parent",V::makeNum((double)node.parent));group.set("name",V::makeStr(node.name));groups.arr.push_back(group);}
    for(const auto& p:placements){const auto* feature=d.find(p.feature);require(feature&&!feature->suppressed,"BOM occurrence references a missing or suppressed feature");++quantities[p.feature];V occurrence=V::makeObj();
        occurrence.set("id",V::makeStr(identity(d,p.node?"node":"standalone",p.node?p.node:p.feature)+"/copy/"+std::to_string(p.copy)));occurrence.set("part_id",V::makeStr(identity(d,"feature",p.feature)));occurrence.set("feature",V::makeNum((double)p.feature));occurrence.set("node",V::makeNum((double)p.node));occurrence.set("copy",V::makeNum(p.copy));occurrence.set("name",V::makeStr(p.name));occurrence.set("visible",V::makeBool(p.visible));
        u64 parent=0;for(const auto& n:d.assembly)if(n.id==p.node)parent=n.parent;occurrence.set("parent",V::makeNum((double)parent));occurrences.arr.push_back(occurrence);
    }
    size_t fasteners=0;std::map<std::string,u64> partNumbers;
    for(const auto& [id,quantity]:quantities){const auto& feature=*d.find(id);const V meta=effectiveMetadata(feature);V row=V::makeObj(),missing=V::makeArr();row.set("part_id",V::makeStr(identity(d,"feature",id)));row.set("feature",V::makeNum((double)id));row.set("name",V::makeStr(feature.name));row.set("quantity",V::makeNum((double)quantity));row.set("unit",V::makeStr("each"));row.set("metadata",meta);row.set("metadata_authority",V::makeStr("caller_declared; source custody is separate"));row.set("manufacturing_route",V::makeStr(feature.manufacturingRoute));row.set("geometry_kind",V::makeStr(featureTypeName(feature.type)));
        std::string material=feature.material;if(material.empty())if(const auto* imported=feature.nativeSpec.find("component_metadata"))material=text(*imported,"material");row.set("material",V::makeStr(material));
        for(const auto* key:{"part_number","description","procurement"})if(text(meta,key).empty()||text(meta,key)=="unspecified")issue(missing,key);
        if(material.empty())issue(missing,"material");if(text(meta,"procurement")=="make"&&(feature.manufacturingRoute.empty()||feature.manufacturingRoute=="unspecified"||feature.manufacturingRoute=="purchased"))issue(missing,"manufacturing_route");
        if(text(meta,"procurement")=="buy"){if(text(meta,"source_url").empty())issue(missing,"source_url");if(text(meta,"manufacturer_part_number").empty()&&text(meta,"supplier_part_number").empty())issue(missing,"purchased_reference");if(text(meta,"sourcing_status")!="selected")issue(missing,"sourcing_selection");if(!text(meta,"manufacturer_part_number").empty()&&text(meta,"manufacturer").empty())issue(missing,"manufacturer");if(!text(meta,"supplier_part_number").empty()&&text(meta,"supplier").empty())issue(missing,"supplier");}
        if(text(meta,"representation")=="simplified"||text(meta,"representation")=="envelope")issue(missing,"representation_requires_engineering_review");
        const auto pn=text(meta,"part_number");if(!pn.empty()&&!partNumbers.emplace(pn,id).second)issue(issues,"duplicate_internal_part_number:"+pn);
        if(text(meta,"category").rfind("fastener.",0)==0)fasteners+=quantity;
        if(featureOperation(feature)=="imported_brep")if(const auto* imported=feature.nativeSpec.find("component_metadata")){for(const auto* key:{"license","provenance","download_url"})if(const auto* value=imported->find(key))row.set(std::string("source_")+key,*value);}
        if(const auto* component=feature.nativeSpec.find("component_id"))row.set("library_component_id",*component);
        if(const auto* package=feature.nativeSpec.find("package")){V source=V::makeObj();for(const auto* key:{"source_sha256","brep_sha256","body_index"})if(const auto* value=package->find(key))source.set(key,*value);row.set("source_custody",source);}
        for(const auto& body:d.evalBodies)if(body.id==id)if(const auto* brep=body.nativeData.find("brep_base64")){row.set("brep_base64_sha256",V::makeStr(iterationSha256(brep->str)));break;}
        row.set("missing_or_review_required",missing);row.set("metadata_complete",V::makeBool(missing.arr.empty()));items.arr.push_back(row);
    }
    for(const auto& f:d.features)if(!f.suppressed&&f.part.t==V::Obj&&!quantities.count(f.id)){V row=V::makeObj();row.set("feature",V::makeNum((double)f.id));row.set("reason",V::makeStr("Annotated definition is not a counted output/occurrence; an operation may have replaced it. Reassign metadata explicitly if this is still the same intended part."));unplaced.arr.push_back(row);}
    if(d.bomDocumentId.empty())issue(issues,"document_identity_unassigned: use bom.initialize in revision-checked doc_apply");
    out.set("authoring_sha256",V::makeStr(iterationSha256(documentToJson(d,false,false))));
    out.set("schema",V::makeStr("ark.bom.v1"));out.set("document_id",V::makeStr(d.bomDocumentId));out.set("revision",V::makeNum((double)revision));out.set("units",V::makeStr("mm"));out.set("scope",V::makeStr(d.assembly.empty()?"terminal_output_definitions":"placed_occurrences_including_hidden"));out.set("items",items);out.set("occurrences",occurrences);out.set("groups",groups);out.set("unplaced_annotated_definitions",unplaced);out.set("issues",issues);out.set("fastener_quantity",V::makeNum((double)fasteners));out.set("fastening_completeness",V::makeStr("not_certified; inspect every required connection and mounting interface"));
    out.set("metadata_complete",V::makeBool(!items.arr.empty()&&issues.arr.empty()&&unplaced.arr.empty()&&std::all_of(items.arr.begin(),items.arr.end(),[](const V& row){return row.find("metadata_complete")->b;})));
    out.set("manufacturing_qualified",V::makeBool(false));out.set("meshworks_adapter",V::makeStr("not_implemented"));out.set("identity_scope",V::makeStr("document identity plus feature/node IDs; renaming and editing preserve IDs, replacing a definition creates a new part identity"));out.set("content_sha256",V::makeStr(iterationSha256(out.dump())));return out;
}

V inspectBomPage(const Document& document,u64 revision,const V& args){
    require(args.t==V::Obj,"BOM inspection requires object arguments");
    for(const auto& entry:args.obj)require(oneOf(entry.first,{"offset","limit","occurrence_offset","occurrence_limit"}),"Unknown BOM inspection argument: "+entry.first);
    auto integer=[&](const char* key,size_t fallback,size_t low,size_t high){const auto* value=args.find(key);if(!value)return fallback;require(value->t==V::Num&&std::isfinite(value->num)&&value->num>=(double)low&&value->num<=(double)high&&value->num==std::floor(value->num),std::string("Invalid BOM page bound: ")+key);return (size_t)value->num;};
    const size_t offset=integer("offset",0,0,1000000),limit=integer("limit",64,1,100),occurrenceOffset=integer("occurrence_offset",0,0,1000000),occurrenceLimit=integer("occurrence_limit",128,1,256);
    V out=inspectBom(document,revision),page=V::makeObj();bool partial=false;
    auto slice=[&](const char* key,size_t start,size_t count){const auto& all=*out.find(key);page.set(std::string("total_")+key,V::makeNum((double)all.arr.size()));const size_t begin=std::min(start,all.arr.size()),end=begin+std::min(count,all.arr.size()-begin);V selected=V::makeArr();selected.arr.assign(all.arr.begin()+begin,all.arr.begin()+end);partial|=selected.arr.size()!=all.arr.size();out.set(key,std::move(selected));};
    slice("items",offset,limit);slice("occurrences",occurrenceOffset,occurrenceLimit);page.set("offset",V::makeNum((double)offset));page.set("limit",V::makeNum((double)limit));page.set("occurrence_offset",V::makeNum((double)occurrenceOffset));page.set("occurrence_limit",V::makeNum((double)occurrenceLimit));out.set("page",page);out.set("partial",V::makeBool(partial));out.set("digest_scope",V::makeStr("content_sha256 describes the complete snapshot before pagination; revision and authoring_sha256 must match across pages"));return out;
}

V exportBom(const Document& document,u64 revision,const V& args){
    require(args.t==V::Obj,"BOM export requires object arguments");for(const auto& entry:args.obj)require(entry.first=="path"||entry.first=="expected_revision","Unknown BOM export argument: "+entry.first);
    expected(revision,args);const auto path=text(args,"path");require(!path.empty()&&path.size()<=4096,"BOM export requires a local JSON path");const auto destination=std::filesystem::u8path(path);require(destination.extension()==".json","BOM export path must end in .json");require(!document.bomDocumentId.empty(),"Initialize persistent BOM identity before export");
    const auto data=inspectBom(document,revision);const auto bytes=data.dump();const auto temporary=std::filesystem::u8path(path+"."+newBomDocumentId()+".tmp");require(!std::filesystem::exists(destination)&&!std::filesystem::exists(temporary),"BOM destination already exists; choose a new revision export path");
    {std::ofstream stream(temporary,std::ios::binary);stream.write(bytes.data(),(std::streamsize)bytes.size());stream.flush();if(!stream){stream.close();std::error_code cleanup;std::filesystem::remove(temporary,cleanup);throw std::runtime_error("BOM write failed; no destination published");}}
    std::error_code error;std::filesystem::create_hard_link(temporary,destination,error);std::error_code cleanup;std::filesystem::remove(temporary,cleanup);require(!error,"Atomic BOM publication failed; existing destination preserved");V out=V::makeObj();out.set("path",V::makeStr(path));out.set("revision",V::makeNum((double)revision));out.set("schema",V::makeStr("ark.bom.v1"));out.set("file_sha256",V::makeStr(iterationSha256(bytes)));out.set("metadata_complete",*data.find("metadata_complete"));out.set("contains_private_design_metadata",V::makeBool(true));out.set("uploaded",V::makeBool(false));return out;
}

V fastenerRecipe(const Document& document,u64 revision,const V& args){
    const auto kind=text(args,"kind"),name=text(args,"name");require(oneOf(kind,{"screw","nut","washer"})&&!name.empty()&&name.size()<=100,"Specify screw, nut or washer and a unique bounded name");
    for(const auto& f:document.features)require(f.name!=name&&f.name.rfind(name+" / ",0)!=0,"Fastener recipe names already exist; inspect/reuse the existing definition");
    for(const auto& [key,value]:args.obj){(void)value;require(oneOf(key,{"kind","name","diameter_mm","length_mm","head_diameter_mm","head_height_mm","inner_diameter_mm","outer_diameter_mm","thickness_mm","part","material"}),"Unknown fastener recipe field: "+key);}
    V commands=V::makeArr(),meta=args.find("part")?validatePartMetadata(*args.find("part")):V::makeObj();meta.set("category",V::makeStr("fastener."+kind));meta.set("representation",V::makeStr("simplified"));if(!meta.find("procurement"))meta.set("procurement",V::makeStr("unspecified"));if(!meta.find("description"))meta.set("description",V::makeStr("Editable nominal "+kind+"; no thread, drive, tolerance or standard certification"));
    if(kind=="screw"){
        const auto diameter=dimension(args,"diameter_mm"),length=dimension(args,"length_mm"),head=dimension(args,"head_diameter_mm"),height=dimension(args,"head_height_mm");require(head>diameter,"Screw head diameter must exceed shank diameter");
        // Head and shank share the origin: the head occupies the first height;
        // nominal under-head length is therefore length, total is length+height.
        commands.arr.push_back(nominalFeature(name+" / shank","cylinder",numbers({{"radius",diameter/2},{"height",length+height}})));
        commands.arr.push_back(nominalFeature(name+" / head","cylinder",numbers({{"radius",head/2},{"height",height}})));
        commands.arr.push_back(nominalFeature(name,"union",V::makeObj(),inputs({name+" / shank",name+" / head"})));
    }else{
        const auto inner=dimension(args,"inner_diameter_mm"),outer=dimension(args,"outer_diameter_mm"),height=dimension(args,"thickness_mm");require(outer>inner,"Fastener outer diameter must exceed inner diameter");
        commands.arr.push_back(nominalFeature(name+" / outer","cylinder",numbers({{"radius",outer/2},{"height",height}})));
        commands.arr.push_back(nominalFeature(name+" / bore","cylinder",numbers({{"radius",inner/2},{"height",height}})));
        commands.arr.push_back(nominalFeature(name,"cut",V::makeObj(),inputs({name+" / outer",name+" / bore"})));
    }
    if(!meta.find("specification")){V dimensions=V::makeObj();for(const auto& entry:args.obj)if(entry.first.size()>3&&entry.first.substr(entry.first.size()-3)=="_mm")dimensions.set(entry.first,entry.second);meta.set("specification",V::makeStr("Caller-declared nominal dimensions at recipe creation, mm: "+dimensions.dump()+". No thread, drive or standard certification."));}
    commands.arr.back().set("part",meta);if(const auto* material=args.find("material")){require(material->t==V::Str,"Fastener material must name a document material");commands.arr.back().set("material",*material);}
    V apply=V::makeObj(),out=V::makeObj();apply.set("expected_revision",V::makeNum((double)revision));apply.set("commands",commands);out.set("apply",apply);out.set("revision",V::makeNum((double)revision));out.set("document_unchanged",V::makeBool(true));out.set("geometry",V::makeStr(kind=="nut"?"Cylindrical annular nut envelope; no hex, thread or drive":"Nominal simplified solid; no thread, drive or tolerances"));out.set("manufacturing_qualified",V::makeBool(false));out.set("next_step",V::makeStr("Review explicit dimensions, apply atomically, inspect native validity, then place required assembly instances and check mounting interfaces. A recipe is not a sourced catalogue part."));return out;
}

void appendBomTools(V& tools){
    auto add=[&](const char* name,const char* description,const char* json,bool readOnly){V tool=V::makeObj(),schema,annotations=V::makeObj();require(arkjson::parse(json,schema),"Invalid BOM discovery schema");tool.set("name",V::makeStr(name));tool.set("description",V::makeStr(description));tool.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(readOnly));tool.set("annotations",annotations);tools.arr.push_back(tool);};
    add("bom_export_parts","Export counted BOM definitions once per format, with occurrence quantities, full BOM and SHA256 manifest. New directory published atomically only if every selected export succeeds. Includes hidden occurrences; local definition coordinates. Optional features/procurement filters. STEP/IGES exact, mesh formats display authority, DXF sheets only. Requires user export authorization and current expected_revision.",R"({"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":4096},"expected_revision":{"type":"integer","minimum":0},"formats":{"type":"array","minItems":1,"maxItems":6,"uniqueItems":true,"items":{"type":"string","enum":["step","iges","stl","obj","fbx","dxf"]}},"features":{"type":"array","minItems":1,"uniqueItems":true,"items":{"type":"integer","minimum":1}},"procurement":{"type":"string","enum":["all","make","buy"]},"pose":{"type":"string","enum":["formed","flat"]}},"required":["path","expected_revision","formats"],"additionalProperties":false})",false);
    add("bom_review","Required before Studio design completion: review current BOM at expected_revision. concept requires explicit pending_notes when incomplete; detailed rejects every unresolved field. Every physical definition needs internal part_number and description. fastening_review must describe inspected connections, missing hardware or justified non-applicability. Never invent sourcing or downgrade a requested detailed deliverable to concept; report needs_input instead. Read bom_inspect for current evidence.",R"({"type":"object","properties":{"expected_revision":{"type":"integer","minimum":0},"scope":{"type":"string","enum":["concept","detailed"]},"pending_notes":{"type":"string","minLength":1,"maxLength":2048},"fastening_review":{"type":"string","minLength":1,"maxLength":2048}},"required":["expected_revision","scope","fastening_review"],"additionalProperties":false})",true);
    add("bom_inspect","Read current ark.bom.v1: physical definition quantities/occurrences including hidden parts, declared metadata, source hashes, missing fields and fasteners. Defaults to 64 items/128 occurrences; partial/page totals are explicit. Read all pages at matching revision and authoring_sha256 when full detail is needed. Aggregate completeness/digest describe the full snapshot. No geometry payload. Use bom.initialize and feature.add/edit part in revision-checked doc_apply.",R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0,"maximum":1000000},"limit":{"type":"integer","minimum":1,"maximum":100},"occurrence_offset":{"type":"integer","minimum":0,"maximum":1000000},"occurrence_limit":{"type":"integer","minimum":1,"maximum":256}},"additionalProperties":false})",true);
    add("bom_export","Atomically export the current BOM to a new local .json path with observed expected_revision. Requires persisted identity; exports drafts with missing fields explicitly. Never overwrites files or uploads to MeshWorks; contains private design metadata.",R"({"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":4096},"expected_revision":{"type":"integer","minimum":0}},"required":["path","expected_revision"],"additionalProperties":false})",false);
    add("fastener_recipe","Read an editable three-command native-solid recipe for an explicitly dimensioned screw, washer or annular nut envelope. No automatic standard/supplier recognition, threads, drives or catalogue insertion. Inspect the local library first for specified purchased parts. Apply returned commands atomically; place/reuse the final definition, not its construction solids.",R"({"type":"object","properties":{"kind":{"type":"string","enum":["screw","nut","washer"]},"name":{"type":"string","minLength":1,"maxLength":100},"diameter_mm":{"type":"number","exclusiveMinimum":0,"maximum":10000},"length_mm":{"type":"number","exclusiveMinimum":0,"maximum":10000},"head_diameter_mm":{"type":"number","exclusiveMinimum":0,"maximum":10000},"head_height_mm":{"type":"number","exclusiveMinimum":0,"maximum":10000},"inner_diameter_mm":{"type":"number","exclusiveMinimum":0,"maximum":10000},"outer_diameter_mm":{"type":"number","exclusiveMinimum":0,"maximum":10000},"thickness_mm":{"type":"number","exclusiveMinimum":0,"maximum":10000},"part":{"type":"object"},"material":{"type":"string"}},"required":["kind","name"],"additionalProperties":false,"oneOf":[{"properties":{"kind":{"enum":["screw"]}},"required":["diameter_mm","length_mm","head_diameter_mm","head_height_mm"]},{"properties":{"kind":{"enum":["nut","washer"]}},"required":["inner_diameter_mm","outer_diameter_mm","thickness_mm"]}]})",true);
    V schema=*tools.arr.back().find("inputSchema"),props=*schema.find("properties");props.set("part",partMetadataSchema());schema.set("properties",props);tools.arr.back().set("inputSchema",schema);
}
}
