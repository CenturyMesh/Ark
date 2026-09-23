#include "ark/occt_sheet.hpp"
#include "common.hpp"
#include "ark/model.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
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
#include <algorithm>
#include <array>
#include <cmath>
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
namespace {
struct Panel {
    std::string id,edge;int parent=-1;double length=0,width=0,offset=0,angle=0,radius=0,k=.5,allowance=0;
    gp_Trsf flat,formed,refold,attachment;TopoDS_Shape local;
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
    const auto& base=required(request,"base");Panel root;root.id=named(base,"id");root.length=expr(base,"length",global);root.width=width;root.k=defaultK;
    if(!(root.length>0&&root.length<=1e6))throw std::runtime_error("Base panel length must be positive and at most 1e6 mm");
    bool legacyOrigin=legacy;if(auto p=request.find("legacy_origin")){if(p->t!=V::Bool)throw std::runtime_error("legacy_origin must be boolean");legacyOrigin=p->b;}
    if(legacyOrigin){root.formed=translation(-root.length);root.refold=root.formed;}
    std::vector<Panel> panels{root};std::set<std::string> ids{root.id},edges;
    if(auto list=request.find("flanges")){
        if(list->t!=V::Arr||list->arr.size()>16)throw std::runtime_error("Sheet program accepts at most 16 flanges");
        for(auto& item:list->arr){
            Panel p;p.id=named(item,"id");auto parent=named(item,"parent");p.edge=string(item,"edge");
            if(!ids.insert(p.id).second)throw std::runtime_error("Duplicate sheet panel identity");
            for(size_t i=0;i<panels.size();++i)if(panels[i].id==parent)p.parent=(int)i;
            if(p.parent<0)throw std::runtime_error("Flange parent must be an earlier panel");
            if((p.parent>0&&p.edge=="u_min")||!edges.insert(parent+"/"+p.edge).second)throw std::runtime_error("Panel edge already has a bend attachment");
            const auto& a=panels[p.parent];ExprEnv env=global;env.vars["parent_length"]=a.length;env.vars["parent_width"]=a.width;env.vars["thickness"]=t;
            const double span=p.edge=="u_min"||p.edge=="u_max"?a.width:a.length;env.vars["edge_length"]=span;
            p.length=expr(item,"length",env);p.width=optional(item,"width",span,env);p.offset=optional(item,"offset",0,env);p.radius=expr(item,"inner_radius",env);p.angle=expr(item,"angle",env)*pi/180;p.k=optional(item,"k_factor",defaultK,env);
            if(!(p.length>0&&p.length<=1e6&&p.width>0&&p.offset>=0&&p.offset+p.width<=span&&p.radius>0&&std::abs(p.angle)>=.001*pi/180&&std::abs(p.angle)<170*pi/180&&p.k>=0&&p.k<=1))throw std::runtime_error("Unsupported flange dimensions, attachment span, radius, angle or K factor");
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
    std::vector<Piece> pieces;V records=V::makeArr(),panelRecords=V::makeArr(),bendRecords=V::makeArr(),animation=V::makeObj(),animationPanels=V::makeArr();
    double removed=0,expectedFormed=0,expectedFlat=0,expectedDelta=0,maxFrameError=0;bool conservation=true;
    std::set<std::string> holeIds;
    const V* openings=request.find("openings");if(openings&&(openings->t!=V::Arr||openings->arr.size()>128))throw std::runtime_error("Sheet accepts at most 128 openings");
    std::vector<bool> bound(openings?openings->arr.size():0,false);
    for(size_t index=0;index<panels.size();++index){
        auto& p=panels[index];p.local=BRepPrimAPI_MakeBox(p.length,t,p.width).Shape();
        ExprEnv env=global;env.vars["panel_length"]=p.length;env.vars["panel_width"]=p.width;env.vars["thickness"]=t;
        std::vector<TopoDS_Shape> cutters;
        if(openings)for(size_t h=0;h<openings->arr.size();++h){
            const V& original=openings->arr[h];const V& which=required(original,"panel");
            const bool matches=which.t==V::Str?which.str==p.id:legacy&&which.t==V::Num&&which.num==(double)index;
            if(!matches)continue;bound[h]=true;auto item=numericOpening(original,env);item.set("panel",V::makeNum(0));
            std::string identity=original.find("id")?named(original,"id"):"opening_"+std::to_string(h+1);
            if(!holeIds.insert(identity).second)throw std::runtime_error("Duplicate opening identity");
            V single=V::makeArr();single.arr.push_back(item);V wrapper=V::makeObj();wrapper.set("openings",single);
            const Opening opening=openingsFrom(wrapper,p.length,p.length,p.width).front();
            const auto tool=cutter(opening,false,0,0,t,{},{},{});
            for(auto& prior:cutters)if(boxesMeet(prior,tool)){BRepExtrema_DistShapeShape distance(prior,tool);if(!distance.IsDone()||distance.Value()<=linearTol)throw std::runtime_error("Sheet openings overlap or touch");}
            cutters.push_back(tool);p.local=cut(p.local,tool);removed+=opening.area*t;
            V row=V::makeObj();row.set("id",V::makeStr(identity));row.set("panel",legacy?V::makeNum((double)index):V::makeStr(p.id));row.set("kind",V::makeStr(opening.kind));row.set("local_center",pairJson(opening.center));row.set("area_mm2",V::makeNum(opening.area));
            const gp_Pnt center(opening.center.x,t/2,opening.center.y);row.set("formed_center",vec(center.Transformed(p.formed).XYZ()));row.set("flat_center",vec(center.Transformed(p.flat).XYZ()));
            if(opening.kind=="circle")row.set("radius_mm",V::makeNum(opening.radius));else{V pts=V::makeArr();for(auto pt:opening.points)pts.arr.push_back(pairJson(pt));row.set("points",pts);}records.arr.push_back(row);
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
            V bend=V::makeObj();bend.set("id",V::makeStr(p.id));bend.set("parent",V::makeStr(parent.id));bend.set("edge",V::makeStr(p.edge));bend.set("angle_deg",V::makeNum(p.angle*180/pi));bend.set("inner_radius_mm",V::makeNum(p.radius));bend.set("k_factor",V::makeNum(p.k));bend.set("bend_allowance_mm",V::makeNum(p.allowance));bend.set("width_mm",V::makeNum(p.width));bend.set("offset_mm",V::makeNum(p.offset));bend.set("flat_attachment",matrix(flatH));bend.set("parent_index",V::makeNum(p.parent));bendRecords.arr.push_back(bend);
        }
        pieces.push_back({transformed(p.local,p.flat),transformed(p.local,p.formed),transformed(p.local,refoldFrame),(int)index,false});
        expectedFormed+=p.length*p.width*t;expectedFlat+=p.length*p.width*t;
        V row=V::makeObj();row.set("id",V::makeStr(p.id));row.set("length_mm",V::makeNum(p.length));row.set("width_mm",V::makeNum(p.width));row.set("formed_frame",matrix(p.formed));row.set("flat_frame",matrix(p.flat));row.set("refold_pose",matrix(p.refold));panelRecords.arr.push_back(row);
        V animated=row;animated.set("flat_mesh",meshData(transformed(p.local,p.flat)));animationPanels.arr.push_back(animated);
    }
    for(bool b:bound)if(!b)throw std::runtime_error("Opening references a missing panel");
    if(maxFrameError>linearTol)throw std::runtime_error("Independent neutral-line refold does not recover panel frames");
    validatePieces(pieces,panels,false);validatePieces(pieces,panels,true);
    const auto formed=united(pieces,0),flat=united(pieces,1),refold=united(pieces,2);
    if(!BRepCheck_Analyzer(formed).IsValid()||shapes(formed,TopAbs_SOLID).size()!=1||!BRepCheck_Analyzer(flat).IsValid()||shapes(flat,TopAbs_SOLID).size()!=1||!BRepCheck_Analyzer(refold).IsValid())throw std::runtime_error("Formed, flat and refold sheet must be valid connected solids");
    expectedFormed-=removed;expectedFlat-=removed;const double formedVolume=properties(formed),flatVolume=properties(flat),delta=formedVolume-flatVolume;
    const double volumeTol=std::max(1e-5,std::abs(expectedFormed)*1e-8),refoldError=symmetricDifference(formed,refold);
    if(std::abs(formedVolume-expectedFormed)>volumeTol||std::abs(flatVolume-expectedFlat)>volumeTol||std::abs(delta-expectedDelta)>volumeTol||refoldError>volumeTol)throw std::runtime_error("Sheet geometric integral or independent refold validation failed");
    V result=shapeResult(formed,true),report=*result.find("report");result.set("flat_mesh",meshData(flat));result.set("flat_brep_base64",V::makeStr(brepData(flat)));result.set("holes",records);result.set("panels",panelRecords);result.set("bends",bendRecords);
    report.set("model",V::makeStr("authored_panel_flange_tree"));report.set("width_mm",V::makeNum(width));report.set("thickness_mm",V::makeNum(t));report.set("k_factor",V::makeNum(defaultK));report.set("panel_count",V::makeNum((double)panels.size()));report.set("bend_count",V::makeNum((double)panels.size()-1));
    for(auto kv:{std::pair{"formed_volume_mm3",formedVolume},std::pair{"flat_volume_mm3",flatVolume},std::pair{"analytic_formed_volume_mm3",expectedFormed},std::pair{"analytic_flat_volume_mm3",expectedFlat},std::pair{"volume_delta_mm3",delta},std::pair{"expected_volume_delta_mm3",expectedDelta},std::pair{"volume_delta_error_mm3",std::abs(delta-expectedDelta)},std::pair{"volume_tolerance_mm3",volumeTol},std::pair{"refold_symmetric_difference_mm3",refoldError},std::pair{"refold_frame_error_mm",maxFrameError},std::pair{"opening_removed_volume_mm3",removed}})report.set(kv.first,V::makeNum(kv.second));
    report.set("opening_count",V::makeNum((double)records.arr.size()));report.set("flat_valid",V::makeBool(true));report.set("flat_solids",V::makeNum(1));report.set("geometric_material_conservation",V::makeBool(conservation));report.set("source_projection_verified",V::makeBool(false));report.set("manufacturing_authoritative",V::makeBool(false));report.set("piece_interference_checked",V::makeBool(true));report.set("bend_trajectory_collision_checked",V::makeBool(false));report.set("k_factor_scope",V::makeStr("declared flat-length design input; no material calibration inferred"));
    if(legacy){const auto& p=panels[1];report.set("leg_a_mm",V::makeNum(legacyA));report.set("leg_b_mm",V::makeNum(p.length));report.set("inner_radius_mm",V::makeNum(p.radius));report.set("angle_deg",V::makeNum(p.angle*180/pi));report.set("bend_allowance_mm",V::makeNum(p.allowance));report.set("flat_length_mm",V::makeNum(legacyA+p.allowance+p.length));}
    animation.set("panels",animationPanels);animation.set("bends",bendRecords);animation.set("thickness_mm",V::makeNum(t));animation.set("root_pose",matrix(panels[0].refold));result.set("animation",animation);result.set("report",report);return result;
}
} // namespace ark::occt
