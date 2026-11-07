#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;
using V=arkjson::Value;
int checks=0;
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
V json(const char* source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Invalid synthetic fixture");return value;}
V& mutableField(V& value,const char* key){for(auto& item:value.obj)if(item.first==key)return item.second;throw std::runtime_error("Missing mutable fixture field");}
V accepted(const V& result){if(result.find("isError")->b)throw std::runtime_error(result.dump());return *result.find("structuredContent");}
V apply(AgentSession& session,V commands){V args=V::makeObj();args.set("commands",std::move(commands));args.set("expected_revision",V::makeNum(session.revision));return accepted(session.call("doc_apply",args));}
void reject(AgentSession& session,V commands){const auto before=documentToJson(session.document);const auto revision=session.revision;V args=V::makeObj();args.set("commands",std::move(commands));args.set("expected_revision",V::makeNum(revision));check(session.call("doc_apply",args).find("isError")->b,"Invalid projection edit must reject");check(session.revision==revision&&documentToJson(session.document)==before,"Rejected projection edit must preserve source and revision");}
const Body& body(const Document& doc,u64 id){for(const auto& result:doc.evalBodies)if(result.id==id)return result;throw std::runtime_error("Missing exact body");}
double volume(const Document& doc,u64 id){return body(doc,id).nativeData.find("report")->find("volume_mm3")->num;}
void near(double actual,double expected,double tolerance,const char* reason){check(std::abs(actual-expected)<=tolerance,reason);}
void box(V& commands,int index){auto command=json(R"({"op":"feature.add","type":"box","p":{"dx":3,"dy":4,"dz":5}})");command.set("name",V::makeStr("Unrelated "+std::to_string(index)));commands.arr.push_back(std::move(command));}
void checkDisk(const Document& doc,u64 id,double radius){near(volume(doc,id),K_PI*radius*radius*2,1e-5,"Projection chain lost independently known analytic volume");}

void oncePerPass(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    auto commands=json(R"([
      {"op":"param.set","name":"Radius","expr":"4"},
      {"op":"feature.add","type":"native_solid","name":"Round","native":{"op":"cylinder"},"p":{"radius":"Radius","height":10}},
      {"op":"sketch.set","name":"A","sketch":{"plane":"XY","entities":[{"kind":"project","name":"rim","feature":"Round","reference":"cap.end","mode":"normal"}]}},
      {"op":"sketch.set","name":"B","sketch":{"plane":"XY","entities":[{"kind":"project","name":"rim","source_sketch":"A","source_entity":"rim","mode":"normal"}]}},
      {"op":"feature.add","type":"native_solid","name":"Disk","native":{"op":"extrude"},"p":{"sketch":"B","depth":2}}
    ])");
    for(int i=0;i<40;++i)box(commands,i);
    apply(session,std::move(commands));checkDisk(session.document,2,4);
    check(session.document.recomputeProjectionBuilds==4,"Audited two-sketch chain should require two waiting and two successful builds");
    check(session.document.recomputeProjectionReused==84,"Unrelated history rows should retain solved exact projection closures");
    const auto initialA=session.document.findSketch("A")->sk.ents[0].projectionBrep;
    const auto initialB=session.document.findSketch("B")->sk.ents[0].projectionBrep;
    const auto original=session.document.evaluationCache.at(1).body;
    apply(session,json(R"([{"op":"feature.edit","id":40,"p":{"dx":9}}])"));
    check(session.document.evaluationCache.at(1).body==original,"Unrelated edit must retain native source body");
    check(session.document.recomputeProjectionBuilds==4&&session.document.recomputeProjectionReused==84,"Each edit must start a fresh, bounded projection pass");
    check(session.document.findSketch("A")->sk.ents[0].projectionBrep==initialA&&session.document.findSketch("B")->sk.ents[0].projectionBrep==initialB,"Unrelated edit changed exact projected curves");
    apply(session,json(R"([{"op":"param.set","name":"Radius","expr":"6"}])"));checkDisk(session.document,2,6);
    check(session.document.findSketch("A")->sk.ents[0].projectionBrep!=initialA&&session.document.findSketch("B")->sk.ents[0].projectionBrep!=initialB,"Real source edit reused stale projected curves");
    accepted(session.call("doc_undo",V::makeObj()));checkDisk(session.document,2,4);
    accepted(session.call("doc_redo",V::makeObj()));checkDisk(session.document,2,6);
    Document reopened;std::string error;check(documentFromJson(documentToJson(session.document),reopened,&error),"Fresh reference document must deserialize");
    reopened.recompute();for(const auto& result:reopened.lastResults)check(result.ok,"Fresh reference recompute must succeed");
    checkDisk(reopened,2,6);near(volume(reopened,2),volume(session.document,2),1e-9,"Fresh reference and edited chain must agree");
    check(reopened.findSketch("B")->sk.ents[0].projectionBrep==session.document.findSketch("B")->sk.ents[0].projectionBrep,"Fresh exact projection must equal retained edited projection");
    reject(session,json(R"([{"op":"feature.edit","id":"Round","suppressed":true}])"));
    reject(session,json(R"([{"op":"sketch.entity.set","name":"A","entity":{"kind":"project","name":"rim","feature":"Round","reference":"missing","mode":"normal"}}])"));
    reject(session,json(R"([{"op":"sketch.entity.set","name":"A","entity":{"kind":"project","name":"rim","feature":"Disk","reference":"cap.end","mode":"normal"}}])"));
    const auto before=documentToJson(session.document);const auto revision=session.revision;
    check(session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"Radius","expr":"7"}]})")).find("isError")->b,"Stale edit must reject");
    check(before==documentToJson(session.document)&&revision==session.revision,"Stale edit changed live document");
}

