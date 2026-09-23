#include "ark/occt_solid.hpp"
#include "../src/occt/common.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using arkjson::Value;
namespace occt = ark::occt;
namespace {
Value parse(const char* json){Value v;if(!arkjson::parse(json,v))throw std::runtime_error("Invalid test JSON");return v;}
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void close(double actual,double expected,double tol,const char* message){check(std::abs(actual-expected)<=tol,message);}
Value result(const Value& request){return occt::modelSolidNative(request);}
Value frameRequest(const Value& solid,const char* name){Value q=Value::makeObj();q.set("input_brep",*solid.find("brep_base64"));q.set("topology",*solid.find("topology"));q.set("reference",Value::makeStr(name));return q;}
Value on(const char* op,const Value& base){Value q=Value::makeObj();q.set("op",Value::makeStr(op));q.set("input_brep",*base.find("brep_base64"));q.set("input_topology",*base.find("topology"));return q;}
double volume(const Value& solid){return solid.find("report")->find("volume_mm3")->num;}
void rejected(const Value& request,bool reference=false){try{if(reference)(void)occt::referenceFrameNative(request);else (void)result(request);}catch(const std::exception&){return;}throw std::runtime_error("Invalid request was accepted");}

void primitivesAndNames(){
    auto box=result(parse(R"({"op":"box","width":10,"depth":20,"height":5})"));
    close(volume(box),1000,1e-7,"box volume");
    auto top=occt::referenceFrameNative(frameRequest(box,"z_max"));
    close(top.find("frame")->arr[14].num,5,1e-8,"top face position");
    close(top.find("frame")->arr[10].num,1,1e-8,"top outward normal");
    auto changed=result(parse(R"({"op":"box","width":15,"depth":20,"height":7})"));
    auto changedTop=occt::referenceFrameNative(frameRequest(changed,"z_max"));
    close(changedTop.find("frame")->arr[14].num,7,1e-8,"reference survived parameter edit");
    close(volume(changed),2100,1e-7,"edited box volume");
    close(volume(result(parse(R"({"op":"cylinder","radius":2,"height":5})"))),20*std::acos(-1.),1e-6,"cylinder volume");
    close(volume(result(parse(R"({"op":"sphere","radius":2})"))),32*std::acos(-1.)/3,1e-6,"sphere volume");
    close(volume(result(parse(R"({"op":"cone","radius_bottom":3,"radius_top":1,"height":6})"))),26*std::acos(-1.),1e-6,"cone volume");
    close(volume(result(parse(R"({"op":"torus","major_radius":5,"minor_radius":1})"))),10*std::acos(-1.)*std::acos(-1.),1e-5,"torus volume");
}

void operations(){
    auto base=result(parse(R"({"op":"box","width":10,"depth":10,"height":5})"));
    auto hole=on("hole",base);
    hole.set("cutter",parse(R"({"kind":"circle","radius":1,"depth":7,"frame":[1,0,0,0,0,1,0,0,0,0,1,0,5,5,-1,1]})"));
    auto drilled=result(hole);close(volume(drilled),500-5*std::acos(-1.),1e-5,"through hole exact volume");
    auto cap=occt::referenceFrameNative(frameRequest(drilled,"z_max"));
    close(cap.find("frame")->arr[14].num,5,1e-8,"cap identity through hole");
    auto slot=on("hole",base);slot.set("cutter",parse(R"({"kind":"slot","length":6,"width":2,"depth":7,"frame":[1,0,0,0,0,1,0,0,0,0,1,0,5,5,-1,1]})"));
    close(volume(result(slot)),500-5*(8+std::acos(-1.)),1e-4,"slot exact volume");
    auto fillet=on("fillet",base);fillet.set("radius",Value::makeNum(.5));fillet.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    auto rounded=result(fillet);check(volume(rounded)<500&&volume(rounded)>490,"fillet solid volume");
    auto chamfer=on("chamfer",base);chamfer.set("distance",Value::makeNum(.5));chamfer.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    auto beveled=result(chamfer);check(volume(beveled)<500&&volume(beveled)>490,"chamfer solid volume");
    auto path=std::filesystem::temp_directory_path()/"ark-occt-solid-tests.step";
    occt::writeStep(occt::readBrep(beveled.find("brep_base64")->str),path);
    auto recovered=occt::readStep(path);std::filesystem::remove(path);
    close(occt::properties(recovered),volume(beveled),1e-5,"chamfer STEP roundtrip");
    auto translated=on("transform",base);translated.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1])"));
    auto moved=result(translated);auto movedCap=occt::referenceFrameNative(frameRequest(moved,"z_max"));
    close(movedCap.find("frame")->arr[14].num,9,1e-8,"transformed semantic cap");
    auto merged=on("union",base);merged.set("tool_brep",*moved.find("brep_base64"));merged.set("tool_topology",*moved.find("topology"));
    auto fused=result(merged);check(volume(fused)>500,"native union volume");
    auto common=on("common",base);common.set("tool_brep",*moved.find("brep_base64"));common.set("tool_topology",*moved.find("topology"));
    close(volume(result(common)),8*7*1,1e-5,"native intersection volume");
    Value unannotated=Value::makeObj();unannotated.set("op",Value::makeStr("cut"));
    unannotated.set("input_brep",*base.find("brep_base64"));unannotated.set("tool_brep",*moved.find("brep_base64"));
    auto exactWithoutNames=result(unannotated);
    close(volume(exactWithoutNames),500-56,1e-5,"unannotated exact B-rep cut");
    check(exactWithoutNames.find("topology")->find("references")->arr.empty(),"unannotated source gained guessed names");
    auto curved=result(parse(R"({"op":"extrude","depth":2,"profile":{"entities":[
      {"kind":"bezier","name":"leading","points":[[0,0],[3,-1],[7,-1],[10,0]]},
      {"kind":"line","name":"right","points":[[10,0],[10,5]]},
      {"kind":"line","name":"trailing","points":[[10,5],[0,5]]},
      {"kind":"line","name":"left","points":[[0,5],[0,0]]}
    ]}})"));
    close(volume(curved),110.2,1e-5,"Bezier profile analytic volume");
    bool hasSide=false;for(const auto& row:curved.find("topology")->find("references")->arr)
        if(row.find("name")->str=="side.leading")hasSide=row.find("status")->str=="bound"&&row.find("frame_available")&&!row.find("frame_available")->b;
    check(hasSide,"Bezier side face has exact identity and explicit frame limit");
    check(occt::referenceFrameNative(frameRequest(curved,"edge.start.leading")).find("kind")->str=="edge","sketch entity boundary curve frame");
}

