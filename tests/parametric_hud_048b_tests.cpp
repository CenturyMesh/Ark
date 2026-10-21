// 048B Parametric HUD contracts: param.set hud metadata, typed rejections,
// persistence, undo/redo, failed rebuilds, hud_info and hud_flex_check.
#include "ark/agent.hpp"
#include "ark/assistant.hpp"
#include "ark/onboarding.hpp"
#include "ark/parametric_hud.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
using V=arkjson::Value;
void check(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);}
V json(const std::string& text){V v;check(arkjson::parse(text,v),"test JSON parses: "+text);return v;}
V payload(const V& r){check(!r.find("isError")->b,"tool failed: "+r.find("structuredContent")->dump());return *r.find("structuredContent");}
V applyBatch(ark::AgentSession& s,const std::string& commands,const char* actor="human"){V a=V::makeObj();a.set("commands",json(commands));a.set("expected_revision",V::makeNum((double)s.revision));V who=V::makeObj();who.set("kind",V::makeStr(actor));who.set("id",V::makeStr("hud-test"));a.set("actor",who);return s.call("doc_apply",a);}
// A rejected batch must name its typed code and leave authoring and revision untouched.
void rejects(ark::AgentSession& s,const std::string& commands,const std::string& code){
    const auto before=ark::documentToJson(s.document);const auto revision=s.revision;const V r=applyBatch(s,commands);
    check(r.find("isError")->b,"expected rejection "+code+" for "+commands);const auto& data=*r.find("structuredContent");
    check(data.find("error_code")&&data.find("error_code")->str==code,"expected "+code+", got "+data.dump());
    check(ark::documentToJson(s.document)==before&&s.revision==revision,"rejected batch changed the document: "+commands);
}
const ark::Parameter* param(const ark::Document& d,const char* name){for(const auto& p:d.params)if(p.name==name)return &p;return nullptr;}
const V* row(const V& info,const char* name){for(const auto& r:info.find("parameters")->arr)if(r.find("name")->str==name)return &r;return nullptr;}
const char* model=R"([
 {"op":"param.set","name":"width","expr":60,"hud":{"label":"Width","min":20,"max":120,"step":5,"unit":"mm","group":"Body","order":1}},
 {"op":"param.set","name":"height","expr":40,"hud":{"label":"Height","min":10,"max":100,"step":5,"unit":"mm","group":"Body","order":2}},
 {"op":"param.set","name":"radius","expr":8,"hud":{"label":"Hole radius","min":2,"max":12,"step":1,"unit":"mm","group":"Disc"}},
 {"op":"param.set","name":"count","expr":3,"hud":{"min":1,"max":6,"unit":"count","group":"Disc","order":5}},
 {"op":"param.set","name":"inner","expr":"width-10","hud":{"label":"Inner","min":0,"max":200}},
 {"op":"feature.add","type":"box","name":"Block","p":{"dx":"width","dy":30,"dz":"height-20"}},
 {"op":"feature.add","type":"box","name":"Inner","p":{"dx":"inner","dy":10,"dz":5}},
 {"op":"sketch.set","name":"Ring","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"ring","dof":[0,0,"radius"]}],"constraints":[]}},
 {"op":"feature.add","type":"extrude","name":"Disc","p":{"sketch":"Ring","height":"4"}}])";
