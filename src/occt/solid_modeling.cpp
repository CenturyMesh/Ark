#include "ark/occt_solid.hpp"
#include "common.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepGProp.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_Mode.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAbs_JoinType.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_Surface.hxx>
#include <GProp_GProps.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_Array1.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>
#include <NCollection_List.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ark::occt {
namespace {
using V = arkjson::Value;
struct Ref { std::string name,kind,status; TopoDS_Shape shape; V seed; V meta; };
bool selectable(const std::string& status){return status=="bound"||status=="merged";}
const V& need(const V& v,const char* key){auto p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing solid field: ")+key);return *p;}
std::string word(const V& v,const char* key){auto& p=need(v,key);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Invalid solid text: ")+key);return p.str;}
double scalar(const V& v,const char* key){auto& p=need(v,key);if(p.t!=V::Num||!std::isfinite(p.num)||std::abs(p.num)>1e8)throw std::runtime_error(std::string("Invalid solid number: ")+key);return p.num;}
double positive(const V& v,const char* key){double x=scalar(v,key);if(x<=0)throw std::runtime_error(std::string("Expected positive solid dimension: ")+key);return x;}
double optional(const V& v,const char* key,double fallback){return v.find(key)?scalar(v,key):fallback;}
std::array<double,16> frame(const V& v){
    std::array<double,16> m{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const V* f=v.find("frame");if(!f)return m;
    if(f->t!=V::Arr||f->arr.size()!=16)throw std::runtime_error("Rigid frame needs 16 column-major numbers");
    for(size_t i=0;i<16;++i){if(f->arr[i].t!=V::Num||!std::isfinite(f->arr[i].num))throw std::runtime_error("Non-finite rigid frame");m[i]=f->arr[i].num;}
    if(std::abs(m[3])>1e-10||std::abs(m[7])>1e-10||std::abs(m[11])>1e-10||std::abs(m[15]-1)>1e-10)throw std::runtime_error("Frame is not affine");
    for(int a=0;a<3;++a)for(int b=a;b<3;++b){double dot=0;for(int k=0;k<3;++k)dot+=m[a*4+k]*m[b*4+k];if(std::abs(dot-(a==b?1.:0.))>1e-10)throw std::runtime_error("Frame is not orthonormal");}
    double det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[4]*(m[1]*m[10]-m[2]*m[9])+m[8]*(m[1]*m[6]-m[2]*m[5]);
    if(std::abs(det-1)>1e-10)throw std::runtime_error("Frame must be right-handed");return m;
}
gp_Trsf transform(const std::array<double,16>& m){gp_Trsf t;t.SetValues(m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]);return t;}
gp_Pnt point(const V& v,const char* label){if(v.t!=V::Arr||v.arr.size()!=3)throw std::runtime_error(std::string(label)+" must be xyz");for(auto& x:v.arr)if(x.t!=V::Num||!std::isfinite(x.num))throw std::runtime_error("Non-finite point");return {v.arr[0].num,v.arr[1].num,v.arr[2].num};}
V xyz(const gp_Pnt& p){return vec(p.XYZ());}
V direction(const gp_Dir& d){return vec(d.XYZ());}
int indexOf(const TopoDS_Shape& parent,const TopoDS_Shape& part,TopAbs_ShapeEnum kind){int i=0;for(auto& s:shapes(parent,kind)){if(s.IsSame(part))return i;++i;}return -1;}
TopAbs_ShapeEnum refType(const std::string& kind){if(kind=="face"||kind=="axis")return TopAbs_FACE;if(kind=="edge")return TopAbs_EDGE;throw std::runtime_error("Invalid reference kind");}
TopoDS_Shape atIndex(const TopoDS_Shape& shape,TopAbs_ShapeEnum kind,int i){auto list=shapes(shape,kind);if(i<0||static_cast<size_t>(i)>=list.size())throw std::runtime_error("Reference index outside bound B-rep");return list[static_cast<size_t>(i)];}
V descriptor(const Ref& ref,double fraction=.5){
    V d=V::makeObj();if(ref.shape.IsNull())return d;
    if(ref.kind=="edge"){
        BRepAdaptor_Curve curve(TopoDS::Edge(ref.shape));double a=curve.FirstParameter(),b=curve.LastParameter();
        if(!std::isfinite(a)||!std::isfinite(b))throw std::runtime_error("Unbounded reference edge");
        gp_Pnt p;gp_Vec tangent;curve.D1(a+(b-a)*fraction,p,tangent);if(tangent.Magnitude()<=1e-12)throw std::runtime_error("Degenerate reference tangent");
        if(const V* prior=ref.seed.find("tangent")){auto old=point(*prior,"prior tangent");if(tangent.Dot(gp_Vec(old.X(),old.Y(),old.Z()))<0)tangent.Reverse();}
        else if(ref.name.rfind("edge.",0)==0&&ref.name.size()>5){char axis=ref.name[5];gp_Vec desired(axis=='x'?1:0,axis=='y'?1:0,axis=='z'?1:0);if(desired.Magnitude()>0&&tangent.Dot(desired)<0)tangent.Reverse();}
        d.set("origin",xyz(p));d.set("tangent",direction(gp_Dir(tangent)));
        if(curve.GetType()==GeomAbs_Circle)d.set("radius",V::makeNum(curve.Circle().Radius()));return d;
    }
    BRepAdaptor_Surface surface(TopoDS::Face(ref.shape));
    if(surface.GetType()==GeomAbs_Plane){
        if(ref.kind=="axis")if(const V* prior=ref.seed.find("axis")){d.set("axis",*prior);if(const V* origin=ref.seed.find("origin"))d.set("origin",*origin);else d.set("origin",xyz(surface.Plane().Location()));return d;}
        auto pl=surface.Plane();gp_Dir n=pl.Axis().Direction();if(ref.shape.Orientation()==TopAbs_REVERSED)n.Reverse();d.set("origin",xyz(pl.Location()));d.set("normal",direction(n));d.set("x_axis",direction(pl.XAxis().Direction()));return d;}
    if(surface.GetType()==GeomAbs_Cylinder){auto cy=surface.Cylinder();gp_Dir axis=cy.Axis().Direction();if(const V* prior=ref.seed.find("axis")){auto old=point(*prior,"prior axis");if(gp_Vec(axis).Dot(gp_Vec(old.X(),old.Y(),old.Z()))<0)axis.Reverse();}d.set("origin",xyz(cy.Location()));d.set("axis",direction(axis));d.set("radius",V::makeNum(cy.Radius()));return d;}
    if(surface.GetType()==GeomAbs_Cone){auto co=surface.Cone();d.set("origin",xyz(co.Location()));d.set("axis",direction(co.Axis().Direction()));return d;}
    if(surface.GetType()==GeomAbs_Sphere){auto sp=surface.Sphere();d.set("origin",xyz(sp.Location()));d.set("radius",V::makeNum(sp.Radius()));return d;}
    if(surface.GetType()==GeomAbs_Torus){auto tor=surface.Torus();d.set("origin",xyz(tor.Location()));d.set("axis",direction(tor.Axis().Direction()));d.set("radius",V::makeNum(tor.MajorRadius()));return d;}
    // Keep exact topological identity even when a non-analytic surface cannot
    // supply a unique rigid datum frame (e.g. a swept Bezier side face).
    d.set("surface_type",V::makeStr("non_analytic"));return d;
}
std::string digest(const std::string& encoded){return sha256(base64Decode(encoded));}
std::vector<Ref> inputRefs(const V& request,const char* brepKey,const char* topologyKey,const TopoDS_Shape& shape){
    const V* topology=request.find(topologyKey);if(!topology)return {};
    const auto& encoded=need(request,brepKey);if(encoded.t!=V::Str)throw std::runtime_error("Input B-rep is not text");
    if(word(*topology,"brep_sha256")!=digest(encoded.str))throw std::runtime_error("Topology registry B-rep digest mismatch");
    auto& rows=need(*topology,"references");if(rows.t!=V::Arr||rows.arr.size()>512)throw std::runtime_error("Invalid topology registry");
    std::set<std::string> names;std::vector<Ref> out;
    for(const auto& row:rows.arr){Ref ref;ref.name=word(row,"name");ref.kind=word(row,"kind");ref.status=word(row,"status");if(!names.insert(ref.name).second)throw std::runtime_error("Duplicate topology reference");
        if(selectable(ref.status)){auto& indices=need(row,"indices");if(indices.t!=V::Arr||indices.arr.size()!=1||indices.arr[0].t!=V::Num||std::floor(indices.arr[0].num)!=indices.arr[0].num||indices.arr[0].num<0||indices.arr[0].num>1000000)throw std::runtime_error("Ambiguous topology binding");ref.shape=atIndex(shape,refType(ref.kind),static_cast<int>(indices.arr[0].num));if(const V* seed=row.find("descriptor"))ref.seed=*seed;V meta=V::makeObj();if(const V* from=row.find("merged_from"))meta.set("merged_from",*from);if(const V* surviving=row.find("surviving"))meta.set("surviving",*surviving);if(!meta.obj.empty())ref.meta=meta;}
        out.push_back(ref);
    }return out;
}
V topologyOf(const TopoDS_Shape& shape,const std::string& encoded,const std::vector<Ref>& refs){
    V out=V::makeObj(),rows=V::makeArr();out.set("brep_sha256",V::makeStr(digest(encoded)));
    std::set<std::string> names;
    for(const auto& ref:refs){if(!names.insert(ref.name).second)throw std::runtime_error("Duplicate output reference name");V row=V::makeObj();row.set("name",V::makeStr(ref.name));row.set("kind",V::makeStr(ref.kind));V indices=V::makeArr();std::string status=ref.status;
        if(selectable(status)){int i=indexOf(shape,ref.shape,refType(ref.kind));if(i<0)status="deleted";else indices.arr.push_back(V::makeNum(i));}
        row.set("status",V::makeStr(status));row.set("indices",indices);if(selectable(status)){V data=descriptor(ref);row.set("frame_available",V::makeBool(data.find("normal")||data.find("axis")||data.find("tangent")));row.set("descriptor",data);if(const V* from=ref.meta.find("merged_from"))row.set("merged_from",*from);if(const V* surviving=ref.meta.find("surviving"))row.set("surviving",*surviving);}rows.arr.push_back(row);
    }out.set("references",rows);return out;
}
template<class Operation>std::vector<Ref> propagated(const std::vector<Ref>& refs,const TopoDS_Shape& result,Operation& op){
    std::vector<Ref> out;out.reserve(refs.size());
    for(auto ref:refs){if(!selectable(ref.status)){out.push_back(ref);continue;}std::vector<TopoDS_Shape> matches;
        auto collect=[&](const TopTools_ListOfShape& list){for(TopTools_ListIteratorOfListOfShape it(list);it.More();it.Next()){auto s=it.Value();if(indexOf(result,s,refType(ref.kind))>=0){bool found=false;for(const auto& old:matches)if(old.IsSame(s))found=true;if(!found)matches.push_back(s);}}};
        collect(op.Modified(ref.shape));collect(op.Generated(ref.shape));
        if(matches.empty()&&indexOf(result,ref.shape,refType(ref.kind))>=0)matches.push_back(ref.shape);
        if(matches.size()==1)ref.shape=matches.front();else{ref.shape.Nullify();ref.status=matches.empty()?"deleted":"split";}
        out.push_back(ref);
    }return out;
}
void add(std::vector<Ref>& refs,const std::string& name,const std::string& kind,const TopoDS_Shape& s){if(s.IsNull())return;for(const auto& existing:refs)if(existing.name==name){if(existing.kind==kind&&existing.shape.IsSame(s))return;throw std::runtime_error("Ambiguous producer reference: "+name);}refs.push_back({name,kind,"bound",s});}
V finish(const TopoDS_Shape& shape,const std::vector<Ref>& refs){V result=shapeResult(shape,true);result.set("topology",topologyOf(shape,need(result,"brep_base64").str,refs));return result;}
void boxEdges(const TopoDS_Shape& solid,double x,double y,double z,std::vector<Ref>& refs){
    for(auto& item:shapes(solid,TopAbs_EDGE)){TopoDS_Vertex a,b;TopExp::Vertices(TopoDS::Edge(item),a,b);if(a.IsNull()||b.IsNull())continue;
        gp_Pnt p=BRep_Tool::Pnt(a),q=BRep_Tool::Pnt(b);double delta[3]{std::abs(p.X()-q.X()),std::abs(p.Y()-q.Y()),std::abs(p.Z()-q.Z())};
        int axis=delta[0]>1e-9?0:delta[1]>1e-9?1:delta[2]>1e-9?2:-1;if(axis<0)continue;
        double mid[3]{(p.X()+q.X())/2,(p.Y()+q.Y())/2,(p.Z()+q.Z())/2};double size[3]{x,y,z};std::string label="edge.";label+="xyz"[axis];
        for(int i=0;i<3;++i)if(i!=axis){if(std::abs(mid[i])<1e-8)label+=std::string(".")+"xyz"[i]+"min";else if(std::abs(mid[i]-size[i])<1e-8)label+=std::string(".")+"xyz"[i]+"max";else throw std::runtime_error("Box edge classification failed");}
        add(refs,label,"edge",item);
    }
}
void axialFaces(const TopoDS_Shape& solid,std::vector<Ref>& refs,bool cylinder){
    for(auto& item:shapes(solid,TopAbs_FACE)){BRepAdaptor_Surface s(TopoDS::Face(item));if(s.GetType()==GeomAbs_Plane){
            const auto p=s.Plane().Location();add(refs,std::abs(p.Z())<1e-8?"cap.start":"cap.end","face",item);
        }else if(s.GetType()==GeomAbs_Cylinder||s.GetType()==GeomAbs_Cone){add(refs,"side","face",item);if(cylinder)add(refs,"axis","axis",item);}
    }
}
std::pair<TopoDS_Face,std::vector<std::pair<std::string,TopoDS_Edge>>> profileFace(const V& profile){
    const char* stage="profile setup";
    try {
    auto m=frame(profile);auto& entities=need(profile,"entities");if(entities.t!=V::Arr||entities.arr.empty()||entities.arr.size()>256)throw std::runtime_error("Profile needs 1..256 entities");
    BRepBuilderAPI_MakeWire wire;std::vector<std::pair<std::string,TopoDS_Edge>> edges;size_t index=0;
    for(auto& e:entities.arr){std::string kind=word(e,"kind");TopoDS_Edge edge;
        if(kind=="circle"){
            auto& c=need(e,"center");if(c.t!=V::Arr||c.arr.size()!=2||c.arr[0].t!=V::Num||c.arr[1].t!=V::Num)throw std::runtime_error("Circle center needs xy");
            double r=positive(e,"radius");edge=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(c.arr[0].num,c.arr[1].num,0),gp_Dir(0,0,1)),r)).Edge();
        }else if(kind=="line"||kind=="bezier"){
            auto& pts=need(e,"points");if(pts.t!=V::Arr||pts.arr.size()!=(kind=="line"?2u:4u))throw std::runtime_error("Line needs two endpoints; Bezier needs four controls");
            auto xy=[](const V& v){if(v.t!=V::Arr||v.arr.size()!=2||v.arr[0].t!=V::Num||v.arr[1].t!=V::Num||!std::isfinite(v.arr[0].num)||!std::isfinite(v.arr[1].num))throw std::runtime_error("Profile point needs finite xy");return gp_Pnt(v.arr[0].num,v.arr[1].num,0);};
            if(kind=="line")edge=BRepBuilderAPI_MakeEdge(xy(pts.arr[0]),xy(pts.arr[1])).Edge();
            else{NCollection_Array1<gp_Pnt> poles(1,4);for(int i=0;i<4;++i)poles.SetValue(i+1,xy(pts.arr[static_cast<size_t>(i)]));edge=BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(poles)).Edge();}
        }else if(kind=="arc"){
            auto& c=need(e,"center");if(c.t!=V::Arr||c.arr.size()!=2||c.arr[0].t!=V::Num||c.arr[1].t!=V::Num)throw std::runtime_error("Arc center needs xy");
            const double radius=positive(e,"radius"),degrees=std::acos(-1.0)/180.0,u0=scalar(e,"a0")*degrees,u1=scalar(e,"a1")*degrees;
            gp_Circ circ(gp_Ax2(gp_Pnt(c.arr[0].num,c.arr[1].num,0),gp_Dir(0,0,1)),radius);
            if(u1+1e-12>=u0){BRepBuilderAPI_MakeEdge maker(circ,u0,u1);if(!maker.IsDone())throw std::runtime_error("Profile arc failed");edge=maker.Edge();}
            else{BRepBuilderAPI_MakeEdge maker(circ,u1,u0);if(!maker.IsDone())throw std::runtime_error("Profile arc failed");edge=maker.Edge();edge.Reverse();}
        }else throw std::runtime_error("Solid profile supports line, Bezier, circle, and arc entities");
        stage="profile wire edge";wire.Add(edge);if(wire.Edge().IsNull())throw std::runtime_error("Profile edge did not join authored wire");std::string name=e.find("name")?word(e,"name"):"side."+std::to_string(index);edges.push_back({name,wire.Edge()});++index;
    }
    if(!wire.IsDone())throw std::runtime_error("Profile wire does not close");stage="profile planar face";BRepBuilderAPI_MakeFace maker(wire.Wire(),true);if(!maker.IsDone())throw std::runtime_error("Profile is not a planar face");
    stage="profile frame transform";BRepBuilderAPI_Transform move(maker.Face(),transform(m),true);if(!move.IsDone())throw std::runtime_error("Profile frame transform failed");
    stage="profile transformed edge";
    auto faceEdges=shapes(maker.Face(),TopAbs_EDGE);
    for(auto& item:edges){
        TopoDS_Shape source;
        for(auto& candidate:faceEdges)if(candidate.IsSame(item.second)){source=candidate;break;}
        if(source.IsNull()){
            TopLoc_Location originLocation;double originFirst=0,originLast=0;
            auto originCurve=BRep_Tool::Curve(item.second,originLocation,originFirst,originLast);
            if(originCurve.IsNull())throw std::runtime_error("Authored edge has no exact curve handle");
            for(auto& candidate:faceEdges){TopLoc_Location candidateLocation;double candidateFirst=0,candidateLast=0;
                auto candidateCurve=BRep_Tool::Curve(TopoDS::Edge(candidate),candidateLocation,candidateFirst,candidateLast);
                if(originCurve==candidateCurve&&originLocation.IsEqual(candidateLocation)&&originFirst==candidateFirst&&originLast==candidateLast){
                    if(!source.IsNull())throw std::runtime_error("Ambiguous exact curve origin in authored profile");source=candidate;
                }
            }
        }
        if(source.IsNull())throw std::runtime_error("Source edge absent from constructed profile");
        TopoDS_Shape changed=move.ModifiedShape(source);
        if(changed.IsNull()&&indexOf(move.Shape(),source,TopAbs_EDGE)>=0)changed=source;
        if(changed.IsNull())throw std::runtime_error("Profile edge was not retained by transform");item.second=TopoDS::Edge(changed);
    }return {TopoDS::Face(move.Shape()),edges};
    } catch(const Standard_Failure& failure) {throw std::runtime_error(std::string("OCCT failed during ")+stage+": "+(failure.GetMessageString()?failure.GetMessageString():""));}
}
TopoDS_Shape makeExtrusion(const V& request,std::vector<Ref>& refs){
    const auto& profile=need(request,"profile");auto [face,edges]=profileFace(profile);auto m=frame(profile);double depth=positive(request,"depth");
    BRepPrimAPI_MakePrism op(face,gp_Vec(m[8]*depth,m[9]*depth,m[10]*depth));op.Build();if(!op.IsDone())throw std::runtime_error("Native prism failed");
    add(refs,"cap.start","face",op.FirstShape());add(refs,"cap.end","face",op.LastShape());
    for(auto& [name,edge]:edges){auto list=op.Generated(edge);int count=0;TopoDS_Shape generated;for(TopTools_ListIteratorOfListOfShape it(list);it.More();it.Next()){generated=it.Value();++count;}
        if(count==1)add(refs,"side."+name,"face",generated);else refs.push_back({"side."+name,"face",count?"split":"deleted",{}});
        add(refs,"edge.start."+name,"edge",op.FirstShape(edge));
        add(refs,"edge.end."+name,"edge",op.LastShape(edge));
    }return op.Shape();
}
TopoDS_Shape cutter(const V& spec){
    std::string kind=word(spec,"kind");auto m=frame(spec);gp_Trsf t=transform(m);double depth=positive(spec,"depth");
    if(kind=="circle")return transformed(BRepPrimAPI_MakeCylinder(positive(spec,"radius"),depth).Shape(),t);
    if(kind=="slot"){
        double length=positive(spec,"length"),width=positive(spec,"width");if(length<width)throw std::runtime_error("Slot length must reach its width");double r=width/2,between=length-width;
        TopoDS_Shape a=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-between/2,0,0),gp_Dir(0,0,1)),r,depth).Shape();
        TopoDS_Shape b=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(between/2,0,0),gp_Dir(0,0,1)),r,depth).Shape();
        if(between==0)return transformed(a,t);
        TopoDS_Shape middle=BRepPrimAPI_MakeBox(gp_Pnt(-between/2,-r,0),between,width,depth).Shape();
        BRepAlgoAPI_Fuse first(a,middle);first.Build();if(!first.IsDone())throw std::runtime_error("Slot cutter union failed");BRepAlgoAPI_Fuse second(first.Shape(),b);second.Build();if(!second.IsDone())throw std::runtime_error("Slot cutter union failed");return transformed(second.Shape(),t);
    }
    if(kind=="profile"){V request=V::makeObj(),profile=need(spec,"profile");if(spec.find("frame")){V f=V::makeArr();for(double value:m)f.arr.push_back(V::makeNum(value));profile.set("frame",f);}request.set("profile",profile);request.set("depth",V::makeNum(depth));std::vector<Ref> unused;return makeExtrusion(request,unused);}
    throw std::runtime_error("Unknown hole cutter kind");
}
}

