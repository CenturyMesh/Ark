#include "ark/occt_solid.hpp"
#include "common.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BezierCurve.hxx>
#include <NCollection_Array1.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

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
struct Ref { std::string name,kind,status; TopoDS_Shape shape; V seed; };
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
    if(surface.GetType()==GeomAbs_Plane){auto pl=surface.Plane();gp_Dir n=pl.Axis().Direction();if(ref.shape.Orientation()==TopAbs_REVERSED)n.Reverse();d.set("origin",xyz(pl.Location()));d.set("normal",direction(n));d.set("x_axis",direction(pl.XAxis().Direction()));return d;}
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
        if(ref.status=="bound"){auto& indices=need(row,"indices");if(indices.t!=V::Arr||indices.arr.size()!=1||indices.arr[0].t!=V::Num||std::floor(indices.arr[0].num)!=indices.arr[0].num||indices.arr[0].num<0||indices.arr[0].num>1000000)throw std::runtime_error("Ambiguous topology binding");ref.shape=atIndex(shape,refType(ref.kind),static_cast<int>(indices.arr[0].num));if(const V* seed=row.find("descriptor"))ref.seed=*seed;}
        out.push_back(ref);
    }return out;
}
V topologyOf(const TopoDS_Shape& shape,const std::string& encoded,const std::vector<Ref>& refs){
    V out=V::makeObj(),rows=V::makeArr();out.set("brep_sha256",V::makeStr(digest(encoded)));
    std::set<std::string> names;
    for(const auto& ref:refs){if(!names.insert(ref.name).second)throw std::runtime_error("Duplicate output reference name");V row=V::makeObj();row.set("name",V::makeStr(ref.name));row.set("kind",V::makeStr(ref.kind));V indices=V::makeArr();std::string status=ref.status;
        if(status=="bound"){int i=indexOf(shape,ref.shape,refType(ref.kind));if(i<0)status="deleted";else indices.arr.push_back(V::makeNum(i));}
        row.set("status",V::makeStr(status));row.set("indices",indices);if(status=="bound"){V data=descriptor(ref);row.set("frame_available",V::makeBool(data.find("normal")||data.find("axis")||data.find("tangent")));row.set("descriptor",data);}rows.arr.push_back(row);
    }out.set("references",rows);return out;
}
template<class Operation>std::vector<Ref> propagated(const std::vector<Ref>& refs,const TopoDS_Shape& result,Operation& op){
    std::vector<Ref> out;out.reserve(refs.size());
    for(auto ref:refs){if(ref.status!="bound"){out.push_back(ref);continue;}std::vector<TopoDS_Shape> matches;
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
        }else throw std::runtime_error("Solid profile supports line, Bezier, and circle entities");
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

V modelSolidNative(const V& request){
    const std::string op=word(request,"op");std::vector<Ref> refs;TopoDS_Shape result;
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
    }else if(op=="extrude")result=makeExtrusion(request,refs);
    else {
        const auto& encoded=need(request,"input_brep");if(encoded.t!=V::Str)throw std::runtime_error("Input must be a native B-rep");TopoDS_Shape input=readBrep(encoded.str);shapeReport(input,true);refs=inputRefs(request,"input_brep","input_topology",input);
        if(op=="union"||op=="cut"||op=="common"||op=="hole"){
            TopoDS_Shape tool;std::vector<Ref> toolRefs;
            if(op=="hole")tool=cutter(need(request,"cutter"));
            else{const auto& encodedTool=need(request,"tool_brep");if(encodedTool.t!=V::Str)throw std::runtime_error("Tool must be native B-rep");tool=readBrep(encodedTool.str);shapeReport(tool,true);toolRefs=inputRefs(request,"tool_brep","tool_topology",tool);}
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
            auto& names=need(request,"edges");if(names.t!=V::Arr||names.arr.empty()||names.arr.size()>64)throw std::runtime_error("Fillet/chamfer needs named edges");double size=positive(request,op=="fillet"?"radius":"distance");
            std::set<std::string> picked;std::vector<TopoDS_Edge> edges;
            for(auto& item:names.arr){if(item.t!=V::Str||!picked.insert(item.str).second)throw std::runtime_error("Invalid or repeated edge name");bool found=false;for(auto& r:refs)if(r.name==item.str){if(r.kind!="edge"||r.status!="bound")throw std::runtime_error("Selected reference is not a bound edge");edges.push_back(TopoDS::Edge(r.shape));found=true;break;}if(!found)throw std::runtime_error("Unknown semantic edge name");}
            if(op=="fillet"){BRepFilletAPI_MakeFillet algorithm(input);for(auto& e:edges)algorithm.Add(size,e);algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native fillet failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);}
            else{BRepFilletAPI_MakeChamfer algorithm(input);for(auto& e:edges)algorithm.Add(size,e);algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native chamfer failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);}
        }else if(op=="transform"){
            auto m=frame(request);auto placement=transform(m);
            for(auto& ref:refs)for(const char* key:{"axis","tangent"})if(const V* previous=ref.seed.find(key)){auto value=point(*previous,"reference direction");gp_Vec vector(value.X(),value.Y(),value.Z());vector.Transform(placement);ref.seed.set(key,vec(vector.XYZ()));}
            BRepBuilderAPI_Transform algorithm(input,placement,true);if(!algorithm.IsDone())throw std::runtime_error("Native rigid transform failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);
        }else throw std::runtime_error("Unknown native solid operation");
    }
    return finish(result,refs);
}

