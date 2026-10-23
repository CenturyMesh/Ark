// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ark/core.hpp"
#include "ark/json.hpp"

namespace ark {

inline constexpr uint32_t RUBBER_PATTERN_MAX_COUNT = 1000000;
inline constexpr uint32_t RUBBER_PATTERN_MAX_PREVIEW = 256;

struct RubberPatternFrame {
    uint32_t index = 0;
    Vec3 position_mm, tangent, bitangent, normal;
    Mat4 transform; // Columns: tangent (x), bitangent (y), outward normal (z), position.
    double u_rad = 0, v_rad = 0;
    // Surface coordinates before placement/offset. u is wrapped to [0, 2*pi).
    // v is torus section angle or sphere polar angle; zero on cylinder/racetrack.
    double transverse_mm = 0; // Cylinder axial / racetrack width coordinate; otherwise zero.
};

// Immutable, compact analytical recipe. Construction and indexed access do not
// enumerate instances or build geometry. frame() is constant work and allocates
// no memory. Copying the plan shares immutable data, safe for concurrent reads.
// Invalid inputs throw std::invalid_argument; an invalid index throws
// std::out_of_range. No implicit field coercion or ignored unsupported fields.
class RubberPatternPlan {
public:
    explicit RubberPatternPlan(const arkjson::Value& recipe);
    uint32_t count() const;
    RubberPatternFrame frame(uint32_t index) const;
    arkjson::Value diagnostics() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

// Schema/capabilities describe placement recipes only. The caller must bind a
// recipe to an authored surface and choose visual instances or explicit geometry
// with its normal revision/atomic-publication contract. Motif names are semantic
// hints; this module does not create solids or qualify rubber manufacturing.
arkjson::Value rubberPatternSchema();
arkjson::Value rubberPatternCapabilities();
arkjson::Value rubberPatternFrameJson(const RubberPatternFrame& frame);
arkjson::Value previewRubberPattern(const arkjson::Value& recipe,
                                  uint32_t offset = 0, uint32_t limit = 64);

} // namespace ark
