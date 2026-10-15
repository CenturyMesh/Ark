#pragma once
#include "ark/model.hpp"
#include <unordered_set>
namespace ark {
struct AssemblyExternalIo {u64 openAttempts=0,filesRead=0,bytesRead=0;};
// Process-local cumulative observations of actual external pin file reads.
AssemblyExternalIo assemblyExternalIo();
// Authored recipes only. The caller publishes a candidate through the normal
// revision-checked document transaction and recomputes before committing.
bool applyAssemblyAuthoringCommand(Document&,const arkjson::Value&,std::string&);
void validateAssemblyAuthoring(const Document&);
void verifyAssemblyExternalReferences(const Document&);
// Exact semantic sources used by an authored feature, including named CAD
// resources and placed/patterned occurrences; does not evaluate geometry.
std::unordered_set<u64> assemblyFeatureDependencies(const Document&,const Feature&);
std::unordered_set<u64> assemblyOccurrenceDependencies(const Document&,u64 node);
std::unordered_set<u64> assemblyLoadedFeatures(const Document&);
bool isAssemblyNodeLoaded(const Document&,u64 node);
arkjson::Value inspectAssemblyAuthoring(const Document&,u64 revision);
}
