#pragma once

#include "ark/model.hpp"

namespace ark {

// Read-only mechanism controls. A rack translation steers through the tie rod;
// it is not a prescribed wheel angle. Jounce moves the lower ball joint in Z.
struct SuspensionState {
    double jounceMm=0;
    double rackMm=0;
    double wheelSpinDeg=0;
};

struct SuspensionPart {
    std::string name;
    Mesh mesh;                 // display mesh in feature coordinates
    Mat4 pose=Mat4::identity();
    Vec3 color{0.7,0.7,0.7};  // display-only sRGB part tint
};

struct SuspensionPreview {
    Mesh mesh;                 // combined display preview
    std::vector<SuspensionPart> parts;
    arkjson::Value report;    // solved hardpoints, closure and spring evidence
};

SuspensionPreview buildSuspensionPreview(const Feature& feature,const ExprEnv& env,
                                          const SuspensionState& state={});
// Evaluates the authored motion envelope before accepting a feature, then
// returns its nominal display body. No B-rep or dynamic simulation is implied.
Body evaluateSuspensionFeature(const Feature& feature,const ExprEnv& env);

} // namespace ark