TopoDS_Shape roundTube(const V& request,std::vector<Ref>& refs){
    const double od=positive(request,"od"),wall=positive(request,"wall");
    if(od<=2*wall)throw std::runtime_error("Round tube wall leaves no opening");
    const double outerR=od*0.5,innerR=outerR-wall,pi=std::acos(-1.0);
    const V& segments=need(request,"segments");
    if(segments.t!=V::Arr||segments.arr.empty()||segments.arr.size()>128)throw std::runtime_error("Round tube needs 1..128 centerline segments");
    gp_Pnt point(0,0,0);gp_Dir tangent(1,0,0),normal(0,1,0),binormal(0,0,1),bendNormal=normal;
    const gp_Pnt startPoint=point;const gp_Dir startTangent=tangent,startNormal=normal;
    BRepBuilderAPI_MakeWire centerline;
    auto addLine=[&](double length){if(length<=1e-9)return;gp_Pnt next=point.Translated(gp_Vec(tangent)*length);BRepBuilderAPI_MakeEdge edge(point,next);if(!edge.IsDone())throw std::runtime_error("Round tube straight span failed");centerline.Add(edge.Edge());if(!centerline.IsDone())throw std::runtime_error("Round tube centerline is not connected");point=next;};
    for(const auto& segment:segments.arr){
        const std::string kind=word(segment,"kind");
        if(kind=="line")addLine(positive(segment,"length"));
        else if(kind=="arc"){
            const double radius=positive(segment,"radius"),signedAngle=scalar(segment,"angle"),roll=optional(segment,"roll",0);
            if(radius<=outerR||std::abs(signedAngle)<.001||std::abs(signedAngle)>180)throw std::runtime_error("Round tube bend needs a CLR beyond the tube and an angle in [-180,180] excluding zero");
            gp_Trsf rolled;rolled.SetRotation(gp_Ax1(point,tangent),roll*pi/180.0);bendNormal=gp_Dir(gp_Vec(bendNormal).Transformed(rolled));
            const gp_Dir inward=signedAngle<0?bendNormal.Reversed():bendNormal;
            const gp_Dir axis=tangent.Crossed(inward);const gp_Pnt center=point.Translated(gp_Vec(inward)*radius);
            const gp_Dir radial(gp_Vec(point,center).Reversed());
            const double angle=std::abs(signedAngle)*pi/180.0;
            BRepBuilderAPI_MakeEdge edge(gp_Circ(gp_Ax2(center,axis,radial),radius),0,angle);
            if(!edge.IsDone())throw std::runtime_error("Round tube bend failed");
            centerline.Add(edge.Edge());if(!centerline.IsDone())throw std::runtime_error("Round tube centerline is not connected");
            gp_Trsf swing;swing.SetRotation(gp_Ax1(center,axis),angle);
            point.Transform(swing);tangent=gp_Dir(gp_Vec(tangent).Transformed(swing));normal=gp_Dir(gp_Vec(normal).Transformed(swing));binormal=tangent.Crossed(normal);bendNormal=gp_Dir(gp_Vec(bendNormal).Transformed(swing));
        }else throw std::runtime_error("Round tube segment must be line or arc");
    }
    if(!centerline.IsDone())throw std::runtime_error("Round tube centerline is empty");
    auto pipeSolid=[&](double radius){
        BRepBuilderAPI_MakeEdge circle(gp_Circ(gp_Ax2(startPoint,startTangent,startNormal),radius));
        if(!circle.IsDone())throw std::runtime_error("Round tube section failed");
        BRepBuilderAPI_MakeWire profile(circle.Edge());
        BRepOffsetAPI_MakePipeShell pipe(centerline.Wire());
        pipe.SetMode(false);pipe.SetTolerance(1e-6,1e-6,1e-4);pipe.Add(profile.Wire(),false,true);pipe.Build();
        if(!pipe.IsDone()||!pipe.MakeSolid())throw std::runtime_error("Round tube sweep failed");
        TopoDS_Shape solid=pipe.Shape();if(properties(solid)<0)solid.Reverse();return solid;
    };
    BRepAlgoAPI_Cut hollow(pipeSolid(outerR),pipeSolid(innerR));hollow.Build();
    if(!hollow.IsDone()||hollow.HasErrors())throw std::runtime_error("Round tube hollow cut failed");
    TopoDS_Shape solid=hollow.Shape();if(properties(solid)<0)solid.Reverse();
    if(shapes(solid,TopAbs_SOLID).size()!=1||!(properties(solid)>0))throw std::runtime_error("Round tube must be one positive solid");
    const gp_Pnt endPoint=point;
    struct FaceRecord{TopoDS_Face face;double radius;gp_Pnt location;gp_Dir axis;bool cylinder;bool outer;};
    std::vector<FaceRecord> curved;std::vector<TopoDS_Face> caps;
    for(const TopoDS_Shape& item:shapes(solid,TopAbs_FACE)){
        TopoDS_Face face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);
        if(surface.GetType()==GeomAbs_Plane)caps.push_back(face);
        else if(surface.GetType()==GeomAbs_Cylinder||surface.GetType()==GeomAbs_Torus){
            const double radius=surface.GetType()==GeomAbs_Cylinder?surface.Cylinder().Radius():surface.Torus().MinorRadius();
            const gp_Ax1 axis=surface.GetType()==GeomAbs_Cylinder?surface.Cylinder().Axis():surface.Torus().Axis();
            const bool outer=std::abs(radius-outerR)<=std::abs(radius-innerR);
            curved.push_back({face,radius,axis.Location(),axis.Direction(),surface.GetType()==GeomAbs_Cylinder,outer});
        }
    }
    auto nearer=[&](const TopoDS_Face& face){gp_XYZ sum(0,0,0);int count=0;for(const TopoDS_Shape& vertex:shapes(face,TopAbs_VERTEX)){sum+=BRep_Tool::Pnt(TopoDS::Vertex(vertex)).XYZ();++count;}gp_Pnt mid(count?sum/count:gp_Pnt());return mid.Distance(startPoint)<=mid.Distance(endPoint);};
    int startCaps=0,endCaps=0;
    for(const auto& face:caps){const bool start=nearer(face);int& slot=start?startCaps:endCaps;add(refs,std::string(start?"cap.start":"cap.end")+(slot?"."+std::to_string(slot):""),"face",face);++slot;}
    std::sort(curved.begin(),curved.end(),[&](const FaceRecord& a,const FaceRecord& b){return a.location.Distance(startPoint)<b.location.Distance(startPoint);});
    int outerIndex=0,innerIndex=0,bendOuter=0,bendInner=0;bool axisBound=false;
    for(const auto& face:curved){
        std::string name;
        if(face.cylinder){
            int& slot=face.outer?outerIndex:innerIndex;name=std::string(face.outer?"side.outer":"side.inner")+(slot? "."+std::to_string(slot):"");++slot;
            add(refs,name,"face",face.face);
            if(face.outer&&!axisBound){Ref axis{"axis","axis","bound",face.face,{}};axis.seed.set("axis",direction(startTangent));refs.push_back(axis);axisBound=true;}
        }else{
            int& slot=face.outer?bendOuter:bendInner;name=std::string(face.outer?"side.bend":"side.bend.inner")+(slot? "."+std::to_string(slot):"");++slot;
            add(refs,name,"face",face.face);
        }
    }
    return solid;
}

