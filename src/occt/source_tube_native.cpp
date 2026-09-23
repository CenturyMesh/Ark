#include "ark/occt_source.hpp"
#include "common.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ark::occt {
namespace {
namespace fs=std::filesystem;
constexpr double tol=1e-6;
using X=gp_XYZ;
X xyz(const gp_Pnt& p){return p.XYZ();}
X xyz(const gp_Vec& p){return p.XYZ();}
X xyz(const gp_Dir& p){return p.XYZ();}
gp_Pnt pnt(const X& p){return gp_Pnt(p);}
gp_Vec vct(const X& p){return gp_Vec(p);}
X unit(X p){const double n=p.Modulus();if(n<tol)throw std::runtime_error("Degenerate source axis");p.Divide(n);return p;}
double dot(const X& a,const X& b){return a.Dot(b);}
X cross(const X& a,const X& b){return a.Crossed(b);}
double norm(const X& a){return a.Modulus();}
X point(const gp_Trsf& m,const X& p){return xyz(pnt(p).Transformed(m));}
X direction(const gp_Trsf& m,const X& p){return xyz(vct(p).Transformed(m));}
gp_Trsf translation(const X& p){gp_Trsf m;m.SetTranslation(vct(p));return m;}
gp_Trsf rotation(const X& axis,double angle){gp_Trsf m;m.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(axis)),angle);return m;}
double distance(const TopoDS_Shape& a,const TopoDS_Shape& b){BRepExtrema_DistShapeShape d(a,b);d.Perform();if(!d.IsDone())throw std::runtime_error("Source distance evaluation failed");return d.Value();}
V numbers(std::initializer_list<double> data){V result=V::makeArr();for(double x:data)result.arr.push_back(V::makeNum(x));return result;}
V jxyz(const X& p){return numbers({p.X(),p.Y(),p.Z()});}
std::string pathString(const fs::path& path){const auto u=path.u8string();return std::string(u.begin(),u.end());}
std::string importerDigest(){
#ifdef _WIN32
    std::array<wchar_t,32768> filename{};
    const DWORD size=GetModuleFileNameW(nullptr,filename.data(),static_cast<DWORD>(filename.size()));
    if(size==0||size>=filename.size())throw std::runtime_error("Cannot identify native importer binary");
    return sha256(readFile(fs::path(std::wstring(filename.data(),size))));
#else
    return sha256(std::string("ark-source-native-")+kernelVersion());
#endif
}
std::string text(const V& o,const char* key){auto p=o.find(key);if(!p||p->t!=V::Str)throw std::runtime_error(std::string("Invalid source package field: ")+key);return p->str;}
const V& field(const V& o,const char* key){auto p=o.find(key);if(!p)throw std::runtime_error(std::string("Missing source package field: ")+key);return *p;}
V array(const std::vector<V>& xs){V a=V::makeArr();a.arr=xs;return a;}
std::vector<TopoDS_Face> faces(const TopoDS_Shape& shape){std::vector<TopoDS_Face> result;for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())result.push_back(TopoDS::Face(e.Current()));return result;}
std::vector<TopoDS_Edge> edges(const TopoDS_Shape& shape){std::vector<TopoDS_Edge> result;for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next())result.push_back(TopoDS::Edge(e.Current()));return result;}
std::vector<TopoDS_Wire> wires(const TopoDS_Shape& shape){std::vector<TopoDS_Wire> result;for(TopExp_Explorer e(shape,TopAbs_WIRE);e.More();e.Next())result.push_back(TopoDS::Wire(e.Current()));return result;}
TopoDS_Shape sector(const X& center,const X& axis,const X& radial,const X& tangent,double angle,double inner,double outer,double width){
    const auto at=[&](double radius,double a){return pnt(center+(radial*std::cos(a)+tangent*std::sin(a))*radius);};
    BRepBuilderAPI_MakeWire w;
    w.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(at(outer,0),at(outer,angle/2),at(outer,angle)).Value()).Edge());
    w.Add(BRepBuilderAPI_MakeEdge(at(outer,angle),at(inner,angle)).Edge());
    w.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(at(inner,angle),at(inner,angle/2),at(inner,0)).Value()).Edge());
    w.Add(BRepBuilderAPI_MakeEdge(at(inner,0),at(outer,0)).Edge());
    if(!w.IsDone())throw std::runtime_error("Cannot construct hinge sector wire");
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(w.Wire()).Face(),vct(axis*width)).Shape();
}
TopoDS_Shape prism(const X& origin,const X& a,const X& b,const X& c){
    BRepBuilderAPI_MakePolygon p;
    for(const X& x:{origin,origin+a,origin+a+b,origin+b})p.Add(pnt(x));
    p.Close();if(!p.IsDone())throw std::runtime_error("Cannot construct source prism");
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(p.Wire()).Face(),vct(c)).Shape();
}
struct Cylinder {
    TopoDS_Face face;int id=0;double r=0,angle=0,width=0,area=0;
    X axis,center,radial,end;
};
struct Hinge {
    X center,axis,radial,tangent;double angle=0,width=0,inner=0,outer=0,neutral=0,expected=0;
    std::array<int,2> faceIds{},ends{};std::array<X,2> radials,tangents;
    TopoDS_Shape shape;
};
void checkHingeBoundary(const Cylinder& c){
    if(wires(c.face).size()!=1)throw std::runtime_error("Holed hinge patch requires a richer deformation map");
    for(const auto& edge:edges(c.face)){
        BRepAdaptor_Curve curve(edge);
        if(curve.GetType()==GeomAbs_Circle){
            const gp_Circ circle=curve.Circle();X offset=xyz(circle.Location())-c.center;
            const double v=dot(offset,c.axis);
            if(std::abs(circle.Radius()-c.r)>tol||norm(cross(offset,c.axis))>tol||
               std::min(std::abs(v),std::abs(v-c.width))>tol||std::abs(dot(xyz(circle.Axis().Direction()),c.axis))<1-1e-10)
                throw std::runtime_error("Non-rectangular hinge boundary is unsupported");
        }else if(curve.GetType()==GeomAbs_Line){
            const auto line=curve.Line();X offset=xyz(line.Location())-c.center;
            const X radial=offset-c.axis*dot(offset,c.axis);
            if(std::abs(dot(xyz(line.Direction()),c.axis))<1-1e-10||
               std::min(norm(radial-c.radial*c.r),norm(radial-c.end*c.r))>tol)
                throw std::runtime_error("Trimmed hinge boundary is unsupported");
        }else throw std::runtime_error("Unsupported hinge boundary curve type");
    }
}
std::pair<std::vector<Hinge>,std::vector<TopoDS_Face>> findHinges(const TopoDS_Shape& principal){
    auto all=faces(principal);
    struct Group{X axis;double area;};std::vector<Group> groups;
    for(const auto& face:all){
        BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane)continue;
        const X n=xyz(s.Plane().Axis().Direction());const double area=properties(face,'a');
        auto same=std::find_if(groups.begin(),groups.end(),[&](const Group& g){return std::abs(dot(g.axis,n))>1-1e-10;});
        if(same==groups.end())groups.push_back({n,area});else same->area+=area;
    }
    std::sort(groups.begin(),groups.end(),[](const auto& a,const auto& b){return a.area>b.area;});
    if(groups.size()<2||groups[0].area<1.5*groups[1].area)
        throw std::runtime_error("No unambiguous dominant planar-wall normal; planar tube required");
    const X normal=groups[0].axis;std::vector<Cylinder> cylinders;
    for(size_t i=0;i<all.size();++i){
        BRepAdaptor_Surface s(all[i]);if(s.GetType()!=GeomAbs_Cylinder)continue;
        const auto cy=s.Cylinder();X axis=xyz(cy.Axis().Direction());
        if(std::abs(dot(axis,normal))<1-1e-10)continue;
        double u0,u1,v0,v1;BRepTools::UVBounds(all[i],u0,u1,v0,v1);
        const double angle=u1-u0;if(!(angle>1e-5&&angle<std::acos(-1.)-1e-5))continue;
        X center=xyz(cy.Location())+axis*v0;
        X radial=unit(xyz(s.Value(u0,v0))-center),end=unit(xyz(s.Value(u1,v0))-center);
        const double components[]={axis.X(),axis.Y(),axis.Z()};int major=0;
        for(int k=1;k<3;++k)if(std::abs(components[k])>std::abs(components[major]))major=k;
        if(components[major]<0){center+=axis*(v1-v0);axis.Reverse();std::swap(radial,end);}
        cylinders.push_back({all[i],static_cast<int>(i)+1,cy.Radius(),angle,v1-v0,properties(all[i],'a'),axis,center,radial,end});
    }
    std::sort(cylinders.begin(),cylinders.end(),[](const auto& a,const auto& b){return a.r<b.r;});
    std::set<int> used;std::vector<Hinge> hinges;
    for(const Cylinder& a:cylinders){
        if(used.count(a.id))continue;
        std::vector<const Cylinder*> candidates;
        for(const Cylinder& b:cylinders){
            const double thickness=b.r-a.r;
            if(used.count(b.id)||thickness<=tol||a.width<4*thickness)continue;
            if(norm(a.center-b.center)<tol&&dot(a.axis,b.axis)>1-1e-10&&norm(a.radial-b.radial)<tol&&
               std::abs(a.width-b.width)<tol&&std::abs(a.angle-b.angle)<1e-8)candidates.push_back(&b);
        }
        if(candidates.empty())continue;
        if(candidates.size()!=1)throw std::runtime_error("Ambiguous cylindrical hinge radius pair");
        const Cylinder& b=*candidates.front();
        for(const Cylinder* c:{&a,&b}){
            checkHingeBoundary(*c);const double expected=c->r*c->width*c->angle;
            if(std::abs(c->area-expected)>std::max(1e-6,expected*1e-8))
                throw std::runtime_error("Trimmed or holed hinge patch requires a richer deformation map");
        }
        Hinge h;h.center=a.center;h.axis=a.axis;h.radial=a.radial;h.angle=a.angle;h.width=a.width;
        h.inner=a.r;h.outer=b.r;h.neutral=(a.r+b.r)/2;h.faceIds={a.id,b.id};h.tangent=cross(h.axis,h.radial);
        h.shape=sector(h.center,h.axis,h.radial,h.tangent,h.angle,h.inner,h.outer,h.width);
        h.expected=h.width*(h.outer*h.outer-h.inner*h.inner)*h.angle/2;
        hinges.push_back(h);used.insert(a.id);used.insert(b.id);
    }
    if(hinges.empty()||hinges.size()>32)throw std::runtime_error("No supported cylindrical hinge chain detected");
    return {hinges,all};
}
struct Bend {
    X start,axis,outward,widthAxis;double width=0,wall=0,inner=0,neutral=0,angle=0,length=0;
    std::array<int,2> faceIds{};
};
V bendJson(const Bend& b){
    V j=V::makeObj();j.set("start",jxyz(b.start));j.set("axis",jxyz(b.axis));
    j.set("outward",jxyz(b.outward));j.set("width_axis",jxyz(b.widthAxis));
    j.set("width",V::makeNum(b.width));j.set("wall",V::makeNum(b.wall));
    j.set("inner_radius",V::makeNum(b.inner));j.set("neutral_radius",V::makeNum(b.neutral));
    j.set("angle",V::makeNum(b.angle));j.set("length",V::makeNum(b.length));
    j.set("source_face_ids",numbers({double(b.faceIds[0]),double(b.faceIds[1])}));return j;
}
std::vector<gp_Trsf> finiteTransforms(const std::vector<Bend>& bends){
    std::vector<gp_Trsf> result(1);
    for(const Bend& b:bends){
        const double radius=b.length/b.angle;
        X end=b.start+X(radius*std::sin(b.angle),0,0)-b.outward*(radius*(1-std::cos(b.angle)));
        const X flatEnd=b.start+X(b.length,0,0);
        gp_Trsf joint=translation(end)*rotation(b.axis,b.angle)*translation(flatEnd*(-1.));
        result.push_back(result.back()*joint);
    }
    return result;
}
struct HoleProof {std::vector<V> holes;V proof;};
HoleProof openingInventory(const std::vector<TopoDS_Face>& sourceFaces,const std::vector<TopoDS_Shape>& parts,
                           const std::vector<int>& order,const std::vector<gp_Trsf>& transforms,const TopoDS_Shape& flat){
    const auto flatBox=bounds(flat);std::vector<std::vector<TopoDS_Face>> partFaces;
    for(int i:order)partFaces.push_back(faces(parts[i]));
    std::vector<V> openings;double maxInverse=0,maxBoundary=0,maxLengthError=0;
    for(size_t faceIndex=0;faceIndex<sourceFaces.size();++faceIndex){
        const TopoDS_Face& face=sourceFaces[faceIndex];BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)continue;
        const auto plane=surface.Plane();const TopoDS_Wire outer=BRepTools::OuterWire(face);
        for(const TopoDS_Wire& wire:wires(face)){
            if(wire.IsSame(outer))continue;
            std::vector<int> owners;
            for(size_t k=0;k<partFaces.size();++k)
                for(const auto& candidate:partFaces[k])if(face.IsSame(candidate)){owners.push_back(static_cast<int>(k));break;}
            const double perimeter=properties(wire,'l');
            if(owners.size()!=1){
                owners.clear();
                for(size_t k=0;k<order.size();++k)
                    if(std::abs(properties(common(wire,parts[order[k]]),'l')-perimeter)<=tol)
                        owners.push_back(static_cast<int>(k));
            }
            if(owners.size()!=1)throw std::runtime_error("Opening contour has no unique rigid source owner");
            const int owner=owners[0];const gp_Trsf& m=transforms[owner];
            const X flatNormal=direction(m,xyz(plane.Axis().Direction()));
            const X flatOrigin=point(m,xyz(plane.Location()));
            const double normal[]={flatNormal.X(),flatNormal.Y(),flatNormal.Z()};
            const double origin[]={flatOrigin.X(),flatOrigin.Y(),flatOrigin.Z()};
            int wallAxis=-1;for(int k=1;k<=2;++k)if(std::abs(normal[k])>1-1e-10){wallAxis=k;break;}
            if(wallAxis<0||std::min(std::abs(origin[wallAxis]-flatBox[wallAxis]),
                                     std::abs(origin[wallAxis]-flatBox[wallAxis+3]))>tol)continue;
            auto contourEdges=edges(wire);
            const TopoDS_Shape projected=transformed(wire,m);
            const double lengthError=std::abs(properties(projected,'l')-perimeter);
            maxLengthError=std::max(maxLengthError,lengthError);
            bool allCircles=true,allLines=true;
            for(const TopoDS_Edge& edge:contourEdges){
                BRepAdaptor_Curve c(edge);allCircles&=c.GetType()==GeomAbs_Circle;allLines&=c.GetType()==GeomAbs_Line;
                for(int sample=0;sample<9;++sample){
                    X q=xyz(c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*sample/8.));
                    maxInverse=std::max(maxInverse,norm(point(m.Inverted(),point(m,q))-q));
                }
            }
            for(const TopoDS_Edge& edge:edges(projected)){
                BRepAdaptor_Curve c(edge);
                for(int sample=0;sample<5;++sample){
                    const gp_Pnt q=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*sample/4.);
                    maxBoundary=std::max(maxBoundary,distance(BRepBuilderAPI_MakeVertex(q).Vertex(),flat));
                }
            }
            std::string kind=allCircles?"circular":allLines?"polygonal":"mixed";
            X center;double radius=0;
            if(allCircles){
                const gp_Circ first=BRepAdaptor_Curve(contourEdges.front()).Circle();
                center=xyz(first.Location());radius=first.Radius();
                for(const TopoDS_Edge& edge:contourEdges){
                    const gp_Circ c=BRepAdaptor_Curve(edge).Circle();
                    if(norm(xyz(c.Location())-center)>tol||std::abs(c.Radius()-radius)>tol){kind="mixed";break;}
                }
            }
            if(kind!="circular"){
                GProp_GProps properties;BRepGProp::LinearProperties(wire,properties);center=xyz(properties.CentreOfMass());
            }
            X sourceNormal=xyz(plane.Axis().Direction());if(face.Orientation()==TopAbs_REVERSED)sourceNormal.Reverse();
            V record=V::makeObj();record.set("id",V::makeNum(double(openings.size()+1)));
            record.set("kind",V::makeStr(kind));record.set("source_face",V::makeNum(double(faceIndex+1)));
            record.set("segment",V::makeNum(owner));record.set("source_center",jxyz(center));
            record.set("flat_center",jxyz(point(m,center)));record.set("source_normal",jxyz(sourceNormal));
            record.set("flat_normal",jxyz(direction(m,sourceNormal)));
            record.set("perimeter_mm",V::makeNum(perimeter));record.set("perimeter_error_mm",V::makeNum(lengthError));
            record.set("contour_edges",V::makeNum(double(contourEdges.size())));
            if(kind=="circular")record.set("radius",V::makeNum(radius));
            openings.push_back(record);
        }
    }
    if(maxInverse>tol||maxBoundary>tol||maxLengthError>tol)throw std::runtime_error("Opening contour projection failed");
    V proof=V::makeObj();proof.set("opening_inverse_max_mm",V::makeNum(maxInverse));
    proof.set("opening_boundary_sample_max_mm",V::makeNum(maxBoundary));
    proof.set("opening_perimeter_max_error_mm",V::makeNum(maxLengthError));
    proof.set("boundary_samples_per_edge",V::makeNum(5));proof.set("inverse_samples_per_edge",V::makeNum(9));
    proof.set("linear_tolerance_mm",V::makeNum(tol));return {openings,proof};
}
void writeOpenings(const fs::path& path,const V& holes){
    std::ofstream output(path,std::ios::binary);
    output<<std::setprecision(17);
    output<<"opening_id,kind,source_face,rigid_span,source_x_mm,source_y_mm,source_z_mm,flat_x_mm,flat_y_mm,flat_z_mm,radius_mm,perimeter_mm,contour_edges\n";
    if(holes.t!=V::Arr)throw std::runtime_error("Invalid opening inventory");
    for(const auto& h:holes.arr){
        auto source=field(h,"source_center"),flat=field(h,"flat_center");
        output<<field(h,"id").asNum()<<','<<text(h,"kind")<<','<<field(h,"source_face").asNum()<<','
              <<field(h,"segment").asNum()+1;
        for(const V& coord:source.arr)output<<','<<coord.asNum();
        for(const V& coord:flat.arr)output<<','<<coord.asNum();
        output<<',';if(auto radius=h.find("radius"))output<<radius->asNum();
        output<<','<<field(h,"perimeter_mm").asNum()<<','<<field(h,"contour_edges").asNum()<<'\n';
    }
    if(!output)throw std::runtime_error("Cannot write opening inventory");
}
} // namespace