void lateSourceAndReplacement(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();auto commands=V::makeArr();
    for(int i=0;i<12;++i)box(commands,i);
    for(auto command:json(R"([
      {"op":"feature.add","type":"native_solid","name":"Late","native":{"op":"cylinder"},"p":{"radius":3,"height":8}},
      {"op":"feature.add","type":"native_solid","name":"Replacement","native":{"op":"cylinder"},"p":{"radius":5,"height":8}},
      {"op":"plane.set","name":"Target plane","p":{"oz":2}},
      {"op":"sketch.set","name":"Late projection","sketch":{"plane":"Target plane","entities":[{"kind":"project","name":"rim","feature":"Late","reference":"cap.end","mode":"normal"}]}},
      {"op":"feature.add","type":"native_solid","name":"Disk","native":{"op":"extrude"},"p":{"sketch":"Late projection","depth":2}}
    ])").arr)commands.arr.push_back(std::move(command));
    for(int i=12;i<24;++i)box(commands,i);
    apply(session,std::move(commands));checkDisk(session.document,15,3);
    check(session.document.recomputeProjectionBuilds==14,"Projection must wait through every earlier row until late source is published");
    check(session.document.recomputeProjectionReused==15,"Projection should retain only after late source is ready");
    apply(session,json(R"([{"op":"sketch.entity.set","name":"Late projection","entity":{"kind":"project","name":"rim","feature":"Replacement","reference":"cap.end","mode":"normal"}}])"));checkDisk(session.document,15,5);
    apply(session,json(R"([{"op":"plane.set","name":"Target plane","p":{"oz":4}}])"));checkDisk(session.document,15,5);
    near(body(session.document,15).bbox().mn.z,4,1e-6,"Target datum edit must move downstream exact extrusion");
}

void occurrencesStayConservative(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    auto commands=json(R"([
      {"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":10,"depth":6,"height":3}},
      {"op":"assembly.add","name":"Placed","feature":"Block","p":{"tx":5}},
      {"op":"sketch.set","name":"Placed projection","sketch":{"plane":"XY","entities":[{"kind":"project","name":"edge","feature":"Block","reference":"edge.x.ymin.zmax","mode":"normal","source_instance":1}]}},
      {"op":"sketch.set","name":"Indirect projection","sketch":{"plane":"XY","entities":[{"kind":"project","name":"edge","source_sketch":"Placed projection","source_entity":"edge","mode":"normal"}]}}
    ])");
    for(int i=0;i<4;++i)box(commands,i);apply(session,std::move(commands));
    check(session.document.recomputeProjectionReused==0,"Occurrence dependencies must not enter pass-local completion reuse");
    apply(session,json(R"([{"op":"assembly.edit","id":1,"p":{"tx":8}}])"));
    for(const char* name:{"Placed projection","Indirect projection"}){
        const auto& points=session.document.findSketch(name)->sk.ents[0].projectionSamples;
        near(std::min(points.front().x,points.back().x),8,1e-7,"Occurrence pose edit must reach direct and indirect projections");
    }
    check(session.document.recomputeProjectionReused==0,"Indirect occurrence path lost conservative refresh after edit");
}