TopoDS_Shape rectTube(const V& request,std::vector<Ref>& refs){
    const double width=positive(request,"width"),height=positive(request,"height"),wall=positive(request,"wall");
    if(!(width>2*wall&&height>2*wall))throw std::runtime_error("Rectangular tube wall leaves no opening");
    const double envelope=std::hypot(width,height)*0.5,pi=std::acos(-1.0);
    const V& segments=need(request,"segments");
    if(segments.t!=V::Arr||segments.arr.empty()||segments.arr.size()>128)throw std::runtime_error("Rectangular tube needs 1..128 centerline segments");
    gp_Pnt point(0,0,0);gp_Dir tangent(1,0,0),normal(0,1,0),binormal(0,0,1),bendNormal=normal;
    const gp_Pnt startPoint=point;const gp_Dir startTangent=tangent,startNormal=normal;
    BRepBuilderAPI_MakeWire centerline;
    auto addLine=[&](double length){if(length<=1e-9)return;gp_Pnt next=point.Translated(gp_Vec(tangent)*length);BRepBuilderAPI_MakeEdge edge(point,next);if(!edge.IsDone())throw std::runtime_error("Rectangular tube straight span failed");centerline.Add(edge.Edge());if(!centerline.IsDone())throw std::runtime_error("Rectangular tube centerline is not connected");point=next;};
    for(const auto& segment:segments.arr){
        const std::string kind=word(segment,"kind");
        if(kind=="line")addLine(positive(segment,"length"));
        else if(kind=="arc"){
            const double radius=positive(segment,"radius"),signedAngle=scalar(segment,"angle"),roll=optional(segment,"roll",0);
            if(radius<=envelope||std::abs(signedAngle)<.001||std::abs(signedAngle)>180)throw std::runtime_error("Rectangular tube bend needs a CLR beyond the tube and an angle in [-180,180] excluding zero");
            gp_Trsf rolled;rolled.SetRotation(gp_Ax1(point,tangent),roll*pi/180.0);bendNormal=gp_Dir(gp_Vec(bendNormal).Transformed(rolled));
            const gp_Dir inward=signedAngle<0?bendNormal.Reversed():bendNormal;
            const gp_Dir axis=tangent.Crossed(inward);const gp_Pnt center=point.Translated(gp_Vec(inward)*radius);
            const gp_Dir radial(gp_Vec(point,center).Reversed());
            const double angle=std::abs(signedAngle)*pi/180.0;
            BRepBuilderAPI_MakeEdge edge(gp_Circ(gp_Ax2(center,axis,radial),radius),0,angle);
            if(!edge.IsDone())throw std::runtime_error("Rectangular tube bend failed");
            centerline.Add(edge.Edge());if(!centerline.IsDone())throw std::runtime_error("Rectangular tube centerline is not connected");
            gp_Trsf swing;swing.SetRotation(gp_Ax1(center,axis),angle);
            point.Transform(swing);tangent=gp_Dir(gp_Vec(tangent).Transformed(swing));normal=gp_Dir(gp_Vec(normal).Transformed(swing));binormal=tangent.Crossed(normal);bendNormal=gp_Dir(gp_Vec(bendNormal).Transformed(swing));
        }else throw std::runtime_error("Rectangular tube segment must be line or arc");
    }
    if(!centerline.IsDone())throw std::runtime_error("Rectangular tube centerline is empty");
    auto sectionWire=[&](double sectionWidth,double sectionHeight){
        const double halfWidth=sectionWidth*0.5,halfHeight=sectionHeight*0.5;
        const gp_Vec alongNormal(startNormal),alongBinormal(startTangent.Crossed(startNormal));
        auto corner=[&](double u,double v){return startPoint.Translated(alongNormal*u+alongBinormal*v);};
        const gp_Pnt corners[4]={corner(-halfWidth,-halfHeight),corner(halfWidth,-halfHeight),corner(halfWidth,halfHeight),corner(-halfWidth,halfHeight)};
        BRepBuilderAPI_MakeWire wire;
        for(int i=0;i<4;++i){BRepBuilderAPI_MakeEdge edge(corners[i],corners[(i+1)%4]);if(!edge.IsDone())throw std::runtime_error("Rectangular tube section failed");wire.Add(edge.Edge());if(!wire.IsDone())throw std::runtime_error("Rectangular tube section failed");}
        return wire.Wire();
    };
    auto pipeSolid=[&](double sectionWidth,double sectionHeight){
        BRepOffsetAPI_MakePipeShell pipe(centerline.Wire());
        pipe.SetMode(false);pipe.SetTolerance(1e-6,1e-6,1e-4);pipe.Add(sectionWire(sectionWidth,sectionHeight),false,true);pipe.Build();
        if(!pipe.IsDone()||!pipe.MakeSolid())throw std::runtime_error("Rectangular tube sweep failed");
        TopoDS_Shape solid=pipe.Shape();if(properties(solid)<0)solid.Reverse();return solid;
    };
    BRepAlgoAPI_Cut hollow(pipeSolid(width,height),pipeSolid(width-2*wall,height-2*wall));hollow.Build();
    if(!hollow.IsDone()||hollow.HasErrors())throw std::runtime_error("Rectangular tube hollow cut failed");
    TopoDS_Shape solid=hollow.Shape();if(properties(solid)<0)solid.Reverse();
    if(shapes(solid,TopAbs_SOLID).size()!=1||!(properties(solid)>0))throw std::runtime_error("Rectangular tube must be one positive solid");
    const gp_Pnt endPoint=point;const gp_Dir endTangent=tangent;
    int startCaps=0,endCaps=0,sides=0;bool axisBound=false;TopoDS_Face firstPlane;
    auto nearer=[&](const TopoDS_Face& face){gp_XYZ sum(0,0,0);int count=0;for(const TopoDS_Shape& vertex:shapes(face,TopAbs_VERTEX)){sum+=BRep_Tool::Pnt(TopoDS::Vertex(vertex)).XYZ();++count;}gp_Pnt mid(count?sum/count:gp_Pnt());return mid.Distance(startPoint)<=mid.Distance(endPoint);};
    for(const TopoDS_Shape& item:shapes(solid,TopAbs_FACE)){
        TopoDS_Face face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane){add(refs,"side."+std::to_string(sides++),"face",face);continue;}
        if(firstPlane.IsNull())firstPlane=face;
        gp_Dir faceNormal=surface.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)faceNormal.Reverse();
        if(std::abs(faceNormal.Dot(startTangent))>0.9||std::abs(faceNormal.Dot(endTangent))>0.9){
            const bool start=nearer(face);int& slot=start?startCaps:endCaps;add(refs,std::string(start?"cap.start":"cap.end")+(slot?"."+std::to_string(slot):""),"face",face);++slot;
        }else add(refs,"side."+std::to_string(sides++),"face",face);
    }
    if(!firstPlane.IsNull()&&!axisBound){
        Ref axis;axis.name="axis";axis.kind="axis";axis.status="bound";axis.shape=firstPlane;axis.seed=V::makeObj();
        axis.seed.set("axis",direction(startTangent));axis.seed.set("origin",xyz(startPoint));refs.push_back(std::move(axis));axisBound=true;
    }
    return solid;
}

