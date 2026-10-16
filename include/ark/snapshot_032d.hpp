#pragma once
#include "ark/io.hpp"
#include "ark/model.hpp"

namespace ark {
std::vector<SnapshotMaterial> snapshotBodyMaterials(const Document&,const Body&,const Mesh&);
}