V surface(AgentSession& session,V args){args.set("expected_revision",V::makeNum(session.revision));return accepted(session.call("surface_control",args));}
std::string hash(const Document& document,u64 id){return body(document,id).nativeData.find("topology")->find("brep_sha256")->str;}
using Cache=std::unordered_map<u64,Document::EvaluationCacheEntry>;
void retained(const Document& document,const Cache& cache,std::initializer_list<u64> ids){for(u64 id:ids){const auto& before=cache.at(id);const auto& after=document.evaluationCache.at(id);check(before.body==after.body&&before.generation==after.generation,"Unrelated edit reran an audited exact surface operation");}}
void rebuilt(const Document& document,const Cache& cache,std::initializer_list<u64> ids){for(u64 id:ids)check(cache.at(id).body!=document.evaluationCache.at(id).body&&cache.at(id).generation!=document.evaluationCache.at(id).generation,"Changed surface input/expression retained stale geometry");}

void surfacePoleAndMatchClosure(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    auto commands=json(R"([{ "op":"param.set","name":"PoleDelta","expr":"1"}])");box(commands,0);
    for(int target=0;target<2;++target){
        auto command=json(R"({"op":"feature.add","type":"bspline_surface","p":{"u_degree":3,"v_degree":3}})");
        V native=V::makeObj(),poles=V::makeArr();
        for(int u=0;u<4;++u){V row=V::makeArr();for(int v=0;v<4;++v){const double z[]={0,0,6,18};V point=V::makeArr();point.arr={V::makeNum(target?-10*u:5+10*u),V::makeNum(10*v),V::makeNum(target?z[u]:2+u)};row.arr.push_back(std::move(point));}poles.arr.push_back(std::move(row));}
        native.set("poles",std::move(poles));command.set("native",std::move(native));command.set("name",V::makeStr(target?"Target":"Source"));commands.arr.push_back(std::move(command));
    }
    apply(session,std::move(commands));
    auto edit=json(R"({"op":"feature.add","type":"surface_edit","name":"Moved pole","inputs":[2],"native":{"op":"deform_poles","max_displacement_mm":8,"edits":[{"u":1,"v":1,"delta_mm":[0,0,"PoleDelta"]}]}})");
    mutableField(edit,"native").set("source_brep_sha256",V::makeStr(hash(session.document,2)));
    auto match=json(R"({"op":"feature.add","type":"surface_edit","name":"Matched boundary","inputs":[2,3],"native":{"op":"match_boundary","source_side":"u_min","target_side":"u_min","continuity":2,"max_displacement_mm":20,"samples":33}})");
    mutableField(match,"native").set("source_brep_sha256",V::makeStr(hash(session.document,2)));mutableField(match,"native").set("target_brep_sha256",V::makeStr(hash(session.document,3)));
    commands=V::makeArr();commands.arr={edit,match};apply(session,std::move(commands));
    const auto initial=session.document.evaluationCache;
    const auto exactPole=body(session.document,4).nativeData.find("brep_base64")->str,exactMatch=body(session.document,5).nativeData.find("brep_base64")->str;
    apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"dx":9}}])"));
    retained(session.document,initial,{2,3,4,5});check(session.document.recomputeEvaluated==1&&session.document.recomputeReused==4,"Unrelated stock edit should evaluate only stock, retaining pole and Match results");
    check(body(session.document,4).nativeData.find("brep_base64")->str==exactPole&&body(session.document,5).nativeData.find("brep_base64")->str==exactMatch,"Unrelated edit changed exact pole/match B-reps");
    const auto beforeDelta=session.document.evaluationCache;apply(session,json(R"([{"op":"param.set","name":"PoleDelta","expr":"2"}])"));
    retained(session.document,beforeDelta,{1,2,3,5});rebuilt(session.document,beforeDelta,{4});
    const auto net=surface(session,json(R"({"action":"inspect","id":4})"));
    near(net.find("poles")->arr[5].find("point_mm")->arr[2].num,5,1e-10,"Pole expression edit must reach exact control point");
    auto replacement=json(R"({"op":"feature.edit","id":3})");auto native=session.document.find(3)->nativeSpec;mutableField(native,"poles").arr[0].arr[0].arr[2]=V::makeNum(1);replacement.set("native",std::move(native));commands=V::makeArr();commands.arr.push_back(std::move(replacement));reject(session,std::move(commands));
    retained(session.document,beforeDelta,{1,2,3,5}); // failed target edit did not replace valid cached outputs
    replacement=json(R"({"op":"feature.edit","id":2})");native=session.document.find(2)->nativeSpec;mutableField(native,"poles").arr[3].arr[3].arr[2]=V::makeNum(7);replacement.set("native",std::move(native));commands=V::makeArr();commands.arr.push_back(std::move(replacement));reject(session,std::move(commands));
    Document reopened;std::string error;check(documentFromJson(documentToJson(session.document),reopened,&error),"Surface closure fixture must reopen");reopened.recompute();for(const auto& result:reopened.lastResults)check(result.ok,"Surface closure fresh reference must succeed");
    check(body(reopened,5).nativeData.find("brep_base64")->str==exactMatch,"Fresh Match and retained Match exact B-reps differ");
}

