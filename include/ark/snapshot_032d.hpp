#pragma once
#include "ark/io.hpp"
#include "ark/model.hpp"

namespace ark {
struct SnapshotAppearanceOptions044B;
// 044B: with options, attaches triplanar textures, patterns and surface stamps.
std::vector<SnapshotMaterial> snapshotBodyMaterials(const Document&,const Body&,const Mesh&,SnapshotAppearanceOptions044B* options=nullptr);
}
