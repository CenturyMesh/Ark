#include "ark/agent.hpp"
#include "ark/modeling_031b.hpp"
#include "ark/electronics.hpp"
#include "ark/serialize.hpp"
#include "ark/native_features.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
V json(const char* source){V result;if(!arkjson::parse(source,result))throw std::runtime_error("Bad assembly 031B test JSON");return result;}
V accepted(const V& response){if(const auto* error=response.find("isError"))if(error->b)throw std::runtime_error(response.find("structuredContent")->dump());const auto* data=response.find("structuredContent");if(!data)throw std::runtime_error("Missing structured result");return *data;}
template<class F>void rejects(F action,const char* expected){try{action();}catch(const std::exception& e){if(std::string(e.what()).find(expected)!=std::string::npos)return;throw std::runtime_error(std::string("Wrong rejection: ")+e.what());}throw std::runtime_error(std::string("Expected rejection: ")+expected);}
void apply(ark::AgentSession& session,const char* commands){V args=json(commands);args.set("expected_revision",V::makeNum((double)session.revision));accepted(session.call("doc_apply",args));}
void fixture(ark::AgentSession& session){apply(session,R"({"commands":[
 {"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":2,"depth":2,"height":2}},
 {"op":"assembly.add","name":"Fixture","feature":"Block","fixed":true},
 {"op":"assembly.add","name":"Slider","feature":"Block"},
 {"op":"mate.add","type":"slider","a":"Fixture","b":"Slider","reference_a":"Z","reference_b":"Z","limits":{"min":0,"max":6}}
]})");}
ark::Feature cable(){ark::Feature f;f.id=2;f.name="Bound cable";f.type=ark::FeatureType::CableRoute;f.inputs={1};f.nativeSpec=json(R"({"start":{"node":1,"offset":[10,0,10]},"end":{"node":2,"offset":[20,0,10]},"radius":0.1,"bend_radius":0.3,"obstacles":[{"node":1}]})");return f;}
V sweepArgs(){return json(R"({"driver":"Slider","mode":"translate","from":0,"to":6,"samples":4,"pairs":[{"a":"Fixture","b":"Slider"}]})");}

void bindingTests(){
    ark::AgentSession session;fixture(session);auto f=cable();const auto before=ark::documentToJson(session.document,true,false);
    auto resolved=ark::resolveCableBindings031B(f,{},session.document);check(resolved.find("start")->find("point")->arr[0].num==10&&resolved.find("end")->find("point")->arr[0].num==20,"Bound endpoint offsets resolve in occurrence world frame");check(resolved.find("occurrence_binding_report")->find("obstacles")->arr[0].find("conservative_AABB")->b,"Bound obstacle explicitly reports AABB conservativeness");
    check(f.nativeSpec.find("start")->find("node"),"Binding resolution preserves authored recipe");auto normalized=f;normalized.nativeSpec=resolved;check(ark::electronicsRequest(normalized,{}).find("report")->find("route_length_mm")->num==10,"Resolved static cable passes existing real routing checks");check(ark::documentToJson(session.document,true,false)==before,"Resolution does not alter authored source");
}

void evaluationFrontierTests(){
    ark::AgentSession session;fixture(session);auto& document=session.document;auto f=cable();
    f.nativeSpec.set("start",json(R"({"node":1,"reference":"z_max","offset":[10,0,10]})"));
    auto preceding=document.evalBodies;document.evalBodies.clear();document.referenceFrameCache.clear();
    std::vector<ark::Body> unrelated;document.evaluatingBodies=&unrelated;
    const auto authored=ark::documentToJson(document,true,false);
    const auto evaluated=ark::evaluateNativeFeature(f,document.buildEnv(),document,preceding);
    check(evaluated.nativeData.find("brep_base64"),"Bound cable uses explicit preceding bodies even when document bodies are absent");
    check(document.evaluatingBodies==&unrelated,"Cable evaluation restores caller evaluation frontier");
    check(!document.referenceFrameCache.empty(),"Cable evaluation retains exact reference memoization across cables");
    check(ark::documentToJson(document,true,false)==authored,"Cable evaluation preserves authored document");
    f.inputs.clear();rejects([&]{ark::evaluateNativeFeature(f,document.buildEnv(),document,preceding);},"retained explicitly");
    check(document.evaluatingBodies==&unrelated,"Rejected cable evaluation restores caller evaluation frontier");
    check(ark::documentToJson(document,true,false)==authored,"Rejected cable evaluation preserves authored document");
    document.evaluatingBodies=nullptr;
}

