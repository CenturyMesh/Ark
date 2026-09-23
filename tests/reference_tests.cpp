#include "ark/model.hpp"
#include "ark/references.hpp"
#include "ark/sketch.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using arkjson::Value;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void close(double actual,double expected,double tol,const char* message){check(std::abs(actual-expected)<=tol,message);}
Value parse(const char* text){Value value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid reference test JSON");return value;}
template<class Fn>void rejected(Fn&& fn,const char* message){try{fn();}catch(const std::exception&){return;}throw std::runtime_error(message);}
double volume(const ark::Document& document,size_t index){return document.evalBodies.at(index).nativeData.find("report")->find("volume_mm3")->num;}

void faceSketchHoleAndDimensionEdit(){
    ark::Document document;
    ark::Feature box;box.type=ark::FeatureType::Box;box.p={{"dx","10"},{"dy","10"},{"dz","5"}};
    const auto baseId=document.addFeature(box);
    ark::DatumPlane top;top.name="Top";top.support=parse(R"({"kind":"face","feature":1,"reference":"z_max"})");document.planes.push_back(top);
    ark::Sketch sketch;sketch.plane="Top";sketch.addCircle(5,5,1);sketch.ents.front().name="bore";
    document.setSketch("TopHole",std::move(sketch));
    ark::Feature hole;hole.type=ark::FeatureType::NativeSolid;hole.p={{"op","hole"},{"sketch","TopHole"}};hole.inputs={baseId};
    hole.nativeSpec=parse(R"({"cutter":{"kind":"profile","plane":"Top","offset":-2,"depth":3}})");
    document.addFeature(hole);
    const auto& status=document.recompute();check(status.size()==2&&status[0].ok&&status[1].ok,"face-supported hole recompute");
    check(document.evalBodies[0].nativeData.find("brep_base64")!=nullptr,"legacy box promoted from dimensions");
    close(volume(document,1),500-2*std::acos(-1.),1e-5,"face-supported blind hole volume");
    auto cap=ark::resolveGeometryReference(document,document.features[1].id,"z_max");
    close(cap.frame.m[14],5,1e-8,"bored cap remains named");
    document.features[0].p["dz"]="8";
    const auto& edited=document.recompute();check(edited[0].ok&&edited[1].ok,"dimension edit recompute");
    close(volume(document,1),800-2*std::acos(-1.),1e-5,"hole tracks edited cap");
    cap=ark::resolveGeometryReference(document,document.features[1].id,"z_max");
    close(cap.frame.m[14],8,1e-8,"cap name tracks dimension edit");
}

void midplaneAndRejections(){
    ark::Document document;ark::Feature box;box.type=ark::FeatureType::Box;box.p={{"dx","10"},{"dy","12"},{"dz","6"}};
    auto id=document.addFeature(box);document.recompute();
    ark::DatumPlane middle;middle.name="Middle";middle.support=parse(R"({"kind":"midplane","feature":1,"reference":"z_min","second_reference":"z_max"})");document.planes.push_back(middle);
    auto frame=ark::resolvePlane(document,"Middle");close(frame.m[14],3,1e-8,"parallel-face midplane");
    rejected([&]{(void)ark::resolveGeometryReference(document,id,"face.0");},"transient face index was accepted");
    Value original=*document.evalBodies[0].nativeData.find("topology");
    Value stale=original;stale.set("brep_sha256",Value::makeStr("stale"));document.evalBodies[0].nativeData.set("topology",stale);
    rejected([&]{(void)ark::resolveGeometryReference(document,id,"z_max");},"stale B-rep digest was accepted");
    Value split=original,rows=*split.find("references");
    for(auto& row:rows.arr)if(row.find("name")&&row.find("name")->str=="z_max"){
        row.set("status",Value::makeStr("split"));row.set("indices",Value::makeArr());
    }
    split.set("references",rows);document.evalBodies[0].nativeData.set("topology",split);
    rejected([&]{(void)ark::resolveGeometryReference(document,id,"z_max");},"split face was accepted");
}
void loftProvenanceAndExactCut(){
    ark::Document document;
    ark::Sketch bottom;bottom.addCircle(0,0,5);document.setSketch("Bottom",bottom);
    ark::DatumPlane upper;upper.name="Upper";upper.p={{"oz","10"}};document.planes.push_back(upper);
    ark::Sketch top;top.plane="Upper";top.addCircle(0,0,5);document.setSketch("Top",top);
    ark::Feature loft;loft.type=ark::FeatureType::Loft;loft.p={{"solid","1"},{"ruled","1"}};
    loft.nativeSpec=parse(R"({"sections":["Bottom","Top"]})");auto id=document.addFeature(loft);
    ark::Feature bore;bore.type=ark::FeatureType::NativeSolid;bore.p={{"op","hole"}};bore.inputs={id};
    bore.nativeSpec=parse(R"({"cutter":{"kind":"circle","radius":1,"depth":12,"frame":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,-1,1]}})");
    document.addFeature(bore);
    const auto& result=document.recompute();check(result.size()==2&&result[0].ok&&result[1].ok,"unannotated loft exact cut");
    check(document.evalBodies[0].nativeData.find("topology")!=nullptr,"loft lost maker provenance");
    auto originalCap=ark::resolveGeometryReference(document,id,"cap.end");
    close(originalCap.frame.m[14],10,1e-8,"loft cap end frame");
    close(volume(document,1),240*std::acos(-1.),1e-4,"loft exact bore volume");
    check(document.evalBodies[1].nativeData.find("topology")!=nullptr,"cut lacks bound output registry");
    auto boredCap=ark::resolveGeometryReference(document,document.features[1].id,"cap.end");
    close(boredCap.frame.m[14],10,1e-8,"loft cap end survived exact bore");
}
void nativeTransformExpressions(){
    ark::Document document;ark::Feature box;box.type=ark::FeatureType::NativeSolid;
    box.p={{"op","box"},{"width","10"},{"depth","4"},{"height","3"}};
    auto base=document.addFeature(box);
    ark::Feature shifted;shifted.type=ark::FeatureType::NativeSolid;shifted.inputs={base};
    shifted.p={{"op","transform"},{"tx","2"},{"tz","7"}};document.addFeature(shifted);
    const auto& result=document.recompute();check(result.size()==2&&result[0].ok&&result[1].ok,"native expression transform recompute");
    const auto& bounds=document.evalBodies[1].nativeData.find("report")->find("bounds_mm")->arr;
    close(bounds[0].num,2,1e-8,"native p tx applied");close(bounds[2].num,7,1e-8,"native p tz applied");
    auto cap=ark::resolveGeometryReference(document,document.features[1].id,"z_max");
    close(cap.frame.m[14],10,1e-8,"native transform propagated cap frame");
}
}
int main(){try{faceSketchHoleAndDimensionEdit();midplaneAndRejections();loftProvenanceAndExactCut();nativeTransformExpressions();std::cout<<"reference tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
