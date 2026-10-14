#pragma once
#include "ark/model.hpp"

namespace ark {
// Prepare concrete existing-authoring commands without publishing them.
// The host must apply the commands atomically at the observed revision.
// Actions: sketch_to_plane, offset_plane, section, section_from_line.
arkjson::Value planeSectionRecipe(const Document& document,
                                 const arkjson::Value& arguments);
}
