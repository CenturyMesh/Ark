#pragma once
#include "ark/model.hpp"
#include <atomic>

namespace ark {
struct ModelTokenPrice {
    std::string model;
    double inputPerMillion=-1,cachedInputPerMillion=-1,cacheWritePerMillion=-1,outputPerMillion=-1;
    double longInputPerMillion=-1,longCachedInputPerMillion=-1,longCacheWritePerMillion=-1,longOutputPerMillion=-1;
};
struct PricingCatalog {
    std::vector<ModelTokenPrice> models;
    std::vector<ModelTokenPrice> subscriptionCredits; // Units: credits/1M, not USD.
    std::string fetchedAtUtc,sourceUrl,error,subscriptionSummary,subscriptionSourceUrl;
};
// Official standard USD rates only. Unknown formats/rates remain unavailable.
PricingCatalog parseOpenAiPricingMarkdown(const std::string& markdown);
std::string parseOpenAiSubscriptionMarkdown(const std::string& markdown);
std::vector<ModelTokenPrice> parseOpenAiCreditRates(const std::string& markdown);
// No authentication/account data sent. Bounded public HTTPS; call off UI thread.
PricingCatalog fetchOpenAiPricing(const std::atomic_bool* cancel=nullptr);
struct CostEstimate {
    double standardShortUsd=0,standardLongUsd=0;
    double standardCredits=0;
    std::uint64_t pricedTokens=0,unpricedTokens=0;
    std::uint64_t pricedCreditTokens=0,unpricedCreditTokens=0;
    bool partial=false;
};
// Current-rate standard API-equivalent scenarios, NOT a bill or subscription
// usage estimate. Ledger lacks per-request context, cache-write and service tier.
// Output already includes reasoning. Unknown model/rate never counts as free.
CostEstimate estimateDocumentCost(const Document& document,const PricingCatalog& catalog);
}
