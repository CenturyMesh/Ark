#pragma once
#include "ark/model.hpp"

namespace ark {

struct StockPreview {
    Mesh mesh;
    arkjson::Value report;
};

// Read-only nominal straight-stock visualization for one authored tube or
// construction frame. Lengths are centerline developments from the existing
// C++ evaluation; no folded/source geometry or manufacturing claim is changed.
StockPreview buildStockPreview(const Feature& feature,const ExprEnv& env,
                               const arkjson::Value& nativeData);

} // namespace ark
