#pragma once
#include "ark/model.hpp"
#include <string>

namespace ark {
struct GeometryReference {
    Mat4 frame = Mat4::identity();
    std::string kind;
    double radius = 0;
};

// Names are producer-authored; a stale, split, or deleted reference throws.
GeometryReference resolveGeometryReference(const Document& document, u64 feature,
                                           const std::string& name, double fraction = 0.5);
}
