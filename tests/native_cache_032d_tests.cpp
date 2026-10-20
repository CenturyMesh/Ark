#include "ark/model.hpp"
#include "ark/native_backend.hpp"
#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;namespace fs=std::filesystem;size_t checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
const V& field(const V& value,const char* key){const auto* result=value.find(key);if(!result)throw std::runtime_error(std::string("Missing ")+key);return *result;}
V box(double width){V model=V::makeObj();model.set("op",V::makeStr("box"));model.set("width",V::makeNum(width));model.set("depth",V::makeNum(4));model.set("height",V::makeNum(5));V request=V::makeObj();request.set("operation",V::makeStr("model_solid"));request.set("request",model);return request;}
void volume(const V& result,double expected){const auto& report=field(result,"report");check(field(report,"brep_valid").b,"current exact B-rep valid");check(std::abs(field(report,"volume_mm3").num-expected)<1e-8,"current exact volume matches request");}
V decoded(const ark::DocumentDerivedPayload& record){V result;check(arkjson::parse(record.bytes.substr(std::string("ARK-NATIVE-CACHE1\n").size()+130),result),"saved result parses");return result;}
ark::DocumentDerivedPayload encoded(std::string key,V result){const auto payload=result.dump();return {key,"ARK-NATIVE-CACHE1\n"+key+"\n"+ark::iterationSha256(payload)+"\n"+payload};}
void erase(V& value,const char* key){value.obj.erase(std::remove_if(value.obj.begin(),value.obj.end(),[&](const auto& item){return item.first==key;}),value.obj.end());}
double counter(const char* name){return field(ark::nativeKernelInfo(),name).num;}
#ifdef _WIN32
struct Environment {
    std::wstring name,previous;bool existed;
    Environment(const wchar_t* key,const std::wstring& value):name(key){const auto* old=_wgetenv(key);existed=old!=nullptr;if(old)previous=old;set(value);}
    void set(const std::wstring& value){check(_wputenv_s(name.c_str(),value.c_str())==0,"test environment");}
    ~Environment(){_wputenv_s(name.c_str(),existed?previous.c_str():L"");}
};
struct Workspace {
    fs::path path;size_t index=0;
    Workspace(){for(int i=0;i<100;++i){auto candidate=fs::temp_directory_path()/("ark-native-portable-032d-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));if(fs::create_directory(candidate)){path=fs::absolute(candidate);return;}}throw std::runtime_error("test workspace");}
    ~Workspace(){std::error_code error;fs::remove_all(path,error);}
    void fresh(Environment& directory){auto child=path/std::to_string(++index);fs::create_directory(child);directory.set(child.wstring());}
};
struct Budget {int old=ark::setNativeThreadBudget(1);~Budget(){ark::setNativeThreadBudget(old);}};
void run(){
    Workspace workspace;Environment directory(L"ARK_NATIVE_CACHE_DIRECTORY",workspace.path.wstring()),enabled(L"ARK_NATIVE_RESULT_CACHE",L"1");Budget budget;
    const auto first=ark::nativeWorkerRequest(box(3));volume(first,60);
    ark::Document source;ark::Body body;body.nativeData=first;source.evalBodies.push_back(std::move(body));
    const auto records=ark::nativeDocumentResults(source);check(records.size()==1,"one portable saved result");
    const auto saved=decoded(records.front());const auto provenance=field(saved,"native_cache_provenance");
    check(field(provenance,"schema").num==1&&field(provenance,"resolved_request_sha256").str.size()==64&&field(provenance,"producer_sha256").str.size()==64,"saved request and original producer identity explicit");
    auto restore=[&](ark::DocumentDerivedPayload record,double width){ark::Document pending;auto status=ark::stageNativeDocumentResults(pending,{record});check(field(status,"entries_staged").num==1,"portable entry stages");ark::NativeDocumentResultScope scope(pending.pendingNativeGeometry);return ark::nativeWorkerRequest(box(width));};
    workspace.fresh(directory);ark::setNativeThreadBudget(2);auto before=counter("native_document_restores");
    const auto threaded=restore(records.front(),3);volume(threaded,60);check(counter("native_document_restores")==before+1,"changed scheduling budget restores through validation");
    check(field(field(threaded,"report"),"snapshot_validation").str=="current_exact_brep_measurements_and_references","portable match runs current kernel validation");
    check(field(field(threaded,"native_cache_provenance"),"resolved_request_sha256").str==field(provenance,"resolved_request_sha256").str,"scheduling excluded from resolved request identity");
    workspace.fresh(directory);V foreign=saved;V origin=provenance;origin.set("producer_sha256",V::makeStr(std::string(64,'a')));foreign.set("native_cache_provenance",origin);before=counter("native_document_restores");
    const auto imported=restore(encoded(std::string(64,'b'),foreign),3);volume(imported,60);check(counter("native_document_restores")==before+1,"different producer restores only through fresh native validation");check(field(imported,"native_snapshot_source_producer_sha256").str==std::string(64,'a'),"source producer provenance retained separately");check(field(field(imported,"native_cache_provenance"),"producer_sha256").str==field(provenance,"producer_sha256").str,"current producer owns regenerated evidence");
    workspace.fresh(directory);before=counter("native_document_restores");volume(restore(records.front(),7),140);check(counter("native_document_restores")==before,"changed geometric request cannot restore old geometry");
    workspace.fresh(directory);V broken=saved;broken.set("brep_base64",V::makeStr("broken"));before=counter("native_document_restores");const auto rejected=counter("native_document_rejections");volume(restore(encoded(std::string(64,'c'),broken),3),60);check(counter("native_document_restores")==before&&counter("native_document_rejections")==rejected+1,"corrupt B-rep with valid transport digest rejects then regenerates");
    workspace.fresh(directory);V legacy=saved;erase(legacy,"native_cache_provenance");before=counter("native_document_restores");volume(restore(encoded(std::string(64,'d'),legacy),3),60);check(counter("native_document_restores")==before,"legacy opaque unmatched key requires replay");
    workspace.fresh(directory);ark::setNativeThreadBudget(1);before=counter("native_document_restores");volume(restore(encoded(records.front().key,legacy),3),60);check(counter("native_document_restores")==before+1,"legacy exact producer/request key remains compatible");
    V malformed=saved;origin=provenance;origin.set("schema",V::makeNum(2));malformed.set("native_cache_provenance",origin);ark::Document discarded;const auto status=ark::stageNativeDocumentResults(discarded,{encoded(records.front().key,malformed)});check(field(status,"entries_staged").num==0&&field(status,"entries_discarded").num==1,"unknown provenance schema rejects rather than downgrading to legacy");

    // Three original inputs need two boolean steps, executed by one worker
    // request; only the final feature body needs persistence. Restore must not
    // reconstruct its prefix.
    workspace.fresh(directory);V sequence=V::makeObj(),sequenceRequest=V::makeObj(),pieces=V::makeArr();
    for(int width=2;width<=4;++width){const auto part=ark::nativeWorkerRequest(box(width));V piece=V::makeObj();piece.set("brep_base64",field(part,"brep_base64"));piece.set("topology",field(part,"topology"));piece.set("namespace",V::makeStr("feature."+std::to_string(width)));pieces.arr.push_back(piece);}
    sequenceRequest.set("schema",V::makeNum(1));sequenceRequest.set("op",V::makeStr("union"));sequenceRequest.set("pieces",pieces);sequence.set("operation",V::makeStr("model_boolean_sequence"));sequence.set("request",sequenceRequest);
    // Reference: the same steps as separate native_solid requests.
    auto pairwise=[&]{const auto& inputs=field(field(sequence,"request"),"pieces");V current=inputs.arr.front();for(size_t i=1;i<inputs.arr.size();++i){V model=V::makeObj(),request=V::makeObj();model.set("op",field(field(sequence,"request"),"op"));model.set("input_brep",field(current,"brep_base64"));model.set("input_topology",field(current,"topology"));model.set("tool_brep",field(inputs.arr[i],"brep_base64"));model.set("tool_topology",field(inputs.arr[i],"topology"));model.set("tool_namespace",field(inputs.arr[i],"namespace"));request.set("operation",V::makeStr("model_solid"));request.set("request",model);current=ark::nativeWorkerRequest(request);}return current;};
    auto semanticRows=[](const V& result){std::string rows;for(const auto& row:field(field(result,"topology"),"references").arr){const auto& name=field(row,"name").str;if(name.find("@ark.")!=std::string::npos)continue;for(const char* key:{"name","kind","status","indices"})rows+=field(row,key).dump();rows+=';';}return rows;};
    const auto reference=pairwise();double hits=counter("native_result_cache_hits");
    const auto combined=ark::nativeBooleanSequenceRequest(sequence);volume(combined,80);check(counter("native_result_cache_hits")==hits,"cold aggregate runs in the worker");
    check(field(combined,"brep_base64").str==field(reference,"brep_base64").str,"one-request sequence produces the pairwise exact B-rep");
    check(!semanticRows(combined).empty()&&semanticRows(combined)==semanticRows(reference),"one-request sequence propagates the pairwise semantic references");
    hits=counter("native_result_cache_hits");const auto combinedHit=ark::nativeBooleanSequenceRequest(sequence);check(counter("native_result_cache_hits")==hits+1&&combinedHit.dump()==combined.dump(),"aggregate local hit returns the exact result");
    ark::Document combinedDocument;ark::Body combinedBody;combinedBody.nativeData=combined;combinedDocument.evalBodies.push_back(std::move(combinedBody));const auto combinedRecords=ark::nativeDocumentResults(combinedDocument);check(combinedRecords.size()==1,"aggregate persists only final result");
    workspace.fresh(directory);ark::setNativeThreadBudget(2);ark::Document pending;ark::stageNativeDocumentResults(pending,combinedRecords);before=counter("native_document_restores");
    {ark::NativeDocumentResultScope scope(pending.pendingNativeGeometry);const auto restored=ark::nativeBooleanSequenceRequest(sequence);volume(restored,80);check(field(field(restored,"report"),"snapshot_validation").str=="current_exact_brep_measurements_and_references","aggregate restore validates final exact geometry");}
    check(counter("native_document_restores")==before+1,"portable aggregate restores once without reconstructing intermediate CSG");
    workspace.fresh(directory);pieces.arr.back().set("namespace",V::makeStr("feature.changed"));sequenceRequest.set("pieces",pieces);sequence.set("request",sequenceRequest);ark::Document edited;ark::stageNativeDocumentResults(edited,combinedRecords);before=counter("native_document_restores");
    {ark::NativeDocumentResultScope scope(edited.pendingNativeGeometry);volume(ark::nativeBooleanSequenceRequest(sequence),80);}
    check(counter("native_document_restores")==before,"changed semantic namespace invalidates aggregate request");
    // A framed tool is placed in the worker exactly as by native_solid transform.
    // The 4 mm box moved 2 mm along X overlaps the 3 mm box: one 6x4x5 solid.
    V framed=pieces;V shifted=V::makeArr();for(double value:{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,2.,0.,0.,1.})shifted.arr.push_back(V::makeNum(value));framed.arr.back().set("frame",shifted);
    sequenceRequest.set("pieces",framed);sequence.set("request",sequenceRequest);volume(ark::nativeBooleanSequenceRequest(sequence),120);

    // 045A: saved geometry is an optional cache. Over capacity, entries are
    // omitted in document order and the authoring save still succeeds.
    ark::Document many;for(double width:{3.,4.,5.}){ark::Body seed;seed.nativeData=ark::nativeWorkerRequest(box(width));many.evalBodies.push_back(std::move(seed));}
    ark::NativeDocumentResultsReport full;const auto all=ark::nativeDocumentResults(many,&full);
    check(all.size()==3&&full.entries==3&&full.omittedEntries==0&&full.bytes==all[0].bytes.size()+all[1].bytes.size()+all[2].bytes.size(),"all saved results fit the default capacity");
    ark::NativeDocumentResultsReport bounded;const auto fitted=ark::nativeDocumentResults(many,&bounded,all[0].bytes.size()+all[1].bytes.size()/2);
    check(fitted.size()==1&&fitted[0].key==all[0].key&&bounded.bytes==all[0].bytes.size()&&bounded.omittedEntries==2&&bounded.omittedBytes==all[1].bytes.size()+all[2].bytes.size(),"byte capacity omits later entries instead of throwing");
    ark::NativeDocumentResultsReport counted;check(ark::nativeDocumentResults(many,&counted,ark::maxDocumentDerivedBytes,2).size()==2&&counted.omittedEntries==1,"entry capacity omits instead of failing");
    V root;check(arkjson::parse(ark::documentToJson(many),root),"fixture document serializes");std::string error;const auto bounds=(workspace.path/"bounded-045a.ark").string();
    check(ark::saveDocumentContainer(root,bounds.c_str(),&error,nullptr,&fitted),"bounded derived subset saves");
    ark::DocumentDerivedPayloads reopened;V loaded;check(ark::loadDocumentContainer(bounds.c_str(),loaded,&error,&reopened)&&reopened.size()==1&&reopened.front().key==all[0].key,"bounded derived subset reopens");
    ark::DocumentSaveReport receipt;check(ark::saveDocument(many,(workspace.path/"report-045a.ark").string().c_str(),&receipt)&&receipt.error.empty()&&receipt.derivedGeometry&&receipt.derivedEntries==3&&receipt.derivedOmittedEntries==0&&receipt.derivedCapacityBytes==ark::maxDocumentDerivedBytes,"save receipt reports saved geometry");
    check(!ark::saveDocument(many,(workspace.path/"missing-045a"/"target.ark").string().c_str(),&receipt)&&!receipt.error.empty(),"failed save explains its reason");
}
#endif
}
int main(){try{
#ifdef _WIN32
run();
#endif
std::cout<<checks<<" portable native identity checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
