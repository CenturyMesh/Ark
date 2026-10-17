#include "ark/occt_solid.hpp"
#include "ark/occt_modeling.hpp"
#include "../src/occt/common.hpp"
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRepOffset_Analyse.hxx>
#include <BRepOffset_Interval.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Surface.hxx>
#include <Geom_Curve.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Pln.hxx>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

using arkjson::Value;
namespace occt = ark::occt;
namespace {
Value parse(const char* json){Value v;if(!arkjson::parse(json,v))throw std::runtime_error("Invalid test JSON");return v;}
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void close(double actual,double expected,double tol,const char* message){check(std::abs(actual-expected)<=tol,message);}
Value result(const Value& request){return occt::modelSolidNative(request);}
Value frameRequest(const Value& solid,const char* name){Value q=Value::makeObj();q.set("input_brep",*solid.find("brep_base64"));q.set("topology",*solid.find("topology"));q.set("reference",Value::makeStr(name));return q;}
Value on(const char* op,const Value& base){Value q=Value::makeObj();q.set("op",Value::makeStr(op));q.set("input_brep",*base.find("brep_base64"));q.set("input_topology",*base.find("topology"));return q;}
double volume(const Value& solid){return solid.find("report")->find("volume_mm3")->num;}
void rejected(const Value& request,bool reference=false){try{if(reference)(void)occt::referenceFrameNative(request);else (void)result(request);}catch(const std::exception&){return;}throw std::runtime_error("Invalid request was accepted");}
const Value* referenceNamed(const Value& solid,const std::string& name){for(const auto& row:solid.find("topology")->find("references")->arr)if(row.find("name")->str==name)return &row;return nullptr;}

void offsetEdgeRegularity(){
    // A planar B-spline patch has the same exact geometry as a plane. Its
    // representation must not replace the operation's angular tolerance with
    // the much looser visual G1 test used by the fillet helper.
    for(const double angle:{-.3,-.05,0.,.05,.3}){
        BRepBuilderAPI_MakePolygon boundary;
        for(const auto& point:{gp_Pnt(0,0,0),gp_Pnt(10,0,0),gp_Pnt(10,10,0),gp_Pnt(0,10,0)})boundary.Add(point);
        boundary.Close();
        const auto plane=BRepBuilderAPI_MakeFace(boundary.Wire()).Face();
        NCollection_Array2<gp_Pnt> poles(1,2,1,2);
        for(int i=1;i<=2;++i)for(int j=1;j<=2;++j)
            poles(i,j)=gp_Pnt(10*(i-1),-10*(j-1)*std::cos(angle),-10*(j-1)*std::sin(angle));
        NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1;
        NCollection_Array1<int> multiplicities(1,2);multiplicities(1)=multiplicities(2)=2;
        occ::handle<Geom_BSplineSurface> support=new Geom_BSplineSurface(poles,knots,knots,multiplicities,multiplicities,1,1);
        auto spline=BRepBuilderAPI_MakeFace(support,1e-7).Face();spline.Reverse();
        BRepBuilderAPI_Sewing sewing(1e-7);sewing.Add(plane);sewing.Add(spline);sewing.Perform();
        const auto shape=sewing.SewedShape();BRepLib::EncodeRegularity(shape);
        check(BRepCheck_Analyzer(shape,true,false,true).IsValid(),"offset regularity fixture has exact curve/pcurve agreement");
        NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> ancestors;
        TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,ancestors);
        BRepOffset_Analyse analysis(shape,1e-5);
        size_t shared=0;
        for(int i=1;i<=ancestors.Extent();++i){
            const auto& faces=ancestors.FindFromIndex(i);if(faces.Extent()!=2)continue;
            ++shared;const auto edge=TopoDS::Edge(ancestors.FindKey(i));
            check((BRep_Tool::Continuity(edge,TopoDS::Face(faces.First()),TopoDS::Face(faces.Last()))>GeomAbs_C0)==(angle==0),"only the coplanar fixture carries tangent regularity");
            const auto expected=angle<0?ChFiDS_Concave:angle>0?ChFiDS_Convex:ChFiDS_Tangential;
            check(analysis.Type(edge).Extent()==1,"offset edge has one unambiguous interval");
            check(analysis.Type(edge).First().Type()==expected,"offset analysis preserves genuine shallow concave and convex C0 joins");
        }
        check(shared==1,"offset regularity fixture has one shared edge");
    }
}

void volumeAcrossPlanarSplineSpans(){
    // The polygon has area 10*5 + 8*3 = 74 mm2. Concentrating two
    // parameter spans does not change its geometry or its five-mm prism.
    const double xy[7][2]={{0,0},{10,0},{10,5},{8,5},{8,8},{0,8},{0,0}};
    const double parameters[7]={0,1e-6,2e-6,.1,.5,.9,1};
    NCollection_Array1<gp_Pnt> poles(1,7);
    NCollection_Array1<double> knots(1,7);
    NCollection_Array1<int> mults(1,7);
    for(int i=1;i<=7;++i){
        poles(i)=gp_Pnt(xy[i-1][0]+123,xy[i-1][1]-97,11);
        knots(i)=parameters[i-1];mults(i)=i==1||i==7?2:1;
    }
    occ::handle<Geom_BSplineCurve> boundary=new Geom_BSplineCurve(poles,knots,mults,1);
    const auto wire=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(boundary).Edge()).Wire();
    const auto face=BRepBuilderAPI_MakeFace(wire).Face();
    const auto solid=BRepPrimAPI_MakePrism(face,gp_Vec(0,0,5)).Shape();
    check(BRepCheck_Analyzer(solid,true,false,true).IsValid(),"piecewise spline prism is exact-valid");
    for(double eps:{1e-10,1e-13}){
        GProp_GProps props;
        const double achieved=BRepGProp::VolumePropertiesGK(solid,props,eps,false,true,false,false,false);
        check(achieved>=0&&std::isfinite(achieved),"piecewise spline volume integration completes");
        close(props.Mass(),370,1e-6,"planar spline trim spans preserve the analytic prism volume");
    }
}

