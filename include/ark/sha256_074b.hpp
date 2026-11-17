#pragma once
#include <string>
#include <string_view>
#include <initializer_list>
namespace ark {
// Platform acceleration only. False requests the existing portable SHA-256;
// success returns the same lowercase digest, without changing hash semantics.
bool platformSha256074B(std::string_view bytes,std::string& digest);
// Hash one logical byte stream without concatenating its existing buffers.
bool platformSha256Segments078B(std::initializer_list<std::string_view> segments,std::string& digest);
}
