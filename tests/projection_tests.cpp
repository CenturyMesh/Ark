#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "ark/sketch.hpp"
#include "ark/occt_modeling.hpp"
#include "common.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <chrono>
#include <stdexcept>
using V=arkjson::Value;
struct PrivateProjectionDirectory {
    std::filesystem::path parent,path;
    PrivateProjectionDirectory(){parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());path=parent/("ark-projection-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));if(!std::filesystem::create_directory(path))throw std::runtime_error("Projection test directory already exists");}
    ~PrivateProjectionDirectory(){std::error_code error;auto resolved=std::filesystem::weakly_canonical(path,error);if(!error&&resolved.parent_path()==parent&&!std::filesystem::is_symlink(path,error))std::filesystem::remove_all(resolved,error);}
};
static int checks=0;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);++checks;}
V parse(const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("Test JSON invalid");return v;}
V accepted(const V& reply){if(auto error=reply.find("isError");error&&error->b)throw std::runtime_error(reply.dump());return *reply.find("structuredContent");}
V apply(ark::AgentSession& session,const char* commands){V args=V::makeObj();args.set("commands",parse(commands));args.set("expected_revision",V::makeNum(session.revision));return accepted(session.call("doc_apply",args));}
void rejected(ark::AgentSession& session,const char* commands){auto before=ark::documentToJson(session.document);auto revision=session.revision;V args=V::makeObj();args.set("commands",parse(commands));args.set("expected_revision",V::makeNum(session.revision));auto result=session.call("doc_apply",args);check(result.find("isError")&&result.find("isError")->b,"Projection mutation must reject");check(before==ark::documentToJson(session.document)&&revision==session.revision,"Rejected projection preserves document and revision");}
const ark::Body& body(const ark::Document& doc,ark::u64 id){for(const auto& b:doc.evalBodies)if(b.id==id)return b;throw std::runtime_error("Missing body");}
double volume(const ark::Document& doc,ark::u64 id){return body(doc,id).nativeData.find("report")->find("volume_mm3")->num;}
void closeValue(double actual,double expected,double tol,const char* message){if(std::abs(actual-expected)>tol)std::cerr<<message<<" actual="<<std::setprecision(16)<<actual<<" expected="<<expected<<" delta="<<(actual-expected)<<"\n";check(std::abs(actual-expected)<=tol,message);}
int main(){try{
    ark::AgentSession s;
    apply(s,R"([{"op":"param.set","name":"R","expr":"100"},{"op":"feature.add","type":"native_solid","name":"Round","native":{"op":"cylinder"},"p":{"radius":"R","height":"10"}},{"op":"sketch.set","name":"Projected","sketch":{"plane":"XY","entities":[{"kind":"project","name":"rim","feature":"Round","reference":"cap.end","mode":"normal"}]}},{"op":"feature.add","type":"native_solid","name":"Disk","native":{"op":"extrude"},"p":{"sketch":"Projected","depth":"2"}}])");
    auto* sketch=s.document.findSketch("Projected");check(sketch&&sketch->sk.ents[0].projectionSamples.size()>65,"Large circle preview does not hit authored 64-segment cap");
    check(sketch->sk.ents[0].dof.size()==4,"Projection preview does not become authored coordinates");
    auto exact=ark::occt::readBrep(sketch->sk.ents[0].projectionBrep);auto edges=ark::occt::shapes(exact,TopAbs_EDGE);check(edges.size()==1,"Circle projection is one exact edge, not polygon segments");
    BRepAdaptor_Curve curve(TopoDS::Edge(edges[0]));check(curve.GetType()==GeomAbs_BSplineCurve&&curve.IsRational(),"Projected circle remains an exact rational curve");
    for(int i=0;i<=40;++i){auto point=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/40.);closeValue(std::hypot(point.X(),point.Y()),100,1e-7,"Exact rational circle point lies on source radius");}
    closeValue(volume(s.document,2),2*ark::K_PI*100*100,1e-4,"Exact circle extrusion has analytical volume");
    auto rimReference=accepted(s.call("reference_inspect",parse(R"({"id":1,"reference":"cap.end.rim"})")));check(rimReference.find("kind")&&rimReference.find("kind")->str=="edge","Cylinder producer publishes its unique cap rim edge");
    auto rimProjection=ark::projectGeometry(s.document,1,"cap.end.rim","XY");check(rimProjection.find("report")->find("projection_exact")->b,"Published circular rim supports exact 2D edge projection");
    auto mass=accepted(s.call("geometry_query",parse(R"({"id":2,"queries":[{"action":"properties","density_g_cm3":1}]})")));
    closeValue(mass.find("results")->arr[0].find("mass_g")->num,2*ark::K_PI*100*100/1000,1e-7,"Curved native mass agrees with analytical volume and density");
    auto saved=ark::documentToJson(s.document);check(saved.find("brep_base64")==std::string::npos,"Derived projection B-rep is never serialized");V json;arkjson::parse(saved,json);check(json.find("version")->num==28,"New projection contracts persist as version28");
    auto sourceRevision=s.revision;
    apply(s,R"([{"op":"param.set","name":"R","expr":"120"}])");closeValue(volume(s.document,2),2*ark::K_PI*120*120,1e-4,"Source edit recomputes exact projection and extrusion");
    accepted(s.call("doc_undo",V::makeObj()));closeValue(volume(s.document,2),2*ark::K_PI*100*100,1e-4,"Undo restores projected geometry");
    accepted(s.call("doc_redo",V::makeObj()));closeValue(volume(s.document,2),2*ark::K_PI*120*120,1e-4,"Redo restores source-driven geometry");
    ark::Document loaded;std::string error;check(ark::documentFromJson(ark::documentToJson(s.document),loaded,&error),"Projection reload parses");loaded.recompute();for(const auto& result:loaded.lastResults)check(result.ok,"Projection reload recomputes");closeValue(volume(loaded,2),volume(s.document,2),1e-6,"Projection reload preserves exact volume");
    rejected(s,R"([{"op":"feature.remove","id":"Round"}])");
    rejected(s,R"([{"op":"sketch.entity.set","name":"Projected","entity":{"kind":"project","name":"rim","feature":"Round","reference":"missing","mode":"normal"}}])");
    rejected(s,R"([{"op":"sketch.entity.set","name":"Projected","entity":{"kind":"project","name":"rim","feature":"Disk","reference":"cap.end","mode":"normal"}}])");
    ark::AgentSession tilted;
    apply(tilted,R"([{"op":"feature.add","type":"native_solid","name":"Cylinder","native":{"op":"cylinder"},"p":{"radius":"4","height":"10"}},{"op":"plane.set","name":"Tilt","p":{"rx":"60"}},{"op":"sketch.set","name":"Ellipse","sketch":{"plane":"Tilt","entities":[{"kind":"project","name":"ellipse","feature":"Cylinder","reference":"cap.end","mode":"normal"}]}},{"op":"feature.add","type":"native_solid","name":"EllipticalDisk","native":{"op":"extrude"},"p":{"sketch":"Ellipse","depth":"3"}}])");
    closeValue(volume(tilted.document,2),ark::K_PI*16*.5*3,1e-4,"Tilted circle projection produces exact ellipse area");
    rejected(tilted,R"([{"op":"plane.set","name":"Tilt","p":{"rx":"90"}}])");
    ark::AgentSession spatial;
    apply(spatial,R"([{"op":"feature.add","type":"curve","name":"Original","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[20,10,15]}]}},{"op":"feature.add","type":"curve","name":"Included3D","inputs":["Original"],"native":{"op":"project","mode":"3d","reference":"edge.0"}},{"op":"sketch.set","name":"Section","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"section","dof":[0,0,1]}]}},{"op":"feature.add","type":"sweep","name":"Pipe","native":{"section":"Section","path":"Included3D"},"p":{"solid":"1","correction":"1"}}])");
    const auto& included=body(spatial.document,2).nativeData;check(included.find("report")->find("projection_exact")->b,"Spatial inclusion reports exact authority");closeValue(included.find("polyline_world")->arr.back().arr[2].num,15,1e-8,"3D inclusion preserves nonzero Z");closeValue(volume(spatial.document,3),ark::K_PI*std::sqrt(725.),1e-3,"Included spatial line is usable as exact sweep path");
    ark::AgentSession sketches;
    apply(sketches,R"([{"op":"param.set","name":"L","expr":"20"},{"op":"plane.set","name":"Raised","p":{"oz":"7","rx":"30"}},{"op":"sketch.set","name":"Source","sketch":{"plane":"Raised","entities":[{"kind":"line","name":"line","dof":[0,0,"L",10]},{"kind":"arc","name":"arc","dof":[0,0,4,20,240]},{"kind":"spline","name":"spline","dof":[0,0,5,8,10,-3,20,4]}]}},{"op":"sketch.set","name":"Target","sketch":{"plane":"XY","entities":[{"kind":"project","name":"line","mode":"normal","source_sketch":"Source","source_entity":"line"},{"kind":"project","name":"arc","mode":"normal","source_sketch":"Source","source_entity":"arc"},{"kind":"project","name":"spline","mode":"normal","source_sketch":"Source","source_entity":"spline"}]}},{"op":"feature.add","type":"curve","name":"Sketch3DCurve","native":{"op":"project","mode":"3d","source_sketch":"Source","source_entity":"spline"}}])");
    auto target=sketches.document.findSketch("Target");check(target&&target->sk.ents.size()==3,"Sketch line, nonminor arc and spline project without a solid intermediary");
    check(target->sk.ents[1].projectionSamples.size()>5,"Nonrectilinear arc display is curved");check(target->sk.ents[2].projectionSamples.size()>5,"Spline display is curved");
    auto includedSketch=body(sketches.document,1).nativeData;closeValue(includedSketch.find("polyline_world")->arr.front().arr[2].num,7,1e-8,"3D sketch entity inclusion preserves source plane height");
    apply(sketches,R"([{"op":"param.set","name":"L","expr":"30"}])");closeValue(sketches.document.findSketch("Target")->sk.ents[0].projectionSamples.back().x,30,1e-8,"Sketch source parameter changes update linked target");
    rejected(sketches,R"([{"op":"sketch.entity.set","name":"Target","entity":{"kind":"project","name":"line","mode":"normal","source_sketch":"Target","source_entity":"arc"}}])");
    rejected(sketches,R"([{"op":"sketch.entity.set","name":"Source","entity":{"kind":"project","name":"line","mode":"normal","source_sketch":"Target","source_entity":"line"}}])");
    rejected(sketches,R"([{"op":"sketch.remove","name":"Source"}])");
    ark::Document sketchReload;check(ark::documentFromJson(ark::documentToJson(sketches.document),sketchReload,&error),"Source sketch projection association reloads");sketchReload.recompute();for(const auto& result:sketchReload.lastResults)check(result.ok,"Sketch source reload recomputes spatial curve");closeValue(sketchReload.findSketch("Target")->sk.ents[0].projectionSamples.back().x,30,1e-8,"Reload preserves source-sketch linkage");
    ark::AgentSession chain;
    apply(chain,R"([{"op":"plane.set","name":"APlane","p":{"oz":"3"}},{"op":"plane.set","name":"BPlane","p":{"oz":"7"}},{"op":"feature.add","type":"native_solid","name":"Parent","native":{"op":"cylinder"},"p":{"radius":"2","height":"10"}},{"op":"sketch.set","name":"A","sketch":{"plane":"APlane","entities":[{"kind":"project","name":"rim","feature":"Parent","reference":"cap.end","mode":"normal"}]}},{"op":"sketch.set","name":"B","sketch":{"plane":"BPlane","entities":[{"kind":"project","name":"rim","source_sketch":"A","source_entity":"rim","mode":"normal"}]}},{"op":"feature.add","type":"native_solid","name":"ChainDisk","native":{"op":"extrude"},"p":{"sketch":"B","depth":"5"}},{"op":"feature.add","type":"curve","name":"Chain3D","native":{"op":"project","mode":"3d","source_sketch":"B","source_entity":"rim"}}])");
    closeValue(volume(chain.document,2),ark::K_PI*4*5,1e-6,"Feature-sketch-sketch-extrusion chain resolves topologically");
    apply(chain,R"([{"op":"feature.edit","id":"Parent","p":{"radius":"3"}}])");closeValue(volume(chain.document,2),ark::K_PI*9*5,1e-6,"Literal source edit invalidates all indirect consumer geometry");closeValue(body(chain.document,3).nativeData.find("report")->find("length_mm")->num,ark::K_2PI*3,1e-5,"Literal source edit invalidates spatial sketch consumer");
    ark::Document chainReload;check(ark::documentFromJson(ark::documentToJson(chain.document),chainReload,&error),"Indirect projection chain reloads");chainReload.recompute();for(const auto& result:chainReload.lastResults)check(result.ok,"Indirect projection reload recomputes without stale derived caches");closeValue(volume(chainReload,2),ark::K_PI*9*5,1e-6,"Reload reproduces indirect exact profile");
    rejected(chain,R"([{"op":"feature.edit","id":"Parent","native":{"op":"extrude"},"p":{"sketch":"B","depth":"10"}}])");
    ark::AgentSession instances;
    apply(instances,R"([{"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":"10","depth":"6","height":"3"}},{"op":"assembly.add","name":"Placed","feature":"Block","p":{"tx":"5","ty":"4","tz":"7"},"pattern":{"type":"linear","count":"3","dx":"20"}},{"op":"sketch.set","name":"PlacedSketch","sketch":{"plane":"XY","entities":[{"kind":"project","name":"edge","feature":"Block","reference":"edge.x.ymin.zmax","mode":"normal","source_instance":1,"source_copy":2}]}},{"op":"feature.add","type":"curve","name":"Placed3D","inputs":["Block"],"native":{"op":"project","mode":"3d","reference":"edge.x.ymin.zmax","source_instance":1,"source_copy":2}}])");
    const auto instanceSamples=instances.document.findSketch("PlacedSketch")->sk.ents[0].projectionSamples;closeValue(std::min(instanceSamples.front().x,instanceSamples.back().x),45,1e-7,"2D projection uses selected physical instance copy");closeValue(instanceSamples.front().y,4,1e-7,"2D projection uses instance world placement");closeValue(body(instances.document,2).nativeData.find("polyline_world")->arr[0].arr[2].num,10,1e-7,"3D inclusion composes instance placement once");
    apply(instances,R"([{"op":"assembly.edit","id":1,"p":{"tx":"8"}}])");const auto moved=instances.document.findSketch("PlacedSketch")->sk.ents[0].projectionSamples;closeValue(std::min(moved.front().x,moved.back().x),48,1e-7,"Instance pose edit refreshes projection with unchanged source B-rep");auto points=body(instances.document,2).nativeData.find("polyline_world")->arr;closeValue(std::min(points.front().arr[0].num,points.back().arr[0].num),48,1e-7,"Instance pose edit invalidates spatial projection cache");
    rejected(instances,R"([{"op":"sketch.entity.set","name":"PlacedSketch","entity":{"kind":"project","name":"edge","feature":"Block","reference":"edge.x.ymin.zmax","mode":"normal","source_instance":1,"source_copy":9}}])");
    rejected(chain,R"([{"op":"feature.edit","id":"Parent","suppressed":true}])");
    arkjson::parse(ark::documentToJson(chain.document,true,false),json);json.set("version",V::makeNum(13));ark::Document old;check(!ark::documentFromJson(json.dump(),old,&error)&&error.find("version 28")!=std::string::npos,"Older document versions reject new projection selectors");
    check(ark::documentToJson(chain.document).find("local_brep_base64")==std::string::npos,"Projection caches are absent from persisted journal");
    V request=V::makeObj();request.set("input_brep",*body(instances.document,1).nativeData.find("brep_base64"));request.set("input_topology",*body(instances.document,1).nativeData.find("topology"));request.set("reference",V::makeStr("edge.x.ymin.zmax"));request.set("mode",V::makeStr("3d"));
    auto expectNativeReject=[&](V candidate,const char* why){bool failed=false;try{ark::occt::projectReferenceNative(candidate);}catch(...){failed=true;}check(failed,why);};
    auto split=request;auto topology=*split.find("input_topology"),references=*topology.find("references");for(auto& row:references.arr)if(row.find("name")->str=="edge.x.ymin.zmax")row.set("status",V::makeStr("split"));topology.set("references",references);split.set("input_topology",topology);expectNativeReject(split,"Split projection reference rejects without rebinding");
    auto duplicate=request;topology=*duplicate.find("input_topology");references=*topology.find("references");references.arr.push_back(references.arr.front());topology.set("references",references);duplicate.set("input_topology",topology);expectNativeReject(duplicate,"Ambiguous producer registry rejects projection");
    auto digest=request;topology=*digest.find("input_topology");topology.set("brep_sha256",V::makeStr("invalid"));digest.set("input_topology",topology);expectNativeReject(digest,"Projection rejects registry bound to another B-rep");
    ark::AgentSession holed;
    apply(holed,R"([{"op":"feature.add","type":"native_solid","name":"Cylinder","native":{"op":"cylinder"},"p":{"radius":"4","height":"10"}},{"op":"feature.add","type":"native_solid","name":"Holed","inputs":["Cylinder"],"native":{"op":"hole","cutter":{"kind":"circle","plane":"XY","radius":1,"depth":10}}}])");
    rejected(holed,R"([{"op":"sketch.set","name":"BadFace","sketch":{"plane":"XY","entities":[{"kind":"project","name":"outline","feature":"Holed","reference":"cap.end","mode":"normal"}]}}])");
    rejected(holed,R"([{"op":"sketch.set","name":"CurvedFace","sketch":{"plane":"XY","entities":[{"kind":"project","name":"outline","feature":"Cylinder","reference":"side","mode":"normal"}]}}])");
    PrivateProjectionDirectory privateFiles;auto binaryPath=privateFiles.path/"projection.ark";auto encodedPath=binaryPath.u8string();std::string path(encodedPath.begin(),encodedPath.end());
    check(ark::saveDocument(chain.document,path.c_str()),"Projection v28 journal saves in native binary .ark");
    ark::AgentSession reopened;V load=V::makeObj();load.set("path",V::makeStr(path));load.set("expected_revision",V::makeNum(reopened.revision));accepted(reopened.call("doc_load",load));
    closeValue(volume(reopened.document,2),ark::K_PI*9*5,1e-6,"Binary .ark reload preserves exact projection chain");
    V binaryJson;arkjson::parse(ark::documentToJson(reopened.document),binaryJson);check(binaryJson.find("version")->num==28&&binaryJson.find("iteration_history"),"Binary document retains v28 and iteration history together");
    apply(reopened,R"([{"op":"feature.edit","id":"Parent","p":{"radius":"4"}}])");closeValue(volume(reopened.document,2),ark::K_PI*16*5,1e-6,"Source association remains editable after binary reload");closeValue(body(reopened.document,3).nativeData.find("report")->find("length_mm")->num,ark::K_2PI*4,1e-5,"Binary reload and source edit refresh spatial sketch curve");
    for(const auto& tool:ark::AgentSession::tools(true).arr)if(tool.find("name")->str=="doc_apply"){const auto& schema=*tool.find("inputSchema");check(schema.find("properties")->find("commands")->find("items")->find("properties")->find("entity")->find("oneOf")->arr[1].find("properties")->find("source_sketch")!=nullptr,"Tool discovery contains typed source-sketch projection fields");}
    std::cout<<"Exact projection checks: "<<checks<<" passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
