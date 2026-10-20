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
template<class Fn>void rejectsWith(Fn&& fn,const char* fragment,const char* what){++checks;try{fn();}catch(const std::exception& error){if(std::string(error.what()).find(fragment)==std::string::npos)throw std::runtime_error(std::string(what)+": "+error.what());return;}throw std::runtime_error(what);}
void setVersion(V& root,double version){for(auto& kv:root.obj)if(kv.first=="version"){kv.second.num=version;return;}throw std::runtime_error("missing version");}
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
void originBisectorAndSelection(){
    Document empty;V emptyJson;check(arkjson::parse(documentToJson(empty),emptyJson),"empty document json");check(emptyJson.find("version")&&emptyJson.find("version")->num==1,"empty document stays version 1");
    Document builtins;close(origin(builtins,"Origin"),{0,0,0},"Origin is world zero");check(inspectDatum(builtins,"Origin").find("kind")->str=="point","Origin kind is point");
    close(z(builtins,"XY"),{0,0,1},"XY normal +Z");close(z(builtins,"XZ"),{0,-1,0},"XZ normal -Y");close(z(builtins,"YZ"),{1,0,0},"YZ normal +X");
    auto originReport=inspectDatum(builtins,"Origin");check(originReport.find("role")->str=="starting_point"&&originReport.find("builtin")->b&&originReport.find("immutable")->b,"Origin inspect role");
    auto xyReport=inspectDatum(builtins,"XY");check(xyReport.find("kind")->str=="plane"&&xyReport.find("role")->str=="standard_plane"&&xyReport.find("builtin")->b,"XY inspect role");
    Document d;put(d,datum("Angle","plane",R"({"kind":"bisector","first":"XY","second":"XZ"})"));
    close(origin(d,"Angle"),{0,0,0},"intersecting bisector origin");close(z(d,"Angle"),Vec3{0,-1,1}.normalized(),"sum angle bisector");
    put(d,datum("AngleDiff","plane",R"({"kind":"bisector","first":"XY","second":"XZ","side":"difference"})"));
    close(z(d,"AngleDiff"),Vec3{0,1,1}.normalized(),"difference angle bisector");
    put(d,datum("Raised","plane",R"({"kind":"offset_plane","plane":"XY","offset":20})"));
    put(d,datum("Mid","plane",R"({"kind":"bisector","first":"XY","second":"Raised"})"));
    close(origin(d,"Mid"),{0,0,10},"parallel sum origin");close(z(d,"Mid"),{0,0,1},"parallel sum keeps first normal");
    put(d,datum("Flip","plane",R"({"kind":"bisector","first":"XY","second":"Raised","side":"difference"})"));
    close(origin(d,"Flip"),{0,0,10},"parallel difference keeps origin");close(z(d,"Flip"),{0,0,-1},"parallel difference flips first normal");
    put(d,datum("Down","axis",R"({"kind":"axis_points","start":[0,0,1],"end":[0,0,0]})"));
    put(d,datum("Ceiling","plane",R"({"kind":"plane_point_normal","point":[0,0,20],"axis":"Down"})"));
    put(d,datum("Opposed","plane",R"({"kind":"bisector","first":"XY","second":"Ceiling"})"));
    close(origin(d,"Opposed"),{0,0,10},"opposed parallel midpoint");close(z(d,"Opposed"),{0,0,1},"opposed parallel keeps first normal");
    put(d,datum("Same","plane",R"({"kind":"bisector","first":"XY","second":"XY"})"));
    close(origin(d,"Same"),{0,0,0},"coincident bisector is the source plane");close(z(d,"Same"),{0,0,1},"omitted side defaults to sum");
    rejectsWith([&]{stageDatumSet(Document{},datum("FromOrigin","plane",R"({"kind":"bisector","first":"Origin","second":"XY"})"),0,0);},"Plane support requires a plane or frame","Origin is not a bisector anchor");
    rejectsWith([&]{validateDatumDefinition(datum("BadSide","plane",R"({"kind":"bisector","first":"XY","second":"XZ","side":"left"})"));},"Bisector side must be sum or difference","bad bisector side");
    rejectsWith([&]{validateDatumDefinition(datum("Missing","plane",R"({"kind":"bisector","first":"XY"})"));},"Missing datum support field: second","bisector needs second");
    validateDatumDefinition(datum("Origin","point",R"({"kind":"point","point":[0,0,0]})"));
    rejectsWith([&]{stageDatumSet(Document{},datum("Origin","point",R"({"kind":"point","point":[0,0,0]})"),0,0);},"Built-in datums are immutable","New Origin creation rejected with document context");
    V saved;check(arkjson::parse(documentToJson(d),saved),"bisector document json");check(saved.find("version")->num==46,"stored bisector selects version 46");
    Document loaded;std::string error;check(documentFromJson(saved.dump(),loaded,&error),error.c_str());close(origin(loaded,"Mid"),{0,0,10},"reloaded bisector geometry");close(z(loaded,"Flip"),{0,0,-1},"reloaded difference normal");
    setVersion(saved,45);check(!documentFromJson(saved.dump(),loaded,&error),"bisector below version 46 rejected");check(error.find("Bisector planes require document version 46")!=std::string::npos,error.c_str());
    Document offset;put(offset,datum("Raised","plane",R"({"kind":"offset_plane","plane":"XY","offset":20})"));V older;check(arkjson::parse(documentToJson(offset),older),"offset json");check(older.find("version")->num==34,"offset plane stays version 34");
    setVersion(older,45);Document promoted;check(documentFromJson(older.dump(),promoted,&error),error.c_str());setVersion(older,46);check(documentFromJson(older.dump(),promoted,&error),error.c_str());setVersion(older,47);check(documentFromJson(older.dump(),promoted,&error),error.c_str());setVersion(older,48);check(!documentFromJson(older.dump(),promoted,&error),"version 48 rejected");check(error.find("unsupported document version")!=std::string::npos,error.c_str());
    AgentSession session;auto denied=session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"plane.set","name":"Origin","kind":"point","support":{"kind":"point","point":[0,0,0]}}]})"));
    check(denied.find("isError")->b&&denied.find("structuredContent")->dump().find("Built-in datums are immutable")!=std::string::npos,"session cannot author Origin");
    auto selected=session.call("selection_set",parse(R"({"plane":"Origin","expected_revision":0})"));check(!selected.find("isError")->b,"Origin selection");
    auto body=selected.find("structuredContent");auto pick=body->find("selection");check(pick->find("feature")->num==0&&pick->find("kind")->str=="point"&&pick->find("datum")->b&&pick->find("builtin")->b,"Origin selection payload");
    check(!body->find("view_state_applied")->b&&body->find("document_unchanged")->b&&body->find("revision")->num==0&&session.revision==0,"headless datum selection does not edit");
    selected=session.call("selection_set",parse(R"({"plane":"XZ","expected_revision":0})"));pick=selected.find("structuredContent")->find("selection");check(pick->find("kind")->str=="plane"&&pick->find("builtin")->b,"XZ selection payload");
    check(session.call("selection_set",parse(R"({"expected_revision":0})")).find("isError")->b,"plane selection needs a name");
    check(session.call("selection_set",parse(R"({"plane":"XY","id":1,"reference":"face.0","expected_revision":0})")).find("structuredContent")->dump().find("Plane selection cannot mix feature references")!=std::string::npos,"mixed plane selection rejected");
    auto created=session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"plane.set","name":"Mark","kind":"point","support":{"kind":"point","point":[1,2,3]}}]})"));check(!created.find("isError")->b,"user datum published");
    V request=V::makeObj();request.set("plane",V::makeStr("Mark"));request.set("expected_revision",V::makeNum((double)session.revision));selected=session.call("selection_set",request);pick=selected.find("structuredContent")->find("selection");
    check(!selected.find("isError")->b&&pick->find("kind")->str=="point"&&!pick->find("builtin")->b,"user datum selection");
    auto inspected=session.call("plane_inspect",parse(R"({"name":"Origin"})"));check(!inspected.find("isError")->b&&inspected.find("structuredContent")->find("kind")->str=="point","plane_inspect keeps Origin kind point");
}
void legacyAuthoredOrigin(){
    Document legacy;DatumPlane authored;authored.name="Origin";authored.p={{"ox","12"},{"oy","-4"},{"oz","6"},{"rz","90"}};put(legacy,authored);
    DatumPlane child;child.name="LegacyChild";child.parent="Origin";child.p={{"ox","2"},{"oz","3"}};put(legacy,child);
    Sketch sketch;sketch.plane="Origin";SketchEnt point;point.name="RetainedPoint";point.kind=SketchEnt::Pnt;point.dof={1,2};sketch.ents.push_back(point);legacy.setSketch("LegacySketch",sketch);
    legacy.recompute();check(legacy.parameterError.empty()&&legacy.sketches[0].error.empty(),"legacy Origin dependencies evaluate");
    close(origin(legacy,"Origin"),{12,-4,6},"authored Origin keeps its nonzero transform");close(resolvePlane(legacy,"Origin").transformDir({1,0,0}),{0,1,0},"authored Origin keeps its rotation");close(origin(legacy,"LegacyChild"),{12,-2,9},"child resolves authored Origin rather than builtin fallback");
    const auto report=inspectDatum(legacy,"Origin");check(!report.find("builtin")->b&&!report.find("immutable")->b&&report.find("kind")->str=="plane"&&report.find("role")->str=="legacy_authored_datum","legacy Origin inspection identifies editable authored plane");
    V source;check(arkjson::parse(documentToJson(legacy,false,false),source),"legacy Origin source serializes");check(source.find("version")->num==5,"legacy datum and sketch keep original source format");std::string error;
    for(int version:{5,34,45,46}){auto input=source;setVersion(input,version);Document reopened;check(documentFromJson(input.dump(),reopened,&error),error.c_str());close(origin(reopened,"Origin"),{12,-4,6},"authored Origin loads at legacy and current source boundaries");close(origin(reopened,"LegacyChild"),{12,-2,9},"dependent frame is preserved at legacy and current source boundaries");check(reopened.sketches[0].sk.plane=="Origin","sketch retains authored Origin identity across source boundaries");}
    auto promoted=legacy.authoringCopy();put(promoted,datum("LegacyBisector","plane",R"({"kind":"bisector","first":"Origin","second":"XY"})"));V latest;check(arkjson::parse(documentToJson(promoted,false,false),latest),"legacy Origin with new bisector serializes");check(latest.find("version")->num==46,"retained Origin coexists with actual source46 semantics");Document reloaded;check(documentFromJson(latest.dump(),reloaded,&error),error.c_str());close(origin(reloaded,"Origin"),{12,-4,6},"source46 reload preserves authored Origin");close(origin(reloaded,"LegacyChild"),{12,-2,9},"source46 reload preserves Origin-dependent frame");close(origin(reloaded,"LegacyBisector"),origin(promoted,"LegacyBisector"),"namedKind uses authored Origin as a plane for new support recipes");
    rejectsWith([&]{stageDatumRemove(legacy,"Origin",0,0);},"remaining consumers","standalone datum removal cannot silently rebind dependents");
    AgentSession session;session.document=legacy.authoringCopy();session.revision=7;
    auto selected=session.call("selection_set",parse(R"({"plane":"Origin","expected_revision":7})"));check(!selected.find("isError")->b,"legacy Origin selection succeeds");auto pick=selected.find("structuredContent")->find("selection");check(!pick->find("builtin")->b&&pick->find("kind")->str=="plane","selection uses authored Origin kind and identity");
    auto edited=session.call("doc_apply",parse(R"({"expected_revision":7,"commands":[{"op":"plane.set","name":"Origin","p":{"ox":"20"}}]})"));check(!edited.find("isError")->b&&session.revision==8,"existing authored Origin remains editable");close(origin(session.document,"LegacyChild"),{20,-2,9},"Origin edit moves existing dependent frame explicitly");
    auto undone=session.call("doc_undo",V::makeObj());check(!undone.find("isError")->b,"authored Origin edit has a working undo");close(origin(session.document,"Origin"),{12,-4,6},"undo restores original authored Origin");close(origin(session.document,"LegacyChild"),{12,-2,9},"undo restores dependent frame");
    auto before=documentToJson(session.document);const auto revision=session.revision;V remove=parse(R"({"commands":[{"op":"plane.remove","name":"Origin"}]})");remove.set("expected_revision",V::makeNum((double)revision));auto rejected=session.call("doc_apply",remove);check(rejected.find("isError")->b&&session.revision==revision&&documentToJson(session.document)==before,"deleting consumed Origin rejects atomically even though builtin fallback exists");
    remove=parse(R"({"commands":[{"op":"plane.remove","name":"Origin"},{"op":"plane.remove","name":"LegacyChild"},{"op":"sketch.remove","name":"LegacySketch"}]})");remove.set("expected_revision",V::makeNum((double)revision));auto cleared=session.call("doc_apply",remove);check(!cleared.find("isError")->b,"one atomic batch may remove Origin and all its consumers");close(origin(session.document,"Origin"),{0,0,0},"builtin zero is available only after explicit unconsumed authored removal");check(inspectDatum(session.document,"Origin").find("builtin")->b,"fallback inspection identifies builtin after authored removal");
    auto unconsumed=Document{};unconsumed.planes.push_back(authored);auto removed=stageDatumRemove(unconsumed,"Origin",0,0);check(removed.planes.empty()&&inspectDatum(removed,"Origin").find("immutable")->b,"unconsumed retained Origin may be removed with immutable fallback");
    auto modified=authored;modified.p["ox"]="30";auto staged=stageDatumSet(unconsumed,modified,0,0);close(origin(staged,"Origin"),{30,-4,6},"context-aware stage allows existing Origin edits");rejectsWith([&]{stageDatumSet(Document{},authored,0,0);},"Built-in datums are immutable","context-aware stage rejects new Origin creation");
    auto pointOrigin=Document{};put(pointOrigin,datum("Origin","point",R"({"kind":"point","point":[3,4,5]})"));close(origin(pointOrigin,"Origin"),{3,4,5},"legacy authored point Origin also takes precedence");check(inspectDatum(pointOrigin,"Origin").find("kind")->str=="point"&&!inspectDatum(pointOrigin,"Origin").find("builtin")->b,"authored point remains distinct from builtin point");
    for(const char* recipe:{R"({"kind":"offset_plane","plane":"Origin","offset":2})",R"({"kind":"point","point":"Origin"})",R"({"kind":"point","point":{"datum":"Origin","local":[1,2,3]}})"}){Document before;before.planes.push_back(authored);auto after=before.authoringCopy();after.planes.clear();DatumPlane consumer;consumer.name="Consumer";consumer.support=parse(recipe);after.planes.push_back(consumer);rejectsWith([&]{validateDatumTransition(before,after);},"remaining consumers","typed datum anchors retain their authored Origin identity");}
    for(const char* selector:{R"({"plane":"Origin"})",R"({"start_ref":"Origin"})",R"({"segments":[{"to_ref":{"datum":"Origin"}}]})",R"({"constraints":[{"type":"on_plane","plane":"Origin"}]})"}){Document before;before.planes.push_back(authored);Feature consumer;consumer.name="Geometry consumer";consumer.type=FeatureType::Curve;consumer.nativeSpec=parse(selector);before.addFeature(consumer);auto after=before.authoringCopy();after.planes.clear();rejectsWith([&]{validateDatumTransition(before,after);},"remaining consumers","retained native source selectors cannot silently rebind");}
    Document labelOnly;labelOnly.planes.push_back(authored);Feature label;label.type=FeatureType::Box;label.name="Origin";label.part=parse(R"({"description":"Origin","source_url":"https://example.com/Origin"})");labelOnly.addFeature(label);auto candidate=labelOnly.authoringCopy();candidate.planes.clear();validateDatumTransition(labelOnly,candidate);check(candidate.features.size()==1,"feature labels and sourcing text do not manufacture Origin dependencies");
}
}
int main(){try{analyticConstructions();sketchEndpointsAndPersistence();invalidAndAtomic();sourceOrdering();
#ifdef ARK_DATUM_NATIVE_TESTS
nativeEndpointAndSourceLineage();
#endif
sessionGuardAndRollback();originBisectorAndSelection();legacyAuthoredOrigin();std::cout<<checks<<" datum authoring checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"Datum authoring failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
