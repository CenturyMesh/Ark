#pragma once
#include "ark/model.hpp"
#include <unordered_map>
namespace ark {
class AgentSession;
// 050B native drag chain (energy chain / cable carrier). A chain is a document recipe
// (Document::dragChains) owning one native B-rep link definition (native_solid op
// drag_chain_link) and N ordinary occurrences of it under one group. Link placements are
// driven followers: every assembly resolve derives them from the solved carriage pose, so
// static poses, assembly.drive previews, motion bakes, picking and clearance use the real
// link solids without motion channels per link.
constexpr size_t kMaxDragChains=16;constexpr int kMaxDragChainLinks=400;
// Chain plane in world space: origin is the fixed-end pin, direction the unit run direction
// from the fixed bracket towards the bend, bend the unit direction from the fixed run to the
// moving run. The moving run lies at 2R along bend; carriage c=(C-origin).direction.
struct DragChainGeometry {double pitch=0,radius=0;int links=0;Vec3 origin,direction,bend;};
struct DragChainLayout {
    double carriage=0,fixedRun=0,movingRun=0; // fixedRun: pin 0 to the bend tangent point; movingRun: bend end to pin N
    int fixedLinks=0,bendLinks=0,movingLinks=0;
    std::vector<Vec3> pins;   // links+1 pin centres on the path; pin 0 at origin, pin N at the carriage pin
    std::vector<double> arc;  // arc length of each pin along the continuous path (straight, semicircle, straight)
    std::vector<Mat4> links;  // link frames: origin at the rear pin, X to the front pin, Y=bend x direction, Z towards the loop centre
    Vec3 apex;                // loop apex on the pin line
};
// Exact rigid-link solution: pins lie on the path (fixed straight, semicircle of radius R,
// moving straight) and consecutive pins are exactly one pitch apart. Fixed-run pins are
// origin+i*pitch*direction for every carriage value; moving-run pins are rigid with the
// carriage pin. Throws DRAG_CHAIN_RUN_NEGATIVE when either straight run would be negative.
DragChainLayout layoutDragChain(const DragChainGeometry&,double carriage);
// Continuous arc-length model: L_fixed=(L_total-pi*R+c)/2, the mean of the rigid-link solution.
inline double dragChainContinuousFixedRun(double total,double radius,double carriage){return (total-K_PI*radius+carriage)/2;}
// Link section checks shared by authoring and the native producer: wall/crossbar thickness and
// a bend radius at which adjacent links clear each other. Throws DRAG_CHAIN_GEOMETRY/BEND_TOO_TIGHT.
void validateDragChainSection(double pitch,double outerWidth,double outerHeight,double innerWidth,double innerHeight,double radius);
const arkjson::Value* findDragChain(const Document&,const std::string& name);
// Structural persistence checks; references are checked by every assembly resolve.
void validateDragChainStorage(const arkjson::Value& chains);
// doc_apply drag_chain.set/remove. Returns false for other operations. The caller evaluates
// geometry first; set resolves the candidate assembly and probes the joint travel.
bool applyDragChainCommand(Document&,const arkjson::Value& command,std::string& error);
// Read-only state of one chain at the given copy-0 world frames (Studio display, playback).
arkjson::Value dragChainState(const Document&,const std::string& name,const std::unordered_map<u64,Mat4>& world);
// Name of the chain driving this occurrence and its 0-based link index, or empty.
std::string dragChainOfNode(const Document&,u64 node,int* index=nullptr);
bool callDragChainTool(AgentSession&,const std::string& tool,const arkjson::Value& args,arkjson::Value& out);
void appendDragChainTools(arkjson::Value& tools);
// assembly/motion onboarding section.
void appendDragChainOnboarding(const std::string& topic,arkjson::Value& out);
}
