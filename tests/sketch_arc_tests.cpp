#include "ark/agent.hpp"
#include "ark/sketch.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;
void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
void close(double actual,double expected,double tolerance,const char* message){check(std::abs(actual-expected)<=tolerance,message);}
V parse(const char* text){V result;check(arkjson::parse(text,result),"Test JSON parses");return result;}
V accepted(V response){if(response.find("isError")->b)throw std::runtime_error(response.find("structuredContent")->dump());return *response.find("structuredContent");}
V call(ark::AgentSession& session,const char* tool,const char* args){return accepted(session.call(tool,parse(args)));}
void reject(ark::AgentSession& session,const char* args){const auto revision=session.revision;const auto before=ark::documentToJson(session.document);check(session.call("doc_apply",parse(args)).find("isError")->b,"Invalid arc rejects");check(session.revision==revision&&ark::documentToJson(session.document)==before,"Invalid arc preserves full document and revision");}

void analyticRecipes(){
    auto minor=ark::arcThroughThreePoints({1,0,0},{0,1,0},{-1,0,0});
    close(minor.radius,1,1e-12,"Three-point radius");check(minor.center.approx({0,0,0},1e-12),"Three-point center");close(minor.sweep,ark::K_PI,1e-12,"Three-point semicircle");
    auto major=ark::arcThroughThreePoints({1,0,0},{-1,0,0},{0,1,0});close(major.sweep,1.5*ark::K_PI,1e-12,"Three-point major arc is retained");check(major.normal.z<-.99,"Clockwise plane orientation retained");check(major.point(2./3).approx({-1,0,0},1e-12),"Major arc contains authored through point");
    auto tangent=ark::arcFromStartTangent({1,0,0},{0,-1,0},{0,1,0});close(tangent.sweep,1.5*ark::K_PI,1e-12,"Tangent arc can be major");check(tangent.tangent(0).approx({0,1,0},1e-12)&&tangent.point(1).approx({0,-1,0},1e-12),"Tangent direction and endpoint retained");
    auto spatial=ark::arcFromStartTangent({2,5,0},{2,0,5},{0,0,2});check(spatial.center.approx({2,0,0},1e-12),"Spatial tangent center");check(spatial.normal.approx({1,0,0},1e-12),"Spatial tangent plane");
}