void extendedTangentShellConservation(){
    // Four exact rational ellipse quadrants make independent G1 supports.
    // The nonuniform section heights force the opening to extend those
    // supports. A shared 3D edge must retain both physical pcurve images.
    Value loft=Value::makeObj(),profiles=Value::makeArr();
    loft.set("op",Value::makeStr("loft"));loft.set("solid",Value::makeBool(true));
    loft.set("compatible",Value::makeBool(true));loft.set("max_degree",Value::makeNum(3));
    const double major[4]={80,79,64,40},minor[4]={50,49,40,25},height[4]={0,30,39,40};
    const double pi=std::acos(-1.),weight=std::sqrt(.5);
    for(int i=0;i<4;++i){
        Value profile=Value::makeObj(),frame=Value::makeArr(),entities=Value::makeArr();
        for(double x:std::array<double,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,height[i],1})frame.arr.push_back(Value::makeNum(x));
        profile.set("frame",frame);
        for(int quadrant=0;quadrant<4;++quadrant){
            const double a=quadrant*pi/2,b=(quadrant+1)*pi/2,m=(a+b)/2;
            Value entity=Value::makeObj(),points=Value::makeArr(),weights=Value::makeArr();
            for(const auto& p:std::array<std::array<double,2>,3>{{{major[i]*std::cos(a),minor[i]*std::sin(a)},
                {major[i]*std::cos(m)/weight,minor[i]*std::sin(m)/weight},{major[i]*std::cos(b),minor[i]*std::sin(b)}}}){
                Value point=Value::makeArr();point.arr={Value::makeNum(p[0]),Value::makeNum(p[1])};points.arr.push_back(point);
            }
            weights.arr={Value::makeNum(1),Value::makeNum(weight),Value::makeNum(1)};
            entity.set("kind",Value::makeStr("bezier"));entity.set("name",Value::makeStr("quadrant."+std::to_string(quadrant)));
            entity.set("points",points);entity.set("weights",weights);entities.arr.push_back(entity);
        }
        profile.set("entities",entities);profiles.arr.push_back(profile);
    }
    loft.set("profiles",profiles);
    const auto base=occt::modelSurfaceNative(loft);
    for(bool moved:{false,true}){
        auto input=base;const double angle=moved?.31:0,c=std::cos(angle),s=std::sin(angle);
        const double tx=moved?123:0,ty=moved?-97:0,tz=moved?41:0;
        if(moved){
            auto transform=on("transform",base);Value frame=Value::makeArr();
            for(double x:std::array<double,16>{c,s,0,0,-s,c,0,0,0,0,1,0,tx,ty,tz,1})frame.arr.push_back(Value::makeNum(x));
            transform.set("frame",frame);input=result(transform);
        }
        auto shellRequest=on("shell",input);shellRequest.set("thickness",Value::makeNum(-2));
        shellRequest.set("faces",parse(R"(["cap.start"])"));
        const auto shell=result(shellRequest);const auto source=occt::readBrep(shell.find("brep_base64")->str);
        check(BRepCheck_Analyzer(source,true,false,true).IsValid(),"extended tangent shell is exact-valid");
        auto split=on("split_plane",shell);Value normal=Value::makeArr(),origin=Value::makeArr();
        normal.arr={Value::makeNum(c),Value::makeNum(s),Value::makeNum(0)};
        origin.arr={Value::makeNum(tx-17.25*c),Value::makeNum(ty-17.25*s),Value::makeNum(tz)};
        split.set("normal",normal);split.set("origin",origin);
        split.set("keep",Value::makeStr("positive"));const auto positive=result(split);
        split.set("keep",Value::makeStr("negative"));const auto negative=result(split);
        close(volume(positive)+volume(negative),volume(shell),volume(shell)*1e-7,"extended tangent shell conserves volume across an off-center split");
        for(const auto& half:{positive,negative})check(BRepCheck_Analyzer(occt::readBrep(half.find("brep_base64")->str),true,false,true).IsValid(),"extended tangent shell halves remain exact-valid");
    }
    // A closed support carries two pcurves for its seam. Reading the reversed
    // original pcurve must not overwrite the extended shared-edge interval.
    auto closedLoft=loft;auto closedProfiles=*loft.find("profiles");size_t section=0;
    for(auto& profile:closedProfiles.arr){
        Value circle=Value::makeObj(),entities=Value::makeArr();
        circle.set("kind",Value::makeStr("circle"));circle.set("name",Value::makeStr("closed.section"));
        circle.set("center",parse("[0,0]"));circle.set("radius",Value::makeNum(major[section++]));
        entities.arr.push_back(circle);profile.set("entities",entities);
    }
    closedLoft.set("profiles",closedProfiles);
    const auto closedBase=occt::modelSurfaceNative(closedLoft);
    const auto closedSource=occt::readBrep(closedBase.find("brep_base64")->str);bool hasSeam=false;
    for(const auto& fs:occt::shapes(closedSource,TopAbs_FACE))for(const auto& es:occt::shapes(fs,TopAbs_EDGE))
        hasSeam=hasSeam||BRep_Tool::IsClosed(TopoDS::Edge(es),TopoDS::Face(fs));
    check(hasSeam,"closed loft control contains a native seam with two pcurves");
    auto shellRequest=on("shell",closedBase);shellRequest.set("thickness",Value::makeNum(-2));shellRequest.set("faces",parse(R"(["cap.start"])"));
    const auto shell=result(shellRequest);check(BRepCheck_Analyzer(occt::readBrep(shell.find("brep_base64")->str),true,false,true).IsValid(),"closed seam shell is exact-valid");
    auto split=on("split_plane",shell);split.set("normal",parse("[1,0,0]"));split.set("origin",parse("[-17.25,0,0]"));
    split.set("keep",Value::makeStr("positive"));const auto positive=result(split);
    split.set("keep",Value::makeStr("negative"));const auto negative=result(split);
    close(volume(positive)+volume(negative),volume(shell),volume(shell)*1e-7,"closed seam shell conserves volume across an off-center split");
    for(const auto& half:{positive,negative})check(BRepCheck_Analyzer(occt::readBrep(half.find("brep_base64")->str),true,false,true).IsValid(),"closed seam shell halves remain exact-valid");
}

