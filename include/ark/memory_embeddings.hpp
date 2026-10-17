#pragma once
#include "ark/json.hpp"
#include <memory>
#include <string>
#include <vector>

namespace ark {
struct MemoryEmbedding {
    std::vector<float> values;
    std::string modelFingerprint;
};

// Provider implementations never turn unavailable inference into synthetic
// vectors. Persist the fingerprint with every vector; compare like models only.
class MemoryEmbeddingProvider {
public:
    virtual ~MemoryEmbeddingProvider() = default;
    virtual MemoryEmbedding embed(const std::string& text) = 0;
    virtual std::vector<MemoryEmbedding> embedBatch(const std::vector<std::string>& texts);
    virtual arkjson::Value health() const = 0;
};

// Loads only pinned files from this explicit directory. No download, process,
// model service or environment/global configuration is used at runtime.
// The returned provider reports ready=false with an error if assets cannot load.
std::shared_ptr<MemoryEmbeddingProvider> makeLocalMemoryEmbeddings(const std::string& assetDirectory);
}
