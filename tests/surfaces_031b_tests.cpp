#include "ark/occt_modeling.hpp"
#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "../src/occt/common.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <Geom_Plane.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using V=arkjson::Value;
V parse(const std::string& text){V v;arkjson::Parser p(text);if(!p.parse(v))throw std::runtime_error("Invalid fixture JSON");return v;}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const V& at(const V& v,const char* key){const auto* value=v.find(key);if(!value)throw std::runtime_error(std::string("Missing ")+key);return *value;}
void closeObserved(double a,double b,double tolerance,const char* label){if(std::abs(a-b)>tolerance)throw std::runtime_error(std::string(label)+": "+std::to_string(a)+" != "+std::to_string(b));}
V run(const V& request){return ark::occt::modelSurfaceNative(request);}
void reject(V request,const char* reason){try{(void)run(request);}catch(const std::exception&){return;}throw std::runtime_error(std::string("Expected rejection: ")+reason);}
V encoded(const TopoDS_Shape& shape){return ark::occt::completeSelectionReferences(ark::occt::shapeResult(shape,false));}
TopoDS_Face plane(double z=0){BRepBuilderAPI_MakeFace make(gp_Pln(gp_Ax3(gp_Pnt(0,0,z),gp_Dir(0,0,1),gp_Dir(1,0,0))),-20,20,-20,20);return make.Face();}
std::string faceName(const V& result){for(const auto& row:at(at(result,"topology"),"references").arr)if(at(row,"kind").str=="face"&&at(row,"status").str=="bound")return at(row,"name").str;throw std::runtime_error("No semantic face");}
V constraint(int edge,int order,const V& support){V v=V::makeObj();v.set("edge",V::makeNum(edge));v.set("continuity",V::makeNum(order));v.set("support_brep",at(support,"brep_base64"));v.set("topology",at(support,"topology"));v.set("face",V::makeStr(faceName(support)));return v;}
V rectangleFill(){return parse(R"({"op":"surface_fill","profile":{"entities":[{"kind":"line","points":[[0,0],[10,0]]},{"kind":"line","points":[[10,0],[10,5]]},{"kind":"line","points":[[10,5],[0,5]]},{"kind":"line","points":[[0,5],[0,0]]}]}})");}

void fillEvidence(){
    const auto support=encoded(plane());
    for(int order:{1,2}){
        auto request=rectangleFill();V constraints=V::makeArr();for(int edge=0;edge<4;++edge)constraints.arr.push_back(constraint(edge,order,support));request.set("constraints",constraints);
        const auto result=run(request);const auto& report=at(result,"report");
        check(at(report,"boundary_continuity_requested").str==(order==1?"G1":"G2"),"Requested continuity lost");
        check(!at(report,"global_g2_verified").b,"Sampled G2 acquired continuous proof");
        check(at(report,"support_boundary_evidence").arr.size()==4,"Missing support evidence");
        for(const auto& edge:at(report,"support_boundary_evidence").arr){check(at(edge,"samples_checked").num==33,"Checkpoint count changed");check(at(edge,"max_position_error_mm").num<=1e-6,"G0 failure");check(at(edge,"max_tangent_plane_angle_deg").num<=.001,"G1 failure");check(at(edge,"max_curvature_tensor_error_per_mm").num<=.0001,"G2 failure");}
        closeObserved(ark::occt::properties(ark::occt::readBrep(at(result,"brep_base64").str),'a'),50,1e-5,"Planar fill area");
        auto off=request;constraints.arr[0]=constraint(0,order,encoded(plane(2)));off.set("constraints",constraints);reject(off,"support off boundary");
        auto stale=request;V wrong=at(support,"topology");wrong.set("brep_sha256",V::makeStr(std::string(64,'0')));constraints=at(request,"constraints");constraints.arr[0].set("topology",wrong);stale.set("constraints",constraints);reject(stale,"stale support digest");
    }
    auto invalid=rectangleFill();invalid.set("mode",V::makeStr("blend"));reject(invalid,"blend without support constraints");
    auto constraints=V::makeArr();constraints.arr.push_back(constraint(0,1,support));constraints.arr.push_back(constraint(0,2,support));invalid=rectangleFill();invalid.set("constraints",constraints);reject(invalid,"duplicate edge support");
}