void currentSelectionCoverage(const Value& solid){
    const auto shape=occt::readBrep(solid.find("brep_base64")->str);
    const auto& topology=*solid.find("topology");const auto& rows=topology.find("references")->arr;
    const auto identity=occt::sha256(occt::base64Decode(solid.find("brep_base64")->str));
    check(topology.find("brep_sha256")->str==identity&&!rows.empty(),"unannotated exact result publishes digest-bound selection coverage");
    for(const auto& row:rows){
        const auto kind=row.find("kind")->str;const auto* indices=row.find("indices");
        check((kind=="face"||kind=="edge")&&row.find("status")->str=="bound"&&indices&&indices->arr.size()==1,"unannotated selection references bind one exact face or edge");
        const auto& index=indices->arr[0];check(index.t==Value::Num&&index.num>=0&&index.num==std::floor(index.num),"selection index is an exact nonnegative integer");
        const auto name="@ark."+kind+"."+identity+"."+std::to_string(static_cast<size_t>(index.num));
        check(row.find("name")->str==name&&row.find("brep_sha256")->str==identity,"unannotated source gains only current-identity handles, never guessed semantic names");
        check(row.find("reference_scope")->str=="current_brep"&&row.find("persistent")->t==Value::Bool&&!row.find("persistent")->b,"current topology handles disclose non-associative scope");
    }
    for(const auto kind:{TopAbs_FACE,TopAbs_EDGE}){
        const auto entities=occt::shapes(shape,kind);const auto& canonical=solid.find("mesh")->find(kind==TopAbs_FACE?"face_canonical":"edge_canonical")->arr;
        check(canonical.size()==entities.size(),"selection canonical map preserves original display topology indices");
        std::set<size_t> bound;for(const auto& row:rows)if(row.find("kind")->str==(kind==TopAbs_FACE?"face":"edge"))bound.insert(static_cast<size_t>(row.find("indices")->arr[0].num));
        for(size_t i=0;i<entities.size();++i){
            const auto& index=canonical[i];check(index.t==Value::Num&&index.num>=0&&index.num==std::floor(index.num)&&index.num<=i,"canonical slot names an existing first occurrence");
            const auto first=static_cast<size_t>(index.num);check(entities[i].IsSame(entities[first]),"canonical aliases retain exact entity identity");
            if(kind==TopAbs_EDGE&&BRep_Tool::Degenerated(TopoDS::Edge(entities[i])))continue;
            check(bound.count(first)!=0,"every selectable exact entity has a current bound reference");
        }
    }
}

void primitivesAndNames(){
    auto pickCylinder=result(parse(R"({"op":"cylinder","radius":500,"height":20})"));
    bool curvedEdge=false;
    for(const auto& edge:pickCylinder.find("mesh")->find("edges")->arr)if(edge.arr.size()>6){
        curvedEdge=true;check(edge.arr.size()>39,"curved picking edges are not fixed twelve-chord approximations");
        for(size_t i=3;i<edge.arr.size();i+=3){
            const double x=(edge.arr[i-3].num+edge.arr[i].num)/2,y=(edge.arr[i-2].num+edge.arr[i+1].num)/2;
            check(500-std::hypot(x,y)<0.0251,"circle picking chord follows surface tessellation deflection");
        }
    }
    check(curvedEdge,"cylinder publishes selectable curved edges");
    auto box=result(parse(R"({"op":"box","width":10,"depth":20,"height":5})"));
    close(volume(box),1000,1e-7,"box volume");
    auto top=occt::referenceFrameNative(frameRequest(box,"z_max"));
    close(top.find("frame")->arr[14].num,5,1e-8,"top face position");
    close(top.find("frame")->arr[10].num,1,1e-8,"top outward normal");
    auto changed=result(parse(R"({"op":"box","width":15,"depth":20,"height":7})"));
    auto changedTop=occt::referenceFrameNative(frameRequest(changed,"z_max"));
    close(changedTop.find("frame")->arr[14].num,7,1e-8,"reference survived parameter edit");
    close(volume(changed),2100,1e-7,"edited box volume");
    close(volume(result(parse(R"({"op":"cylinder","radius":2,"height":5})"))),20*std::acos(-1.),1e-6,"cylinder volume");
    close(volume(result(parse(R"({"op":"sphere","radius":2})"))),32*std::acos(-1.)/3,1e-6,"sphere volume");
    close(volume(result(parse(R"({"op":"cone","radius_bottom":3,"radius_top":1,"height":6})"))),26*std::acos(-1.),1e-6,"cone volume");
    close(volume(result(parse(R"({"op":"torus","major_radius":5,"minor_radius":1})"))),10*std::acos(-1.)*std::acos(-1.),1e-5,"torus volume");
}