void drivenSketch(){
    ark::AgentSession session;
    call(session,"doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"R","expr":"5"},{"op":"sketch.set","name":"Arcs","sketch":{"plane":"XZ","entities":[{"kind":"arc","name":"three","recipe":{"type":"three_point","start":["R",0],"through":[0,"R"],"end":["-R",0]}},{"kind":"arc","name":"tangent","construction":true,"recipe":{"type":"tangent","start":["R",0],"end":[0,"-R"],"tangent":[0,1]}}]}}]})");
    const auto* named=session.document.findSketch("Arcs");check(named&&named->freeDofs==0,"Recipe coordinates are driven even for numeric inputs");close(named->sk.ents[0].dof[2],5,1e-12,"Planar recipe radius");close(named->sk.ents[1].dof[4]-named->sk.ents[1].dof[3],270,1e-12,"Planar tangent major sweep");
    auto inspected=call(session,"sketch_inspect",R"({"name":"Arcs","entity":"three"})");check(inspected.find("recipe")->find("start")->arr[0].str=="R","Inspector preserves source expression");close(inspected.find("length_mm")->num,5*ark::K_PI,1e-12,"Analytic sketch arc length");
    auto raw=ark::documentToJson(session.document,true,false);V saved;check(arkjson::parse(raw,saved),"Recipe persistence JSON");check(saved.find("version")->num==30,"New arc semantics select document version30");auto& persisted=saved.find("sketches")->arr[0].find("entities")->arr[0];check(persisted.find("recipe")&&!persisted.find("dof"),"Save preserves source recipe without cached center angles");
    ark::Document reload;std::string error;check(ark::documentFromJson(ark::documentToJson(session.document),reload,&error),"Recipe history reloads");reload.recompute();check(reload.findSketch("Arcs")->error.empty(),"Reload evaluates original recipes");
    saved.set("version",V::makeNum(29));check(!ark::documentFromJson(saved.dump(),reload,&error),"Older document version cannot silently drop recipe semantics");
    call(session,"doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"R","expr":"8"}]})");close(session.document.findSketch("Arcs")->sk.ents[0].dof[2],8,1e-12,"Parameter edit recomputes recipe");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"sketch.patch","name":"Arcs","entity":"three","r":99}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"param.set","name":"R","expr":"0"}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"sketch.entity.set","name":"Arcs","entity":{"kind":"arc","name":"three","recipe":{"type":"three_point","start":[0,0],"through":[5,0],"end":[10,0]}}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"sketch.entity.set","name":"Arcs","entity":{"kind":"arc","name":"three","recipe":{"type":"tangent","start":[0,0],"tangent":[0,0],"end":[10,10]}}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"sketch.entity.set","name":"Arcs","entity":{"kind":"arc","name":"three","recipe":{"type":"tangent","start":[0,0],"tangent":[1,0],"end":[10,0]}}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"sketch.entity.set","name":"Arcs","entity":{"kind":"arc","name":"three","dof":[0,0,2,0,90],"recipe":{"type":"three_point","start":[0,0],"through":[5,5],"end":[10,0]}}}]})");
    call(session,"doc_apply",R"({"expected_revision":2,"commands":[{"op":"sketch.entity.set","name":"Arcs","entity":{"kind":"arc","name":"three","recipe":{"type":"three_point","start":["R",0],"through":[0,"-R"],"end":["-R",0]}}}]})");check(session.document.findSketch("Arcs")->sk.ents[0].dof[4]<session.document.findSketch("Arcs")->sk.ents[0].dof[3],"Clockwise recipe is authored explicitly");
    call(session,"doc_undo",R"({})");check(session.document.findSketch("Arcs")->sk.ents[0].dof[4]>session.document.findSketch("Arcs")->sk.ents[0].dof[3],"Undo restores original through point");
}