void curvedBlend(){
    const auto low=encoded(plane()),high=encoded(plane(2));
    V request=parse(R"({"op":"surface_fill","mode":"blend","profile":{"entities":[
      {"kind":"bezier","points":[[0,0,0],[2,0,0],[4,0,0],[6,0,2],[8,0,2],[10,0,2]]},
      {"kind":"line","points":[[10,0,2],[10,5,2]]},
      {"kind":"bezier","points":[[10,5,2],[8,5,2],[6,5,2],[4,5,0],[2,5,0],[0,5,0]]},
      {"kind":"line","points":[[0,5,0],[0,0,0]]}]}})");
    V constraints=V::makeArr();constraints.arr.push_back(constraint(1,2,high));constraints.arr.push_back(constraint(3,2,low));request.set("constraints",constraints);
    auto result=run(request);check(at(at(result,"report"),"operation").str=="surface_blend","Blend mode lost");
    const auto shape=ark::occt::readBrep(at(result,"brep_base64").str);check(BRepCheck_Analyzer(shape).IsValid(),"Blend B-rep invalid");
    const auto bounds=ark::occt::bounds(shape);check(bounds[5]-bounds[2]>1.9,"Blend flattened curved transition");
    constraints.arr[1]=constraint(3,2,high);request.set("constraints",constraints);reject(request,"conflicting supports");
}

void domainEdits(){
    BRepBuilderAPI_MakeFace make(gp_Pln(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1),gp_Dir(1,0,0))),0,10,0,10);
    BRepBuilderAPI_MakePolygon hole;hole.Add(gp_Pnt(3,3,0));hole.Add(gp_Pnt(3,7,0));hole.Add(gp_Pnt(7,7,0));hole.Add(gp_Pnt(7,3,0));hole.Close();make.Add(hole.Wire());
    auto request=parse(R"({"op":"surface_untrim","bounds":[0,10,0,10]})");request.set("input_brep",V::makeStr(ark::occt::brepData(make.Face())));
    const auto result=run(request);closeObserved(at(at(result,"report"),"added_area_mm2").num,16,1e-6,"Untrim restored hole");
    request.set("op",V::makeStr("surface_extend"));request.set("bounds",parse("[-2,12,-2,12]"));const auto extended=run(request);closeObserved(ark::occt::properties(ark::occt::readBrep(at(extended,"brep_base64").str),'a'),196,1e-6,"Analytic face extension area");
    request.set("bounds",parse("[1,9,0,10]"));reject(request,"extension discarding original region");
    request.set("bounds",parse("[0,10,0,10]"));request.set("input_brep",at(result,"brep_base64"));reject(request,"empty extension");
    auto nurbs=run(parse(R"({"op":"surface_nurbs","poles":[[[0,0,0],[0,1,0],[0,2,0],[0,3,0]],[[1,0,0],[1,1,0],[1,2,0],[1,3,0]],[[2,0,0],[2,1,0],[2,2,0],[2,3,0]],[[3,0,0],[3,1,0],[3,2,0],[3,3,0]]]})"));
    request=parse(R"({"op":"surface_extend","extension":{"direction":"u_max","length_mm":2,"continuity":2}})");request.set("input_brep",at(nurbs,"brep_base64"));const auto n=run(request);closeObserved(at(at(n,"report"),"added_area_mm2").num,6,1e-5,"Bounded NURBS extension area");
    check(at(at(n,"report"),"original_support_max_error_mm").num<=1e-6,"NURBS extension changed original support");
}

void mutualTrim(){
    const auto a=BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1),gp_Dir(1,0,0))),-5,5,-5,5).Face();
    const auto b=BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0))),-5,5,-5,5).Face();
    auto request=parse(R"({"op":"surface_mutual_trim","keep_points":[[-2,0,0],[0,0,2]]})");request.set("input_brep",V::makeStr(ark::occt::brepData(a)));request.set("other_brep",V::makeStr(ark::occt::brepData(b)));
    const auto result=run(request);check(at(at(result,"report"),"retained_faces").num==2,"Mutual trim lost one retained face");
    for(const auto& row:at(at(result,"report"),"per_input").arr){closeObserved(at(row,"retained_area_mm2").num,50,1e-6,"Mutual trim area");check(at(row,"area_conservation_checked").b,"Missing per-input conservation");}
    request.set("keep_points",parse("[[0,0,0],[0,0,2]]"));reject(request,"ambiguous trim keep point");
    request.set("other_brep",at(request,"input_brep"));reject(request,"coincident surfaces do not split both");
}

