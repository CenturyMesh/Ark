#include "ark/agent.hpp"
#include "ark/manufacturing.hpp"
#ifdef ARK_MANUFACTURING_NATIVE_TESTS
#include "ark/native_backend.hpp"
#endif
#include <iostream>
#include <stdexcept>

using namespace ark;using V=arkjson::Value;
namespace {
void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
V parse(const char* text){V out;check(arkjson::parse(text,out),"test JSON must parse");return out;}
V accepted(const V& result){check(result.find("isError")!=nullptr,"tool envelope");if(result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());return *result.find("structuredContent");}
V call(AgentSession& session,const char* tool,const char* args){return accepted(session.call(tool,parse(args)));}
const V& component(const V& report){return report.find("components")->arr.at(0);}
const V* requirement(const V& row,const char* name){for(const auto& item:row.find("requirements")->arr)if(item.find("id")->str==name)return &item;return nullptr;}
const char* fdm=R"({"route":"additive","process":{"method":"fdm","material_grade":"supplier PLA grade","tolerance_mm":0.2,"load_case":"enclosure with measured mounting load","build_direction":[0,0,1],"nozzle_diameter_mm":0.4,"layer_height_mm":0.2,"line_width_mm":0.45,"perimeters":3,"top_layers":5,"bottom_layers":5,"infill_percent":15,"support_strategy":"none","filament_diameter_mm":1.75,"slicer_profile":"printer/material profile version 1","build_volume_mm":[250,210,220]}})";
const char* injection=R"({"route":"injection_molding","process":{"material_grade":"supplier ABS grade","tolerance_mm":0.1,"load_case":"same functional enclosure load","nominal_wall_mm":2,"min_draft_deg":1,"pull_direction":[0,0,1],"parting_line":"declared tooling split at base","undercut_strategy":"none","gate_plan":"supplier review of base gate","ejection_plan":"supplier review of base pads","shrinkage_percent":0.5}})";
Feature part(const char* name){Feature f;f.name=name;f.type=FeatureType::NativeSolid;f.nativeSpec=parse(R"({"op":"box"})");f.p={{"width","20"},{"depth","10"},{"height","5"}};return f;}
void coreContracts(){
    Feature print=part("Plastic print"),mold=part("Plastic mold");std::string error;
    check(applyManufacturingIntent(print,parse(fdm),error),"explicit additive plan accepted");check(applyManufacturingIntent(mold,parse(injection),error),"explicit injection plan accepted");
    Document document;const auto printId=document.addFeature(print);const auto moldId=document.addFeature(mold);
    check(validateManufacturingRoutes(document,error),"route and exact authoring validation");
    const auto saved=documentToJson(document);V package;check(arkjson::parse(saved,package)&&package.find("version")->num==23,"process intent saves document version 23");
    Document restored;check(documentFromJson(saved,restored,&error),"process persistence load");check(documentToJson(restored)==saved&&restored.features[1].manufacturingRoute=="injection_molding","route and entire plan survive roundtrip");
    V selector=V::makeObj();selector.set("id",V::makeNum((double)printId));const auto inspected=inspectManufacturing(document,selector,7);const auto& printRow=component(inspected);
    check(inspected.find("revision")->num==7&&!inspected.find("manufacturing_qualified")->b&&!inspected.find("ready_for_process_review")->b,"missing exact geometry never passes process inspection");
    check(requirement(printRow,"exact_single_solid")->find("status")->str=="blocked","exact authoring type is insufficient without evaluated B-rep");
    check(requirement(printRow,"slicer_consumption_checked")&&requirement(printRow,"layer_strength_checked")&&!requirement(printRow,"gate_plan"),"additive policy is distinct from injection");
    check(printRow.find("filament_budget")->find("actual_filament_length_mm")->t==V::Null&&!printRow.find("filament_budget")->find("solid_equivalent_length_mm"),"no filament amount manufactured from unevaluated authoring");
    selector.set("id",V::makeNum((double)moldId));auto moldReport=inspectManufacturing(document,selector);const auto& moldRow=component(moldReport);
    check(requirement(moldRow,"gate_plan")&&requirement(moldRow,"wall_distribution_checked")&&!requirement(moldRow,"infill_percent")&&!moldRow.find("filament_budget"),"injection uses draft, tooling and wall policy instead of slicer settings");
    check(!moldRow.find("unknowns")->arr.empty()&&!moldRow.find("ready_for_process_review")->b,"unverified mold checks cannot pass");
    Feature untouched=print;check(!applyManufacturingIntent(print,parse(R"({"route":"injection_molding","process":{"infill_percent":15}})"),error)&&print.manufacturingProcess.dump()==untouched.manufacturingProcess.dump()&&print.manufacturingRoute=="additive","cross-route input fails without changing existing intent");
    check(!applyManufacturingIntent(print,parse(R"({"route":"additive","process":{"method":"sla"}})"),error),"unsupported additive methods reject explicitly");
    check(!applyManufacturingIntent(print,parse(R"({"route":"additive","process":{"build_direction":[0,0,0]}})"),error),"degenerate direction rejects");
    check(!applyManufacturingIntent(print,parse(R"({"route":"additive","process":{"perimeters":1.5}})"),error),"fractional shell count rejects");
    check(!applyManufacturingIntent(print,parse(R"({"route":"additive","process":{"nozzle_diameter_mm":"guessed"}})"),error),"process dimensions cannot be invented or unbound text");
    auto malicious=package;malicious.set("version",V::makeNum(21));check(!documentFromJson(malicious.dump(),restored,&error),"old versions cannot smuggle new route/process");
    malicious=package;V features=*malicious.find("features"),intent=*features.arr[0].find("manufacturing"),process=*intent.find("process");process.set("gate_plan",V::makeStr("cross-route"));intent.set("process",process);features.arr[0].set("manufacturing",intent);malicious.set("features",features);check(!documentFromJson(malicious.dump(),restored,&error),"load rejects cross-route process fields");
    malicious=package;features=*malicious.find("features");features.arr[0].set("type",V::makeStr("box"));malicious.set("features",features);check(!documentFromJson(malicious.dump(),restored,&error),"load rejects display solid claiming exact additive route");
    Feature legacy;legacy.type=FeatureType::Box;legacy.manufacturingRoute="injection_molding";Document invalid;invalid.addFeature(legacy);check(!validateManufacturingRoutes(invalid,error),"injection cannot route a legacy display body");
    legacy.manufacturingRoute="additive";invalid.features[0]=legacy;check(!validateManufacturingRoutes(invalid,error),"additive cannot route a legacy display body");
    check(applyManufacturingIntent(print,V{},error)&&print.manufacturingRoute.empty()&&print.manufacturingProcess.t==V::Null,"clearing intent clears process too");
    check(applyManufacturingIntent(mold,parse(R"({"route":"additive"})"),error)&&mold.manufacturingProcess.t==V::Null,"changing route never carries previous incompatible process plan");
    AgentSession session;call(session,"doc_apply",R"({"commands":[{"op":"feature.add","type":"box","name":"Envelope","p":{"dx":20,"dy":10,"dz":5},"manufacturing":{"route":"purchased"}}]})");
    const auto before=documentToJson(session.document);const auto revision=session.revision;const auto generation=session.document.evaluationGeneration;const auto bodies=session.document.evalBodies.size();
    auto result=call(session,"manufacturing_inspect",R"({"id":"Envelope"})");check(!result.find("geometry_mutated")->b&&documentToJson(session.document)==before&&session.revision==revision&&session.document.evaluationGeneration==generation&&session.document.evalBodies.size()==bodies,"inspection preserves document, revision and evaluated geometry");
    auto rejected=session.call("doc_apply",parse(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"uncommitted","expr":42},{"op":"feature.edit","id":"Envelope","manufacturing":{"route":"additive"}}]})"));
    check(rejected.find("isError")->b&&documentToJson(session.document)==before&&session.revision==revision,"invalid route rolls back the entire edit batch and revision");
    auto batch=call(session,"query_batch",R"({"queries":[{"tool":"manufacturing_inspect","args":{"id":"Envelope"}}]})");check(batch.find("results")->arr.size()==1&&!batch.find("results")->arr[0].find("isError")->b,"manufacturing inspection available to batch/postcondition queries");
    bool discovered=false;for(const auto& tool:AgentSession::tools().arr)if(tool.find("name")->str=="manufacturing_inspect")discovered=true;check(discovered,"inspection appears in tool discovery");
}
#ifdef ARK_MANUFACTURING_NATIVE_TESTS
void exactContracts(){
    AgentSession session;call(session,"doc_apply",R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Part","native":{"op":"box"},"p":{"width":20,"depth":10,"height":5}}]})");
    V command=parse(R"({"op":"feature.edit","id":"Part"})");command.set("manufacturing",parse(fdm));V commands=V::makeArr();commands.arr.push_back(command);V args=V::makeObj();args.set("expected_revision",V::makeNum(1));args.set("commands",commands);accepted(session.call("doc_apply",args));
    const auto before=documentToJson(session.document);const auto generation=session.document.evaluationGeneration;const auto workerBefore=nativeKernelInfo();
    const auto report=call(session,"manufacturing_inspect",R"({"id":"Part"})");const auto& row=component(report);check(row.find("geometry")->find("brep_valid")->b&&std::abs(row.find("geometry")->find("volume_mm3")->num-1000)<1e-8,"inspection uses actual exact measured volume");
    check(requirement(row,"exact_single_solid")->find("status")->str=="verified"&&row.find("process_parameters_complete")->b,"checked solid and declared complete process remain distinct");
    check(!row.find("manufacturing_qualified")->b&&!row.find("ready_for_process_review")->b&&!row.find("unknowns")->arr.empty(),"even complete FDM settings leave actual process checks unknown");
    const auto* filament=row.find("filament_budget");check(!filament->find("actual_consumption_checked")->b&&filament->find("actual_filament_length_mm")->t==V::Null,"actual filament consumption requires slicer evidence");
    const double solidEquivalent=1000/(K_PI*1.75*1.75/4);check(std::abs(filament->find("solid_equivalent_length_mm")->num-solidEquivalent)<1e-8,"solid equivalent derived from exact volume and explicit filament area");
    check(std::abs(row.find("configured_shell")->find("nominal_perimeter_band_mm")->num-1.35)<1e-10,"shell settings arithmetic is bounded and explicit");
    const auto workerAfter=nativeKernelInfo();check(documentToJson(session.document)==before&&session.revision==2&&session.document.evaluationGeneration==generation&&workerAfter.find("total_worker_requests")->num==workerBefore.find("total_worker_requests")->num+1,"inspection itself has no worker calls or state mutation");
    Document restored;std::string error;check(documentFromJson(before,restored,&error),"native plan roundtrip load");restored.recompute();auto cold=inspectManufacturing(restored,parse(R"({"id":"Part"})"));check(std::abs(component(cold).find("geometry")->find("volume_mm3")->num-1000)<1e-8&&restored.features[0].manufacturingRoute=="additive","cold native recompute preserves exact geometry and process plan");
    command.set("manufacturing",parse(injection));commands.arr[0]=command;args.set("commands",commands);args.set("expected_revision",V::makeNum(2));accepted(session.call("doc_apply",args));auto molded=call(session,"manufacturing_inspect",R"({"id":"Part"})");check(component(molded).find("process_parameters_complete")->b&&requirement(component(molded),"draft_and_undercuts_checked")->find("status")->str=="unknown","full injection plan does not invent a mold analysis");
    const auto injectionState=documentToJson(session.document);auto invalid=session.call("doc_apply",parse(R"({"expected_revision":3,"commands":[{"op":"feature.edit","id":"Part","manufacturing":{"route":"injection_molding","process":{"nozzle_diameter_mm":0.4}}}]})"));check(invalid.find("isError")->b&&session.revision==3&&documentToJson(session.document)==injectionState,"cross-route failed batch leaves native state and revision intact");
    call(session,"doc_apply",R"({"expected_revision":3,"commands":[{"op":"feature.edit","id":"Part","manufacturing":{"route":"machined","process":{"material_grade":"supplier stock grade","tolerance_mm":0.05,"load_case":"original enclosure load","stock_size_mm":[10,10,5],"stock_origin_mm":[0,0,0],"tool_axis":[0,0,1],"cutter_diameter_mm":4,"minimum_internal_radius_mm":1,"workholding_plan":"vise","setup_plan":"top setup"}}}]})");
    auto machined=call(session,"manufacturing_inspect",R"({"id":"Part"})");check(requirement(component(machined),"stock_enclosure")->find("status")->str=="blocked"&&requirement(component(machined),"declared_cutter_corner_fit")->find("status")->str=="blocked","exact stock enclosure and declared cutter mismatch block machining");
    const auto& evidence=component(machined).find("evidence")->arr;bool exactStock=false;for(const auto& item:evidence)if(item.find("id")->str=="axis_aligned_stock_enclosure")exactStock=item.find("authority")->str=="exact_brep_bounds_and_caller_declared_local_stock"&&!item.find("fits")->b;check(exactStock,"stock check carries exact bounds provenance");
}
#endif
}
int main(){try{coreContracts();
#ifdef ARK_MANUFACTURING_NATIVE_TESTS
exactContracts();
#endif
std::cout<<"manufacturing intent and evidence tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
