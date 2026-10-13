#include "ark/agent.hpp"
#include "ark/native_features.hpp"
#include "ark/occt_modeling.hpp"
#include "ark/occt_solid.hpp"
#include "ark/serialize.hpp"
#include "../src/occt/common.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;
constexpr double pi=3.14159265358979323846;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void checkNear(double observed,double expected,double tolerance,const char* message){check(std::isfinite(observed)&&std::abs(observed-expected)<=tolerance,message);}
V parse(const char* source){V result;check(arkjson::parse(source,result),"test JSON");return result;}
template<class F>void rejects(F&& call,const char* message){try{call();}catch(const std::exception&){return;}throw std::runtime_error(message);}
V accepted(const V& response){check(!(response.find("isError")&&response.find("isError")->b),response.find("structuredContent")&&response.find("structuredContent")->find("error")?response.find("structuredContent")->find("error")->str.c_str():"agent call failed");return *response.find("structuredContent");}
const V& report(const V& result){return *result.find("report");}

void helixEvidenceAndSweep(){
    const V source=parse(R"({"op":"helix","radius":20,"pitch":10,"turns":2.25,"start_angle":30,"origin":[4,5,6],"axis":[0,0,1],"x_axis":[1,0,0],"handedness":1})");
    const V helix=ark::occt::modelSurfaceNative(source);const V& evidence=report(helix);
    checkNear(evidence.find("length_mm")->num,2.25*std::sqrt(4*pi*pi*400+100),1e-5,"helix analytic length");
    check(evidence.find("helix_observed_max_error_mm")->num<=1e-6&&!evidence.find("helix_exact_analytic_brep")->b,"helix fit reports observed accuracy and representation");
    const auto wire=TopoDS::Wire(ark::occt::shapes(ark::occt::readBrep(helix.find("brep_base64")->str),TopAbs_WIRE).front());
    BRepTools_WireExplorer walk(wire);const BRepAdaptor_Curve first(walk.Current());
    auto start=first.Value(first.FirstParameter());checkNear(start.X(),4+20*std::cos(pi/6),1e-6,"helix rotated start X");checkNear(start.Y(),15,1e-6,"helix rotated start Y");checkNear(start.Z(),6,1e-6,"helix starts at authored origin axial station");
    BRepAdaptor_Curve last(walk.Current());for(;walk.More();walk.Next())last.Initialize(walk.Current());auto end=last.Value(last.LastParameter());checkNear(end.Z(),28.5,1e-6,"helix fractional-turn rise");
    V left=source;left.set("handedness",V::makeNum(-1));const auto leftResult=ark::occt::modelSurfaceNative(left);checkNear(report(leftResult).find("length_mm")->num,evidence.find("length_mm")->num,1e-6,"left/right helix length");
    left.set("axis",parse("[1,0,0]"));left.set("x_axis",parse("[0,1,0]"));left.set("start_angle",V::makeNum(0));const auto axial=ark::occt::modelSurfaceNative(left);const auto axialEdges=ark::occt::shapes(ark::occt::readBrep(axial.find("brep_base64")->str),TopAbs_EDGE);BRepAdaptor_Curve axialLast(TopoDS::Edge(axialEdges.back()));checkNear(axialLast.Value(axialLast.LastParameter()).X(),26.5,1e-6,"helix arbitrary axis rise");
    // A physically aligned small circular section follows the native helix path.
    const V path=ark::occt::modelSurfaceNative(parse(R"({"op":"helix","radius":20,"pitch":10,"turns":1})"));
    V sweep=parse(R"({"op":"sweep","solid":true,"correction":true,"contact":false,"section":{"entities":[{"kind":"circle","center":[0,0],"radius":0.4}]},"path":{}})");
    const double speed=std::sqrt(400+100/(4*pi*pi)),ty=20/speed,tz=10/(2*pi*speed);
    V frame=V::makeArr();for(double v:{1.,0.,0.,0., 0.,tz,-ty,0., 0.,ty,tz,0., 20.,0.,0.,1.})frame.arr.push_back(V::makeNum(v));V section=*sweep.find("section");section.set("frame",frame);sweep.set("section",section);V nativePath=V::makeObj();nativePath.set("wire_brep",*path.find("brep_base64"));sweep.set("path",nativePath);
    const V swept=ark::occt::modelSurfaceNative(sweep);checkNear(report(swept).find("volume_mm3")->num,pi*.16*std::sqrt(4*pi*pi*400+100),.01,"helix sweep analytic constant-section volume");
    const V outer=ark::occt::modelSurfaceNative(parse(R"({"op":"helix","radius":22,"pitch":10,"turns":1})"));V loft=V::makeObj(),profiles=V::makeArr(),profileA=V::makeObj(),profileB=V::makeObj();loft.set("op",V::makeStr("loft"));loft.set("ruled",V::makeBool(true));profileA.set("wire_brep",*path.find("brep_base64"));profileB.set("wire_brep",*outer.find("brep_base64"));profiles.arr={profileA,profileB};loft.set("profiles",profiles);check(report(ark::occt::modelSurfaceNative(loft)).find("faces")->num>0,"native helices are usable as open loft sections");
    for(const char* invalid:{R"({"op":"helix","radius":0,"pitch":10,"turns":1})",R"({"op":"helix","radius":20,"pitch":0,"turns":1})",R"({"op":"helix","radius":20,"pitch":10,"turns":101})",R"({"op":"helix","radius":20,"pitch":10,"turns":1,"axis":[0,0,0]})",R"({"op":"helix","radius":20,"pitch":10,"turns":1,"x_axis":[0,0,1]})",R"({"op":"helix","radius":20,"pitch":10,"turns":1,"handedness":0})",R"({"op":"helix","radius":20,"pitch":10,"turns":1,"silent_fit":true})"})rejects([&]{ark::occt::modelSurfaceNative(parse(invalid));},"hostile helix accepted");
}

