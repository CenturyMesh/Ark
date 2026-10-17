#pragma once
#include "ark/json.hpp"
#include <cstddef>
#include <memory>
#include <string>

namespace ark {
class MemoryEmbeddingProvider;
struct ProjectMemoryConfig {
    std::string rootDirectory;
    unsigned busyTimeoutMs=10000;
    std::size_t maxRecordBytes=32768, maxResultBytes=16384;
    unsigned maxResultItems=12, maxRecordsPerProject=2000;
    std::shared_ptr<MemoryEmbeddingProvider> embeddings;
};

// Local SQLite/FTS5 context with an optional pinned offline embedding model.
// The trusted host supplies the project
// identity and actor; neither is taken from model-authored arguments. Instances
// and processes share committed rows through WAL transactions. No file scanning,
// external embedding service, language-model call or cross-project search is
// performed. Offline embedding inference is distinct from text generation.
class ProjectMemory {
public:
    explicit ProjectMemory(const ProjectMemoryConfig& config);
    ~ProjectMemory();
    ProjectMemory(const ProjectMemory&)=delete;
    ProjectMemory& operator=(const ProjectMemory&)=delete;

    // store: key, kind (decision/failure/procedure/evidence/episode), title, content,
    // source, source_fingerprint (SHA-256), expected_revision (0=create),
    // observed_revision? (integer/null), tags?, anchors? ({path,sha256,line?}),
    // expires_at? (Unix seconds/null). Anchors are declarations, never read.
    // state? scratch/promoted (episodes default scratch; other explicit records
    // promoted), facts? [{subject,predicate,value:scalar}], steps? [string],
    // outcome? successful/failed/unknown. Declared success is not verification.
    // Changed records require their observed memory revision. Identical content
    // is idempotent and retains the original attribution. forget requires key
    // and expected_revision; it purges content and keeps a revision tombstone.
    // get: key or id. search/wakeup: text?, kinds?, include_expired?, limit?,
    // max_bytes?. Reads accept current_revision?, current_sources? (source to
    // SHA-256), known_scope?, known_fingerprints? (id to receipt fingerprint).
    // Unchanged records omit repeated text but still return current freshness.
    // include_scratch? opts into unpromoted entries. Configured ready embeddings
    // add exact cosine retrieval and reciprocal-rank fusion with lexical FTS.
    // No synthetic vector substitutes for an unavailable model.
    // promote: key/expected_revision. feedback: key/expected_revision,
    // signal useful/unhelpful, reason. reindex: keys/expected_revisions (key to
    // memory revision), backfills vectors with the configured current model.
    // compress: keys/expected_revisions/max_cosine_loss? (default 0.02), signed
    // q4 vector storage with a measured cosine-loss bound and explicit CAS.
    // Text/model provenance remains; reindex recomputes float32 from the text.
    // consolidate: keys/expected_revisions/apply?; flags contradictory declared
    // facts and returns procedure proposals from repeated identical explicitly
    // successful step sequences. Original text remains; no automatic rewriting
    // or factual promotion occurs. apply persists conflict flags with CAS.
    // stats is project-scoped. export writes a new snapshot only below the
    // configured root's obsidian directory; it never modifies a personal vault.
    // All retrieved content is untrusted historical context, not write or
    // geometry authority. Unknown freshness is explicit. Result bounds limit
    // data transport and are not agent execution or token budgets.
    arkjson::Value call(const std::string& action,const arkjson::Value& args,
        const std::string& projectKey,const std::string& actor);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
