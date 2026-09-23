#include "ark/model.hpp"
#include "ark/sketch.hpp"
#include <cmath>
#include <iostream>

using namespace ark;
using V=arkjson::Value;
static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';++failures;}}while(0)

static V support(u64 feature,const std::string& station){
    V s=V::makeObj();s.set("kind",V::makeStr("tube_path"));
    s.set("feature",V::makeNum((double)feature));s.set("station",V::makeStr(station));return s;
}
static Document tubeDocument(const std::string& angle="90",const std::string& roll="0"){
    Document d;d.setParam("feed","100");d.setParam("radius","60");
    d.setParam("station","feed+radius*pi/4");d.setParam("lift","8");
    Feature f;f.type=FeatureType::Tube;f.name="Analytic centerline";
    f.p={{"od","20"},{"wall","2"},{"tail","40"}};
    f.bends={{"feed","radius",angle,roll}};
    d.addFeature(f);d.recompute();
    DatumPlane bend;bend.name="bend";bend.support=support(1,"station");
    d.planes.push_back(bend);
    return d;
}
static Vec3 o(const Mat4& f){return f.transformPoint({0,0,0});}
static Vec3 z(const Mat4& f){return f.transformDir({0,0,1});}
int main(){
    const double s=std::sqrt(.5);
    {
        auto d=tubeDocument();CHECK(d.lastResults[0].ok);
        auto frame=resolvePlane(d,"bend");
        CHECK(o(frame).approx({100+60*s,60-60*s,0},1e-9));
        CHECK(z(frame).approx({s,s,0},1e-9));
        CHECK(frame.transformDir({1,0,0}).approx({-s,s,0},1e-9));
        CHECK(frame.transformDir({0,1,0}).approx({0,0,1},1e-9));
        DatumPlane nested;nested.name="tool";nested.parent="bend";
        nested.p={{"oz","10"},{"ox","lift"},{"ry","90"}};d.planes.push_back(nested);
        auto child=resolvePlane(d,"tool");
        CHECK(o(child).approx(o(frame)+z(frame)*10+frame.transformDir({1,0,0})*8,1e-9));
        CHECK(z(child).approx(frame.transformDir({1,0,0}),1e-9));
        d.setParam("feed","120");d.setParam("radius","80");d.recompute();
        frame=resolvePlane(d,"bend");
        CHECK(o(frame).approx({120+80*s,80-80*s,0},1e-9));
        CHECK(z(frame).approx({s,s,0},1e-9));
        d.setParam("station","feed+radius*pi/2");d.recompute();
        frame=resolvePlane(d,"bend");
        CHECK(o(frame).approx({200,80,0},1e-9));
        CHECK(z(frame).approx({0,1,0},1e-9));
    }
    {
        auto d=tubeDocument("90","90");auto frame=resolvePlane(d,"bend");
        CHECK(o(frame).approx({100+60*s,0,60-60*s},1e-9));
        CHECK(z(frame).approx({s,0,s},1e-9));
        CHECK(frame.transformDir({1,0,0}).approx({0,1,0},1e-9));
    }
    {
        auto d=tubeDocument("-90","0");auto frame=resolvePlane(d,"bend");
        CHECK(o(frame).approx({100+60*s,-60+60*s,0},1e-9));
        CHECK(z(frame).approx({s,-s,0},1e-9));
    }
    {
        auto d=tubeDocument();d.planes.push_back({"missing","absent",{}});
        bool rejected=false;try{resolvePlane(d,"missing");}catch(const std::exception& e){rejected=std::string(e.what()).find("not found")!=std::string::npos;}
        CHECK(rejected);
        d.planes.push_back({"cycle_a","cycle_b",{}});
        d.planes.push_back({"cycle_b","cycle_a",{}});
        rejected=false;try{resolvePlane(d,"cycle_a");}catch(const std::exception& e){rejected=std::string(e.what()).find("Cyclic")!=std::string::npos;}
        CHECK(rejected);
        d.planes[0].support=support(1,"100000");
        rejected=false;try{resolvePlane(d,"bend");}catch(const std::exception& e){rejected=std::string(e.what()).find("exceeds")!=std::string::npos;}
        CHECK(rejected);
    }
    std::cout<<checks<<" checks / "<<failures<<" failures\n";
    return failures?1:0;
}
