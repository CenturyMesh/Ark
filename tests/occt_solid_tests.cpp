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
void finishingFeatures(){
    const double pi=std::acos(-1.);
    auto box=result(parse(R"({"op":"box","width":10,"depth":10,"height":5})"));
    auto fillet=on("fillet",box);fillet.set("radius",Value::makeNum(0.5));fillet.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    const double oneEdge=500-2.5*(1-pi/4);
    close(volume(result(fillet)),oneEdge,1e-4,"single fillet volume");
    auto convex=on("fillet",box);convex.set("radius",Value::makeNum(0.5));convex.set("selection",Value::makeStr("convex"));convex.set("min_angle",Value::makeNum(15));
    const double allEdges=500-4*(10+10+5)*0.25*(1-pi/4)+8*0.125*(2-7*pi/12);
    close(volume(result(convex)),allEdges,1e-3,"convex fillet volume");
    auto chamfer=on("chamfer",box);chamfer.set("distance",Value::makeNum(0.5));chamfer.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    close(volume(result(chamfer)),500-1.25,1e-4,"single chamfer volume");
    auto pattern=on("linear_pattern",box);pattern.set("count",Value::makeNum(3));pattern.set("dx",Value::makeNum(20));pattern.set("fuse",Value::makeNum(0));
    auto copies=result(pattern);
    close(volume(copies),1500,1e-4,"disjoint linear pattern volume");
    check(copies.find("report")->find("solids")->num==3,"linear pattern keeps three solids");
    auto placed=on("transform",box);placed.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,5,5,2,1])"));
    auto interior=result(placed);auto pair=on("linear_pattern",interior);pair.set("count",Value::makeNum(2));pair.set("dx",Value::makeNum(15));pair.set("fuse",Value::makeNum(0));
    auto tools=result(pair);auto cover=result(parse(R"({"op":"box","width":40,"depth":20,"height":15})"));
    auto cut=on("cut",cover);cut.set("tool_brep",*tools.find("brep_base64"));cut.set("tool_topology",*tools.find("topology"));
    close(volume(result(cut)),12000-1000,1e-3,"compound pattern cuts one solid");
    auto moved=on("transform",result(parse(R"({"op":"cylinder","radius":1,"height":2})")));
    moved.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,20,0,0,1])"));
    auto orbit=result(moved);auto circular=on("circular_pattern",orbit);circular.set("count",Value::makeNum(4));circular.set("step",Value::makeNum(90));circular.set("axis",Value::makeStr("Z"));circular.set("fuse",Value::makeNum(0));
    auto ring=result(circular);close(volume(ring),8*pi,1e-4,"circular pattern volume");check(ring.find("report")->find("solids")->num==4,"circular pattern count");
    auto mesh=box.find("mesh");check(mesh&&mesh->find("face_spans")&&mesh->find("face_spans")->arr.size()==6,"box publishes six face spans");
    auto edgePolylines=mesh->find("edges");check(edgePolylines&&!edgePolylines->arr.empty(),"box publishes edge polylines");
    int namedEdges=0;bool classified=false;
    for(const auto& row:box.find("topology")->find("references")->arr){if(row.find("kind")->str!="edge"||row.find("status")->str!="bound")continue;
        int index=(int)row.find("indices")->arr[0].num;check(index>=0&&index<(int)edgePolylines->arr.size(),"named edge index addresses a polyline");
        const auto& poly=edgePolylines->arr[(size_t)index];check(poly.arr.size()>=6&&poly.arr.size()%3==0,"edge polyline has points");
        if(row.find("name")->str=="edge.x.ymin.zmin"){classified=true;auto coord=[&](int point,int axis){return poly.arr[(size_t)(point*3+axis)].num;};int last=(int)poly.arr.size()/3-1;
            bool forward=std::abs(coord(0,0))<1e-6&&std::abs(coord(0,1))<1e-6&&std::abs(coord(0,2))<1e-6&&std::abs(coord(last,0)-10)<1e-6&&std::abs(coord(last,1))<1e-6&&std::abs(coord(last,2))<1e-6;
            bool reverse=std::abs(coord(last,0))<1e-6&&std::abs(coord(last,1))<1e-6&&std::abs(coord(last,2))<1e-6&&std::abs(coord(0,0)-10)<1e-6&&std::abs(coord(0,1))<1e-6&&std::abs(coord(0,2))<1e-6;
            check(forward||reverse,"classified edge follows the box corner");}
        ++namedEdges;}
    check(namedEdges==12&&classified,"box publishes twelve named edges");
    int triangles=0;for(const auto& span:mesh->find("face_spans")->arr)triangles+=(int)span.arr[1].num;
    check(triangles*3==(int)mesh->find("idx")->arr.size(),"face spans cover the display mesh");
    auto mirror=on("mirror",box);mirror.set("plane",Value::makeStr("YZ"));mirror.set("fuse",Value::makeNum(0));
    auto mirrored=result(mirror);close(volume(mirrored),1000,1e-4,"mirror volume");check(mirrored.find("report")->find("solids")->num==2,"mirror keeps both solids");
    auto faceMirror=on("mirror",box);faceMirror.set("face",Value::makeStr("z_max"));faceMirror.set("fuse",Value::makeNum(0));
    auto acrossFace=result(faceMirror);close(volume(acrossFace),1000,1e-4,"planar face mirror volume");
    check(acrossFace.find("report")->find("solids")->num==2,"face mirror keeps both solids");
    close(acrossFace.find("report")->find("bounds_mm")->arr[5].num,10,1e-4,"face mirror reaches the reflected top");
    auto offset=on("mirror",box);offset.set("normal",parse("[0,0,1]"));offset.set("origin",parse("[0,0,20]"));offset.set("fuse",Value::makeNum(0));
    auto raised=result(offset);close(volume(raised),1000,1e-4,"offset plane mirror volume");
    close(raised.find("report")->find("bounds_mm")->arr[5].num,40,1e-3,"offset mirror reaches z 40");
    close(raised.find("report")->find("bounds_mm")->arr[2].num,0,1e-4,"offset mirror keeps the seed");
    auto cube=result(parse(R"({"op":"box","width":10,"depth":10,"height":10})"));
    auto draft=on("draft",cube);draft.set("angle",Value::makeNum(5));draft.set("direction",Value::makeStr("Z"));draft.set("neutral",Value::makeStr("XY"));draft.set("faces",parse(R"(["x_max"])"));
    close(volume(result(draft)),1000-500*std::tan(5*pi/180),1e-3,"drafted face volume");
    auto curved=result(parse(R"({"op":"extrude","depth":2,"profile":{"entities":[
      {"kind":"bezier","name":"leading","points":[[0,0],[3,-1],[7,-1],[10,0]]},
      {"kind":"line","name":"right","points":[[10,0],[10,5]]},
      {"kind":"line","name":"trailing","points":[[10,5],[0,5]]},
      {"kind":"line","name":"left","points":[[0,5],[0,0]]}
    ]}})"));
    auto freeform=on("draft",curved);freeform.set("angle",Value::makeNum(5));freeform.set("direction",Value::makeStr("Z"));freeform.set("neutral",Value::makeStr("XY"));freeform.set("faces",parse(R"(["side.leading"])"));
    rejected(freeform);
    auto curvedMirror=on("mirror",curved);curvedMirror.set("face",Value::makeStr("side.leading"));curvedMirror.set("fuse",Value::makeNum(0));
    rejected(curvedMirror);
    auto steep=on("fillet",box);steep.set("selection",Value::makeStr("sharp"));steep.set("min_angle",Value::makeNum(179));steep.set("radius",Value::makeNum(0.2));
    rejected(steep);
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
int main(){try{primitivesAndNames();operations();rejection();finishingFeatures();repeatedUnionNamespaces();std::cout<<"native solid tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