void poseBindingTests(){
    ark::AgentSession session;fixture(session);auto f=cable();ark::AssemblyManipulationProgram program;std::string error;check(ark::buildAssemblyManipulation(session.document,program,error),error.c_str());ark::AssemblyManipulationPreview preview;check(ark::previewAssemblyManipulation(program,2,0,"translate",6,preview,error),error.c_str());
    auto resolved=ark::resolveCableBindings031B(f,{},session.document,&preview.placements);check(std::abs(resolved.find("end")->find("point")->arr[2].num-16)<1e-8,"Bound endpoint follows sampled slider pose");
    ark::Feature normalized=f;normalized.nativeSpec=resolved;auto request=ark::electronicsRequest(normalized,{});check(std::abs(request.find("report")->find("route_length_mm")->num-std::sqrt(136.))<1e-7,"Actual cable length recomputes at moved endpoint");
    auto ref=f;V endpoint=json(R"({"node":2,"reference":"z_max","offset":[0,0,2]})");ref.nativeSpec.set("end",endpoint);resolved=ark::resolveCableBindings031B(ref,{},session.document,&preview.placements);check(resolved.find("occurrence_binding_report")->find("endpoints")->arr[1].find("reference")->str=="z_max","Exact semantic connector reference resolves");
    ref=f;ref.nativeSpec.set("waypoints",json(R"([{"node":1,"offset":[15,2,10]}])"));resolved=ark::resolveCableBindings031B(ref,{},session.document,&preview.placements);check(resolved.find("waypoints")->arr[0].find("point")->arr[1].num==2,"Authored waypoint binds through the same occurrence frame contract");
    auto patterned=session.document.authoringCopy();patterned.evalBodies=session.document.evalBodies;auto& pattern=patterned.assembly[1].pattern;pattern.active=true;pattern.type="linear";pattern.count="2";pattern.dx="5";ref=f;ref.nativeSpec.set("end",json(R"({"node":2,"copy":1,"offset":[20,0,10]})"));resolved=ark::resolveCableBindings031B(ref,{},patterned);check(resolved.find("end")->find("point")->arr[0].num==25,"Stable pattern copy binds actual expanded placement");
    auto bad=f;bad.inputs.clear();rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"retained explicitly");bad=f;bad.id=1;rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"self-dependency");
    bad=f;bad.nativeSpec.set("start",json(R"({"node":"Fixture"})"));rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"stable numeric node ID");bad=f;bad.nativeSpec.set("obstacles",json(R"([{"node":"Fixture"}])"));rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"stable numeric node ID");
    bad=f;bad.nativeSpec.set("start",json(R"({"node":1,"copy":1})"));rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"not placed");bad=f;bad.nativeSpec.set("start",json(R"({"node":1,"copy":0.5})"));rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"integer");bad=f;bad.nativeSpec.set("start",json(R"({"node":1,"reference":"nonexistent"})"));rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"reference");
    bad=f;bad.nativeSpec.set("obstacles",json(R"([{"node":1},{"node":1}])"));rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"Duplicate");bad=f;bad.nativeSpec.set("start",json(R"({"node":1,"offset":[10001,0,0]})"));rejects([&]{ark::resolveCableBindings031B(bad,{},session.document);},"10 m");
    ark::Feature legacy=f;legacy.nativeSpec=json(R"({"start":[0,0,0],"end":[10,0,0],"radius":0.1})");ark::Document empty;check(ark::resolveCableBindings031B(legacy,{},empty).dump()==legacy.nativeSpec.dump(),"Legacy numeric cable does not require an assembly solve");
}