void rejection(){
    auto box=result(parse(R"({"op":"box","width":10,"depth":10,"height":5})"));
    auto stale=frameRequest(box,"z_max");
    Value altered=*box.find("topology");altered.set("brep_sha256",Value::makeStr("stale"));stale.set("topology",altered);rejected(stale,true);
    auto missing=frameRequest(box,"face.3");rejected(missing,true);
    auto split=frameRequest(box,"z_max");altered=*box.find("topology");
    auto rows=*altered.find("references");for(auto& row:rows.arr)if(row.find("name")->str=="z_max"){row.set("status",Value::makeStr("split"));row.set("indices",Value::makeArr());}
    altered.set("references",rows);split.set("topology",altered);rejected(split,true);
    auto tool=result(parse(R"({"op":"box","width":2,"depth":2,"height":5})"));
    auto move=on("transform",tool);move.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,9,4,0,1])"));
    auto located=result(move);auto cut=on("cut",box);cut.set("tool_brep",*located.find("brep_base64"));cut.set("tool_topology",*located.find("topology"));
    auto sliced=result(cut);check(sliced.find("report")->find("solids")->num==1,"split-case single solid");
    auto slicedFace=frameRequest(sliced,"x_max");rejected(slicedFace,true);
    rejected(parse(R"({"op":"box","width":0,"depth":10,"height":5})"));
}
void repeatedUnionNamespaces(){
    auto housing=result(parse(R"({"op":"cylinder","radius":11,"height":15})"));
    auto bell=result(parse(R"({"op":"cylinder","radius":13,"height":8})"));
    auto bellMove=on("transform",bell);
    bellMove.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,0,0,6,1])"));
    auto raised=result(bellMove);
    auto housingUnion=on("union",housing);housingUnion.set("tool_brep",*raised.find("brep_base64"));
    housingUnion.set("tool_topology",*raised.find("topology"));
    housingUnion.set("tool_namespace",Value::makeStr("tool.19"));
    auto assembly=result(housingUnion);
    auto shaft=result(parse(R"({"op":"cylinder","radius":2.5,"height":7})"));
    auto shaftMove=on("transform",shaft);
    shaftMove.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,0,0,15,1])"));
    auto raisedShaft=result(shaftMove);
    auto finalUnion=on("union",assembly);finalUnion.set("tool_brep",*raisedShaft.find("brep_base64"));
    finalUnion.set("tool_topology",*raisedShaft.find("topology"));
    finalUnion.set("tool_namespace",Value::makeStr("tool.22"));
    auto motor=result(finalUnion);
    bool baseSplit=false,bellAxis=false,shaftAxis=false;
    for(const auto& row:motor.find("topology")->find("references")->arr){
        const auto& name=row.find("name")->str;
        if(name=="axis")baseSplit=row.find("status")->str=="split";
        if(name=="tool.19.axis")bellAxis=row.find("status")->str=="bound";
        if(name=="tool.22.axis")shaftAxis=row.find("status")->str=="bound";
    }
    check(baseSplit&&bellAxis&&shaftAxis,"stable multi-union semantic namespaces");
    auto frame=occt::referenceFrameNative(frameRequest(motor,"tool.22.axis"));
    close(frame.find("frame")->arr[10].num,1,1e-8,"shaft axis frame");
    auto noNamespace=on("union",assembly);noNamespace.set("tool_brep",*raisedShaft.find("brep_base64"));
    noNamespace.set("tool_topology",*raisedShaft.find("topology"));
    auto defaulted=result(noNamespace);bool ordinal=false;
    for(const auto& row:defaulted.find("topology")->find("references")->arr)
        if(row.find("name")->str=="tool2.axis")ordinal=true;
    check(ordinal,"direct worker union namespace determinism");
}
}
int main(){try{primitivesAndNames();operations();rejection();repeatedUnionNamespaces();std::cout<<"native solid tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