V sewRequest(const std::vector<TopoDS_Shape>& faces,bool solid){V request=V::makeObj(),inputs=V::makeArr();request.set("op",V::makeStr("sew"));request.set("solid",V::makeBool(solid));request.set("require_connected",V::makeBool(true));for(const auto& face:faces)inputs.arr.push_back(V::makeStr(ark::occt::brepData(face)));request.set("inputs",inputs);return request;}
void sewDiagnostics(){
    const auto box=BRepPrimAPI_MakeBox(2,3,4).Shape();const auto faces=ark::occt::shapes(box,TopAbs_FACE);V request=sewRequest(faces,true);V result=ark::occt::modelSurfaceNative(request);checkNear(report(result).find("volume_mm3")->num,24,1e-8,"sewn box volume");check(report(result).find("free_edges")->num==0&&report(result).find("connected")->b&&report(result).find("closed")->b,"sewn closed solid diagnostics");
    std::vector<TopoDS_Shape> missing(faces.begin(),faces.end()-1);rejects([&]{ark::occt::modelSurfaceNative(sewRequest(missing,true));},"open box gained solid authority");result=ark::occt::modelSurfaceNative(sewRequest(missing,false));check(report(result).find("free_edges")->num==4&&report(result).find("free_boundary_length_mm")->num>0,"open shell free boundaries measured");
    const auto a=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,1,0,1).Face();const auto b=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),1,2,0,1).Face();result=ark::occt::modelSurfaceNative(sewRequest({a,b},false));checkNear(report(result).find("area_mm2")->num,2,1e-8,"adjacent sewn face area");checkNear(report(result).find("free_boundary_length_mm")->num,6,1e-8,"sewn rectangle perimeter");
    const auto gap=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),1.001,2.001,0,1).Face();rejects([&]{ark::occt::modelSurfaceNative(sewRequest({a,gap},false));},"gap silently healed");request=sewRequest({a,gap},false);request.set("require_connected",V::makeBool(false));result=ark::occt::modelSurfaceNative(request);check(report(result).find("component_count")->num==2,"disconnected sew explicitly measured");rejects([&]{ark::occt::modelSurfaceNative(sewRequest({a,a},false));},"duplicate sew input accepted");request.set("tolerance",V::makeNum(.01));rejects([&]{ark::occt::modelSurfaceNative(request);},"sew tolerance inflated");
}

V circlesLoft(){return parse(R"({"op":"loft","solid":true,"ruled":true,"compatible":false,"profiles":[{"entities":[{"kind":"circle","center":[0,0],"radius":5,"name":"rim"}]},{"frame":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,10,1],"entities":[{"kind":"circle","center":[0,0],"radius":5,"name":"rim"}]}]})");}
void loftAlignmentAndGuideChecks(){
    V loft=circlesLoft();loft.set("controls",parse(R"({"section_alignment":[{"seam_angle":45},{"seam_angle":45}]})"));const V aligned=ark::occt::modelSurfaceNative(loft);checkNear(report(aligned).find("volume_mm3")->num,250*pi,1e-5,"matching circular seams preserve cylinder volume");checkNear(report(aligned).find("section_alignment")->arr[0].find("start_mm")->arr[0].num,5/std::sqrt(2.),1e-6,"circular seam angle acts on wire");
    V polygon=parse(R"({"op":"loft","solid":true,"ruled":true,"profiles":[{"entities":[{"kind":"line","name":"south","points":[[0,0],[2,0]]},{"kind":"line","name":"east","points":[[2,0],[2,2]]},{"kind":"line","name":"north","points":[[2,2],[0,2]]},{"kind":"line","name":"west","points":[[0,2],[0,0]]}]},{"frame":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,10,1],"entities":[{"kind":"line","name":"south","points":[[0,0],[2,0]]},{"kind":"line","name":"east","points":[[2,0],[2,2]]},{"kind":"line","name":"north","points":[[2,2],[0,2]]},{"kind":"line","name":"west","points":[[0,2],[0,0]]}]}],"controls":{"section_alignment":[{"seam_edge":1},{"seam_edge":1}]}})");
    const V reordered=ark::occt::modelSurfaceNative(polygon);checkNear(report(reordered).find("volume_mm3")->num,40,1e-8,"matching polygon seams preserve analytic box volume");checkNear(report(reordered).find("section_alignment")->arr[0].find("start_mm")->arr[0].num,2,1e-8,"polygon seam changes directed first edge");bool namedSouth=false;for(const V& ref:reordered.find("topology")->find("references")->arr)if(ref.find("name")->str=="edge.start.south"&&ref.find("status")->str=="bound")namedSouth=true;check(namedSouth,"semantic polygon edge name follows source after reordering");
    polygon.set("controls",parse(R"({"section_alignment":[{"reverse":true},{"reverse":true}]})"));const V reversed=ark::occt::modelSurfaceNative(polygon);checkNear(report(reversed).find("volume_mm3")->num,40,1e-8,"coherent reversed polygon section orientation preserves volume");
    loft.set("controls",parse(R"({"section_alignment":[{},{}],"unknown":1})"));rejects([&]{ark::occt::modelSurfaceNative(loft);},"ignored loft control accepted");loft.set("controls",parse(R"({"section_alignment":[{}]})"));rejects([&]{ark::occt::modelSurfaceNative(loft);},"mismatched section alignment accepted");
    loft=circlesLoft();loft.set("ruled",V::makeBool(false));loft.set("guides",parse(R"([{"entities":[{"kind":"line","points":[[5,0,0],[5,0,10]]}]}])"));loft.set("controls",parse(R"({"guide_samples":17,"max_guide_deviation_mm":0.000001,"require_ordered_contacts":true})"));const V guided=ark::occt::modelSurfaceNative(loft);check(report(guided).find("guide_contact_order_checked")->b&&report(guided).find("guide_observed_max_surface_deviation_mm")->num<=1e-6,"straight guide full checkpoint evidence");check(!report(guided).find("guide_between_samples_bound_verified")->b,"sampled loft bound falsely certified");
    loft.set("guides",parse(R"([{"entities":[{"kind":"line","points":[[5.0001,0,0],[5.0001,0,10]]}]}])"));rejects([&]{ark::occt::modelSurfaceNative(loft);},"strict loft contact misses accepted");
    loft.set("guides",parse(R"([{"entities":[{"kind":"line","points":[[5,0,0],[5,0,10]]}]}])"));loft.set("controls",parse(R"({"guide_samples":130})"));rejects([&]{ark::occt::modelSurfaceNative(loft);},"unbounded loft checkpoint count accepted");
}

