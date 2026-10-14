#include "ark/cad_import.hpp"
#include "ark/native_backend.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
V request(const char* operation,const std::string& path){V value=V::makeObj();value.set("operation",V::makeStr(operation));value.set("path",V::makeStr(path));return nativeWorkerRequest(value);}
const V& required(const V& object,const char* key){auto p=object.find(key);if(!p)throw std::runtime_error(std::string("CAD import result missing ")+key);return *p;}
}
V inspectCadFile(const std::string& path){return request("inspect_cad",path);}
V inspectImportedCad(const Feature& feature,const Body* candidate){
    std::string error;if(!validateCadPackageAuthoring(feature,error))throw std::runtime_error(error);
    auto operation=feature.nativeSpec.find("op");if(!operation||operation->t!=V::Str||operation->str!="imported_cad")throw std::runtime_error("CAD source inspection requires an imported_cad feature");
    V value=V::makeObj();value.set("operation",V::makeStr("inspect_imported_cad"));value.set("package",required(feature.nativeSpec,"package"));
    if(candidate)value.set("candidate_brep",required(candidate->nativeData,"brep_base64"));
    return nativeWorkerRequest(value);
}
V buildCadImport(const std::string& path,const std::string& name){
    V result=request("import_cad",path),native=V::makeObj(),command=V::makeObj();
    native.set("op",V::makeStr("imported_cad"));native.set("package",required(result,"package"));
    command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("native_solid"));
    command.set("name",V::makeStr(name.empty()?required(result,"source_name").str:name));command.set("native",native);
    result.set("command",command);
    // Tool receipts stay compact; the command carries the portable source package.
    result.obj.erase(std::remove_if(result.obj.begin(),result.obj.end(),[](const auto& item){return item.first=="package"||item.first=="brep_base64"||item.first=="mesh"||item.first=="topology";}),result.obj.end());
    return result;
}
bool validateCadPackageAuthoring(const Feature& feature,std::string& error){
    auto op=feature.nativeSpec.find("op");const bool nativeImport=op&&op->t==V::Str&&op->str=="imported_cad";
    if(!nativeImport&&feature.get("op")!="imported_cad")return true;
    auto fail=[&](const char* reason){error=reason;return false;};
    if(!nativeImport)return fail("Imported CAD requires an explicit native imported_cad operation");
    if(feature.type!=FeatureType::NativeSolid||!feature.inputs.empty())return fail("Imported CAD must be a source-owned native_solid without feature inputs");
    for(const auto& item:feature.p)if(item.first!="density")return fail("Imported CAD geometry is immutable; reconstruct editable native features to alter it");
    for(const auto& item:feature.nativeSpec.obj)if(item.first!="op"&&item.first!="package")return fail("Unknown imported CAD authoring field");
    auto package=feature.nativeSpec.find("package");if(!package||package->t!=V::Obj)return fail("Imported CAD requires its embedded exact source package");
    auto format=package->find("format"),version=package->find("version"),sourceFormat=package->find("source_format");
    if(!format||format->t!=V::Str||format->str!="ark-source-cad"||!version||version->t!=V::Num||version->num!=1||!sourceFormat||sourceFormat->t!=V::Str||(sourceFormat->str!="step"&&sourceFormat->str!="iges"))return fail("Unsupported imported CAD package");
    for(const char* key:{"source_name","source_base64","source_sha256","brep_base64","brep_sha256"}){auto v=package->find(key);if(!v||v->t!=V::Str||v->str.empty()||v->str.size()>96u*1024u*1024u)return fail("Imported CAD source/hash/B-rep field missing or oversized");}
    for(const char* key:{"source_sha256","brep_sha256"}){auto& hash=package->find(key)->str;if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)return fail("Imported CAD requires lowercase SHA-256 digests");}
    if(package->find("source_name")->str.size()>4096)return fail("Imported CAD source name is oversized");
    return true;
}
std::string cadImportArkitfyPrompt(const V& receipt,u64 featureId){
    std::ostringstream prompt;
    prompt<<"Arkitfy the imported exact CAD reference feature "<<featureId<<" into an editable ARK parametric design. The user selected Arkitfy, authorizing reconstruction. Keep the imported source intact as a reference until an equivalent reconstruction has passed validation. The source feature and its original exchange bytes are the immutable geometric authority.\n";
    prompt<<"Read agent_onboard overview, doc_info detail design and native_inspect for that source at the current revision. Inspect exact faces, edge curves, dimensions, analytic surface axes, bounds and body census; source indices are digest-bound observations, not inferred manufacturing intent. Source inspection receipt:\n";
    V evidence=V::makeObj();for(const char* key:{"source_name","source_sha256","source_format","report","census","units","recognition"})if(auto value=receipt.find(key))evidence.set(key,*value);prompt<<evidence.dump()<<"\n";
    prompt<<"Build coherent design intent: datums/origins, minimal fully driven sketches, named driving parameters and meaningful native feature dependencies. Prefer sketch/extrude or revolve for stock, explicit hole features for openings, then patterns and finishing features when the measured geometry supports them. A measured box/cylinder recipe is only an equivalence candidate; do not claim the original design history or fabrication route. Reconstruct every imported solid independently, preserve surface bodies, and keep unresolved geometry as an explicit exact reference. Do not invent hidden geometry or replace exact geometry with mesh approximations. Arbitrary/freeform sources require deliberate exact inspection and supported authoring; report partial reconstruction when necessary.\n";
    prompt<<"Use doc_preview and atomic doc_apply with a freshly observed expected_revision. After reconstruction call cad_inspect{id:"<<featureId<<",compare_id:<reconstructed exact feature>} to measure closed-solid equivalence in the same coordinate frame: solid count, closedness, area, bounds and exact Boolean symmetric difference (both directions). Compare relevant exact dimensions too. Multi-body source reconstruction must retain every solid and use a compound candidate for whole-source proof. Surface equivalents additionally require bidirectional exact surface/trim evidence; this comparison currently cannot qualify free surface equivalence, and bounds/area alone are insufficient. Record numerical errors and unresolved checks. Preserve all failures; never widen tolerances, drop bodies, silently rebase or assert equivalence from screenshots. Distinguish geometric equivalence from engineering or manufacturing qualification.\n";
    prompt<<"Only after equivalent native geometry has been proven, suppress the imported reference and intermediate tools so the model/assembly/BOM counts each physical body once; retain the source feature/history. If a proof is incomplete, keep the source active and reconstructed candidates suppressed, identify the unresolved bodies and report partial status. No drawings are authorized. Report created editable features, measured equivalence evidence, and remaining limitations.";
    return prompt.str();
}
}