void sweepTests(){
    ark::AgentSession session;fixture(session);const auto baseline=ark::documentToJson(session.document,true,false);const auto revision=session.revision;auto args=sweepArgs();auto result=ark::inspectAssemblySweep031B(session.document,args);
    check(result.find("sample_count")->num==4&&result.find("complete_requested_samples")->b,"All requested sampled poses evaluated");check(result.find("interfering_pair_samples")->num>=1&&!result.find("between_sample_clearance_checked")->b,"Actual interference measured and between-sample scope explicit");const auto& rows=result.find("samples")->arr;check(rows.front().find("pairs")->arr[0].find("measurement")->find("common_volume_mm3")->num>7.9,"Exact pair at zero delta has measured common volume");check(rows.back().find("pairs")->arr[0].find("measurement")->find("min_distance_mm")->num>3.9,"Exact final pose has measured separation");
    check(session.revision==revision&&ark::documentToJson(session.document,true,false)==baseline,"Read-only sweep preserves authoring and revision");
    args.set("collision_mode",V::makeStr("mesh"));result=ark::inspectAssemblySweep031B(session.document,args);check(!result.find("samples")->arr[0].find("pairs")->arr[0].find("measurement")->find("exact_geometry")->b,"Mesh sweep retains triangle authority");
    auto invalid=sweepArgs();invalid.set("to",V::makeNum(7));rejects([&]{ark::inspectAssemblySweep031B(session.document,invalid);},"sample");check(ark::documentToJson(session.document,true,false)==baseline,"Failed later pose publishes no state");invalid=sweepArgs();invalid.set("samples",V::makeNum(1));rejects([&]{ark::inspectAssemblySweep031B(session.document,invalid);},"2..65");invalid=sweepArgs();invalid.set("pairs",json(R"([{"a":1,"b":2},{"a":2,"b":1}])"));rejects([&]{ark::inspectAssemblySweep031B(session.document,invalid);},"Duplicate");invalid=sweepArgs();invalid.set("pairs",json(R"([{"a":1,"b":1}])"));rejects([&]{ark::inspectAssemblySweep031B(session.document,invalid);},"distinct");invalid=sweepArgs();invalid.set("pairs",json(R"([{"a":1,"b":2,"copy_b":1}])"));rejects([&]{ark::inspectAssemblySweep031B(session.document,invalid);},"not placed");invalid=sweepArgs();invalid.set("mode",V::makeStr("free"));rejects([&]{ark::inspectAssemblySweep031B(session.document,invalid);},"rotate or translate");
    V query=V::makeObj();query.set("sweep",sweepArgs());result=accepted(session.call("assembly_info",query));check(result.find("sweep")&&result.find("sweep")->find("sample_count")->num==4,"assembly_info exposes sampled inspection");
}