void nativeDisplayEdges(){
    auto assertAligned=[](const Value& mesh,const TopoDS_Shape& shape){
        const auto* edges=mesh.find("edges"),*visible=mesh.find("edge_visible");
        check(edges&&visible&&visible->t==Value::Arr,"native preview has optional display-edge metadata");
        check(visible->arr.size()==edges->arr.size()&&edges->arr.size()==occt::shapes(shape,TopAbs_EDGE).size(),"display metadata preserves every original edge occurrence and index");
        for(const auto& flag:visible->arr)check(flag.t==Value::Bool,"display edge mask is typed bool");
    };
    auto box=result(parse(R"({"op":"box","width":10,"depth":20,"height":5})"));auto boxShape=occt::readBrep(box.find("brep_base64")->str);const auto& boxMesh=*box.find("mesh");assertAligned(boxMesh,boxShape);
    for(const auto& flag:boxMesh.find("edge_visible")->arr)check(flag.b,"all sharp box boundaries remain visible");
    const auto reportBefore=occt::shapeReport(boxShape,true).dump(),original=occt::brepData(boxShape);
    const auto facesBefore=occt::shapes(boxShape,TopAbs_FACE),edgesBefore=occt::shapes(boxShape,TopAbs_EDGE);
    std::vector<occ::handle<Geom_Surface>> surfaces;std::vector<occ::handle<Geom_Curve>> curves;std::vector<double> faceTolerances,edgeTolerances,firstParameters,lastParameters;
    for(const auto& item:facesBefore){const auto face=TopoDS::Face(item);surfaces.push_back(BRep_Tool::Surface(face));faceTolerances.push_back(BRep_Tool::Tolerance(face));}
    for(const auto& item:edgesBefore){const auto edge=TopoDS::Edge(item);double first,last;curves.push_back(BRep_Tool::Curve(edge,first,last));edgeTolerances.push_back(BRep_Tool::Tolerance(edge));firstParameters.push_back(first);lastParameters.push_back(last);}
    const auto verticesBefore=occt::shapes(boxShape,TopAbs_VERTEX);std::vector<gp_Pnt> points;std::vector<double> vertexTolerances;for(const auto& item:verticesBefore){const auto vertex=TopoDS::Vertex(item);points.push_back(BRep_Tool::Pnt(vertex));vertexTolerances.push_back(BRep_Tool::Tolerance(vertex));}
    (void)occt::meshData(boxShape);const auto meshed=occt::brepData(boxShape);
    if(meshed!=original){const auto artifacts=std::filesystem::temp_directory_path()/"ark-native-display-edges";occt::writeFile(artifacts/"box-before-mesh.brep",occt::base64Decode(original));occt::writeFile(artifacts/"box-after-mesh.brep",occt::base64Decode(meshed));std::cout<<"Existing mesher serialization delta retained in "<<artifacts.string()<<'\n';}
    check(occt::shapeReport(boxShape,true).dump()==reportBefore,"mesh attachment preserves exact volume, area, bounds and topology report");
    const auto facesAfter=occt::shapes(boxShape,TopAbs_FACE),edgesAfter=occt::shapes(boxShape,TopAbs_EDGE);check(facesAfter.size()==facesBefore.size()&&edgesAfter.size()==edgesBefore.size(),"meshing preserves face and edge census");
    for(size_t i=0;i<facesAfter.size();++i){const auto face=TopoDS::Face(facesAfter[i]);check(face.IsSame(facesBefore[i])&&BRep_Tool::Surface(face)==surfaces[i]&&BRep_Tool::Tolerance(face)==faceTolerances[i],"meshing preserves exact face identity, surface and tolerance");}
    for(size_t i=0;i<edgesAfter.size();++i){const auto edge=TopoDS::Edge(edgesAfter[i]);double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);check(edge.IsSame(edgesBefore[i])&&curve==curves[i]&&BRep_Tool::Tolerance(edge)==edgeTolerances[i]&&first==firstParameters[i]&&last==lastParameters[i],"meshing preserves exact edge identity, curve, range and tolerance");}
    const auto verticesAfter=occt::shapes(boxShape,TopAbs_VERTEX);check(verticesAfter.size()==verticesBefore.size(),"meshing preserves vertex census");for(size_t i=0;i<verticesAfter.size();++i){const auto vertex=TopoDS::Vertex(verticesAfter[i]);const auto point=BRep_Tool::Pnt(vertex);check(vertex.IsSame(verticesBefore[i])&&point.X()==points[i].X()&&point.Y()==points[i].Y()&&point.Z()==points[i].Z()&&BRep_Tool::Tolerance(vertex)==vertexTolerances[i],"meshing preserves exact vertex identity, coordinates and tolerance");}
    const auto classificationBaseline=occt::brepData(boxShape);const auto classification=occt::edgeVisibility(boxShape);check(occt::brepData(boxShape)==classificationBaseline,"read-only display classification leaves already-meshed B-rep byte-identical");check(classification.dump()==boxMesh.find("edge_visible")->dump(),"direct read-only classification agrees with worker mesh metadata");

    auto cylinder=result(parse(R"({"op":"cylinder","radius":5,"height":20})"));auto cylinderShape=occt::readBrep(cylinder.find("brep_base64")->str);const auto& cylinderMesh=*cylinder.find("mesh");assertAligned(cylinderMesh,cylinderShape);
    const auto cylinderEdges=occt::shapes(cylinderShape,TopAbs_EDGE);size_t seams=0,rims=0;
    for(size_t i=0;i<cylinderEdges.size();++i){BRepAdaptor_Curve curve(TopoDS::Edge(cylinderEdges[i]));bool visible=cylinderMesh.find("edge_visible")->arr[i].b;
        if(curve.GetType()==GeomAbs_Line){++seams;check(!visible,"cylinder parametric seam is suppressed for rendering");check(!cylinderMesh.find("edges")->arr[i].arr.empty(),"hidden cylinder seam remains available for picking");}
        else if(curve.GetType()==GeomAbs_Circle){++rims;check(visible,"cylinder sharp circular rims remain visible");}
        else throw std::runtime_error("unexpected cylinder edge type");
    }
    check(seams>0&&rims>0,"cylinder tests both seam suppression and rim retention");
    for(const auto& ref:cylinder.find("topology")->find("references")->arr)if(ref.find("kind")&&ref.find("kind")->str=="edge")if(const auto* indices=ref.find("indices"))for(const auto& index:indices->arr)check(index.num>=0&&index.num<cylinderEdges.size(),"semantic edge indices still bind into the complete edge list");

    auto sphere=result(parse(R"({"op":"sphere","radius":5})"));auto sphereShape=occt::readBrep(sphere.find("brep_base64")->str);const auto& sphereMesh=*sphere.find("mesh");assertAligned(sphereMesh,sphereShape);size_t sphereSeams=0;
    const auto sphereEdges=occt::shapes(sphereShape,TopAbs_EDGE);
    for(size_t i=0;i<sphereEdges.size();++i){const auto edge=TopoDS::Edge(sphereEdges[i]);check(!sphereMesh.find("edge_visible")->arr[i].b,"smooth sphere has no artificial seam or pole display edges");if(!BRep_Tool::Degenerated(edge)){++sphereSeams;check(!sphereMesh.find("edges")->arr[i].arr.empty(),"sphere seam remains pickable");}}
    check(sphereSeams>0,"sphere test includes a nondegenerate seam");

    auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,20,0,10).Face();auto surfaceMesh=occt::meshData(face);assertAligned(surfaceMesh,face);for(const auto& flag:surfaceMesh.find("edge_visible")->arr)check(flag.b,"open surface boundaries remain visible");
    auto wireEdge=BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(2,3,4)).Edge();auto curveMesh=occt::meshData(wireEdge);assertAligned(curveMesh,wireEdge);check(curveMesh.find("edge_visible")->arr[0].b,"standalone curve remains visible");

    auto roundedRequest=on("fillet",box);roundedRequest.set("radius",Value::makeNum(.5));roundedRequest.set("edges",parse(R"(["edge.x.ymin.zmin"])"));auto rounded=result(roundedRequest);auto roundedShape=occt::readBrep(rounded.find("brep_base64")->str);const auto& roundedMesh=*rounded.find("mesh");assertAligned(roundedMesh,roundedShape);
    NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> owners;TopExp::MapShapesAndAncestors(roundedShape,TopAbs_EDGE,TopAbs_FACE,owners);const auto roundedEdges=occt::shapes(roundedShape,TopAbs_EDGE);size_t smooth=0;
    for(size_t i=0;i<roundedEdges.size();++i){const auto edge=TopoDS::Edge(roundedEdges[i]);const auto& faces=owners.FindFromKey(edge);if(faces.Extent()!=2)continue;const auto first=TopoDS::Face(faces.First());const auto second=TopoDS::Face(faces.Last());if(first.IsSame(second))continue;
        if(BRep_Tool::HasContinuity(edge,first,second)&&BRep_Tool::Continuity(edge,first,second)>=GeomAbs_G1){++smooth;check(!roundedMesh.find("edge_visible")->arr[i].b,"proven tangent fillet face junction is suppressed");}
        else check(roundedMesh.find("edge_visible")->arr[i].b,"unproven or sharp fillet boundaries remain visible");
    }
    check(smooth>0,"fillet test exercises stored native tangent continuity");
}