V sweep(){return parse(R"({"op":"sweep","solid":true,"section":{"entities":[{"kind":"line","points":[[-2,-1],[2,-1]]},{"kind":"line","points":[[2,-1],[2,1]]},{"kind":"line","points":[[2,1],[-2,1]]},{"kind":"line","points":[[-2,1],[-2,-1]]}]},"path":{"entities":[{"kind":"line","points":[[0,0,0],[0,0,10]]}]}})");}
void sweepOrientations(){
    for(const char* mode:{"fixed_binormal","frenet","corrected_frenet","auxiliary"}) {
        auto request=sweep();V orientation=V::makeObj();orientation.set("mode",V::makeStr(mode));
        if(std::string(mode)=="fixed_binormal")orientation.set("binormal",parse("[0,1,0]"));
        if(std::string(mode)=="auxiliary"){orientation.set("guide",parse(R"({"entities":[{"kind":"line","points":[[5,0,0],[5,0,10]]}]})"));orientation.set("equivalence",V::makeStr("curvilinear"));}
        request.set("orientation",orientation);const auto result=run(request);closeObserved(ark::occt::properties(ark::occt::readBrep(at(result,"brep_base64").str)),80,1e-4,(std::string("Oriented sweep volume: ")+mode).c_str());check(at(at(result,"report"),"orientation_mode").str==mode,"Sweep mode ignored");
    }
    auto request=sweep();request.set("orientation",parse(R"({"mode":"fixed_binormal","binormal":[0,0,1]})"));reject(request,"parallel fixed binormal");
    request.set("orientation",parse(R"({"mode":"corrected_frenet","binormal":[0,1,0]})"));reject(request,"unused orientation field");
    // A vertical path has a representation on this vertical plane support.
    auto support=BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,1,0),gp_Dir(1,0,0))),-20,20,-20,20).Face();
    V orientation=parse(R"({"mode":"support"})");orientation.set("support_brep",V::makeStr(ark::occt::brepData(support)));request=sweep();request.set("orientation",orientation);const auto result=run(request);closeObserved(ark::occt::properties(ark::occt::readBrep(at(result,"brep_base64").str)),80,1e-4,"Support sweep volume");
}

void sweepLaws(){
    auto request=sweep();request.set("laws",parse(R"({"interpolation":"linear","stations":[{"fraction":0,"twist_deg":0,"scale":1},{"fraction":1,"twist_deg":90,"scale":2}]})"));
    const auto result=run(request);const auto shape=ark::occt::readBrep(at(result,"brep_base64").str);check(BRepCheck_Analyzer(shape).IsValid(),"Law sweep invalid");
    closeObserved(ark::occt::properties(shape),8*10*7./3.,.001,"Continuous law sweep volume (twist preserves section area)");
    const auto faces=ark::occt::shapes(shape,TopAbs_FACE);const auto surface=ark::occt::compound(faces);
    for(double fraction:{.125,.25,.5,.75,.875})for(const auto& local:{gp_Pnt(2,1,0),gp_Pnt(-2,1,0),gp_Pnt(0,1,0)}){
        const double angle=fraction*std::acos(-1.)/2,scale=1+fraction;
        const gp_Pnt expected(scale*(local.X()*std::cos(angle)-local.Y()*std::sin(angle)),scale*(local.X()*std::sin(angle)+local.Y()*std::cos(angle)),10*fraction);
        BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(expected).Vertex(),surface);distance.Perform();check(distance.IsDone()&&distance.Value()<=1e-6,"Continuous law surface misses an independent angle/scale checkpoint");
    }
    check(at(at(result,"report"),"kernel_max_fit_error_mm").num<=1e-6,"Law sweep exceeded fit tolerance");check(!at(at(result,"report"),"continuous_error_bound_certified").b,"Kernel estimate became a certified bound");
    auto noTwist=request;noTwist.set("laws",parse(R"({"interpolation":"linear","stations":[{"fraction":0,"twist_deg":0,"scale":1},{"fraction":1,"twist_deg":0,"scale":2}]})"));const auto straight=run(noTwist);check(at(straight,"brep_base64").str!=at(result,"brep_base64").str,"Authored twist ignored");
    request.set("path",parse(R"({"entities":[{"kind":"bezier","points":[[0,0,0],[0,0,5],[4,0,10]]}]})"));reject(request,"curved spine twist currently unsupported");
    request=sweep();request.set("laws",parse(R"({"interpolation":"linear","stations":[{"fraction":0,"twist_deg":0,"scale":1},{"fraction":1,"twist_deg":45,"scale":0}]})"));reject(request,"zero scale");
    request.set("laws",parse(R"({"interpolation":"linear","stations":[{"fraction":0,"twist_deg":0,"scale":1},{"fraction":0.5,"twist_deg":45,"scale":1}]})"));reject(request,"missing endpoint law");
}