void surfaceTrimAndExtensionClosure(){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    auto commands=json(R"([{ "op":"param.set","name":"Width","expr":"20"},{"op":"param.set","name":"Cut","expr":"8"},{"op":"param.set","name":"Margin","expr":"2"}])");box(commands,0);
    const auto recipe=json(R"([
      {"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":"Width","depth":10,"height":5}},
      {"op":"feature.add","type":"surface_edit","name":"Top","inputs":[2],"native":{"op":"extract_face","face":"z_max"}},
      {"op":"feature.add","type":"surface_edit","name":"Bottom","inputs":[2],"native":{"op":"extract_face","face":"z_min"}},
      {"op":"feature.add","type":"bspline_surface","name":"Cutter","p":{"u_degree":1,"v_degree":1},"native":{"poles":[[["Cut",-5,-5],["Cut",15,-5]],[["Cut",-5,10],["Cut",15,10]]]}},
      {"op":"feature.add","type":"surface_edit","name":"Trimmed pair","inputs":[3,4,5],"native":{"op":"multi_trim","source_faces":["",""],"keep_points":[[12,5,5],[12,5,0]]}},
      {"op":"feature.add","type":"surface_edit","name":"Extended top","inputs":[3],"native":{"op":"extend_trimmed","bounds":["-Margin","Width+Margin","-Margin","10+Margin"]}}
    ])");for(const auto& command:recipe.arr)commands.arr.push_back(command);apply(session,std::move(commands));
    auto area=[&](u64 id){return body(session.document,id).nativeData.find("report")->find("area_mm2")->num;};
    near(area(6),240,1e-5,"Initial multi-trim analytic area");near(area(7),336,1e-5,"Initial extension analytic area");
    const auto initial=session.document.evaluationCache;apply(session,json(R"([{"op":"feature.edit","id":1,"p":{"dx":9}}])"));
    retained(session.document,initial,{2,3,4,5,6,7});check(session.document.recomputeEvaluated==1&&session.document.recomputeReused==6,"Unrelated stock edit should retain exact multi-trim and extension");
    const auto beforeWidth=session.document.evaluationCache;apply(session,json(R"([{"op":"param.set","name":"Width","expr":"22"}])"));
    retained(session.document,beforeWidth,{1,5});rebuilt(session.document,beforeWidth,{2,3,4,6,7});near(area(6),280,1e-5,"Source width change must reach both exact trimmed faces");near(area(7),364,1e-5,"Source width change must reach exact extended face");
    const auto beforeCut=session.document.evaluationCache;apply(session,json(R"([{"op":"param.set","name":"Cut","expr":"9"}])"));
    retained(session.document,beforeCut,{1,2,3,4,7});rebuilt(session.document,beforeCut,{5,6});near(area(6),260,1e-5,"Cutter expression change must move both trim boundaries");
    const auto beforeMargin=session.document.evaluationCache;apply(session,json(R"([{"op":"param.set","name":"Margin","expr":"3"}])"));
    retained(session.document,beforeMargin,{1,2,3,4,5,6});rebuilt(session.document,beforeMargin,{7});near(area(7),448,1e-5,"Extension expression change must alter exact added area");
    reject(session,json(R"([{"op":"param.set","name":"Cut","expr":"12"}])"));
    near(area(6),260,1e-5,"Failed boundary keep point must preserve previous exact trim");
    Document reopened;std::string error;check(documentFromJson(documentToJson(session.document),reopened,&error),"Trim and extension fixture must reopen");reopened.recompute();for(const auto& result:reopened.lastResults)check(result.ok,"Fresh trim/extension reference failed");
    for(u64 id:{6u,7u})check(body(reopened,id).nativeData.find("brep_base64")->str==body(session.document,id).nativeData.find("brep_base64")->str,"Fresh and retained trim/extension B-reps differ");
}
}
int main(){try{oncePerPass();lateSourceAndReplacement();occurrencesStayConservative();surfacePoleAndMatchClosure();surfaceTrimAndExtensionClosure();std::cout<<"069D projection and surface recompute: "<<checks<<" operation-count, analytic geometry, fresh-reference, expression, stale hash, rollback and occurrence checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
