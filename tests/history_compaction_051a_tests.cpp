#include "ark/agent.hpp"
#include "ark/iteration_history.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
V parse(const std::string& text){V result;check(arkjson::parse(text,result),"fixture parses");return result;}
V result(const V& response){check(!response.find("isError")->b,response.dump());return *response.find("structuredContent");}
V edit(ark::AgentSession& session,int number){V args=parse(R"({"commands":[{"op":"param.set","name":"length","expr":"1"}]})");args.obj.front().second.arr.front().set("expr",V::makeStr(std::to_string(number)));args.set("expected_revision",V::makeNum(session.revision));return result(session.call("doc_apply",args));}
V compactArgs(ark::AgentSession& session,const fs::path& file,int keep){V args=V::makeObj();args.set("archive_path",V::makeStr(file.string()));args.set("keep_recent",V::makeNum(keep));args.set("confirm",V::makeBool(true));args.set("expected_revision",V::makeNum(session.revision));return args;}
std::string bytes(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
V& member(V& value,const char* key){for(auto& field:value.obj)if(field.first==key)return field.second;throw std::runtime_error(key);}
}
int main(){try{
    const fs::path directory=fs::temp_directory_path()/("ark-compaction-synthetic-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(directory);
    ark::AgentSession session;result(session.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Retained geometry","p":{"dx":"5","dy":"6","dz":"7"}}]})")));
    for(int i=1;i<=511;++i)edit(session,i);
    const auto original=ark::documentToJson(session.document);const auto originalAuthoring=ark::iterationAuthoring(session.document).dump();const auto revision=session.revision;
    auto args=compactArgs(session,directory/"first.ark",16);auto stale=args;stale.set("expected_revision",V::makeNum(0));check(session.call("history_compact",stale).find("isError")->b&&!fs::exists(directory/"first.ark"),"stale rejection precedes archive writes");
    auto unconfirmed=args;unconfirmed.set("confirm",V::makeBool(false));check(session.call("history_compact",unconfirmed).find("isError")->b,"explicit confirmation required");
    auto badPath=compactArgs(session,directory/"missing"/"archive.ark",16);check(session.call("history_compact",badPath).find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==original,"failed archive leaves source unchanged");
    const auto receipt=result(session.call("history_compact",args));check(session.revision==revision+1&&ark::iterationAuthoring(session.document).dump()==originalAuthoring,"compaction retains exact authoring and revision advances");
    check(receipt.find("compaction")->find("nodes_retained")->num==16&&receipt.find("compaction")->find("undo_retained")->num==16,"explicit recent range retained");
    const auto archiveBytes=bytes(directory/"first.ark");check(ark::iterationSha256(archiveBytes)==receipt.find("archive")->find("file_sha256")->str,"receipt fingerprints real binary archive bytes");
    ark::Document archived;std::string error;check(ark::loadDocument((directory/"first.ark").string().c_str(),archived,&error)&&ark::documentToJson(archived)==original,"full 512-node archive reopens identically");
    auto collision=compactArgs(session,directory/"first.ark",0);const auto beforeCollision=ark::documentToJson(session.document);check(session.call("history_compact",collision).find("isError")->b&&bytes(directory/"first.ark")==archiveBytes&&ark::documentToJson(session.document)==beforeCollision,"archive never overwritten");
    check(ark::saveDocument(session.document,(directory/"compacted.ark").string().c_str()),"compacted file saves");ark::AgentSession reopened;check(ark::loadDocument((directory/"compacted.ark").string().c_str(),reopened.document,&error),error);reopened.revision=1000;
    for(int i=0;i<16;++i)result(reopened.call("doc_undo",V::makeObj()));check(reopened.document.getParam("length")==495,"retained undo range survives reopen");check(reopened.call("doc_undo",V::makeObj()).find("isError")->b,"undo stops at explicit compaction boundary");result(reopened.call("doc_redo",V::makeObj()));check(reopened.document.getParam("length")==496,"redo works for post-compaction undo");
    // Continue beyond the former project-wide 512-node stop, then compact again.
    for(int i=512;i<=600;++i)edit(session,i);check(session.document.getParam("length")==600&&session.document.features.size()==1,"continued edits preserve geometry authoring");
    const auto second=result(session.call("history_compact",compactArgs(session,directory/"second.ark",4)));check(second.find("compaction")->find("nodes_retained")->num==4,"repeat compaction has bounded latest receipt");
    ark::Document previous;check(ark::loadDocument((directory/"second.ark").string().c_str(),previous,&error)&&previous.iterationHistory.find("compaction")->find("archive")->find("file_sha256")->str==ark::iterationSha256(archiveBytes),"archive chain retains preceding archive lineage");
    auto tampered=parse(ark::documentToJson(session.document));member(member(tampered,"iteration_history"),"compaction").set("policy",V::makeStr("silent pruning"));check(!ark::documentFromJson(tampered.dump(),previous,&error),"receipt tampering rejects");
    tampered=parse(ark::documentToJson(session.document));auto& journal=member(tampered,"iteration_history");journal.obj.erase(std::remove_if(journal.obj.begin(),journal.obj.end(),[](const auto& p){return p.first=="compaction";}),journal.obj.end());check(!ark::documentFromJson(tampered.dump(),previous,&error),"missing receipt rejects");
    tampered=parse(ark::documentToJson(session.document));tampered.set("version",V::makeNum(49));check(!ark::documentFromJson(tampered.dump(),previous,&error),"legacy format cannot silently accept compacted history");
    // Restore archive through the public tool; its complete old branch set is usable.
    ark::AgentSession restore;V load=V::makeObj();load.set("path",V::makeStr((directory/"first.ark").string()));result(restore.call("doc_load",load));check(restore.document.iterationHistory.find("nodes")->arr.size()==512,"public doc_load restores full archive");
    // Free archive capacity explicitly before further mutation at the old bound.
    result(restore.call("history_compact",compactArgs(restore,directory/"restored-full.ark",8)));result(restore.call("doc_undo",V::makeObj()));check(restore.document.getParam("length")==510,"archive restore + explicit compaction recovers old undo");
    ark::AgentSession branches;const auto first=edit(branches,1);edit(branches,2);edit(branches,3);V pick=V::makeObj();pick.set("id",*first.find("iteration_id"));pick.set("expected_revision",V::makeNum(branches.revision));result(branches.call("iteration_restore",pick));
    const auto branchOriginal=ark::documentToJson(branches.document);const auto branchReceipt=result(branches.call("history_compact",compactArgs(branches,directory/"branches.ark",8)));
    check(branchReceipt.find("compaction")->find("archived_branch_heads")->num==1&&branchReceipt.find("compaction")->find("undo_retained")->num==0,"branch compaction honestly bounds cross-branch undo without skipping target");
    check(ark::loadDocument((directory/"branches.ark").string().c_str(),previous,&error)&&ark::documentToJson(previous)==branchOriginal,"archive retains alternate branch topology");
    result(branches.call("history_compact",compactArgs(branches,directory/"baseline.ark",0)));check(branches.document.iterationHistory.find("nodes")->arr.empty()&&branches.document.getParam("length")==1,"zero-retention keeps full current state as baseline");edit(branches,7);
    // A full archive must also recover an intermediate state on an old branch,
    // which is not an ancestor of its active head.
    ark::AgentSession fullBranch;result(fullBranch.call("doc_apply",parse(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Branch geometry","p":{"dx":"5","dy":"6","dz":"7"}}]})")));
    const auto branchA=edit(fullBranch,1),branchB=edit(fullBranch,2);edit(fullBranch,3);pick.set("id",*branchA.find("iteration_id"));pick.set("expected_revision",V::makeNum(fullBranch.revision));result(fullBranch.call("iteration_restore",pick));
    for(int i=4;i<=510;++i)edit(fullBranch,i);check(fullBranch.document.iterationHistory.find("nodes")->arr.size()==512,"branched fixture reaches exact capacity");
    check(ark::saveDocument(fullBranch.document,(directory/"full-branches.ark").string().c_str()),"full branched archive saves");ark::AgentSession recoveredBranch;load.set("path",V::makeStr((directory/"full-branches.ark").string()));result(recoveredBranch.call("doc_load",load));
    const auto branchAuthoring=ark::iterationAuthoring(recoveredBranch.document).dump();auto retain=compactArgs(recoveredBranch,directory/"full-branches-copy.ark",0);V selected=V::makeArr();selected.arr.push_back(*branchB.find("iteration_id"));selected.arr.push_back(*branchB.find("iteration_id"));selected.arr.push_back(*recoveredBranch.document.iterationHistory.find("head_id"));retain.set("retain_ids",selected);
    const auto selectedReceipt=result(recoveredBranch.call("history_compact",retain));check(ark::iterationAuthoring(recoveredBranch.document).dump()==branchAuthoring&&selectedReceipt.find("compaction")->find("selected_extra_nodes")->num==1,"selected old branch retained without changing authoring or duplicating nodes");
    std::string selectedId;for(const auto& row:selectedReceipt.find("compaction")->find("retained_ids")->arr)if(row.find("original_id")->str==branchB.find("iteration_id")->str)selectedId=row.find("id")->str;check(!selectedId.empty(),"selected branch receipt maps old id");
    pick.set("id",V::makeStr(selectedId));pick.set("expected_revision",V::makeNum(recoveredBranch.revision));result(recoveredBranch.call("iteration_restore",pick));check(recoveredBranch.document.getParam("length")==2&&recoveredBranch.document.features.size()==1&&recoveredBranch.document.features[0].name=="Branch geometry","full archive arbitrary old branch state restores correct authoring and geometry");
    auto invalid=compactArgs(recoveredBranch,directory/"invalid-target.ark",0);selected.arr={V::makeStr("missing-iteration")};invalid.set("retain_ids",selected);const auto unchanged=ark::documentToJson(recoveredBranch.document);check(recoveredBranch.call("history_compact",invalid).find("isError")->b&&ark::documentToJson(recoveredBranch.document)==unchanged&&!fs::exists(directory/"invalid-target.ark"),"unknown retained id fails without publication or mutation");
    fs::remove_all(directory);std::cout<<"history compaction 051A tests passed: 601 edits, full branched archive recovery, reopen, undo, failures, lineage\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
