#include "ark/occt_sheet.hpp"
#include "common.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace ark::occt {
namespace {
constexpr double linearTol=1e-6;
constexpr double pi=3.141592653589793238462643383279502884;
struct P2 {double x=0,y=0;};
struct Envelope {double xmin=0,xmax=0,ymin=0,ymax=0;};
struct Opening {
    int panel=0;std::string kind;P2 center;double radius=0,area=0;
    std::vector<P2> points;Envelope envelope;
};
const V& required(const V& object,const char* name){
    const V* value=object.find(name);
    if(!value)throw std::runtime_error(std::string("Missing sheet field: ")+name);
    return *value;
}
double finiteNumber(const V& value,const char* name){
    if(value.t!=V::Num||!std::isfinite(value.num))
        throw std::runtime_error(std::string("Sheet field must be a finite number: ")+name);
    return value.num;
}
double number(const V& object,const char* name){return finiteNumber(required(object,name),name);}
std::string string(const V& object,const char* name){
    const V& value=required(object,name);
    if(value.t!=V::Str)throw std::runtime_error(std::string("Sheet field must be text: ")+name);
    return value.str;
}
V pairJson(P2 p){V result=V::makeArr();result.arr={V::makeNum(p.x),V::makeNum(p.y)};return result;}
double cross(P2 a,P2 b,P2 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool onSegment(P2 a,P2 b,P2 p){
    return std::abs(cross(a,b,p))<=linearTol && p.x>=std::min(a.x,b.x)-linearTol &&
           p.x<=std::max(a.x,b.x)+linearTol && p.y>=std::min(a.y,b.y)-linearTol &&
           p.y<=std::max(a.y,b.y)+linearTol;
}
bool intersects(P2 a,P2 b,P2 c,P2 d){
    const double abC=cross(a,b,c),abD=cross(a,b,d),cdA=cross(c,d,a),cdB=cross(c,d,b);
    if((abC>linearTol&&abD<-linearTol||abC<-linearTol&&abD>linearTol)&&
       (cdA>linearTol&&cdB<-linearTol||cdA<-linearTol&&cdB>linearTol))return true;
    return onSegment(a,b,c)||onSegment(a,b,d)||onSegment(c,d,a)||onSegment(c,d,b);
}
bool envelopeTouch(const Envelope& a,const Envelope& b){
    return a.xmin<=b.xmax+linearTol&&b.xmin<=a.xmax+linearTol&&
           a.ymin<=b.ymax+linearTol&&b.ymin<=a.ymax+linearTol;
}
std::vector<Opening> openingsFrom(const V& request,double legA,double legB,double width){
    const V* list=request.find("openings");
    if(!list)return {};
    if(list->t!=V::Arr||list->arr.size()>128)throw std::runtime_error("Expected at most 128 sheet openings");
    std::vector<Opening> openings;
    for(const V& item:list->arr){
        const double panelNumber=number(item,"panel");
        if(panelNumber!=0&&panelNumber!=1)throw std::runtime_error("Opening panel must be 0 or 1");
        Opening o;o.panel=static_cast<int>(panelNumber);o.kind=string(item,"kind");
        const double leg=o.panel==0?legA:legB;
        if(o.kind=="circle"){
            o.center={number(item,"x"),number(item,"y")};o.radius=number(item,"radius");
            if(o.radius<=0)throw std::runtime_error("Opening radius must be positive");
            o.envelope={o.center.x-o.radius,o.center.x+o.radius,o.center.y-o.radius,o.center.y+o.radius};
            o.area=pi*o.radius*o.radius;
        }else if(o.kind=="polygon"){
            const V& points=required(item,"points");
            if(points.t!=V::Arr||points.arr.size()<3||points.arr.size()>64)
                throw std::runtime_error("Polygon opening requires 3..64 points");
            o.envelope={INFINITY,-INFINITY,INFINITY,-INFINITY};
            for(const V& point:points.arr){
                if(point.t!=V::Arr||point.arr.size()!=2)throw std::runtime_error("Invalid opening polygon point");
                P2 p{finiteNumber(point.arr[0],"polygon x"),finiteNumber(point.arr[1],"polygon y")};
                o.points.push_back(p);o.envelope.xmin=std::min(o.envelope.xmin,p.x);
                o.envelope.xmax=std::max(o.envelope.xmax,p.x);
                o.envelope.ymin=std::min(o.envelope.ymin,p.y);
                o.envelope.ymax=std::max(o.envelope.ymax,p.y);
            }
            double signedArea=0,cx=0,cy=0;
            for(size_t i=0;i<o.points.size();++i){
                const P2 a=o.points[i],b=o.points[(i+1)%o.points.size()];
                const double term=a.x*b.y-b.x*a.y;
                signedArea+=term;cx+=(a.x+b.x)*term;cy+=(a.y+b.y)*term;
                if(std::hypot(a.x-b.x,a.y-b.y)<=linearTol)throw std::runtime_error("Degenerate opening polygon edge");
            }
            if(std::abs(signedArea)<=2*linearTol*linearTol)throw std::runtime_error("Opening polygon has zero area");
            o.center={cx/(3*signedArea),cy/(3*signedArea)};o.area=std::abs(signedArea)/2;
            for(size_t i=0;i<o.points.size();++i)
                for(size_t j=i+1;j<o.points.size();++j){
                    if(j==i+1||(i==0&&j+1==o.points.size()))continue;
                    if(intersects(o.points[i],o.points[(i+1)%o.points.size()],
                                  o.points[j],o.points[(j+1)%o.points.size()]))
                        throw std::runtime_error("Self-intersecting opening polygon");
                }
            if(signedArea<0)std::reverse(o.points.begin(),o.points.end());
        }else throw std::runtime_error("Unsupported sheet opening kind");
        if(o.envelope.xmin<=linearTol||o.envelope.xmax>=leg-linearTol||
           o.envelope.ymin<=linearTol||o.envelope.ymax>=width-linearTol)
            throw std::runtime_error("Sheet opening reaches panel edge or finite hinge");
        for(const Opening& previous:openings)
            if(previous.panel==o.panel&&envelopeTouch(previous.envelope,o.envelope))
                throw std::runtime_error("Sheet opening envelopes overlap or touch");
        openings.push_back(std::move(o));
    }
    return openings;
}
gp_Pnt formedPoint(const Opening& o,P2 p,double offset,double legA,const gp_XYZ& childInner,
                   const gp_XYZ& tangent,const gp_XYZ& radial){
    if(o.panel==0)return gp_Pnt(p.x-legA,offset,p.y);
    return gp_Pnt(childInner+tangent*p.x+gp_XYZ(0,0,p.y)+radial*offset);
}
gp_Pnt flatPoint(const Opening& o,P2 p,double offset,double legA,double allowance){
    return gp_Pnt((o.panel==0?p.x:legA+allowance+p.x),offset,p.y);
}
TopoDS_Shape cutter(const Opening& o,bool formed,double legA,double allowance,double thickness,
                    const gp_XYZ& childInner,const gp_XYZ& tangent,const gp_XYZ& radial){
    const auto point=[&](P2 p,double offset){return formed?formedPoint(o,p,offset,legA,childInner,tangent,radial):
                                                flatPoint(o,p,offset,legA,allowance);};
    const gp_XYZ extrusion=formed&&o.panel==1?radial*(thickness+2):gp_XYZ(0,thickness+2,0);
    if(o.kind=="circle"){
        return BRepPrimAPI_MakeCylinder(gp_Ax2(point(o.center,-1),gp_Dir(extrusion)),
                                        o.radius,thickness+2).Shape();
    }
    BRepBuilderAPI_MakePolygon wire;
    for(P2 p:o.points)wire.Add(point(p,-1));
    wire.Close();
    if(!wire.IsDone())throw std::runtime_error("Invalid sheet opening wire");
    const TopoDS_Face face=BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    if(!BRepCheck_Analyzer(face).IsValid())throw std::runtime_error("Invalid sheet opening face");
    const TopoDS_Shape prism=BRepPrimAPI_MakePrism(face,gp_Vec(extrusion)).Shape();
    if(!BRepCheck_Analyzer(prism).IsValid())throw std::runtime_error("Invalid sheet opening cutter");
    return prism;
}
V openingRecord(const Opening& o,double legA,double allowance,double thickness,
                const gp_XYZ& childInner,const gp_XYZ& tangent,const gp_XYZ& radial){
    const gp_Pnt formed=formedPoint(o,o.center,thickness/2,legA,childInner,tangent,radial);
    const gp_Pnt flat=flatPoint(o,o.center,thickness/2,legA,allowance);
    V record=V::makeObj();record.set("panel",V::makeNum(o.panel));record.set("kind",V::makeStr(o.kind));
    record.set("local_center",pairJson(o.center));record.set("formed_center",vec(formed.XYZ()));
    record.set("flat_center",vec(flat.XYZ()));record.set("area_mm2",V::makeNum(o.area));
    if(o.kind=="circle")record.set("radius_mm",V::makeNum(o.radius));
    else{V points=V::makeArr();for(P2 p:o.points)points.arr.push_back(pairJson(p));record.set("points",points);}
    return record;
}
} // namespace

V modelSheetNative(const V& request){
    const double width=number(request,"width"),legA=number(request,"leg_a"),legB=number(request,"leg_b"),
                 thickness=number(request,"thickness"),radius=number(request,"inner_radius"),
                 degrees=number(request,"angle"),k=number(request,"k_factor");
    if(!(width>0&&width<=10000&&legA>0&&legA<=1e6&&legB>0&&legB<=1e6&&
         thickness>0&&thickness<=10000&&radius>0&&degrees>0&&degrees<170&&k>=0&&k<=1))
        throw std::runtime_error("Unsupported sheet dimensions, angle or declared K factor");
    const double angle=degrees*pi/180,allowance=angle*(radius+k*thickness),flatLength=legA+allowance+legB;
    const auto openings=openingsFrom(request,legA,legB,width);
    const gp_XYZ center(0,radius+thickness,0),radial(std::sin(angle),-std::cos(angle),0),
                 tangent(std::cos(angle),std::sin(angle),0),childInner=center+radial*radius;
    TopoDS_Shape root=BRepPrimAPI_MakeBox(gp_Pnt(-legA,0,0),legA,thickness,width).Shape();
    // This cylindrical Boolean construction is independent of the source STEP
    // importer's arc-and-line sector construction.
    const gp_Ax2 axis(gp_Pnt(center),gp_Dir(0,0,1),gp_Dir(0,-1,0));
    const TopoDS_Shape outer=BRepPrimAPI_MakeCylinder(axis,radius+thickness,width,angle).Shape();
    const TopoDS_Shape inner=BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(center+gp_XYZ(0,0,-1)),gp_Dir(0,0,1)),radius,width+2).Shape();
    const TopoDS_Shape hinge=cut(outer,inner);
    gp_Trsf childFrame;childFrame.SetValues(tangent.X(),0,radial.X(),childInner.X(),
                                            tangent.Y(),0,radial.Y(),childInner.Y(),
                                            0,1,0,0);
    const TopoDS_Shape child=transformed(BRepPrimAPI_MakeBox(legB,width,thickness).Shape(),childFrame);
    TopoDS_Shape formed=fuse(fuse(root,hinge),child);
    TopoDS_Shape flat=BRepPrimAPI_MakeBox(flatLength,thickness,width).Shape();
    double openingArea=0;V records=V::makeArr();
    for(const Opening& opening:openings){
        formed=cut(formed,cutter(opening,true,legA,allowance,thickness,childInner,tangent,radial));
        flat=cut(flat,cutter(opening,false,legA,allowance,thickness,childInner,tangent,radial));
        records.arr.push_back(openingRecord(opening,legA,allowance,thickness,childInner,tangent,radial));
        openingArea+=opening.area;
    }
    if(!BRepCheck_Analyzer(formed).IsValid()||shapes(formed,TopAbs_SOLID).size()!=1||
       !BRepCheck_Analyzer(flat).IsValid()||shapes(flat,TopAbs_SOLID).size()!=1)
        throw std::runtime_error("Formed and flat sheet must each be one valid connected solid");
    const double formedVolume=properties(formed),flatVolume=properties(flat),removed=openingArea*thickness;
    const double expectedFormed=width*thickness*(legA+legB+angle*(radius+thickness/2))-removed;
    const double expectedFlat=width*thickness*flatLength-removed;
    const double expectedDelta=angle*width*thickness*thickness*(.5-k);
    const double measuredDelta=formedVolume-flatVolume;
    const double volumeTol=std::max(1e-5,std::abs(expectedFormed)*1e-8);
    if(std::abs(formedVolume-expectedFormed)>volumeTol||
       std::abs(flatVolume-expectedFlat)>volumeTol||
       std::abs(measuredDelta-expectedDelta)>volumeTol)
        throw std::runtime_error("Sheet B-rep volume disagrees with independent bend and opening integrals");
    V result=shapeResult(formed,true),report=*result.find("report");
    result.set("flat_mesh",meshData(flat));result.set("flat_brep_base64",V::makeStr(brepData(flat)));
    result.set("holes",records);
    report.set("model",V::makeStr("designed_finite_cylindrical_sheet"));
    report.set("width_mm",V::makeNum(width));report.set("leg_a_mm",V::makeNum(legA));
    report.set("leg_b_mm",V::makeNum(legB));report.set("thickness_mm",V::makeNum(thickness));
    report.set("inner_radius_mm",V::makeNum(radius));report.set("angle_deg",V::makeNum(degrees));
    report.set("k_factor",V::makeNum(k));report.set("bend_allowance_mm",V::makeNum(allowance));
    report.set("flat_length_mm",V::makeNum(flatLength));report.set("formed_volume_mm3",V::makeNum(formedVolume));
    report.set("flat_volume_mm3",V::makeNum(flatVolume));
    report.set("analytic_formed_volume_mm3",V::makeNum(expectedFormed));
    report.set("analytic_flat_volume_mm3",V::makeNum(expectedFlat));
    report.set("opening_count",V::makeNum(static_cast<double>(openings.size())));
    report.set("opening_removed_volume_mm3",V::makeNum(removed));
    report.set("volume_delta_mm3",V::makeNum(measuredDelta));
    report.set("expected_volume_delta_mm3",V::makeNum(expectedDelta));
    report.set("volume_delta_error_mm3",V::makeNum(std::abs(measuredDelta-expectedDelta)));
    report.set("volume_tolerance_mm3",V::makeNum(volumeTol));
    report.set("flat_valid",V::makeBool(true));report.set("flat_solids",V::makeNum(1));
    report.set("geometric_material_conservation",V::makeBool(k==.5));
    report.set("source_projection_verified",V::makeBool(false));
    report.set("manufacturing_authoritative",V::makeBool(false));
    report.set("k_factor_scope",V::makeStr("declared flat-length design input; no material calibration inferred"));
    result.set("report",report);return result;
}
} // namespace ark::occt