V collision(const TopoDS_Shape& a,const TopoDS_Shape& b){V request=V::makeObj();request.set("op",V::makeStr("collision"));request.set("a_brep",V::makeStr(ark::occt::brepData(a)));request.set("b_brep",V::makeStr(ark::occt::brepData(b)));return ark::occt::modelSurfaceNative(request);}
void collisionAnalyticCases(){
    const auto a=BRepPrimAPI_MakeBox(10,10,10).Shape();V result=collision(a,BRepPrimAPI_MakeBox(gp_Pnt(15,0,0),10,10,10).Shape());check(result.find("status")->str=="separated","separation classified");checkNear(result.find("min_distance_mm")->num,5,1e-8,"box clearance analytic");checkNear(result.find("common_volume_mm3")->num,0,0,"separated overlap zero");
    result=collision(a,BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),10,10,10).Shape());check(result.find("status")->str=="touching"&&!result.find("interference")->b,"face touch has no volume interference");
    result=collision(a,BRepPrimAPI_MakeBox(gp_Pnt(5,0,0),10,10,10).Shape());check(result.find("status")->str=="interference","interference classified");checkNear(result.find("common_volume_mm3")->num,500,1e-8,"box overlap analytic volume");
    result=collision(a,BRepPrimAPI_MakeBox(gp_Pnt(1,1,1),2,2,2).Shape());checkNear(result.find("common_volume_mm3")->num,8,1e-8,"full containment overlap");check(result.find("inner_solution")->b,"full containment distance recognized");
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,1,0,1).Face();rejects([&]{collision(a,face);},"surface used for solid collision");rejects([&]{collision(a,ark::occt::compound({a,BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),1,1,1).Shape()}));},"multi-solid collision silently reduced");
    V invalid=parse(R"({"op":"collision","a_brep":"bad","b_brep":"bad"})");rejects([&]{ark::occt::modelSurfaceNative(invalid);},"invalid B-rep collision accepted");
    invalid.set("a_brep",V::makeStr(ark::occt::brepData(a)));invalid.set("b_brep",*invalid.find("a_brep"));invalid.set("a_transform",parse("[2,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]"));rejects([&]{ark::occt::modelSurfaceNative(invalid);},"collision accepted nonrigid frame");
}

void atomicSourceEditAndReadOnlyWrapper(){
    ark::AgentSession session;session.suppressIterationHistory();accepted(session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"coil_radius","expr":"12"},{"op":"feature.add","type":"curve","name":"Coil","native":{"op":"helix","radius":"coil_radius","pitch":"8","turns":"1.5"}}]})")));
    const double before=session.document.evalBodies[0].nativeData.find("report")->find("length_mm")->num;
    accepted(session.call("doc_apply",parse(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"coil_radius","expr":"16"}]})")));check(session.document.evalBodies[0].nativeData.find("report")->find("length_mm")->num>before,"helix expression mutation recomputes native source");
    const auto revision=session.revision;const auto saved=ark::documentToJson(session.document);V rejected=session.call("doc_apply",parse(R"({"expected_revision":2,"commands":[{"op":"param.set","name":"coil_radius","expr":"0"}]})"));check(rejected.find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==saved,"hostile helix edit rolls back document and revision");
    V document;check(arkjson::parse(saved,document),"helix serialization");ark::Document recovered;std::string error;check(ark::documentFromJson(saved,recovered,&error),"helix load source");check(recovered.features[0].nativeSpec.find("op")->str=="helix","helix typed source persists");
    ark::Body a,b;a.nativeData=V::makeObj();b.nativeData=V::makeObj();a.nativeData.set("brep_base64",V::makeStr(ark::occt::brepData(BRepPrimAPI_MakeBox(10,10,10).Shape())));b.nativeData=a.nativeData;ark::Mat4 transform=ark::Mat4::identity();transform.m[12]=15;const V pair=ark::inspectNativeCollision(a,ark::Mat4::identity(),b,transform);checkNear(pair.find("min_distance_mm")->num,5,1e-8,"worker wrapper uses explicit placement");check(session.revision==revision&&ark::documentToJson(session.document)==saved,"collision preserves document and revision");
}

