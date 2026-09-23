#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/sketch.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace ark {
namespace {
using V = arkjson::Value;

const V& required(const V& object, const char* key) {
    const V* found = object.find(key);
    if (!found) throw std::runtime_error(std::string("Missing native ") + key);
    return *found;
}

double expressionValue(const std::string& text, const ExprEnv& env) {
    bool ok = false;
    const double value = evalExpr(text, env, &ok);
    if (!ok || !std::isfinite(value) || std::abs(value) > 1.0e8)
        throw std::runtime_error("Invalid native geometry expression: " + text);
    return value;
}

double param(const Feature& feature, const ExprEnv& env, const char* key) {
    auto found = feature.p.find(key);
    if (found == feature.p.end())
        throw std::runtime_error(std::string("Missing native parameter: ") + key);
    return expressionValue(found->second, env);
}

V point(Vec2 p) {
    V value = V::makeArr();
    value.arr.push_back(V::makeNum(p.x));
    value.arr.push_back(V::makeNum(p.y));
    return value;
}

V edge(const char* kind, std::initializer_list<Vec2> coordinates) {
    V item = V::makeObj(), points = V::makeArr();
    item.set("kind", V::makeStr(kind));
    for (auto coordinate : coordinates) points.arr.push_back(point(coordinate));
    item.set("points", std::move(points));
    return item;
}

V profileFromSketch(Document& document, const std::string& name) {
    NamedSketch* named = document.findSketch(name);
    if (!named || !named->error.empty())
        throw std::runtime_error("Native profile sketch is missing or invalid: " + name);
    V profile = V::makeObj(), frame = V::makeArr(), entities = V::makeArr();
    const Mat4 transform = resolvePlane(document, named->sk.plane);
    for (double value : transform.m) frame.arr.push_back(V::makeNum(value));
    profile.set("frame", std::move(frame));
    for (const SketchEnt& source : named->sk.ents) {
        if (source.construction || source.kind == SketchEnt::Pnt) continue;
        auto anchor = [&](int index) {
            return Vec2{source.dof[static_cast<size_t>(2 * index)],
                        source.dof[static_cast<size_t>(2 * index + 1)]};
        };
        if (source.kind == SketchEnt::Line) {
            if (source.dof.size() != 4) throw std::runtime_error("Native line needs two endpoints");
            entities.arr.push_back(edge("line", {anchor(0), anchor(1)}));
        } else if (source.kind == SketchEnt::Circle) {
            if (source.dof.size() != 3) throw std::runtime_error("Native circle needs center and radius");
            V circle = V::makeObj(); circle.set("kind", V::makeStr("circle"));
            circle.set("center", point(anchor(0)));
            circle.set("radius", V::makeNum(source.dof[2]));
            entities.arr.push_back(std::move(circle));
        } else if (source.kind == SketchEnt::Spline) {
            for (const auto& segment : sketchSplineSegments(source))
                entities.arr.push_back(edge("bezier", {segment[0], segment[1], segment[2], segment[3]}));
        }
    }
    if (entities.arr.empty()) throw std::runtime_error("Native sketch has no profile edges");
    profile.set("entities", std::move(entities));
    return profile;
}

V numericNative(const V& value, const ExprEnv& env, const std::string& key = {}) {
    if (value.t == V::Num) {
        if (!std::isfinite(value.num) || std::abs(value.num) > 1e8)
            throw std::runtime_error("Native geometry number exceeds finite supported bounds");
        return value;
    }
    if (value.t == V::Str) {
        if (key == "kind" || key == "sketch" || key == "name") return value;
        return V::makeNum(expressionValue(value.str, env));
    }
    if (value.t == V::Arr) {
        V result = V::makeArr();
        for (const V& item : value.arr) result.arr.push_back(numericNative(item, env, key));
        return result;
    }
    if (value.t == V::Obj) {
        V result = V::makeObj();
        for (const auto& pair : value.obj)
            result.set(pair.first, numericNative(pair.second, env, pair.first));
        return result;
    }
    return value;
}

V surfaceRequest(const Feature& feature, const ExprEnv& env, Document& document,
                 const std::vector<Body>& preceding) {
    V request = V::makeObj();
    if (feature.type == FeatureType::Loft) {
        request.set("op", V::makeStr("loft"));
        const V& sections = required(feature.nativeSpec, "sections");
        if (sections.t != V::Arr || sections.arr.size() < 2 || sections.arr.size() > 32)
            throw std::runtime_error("Loft needs 2..32 sketch sections");
        V profiles = V::makeArr();
        for (const V& name : sections.arr) {
            if (name.t != V::Str || name.str.empty()) throw std::runtime_error("Loft section must be a sketch name");
            profiles.arr.push_back(profileFromSketch(document, name.str));
        }
        request.set("profiles", std::move(profiles));
        const double solid = param(feature, env, "solid");
        const double ruled = param(feature, env, "ruled");
        if ((solid != 0 && solid != 1) || (ruled != 0 && ruled != 1))
            throw std::runtime_error("Loft solid and ruled must be 0 or 1");
        request.set("solid", V::makeBool(solid == 1));
        request.set("ruled", V::makeBool(ruled == 1));
        if (feature.p.count("max_degree")) request.set("max_degree", V::makeNum(param(feature, env, "max_degree")));
    } else if (feature.type == FeatureType::SurfaceFill) {
        request.set("op", V::makeStr("surface_fill"));
        if (const V* profile = feature.nativeSpec.find("profile"))
            request.set("profile", numericNative(*profile, env));
        else request.set("profile", profileFromSketch(document, feature.get("sketch")));
    } else if (feature.type == FeatureType::BSplineSurface) {
        request.set("op", V::makeStr("surface_nurbs"));
        request.set("poles", numericNative(required(feature.nativeSpec, "poles"), env));
        if (const V* weights = feature.nativeSpec.find("weights"))
            request.set("weights", numericNative(*weights, env));
        if (feature.p.count("u_degree")) request.set("u_degree", V::makeNum(param(feature, env, "u_degree")));
        if (feature.p.count("v_degree")) request.set("v_degree", V::makeNum(param(feature, env, "v_degree")));
    } else if (feature.type == FeatureType::Thicken) {
        request.set("op", V::makeStr("thicken"));
        if (feature.inputs.size() != 1) throw std::runtime_error("Thicken requires one native surface input");
        const Body* source = nullptr;
        for (const Body& body : preceding) if (body.id == feature.inputs.front()) source = &body;
        if (!source) throw std::runtime_error("Thicken input missing");
        const V& brep = required(source->nativeData, "brep_base64");
        if (brep.t != V::Str) throw std::runtime_error("Thicken input has no native B-rep");
        request.set("input_brep", brep);
        request.set("thickness", V::makeNum(param(feature, env, "thickness")));
    }
    return request;
}

V sheetRequest(const Feature& feature, const ExprEnv& env) {
    V request = V::makeObj();
    for (const char* key : {"width", "leg_a", "leg_b", "thickness", "inner_radius", "angle", "k_factor"})
        request.set(key, V::makeNum(param(feature, env, key)));
    if (const V* openings = feature.nativeSpec.find("openings"))
        request.set("openings", numericNative(*openings, env));
    return request;
}
} // namespace

