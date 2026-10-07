#include "ark/usage.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr u64 maxExact=9007199254740991ull;
constexpr size_t maxReceipts=256;
bool safeText(const std::string& text,size_t max){if(text.empty()||text.size()>max)return false;for(unsigned char c:text)if(c<0x20||c==0x7f)return false;return true;}
bool validUtc(const std::string& time){if(time.size()!=20||time[4]!='-'||time[7]!='-'||time[10]!='T'||time[13]!=':'||time[16]!=':'||time[19]!='Z')return false;for(size_t i=0;i<time.size();++i)if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&i!=19&&(time[i]<'0'||time[i]>'9'))return false;return true;}
bool add(u64& total,u64 value){if(value>maxExact-total)return false;total+=value;return true;}
bool sameIdentity(const UsageReceipt& a,const UsageReceipt& b){return a.id==b.id&&a.source==b.source&&a.provenance==b.provenance&&a.model==b.model;}
bool sameCounts(const UsageReceipt& a,const UsageReceipt& b){return a.inputTokens==b.inputTokens&&a.outputTokens==b.outputTokens&&a.totalTokens==b.totalTokens&&a.cachedInputTokens==b.cachedInputTokens&&a.reasoningTokens==b.reasoningTokens&&a.reportedResponses==b.reportedResponses&&a.missingResponses==b.missingResponses;}
bool monotone(const UsageReceipt& before,const UsageReceipt& after){return after.inputTokens>=before.inputTokens&&after.outputTokens>=before.outputTokens&&after.totalTokens>=before.totalTokens&&after.cachedInputTokens>=before.cachedInputTokens&&after.reasoningTokens>=before.reasoningTokens&&after.reportedResponses>=before.reportedResponses&&after.missingResponses>=before.missingResponses;}
std::string nowUtc(){
    auto now=std::chrono::system_clock::now();std::time_t time=std::chrono::system_clock::to_time_t(now);std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&time);
#else
    gmtime_r(&time,&tm);
#endif
    char buffer[32];if(std::strftime(buffer,sizeof buffer,"%Y-%m-%dT%H:%M:%SZ",&tm)==0)throw std::runtime_error("Cannot format usage UTC time");return buffer;
}
bool totalsOf(const UsageLedger& ledger,UsageTotals& totals,std::string& error){
    totals={};for(const UsageReceipt& receipt:ledger.entries){
        if(!add(totals.inputTokens,receipt.inputTokens)||!add(totals.outputTokens,receipt.outputTokens)||!add(totals.totalTokens,receipt.totalTokens)||!add(totals.cachedInputTokens,receipt.cachedInputTokens)||!add(totals.reasoningTokens,receipt.reasoningTokens)||!add(totals.reportedResponses,receipt.reportedResponses)||!add(totals.missingResponses,receipt.missingResponses)){error="Usage totals exceed exact JSON integer range";return false;}
    }return true;
}
V receiptJson(const UsageReceipt& receipt){V row=V::makeObj();row.set("id",V::makeStr(receipt.id));row.set("source",V::makeStr(receipt.source));row.set("provenance",V::makeStr(receipt.provenance));row.set("model",V::makeStr(receipt.model));row.set("recorded_at_utc",V::makeStr(receipt.recordedAtUtc));row.set("input_tokens",V::makeNum((double)receipt.inputTokens));row.set("output_tokens",V::makeNum((double)receipt.outputTokens));row.set("total_tokens",V::makeNum((double)receipt.totalTokens));row.set("cached_input_tokens",V::makeNum((double)receipt.cachedInputTokens));row.set("reasoning_tokens",V::makeNum((double)receipt.reasoningTokens));row.set("reported_responses",V::makeNum((double)receipt.reportedResponses));row.set("missing_responses",V::makeNum((double)receipt.missingResponses));return row;}
UsageRecordResult record(Document& document,UsageReceipt receipt,bool allowUpdate,std::string* error){
    auto reject=[&](const std::string& reason){if(error)*error=reason;return UsageRecordResult::Rejected;};
    std::string reason;if(!validateUsageReceipt(receipt,reason))return reject(reason);
    if(document.usage.serial>=maxExact)return reject("Usage serial exceeds exact JSON integer range");
    auto found=std::find_if(document.usage.entries.begin(),document.usage.entries.end(),[&](const UsageReceipt& item){return item.id==receipt.id;});
    if(found!=document.usage.entries.end()){
        if(!sameIdentity(*found,receipt))return reject("Usage receipt ID belongs to another source/model");
        if(sameCounts(*found,receipt))return UsageRecordResult::Unchanged;
        if(!allowUpdate)return reject("Usage receipt ID already exists with different counts");
        if(!monotone(*found,receipt))return reject("Usage snapshot counters cannot decrease");
        receipt.recordedAtUtc=found->recordedAtUtc;
        UsageLedger trial=document.usage;trial.entries[(size_t)(found-document.usage.entries.begin())]=receipt;trial.serial++;trial.updatedAtUtc=nowUtc();
        if(!validateUsageLedger(trial,reason))return reject(reason);
        *found=std::move(receipt);++document.usage.serial;document.usage.updatedAtUtc=trial.updatedAtUtc;return UsageRecordResult::Updated;
    }
    if(document.usage.entries.size()>=maxReceipts)return reject("Usage receipt capacity is 256");
    if(receipt.recordedAtUtc.empty())receipt.recordedAtUtc=nowUtc();
    UsageLedger trial=document.usage;trial.entries.push_back(receipt);trial.serial++;trial.updatedAtUtc=nowUtc();
    if(!validateUsageLedger(trial,reason))return reject(reason);
    document.usage.entries.push_back(std::move(receipt));++document.usage.serial;document.usage.updatedAtUtc=trial.updatedAtUtc;return UsageRecordResult::Added;
}
}

