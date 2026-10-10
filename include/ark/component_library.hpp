#pragma once
#include "ark/json.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace ark {
struct ComponentLibraryConfig {
    std::string rootDirectory;
    std::size_t maxStepBytes=64*1024*1024, maxNativeBytes=64*1024*1024;
    unsigned busyTimeoutMs=10000;
};
// Trusted host/OCCT callback, never model-authored verification. It inspects the
// library-owned copied STEP snapshot, returning source_sha256, body_index,
// native_data {brep_base64,report,topology?}. report.brep_valid must be true.
// bodyIndex=-1 means select only when the source has exactly one solid; the
// validator must reject an ambiguous multi-body source rather than guessing.
// bodyIndex=-2 is an explicitly requested full compound (id suffix body:all).
using ComponentValidator=std::function<arkjson::Value(const std::string&,
    const std::string&,int bodyIndex)>;

// SQLite WAL/FTS5 indexed metadata; immutable hash-addressed source STEP and
// native packages live outside the DB. Public operations are thread-safe;
// independent instances/processes use SQLite transactions and busy timeout.
// Standards, materials and requested dimensions remain caller declarations.
// Native validity does not verify a supplier, license or manufacturing norm.
class ComponentLibrary {
public:
    explicit ComponentLibrary(const ComponentLibraryConfig& config);
    ~ComponentLibrary();
    ComponentLibrary(const ComponentLibrary&)=delete;
    ComponentLibrary& operator=(const ComponentLibrary&)=delete;
    // Matching source/body + matching metadata is idempotent. Conflicting
    // metadata throws, preserving the current record; use update with revision.
    arkjson::Value ingest(const std::string& path,const arkjson::Value& metadata,
        const ComponentValidator& validator,int bodyIndex=-1);
    arkjson::Value update(const std::string& id,std::uint64_t expectedRevision,
        const arkjson::Value& metadata);
    // Query fields: text, category, standard, thread_diameter_mm,
    // thread_pitch_mm, length_mm, material, manufacturer, part_number,
    // geometry_verified, limit(1..100), offset(0..1000000). Bound parameters;
    // Washer: inner_diameter_mm/outer_diameter_mm/thickness_mm; electrical:
    // pin_count/pin_pitch_mm/rated_voltage_v/rated_current_a; fastener strings:
    // strength_class/head_type/drive_type. All are indexed caller declarations.
    // no SQL/FTS expression language is exposed to agents.
    arkjson::Value search(const arkjson::Value& query);
    arkjson::Value get(const std::string& id,bool includeNativeData=false);
    std::string resolveStep(const std::string& id,bool verifyDigest=true);
    arkjson::Value info();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
