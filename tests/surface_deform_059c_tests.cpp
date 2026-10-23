#include "ark/occt_modeling.hpp"
#include "../src/occt/common.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom_BSplineSurface.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS_Face.hxx>
#include <iostream>
#include <stdexcept>
#include <cmath>

namespace {
using V=arkjson::Value;
using namespace ark::occt;
unsigned checks=0;
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
V json(const char* str){V v;if(!arkjson::parse(str,v))throw std::runtime_error("fixture JSON");return v;}
const V& f(const V& a,const char* k){const auto* p=a.find(k);if(!p)throw std::runtime_error(std::string("Missing result ")+k);return *p;}
void reject(const V& q,const char* fragment){try{modelSurfaceNative(q);}catch(const std::exception& e){check(std::string(e.what()).find(fragment)!=std::string::npos,e.what());return;}throw std::runtime_error(std::string("Accepted invalid request: ")+fragment);}
V tagged(const TopoDS_Shape& shape){V out=shapeResult(shape,false),topology=json(R"({"references":[{"name":"surface","kind":"face","status":"bound","indices":[0]}]})");topology.set("brep_sha256",V::makeStr(sha256(base64Decode(f(out,"brep_base64").str))));out.set("topology",topology);return completeSelectionReferences(out);}
V request(const V& source,const char* op){V q=V::makeObj();q.set("op",V::makeStr(op));q.set("input_brep",f(source,"brep_base64"));q.set("topology",f(source,"topology"));q.set("reference",V::makeStr("surface"));return q;}
Handle(Geom_BSplineSurface) spline(){NCollection_Array2<gp_Pnt> poles(1,5,1,4);NCollection_Array2<double> weights(1,5,1,4);for(int u=1;u<=5;++u)for(int v=1;v<=4;++v){poles(u,v)=gp_Pnt((u-1)*20,(v-1)*20,u==3&&v==2?4:0);weights(u,v)=u==2&&v==2?1.2:1.;}NCollection_Array1<double> uk(1,3),vk(1,2);NCollection_Array1<int> um(1,3),vm(1,2);uk(1)=0;uk(2)=.3;uk(3)=1;vk(1)=0;vk(2)=1;um(1)=4;um(2)=1;um(3)=4;vm(1)=vm(2)=4;return new Geom_BSplineSurface(poles,weights,uk,vk,um,vm,3,3);}
void run(){
    const auto basis=spline();const V source=tagged(BRepBuilderAPI_MakeFace(basis,1e-6).Face());const auto original=f(source,"brep_base64").str;const V inspect=request(source,"surface_control_net"),net=modelSurfaceNative(inspect);check(f(net,"poles").arr.size()==20,"control inventory size");check(f(net,"u_knots").arr.at(1).num==.3,"nonuniform knot fixture");check(f(net,"weights").arr.at(1).arr.at(1).num==1.2,"rational fixture");
    V stretch=request(source,"surface_deform_poles");stretch.set("source_brep_sha256",f(net,"source_brep_sha256"));stretch.set("max_displacement_mm",V::makeNum(10));stretch.set("edits",json(R"([{"u":2,"v":1,"delta_mm":[0,0,5]}])"));const V result=modelSurfaceNative(stretch),after=modelSurfaceNative(request(result,"surface_control_net"));
    for(const auto* key:{"u_degree","v_degree","u_count","v_count","u_knots","v_knots","u_multiplicities","v_multiplicities","weights"})check(f(net,key).dump()==f(after,key).dump(),"exact spline basis changed");
    for(size_t i=0;i<20;++i){const auto& a=f(f(net,"poles").arr[i],"point_mm").arr;const auto& b=f(f(after,"poles").arr[i],"point_mm").arr;check(a[0].num==b[0].num&&a[1].num==b[1].num&&std::abs(b[2].num-a[2].num-(i==9?5:0))<1e-12,"unexpected pole movement");}
    check(BRepCheck_Analyzer(readBrep(f(result,"brep_base64").str)).IsValid(),"stretch B-rep validity");check(f(source,"brep_base64").str==original,"source bytes changed");
    auto bad=stretch;bad.set("source_brep_sha256",V::makeStr("stale"));reject(bad,"source B-rep changed");
    bad=stretch;bad.set("max_displacement_mm",V::makeNum(4));reject(bad,"displacement exceeds");
    bad=stretch;bad.set("edits",json(R"([{"u":2,"v":1,"delta_mm":[0,0,1]},{"u":2,"v":1,"delta_mm":[0,0,2]}])"));reject(bad,"duplicate pole");
    bad=stretch;bad.set("edits",json(R"([{"u":32,"v":1,"delta_mm":[0,0,1]}])"));reject(bad,"u out of range");
    bad=stretch;bad.set("edits",json(R"([{"u":2,"v":1,"delta_mm":[0,0,0]}])"));reject(bad,"zero pole");
    bad=inspect;bad.set("tolerance",V::makeNum(.1));reject(bad,"Only 1e-6");
    bad=inspect;auto top=f(source,"topology");top.set("brep_sha256",V::makeStr("stale"));bad.set("topology",top);reject(bad,"topology fingerprint");
    bad=inspect;bad.set("reference",V::makeStr("unbound"));reject(bad,"Unknown semantic face");
    bad=inspect;bad.set("guess",V::makeBool(true));reject(bad,"unknown field");
    const V trimmed=tagged(BRepBuilderAPI_MakeFace(basis,.1,1,0,1,1e-6).Face());reject(request(trimmed,"surface_control_net"),"trimmed/holed");
    auto closed=Handle(Geom_BSplineSurface)::DownCast(basis->Copy());for(int v=1;v<=4;++v)closed->SetPole(5,v,closed->Pole(1,v));const V closedSource=tagged(BRepBuilderAPI_MakeFace(closed,1e-6).Face());reject(request(closedSource,"surface_control_net"),"periodic/closed");
    const V box=tagged(BRepPrimAPI_MakeBox(gp_Pnt(-2,-2,3),5,5,5).Shape());reject(request(box,"surface_control_net"),"solids and multi-face");
    V candidates=request(source,"surface_glue_candidates");candidates.set("u",V::makeNum(0));candidates.set("v",V::makeNum(0));candidates.set("target_brep",f(box,"brep_base64"));candidates.set("target_topology",f(box,"topology"));candidates.set("target_brep_sha256",f(f(box,"topology"),"brep_sha256"));candidates.set("max_distance_mm",V::makeNum(100));candidates.set("limit",V::makeNum(2));const V list=modelSurfaceNative(candidates);const auto& rows=f(list,"candidates").arr;check(rows.size()==2&&f(list,"partial").b&&f(list,"matched_count").num==8,"bounded candidate inventory");check(f(rows[0],"distance_mm").num<=f(rows[1],"distance_mm").num,"candidate ordering");check(modelSurfaceNative(candidates).dump()==list.dump(),"candidate order is nondeterministic");check(f(rows[0],"edges").arr.size()>=3,"missing exact incident tangent lines");
    V glue=V::makeObj();for(const auto* key:{"u","v","target_brep","target_topology","target_brep_sha256","max_distance_mm"})glue.set(key,f(candidates,key));glue.set("target_vertex",f(rows[0],"target_vertex"));glue.set("tangent_axis",V::makeStr("u"));glue.set("tangent_tolerance_deg",V::makeNum(.01));bool edgeFound=false;for(const auto& edge:f(rows[0],"edges").arr)if(std::abs(f(edge,"tangent").arr[0].num)>.9){glue.set("target_edge",f(edge,"reference"));edgeFound=true;break;}check(edgeFound,"fixture exact X tangent absent");
    V glueRequest=request(source,"surface_deform_poles");glueRequest.set("source_brep_sha256",f(net,"source_brep_sha256"));glueRequest.set("max_displacement_mm",V::makeNum(20));glueRequest.set("glue",glue);const V joined=modelSurfaceNative(glueRequest);const auto& proof=f(f(joined,"report"),"glue");check(f(proof,"result_gap_mm").num<=1e-6,"glue exact point gap");check(f(proof,"tangent_angle_deg").num<=.01,"glue tangent angular proof");check(std::abs(f(proof,"preserved_handle_length_mm").num-20)<1e-12,"original handle length changed");const V joinedNet=modelSurfaceNative(request(joined,"surface_control_net"));check(f(joinedNet,"weights").dump()==f(net,"weights").dump(),"glue rational weights changed");
    bad=glueRequest;auto g=glue;g.set("target_brep_sha256",V::makeStr("stale"));bad.set("glue",g);reject(bad,"target B-rep fingerprint");
    bad=glueRequest;g=glue;g.set("target_vertex",V::makeStr("vertex.999"));bad.set("glue",g);reject(bad,"outside the exact inventory");
    bad=glueRequest;g=glue;g.set("target_edge",V::makeStr("unbound"));bad.set("glue",g);reject(bad,"does not meet");
    bad=glueRequest;g=glue;g.set("max_distance_mm",V::makeNum(1));bad.set("glue",g);reject(bad,"outside the glue search radius");
    bad=glueRequest;g=glue;g.set("u",V::makeNum(1));bad.set("glue",g);reject(bad,"only corner");
    bad=candidates;bad.set("limit",V::makeNum(65));reject(bad,"limit out of range");
    bad=candidates;bad.set("max_distance_mm",V::makeNum(1001));reject(bad,"candidate radius");
    bad=candidates;bad.set("max_distance_mm",V::makeNum(.01));check(f(modelSurfaceNative(bad),"candidates").arr.empty(),"empty search chose a broad fallback");
}
}
int main(){try{run();std::cout<<"Surface deformation: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