void exactDocumentCollisionOccurrences(){
    ark::AgentSession session;session.suppressIterationHistory();
    accepted(session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[
      {"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":10,"depth":10,"height":10}},
      {"op":"feature.add","type":"native_solid","name":"Offset definition","inputs":["Block"],"native":{"op":"transform"},"p":{"tx":25}},
      {"op":"assembly.add","name":"Fixed block","feature":"Block","p":{"tx":0}},
      {"op":"assembly.add","name":"Moving block","feature":"Block","p":{"tx":15},"pattern":{"type":"linear","count":2,"dx":30}}
    ]})")));
    const auto fixed=session.document.assembly[0].id,moving=session.document.assembly[1].id;
    const auto block=session.document.features[0].id,offset=session.document.features[1].id;
    auto query=[&](V args){
        args.set("mode",V::makeStr("exact"));args.set("expected_revision",V::makeNum((double)session.revision));
        const auto revision=session.revision;const auto authoring=ark::documentToJson(session.document,false,false);
        V result=accepted(session.call("collision_inspect",args));
        check(result.find("authority")->str=="exact_native_BRep_distance_and_boolean_common"&&result.find("requested_mode")->str=="exact","document collision uses exact branch");
        check(result.find("document_unchanged")->b&&!result.find("physics_simulation")->b&&result.find("revision")->num==(double)revision,"exact document collision reports read-only evidence");
        check(session.revision==revision&&ark::documentToJson(session.document,false,false)==authoring,"exact collision query preserves full authoring and revision");return result;
    };
    auto failedQuery=[&](const V& args,const char* diagnostic){
        const auto revision=session.revision;const auto authoring=ark::documentToJson(session.document,false,false);
        const V result=session.call("collision_inspect",args);check(result.find("isError")->b,"illegal exact document collision accepted");
        check(result.find("structuredContent")->find("error")->str.find(diagnostic)!=std::string::npos,"rejected collision query reports the expected diagnostic");
        check(session.revision==revision&&ark::documentToJson(session.document,false,false)==authoring,"rejected collision query preserves full authoring and revision");
    };
    V definitions=V::makeObj(),selected=V::makeObj();selected.set("id",V::makeNum((double)offset));definitions.set("a",V::makeStr("Block"));definitions.set("b",selected);
    V result=query(definitions);check(result.find("status")->str=="separated","exact native definitions are separated");checkNear(result.find("min_distance_mm")->num,15,1e-8,"exact definition clearance");check(result.find("a")->find("feature_id")->num==(double)block&&result.find("b")->find("feature_id")->num==(double)offset,"exact definition name/id selectors retain identities");
    V occurrences=parse(R"({"a":{"node":"Fixed block"},"b":{"copy":0}})");V second=*occurrences.find("b");second.set("node",V::makeNum((double)moving));occurrences.set("b",second);
    result=query(occurrences);check(result.find("status")->str=="separated","same-part occurrences are separated");checkNear(result.find("min_distance_mm")->num,5,1e-8,"assembly occurrence clearance");
    check(result.find("a")->find("feature_id")->num==result.find("b")->find("feature_id")->num&&result.find("a")->find("node")->num==(double)fixed&&result.find("b")->find("node")->num==(double)moving,"shared definition keeps distinct occurrence identities");
    second.set("copy",V::makeNum(1));occurrences.set("b",second);result=query(occurrences);checkNear(result.find("min_distance_mm")->num,35,1e-8,"pattern copy contributes its world placement");check(result.find("b")->find("copy")->num==1,"pattern copy identity retained");
    V first=V::makeObj();first.set("node",V::makeStr("Moving block"));first.set("copy",V::makeNum(0));occurrences.set("a",first);result=query(occurrences);checkNear(result.find("min_distance_mm")->num,20,1e-8,"different copies of one occurrence are distinct collision operands");
    occurrences.set("a",parse(R"({"node":"Fixed block"})"));second.set("copy",V::makeNum(0));occurrences.set("b",second);
    accepted(session.call("doc_apply",parse(R"({"expected_revision":1,"commands":[{"op":"assembly.edit","id":"Moving block","p":{"tx":10}}]})")));
    result=query(occurrences);check(result.find("status")->str=="touching"&&result.find("touching")->b&&!result.find("interference")->b,"assembly edit changes exact separation to face contact");checkNear(result.find("min_distance_mm")->num,0,1e-8,"assembly face-contact distance");checkNear(result.find("common_volume_mm3")->num,0,1e-8,"assembly face contact has zero common volume");
    V edit=parse(R"({"commands":[{"op":"assembly.edit","p":{"tx":5}}]})");edit.set("expected_revision",V::makeNum((double)session.revision));V commands=*edit.find("commands"),command=commands.arr[0];command.set("id",V::makeNum((double)moving));commands.arr[0]=command;edit.set("commands",commands);accepted(session.call("doc_apply",edit));
    result=query(occurrences);check(result.find("status")->str=="interference"&&result.find("interference")->b&&!result.find("touching")->b,"assembly edit changes exact contact to interference");checkNear(result.find("common_volume_mm3")->num,500,1e-8,"assembly overlap analytic volume");
    V invalid=occurrences;invalid.set("mode",V::makeStr("exact"));invalid.set("expected_revision",V::makeNum((double)session.revision-1));failedQuery(invalid,"Revision conflict");
    invalid.set("expected_revision",V::makeNum((double)session.revision));invalid.set("a",parse(R"({"node":"Moving block","copy":0})"));invalid.set("b",second);failedQuery(invalid,"distinct collision operands");
    invalid=occurrences;invalid.set("mode",V::makeStr("exact"));invalid.set("expected_revision",V::makeNum((double)session.revision));second.set("copy",V::makeNum(2));invalid.set("b",second);failedQuery(invalid,"not placed");
}

