#pragma once
#include "ark/document_container.hpp"
#include <cstddef>
#include <string>

namespace ark {

// 051A: the optional derived section is one zstd frame (a "pack") over every
// entry in caller order, split across rows named pack1.<manifest>.<index>.
// Long canonical base64 JSON strings travel as their decoded bytes, so B-rep
// text shared by successive feature bodies compresses across entries. Decoding
// reproduces each entry byte for byte; derived data stays an optional cache.
inline constexpr char derivedPackRowPrefix[]="pack1.";
inline constexpr size_t derivedPackChunkBytes=16*1024*1024;
inline constexpr size_t derivedPackMinimumSplice=1024;

struct DerivedPackReport {
    size_t entries=0,decodedBytes=0,storedBytes=0,splicedStrings=0,splicedBytes=0;
    size_t omittedEntries=0,omittedBytes=0;
};

// SHA-256 over the keys, lengths and per-entry digests of the first `count`
// records in order; it names the pack rows and is checked again after decoding.
std::string derivedPackManifest(const DocumentDerivedPayloads& records,size_t count);
// Encodes the longest record prefix whose pack fits `storedLimit`; the report
// counts the omitted tail. An empty result means no entry fits.
std::string encodeDerivedPack(const DocumentDerivedPayloads& records,size_t storedLimit,DerivedPackReport* report=nullptr);
// Rejects truncated, trailing, oversized or inconsistent packs by throwing.
DocumentDerivedPayloads decodeDerivedPack(const std::string& stored,DerivedPackReport* report=nullptr);

} // namespace ark
