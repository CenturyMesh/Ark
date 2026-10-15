#include "ark/occt_modeling.hpp"
#include "../src/occt/common.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark::occt;
const V& need(const V& value,const char* key){const auto* field=value.find(key);if(!field)throw std::runtime_error(std::string("Missing ")+key);return *field;}
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
V parse(const char* text){V value;check(arkjson::parse(text,value),"Test JSON");return value;}
V request(const TopoDS_Shape& shape){auto value=parse(R"({"op":"surface_repair","mode":"orientation"})");value.set("input_brep",V::makeStr(brepData(shape)));return value;}
TopoDS_Shape shellVariant(const TopoDS_Shape& solid,bool omit,bool reverse){BRep_Builder builder;TopoDS_Shell shell;builder.MakeShell(shell);auto faces=shapes(solid,TopAbs_FACE);for(size_t i=0;i<faces.size()-(omit?1:0);++i){if(reverse&&i==0)faces[i].Reverse();builder.Add(shell,faces[i]);}return shell;}
V rows=V::makeArr();int failures=0;
void trial(const char* name,V req,const std::string& expected,bool changed=false){
    V row=V::makeObj();row.set("case",V::makeStr(name));row.set("expected",V::makeStr(expected.empty()?"accept":expected));
    const auto bytes=need(req,"input_brep").str;
    try{
        V result;std::string rejection;try{result=modelSurfaceNative(req);}catch(const std::exception& e){rejection=e.what();row.set("error",V::makeStr(rejection));}
        if(expected.empty()){
            check(rejection.empty(),"Unexpected rejection: "+rejection);const auto& report=need(result,"report");const auto& before=need(report,"before");const auto& after=need(report,"after");check(need(after,"valid").b&&need(after,"orientation_consistent").b,"Invalid or inconsistently oriented result");check(need(report,"changed").b==changed,"Unexpected orientation-change evidence");check(need(report,"geometry_deviation_mm").num==0&&need(report,"tolerances_preserved").b,"Lost geometry/tolerance custody");
            for(const char* field:{"solids","faces","edges","vertices","free_edges","periodic_seam_edges","degenerated_edges","max_entity_tolerance_mm"})check(need(before,field).num==need(after,field).num,std::string("Changed input inventory: ")+field);
            if(std::string(name)=="invalid-solid-neighbour-orientation")check(!need(before,"valid").b&&!need(before,"orientation_consistent").b,"Invalid fixture was not measured before repair");
            row.set("report",report);
            if(const auto* sourceTopology=req.find("topology")){
                const auto& refs=need(need(result,"topology"),"references");bool semantic=false,obsolete=false,edgeConvention=false;
                for(const auto& ref:refs.arr){if(need(ref,"name").str=="authored_face"){semantic=true;check(need(ref,"status").str=="bound","Lost semantic face");check(need(need(ref,"descriptor"),"normal").arr[2].num>0.99,"Semantic normal retained stale orientation");}if(need(ref,"name").str.find("@ark.")==0&&need(ref,"status").str=="obsolete")obsolete=true;}
                for(const auto& ref:refs.arr)if(need(ref,"name").str=="authored_edge")for(const auto& original:need(*sourceTopology,"references").arr)if(need(original,"name").str=="authored_edge"){check(need(original,"descriptor").dump()==need(ref,"descriptor").dump(),"Repair changed producer edge tangent/radius convention");edgeConvention=true;}
                check(semantic&&obsolete&&edgeConvention,"Semantic identity/current-B-rep custody not explicit");
            }
        }else check(rejection.find(expected+":")==0,"Wrong rejection: "+rejection);
        check(need(req,"input_brep").str==bytes,"Mutated original producer input");row.set("passed",V::makeBool(true));
    }catch(const std::exception& e){++failures;row.set("passed",V::makeBool(false));row.set("failure",V::makeStr(e.what()));std::cerr<<name<<": "<<e.what()<<'\n';}
    rows.arr.push_back(row);
}
}
int main(int argc,char** argv){
    try{
        auto box=BRepPrimAPI_MakeBox(10,8,6).Shape();
        auto inconsistent=shellVariant(box,false,true);check(BRepCheck_Shell(TopoDS::Shell(inconsistent)).Orientation()!=BRepCheck_NoError,"Fixture must begin with inconsistent shell orientation");trial("six-face-neighbour-orientation",request(inconsistent),"",true);
        auto invalidSolid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(inconsistent)).Solid();check(!BRepCheck_Analyzer(invalidSolid).IsValid(),"Solid fixture should be invalid before explicit repair");trial("invalid-solid-neighbour-orientation",request(invalidSolid),"",true);
        trial("valid-solid-noop",request(box),"");
        auto inward=BRepPrimAPI_MakeCylinder(4,12).Shape();inward.Reverse();trial("inward-periodic-cylinder",request(inward),"",true);
        auto cone=BRepPrimAPI_MakeCone(5,0,10).Shape();cone.Reverse();trial("apex-degenerate-seam-preserved",request(cone),"",true);
        auto missing=shellVariant(box,true,false);trial("open-shell-boundary-retained",request(missing),"");
        trial("open-solid-gap-refused",request(BRepBuilderAPI_MakeSolid(TopoDS::Shell(missing)).Solid()),"repair_open_solid");
        trial("multiple-solids-refused",request(compound({box,inward})),"repair_unsupported_shape");
        BRep_Builder builder;TopoDS_Shell disconnected;builder.MakeShell(disconnected);builder.Add(disconnected,shapes(box,TopAbs_FACE)[0]);builder.Add(disconnected,shapes(box,TopAbs_FACE)[1]);trial("disconnected-gap-refused",request(disconnected),"repair_disconnected");
        TopoDS_Shell nonmanifold;builder.MakeShell(nonmanifold);for(const auto& face:shapes(box,TopAbs_FACE))builder.Add(nonmanifold,face);builder.Add(nonmanifold,shapes(box,TopAbs_FACE)[0]);trial("multiple-edge-use-refused",request(nonmanifold),"repair_nonmanifold");
        auto planar=BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()),0,10,0,8).Shape();planar.Reverse();auto orient=request(planar);orient.set("normal",parse("[0,0,1]"));
        TopoDS_Wire brokenWire;builder.MakeWire(brokenWire);auto boundary=shapes(planar,TopAbs_EDGE);for(size_t i=1;i<boundary.size();++i)builder.Add(brokenWire,boundary[i]);TopoDS_Face brokenFace;builder.MakeFace(brokenFace,BRep_Tool::Surface(TopoDS::Face(planar)),BRep_Tool::Tolerance(TopoDS::Face(planar)));builder.Add(brokenFace,brokenWire);auto broken=request(brokenFace);broken.set("normal",parse("[0,0,1]"));trial("missing-boundary-edge-no-reconstruction",broken,"repair_invalid_geometry");
        auto source=shapeResult(planar,false);V topology=need(source,"topology"),references=need(topology,"references");V alias=parse(R"({"name":"authored_face","kind":"face","status":"bound","indices":[0]})");references.arr.push_back(alias);
        V edgeAlias=parse(R"({"name":"authored_edge","kind":"edge","status":"bound","indices":[0]})");for(const auto& ref:need(topology,"references").arr)if(need(ref,"kind").str=="edge"&&need(ref,"indices").arr[0].num==0){V descriptor=need(ref,"descriptor"),tangent=need(descriptor,"tangent");for(auto& coordinate:tangent.arr)coordinate.num=-coordinate.num;descriptor.set("tangent",tangent);edgeAlias.set("descriptor",descriptor);break;}references.arr.push_back(edgeAlias);
        topology.set("references",references);orient.set("input_brep",need(source,"brep_base64"));orient.set("topology",topology);
        trial("explicit-planar-normal-with-semantic-custody",orient,"",true);
        auto stale=orient;V staleTopology=topology;staleTopology.set("brep_sha256",V::makeStr(std::string(64,'0')));stale.set("topology",staleTopology);trial("stale-source-topology",stale,"repair_stale_topology");
        auto tilted=request(planar);tilted.set("normal",parse("[1,0,1]"));trial("tilted-normal-no-geometry-rotation",tilted,"repair_normal_mismatch");
        trial("normal-required-for-single-face",request(planar),"repair_normal_required");
        auto cylinder=BRepPrimAPI_MakeCylinder(4,12).Shape();for(const auto& f:shapes(cylinder,TopAbs_FACE))if(BRepAdaptor_Surface(TopoDS::Face(f)).GetType()==GeomAbs_Cylinder){auto periodic=request(f);periodic.set("normal",parse("[0,0,1]"));trial("periodic-face-no-global-normal",periodic,"repair_unsupported_normal");}
        auto badMode=request(box);badMode.set("mode",V::makeStr("heal"));trial("arbitrary-healing-refused",badMode,"repair_unsupported_mode");
        auto tolerance=request(box);tolerance.set("tolerance",V::makeNum(.01));trial("tolerance-widening-refused",tolerance,"repair_invalid_request");
        const auto loft=modelSurfaceNative(parse(R"({"op":"loft","solid":true,"ruled":true,"profiles":[{"entities":[{"kind":"circle","center":[0,0],"radius":5}]},{"frame":[1,0,0,0,0,1,0,0,0,0,1,0,2,0,10,1],"entities":[{"kind":"circle","center":[0,0],"radius":3}]}]})"));auto loftShape=readBrep(need(loft,"brep_base64").str);loftShape.Reverse();trial("eccentric-loft-inward-orientation",request(loftShape),"",true);
        const auto sweep=modelSurfaceNative(parse(R"({"op":"sweep","solid":true,"section":{"entities":[{"kind":"circle","center":[0,0],"radius":2}]},"path":{"entities":[{"kind":"line","points":[[0,0,0],[0,0,10]]}]}})"));auto sweepShape=readBrep(need(sweep,"brep_base64").str);sweepShape.Reverse();trial("sweep-inward-orientation",request(sweepShape),"",true);
        auto thick=parse(R"({"op":"thicken","thickness":2})");thick.set("input_brep",V::makeStr(brepData(planar)));auto thickResult=modelSurfaceNative(thick);auto thickShape=readBrep(need(thickResult,"brep_base64").str);thickShape.Reverse();trial("thickened-face-inward-orientation",request(thickShape),"",true);
    }catch(const std::exception& e){++failures;std::cerr<<"Fixture construction: "<<e.what()<<'\n';}
    V receipt=V::makeObj();receipt.set("schema",V::makeStr("ark.surface.repair.corpus.v1"));receipt.set("milestone",V::makeStr("B.03"));receipt.set("kernel",V::makeStr(kernelVersion()));receipt.set("trials",rows);receipt.set("failures",V::makeNum(failures));
    if(argc==3&&std::string(argv[1])=="--receipt"){std::ofstream file(argv[2],std::ios::binary);file<<receipt.dump()<<'\n';if(!file)++failures;}
    std::cout<<"B.03 surface repair: "<<rows.arr.size()<<" trials; "<<failures<<" failures\n";return failures?1:0;
}
