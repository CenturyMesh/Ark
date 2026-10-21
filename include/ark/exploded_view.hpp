#pragma once
#include "ark/model.hpp"
#include "ark/io.hpp"
#include <array>
namespace ark {
class AgentSession;
// 048B exploded views: named, persisted display layouts over assembly occurrences.
// Ordered steps translate occurrence sets along a world direction by a distance
// (mm, number or parameter expression). Factor t in [0,1] plays the steps in
// order, each in its own slice; with_previous shares the previous step's slice.
// Display transforms only: B-rep, mates and authored placements never change.
constexpr size_t kMaxExplodedViews=16,kMaxExplodedSteps=64,kMaxExplodedStepInstances=256;
struct ExplodedStep {std::vector<u64> nodes;Vec3 direction;double distance=0;size_t slice=0;bool withPrevious=false;std::string label;};
struct ExplodedViewPlan {std::string name;std::vector<ExplodedStep> steps;size_t slices=0;std::vector<u64> missing;};
const arkjson::Value* findExplodedView(const Document&,const std::string& name);
// Evaluates directions, distance expressions and slices. Occurrences removed
// since authoring are listed in missing and contribute no offset.
bool compileExplodedView(const Document&,const arkjson::Value& view,ExplodedViewPlan&,std::string& error);
// Local progress of one step at factor t: linear within its slice, clamped to [0,1].
double explodedStepProgress(const ExplodedViewPlan&,size_t step,double t);
// World translation per occurrence node at factor t. A step naming a group moves
// its descendants; offsets of several steps accumulate.
std::vector<Vec3> explodedOffsets(const Document&,const ExplodedViewPlan&,const std::vector<u64>& nodes,double t);
// Structural persistence checks; instance existence is checked by authoring.
void validateExplodedViewStorage(const arkjson::Value& views);
// doc_apply explode.set/rename/remove. Returns false for other operations.
bool applyExplodedViewCommand(Document&,const arkjson::Value& command,std::string& error);
// Motion graph signal channel bound to a view factor ("explode_view"); empty when unbound.
std::string motionExplodeView(const arkjson::Value& channel);
std::vector<std::string> motionsDrivingExplodedView(const Document&,const std::string& view);

// Visualize: BOM-numbered balloons and a BOM table drawn over an assembly image.
struct VisualizeBomItem {int item=0,quantity=0;u64 feature=0;std::string name,partNumber;};
// Item numbers follow bom_inspect item order.
std::vector<VisualizeBomItem> visualizeBomItems(const Document&,u64 revision);
struct VisualizeAnchor {int item=0;double x=0,y=0;};
struct VisualizeBalloon {int item=0;double ax=0,ay=0,bx=0,by=0,radius=0;};
struct VisualizeOverlayLayout {
    int scale=2;double tableX=0,tableY=0,tableW=0,tableH=0,rowH=0;std::vector<double> columns;
    std::vector<std::array<std::string,3>> rows;size_t hiddenRows=0;std::vector<VisualizeBalloon> balloons;
};
// silhouette = projected model extent {x0,y0,x1,y1} in pixels; balloons ring it and avoid the table.
VisualizeOverlayLayout layoutVisualizeOverlay(int width,int height,const std::vector<VisualizeBomItem>&,const std::vector<VisualizeAnchor>&,const double silhouette[4],bool balloons,bool table);
void drawVisualizeOverlay(Image&,const VisualizeOverlayLayout&,bool darkBackground);
int visualizeTextWidth(const std::string&,int scale);
int drawVisualizeText(Image&,const std::string&,int x,int y,int scale,const u8 rgb[3]);
// Read-only software render of the current assembly, optionally exploded and/or at a
// baked motion time, with balloons and BOM. Display tessellation evidence only.
arkjson::Value visualizeRender(Document&,u64 revision,const arkjson::Value& args,Image& image);
// explode_info and visualize_render. image is filled by visualize_render.
bool callExplodedViewTool(AgentSession&,const std::string& tool,const arkjson::Value& args,arkjson::Value& out,Image* image);
void appendExplodedViewTools(arkjson::Value& tools);
}
