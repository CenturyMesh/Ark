#include "ark/occt_modeling.hpp"
#include "ark/occt_solid.hpp"
#include "common.hpp"
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomConvert.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <cmath>
#include <stdexcept>

namespace ark::occt {
namespace {
const V& need(const V& value,const char* key){auto p=value.find(key);if(!p)throw std::runtime_error(std::string("Missing projection field: ")+key);return *p;}
gp_Trsf frame(const V& request,const char* key){
    gp_Trsf result;auto p=request.find(key);if(!p)return result;
    if(p->t!=V::Arr||p->arr.size()!=16)throw std::runtime_error("Projection frame needs 16 numbers");
    double m[16];for(int i=0;i<16;++i){if(p->arr[i].t!=V::Num||!std::isfinite(p->arr[i].num))throw std::runtime_error("Non-finite projection frame");m[i]=p->arr[i].num;}
    if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-10)throw std::runtime_error("Projection frame must be affine");
    for(int a=0;a<3;++a)for(int b=a;b<3;++b){double dot=0;for(int c=0;c<3;++c)dot+=m[4*a+c]*m[4*b+c];if(std::abs(dot-(a==b?1.:0.))>1e-10)throw std::runtime_error("Projection frame must be rigid");}
    result.SetValues(m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]);
    if(result.IsNegative())throw std::runtime_error("Projection frame must preserve orientation");return result;
}
gp_Pnt projected(gp_Pnt point,const gp_Pnt& origin,const gp_Dir& normal){return point.Translated(gp_Vec(normal)*(-gp_Vec(origin,point).Dot(gp_Vec(normal))));}
TopoDS_Edge projectEdge(const TopoDS_Edge& source,const gp_Pnt& origin,const gp_Dir& normal){
    BRepAdaptor_Curve adaptor(source);const auto kind=adaptor.GetType();
    if(kind!=GeomAbs_Line&&kind!=GeomAbs_Circle&&kind!=GeomAbs_Ellipse&&kind!=GeomAbs_BezierCurve&&kind!=GeomAbs_BSplineCurve)throw std::runtime_error("Exact projection supports lines, circles/arcs, ellipses, Bezier and B-spline curves");
    TopLoc_Location location;double first=0,last=0;auto curve=BRep_Tool::Curve(source,location,first,last);
    if(curve.IsNull()||!std::isfinite(first)||!std::isfinite(last))throw std::runtime_error("Projection needs a bounded exact curve");
    curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));TopoDS_Edge edge;
    if(kind==GeomAbs_Line){
        auto a=projected(curve->Value(first),origin,normal),b=projected(curve->Value(last),origin,normal);
        if(a.Distance(b)<1e-8)throw std::runtime_error("Projection collapses an edge to a point");
        BRepBuilderAPI_MakeEdge maker(a,b);if(!maker.IsDone())throw std::runtime_error("Projected line construction failed");edge=maker.Edge();
    }else{
        // Affine projection of an exact rational representation. No fitting or polyline reconstruction.
        occ::handle<Geom_TrimmedCurve> trimmed=new Geom_TrimmedCurve(curve,first,last);
        auto spline=GeomConvert::CurveToBSplineCurve(trimmed);
        for(int i=1;i<=spline->NbPoles();++i)spline->SetPole(i,projected(spline->Pole(i),origin,normal));
        BRepBuilderAPI_MakeEdge maker(spline);if(!maker.IsDone())throw std::runtime_error("Projected exact curve construction failed");edge=maker.Edge();
        BRepAdaptor_Curve check(edge);if(GCPnts_AbscissaPoint::Length(check)<1e-8)throw std::runtime_error("Projection collapses a curve");
        if((kind==GeomAbs_Circle||kind==GeomAbs_Ellipse)&&source.Closed()){
            gp_Vec x=kind==GeomAbs_Circle?gp_Vec(adaptor.Circle().XAxis().Direction()):gp_Vec(adaptor.Ellipse().XAxis().Direction());
            gp_Vec y=kind==GeomAbs_Circle?gp_Vec(adaptor.Circle().YAxis().Direction()):gp_Vec(adaptor.Ellipse().YAxis().Direction());
            x-=gp_Vec(normal)*x.Dot(gp_Vec(normal));y-=gp_Vec(normal)*y.Dot(gp_Vec(normal));
            if(x.Crossed(y).Magnitude()<1e-10)throw std::runtime_error("Projection collapses a closed conic to a retraced line");
        }
    }
    if(source.Orientation()==TopAbs_REVERSED)edge.Reverse();return edge;
}
}
V projectReferenceNative(const V& request){
    V selected;
    if(auto profile=request.find("input_profile")){selected=profileCurveNative(*profile);selected.set("kind",V::makeStr("wire"));}
    else selected=geometryReferenceShapeNative(request);
    auto source=readBrep(need(selected,"brep_base64").str);
    source=transformed(source,frame(request,"source_frame"));
    const auto* modeValue=request.find("mode");const std::string mode=modeValue&&modeValue->t==V::Str?modeValue->str:"normal";
    if(mode!="normal"&&mode!="legacy"&&mode!="3d")throw std::runtime_error("Projection mode must be normal or 3d");
    const auto destination=frame(request,"plane_frame");const gp_Pnt origin=gp_Pnt(0,0,0).Transformed(destination);const gp_Dir normal=gp_Dir(0,0,1).Transformed(destination);bool section=false;
    if(selected.find("kind")->str=="face"){
        if(mode=="legacy"&&std::abs(BRepAdaptor_Surface(TopoDS::Face(source)).Plane().Axis().Direction().Dot(normal))<1-1e-10){
            BRepAlgoAPI_Section cut(source,gp_Pln(origin,normal),false);cut.Approximation(false);cut.Build();if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Exact face-plane section failed");source=cut.Shape();section=true;
        }else{auto wires=shapes(source,TopAbs_WIRE);if(wires.size()!=1)throw std::runtime_error("Face projection requires one boundary wire; choose individual edges for holes");source=wires[0];}
    }
    const auto edges=shapes(source,TopAbs_EDGE);if(edges.empty()||edges.size()>64)throw std::runtime_error("Projection needs 1..64 connected exact edges");
    BRepBuilderAPI_MakeWire builder;
    if(source.ShapeType()==TopAbs_WIRE){for(BRepTools_WireExplorer walk(TopoDS::Wire(source));walk.More();walk.Next())builder.Add(mode=="3d"||section?walk.Current():projectEdge(walk.Current(),origin,normal));}
    else for(const auto& edge:edges)builder.Add(mode=="3d"||section?TopoDS::Edge(edge):projectEdge(TopoDS::Edge(edge),origin,normal));
    if(!builder.IsDone()||shapes(builder.Wire(),TopAbs_EDGE).size()!=edges.size())throw std::runtime_error("Projection produced disconnected or ambiguous wires");
    V internal=V::makeObj();internal.set("op",V::makeStr("curve_brep"));internal.set("wire_brep",V::makeStr(brepData(builder.Wire())));
    V result=modelCurveNative(internal),polyline=V::makeArr();double tolerance=.025;
    if(auto t=request.find("display_tolerance")){if(t->t!=V::Num||!std::isfinite(t->num)||t->num<.0001||t->num>5)throw std::runtime_error("Display tolerance must be .0001..5 mm");tolerance=t->num;}
    for(BRepTools_WireExplorer walk(builder.Wire());walk.More();walk.Next()){
        auto edge=walk.Current();BRepAdaptor_Curve curve(edge);GCPnts_QuasiUniformDeflection samples(curve,tolerance,GeomAbs_C0);
        if(!samples.IsDone()||samples.NbPoints()<2||polyline.arr.size()+samples.NbPoints()>4096)throw std::runtime_error("Projected display exceeds 4096 samples");
        for(int i=1;i<=samples.NbPoints();++i){int index=edge.Orientation()==TopAbs_REVERSED?samples.NbPoints()+1-i:i;if(i==1&&!polyline.arr.empty())continue;polyline.arr.push_back(vec(samples.Value(index).XYZ()));}
    }
    auto report=need(result,"report");report.set("projection_exact",V::makeBool(true));report.set("projection_mode",V::makeStr(mode));report.set("geometry_authority",V::makeStr("exact_brep"));result.set("report",report);
    result.set("polyline_world",polyline);result.set("local_brep_base64",V::makeStr(brepData(transformed(builder.Wire(),destination.Inverted()))));return result;
}
}