bool validateUsageReceipt(const UsageReceipt& receipt,std::string& error){
    if(!safeText(receipt.id,128)||!safeText(receipt.source,128)||!safeText(receipt.provenance,128)){error="Usage receipt needs bounded ID, source and provenance";return false;}
    if(receipt.model.size()>128){error="Usage model name exceeds 128 characters";return false;}
    if(!receipt.recordedAtUtc.empty()&&!validUtc(receipt.recordedAtUtc)){error="Usage UTC timestamp must be ISO 8601 seconds";return false;}
    if(receipt.totalTokens>maxExact||receipt.inputTokens>maxExact||receipt.outputTokens>maxExact||receipt.cachedInputTokens>maxExact||receipt.reasoningTokens>maxExact||receipt.reportedResponses>maxExact||receipt.missingResponses>maxExact){error="Usage count exceeds exact JSON integer range";return false;}
    if(receipt.inputTokens>maxExact-receipt.outputTokens||receipt.totalTokens!=receipt.inputTokens+receipt.outputTokens){error="Usage total_tokens must equal input_tokens plus output_tokens";return false;}
    if(receipt.cachedInputTokens>receipt.inputTokens||receipt.reasoningTokens>receipt.outputTokens){error="Cached/reasoning tokens must be subsets";return false;}
    if(receipt.reportedResponses==0&&(receipt.totalTokens||receipt.cachedInputTokens||receipt.reasoningTokens)){error="Usage tokens need at least one reported response";return false;}
    if(receipt.reportedResponses==0&&receipt.missingResponses==0){error="Usage receipt must report a response or an omission";return false;}
    return true;
}
bool validateUsageLedger(const UsageLedger& ledger,std::string& error){
    if(!ledger.externalUsageUnknown){error="External agent token coverage cannot be certified";return false;}
    if(ledger.entries.size()>maxReceipts||ledger.serial>maxExact||ledger.serial<ledger.entries.size()){error="Usage ledger capacity or serial is invalid";return false;}
    if((ledger.serial>0&&!validUtc(ledger.updatedAtUtc))||(ledger.serial==0&&!ledger.updatedAtUtc.empty())){error="Usage ledger updated UTC timestamp is invalid";return false;}
    std::set<std::string> ids;for(const auto& receipt:ledger.entries){if(!validateUsageReceipt(receipt,error))return false;if(receipt.recordedAtUtc.empty()){error="Saved usage receipt needs a UTC timestamp";return false;}if(!ids.insert(receipt.id).second){error="Duplicate usage receipt ID";return false;}}
    UsageTotals totals;return totalsOf(ledger,totals,error);
}
UsageRecordResult addUsageReceipt(Document& document,UsageReceipt receipt,std::string* error){return record(document,std::move(receipt),false,error);}
UsageRecordResult updateUsageSnapshot(Document& document,UsageReceipt receipt,std::string* error){return record(document,std::move(receipt),true,error);}
UsageTotals usageTotals(const Document& document){UsageTotals totals;std::string error;if(!totalsOf(document.usage,totals,error))throw std::runtime_error(error);return totals;}
V usageInfo(const Document& document,bool includeReceipts){
    UsageTotals totals=usageTotals(document);V result=V::makeObj();result.set("serial",V::makeNum((double)document.usage.serial));result.set("updated_at_utc",document.usage.updatedAtUtc.empty()?V{}:V::makeStr(document.usage.updatedAtUtc));result.set("receipt_count",V::makeNum((double)document.usage.entries.size()));result.set("reported_input_tokens",V::makeNum((double)totals.inputTokens));result.set("reported_output_tokens",V::makeNum((double)totals.outputTokens));result.set("reported_total_tokens",V::makeNum((double)totals.totalTokens));result.set("reported_cached_input_tokens",V::makeNum((double)totals.cachedInputTokens));result.set("reported_reasoning_tokens",V::makeNum((double)totals.reasoningTokens));result.set("reported_responses",V::makeNum((double)totals.reportedResponses));result.set("missing_responses",V::makeNum((double)totals.missingResponses));result.set("external_usage_unknown",V::makeBool(true));result.set("reported_total_is_lower_bound",V::makeBool(true));result.set("has_reported_usage",V::makeBool(totals.reportedResponses>0));result.set("status",V::makeStr(totals.reportedResponses==0?"not reported":totals.missingResponses?"partial":"reported only"));result.set("scope",V::makeStr("saved document receipts; external client usage may be absent"));
    if(includeReceipts){V entries=V::makeArr();for(const UsageReceipt& receipt:document.usage.entries)entries.arr.push_back(receiptJson(receipt));result.set("entries",entries);}return result;
}
V usageLedgerToJson(const UsageLedger& ledger){
    std::string error;if(!validateUsageLedger(ledger,error))throw std::runtime_error(error);
    V result=V::makeObj(),entries=V::makeArr();result.set("schema",V::makeNum(1));result.set("serial",V::makeNum((double)ledger.serial));result.set("updated_at_utc",ledger.updatedAtUtc.empty()?V{}:V::makeStr(ledger.updatedAtUtc));result.set("external_usage_unknown",V::makeBool(true));for(const auto& receipt:ledger.entries)entries.arr.push_back(receiptJson(receipt));result.set("entries",entries);return result;
}
bool usageLedgerFromJson(const V& source,UsageLedger& ledger,std::string* error){
    auto fail=[&](const std::string& reason){if(error)*error=reason;return false;};
    if(source.t!=V::Obj)return fail("Usage ledger must be an object");
    const V *schema=source.find("schema"),*serial=source.find("serial"),*updated=source.find("updated_at_utc"),*external=source.find("external_usage_unknown"),*items=source.find("entries");
    if(!schema||schema->t!=V::Num||schema->num!=1||!serial||serial->t!=V::Num||!std::isfinite(serial->num)||serial->num<0||serial->num>(double)maxExact||std::floor(serial->num)!=serial->num||!updated||(updated->t!=V::Str&&updated->t!=V::Null)||!external||external->t!=V::Bool||!external->b||!items||items->t!=V::Arr||items->arr.size()>maxReceipts)return fail("Invalid usage ledger envelope");
    UsageLedger parsed;parsed.serial=(u64)serial->num;parsed.externalUsageUnknown=true;if(updated->t==V::Str)parsed.updatedAtUtc=updated->str;
    auto count=[&](const V& item,const char* key,u64& out){const V* value=item.find(key);if(!value||value->t!=V::Num||!std::isfinite(value->num)||value->num<0||value->num>(double)maxExact||std::floor(value->num)!=value->num)return false;out=(u64)value->num;return true;};
    auto string=[&](const V& item,const char* key,std::string& out){const V* value=item.find(key);if(!value||value->t!=V::Str)return false;out=value->str;return true;};
    for(const V& item:items->arr){if(item.t!=V::Obj)return fail("Usage receipt must be an object");UsageReceipt receipt;
        if(!string(item,"id",receipt.id)||!string(item,"source",receipt.source)||!string(item,"provenance",receipt.provenance)||!string(item,"model",receipt.model)||!string(item,"recorded_at_utc",receipt.recordedAtUtc)||!count(item,"input_tokens",receipt.inputTokens)||!count(item,"output_tokens",receipt.outputTokens)||!count(item,"total_tokens",receipt.totalTokens)||!count(item,"cached_input_tokens",receipt.cachedInputTokens)||!count(item,"reasoning_tokens",receipt.reasoningTokens)||!count(item,"reported_responses",receipt.reportedResponses)||!count(item,"missing_responses",receipt.missingResponses))return fail("Invalid usage receipt fields");parsed.entries.push_back(std::move(receipt));
    }
    std::string reason;if(!validateUsageLedger(parsed,reason))return fail(reason);ledger=std::move(parsed);return true;
}
std::string newUsageReceiptId(){
    static std::atomic<u64> sequence{0};std::random_device random;auto now=std::chrono::steady_clock::now().time_since_epoch().count();u64 a=((u64)random()<<32)^(u64)random();u64 b=((u64)now)^++sequence^(((u64)random()<<32)^(u64)random());std::ostringstream out;out<<"run-"<<std::hex<<std::setfill('0')<<std::setw(16)<<a<<std::setw(16)<<b;return out.str();
}
} // namespace ark
