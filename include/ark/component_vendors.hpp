#pragma once
#include "ark/json.hpp"
#include <string>
namespace ark {
// Local approval is a network policy, never a supplier or geometry qualification.
arkjson::Value componentVendors(const arkjson::Value& args);
arkjson::Value componentVendorEvidence(const std::string& vendor,const std::string& url,const std::string& root={});
std::string componentVendorForUrl(const std::string& url);
}
