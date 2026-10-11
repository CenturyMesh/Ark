#include "ark/agent.hpp"
#include "ark/bom.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
V parse(const std::string& text){V value;check(arkjson::parse(text,value),"fixture JSON parses");return value;}
V accepted(const V& result){if(result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());return *result.find("structuredContent");}
V call(AgentSession& session,const char* tool,const V& args=V::makeObj()){return accepted(session.call(tool,args));}
V apply(AgentSession& session,const std::string& commands){V args=V::makeObj();args.set("expected_revision",V::makeNum((double)session.revision));args.set("commands",parse(commands));return call(session,"doc_apply",args);}
const V& item(const V& bom,u64 id){for(const auto& row:bom.find("items")->arr)if(row.find("feature")->num==(double)id)return row;throw std::runtime_error("BOM item missing");}
std::string bytes(const fs::path& path){std::ifstream stream(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});}
std::string brep(const Document& doc,u64 id){for(const auto& body:doc.evalBodies)if(body.id==id&&body.nativeData.find("brep_base64"))return body.nativeData.find("brep_base64")->str;return {};}
void persistenceAndMetadata(const fs::path& directory){
    Document legacy,restored;std::string error;const auto legacyJson=documentToJson(legacy);check(documentFromJson(legacyJson,restored,&error)&&documentToJson(restored)==legacyJson,"legacy save version and representation remain unchanged without BOM authoring");
    AgentSession session;const auto before=documentToJson(session.document);auto invalid=session.call("doc_apply",parse(R"({"commands":[{"op":"bom.initialize"}]})"));check(invalid.find("isError")->b&&session.document.bomDocumentId.empty()&&before==documentToJson(session.document),"identity authoring requires revision and rejects without mutation");
    apply(session,R"([{"op":"material.set","name":"Declared steel","density":7.85},{"op":"feature.add","type":"native_solid","name":"Bracket","native":{"op":"box"},"p":{"width":20,"depth":10,"height":3},"material":"Declared steel","manufacturing":{"route":"machined"},"part":{"part_number":"TEST-BRACKET-01","description":"Authored bracket fixture","procurement":"make","representation":"exact"}}])");
    const auto id=session.document.bomDocumentId;check(validBomDocumentId(id),"host assigns persistent bounded BOM identity");auto bom=call(session,"bom_inspect");check(bom.find("metadata_complete")->b&&bom.find("items")->arr.size()==1&&!bom.find("manufacturing_qualified")->b,"complete declared metadata remains distinct from manufacturing qualification");
    const auto partId=item(bom,1).find("part_id")->str,shape=brep(session.document,1);check(!shape.empty(),"native fixture retains exact BRep");
    auto edited=apply(session,R"([{"op":"feature.edit","id":1,"part":{"part_number":"TEST-BRACKET-01","description":"Updated bracket fixture","procurement":"make","representation":"exact"}}])");
    check(!edited.find("geometry_recomputed")->b&&brep(session.document,1)==shape,"metadata-only edit preserves exact BRep and avoids geometry recomputation");
    check(session.document.find(1)&&session.document.find(1)->part.find("description")&&session.document.find(1)->part.find("description")->str=="Updated bracket fixture","metadata update actually commits the requested new description");
    check(session.document.bomDocumentId==id&&item(call(session,"bom_inspect"),1).find("part_id")->str==partId,"metadata edits preserve document and part identity");
    call(session,"doc_undo",parse("{\"expected_revision\":"+std::to_string(session.revision)+"}"));check(session.document.find(1)&&session.document.find(1)->part.find("description")&&session.document.find(1)->part.find("description")->str=="Authored bracket fixture","undo restores exact prior metadata");
    call(session,"doc_redo",parse("{\"expected_revision\":"+std::to_string(session.revision)+"}"));check(session.document.find(1)->part.find("description")->str=="Updated bracket fixture","redo restores revised metadata");
    const auto file=directory/"roundtrip.ark";check(saveDocument(session.document,file.string().c_str())&&loadDocument(file.string().c_str(),restored,&error),"binary .ark with journal and BOM reopens");check(restored.bomDocumentId==id&&restored.find(1)->part.dump()==session.document.find(1)->part.dump(),"native container retains identity, descriptions and all metadata");
    auto json=parse(documentToJson(session.document,false,false));check(json.find("version")->num==32,"new persisted BOM data uses document version 32");json.set("version",V::makeNum(31));check(!documentFromJson(json.dump(),restored,&error),"old schema cannot silently discard new metadata");
    AgentSession identityOnly;apply(identityOnly,R"([{"op":"bom.initialize"}])");check(validBomDocumentId(identityOnly.document.bomDocumentId),"explicit identity-only command actually persists on metadata-only path");
    const auto frozen=documentToJson(session.document);for(const auto* part:{R"({"procurement":"invented"})",R"({"source_url":"https://secret@example.test/x"})",R"({"sourcing_status":"selected"})",R"({"unknown":true})"}){
        V args=parse("{\"expected_revision\":"+std::to_string(session.revision)+",\"commands\":[{\"op\":\"feature.edit\",\"id\":1,\"part\":"+part+"}]}");check(session.call("doc_apply",args).find("isError")->b&&documentToJson(session.document)==frozen,"invalid metadata rolls back without changing geometry, revision or history");
    }
    const auto exported=directory/"bom.json";V args=parse("{\"expected_revision\":"+std::to_string(session.revision)+"}");args.set("path",V::makeStr(exported.string()));const auto receipt=call(session,"bom_export",args);auto payload=parse(bytes(exported));check(payload.find("schema")->str=="ark.bom.v1"&&payload.find("document_id")->str==id&&!receipt.find("uploaded")->b,"export has versioned schema/identity and performs no upload");check(bytes(exported).find("\"brep_base64\":")==std::string::npos,"BOM excludes native geometry payloads");
    const auto prior=bytes(exported);check(session.call("bom_export",args).find("isError")->b&&bytes(exported)==prior,"atomic export never replaces an existing BOM file");args.set("path",V::makeStr((directory/"stale.json").string()));args.set("expected_revision",V::makeNum(0));check(session.call("bom_export",args).find("isError")->b&&!fs::exists(directory/"stale.json"),"stale revision cannot publish a BOM");
}
void fastenersAndQuantities(){
    AgentSession session;call(session,"agent_onboard",parse(R"({"topic":"fasteners"})"));
    for(const auto* args:{R"({"kind":"screw","name":"Screw","diameter_mm":6,"length_mm":20,"head_diameter_mm":10,"head_height_mm":6})",R"({"kind":"washer","name":"Washer","inner_diameter_mm":6.4,"outer_diameter_mm":12,"thickness_mm":1.6})",R"({"kind":"nut","name":"Nut envelope","inner_diameter_mm":6,"outer_diameter_mm":10,"thickness_mm":5})"}){
        const auto before=documentToJson(session.document);const auto recipe=call(session,"fastener_recipe",parse(args));check(recipe.find("document_unchanged")->b&&documentToJson(session.document)==before,"recipe is read-only and creates no catalogue entry");call(session,"doc_apply",*recipe.find("apply"));
    }
    auto bom=call(session,"bom_inspect");check(bom.find("items")->arr.size()==3&&bom.find("fastener_quantity")->num==3,"BOM counts three terminal fasteners, not nine construction/history features");check(!bom.find("metadata_complete")->b,"generic fasteners retain missing procurement and specification evidence");
    const auto screw=call(session,"native_inspect",parse(R"({"id":3})"));check(screw.find("report")->find("brep_valid")->b&&std::abs(screw.find("report")->find("volume_mm3")->num-std::acos(-1.)*(25*6+9*20))<1e-6,"nominal screw is one valid exact solid with measured expected volume");
    apply(session,R"([{"op":"assembly.add","name":"Robot hardware","feature":0},{"op":"assembly.add","name":"Mount screws","parent":"Robot hardware","feature":"Screw","visible":false,"pattern":{"type":"linear","count":4,"dx":20}},{"op":"assembly.add","name":"Mount washers","parent":"Robot hardware","feature":"Washer","pattern":{"type":"linear","count":4,"dx":20}},{"op":"assembly.add","name":"Mount nuts","parent":"Robot hardware","feature":"Nut envelope","pattern":{"type":"linear","count":4,"dx":20}}])");
    bom=call(session,"bom_inspect");check(bom.find("groups")->arr.size()==1&&bom.find("occurrences")->arr.size()==12&&bom.find("fastener_quantity")->num==12,"hierarchy and pattern copies count twelve hardware occurrences");for(const auto& row:bom.find("items")->arr)check(row.find("quantity")->num==4,"each shared fastener definition has quantity four including hidden screws");
    const auto assembly=call(session,"assembly_info");check(assembly.find("bom")->dump()==bom.find("items")->dump(),"legacy assembly_info exposes the same enriched authoritative BOM");
    const auto page=call(session,"bom_inspect",parse(R"({"offset":1,"limit":1,"occurrence_offset":4,"occurrence_limit":2})"));check(page.find("partial")->b&&page.find("items")->arr.size()==1&&page.find("occurrences")->arr.size()==2&&page.find("page")->find("total_items")->num==3&&page.find("page")->find("total_occurrences")->num==12,"bounded pages retain complete inventory totals without pretending to be complete");check(page.find("items")->arr.front().dump()==bom.find("items")->arr[1].dump()&&page.find("content_sha256")->str==bom.find("content_sha256")->str&&page.find("authoring_sha256")->str==bom.find("authoring_sha256")->str,"page content and full-snapshot digest agree at one unchanged revision");
    check(session.call("bom_inspect",parse(R"({"limit":0})")).find("isError")->b&&session.call("bom_inspect",parse(R"({"offset":0.5})")).find("isError")->b,"invalid pagination rejects instead of silently truncating or coercing");
    const auto frozen=documentToJson(session.document);check(session.call("fastener_recipe",parse(R"({"kind":"screw","name":"Screw","diameter_mm":6,"length_mm":20,"head_diameter_mm":10,"head_height_mm":6})")).find("isError")->b&&documentToJson(session.document)==frozen,"same recipe rejects duplicate definition names without writes");
    apply(session,R"([{"op":"feature.edit","id":3,"part":{"part_number":"DUPLICATE","procurement":"unspecified"}},{"op":"feature.edit","id":6,"part":{"part_number":"DUPLICATE","procurement":"unspecified"}}])");bom=call(session,"bom_inspect");check(!bom.find("issues")->arr.empty()&&!bom.find("metadata_complete")->b&&bom.find("items")->arr.size()==3,"part-number collisions never silently merge distinct definitions");
    std::cout<<"BOM_NATIVE_FASTENERS definitions=3 occurrences=12 hidden_included=true no_sourcing_claim=true\n";
}
}
int main(){fs::path directory;try{directory=fs::temp_directory_path()/("ark-bom-test-"+newBomDocumentId());check(fs::create_directory(directory),"private fixture directory created");persistenceAndMetadata(directory);fastenersAndQuantities();fs::remove_all(directory);std::cout<<checks<<" BOM checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"BOM tests failed: "<<error.what()<<"; fixture "<<directory.string()<<'\n';return 1;}}
