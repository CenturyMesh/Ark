#include "ark/direct_preview.hpp"
#include "ark/agent_activity.hpp"
#include <iostream>
#include <stdexcept>
#include <condition_variable>

using namespace ark;
using V=arkjson::Value;
static void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
static V parse(const char* text){V value;require(arkjson::parse(text,value),"fixture JSON");return value;}
static V call(AgentSession& session,const char* tool,V args){if(std::string(tool)=="doc_apply")args.set("expected_revision",V::makeNum((double)session.revision));auto result=session.call(tool,args);if(result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());return *result.find("structuredContent");}
static V editHeight(int height){V command=parse(R"({"op":"feature.edit","id":1,"p":{}})");V p=V::makeObj();p.set("dz",V::makeStr(std::to_string(height)));command.set("p",p);V commands=V::makeArr();commands.arr.push_back(command);return commands;}
static DirectPreviewWorker::Result waitResult(DirectPreviewWorker& worker){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);while(std::chrono::steady_clock::now()<until){if(auto result=worker.take())return std::move(*result);std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("worker result timeout");}
int main(){try{
    AgentSession live;call(live,"doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"box","name":"Source","p":{"dx":"40","dy":"30","dz":"20"}}]})"));
    const auto original=documentToJson(live.document);const auto revision=live.revision;
    std::mutex mutex;std::condition_variable condition;bool started=false,released=false;int hooks=0;
    DirectPreviewWorker worker([&]{std::unique_lock lock(mutex);if(++hooks==1){started=true;condition.notify_one();condition.wait(lock,[&]{return released;});}});
    const u64 gesture=worker.begin(DirectPreviewWorker::capture(live.document),revision,12);
    worker.request(editHeight(21),1,Mat4::identity());
    {std::unique_lock lock(mutex);require(condition.wait_for(lock,std::chrono::seconds(5),[&]{return started;}),"worker never started");}
    u64 latest=0;for(int n=22;n<=150;++n)latest=worker.request(editHeight(n),1,Mat4::identity());
    require(documentToJson(live.document)==original&&live.revision==revision,"pointer submissions mutated live document");
    {std::lock_guard lock(mutex);released=true;}condition.notify_one();
    auto result=waitResult(worker);require(result.error.empty()&&result.receipt&&result.gesture==gesture&&result.generation==latest&&result.epoch==12,"latest receipt or identity lost");
    auto stats=worker.statistics();require(stats.submitted==130&&stats.evaluated==2&&stats.coalesced==128&&stats.discarded==1,"obsolete pointer requests were evaluated/published");
    require(std::abs(result.mesh.computeBBox().mx.z-150)<1e-7&&!result.fill.empty(),"preview is not latest geometry");
    const auto id=result.receipt->id();bool rejected=false;try{live.adoptPreparedCandidate(std::move(*result.receipt),revision+1);}catch(const std::exception&){rejected=true;}require(rejected,"wrong revision adopted");
    live.adoptPreparedCandidate(std::move(*result.receipt),revision);
    rejected=false;try{live.adoptPreparedCandidate(std::move(*result.receipt),revision);}catch(const std::exception&){rejected=true;}require(rejected,"receipt consumed twice");
    V apply=V::makeObj();apply.set("candidate_id",V::makeStr(id));auto saved=call(live,"doc_apply",apply);
    require(saved.find("applied_prepared")->b&&!saved.find("geometry_recomputed")->b&&saved.find("features_evaluated")->num==0,"commit reevaluated validated geometry");
    require(live.revision==revision+1&&live.document.features.size()==1&&live.document.find(1)->get("dz")=="150","commit changed part identity");
    Document roundtrip;std::string error;require(documentFromJson(documentToJson(live.document),roundtrip,&error),"prepared commit history did not roundtrip");
    call(live,"doc_undo",V::makeObj());require(live.document.find(1)->get("dz")=="20","undo did not restore source height");
    worker.begin(DirectPreviewWorker::capture(live.document),live.revision,13);worker.request(editHeight(45),1,Mat4::identity());result=waitResult(worker);require(result.receipt!=nullptr,"new gesture not evaluated");
    // Same public revision with a different source must still reject a transfer.
    live.document.find(1)->p["dx"]="41";rejected=false;try{live.adoptPreparedCandidate(std::move(*result.receipt),live.revision);}catch(const std::exception&){rejected=true;}require(rejected&&live.document.find(1)->get("dz")=="20","source fingerprint guard missing");
    live.document.find(1)->p["dx"]="40"; // Restore the intentionally corrupted fixture before later history commits.
    worker.begin(DirectPreviewWorker::capture(live.document),live.revision,14);worker.request(editHeight(-5),1,Mat4::identity());result=waitResult(worker);require(!result.error.empty()&&!result.receipt,"invalid geometry retained an apply receipt");
    worker.begin(DirectPreviewWorker::capture(live.document),live.revision,15);worker.request(editHeight(60),1,Mat4::identity());worker.cancel();
    // A second valid gesture is also the completion barrier for cancelled work.
    worker.begin(DirectPreviewWorker::capture(live.document),live.revision,16);worker.request(editHeight(65),1,Mat4::identity());result=waitResult(worker);require(result.epoch==16&&result.receipt&&result.mesh.computeBBox().mx.z==65,"cancelled epoch surfaced as current");
    V audit=V::makeObj(),actor=V::makeObj();actor.set("kind",V::makeStr("agent"));actor.set("id",V::makeStr("audit-fixture"));audit.set("actor",actor);call(live,"doc_info",audit);auto ledger=live.document.agentActivity;
    auto captured=DirectPreviewWorker::capture(live.document);require(!captured->agentActivity,"captured snapshot shares mutable activity ledger");
    worker.begin(captured,live.revision,17);worker.request(editHeight(70),1,Mat4::identity());call(live,"doc_info",audit);const auto actions=ledger->entries.size();result=waitResult(worker);require(result.receipt&&!result.receipt->document().agentActivity,"candidate copied live audit state");
    const auto retainedId=result.receipt->id();live.adoptPreparedCandidate(std::move(*result.receipt),live.revision);apply=V::makeObj();apply.set("candidate_id",V::makeStr(retainedId));call(live,"doc_apply",apply);require(live.document.agentActivity==ledger&&ledger->entries.size()==actions,"commit lost newer live activity");
    std::cout<<"Direct preview: coalescing, source/revision/epoch, invalidity, receipt reuse, history and cancellation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