V importSourceTubeNative(const std::string& sourcePath,const std::string& cacheDirectory){
    const fs::path sourcePathFs=fs::absolute(fs::u8path(sourcePath)),out=fs::u8path(cacheDirectory);
    fs::create_directories(out);
    const std::string sourceBytes=readFile(sourcePathFs);
    if(sourceBytes.size()>64ull*1024*1024)throw std::runtime_error("Source STEP exceeds 64 MiB import limit");
    const std::string sourceDigest=sha256(sourceBytes);
    const fs::path snapshot=out/"source-input.step";writeFile(snapshot,sourceBytes);
    const TopoDS_Shape source=readStep(snapshot);
    if(source.IsNull()||!BRepCheck_Analyzer(source).IsValid())throw std::runtime_error("Invalid source B-rep");
    auto sourceSolids=shapes(source,TopAbs_SOLID);
    if(sourceSolids.empty())throw std::runtime_error("Source STEP has no solid");
    std::sort(sourceSolids.begin(),sourceSolids.end(),[](const auto& a,const auto& b){return properties(a)>properties(b);});
    const TopoDS_Shape principal=sourceSolids.front();
    auto [hinges,sourceFaces]=findHinges(principal);
    TopoDS_Shape remainder=principal;
    for(const Hinge& h:hinges){
        const double measured=properties(common(principal,h.shape));
        if(std::abs(measured-h.expected)>std::max(1e-6,h.expected*1e-8))
            throw std::runtime_error("Hinge sector does not fully cover measured material");
        remainder=cut(remainder,h.shape);
    }
    const auto parts=shapes(remainder,TopAbs_SOLID);
    if(parts.size()!=hinges.size()+1)throw std::runtime_error("Hinge subtraction did not produce one chain of rigid spans");
    struct Link{int to,hinge,side;};std::vector<std::vector<Link>> graph(parts.size());
    for(size_t hi=0;hi<hinges.size();++hi){
        Hinge& h=hinges[hi];
        for(int side=0;side<2;++side){
            const double angle=side?h.angle:0;
            const X radial=h.radial*std::cos(angle)+h.tangent*std::sin(angle);
            const X tangent=h.radial*(-std::sin(angle))+h.tangent*std::cos(angle);
            const X origin=h.center+radial*h.inner;
            BRepBuilderAPI_MakePolygon polygon;
            for(const X& p:{origin,origin+radial*(h.outer-h.inner),
                            origin+radial*(h.outer-h.inner)+h.axis*h.width,origin+h.axis*h.width})polygon.Add(pnt(p));
            polygon.Close();if(!polygon.IsDone())throw std::runtime_error("Hinge interfaceFace construction failed");
            const TopoDS_Face interfaceFace=BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
            std::vector<int> matches;
            for(size_t k=0;k<parts.size();++k)
                if(properties(common(parts[k],interfaceFace),'a')>h.width*(h.outer-h.inner)*.999999)
                    matches.push_back(static_cast<int>(k));
            if(matches.size()!=1)throw std::runtime_error("Hinge endpoint adjacency ambiguous");
            h.ends[side]=matches[0];h.radials[side]=radial;h.tangents[side]=tangent;
        }
        graph[h.ends[0]].push_back({h.ends[1],static_cast<int>(hi),0});
        graph[h.ends[1]].push_back({h.ends[0],static_cast<int>(hi),1});
    }
    std::vector<int> endpoints;
    for(size_t i=0;i<parts.size();++i){
        if(graph[i].size()==1)endpoints.push_back(static_cast<int>(i));
        else if(graph[i].size()!=2)throw std::runtime_error("Not a simple hinge chain");
    }
    if(endpoints.size()!=2)throw std::runtime_error("Not a simple hinge chain");
    const auto boxCenter=[&](int i){auto b=bounds(parts[i]);return X((b[0]+b[3])/2,(b[1]+b[4])/2,(b[2]+b[5])/2);};
    int root=norm(boxCenter(endpoints[0]))<=norm(boxCenter(endpoints[1]))?endpoints[0]:endpoints[1];
    std::vector<int> order{root};std::vector<std::pair<int,int>> ordered;
    int previous=-1,current=root;
    for(;;){
        auto next=std::find_if(graph[current].begin(),graph[current].end(),[&](const Link& link){return link.to!=previous;});
        if(next==graph[current].end())break;
        ordered.emplace_back(next->hinge,next->side);order.push_back(next->to);
        previous=current;current=next->to;
    }
    if(order.size()!=parts.size())throw std::runtime_error("Disconnected hinge graph");
    const Hinge& first=hinges[ordered.front().first];const int firstSide=ordered.front().second;
    const int firstSign=firstSide==0?1:-1;
    const X tangent=first.tangents[firstSide]*firstSign,depth=first.radials[firstSide]*(-1.);
    const X transverse=cross(tangent,depth);
    gp_Trsf base;base.SetValues(tangent.X(),tangent.Y(),tangent.Z(),0,
                                depth.X(),depth.Y(),depth.Z(),0,
                                transverse.X(),transverse.Y(),transverse.Z(),0);
    const auto rootBox=bounds(transformed(parts[root],base));
    base=translation(X(-rootBox[0],-rootBox[1],-rootBox[2]))*base;
    std::vector<gp_Trsf> transforms{base};std::vector<TopoDS_Shape> bands;
    std::vector<Bend> bends;
    for(const auto& [hi,side]:ordered){
        const Hinge& h=hinges[hi];const int sign=side==0?1:-1;
        const X radial=h.radials[side],tangent=h.tangents[side]*sign;
        const X center=h.center+h.axis*(h.width/2);
        const X a=center+radial*h.neutral,b=center+h.radials[1-side]*h.neutral;
        const double length=h.angle*h.neutral;
        gp_Trsf local=translation(a+tangent*length)*rotation(h.axis,-sign*h.angle)*translation(b*(-1.));
        const gp_Trsf parent=transforms.back();transforms.push_back(parent*local);
        Bend bend;bend.start=point(parent,a);bend.axis=direction(parent,h.axis*sign);
        bend.outward=direction(parent,radial);bend.widthAxis=direction(parent,h.axis);
        bend.width=h.width;bend.wall=h.outer-h.inner;bend.inner=h.inner;
        bend.neutral=h.neutral;bend.angle=h.angle;bend.length=length;bend.faceIds=h.faceIds;
        const X origin=bend.start-bend.widthAxis*(bend.width/2)+bend.outward*(bend.inner-bend.neutral);
        bands.push_back(prism(origin,X(length,0,0),bend.outward*bend.wall,bend.widthAxis*bend.width));
        bends.push_back(bend);
    }
    std::vector<TopoDS_Shape> flatParts;
    for(size_t i=0;i<order.size();++i)flatParts.push_back(transformed(parts[order[i]],transforms[i]));
    const auto referenceBox=bounds(flatParts.front());double crossError=0;
    for(size_t i=1;i<flatParts.size();++i){const auto b=bounds(flatParts[i]);
        for(int k:{1,2,4,5})crossError=std::max(crossError,std::abs(b[k]-referenceBox[k]));}
    if(crossError>tol)throw std::runtime_error("Unfolded spans have different cross-section envelopes");
    struct Extra{int owner;double volume,distance;};std::vector<Extra> extraRecords;
    std::vector<TopoDS_Shape> extras;
    for(size_t i=1;i<sourceSolids.size();++i){
        std::vector<std::pair<double,int>> distances;
        for(size_t k=0;k<order.size();++k)distances.emplace_back(distance(sourceSolids[i],parts[order[k]]),static_cast<int>(k));
        std::sort(distances.begin(),distances.end());
        if(distances.front().first>tol||distances[1].first-distances[0].first<tol)
            throw std::runtime_error("Additional solid has no unique source attachment");
        const int owner=distances.front().second;
        extras.push_back(transformed(sourceSolids[i],transforms[owner]));
        extraRecords.push_back({owner,properties(sourceSolids[i]),distances.front().first});
    }
    TopoDS_Shape flat=flatParts.front();
    for(size_t i=0;i<bands.size();++i){flat=fuse(flat,bands[i]);flat=fuse(flat,flatParts[i+1]);}
    if(!extras.empty()){std::vector<TopoDS_Shape> all{flat};all.insert(all.end(),extras.begin(),extras.end());flat=compound(all);}
    const auto flatBox=bounds(flat);
    for(int k:{1,2,4,5})crossError=std::max(crossError,std::abs(flatBox[k]-referenceBox[k]));
    if(crossError>tol)throw std::runtime_error("Additional material extends beyond straight stock envelope");
    const HoleProof opening=openingInventory(sourceFaces,parts,order,transforms,flat);
    const auto poses=finiteTransforms(bends);double matrixError=0;
    for(size_t i=0;i<transforms.size();++i){
        gp_Trsf product=poses[i]*transforms[i];
        for(const X& test:{X(0,0,0),X(1000,0,0),X(0,1000,0),X(0,0,1000)})
            matrixError=std::max(matrixError,norm(point(product,test)-point(base,test)));
    }
    if(matrixError>tol)throw std::runtime_error("Finite-radius fold disagrees with source placement");
    std::vector<TopoDS_Shape> foldedParts;
    for(size_t i=0;i<flatParts.size();++i)foldedParts.push_back(transformed(flatParts[i],poses[i]));
    TopoDS_Shape folded=foldedParts.front();
    for(size_t i=0;i<bends.size();++i){
        const Bend& b=bends[i];
        const X center=b.start-b.outward*b.neutral-b.widthAxis*(b.width/2);
        const TopoDS_Shape forward=sector(center,b.widthAxis,b.outward,X(1,0,0),b.length/b.neutral,
                                          b.neutral-b.wall/2,b.neutral+b.wall/2,b.width);
        folded=fuse(folded,transformed(forward,poses[i]));folded=fuse(folded,foldedParts[i+1]);
    }
    std::vector<TopoDS_Shape> foldedAll{folded};
    for(size_t i=0;i<extras.size();++i)foldedAll.push_back(transformed(extras[i],poses[extraRecords[i].owner]));
    const TopoDS_Shape reconstructed=transformed(compound(foldedAll),base.Inverted());
    const double difference=std::abs(properties(cut(source,reconstructed)))+
                            std::abs(properties(cut(reconstructed,source)));
    const double sourceVolume=properties(source),volumeTolerance=std::max(1e-5,sourceVolume*1e-8);
    const bool flatValid=BRepCheck_Analyzer(flat).IsValid();
    const int flatSolids=static_cast<int>(shapes(flat,TopAbs_SOLID).size());
    if(difference>volumeTolerance||std::abs(properties(flat)-sourceVolume)>volumeTolerance||
       !flatValid||flatSolids!=static_cast<int>(sourceSolids.size()))
        throw std::runtime_error("Whole-source reconstruction/material conservation failed");
    const fs::path flatPath=fs::absolute(out/"straight.step");writeStep(flat,flatPath);
    const TopoDS_Shape readback=readStep(flatPath);
    const double stepDifference=std::abs(properties(cut(flat,readback)))+
                                std::abs(properties(cut(readback,flat)));
    if(!BRepCheck_Analyzer(readback).IsValid()||stepDifference>volumeTolerance)
        throw std::runtime_error("Straight STEP roundtrip failed");
    if(sha256(readFile(sourcePathFs))!=sourceDigest)throw std::runtime_error("Source changed during import; reimport stable file");
    V report=V::makeObj(),bendList=V::makeArr(),extraList=V::makeArr(),matrices=V::makeArr();
    for(const Bend& b:bends)bendList.arr.push_back(bendJson(b));
    for(const Extra& x:extraRecords){V r=V::makeObj();r.set("segment",V::makeNum(x.owner));
        r.set("volume_mm3",V::makeNum(x.volume));r.set("distance_mm",V::makeNum(x.distance));extraList.arr.push_back(r);}
    for(const gp_Trsf& m:transforms)matrices.arr.push_back(matrix(m));
    std::map<std::string,int> kinds;for(const V& h:opening.holes)++kinds[text(h,"kind")];
    V kindList=V::makeObj();for(const auto& [k,count]:kinds)kindList.set(k,V::makeNum(count));
    report.set("units",V::makeStr("mm"));report.set("display_deflection_mm",V::makeNum(.025));
    report.set("display_angular_tolerance_rad",V::makeNum(.12));report.set("display_mesh_authoritative",V::makeBool(false));
    report.set("importer_version",V::makeStr("0.7.0-native"));
    report.set("importer_sha256",V::makeStr(importerDigest()));
    report.set("ocp_version",V::makeStr("native OCCT "+kernelVersion()));
    report.set("native_kernel_version",V::makeStr(kernelVersion()));
    report.set("source_sha256",V::makeStr(sourceDigest));report.set("source_solids",V::makeNum(double(sourceSolids.size())));
    report.set("rigid_spans",V::makeNum(double(parts.size())));report.set("hinges",V::makeNum(double(hinges.size())));
    report.set("source_volume_mm3",V::makeNum(sourceVolume));report.set("flat_volume_mm3",V::makeNum(properties(flat)));
    report.set("flat_bounds_mm",numbers({flatBox[0],flatBox[1],flatBox[2],flatBox[3],flatBox[4],flatBox[5]}));
    report.set("flat_valid",V::makeBool(flatValid));report.set("flat_solids",V::makeNum(flatSolids));
    report.set("extra_solids",extraList);report.set("bends",bendList);report.set("root_from_source",matrix(base));
    report.set("source_to_flat",matrices);report.set("cross_section_envelope_error_mm",V::makeNum(crossError));
    report.set("holes",array(opening.holes));report.set("opening_count",V::makeNum(double(opening.holes.size())));
    report.set("opening_inventory_scope",V::makeStr("closed inner wires on planar outer walls; end caps and inner mouths excluded"));
    report.set("opening_kinds",kindList);report.set("projection",opening.proof);
    report.set("fold_transform_error_mm",V::makeNum(matrixError));
    report.set("refold_symmetric_difference_volume_mm3",V::makeNum(difference));
    report.set("volume_tolerance_mm3",V::makeNum(volumeTolerance));
    report.set("source_projection_verified",V::makeBool(true));
    report.set("manufacturing_authoritative",V::makeBool(false));
    report.set("neutral_axis_model",V::makeStr("mid-wall volume-preserving geometric baseline"));
    report.set("step_roundtrip_symmetric_difference_volume_mm3",V::makeNum(stepDifference));
    report.set("brep_valid",V::makeBool(true));report.set("solids",V::makeNum(flatSolids));
    writeFile(out/"report.json",report.dump());
    writeOpenings(out/"openings.csv",array(opening.holes));
    V rigid=V::makeArr();
    for(size_t i=0;i<flatParts.size();++i){
        std::vector<TopoDS_Shape> together{flatParts[i]};
        for(size_t k=0;k<extras.size();++k)if(extraRecords[k].owner==static_cast<int>(i))together.push_back(extras[k]);
        rigid.arr.push_back(meshData(compound(together)));
    }
    const std::string flatBytes=readFile(flatPath);
    V package=V::makeObj();package.set("format",V::makeStr("ark-source-tube"));package.set("version",V::makeNum(1));
    package.set("source_name",V::makeStr(pathString(sourcePathFs.stem())));
    package.set("source_path",V::makeStr(pathString(sourcePathFs)));
    package.set("source_sha256",V::makeStr(sourceDigest));package.set("flat_step_path",V::makeStr(pathString(flatPath)));
    package.set("flat_step_sha256",V::makeStr(sha256(flatBytes)));
    package.set("flat_step_base64",V::makeStr(base64Encode(flatBytes)));
    package.set("report",report);package.set("bends",bendList);package.set("holes",array(opening.holes));
    package.set("rigid",rigid);package.set("straight",meshData(flat));
    writeFile(out/"package.json",package.dump());return package;
}

