#include "ark/agent.hpp"
#include "ark/occt_solid.hpp"
#include "ark/serialize.hpp"
#include "ark/sketch.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;using namespace ark;
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static V json(const char* text){V result;check(arkjson::parse(text,result),"Invalid fixture JSON");return result;}
static V apply(AgentSession& s,const char* commands){V args=V::makeObj();args.set("commands",json(commands));args.set("expected_revision",V::makeNum(s.revision));auto result=s.call("doc_apply",args);if(result.find("isError")->b)throw std::runtime_error(result.dump());return *result.find("structuredContent");}
static const V& report(const AgentSession& s){return *s.document.evalBodies.back().nativeData.find("report");}
static void closeValue(double a,double b,const char* message){++checks;if(std::abs(a-b)>=1e-7*std::max(1.,std::abs(b)))throw std::runtime_error(std::string(message)+" actual="+std::to_string(a)+" expected="+std::to_string(b));}
static void batch(){
    AgentSession s;s.suppressFailureCapture();apply(s,R"([{"op":"feature.add","type":"native_solid","name":"A","native":{"op":"box"},"p":{"width":"10","depth":"10","height":"4"}},{"op":"feature.add","type":"native_solid","name":"B source","native":{"op":"box"},"p":{"width":"10","depth":"10","height":"4"}},{"op":"feature.add","type":"native_solid","name":"B","inputs":["B source"],"native":{"op":"transform"},"p":{"tx":"20"}},{"op":"feature.add","type":"native_solid","name":"Bridge source","native":{"op":"box"},"p":{"width":"14","depth":"10","height":"4"}},{"op":"feature.add","type":"native_solid","name":"Bridge","inputs":["Bridge source"],"native":{"op":"transform"},"p":{"tx":"8"}},{"op":"feature.add","type":"native_solid","name":"Joined","inputs":["A","B","Bridge"],"native":{"op":"union","boolean_mode":"all","result_mode":"single"}}])");
    closeValue(report(s).find("volume_mm3")->num,1200,"All-input union must fuse a bridge even when initially separated targets precede it");check(report(s).find("bodies")&&report(s).find("bodies")->arr.size()==1,"Missing exact body inventory");
    AgentSession m;m.suppressFailureCapture();apply(m,R"([{"op":"feature.add","type":"native_solid","name":"A","native":{"op":"box"},"p":{"width":"10","depth":"10","height":"4"}},{"op":"feature.add","type":"native_solid","name":"B source","native":{"op":"box"},"p":{"width":"10","depth":"10","height":"4"}},{"op":"feature.add","type":"native_solid","name":"B","inputs":["B source"],"native":{"op":"transform"},"p":{"tx":"20"}},{"op":"feature.add","type":"native_solid","name":"Bodies","inputs":["A","B"],"native":{"op":"union","boolean_mode":"all","result_mode":"bodies"}}])");
    closeValue(report(m).find("volume_mm3")->num,800,"Multibody result lost source volume");check(report(m).find("solids")->num==2&&report(m).find("bodies")->arr.size()==2,"Multibody union silently dropped a solid");for(const auto& body:report(m).find("bodies")->arr)check(body.find("id")&&body.find("face_indices")&&!body.find("face_indices")->arr.empty(),"Body selection lacks current exact identity/face ownership");
}
static void arrangement(){
    AgentSession s;s.suppressFailureCapture();apply(s,R"([{"op":"sketch.set","name":"Grid","sketch":{"entities":[{"kind":"wire","name":"Border","start":[0,0],"segments":[{"kind":"line","to":[10,0]},{"kind":"line","to":[10,10]},{"kind":"line","to":[0,10]},{"kind":"line","to":[0,0]}]},{"kind":"line","name":"Horizontal","dof":[0,5,10,5]},{"kind":"line","name":"Vertical","dof":[5,0,5,10]}]}}])");
    auto result=s.call("sketch_inspect",json(R"({"name":"Grid","native_regions":true,"region_mode":"arrangement"})"));if(result.find("isError")->b)throw std::runtime_error(result.dump());const auto& census=*result.find("structuredContent")->find("native_regions");if(census.find("status")->str!="closed")throw std::runtime_error(census.dump());check(census.find("regions")->arr.size()==4,"Crossing grid must produce four bounded exact cells");for(const auto& cell:census.find("regions")->arr){closeValue(cell.find("area_mm2")->num,25,"Grid cell area differs from analytic partition");check(cell.find("seed")->find("point"),"Arrangement cell must expose an exact interior seed");}
    apply(s,R"([{"op":"feature.add","type":"native_solid","name":"Selected cell","native":{"op":"extrude","region_mode":"arrangement","regions":[{"point":[2,2]}]},"p":{"sketch":"Grid","depth":"4"}}])");closeValue(report(s).find("volume_mm3")->num,100,"Selected arrangement cell was not the exact extrusion authority");
    apply(s,R"([{"op":"feature.add","type":"native_solid","name":"All cells","native":{"op":"extrude","region_mode":"arrangement","result_mode":"bodies"},"p":{"sketch":"Grid","depth":"4"}}])");check(report(s).find("solids")->num==4&&report(s).find("bodies")->arr.size()==4,"Multibody cell extrusion lost a selected region");closeValue(report(s).find("volume_mm3")->num,400,"Cell-volume sum does not conserve the bounded region");
}

#include "solid_advanced_074b_cases.inl"
#include "solid_arrangement_074b_cases.inl"
#include "solid_selection_074b_cases.inl"
int main(){try{std::cerr<<"batch\n";batch();std::cerr<<"arrangement\n";arrangement();std::cerr<<"batch semantics\n";batchSemanticsCases074();std::cerr<<"source selection\n";sourceSelectionCases074();std::cerr<<"intersecting curves\n";intersectingCurves074();std::cerr<<"thin offset\n";thinOffsetCases074();std::cerr<<"variable contour\n";variableContourCases074();std::cout<<"Solid authoring074B: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