int whole(const V& request, const char* key, int low, int high){
    double value=scalar(request,key);
    if(value<low||value>high||std::floor(value)!=value)throw std::runtime_error(std::string(key)+" is outside its integer range");
    return static_cast<int>(value);
}
bool bit(const V& request, const char* key, bool fallback){
    const V* value=request.find(key);if(!value)return fallback;
    if(value->t!=V::Num||(value->num!=0&&value->num!=1))throw std::runtime_error(std::string(key)+" must be 0 or 1");
    return value->num==1;
}
gp_Dir axisDir(const std::string& name){
    if(name=="X")return {1,0,0};if(name=="Y")return {0,1,0};if(name=="Z")return {0,0,1};
    throw std::runtime_error("Axis must be X, Y, or Z");
}
gp_Dir planeNormal(const std::string& name){
    if(name=="XY")return {0,0,1};if(name=="XZ")return {0,1,0};if(name=="YZ")return {1,0,0};
    throw std::runtime_error("Plane must be XY, XZ, or YZ");
}
void requireOneSolid(const TopoDS_Shape& shape,const char* action){
    if(shapes(shape,TopAbs_SOLID).size()!=1)throw std::runtime_error(std::string(action)+" needs one solid");
}
gp_Dir outwardNormal(const TopoDS_Face& face,const gp_Pnt& probe){
    Handle(Geom_Surface) surface=BRep_Tool::Surface(face);
    GeomAPI_ProjectPointOnSurf projection(probe,surface);
    if(projection.NbPoints()<1)throw std::runtime_error("Edge midpoint missed an adjacent face");
    double u=0,v=0;projection.LowerDistanceParameters(u,v);
    BRepLProp_SLProps props(BRepAdaptor_Surface(face),u,v,1,1e-6);
    if(!props.IsNormalDefined())throw std::runtime_error("Adjacent face has no normal");
    gp_Dir normal=props.Normal();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();return normal;
}
std::vector<TopoDS_Edge> chosenEdges(const TopoDS_Shape& shape,const V& request,const std::vector<Ref>& refs){
    if(const V* names=request.find("edges")){
        if(names->t!=V::Arr||names->arr.empty()||names->arr.size()>64)throw std::runtime_error("Fillet/chamfer needs 1..64 named edges");
        std::set<std::string> picked;std::vector<TopoDS_Edge> edges;
        for(const auto& item:names->arr){if(item.t!=V::Str||!picked.insert(item.str).second)throw std::runtime_error("Invalid or repeated edge name");bool found=false;for(const auto& ref:refs)if(ref.name==item.str){if(ref.kind!="edge"||!selectable(ref.status))throw std::runtime_error("Selected reference is not a bound edge");edges.push_back(TopoDS::Edge(ref.shape));found=true;break;}if(!found)throw std::runtime_error("Unknown semantic edge name");}
        return edges;
    }
    const std::string selection=request.find("selection")?word(request,"selection"):"";
    if(selection!="sharp"&&selection!="convex"&&selection!="concave"&&selection!="all")throw std::runtime_error("Fillet/chamfer needs named edges or selection sharp, convex, concave, or all");
    const double minimum=request.find("min_angle")?scalar(request,"min_angle"):15;
    if(minimum<0||minimum>=180)throw std::runtime_error("min_angle must be in [0, 180) degrees");
    TopTools_IndexedDataMapOfShapeListOfShape ancestors;TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,ancestors);
    std::vector<TopoDS_Edge> edges;
    for(int index=1;index<=ancestors.Extent();++index){
        TopoDS_Edge edge=TopoDS::Edge(ancestors.FindKey(index));const auto& faces=ancestors.FindFromIndex(index);if(faces.Extent()!=2)continue;
        BRepAdaptor_Curve curve(edge);double first=curve.FirstParameter(),last=curve.LastParameter();gp_Pnt midpoint;gp_Vec tangent;curve.D1((first+last)*0.5,midpoint,tangent);
        if(tangent.Magnitude()<=1e-12)continue;if(edge.Orientation()==TopAbs_REVERSED)tangent.Reverse();
        gp_Dir firstNormal=outwardNormal(TopoDS::Face(faces.First()),midpoint),secondNormal=outwardNormal(TopoDS::Face(faces.Last()),midpoint);
        double dot=firstNormal.Dot(secondNormal);dot=dot<-1?-1:dot>1?1:dot;double turn=std::acos(dot)*180/std::acos(-1.0);
        bool convex=gp_Vec(firstNormal).Crossed(gp_Vec(secondNormal)).Dot(tangent)>0;bool take=false;
        if(selection=="all")take=true;
        else if(selection=="sharp")take=turn+1e-8>=minimum;
        else if(selection=="convex")take=convex&&turn+1e-8>=minimum;
        else take=!convex&&turn+1e-8>=minimum;
        if(take)edges.push_back(edge);
    }
    if(edges.empty())throw std::runtime_error("Edge selection matched no boundary");
    if(edges.size()>64)throw std::runtime_error("Edge selection matched more than 64 edges");
    return edges;
}
std::vector<TopoDS_Face> draftFaces(const TopoDS_Shape& shape,const V& request,const std::vector<Ref>& refs,const gp_Dir& pull){
    std::vector<TopoDS_Face> faces;
    if(const V* names=request.find("faces")){
        if(names->t!=V::Arr||names->arr.empty()||names->arr.size()>64)throw std::runtime_error("Draft needs 1..64 named faces");
        std::set<std::string> picked;
        for(const auto& item:names->arr){if(item.t!=V::Str||!picked.insert(item.str).second)throw std::runtime_error("Invalid or repeated face name");bool found=false;for(const auto& ref:refs)if(ref.name==item.str){if(ref.kind!="face"||!selectable(ref.status))throw std::runtime_error("Selected reference is not a bound face");faces.push_back(TopoDS::Face(ref.shape));found=true;break;}if(!found)throw std::runtime_error("Unknown semantic face name");}
        return faces;
    }
    for(const TopoDS_Shape& item:shapes(shape,TopAbs_FACE)){
        TopoDS_Face face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane&&surface.GetType()!=GeomAbs_Cylinder&&surface.GetType()!=GeomAbs_Cone)continue;
        gp_Dir axis=surface.GetType()==GeomAbs_Plane?surface.Plane().Axis().Direction():surface.GetType()==GeomAbs_Cylinder?surface.Cylinder().Axis().Direction():surface.Cone().Axis().Direction();
        if(std::abs(axis.Dot(pull))>0.999)continue;
        faces.push_back(face);
    }
    if(faces.empty()||faces.size()>64)throw std::runtime_error("Draft found no planar, cylindrical, or conical side face");
    return faces;
}
gp_Ax2 mirrorPlane(const V& request,const std::vector<Ref>& refs){
    if(const V* faceName=request.find("face")){
        if(faceName->t!=V::Str||faceName->str.empty())throw std::runtime_error("Mirror face must be a semantic name");
        for(const auto& ref:refs)if(ref.name==faceName->str){
            if(ref.kind!="face"||!selectable(ref.status))throw std::runtime_error("Mirror reference is not a bound face");
            BRepAdaptor_Surface surface(TopoDS::Face(ref.shape));
            if(surface.GetType()!=GeomAbs_Plane)throw std::runtime_error("Mirror face must be planar");
            gp_Pln plane=surface.Plane();return gp_Ax2(plane.Location(),plane.Axis().Direction());
        }
        throw std::runtime_error("Unknown semantic face name");
    }
    if(const V* normal=request.find("normal")){
        gp_Pnt directionPoint=point(*normal,"mirror normal");gp_Vec axis(directionPoint.X(),directionPoint.Y(),directionPoint.Z());
        if(axis.Magnitude()<=1e-12)throw std::runtime_error("Mirror normal is zero");
        gp_Pnt origin=request.find("origin")?point(*request.find("origin"),"mirror origin"):gp_Pnt(optional(request,"ox",0),optional(request,"oy",0),optional(request,"oz",0));
        return gp_Ax2(origin,gp_Dir(axis));
    }
    std::string plane=request.find("plane")?word(request,"plane"):"XY";
    return gp_Ax2(gp_Pnt(optional(request,"ox",0),optional(request,"oy",0),optional(request,"oz",0)),planeNormal(plane));
}
TopoDS_Shape patterned(const TopoDS_Shape& input,const V& request,const std::string& op,std::vector<Ref>& refs){
    const bool mirror=op=="mirror";
    const int count=mirror?2:whole(request,"count",2,64);
    const bool fuseCopies=bit(request,"fuse",false);
    auto copyAt=[&](int index){
        gp_Trsf step;
        if(mirror)step.SetMirror(mirrorPlane(request,refs));
        else if(op=="circular_pattern"){std::string axis=request.find("axis")?word(request,"axis"):"Z";double angle=scalar(request,"step");if(!(std::abs(angle)>0&&std::abs(angle)<=360))throw std::runtime_error("Circular pattern step must be in (0, 360] degrees");step.SetRotation(gp_Ax1(gp_Pnt(optional(request,"ox",0),optional(request,"oy",0),optional(request,"oz",0)),axisDir(axis)),index*angle*std::acos(-1.0)/180);}
        else{double dx=optional(request,"dx",0),dy=optional(request,"dy",0),dz=optional(request,"dz",0);if(dx*dx+dy*dy+dz*dz<=1e-18)throw std::runtime_error("Linear pattern spacing is zero");step.SetTranslation(gp_Vec(dx,dy,dz)*index);}
        return transformed(input,step);
    };
    std::vector<TopoDS_Shape> copies{input};for(int index=1;index<count;++index)copies.push_back(copyAt(index));
    if(!fuseCopies)return copies.size()==1?copies.front():compound(copies);
    TopoDS_Shape accumulated=copies.front();
    for(size_t index=1;index<copies.size();++index){
        BRepAlgoAPI_Fuse algorithm(accumulated,copies[index]);algorithm.Build();
        if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Pattern fuse failed; use fuse 0 to keep separate solids");
        refs=propagated(refs,algorithm.Shape(),algorithm);accumulated=algorithm.Shape();
    }
    return accumulated;
}
V finishBodies(const TopoDS_Shape& shape,const std::vector<Ref>& refs){
    auto solids=shapes(shape,TopAbs_SOLID);if(solids.empty()||solids.size()>64)throw std::runtime_error("Pattern needs 1..64 solids");
    for(const auto& solid:solids)if(!(properties(solid)>0))throw std::runtime_error("Pattern instance has no positive volume");
    V result=shapeResult(shape,false);result.set("topology",topologyOf(shape,need(result,"brep_base64").str,refs));return result;
}

