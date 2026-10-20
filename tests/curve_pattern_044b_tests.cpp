#include "ark/occt_solid.hpp"
#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "ark/history_authoring.hpp"
#include "../src/occt/common.hpp"
#include <BRepAdaptor_CompCurve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_HArray1.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Circ.hxx>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using arkjson::Value;
namespace occt=ark::occt;
namespace {
Value parse(const char* json){Value v;if(!arkjson::parse(json,v))throw std::runtime_error("Invalid test JSON");return v;}
void check(bool good,const std::string& message){if(!good)throw std::runtime_error(message);}
void close(double actual,double expected,double tol,const std::string& message){if(!(std::abs(actual-expected)<=tol))throw std::runtime_error(message+": actual "+std::to_string(actual)+" expected "+std::to_string(expected));}
Value solid(const char* json){return occt::modelSolidNative(parse(json));}
Value wireResult(const TopoDS_Shape& wire){return occt::generatedShapeResult(wire,false,{},false);}
std::string edgeRef(const Value& path,size_t index){for(const auto& row:path.find("topology")->find("references")->arr)if(row.find("kind")->str=="edge"&&row.find("indices")->arr.size()==1&&row.find("indices")->arr[0].num==(double)index)return row.find("name")->str;throw std::runtime_error("edge reference missing");}
std::string faceRef(const Value& body,size_t index){for(const auto& row:body.find("topology")->find("references")->arr)if(row.find("kind")->str=="face"&&row.find("indices")->arr.size()==1&&row.find("indices")->arr[0].num==(double)index)return row.find("name")->str;throw std::runtime_error("face reference missing");}
Value patternRequest(const Value& seed,const Value& path,const Value& native){Value q=native;q.set("op",Value::makeStr("curve_pattern"));q.set("input_brep",*seed.find("brep_base64"));q.set("input_topology",*seed.find("topology"));q.set("tool_brep",*path.find("brep_base64"));q.set("tool_topology",*path.find("topology"));return q;}
Value pattern(const Value& seed,const Value& path,const Value& native){return occt::modelSolidNative(patternRequest(seed,path,native));}
bool rejects(const Value& seed,const Value& path,const Value& native,const char* fragment){try{(void)pattern(seed,path,native);}catch(const std::exception& error){if(std::string(error.what()).find(fragment)!=std::string::npos)return true;throw std::runtime_error(std::string("Unexpected rejection text: ")+error.what());}return false;}
Value refs(std::initializer_list<std::string> names){Value a=Value::makeArr();for(const auto& n:names)a.arr.push_back(Value::makeStr(n));return a;}
TopoDS_Shape splineEdge(){NCollection_Array1<gp_Pnt> poles(1,4);poles(1)=gp_Pnt(0,0,0);poles(2)=gp_Pnt(30,20,5);poles(3)=gp_Pnt(60,-15,10);poles(4)=gp_Pnt(90,5,0);GeomAPI_PointsToBSpline fit(poles);return BRepBuilderAPI_MakeEdge(fit.Curve()).Edge();}
// Station frames are the copies' own locations: each copy is the seed TShape.
std::vector<gp_Trsf> copyFrames(const Value& result){std::vector<gp_Trsf> out;const auto shape=occt::readBrep(result.find("brep_base64")->str);for(TopoDS_Iterator it(shape);it.More();it.Next())out.push_back(it.Value().Location().Transformation());return out;}
gp_Pnt origin(const gp_Trsf& t){return gp_Pnt(t.TranslationPart());}
gp_Vec axis(const gp_Trsf& t,int column){return gp_Vec(t.Value(1,column),t.Value(2,column),t.Value(3,column));}
const Value& receipt(const Value& result){return *result.find("report")->find("curve_pattern");}
size_t namedRows(const Value& result,const std::string& prefix){size_t n=0;for(const auto& row:result.find("topology")->find("references")->arr)if(row.find("name")->str.rfind(prefix,0)==0)++n;return n;}

void legacySingleEdgeUnchanged(){
    // Digests recorded from the unchanged 043A/042D code path (OCCT 8.0.1).
    const Value seed=solid(R"({"op":"box","width":2,"depth":3,"height":4})");
    const Value path=wireResult(splineEdge());
    Value native=parse(R"({"count":7,"margin_mm":1.5,"x_hint":[0,0,1]})");native.set("reference",Value::makeStr(edgeRef(path,0)));
    const Value hinted=pattern(seed,path,native);
    Value plain=parse(R"({"count":5,"margin_mm":0})");plain.set("reference",Value::makeStr(edgeRef(path,0)));
    const Value moved=pattern(seed,path,plain);
    check(!hinted.find("report")->find("curve_pattern")&&!moved.find("report")->find("curve_pattern"),"legacy requests stay on the legacy path");
    if(occt::kernelVersion()!="8.0.1"){std::cout<<"legacy digest comparison skipped: kernel "<<occt::kernelVersion()<<" differs from the recorded 8.0.1\n";return;}
    check(occt::sha256(occt::base64Decode(hinted.find("brep_base64")->str))=="8f44ccfd7c8785f72be43b6d5bd7e882a9f05144dc533382181941e23a24d3f5","legacy hinted B-rep bytes changed");
    check(occt::sha256(hinted.find("topology")->find("references")->dump())=="a3fbab5b3492666d6ead46e606b0e61477ea21614bd201ad02a104340b96e893","legacy hinted references changed");
    check(occt::sha256(occt::base64Decode(moved.find("brep_base64")->str))=="8c932448ff028275b1390029685a749c8a172f51ae7d186b7ebcbdc7a09edf5e","legacy plain B-rep bytes changed");
    check(occt::sha256(moved.find("topology")->find("references")->dump())=="d093134efcdc801dd84a7d6e42342316241e995b3c68ba14ebac5bd6a5ca326e","legacy plain references changed");
    std::cout<<"legacy single-edge curve_pattern: B-rep and reference digests unchanged\n";
}
void chainedLineArcSpline(){
    // line + tangent arc + B-spline, listed out of order; one arc-length domain.
    const TopoDS_Edge line=BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(20,0,0)).Edge();
    const auto arcCurve=GC_MakeArcOfCircle(gp_Pnt(20,0,0),gp_Vec(1,0,0),gp_Pnt(30,10,0)).Value();const TopoDS_Edge arc=BRepBuilderAPI_MakeEdge(arcCurve).Edge();
    NCollection_Array1<gp_Pnt> poles(1,4);poles(1)=gp_Pnt(30,10,0);poles(2)=gp_Pnt(30,25,4);poles(3)=gp_Pnt(45,35,8);poles(4)=gp_Pnt(60,40,6);GeomAPI_PointsToBSpline fit(poles);const TopoDS_Edge spline=BRepBuilderAPI_MakeEdge(fit.Curve()).Edge();
    BRepBuilderAPI_MakeWire builder;builder.Add(line);builder.Add(arc);builder.Add(spline);check(builder.IsDone(),"test wire");const TopoDS_Wire wire=builder.Wire();
    const Value path=wireResult(wire),seed=solid(R"({"op":"box","width":1,"depth":1,"height":1})");
    Value native=parse(R"({"count":9})");native.set("references",refs({edgeRef(path,1),edgeRef(path,2),edgeRef(path,0)}));
    const Value result=pattern(seed,path,native);const auto& r=receipt(result);
    check(r.find("path_edges")->num==3&&!r.find("closed")->b&&r.find("copies")->num==9,"three-edge open chain");
    BRepAdaptor_CompCurve composite(wire);const double length=GCPnts_AbscissaPoint::Length(composite,1e-10);close(r.find("path_length_mm")->num,length,1e-6,"chained path length");
    const auto frames=copyFrames(result);check(frames.size()==9,"nine located copies");
    for(size_t i=0;i<frames.size();++i){const double s=length*(double)i/8;double u=composite.FirstParameter()+(composite.LastParameter()-composite.FirstParameter())*(double)i/8;for(int k=0;k<40;++k){const double error=s-GCPnts_AbscissaPoint::Length(composite,composite.FirstParameter(),u,1e-11);gp_Pnt p;gp_Vec d;composite.D1(u,p,d);u=std::clamp(u+error/d.Magnitude(),composite.FirstParameter(),composite.LastParameter());if(std::abs(error)<1e-11)break;}close(origin(frames[i]).Distance(composite.Value(u)),0,1e-7,"uniform arc-length station "+std::to_string(i));}
    // Reversed travel starts at the other end; the shared seed geometry keeps the B-rep compact.
    native.set("reverse",Value::makeBool(true));const auto reversed=copyFrames(pattern(seed,path,native));close(origin(reversed.front()).Distance(gp_Pnt(60,40,6)),0,1e-7,"reverse starts at the chain end");
    std::cout<<"chain line+arc+spline: 9 stations uniform on composite arc length "<<length<<" mm\n";
}
Value closedSpline(){
    const int samples=12;Handle(NCollection_HArray1<gp_Pnt>) points=new NCollection_HArray1<gp_Pnt>(1,samples);
    for(int i=0;i<samples;++i){const double a=2*std::acos(-1.)*i/samples;points->SetValue(i+1,gp_Pnt(60*std::cos(a)+8*std::cos(3*a),40*std::sin(a),3*std::sin(2*a)));}
    GeomAPI_Interpolate interpolate(points,true,1e-7);interpolate.Perform();check(interpolate.IsDone(),"periodic interpolation");return wireResult(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(interpolate.Curve()).Edge()).Wire());
}
void closedLoopWithoutSeamDuplicate(){
    const Value path=closedSpline(),seed=solid(R"({"op":"box","width":0.5,"depth":0.5,"height":2})");
    Value native=parse(R"({"count":360,"x_hint":[0,0,1],"reference_scheme":"compact"})");native.set("reference",Value::makeStr(edgeRef(path,0)));
    const auto started=std::chrono::steady_clock::now();const Value result=pattern(seed,path,native);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto& r=receipt(result);check(r.find("closed")->b&&r.find("closed_spacing")->b&&r.find("copies")->num==360,"closed loop census");const double length=r.find("path_length_mm")->num;
    const auto frames=copyFrames(result);check(frames.size()==360,"360 copies");double low=1e9,high=0;
    for(size_t i=0;i<frames.size();++i){const double d=origin(frames[i]).Distance(origin(frames[(i+1)%frames.size()]));low=std::min(low,d);high=std::max(high,d);}
    check(low>0.9*length/360,"no duplicate station at the seam");check(high<1.1*length/360,"uniform chords around the loop including the seam");
    for(const auto& frame:frames){check(axis(frame,1).Z()>0.9,"x_hint keeps local X up");close(axis(frame,1).Magnitude(),1,1e-9,"rigid copy frame");}
    check(namedRows(result,"copy.0.")==18&&namedRows(result,"copy.1.")==0,"compact scheme names only copy 0");check(r.find("copy_face_stride")->num==6,"face stride published");
    // A whole-feature path needs no references and reproduces the same stations.
    Value whole=parse(R"({"count":360,"x_hint":[0,0,1],"reference_scheme":"compact"})");const auto again=copyFrames(pattern(seed,path,whole));close(origin(again[17]).Distance(origin(frames[17])),0,1e-9,"whole-feature path equals the single edge");
    Value phased=parse(R"({"count":4,"phase_mm":10,"x_hint":[0,0,1],"reference_scheme":"compact"})");const auto shifted=pattern(seed,path,phased);check(copyFrames(shifted).size()==4,"phase keeps count");close(receipt(shifted).find("station_frames")->arr[0].find("arc_length_mm")->num,10,1e-12,"phase shifts the seam station");close(receipt(shifted).find("station_frames")->arr[3].find("arc_length_mm")->num,10+0.75*length,1e-9,"phase keeps closed spacing");
    Value copiesOver=parse(R"({"count":360,"x_hint":[0,0,1]})");check(rejects(seed,path,copiesOver,"semantic references (limit 512)"),"copies scheme over the 512 budget rejects explicitly");
    std::cout<<"closed spline loop: 360 copies, chord min "<<low<<" max "<<high<<" (L/360="<<length/360<<"), "<<ms<<" ms\n";
}
void closedPolygonWholeFeature(){
    const TopoDS_Wire square=BRepBuilderAPI_MakePolygon(gp_Pnt(0,0,0),gp_Pnt(40,0,0),gp_Pnt(40,40,0),gp_Pnt(0,40,0),true).Wire();const Value path=wireResult(square),seed=solid(R"({"op":"cylinder","radius":0.3,"height":1})");
    const Value result=pattern(seed,path,parse(R"({"count":16,"x_hint":[0,0,1]})"));const auto frames=copyFrames(result);check(frames.size()==16&&receipt(result).find("closed")->b,"closed polygon from whole feature");
    for(size_t i=0;i<16;++i)close(origin(frames[i]).Distance(origin(frames[(i+1)%16])),10,1e-7,"square stations every 10 mm with corners included");
    const Value margins=pattern(seed,path,parse(R"({"count":5,"start_margin_mm":10,"end_margin_mm":10})"));check(!receipt(margins).find("closed_spacing")->b,"margins on a closed loop space an open interval");
    const auto open=copyFrames(margins);close(origin(open.front()).Distance(gp_Pnt(10,0,0)),0,1e-9,"start margin");close(origin(open.back()).Distance(gp_Pnt(0,10,0)),0,1e-9,"end margin");
}
void pitchSpacing(){
    const Value path=wireResult(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(100,0,0)).Edge()).Wire()),seed=solid(R"({"op":"box","width":1,"depth":1,"height":1})");
    auto fit=pattern(seed,path,parse(R"({"pitch_mm":12})"));check(receipt(fit).find("copies")->num==9,"fit rounds to 8 intervals");close(receipt(fit).find("pitch_mm")->num,12.5,1e-12,"fit adjusts pitch");close(origin(copyFrames(fit).back()).X(),100,1e-9,"fit reaches the end");
    auto truncate=pattern(seed,path,parse(R"({"pitch_mm":12,"pitch_mode":"truncate"})"));check(receipt(truncate).find("copies")->num==9,"truncate keeps exact pitch count");close(receipt(truncate).find("remainder_mm")->num,4,1e-9,"truncate remainder");close(origin(copyFrames(truncate).back()).X(),96,1e-9,"truncate exact pitch");
    auto margins=pattern(seed,path,parse(R"({"pitch_mm":12,"start_margin_mm":5,"end_margin_mm":5,"skip":[0,3]})"));const auto& r=receipt(margins);check(r.find("stations")->num==9&&r.find("copies")->num==7,"margins and skip");close(r.find("pitch_mm")->num,11.25,1e-12,"margin fit pitch");
    const auto frames=copyFrames(margins);close(origin(frames.front()).X(),5+11.25,1e-9,"skip keeps station positions");check(namedRows(margins,"copy.0.")==0&&namedRows(margins,"copy.1.")>0&&namedRows(margins,"copy.4.")>0,"skipped stations publish no copy names; names keep station indices");
}
void surfaceOrientation(){
    // Cylinder r20 h50; path circle r25 at z=25, projected with a 1 mm offset.
    const Value cylinder=solid(R"({"op":"cylinder","radius":20,"height":50})");const Value seed=solid(R"({"op":"box","width":0.4,"depth":0.4,"height":3})");
    const Value path=wireResult(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,25),gp_Dir(0,0,1)),25)).Edge()).Wire());
    Value native=parse(R"({"count":24,"orientation":"surface","project":true,"offset_mm":1})");native.set("support_brep",*cylinder.find("brep_base64"));native.set("support_topology",*cylinder.find("topology"));Value faces=Value::makeArr();faces.arr.push_back(Value::makeStr("side"));native.set("support_faces",faces);
    const Value result=pattern(seed,path,native);const auto& s=*receipt(result).find("surface");close(s.find("max_deviation_mm")->num,5,1e-6,"cylinder deviation reported");check(s.find("projected")->b,"projected receipt");
    for(const auto& frame:copyFrames(result)){const gp_Pnt p=origin(frame);close(std::hypot(p.X(),p.Y()),21,1e-6,"projected radius plus offset");close(p.Z(),25,1e-6,"projection keeps height");const gp_Vec radial=gp_Vec(p.X(),p.Y(),0).Normalized();close(axis(frame,1).Dot(radial),1,1e-7,"local X follows the outward normal");close(std::abs(axis(frame,3).Dot(gp_Vec(0,0,1))),0,1e-7,"local Z stays tangent");}
    native.set("max_deviation_mm",Value::makeNum(4));check(rejects(seed,path,native,"max_deviation_mm"),"deviation bound rejects explicitly");
    // Torus: path above the tube; nearest point and normal are exact.
    const Value torus=solid(R"({"op":"torus","major_radius":40,"minor_radius":10})");const Value ring=wireResult(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,12),gp_Dir(0,0,1)),45)).Edge()).Wire());
    Value onTorus=parse(R"({"count":12,"orientation":"surface","project":true})");onTorus.set("support_brep",*torus.find("brep_base64"));onTorus.set("support_topology",*torus.find("topology"));
    const Value t=pattern(seed,ring,onTorus);close(receipt(t).find("surface")->find("max_deviation_mm")->num,3,1e-6,"torus deviation");
    for(const auto& frame:copyFrames(t)){const gp_Pnt p=origin(frame);close(std::hypot(p.X(),p.Y()),40+10*5./13,1e-6,"torus projected radius");close(p.Z(),10*12./13,1e-6,"torus projected height");const gp_Vec normal=gp_Vec(std::hypot(p.X(),p.Y())-40,0,p.Z()).Normalized();close(axis(frame,1).Dot(gp_Vec(normal.X()*p.X()/std::hypot(p.X(),p.Y()),normal.X()*p.Y()/std::hypot(p.X(),p.Y()),normal.Z())),1,1e-6,"torus normal");}
    // Without projection the stations stay on the path and only orient.
    Value orient=parse(R"({"count":6,"orientation":"surface"})");orient.set("support_brep",*cylinder.find("brep_base64"));orient.set("support_topology",*cylinder.find("topology"));for(const auto& frame:copyFrames(pattern(seed,path,orient)))close(std::hypot(origin(frame).X(),origin(frame).Y()),25,1e-9,"orientation-only keeps path points");
    std::cout<<"surface orientation: cylinder deviation "<<s.find("max_deviation_mm")->num<<" mm, torus deviation "<<receipt(t).find("surface")->find("max_deviation_mm")->num<<" mm\n";
}
void frenetFrames(){
    const Value path=wireResult(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),30)).Edge()).Wire()),seed=solid(R"({"op":"box","width":1,"depth":1,"height":1})");
    for(const auto& frame:copyFrames(pattern(seed,path,parse(R"({"count":8,"orientation":"frenet"})")))){const gp_Vec inward=gp_Vec(origin(frame).XYZ()).Reversed().Normalized();close(axis(frame,1).Dot(inward),1,1e-7,"frenet X is the principal normal");}
    const Value line=wireResult(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(50,0,0)).Edge()).Wire());check(rejects(seed,line,parse(R"({"count":4,"orientation":"frenet"})"),"Frenet frame is undefined"),"frenet on a straight path needs x_hint");
    close(axis(copyFrames(pattern(seed,line,parse(R"({"count":4,"orientation":"frenet","x_hint":[0,1,0]})"))).front(),1).Y(),1,1e-12,"frenet falls back to x_hint");
}
void rejections(){
    const Value seed=solid(R"({"op":"box","width":1,"depth":1,"height":1})");
    BRepBuilderAPI_MakeEdge a(gp_Pnt(0,0,0),gp_Pnt(10,0,0)),b(gp_Pnt(20,0,0),gp_Pnt(30,0,0)),c(gp_Pnt(10,0,0),gp_Pnt(10,10,0)),d(gp_Pnt(10,0,0),gp_Pnt(10,-10,0));
    const Value apart=wireResult(occt::compound({a.Edge(),b.Edge()}));Value disconnected=parse(R"({"count":4})");disconnected.set("references",refs({edgeRef(apart,0),edgeRef(apart,1)}));check(rejects(seed,apart,disconnected,"disconnected"),"disconnected chain");
    check(rejects(seed,apart,parse(R"({"count":4})"),"disconnected"),"disconnected whole-feature wire");
    const Value fork=wireResult(occt::compound({a.Edge(),c.Edge(),d.Edge()}));check(rejects(seed,fork,parse(R"({"count":4})"),"branches"),"branching path");
    const Value line=wireResult(BRepBuilderAPI_MakeWire(a.Edge()).Wire());
    check(rejects(seed,line,parse(R"({"count":4,"start_margin_mm":6,"end_margin_mm":6})"),"no usable path length"),"margins consume the path");
    check(rejects(seed,line,parse(R"({"count":2049,"reference_scheme":"compact"})"),"2..2048"),"count above 2048");
    check(rejects(seed,line,parse(R"({"pitch_mm":0.001})"),"more than 2048"),"pitch above 2048 copies");
    check(rejects(seed,line,parse(R"({"count":1000,"reference_scheme":"compact"})"),"capacity 16384"),"selection inventory bound");
    check(rejects(seed,seed,parse(R"({"count":4})"),"wire-only"),"non-wire whole-feature path");
    check(rejects(seed,line,parse(R"({"count":4,"pitch_mm":2})"),"exactly one of count or pitch_mm"),"count and pitch together");
    check(rejects(seed,line,parse(R"({"count":4,"skip":[0,1,2,3]})"),"skips every station"),"all skipped");
    check(rejects(seed,line,parse(R"({"count":4,"orientation":"x_hint","x_hint":[1,0,0]})"),"parallel"),"x_hint parallel to tangent");
    check(rejects(seed,line,parse(R"({"count":4,"project":true})"),"requires orientation surface"),"projection needs surface orientation");
    check(rejects(seed,line,parse(R"({"count":4,"spacing":"x"})"),"Unknown curve pattern field"),"unknown v2 field");
    Value both=parse(R"({"count":4})");both.set("reference",Value::makeStr(edgeRef(line,0)));both.set("references",refs({edgeRef(line,0)}));check(rejects(seed,line,both,"not both"),"reference and references");
}
Value accepted(const Value& response){if(const auto* error=response.find("isError");error&&error->b)throw std::runtime_error(response.find("structuredContent")->dump());return *response.find("structuredContent");}
Value apply(ark::AgentSession& s,const char* commands){Value args=Value::makeObj();args.set("commands",parse(commands));args.set("expected_revision",Value::makeNum((double)s.revision));return accepted(s.call("doc_apply",args));}
Value plan(ark::AgentSession& s,const char* json){Value args=parse(json);args.set("expected_revision",Value::makeNum((double)s.revision));const Value preview=accepted(s.call("pattern_plan",args));if(!preview.find("valid")->b)throw std::runtime_error(preview.dump());return preview;}
const ark::Body& body(const ark::AgentSession& s,const char* name){for(const auto& f:s.document.features)if(f.name==name)for(const auto& b:s.document.evalBodies)if(b.id==f.id)return b;throw std::runtime_error("body missing");}
double savedVersion(const ark::Document& d){Value root;check(arkjson::parse(ark::documentToJson(d,true,false),root),"saved json");return root.find("version")->num;}
void documentContract(){
    ark::AgentSession s;apply(s,R"([
      {"op":"feature.add","type":"native_solid","name":"Seed","native":{"op":"cylinder"},"p":{"radius":0.5,"height":2}},
      {"op":"feature.add","type":"curve","name":"Guide","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[40,0,0]},{"kind":"arc","to":[50,10,0],"radius":10},{"kind":"line","to":[50,40,0]}]}},
      {"op":"feature.add","type":"native_solid","name":"Card","native":{"op":"box"},"p":{"width":80,"depth":80,"height":5}}])");
    check(savedVersion(s.document)<47,"no 044B feature keeps the earlier format");
    const Value preview=plan(s,R"({"path":"Guide","feature":"Seed","name":"Stitches","mode":"solid","pitch_mm":5,"support":"Card","support_faces":["z_max"],"orientation":"surface","project":true,"offset_mm":0.5,"reference_scheme":"compact"})");check(preview.find("curve_pattern_044b")->b,"wrapper marks a 044B spec");
    accepted(s.call("doc_apply",*preview.find("apply")));const auto& result=body(s,"Stitches").nativeData;const auto& r=*result.find("report")->find("curve_pattern");
    check(r.find("path_source")->str=="feature_wire"&&r.find("path_edges")->num==3,"whole curve feature path through doc_apply");const double length=40+5*std::acos(-1.)+30;close(r.find("path_length_mm")->num,length,1e-6,"curve feature length");
    check(r.find("copies")->num==18,"pitch fit copies");close(r.find("surface")->find("max_deviation_mm")->num,5,1e-9,"support deviation");check(result.find("report")->find("solids")->num==18,"18 solids published");
    for(const auto& frame:copyFrames(result)){close(origin(frame).Z(),5.5,1e-9,"projected onto the support top face with offset");close(axis(frame,1).Z(),1,1e-12,"local X follows the support normal");}
    const auto consumed=ark::consumedFeatureInputs(s.document);check(consumed.count(s.document.features[0].id)&&consumed.count(s.document.features[1].id)&&!consumed.count(s.document.features[2].id),"seed/path consumed; support remains visible");
    check(savedVersion(s.document)==47,"044B pattern saves version 47");const std::string saved=ark::documentToJson(s.document,true,false);
    ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),error);restored.recompute();check(restored.lastResults.back().ok,"cold rebuild");
    close(restored.evalBodies.back().nativeData.find("report")->find("volume_mm3")->num,result.find("report")->find("volume_mm3")->num,1e-6,"cold rebuild parity");
    Value older;check(arkjson::parse(saved,older),"older");older.set("version",Value::makeNum(46));ark::Document rejected;check(!ark::documentFromJson(older.dump(),rejected,&error)&&error.find("version 47")!=std::string::npos,"version 46 cannot carry 044B fields");
    // Seed edits regenerate the linked pattern; a removed support face rejects explicitly.
    apply(s,R"([{"op":"feature.edit","id":"Seed","p":{"radius":0.4}}])");check(body(s,"Stitches").nativeData.find("report")->find("curve_pattern")->find("copies")->num==18,"seed edit regenerates");
    // Legacy pattern_plan output stays a version-19 single-edge spec.
    ark::AgentSession legacy;apply(legacy,R"([{"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":10,"depth":20,"height":30}}])");
    const Value old=plan(legacy,R"({"path":"Block","reference":"edge.x.ymin.zmin","count":3,"margin_mm":1,"name":"Copies","feature":"Block","mode":"solid"})");check(!old.find("curve_pattern_044b")->b,"legacy wrapper output stays legacy");
    const auto& command=old.find("recipe")->find("commands")->arr[0];check(command.find("native")->obj.size()==4,"legacy native payload unchanged: op, reference, count, margin_mm");
    accepted(legacy.call("doc_apply",*old.find("apply")));check(savedVersion(legacy.document)==19,"legacy pattern keeps version 19");
    std::cout<<"document contract: doc_apply 044B pattern, support stays visible, version 47 round-trip, legacy stays 19\n";
}
void documentScale(){
    for(int count:{500,2000}){ark::AgentSession s;apply(s,R"([
      {"op":"feature.add","type":"native_solid","name":"Seed","native":{"op":"cylinder"},"p":{"radius":0.4,"height":3}},
      {"op":"feature.add","type":"curve","name":"Loop","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[300,0,0]},{"kind":"arc","to":[350,50,0],"radius":50},{"kind":"line","to":[350,200,0]},{"kind":"line","to":[0,200,0]},{"kind":"line","to":[0,0,0]}]}}])");
        std::string commands=R"([{"op":"feature.add","type":"native_solid","name":"Stitches","inputs":["Seed","Loop"],"native":{"op":"curve_pattern","x_hint":[0,0,1],"reference_scheme":"compact","count":)"+std::to_string(count)+"}}]";
        const auto started=std::chrono::steady_clock::now();apply(s,commands.c_str());const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        const auto& r=*body(s,"Stitches").nativeData.find("report")->find("curve_pattern");check(r.find("copies")->num==count&&r.find("closed")->b,"document-scale closed loop");
        const auto reload=std::chrono::steady_clock::now();ark::Document restored;std::string error;check(ark::documentFromJson(ark::documentToJson(s.document,true,false),restored,&error),error);restored.recompute();check(restored.lastResults.back().ok,"document-scale cold rebuild");
        std::cout<<"document scale: "<<count<<" copies doc_apply "<<ms<<" ms, cold reload+recompute "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-reload).count()<<" ms\n";
    }
}
void scale(){
    const Value path=closedSpline(),seed=solid(R"({"op":"cylinder","radius":0.25,"height":1.5})");
    for(int count:{500,2000}){Value native=parse(R"({"x_hint":[0,0,1],"reference_scheme":"compact"})");native.set("count",Value::makeNum(count));
        const auto started=std::chrono::steady_clock::now();const Value result=pattern(seed,path,native);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        check(result.find("report")->find("solids")->num==count,"scaled solid census");const size_t bytes=occt::base64Decode(result.find("brep_base64")->str).size(),rows=result.find("topology")->find("references")->arr.size();
        std::cout<<"scale: "<<count<<" copies in "<<ms<<" ms, brep_bytes="<<bytes<<", reference_rows="<<rows<<", mesh_vertices="<<result.find("mesh")->find("pos")->arr.size()/3<<"\n";
    }
}
}
int main(){std::cout<<std::unitbuf;try{legacySingleEdgeUnchanged();chainedLineArcSpline();closedLoopWithoutSeamDuplicate();closedPolygonWholeFeature();pitchSpacing();surfaceOrientation();frenetFrames();rejections();documentContract();scale();documentScale();std::cout<<"curve pattern 044B tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
