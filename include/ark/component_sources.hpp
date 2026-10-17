#pragma once
#include "ark/json.hpp"
#include <string>
#include <functional>
#include <vector>
namespace ark {
void validateComponentHttpsUrl(const std::string& url);
std::string componentHttpsHost(const std::string& url);
struct ComponentHttpResponse {
    unsigned status=200;
    std::string body,content_type,location;
};
using ComponentHttpTransport=std::function<ComponentHttpResponse(const std::string&,std::size_t)>;
enum class ComponentResourceKind { Data, Page, Step };
struct ComponentResource {std::string bytes;arkjson::Value receipt;};
// Optional transport is an offline-test seam; production callers use the bounded
// native HTTPS implementation. Redirects remain HTTPS and same-host unless an
// explicit finite host list authorizes the destination. No auth or cookies.
ComponentResource fetchComponentResource(const std::string& url,ComponentResourceKind kind,
    const std::vector<std::string>& redirectHosts={},const ComponentHttpTransport& transport={});
arkjson::Value componentSources();
// Public web search and paged KiCad discovery. Search results are candidates;
// read the actual product page before recording supplier metadata.
arkjson::Value searchComponentSource(const arkjson::Value& args);
arkjson::Value parseComponentWebSearch(const std::string& html,const std::string& query);
arkjson::Value componentSourcePage(const arkjson::Value& args);
arkjson::Value parseComponentSourcePage(const std::string& html,const std::string& requestedUrl,const std::string& finalUrl);
std::string componentPageText(const std::string& html);
// Public HTTPS STEP only, bounded to 64 MiB and 90 seconds per resource. No account
// credentials, cookies or background crawling. Destination is owned
// by the host, not selected by an external catalog.
arkjson::Value downloadComponentStep(const std::string& url,const std::string& destination,
    const std::vector<std::string>& redirectHosts={});
}
