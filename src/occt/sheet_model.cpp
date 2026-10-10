#include "ark/occt_sheet.hpp"
#include "common.hpp"
#include "ark/model.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Pln.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>

namespace ark::occt {
namespace {
constexpr double linearTol=1e-6;
constexpr double pi=3.141592653589793238462643383279502884;
struct P2 {double x=0,y=0;};
struct Envelope {double xmin=0,xmax=0,ymin=0,ymax=0;};
struct Opening {
    int panel=0;std::string kind;P2 center;double radius=0,area=0;
    double length=0,width=0,height=0,angle=0;
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
        }else if(o.kind=="slot"){
            o.center={number(item,"x"),number(item,"y")};
            o.length=number(item,"length");o.width=number(item,"width");
            o.angle=item.find("angle")?number(item,"angle"):0;
            if(!(o.width>0&&o.length>o.width+linearTol&&std::abs(o.angle)<=360))
                throw std::runtime_error("Slot needs overall length greater than width and angle within 360 degrees");
            o.radius=o.width/2;o.angle*=pi/180;
            const double half=(o.length-o.width)/2,ux=std::cos(o.angle),uy=std::sin(o.angle);
            o.envelope={o.center.x-half*std::abs(ux)-o.radius,o.center.x+half*std::abs(ux)+o.radius,
                        o.center.y-half*std::abs(uy)-o.radius,o.center.y+half*std::abs(uy)+o.radius};
            o.area=(o.length-o.width)*o.width+pi*o.radius*o.radius;
        }else if(o.kind=="window"){
            o.center={number(item,"x"),number(item,"y")};
            o.width=number(item,"width");o.height=number(item,"height");
            o.angle=item.find("angle")?number(item,"angle"):0;
            if(!(o.width>linearTol&&o.height>linearTol&&std::abs(o.angle)<=360))
                throw std::runtime_error("Window needs positive width/height and angle within 360 degrees");
            o.angle*=pi/180;const double c=std::cos(o.angle),s=std::sin(o.angle);
            o.envelope={INFINITY,-INFINITY,INFINITY,-INFINITY};
            for(P2 local:{P2{-o.width/2,-o.height/2},P2{o.width/2,-o.height/2},
                          P2{o.width/2,o.height/2},P2{-o.width/2,o.height/2}}){
                P2 p{o.center.x+local.x*c-local.y*s,o.center.y+local.x*s+local.y*c};
                o.points.push_back(p);o.envelope.xmin=std::min(o.envelope.xmin,p.x);
                o.envelope.xmax=std::max(o.envelope.xmax,p.x);
                o.envelope.ymin=std::min(o.envelope.ymin,p.y);
                o.envelope.ymax=std::max(o.envelope.ymax,p.y);
            }
            o.area=o.width*o.height;
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
    if(o.kind=="slot"){
        const double half=(o.length-o.width)/2,ux=std::cos(o.angle),uy=std::sin(o.angle);
        const P2 start{o.center.x-half*ux,o.center.y-half*uy},end{o.center.x+half*ux,o.center.y+half*uy};
        const P2 n{-uy*o.radius,ux*o.radius};
        const P2 corners[4]={{start.x+n.x,start.y+n.y},{end.x+n.x,end.y+n.y},
                             {end.x-n.x,end.y-n.y},{start.x-n.x,start.y-n.y}};
        BRepBuilderAPI_MakePolygon wire;for(P2 corner:corners)wire.Add(point(corner,-1));wire.Close();
        if(!wire.IsDone())throw std::runtime_error("Invalid slot center rectangle");
        const TopoDS_Shape middle=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),gp_Vec(extrusion)).Shape();
        const TopoDS_Shape roundStart=BRepPrimAPI_MakeCylinder(gp_Ax2(point(start,-1),gp_Dir(extrusion)),o.radius,thickness+2).Shape();
        const TopoDS_Shape roundEnd=BRepPrimAPI_MakeCylinder(gp_Ax2(point(end,-1),gp_Dir(extrusion)),o.radius,thickness+2).Shape();
        return fuse(fuse(middle,roundStart),roundEnd);
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
    else if(o.kind=="slot"){
        record.set("length_mm",V::makeNum(o.length));record.set("width_mm",V::makeNum(o.width));
        record.set("angle_deg",V::makeNum(o.angle*180/pi));
    }
    else if(o.kind=="window"){
        record.set("width_mm",V::makeNum(o.width));record.set("height_mm",V::makeNum(o.height));
        record.set("angle_deg",V::makeNum(o.angle*180/pi));
    }
    else{V points=V::makeArr();for(P2 p:o.points)points.arr.push_back(pairJson(p));record.set("points",points);}
    return record;
}
} // namespace
namespace {
struct Panel {
    std::string id,edge;int parent=-1;double length=0,width=0,offset=0,angle=0,radius=0,k=.5,allowance=0;
    bool hem=false;gp_Trsf flat,formed,refold,attachment;TopoDS_Shape local,formedLocal;
};
struct Piece {TopoDS_Shape flat,formed,refold;int panel=-1;bool band=false;};
double evaluated(const V& value,const ExprEnv& env){
    bool ok=value.t==V::Num;double n=ok?value.num:value.t==V::Str?evalExpr(value.str,env,&ok):0;
    if(!ok||!std::isfinite(n)||std::abs(n)>1e7)throw std::runtime_error("Invalid sheet program expression");return n;
}
double expr(const V& item,const char* name,const ExprEnv& env){return evaluated(required(item,name),env);}
double optional(const V& item,const char* name,double fallback,const ExprEnv& env){auto p=item.find(name);return p?evaluated(*p,env):fallback;}
gp_Trsf translation(double x,double y=0,double z=0){gp_Trsf tr;tr.SetTranslation(gp_Vec(x,y,z));return tr;}
gp_Trsf rotation(double angle){gp_Trsf tr;tr.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),angle);return tr;}
std::string named(const V& item,const char* key){auto n=string(item,key);if(n.empty()||n.size()>128||n.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::runtime_error("Sheet identities must use 1..128 letters, digits, dot, underscore or hyphen");return n;}
gp_Trsf edgeFrame(const Panel& parent,const Panel& panel){
    gp_Trsf frame;
    if(panel.edge=="u_max")frame=translation(parent.length,0,panel.offset);
    else if(panel.edge=="u_min")frame.SetValues(-1,0,0,0,0,1,0,0,0,0,-1,parent.width-panel.offset);
    else if(panel.edge=="v_max")frame.SetValues(0,0,-1,parent.length-panel.offset,0,1,0,0,1,0,0,parent.width);
    else if(panel.edge=="v_min")frame.SetValues(0,0,1,panel.offset,0,1,0,0,-1,0,0,0);
    else throw std::runtime_error("Flange edge must be u_min, u_max, v_min or v_max");
    return frame;
}
V numericOpening(const V& value,const ExprEnv& env,const std::string& key={}){
    if(value.t==V::Str&&key!="kind"&&key!="id"&&key!="panel")return V::makeNum(evaluated(value,env));
    if(value.t==V::Arr){V r=V::makeArr();for(auto& x:value.arr)r.arr.push_back(numericOpening(x,env,key));return r;}
    if(value.t==V::Obj){V r=V::makeObj();for(auto& kv:value.obj)r.set(kv.first,numericOpening(kv.second,env,kv.first));return r;}return value;
}
// Revolving the analytic offset section preserves normal thickness on each
// planar/conical skin. Corners are explicitly mitred, without an inferred die radius.
struct StampShape {TopoDS_Shape shape,footprint;double analyticVolume=0,footprintRadius=0;};
StampShape circularEmboss(double outer,double top,double depth,double t){
    if(!(top>t&&outer>top+2*t&&std::abs(depth)>t&&std::abs(depth)<=outer-top))
        throw std::runtime_error("Circular emboss requires top_radius>thickness, radius>top_radius+2*thickness and thickness<abs(depth)<=radius-top_radius");
    const double slope=-depth/(outer-top),q=t/2*std::sqrt(1+slope*slope);
    const double upper=(t/2-q)/slope,lower=(-t/2+q)/slope,extent=outer+t;
    const std::vector<P2> profile{{extent,0},{extent,t},{outer+upper,t},{top+upper,depth+t},
                                  {0,depth+t},{0,depth},{top+lower,depth},{outer+lower,0}};
    BRepBuilderAPI_MakePolygon polygon;double firstMoment=0;
    for(size_t i=0;i<profile.size();++i){const auto a=profile[i],b=profile[(i+1)%profile.size()];
        polygon.Add(gp_Pnt(a.x,a.y,0));firstMoment+=(a.x+b.x)*(a.x*b.y-b.x*a.y);}
    polygon.Close();const auto face=BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
    const auto shape=BRepPrimAPI_MakeRevol(face,gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),2*pi).Shape();
    if(!BRepCheck_Analyzer(shape).IsValid()||shapes(shape,TopAbs_SOLID).size()!=1)
        throw std::runtime_error("Circular emboss section did not produce one valid exact solid");
    const double volume=std::abs(firstMoment)*pi/3;
    if(std::abs(properties(shape)-volume)>std::max(1e-6,volume*1e-8))
        throw std::runtime_error("Circular emboss exact volume disagrees with independent section integral");
    return {shape,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,-1,0),gp_Dir(0,1,0)),extent,t+2).Shape(),volume,extent};
}
bool boxesMeet(const TopoDS_Shape& a,const TopoDS_Shape& b){auto x=bounds(a),y=bounds(b);for(int i=0;i<3;++i)if(x[i]>y[i+3]+linearTol||y[i]>x[i+3]+linearTol)return false;return true;}
void validatePieces(const std::vector<Piece>& parts,const std::vector<Panel>& panels,bool flat){
    for(size_t i=0;i<parts.size();++i)for(size_t j=i+1;j<parts.size();++j){
        const auto& a=parts[i];const auto& b=parts[j];const auto& sa=flat?a.flat:a.formed;const auto& sb=flat?b.flat:b.formed;
        if(!boxesMeet(sa,sb))continue;
        const double overlap=std::abs(properties(common(sa,sb)));
        const double tol=std::max(1e-7,std::min(properties(sa),properties(sb))*1e-9);
        if(overlap>tol)throw std::runtime_error(std::string(flat?"Flat layout":"Formed sheet")+" overlaps between "+panels[a.panel].id+" and "+panels[b.panel].id);
        const Piece* band=a.band?&a:b.band?&b:nullptr;const Piece* panel=a.band?&b:&a;
        const bool adjacent=band&&!panel->band&&(band->panel==panel->panel||panels[band->panel].parent==panel->panel);
        if(!adjacent){BRepExtrema_DistShapeShape distance(sa,sb);if(!distance.IsDone())throw std::runtime_error("Sheet clearance check failed");if(distance.Value()<=linearTol)throw std::runtime_error(std::string(flat?"Flat layout":"Formed sheet")+" has unrelieved nonadjacent contact: "+panels[a.panel].id+" / "+panels[b.panel].id);}
    }
}
TopoDS_Shape united(const std::vector<Piece>& pieces,int mode){
    TopoDS_Shape out;for(auto& piece:pieces){const auto& s=mode==0?piece.formed:mode==1?piece.flat:piece.refold;out=out.IsNull()?s:fuse(out,s);}return out;
}
}

