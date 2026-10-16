#include "ark/agent.hpp"
#include "ark/iteration_history.hpp"
#include "ark/usage.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <future>
#include <stdexcept>
namespace {
using V=arkjson::Value;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
V json(const std::string& text){V v;check(arkjson::parse(text,v),"test JSON parses");return v;}
V& member(V& v,const char* key){for(auto& p:v.obj)if(p.first==key)return p.second;throw std::runtime_error(key);}
V payload(const V& response){check(response.find("isError")&&!response.find("isError")->b,response.find("structuredContent")?response.find("structuredContent")->dump().c_str():"missing response");return *response.find("structuredContent");}
V apply(ark::AgentSession& s,const std::string& expression,const std::string& name="length"){V args=V::makeObj(),commands=V::makeArr(),op=V::makeObj();args.set("expected_revision",V::makeNum((double)s.revision));op.set("op",V::makeStr("param.set"));op.set("name",V::makeStr(name));op.set("expr",V::makeStr(expression));commands.arr.push_back(op);args.set("commands",commands);return args;}
V restoreArgs(const ark::AgentSession& s,const std::string& id){V args=V::makeObj();args.set("id",V::makeStr(id));args.set("expected_revision",V::makeNum((double)s.revision));return args;}
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
void memoContracts(const ark::Document& source){
    // A fresh thread starts with an empty thread-local memo. No drawing files or
    // native geometry are needed for this comparison of identical inputs.
    auto measured=std::async(std::launch::async,[&](){
        using Clock=std::chrono::steady_clock;std::string error;
        const auto start=Clock::now();check(ark::iterationValidate(source,source.iterationHistory,error),error.c_str());
        const auto cold=Clock::now();check(ark::iterationValidate(source,source.iterationHistory,error),error.c_str());
        const auto warm=Clock::now();
        V bad=source.iterationHistory;member(bad,"nodes").arr.front().set("motivation",V::makeStr("old node corrupted after warm validation"));
        check(!ark::iterationValidate(source,bad,error),"Warm snapshot memo cannot hide old node corruption");
        auto changed=source.authoringCopy();changed.setParam("memo_only_change","99");
        check(!ark::iterationValidate(changed,source.iterationHistory,error),"Warm snapshot memo cannot hide changed head authoring");
        bad=source.iterationHistory;member(bad,"undo_ids").arr.push_back(V::makeStr("missing-node"));
        check(!ark::iterationValidate(source,bad,error),"Warm snapshot memo cannot hide corrupted undo stack");
        check(ark::iterationValidate(source,source.iterationHistory,error),"Rejected history never poisons valid snapshot memo");
        return std::pair{std::chrono::duration<double,std::milli>(cold-start).count(),std::chrono::duration<double,std::milli>(warm-cold).count()};
    }).get();
    ark::Document doc;doc.setParam("memo_version_fixture","3");V state=ark::iterationAuthoring(doc);
    auto baseline=[](const V& value){const auto hash=ark::iterationSha256(value.dump());V h=V::makeObj();h.set("schema",V::makeNum(1));h.set("baseline",value);h.set("baseline_id",V::makeStr("baseline-"+hash));h.set("baseline_sha256",V::makeStr(hash));h.set("head_id",V::makeStr("baseline-"+hash));h.set("nodes",V::makeArr());h.set("blobs",V::makeObj());h.set("undo_ids",V::makeArr());h.set("redo_ids",V::makeArr());return h;};
    std::string error;check(ark::iterationValidate(doc,baseline(state),error),"Canonical baseline warms snapshot memo");
    state.set("version",V::makeNum(2));const auto distinct=baseline(state);
    check(!ark::iterationValidate(doc,distinct,error),"Same authoring with a distinct noncanonical version cannot hit warm memo");
    check(!ark::iterationValidate(doc,distinct,error),"Failed snapshot validation is not memoized as success");
    check(!std::async(std::launch::async,[&](){std::string coldError;return ark::iterationValidate(doc,distinct,coldError);}).get(),"Cold and warm source-version validation agree");
    std::cout<<"synthetic 101-node history validation cold_ms="<<measured.first<<" warm_ms="<<measured.second<<'\n';
}
}
int main(){try{
    check(ark::iterationSha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty vector");
    check(ark::iterationSha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc vector");
    ark::AgentSession session;check(payload(session.call("history_info",V::makeObj())).find("node_count")->num==0,"fresh document has no fabricated history");
    V first=apply(session,"10");first.set("actor",json(R"({"kind":"human","id":"studio"})"));first.set("motivation",V::makeStr("Initial dimension"));
    auto receipt=payload(session.call("doc_apply",first));const std::string a=receipt.find("iteration_id")->str;
    auto history=payload(session.call("history_info",V::makeObj()));const std::string baseline=history.find("baseline_id")->str;check(history.find("nodes")->arr[0].find("actor")->find("kind")->str=="human","human provenance persists");
    V second=apply(session,"20");second.set("actor",json(R"({"kind":"agent","id":"part-designer","role":"Part designer","model":"declared-model"})"));const std::string b=payload(session.call("doc_apply",second)).find("iteration_id")->str;
    const auto unchanged=ark::documentToJson(session.document);const auto rev=session.revision;
    check(session.call("doc_apply",apply(session,"sqrt(-1)" )).find("isError")->b&&session.revision==rev&&ark::documentToJson(session.document)==unchanged,"invalid geometry/expression has no journal node");
    V failed=apply(session,"30");failed.set("checks",json(R"([{"query":{"tool":"doc_info","args":{},"path":"feature_count"},"op":"eq","value":100}])"));check(session.call("doc_apply",failed).find("isError")->b&&ark::documentToJson(session.document)==unchanged,"failed required conditions have no node");
    V preview=payload(session.call("doc_preview",apply(session,"30")));check(preview.find("valid")->b&&session.revision==rev&&ark::documentToJson(session.document)==unchanged,"preview has no durable journal mutation");
    check(session.call("iteration_restore",json("{\"id\":\""+a+"\",\"expected_revision\":0}")).find("isError")->b,"restore requires observed revision");
    std::string error;ark::UsageReceipt usage;usage.id="provider-1";usage.source="studio_assistant";usage.provenance="provider_response";usage.model="declared-model";usage.inputTokens=50;usage.outputTokens=10;usage.totalTokens=60;usage.reportedResponses=1;check(ark::addUsageReceipt(session.document,usage,&error)==ark::UsageRecordResult::Added,"provider receipt added");
    const auto usageSerial=session.document.usage.serial;const auto r=payload(session.call("iteration_restore",restoreArgs(session,a)));const std::string restored=r.find("iteration_id")->str;
    check(session.document.getParam("length")==10&&session.revision==rev+1&&session.document.usage.serial==usageSerial&&ark::usageTotals(session.document).totalTokens==60,"restore preserves monotonic revision and current usage");
    history=payload(session.call("history_info",V::makeObj()));check(history.find("node_count")->num==3&&history.find("branch_heads")->arr.size()==2&&history.find("nodes")->arr[0].find("parent_id")->str==a&&history.find("nodes")->arr[0].find("previous_head_id")->str==b,"restore keeps prior branch and explicit prior active head");
    check(!payload(session.call("doc_get",V::makeObj())).find("document")->find("iteration_history"),"doc_get sends current authoring without old journal payload");
    std::string saved=ark::documentToJson(session.document);check(json(saved).find("version")->num==33,"history with agent activity persists as version 33");ark::AgentSession reopened;check(ark::documentFromJson(saved,reopened.document,&error),"history reopens");reopened.revision=100;
    payload(reopened.call("doc_undo",V::makeObj()));check(reopened.document.getParam("length")==20&&reopened.revision==101,"durable undo works after reopening");saved=ark::documentToJson(reopened.document);ark::AgentSession again;check(ark::documentFromJson(saved,again.document,&error),"undo state reopens");again.revision=200;payload(again.call("doc_redo",V::makeObj()));check(again.document.getParam("length")==10&&again.revision==201&&ark::usageTotals(again.document).totalTokens==60,"durable redo and usage survive another reopening");
    check(payload(again.call("history_info",json(R"({"limit":1})"))).find("nodes")->arr.size()==1&&again.call("history_info",json(R"({"limit":65})")).find("isError")->b,"history list has bounded limits");
    V tampered=json(saved);member(member(tampered,"iteration_history"),"nodes").arr[0].set("motivation",V::makeStr("tampered"));ark::Document destination=again.document;const std::string stable=ark::documentToJson(destination);check(!ark::documentFromJson(tampered.dump(),destination,&error)&&ark::documentToJson(destination)==stable,"metadata/node tampering rejects atomically");
    tampered=json(saved);member(member(tampered,"iteration_history"),"nodes").arr[0].set("parent_id",V::makeStr("missing"));check(!ark::documentFromJson(tampered.dump(),destination,&error),"missing/cyclic parent rejects");
    tampered=json(saved);member(tampered,"params").arr[0].set("expr",V::makeStr("999"));check(!ark::documentFromJson(tampered.dump(),destination,&error),"head authoring tampering rejects");
    ark::AgentSession legacy;legacy.document.setParam("original","7");V legacyFile=json(ark::documentToJson(legacy.document));check(legacyFile.find("version")->num==1,"legacy document retains legacy version before first mutation");payload(legacy.call("doc_apply",apply(legacy,"8","original")));history=payload(legacy.call("history_info",V::makeObj()));payload(legacy.call("iteration_restore",restoreArgs(legacy,history.find("baseline_id")->str)));check(legacy.document.getParam("original")==7,"legacy baseline is first observed real state");
    ark::AgentSession ids;V add=json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"A","p":{"dx":"1","dy":"1","dz":"1"}}]})");const auto added=payload(ids.call("doc_apply",add));const std::string beforeB=added.find("iteration_id")->str;add.set("expected_revision",V::makeNum(ids.revision));member(add,"commands").arr[0].set("name",V::makeStr("B"));const double bid=payload(ids.call("doc_apply",add)).find("created_ids")->arr[0].num;payload(ids.call("iteration_restore",restoreArgs(ids,beforeB)));add.set("expected_revision",V::makeNum(ids.revision));member(add,"commands").arr[0].set("name",V::makeStr("C"));check(payload(ids.call("doc_apply",add)).find("created_ids")->arr[0].num>bid,"branch restores do not reuse allocated feature ids");
    ark::AgentSession many;for(int i=0;i<100;++i){add.set("expected_revision",V::makeNum(many.revision));member(add,"commands").arr[0].set("name",V::makeStr("part "+std::to_string(i)));payload(many.call("doc_apply",add));}check(ark::documentToJson(many.document).size()<180000,"one-part-per-step history uses array deltas rather than full array snapshots");
    V page=payload(many.call("history_info",V::makeObj()));check(page.find("nodes")->arr.size()==64&&page.find("has_more")->b,"history first page is bounded and advertises older nodes");V older=V::makeObj();older.set("before_id",*page.find("next_before_id"));V tail=payload(many.call("history_info",older));check(tail.find("nodes")->arr.size()==36&&!tail.find("has_more")->b&&tail.find("next_before_id")->str.empty(),"pagination makes all retained nodes inspectable without overlap");payload(many.call("doc_apply",apply(many,"1")));check(payload(many.call("history_info",older)).find("nodes")->dump()==tail.find("nodes")->dump(),"history cursors remain stable under new commits");check(many.call("history_info",json(R"({"before_id":"missing"})")).find("isError")->b,"unknown history cursor rejects");
    memoContracts(many.document);
    ark::AgentSession bounded;ark::Feature retained;retained.name="Retained native program";retained.type=ark::FeatureType::NativeSolid;retained.suppressed=true;retained.nativeSpec=json(R"({"op":"box"})");retained.nativeSpec.set("retained_source_bytes",V::makeStr(std::string(ark::maxDocumentBytes-3000,'x')));bounded.document.addFeature(retained);const auto boundedBefore=ark::documentToJson(bounded.document);ark::Document boundedRoundtrip;check(ark::documentFromJson(boundedBefore,boundedRoundtrip,&error),"large suppressed native authoring baseline is a legitimate document");
    V candidate=payload(bounded.call("doc_preview",apply(bounded,"1")));check(candidate.find("valid")->b&&candidate.find("candidate_cached")->b,"large valid prepared candidate cached");V commit=*candidate.find("apply");commit.set("motivation",V::makeStr(std::string(4096,'m')));check(bounded.call("doc_apply",commit).find("isError")->b&&bounded.revision==0&&ark::documentToJson(bounded.document)==boundedBefore,"prepared capacity failure restores document revision and prepared custody");commit.set("motivation",V::makeStr(""));payload(bounded.call("doc_apply",commit));check(bounded.document.getParam("length")==1&&bounded.revision==1,"capacity-rejected prepared candidate remains usable");
    V boundedJson=json(ark::documentToJson(bounded.document));const V& blobs=*boundedJson.find("iteration_history")->find("blobs");check(blobs.obj.size()==1,"large unchanged authoring string stored once in blob dictionary");tampered=boundedJson;member(member(tampered,"iteration_history"),"blobs").obj[0].second.str[0]='y';check(!ark::documentFromJson(tampered.dump(),destination,&error),"large blob tampering rejects");
    tampered=boundedJson;V repeated=member(tampered,"features").arr[0];repeated.set("id",V::makeNum(2));repeated.set("name",V::makeStr("copy"));member(tampered,"features").arr.push_back(repeated);check(tampered.dump().size()<ark::maxDocumentBytes,"repeated-blob attack fixture fits the physical file cap");check(!ark::documentFromJson(tampered.dump(),destination,&error)&&error.find("capacity")!=std::string::npos,"small repeated-blob file cannot expand to unbounded authoring memory");
    const std::filesystem::path path=std::filesystem::temp_directory_path()/"ark-iteration-history-test.ark";const auto pathText=path.string();check(ark::saveDocument(again.document,pathText.c_str()),"journal saves");const std::string onDisk=bytes(path);ark::Document oversized;ark::PlaneCanvas canvas;canvas.name="Oversized";canvas.path=std::string(ark::maxDocumentBytes+1,'x');oversized.canvases.push_back(canvas);check(!ark::saveDocument(oversized,pathText.c_str())&&bytes(path)==onDisk,"oversized save preserves existing file");
    ark::AgentSession deep;V nesting=V::makeNum(1);for(int i=0;i<58;++i){V layer=V::makeObj();layer.set("x",nesting);nesting=std::move(layer);}ark::Feature deepFeature;deepFeature.type=ark::FeatureType::NativeSolid;deepFeature.suppressed=true;deepFeature.nativeSpec=json(R"({"op":"box"})");deepFeature.nativeSpec.set("extension",nesting);deep.document.addFeature(deepFeature);const std::string deepBefore=ark::documentToJson(deep.document);ark::Document deepCheck;check(ark::documentFromJson(deepBefore,deepCheck,&error),"legacy authoring near JSON depth cap is valid");V deepFailure=deep.call("doc_apply",apply(deep,"1"));check(deepFailure.find("isError")->b&&deepFailure.find("structuredContent")->find("error")->str.find("nesting capacity")!=std::string::npos&&deep.revision==0&&ark::documentToJson(deep.document)==deepBefore,"journal wrapping cannot commit unreadable nested JSON");
    ark::AgentSession loader;V loadArgs=V::makeObj();loadArgs.set("path",V::makeStr(pathText));payload(loader.call("doc_load",loadArgs));check(payload(loader.call("history_info",V::makeObj())).find("node_count")->num==5,"load adopts saved journal without fabricated load iteration");
    payload(loader.call("doc_undo",V::makeObj()));check(loader.document.getParam("length")==20,"binary reopen retains persistent undo target");payload(loader.call("doc_redo",V::makeObj()));check(loader.document.getParam("length")==10&&ark::usageTotals(loader.document).totalTokens==60,"binary reopen undo/redo preserves geometry and usage");
    V newArgs=V::makeObj();newArgs.set("expected_revision",V::makeNum(bounded.revision));payload(bounded.call("doc_new",newArgs));payload(bounded.call("doc_apply",apply(bounded,"2")));check(ark::documentToJson(bounded.document).find(std::string(1024,'x'))==std::string::npos&&bounded.document.canvases.empty(),"new project journal never retains old project blob data");std::error_code ignored;std::filesystem::remove(path,ignored);
    std::cout<<"iteration history tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
