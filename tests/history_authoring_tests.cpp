#include "ark/agent.hpp"
#include "ark/history_authoring.hpp"
#include "ark/iteration_history.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
static void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
static V parse(const std::string& text){V value;check(arkjson::parse(text,value),"test fixture parse");return value;}
static V call(AgentSession& session,const std::string& tool,V args){auto response=session.call(tool,args);if(response.find("isError")->b)throw std::runtime_error(response.dump());return *response.find("structuredContent");}
static V apply(AgentSession& session,const std::string& commands){auto args=V::makeObj();args.set("expected_revision",V::makeNum((double)session.revision));args.set("commands",parse(commands));return call(session,"doc_apply",args);}
static void rejected(AgentSession& session,const std::string& commands){const auto before=documentToJson(session.document);const auto revision=session.revision;V args=V::makeObj();args.set("commands",parse(commands));args.set("expected_revision",V::makeNum((double)revision));auto result=session.call("doc_apply",args);check(result.find("isError")->b,"invalid history operation rejects");check(session.revision==revision&&documentToJson(session.document)==before,"rejected history operation is atomic");}
// Expected rows are fixture facts: id, history_active, evaluation_active, output.
static void inventory(AgentSession& session,const char* expected){
    const auto before=documentToJson(session.document);const auto revision=session.revision;
    const auto generation=session.document.evaluationGeneration;const auto bodies=session.document.evalBodies.size();
    const auto evaluated=session.document.recomputeEvaluated,reused=session.document.recomputeReused;
    const auto elapsed=session.document.recomputeMs;
    const auto report=call(session,"doc_info",V::makeObj());const auto history=call(session,"feature_history",V::makeObj());const auto rows=parse(expected);
    const auto* features=report.find("features");check(features&&features->arr.size()==rows.arr.size(),"inventory retains every history row");
    check(history.find("features")->arr.size()==rows.arr.size()&&history.find("tip")->dump()==report.find("history_tip")->dump(),"history inspection retains the same rows and rollback cursor");
    check(report.find("feature_count")->num==rows.arr.size()&&report.find("revision")->num==revision,"inventory reports current count and revision");
    for(size_t i=0;i<rows.arr.size();++i){
        const auto& actual=features->arr[i];const auto& row=rows.arr[i].arr;
        check(actual.find("id")->num==row[0].num,"inventory preserves authored order after reorder");
        check(actual.find("history_active")->b==row[1].b&&actual.find("evaluation_active")->b==row[2].b&&actual.find("output")->b==row[3].b,"inventory distinguishes history, evaluation and terminal output");
        check(actual.obj.size()==6&&actual.find("name")->str==session.document.features[i].name&&actual.find("type")->str==featureTypeName(session.document.features[i].type),"inventory preserves row schema and identity");
        const auto& historyRow=history.find("features")->arr[i];
        check(historyRow.find("id")->num==row[0].num&&historyRow.find("position")->num==i&&historyRow.find("history_active")->b==row[1].b,"history inspection uses the inclusive authored prefix after reorder");
    }
    check(session.revision==revision&&documentToJson(session.document)==before,"inventory does not mutate authoring or revision");
    check(session.document.evaluationGeneration==generation&&session.document.evalBodies.size()==bodies&&session.document.recomputeEvaluated==evaluated&&session.document.recomputeReused==reused&&session.document.recomputeMs==elapsed,"inventory does not recompute geometry or change evaluation counters");
}
static void suppressedInventory(){
    AgentSession session;
    apply(session,R"([{"op":"feature.add","type":"box","name":"Base","p":{"dx":10,"dy":5,"dz":2}},{"op":"feature.add","type":"transform","name":"Prefix consumer","inputs":[1],"p":{"tx":20}},{"op":"feature.add","type":"box","name":"Independent","p":{"dx":2,"dy":3,"dz":4}},{"op":"feature.add","type":"transform","name":"Suffix consumer","inputs":[3],"p":{"tx":30}},{"op":"feature.move","id":3,"delta":-2},{"op":"feature.edit","id":2,"suppressed":true},{"op":"feature.edit","id":4,"suppressed":true},{"op":"feature.rollback","id":2}])");
    inventory(session,R"([[3,true,true,true],[1,true,true,true],[2,true,false,false],[4,false,false,false]])");
    apply(session,R"([{"op":"feature.rollback","id":null}])");
    inventory(session,R"([[3,true,true,true],[1,true,true,true],[2,true,false,false],[4,true,false,false]])");
    apply(session,R"([{"op":"feature.edit","id":2,"suppressed":false}])");
    inventory(session,R"([[3,true,true,true],[1,true,true,false],[2,true,true,true],[4,true,false,false]])");
}
static void referenceInputInventory(){
    // Inventory describes authored evaluation eligibility without evaluating this
    // metadata-only reference extrusion fixture or qualifying its geometry.
    AgentSession session;Feature base;base.type=FeatureType::Box;base.name="Reference source";session.document.addFeature(base);
    Feature extrusion;extrusion.type=FeatureType::NativeSolid;extrusion.name="Reference extrusion";extrusion.inputs={1};extrusion.nativeSpec=parse(R"({"op":"extrude"})");session.document.addFeature(extrusion);
    inventory(session,R"([[1,true,true,true],[2,true,true,true]])");
    check(session.document.evaluationGeneration==0&&session.document.evalBodies.empty(),"querying reference-input inventory leaves unevaluated geometry untouched");
}
int main(){try{
    suppressedInventory();referenceInputInventory();
    AgentSession session;
    apply(session,R"([{"op":"feature.add","type":"box","name":"Base","p":{"dx":10,"dy":20,"dz":30}},{"op":"feature.add","type":"transform","name":"Moved","inputs":[1],"p":{"tx":20}},{"op":"feature.add","type":"box","name":"Independent","p":{"dx":2,"dy":3,"dz":4}}])");
    V legacy;check(arkjson::parse(documentToJson(session.document),legacy),"legacy fixture serializes");check(legacy.find("version")->num<40,"ordinary history retains earlier format");
    apply(session,R"([{"op":"feature.move","id":"Independent","delta":-2}])");
    check(session.document.features[0].id==3&&session.document.features[1].id==1&&session.document.features[2].id==2,"multi-position move preserves intervening order");
    rejected(session,R"([{"op":"feature.move","id":"Moved","delta":-2}])");
    rejected(session,R"([{"op":"feature.move","id":1,"delta":99}])");
    rejected(session,R"([{"op":"feature.move","id":1,"delta":0.5}])");
    apply(session,R"([{"op":"feature.rollback","id":"Base"}])");
    inventory(session,R"([[3,true,true,true],[1,true,true,true],[2,false,false,false]])");
    check(session.document.historyTip==1&&session.document.evalBodies.size()==2,"rollback evaluates only history prefix");
    check(!session.document.find(2)->suppressed&&!featureWithinHistory(session.document,2),"rollback retains downstream suppression state");
    check(!consumedFeatureInputs(session.document).count(1),"inactive downstream consumer cannot hide the current prefix output");
    auto visible=call(session,"doc_info",V::makeObj());bool baseOutput=false;for(const auto& feature:visible.find("features")->arr)if(feature.find("id")->num==1)baseOutput=feature.find("output")->b;check(baseOutput,"document inventory exposes rollback tip as a current output");
    for(const auto& feature:visible.find("features")->arr)if(feature.find("id")->num==2)check(!feature.find("output")->b&&!feature.find("evaluation_active")->b,"retained inactive source cannot be advertised as an evaluated output");
    auto bom=call(session,"bom_inspect",V::makeObj());check(bom.find("items")->arr.size()==2,"standalone BOM counts active prefix terminal definitions");
    auto report=call(session,"feature_history",V::makeObj());check(report.find("tip")->num==1&&!report.find("features")->arr[2].find("history_active")->b,"history query exposes inactive suffix");
    rejected(session,R"([{"op":"feature.add","type":"box","name":"Hidden mutation"}])");
    rejected(session,R"([{"op":"sheet.create","name":"Hidden sheet","p":{"width":10,"thickness":1,"k_factor":0.4},"base":{"id":"base","length":20}}])");
    rejected(session,R"([{"op":"feature.move","id":3,"delta":1}])");
    rejected(session,R"([{"op":"feature.remove","id":1}])");
    apply(session,R"([{"op":"feature.edit","id":1,"p":{"dx":12}}])");
    V saved;check(arkjson::parse(documentToJson(session.document),saved)&&saved.find("version")->num==40,"rollback authoring selects format40");
    Document restored;std::string error;check(documentFromJson(saved.dump(),restored,&error),"rollback source roundtrip");restored.recompute();check(restored.historyTip==1&&restored.evalBodies.size()==2,"reopened rollback retains active prefix");
    saved.set("version",V::makeNum(39));check(!documentFromJson(saved.dump(),restored,&error),"old version rejects rollback semantics");
    auto missing=session.call("doc_apply",parse(R"({"commands":[{"op":"feature.rollback","id":null}]})"));check(missing.find("isError")->b&&session.document.historyTip==1,"rollback requires observed revision");
    apply(session,R"([{"op":"feature.rollback","id":null}])");check(session.document.evalBodies.size()==3&&!session.document.historyTip&&session.document.find(1)->get("dx")=="12","resume reevaluates retained editable source");
    call(session,"doc_undo",V::makeObj());check(session.document.historyTip==1,"persistent undo restores rollback cursor");call(session,"doc_redo",V::makeObj());check(!session.document.historyTip,"redo restores full history");
    const auto path=std::filesystem::temp_directory_path()/("ark-history-"+iterationSha256(documentToJson(session.document)).substr(0,16)+".ark");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove(path,ignored);}}cleanup{path};
    apply(session,R"([{"op":"feature.rollback","id":1}])");check(saveDocument(session.document,path.string().c_str()),"binary history save");check(loadDocument(path.string().c_str(),restored,&error)&&restored.historyTip==1,"binary history load retains cursor");
    AgentSession placed;
    apply(placed,R"([{"op":"feature.add","type":"box","name":"Placed definition","p":{"dx":10,"dy":5,"dz":3}},{"op":"assembly.add","feature":1,"name":"Physical occurrence"}])");
    rejected(placed,R"([{"op":"feature.edit","id":1,"suppressed":true}])");
    apply(placed,R"([{"op":"assembly.edit","id":1,"visible":false}])");
    check(!placed.document.find(1)->suppressed,"assembly visibility retains the physical definition");
    apply(placed,R"([{"op":"assembly.remove","id":1},{"op":"feature.edit","id":1,"suppressed":true}])");
    check(placed.document.find(1)->suppressed,"removing the last occurrence and suppressing is one valid atomic batch");
    std::cout<<"Feature history authoring passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