void disconnectedDiagnostic(){
    auto base=result(parse(R"({"op":"box","width":10,"depth":10,"height":10})"));
    auto move=on("transform",base);move.set("frame",parse("[1,0,0,0,0,1,0,0,0,0,1,0,50,0,0,1]"));auto distant=result(move);
    auto request=on("union",base);request.set("tool_brep",*distant.find("brep_base64"));
    bool rejectedWithEvidence=false;
    try{(void)result(request);}catch(const std::exception& e){std::string message=e.what();rejectedWithEvidence=message.find("2 solids")!=std::string::npos&&message.find("assembly instances")!=std::string::npos;}
    check(rejectedWithEvidence,"disconnected union retains rejection with actionable solid-count evidence");
}

void operations(){
    auto base=result(parse(R"({"op":"box","width":10,"depth":10,"height":5})"));
    auto hole=on("hole",base);
    hole.set("cutter",parse(R"({"kind":"circle","radius":1,"depth":7,"frame":[1,0,0,0,0,1,0,0,0,0,1,0,5,5,-1,1]})"));
    auto drilled=result(hole);close(volume(drilled),500-5*std::acos(-1.),1e-5,"through hole exact volume");
    auto cap=occt::referenceFrameNative(frameRequest(drilled,"z_max"));
    close(cap.find("frame")->arr[14].num,5,1e-8,"cap identity through hole");
    auto slot=on("hole",base);slot.set("cutter",parse(R"({"kind":"slot","length":6,"width":2,"depth":7,"frame":[1,0,0,0,0,1,0,0,0,0,1,0,5,5,-1,1]})"));
    close(volume(result(slot)),500-5*(8+std::acos(-1.)),1e-4,"slot exact volume");
    auto fillet=on("fillet",base);fillet.set("radius",Value::makeNum(.5));fillet.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    auto rounded=result(fillet);check(volume(rounded)<500&&volume(rounded)>490,"fillet solid volume");
    auto chamfer=on("chamfer",base);chamfer.set("distance",Value::makeNum(.5));chamfer.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    auto beveled=result(chamfer);check(volume(beveled)<500&&volume(beveled)>490,"chamfer solid volume");
    auto path=std::filesystem::temp_directory_path()/"ark-occt-solid-tests.step";
    occt::writeStep(occt::readBrep(beveled.find("brep_base64")->str),path);
    auto recovered=occt::readStep(path);std::filesystem::remove(path);
    close(occt::properties(recovered),volume(beveled),1e-5,"chamfer STEP roundtrip");
    auto translated=on("transform",base);translated.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1])"));
    auto moved=result(translated);auto movedCap=occt::referenceFrameNative(frameRequest(moved,"z_max"));
    close(movedCap.find("frame")->arr[14].num,9,1e-8,"transformed semantic cap");
    auto merged=on("union",base);merged.set("tool_brep",*moved.find("brep_base64"));merged.set("tool_topology",*moved.find("topology"));
    auto fused=result(merged);check(volume(fused)>500,"native union volume");
    auto common=on("common",base);common.set("tool_brep",*moved.find("brep_base64"));common.set("tool_topology",*moved.find("topology"));
    close(volume(result(common)),8*7*1,1e-5,"native intersection volume");
    Value unannotated=Value::makeObj();unannotated.set("op",Value::makeStr("cut"));
    unannotated.set("input_brep",*base.find("brep_base64"));unannotated.set("tool_brep",*moved.find("brep_base64"));
    auto exactWithoutNames=result(unannotated);
    close(volume(exactWithoutNames),500-56,1e-5,"unannotated exact B-rep cut");
    currentSelectionCoverage(exactWithoutNames);
    const auto expectedCut=occt::cut(occt::readBrep(base.find("brep_base64")->str),occt::readBrep(moved.find("brep_base64")->str));
    close(occt::symmetricDifference(occt::readBrep(exactWithoutNames.find("brep_base64")->str),expectedCut),0,1e-8,"selection metadata preserves exact Boolean geometry");
    auto curved=result(parse(R"({"op":"extrude","depth":2,"profile":{"entities":[
      {"kind":"bezier","name":"leading","points":[[0,0],[3,-1],[7,-1],[10,0]]},
      {"kind":"line","name":"right","points":[[10,0],[10,5]]},
      {"kind":"line","name":"trailing","points":[[10,5],[0,5]]},
      {"kind":"line","name":"left","points":[[0,5],[0,0]]}
    ]}})"));
    close(volume(curved),110.2,1e-5,"Bezier profile analytic volume");
    bool hasSide=false;for(const auto& row:curved.find("topology")->find("references")->arr)
        if(row.find("name")->str=="side.leading")hasSide=row.find("status")->str=="bound"&&row.find("frame_available")&&!row.find("frame_available")->b;
    check(hasSide,"Bezier side face has exact identity and explicit frame limit");
    check(occt::referenceFrameNative(frameRequest(curved,"edge.start.leading")).find("kind")->str=="edge","sketch entity boundary curve frame");
}