V referenceFrameNative(const V& request){
    const auto& encoded=need(request,"input_brep");if(encoded.t!=V::Str)throw std::runtime_error("Reference input is not a B-rep");TopoDS_Shape shape=readBrep(encoded.str);
    auto refs=inputRefs(request,"input_brep","topology",shape);std::string name=word(request,"reference");double fraction=optional(request,"fraction",.5);if(fraction<0||fraction>1)throw std::runtime_error("Reference fraction outside [0,1]");
    for(const auto& ref:refs)if(ref.name==name){if(ref.status!="bound")throw std::runtime_error("Reference "+name+" is "+ref.status);V d=descriptor(ref,fraction);const auto* origin=d.find("origin");if(!origin)throw std::runtime_error("Reference has no analytic origin");gp_Pnt p=point(*origin,"origin");
        const V* normal=d.find("normal");if(!normal)normal=d.find("axis");if(!normal)normal=d.find("tangent");if(!normal)throw std::runtime_error("Reference has no analytic direction");gp_Pnt npoint=point(*normal,"direction");gp_Dir z(npoint.X(),npoint.Y(),npoint.Z());
        gp_Dir x(1,0,0);if(const auto* xAxis=d.find("x_axis")){auto v=point(*xAxis,"x_axis");x=gp_Dir(v.X(),v.Y(),v.Z());}
        else if(std::abs(z.Dot(x))>.95)x=gp_Dir(0,1,0);gp_Vec projected=gp_Vec(x)-gp_Vec(z)*z.Dot(x);if(projected.Magnitude()<1e-10)throw std::runtime_error("Reference frame axis degeneracy");x=gp_Dir(projected);gp_Dir y(gp_Vec(z).Crossed(gp_Vec(x)));
        V frameValue=V::makeArr();for(double value:{x.X(),x.Y(),x.Z(),0.,y.X(),y.Y(),y.Z(),0.,z.X(),z.Y(),z.Z(),0.,p.X(),p.Y(),p.Z(),1.})frameValue.arr.push_back(V::makeNum(value));
        V output=V::makeObj(),evidence=V::makeObj();output.set("frame",frameValue);output.set("kind",V::makeStr(ref.kind));if(auto radius=d.find("radius"))output.set("radius",*radius);evidence.set("name",V::makeStr(name));evidence.set("kind",V::makeStr(ref.kind));evidence.set("brep_sha256",V::makeStr(digest(encoded.str)));V indices=V::makeArr();indices.arr.push_back(V::makeNum(indexOf(shape,ref.shape,refType(ref.kind))));evidence.set("indices",indices);output.set("evidence",evidence);return output;
    }throw std::runtime_error("Unknown semantic reference: "+name);
}
}
