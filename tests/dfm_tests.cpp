#include "ark/agent.hpp"
#include "ark/dfm.hpp"
#include "ark/manufacturing.hpp"
#include <cmath>
#ifdef ARK_DFM_NATIVE_TESTS
#include "ark/native_backend.hpp"
#include <filesystem>
#include <fstream>
#endif
#include <iostream>
#include <stdexcept>

using namespace ark;using V=arkjson::Value;
namespace {
void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
V parse(const std::string& text){V result;check(arkjson::parse(text,result),"test JSON parses");return result;}
V accepted(const V& result){if(result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());return *result.find("structuredContent");}
V call(AgentSession& session,const char* tool,const std::string& args){return accepted(session.call(tool,parse(args)));}
const V& dfm(const V& report){return *report.find("components")->arr.at(0).find("dfm");}
const V& rule(const V& report,const char* id){for(const auto& row:report.find("rules")->arr)if(row.find("id")->str==id)return row;throw std::runtime_error(std::string("missing rule ")+id);}
void status(const V& report,const char* id,const char* expected){const auto& row=rule(report,id);check(row.find("status")->str==expected,(std::string(id)+" expected "+expected+": "+row.dump()).c_str());}
void intent(AgentSession& session,const char* name,const std::string& plan){V command=V::makeObj(),commands=V::makeArr(),args=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeStr(name));command.set("manufacturing",parse(plan));commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)session.revision));accepted(session.call("doc_apply",args));}
void core(){
    Feature sheet;sheet.type=FeatureType::SheetMetal;sheet.name="Sheet";std::string error;
    check(applyManufacturingIntent(sheet,parse(R"({"route":"sheet_metal","process":{"cutting_method":"laser","laser_max_thickness_mm":3,"minimum_bend_radius_mm":2,"process_reference":"machine/material capability revision 1"}})"),error),"sheet plan accepts explicit process capability");
    Document document;document.addFeature(sheet);const auto encoded=documentToJson(document);Document restored;check(parse(encoded).find("version")->num==23&&documentFromJson(encoded,restored,&error)&&documentToJson(restored)==encoded,"sheet DFM process settings persist without document version bump");
    auto result=inspectDfm(document,document.features[0]);status(result,"DFM.LASER.01","unknown");status(result,"DFM.BEND.01","unknown");check(result.find("ruleset_version")->num==1&&!result.find("manufacturing_qualified")->b,"versioned rules never infer geometry or qualification");
    Feature original=sheet;for(const char* invalid:{R"({"route":"sheet_metal","process":{"minimum_bend_radius_mm":-1}})",R"({"route":"sheet_metal","process":{"cutting_method":"guessed"}})",R"({"route":"sheet_metal","process":{"minimum_weld_throat_mm":2}})"})check(!applyManufacturingIntent(sheet,parse(invalid),error)&&sheet.manufacturingProcess.dump()==original.manufacturingProcess.dump(),"invalid and cross-route settings reject atomically");
    Feature frame;frame.type=FeatureType::ConstructionFrame;check(applyManufacturingIntent(frame,parse(R"({"route":"frame","process":{"minimum_weld_throat_mm":2,"welding_procedure":"qualified procedure revision 4","process_reference":"joint design revision 2"}})"),error),"frame plan accepts explicit throat and procedure reference");document.addFeature(frame);result=inspectDfm(document,document.features[1]);status(result,"DFM.WELD.01","unknown");
    Feature legacy;legacy.type=FeatureType::Box;legacy.manufacturingRoute="additive";legacy.manufacturingProcess=parse(R"({"method":"fdm","layer_height_mm":0.2,"nozzle_diameter_mm":0.4,"build_direction":[0,0,1],"build_volume_mm":[20,10,5]})");document.addFeature(legacy);result=inspectDfm(document,document.features[2]);status(result,"DFM.FDM.01","compatible");status(result,"DFM.FDM.02","unknown");check(!rule(result,"DFM.FDM.02").find("evidence")->find("part_extents_mm"),"display geometry cannot supply an exact envelope");
    legacy.manufacturingProcess=parse(R"({"method":"fdm","nozzle_diameter_mm":0.4,"gate_plan":"invalid"})");result=inspectDfm(document,legacy);status(result,"DFM.INPUT.01","unknown");check(result.find("rules")->arr.size()==1,"invalid plan does not evaluate numeric rules");
    AgentSession session;call(session,"doc_apply",R"({"commands":[{"op":"feature.add","type":"box","name":"Envelope","p":{"dx":20,"dy":10,"dz":5},"manufacturing":{"route":"purchased"}}]})");const auto before=documentToJson(session.document);const auto generation=session.document.evaluationGeneration,revision=session.revision;
    auto inspection=call(session,"manufacturing_inspect",R"({"id":"Envelope"})");check(dfm(inspection).find("milestone")->str=="C.03"&&documentToJson(session.document)==before&&session.document.evaluationGeneration==generation&&session.revision==revision,"existing agent tool exposes DFM without mutation");
    auto batch=call(session,"query_batch",R"({"queries":[{"tool":"manufacturing_inspect","args":{"id":"Envelope"}}]})");check(batch.find("results")->arr.size()==1&&!batch.find("results")->arr[0].find("isError")->b,"DFM is available in existing batch inspection");
}
#ifdef ARK_DFM_NATIVE_TESTS
void exactRules(){
    AgentSession session;call(session,"doc_apply",R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Part","native":{"op":"box"},"p":{"width":20,"depth":10,"height":5}}]})");
    intent(session,"Part",R"({"route":"additive","process":{"method":"fdm","layer_height_mm":0.2,"nozzle_diameter_mm":0.4,"build_direction":[0,0,1],"build_volume_mm":[20,10,5]}})");
    const auto saved=documentToJson(session.document),workerBefore=nativeKernelInfo().dump();const auto generation=session.document.evaluationGeneration,revision=session.revision;
    auto inspected=call(session,"manufacturing_inspect",R"({"id":"Part"})");auto result=dfm(inspected);status(result,"DFM.FDM.01","compatible");status(result,"DFM.FDM.02","compatible");status(result,"DFM.FDM.03","unknown");const auto& envelope=*rule(result,"DFM.FDM.02").find("evidence");check(std::abs(envelope.find("part_extents_mm")->arr[0].num-20)<1e-8,"envelope comes from exact evaluated B-rep");
    const auto workerAfter=nativeKernelInfo();check(workerAfter.find("total_worker_requests")->num==parse(workerBefore).find("total_worker_requests")->num+1&&documentToJson(session.document)==saved&&session.document.evaluationGeneration==generation&&session.revision==revision,"DFM inspection makes no native requests or document changes");
    intent(session,"Part",R"({"route":"additive","process":{"method":"fdm","layer_height_mm":0.4,"nozzle_diameter_mm":0.4,"build_direction":[0,0,-2],"build_volume_mm":[19,10,5]}})");inspected=call(session,"manufacturing_inspect",R"({"id":"Part"})");result=dfm(inspected);status(result,"DFM.FDM.01","incompatible");status(result,"DFM.FDM.02","incompatible");check(!inspected.find("components")->arr[0].find("blockers")->arr.empty()&&!result.find("recommendations")->arr[0].find("automatic_edit")->b,"violations yield typed recommendations and blockers, never edits");
    intent(session,"Part",R"({"route":"additive","process":{"method":"fdm","build_direction":[1,0,1],"build_volume_mm":[100,100,100]}})");result=dfm(call(session,"manufacturing_inspect",R"({"id":"Part"})"));status(result,"DFM.FDM.02","unknown");check(!rule(result,"DFM.FDM.02").find("evidence")->find("part_extents_mm"),"arbitrary build direction never reuses an unoriented AABB as measured fit");
    intent(session,"Part",R"({"route":"machined","process":{"stock_size_mm":[19,10,5],"stock_origin_mm":[0,0,0],"cutter_diameter_mm":4,"minimum_internal_radius_mm":1}})");result=dfm(call(session,"manufacturing_inspect",R"({"id":"Part"})"));status(result,"DFM.CNC.01","incompatible");status(result,"DFM.CNC.02","incompatible");
    intent(session,"Part",R"({"route":"machined","process":{"stock_size_mm":[20,10,5],"stock_origin_mm":[0,0,0],"cutter_diameter_mm":4,"minimum_internal_radius_mm":2}})");result=dfm(call(session,"manufacturing_inspect",R"({"id":"Part"})"));status(result,"DFM.CNC.01","compatible");status(result,"DFM.CNC.02","compatible");status(result,"DFM.CNC.03","unknown");check(rule(result,"DFM.CNC.02").find("evidence_authority")->str=="caller_declared","declared corner fit never claims measured B-rep corner evidence");
    Document restored;std::string error;check(documentFromJson(documentToJson(session.document),restored,&error),"native process settings roundtrip");restored.recompute();result=inspectDfm(restored,restored.features[0]);status(result,"DFM.CNC.01","compatible");
    const auto state=documentToJson(session.document);const auto rev=session.revision;auto invalid=session.call("doc_apply",parse(R"({"expected_revision":6,"commands":[{"op":"feature.edit","id":"Part","manufacturing":{"route":"machined","process":{"minimum_weld_throat_mm":3}}}]})"));check(invalid.find("isError")->b&&documentToJson(session.document)==state&&session.revision==rev,"cross-process rejection preserves existing exact design");
}
void sheetRules(){
    AgentSession session;call(session,"doc_apply",R"({"commands":[{"op":"sheet.create","name":"Bracket","p":{"width":30,"thickness":2,"k_factor":0.5},"base":{"id":"base","length":40}},{"op":"sheet.flange.add","id":"Bracket","flange":{"id":"flange","parent":"base","edge":"u_max","length":20,"inner_radius":1,"angle":90}},{"op":"feature.edit","id":"Bracket","manufacturing":{"route":"sheet_metal","process":{"cutting_method":"laser","laser_max_thickness_mm":1,"minimum_bend_radius_mm":2,"process_reference":"machine-material revision 1"}}}]})");
    auto result=dfm(call(session,"manufacturing_inspect",R"({"id":"Bracket"})"));status(result,"DFM.LASER.01","incompatible");status(result,"DFM.LASER.02","compatible");status(result,"DFM.BEND.01","incompatible");check(rule(result,"DFM.BEND.01").find("evidence")->find("bends")->arr.size()==1,"every evaluated bend participates in radius screening");
    call(session,"doc_apply",R"({"expected_revision":1,"commands":[{"op":"sheet.flange.edit","id":"Bracket","entity":"flange","values":{"inner_radius":3}},{"op":"feature.add","type":"native_solid","name":"Installed","inputs":["Bracket"],"native":{"op":"transform"},"p":{"tx":100,"ry":30},"manufacturing":{"route":"sheet_metal","process":{"cutting_method":"laser","laser_max_thickness_mm":2,"minimum_bend_radius_mm":2}}}]})");result=dfm(call(session,"manufacturing_inspect",R"({"id":"Installed"})"));status(result,"DFM.LASER.01","compatible");status(result,"DFM.BEND.01","compatible");check(rule(result,"DFM.BEND.01").find("evidence")->find("geometry_feature_id")->num==1&&rule(result,"DFM.BEND.01").find("evidence")->find("bends")->arr[0].find("inner_radius_mm")->num==3,"rigid placement retains originating sheet evidence after actual geometry edit");
    call(session,"doc_apply",R"({"expected_revision":2,"commands":[{"op":"sheet.stamp.add","id":"Bracket","stamp":{"id":"Emboss","panel":"base","kind":"circular_emboss","x":20,"y":15,"radius":8,"top_radius":3,"depth":3}}]})");result=dfm(call(session,"manufacturing_inspect",R"({"id":"Installed"})"));status(result,"DFM.LASER.02","incompatible");check(!result.find("manufacturing_qualified")->b,"nominal stamped blank never becomes a qualified laser blank");
}
void weldRules(){
    AgentSession session;std::ifstream stream(std::filesystem::path(ARK_SOURCE_DIR)/"examples"/"frame-trim-weld.arkops",std::ios::binary);check((bool)stream,"weld fixture exists");const auto envelope=parse(std::string(std::istreambuf_iterator<char>(stream),{}));accepted(session.call(envelope.find("tool")->str,*envelope.find("arguments")));const auto name=session.document.features[0].name;
    intent(session,name.c_str(),R"({"route":"frame","process":{"minimum_weld_throat_mm":3,"welding_procedure":"procedure revision 1","process_reference":"joint design revision 1"}})");V args=V::makeObj();args.set("id",V::makeNum(1));auto result=dfm(accepted(session.call("manufacturing_inspect",args)));status(result,"DFM.WELD.01","incompatible");const auto* throats=rule(result,"DFM.WELD.01").find("evidence")->find("welds");check(throats->arr.size()==1&&std::abs(throats->arr[0].find("theoretical_throat_mm")->num-3/std::sqrt(2.))<1e-9,"theoretical throat derives from actual evaluated exact bead");
    intent(session,name.c_str(),R"({"route":"frame","process":{"minimum_weld_throat_mm":2,"welding_procedure":"procedure revision 1"}})");result=dfm(accepted(session.call("manufacturing_inspect",args)));status(result,"DFM.WELD.01","compatible");status(result,"DFM.WELD.02","unknown");check(!result.find("manufacturing_qualified")->b,"passing throat comparison never qualifies weld fusion/strength");
    call(session,"doc_apply",R"({"commands":[{"op":"frame.weld.remove","id":1,"entity":"Seam1"}]})");result=dfm(accepted(session.call("manufacturing_inspect",args)));status(result,"DFM.WELD.01","unknown");
}
#endif
}
int main(){try{core();
#ifdef ARK_DFM_NATIVE_TESTS
exactRules();sheetRules();weldRules();
#endif
std::cout<<"versioned DFM evidence and agent contracts passed\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