V modelSheetNative(const V& input){
    V request=input;const bool legacy=!input.find("base");ExprEnv global;
    if(auto env=input.find("parameters")){if(env->t!=V::Obj)throw std::runtime_error("Sheet parameters must be object");for(auto& kv:env->obj)global.vars[kv.first]=finiteNumber(kv.second,"parameter");}
    const double width=expr(input,"width",global),t=expr(input,"thickness",global),defaultK=expr(input,"k_factor",global);
    if(!(width>0&&width<=10000&&t>0&&t<=10000&&defaultK>=0&&defaultK<=1))throw std::runtime_error("Unsupported sheet width, thickness or declared K factor");
    double legacyA=0;
    if(legacy){
        legacyA=expr(input,"leg_a",global);const double degrees=expr(input,"angle",global);
        if(degrees<=0||degrees>=170)throw std::runtime_error("Legacy L bend angle must be between 0 and 170 degrees");
        V base=V::makeObj();base.set("id",V::makeStr("panel_0"));base.set("length",V::makeNum(legacyA));request.set("base",base);
        V child=V::makeObj();child.set("id",V::makeStr("panel_1"));child.set("parent",V::makeStr("panel_0"));child.set("edge",V::makeStr("u_max"));child.set("length",required(input,"leg_b"));child.set("angle",required(input,"angle"));child.set("inner_radius",required(input,"inner_radius"));
        V flanges=V::makeArr();flanges.arr.push_back(child);request.set("flanges",flanges);
    }
    V formingOps=V::makeArr();
    if(request.find("jog")||request.find("sketch_bend")){
        if(legacy||(request.find("jog")&&request.find("sketch_bend")))throw std::runtime_error("Sheet jog/sketch bend requires one original rectangular base and one forming program");
        for(const char* key:{"flanges","hems","stamps","openings"})if(const auto* rows=request.find(key))if(rows->t!=V::Arr||!rows->arr.empty())throw std::runtime_error("Jog/sketch bend currently requires an unperforated rectangular blank without other flanges, hems or stamps");
        const bool jog=request.find("jog")!=nullptr;const V& operation=required(request,jog?"jog":"sketch_bend");const auto& original=required(request,"base");const std::string rootId=named(original,"id"),id=named(operation,"id");if(id==rootId)throw std::runtime_error("Forming operation id must differ from base id");const double originalLength=expr(original,"length",global),station=expr(operation,"station",global),radius=expr(operation,"inner_radius",global),degrees=expr(operation,"angle",global),k=optional(operation,"k_factor",defaultK,global);const double alpha=std::abs(degrees)*pi/180;
        if(!(station>0&&station<originalLength&&radius>0&&k>=0&&k<=1&&alpha>.001*pi/180&&alpha<170*pi/180))throw std::runtime_error("Unsupported jog/sketch bend station, radius, angle or K-factor");const double allowance=alpha*(radius+k*t);V base=original;base.set("length",V::makeNum(station));request.set("base",base);V flanges=V::makeArr();
        auto flange=[&](const std::string& child,const std::string& parent,double length,double angle){if(length<=1e-6)throw std::runtime_error("Forming operation leaves no positive distal panel/web in the original blank");V row=V::makeObj();row.set("id",V::makeStr(child));row.set("parent",V::makeStr(parent));row.set("edge",V::makeStr("u_max"));row.set("length",V::makeNum(length));row.set("inner_radius",V::makeNum(radius));row.set("angle",V::makeNum(angle));row.set("k_factor",V::makeNum(k));flanges.arr.push_back(row);};
        V receipt=V::makeObj();receipt.set("id",V::makeStr(id));receipt.set("kind",V::makeStr(jog?"jog":"sketch_bend"));receipt.set("original_blank_length_mm",V::makeNum(originalLength));receipt.set("station_mm",V::makeNum(station));receipt.set("station_basis",V::makeStr("start of first neutral-line bend band in original flat blank"));receipt.set("blank_extent_preserved",V::makeBool(true));receipt.set("manufacturing_qualified",V::makeBool(false));
        if(jog){const double height=expr(operation,"height",global);if(alpha>pi/2||std::abs(height)<=1e-6)throw std::runtime_error("Jog angle magnitude must be at most 90 degrees with nonzero signed height");const double web=(std::abs(height)-2*(radius+t/2)*(1-std::cos(alpha)))/std::sin(alpha),signedDegrees=height>0?std::abs(degrees):-std::abs(degrees);flange(id+".web",rootId,web,signedDegrees);flange(id+".distal",id+".web",originalLength-station-2*allowance-web,-signedDegrees);receipt.set("height_mm",V::makeNum(height));receipt.set("web_length_mm",V::makeNum(web));receipt.set("distal_parallel",V::makeBool(true));}
        else{flange(id+".folded",rootId,originalLength-station-allowance,degrees);receipt.set("sketch",required(operation,"sketch"));receipt.set("entity",required(operation,"entity"));}
        request.set("flanges",flanges);formingOps.arr.push_back(receipt);
    }
    const auto& base=required(request,"base");Panel root;root.id=named(base,"id");root.length=expr(base,"length",global);root.width=width;root.k=defaultK;
    if(!(root.length>0&&root.length<=1e6))throw std::runtime_error("Base panel length must be positive and at most 1e6 mm");
    bool legacyOrigin=legacy;if(auto p=request.find("legacy_origin")){if(p->t!=V::Bool)throw std::runtime_error("legacy_origin must be boolean");legacyOrigin=p->b;}
    if(legacyOrigin){root.formed=translation(-root.length);root.refold=root.formed;}
    if(auto flanges=request.find("flanges")){if(flanges->t!=V::Arr||flanges->arr.size()>16)throw std::runtime_error("Sheet accepts at most 16 flanges");for(const auto& row:flanges->arr)if(row.find("hem"))throw std::runtime_error("Use native.hems for open round hem attachments");}
    if(auto hems=request.find("hems")){
        if(hems->t!=V::Arr||hems->arr.size()>16)throw std::runtime_error("Sheet accepts at most 16 open round hems");
        V combined=request.find("flanges")?*request.find("flanges"):V::makeArr();
        if(combined.t!=V::Arr)throw std::runtime_error("Sheet flanges must be an array");
        for(const auto& hem:hems->arr){if(hem.find("angle"))throw std::runtime_error("Open round hem angle is fixed at 180 degrees");
            const double direction=optional(hem,"direction",1,global);if(direction!=1&&direction!=-1)throw std::runtime_error("Hem direction must be +1 or -1");
            if(hem.t!=V::Obj)throw std::runtime_error("Hem must be an object");
            const std::set<std::string> fields{"id","parent","edge","length","inner_radius","direction","width","offset","k_factor","kind"};
            for(const auto& field:hem.obj)if(!fields.count(field.first))throw std::runtime_error("Unsupported open round hem field: "+field.first);
            if(hem.find("kind")&&string(hem,"kind")!="open_round")throw std::runtime_error("Supported hem kind is open_round; closed/crushed/teardrop hems are unsupported");
            V row=hem;row.set("angle",V::makeNum(direction*180));row.set("hem",V::makeBool(true));combined.arr.push_back(row);}
        request.set("flanges",combined);
    }
    std::vector<Panel> panels{root};std::set<std::string> ids{root.id},edges;
    if(auto list=request.find("flanges")){
        if(list->t!=V::Arr||list->arr.size()>32)throw std::runtime_error("Sheet program accepts at most 32 flange/hem attachments");
        for(auto& item:list->arr){
            Panel p;if(auto flag=item.find("hem")){if(flag->t!=V::Bool)throw std::runtime_error("Invalid hem marker");p.hem=flag->b;}p.id=named(item,"id");auto parent=named(item,"parent");p.edge=string(item,"edge");
            if(!ids.insert(p.id).second)throw std::runtime_error("Duplicate sheet panel identity");
            for(size_t i=0;i<panels.size();++i)if(panels[i].id==parent)p.parent=(int)i;
            if(p.parent>=0&&panels[p.parent].hem)throw std::runtime_error("Open round hems are terminal panels");
            if(p.parent<0)throw std::runtime_error("Flange parent must be an earlier panel");
            if((p.parent>0&&p.edge=="u_min")||!edges.insert(parent+"/"+p.edge).second)throw std::runtime_error("Panel edge already has a bend attachment");
            const auto& a=panels[p.parent];ExprEnv env=global;env.vars["parent_length"]=a.length;env.vars["parent_width"]=a.width;env.vars["thickness"]=t;
            const double span=p.edge=="u_min"||p.edge=="u_max"?a.width:a.length;env.vars["edge_length"]=span;
            p.length=expr(item,"length",env);p.width=optional(item,"width",span,env);p.offset=optional(item,"offset",0,env);p.radius=expr(item,"inner_radius",env);p.angle=expr(item,"angle",env)*pi/180;p.k=optional(item,"k_factor",defaultK,env);
            if(!(p.length>0&&p.length<=1e6&&p.width>0&&p.offset>=0&&p.offset+p.width<=span&&p.radius>0&&std::abs(p.angle)>=.001*pi/180&&(p.hem?std::abs(std::abs(p.angle)-pi)<1e-12:std::abs(p.angle)<170*pi/180)&&p.k>=0&&p.k<=1))throw std::runtime_error("Unsupported flange dimensions, attachment span, radius, angle or K factor");
            p.allowance=std::abs(p.angle)*(p.radius+p.k*t);p.attachment=edgeFrame(a,p);
            p.flat=a.flat*p.attachment*translation(p.allowance);
            const double alpha=std::abs(p.angle),sign=p.angle>0?1.:-1.,mid=p.radius+t/2;
            const gp_XYZ end(mid*std::sin(alpha),t/2+sign*mid*(1-std::cos(alpha)),0),normal(-sign*std::sin(alpha),std::cos(alpha),0);
            const auto lower=end-normal*(t/2);auto local=rotation(p.angle);local.SetTranslationPart(gp_Vec(lower));p.formed=a.formed*p.attachment*local;
            // Refold derives angle from developed band length; its neutral-line
            // construction is independent of the forward midwall frame above.
            const double recovered=p.allowance/(p.radius+p.k*t),yn=sign>0?(1-p.k)*t:p.k*t,rn=p.radius+p.k*t;
            const auto joint=translation(rn*std::sin(recovered),yn+sign*rn*(1-std::cos(recovered)))*rotation(sign*recovered)*translation(-p.allowance,-yn);
            const auto flatH=a.flat*p.attachment;p.refold=a.refold*flatH*joint*flatH.Inverted();
            panels.push_back(p);
        }
    }
    std::vector<Piece> pieces;V stampRecords=V::makeArr();V records=V::makeArr(),panelRecords=V::makeArr(),bendRecords=V::makeArr(),animation=V::makeObj(),animationPanels=V::makeArr();
    double removed=0,expectedFormed=0,expectedFlat=0,expectedDelta=0,maxFrameError=0;bool conservation=true;double stampDelta=0;
    std::set<std::string> holeIds;
    const V* openings=request.find("openings");if(openings&&(openings->t!=V::Arr||openings->arr.size()>128))throw std::runtime_error("Sheet accepts at most 128 openings");
    const V* stamps=request.find("stamps");if(stamps&&(stamps->t!=V::Arr||stamps->arr.size()>32))throw std::runtime_error("Sheet accepts at most 32 circular emboss stamps");
    std::vector<bool> stampBound(stamps?stamps->arr.size():0,false);
    std::vector<bool> bound(openings?openings->arr.size():0,false);
    for(size_t index=0;index<panels.size();++index){
        auto& p=panels[index];p.local=BRepPrimAPI_MakeBox(p.length,t,p.width).Shape();
        ExprEnv env=global;env.vars["panel_length"]=p.length;env.vars["panel_width"]=p.width;env.vars["thickness"]=t;
        std::vector<TopoDS_Shape> cutters;
        if(openings)for(size_t h=0;h<openings->arr.size();++h){
            const V& original=openings->arr[h];const V& which=required(original,"panel");
            const bool matches=which.t==V::Str?which.str==p.id:legacy&&which.t==V::Num&&which.num==(double)index;
            if(!matches)continue;if(p.hem)throw std::runtime_error("Open round hem return panels do not accept openings");bound[h]=true;auto item=numericOpening(original,env);item.set("panel",V::makeNum(0));
            std::string identity=original.find("id")?named(original,"id"):"opening_"+std::to_string(h+1);
            if(!holeIds.insert(identity).second||!ids.insert(identity).second)throw std::runtime_error("Duplicate opening identity");
            V single=V::makeArr();single.arr.push_back(item);V wrapper=V::makeObj();wrapper.set("openings",single);
            const Opening opening=openingsFrom(wrapper,p.length,p.length,p.width).front();
            const auto tool=cutter(opening,false,0,0,t,{},{},{});
            for(auto& prior:cutters)if(boxesMeet(prior,tool)){BRepExtrema_DistShapeShape distance(prior,tool);if(!distance.IsDone()||distance.Value()<=linearTol)throw std::runtime_error("Sheet openings overlap or touch");}
            cutters.push_back(tool);p.local=cut(p.local,tool);removed+=opening.area*t;
            V row=V::makeObj();row.set("id",V::makeStr(identity));row.set("panel",legacy?V::makeNum((double)index):V::makeStr(p.id));row.set("kind",V::makeStr(opening.kind));row.set("local_center",pairJson(opening.center));row.set("area_mm2",V::makeNum(opening.area));
            const gp_Pnt center(opening.center.x,t/2,opening.center.y);row.set("formed_center",vec(center.Transformed(p.formed).XYZ()));row.set("flat_center",vec(center.Transformed(p.flat).XYZ()));
            if(opening.kind=="circle")row.set("radius_mm",V::makeNum(opening.radius));
            else if(opening.kind=="slot"){
                row.set("length_mm",V::makeNum(opening.length));row.set("width_mm",V::makeNum(opening.width));
                row.set("angle_deg",V::makeNum(opening.angle*180/pi));
            }else if(opening.kind=="window"){
                row.set("width_mm",V::makeNum(opening.width));row.set("height_mm",V::makeNum(opening.height));
                row.set("angle_deg",V::makeNum(opening.angle*180/pi));
                V pts=V::makeArr();for(P2 point:opening.points)pts.arr.push_back(pairJson(point));row.set("points",std::move(pts));
            }else{V pts=V::makeArr();for(auto pt:opening.points)pts.arr.push_back(pairJson(pt));row.set("points",pts);}records.arr.push_back(row);
        }
        p.formedLocal=p.local;std::vector<TopoDS_Shape> stampFootprints;
        if(stamps)for(size_t h=0;h<stamps->arr.size();++h){
            const auto& item=stamps->arr[h];if(named(item,"panel")!=p.id)continue;stampBound[h]=true;
            if(p.hem)throw std::runtime_error("Open round hem return panels do not accept stamping");
            const auto identity=named(item,"id");if(!ids.insert(identity).second)throw std::runtime_error("Duplicate sheet stamp identity");
            const std::set<std::string> fields{"id","panel","kind","x","y","radius","top_radius","depth"};
            for(const auto& field:item.obj)if(!fields.count(field.first))throw std::runtime_error("Unsupported circular emboss field: "+field.first);
            if(string(item,"kind")!="circular_emboss")throw std::runtime_error("Supported stamp kind is circular_emboss");
            const double x=expr(item,"x",env),y=expr(item,"y",env),radius=expr(item,"radius",env),top=expr(item,"top_radius",env),depth=expr(item,"depth",env);
            const auto stamp=circularEmboss(radius,top,depth,t);const double extent=stamp.footprintRadius;
            if(x-extent<=linearTol||x+extent>=p.length-linearTol||y-extent<=linearTol||y+extent>=p.width-linearTol)
                throw std::runtime_error("Stamp footprint reaches a panel edge or bend");
            const auto shift=translation(x,0,y);const auto footprint=transformed(stamp.footprint,shift);
            for(const auto& tool:cutters){BRepExtrema_DistShapeShape clearance(footprint,tool);if(!clearance.IsDone()||clearance.Value()<=linearTol)throw std::runtime_error("Stamp footprint intersects an opening");}
            for(const auto& prior:stampFootprints){BRepExtrema_DistShapeShape clearance(footprint,prior);if(!clearance.IsDone()||clearance.Value()<=linearTol)throw std::runtime_error("Stamp footprints overlap or touch");}
            stampFootprints.push_back(footprint);
            p.formedLocal=fuse(cut(p.formedLocal,footprint),transformed(stamp.shape,shift));
            const double change=stamp.analyticVolume-pi*extent*extent*t;stampDelta+=change;
            V row=V::makeObj();row.set("id",V::makeStr(identity));row.set("panel",V::makeStr(p.id));row.set("kind",V::makeStr("circular_emboss"));
            for(auto field:{std::pair{"radius_mm",radius},std::pair{"top_radius_mm",top},std::pair{"depth_mm",depth},std::pair{"footprint_radius_mm",extent},std::pair{"analytic_volume_delta_mm3",change}})row.set(field.first,V::makeNum(field.second));
            row.set("local_center",pairJson({x,y}));row.set("formed_center",vec(gp_Pnt(x,depth+t/2,y).Transformed(p.formed).XYZ()));
            row.set("wall_model",V::makeStr("constant normal thickness conical wall, planar plateau, sharp mitred transitions"));row.set("forming_strain_checked",V::makeBool(false));stampRecords.arr.push_back(row);
        }
        const auto refoldFrame=p.refold*p.flat;
        for(auto point:{gp_Pnt(0,0,0),gp_Pnt(p.length,0,0),gp_Pnt(0,t,0),gp_Pnt(0,0,p.width)})maxFrameError=std::max(maxFrameError,point.Transformed(refoldFrame).Distance(point.Transformed(p.formed)));
        if(index>0){
            const auto& parent=panels[p.parent];const double sign=p.angle>0?1.:-1.,alpha=std::abs(p.angle);
            const gp_Pnt center(0,t/2+sign*(p.radius+t/2),sign>0?0:p.width);const gp_Dir axis(0,0,sign);
            const auto outer=BRepPrimAPI_MakeCylinder(gp_Ax2(center,axis,gp_Dir(0,-sign,0)),p.radius+t,p.width,alpha).Shape();
            const auto inner=BRepPrimAPI_MakeCylinder(gp_Ax2(center.Translated(gp_Vec(0,0,-sign)),axis),p.radius,p.width+2).Shape();
            const auto sector=cut(outer,inner),band=BRepPrimAPI_MakeBox(p.allowance,t,p.width).Shape();
            // A revolved rectangular wall cross-section independently checks
            // the sector built by subtracting coaxial cylinders.
            BRepBuilderAPI_MakePolygon wire;wire.Add(gp_Pnt(0,0,0));wire.Add(gp_Pnt(0,t,0));wire.Add(gp_Pnt(0,t,p.width));wire.Add(gp_Pnt(0,0,p.width));wire.Close();
            const double measuredBA=properties(band)/(p.width*t),measuredAngle=measuredBA/(p.radius+p.k*t);
            auto revolved=BRepPrimAPI_MakeRevol(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),gp_Ax1(center,axis),measuredAngle).Shape();
            const auto flatH=parent.flat*p.attachment,formedH=parent.formed*p.attachment,refoldH=parent.refold*flatH;
            pieces.push_back({transformed(band,flatH),transformed(sector,formedH),transformed(revolved,refoldH),(int)index,true});
            expectedFormed+=p.width*t*alpha*(p.radius+t/2);expectedFlat+=p.width*t*p.allowance;expectedDelta+=alpha*p.width*t*t*(.5-p.k);conservation&=p.k==.5;
            V bend=V::makeObj();bend.set("kind",V::makeStr(p.hem?"open_round_hem":"flange"));if(p.hem){bend.set("hem_type",V::makeStr("open_round"));bend.set("gap_mm",V::makeNum(2*p.radius));}bend.set("id",V::makeStr(p.id));bend.set("parent",V::makeStr(parent.id));bend.set("edge",V::makeStr(p.edge));bend.set("angle_deg",V::makeNum(p.angle*180/pi));bend.set("inner_radius_mm",V::makeNum(p.radius));bend.set("k_factor",V::makeNum(p.k));bend.set("bend_allowance_mm",V::makeNum(p.allowance));bend.set("width_mm",V::makeNum(p.width));bend.set("offset_mm",V::makeNum(p.offset));bend.set("flat_attachment",matrix(flatH));bend.set("parent_index",V::makeNum(p.parent));bendRecords.arr.push_back(bend);
            bend.set("direction",V::makeStr(p.angle>0?"positive":"negative"));
            const auto flatA=gp_Pnt(p.allowance/2,t/2,0).Transformed(flatH);
            const auto flatB=gp_Pnt(p.allowance/2,t/2,p.width).Transformed(flatH);
            V bendLine=V::makeArr();bendLine.arr.push_back(pairJson({flatA.X(),flatA.Z()}));bendLine.arr.push_back(pairJson({flatB.X(),flatB.Z()}));
            bend.set("bend_line_flat_mm",std::move(bendLine));bendRecords.arr.back()=std::move(bend);
        }
        pieces.push_back({transformed(p.local,p.flat),transformed(p.formedLocal,p.formed),transformed(p.formedLocal,refoldFrame),(int)index,false});
        expectedFormed+=p.length*p.width*t;expectedFlat+=p.length*p.width*t;
        V row=V::makeObj();row.set("kind",V::makeStr(p.hem?"hem_return":"panel"));row.set("id",V::makeStr(p.id));row.set("length_mm",V::makeNum(p.length));row.set("width_mm",V::makeNum(p.width));row.set("formed_frame",matrix(p.formed));row.set("flat_frame",matrix(p.flat));row.set("refold_pose",matrix(p.refold));panelRecords.arr.push_back(row);
        V animated=row;animated.set("flat_mesh",meshData(transformed(p.local,p.flat)));animationPanels.arr.push_back(animated);
    }
    for(bool b:stampBound)if(!b)throw std::runtime_error("Stamp references a missing panel");
    for(bool b:bound)if(!b)throw std::runtime_error("Opening references a missing panel");
    if(maxFrameError>linearTol)throw std::runtime_error("Independent neutral-line refold does not recover panel frames");
    validatePieces(pieces,panels,false);validatePieces(pieces,panels,true);
    const auto formed=united(pieces,0),flat=united(pieces,1),refold=united(pieces,2);
    if(!BRepCheck_Analyzer(formed).IsValid()||shapes(formed,TopAbs_SOLID).size()!=1||!BRepCheck_Analyzer(flat).IsValid()||shapes(flat,TopAbs_SOLID).size()!=1||!BRepCheck_Analyzer(refold).IsValid())throw std::runtime_error("Formed, flat and refold sheet must be valid connected solids");
    expectedFormed+=stampDelta;expectedDelta+=stampDelta;conservation&=stampRecords.arr.empty();expectedFormed-=removed;expectedFlat-=removed;const double formedVolume=properties(formed),flatVolume=properties(flat),delta=formedVolume-flatVolume;
    const double volumeTol=std::max(1e-5,std::abs(expectedFormed)*1e-8),refoldError=symmetricDifference(formed,refold);
    if(std::abs(formedVolume-expectedFormed)>volumeTol||std::abs(flatVolume-expectedFlat)>volumeTol||std::abs(delta-expectedDelta)>volumeTol||refoldError>volumeTol)throw std::runtime_error("Sheet geometric integral or independent refold validation failed");
    V result=shapeResult(formed,true),report=*result.find("report");result.set("flat_mesh",meshData(flat));
    const std::string flatEncoded=brepData(flat);result.set("flat_brep_base64",V::makeStr(flatEncoded));
    result.set("flat_brep_sha256",V::makeStr(sha256(base64Decode(flatEncoded))));
    result.set("stamps",stampRecords);result.set("holes",records);result.set("panels",panelRecords);result.set("bends",bendRecords);
    report.set("model",V::makeStr("authored_panel_flange_tree"));report.set("width_mm",V::makeNum(width));report.set("thickness_mm",V::makeNum(t));report.set("k_factor",V::makeNum(defaultK));report.set("panel_count",V::makeNum((double)panels.size()));report.set("bend_count",V::makeNum((double)panels.size()-1));
    for(auto kv:{std::pair{"formed_volume_mm3",formedVolume},std::pair{"flat_volume_mm3",flatVolume},std::pair{"analytic_formed_volume_mm3",expectedFormed},std::pair{"analytic_flat_volume_mm3",expectedFlat},std::pair{"volume_delta_mm3",delta},std::pair{"expected_volume_delta_mm3",expectedDelta},std::pair{"volume_delta_error_mm3",std::abs(delta-expectedDelta)},std::pair{"volume_tolerance_mm3",volumeTol},std::pair{"refold_symmetric_difference_mm3",refoldError},std::pair{"refold_frame_error_mm",maxFrameError},std::pair{"opening_removed_volume_mm3",removed}})report.set(kv.first,V::makeNum(kv.second));
    report.set("forming_ops",formingOps);report.set("jog_count",V::makeNum(input.find("jog")?1:0));report.set("sketch_bend_count",V::makeNum(input.find("sketch_bend")?1:0));
    report.set("hem_count",V::makeNum(request.find("hems")?(double)request.find("hems")->arr.size():0));report.set("stamp_count",V::makeNum((double)stampRecords.arr.size()));
    report.set("flat_pattern_qualified",V::makeBool(stampRecords.arr.empty()));report.set("forming_strain_checked",V::makeBool(false));
    report.set("flat_pattern_scope",V::makeStr(stampRecords.arr.empty()?"declared K factor geometric development":"nominal unstamped reference only; blank development requires a forming/strain model"));
    report.set("rigid_panel_refold_verified",V::makeBool(true));report.set("stamp_analytic_volume_delta_mm3",V::makeNum(stampDelta));
    if(!stampRecords.arr.empty()){report.set("refold_symmetric_difference_mm3",V{});report.set("rigid_panel_pose_symmetric_difference_mm3",V::makeNum(refoldError));}
    report.set("opening_count",V::makeNum((double)records.arr.size()));report.set("flat_valid",V::makeBool(true));report.set("flat_solids",V::makeNum(1));report.set("geometric_material_conservation",V::makeBool(conservation));report.set("source_projection_verified",V::makeBool(false));report.set("manufacturing_authoritative",V::makeBool(false));report.set("piece_interference_checked",V::makeBool(true));report.set("bend_trajectory_collision_checked",V::makeBool(false));report.set("k_factor_scope",V::makeStr("declared flat-length design input; no material calibration inferred"));
    V flatBounds=V::makeArr();for(double value:bounds(flat))flatBounds.arr.push_back(V::makeNum(value));report.set("flat_bounds_mm",std::move(flatBounds));
    if(legacy){const auto& p=panels[1];report.set("leg_a_mm",V::makeNum(legacyA));report.set("leg_b_mm",V::makeNum(p.length));report.set("inner_radius_mm",V::makeNum(p.radius));report.set("angle_deg",V::makeNum(p.angle*180/pi));report.set("bend_allowance_mm",V::makeNum(p.allowance));report.set("flat_length_mm",V::makeNum(legacyA+p.allowance+p.length));}
    animation.set("stamping_present",V::makeBool(!stampRecords.arr.empty()));animation.set("panels",animationPanels);animation.set("bends",bendRecords);animation.set("thickness_mm",V::makeNum(t));animation.set("root_pose",matrix(panels[0].refold));result.set("animation",animation);result.set("report",report);return result;
}