V modelSolidNative(const V& request){
    const std::string op=word(request,"op");std::vector<Ref> refs;TopoDS_Shape result;bool many=false;
    if(op=="box"){
        double x=positive(request,"width"),y=positive(request,"depth"),z=positive(request,"height");BRepPrimAPI_MakeBox box(x,y,z);result=box.Shape();
        add(refs,"x_min","face",box.BackFace());add(refs,"x_max","face",box.FrontFace());add(refs,"y_min","face",box.LeftFace());add(refs,"y_max","face",box.RightFace());add(refs,"z_min","face",box.BottomFace());add(refs,"z_max","face",box.TopFace());boxEdges(result,x,y,z,refs);
    }else if(op=="cylinder"){
        result=BRepPrimAPI_MakeCylinder(positive(request,"radius"),positive(request,"height")).Shape();axialFaces(result,refs,true);
    }else if(op=="sphere"){
        result=BRepPrimAPI_MakeSphere(positive(request,"radius")).Shape();for(auto& f:shapes(result,TopAbs_FACE))add(refs,"surface","face",f);
    }else if(op=="cone"){
        double a=optional(request,"radius_bottom",-1),b=optional(request,"radius_top",-1),h=positive(request,"height");if(a<0||b<0||(a==0&&b==0))throw std::runtime_error("Cone radii must be nonnegative with one positive");result=BRepPrimAPI_MakeCone(a,b,h).Shape();axialFaces(result,refs,false);
    }else if(op=="torus"){
        double major=positive(request,"major_radius"),minor=positive(request,"minor_radius");if(major<=minor)throw std::runtime_error("Torus major radius must exceed minor radius");result=BRepPrimAPI_MakeTorus(major,minor).Shape();for(auto& f:shapes(result,TopAbs_FACE))add(refs,"surface","face",f);
    }else if(op=="round_tube")result=roundTube(request,refs);
    else if(op=="rect_tube")result=rectTube(request,refs);
    else if(op=="extrude")result=makeExtrusion(request,refs);
    else {
        const auto& encoded=need(request,"input_brep");if(encoded.t!=V::Str)throw std::runtime_error("Input must be a native B-rep");TopoDS_Shape input=readBrep(encoded.str);shapeReport(input,true);refs=inputRefs(request,"input_brep","input_topology",input);
        if(op=="union"||op=="cut"||op=="common"||op=="hole"){
            TopoDS_Shape tool;std::vector<Ref> toolRefs;
            if(op=="hole")tool=cutter(need(request,"cutter"));
            else{const auto& encodedTool=need(request,"tool_brep");if(encodedTool.t!=V::Str)throw std::runtime_error("Tool must be native B-rep");tool=readBrep(encodedTool.str);shapeReport(tool,false);if(shapes(tool,TopAbs_SOLID).empty()||!(properties(tool)>0))throw std::runtime_error("Boolean tool needs one or more positive-volume solids");toolRefs=inputRefs(request,"tool_brep","tool_topology",tool);}
            if(op=="union"){BRepAlgoAPI_Fuse algorithm(input,tool);algorithm.Build();if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Native union failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);auto next=propagated(toolRefs,result,algorithm);
                std::string nameSpace="tool";
                const V* supplied=request.find("reference_namespace");if(!supplied)supplied=request.find("tool_namespace");
                if(supplied){if(supplied->t!=V::Str||supplied->str.empty()||supplied->str.size()>128)throw std::runtime_error("Invalid reference namespace");nameSpace=supplied->str;}
                else{int ordinal=2;bool occupied=true;while(occupied){occupied=false;for(const auto& r:refs)if(r.name.rfind(nameSpace+".",0)==0){occupied=true;break;}if(occupied)nameSpace="tool"+std::to_string(ordinal++);}}
                for(auto& r:next){r.name=nameSpace+"."+r.name;refs.push_back(r);}
            }
            else if(op=="common"){BRepAlgoAPI_Common algorithm(input,tool);algorithm.Build();if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Native common failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);}
            else{BRepAlgoAPI_Cut algorithm(input,tool);algorithm.Build();if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Native cut failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);}
        }else if(op=="fillet"||op=="chamfer"){
            requireOneSolid(input,"Fillet/chamfer");double size=positive(request,op=="fillet"?"radius":"distance");
            if(op=="chamfer"&&request.find("distance2")){double second=positive(request,"distance2");if(std::abs(second-size)>1e-12)throw std::runtime_error("Asymmetric chamfer is not supported; use one distance");}
            auto edges=chosenEdges(input,request,refs);
            if(op=="fillet"){BRepFilletAPI_MakeFillet algorithm(input);for(auto& edge:edges)algorithm.Add(size,edge);algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native fillet failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);}
            else{BRepFilletAPI_MakeChamfer algorithm(input);for(auto& edge:edges)algorithm.Add(size,edge);algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native chamfer failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);}
        }else if(op=="draft"){
            requireOneSolid(input,"Draft");gp_Dir pull=axisDir(request.find("direction")?word(request,"direction"):"Z");
            double degrees=scalar(request,"angle");if(!(std::abs(degrees)>0&&std::abs(degrees)<89))throw std::runtime_error("Draft angle must be in (-89, 89) degrees excluding 0");
            std::string neutral=request.find("neutral")?word(request,"neutral"):"XY";gp_Dir normal=planeNormal(neutral);
            gp_Pln plane(gp_Pnt(normal.XYZ()*optional(request,"neutral_offset",0)),normal);
            BRepOffsetAPI_DraftAngle algorithm(input);
            for(const auto& face:draftFaces(input,request,refs,pull)){
                algorithm.Add(face,pull,degrees*std::acos(-1.0)/180,plane,true);
                if(!algorithm.AddDone())throw std::runtime_error("Draft rejected a face that is not planar, cylindrical, or conical");
            }
            algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native draft failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);
        }else if(op=="linear_pattern"||op=="circular_pattern"||op=="mirror"){
            result=patterned(input,request,op,refs);many=true;
        }else if(op=="shell"){
            requireOneSolid(input,"Shell");
            double thickness=scalar(request,"thickness");if(!(std::abs(thickness)>1e-6))throw std::runtime_error("Shell thickness must be nonzero");
            NCollection_List<TopoDS_Shape> opening;
            if(const V* names=request.find("faces")){
                if(names->t!=V::Arr||names->arr.size()>64)throw std::runtime_error("Shell openings need 0..64 named faces");
                std::set<std::string> picked;
                for(const auto& item:names->arr){if(item.t!=V::Str||!picked.insert(item.str).second)throw std::runtime_error("Invalid or repeated face name");bool found=false;for(const auto& ref:refs)if(ref.name==item.str){if(ref.kind!="face"||!selectable(ref.status))throw std::runtime_error("Selected reference is not a bound face");bool same=false;for(NCollection_List<TopoDS_Shape>::Iterator it(opening);it.More();it.Next())if(it.Value().IsSame(ref.shape))same=true;if(!same)opening.Append(ref.shape);found=true;break;}if(!found)throw std::runtime_error("Unknown semantic face name");}
            }
            BRepOffsetAPI_MakeThickSolid algorithm;
            algorithm.MakeThickSolidByJoin(input,opening,thickness,1e-4,BRepOffset_Skin,false,false,GeomAbs_Arc,false);
            algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native shell failed");
            result=algorithm.Shape();refs=propagated(refs,result,algorithm);
        }else if(op=="transform"){
            auto m=frame(request);auto placement=transform(m);
            for(auto& ref:refs)for(const char* key:{"axis","tangent"})if(const V* previous=ref.seed.find(key)){auto value=point(*previous,"reference direction");gp_Vec vector(value.X(),value.Y(),value.Z());vector.Transform(placement);ref.seed.set(key,vec(vector.XYZ()));}
            BRepBuilderAPI_Transform algorithm(input,placement,true);if(!algorithm.IsDone())throw std::runtime_error("Native rigid transform failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);
        }else throw std::runtime_error("Unknown native solid operation");
    }
    return many?finishBodies(result,refs):finish(result,refs);
}