void cableSweepAndPersistence(){
    ark::AgentSession session;fixture(session);apply(session,R"({"commands":[{"op":"feature.add","type":"cable_route","name":"Bound cable","inputs":["Block"],"native":{"start":{"node":1,"offset":[10,0,10]},"end":{"node":2,"offset":[20,0,10]},"radius":0.1,"bend_radius":0.3,"obstacles":[{"node":1}]}}]})");
    auto args=sweepArgs();args.set("cables",json("[2]"));auto result=ark::inspectAssemblySweep031B(session.document,args);const auto& samples=result.find("samples")->arr;const double first=samples.front().find("cables")->arr[0].find("report")->find("route_length_mm")->num,last=samples.back().find("cables")->arr[0].find("report")->find("route_length_mm")->num;check(std::abs(first-10)<1e-7&&std::abs(last-std::sqrt(136.))<1e-7,"Sweep re-evaluates actual routing at every sampled endpoint pose");check(samples.back().find("cables")->arr[0].find("report")->find("exact_obstacle_checked")->b,"Every sampled route checks its actual native swept cable against obstacle boxes");
    V stored=json(ark::documentToJson(session.document,true,false).c_str());check(stored.find("version")->num==43,"Cable occurrence recipe conditionally requires source43");ark::Document restored;std::string error;check(ark::documentFromJson(stored.dump(),restored,&error),error.c_str());restored.recompute();result=ark::inspectAssemblySweep031B(restored,args);check(result.find("sample_count")->num==4,"Occurrence bindings and sweep survive save/load");stored.set("version",V::makeNum(42));check(!ark::documentFromJson(stored.dump(),restored,&error),"Old source format rejects bound cable semantics");
    const auto baseline=ark::documentToJson(session.document,true,false);const auto revision=session.revision;V invalid=args;invalid.set("cables",json("[2,2]"));rejects([&]{ark::inspectAssemblySweep031B(session.document,invalid);},"unique");
    // The moving endpoint enters the source AABB at a later pose. No partial
    // clear prefix is published after routing fails at that later sample.
    auto* f=session.document.find(2);f->nativeSpec.set("start",json("[10,0,3]"));f->nativeSpec.set("end",json(R"({"node":2,"offset":[1,1,-2]})"));rejects([&]{ark::inspectAssemblySweep031B(session.document,args);},"obstacle");check(session.revision==revision,"Failed cable sweep leaves revision untouched");f->nativeSpec=cable().nativeSpec;check(ark::documentToJson(session.document,true,false)==baseline,"Test restores only its local authored fixture");
    apply(session,R"({"commands":[{"op":"assembly.drive","driver":"Slider","mode":"translate","delta":6}]})");
    auto inspected=accepted(session.call("electronics_inspect",json("{\"id\":2}")));check(std::abs(inspected.find("route_length_mm")->num-std::sqrt(136.))<1e-6,"Moving occurrence invalidates bound cable cache despite unchanged producer feature definitions");check(inspected.find("occurrence_binding_report")||inspected.find("geometry")->find("occurrence_binding_report"),"Evaluated route inspection retains occurrence binding evidence");
    accepted(session.call("doc_undo",V::makeObj()));inspected=accepted(session.call("electronics_inspect",json("{\"id\":2}")));check(std::abs(inspected.find("route_length_mm")->num-10)<1e-6,"Undoing occurrence motion restores associative cable route");
}
void roundedCableCannotTrustPolyline(){
    ark::AgentSession session;fixture(session);ark::Feature rounded;rounded.id=2;rounded.type=ark::FeatureType::CableRoute;rounded.name="Rounded bend candidate";rounded.nativeSpec=json(R"({"start":[0,0,0],"end":[10,10,0],"waypoints":[[10,0,0]],"radius":0.1,"bend_radius":3,"obstacles":[{"name":"Inside bend","min":[9,0.7,-0.2],"max":[9.3,1,0.2]}]})");
    check(ark::electronicsRequest(rounded,{}).find("report")->find("obstacle_prefilter_checked")->b,"Rounded-bend fixture passes existing straight-segment prefilter");
    // A read-only candidate added directly to this isolated synthetic fixture
    // must still run native construction; no evaluated cable result is trusted.
    session.document.features.push_back(rounded);auto args=sweepArgs();args.set("cables",json("[2]"));const auto baseline=ark::documentToJson(session.document,true,false);rejects([&]{ark::inspectAssemblySweep031B(session.document,args);},"Exact cable collides");check(ark::documentToJson(session.document,true,false)==baseline,"Rounded cable failure leaves candidate authoring unchanged");
}
}
int main(){
#ifdef ARK_CONTRACTS_NATIVE_TESTS
    try{bindingTests();evaluationFrontierTests();poseBindingTests();sweepTests();cableSweepAndPersistence();roundedCableCannotTrustPolyline();std::cout<<"031B assembly sweep and routing passed\n";return 0;}catch(const std::exception& e){std::cerr<<"031B assembly sweep/routing failed: "<<e.what()<<'\n';return 1;}
#else
    try{ark::Document empty;auto legacy=cable();legacy.nativeSpec=json(R"({"start":[0,0,0],"end":[10,0,0],"radius":0.1})");check(ark::resolveCableBindings031B(legacy,{},empty).dump()==legacy.nativeSpec.dump(),"Legacy route requires no assembly or native geometry");legacy.nativeSpec.set("waypoints",V::makeNum(1));rejects([&]{ark::resolveCableBindings031B(legacy,{},empty);},"must be an array");std::cout<<"031B portable legacy binding checks passed; native sweep checks skipped (pinned worker required)\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#endif
}
