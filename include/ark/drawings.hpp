#pragma once
#include "ark/model.hpp"

namespace ark {
// Authored drawing data is persistent; projected B-reps and measurements are derived.
void validateDrawing(const arkjson::Value& drawing, bool bound);
arkjson::Value drawingAuthoringSchema();
void applyDrawingCommand(Document&, const arkjson::Value& command);
void refreshDrawing(Document&, const std::string& name, u64 observedRevision);
arkjson::Value inspectDrawing(Document&, const arkjson::Value& args, u64 revision);
arkjson::Value exportDrawing(Document&, const arkjson::Value& args, u64 revision);
}

namespace ark::occt {
arkjson::Value drawingViewNative(const arkjson::Value& request);
}