void genuineMergedFaceAliases(){
    auto box=result(parse(R"({"op":"box","width":10,"depth":10,"height":5})"));
    const auto original=*referenceNamed(box,"x_min");auto topology=*box.find("topology"),rows=*topology.find("references");
    // A producer may give two authored labels to the same exact entity. This
    // fixture proves real alias identity, independently of loft edge history.
    for(const char* name:{"side.skin.0","side.skin.1"}){auto member=original;member.set("name",Value::makeStr(name));rows.arr.push_back(member);}
    auto alias=original;alias.set("name",Value::makeStr("side.skin"));alias.set("status",Value::makeStr("merged"));
    alias.set("merged_from",parse(R"(["side.skin.0","side.skin.1"])"));alias.set("surviving",Value::makeStr("side.skin.0"));rows.arr.push_back(alias);
    topology.set("references",rows);box.set("topology",topology);box=occt::completeSelectionReferences(box);
    const auto source=box.dump();const auto support=occt::referenceFrameNative(frameRequest(box,"side.skin"));
    close(support.find("frame")->arr[12].num,0,1e-8,"genuine merged alias supplies the actual planar support origin");
    close(support.find("frame")->arr[8].num,-1,1e-8,"genuine merged alias supplies the actual outward support normal");
    check(box.dump()==source,"support inspection preserves producer alias authoring");
    auto draft=on("draft",box);draft.set("angle",Value::makeNum(3));draft.set("direction",Value::makeStr("Z"));draft.set("neutral",Value::makeStr("XY"));draft.set("faces",parse(R"(["side.skin"])"));
    const auto tapered=result(draft);close(volume(tapered),500-125*std::tan(3*std::acos(-1.)/180),1e-3,"genuine merged face draft has analytic volume");
    draft.set("faces",parse(R"(["x_min"])"));const auto byOriginal=result(draft);
    close(occt::symmetricDifference(occt::readBrep(tapered.find("brep_base64")->str),occt::readBrep(byOriginal.find("brep_base64")->str)),0,1e-8,"merged alias and original name select the same exact draft face");
    const auto* propagated=referenceNamed(tapered,"side.skin");check(propagated&&propagated->find("status")->str=="merged"&&propagated->find("indices")->arr.size()==1,"real merged alias survives exact draft history");
    check(propagated->find("merged_from")->dump()==alias.find("merged_from")->dump()&&propagated->find("surviving")->str=="side.skin.0","real merged alias preserves source membership");
    for(const char* name:{"side.skin.0","side.skin.1"}){const auto* member=referenceNamed(tapered,name);check(member&&member->find("status")->str=="bound"&&member->find("indices")->dump()==propagated->find("indices")->dump(),"both authored labels still bind the same exact drafted face");}
    check(occt::referenceFrameNative(frameRequest(tapered,"side.skin")).find("kind")->str=="face","propagated merged face still supports a rigid datum");
}