V exportSheetFlatDxfNative(const V& request){
    const V& package=required(request,"package"),report=required(package,"report");
    if(string(report,"model")!="authored_panel_flange_tree")
        throw std::runtime_error("Flat DXF is supported only for authored panel graphs");
    if(auto qualified=report.find("flat_pattern_qualified");qualified&&qualified->t==V::Bool&&!qualified->b)throw std::runtime_error("Stamped sheet has no qualified developed blank; flat DXF export requires a forming/strain model");
    const V& valid=required(report,"flat_valid");
    if(valid.t!=V::Bool||!valid.b||number(report,"refold_symmetric_difference_mm3")>number(report,"volume_tolerance_mm3"))
        throw std::runtime_error("Sheet flat/refold validation is not current");
    const std::string encoded=string(package,"flat_brep_base64");
    if(sha256(base64Decode(encoded))!=string(package,"flat_brep_sha256"))
        throw std::runtime_error("Sheet flat B-rep digest mismatch");
    const TopoDS_Shape flat=readBrep(encoded);
    const double thickness=number(report,"thickness_mm");
    if(!(thickness>0&&shapes(flat,TopAbs_SOLID).size()==1))
        throw std::runtime_error("Flat DXF needs one validated sheet solid");
    std::string path=string(request,"path");
    fs::path destination=fs::absolute(fs::u8path(path));
    std::string extension=destination.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return(char)std::tolower(c);});
    if(extension!=".dxf")throw std::runtime_error("Flat outline export requires .dxf");

    BRepAlgoAPI_Section section(flat,gp_Pln(gp_Pnt(0,thickness/2,0),gp_Dir(0,1,0)),false);
    section.Build();
    if(!section.IsDone()||section.HasErrors())throw std::runtime_error("Exact flat midplane section failed");
    const auto edges=shapes(section.Shape(),TopAbs_EDGE);
    if(edges.empty()||edges.size()>4096)throw std::runtime_error("Flat section has no bounded analytic contour");
    std::ostringstream out;out<<std::setprecision(17);
    out<<"0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    auto line=[&](const char* layer,double x0,double y0,double x1,double y1){
        out<<"0\nLINE\n8\n"<<layer<<"\n10\n"<<x0<<"\n20\n"<<y0<<"\n30\n0\n11\n"<<x1<<"\n21\n"<<y1<<"\n31\n0\n";
    };
    const auto angle=[](double x,double y){double d=std::atan2(y,x)*180/pi;return d<0?d+360:d;};
    const auto inside=[](double start,double end,double middle){
        double sweep=std::fmod(end-start+360,360),part=std::fmod(middle-start+360,360);
        return part<=sweep+1e-6;
    };
    double contourBounds[4]{INFINITY,INFINITY,-INFINITY,-INFINITY};
    auto includePoint=[&](double x,double y){
        contourBounds[0]=std::min(contourBounds[0],x);contourBounds[1]=std::min(contourBounds[1],y);
        contourBounds[2]=std::max(contourBounds[2],x);contourBounds[3]=std::max(contourBounds[3],y);
    };
    using Key=std::pair<long long,long long>;std::map<Key,int> endpoints;
    auto key=[](const gp_Pnt& p){return Key{std::llround(p.X()*100000),std::llround(p.Z()*100000)};};
    size_t lines=0,arcs=0,circles=0;
    for(const TopoDS_Shape& shape:edges){
        BRepAdaptor_Curve curve(TopoDS::Edge(shape));
        const double first=curve.FirstParameter(),last=curve.LastParameter();
        if(!std::isfinite(first)||!std::isfinite(last)||last-first<=1e-12)
            throw std::runtime_error("Flat contour has an unbounded or degenerate edge");
        const gp_Pnt a=curve.Value(first),b=curve.Value(last),mid=curve.Value((first+last)/2);
        if(std::abs(a.Y()-thickness/2)>1e-5||std::abs(b.Y()-thickness/2)>1e-5)
            throw std::runtime_error("Flat contour left the mid-thickness plane");
        if(curve.GetType()==GeomAbs_Line){
            if(a.Distance(b)<=1e-7)throw std::runtime_error("Flat outline has a zero-length line");
            includePoint(a.X(),a.Z());includePoint(b.X(),b.Z());
            line("CUT",a.X(),a.Z(),b.X(),b.Z());++lines;
        }else if(curve.GetType()==GeomAbs_Circle){
            const gp_Circ circle=curve.Circle();const gp_Pnt center=circle.Location();const double radius=circle.Radius();
            if(!(radius>0))throw std::runtime_error("Flat contour circle radius is invalid");
            if(a.Distance(b)<=1e-6&&last-first>=2*pi-1e-6){
                includePoint(center.X()-radius,center.Z()-radius);
                includePoint(center.X()+radius,center.Z()+radius);
                out<<"0\nCIRCLE\n8\nCUT\n10\n"<<center.X()<<"\n20\n"<<center.Z()<<"\n30\n0\n40\n"<<radius<<"\n";
                ++circles;continue;
            }
            double start=angle(a.X()-center.X(),a.Z()-center.Z());
            double end=angle(b.X()-center.X(),b.Z()-center.Z());
            const double middle=angle(mid.X()-center.X(),mid.Z()-center.Z());
            if(!inside(start,end,middle))std::swap(start,end);
            includePoint(a.X(),a.Z());includePoint(b.X(),b.Z());
            for(double cardinal:{0.,90.,180.,270.})if(inside(start,end,cardinal))
                includePoint(center.X()+radius*std::cos(cardinal*pi/180),center.Z()+radius*std::sin(cardinal*pi/180));
            out<<"0\nARC\n8\nCUT\n10\n"<<center.X()<<"\n20\n"<<center.Z()<<"\n30\n0\n40\n"<<radius
               <<"\n50\n"<<start<<"\n51\n"<<end<<"\n";
            ++arcs;
        }else throw std::runtime_error("Flat contour contains a curve outside analytic LINE/ARC/CIRCLE DXF scope");
        ++endpoints[key(a)];++endpoints[key(b)];
    }
    for(const auto& [coordinate,degree]:endpoints)
        if(degree!=2)throw std::runtime_error("Flat analytic section has an open or branched contour");
    const auto sourceBounds=bounds(flat);
    if(std::abs(contourBounds[0]-sourceBounds[0])>1e-3||std::abs(contourBounds[1]-sourceBounds[2])>1e-3||
       std::abs(contourBounds[2]-sourceBounds[3])>1e-3||std::abs(contourBounds[3]-sourceBounds[5])>1e-3)
        throw std::runtime_error("Flat analytic contour does not cover exact B-rep extents");
    size_t bendCount=0;
    const V& bends=required(package,"bends");
    if(bends.t!=V::Arr)throw std::runtime_error("Invalid sheet bend table");
    for(const V& bend:bends.arr){
        const V& segment=required(bend,"bend_line_flat_mm");
        if(segment.t!=V::Arr||segment.arr.size()!=2)throw std::runtime_error("Invalid flat bend line");
        const auto& a=segment.arr[0],&b=segment.arr[1];
        if(a.t!=V::Arr||b.t!=V::Arr||a.arr.size()!=2||b.arr.size()!=2)
            throw std::runtime_error("Flat bend line needs two points");
        const char* layer=number(bend,"angle_deg")>=0?"BEND_UP":"BEND_DOWN";
        line(layer,finiteNumber(a.arr[0],"bend x"),finiteNumber(a.arr[1],"bend y"),
                   finiteNumber(b.arr[0],"bend x"),finiteNumber(b.arr[1],"bend y"));++bendCount;
    }
    out<<"0\nENDSEC\n0\nEOF\n";
    const std::string bytes=out.str();
    fs::path staged=destination;staged+=".pending.dxf";
    writeFile(staged,bytes);
    if(readFile(staged)!=bytes)throw std::runtime_error("Flat DXF staged readback changed bytes");
    replaceFile(staged,destination);
    const std::string published=readFile(destination);
    if(published!=bytes)throw std::runtime_error("Flat DXF published readback changed bytes");
    const auto pathBytes=destination.u8string();
    V receipt=V::makeObj();receipt.set("path",V::makeStr(std::string(pathBytes.begin(),pathBytes.end())));
    receipt.set("sha256",V::makeStr(sha256(published)));
    receipt.set("source_flat_brep_sha256",required(package,"flat_brep_sha256"));
    receipt.set("line_count",V::makeNum(lines));receipt.set("arc_count",V::makeNum(arcs));
    receipt.set("circle_count",V::makeNum(circles));receipt.set("bend_line_count",V::makeNum(bendCount));
    receipt.set("analytic_contours",V::makeBool(true));
    V boundsValue=V::makeArr();for(double value:contourBounds)boundsValue.arr.push_back(V::makeNum(value));
    receipt.set("contour_bounds_mm",std::move(boundsValue));
    receipt.set("manufacturing_authoritative",V::makeBool(false));
    receipt.set("scope",V::makeStr("authored rectangular panel graph; flat midplane outline and declared bend lines"));
    return receipt;
}
} // namespace ark::occt
