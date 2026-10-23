#include "ark/agent.hpp"
#include "ark/exploded_view.hpp"
#include "ark/history_authoring.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace ark;
using V=arkjson::Value;
namespace {
unsigned checks=0;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing ")+key);return *found;}
V json(const char* text){V value;check(arkjson::parse(text,value),"Invalid fixture JSON");return value;}
void apply(AgentSession& session,V commands){V args=V::makeObj();args.set("expected_revision",V::makeNum(session.revision));args.set("commands",commands);const auto result=session.call("doc_apply",args);check(!field(result,"isError").b,result.dump());}
const Body& body(const Document& document,u64 id){for(const auto& candidate:document.evalBodies)if(candidate.id==id)return candidate;throw std::runtime_error("Missing exact body");}
void visiblePair(Document& document,u64 revision,const std::string& output={}){
    // Check actual authored outputs and native evidence before checking pixels.
    const auto consumed=consumedFeatureInputs(document);
    check(!consumed.count(1),"A derivative support was consumed by its patch");
    check(consumed.count(2)&&!consumed.count(3),"Closed boundary must remain construction input and patch an output");
    const auto& support=body(document,1);const auto& patch=body(document,3);
    check(std::abs(field(field(support.nativeData,"report"),"area_mm2").num-50)<1e-6,"Support area changed");
    const auto& report=field(patch.nativeData,"report");
    check(std::abs(field(report,"area_mm2").num-100)<1e-6,"Patch area changed");
    check(field(report,"support_boundary_evidence").arr.size()==1,"Exact G1 boundary evidence missing");
    // The separate coplanar faces share just an edge. A balloon is emitted only
    // for a rendered owner with visible pixels, so this proves both are drawn.
    // This is a headless software render, not a Studio/window launch or GPU claim.
    const auto before=documentToJson(document);Image image;
    const auto evidence=visualizeRender(document,revision,json(R"({"view":"top","projection":"orthographic","samples":1,"balloons":true})"),image);
    check(field(evidence,"occurrences").num==2,"Render scene omitted the support or included the construction wire");
    std::set<std::string> visibleNames;for(const auto& balloon:field(evidence,"balloons").arr)visibleNames.insert(field(balloon,"name").str);
    check(visibleNames==std::set<std::string>{"Continuity support","Supported patch"},"Both adjacent faces must own visible rendered pixels");
    check(documentToJson(document)==before,"Read-only support visibility render changed the drawing");
    if(!output.empty())check(writePNG(output.c_str(),image),"Could not save support visibility evidence");
}
void run(const std::string& output){
    AgentSession session;session.suppressFailureCapture();session.suppressIterationHistory();
    apply(session,json(R"([
      {"op":"feature.add","type":"bspline_surface","name":"Continuity support","p":{"u_degree":1,"v_degree":1},"native":{"poles":[[[0,-5,0],[0,0,0]],[[10,-5,0],[10,0,0]]]},"appearance":{"base_color":[0.1,0.4,0.8]}},
      {"op":"feature.add","type":"curve","name":"Closed boundary","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[10,0,0]},{"kind":"line","to":[10,10,0]},{"kind":"line","to":[0,10,0]},{"kind":"line","to":[0,0,0]}]}}
    ])"));
    const auto& topology=field(body(session.document,1).nativeData,"topology");const auto supportDigest=field(topology,"brep_sha256").str;
    std::string supportFace;for(const auto& reference:field(topology,"references").arr)if(field(reference,"kind").str=="face"&&field(reference,"status").str=="bound"&&field(reference,"indices").arr.size()==1){supportFace=field(reference,"name").str;break;}
    check(!supportFace.empty(),"Support has no current bound face");
    auto commands=json(R"([{"op":"feature.add","type":"surface_fill","name":"Supported patch","inputs":[2,1],"native":{"boundary_curve":2,"constraints":[{"edge":0,"support":1,"face":"pending","continuity":1}]},"appearance":{"base_color":[0.8,0.2,0.1]}}])");
    auto native=field(commands.arr[0],"native"),constraints=field(native,"constraints");constraints.arr[0].set("face",V::makeStr(supportFace));native.set("constraints",constraints);commands.arr[0].set("native",native);
    apply(session,commands);visiblePair(session.document,session.revision,output);
    check(field(field(body(session.document,1).nativeData,"topology"),"brep_sha256").str==supportDigest,"Adding a patch rewrote its support B-rep");
    // Dependency order is not a statement that the first input is consumed.
    apply(session,json(R"([{"op":"feature.edit","id":3,"inputs":[1,2]}])"));visiblePair(session.document,session.revision);
    const auto snapshot=documentToJson(session.document);Document restored;std::string error;
    check(documentFromJson(snapshot,restored,&error),error);restored.recompute();visiblePair(restored,session.revision);
    check(documentToJson(restored)==snapshot,"Support visibility changed persisted authoring");
}
}
int main(int argc,char** argv){try{run(argc>1?argv[1]:std::string());std::cout<<checks<<" surface support visibility checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