void rejection(){
    auto box=result(parse(R"({"op":"box","width":10,"depth":10,"height":5})"));
    auto stale=frameRequest(box,"z_max");
    Value altered=*box.find("topology");altered.set("brep_sha256",Value::makeStr("stale"));stale.set("topology",altered);rejected(stale,true);
    auto missing=frameRequest(box,"face.3");rejected(missing,true);
    auto split=frameRequest(box,"z_max");altered=*box.find("topology");
    auto rows=*altered.find("references");for(auto& row:rows.arr)if(row.find("name")->str=="z_max"){row.set("status",Value::makeStr("split"));row.set("indices",Value::makeArr());}
    altered.set("references",rows);split.set("topology",altered);rejected(split,true);
    auto tool=result(parse(R"({"op":"box","width":2,"depth":2,"height":5})"));
    auto move=on("transform",tool);move.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,9,4,0,1])"));
    auto located=result(move);auto cut=on("cut",box);cut.set("tool_brep",*located.find("brep_base64"));cut.set("tool_topology",*located.find("topology"));
    auto sliced=result(cut);check(sliced.find("report")->find("solids")->num==1,"split-case single solid");
    auto slicedFace=frameRequest(sliced,"x_max");rejected(slicedFace,true);
    rejected(parse(R"({"op":"box","width":0,"depth":10,"height":5})"));
}
void finishingFeatures(){
    const double pi=std::acos(-1.);
    auto box=result(parse(R"({"op":"box","width":10,"depth":10,"height":5})"));
    auto fillet=on("fillet",box);fillet.set("radius",Value::makeNum(0.5));fillet.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    const double oneEdge=500-2.5*(1-pi/4);
    close(volume(result(fillet)),oneEdge,1e-4,"single fillet volume");
    auto convex=on("fillet",box);convex.set("radius",Value::makeNum(0.5));convex.set("selection",Value::makeStr("convex"));convex.set("min_angle",Value::makeNum(15));
    const double allEdges=500-4*(10+10+5)*0.25*(1-pi/4)+8*0.125*(2-7*pi/12);
    close(volume(result(convex)),allEdges,1e-3,"convex fillet volume");
    auto chamfer=on("chamfer",box);chamfer.set("distance",Value::makeNum(0.5));chamfer.set("edges",parse(R"(["edge.x.ymin.zmin"])"));
    close(volume(result(chamfer)),500-1.25,1e-4,"single chamfer volume");
    auto pattern=on("linear_pattern",box);pattern.set("count",Value::makeNum(3));pattern.set("dx",Value::makeNum(20));pattern.set("fuse",Value::makeNum(0));
    auto copies=result(pattern);
    close(volume(copies),1500,1e-4,"disjoint linear pattern volume");
    check(copies.find("report")->find("solids")->num==3,"linear pattern keeps three solids");
    auto placed=on("transform",box);placed.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,5,5,2,1])"));
    auto interior=result(placed);auto pair=on("linear_pattern",interior);pair.set("count",Value::makeNum(2));pair.set("dx",Value::makeNum(15));pair.set("fuse",Value::makeNum(0));
    auto tools=result(pair);auto cover=result(parse(R"({"op":"box","width":40,"depth":20,"height":15})"));
    auto cut=on("cut",cover);cut.set("tool_brep",*tools.find("brep_base64"));cut.set("tool_topology",*tools.find("topology"));
    close(volume(result(cut)),12000-1000,1e-3,"compound pattern cuts one solid");
    auto moved=on("transform",result(parse(R"({"op":"cylinder","radius":1,"height":2})")));
    moved.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,20,0,0,1])"));
    auto orbit=result(moved);auto circular=on("circular_pattern",orbit);circular.set("count",Value::makeNum(4));circular.set("step",Value::makeNum(90));circular.set("axis",Value::makeStr("Z"));circular.set("fuse",Value::makeNum(0));
    auto ring=result(circular);close(volume(ring),8*pi,1e-4,"circular pattern volume");check(ring.find("report")->find("solids")->num==4,"circular pattern count");
    auto mesh=box.find("mesh");check(mesh&&mesh->find("face_spans")&&mesh->find("face_spans")->arr.size()==6,"box publishes six face spans");
    auto edgePolylines=mesh->find("edges");check(edgePolylines&&!edgePolylines->arr.empty(),"box publishes edge polylines");
    int namedEdges=0;bool classified=false;
    for(const auto& row:box.find("topology")->find("references")->arr){if(row.find("kind")->str!="edge"||row.find("status")->str!="bound")continue;
        int index=(int)row.find("indices")->arr[0].num;check(index>=0&&index<(int)edgePolylines->arr.size(),"named edge index addresses a polyline");
        const auto& poly=edgePolylines->arr[(size_t)index];check(poly.arr.size()>=6&&poly.arr.size()%3==0,"edge polyline has points");
        if(row.find("name")->str=="edge.x.ymin.zmin"){classified=true;auto coord=[&](int point,int axis){return poly.arr[(size_t)(point*3+axis)].num;};int last=(int)poly.arr.size()/3-1;
            bool forward=std::abs(coord(0,0))<1e-6&&std::abs(coord(0,1))<1e-6&&std::abs(coord(0,2))<1e-6&&std::abs(coord(last,0)-10)<1e-6&&std::abs(coord(last,1))<1e-6&&std::abs(coord(last,2))<1e-6;
            bool reverse=std::abs(coord(last,0))<1e-6&&std::abs(coord(last,1))<1e-6&&std::abs(coord(last,2))<1e-6&&std::abs(coord(0,0)-10)<1e-6&&std::abs(coord(0,1))<1e-6&&std::abs(coord(0,2))<1e-6;
            check(forward||reverse,"classified edge follows the box corner");}
        ++namedEdges;}
    check(namedEdges==12&&classified,"box publishes twelve named edges");
    int triangles=0;for(const auto& span:mesh->find("face_spans")->arr)triangles+=(int)span.arr[1].num;
    check(triangles*3==(int)mesh->find("idx")->arr.size(),"face spans cover the display mesh");
    auto mirror=on("mirror",box);mirror.set("plane",Value::makeStr("YZ"));mirror.set("fuse",Value::makeNum(0));
    auto mirrored=result(mirror);close(volume(mirrored),1000,1e-4,"mirror volume");check(mirrored.find("report")->find("solids")->num==2,"mirror keeps both solids");
    auto faceMirror=on("mirror",box);faceMirror.set("face",Value::makeStr("z_max"));faceMirror.set("fuse",Value::makeNum(0));
    auto acrossFace=result(faceMirror);close(volume(acrossFace),1000,1e-4,"planar face mirror volume");
    check(acrossFace.find("report")->find("solids")->num==2,"face mirror keeps both solids");
    close(acrossFace.find("report")->find("bounds_mm")->arr[5].num,10,1e-4,"face mirror reaches the reflected top");
    auto offset=on("mirror",box);offset.set("normal",parse("[0,0,1]"));offset.set("origin",parse("[0,0,20]"));offset.set("fuse",Value::makeNum(0));
    auto raised=result(offset);close(volume(raised),1000,1e-4,"offset plane mirror volume");
    close(raised.find("report")->find("bounds_mm")->arr[5].num,40,1e-3,"offset mirror reaches z 40");
    close(raised.find("report")->find("bounds_mm")->arr[2].num,0,1e-4,"offset mirror keeps the seed");
    auto cube=result(parse(R"({"op":"box","width":10,"depth":10,"height":10})"));
    auto draft=on("draft",cube);draft.set("angle",Value::makeNum(5));draft.set("direction",Value::makeStr("Z"));draft.set("neutral",Value::makeStr("XY"));draft.set("faces",parse(R"(["x_max"])"));
    close(volume(result(draft)),1000-500*std::tan(5*pi/180),1e-3,"drafted face volume");
    auto curved=result(parse(R"({"op":"extrude","depth":2,"profile":{"entities":[
      {"kind":"bezier","name":"leading","points":[[0,0],[3,-1],[7,-1],[10,0]]},
      {"kind":"line","name":"right","points":[[10,0],[10,5]]},
      {"kind":"line","name":"trailing","points":[[10,5],[0,5]]},
      {"kind":"line","name":"left","points":[[0,5],[0,0]]}
    ]}})"));
    auto freeform=on("draft",curved);freeform.set("angle",Value::makeNum(5));freeform.set("direction",Value::makeStr("Z"));freeform.set("neutral",Value::makeStr("XY"));freeform.set("faces",parse(R"(["side.leading"])"));
    rejected(freeform);
    auto curvedMirror=on("mirror",curved);curvedMirror.set("face",Value::makeStr("side.leading"));curvedMirror.set("fuse",Value::makeNum(0));
    rejected(curvedMirror);
    auto steep=on("fillet",box);steep.set("selection",Value::makeStr("sharp"));steep.set("min_angle",Value::makeNum(179));steep.set("radius",Value::makeNum(0.2));
    rejected(steep);
}

