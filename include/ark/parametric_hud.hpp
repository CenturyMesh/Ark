#pragma once
// 048B Parametric HUD: optional human-facing exposure of named document
// parameters. Values stay ordinary param.set expressions; the HUD only adds
// label/range/step/unit/group/order metadata and range-checked edits.
#include "ark/model.hpp"
namespace ark {
class AgentSession;
struct HudSpec {
    std::string label, unit="none", group;
    double min=0, max=0, step=1;
    int order=0;
};
constexpr size_t maxHudParameters=64;
// Validates and canonicalizes a hud object. Throws "HUD_INVALID: ..." on any
// unknown field, non-finite bound, min>=max, step<=0 or out-of-range order.
arkjson::Value canonicalHud(const arkjson::Value& hud);
bool hudExposed(const Parameter& parameter);
HudSpec hudSpec(const Parameter& parameter);
// param.set hud:{...}|null on an existing parameter (doc_apply batch).
void applyParameterHud(Document& next,const std::string& name,const arkjson::Value& hud);
// Rejects a batch whose HUD count exceeds the bound or whose new/changed HUD
// parameter evaluates outside [min,max]. Never clamps.
void validateHudTransition(const Document& before,const Document& after);
// Display helpers shared by Studio and the read tools.
std::string hudFormat(double value,const HudSpec& spec);
double hudSnap(double value,const HudSpec& spec); // clamp to range and snap to step from min
// Feature ids whose authored expressions reference the parameter directly or
// through a dependent parameter, sketch dimension, datum, canvas or stamp.
std::vector<u64> hudDependentFeatures(const Document& document,const std::string& parameter,std::vector<std::string>* via=nullptr);
bool callParametricHudTool(AgentSession& session,const std::string& tool,const arkjson::Value& args,arkjson::Value& out);
void appendParametricHudTools(arkjson::Value& tools);
bool onboardingParametricHud(const AgentSession& session,const std::string& topic,arkjson::Value& out);
}