void authoredPersistence(){
    ark::AgentSession session;session.suppressIterationHistory();session.suppressFailureCapture();
    auto apply=[&](V commands,bool accepted=true){V args=V::makeObj();args.set("expected_revision",V::makeNum(session.revision));args.set("commands",commands);const auto out=session.call("doc_apply",args);check(at(out,"isError").b!=accepted,accepted?out.dump().c_str():"Invalid authoring accepted");};
    apply(parse(R"([{"op":"param.set","name":"ScaleEnd","expr":"2"},{"op":"feature.add","type":"sweep","name":"Authored laws","native":{"section":{"entities":[{"kind":"circle","center":[0,0],"radius":"1"}]},"path":{"entities":[{"kind":"line","points":[[0,0,0],[0,0,10]]}]},"laws":{"interpolation":"linear","stations":[{"fraction":0,"twist_deg":0,"scale":1},{"fraction":1,"twist_deg":"45","scale":"ScaleEnd"}]}},"p":{"solid":"1"}}])"));
    auto volume=[&](){return at(at(session.document.evalBodies.back().nativeData,"report"),"volume_mm3").num;};closeObserved(volume(),std::acos(-1.)*10*7./3.,1e-4,"Authored expression law volume");
    apply(parse(R"([{"op":"param.set","name":"ScaleEnd","expr":"3"}])"));closeObserved(volume(),std::acos(-1.)*10*13./3.,1e-4,"Edited scale recompute");
    const auto saved=ark::documentToJson(session.document,false,false);check(at(parse(saved),"version").num==43,"New surface recipes must require source43");ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),error.c_str());restored.recompute();closeObserved(at(at(restored.evalBodies.back().nativeData,"report"),"volume_mm3").num,volume(),1e-4,"Law roundtrip volume");
    auto old=parse(saved);old.set("version",V::makeNum(42));check(!ark::documentFromJson(old.dump(),restored,&error),"New laws accepted a source42 downgrade");
    const auto revision=session.revision;apply(parse(R"([{"op":"param.set","name":"ScaleEnd","expr":"0"}])"),false);check(session.revision==revision&&ark::documentToJson(session.document,false,false)==saved,"Invalid scale failed atomicity");
}

void authoredSupportBindings(){
    ark::AgentSession session;session.suppressIterationHistory();session.suppressFailureCapture();
    auto apply=[&](V commands,bool accepted=true){V args=V::makeObj();args.set("expected_revision",V::makeNum(session.revision));args.set("commands",commands);const auto out=session.call("doc_apply",args);check(at(out,"isError").b!=accepted,accepted?out.dump().c_str():"Missing support dependency accepted");};
    apply(parse(R"([{"op":"feature.add","type":"native_solid","name":"Support stock","native":{"op":"box"},"p":{"width":20,"depth":20,"height":1}},{"op":"feature.add","type":"surface_edit","name":"Top support","inputs":[1],"native":{"op":"extract_face","face":"z_max"}},{"op":"feature.add","type":"surface_fill","name":"Supported patch","inputs":[2],"native":{"profile":{"entities":[{"kind":"line","points":[[2,2,1],[8,2,1]]},{"kind":"line","points":[[8,2,1],[8,8,1]]},{"kind":"line","points":[[8,8,1],[2,8,1]]},{"kind":"line","points":[[2,8,1],[2,2,1]]}]},"constraints":[{"edge":"0","support":2,"face":"surface","continuity":"2"}]}}])"));
    const auto& report=at(session.document.evalBodies.back().nativeData,"report");check(at(report,"support_boundary_evidence").arr.size()==1,"Support binding not forwarded");
    const auto saved=ark::documentToJson(session.document,false,false);const auto revision=session.revision;
    apply(parse(R"([{"op":"feature.edit","id":3,"inputs":[1]}])"),false);
    check(session.revision==revision&&ark::documentToJson(session.document,false,false)==saved,"Undeclared support failed atomicity");
}
}

int main(){
    const struct Case{const char* name;void(*run)();}cases[]{
        {"support G1/G2 evidence",fillEvidence},{"curved G2 blend",curvedBlend},
        {"untrim and extension",domainEdits},{"mutual trim",mutualTrim},
        {"sweep orientations",sweepOrientations},{"continuous twist/scale laws",sweepLaws},
        {"expression, persistence and atomicity",authoredPersistence},{"authored support bindings",authoredSupportBindings}};
    int failures=0;for(const auto& test:cases)try{test.run();std::cout<<test.name<<" passed\n";}catch(const std::exception& e){std::cerr<<test.name<<": "<<e.what()<<'\n';++failures;}
    return failures?1:0;
}
