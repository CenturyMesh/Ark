#include "ark/usage.hpp"
#include "ark/serialize.hpp"
#include "ark/agent.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
ark::UsageReceipt receipt(const std::string& id,ark::u64 in,ark::u64 out){ark::UsageReceipt value;value.id=id;value.source="studio_assistant";value.provenance="provider_response";value.model="test-model";value.inputTokens=in;value.outputTokens=out;value.totalTokens=in+out;value.reportedResponses=1;return value;}
}
int main(){
    try{
        ark::Document document;auto empty=ark::usageInfo(document);
        check(empty.find("status")->str=="not reported"&&!empty.find("has_reported_usage")->b&&empty.find("external_usage_unknown")->b,"empty document states usage unknown");
        const auto id=ark::newUsageReceiptId();check(id!=ark::newUsageReceiptId(),"run receipt IDs differ");
        std::string error;auto first=receipt(id,100,20);first.cachedInputTokens=10;first.reasoningTokens=5;
        check(ark::addUsageReceipt(document,first,&error)==ark::UsageRecordResult::Added,"reported receipt added");
        check(document.usage.serial==1&&document.usage.entries[0].recordedAtUtc.size()==20&&document.usage.updatedAtUtc.size()==20,"receipt and ledger timestamps recorded");
        check(ark::addUsageReceipt(document,first,&error)==ark::UsageRecordResult::Unchanged&&document.usage.serial==1,"exact duplicate is idempotent");
        auto changed=first;changed.inputTokens=101;changed.totalTokens=121;
        check(ark::addUsageReceipt(document,changed,&error)==ark::UsageRecordResult::Rejected&&document.usage.serial==1,"external duplicate with changed counts rejected");
        auto next=receipt(id,130,30);next.cachedInputTokens=20;next.reasoningTokens=7;next.reportedResponses=2;next.missingResponses=1;
        check(ark::updateUsageSnapshot(document,next,&error)==ark::UsageRecordResult::Updated&&document.usage.serial==2,"cumulative Studio snapshot replaces receipt");
        auto totals=ark::usageTotals(document);check(totals.totalTokens==160&&totals.cachedInputTokens==20&&totals.reasoningTokens==7&&totals.reportedResponses==2&&totals.missingResponses==1,"reported totals and partial count are exact");
        check(ark::usageInfo(document).find("status")->str=="partial","partial reported usage is labeled");
        check(ark::updateUsageSnapshot(document,first,&error)==ark::UsageRecordResult::Rejected&&document.usage.serial==2,"snapshot cannot decrease");
        ark::UsageReceipt external;external.id="client-response-1";external.source="external_mcp";external.provenance="client_supplied_provider_usage";external.model="external-model";external.inputTokens=200;external.outputTokens=50;external.totalTokens=250;external.reportedResponses=1;
        check(ark::addUsageReceipt(document,external,&error)==ark::UsageRecordResult::Added,"external provider report recorded with provenance");
        auto info=ark::usageInfo(document,true);check(info.find("reported_total_tokens")->num==410&&info.find("receipt_count")->num==2&&info.find("external_usage_unknown")->b,"reported total remains a lower bound");
        check(info.find("entries")->arr.size()==2,"receipt history available on request");
        ark::UsageReceipt omitted;omitted.id="response-without-usage";omitted.source="studio_assistant";omitted.provenance="provider_response";omitted.model="test-model";omitted.missingResponses=1;
        check(ark::addUsageReceipt(document,omitted,&error)==ark::UsageRecordResult::Added&&ark::usageTotals(document).totalTokens==410,"omitted provider usage is counted as missing, never guessed");

        std::string saved=ark::documentToJson(document);arkjson::Value parsed;check(arkjson::parse(saved,parsed)&&parsed.find("version")->num==17,"usage package has version 17");
        ark::Document restored;check(ark::documentFromJson(saved,restored,&error),"usage ledger round-trips");check(ark::documentToJson(restored)==saved&&restored.usage.serial==4,"receipt IDs and totals persist without inflation");
        const std::string stable=ark::documentToJson(restored);
        check(!ark::documentFromJson(R"({"format":"ark","version":17,"features":[],"usage":{"schema":1,"serial":1,"updated_at_utc":"2026-10-07T12:00:00Z","external_usage_unknown":false,"entries":[]}})",restored,&error)&&ark::documentToJson(restored)==stable,"false external coverage rejected atomically");
        check(!ark::documentFromJson(R"({"format":"ark","version":17,"features":[],"usage":{"schema":1,"serial":1,"updated_at_utc":"2026-10-07T12:00:00Z","external_usage_unknown":true,"entries":[{"id":"bad","source":"external_mcp","provenance":"client_supplied_provider_usage","model":"x","recorded_at_utc":"2026-10-07T12:00:00Z","input_tokens":5,"output_tokens":4,"total_tokens":10,"cached_input_tokens":0,"reasoning_tokens":0,"reported_responses":1,"missing_responses":0}]}})",restored,&error)&&ark::documentToJson(restored)==stable,"inconsistent token total rejected atomically");
        ark::Document bounded;for(int i=0;i<256;++i)check(ark::addUsageReceipt(bounded,receipt("receipt-"+std::to_string(i),1,0),&error)==ark::UsageRecordResult::Added,"bounded receipt accepted");
        check(ark::addUsageReceipt(bounded,receipt("receipt-257",1,0),&error)==ark::UsageRecordResult::Rejected&&bounded.usage.entries.size()==256,"receipt capacity rejects without discarding totals");
        ark::Document legacy;check(ark::documentFromJson(R"({"format":"ark","version":1,"features":[],"params":[],"sketches":[]})",legacy,&error)&&legacy.usage.entries.empty()&&legacy.usage.externalUsageUnknown,"legacy document loads with unknown usage");
        ark::AgentSession session;
        auto request=[&](const char* tool,const char* payload){arkjson::Value args;check(arkjson::parse(payload,args),"usage test request JSON");auto reply=session.call(tool,args);check(!reply.find("isError")->b,"usage MCP call succeeds");return *reply.find("structuredContent");};
        request("doc_apply",R"({"commands":[{"op":"feature.add","type":"box","name":"Receipt test","p":{"dx":10,"dy":10,"dz":10}}]})");
        request("usage_record",R"({"id":"external-test","input_tokens":100,"output_tokens":30,"total_tokens":130,"model":"offline-test"})");
        check(session.revision==1&&session.document.usage.serial==1,"usage receipt changes metadata only");
        auto repeated=request("usage_record",R"({"id":"external-test","input_tokens":100,"output_tokens":30,"total_tokens":130,"model":"offline-test"})");
        check(!repeated.find("changed")->b&&session.document.usage.serial==1,"MCP receipt retry cannot double-charge drawing");
        request("doc_undo","{}");
        check(session.document.features.empty()&&ark::usageTotals(session.document).totalTokens==130,"geometry undo retains already reported consumption");
        request("doc_redo","{}");
        check(session.document.features.size()==1&&ark::usageTotals(session.document).totalTokens==130,"geometry redo does not inflate receipt totals");
        auto listed=request("usage_info",R"({"detail":"receipts"})");
        check(listed.find("entries")->arr[0].find("provenance")->str=="client_supplied_provider_usage","external evidence is not mislabeled as Studio provider receipt");
        std::cout<<"usage ledger tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
