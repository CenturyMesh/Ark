#include "ark/datum_authoring.hpp"
#include "ark/sketch.hpp"
#include "ark/agent.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
namespace {
int checks=0;
void check(bool yes,const char* what){++checks;if(!yes)throw std::runtime_error(what);}
void close(Vec3 actual,Vec3 expected,const char* what){check(actual.approx(expected,1e-8),what);}
V parse(const char* text){V result;if(!arkjson::parse(text,result))throw std::runtime_error("Invalid fixture JSON");return result;}
template<class Fn>void rejects(Fn&& fn,const char* what){++checks;try{fn();}catch(const std::exception&){return;}throw std::runtime_error(what);}
DatumPlane datum(const char* name,const char* kind,const char* recipe){DatumPlane d;d.name=name;d.kind=kind;d.support=parse(recipe);return d;}
void put(Document& doc,DatumPlane d){validateDatumDefinition(d);doc.planes.push_back(std::move(d));}
Vec3 origin(const Document& d,const char* name){return resolvePlane(d,name).transformPoint({});}
Vec3 z(const Document& d,const char* name){return resolvePlane(d,name).transformDir({0,0,1});}

void analyticConstructions(){
    Document d;d.setParam("height","20");d.setParam("angle","90");
    put(d,datum("Raised","plane",R"({"kind":"offset_plane","plane":"XY","offset":"height"})"));
    close(origin(d,"Raised"),{0,0,20},"offset along plane normal");
    put(d,datum("Hinge","axis",R"({"kind":"axis_points","start":[0,0,"height"],"end":[30,0,"height"]})"));
    put(d,datum("Tilted","plane",R"({"kind":"angle_plane","plane":"Raised","axis":"Hinge","angle":"angle"})"));
    close(origin(d,"Tilted"),{0,0,20},"angle plane retains pivot");close(z(d,"Tilted"),{0,-1,0},"plane rotation about authored axis");
    d.setParam("height","35");d.setParam("angle","30");
    close(origin(d,"Tilted"),{0,0,35},"angle/offset tracks dimensions");close(z(d,"Tilted"),{0,-.5,std::sqrt(.75)},"angle expression updates orientation");
    put(d,datum("OnPlane","point",R"({"kind":"point","point":{"datum":"Tilted","local":[10,20,0]}})"));
    close(origin(d,"OnPlane"),{10,20*std::sqrt(.75),45},"local point follows offset/rotating support plane");
    put(d,datum("Three","plane",R"({"kind":"plane_three_points","a":[1,2,3],"b":[4,2,3],"c":[1,6,3]})"));
    close(origin(d,"Three"),{1,2,3},"three-point origin");close(z(d,"Three"),{0,0,1},"three-point right handed normal");
    put(d,datum("Contains","plane",R"({"kind":"plane_axis_point","axis":"Hinge","point":[0,20,35]})"));
    close(z(d,"Contains"),{0,0,1},"axis plus external point plane");
    put(d,datum("Normal","plane",R"({"kind":"plane_point_normal","axis":"Hinge","point":[5,6,7]})"));
    close(origin(d,"Normal"),{5,6,7},"normal plane origin");close(z(d,"Normal"),{1,0,0},"plane normal follows axis");
    put(d,datum("Intersection","axis",R"({"kind":"axis_planes","first":"Raised","second":"XZ"})"));
    close(origin(d,"Intersection"),{0,0,35},"non-origin plane intersection point");
    check(std::abs(z(d,"Intersection").dot({1,0,0}))>1-1e-8,"intersection axis parallel X");
    put(d,datum("P1","point",R"({"kind":"point","point":[1,2,"height"]})"));
    put(d,datum("P2","point",R"({"kind":"point","point":[1,2,"height+100"]})"));
    put(d,datum("Linked","point",R"({"kind":"point","point":{"datum":"P1"}})"));
    put(d,datum("Station","frame",R"({"kind":"between_points","start":"Linked","end":"P2","fraction":"0.25","roll":"90","x_axis":{"datum":"XY","axis":"x"}})"));
    close(origin(d,"Station"),{1,2,60},"between-points frame fraction");close(z(d,"Station"),{0,0,1},"between-points axial direction");
    close(resolvePlane(d,"Station").transformDir({1,0,0}),{0,1,0},"explicit guide and roll");
    d.setParam("height","50");close(origin(d,"Linked"),{1,2,50},"point-to-point association");close(origin(d,"Station"),{1,2,75},"station follows both points");
    check(inspectDatum(d,"Station").find("support")->dump()==d.planes.back().support.dump(),"inspection retains source recipe");
}
void sketchEndpointsAndPersistence(){
    Document d;d.setParam("x","40");Sketch s;s.addLine(1,2,40,5);s.ents[0].name="Guide";s.ents[0].expressions={"1","2","x","5"};d.setSketch("Path",s);
    put(d,datum("Endpoint","point",R"({"kind":"point","point":{"sketch":"Path","entity":"Guide","endpoint":"end"}})"));
    d.recompute();check(d.parameterError.empty(),"endpoint recompute accepted");close(origin(d,"Endpoint"),{40,5,0},"authored sketch endpoint");
    d.setParam("x","65");d.recompute();close(origin(d,"Endpoint"),{65,5,0},"endpoint tracks source expression edit");
    put(d,datum("LinkedPlane","plane",R"({"kind":"plane_point_normal","axis":{"datum":"XY","axis":"z"},"point":{"datum":"Endpoint"}})"));
    close(origin(d,"LinkedPlane"),{65,5,0},"plane follows linked endpoint point");
    Document loaded;std::string error;const bool restored=documentFromJson(documentToJson(d),loaded,&error);++checks;if(!restored)throw std::runtime_error("datum recipes save/load: "+error);loaded.recompute();close(origin(loaded,"LinkedPlane"),{65,5,0},"restored associative plane");
    loaded.setParam("x","90");loaded.recompute();close(origin(loaded,"LinkedPlane"),{90,5,0},"restored source relationship remains live");
    close(datumAnchorPoint(loaded,parse(R"({"sketch":"Path","entity":"Guide","endpoint":"start"})")),{1,2,0},"shared curve anchor API start");
    loaded.sketches[0].sk.plane="LinkedPlane";
    rejects([&]{resolvePlane(loaded,"LinkedPlane");},"datum/sketch cross-resource cycle rejected");
    loaded.sketches[0].sk.plane="XY";loaded.sketches[0].sk.ents[0].name="Renamed";
    rejects([&]{resolvePlane(loaded,"Endpoint");},"missing endpoint source rejected without nearest geometry fallback");
}
void invalidAndAtomic(){
    Document d;put(d,datum("P","point",R"({"kind":"point","point":[0,0,0]})"));
    auto before=documentToJson(d);auto valid=datum("Axis","axis",R"({"kind":"axis_points","start":"P","end":[1,0,0]})");
    rejects([&]{stageDatumSet(d,valid,4,5);},"stale datum stage rejected");check(documentToJson(d)==before,"stale edit leaves authoring intact");
    auto candidate=stageDatumSet(d,valid,5,5);check(candidate.planes.size()==2&&d.planes.size()==1,"staging returns independent candidate");
    rejects([&]{stageDatumRemove(candidate,"P",5,5);},"dependent datum deletion rejected");check(candidate.planes.size()==2,"failed deletion preserves caller");
    for(const char* recipe:{R"({"kind":"axis_points","start":[0,0,0],"end":[0,0,0]})",R"({"kind":"axis_planes","first":"XY","second":"XY"})"}){
        auto invalid=datum("Invalid","axis",recipe);rejects([&]{stageDatumSet(d,invalid,5,5);},"degenerate axis stage rejected");check(documentToJson(d)==before,"failed geometry publication remains atomic");
    }
    auto collinear=datum("Collinear","plane",R"({"kind":"plane_three_points","a":[0,0,0],"b":[1,1,1],"c":[2,2,2]})");
    rejects([&]{stageDatumSet(d,collinear,5,5);},"collinear three points rejected");
    auto off=datum("OffAxis","plane",R"({"kind":"angle_plane","plane":"XY","axis":{"datum":"XY","axis":"z"},"angle":30})");
    rejects([&]{stageDatumSet(d,off,5,5);},"angle plane pivot outside plane direction rejected");
    auto bad=datum("Bad","point",R"({"kind":"point","point":[0,0,"nan"],"ignored":true})");
    rejects([&]{validateDatumDefinition(bad);},"unknown recipe fields rejected");
    bad.support=parse(R"({"kind":"point","point":[0,0,"1/0"]})");rejects([&]{stageDatumSet(d,bad,5,5);},"nonfinite datum expressions rejected");
    bad.support=parse(R"({"kind":"point","point":{"datum":"Bad"}})");rejects([&]{stageDatumSet(d,bad,5,5);},"self cycle rejected");
    auto far=datum("TooFar","point",R"({"kind":"point","point":[10000000,0,0]})");far.p["ox"]="1";rejects([&]{stageDatumSet(d,far,5,5);},"composed frame respects finite coordinate bounds");
    auto fraction=datum("Fraction","frame",R"({"kind":"between_points","start":[0,0,0],"end":[1,0,0],"fraction":1.1})");rejects([&]{stageDatumSet(d,fraction,5,5);},"out-of-range station rejected");
    fraction.support=parse(R"({"kind":"between_points","start":[0,0,0],"end":[1,0,0],"x_axis":{"datum":"XY","axis":"x"}})");rejects([&]{stageDatumSet(d,fraction,5,5);},"parallel guide rejected explicitly");
    auto malformed=datum("Malformed","point",R"({"kind":"point","point":{"feature":1,"reference":"edge","fraction":0.5,"endpoint":"end"}})");rejects([&]{validateDatumDefinition(malformed);},"conflicting fraction/endpoint rejected");
}
#ifdef ARK_DATUM_NATIVE_TESTS
void nativeEndpointAndSourceLineage(){
    Document d;Feature source;source.type=FeatureType::Curve;source.name="Spatial path";source.nativeSpec=parse(R"({"start":[0,0,0],"segments":[{"kind":"line","to":[10,20,30]}]})");source.p={{"op","curve"}};
    const auto id=d.addFeature(source);d.recompute();check(d.lastResults.size()==1&&d.lastResults[0].ok,"native curve fixture evaluates");
    auto support=parse(R"({"kind":"point","point":{"feature":"Spatial path","reference":"edge.0","endpoint":"end"}})");bindDatumSupportProducers(d,support);
    check(support.find("point")->find("feature")->num==(double)id,"nested producer name binds stable ID");
    DatumPlane endpoint;endpoint.name="CurveEnd";endpoint.kind="point";endpoint.support=support;put(d,endpoint);
    close(origin(d,"CurveEnd"),{10,20,30},"exact native endpoint anchor");
    d.features[0].nativeSpec=parse(R"({"start":[0,0,0],"segments":[{"kind":"line","to":[20,40,60]}]})");d.recompute();close(origin(d,"CurveEnd"),{20,40,60},"exact endpoint follows authored curve edit");
    V anchor=*endpoint.support.find("point");anchor.set("reference",V::makeStr("edge.999"));endpoint.support.set("point",anchor);rejects([&]{stageDatumSet(d,endpoint,7,7);},"missing exact curve reference rejected");
}
#endif
void sourceOrdering(){
    Document d;Feature consumer;consumer.type=FeatureType::Curve;consumer.name="Consumer";consumer.nativeSpec=parse(R"({"start_ref":{"datum":"NestedPoint"},"segments":[{"kind":"line","to":[10,0,0]}]})");d.addFeature(consumer);
    Feature producer;producer.type=FeatureType::Box;producer.name="Future producer";producer.p={{"dx","10"},{"dy","10"},{"dz","5"}};const auto id=d.addFeature(producer);
    put(d,datum("SourcePoint","point",R"({"kind":"point","point":{"feature":2,"reference":"edge.x.ymin.zmin","endpoint":"end"}})"));
    put(d,datum("NestedPoint","point",R"({"kind":"point","point":"SourcePoint"})"));d.recompute();
    check(d.lastResults.size()==2&&!d.lastResults[0].ok&&d.lastResults[0].error.find("Forward or cyclic associative feature dependency")!=std::string::npos,"nested curve point producer must precede consumer");
    d.features[0].nativeSpec=parse(R"({"start_ref":"SelfPoint","segments":[{"kind":"line","to":[10,0,0]}]})");put(d,datum("SelfPoint","point",R"({"kind":"point","point":{"feature":1,"reference":"edge.0","endpoint":"start"}})"));d.recompute();
    check(!d.lastResults[0].ok&&d.lastResults[0].error.find("Forward or cyclic associative feature dependency")!=std::string::npos,"curve-to-point-to-self cycle rejected before native evaluation");
    d.features[0].nativeSpec=parse(R"({"start_ref":"NestedPoint","segments":[{"kind":"line","to":[10,0,0]}]})");d.find(id)->suppressed=true;d.recompute();
    check(!d.lastResults[0].ok&&d.lastResults[0].error.find("suppressed")!=std::string::npos,"suppressed nested datum producer rejected");
}
void sessionGuardAndRollback(){
    AgentSession session;auto result=session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"plane.set","name":"P","kind":"point","support":{"kind":"point","point":[1,2,3]}},{"op":"plane.set","name":"N","kind":"plane","support":{"kind":"plane_point_normal","axis":{"datum":"XY","axis":"z"},"point":"P"}}]})"));
    check(!result.find("isError")->b,"atomic session datum batch");auto revision=session.revision;auto before=documentToJson(session.document);
    result=session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"plane.remove","name":"P"}]})"));
    check(result.find("isError")->b&&session.revision==revision&&documentToJson(session.document)==before,"session stale expected_revision preserves datums");
    V request=parse(R"({"commands":[{"op":"plane.remove","name":"P"}]})");request.set("expected_revision",V::makeNum((double)revision));result=session.call("doc_apply",request);
    check(result.find("isError")->b&&session.revision==revision&&documentToJson(session.document)==before,"dependent point removal atomically rejected");
}
}
int main(){try{analyticConstructions();sketchEndpointsAndPersistence();invalidAndAtomic();sourceOrdering();
#ifdef ARK_DATUM_NATIVE_TESTS
nativeEndpointAndSourceLineage();
#endif
sessionGuardAndRollback();std::cout<<checks<<" datum authoring checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"Datum authoring failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
