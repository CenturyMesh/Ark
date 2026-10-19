#pragma once
#include "ark/json.hpp"
#include <string>

namespace ark {
// Bounded selection inside a JSON tool result: dot-joined steps of key,
// key[n], key[a:b] (each element in that range) and key[] (each element).
// No wildcards, filters or code. Throws std::runtime_error on an invalid path.
void validateResultPath(const std::string& path);
// False when a key or index is absent outside an element map; where names the
// missing step and the keys available there. Inside a map an absent value is null.
bool selectResultPath(const arkjson::Value& value,const std::string& path,arkjson::Value& out,std::string& where);
}