void repeatedUnionNamespaces(){
    auto housing=result(parse(R"({"op":"cylinder","radius":11,"height":15})"));
    auto bell=result(parse(R"({"op":"cylinder","radius":13,"height":8})"));
    auto bellMove=on("transform",bell);
    bellMove.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,0,0,6,1])"));
    auto raised=result(bellMove);
    auto housingUnion=on("union",housing);housingUnion.set("tool_brep",*raised.find("brep_base64"));
    housingUnion.set("tool_topology",*raised.find("topology"));
    housingUnion.set("tool_namespace",Value::makeStr("tool.19"));
    auto assembly=result(housingUnion);
    auto shaft=result(parse(R"({"op":"cylinder","radius":2.5,"height":7})"));
    auto shaftMove=on("transform",shaft);
    shaftMove.set("frame",parse(R"([1,0,0,0,0,1,0,0,0,0,1,0,0,0,15,1])"));
    auto raisedShaft=result(shaftMove);
    auto finalUnion=on("union",assembly);finalUnion.set("tool_brep",*raisedShaft.find("brep_base64"));
    finalUnion.set("tool_topology",*raisedShaft.find("topology"));
    finalUnion.set("tool_namespace",Value::makeStr("tool.22"));
    auto motor=result(finalUnion);
    bool baseSplit=false,bellAxis=false,shaftAxis=false;
    for(const auto& row:motor.find("topology")->find("references")->arr){
        const auto& name=row.find("name")->str;
        if(name=="axis")baseSplit=row.find("status")->str=="split";
        if(name=="tool.19.axis")bellAxis=row.find("status")->str=="bound";
        if(name=="tool.22.axis")shaftAxis=row.find("status")->str=="bound";
    }
    check(baseSplit&&bellAxis&&shaftAxis,"stable multi-union semantic namespaces");
    auto frame=occt::referenceFrameNative(frameRequest(motor,"tool.22.axis"));
    close(frame.find("frame")->arr[10].num,1,1e-8,"shaft axis frame");
    auto noNamespace=on("union",assembly);noNamespace.set("tool_brep",*raisedShaft.find("brep_base64"));
    noNamespace.set("tool_topology",*raisedShaft.find("topology"));
    auto defaulted=result(noNamespace);bool ordinal=false;
    for(const auto& row:defaulted.find("topology")->find("references")->arr)
        if(row.find("name")->str=="tool2.axis")ordinal=true;
    check(ordinal,"direct worker union namespace determinism");
}
void planeSplitAndMirrorCopy(){
    auto box=result(parse(R"({"op":"box","width":10,"depth":10,"height":10})"));
    auto split=on("split_plane",box);
    split.set("normal",parse("[0,0,7]"));
    split.set("origin",parse("[0,0,4]"));
    split.set("keep",Value::makeStr("positive"));
    auto upper=result(split);
    close(volume(upper),600,1e-5,"positive plane half volume");
    close(upper.find("report")->find("bounds_mm")->arr[2].num,4,1e-6,"positive plane lower bound");
    close(occt::referenceFrameNative(frameRequest(upper,"z_max")).find("frame")->arr[14].num,10,1e-6,"retained top semantic face");
    split.set("keep",Value::makeStr("negative"));
    auto lower=result(split);
    close(volume(lower),400,1e-5,"negative plane half volume");
    close(lower.find("report")->find("bounds_mm")->arr[5].num,4,1e-6,"negative plane upper bound");
    split.set("keep",Value::makeStr("both"));
    auto pair=result(split);
    close(volume(pair),1000,1e-4,"both plane halves conserve exact volume");
    check(pair.find("report")->find("solids")->num==2,"both halves remain separate solids");
    rejected(frameRequest(pair,"x_min"),true);
    split.set("keep",Value::makeStr("invalid"));rejected(split);
    split.set("keep",Value::makeStr("positive"));split.set("origin",parse("[0,0,20]"));rejected(split);
    split.set("origin",parse("[0,0,0]"));rejected(split);
    split.set("origin",parse("[0,0,4]"));split.set("plane",Value::makeStr("XY"));rejected(split);
    auto mirror=on("mirror",box);
    mirror.set("normal",parse("[0,0,1]"));mirror.set("origin",parse("[0,0,20]"));
    mirror.set("copy_only",Value::makeNum(1));
    auto copy=result(mirror);
    close(volume(copy),1000,1e-5,"mirror copy volume");
    check(copy.find("report")->find("solids")->num==1,"mirror copy omits source solid");
    close(copy.find("report")->find("bounds_mm")->arr[2].num,30,1e-6,"mirror copy lower bound");
    close(copy.find("report")->find("bounds_mm")->arr[5].num,40,1e-6,"mirror copy upper bound");
    close(occt::referenceFrameNative(frameRequest(copy,"z_max")).find("frame")->arr[14].num,30,1e-6,"mirror copy preserves authored face identity");
    mirror.set("fuse",Value::makeNum(1));rejected(mirror);
}
}
int main(){try{offsetEdgeRegularity();volumeAcrossPlanarSplineSpans();extendedTangentShellConservation();primitivesAndNames();nativeDisplayEdges();disconnectedDiagnostic();operations();genuineMergedFaceAliases();rejection();finishingFeatures();repeatedUnionNamespaces();planeSplitAndMirrorCopy();std::cout<<"native solid tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