V referenceFrameNative(const V& request){
    const auto& encoded=need(request,"input_brep");if(encoded.t!=V::Str)throw std::runtime_error("Reference input is not a B-rep");TopoDS_Shape shape=readBrep(encoded.str);
    auto refs=inputRefs(request,"input_brep","topology",shape);std::string name=word(request,"reference");double fraction=optional(request,"fraction",.5);if(fraction<0||fraction>1)throw std::runtime_error("Reference fraction outside [0,1]");
    for(const auto& ref:refs)if(ref.name==name){if(!selectable(ref.status))throw std::runtime_error("Reference "+name+" is "+ref.status);V d=descriptor(ref,fraction);const auto* origin=d.find("origin");if(!origin)throw std::runtime_error("Reference has no analytic origin");gp_Pnt p=point(*origin,"origin");
        const V* normal=d.find("normal");if(!normal)normal=d.find("axis");if(!normal)normal=d.find("tangent");if(!normal)throw std::runtime_error("Reference has no analytic direction");gp_Pnt npoint=point(*normal,"direction");gp_Dir z(npoint.X(),npoint.Y(),npoint.Z());
        gp_Dir x(1,0,0);if(const auto* xAxis=d.find("x_axis")){auto v=point(*xAxis,"x_axis");x=gp_Dir(v.X(),v.Y(),v.Z());}
        else if(std::abs(z.Dot(x))>.95)x=gp_Dir(0,1,0);gp_Vec projected=gp_Vec(x)-gp_Vec(z)*z.Dot(x);if(projected.Magnitude()<1e-10)throw std::runtime_error("Reference frame axis degeneracy");x=gp_Dir(projected);gp_Dir y(gp_Vec(z).Crossed(gp_Vec(x)));
        V frameValue=V::makeArr();for(double value:{x.X(),x.Y(),x.Z(),0.,y.X(),y.Y(),y.Z(),0.,z.X(),z.Y(),z.Z(),0.,p.X(),p.Y(),p.Z(),1.})frameValue.arr.push_back(V::makeNum(value));
        V output=V::makeObj(),evidence=V::makeObj();output.set("frame",frameValue);output.set("kind",V::makeStr(ref.kind));if(auto radius=d.find("radius"))output.set("radius",*radius);evidence.set("name",V::makeStr(name));evidence.set("kind",V::makeStr(ref.kind));evidence.set("brep_sha256",V::makeStr(digest(encoded.str)));V indices=V::makeArr();indices.arr.push_back(V::makeNum(indexOf(shape,ref.shape,refType(ref.kind))));evidence.set("indices",indices);output.set("evidence",evidence);return output;
    }throw std::runtime_error("Unknown semantic reference: "+name);
}

