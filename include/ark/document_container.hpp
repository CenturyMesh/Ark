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
// 051A: decoded entry bytes (memory) and compressed pack bytes (file) are bounded separately.
inline constexpr size_t maxDocumentDerivedBytes=256*1024*1024;
inline constexpr size_t maxDocumentDerivedStoredBytes=128*1024*1024;
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
    // 051A derived pack: entries written, their decoded and stored bytes, and the
    // tail omitted when the compressed pack would exceed its stored capacity.
    size_t derivedEntries=0,derivedDecodedBytes=0,derivedStoredBytes=0,derivedOmittedEntries=0,derivedOmittedBytes=0;
    bool derivedRewritten=false;
    // 051A compaction: an incremental save that leaves at least a quarter of the
    // file free is compacted by transactional SQLite VACUUM, preserving writer locks.
    bool compacted=false;
    size_t bytesBeforeCompaction=0;
    std::string compactionSkipped;
};

struct DocumentContainerReadStats {
    std::string derivedEncoding="none"; // none, entries (container v2 before 051A) or zstd_pack
    size_t derivedStoredBytes=0,derivedDecodedBytes=0;
};

// SHA-256 used by the container for payloads and derived rows.
std::string documentContainerDigest(const std::string& bytes);

// This identifies any SQLite file. Ownership/version/schema checks happen on load.
bool documentContainerFile(const char* path);
bool saveDocumentContainer(const arkjson::Value& root,const char* path,
                           std::string* error=nullptr,DocumentContainerWriteStats* stats=nullptr,
                           const DocumentDerivedPayloads* derived=nullptr);
// Derived bytes are optional transport, never validated geometry. A damaged
// derived section is discarded independently of strictly checked authoring.
bool loadDocumentContainer(const char* path,arkjson::Value& root,std::string* error=nullptr,
                           DocumentDerivedPayloads* derived=nullptr,std::string* derivedDiscardedReason=nullptr,
                           DocumentContainerReadStats* readStats=nullptr);

// Deterministic typed codec, useful to other owned persistence components.
// Object keys are sorted, numbers retain their exact finite double value.
bool encodeDocumentValue(const arkjson::Value& value,std::string& bytes,std::string* error=nullptr);
bool decodeDocumentValue(const std::string& bytes,arkjson::Value& value,std::string* error=nullptr);

} // namespace ark
