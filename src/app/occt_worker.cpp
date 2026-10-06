#include "common.hpp"
#include "ark/occt_source.hpp"
#include "ark/occt_modeling.hpp"
#include "ark/occt_sheet.hpp"
#include "ark/occt_solid.hpp"
#include <Standard_Failure.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>
#include <iostream>
#include <stdexcept>
using namespace ark::occt;
int runWorker(const fs::path& requestPath,const fs::path& resultPath){

    try{
        V request;if(!arkjson::parse(readFile(requestPath),request)||request.t!=V::Obj)throw std::runtime_error("Invalid native worker request");
        auto field=[&](const char* name)->const V&{auto p=request.find(name);if(!p)throw std::runtime_error(std::string("Missing native field: ")+name);return *p;};
        auto text=[&](const char* name){auto& p=field(name);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Invalid native string: ")+name);return p.str;};
        auto op=text("operation");V result;
        if(op=="import_tube")result=importSourceTubeNative(text("source_path"),text("cache_directory"));
        else if(op=="export_tube")result=exportSourceTubeNative(field("package"),text("path"));
        else if(op=="export_notch")result=exportNotchNative(field("plan"),text("path"));
        else if(op=="model")result=modelSurfaceNative(field("request"));
        else if(op=="model_sheet")result=modelSheetNative(field("request"));
        else if(op=="model_solid")result=modelSolidNative(field("request"));
        else if(op=="reference_frame")result=referenceFrameNative(field("request"));
        else if(op=="clearance")result=clearanceNative(field("request"));
        else if(op=="point_distance")result=pointDistanceNative(field("request"));
        else if(op=="solid_centroid")result=solidCentroidNative(field("request"));
        else if(op=="import_sheet")result=importSheetNative(request);
        else if(op=="export_sheet")result=exportSheetNative(request);
        else if(op=="step_inspect"){
            auto path=fs::u8path(text("path"));auto bytes=readFile(path);auto digest=sha256(bytes);auto shape=readStep(path);result=shapeReport(shape,false);result.set("source_sha256",V::makeStr(digest));result.set("units",V::makeStr("mm"));V bodies=V::makeArr();
            bool details=false;if(auto p=request.find("faces")){if(p->t!=V::Bool)throw std::runtime_error("faces must be boolean");details=p->b;}
            const char* names[]={"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"};auto solids=shapes(shape,TopAbs_SOLID);
            for(size_t i=0;i<solids.size();++i){auto record=shapeReport(solids[i],true);record.set("body_index",V::makeNum(i));V counts=V::makeObj(),faces=V::makeArr();size_t index=0;
                for(auto& item:shapes(solids[i],TopAbs_FACE)){auto face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);int kind=(int)surface.GetType();std::string name=names[kind>=0&&kind<10?kind:10];auto count=counts.find(name.c_str());counts.set(name,V::makeNum(count?count->num+1:1));
                    if(details){V row=V::makeObj();row.set("face_index",V::makeNum(index));row.set("type",V::makeStr(name));row.set("area_mm2",V::makeNum(properties(face,'a')));V box=V::makeArr();for(auto n:bounds(face))box.arr.push_back(V::makeNum(n));row.set("bounds_mm",box);faces.arr.push_back(row);}++index;
                }record.set("surface_counts",counts);if(details)record.set("face_inventory",faces);bodies.arr.push_back(record);
            }result.set("bodies",bodies);if(sha256(readFile(path))!=digest)throw std::runtime_error("STEP changed during inspection");
        }
        else if(op=="kernel_info"){result=V::makeObj();result.set("kernel",V::makeStr("OCCT"));result.set("version",V::makeStr(kernelVersion()));result.set("language",V::makeStr("C++"));result.set("python_required",V::makeBool(false));}
        else if(op=="export_brep"){
            auto shape=readBrep(text("brep_base64"));auto path=fs::u8path(text("path")),pending=path;pending+=".pending.step";writeStep(shape,pending);auto returned=readStep(pending);
            bool solid=!shapes(shape,TopAbs_SOLID).empty();double error=solid?symmetricDifference(shape,returned):0,tol=std::max(1e-6,std::abs(properties(shape))*1e-8);
            auto before=bounds(shape),after=bounds(returned);double boundError=0;for(int i=0;i<6;++i)boundError=std::max(boundError,std::abs(before[i]-after[i]));double areaError=std::abs(properties(shape,'a')-properties(returned,'a'));
            if(error>tol||boundError>1e-6||areaError>std::max(1e-6,properties(shape,'a')*1e-8)||shapes(shape,TopAbs_SOLID).size()!=shapes(returned,TopAbs_SOLID).size())throw std::runtime_error("STEP roundtrip changed native geometry");
            result=shapeReport(returned,solid);replaceFile(pending,path);result.set("step_roundtrip_difference_mm3",solid?V::makeNum(error):V{});result.set("step_roundtrip_area_error_mm2",V::makeNum(areaError));result.set("step_roundtrip_bounds_error_mm",V::makeNum(boundError));result.set("step_sha256",V::makeStr(sha256(readFile(path))));result.set("path",V::makeStr(text("path")));auto sidecar=path;sidecar.replace_extension(".validation.json");writeFile(sidecar,result.dump());
        }else throw std::runtime_error("Unknown native worker operation");
        writeFile(resultPath,result.dump());return 0;
    }catch(const Standard_Failure& e){V error=V::makeObj();error.set("error",V::makeStr(e.GetMessageString()?e.GetMessageString():"OCCT failure"));writeFile(resultPath,error.dump());std::cerr<<error.dump()<<'\n';return 1;}
    catch(const std::exception& e){V error=V::makeObj();error.set("error",V::makeStr(e.what()));writeFile(resultPath,error.dump());std::cerr<<error.dump()<<'\n';return 1;}
}

#ifdef _WIN32
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;return runWorker(fs::path(argv[1]),fs::path(argv[2]));}
#else
int main(int argc,char** argv){if(argc!=3)return 2;return runWorker(fs::u8path(argv[1]),fs::u8path(argv[2]));}
#endif