void nativeSkeletonPattern(){
    ark::AgentSession session;session.suppressIterationHistory();
    auto apply=[&](V commands){V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum((double)session.revision));return accepted(session.call("doc_apply",args));};
    const std::string source="input copies = 4\ninput spacing = 10\nrelation pitch = spacing\npattern Ribs from Seed count copies step [pitch, 0, 0]\n";
    V commands=parse(R"([{"op":"feature.add","type":"native_solid","name":"Seed","native":{"op":"box"},"p":{"width":2,"depth":3,"height":4}}])");
    V skeleton=V::makeObj();skeleton.set("op",V::makeStr("skeleton.set"));skeleton.set("source",V::makeStr(source));commands.arr.push_back(skeleton);apply(commands);
    const auto patternId=session.document.features.back().id;
    auto patternBody=[&](const ark::Document& document)->const ark::Body&{for(const auto& body:document.evalBodies)if(body.id==patternId)return body;throw std::runtime_error("Native Skeleton pattern body missing");};
    auto evidence=[&](const ark::Document& document,int count){
        const auto* feature=document.find(patternId);check(feature&&feature->type==ark::FeatureType::LinearPattern&&feature->p.at("count")=="copies"&&feature->p.at("dx")=="pitch","native bridge preserves Skeleton feature type and editable source expressions");
        const auto& body=patternBody(document);check(body.nativeData.find("brep_base64")&&!body.nativeData.find("brep_base64")->str.empty(),"Skeleton native pattern retains B-rep authority");
        const V& measured=report(body.nativeData);check(measured.find("brep_valid")->b&&measured.find("solids")->num==count&&!measured.find("display_mesh_authoritative")->b,"native pattern retains every requested solid");
        checkNear(measured.find("volume_mm3")->num,24.*count,1e-8,"Skeleton native pattern analytic volume");checkNear(body.bbox().mx.x,10.*(count-1)+2,1e-5,"Skeleton native pattern expression spacing");
    };
    evidence(session.document,4);
    apply(parse(R"([{"op":"param.set","name":"copies","expr":5}])"));check(session.document.features.back().id==patternId,"native Skeleton count change preserves pattern id");evidence(session.document,5);
    const std::string saved=ark::documentToJson(session.document,false,false);V persisted;check(arkjson::parse(saved,persisted)&&persisted.find("version")->num==35,"native Skeleton pattern saves source format35");
    check(persisted.find("skeleton")->find("source")->str==source&&persisted.find("skeleton")->find("overrides")->find("copies")->str=="5","native Skeleton source and input override persist");
    ark::Document loaded;std::string error;check(ark::documentFromJson(saved,loaded,&error),"native Skeleton source loads");loaded.recompute();check(loaded.parameterError.empty()&&loaded.lastResults.back().ok,"native Skeleton pattern recomputes after reload");evidence(loaded,5);
    // The same existing legacy pattern helper supplies its historical defaults.
    ark::Feature defaults=*session.document.find(patternId);defaults.p.clear();const auto defaultBody=ark::evaluateExactLegacyOperation(defaults,session.document.buildEnv(),session.document,session.document.evalBodies);
    checkNear(report(defaultBody.nativeData).find("volume_mm3")->num,72,1e-8,"exact legacy pattern default count is three");checkNear(defaultBody.bbox().mx.x,42,1e-5,"exact legacy pattern default X step is20mm");check(ark::documentToJson(session.document,false,false)==saved,"temporary native bridge cannot replace authored expressions");
    apply(parse(R"([{"op":"param.set","name":"copies","expr":1}])"));evidence(session.document,1);check(session.document.features.back().id==patternId,"count-one native identity retains pattern id");
    const auto revision=session.revision;const auto before=ark::documentToJson(session.document,false,false);const auto brep=patternBody(session.document).nativeData.find("brep_base64")->str;
    V invalid=parse(R"({"commands":[{"op":"param.set","name":"copies","expr":65}]})");invalid.set("expected_revision",V::makeNum((double)revision));const V rejected=session.call("doc_apply",invalid);
    check(rejected.find("isError")->b&&rejected.find("structuredContent")->find("error")->str.find("1..64")!=std::string::npos,"native Skeleton rejects count65 explicitly");
    check(session.revision==revision&&ark::documentToJson(session.document,false,false)==before&&patternBody(session.document).nativeData.find("brep_base64")->str==brep,"oversized native Skeleton pattern rolls back authoring, revision and B-rep");evidence(session.document,1);
}