bool isNativeFeature(FeatureType type) {
    return type == FeatureType::Loft || type == FeatureType::SurfaceFill ||
           type == FeatureType::BSplineSurface || type == FeatureType::Thicken ||
           type == FeatureType::SheetMetal || type == FeatureType::SourceSheet;
}

Body evaluateNativeFeature(const Feature& feature, const ExprEnv& env,
                           Document& document, const std::vector<Body>& preceding) {
    V result;
    if (feature.type == FeatureType::SourceSheet) {
        const V& format = required(feature.sourceData, "format");
        const V& version = required(feature.sourceData, "version");
        const V& report = required(feature.sourceData, "report");
        const V& verified = required(report, "source_projection_verified");
        if (format.t != V::Str || format.str != "ark-source-sheet" ||
            version.t != V::Num || version.num != 1 ||
            verified.t != V::Bool || !verified.b)
            throw std::runtime_error("Source sheet package lacks verified native projection");
        result = feature.sourceData;
    }
    else if (feature.type == FeatureType::SheetMetal) {
        V request = V::makeObj();
        request.set("operation", V::makeStr("model_sheet"));
        request.set("request", sheetRequest(feature, env));
        result = nativeWorkerRequest(request);
    } else result = nativeModelRequest(surfaceRequest(feature, env, document, preceding));
    Body body;
    body.nativeData = std::move(result);
    body.baked = std::make_unique<Mesh>(nativeResultMesh(body.nativeData));
    return body;
}
} // namespace ark
