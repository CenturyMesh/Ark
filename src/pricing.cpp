#include "ark/pricing.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace ark {
namespace {
constexpr const char* apiSource="https://developers.openai.com/api/docs/pricing";
constexpr const char* planSource="https://learn.chatgpt.com/docs/pricing";
std::string trim(std::string text){auto a=text.find_first_not_of(" \r\n\t"),b=text.find_last_not_of(" \r\n\t");return a==std::string::npos?std::string():text.substr(a,b-a+1);}
std::vector<std::string> cells(const std::string& line){std::vector<std::string> out;std::istringstream stream(line);std::string cell;while(std::getline(stream,cell,'|'))out.push_back(trim(cell));if(!out.empty()&&out.front().empty())out.erase(out.begin());if(!out.empty()&&out.back().empty())out.pop_back();return out;}
double rate(const std::string& value){
    if(value.size()<2||value[0]!='$')return -1;
    // Fail closed for ranges, commas, annotations and unexpected formatting.
    if(value.find_first_not_of("0123456789.",1)!=std::string::npos)return -1;
    try{size_t end=0;double number=std::stod(value.substr(1),&end);return end==value.size()-1&&std::isfinite(number)&&number>=0&&number<=1e9?number:-1;}catch(...){return -1;}
}
std::string attribute(const std::string& tag,const std::string& key){auto pos=tag.find(key+"=\"");if(pos==std::string::npos)return {};pos+=key.size()+2;auto end=tag.find('"',pos);return end==std::string::npos?std::string():tag.substr(pos,end-pos);}
std::string utcNow(){auto now=std::time(nullptr);std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&now);
#else
    gmtime_r(&now,&tm);
#endif
    std::ostringstream out;out<<std::put_time(&tm,"%Y-%m-%dT%H:%M:%SZ");return out.str();
}
#ifdef _WIN32
struct Http {HINTERNET handle;explicit Http(HINTERNET value):handle(value){if(!handle)throw std::runtime_error("Pricing HTTPS unavailable");}~Http(){WinHttpCloseHandle(handle);}Http(const Http&)=delete;};
std::string fetch(const wchar_t* host,const wchar_t* path,const std::atomic_bool* cancel){
    if(cancel&&cancel->load())throw std::runtime_error("Pricing refresh cancelled");
    Http session(WinHttpOpen(L"ARK-Point/0.9 public-pricing",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session.handle,4000,4000,4000,4000);
    Http connection(WinHttpConnect(session.handle,host,INTERNET_DEFAULT_HTTPS_PORT,0));
    Http request(WinHttpOpenRequest(connection.handle,L"GET",path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD disable=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(request.handle,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof disable);
    DWORD auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;WinHttpSetOption(request.handle,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof auth);
    auto start=std::chrono::steady_clock::now();
    if(!WinHttpSendRequest(request.handle,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0))throw std::runtime_error("Official pricing fetch failed");
    if(cancel&&cancel->load())throw std::runtime_error("Pricing refresh cancelled");
    if(!WinHttpReceiveResponse(request.handle,nullptr))throw std::runtime_error("Official pricing fetch failed");
    DWORD code=0,size=sizeof code;if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX)||code!=200)throw std::runtime_error("Official pricing HTTP "+std::to_string(code));
    std::string result;char buffer[16384];
    for(;;){if(cancel&&cancel->load())throw std::runtime_error("Pricing refresh cancelled");if(std::chrono::steady_clock::now()-start>std::chrono::seconds(15))throw std::runtime_error("Pricing fetch timed out");DWORD read=0;if(!WinHttpReadData(request.handle,buffer,sizeof buffer,&read))throw std::runtime_error("Pricing response failed");if(!read)break;if(result.size()+read>1024*1024)throw std::runtime_error("Pricing response exceeds 1 MiB");result.append(buffer,read);}
    return result;
}
#endif
}
PricingCatalog parseOpenAiPricingMarkdown(const std::string& markdown){
    PricingCatalog catalog;catalog.sourceUrl=apiSource;catalog.subscriptionSourceUrl=planSource;
    if(markdown.size()>1024*1024){catalog.error="Pricing document exceeds 1 MiB";return catalog;}
    // Select the explicitly labeled Standard table, never Batch/Flex/Fast.
    auto begin=markdown.find("### Standard pricing data");
    if(begin==std::string::npos){catalog.error="Official Standard pricing table unavailable";return catalog;}
    auto end=markdown.find("\n### ",begin+4);std::istringstream lines(markdown.substr(begin,end==std::string::npos?end:end-begin));
    std::string line;bool header=false;std::set<std::string> seen;
    const std::vector<std::string> expected={"Model","Short context input","Short context cached input","Short context cache writes","Short context output","Long context input","Long context cached input","Long context cache writes","Long context output"};
    while(std::getline(lines,line)){
        line=trim(line);if(line.empty()){if(header&&!catalog.models.empty())break;continue;}
        if(line.front()!='|')continue;auto row=cells(line);
        if(!header){if(row!=expected){catalog.error="Official pricing table format changed";return catalog;}header=true;continue;}
        if(row.size()==9&&row[0]=="---")continue;
        if(row.size()!=9){catalog.models.clear();catalog.error="Official pricing row format changed";return catalog;}
        // Annotated model names are deliberately not aliased to API identifiers.
        if(row[0].empty()||row[0].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._")!=std::string::npos)continue;
        if(!seen.insert(row[0]).second){catalog.models.clear();catalog.error="Duplicate Standard model pricing";return catalog;}
        ModelTokenPrice p;p.model=row[0];p.inputPerMillion=rate(row[1]);p.cachedInputPerMillion=rate(row[2]);p.cacheWritePerMillion=rate(row[3]);p.outputPerMillion=rate(row[4]);p.longInputPerMillion=rate(row[5]);p.longCachedInputPerMillion=rate(row[6]);p.longCacheWritePerMillion=rate(row[7]);p.longOutputPerMillion=rate(row[8]);
        if(p.inputPerMillion>=0&&p.outputPerMillion>=0)catalog.models.push_back(p);
    }
    if(catalog.models.empty())catalog.error="No recognized Standard model rates";
    return catalog;
}
std::string parseOpenAiSubscriptionMarkdown(const std::string& markdown){
    if(markdown.size()>1024*1024)return "Published plans unavailable";
    std::string summary;size_t position=0;unsigned count=0;
    while((position=markdown.find("<PricingCard",position))!=std::string::npos&&count<8){auto end=markdown.find('>',position);if(end==std::string::npos)break;auto tag=markdown.substr(position,end-position);position=end+1;auto name=attribute(tag,"name"),price=attribute(tag,"price"),interval=attribute(tag,"interval");if(name.empty()||price.empty()||interval.empty()||name.size()>40||price.size()>30||interval.size()>40)continue;
        if(!summary.empty())summary+="; ";summary+=name+" "+(attribute(tag,"priceEyebrow")=="From"?"from ":"")+price+interval;++count;}
    return summary.empty()?"Published plan prices unavailable":summary+" (published starting prices; account invoice, tax and included usage not exposed)";
}
std::vector<ModelTokenPrice> parseOpenAiCreditRates(const std::string& markdown){
    std::vector<ModelTokenPrice> rates;if(markdown.size()>1024*1024)return rates;
    auto start=markdown.find("#### Token rates");if(start==std::string::npos)return rates;
    auto finish=markdown.find("</table>",start);if(finish==std::string::npos)return rates;
    auto table=markdown.substr(start,finish-start);if(table.find("Credits per 1M tokens")==std::string::npos)return rates;
    size_t pos=0;std::set<std::string> seen;
    while((pos=table.find("<tr>",pos))!=std::string::npos){auto end=table.find("</tr>",pos);if(end==std::string::npos)break;auto row=table.substr(pos,end-pos);pos=end+5;std::vector<std::string> values;size_t cell=0;
        while((cell=row.find("<td",cell))!=std::string::npos){auto a=row.find('>',cell),b=row.find("</td>",a);if(a==std::string::npos||b==std::string::npos)break;values.push_back(trim(row.substr(a+1,b-a-1)));cell=b+5;}
        if(values.size()!=4||values[0].rfind("GPT-",0)!=0)continue;
        std::string model=values[0];for(char& c:model){if(c==' ')c='-';else if(c>='A'&&c<='Z')c+=32;}
        if(model.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos)continue;
        auto credit=[](std::string value){auto suffix=value.find(" credits");if(suffix==std::string::npos||suffix+8!=value.size())return -1.;value=value.substr(0,suffix);value.erase(std::remove(value.begin(),value.end(),','),value.end());return rate("$"+value);};
        ModelTokenPrice p;p.model=model;p.inputPerMillion=credit(values[1]);p.cachedInputPerMillion=credit(values[2]);p.outputPerMillion=credit(values[3]);
        if(p.inputPerMillion<0||p.cachedInputPerMillion<0||p.outputPerMillion<0)continue;
        if(!seen.insert(model).second)return {};rates.push_back(p);
    }
    return rates;
}
PricingCatalog fetchOpenAiPricing(const std::atomic_bool* cancel){
    PricingCatalog catalog;catalog.sourceUrl=apiSource;catalog.subscriptionSourceUrl=planSource;
#ifdef _WIN32
    try{catalog=parseOpenAiPricingMarkdown(fetch(L"developers.openai.com",L"/api/docs/pricing.md",cancel));if(catalog.error.empty())catalog.fetchedAtUtc=utcNow();}catch(const std::exception& e){catalog.error=e.what();}
    try{auto markdown=fetch(L"learn.chatgpt.com",L"/docs/pricing.md",cancel);catalog.subscriptionSummary=parseOpenAiSubscriptionMarkdown(markdown);catalog.subscriptionCredits=parseOpenAiCreditRates(markdown);}catch(...){catalog.subscriptionSummary="Published subscription prices unavailable; no account billing rate exposed";}
#else
    catalog.error="Native public pricing fetch requires Windows";
#endif
    return catalog;
}
CostEstimate estimateDocumentCost(const Document& document,const PricingCatalog& catalog){
    CostEstimate result;
    for(const auto& receipt:document.usage.entries){
        result.partial=result.partial||receipt.missingResponses>0;
        auto credit=std::find_if(catalog.subscriptionCredits.begin(),catalog.subscriptionCredits.end(),[&](const auto& price){return price.model==receipt.model;});
        if(credit!=catalog.subscriptionCredits.end()&&receipt.cachedInputTokens<=receipt.inputTokens&&credit->inputPerMillion>=0&&credit->cachedInputPerMillion>=0&&credit->outputPerMillion>=0){
            result.standardCredits+=((receipt.inputTokens-receipt.cachedInputTokens)*credit->inputPerMillion+receipt.cachedInputTokens*credit->cachedInputPerMillion+receipt.outputTokens*credit->outputPerMillion)/1e6;
            result.pricedCreditTokens+=receipt.totalTokens;
        }else result.unpricedCreditTokens+=receipt.totalTokens;
        auto it=std::find_if(catalog.models.begin(),catalog.models.end(),[&](const auto& price){return price.model==receipt.model;});
        if(it==catalog.models.end()||it->inputPerMillion<0||it->outputPerMillion<0||(receipt.cachedInputTokens&&it->cachedInputPerMillion<0)||receipt.cachedInputTokens>receipt.inputTokens){result.unpricedTokens+=receipt.totalTokens;result.partial=true;continue;}
        const auto& p=*it;double input=(double)(receipt.inputTokens-receipt.cachedInputTokens),cached=(double)receipt.cachedInputTokens,output=(double)receipt.outputTokens;
        result.standardShortUsd+=(input*p.inputPerMillion+cached*std::max(0.,p.cachedInputPerMillion)+output*p.outputPerMillion)/1e6;
        const double longInput=p.longInputPerMillion>=0?p.longInputPerMillion:p.inputPerMillion;
        const double longCached=p.longCachedInputPerMillion>=0?p.longCachedInputPerMillion:p.cachedInputPerMillion;
        const double longOutput=p.longOutputPerMillion>=0?p.longOutputPerMillion:p.outputPerMillion;
        result.standardLongUsd+=(input*longInput+cached*std::max(0.,longCached)+output*longOutput)/1e6;
        result.pricedTokens+=receipt.totalTokens;
    }
    return result;
}
}
