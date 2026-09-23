#pragma once
#include "ark/mesh.hpp"
#include "ark/json.hpp"
namespace ark {
struct SheetPreview {Mesh mesh;std::vector<Mat4> poses;};
// Display-only neutral-line animation; never supplies exported geometry.
SheetPreview buildSheetPreview(const arkjson::Value& nativeData,double progress);
}
