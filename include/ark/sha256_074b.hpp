#pragma once
#include <string>
#include <string_view>
namespace ark {
// Platform acceleration only. False requests the existing portable SHA-256;
// success returns the same lowercase digest, without changing hash semantics.
bool platformSha256074B(std::string_view bytes,std::string& digest);
}