void validation(){
    ark::AgentSession s;payload(applyBatch(s,R"([{"op":"param.set","name":"a","expr":5},{"op":"param.set","name":"n","expr":2}])"));
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"min":10,"max":5}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"min":5,"max":5}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"min":0,"max":10,"step":0}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"min":0,"max":10,"step":-1}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"min":0,"max":1e300}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"min":"0","max":10}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"colour":"red"}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"unit":"inch"}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"label":""}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"a","hud":7}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"n","hud":{"min":0,"max":10,"step":0.5,"unit":"count"}}])","HUD_INVALID");
    rejects(s,R"([{"op":"param.set","name":"missing","hud":{"min":0,"max":10}}])","HUD_PARAMETER_NOT_FOUND");
    // Declaring a range that excludes the current value is itself out of range.
    rejects(s,R"([{"op":"param.set","name":"a","hud":{"min":10,"max":20}}])","HUD_VALUE_OUT_OF_RANGE");
    payload(applyBatch(s,R"([{"op":"param.set","name":"a","hud":{"min":0,"max":10,"step":0.5,"unit":"mm"}}])"));
    check(param(s.document,"a")->expr=="5"&&param(s.document,"a")->hud.find("max")->num==10,"hud-only param.set keeps the expression");
    rejects(s,R"([{"op":"param.set","name":"a","expr":11}])","HUD_VALUE_OUT_OF_RANGE");
    rejects(s,R"([{"op":"param.set","name":"a","expr":-0.5}])","HUD_VALUE_OUT_OF_RANGE");
    const V out=applyBatch(s,R"([{"op":"param.set","name":"a","expr":"12"}])");check(out.find("structuredContent")->find("error")->str.find("[0, 10] mm")!=std::string::npos,"range rejection reports the limits");
    payload(applyBatch(s,R"([{"op":"param.set","name":"n","hud":{"min":0,"max":10,"unit":"count"}}])"));
    rejects(s,R"([{"op":"param.set","name":"n","expr":2.5}])","HUD_VALUE_NOT_INTEGER");
    payload(applyBatch(s,R"([{"op":"param.set","name":"a","expr":10}])"));check(param(s.document,"a")->value==10,"boundary value accepted");
    // doc_preview uses the same validation and never commits.
    V preview=V::makeObj();preview.set("commands",json(R"([{"op":"param.set","name":"a","expr":99}])"));preview.set("expected_revision",V::makeNum((double)s.revision));
    const auto previewed=payload(s.call("doc_preview",preview));check(!previewed.find("valid")->b&&previewed.find("candidate_result")->find("error_code")->str=="HUD_VALUE_OUT_OF_RANGE"&&param(s.document,"a")->value==10,"doc_preview rejects an out-of-range candidate");
    payload(applyBatch(s,R"([{"op":"param.set","name":"a","hud":null}])"));check(!ark::hudExposed(*param(s.document,"a"))&&param(s.document,"a")->value==10,"hud:null hides without removing the parameter");
    payload(applyBatch(s,R"([{"op":"param.set","name":"a","expr":99}])"));check(param(s.document,"a")->value==99,"a hidden parameter has no HUD range");
    // Capacity: 64 exposed parameters are accepted, the 65th rejects atomically.
    ark::AgentSession many;std::string batch="[";for(int i=0;i<64;++i)batch+=std::string(i?",":"")+R"({"op":"param.set","name":"p)"+std::to_string(i)+R"(","expr":1,"hud":{}})";payload(applyBatch(many,batch+"]"));
    rejects(many,R"([{"op":"param.set","name":"extra","expr":1,"hud":{}}])","HUD_CAPACITY");
    // Canonical defaults: unit none and a derived step.
    const auto canonical=ark::canonicalHud(json(R"({"min":0,"max":200})"));check(canonical.find("unit")->str=="none"&&canonical.find("step")->num==10,"canonical HUD defaults");
    std::cout<<"validation ok\n";
}
void persistence(){
    ark::AgentSession s;payload(applyBatch(s,model));const auto text=ark::documentToJson(s.document);
    ark::Document loaded;std::string error;check(ark::documentFromJson(text,loaded,&error),error);
    for(const auto& p:s.document.params)check(param(loaded,p.name.c_str())->hud.dump()==p.hud.dump(),"JSON round trip keeps HUD metadata of "+p.name);
    const auto dir=std::filesystem::temp_directory_path()/"ark-hud-048b";std::filesystem::create_directories(dir);
    for(const char* name:{"hud.ark","hud.arkjson"}){const auto path=(dir/name).string();check(ark::saveDocument(s.document,path.c_str()),"save "+path);ark::Document reopened;check(ark::loadDocument(path.c_str(),reopened,&error),error);
        for(const auto& p:s.document.params)check(param(reopened,p.name.c_str())->hud.dump()==p.hud.dump(),std::string(name)+" keeps HUD metadata");}
    std::filesystem::remove_all(dir);
    // The document format version does not depend on HUD metadata.
    ark::Document exposed=s.document.authoringCopy();exposed.iterationHistory=V{};ark::Document plain=exposed.authoringCopy();for(auto& p:plain.params)p.hud=V{};V withHud=json(ark::documentToJson(exposed)),withoutHud=json(ark::documentToJson(plain));
    check(withHud.find("version")->num==withoutHud.find("version")->num,"HUD metadata does not bump the document version");
    check(withoutHud.dump().find("\"hud\"")==std::string::npos,"hidden parameters serialize without a hud field");
    // Older documents without HUD metadata load unchanged.
    ark::Document legacy;check(ark::documentFromJson(R"({"format":"ark","version":1,"units":"mm","params":[{"name":"a","expr":"5"}],"features":[]})",legacy,&error),error);
    check(legacy.params.size()==1&&!ark::hudExposed(legacy.params[0])&&ark::documentToJson(legacy).find("\"hud\"")==std::string::npos,"legacy document loads without HUD metadata");
    ark::Document bad;check(!ark::documentFromJson(R"({"format":"ark","version":1,"units":"mm","params":[{"name":"a","expr":"5","hud":{"min":3,"max":1}}],"features":[]})",bad,&error)&&error.find("HUD_INVALID")!=std::string::npos,"invalid stored HUD metadata rejects the load");
    // A stored out-of-range value loads; unrelated edits work, changing it rechecks the range.
    ark::AgentSession stored;check(ark::documentFromJson(R"({"format":"ark","version":1,"units":"mm","params":[{"name":"a","expr":"50","hud":{"min":0,"max":10}},{"name":"b","expr":"1"}],"features":[]})",stored.document,&error),error);
    payload(applyBatch(stored,R"([{"op":"param.set","name":"b","expr":2}])"));rejects(stored,R"([{"op":"param.set","name":"a","expr":60}])","HUD_VALUE_OUT_OF_RANGE");
    V info=payload(stored.call("hud_info",V::makeObj()));check(!row(info,"a")->find("in_range")->b,"hud_info flags a stored out-of-range value");
    std::cout<<"persistence ok\n";
}
void history(){
    ark::AgentSession s;payload(applyBatch(s,R"([{"op":"param.set","name":"a","expr":5}])"));
    payload(applyBatch(s,R"([{"op":"param.set","name":"a","hud":{"min":0,"max":10}}])"));check(ark::hudExposed(*param(s.document,"a")),"exposed");
    payload(s.call("doc_undo",V::makeObj()));check(!ark::hudExposed(*param(s.document,"a")),"undo removes HUD exposure");
    payload(s.call("doc_redo",V::makeObj()));check(ark::hudExposed(*param(s.document,"a"))&&param(s.document,"a")->hud.find("max")->num==10,"redo restores HUD exposure");
    payload(applyBatch(s,R"([{"op":"param.set","name":"a","expr":7}])"));payload(s.call("doc_undo",V::makeObj()));check(param(s.document,"a")->value==5,"undo restores the HUD value");
    payload(s.call("doc_redo",V::makeObj()));check(param(s.document,"a")->value==7,"redo reapplies the HUD value");
    const auto nodes=payload(s.call("history_info",V::makeObj())).find("nodes")->arr;bool human=false;for(const auto& n:nodes)human|=n.find("actor")->find("kind")->str=="human"&&n.find("actor")->find("id")->str=="hud-test";
    check(human,"iteration history records the declared human actor of HUD edits");
    std::cout<<"undo/redo ok\n";
}
void rebuild(){
    ark::AgentSession s;payload(applyBatch(s,model));check(s.document.evalBodies.size()==3,"fixture evaluates three bodies");
    const auto before=ark::documentToJson(s.document);const auto revision=s.revision;const auto bodies=s.document.evalBodies.size();
    // In range, but the Block depth height-20 becomes negative: the rebuild fails atomically.
    const V failed=applyBatch(s,R"([{"op":"param.set","name":"height","expr":15}])");const auto& error=*failed.find("structuredContent");
    check(failed.find("isError")->b&&error.find("error")->str.find("Feature #1")!=std::string::npos&&error.find("document_unchanged")->b,"failed rebuild is rejected with the failing feature: "+error.dump());
    check(ark::documentToJson(s.document)==before&&s.revision==revision&&s.document.evalBodies.size()==bodies&&param(s.document,"height")->value==40,"failed rebuild keeps previous geometry, value and revision");
    payload(applyBatch(s,R"([{"op":"param.set","name":"height","expr":50},{"op":"param.set","name":"width","expr":80}])","agent"));
    check(s.revision==revision+1&&param(s.document,"inner")->value==70,"a two-value staged batch publishes once");
    std::cout<<"failed rebuild ok\n";
}
void tools(){
    ark::AgentSession s;payload(applyBatch(s,model));
    V info=payload(s.call("hud_info",V::makeObj()));check(info.find("exposed_count")->num==5&&info.find("parameters")->arr.size()==5,"hud_info lists every exposed parameter");
    check(info.find("parameters")->arr.front().find("name")->str=="inner","rows are ordered by group then order");
    auto dependent=[&](const char* name,ark::u64 id,const char* via){for(const auto& d:row(info,name)->find("dependents")->arr)if((ark::u64)d.find("id")->num==id&&d.find("via")->str==via)return true;return false;};
    check(dependent("height",1,"expression")&&dependent("width",1,"expression")&&dependent("width",2,"parameter:inner")&&dependent("radius",3,"sketch:Ring")&&dependent("inner",2,"expression"),"hud_info reports direct, parameter and sketch dependents: "+info.dump());
    check(row(info,"count")->find("dependents")->arr.empty()&&row(info,"inner")->find("driven")->b&&!row(info,"width")->find("driven")->b,"unused and driven parameters are explicit");
    check(row(info,"width")->find("label")->str=="Width"&&row(info,"width")->find("display")->str=="60 mm"&&row(info,"count")->find("label")->str=="count","labels default to the parameter name and values format by step");
    V subset=payload(s.call("hud_info",json(R"({"names":["radius"]})")));check(subset.find("parameters")->arr.size()==1,"hud_info names subset");
    check(s.call("hud_info",json(R"({"expected_revision":999})")).find("structuredContent")->find("error_code")->str=="REVISION_CONFLICT","hud_info rejects a stale expected_revision");
    check(s.call("hud_info",json(R"({"names":["nope"]})")).find("structuredContent")->find("error_code")->str=="HUD_PARAMETER_NOT_FOUND","hud_info rejects unknown names");
    const auto before=ark::documentToJson(s.document);const auto revision=s.revision;
    V flex=payload(s.call("hud_flex_check",V::makeObj()));
    check(ark::documentToJson(s.document)==before&&s.revision==revision&&!flex.find("committed")->b&&flex.find("sampled_values_only")->b,"hud_flex_check never commits");
    check(flex.find("scope")->str.find("not a proof for the whole range")!=std::string::npos,"flex report states its sampled scope");
    check(flex.find("evaluations")->num==12&&flex.find("failed")->num==1&&flex.find("passed")->num==11&&!flex.find("all_passed")->b&&flex.find("complete")->b,"flex check samples 4 literal parameters at 3 values: "+flex.dump());
    check(flex.find("skipped")->arr.size()==1&&flex.find("skipped")->arr[0].find("name")->str=="inner","driven parameters are skipped explicitly");
    const V* height=nullptr;for(const auto& p:flex.find("parameters")->arr)if(p.find("name")->str=="height")height=&p;check(height,"height sampled");
    const auto& low=height->find("samples")->arr[0];check(low.find("at")->str=="min"&&low.find("status")->str=="fail"&&low.find("failing_feature")->find("id")->num==1&&low.find("failing_feature")->find("name")->str=="Block"&&low.find("error")->str.find("dz must be positive")!=std::string::npos,"failing feature and error reported: "+low.dump());
    check(height->find("samples")->arr[1].find("value")->num==55&&height->find("samples")->arr[1].find("status")->str=="pass"&&height->find("samples")->arr[2].find("status")->str=="pass","mid and max of height pass");
    const V* count=nullptr;for(const auto& p:flex.find("parameters")->arr)if(p.find("name")->str=="count")count=&p;check(count&&count->find("samples")->arr[1].find("value")->num==4,"count mid snaps to the integer step");
    V bounded=payload(s.call("hud_flex_check",json(R"({"max_evaluations":2})")));check(!bounded.find("complete")->b&&bounded.find("not_evaluated")->num==10&&bounded.find("evaluations")->num==2,"evaluation bound reports unevaluated samples");
    V pass=payload(s.call("hud_flex_check",json(R"({"names":["width","radius"],"values":["min","max"]})")));check(pass.find("all_passed")->b&&pass.find("evaluations")->num==4,"subset of parameters and values passes");
    check(s.call("hud_flex_check",json(R"({"values":["low"]})")).find("structuredContent")->find("error_code")->str=="HUD_INVALID","invalid sample selector rejects");
    // Fix the model so the minimum height rebuilds, then the full check passes.
    payload(applyBatch(s,R"([{"op":"feature.edit","id":"Block","p":{"dz":"height"}}])","agent"));check(payload(s.call("hud_flex_check",V::makeObj())).find("all_passed")->b,"fixed model passes every sampled value");
    // An agent edit is visible to the HUD inventory immediately.
    payload(applyBatch(s,R"([{"op":"param.set","name":"radius","expr":11}])","agent"));check(row(payload(s.call("hud_info",V::makeObj())),"radius")->find("value")->num==11,"hud_info refreshes after an agent param.set");
    std::cout<<"hud tools ok\n";
}
void discovery(){
    ark::AgentSession s;const auto guide=ark::agentOnboard(s,json(R"({"topic":"hud"})"));
    check(guide.find("protocol")&&guide.find("protocol")->arr.size()==4&&guide.find("protocol")->dump().find("hud_flex_check")!=std::string::npos&&guide.find("when")->str.find("only when the user explicitly asks")!=std::string::npos,"hud onboarding protocol exists");
    check(guide.find("example_doc_apply")&&guide.find("validation"),"hud onboarding has example and validation");
    const auto overview=ark::agentOnboard(s,json(R"({"topic":"overview"})"));bool listed=false;for(const auto& t:overview.find("available_topics")->arr)listed|=t.str=="hud";
    check(listed&&overview.find("parametric_hud")&&overview.find("parametric_hud")->str.find("topic hud")!=std::string::npos,"hud topic is discoverable from the general guide");
    const auto catalogue=ark::AgentSession::tools();bool info=false,flex=false,schema=false,preview=false;
    for(const auto& t:catalogue.arr){const auto name=t.find("name")->str;const bool readOnly=t.find("annotations")->find("readOnlyHint")->b;
        if(name=="hud_info")info=readOnly;if(name=="hud_flex_check")flex=readOnly;
        if(name=="doc_apply")schema=t.find("inputSchema")->find("properties")->find("commands")->find("items")->find("properties")->find("hud")!=nullptr;
        if(name=="doc_preview")preview=t.find("inputSchema")->find("properties")->find("commands")->find("items")->find("properties")->find("hud")!=nullptr;}
    check(info&&flex&&schema&&preview,"HUD tools are read-only catalogue entries and param.set hud is in the command schema");
    auto has=[](const V& tools,const char* name){for(const auto& t:tools.arr)if(t.find("name")->str==name)return true;return false;};
    check(!has(ark::assistantActiveTools(catalogue,V::makeArr(),""),"hud_info"),"HUD tools wait for their topic in normal designs");
    V history=V::makeArr(),call=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr("load_hud"));call.set("name",V::makeStr("agent_onboard"));call.set("arguments",V::makeStr(R"({"topic":"hud"})"));history.arr.push_back(call);
    const auto loaded=ark::assistantActiveTools(catalogue,history,"");check(has(loaded,"hud_info")&&has(loaded,"hud_flex_check"),"loading topic hud offers its tools");
    std::cout<<"discovery ok\n";
}
}
int main(){try{validation();persistence();history();rebuild();tools();discovery();std::cout<<"parametric HUD 048B tests passed\n";return 0;}
catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