#ifdef ARK_SKETCH_ARC_NATIVE_TESTS
const V& body(ark::AgentSession& session,size_t index){return session.document.evalBodies.at(index).nativeData;}
void spatialRecipes(){
    ark::AgentSession session;
    call(session,"doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"R","expr":5},{"op":"feature.add","type":"curve","name":"Spatial","native":{"start":[2,"R",0],"segments":[{"kind":"arc","mode":"three_point","through":[2,0,"R"],"to":[2,"-R",0]}]}},{"op":"feature.add","type":"curve","name":"Tangent chain","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[10,0,0]},{"kind":"arc","mode":"tangent","to":[20,10,0]},{"kind":"arc","mode":"tangent","to":[10,20,10]}]}},{"op":"feature.add","type":"curve","name":"Major","native":{"start":[1,0,0],"segments":[{"kind":"arc","mode":"three_point","through":[-1,0,0],"to":[0,1,0]}]}},{"op":"feature.add","type":"curve","name":"Radius major","native":{"start":[1,0,0],"segments":[{"kind":"arc","radius":1,"major":true,"normal":[0,0,1],"sweep":1,"to":[0,1,0]}]}}]})");
    close(body(session,0).find("report")->find("length_mm")->num,5*ark::K_PI,1e-8,"Spatial arc is exact semicircle");close(body(session,2).find("report")->find("length_mm")->num,1.5*ark::K_PI,1e-8,"Native three-point major arc retains exact length");close(body(session,3).find("report")->find("length_mm")->num,1.5*ark::K_PI,1e-8,"Native radius-major arc retains exact length");
    auto query=call(session,"geometry_query",R"({"id":"Tangent chain","queries":[{"action":"continuity","a":"edge.0","b":"edge.1","fraction_a":1,"fraction_b":0},{"action":"continuity","a":"edge.1","b":"edge.2","fraction_a":1,"fraction_b":0}]})");for(const auto& row:query.find("results")->arr)check(row.find("ok")->b&&row.find("g1")->b,"Inherited tangent joins are exact G1 in spatial wire");
    call(session,"doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"R","expr":8}]})");close(body(session,0).find("report")->find("length_mm")->num,8*ark::K_PI,1e-8,"Native expression change rebuilds exact arc");
    ark::Document reload;std::string error;check(ark::documentFromJson(ark::documentToJson(session.document),reload,&error),"Spatial arc history reloads");reload.recompute();for(const auto& status:reload.lastResults)check(status.ok,"Spatial recipes cold-recompute after reload");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"curve","native":{"start":[0,0,0],"segments":[{"kind":"arc","mode":"three_point","through":[1,0,0],"to":[2,0,0]}]}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"curve","native":{"start":[0,0,0],"segments":[{"kind":"arc","mode":"three_point","through":[0,1,1],"to":[1,0,0],"normal":[0,0,1]}]}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"curve","native":{"start":[0,0,0],"segments":[{"kind":"arc","mode":"three_point","through":[0,1,0],"to":[1,0,0],"normal":[0.000001,0,1]}]}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"curve","native":{"start":[0,0,0],"segments":[{"kind":"arc","mode":"tangent","to":[10,10,0]}]}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"curve","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[10,0,0]},{"kind":"arc","mode":"tangent","tangent":[-1,0,0],"to":[20,10,0]}]}}]})");
    reject(session,R"({"expected_revision":2,"commands":[{"op":"feature.add","type":"curve","native":{"start":[0,0,0],"segments":[{"kind":"arc","radius":10,"sweep":1.5,"to":[10,10,0]}]}}]})");
}
void exactSweepConsumer(){
    ark::AgentSession session;
    call(session,"doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"R","expr":8},{"op":"plane.set","name":"Section plane","p":{"ox":2,"oy":"R"}},{"op":"sketch.set","name":"Section","sketch":{"plane":"Section plane","entities":[{"kind":"circle","name":"disk","dof":[0,0,"1"]}]}},{"op":"feature.add","type":"curve","name":"Spine","native":{"start":[2,"R",0],"segments":[{"kind":"arc","mode":"three_point","through":[2,0,"R"],"to":[2,"-R",0],"normal":[-1,0,0]}]}},{"op":"feature.add","type":"sweep","name":"Swept arc","native":{"section":"Section","path":"Spine"},"p":{"solid":1,"correction":1}}]})");
    close(body(session,1).find("report")->find("volume_mm3")->num,8*ark::K_PI*ark::K_PI,1e-6,"Sweep consumes exact authored spatial circle, not its display samples");
    call(session,"doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"R","expr":12}]})");
    close(body(session,1).find("report")->find("volume_mm3")->num,12*ark::K_PI*ark::K_PI,1e-6,"Linked sweep recomputes from changed arc recipe");
    call(session,"doc_apply",R"({"expected_revision":2,"commands":[{"op":"sketch.set","name":"Cap","sketch":{"entities":[{"kind":"arc","name":"crown","recipe":{"type":"three_point","start":["-R",0],"through":[0,"R"],"end":["R",0]}},{"kind":"line","name":"base","dof":["R",0,"-R",0]}]}},{"op":"feature.add","type":"native_solid","name":"Arc cap","native":{"op":"extrude"},"p":{"sketch":"Cap","depth":2}}]})");
    close(body(session,2).find("report")->find("volume_mm3")->num,144*ark::K_PI,1e-6,"Native planar recipe extrudes an exact semicircle area");
}
#endif
}
int main(){try{analyticRecipes();drivenSketch();
#ifdef ARK_SKETCH_ARC_NATIVE_TESTS
spatialRecipes();exactSweepConsumer();
#endif
std::cout<<"Analytic/parametric arc recipes, persistence, orientation and atomic rejection passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