void nativeSelectionCoverage(){
    auto complete=[](const V& result){
        const auto shape=ark::occt::readBrep(result.find("brep_base64")->str);const V& topology=*result.find("topology");
        check(topology.find("selection_inventory_complete")->b,"native selection inventory completeness receipt");
        for(const auto kind:{TopAbs_FACE,TopAbs_EDGE}){const auto entities=ark::occt::shapes(shape,kind);const auto& canonical=*result.find("mesh")->find(kind==TopAbs_FACE?"face_canonical":"edge_canonical");check(canonical.arr.size()==entities.size(),"canonical display mapping preserves every ordered face/edge occurrence");
            for(size_t index=0;index<entities.size();++index){const auto first=(size_t)canonical.arr[index].num;check(first<=index&&entities[first].IsSame(entities[index]),"canonical display mapping binds exact IsSame identity");}
            for(const auto& entity:entities){if(kind==TopAbs_EDGE&&BRep_Tool::Degenerated(TopoDS::Edge(entity)))continue;bool bound=false;
                for(const auto& row:topology.find("references")->arr){if(row.find("kind")->str!=(kind==TopAbs_FACE?"face":"edge")||(row.find("status")->str!="bound"&&row.find("status")->str!="merged"))continue;const auto& indices=*row.find("indices");check(indices.arr.size()==1&&indices.arr[0].num<entities.size(),"native pick binding has a valid exact index");if(entities[(size_t)indices.arr[0].num].IsSame(entity))bound=true;}
                check(bound,"displayed exact entity has no bound selection reference");
            }
        }
    };
    V square=parse(R"({"entities":[{"kind":"line","name":"south","points":[[0,0],[10,0]]},{"kind":"line","name":"east","points":[[10,0],[10,10]]},{"kind":"line","name":"north","points":[[10,10],[0,10]]},{"kind":"line","name":"west","points":[[0,10],[0,0]]}]})");
    V fill=V::makeObj();fill.set("op",V::makeStr("surface_fill"));fill.set("profile",square);const V surface=ark::occt::modelSurfaceNative(fill);complete(surface);
    std::string genericFace;for(const auto& row:surface.find("topology")->find("references")->arr)if(row.find("kind")->str=="face"){genericFace=row.find("name")->str;check(row.find("reference_scope")->str=="current_brep"&&!row.find("persistent")->b,"generic source is explicitly current B-rep scope");check(genericFace.find(surface.find("topology")->find("brep_sha256")->str)!=std::string::npos,"generic name binds exact B-rep hash");}
    V edgeTopology=*surface.find("topology"),edgeRows=*edgeTopology.find("references");std::string edgeName;V actualTangent;
    for(auto& row:edgeRows.arr)if(edgeName.empty()&&row.find("kind")->str=="edge"){edgeName=row.find("name")->str;actualTangent=*row.find("descriptor")->find("tangent");V forged=*row.find("descriptor"),negative=actualTangent;for(auto& component:negative.arr)component.num=-component.num;forged.set("tangent",negative);row.set("descriptor",forged);}edgeTopology.set("references",edgeRows);
    V edgeQuery=V::makeObj();edgeQuery.set("input_brep",*surface.find("brep_base64"));edgeQuery.set("topology",edgeTopology);edgeQuery.set("reference",V::makeStr(edgeName));const auto exactFrame=ark::occt::referenceFrameNative(edgeQuery);for(size_t k=0;k<3;++k)checkNear(exactFrame.find("frame")->arr[8+k].num,actualTangent.arr[k].num,1e-12,"generic frame ignores a forged incoming tangent descriptor");
    for(auto& row:edgeRows.arr)if(row.find("name")->str==edgeName)row.set("indices",parse("[1]"));edgeTopology.set("references",edgeRows);edgeQuery.set("topology",edgeTopology);rejects([&]{ark::occt::referenceFrameNative(edgeQuery);},"same-hash generic name accepted a different encoded index");
    V adjacent=square;adjacent.set("frame",parse("[1,0,0,0,0,1,0,0,0,0,1,0,10,0,0,1]"));V adjacentFill=fill;adjacentFill.set("profile",adjacent);const V secondSurface=ark::occt::modelSurfaceNative(adjacentFill);
    V sewn=parse(R"({"op":"sew","solid":false,"require_connected":true,"inputs":[]})");V inputs=V::makeArr();inputs.arr={*surface.find("brep_base64"),*secondSurface.find("brep_base64")};sewn.set("inputs",inputs);complete(ark::occt::modelSurfaceNative(sewn));
    V extrude=fill;extrude.set("op",V::makeStr("extrude_surface"));extrude.set("distance",V::makeNum(10));complete(ark::occt::modelSurfaceNative(extrude));
    V raised=square;raised.set("frame",parse("[1,0,0,0,0,1,0,0,0,0,1,0,0,0,10,1]"));V loft=parse(R"({"op":"loft","solid":true,"ruled":true})");V profiles=V::makeArr();profiles.arr={square,raised};loft.set("profiles",profiles);const V lofted=ark::occt::modelSurfaceNative(loft);complete(lofted);
    for(const char* name:{"side.south","side.east","side.north","side.west"}){bool bound=false;for(const auto& row:lofted.find("topology")->find("references")->arr)if(row.find("name")->str==name&&row.find("status")->str=="bound")bound=true;check(bound,"loft semantic sides follow actual MakeWire edges");}
    loft.set("ruled",V::makeBool(false));loft.set("guides",parse(R"([{"entities":[{"kind":"line","points":[[10,0,0],[10,0,10]]}]}])"));complete(ark::occt::modelSurfaceNative(loft));
    const V box=ark::occt::modelSolidNative(parse(R"({"op":"box","width":10,"depth":10,"height":10})"));complete(box);for(const auto& row:box.find("topology")->find("references")->arr)check(row.find("name")->str.find("@ark.")==std::string::npos,"primitive semantic names stay preferred without duplicate generic aliases");
    V aliased=box,aliasTopology=*box.find("topology"),aliasRows=*aliasTopology.find("references"),merged=aliasRows.arr[0],deleted=aliasRows.arr[0];merged.set("name",V::makeStr("merged_source"));merged.set("status",V::makeStr("merged"));merged.set("merged_from",parse("[\"before_a\",\"before_b\"]"));merged.set("surviving",*aliasRows.arr[0].find("name"));deleted.set("name",V::makeStr("deleted_source"));deleted.set("status",V::makeStr("deleted"));deleted.set("indices",V::makeArr());aliasRows.arr.push_back(merged);aliasRows.arr.push_back(deleted);aliasTopology.set("references",aliasRows);aliased.set("topology",aliasTopology);const auto retained=ark::occt::completeSelectionReferences(aliased);bool mergedKept=false,deletedKept=false;for(const auto& row:retained.find("topology")->find("references")->arr){if(row.find("name")->str=="merged_source")mergedKept=row.dump()==merged.dump();if(row.find("name")->str=="deleted_source")deletedKept=row.dump()==deleted.dump();}check(mergedKept&&deletedKept,"selection completion preserves merged/deleted semantic aliases exactly");
    V pattern=parse(R"({"op":"linear_pattern","count":3,"dx":20})");pattern.set("input_brep",*box.find("brep_base64"));pattern.set("input_topology",*box.find("topology"));const V patterned=ark::occt::modelSolidNative(pattern);complete(patterned);checkNear(report(patterned).find("volume_mm3")->num,3000,1e-8,"selection metadata cannot alter pattern geometry");
    const V tool=ark::occt::modelSolidNative(parse(R"({"op":"box","width":4,"depth":4,"height":12})"));V move=parse(R"({"op":"transform","frame":[1,0,0,0,0,1,0,0,0,0,1,0,3,3,-1,1]})");move.set("input_brep",*tool.find("brep_base64"));move.set("input_topology",*tool.find("topology"));const V cutter=ark::occt::modelSolidNative(move);
    V cut=parse(R"({"op":"cut"})");cut.set("input_brep",*box.find("brep_base64"));cut.set("input_topology",*box.find("topology"));cut.set("tool_brep",*cutter.find("brep_base64"));cut.set("tool_topology",*cutter.find("topology"));const V cutResult=ark::occt::modelSolidNative(cut);complete(cutResult);checkNear(report(cutResult).find("volume_mm3")->num,840,1e-8,"selection metadata preserves analytic cut volume");
    std::string cutFace;for(const auto& row:cutResult.find("topology")->find("references")->arr)if(row.find("name")->str.rfind("@ark.face.",0)==0&&row.find("status")->str=="bound"){cutFace=row.find("name")->str;break;}check(!cutFace.empty(),"new Boolean wall receives a current-B-rep reference");
    check(ark::occt::completeSelectionReferences(cutResult).dump()==cutResult.dump(),"selection inventory completion is idempotent");
    V restored=cutResult,restoredTopology=*cutResult.find("topology"),restoredRows=*restoredTopology.find("references");V actualDescriptor;
    for(auto& row:restoredRows.arr)if(row.find("name")->str==cutFace){actualDescriptor=*row.find("descriptor");row.set("status",V::makeStr("obsolete"));row.set("indices",V::makeArr());row.set("descriptor",parse(R"({"origin":[999,999,999],"normal":[0,0,-1]})"));}
    restoredTopology.set("references",restoredRows);restored.set("topology",restoredTopology);restored=ark::occt::completeSelectionReferences(restored);bool rebound=false;for(const auto& row:restored.find("topology")->find("references")->arr)if(row.find("name")->str==cutFace){rebound=row.find("status")->str=="bound"&&row.find("descriptor")->dump()==actualDescriptor.dump();}check(rebound,"restoring exact B-rep identity safely rebinds an obsolete index and ignores forged descriptors");complete(restored);
    V moved=move;moved.set("input_brep",*cutResult.find("brep_base64"));moved.set("input_topology",*cutResult.find("topology"));const V changedCut=ark::occt::modelSolidNative(moved);complete(changedCut);bool obsolete=false;for(const auto& row:changedCut.find("topology")->find("references")->arr)if(row.find("name")->str==cutFace){obsolete=row.find("status")->str=="obsolete"&&row.find("indices")->arr.empty();}check(obsolete,"generic references cannot acquire a different B-rep identity during propagation");
    V query=V::makeObj();query.set("input_brep",*changedCut.find("brep_base64"));query.set("topology",*changedCut.find("topology"));query.set("reference",V::makeStr(cutFace));rejects([&]{ark::occt::referenceFrameNative(query);},"obsolete generic face was silently rebound");
    const auto path=std::filesystem::temp_directory_path()/("ark-native-selection-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".step");ark::occt::writeStep(ark::occt::readBrep(box.find("brep_base64")->str),path);V importRequest=V::makeObj();importRequest.set("path",V::makeStr(path.string()));complete(ark::occt::importComponentNative(importRequest));std::filesystem::remove(path);
    // Registry capacity rejects the whole inventory instead of truncating it.
    V crowded=box,topology=*box.find("topology"),rows=*topology.find("references");const std::string oldHash(64,'0');while(rows.arr.size()<16384){V row=V::makeObj();row.set("name",V::makeStr("@ark.edge."+oldHash+"."+std::to_string(rows.arr.size())));row.set("kind",V::makeStr("edge"));row.set("status",V::makeStr("obsolete"));row.set("indices",V::makeArr());rows.arr.push_back(row);}topology.set("references",rows);crowded.set("topology",topology);check(ark::occt::completeSelectionReferences(crowded).find("topology")->find("references")->arr.size()==16384,"combined reference capacity boundary");rows.arr.push_back(rows.arr.back());topology.set("references",rows);crowded.set("topology",topology);rejects([&]{ark::occt::completeSelectionReferences(crowded);},"oversized reference inventory was truncated");
    V changed=surface;changed.set("brep_base64",*ark::occt::modelSurfaceNative(sewn).find("brep_base64"));changed.set("topology",*surface.find("topology"));if(changed.find("brep_base64")->str!=surface.find("brep_base64")->str)rejects([&]{ark::occt::completeSelectionReferences(changed);},"mismatched topology hash accepted");
    V semantic=box;topology=*box.find("topology");rows=*topology.find("references");while(rows.arr.size()<512){V row=rows.arr[0];row.set("name",V::makeStr("alias."+std::to_string(rows.arr.size())));rows.arr.push_back(row);}topology.set("references",rows);semantic.set("topology",topology);check(ark::occt::completeSelectionReferences(semantic).find("topology")->find("references")->arr.size()==512,"semantic registry512 boundary retained");V extra=rows.arr[0];extra.set("name",V::makeStr("too_many_semantic_aliases"));rows.arr.push_back(extra);topology.set("references",rows);semantic.set("topology",topology);rejects([&]{ark::occt::completeSelectionReferences(semantic);},"semantic512 cap was weakened by generic inventory");
    ark::AgentSession session;session.suppressIterationHistory();accepted(session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"native_solid","name":"Stock","native":{"op":"box"},"p":{"width":10,"depth":10,"height":10}},{"op":"feature.add","type":"native_solid","name":"Tool","native":{"op":"box"},"p":{"width":4,"depth":4,"height":12}},{"op":"feature.add","type":"native_solid","name":"Placed tool","native":{"op":"transform"},"inputs":["Tool"],"p":{"tx":3,"ty":3,"tz":-1}},{"op":"feature.add","type":"native_solid","name":"Cut","native":{"op":"cut"},"inputs":["Stock","Placed tool"]}]})")));
    std::string selected;for(const auto& body:session.document.evalBodies)if(body.id==session.document.features.back().id)for(const auto& row:body.nativeData.find("topology")->find("references")->arr)if(row.find("name")->str.rfind("@ark.face.",0)==0&&row.find("status")->str=="bound"){selected=row.find("name")->str;break;}check(!selected.empty(),"document Boolean exposes generic wall selection");
    V args=parse(R"({"expected_revision":1,"commands":[{"op":"feature.add","type":"surface_edit","name":"Selected wall","inputs":["Cut"],"native":{"op":"extract_face"}}]})");V edits=*args.find("commands"),command=edits.arr[0],native=*command.find("native");native.set("face",V::makeStr(selected));command.set("native",native);edits.arr[0]=command;args.set("commands",edits);accepted(session.call("doc_apply",args));
    const auto revision=session.revision;const auto saved=ark::documentToJson(session.document,false,false);V invalid=parse(R"({"commands":[{"op":"feature.edit","id":"Stock","p":{"width":11}}]})");invalid.set("expected_revision",V::makeNum((double)revision));const V rejected=session.call("doc_apply",invalid);check(rejected.find("isError")->b,"old current-B-rep wall selection silently followed an upstream change");check(session.revision==revision&&ark::documentToJson(session.document,false,false)==saved,"obsolete current-B-rep source reference rolls back the whole upstream edit");
    V stored;check(arkjson::parse(saved,stored)&&stored.find("version")->num==35,"authored current-B-rep source reference requires format35");ark::Document loaded;std::string loadError;check(ark::documentFromJson(saved,loaded,&loadError),"current-B-rep source reference persists");loaded.recompute();check(loaded.lastResults.back().ok,"unchanged exact B-rep source reference resolves after reload");stored.set("version",V::makeNum(34));check(!ark::documentFromJson(stored.dump(),loaded,&loadError),"downgraded current-B-rep source reference rejects format34");
}
}
int main(){try{helixEvidenceAndSweep();sewDiagnostics();loftAlignmentAndGuideChecks();collisionAnalyticCases();atomicSourceEditAndReadOnlyWrapper();exactDocumentCollisionOccurrences();nativeSkeletonPattern();nativeSelectionCoverage();std::cout<<"019B native geometry tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<"019B native geometry: "<<e.what()<<'\n';return 1;}}
