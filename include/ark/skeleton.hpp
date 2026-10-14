#pragma once
#include "ark/model.hpp"

namespace ark {
// A bounded native declarative program, never a shell, Python or machine-code
// execution surface. Scalar inputs remain editable; relations are source-owned.
void setSkeletonSource(Document&, const std::string& source);
void refreshSkeleton(Document&);
// Validate persisted ownership against source declarations before removing or
// replacing generated parameters/features. Does not evaluate or edit geometry.
void validateSkeletonAuthoring(const Document&);
bool setSkeletonInput(Document&, const std::string& name, const std::string& expression);
bool skeletonOwnsParameter(const Document&, const std::string& name);
bool skeletonOwnsFeature(const Document&, u64 id);
arkjson::Value inspectSkeleton(const Document&);
arkjson::Value compileSkeletonPreview(const Document&, const std::string& source);
bool skeletonDocument(const Document&);
std::string skeletonExample();
}
