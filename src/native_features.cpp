#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/sketch.hpp"

#include <cmath>
#include <set>
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
    size_t entityIndex = 0;
    for (const SketchEnt& source : named->sk.ents) {
        if (source.construction || source.kind == SketchEnt::Pnt) continue;
        auto anchor = [&](int index) {
            return Vec2{source.dof[static_cast<size_t>(2 * index)],
                        source.dof[static_cast<size_t>(2 * index + 1)]};
        };
        if (source.kind == SketchEnt::Line) {
            if (source.dof.size() != 4) throw std::runtime_error("Native line needs two endpoints");
            V item = edge("line", {anchor(0), anchor(1)});
            item.set("name", V::makeStr(source.name.empty() ? "entity." + std::to_string(entityIndex) : source.name));
            entities.arr.push_back(std::move(item));
        } else if (source.kind == SketchEnt::Circle) {
            if (source.dof.size() != 3) throw std::runtime_error("Native circle needs center and radius");
            V circle = V::makeObj(); circle.set("kind", V::makeStr("circle"));
            circle.set("center", point(anchor(0)));
            circle.set("radius", V::makeNum(source.dof[2]));
            circle.set("name", V::makeStr(source.name.empty() ? "entity." + std::to_string(entityIndex) : source.name));
            entities.arr.push_back(std::move(circle));
        } else if (source.kind == SketchEnt::Spline) {
            size_t segmentIndex = 0;
            for (const auto& segment : sketchSplineSegments(source)) {
                V item = edge("bezier", {segment[0], segment[1], segment[2], segment[3]});
                item.set("name", V::makeStr((source.name.empty() ? "entity." + std::to_string(entityIndex) : source.name) + "." + std::to_string(segmentIndex++)));
                entities.arr.push_back(std::move(item));
            }
        }
        ++entityIndex;
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
        if (key == "kind" || key == "sketch" || key == "name" || key == "op" || key == "reference" || key == "second_reference" || key == "mode" || key == "edges" || key == "plane") return value;
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
    if (feature.nativeSpec.find("base")) {
        V request = feature.nativeSpec;
        for (const char* key : {"width", "thickness", "k_factor"})
            request.set(key, V::makeNum(param(feature, env, key)));
        V parameters = V::makeObj();
        for (const auto& [name, value] : env.vars)
            parameters.set(name, V::makeNum(value));
        request.set("parameters", std::move(parameters));
        return request;
    }
    V request = V::makeObj();
    for (const char* key : {"width", "leg_a", "leg_b", "thickness", "inner_radius", "angle", "k_factor"})
        request.set(key, V::makeNum(param(feature, env, key)));
    if (const V* openings = feature.nativeSpec.find("openings"))
        request.set("openings", numericNative(*openings, env));
    return request;
}

V solidWorker(V model) {
    V request = V::makeObj();
    request.set("operation", V::makeStr("model_solid"));
    request.set("request", std::move(model));
    return nativeWorkerRequest(request);
}

V frameValue(const Mat4& matrix) {
    V value = V::makeArr();
    for (double element : matrix.m) value.arr.push_back(V::makeNum(element));
    return value;
}

V exactLegacyProfile(const Feature& feature, const ExprEnv& env,
                     Document& document, const Body& body) {
    if (!feature.get("sketch").empty()) return profileFromSketch(document, feature.get("sketch"));
    V profile = V::makeObj(), entities = V::makeArr();
    profile.set("frame", frameValue(body.profile.frame));
    if (std::string(feature.get("shape", "rect")) == "circle") {
        V circle = V::makeObj();
        circle.set("kind", V::makeStr("circle"));
        circle.set("name", V::makeStr("outline"));
        circle.set("center", point({feature.num(env, "cx"), feature.num(env, "cy")}));
        circle.set("radius", V::makeNum(feature.num(env, "r", 5)));
        entities.arr.push_back(std::move(circle));
    } else {
        size_t index = 0;
        for (const auto& segment : body.profile.segs) {
            if (segment.type != ProfileSeg::Line)
                throw std::runtime_error("Legacy arc profile needs an explicit native sketch");
            V item = edge("line", {segment.p0, segment.p1});
            item.set("name", V::makeStr("outline." + std::to_string(index++)));
            entities.arr.push_back(std::move(item));
        }
    }
    profile.set("entities", std::move(entities));
    return profile;
}

