#include "surface_repair.hpp"
#include "common.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <ShapeFix_Shell.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ark::occt {
namespace {
[[noreturn]] void fail(const char* code,const std::string& message){throw std::runtime_error(std::string(code)+": "+message);}
const V& need(const V& value,const char* key){const auto* field=value.find(key);if(!field)fail("repair_invalid_request",std::string("Missing ")+key);return *field;}
using ShapeMap=NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher>;
ShapeMap unique(const TopoDS_Shape& shape,TopAbs_ShapeEnum kind){ShapeMap map;TopExp::MapShapes(shape,kind,map);return map;}
struct Inventory {V report;std::vector<int> incidence;int components=0;};
Inventory inspect(const TopoDS_Shape& shape){
    Inventory result;result.report=V::makeObj();
    for(const auto& entry:std::initializer_list<std::pair<const char*,TopAbs_ShapeEnum>>{{"solids",TopAbs_SOLID},{"shells",TopAbs_SHELL},{"faces",TopAbs_FACE},{"wires",TopAbs_WIRE},{"edges",TopAbs_EDGE},{"vertices",TopAbs_VERTEX}})
        result.report.set(entry.first,V::makeNum(unique(shape,entry.second).Extent()));
    const auto faces=unique(shape,TopAbs_FACE),edges=unique(shape,TopAbs_EDGE);
    result.report.set("area_mm2",V::makeNum(properties(shape,'a')));
    double maximumTolerance=0;for(const auto kind:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}){const auto entities=unique(shape,kind);for(int i=1;i<=entities.Extent();++i){const double value=kind==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(entities(i))):kind==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(entities(i))):BRep_Tool::Tolerance(TopoDS::Vertex(entities(i)));if(!std::isfinite(value)||value<0)fail("repair_invalid_geometry","Nonfinite or negative native entity tolerance");maximumTolerance=std::max(maximumTolerance,value);}}
    result.report.set("max_entity_tolerance_mm",V::makeNum(maximumTolerance));
    std::vector<std::vector<int>> owners(edges.Extent());result.incidence.resize(edges.Extent());
    for(int face=1;face<=faces.Extent();++face)for(TopExp_Explorer ex(faces(face),TopAbs_EDGE);ex.More();ex.Next()){
        const int index=edges.FindIndex(ex.Current())-1;++result.incidence[index];owners[index].push_back(face-1);
    }
    std::vector<int> groups(faces.Extent());for(size_t i=0;i<groups.size();++i)groups[i]=(int)i;
    auto leader=[&](int i){while(groups[i]!=i)i=groups[i];return i;};
    int free=0,seams=0,degenerated=0,multiple=0;double length=0;V boundaries=V::makeArr();
    for(int i=0;i<edges.Extent();++i){
        const auto edge=TopoDS::Edge(edges(i+1));if(BRep_Tool::Degenerated(edge)){++degenerated;continue;}
        const auto& list=owners[i];if(list.size()>2)++multiple;
        if(list.size()==2&&list[0]==list[1])++seams;
        for(size_t j=1;j<list.size();++j)groups[leader(list[j])]=leader(list[0]);
        if(list.size()==1){++free;const double measured=properties(edge,'l');if(!std::isfinite(measured))fail("repair_invalid_geometry","Nonfinite boundary length");length+=measured;V row=V::makeObj();row.set("edge",V::makeNum(i));row.set("length_mm",V::makeNum(measured));boundaries.arr.push_back(row);}
    }
    std::set<int> components;for(size_t i=0;i<groups.size();++i)components.insert(leader((int)i));result.components=(int)components.size();
    result.report.set("valid",V::makeBool(BRepCheck_Analyzer(shape,true,false,true).IsValid()));
    bool oriented=true;const auto shells=unique(shape,TopAbs_SHELL);for(int i=1;i<=shells.Extent();++i){BRepCheck_Shell check(TopoDS::Shell(shells(i)));oriented&=check.Orientation()==BRepCheck_NoError;}result.report.set("orientation_consistent",V::makeBool(oriented));
    result.report.set("free_edges",V::makeNum(free));result.report.set("free_boundary_length_mm",V::makeNum(length));result.report.set("boundaries",boundaries);
    result.report.set("periodic_seam_edges",V::makeNum(seams));result.report.set("degenerated_edges",V::makeNum(degenerated));result.report.set("nonmanifold_edges",V::makeNum(multiple));result.report.set("components",V::makeNum(result.components));
    if(shape.ShapeType()==TopAbs_SOLID)result.report.set("signed_volume_mm3",V::makeNum(properties(shape)));
    return result;
}
V remapTopology(const V& topology,const std::string& inputDigest,const TopoDS_Shape& source,const TopoDS_Shape& output,const V& current){
    if(topology.t!=V::Obj||need(topology,"brep_sha256").asStr()!=inputDigest)fail("repair_stale_topology","Source topology does not match the exact B-rep");
    const auto& rows=need(topology,"references");if(rows.t!=V::Arr)fail("repair_invalid_reference","Source references must be an array");V mapped=V::makeArr();
    for(auto row:rows.arr){
        const auto name=need(row,"name").asStr(),kind=need(row,"kind").asStr(),status=need(row,"status").asStr();
        if(name.find("@ark.")!=std::string::npos){row.set("status",V::makeStr("obsolete"));row.set("indices",V::makeArr());row.set("reason",V::makeStr("Repair created a new B-rep identity; select current geometry"));mapped.arr.push_back(row);continue;}
        if(status!="bound"&&status!="merged"){mapped.arr.push_back(row);continue;}
        if(kind!="face"&&kind!="edge"&&kind!="axis")fail("repair_invalid_reference","Unsupported bound semantic reference kind");
        const auto prior=shapes(source,kind=="edge"?TopAbs_EDGE:TopAbs_FACE),next=shapes(output,kind=="edge"?TopAbs_EDGE:TopAbs_FACE);const auto& indices=need(row,"indices");
        if(indices.t!=V::Arr||indices.arr.size()!=1||indices.arr[0].t!=V::Num||indices.arr[0].num<0||std::floor(indices.arr[0].num)!=indices.arr[0].num||indices.arr[0].num>=prior.size())fail("repair_invalid_reference","Semantic reference must bind one source entity");
        const auto& entity=prior[(size_t)indices.arr[0].num];size_t index=0;while(index<next.size()&&!entity.IsSame(next[index]))++index;
        if(index==next.size())fail("repair_custody_violation","A semantic entity was lost");V target=V::makeArr();target.arr.push_back(V::makeNum(index));row.set("indices",target);
        // Edge tangent conventions and analytic axis/radius descriptors belong
        // to the producer. Only a planar face normal depends on this repair.
        if(kind=="face")for(const auto& generated:need(current,"references").arr)if(need(generated,"kind").asStr()==kind){const auto* ids=generated.find("indices");if(ids&&ids->arr.size()==1&&ids->arr[0].num==index){if(const auto* desc=generated.find("descriptor");desc&&desc->find("normal"))row.set("descriptor",*desc);break;}}
        mapped.arr.push_back(row);
    }
    V result=V::makeObj();result.set("brep_sha256",need(current,"brep_sha256"));result.set("references",mapped);return result;
}
}
V repairSurfaceOrientation(const V& request){
    try{
        for(const auto& field:request.obj)if(field.first!="op"&&field.first!="input_brep"&&field.first!="topology"&&field.first!="mode"&&field.first!="normal")fail("repair_invalid_request","Unknown repair field: "+field.first);
        if(need(request,"mode").asStr()!="orientation")fail("repair_unsupported_mode","Only explicit orientation repair is supported");
        const auto& encoded=need(request,"input_brep");if(encoded.t!=V::Str||encoded.str.empty())fail("repair_invalid_request","input_brep must be exact nonempty base64");
        // Only this explicitly requested repair path parses an invalid candidate.
        // It is never published until all face-custody and output-validity gates pass.
        std::istringstream stream(base64Decode(encoded.str));TopoDS_Shape source;BRep_Builder reader;BRepTools::Read(source,stream,reader);
        if(source.IsNull()||stream.fail())fail("repair_invalid_input","Cannot parse the exact B-rep candidate");
        const bool solid=source.ShapeType()==TopAbs_SOLID,face=source.ShapeType()==TopAbs_FACE;
        if(!solid&&!face&&source.ShapeType()!=TopAbs_SHELL)fail("repair_unsupported_shape","Requires one face, one connected shell or one single-shell solid; compounds and multiple solids reject");
        const auto faces=unique(source,TopAbs_FACE),shells=unique(source,TopAbs_SHELL);
        if(shapes(source,TopAbs_FACE).size()!=(size_t)faces.Extent())fail("repair_nonmanifold","Repeated use of the same face is outside the supported manifold domain");
        if(faces.Extent()==0||faces.Extent()>512||(solid&&shells.Extent()!=1))fail("repair_unsupported_shape","Requires 1..512 faces and at most one shell; cavities are outside scope");
        if(unique(source,TopAbs_EDGE).Extent()>4096)fail("repair_unsupported_shape","More than 4096 edges");
        const auto before=inspect(source);
        if(before.components!=1)fail("repair_disconnected","Orientation repair cannot join disconnected surfaces or close gaps");
        if(need(before.report,"nonmanifold_edges").num)fail("repair_nonmanifold","More than two uses of an edge");
        if(solid&&need(before.report,"free_edges").num)fail("repair_open_solid","Orientation repair cannot fill a missing face or close a geometric gap");
        std::vector<std::string> faceCustody;for(int i=1;i<=faces.Extent();++i){
            if(!BRepCheck_Analyzer(faces(i),true,false,true).IsValid())fail("repair_invalid_geometry","An individual face has invalid geometry, wires or pcurves; orientation repair cannot heal it");
            faceCustody.push_back(brepData(faces(i).Oriented(TopAbs_FORWARD)));
        }
        TopoDS_Shape output=source;
        if(!face){
            ShapeFix_Shell fixer;fixer.FixFaceMode()=0;
            fixer.FixFaceOrientation(TopoDS::Shell(solid?shells(1):source),true,false);
            if(!shapes(fixer.ErrorFaces(),TopAbs_FACE).empty()||fixer.NbShells()!=1)fail("repair_unorientable","Kernel could not orient every face in one shell");
            output=fixer.Shell();
            if(solid){BRepBuilderAPI_MakeSolid maker(TopoDS::Shell(output));if(!maker.IsDone())fail("repair_invalid_result","Cannot rebuild the same shell as a solid");auto fixed=maker.Solid();if(!BRepLib::OrientClosedSolid(fixed))fail("repair_unorientable","Cannot determine outward solid orientation");output=fixed;}
        }
        if(const auto* normal=request.find("normal")){
            if(!face)fail("repair_unsupported_normal","Explicit normal requires one planar face; extract the face first");
            if(normal->t!=V::Arr||normal->arr.size()!=3)fail("repair_invalid_request","normal must contain three finite numbers");
            for(const auto& value:normal->arr)if(value.t!=V::Num||!std::isfinite(value.num)||std::abs(value.num)>1e8)fail("repair_invalid_request","normal must contain three finite numbers");
            gp_Vec desired(normal->arr[0].num,normal->arr[1].num,normal->arr[2].num);if(desired.Magnitude()<=1e-12)fail("repair_invalid_request","normal cannot be zero");desired.Normalize();
            BRepAdaptor_Surface support(TopoDS::Face(source));if(support.GetType()!=GeomAbs_Plane)fail("repair_unsupported_normal","A periodic or curved face has no single global normal");
            gp_Vec actual(support.Plane().Axis().Direction());if(source.Orientation()==TopAbs_REVERSED)actual.Reverse();const double dot=actual.Dot(desired);
            if(std::abs(std::abs(dot)-1)>1e-10)fail("repair_normal_mismatch","Requested normal must be parallel to the exact planar support; geometry is never rotated");if(dot<0)output.Reverse();
        }else if(face)fail("repair_normal_required","A single face requires an explicit planar normal orientation intent");
        const auto after=inspect(output);
        for(const char* key:{"solids","shells","faces","wires","edges","vertices","free_edges","free_boundary_length_mm","periodic_seam_edges","degenerated_edges","components","max_entity_tolerance_mm"})if(need(before.report,key).num!=need(after.report,key).num)fail("repair_custody_violation",std::string("Changed ")+key);
        const auto finalFaces=unique(output,TopAbs_FACE);int changedFaces=0;
        for(int i=1;i<=faces.Extent();++i){const int target=finalFaces.FindIndex(faces(i));if(!target||brepData(finalFaces(target).Oriented(TopAbs_FORWARD))!=faceCustody[i-1])fail("repair_custody_violation","Underlying face geometry, boundary topology or tolerances changed");if(faces(i).Orientation()!=finalFaces(target).Orientation())++changedFaces;}
        if(!need(after.report,"valid").b)fail("repair_invalid_result","Corrected result still fails exact-method B-rep validation");
        if(!need(after.report,"orientation_consistent").b)fail("repair_invalid_result","Corrected shell still has inconsistent face orientation");
        if(solid&&need(after.report,"signed_volume_mm3").num<=0)fail("repair_invalid_result","Corrected solid is not outward with positive volume");
        auto result=shapeResult(output,solid);auto report=need(result,"report");const auto digest=sha256(base64Decode(encoded.str));
        report.set("operation",V::makeStr("surface_repair"));report.set("repair_mode",V::makeStr("orientation"));report.set("before",before.report);report.set("after",after.report);report.set("changed_faces",V::makeNum(changedFaces));report.set("changed",V::makeBool(changedFaces!=0));
        report.set("source_brep_sha256",V::makeStr(digest));report.set("geometry_deviation_mm",V::makeNum(0));report.set("geometry_deviation_authority",V::makeStr("identical serialized forward faces, native supports, pcurves, boundaries and tolerances"));report.set("tolerances_preserved",V::makeBool(true));report.set("all_faces_preserved",V::makeBool(true));report.set("all_solids_preserved",V::makeBool(true));report.set("gap_healing",V::makeBool(false));report.set("exact_method_validation",V::makeBool(true));report.set("manufacturing_qualified",V::makeBool(false));
        if(const auto* normal=request.find("normal"))report.set("requested_normal",*normal);result.set("report",report);
        if(const auto* topology=request.find("topology"))result.set("topology",remapTopology(*topology,digest,source,output,need(result,"topology")));
        return completeSelectionReferences(std::move(result));
    }catch(const Standard_Failure& error){fail("repair_kernel_failure",error.GetMessageString()?error.GetMessageString():"OCCT exception");}
    catch(const std::exception& error){if(std::string(error.what()).rfind("repair_",0)==0)throw;fail("repair_invalid_input",error.what());}
}
}
