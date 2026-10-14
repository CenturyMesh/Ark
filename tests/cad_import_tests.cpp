#include "ark/agent.hpp"
#include "ark/cad_import.hpp"
#include "ark/native_backend.hpp"
#include "ark/serialize.hpp"
#include "../src/occt/common.hpp"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax2.hxx>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;int checks=0;
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void assertNear(double value,double expected,double tolerance,const char* reason){check(std::isfinite(value)&&std::abs(value-expected)<=tolerance,reason);}
template<class F>void rejects(F action,const char* reason){bool caught=false;try{action();}catch(const std::exception&){caught=true;}check(caught,reason);}
std::string utf8(const fs::path& path){auto result=path.u8string();return std::string(result.begin(),result.end());}
struct Workspace {fs::path directory;Workspace(){directory=fs::temp_directory_path()/("ark-cad-import-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(directory);}~Workspace(){std::error_code error;fs::remove_all(directory,error);}};
V accepted(const V& response){if(auto error=response.find("isError");error&&error->b){auto payload=response.find("structuredContent");throw std::runtime_error(payload&&payload->find("error")?payload->find("error")->str:"CAD test tool failed");}auto result=response.find("structuredContent");check(result!=nullptr,"CAD test missing structured result");return *result;}
V arguments(const fs::path& path){V value=V::makeObj();value.set("path",V::makeStr(utf8(path)));return value;}
V importFile(ark::AgentSession& session,const fs::path& path,const char* mode="import"){V value=arguments(path);value.set("expected_revision",V::makeNum(session.revision));value.set("mode",V::makeStr(mode));return accepted(session.call("import_cad",value));}
void writeIges(const TopoDS_Shape& shape,const fs::path& path,const char* units="MM"){
    IGESControl_Controller::Init();IGESControl_Writer writer(units,1);check(writer.AddShape(shape),"synthetic IGES adds whole exact shape");writer.ComputeModel();check(writer.Write(utf8(path).c_str()),"synthetic IGES writes");
}
V model(const V& package){V request=V::makeObj(),spec=V::makeObj();request.set("operation",V::makeStr("model_solid"));spec.set("op",V::makeStr("imported_cad"));spec.set("package",package);request.set("request",spec);return ark::nativeWorkerRequest(request);}
V package(const V& receipt){return *receipt.find("command")->find("native")->find("package");}
void exactMultiBodyPortable(){
    Workspace workspace;const auto path=workspace.directory/"multi-body.step";
    const auto a=BRepPrimAPI_MakeBox(gp_Pnt(10,20,30),10,15,20).Shape(),b=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(70,0,0),gp_Dir(0,0,1)),4,12).Shape();
    ark::occt::writeStep(ark::occt::compound({a,b}),path);const auto inspected=ark::inspectCadFile(utf8(path));
    check(inspected.find("census")->find("solids")->num==2,"STEP inspection retains both solids");check(inspected.find("recognition")->find("proven_primitive_candidates")->num==2,"box and cylinder recognized only after measured equivalence");
    check(!inspected.find("recognition")->find("automatic_conversion_complete")->b,"primitive recognition does not claim finished Arkitfy");
    auto receipt=ark::buildCadImport(utf8(path));auto sourcePackage=package(receipt);const auto native=model(sourcePackage);
    check(native.find("report")->find("source_transfer_reproduced")->b,"stored exact B-rep is reproduced from source");assertNear(native.find("report")->find("volume_mm3")->num,3000+std::acos(-1.0)*16*12,1e-4,"multi-body source exact total volume");
    ark::Feature source;source.type=ark::FeatureType::NativeSolid;source.nativeSpec=*receipt.find("command")->find("native");ark::Body shifted;gp_Trsf offset;offset.SetTranslation(gp_Vec(1,0,0));shifted.nativeData=ark::occt::shapeResult(ark::occt::transformed(ark::occt::compound({a,b}),offset),false);
    const auto mismatch=ark::inspectImportedCad(source,&shifted);check(mismatch.find("comparison")->find("comparison_checked")->b&&!mismatch.find("comparison")->find("equivalent_in_scope")->b&&mismatch.find("comparison")->find("symmetric_difference_mm3")->num>0,"equal-volume shifted reconstruction fails exact equivalence");
    ark::AgentSession session;auto imported=importFile(session,path,"arkitfy");check(session.document.features.size()==1&&session.document.evalBodies.size()==1,"generic import creates one whole-source reference");
    V persisted;check(arkjson::parse(ark::documentToJson(session.document),persisted)&&persisted.find("version")->num==38,"new exact source recipe selects document version 38");persisted.set("version",V::makeNum(37));ark::Document downgraded;std::string downgradeError;check(!ark::documentFromJson(persisted.dump(),downgraded,&downgradeError),"exact source recipe cannot be mislabeled as legacy document version");
    check(imported.find("agent_prompt")&&imported.find("agent_prompt")->str.find("cad_inspect")!=std::string::npos,"Arkitfy returns actionable exact agent reconstruction prompt");
    V inspect=V::makeObj();inspect.set("id",V::makeNum(session.document.features.front().id));inspect.set("compare_id",V::makeNum(session.document.features.front().id));auto self=accepted(session.call("cad_inspect",inspect));check(self.find("comparison")->find("equivalent_in_scope")->b,"source self comparison proves exact closed-solid equivalence");
    const auto storedPath=workspace.directory/"portable.ark";accepted(session.call("doc_save",arguments(storedPath)));fs::remove(path);
    ark::AgentSession restored;accepted(restored.call("doc_load",arguments(storedPath)));check(restored.document.features.size()==1,"portable imported CAD survives original source deletion");
    check(restored.document.evalBodies.front().nativeData.find("report")->find("solids")->num==2,"portable recomputation retains all solids");
    auto corruption=sourcePackage;corruption.set("source_sha256",V::makeStr(std::string(64,'0')));rejects([&]{model(corruption);},"tampered embedded STEP digest rejects");
    corruption=sourcePackage;corruption.set("brep_sha256",V::makeStr(std::string(64,'0')));rejects([&]{model(corruption);},"tampered embedded B-rep digest rejects");
    corruption=sourcePackage;const auto foreign=ark::occt::brepData(BRepPrimAPI_MakeBox(2,3,4).Shape());corruption.set("brep_base64",V::makeStr(foreign));corruption.set("brep_sha256",V::makeStr(ark::occt::sha256(ark::occt::base64Decode(foreign))));rejects([&]{model(corruption);},"self-consistent foreign exact geometry cannot replace source transfer");
}
void surfacesIgesUnitsAndRecognition(){
    Workspace workspace;const auto surface=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,4),gp_Dir(0,0,1)),0,30,0,12).Shape();
    const auto mixed=ark::occt::compound({BRepPrimAPI_MakeBox(10,15,20).Shape(),surface});const auto path=workspace.directory/"mixed.step";ark::occt::writeStep(mixed,path);
    auto result=ark::inspectCadFile(utf8(path));check(result.find("census")->find("surface_only_faces")->num==1,"mixed STEP does not discard free exact surfaces");auto sourcePackage=package(ark::buildCadImport(utf8(path)));auto native=model(sourcePackage);check(native.find("report")->find("surface_only_faces")->num==1,"mixed STEP surfaces survive source reproduction");
    ark::Feature feature;feature.type=ark::FeatureType::NativeSolid;feature.nativeSpec=V::makeObj();feature.nativeSpec.set("op",V::makeStr("imported_cad"));feature.nativeSpec.set("package",sourcePackage);ark::Body candidate;candidate.nativeData=native;
    const auto comparison=ark::inspectImportedCad(feature,&candidate);check(!comparison.find("comparison")->find("comparison_checked")->b&&!comparison.find("comparison")->find("equivalent_in_scope")->b,"free surface equivalence never inferred from area or bounds");
    const auto igesPath=workspace.directory/"surface.iges";writeIges(surface,igesPath);result=ark::inspectCadFile(utf8(igesPath));check(result.find("census")->find("solids")->num==0&&result.find("census")->find("faces")->num==1,"IGES imports standalone exact trimmed surface");assertNear(result.find("report")->find("area_mm2")->num,360,1e-5,"IGES surface area preserves trim");model(package(ark::buildCadImport(utf8(igesPath))));
    const auto inchPath=workspace.directory/"inch.igs";writeIges(BRepPrimAPI_MakeBox(25.4,50.8,76.2).Shape(),inchPath,"INCH");result=ark::inspectCadFile(utf8(inchPath));assertNear(result.find("units")->find("source_length_to_mm")->num,25.4,1e-10,"IGES source declares inches converted to millimetres");assertNear(result.find("report")->find("bounds_mm")->arr[3].num,25.4,1e-5,"IGES inch unit X size in mm");assertNear(result.find("report")->find("bounds_mm")->arr[5].num,76.2,1e-5,"IGES inch unit Z size in mm");model(package(ark::buildCadImport(utf8(inchPath))));
    const auto stepInchPath=workspace.directory/"inch.step";Interface_Static::SetCVal("write.step.unit","INCH");ark::occt::writeStep(BRepPrimAPI_MakeBox(25.4,50.8,76.2).Shape(),stepInchPath);Interface_Static::SetCVal("write.step.unit","MM");result=ark::inspectCadFile(utf8(stepInchPath));assertNear(result.find("report")->find("bounds_mm")->arr[3].num,25.4,1e-5,"STEP inch source retained in model millimetres");check(!result.find("units")->find("declared_length_units")->arr.empty(),"STEP source unit declarations retained");
    const auto arbitraryPath=workspace.directory/"sphere.step";ark::occt::writeStep(BRepPrimAPI_MakeSphere(12).Shape(),arbitraryPath);result=ark::inspectCadFile(utf8(arbitraryPath));check(result.find("recognition")->find("proven_primitive_candidates")->num==0&&!result.find("recognition")->find("automatic_conversion_complete")->b,"arbitrary source remains exact valid reference without false reconstruction success");
    const auto curvePath=workspace.directory/"mixed-curve.step";ark::occt::writeStep(ark::occt::compound({BRepPrimAPI_MakeBox(10,15,20).Shape(),BRepBuilderAPI_MakeEdge(gp_Pnt(2,3,4),gp_Pnt(8,9,12)).Shape()}),curvePath);result=ark::inspectCadFile(utf8(curvePath));check(result.find("census")->find("free_edges")->num==1,"free STEP curve retained alongside solid geometry");
}
void rejectionAndAtomicity(){
    Workspace workspace;const auto badPath=workspace.directory/"bad.step";ark::occt::writeFile(badPath,"corrupted STEP source");rejects([&]{ark::inspectCadFile(utf8(badPath));},"corrupt STEP rejects");
    const auto invalidExtension=workspace.directory/"not-cad.obj";ark::occt::writeFile(invalidExtension,"anything");rejects([&]{ark::inspectCadFile(utf8(invalidExtension));},"unsupported exchange format rejects explicitly");
    ark::AgentSession session;V args=arguments(badPath);args.set("expected_revision",V::makeNum(session.revision));args.set("mode",V::makeStr("import"));const auto before=session.revision;const auto failed=session.call("import_cad",args);check(failed.find("isError")&&failed.find("isError")->b&&session.revision==before&&session.document.features.empty(),"corrupt source import leaves document and revision untouched");
    const auto goodPath=workspace.directory/"good.step";ark::occt::writeStep(BRepPrimAPI_MakeBox(10,20,30).Shape(),goodPath);args=arguments(goodPath);args.set("expected_revision",V::makeNum(before+100));args.set("mode",V::makeStr("import"));const auto stale=session.call("import_cad",args);check(stale.find("isError")&&stale.find("isError")->b&&session.revision==before,"stale CAD import rejects atomic publication");
    auto receipt=ark::buildCadImport(utf8(goodPath));ark::Feature feature;feature.type=ark::FeatureType::NativeSolid;feature.nativeSpec=*receipt.find("command")->find("native");std::string error;check(ark::validateCadPackageAuthoring(feature,error),"valid immutable CAD authoring accepted");feature.p["width"]="9";check(!ark::validateCadPackageAuthoring(feature,error),"source geometry cannot be changed through invented parameters");
}
}
int main(){try{exactMultiBodyPortable();surfacesIgesUnitsAndRecognition();rejectionAndAtomicity();std::cout<<"CAD import checks passed: "<<checks<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"CAD import checks failed: "<<error.what()<<'\n';return 1;}}
