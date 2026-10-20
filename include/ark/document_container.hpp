#pragma once
#include "ark/json.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ark {

// Physical storage version is independent of the authored document version.
inline constexpr int documentContainerVersion=2;
inline constexpr int documentContainerApplicationId=0x41524b31; // ARK1
inline constexpr size_t maxDocumentContainerBytes=256*1024*1024;
inline constexpr size_t maxDocumentValueBytes=16*1024*1024;
inline constexpr size_t maxDocumentDerivedEntryBytes=64*1024*1024;
inline constexpr size_t maxDocumentDerivedBytes=128*1024*1024;
inline constexpr size_t maxDocumentDerivedEntries=8192;
struct DocumentDerivedPayload {std::string key,bytes;};
using DocumentDerivedPayloads=std::vector<DocumentDerivedPayload>;

struct DocumentContainerWriteStats {
    uint64_t generation=0;
    size_t payloadsInserted=0;
    size_t payloadBytesInserted=0;
    size_t slotsChanged=0;
    size_t slotsRemoved=0;
    size_t physicalBytes=0;
    bool stagedReplacement=false;
};

// This identifies any SQLite file. Ownership/version/schema checks happen on load.
bool documentContainerFile(const char* path);
bool saveDocumentContainer(const arkjson::Value& root,const char* path,
                           std::string* error=nullptr,DocumentContainerWriteStats* stats=nullptr,
                           const DocumentDerivedPayloads* derived=nullptr);
// Derived bytes are optional transport, never validated geometry. A damaged
// derived section is discarded independently of strictly checked authoring.
bool loadDocumentContainer(const char* path,arkjson::Value& root,std::string* error=nullptr,
                           DocumentDerivedPayloads* derived=nullptr,std::string* derivedDiscardedReason=nullptr);

// Deterministic typed codec, useful to other owned persistence components.
// Object keys are sorted, numbers retain their exact finite double value.
bool encodeDocumentValue(const arkjson::Value& value,std::string& bytes,std::string* error=nullptr);
bool decodeDocumentValue(const std::string& bytes,arkjson::Value& value,std::string* error=nullptr);

} // namespace ark
