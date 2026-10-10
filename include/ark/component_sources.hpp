#pragma once
#include "ark/json.hpp"
#include <string>
namespace ark {
void validateComponentHttpsUrl(const std::string& url);
arkjson::Value componentSources();
// Public, paged KiCad repository discovery. Source metadata is not a supplier
// or standard attestation. Other catalogs are listed with their access needs.
arkjson::Value searchComponentSource(const arkjson::Value& args);
// Direct public HTTPS STEP only, bounded to 64 MiB and 90 seconds. No account
// credentials, cookies, redirects or background crawling. Destination is owned
// by the host, not selected by an external catalog.
void downloadComponentStep(const std::string& url,const std::string& destination);
}