V exactPrimitive(const Feature& feature, const ExprEnv& env, Document& document,
                 const Body& body) {
    V model = V::makeObj();
    if (feature.type == FeatureType::Box) {
        model.set("op", V::makeStr("box"));
        model.set("width", V::makeNum(feature.num(env, "dx", 10)));
        model.set("depth", V::makeNum(feature.num(env, "dy", 10)));
        model.set("height", V::makeNum(feature.num(env, "dz", 10)));
    } else if (feature.type == FeatureType::Cylinder) {
        model.set("op", V::makeStr("cylinder"));
        model.set("radius", V::makeNum(feature.num(env, "r", 5)));
        model.set("height", V::makeNum(feature.num(env, "h", 10)));
    } else if (feature.type == FeatureType::Sphere) {
        model.set("op", V::makeStr("sphere"));
        model.set("radius", V::makeNum(feature.num(env, "r", 5)));
    } else if (feature.type == FeatureType::Cone) {
        model.set("op", V::makeStr("cone"));
        model.set("radius_bottom", V::makeNum(feature.num(env, "rb", 5)));
        model.set("radius_top", V::makeNum(feature.num(env, "rt", 0)));
        model.set("height", V::makeNum(feature.num(env, "h", 10)));
    } else if (feature.type == FeatureType::Torus) {
        model.set("op", V::makeStr("torus"));
        model.set("major_radius", V::makeNum(feature.num(env, "R", 8)));
        model.set("minor_radius", V::makeNum(feature.num(env, "r", 2)));
    } else if (feature.type == FeatureType::Extrude) {
        model.set("op", V::makeStr("extrude"));
        V profile = exactLegacyProfile(feature, env, document, body);
        profile.set("frame", frameValue(body.profile.frame));
        model.set("profile", std::move(profile));
        model.set("depth", V::makeNum(feature.num(env, "height", 8)));
    } else throw std::runtime_error("Feature has no supported analytic B-rep promotion");
    V result = solidWorker(model);
    if (feature.type == FeatureType::Cylinder || feature.type == FeatureType::Cone ||
        feature.type == FeatureType::Torus) {
        Mat4 orientation = Mat4::rotX(-K_HALF_PI);
        if (feature.type == FeatureType::Torus)
            orientation = Mat4::translation({0,feature.num(env,"r",2),0}) * orientation;
        V moved = V::makeObj();
        moved.set("op", V::makeStr("transform"));
        moved.set("frame", frameValue(orientation));
        moved.set("input_brep", required(result, "brep_base64"));
        moved.set("input_topology", required(result, "topology"));
        result = solidWorker(std::move(moved));
    }
    return result;
}

V solidFeatureRequest(const Feature& feature, const ExprEnv& env,
                     Document& document, std::vector<Body>& preceding) {
    V model = numericNative(feature.nativeSpec, env);
    if (model.t != V::Obj) model = V::makeObj();
    for (const auto& [key, expression] : feature.p) {
        if (key == "op" || key == "sketch" || key == "plane") continue;
        model.set(key, V::makeNum(expressionValue(expression, env)));
    }
    std::string op = feature.get("op", "");
    if (op.empty()) {
        const V& value = required(model, "op");
        if (value.t != V::Str || value.str.empty()) throw std::runtime_error("Native solid op must be text");
        op = value.str;
    }
    if (op == "boolean_union") op = "union";
    if (op == "boolean_cut") op = "cut";
    if (op == "boolean_common" || op == "intersect") op = "common";
    if (op == "pocket" || op == "slot") op = "hole";
    if (op == "rigid_transform") op = "transform";
    model.set("op", V::makeStr(op));
    if (op == "transform" && !model.find("frame")) {
        Mat4 matrix = Mat4::translation({feature.num(env,"tx"),feature.num(env,"ty"),feature.num(env,"tz")})
                    * Mat4::rotZ(deg2rad(feature.num(env,"rz")))
                    * Mat4::rotY(deg2rad(feature.num(env,"ry")))
                    * Mat4::rotX(deg2rad(feature.num(env,"rx")));
        model.set("frame", frameValue(matrix));
    }
    if (op == "extrude" && !model.find("profile"))
        model.set("profile", profileFromSketch(document, feature.get("sketch")));
    if (op == "hole") {
        if (const V* source = model.find("cutter")) {
            V cutter = *source;
            if (const V* kind = cutter.find("kind"))
                if (kind->t == V::Str && kind->str == "profile" && !cutter.find("profile") && !feature.get("sketch").empty())
                    cutter.set("profile", profileFromSketch(document, feature.get("sketch")));
            if (const V* plane = cutter.find("plane")) {
                if (plane->t != V::Str) throw std::runtime_error("Hole cutter plane must be a datum name");
                auto value = [&](const char* key) {
                    const V* found = cutter.find(key);
                    if (!found) return 0.0;
                    if (found->t != V::Num || !std::isfinite(found->num))
                        throw std::runtime_error("Invalid hole cutter offset");
                    return found->num;
                };
                Mat4 placement = resolvePlane(document, plane->str) *
                                 Mat4::translation({value("cx"), value("cy"), value("offset")});
                cutter.set("frame", frameValue(placement));
                model.set("cutter", std::move(cutter));
            }
        }
    }
    if (!feature.inputs.empty()) {
        ensureNativeBody(document, feature.inputs[0], preceding);
        const Body* input = nullptr;
        for (const auto& body : preceding) if (body.id == feature.inputs[0]) input = &body;
        if (!input) throw std::runtime_error("Native solid input missing");
        model.set("input_brep", required(input->nativeData, "brep_base64"));
        if (const V* topology = input->nativeData.find("topology")) model.set("input_topology", *topology);
    }
    if (feature.inputs.size() > 1) {
        if (feature.inputs.size() != 2) throw std::runtime_error("Native solid supports at most two inputs");
        ensureNativeBody(document, feature.inputs[1], preceding);
        const Body* tool = nullptr;
        for (const auto& body : preceding) if (body.id == feature.inputs[1]) tool = &body;
        if (!tool) throw std::runtime_error("Native solid tool missing");
        model.set("tool_brep", required(tool->nativeData, "brep_base64"));
        if (const V* topology = tool->nativeData.find("topology")) model.set("tool_topology", *topology);
        model.set("tool_namespace", V::makeStr("tool." + std::to_string(feature.inputs[1])));
    }
    return model;
}

