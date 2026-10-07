#pragma once
#include "ark/model.hpp"

namespace ark {

enum class UsageRecordResult { Added, Updated, Unchanged, Rejected };
struct UsageTotals {
    u64 inputTokens=0,outputTokens=0,totalTokens=0;
    u64 cachedInputTokens=0,reasoningTokens=0;
    u64 reportedResponses=0,missingResponses=0;
};

// Validates one provider/client receipt. Empty recordedAtUtc is filled with
// current UTC when the receipt is first added.
bool validateUsageReceipt(const UsageReceipt& receipt,std::string& error);
bool validateUsageLedger(const UsageLedger& ledger,std::string& error);
UsageRecordResult addUsageReceipt(Document& document,UsageReceipt receipt,std::string* error=nullptr);
// Studio receives cumulative usage snapshots for one assistant run. This may
// replace that receipt only when every count is monotone and identity matches.
UsageRecordResult updateUsageSnapshot(Document& document,UsageReceipt receipt,std::string* error=nullptr);
UsageTotals usageTotals(const Document& document);
arkjson::Value usageInfo(const Document& document,bool includeReceipts=false);
arkjson::Value usageLedgerToJson(const UsageLedger& ledger);
bool usageLedgerFromJson(const arkjson::Value& source,UsageLedger& ledger,std::string* error=nullptr);
std::string newUsageReceiptId();

} // namespace ark
