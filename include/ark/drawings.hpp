#pragma once
#include "ark/model.hpp"

namespace ark {
// Authored drawing data is persistent; projected B-reps and measurements are derived.
void validateDrawing(const arkjson::Value& drawing, bool bound);
arkjson::Value drawingAuthoringSchema();
// 022B source/template recipes use document v37; legacy feature-only sheets stay v36.
bool drawingRequiresVersion37(const arkjson::Value& drawing);
arkjson::Value drawingCatalog(Document&);
arkjson::Value makeDrawingTemplate(Document&, const arkjson::Value& source,
    const std::string& name, const std::string& templateId="ark_precision");
void applyDrawingCommand(Document&, const arkjson::Value& command);
void refreshDrawing(Document&, const std::string& name, u64 observedRevision);
arkjson::Value inspectDrawing(Document&, const arkjson::Value& args, u64 revision);
arkjson::Value exportDrawing(Document&, const arkjson::Value& args, u64 revision);
// Vector presentation of current derived sheets. Curves use the retained display
// samples, not a new exact geometry authority. The caller owns file publication.
std::string drawingPdfPresentation(const std::vector<arkjson::Value>& reports);
}

namespace ark::occt {
arkjson::Value drawingViewNative(const arkjson::Value& request);
// Exact canonical drawing exchange; all derived edge roles and source tags retained.
arkjson::Value drawingDxfNative(const arkjson::Value& request);
}