V exportSourceTubeNative(const V& package,const std::string& target){
    if(text(package,"format")!="ark-source-tube"||field(package,"version").asNum()!=1)
        throw std::runtime_error("Invalid source tube package");
    const fs::path source=fs::u8path(text(package,"source_path"));
    const std::string sourceDigest=text(package,"source_sha256");
    if(fs::exists(source)&&sha256(readFile(source))!=sourceDigest)
        throw std::runtime_error("Source STEP changed since verified import");
    std::string payload;
    if(const V* encoded=package.find("flat_step_base64")){
        if(encoded->t!=V::Str)throw std::runtime_error("Invalid embedded straight STEP");
        payload=base64Decode(encoded->str);
    }else payload=readFile(fs::u8path(text(package,"flat_step_path")));
    const std::string stepDigest=text(package,"flat_step_sha256");
    if(sha256(payload)!=stepDigest)throw std::runtime_error("Stored source projection changed");
    const V& importReport=field(package,"report");
    if(importReport.t!=V::Obj||field(importReport,"source_projection_verified").t!=V::Bool||
       !field(importReport,"source_projection_verified").b||text(importReport,"source_sha256")!=sourceDigest)
        throw std::runtime_error("Package has no verified source projection");
    const fs::path destination=fs::u8path(target);fs::create_directories(destination.parent_path());
    fs::path temporary=destination;temporary.replace_extension(".pending.step");writeFile(temporary,payload);
    // Embedded bytes are rechecked through an independent OCCT STEP parse, so
    // a matching package digest alone cannot bless an invalid replacement.
    const TopoDS_Shape restored=readStep(temporary);
    if(!BRepCheck_Analyzer(restored).IsValid()||
       static_cast<int>(shapes(restored,TopAbs_SOLID).size())!=field(importReport,"flat_solids").asNum())
        throw std::runtime_error("Stored straight STEP B-rep changed");
    fs::rename(temporary,destination);
    V receipt=importReport;receipt.set("export_pose",V::makeStr("straight"));
    receipt.set("source_sha256",V::makeStr(sourceDigest));receipt.set("step_sha256",V::makeStr(stepDigest));
    receipt.set("source_projection_verified",V::makeBool(true));
    receipt.set("manufacturing_authoritative",V::makeBool(false));
    fs::path sidecar=destination;sidecar.replace_extension(".validation.json");writeFile(sidecar,receipt.dump());
    sidecar=destination;sidecar.replace_extension(".openings.csv");writeOpenings(sidecar,field(package,"holes"));
    return receipt;
}
} // namespace ark::occt
