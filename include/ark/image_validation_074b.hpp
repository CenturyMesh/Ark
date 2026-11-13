#pragma once
#include "ark/json.hpp"
namespace ark {
// Bounded metadata-only memo. Every lookup hashes the complete current encoded source
// bytes and binds them to previously decoded/verified metadata; it never trusts a caller-supplied hash or retains user image pixels.
arkjson::Value imageValidationCacheInfo074B();
void clearImageValidationCache074B();
}
