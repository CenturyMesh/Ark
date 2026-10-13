#pragma once
#include "ark/json.hpp"
#include <string>
namespace ark {
void validateComponentHttpsUrl(const std::string& url);
arkjson::Value componentSources();
// Public web search and paged KiCad discovery. Search results are candidates;
// read the actual product page before recording supplier metadata.
arkjson::Value searchComponentSource(const arkjson::Value& args);
arkjson::Value parseComponentWebSearch(const std::string& html,const std::string& query);
arkjson::Value componentSourcePage(const arkjson::Value& args);
arkjson::Value parseComponentSourcePage(const std::string& html,const std::string& requestedUrl,const std::string& finalUrl);
std::string componentPageText(const std::string& html);
// Direct public HTTPS STEP only, bounded to 64 MiB and 90 seconds. No account
// credentials, cookies, redirects or background crawling. Destination is owned
// by the host, not selected by an external catalog.
void downloadComponentStep(const std::string& url,const std::string& destination);
}
