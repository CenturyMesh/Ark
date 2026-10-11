#include "ark/agent.hpp"
#include "ark/failure_record.hpp"
#include "ark/updates.hpp"
#include "ark/native_backend.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr size_t storeLimit=16*1024*1024,bundleLimit=12*1024*1024;
const V& need(const V& value,const char* key){const auto* field=value.find(key);if(!field)throw std::runtime_error(std::string("Missing failure bundle field: ")+key);return *field;}
std::string text(const V& value,const char* key){const auto& field=need(value,key);if(field.t!=V::Str||field.str.empty())throw std::runtime_error(std::string("Invalid failure text: ")+key);return field.str;}
std::string recordId(){static std::atomic<unsigned long long> sequence{0};static const auto salt=[](){std::random_device r;return ((unsigned long long)r()<<32)^r();}();std::ostringstream out;out<<"failure-"<<std::hex<<salt<<'-'<<++sequence;return out.str();}
bool sameCause(const V& a,const V& b){for(const char* key:{"error_code","operation","command_index","field","observed_solid_count","error"}){const auto* x=a.find(key);const auto* y=b.find(key);if(bool(x)!=bool(y)||(x&&x->dump()!=y->dump()))return false;}return true;}
void canonical(V& value){if(value.t==V::Obj){for(auto& item:value.obj)canonical(item.second);std::sort(value.obj.begin(),value.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(value.t==V::Arr)for(auto& item:value.arr)canonical(item);}
std::string diagnosticRequestKey(V recipe){
    // Actor, motivation and observed revision do not change an authoring recipe.
    // Keep all other fields, including checks, and keep command/array ordering.
    recipe.obj.erase(std::remove_if(recipe.obj.begin(),recipe.obj.end(),[](const auto& entry){return entry.first=="actor"||entry.first=="motivation"||entry.first=="expected_revision"||entry.first=="detail";}),recipe.obj.end());
    canonical(recipe);return iterationSha256(recipe.dump());
}
V attach(V response,const std::string& key,const V& value){auto data=need(response,"structuredContent");data.set(key,value);response.set("structuredContent",data);V content=need(response,"content");for(auto& part:content.arr)if(part.find("type")&&part.find("type")->asStr()=="text"){part.set("text",V::makeStr(data.dump()));break;}response.set("content",content);return response;}
void checkFailureRevision(const V& args,u64 observed){const auto& supplied=need(args,"expected_revision");if(supplied.t!=V::Num||supplied.num!=(double)observed)throw std::runtime_error("Revision conflict; read doc_info with detail design and retry");}
V runtimeEnvironment(bool probeKernel){V environment=V::makeObj();environment.set("iteration_code",V::makeStr(app_iteration_code()));environment.set("runtime_version",V::makeStr(app_version()));V kernel=V::makeObj();kernel.set("available",V::makeBool(false));kernel.set("evidence",V::makeStr(probeKernel?"Native kernel provenance unavailable; not inferred.":"Native kernel not probed at failure capture; version not inferred."));if(probeKernel)try{kernel=nativeKernelInfo();}catch(const std::exception&){}environment.set("kernel",kernel);return environment;}
FailureRecord readBundle(const std::string& path){
    const auto filename=std::filesystem::u8path(path);std::error_code error;const auto bytes=std::filesystem::file_size(filename,error);if(error||bytes>bundleLimit)throw std::runtime_error("Failure bundle is missing or exceeds 12 MiB");
    std::ifstream input(filename,std::ios::binary);std::string raw((size_t)bytes,'\0');input.read(raw.data(),(std::streamsize)raw.size());V bundle;if(!input||input.peek()!=std::char_traits<char>::eof()||!arkjson::parse(raw,bundle)||bundle.t!=V::Obj)throw std::runtime_error("Failure bundle must be complete valid JSON");
    if(text(bundle,"schema")!="ark.failure.v1")throw std::runtime_error("Unsupported failure bundle schema");
    const auto& source=need(bundle,"record");FailureRecord record;record.id=text(source,"id");if(record.id.size()>128)throw std::runtime_error("Invalid failure record identity");record.tool=text(source,"tool");if(record.tool!="doc_apply"&&record.tool!="doc_preview")throw std::runtime_error("Failure replay allows only recorded doc_apply/doc_preview");
    const auto& rev=need(source,"baseline_revision");if(rev.t!=V::Num||!std::isfinite(rev.num)||rev.num<0||rev.num>9007199254740991.||std::floor(rev.num)!=rev.num)throw std::runtime_error("Invalid failure baseline revision");record.revision=(u64)rev.num;
    record.baseline=text(bundle,"baseline");if(record.baseline.size()>maxDocumentBytes)throw std::runtime_error("Failure baseline exceeds document limit");Document checked;std::string reason;if(!documentFromJson(record.baseline,checked,&reason))throw std::runtime_error("Failure baseline invalid: "+reason);
    record.request=need(source,"request");record.replayRequest=need(source,"replay_request");record.error=need(source,"error");if(record.request.t!=V::Obj||record.replayRequest.t!=V::Obj||record.error.t!=V::Obj||!record.error.find("error"))throw std::runtime_error("Failure bundle has invalid command/error custody");
    record.environment=V::makeObj();if(const auto* runtime=bundle.find("runtime_version")){if(runtime->t!=V::Str||runtime->str.size()>128)throw std::runtime_error("Invalid recorded runtime provenance");record.environment.set("runtime_version",*runtime);}if(const auto* iteration=bundle.find("iteration_code")){if(iteration->t!=V::Str||iteration->str.size()>128)throw std::runtime_error("Invalid recorded iteration provenance");record.environment.set("iteration_code",*iteration);}if(const auto* kernel=bundle.find("kernel")){if(kernel->t!=V::Obj)throw std::runtime_error("Invalid recorded kernel provenance");record.environment.set("kernel",*kernel);}record.environment.set("scope",V::makeStr("Exported bundle provenance; native kernel observed at export, not assumed identical at replay."));
    record.bytes=raw.size();return record;
}
}
arkjson::Value AgentSession::captureFailure(const std::string& tool,const V& args,const V& replayArgs,const std::string& baseline,u64 baselineRevision,V response){
    const auto* data=response.find("structuredContent");if(!data)return response;const auto* rejected=response.find("isError");
    V error;if(rejected&&rejected->b)error=*data;else if(tool=="doc_preview"&&data->find("valid")&&!data->find("valid")->b&&data->find("candidate_result"))error=*data->find("candidate_result");else return response;
    FailureRecord record;record.id=recordId();record.baseline=baseline;record.revision=baselineRevision;record.tool=tool;record.request=args;record.replayRequest=replayArgs;record.error=error;record.environment=runtimeEnvironment(false);record.environment.set("scope",V::makeStr("Runtime at failure capture; native kernel not probed."));record.bytes=baseline.size()+args.dump().size()+replayArgs.dump().size()+error.dump().size()+record.environment.dump().size()+512;
    if(record.bytes>storeLimit/2)return attach(std::move(response),"failure_capture",V::makeStr("Record exceeds 8 MiB diagnostic custody limit; original rejection retained, baseline not stored."));
    // call() captures rejection only after restoring the live baseline. Compare
    // its authored inputs directly, avoiding replay/history validation here.
    record.diagnosticBaselineKey=iterationSha256(iterationAuthoring(document).dump());record.diagnosticRequestKey=diagnosticRequestKey(replayArgs);record.bytes+=record.diagnosticBaselineKey.size()+record.diagnosticRequestKey.size();
    size_t used=0;for(const auto& prior:failures_)used+=prior.bytes;while(!failures_.empty()&&(failures_.size()>=8||used+record.bytes>storeLimit)){used-=failures_.front().bytes;failures_.erase(failures_.begin());}
    unsigned repeats=1;std::string previous;for(const auto& old:failures_)if(old.diagnosticBaselineKey==record.diagnosticBaselineKey&&old.tool==tool&&old.diagnosticRequestKey==record.diagnosticRequestKey&&sameCause(old.error,error)){++repeats;previous=old.id;}
    const auto id=record.id;failures_.push_back(std::move(record));response=attach(attach(std::move(response),"failure_id",V::makeStr(id)),"failure_capture",V::makeStr("local_session; explicit failure_export preserves across restart"));response=attach(std::move(response),"repeat_count",V::makeNum(repeats));if(repeats>1){response=attach(std::move(response),"previous_failure_id",V::makeStr(previous));response=attach(std::move(response),"repeated_failure_guidance",V::makeStr("The same command recipe and checks failed against the same authoring baseline, ignoring actor, motivation, revision, usage and history metadata. Inspect failure_info and the owned geometric predicate before changing the recipe; no automatic retry was performed."));}return response;
}
arkjson::Value AgentSession::failureInfo(const V& args) const {
    V out=V::makeObj(),rows=V::makeArr();if(const auto* id=args.find("id")){if(id->t!=V::Str)throw std::runtime_error("failure id must be text");for(const auto& record:failures_)if(record.id==id->str){out.set("record",failureSummary(record,true));out.set("revision",V::makeNum((double)revision));return out;}throw std::runtime_error("Failure record missing or evicted");}
    size_t count=8;if(const auto* limit=args.find("limit")){if(limit->t!=V::Num||limit->num<1||limit->num>8||std::floor(limit->num)!=limit->num)throw std::runtime_error("failure_info limit must be 1..8");count=(size_t)limit->num;}
    for(auto i=failures_.rbegin();i!=failures_.rend()&&rows.arr.size()<count;++i)rows.arr.push_back(failureSummary(*i));out.set("records",rows);out.set("revision",V::makeNum((double)revision));out.set("persistence",V::makeStr("local session; explicit export bundle for restart"));return out;
}
arkjson::Value AgentSession::failureExport(const V& args) const {
    const auto id=text(args,"id"),path=text(args,"path");const auto found=std::find_if(failures_.begin(),failures_.end(),[&](const FailureRecord& r){return r.id==id;});if(found==failures_.end())throw std::runtime_error("Failure record missing or evicted");
    const V exportEnvironment=runtimeEnvironment(true);V bundle=V::makeObj();bundle.set("schema",V::makeStr("ark.failure.v1"));bundle.set("iteration_code",need(found->environment,"iteration_code"));bundle.set("runtime_version",need(found->environment,"runtime_version"));bundle.set("kernel",need(exportEnvironment,"kernel"));bundle.set("kernel_provenance_scope",V::makeStr("observed_at_export"));bundle.set("baseline",V::makeStr(found->baseline));bundle.set("record",failureSummary(*found,true));const auto bytes=bundle.dump();if(bytes.size()>bundleLimit)throw std::runtime_error("Failure bundle exceeds 12 MiB");
    const auto filename=std::filesystem::u8path(path),temporary=std::filesystem::u8path(path+"."+recordId()+".tmp");if(std::filesystem::exists(filename)||std::filesystem::exists(temporary))throw std::runtime_error("Failure export path already exists; choose a new local bundle path");
    {std::ofstream output(temporary,std::ios::binary);output.write(bytes.data(),(std::streamsize)bytes.size());output.flush();if(!output){output.close();std::error_code cleanup;std::filesystem::remove(temporary,cleanup);throw std::runtime_error("Failure bundle write failed; no destination was published");}}
    // Creating a hard link publishes the complete same-filesystem file and
    // fails atomically if the destination exists, including an exists() race.
    // Filesystems without hard-link support reject rather than replacing data.
    std::error_code publish;std::filesystem::create_hard_link(temporary,filename,publish);if(publish){std::error_code cleanup;std::filesystem::remove(temporary,cleanup);throw std::runtime_error("Failure bundle publication failed; no existing destination was replaced");}std::error_code cleanup;std::filesystem::remove(temporary,cleanup);
    V result=V::makeObj();result.set("id",V::makeStr(id));result.set("path",V::makeStr(path));result.set("bytes",V::makeNum(bytes.size()));result.set("contains_private_design",V::makeBool(true));result.set("document_unchanged",V::makeBool(true));result.set("temporary_cleanup_complete",V::makeBool(!cleanup));if(cleanup)result.set("temporary_path",V::makeStr(temporary.string()));return result;
}
arkjson::Value AgentSession::failureReplay(const V& args){
    checkFailureRevision(args,revision);if(bool(args.find("id"))==bool(args.find("path")))throw std::runtime_error("Select one failure id or local exported bundle path");FailureRecord record;
    if(args.find("path"))record=readBundle(text(args,"path"));else{const auto id=text(args,"id");const auto found=std::find_if(failures_.begin(),failures_.end(),[&](const FailureRecord& r){return r.id==id;});if(found==failures_.end())throw std::runtime_error("Failure record missing or evicted");record=*found;}
    const auto* commands=record.replayRequest.find("commands");if(!commands||commands->t!=V::Arr)throw std::runtime_error("Recorded request has no immutable command recipe to replay; original failure remains inspectable");
    AgentSession scratch;scratch.suppressIterationHistory();scratch.suppressFailureCapture();scratch.revision=record.revision;std::string reason;if(!documentFromJson(record.baseline,scratch.document,&reason))throw std::runtime_error("Failure baseline invalid: "+reason);scratch.document.evaluatingBodies=nullptr;
    const std::string liveBefore=documentToJson(document);const auto liveRevision=revision;const auto started=std::chrono::steady_clock::now();const V response=scratch.call(record.tool,record.replayRequest);const V& data=need(response,"structuredContent");bool rejected=need(response,"isError").b;V observed=data;
    if(record.tool=="doc_preview"&&data.find("valid")&&!data.find("valid")->b&&data.find("candidate_result")){rejected=true;observed=*data.find("candidate_result");}
    V current=runtimeEnvironment(true),comparison=V::makeObj();const auto* originalRuntime=record.environment.find("runtime_version");const bool runtimeKnown=originalRuntime&&originalRuntime->t==V::Str;comparison.set("runtime_comparison_known",V::makeBool(runtimeKnown));comparison.set("runtime_changed",runtimeKnown?V::makeBool(originalRuntime->str!=need(current,"runtime_version").str):V{});const auto* oldKernel=record.environment.find("kernel");const auto* oldVersion=oldKernel?oldKernel->find("version"):nullptr;const auto* newVersion=need(current,"kernel").find("version");const bool kernelKnown=oldVersion&&newVersion&&oldVersion->t==V::Str&&newVersion->t==V::Str;comparison.set("kernel_comparison_known",V::makeBool(kernelKnown));comparison.set("kernel_version_changed",kernelKnown?V::makeBool(oldVersion->str!=newVersion->str):V{});comparison.set("scope",V::makeStr("Reported runtime/kernel versions only; same_error does not imply identical toolchain or manufacturing qualification."));
    V out=V::makeObj();out.set("id",V::makeStr(record.id));out.set("recorded_environment",record.environment);out.set("current_environment",current);out.set("environment_comparison",comparison);out.set("baseline_revision",V::makeNum((double)record.revision));out.set("revision",V::makeNum((double)revision));out.set("rejected",V::makeBool(rejected));out.set("same_error",V::makeBool(rejected&&sameCause(record.error,observed)));out.set("observed",observed);out.set("original_error",record.error);out.set("elapsed_ms",V::makeNum(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()));out.set("live_document_unchanged",V::makeBool(liveRevision==revision&&liveBefore==documentToJson(document)));out.set("isolated",V::makeBool(true));return out;
}
}