std::string faceNear(const std::vector<Ref>& refs,const gp_Pnt& partPoint){
    std::string best;double bestDist=1e-2;bool bestMerged=false;
    for(const auto& ref:refs){
        if(ref.kind!="face"||!selectable(ref.status)||ref.shape.IsNull())continue;
        TopoDS_Face face=TopoDS::Face(ref.shape);TopLoc_Location location;Handle(Geom_Surface) surface=BRep_Tool::Surface(face,location);
        if(surface.IsNull())continue;
        gp_Pnt local=partPoint.Transformed(location.Transformation().Inverted());
        GeomAPI_ProjectPointOnSurf projection(local,surface);
        if(projection.NbPoints()<1||projection.LowerDistance()>1e-3)continue;
        double u=0,v=0;projection.LowerDistanceParameters(u,v);
        BRepClass_FaceClassifier classifier(face,gp_Pnt2d(u,v),1e-4);
        if(classifier.State()==TopAbs_OUT)continue;
        const bool merged=ref.status=="merged";const double dist=projection.LowerDistance();
        if(best.empty()||dist<bestDist-1e-6||(std::abs(dist-bestDist)<=1e-6&&merged&&!bestMerged)){best=ref.name;bestDist=dist;bestMerged=merged;}
    }
    return best;
}
double parallelGap(const TopoDS_Shape& shape,bool& found){
    struct PlaneFace{gp_Pnt origin;gp_Dir normal;};
    std::vector<PlaneFace> planes;found=false;double best=1e300;
    for(const auto& item:shapes(shape,TopAbs_FACE)){
        TopoDS_Face face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)continue;
        gp_Dir normal=surface.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        planes.push_back({surface.Plane().Location(),normal});
    }
    for(size_t i=0;i<planes.size();++i)for(size_t j=i+1;j<planes.size();++j){
        if(std::abs(planes[i].normal.Dot(planes[j].normal))<0.999)continue;
        const double gap=std::abs(gp_Vec(planes[i].origin,planes[j].origin).Dot(planes[i].normal));
        if(gap>1e-4&&gap<best){best=gap;found=true;}
    }
    return best;
}
V clearanceNative(const V& request){
    const V& encodedA=need(request,"a_brep");const V& encodedB=need(request,"b_brep");
    if(encodedA.t!=V::Str||encodedB.t!=V::Str)throw std::runtime_error("Clearance inputs must be native B-reps");
    TopoDS_Shape shapeA=readBrep(encodedA.str),shapeB=readBrep(encodedB.str);
    V holderA=V::makeObj(),holderB=V::makeObj();
    if(const V* frameA=request.find("a_frame"))holderA.set("frame",*frameA);
    if(const V* frameB=request.find("b_frame"))holderB.set("frame",*frameB);
    const gp_Trsf placeA=transform(frame(holderA)),placeB=transform(frame(holderB));
    TopoDS_Shape placedA=transformed(shapeA,placeA),placedB=transformed(shapeB,placeB);
    BRepExtrema_DistShapeShape distance(placedA,placedB);distance.Perform();
    if(!distance.IsDone()||distance.NbSolution()<1)throw std::runtime_error("Exact clearance failed");
    const double gap=distance.Value();
    BRepAlgoAPI_Common overlap(placedA,placedB);overlap.Build();
    const bool overlapOk=overlap.IsDone()&&!overlap.HasErrors();
    const double overlapVolume=overlapOk?std::abs(properties(overlap.Shape())):0;
    if(gap<=1e-3&&!overlapOk)throw std::runtime_error("Exact clearance could not measure the intersection");
    constexpr double gapTol=1e-3,volumeTol=1e-2;
    std::string state,method;double clearance=0;
    if(gap>gapTol&&overlapVolume>volumeTol){state="contain";method="exact_distance";clearance=-gap;}
    else if(gap>gapTol){state="separate";method="exact_distance";clearance=gap;}
    else if(overlapVolume>volumeTol){
        state="interfere";bool width=false;const double penetration=parallelGap(overlap.Shape(),width);
        if(width){method="intersection_width";clearance=-penetration;}
        else{auto box=bounds(overlap.Shape());const double side=std::min(box[3]-box[0],std::min(box[4]-box[1],box[5]-box[2]));method="intersection_bounds";clearance=-side;}
    }else{state="contact";method="exact_distance";clearance=0;}
    const gp_Pnt pointA=distance.PointOnShape1(1).Transformed(placeA.Inverted());
    const gp_Pnt pointB=distance.PointOnShape2(1).Transformed(placeB.Inverted());
    V result=V::makeObj();
    result.set("clearance_checked",V::makeBool(true));
    result.set("clearance_mm",V::makeNum(clearance));
    result.set("clearance_state",V::makeStr(state));
    result.set("clearance_method",V::makeStr(method));
    result.set("face_a",V::makeStr(faceNear(inputRefs(request,"a_brep","a_topology",shapeA),pointA)));
    result.set("face_b",V::makeStr(faceNear(inputRefs(request,"b_brep","b_topology",shapeB),pointB)));
    result.set("clearance_authority",V::makeStr("exact_brep"));
    return result;
}
V pointDistanceNative(const V& request){
    const V& encoded=need(request,"brep");
    if(encoded.t!=V::Str)throw std::runtime_error("Point distance input is not a B-rep");
    TopoDS_Shape shape=readBrep(encoded.str);
    gp_Pnt query=point(need(request,"point"),"point");
    TopoDS_Shape boundary=shape;
    const auto faces=shapes(shape,TopAbs_FACE);
    if(!faces.empty()){BRep_Builder builder;TopoDS_Compound skin;builder.MakeCompound(skin);for(const auto& face:faces)builder.Add(skin,face);boundary=skin;}
    const TopoDS_Shape probe=BRepBuilderAPI_MakeVertex(query).Shape();
    BRepExtrema_DistShapeShape distance(probe,boundary);
    distance.Perform();
    if(!distance.IsDone()||distance.NbSolution()<1)throw std::runtime_error("Point distance failed");
    double gap=distance.Value();
    // A wire has no interior. Only one solid reports an inside point as negative.
    const auto solids=shapes(shape,TopAbs_SOLID);
    if(solids.size()==1){
        BRepClass3d_SolidClassifier classifier(solids[0],query,1e-7);
        if(classifier.State()==TopAbs_IN) gap=-gap;
    }
    std::string face;
    if(request.find("topology")) face=faceNear(inputRefs(request,"brep","topology",shape),distance.PointOnShape2(1));
    V result=V::makeObj();
    result.set("distance_mm",V::makeNum(gap));
    result.set("face",V::makeStr(face));
    return result;
}
V solidCentroidNative(const V& request){
    const V& encoded=need(request,"brep");
    if(encoded.t!=V::Str)throw std::runtime_error("Centroid input is not a B-rep");
    TopoDS_Shape shape=readBrep(encoded.str);
    if(shapes(shape,TopAbs_SOLID).size()!=1)throw std::runtime_error("Centroid needs one solid");
    GProp_GProps props;BRepGProp::VolumeProperties(shape,props);
    if(!(props.Mass()>0))throw std::runtime_error("Centroid needs positive volume");
    const gp_Pnt center=props.CentreOfMass();
    V result=V::makeObj(),pointValue=V::makeArr();
    result.set("volume_mm3",V::makeNum(props.Mass()));
    for(double value:{center.X(),center.Y(),center.Z()})pointValue.arr.push_back(V::makeNum(value));
    result.set("center_mm",pointValue);
    return result;
}
}
