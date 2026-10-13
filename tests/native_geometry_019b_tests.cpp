#include "ark/agent.hpp"
#include "ark/native_features.hpp"
#include "ark/occt_modeling.hpp"
#include "ark/serialize.hpp"
#include "../src/occt/common.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <cmath>
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
}
int main(){try{helixEvidenceAndSweep();sewDiagnostics();loftAlignmentAndGuideChecks();collisionAnalyticCases();atomicSourceEditAndReadOnlyWrapper();exactDocumentCollisionOccurrences();nativeSkeletonPattern();std::cout<<"019B native geometry tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<"019B native geometry: "<<e.what()<<'\n';return 1;}}
