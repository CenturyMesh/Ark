#include "ark/curve_pattern_044b.hpp"
#include "ark/native_features.hpp"
#include <set>
#include <stdexcept>
#include <string>

namespace ark {
namespace {
using V=arkjson::Value;
const std::set<std::string>& fields044B(){static const std::set<std::string> keys{"references","pitch_mm","pitch_mode","start_margin_mm","end_margin_mm","phase_mm","skip","reverse","orientation","support_faces","project","offset_mm","max_deviation_mm","flip_normal","reference_scheme","named_copies"};return keys;}
}
bool curvePattern044BSpec(const Feature& feature){
    if(feature.type!=FeatureType::NativeSolid||featureOperation(feature)!="curve_pattern")return false;if(feature.inputs.size()==3)return true;
    for(const auto& field:feature.nativeSpec.obj)if(fields044B().count(field.first))return true;for(const auto& field:feature.p)if(fields044B().count(field.first))return true;
    return !feature.nativeSpec.find("reference")&&!feature.p.count("reference");
}
bool curvePattern044BNeedsVersion47(const Document& document){for(const auto& feature:document.features)if(curvePattern044BSpec(feature))return true;return false;}
void curvePatternHostInputs044B(const Feature& feature,Document& document,std::vector<Body>& preceding,const Body& path,V& model){
    if(feature.inputs.size()==3){
        if(feature.inputs[2]==feature.inputs[0])throw std::runtime_error("Curve pattern support must differ from the seed");
        ensureNativeBody(document,feature.inputs[2],preceding);const Body* support=nullptr;for(const auto& body:preceding)if(body.id==feature.inputs[2])support=&body;
        if(!support)throw std::runtime_error("Curve pattern support input missing");const auto* brep=support->nativeData.find("brep_base64");if(!brep||brep->t!=V::Str)throw std::runtime_error("Curve pattern support needs an exact B-rep");
        model.set("support_brep",*brep);if(const auto* topology=support->nativeData.find("topology"))model.set("support_topology",*topology);
        const auto* orientation=model.find("orientation");if(!orientation||orientation->t!=V::Str||orientation->str!="surface")throw std::runtime_error("A curve pattern support input requires orientation surface");
    }
    if(model.find("reference")||model.find("references"))return;
    if(const auto* source=document.find(feature.inputs[1]);source&&source->type==FeatureType::CableRoute){
        const auto* electronics=path.nativeData.find("electronics");const auto* centerline=electronics?electronics->find("centerline"):nullptr;
        if(!centerline)throw std::runtime_error("This cable route has no exact centerline B-spline; use a spline cable or select a curve feature path");model.set("tool_centerline",*centerline);
    }
}
void curvePatternAgentFields044B(const V& args,V& native){
    for(const auto& key:fields044B())if(const auto* value=args.find(key.c_str()))native.set(key,*value);
}
}
