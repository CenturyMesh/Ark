#include "common.hpp"
#include "ark/occt_electronics.hpp"
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace ark::occt {
namespace {
const V& need(const V& source,const char* key){const V* value=source.find(key);if(!value)throw std::runtime_error(std::string("Electronics native request needs ")+key);return *value;}
double scalar(const V& source,const char* label){if(source.t!=V::Num||!std::isfinite(source.num)||std::abs(source.num)>1e6)throw std::runtime_error(std::string("Invalid native electronics number: ")+label);return source.num;}
double number(const V& source,const char* key){return scalar(need(source,key),key);}
std::string word(const V& source,const char* key){const V& value=need(source,key);if(value.t!=V::Str||value.str.empty()||value.str.size()>128)throw std::runtime_error(std::string("Invalid electronics name: ")+key);return value.str;}
const V& sequence(const V& source,const char* key,size_t max){const V& value=need(source,key);if(value.t!=V::Arr||value.arr.size()>max)throw std::runtime_error(std::string("Invalid electronics array: ")+key);return value;}
gp_Pnt xy(const V& value,double z=0){if(value.t!=V::Arr||value.arr.size()!=2)throw std::runtime_error("Expected XY coordinate");return gp_Pnt(scalar(value.arr[0],"x"),scalar(value.arr[1],"y"),z);}
gp_Pnt xyz(const V& value){if(value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error("Expected XYZ coordinate");return gp_Pnt(scalar(value.arr[0],"x"),scalar(value.arr[1],"y"),scalar(value.arr[2],"z"));}
TopoDS_Shape slab(const std::vector<gp_Pnt>& points,double z,double depth){
    if(points.size()<3||depth<=0)throw std::runtime_error("Invalid electronics prism");
    BRepBuilderAPI_MakePolygon polygon;for(const auto& p:points)polygon.Add(gp_Pnt(p.X(),p.Y(),z));polygon.Close();if(!polygon.IsDone())throw std::runtime_error("Electronics polygon failed");
    BRepBuilderAPI_MakeFace face(polygon.Wire());if(!face.IsDone())throw std::runtime_error("Electronics face failed");
    BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,0,depth));if(!prism.IsDone())throw std::runtime_error("Electronics prism failed");return prism.Shape();
}
TopoDS_Shape disk(double x,double y,double z,double radius,double depth){if(radius<=0||depth<=0)throw std::runtime_error("Invalid electronics disk");return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,y,z),gp_Dir(0,0,1)),radius,depth).Shape();}
TopoDS_Shape rectangle(double x,double y,double z,double width,double height,double depth){if(width<=0||height<=0||depth<=0)throw std::runtime_error("Invalid electronics rectangle");return BRepPrimAPI_MakeBox(gp_Pnt(x-width/2,y-height/2,z),width,height,depth).Shape();}
V modelPcb(const V& request){
    const V& outline=sequence(request,"outline",128);if(outline.arr.size()<3)throw std::runtime_error("PCB outline needs three vertices");double thickness=number(request,"thickness");if(thickness<=0||thickness>20)throw std::runtime_error("Invalid PCB thickness");
    std::vector<gp_Pnt> polygon;for(const V& p:outline.arr)polygon.push_back(xy(p));TopoDS_Shape board=slab(polygon,0,thickness);
    for(const V& row:sequence(request,"holes",128).arr){double x=number(row,"x"),y=number(row,"y"),diameter=number(row,"diameter");if(diameter<=0)throw std::runtime_error("Invalid PCB hole");BRepAlgoAPI_Cut cut(board,disk(x,y,-.1,diameter/2,thickness+.2));cut.Build();if(!cut.IsDone())throw std::runtime_error("PCB hole cut failed");board=cut.Shape();}
    for(const V& row:sequence(request,"vias",128).arr){double x=number(row,"x"),y=number(row,"y"),drill=number(row,"drill");if(drill<=0)throw std::runtime_error("Invalid PCB via drill");BRepAlgoAPI_Cut cut(board,disk(x,y,-.1,drill/2,thickness+.2));cut.Build();if(!cut.IsDone())throw std::runtime_error("PCB via drill cut failed");board=cut.Shape();}
    std::vector<TopoDS_Shape> parts{board};std::map<std::string,std::pair<double,double>> layers;
    for(const V& layer:sequence(request,"layers",8).arr){std::string name=word(layer,"name");double z=number(layer,"z"),copper=number(layer,"copper_thickness");if(z<0||z>thickness||copper<=0||copper>.5||!layers.emplace(name,std::make_pair(z,copper)).second)throw std::runtime_error("Invalid PCB layer");}
    auto copperHeight=[&](const std::string& layer){auto found=layers.find(layer);if(found==layers.end())throw std::runtime_error("PCB copper references unknown layer");double z=found->second.first,copper=found->second.second;return std::pair<double,double>{z<1e-7?-copper:z,copper};};
    for(const V& row:sequence(request,"pads",256).arr){auto [z,depth]=copperHeight(word(row,"layer"));parts.push_back(rectangle(number(row,"x"),number(row,"y"),z,number(row,"width"),number(row,"height"),depth));}
    size_t totalTracePoints=0;for(const V& trace:sequence(request,"traces",128).arr){
        auto [z,depth]=copperHeight(word(trace,"layer"));double radius=number(trace,"width")/2;if(radius<=0)throw std::runtime_error("Invalid PCB trace width");const V& points=sequence(trace,"points",64);if(points.arr.size()<2)throw std::runtime_error("PCB trace needs two points");
        totalTracePoints+=points.arr.size();if(totalTracePoints>512)throw std::runtime_error("PCB total trace point limit is 512");
        for(size_t i=1;i<points.arr.size();++i){gp_Pnt a=xy(points.arr[i-1]),b=xy(points.arr[i]);double dx=b.X()-a.X(),dy=b.Y()-a.Y(),distance=std::hypot(dx,dy);if(distance<=1e-5)throw std::runtime_error("PCB trace has zero segment");double px=-dy/distance*radius,py=dx/distance*radius;parts.push_back(slab({gp_Pnt(a.X()-px,a.Y()-py,0),gp_Pnt(b.X()-px,b.Y()-py,0),gp_Pnt(b.X()+px,b.Y()+py,0),gp_Pnt(a.X()+px,a.Y()+py,0)},z,depth));if(i==1)parts.push_back(disk(a.X(),a.Y(),z,radius,depth));parts.push_back(disk(b.X(),b.Y(),z,radius,depth));}
    }
    for(const V& via:sequence(request,"vias",128).arr){double x=number(via,"x"),y=number(via,"y"),outer=number(via,"diameter")/2,drill=number(via,"drill")/2;if(drill<=0||outer<=drill)throw std::runtime_error("Invalid PCB via annulus");TopoDS_Shape ring=disk(x,y,-.035,outer,thickness+.07);BRepAlgoAPI_Cut cut(ring,disk(x,y,-.04,drill,thickness+.08));cut.Build();if(!cut.IsDone())throw std::runtime_error("PCB via plating failed");parts.push_back(cut.Shape());}
    for(const V& item:sequence(request,"components",128).arr)parts.push_back(rectangle(number(item,"x"),number(item,"y"),thickness+.04,number(item,"width"),number(item,"height"),number(item,"height_mm")));
    V result=shapeResult(compound(parts),false);result.set("electronics",need(request,"report"));result.set("electronics_kind",V::makeStr("pcb"));return result;
}
V modelCable(const V& request){
    const V& input=sequence(request,"path",34);if(input.arr.size()<2)throw std::runtime_error("Cable needs two endpoints");double radius=number(request,"radius"),bend=number(request,"bend_radius");if(radius<=0||bend<radius)throw std::runtime_error("Invalid cable radius");std::vector<gp_Pnt> path;for(const V& p:input.arr)path.push_back(xyz(p));
    BRepBuilderAPI_MakeWire wire;gp_Pnt cursor=path.front();
    for(size_t i=1;i+1<path.size();++i){gp_Vec incoming(path[i-1],path[i]),outgoing(path[i],path[i+1]);double a=incoming.Magnitude(),b=outgoing.Magnitude();if(a<1e-5||b<1e-5)throw std::runtime_error("Cable has a zero segment");incoming.Normalize();outgoing.Normalize();double angle=std::acos(std::clamp(incoming.Dot(outgoing),-1.,1.));if(angle<1e-7)continue;if(angle>3.13)throw std::runtime_error("Cable reverses at waypoint");double tangent=bend*std::tan(angle/2);if(tangent>=.49*std::min(a,b))throw std::runtime_error("Cable bend radius cannot fit segment");gp_Pnt enter=path[i].Translated(incoming.Multiplied(-tangent)),leave=path[i].Translated(outgoing.Multiplied(tangent));if(cursor.Distance(enter)>1e-7)wire.Add(BRepBuilderAPI_MakeEdge(cursor,enter));gp_Vec normal=incoming.Crossed(outgoing);if(normal.Magnitude()<1e-10)throw std::runtime_error("Cable bend plane degenerate");normal.Normalize();gp_Vec radial=normal.Crossed(incoming);radial.Normalize();gp_Pnt center=enter.Translated(radial.Multiplied(bend));gp_Vec r0(center,enter),r1(center,leave);gp_Vec mid=r0.Added(r1);if(mid.Magnitude()<1e-10)throw std::runtime_error("Cable bend arc degenerate");mid.Normalize();gp_Pnt middle=center.Translated(mid.Multiplied(bend));Handle(Geom_TrimmedCurve) arc=GC_MakeArcOfCircle(enter,middle,leave);wire.Add(BRepBuilderAPI_MakeEdge(arc));cursor=leave;}
    if(cursor.Distance(path.back())>1e-7)wire.Add(BRepBuilderAPI_MakeEdge(cursor,path.back()));if(!wire.IsDone())throw std::runtime_error("Cable path wire failed");TopoDS_Wire spine=wire.Wire();gp_Vec initial(path[0],path[1]);if(initial.Magnitude()<1e-5)throw std::runtime_error("Cable start direction invalid");gp_Circ circle(gp_Ax2(path[0],gp_Dir(initial)),radius);TopoDS_Edge sectionEdge=BRepBuilderAPI_MakeEdge(circle).Edge();BRepBuilderAPI_MakeWire section(sectionEdge);BRepBuilderAPI_MakeFace face(section.Wire());BRepOffsetAPI_MakePipe pipe(spine,face.Face());pipe.Build();if(!pipe.IsDone())throw std::runtime_error("Exact cable sweep failed");TopoDS_Shape shape=pipe.Shape();
    double nearest=1e300;for(const V& item:sequence(request,"obstacles",64).arr){gp_Pnt lo=xyz(need(item,"min")),hi=xyz(need(item,"max"));if(hi.X()<=lo.X()||hi.Y()<=lo.Y()||hi.Z()<=lo.Z())throw std::runtime_error("Cable obstacle bounds invalid");TopoDS_Shape block=BRepPrimAPI_MakeBox(lo,hi).Shape();BRepExtrema_DistShapeShape distance(shape,block);distance.Perform();if(!distance.IsDone())throw std::runtime_error("Exact cable obstacle check failed");nearest=std::min(nearest,distance.Value());if(distance.Value()<=1e-6)throw std::runtime_error("Exact cable collides with obstacle "+word(item,"name"));}
    GProp_GProps properties;BRepGProp::LinearProperties(spine,properties);V result=shapeResult(shape,true),report=need(request,"report");report.set("route_length_mm",V::makeNum(properties.Mass()));report.set("exact_obstacle_checked",V::makeBool(true));if(nearest<1e299)report.set("minimum_obstacle_clearance_mm",V::makeNum(nearest));result.set("electronics",report);result.set("electronics_kind",V::makeStr("cable_route"));return result;
}
#include "electronics_cables_035b.inl"
} // namespace

V modelElectronicsNative(const V& request){
    if(request.t!=V::Obj)throw std::runtime_error("Electronics request must be an object");
    const std::string kind=word(request,"kind");
    if(kind=="pcb")return modelPcb(request);
    if(kind=="cable_route"){
        if(const auto* mode=request.find("route_mode")){
            if(mode->t!=V::Str||mode->str!="spline")throw std::runtime_error("CABLE_MODE_INVALID: native route_mode must be spline");
            return modelSplineCable035B(request);
        }
        return modelCable(request);
    }
    throw std::runtime_error("Unknown electronics kind");
}

} // namespace ark::occt