V legacyOperationResult(const Feature& feature, const ExprEnv& env,
                        Document& document, std::vector<Body>& bodies) {
    if (feature.type != FeatureType::Transform && feature.type != FeatureType::Boolean)
        throw std::runtime_error("Unsupported legacy exact operation");
    if (feature.inputs.empty()) throw std::runtime_error("Legacy operation has no exact input");
    V model = V::makeObj();
    model.set("op", V::makeStr(feature.type == FeatureType::Transform ? "transform" :
                               feature.opInt == 1 ? "cut" : feature.opInt == 2 ? "common" : "union"));
    ensureNativeBody(document, feature.inputs[0], bodies);
    const Body* source = nullptr;
    for (const auto& candidate : bodies) if (candidate.id == feature.inputs[0]) source = &candidate;
    model.set("input_brep", required(source->nativeData, "brep_base64"));
    if (const V* topology = source->nativeData.find("topology")) model.set("input_topology", *topology);
    if (feature.type == FeatureType::Boolean) {
        if (feature.inputs.size() != 2) throw std::runtime_error("Legacy Boolean needs two inputs");
        ensureNativeBody(document, feature.inputs[1], bodies);
        const Body* tool = nullptr;
        for (const auto& candidate : bodies) if (candidate.id == feature.inputs[1]) tool = &candidate;
        model.set("tool_brep", required(tool->nativeData, "brep_base64"));
        if (const V* topology = tool->nativeData.find("topology")) model.set("tool_topology", *topology);
        model.set("tool_namespace", V::makeStr("tool." + std::to_string(feature.inputs[1])));
    } else {
        Mat4 matrix = Mat4::translation({feature.num(env,"tx"),feature.num(env,"ty"),feature.num(env,"tz")})
                    * Mat4::rotZ(deg2rad(feature.num(env,"rz")))
                    * Mat4::rotY(deg2rad(feature.num(env,"ry")))
                    * Mat4::rotX(deg2rad(feature.num(env,"rx")));
        model.set("frame", frameValue(matrix));
    }
    return solidWorker(std::move(model));
}
} // namespace

bool isNativeFeature(FeatureType type) {
    return type == FeatureType::Loft || type == FeatureType::SurfaceFill ||
           type == FeatureType::BSplineSurface || type == FeatureType::Thicken ||
           type == FeatureType::SheetMetal || type == FeatureType::SourceSheet ||
           type == FeatureType::NativeSolid;
}

Body evaluateNativeFeature(const Feature& feature, const ExprEnv& env,
                           Document& document, std::vector<Body>& preceding) {
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
    } else if (feature.type == FeatureType::NativeSolid) {
        result = solidWorker(solidFeatureRequest(feature, env, document, preceding));
    } else result = nativeModelRequest(surfaceRequest(feature, env, document, preceding));
    Body body;
    body.nativeData = std::move(result);
    body.baked = std::make_unique<Mesh>(nativeResultMesh(body.nativeData));
    return body;
}

void ensureNativeBody(Document& document, u64 featureId, std::vector<Body>& bodies) {
    Body* body = nullptr;
    for (auto& candidate : bodies) if (candidate.id == featureId) { body = &candidate; break; }
    if (!body) throw std::runtime_error("Analytic reference feature has no earlier evaluated body");
    if (body->nativeData.find("brep_base64")) return;
    const Feature* feature = document.find(featureId);
    if (!feature) throw std::runtime_error("Analytic reference feature is absent");
    const ExprEnv env = document.buildEnv();
    if (feature->type == FeatureType::Transform || feature->type == FeatureType::Boolean) {
        body->nativeData = legacyOperationResult(*feature, env, document, bodies);
    } else body->nativeData = exactPrimitive(*feature, env, document, *body);
}

Body evaluateExactLegacyOperation(const Feature& feature, const ExprEnv& env,
                                  Document& document, std::vector<Body>& preceding) {
    Body result;
    result.nativeData = legacyOperationResult(feature, env, document, preceding);
    result.baked = std::make_unique<Mesh>(nativeResultMesh(result.nativeData));
    return result;
}
} // namespace ark
