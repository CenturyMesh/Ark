#include "ark/occt_sheet.hpp"
#include "common.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace ark::occt {
namespace {
using X=gp_XYZ;constexpr double tol=1e-6;constexpr double pi=3.14159265358979323846;
void require(bool condition,const std::string& error){if(!condition)throw std::runtime_error(error);}
const V& field(const V& j,const char* key){auto p=j.find(key);require(p!=nullptr,std::string("Missing sheet field: ")+key);return *p;}
double number(const V& j,const char* key){auto& p=field(j,key);require(p.t==V::Num&&std::isfinite(p.num),std::string("Invalid sheet number: ")+key);return p.num;}
std::string text(const V& j,const char* key){auto& p=field(j,key);require(p.t==V::Str&&!p.str.empty(),std::string("Invalid sheet string: ")+key);return p.str;}
std::string utf8(const fs::path& path){auto s=path.u8string();return std::string(s.begin(),s.end());}
X unit(X p){require(p.Modulus()>1e-12,"Degenerate sheet frame");p.Normalize();return p;}
X point(const gp_Trsf& t,const X& p){return gp_Pnt(p).Transformed(t).XYZ();}
X direction(const gp_Trsf& t,const X& p){return gp_Vec(p).Transformed(t).XYZ();}
gp_Trsf translation(const X& p){gp_Trsf t;t.SetTranslation(gp_Vec(p));return t;}
gp_Trsf rotation(const X& axis,double angle){gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(axis)),angle);return t;}
gp_Trsf frame(const X& x,const X& y){auto z=x.Crossed(y);gp_Trsf m;m.SetValues(x.X(),x.Y(),x.Z(),0,y.X(),y.Y(),y.Z(),0,z.X(),z.Y(),z.Z(),0);return m;}
double distance(const TopoDS_Shape& a,const TopoDS_Shape& b){BRepExtrema_DistShapeShape d(a,b);d.Perform();require(d.IsDone(),"Sheet boundary distance failed");return d.Value();}
TopoDS_Face quad(const X& origin,const X& a,const X& b){BRepBuilderAPI_MakePolygon polygon;for(auto p:{origin,origin+a,origin+a+b,origin+b})polygon.Add(gp_Pnt(p));polygon.Close();require(polygon.IsDone(),"Sheet rectangle is invalid");return BRepBuilderAPI_MakeFace(polygon.Wire()).Face();}
TopoDS_Shape prism(const X& o,const X& a,const X& b,const X& c){return BRepPrimAPI_MakePrism(quad(o,a,b),gp_Vec(c)).Shape();}
TopoDS_Shape sector(const X& center,const X& axis,const X& radial,const X& tangent,double angle,double inner,double outer,double width){
    auto at=[&](double r,double a){return gp_Pnt(center+(radial*std::cos(a)+tangent*std::sin(a))*r);};BRepBuilderAPI_MakeWire w;
    w.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(at(outer,0),at(outer,angle/2),at(outer,angle)).Value()).Edge());w.Add(BRepBuilderAPI_MakeEdge(at(outer,angle),at(inner,angle)).Edge());
    w.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(at(inner,angle),at(inner,angle/2),at(inner,0)).Value()).Edge());w.Add(BRepBuilderAPI_MakeEdge(at(inner,0),at(outer,0)).Edge());
    require(w.IsDone(),"Cannot construct source-owned sheet bend");return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(w.Wire()).Face(),gp_Vec(axis*width)).Shape();
}
struct Skin{TopoDS_Face face;int id=0;X axis,center,radial,end;double radius=0,angle=0,width=0;};
struct Bend{Skin inner,outer;TopoDS_Shape material;};
void rectangularSkin(const Skin& c){
    require(shapes(c.face,TopAbs_WIRE).size()==1,"Unsupported sheet bend: opening crosses deforming skin");
    for(auto& e:shapes(c.face,TopAbs_EDGE)){BRepAdaptor_Curve curve(TopoDS::Edge(e));
        if(curve.GetType()==GeomAbs_Circle){auto circle=curve.Circle();auto delta=circle.Location().XYZ()-c.center;double v=delta.Dot(c.axis);
            require(std::abs(circle.Radius()-c.radius)<=tol&&delta.Crossed(c.axis).Modulus()<=tol&&std::min(std::abs(v),std::abs(v-c.width))<=tol,"Unsupported trimmed sheet bend arc");
        }else if(curve.GetType()==GeomAbs_Line){auto line=curve.Line();auto delta=line.Location().XYZ()-c.center;auto radial=delta-c.axis*delta.Dot(c.axis);
            require(std::abs(line.Direction().XYZ().Dot(c.axis))>1-1e-10&&std::min((radial-c.radial*c.radius).Modulus(),(radial-c.end*c.radius).Modulus())<=tol,"Unsupported trimmed sheet bend edge");
        }else throw std::runtime_error("Unsupported bend boundary curve");
    }
    double expected=c.radius*c.width*c.angle;require(std::abs(properties(c.face,'a')-expected)<=std::max(1e-6,expected*1e-8),"Incomplete cylindrical sheet skin");
}
std::vector<Bend> recognizeBends(const TopoDS_Shape& source){
    auto all=shapes(source,TopAbs_FACE);std::vector<Skin> skins;
    for(size_t i=0;i<all.size();++i){auto face=TopoDS::Face(all[i]);BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Cylinder)continue;
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);double angle=u1-u0;if(angle<1e-5||angle>=pi-1e-5)continue;
        auto cy=s.Cylinder();Skin c;c.face=face;c.id=(int)i+1;c.axis=cy.Axis().Direction().XYZ();c.center=cy.Location().XYZ()+c.axis*v0;c.radius=cy.Radius();c.width=v1-v0;c.angle=angle;
        c.radial=unit(s.Value(u0,v0).XYZ()-c.center);c.end=unit(s.Value(u1,v0).XYZ()-c.center);
        int major=1;for(int axis=2;axis<=3;++axis)if(std::abs(c.axis.Coord(axis))>std::abs(c.axis.Coord(major)))major=axis;
        if(c.axis.Coord(major)<0){c.center+=c.axis*c.width;c.axis.Reverse();std::swap(c.radial,c.end);}skins.push_back(c);
    }
    std::vector<Bend> bends;
    for(auto& a:skins)for(auto& b:skins){double t=b.radius-a.radius;
        if(t<=tol||a.width<4*t||(a.center-b.center).Modulus()>tol||a.axis.Dot(b.axis)<1-1e-10||(a.radial-b.radial).Modulus()>tol||(a.end-b.end).Modulus()>tol||std::abs(a.angle-b.angle)>1e-8||std::abs(a.width-b.width)>tol)continue;
        rectangularSkin(a);rectangularSkin(b);
        // A bend is a pair of opposed material skins, not merely two cylinders.
        auto outwardSign=[](const Skin& c){BRepAdaptor_Surface s(c.face);double u0,u1,v0,v1;BRepTools::UVBounds(c.face,u0,u1,v0,v1);gp_Pnt p;gp_Vec du,dv;s.D1((u0+u1)/2,(v0+v1)/2,p,du,dv);auto n=du.Crossed(dv).XYZ();if(c.face.Orientation()==TopAbs_REVERSED)n.Reverse();auto delta=p.XYZ()-c.center;auto r=delta-c.axis*delta.Dot(c.axis);return unit(n).Dot(unit(r));};
        require(outwardSign(a)<-1+1e-8&&outwardSign(b)>1-1e-8,"Ambiguous sheet material skin orientation");
        bends.push_back({a,b,sector(a.center,a.axis,a.radial,a.axis.Crossed(a.radial),a.angle,a.radius,b.radius,a.width)});
    }
    require(bends.size()<=1,"Unsupported sheet domain: more than one cylindrical bend");return bends;
}
double uniformPlateProof(const TopoDS_Shape& flat,double thickness){
    auto box=bounds(flat);require(std::abs((box[4]-box[1])-thickness)<=tol,"Sheet flange does not have the paired bend thickness");std::vector<TopoDS_Shape> prisms;
    for(auto& item:shapes(flat,TopAbs_FACE)){auto face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);if(surface.GetType()!=GeomAbs_Plane)continue;auto plane=surface.Plane();
        if(std::abs(plane.Axis().Direction().Y())>1-1e-10&&std::abs(plane.Location().Y()-box[1])<=tol)prisms.push_back(BRepPrimAPI_MakePrism(face,gp_Vec(0,thickness,0)).Shape());
    }
    require(!prisms.empty(),"Sheet flange has no complete planar material skin");auto rebuilt=prisms.front();for(size_t i=1;i<prisms.size();++i)rebuilt=fuse(rebuilt,prisms[i]);
    double error=symmetricDifference(flat,rebuilt);require(error<=std::max(1e-6,properties(flat)*1e-8),"Unsupported nonuniform flange: pocket, countersink or foreign material");return error;
}
struct Development{TopoDS_Shape source,flat;std::vector<TopoDS_Shape> parts;std::vector<gp_Trsf> mappings;gp_Trsf base;double thickness=0,k=0,bendCount=0,inner=0,angle=0,width=0,allowance=0,refoldError=0,plateError=0;};
Development unfold(const TopoDS_Shape& source,double k,int rootPanel){
    Development d;d.source=source;d.k=k;auto bends=recognizeBends(source);
    if(bends.empty()){
        double bestArea=0;bool found=false;
        for(auto& item:shapes(source,TopAbs_FACE)){auto face=TopoDS::Face(item);BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane)continue;double area=properties(face,'a');if(area<=bestArea)continue;
            X inward=s.Plane().Axis().Direction().XYZ();if(face.Orientation()!=TopAbs_REVERSED)inward.Reverse();double thickness=properties(source)/area;
            auto local=frame(s.Plane().XAxis().Direction().XYZ(),inward);auto box=bounds(transformed(source,local));if(thickness<=tol||thickness>std::min(box[3]-box[0],box[5]-box[2])*.25)continue;
            local=translation(X(-box[0],-box[1],-box[2]))*local;auto flat=transformed(source,local);
            try{d.plateError=uniformPlateProof(flat,thickness);}catch(const std::exception&){continue;}
            found=true;bestArea=area;d.thickness=thickness;d.base=local;d.flat=flat;
        }
        require(found,"Unsupported source: no uniform flat sheet or complete single bend");d.parts={source};d.mappings={d.base};d.refoldError=symmetricDifference(source,transformed(d.flat,d.base.Inverted()));return d;
    }
    auto& bend=bends.front();auto& skin=bend.inner;double t=bend.outer.radius-skin.radius;double width=skin.width,theta=skin.angle,rn=skin.radius+k*t;
    double expected=width*t*theta*(skin.radius+t/2),vTol=std::max(1e-6,expected*1e-8);
    require(std::abs(properties(common(source,bend.material))-expected)<=vTol,"Source bend does not contain its complete paired-skin material");
    auto parts=shapes(cut(source,bend.material),TopAbs_SOLID);require(parts.size()==2,"Unsupported sheet graph: expected exactly two rigid flanges");
    std::array<X,2> radials,tangents;std::array<int,2> owners{};X tangent=skin.axis.Crossed(skin.radial);
    for(int side=0;side<2;++side){double a=side?theta:0;radials[side]=skin.radial*std::cos(a)+tangent*std::sin(a);tangents[side]=skin.radial*(-std::sin(a))+tangent*std::cos(a);
        auto contact=quad(skin.center+radials[side]*skin.radius,radials[side]*t,skin.axis*width);std::vector<int> matches;
        for(int i=0;i<2;++i)if(std::abs(properties(common(parts[i],contact),'a')-width*t)<=std::max(1e-6,width*t*1e-8))matches.push_back(i);
        require(matches.size()==1,"Ambiguous or incomplete bend-to-flange contact");owners[side]=matches.front();
    }
    require(owners[0]!=owners[1],"Cyclic/ambiguous sheet bend attachment");int root=rootPanel;
    if(root<0){double va=properties(parts[0]),vb=properties(parts[1]);require(std::abs(va-vb)>std::max(1e-6,std::max(va,vb)*1e-8),"Ambiguous fixed flange: specify root_panel 0 or 1");root=va>vb?0:1;}
    require(root==0||root==1,"root_panel must be 0 or 1");int side=owners[0]==root?0:1,sign=side==0?1:-1,child=1-root;
    X tx=tangents[side]*sign,inward=radials[side]*(-1);auto base=frame(tx,inward);auto bb=bounds(transformed(parts[root],base));base=translation(X(-bb[0],-bb[1],-bb[2]))*base;
    X center=skin.center+skin.axis*(width/2),a=center+radials[side]*rn,b=center+radials[1-side]*rn;double allowance=theta*rn;
    auto childMapping=base*translation(a+tx*allowance)*rotation(skin.axis,-sign*theta)*translation(b*(-1.));
    auto p0=transformed(parts[root],base),p1=transformed(parts[child],childMapping);double plateError=std::max(uniformPlateProof(p0,t),uniformPlateProof(p1,t));
    X start=point(base,a),outward=direction(base,radials[side]),axial=direction(base,skin.axis),foldAxis=axial*sign;
    auto band=prism(start-axial*(width/2)+outward*(skin.radius-rn),X(allowance,0,0),outward*t,axial*width);
    auto b0=bounds(p0),b1=bounds(p1);for(int i:{1,2,4,5})require(std::abs(b0[i]-b1[i])<=tol,"Unsupported flange width or offset across bend");
    double materialTolerance=std::max(1e-6,properties(source)*1e-8);require(properties(common(p0,p1))<=materialTolerance&&properties(common(p0,band))<=materialTolerance&&properties(common(p1,band))<=materialTolerance,"Developed sheet material overlaps");
    auto flat=fuse(fuse(p0,band),p1);require(BRepCheck_Analyzer(flat).IsValid()&&shapes(flat,TopAbs_SOLID).size()==1,"Unfolded sheet is not one valid connected solid");
    // Recover the developed bend length from the constructed material volume,
    // then independently refold both the rigid child and a new annular band.
    double measuredLength=properties(band)/(width*t),measuredAngle=measuredLength/rn;
    X end=start+X(rn*std::sin(measuredAngle),0,0)-outward*(rn*(1-std::cos(measuredAngle)));
    auto fold=translation(end)*rotation(foldAxis,measuredAngle)*translation((start+X(measuredLength,0,0))*(-1.));
    auto curved=sector(start-outward*rn-axial*(width/2),axial,outward,X(1,0,0),measuredAngle,skin.radius,bend.outer.radius,width);
    auto reconstructed=transformed(fuse(fuse(p0,curved),transformed(p1,fold)),base.Inverted());double difference=symmetricDifference(source,reconstructed);
    require(difference<=materialTolerance,"Sheet refold changed original source material");
    double expectedDelta=theta*width*t*t*(.5-k);require(std::abs((properties(source)-properties(flat))-expectedDelta)<=materialTolerance,"Sheet volume does not match the declared neutral-factor model");
    d.flat=flat;d.parts={parts[root],parts[child]};d.mappings={base,childMapping};d.base=base;d.thickness=t;d.inner=skin.radius;d.angle=theta;d.width=width;d.allowance=allowance;d.bendCount=1;d.refoldError=difference;d.plateError=plateError;return d;
}
V openingInventory(const Development& d,V& proof){
    V holes=V::makeArr();double inverseError=0,boundaryError=0,lengthError=0;auto allFaces=shapes(d.source,TopAbs_FACE);auto box=bounds(d.flat);
    for(size_t fi=0;fi<allFaces.size();++fi){auto face=TopoDS::Face(allFaces[fi]);BRepAdaptor_Surface surface(face);if(surface.GetType()!=GeomAbs_Plane)continue;auto outer=BRepTools::OuterWire(face);
        for(auto& wire:shapes(face,TopAbs_WIRE)){if(wire.IsSame(outer))continue;double perimeter=properties(wire,'l');std::vector<size_t> owners;
            for(size_t i=0;i<d.parts.size();++i)if(std::abs(properties(common(wire,d.parts[i]),'l')-perimeter)<=tol)owners.push_back(i);
            require(owners.size()==1,"Sheet opening has no unique rigid-panel owner");size_t owner=owners.front();auto m=d.mappings[owner];auto plane=surface.Plane();X normal=direction(m,plane.Axis().Direction().XYZ()),origin=point(m,plane.Location().XYZ());
            if(std::abs(normal.Y())<1-1e-10||std::abs(origin.Y()-box[1])>tol)continue; // one mouth per through-opening
            auto mapped=transformed(wire,m);lengthError=std::max(lengthError,std::abs(properties(mapped,'l')-perimeter));GProp_GProps g;BRepGProp::LinearProperties(wire,g);X center=g.CentreOfMass().XYZ();bool circular=true;double radius=0;bool first=true;
            for(auto& edge:shapes(wire,TopAbs_EDGE)){BRepAdaptor_Curve c(TopoDS::Edge(edge));if(c.GetType()==GeomAbs_Circle){auto circle=c.Circle();if(first){center=circle.Location().XYZ();radius=circle.Radius();}else if((center-circle.Location().XYZ()).Modulus()>tol||std::abs(radius-circle.Radius())>tol)circular=false;}else circular=false;first=false;
                for(int i=0;i<=8;++i){auto p=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*i/8.).XYZ();inverseError=std::max(inverseError,(point(m.Inverted(),point(m,p))-p).Modulus());}
            }
            for(auto& edge:shapes(mapped,TopAbs_EDGE)){BRepAdaptor_Curve c(TopoDS::Edge(edge));for(int i=0;i<=4;++i){auto p=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*i/4.);boundaryError=std::max(boundaryError,distance(BRepBuilderAPI_MakeVertex(p).Vertex(),d.flat));}}
            if(!circular)center=g.CentreOfMass().XYZ();V h=V::makeObj();h.set("id",V::makeNum(holes.arr.size()+1));h.set("source_face",V::makeNum(fi+1));h.set("panel",V::makeNum(owner));h.set("kind",V::makeStr(circular?"circular":"contour"));h.set("source_center",vec(center));h.set("flat_center",vec(point(m,center)));h.set("perimeter_mm",V::makeNum(perimeter));if(circular)h.set("radius",V::makeNum(radius));holes.arr.push_back(h);
        }
    }
    require(inverseError<=tol&&boundaryError<=tol&&lengthError<=tol,"Sheet opening projection validation failed");proof=V::makeObj();proof.set("inverse_max_mm",V::makeNum(inverseError));proof.set("boundary_sample_max_mm",V::makeNum(boundaryError));proof.set("perimeter_max_error_mm",V::makeNum(lengthError));proof.set("linear_tolerance_mm",V::makeNum(tol));return holes;
}
struct Snapshot{fs::path directory,path;Snapshot(const std::string& bytes){for(int attempt=0;attempt<128;++attempt){directory=fs::temp_directory_path()/("ark-sheet-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(attempt));if(fs::create_directory(directory)){path=directory/"source.step";writeFile(path,bytes);return;}}throw std::runtime_error("Cannot snapshot sheet source");}~Snapshot(){std::error_code ec;fs::remove(path,ec);fs::remove(directory,ec);}};
}

V importSheetNative(const V& request){
    double k=number(request,"k_factor");require(k>=0&&k<=1,"Declared K factor must be in [0,1]");auto path=fs::absolute(fs::u8path(text(request,"source_path")));auto bytes=readFile(path);require(bytes.size()<=64*1024*1024,"Sheet STEP exceeds 64 MiB");auto digest=sha256(bytes);Snapshot snapshot(bytes);auto imported=readStep(snapshot.path);auto solids=shapes(imported,TopAbs_SOLID);require(!solids.empty(),"STEP contains no solid sheet candidate");
    int body=0;if(auto p=request.find("body_index")){require(p->t==V::Num&&p->num>=0&&p->num<(double)solids.size()&&p->num==std::floor(p->num),"Invalid explicit sheet body_index");body=(int)p->num;}else require(solids.size()==1,"Multiple STEP bodies: select body_index explicitly");
    int root=-1;if(auto p=request.find("root_panel")){require(p->t==V::Num&&(p->num==0||p->num==1),"root_panel must be 0 or 1");root=(int)p->num;}
    auto source=solids[(size_t)body];require(properties(source)>0,"Selected sheet body has nonpositive volume");auto developed=unfold(source,k,root);double volumeTolerance=std::max(1e-6,properties(source)*1e-8);require(developed.refoldError<=volumeTolerance,"Source refold verification failed");
    V proof;auto holes=openingInventory(developed,proof);auto result=shapeResult(source,true);auto report=*result.find("report");auto flatBox=bounds(developed.flat);
    report.set("source_projection_verified",V::makeBool(true));report.set("source_sha256",V::makeStr(digest));report.set("source_bodies",V::makeNum(solids.size()));report.set("body_index",V::makeNum(body));report.set("units",V::makeStr("mm"));
    report.set("thickness_mm",V::makeNum(developed.thickness));report.set("k_factor",V::makeNum(k));report.set("bend_count",V::makeNum(developed.bendCount));report.set("allowance_model",V::makeStr("declared_k_factor_constant_thickness_geometry"));report.set("flat_length_mm",V::makeNum(flatBox[3]-flatBox[0]));report.set("formed_volume_mm3",V::makeNum(properties(source)));report.set("flat_volume_mm3",V::makeNum(properties(developed.flat)));
    report.set("refold_symmetric_difference_volume_mm3",V::makeNum(developed.refoldError));report.set("uniform_panel_proof_max_mm3",V::makeNum(developed.plateError));report.set("volume_tolerance_mm3",V::makeNum(volumeTolerance));report.set("opening_count",V::makeNum(holes.arr.size()));report.set("opening_inventory_scope",V::makeStr("one lower-skin mouth per rigid through-opening"));report.set("projection",proof);
    report.set("geometric_material_conservation",V::makeBool(developed.bendCount==0||k==.5));report.set("expected_volume_delta_mm3",V::makeNum(developed.angle*developed.width*developed.thickness*developed.thickness*(.5-k)));
    if(developed.bendCount){report.set("inner_radius_mm",V::makeNum(developed.inner));report.set("angle_deg",V::makeNum(developed.angle*180/pi));report.set("bend_allowance_mm",V::makeNum(developed.allowance));}
    V mappings=V::makeArr();for(auto& transform:developed.mappings)mappings.arr.push_back(matrix(transform));report.set("source_to_flat",mappings);report.set("root_from_source",matrix(developed.base));
    result.set("format",V::makeStr("ark-source-sheet"));result.set("version",V::makeNum(1));result.set("source_path",V::makeStr(utf8(path)));result.set("source_name",V::makeStr(utf8(path.stem())));result.set("source_sha256",V::makeStr(digest));result.set("report",report);result.set("holes",holes);result.set("flat_mesh",meshData(developed.flat));result.set("flat_brep_base64",V::makeStr(brepData(developed.flat)));
    result.set("formed_brep_sha256",V::makeStr(sha256(base64Decode(result.find("brep_base64")->str))));result.set("flat_brep_sha256",V::makeStr(sha256(base64Decode(result.find("flat_brep_base64")->str))));require(sha256(readFile(path))==digest,"Sheet source changed during import");return result;
}
V exportSheetNative(const V& request){
    const auto& package=field(request,"package");require(text(package,"format")=="ark-source-sheet"&&number(package,"version")==1,"Invalid source sheet package");const auto& report=field(package,"report");require(field(report,"source_projection_verified").t==V::Bool&&field(report,"source_projection_verified").b,"Sheet source proof is missing");
    auto source=fs::u8path(text(package,"source_path"));if(fs::exists(source))require(sha256(readFile(source))==text(package,"source_sha256"),"Source sheet STEP changed; reimport before export");
    std::string pose=request.find("pose")?request.find("pose")->asStr():"flat";require(pose=="flat"||pose=="formed","Sheet export pose must be flat or formed");auto payload=text(package,pose=="flat"?"flat_brep_base64":"brep_base64");require(sha256(base64Decode(payload))==text(package,pose=="flat"?"flat_brep_sha256":"formed_brep_sha256"),"Stored sheet projection changed");auto shape=readBrep(payload);auto destination=fs::absolute(fs::u8path(text(request,"path"))),pending=destination;auto extension=destination.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return(char)std::tolower(c);});require(extension==".step"||extension==".stp","Sheet export requires .step or .stp");pending+=".pending.step";writeStep(shape,pending);auto returned=readStep(pending);double error=symmetricDifference(shape,returned);require(error<=number(report,"volume_tolerance_mm3"),"Sheet STEP roundtrip changed material");
    auto targetBytes=readFile(pending);replaceFile(pending,destination);V receipt=report;receipt.set("export_pose",V::makeStr(pose));receipt.set("step_sha256",V::makeStr(sha256(targetBytes)));receipt.set("step_roundtrip_symmetric_difference_volume_mm3",V::makeNum(error));receipt.set("path",V::makeStr(utf8(destination)));auto sidecar=destination;sidecar.replace_extension(".validation.json");writeFile(sidecar,receipt.dump());return receipt;
}
}
