#include "ark/cad_import.hpp"
#include "ark/native_features.hpp"
#include "ark/assembly_authoring.hpp"
#include "ark/native_backend.hpp"
#include "ark/references.hpp"
#include "ark/sketch.hpp"
#include "ark/tube.hpp"
#include "ark/frame.hpp"
#include "ark/electronics.hpp"
#include "ark/spatial_sketch.hpp"
#include "ark/modeling_031b.hpp"
#include "ark/curve_pattern_044b.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace ark {
bool profileRegions046BNeedsVersion48(const Document& document){
    for(const auto& feature:document.features)if((feature.type==FeatureType::NativeSolid||feature.type==FeatureType::ExtrudeSurface||feature.type==FeatureType::RevolveSurface||feature.type==FeatureType::Sweep)&&feature.nativeSpec.t==arkjson::Value::Obj&&feature.nativeSpec.find("regions"))return true;
    return false;
}
arkjson::Value inspectNativeCollision(const Body& a,const Mat4& aWorld,const Body& b,const Mat4& bWorld,double tolerance){
    if(tolerance!=1e-6)throw std::runtime_error("Collision uses fixed 1e-6 mm native contact tolerance");
    arkjson::Value request=arkjson::Value::makeObj();request.set("op",arkjson::Value::makeStr("collision"));
    auto body=[&](const Body& source,const Mat4& world,const char* brepKey,const char* transformKey){
        const auto* encoded=source.nativeData.find("brep_base64");if(!encoded||encoded->t!=arkjson::Value::Str||encoded->str.empty())throw std::runtime_error("Collision requires exact native solid bodies");
        request.set(brepKey,*encoded);arkjson::Value frame=arkjson::Value::makeArr();for(double value:world.m)frame.arr.push_back(arkjson::Value::makeNum(value));request.set(transformKey,frame);
    };
    body(a,aWorld,"a_brep","a_transform");body(b,bWorld,"b_brep","b_transform");
    request.set("tolerance",arkjson::Value::makeNum(tolerance));return nativeModelRequest(request);
}
namespace {
using V = arkjson::Value;

template<class Object> const V& required(const Object& object, const char* key) {
    const V* found = object.find(key);
    if (!found) throw std::runtime_error(std::string("Missing native ") + key);
    return *found;
}

void rejectDerivedSpatialHandles(const Feature& feature){
    const V* operation=feature.nativeSpec.find("op");if(!operation||operation->t!=V::Str||(operation->str!="surface_spline"&&operation->str!="project_surface"&&operation->str!="project"&&operation->str!="equation"&&operation->str!="helix"))return;
    bool handles=feature.nativeSpec.find("constraints")||feature.nativeSpec.find("start_ref")||feature.nativeSpec.find("to_ref")||feature.nativeSpec.find("through_ref");if(const V* segments=feature.nativeSpec.find("segments");segments&&segments->t==V::Arr)for(const V& segment:segments->arr)handles|=segment.find("to_ref")||segment.find("through_ref");
    if(handles)throw std::runtime_error("Spatial point constraints and datum point anchors require authored line, arc, NURBS or spline segments; derived "+operation->str+" curves do not expose those solver handles");
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
        throw std::runtime_error(std::string("Missing native parameter: ") + key + " (an expression in the feature's p object, e.g. p:{\"" + key + "\":1})");
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

V profileFromSketch(Document& document, const std::string& name,const NamedSketch* supplied=nullptr,std::vector<size_t>* origins=nullptr) {
    const NamedSketch* named = supplied?supplied:document.findSketch(name);
    if (!named)
        throw std::runtime_error("Native profile sketch is missing or invalid: " + name);
    if (!named->error.empty())
        throw std::runtime_error("Native profile sketch is invalid: " + name + "; " + named->error);
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
        if(source.kind==SketchEnt::Recipe){
            if(source.recipeGeometry.t!=V::Arr)throw std::runtime_error("Entity recipe is not evaluated");
            size_t primitive=0;for(auto item:source.recipeGeometry.arr){item.set("name",V::makeStr((source.name.empty()?"entity."+std::to_string(entityIndex):source.name)+"."+std::to_string(primitive++)));entities.arr.push_back(std::move(item));}
        } else if(source.kind==SketchEnt::Project){
            if(source.projectionBrep.empty())throw std::runtime_error("Exact projected sketch entity is not evaluated");
            V item=V::makeObj();item.set("kind",V::makeStr("brep"));item.set("brep_base64",V::makeStr(source.projectionBrep));item.set("name",V::makeStr(source.name.empty()?"entity."+std::to_string(entityIndex):source.name));entities.arr.push_back(item);
        } else if (source.kind == SketchEnt::Line) {
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
        } else if (source.kind == SketchEnt::Arc) {
            if (source.dof.size() != 5) throw std::runtime_error("Native arc needs center, radius and angles");
            V arc = V::makeObj();
            arc.set("kind", V::makeStr("arc"));
            arc.set("center", point({source.dof[0], source.dof[1]}));
            arc.set("radius", V::makeNum(source.dof[2]));
            arc.set("a0", V::makeNum(source.dof[3]));
            arc.set("a1", V::makeNum(source.dof[4]));
            arc.set("name", V::makeStr(source.name.empty() ? "entity." + std::to_string(entityIndex) : source.name));
            entities.arr.push_back(std::move(arc));
        } else if (source.kind == SketchEnt::Wire || source.kind == SketchEnt::Offset) {
            bool trimmed = false;
            for (const auto& corner : named->sk.corners)
                if (corner.entity == source.name && !source.name.empty()) trimmed = true;
            if (trimmed) {
                const std::string base = source.name.empty() ? "entity." + std::to_string(entityIndex) : source.name;
                int curveIndex = 0;
                for (const auto& curve : sketchPresentedCurves(named->sk, source)) {
                    if (curve.kind == SketchCurve::Arc) {
                        V arc = V::makeObj();
                        arc.set("kind", V::makeStr("arc"));
                        arc.set("center", point(curve.center));
                        arc.set("radius", V::makeNum(curve.radius));
                        arc.set("a0", V::makeNum(curve.a0));
                        arc.set("a1", V::makeNum(curve.a1));
                        arc.set("name", V::makeStr(base + ".corner." + std::to_string(curve.corner)));
                        entities.arr.push_back(std::move(arc));
                    } else {
                        V item = edge("line", {curve.a, curve.b});
                        item.set("name", V::makeStr(curve.corner >= 0 ? base + ".corner." + std::to_string(curve.corner) : base + "." + std::to_string(curveIndex)));
                        entities.arr.push_back(std::move(item));
                    }
                    ++curveIndex;
                }
            } else if (source.dof.size() < 2) throw std::runtime_error("Wire requires start and segment endpoints");
            else {
            Vec2 cursor{source.dof[0], source.dof[1]};
            auto xy = [&](int index) { return Vec2{source.dof.at(static_cast<size_t>(index)), source.dof.at(static_cast<size_t>(index + 1))}; };
            for (int i = 0; i < static_cast<int>(source.segments.size()); ++i) {
                const auto& segment = source.segments[static_cast<size_t>(i)];
                // Payload starts at this segment's endpoint (or first Bezier
                // control), not two scalars after the previous endpoint: arcs
                // also carry a radius between consecutive vertices.
                const int payload = sketchVertexDof(source, i + 1)
                                  - (segment.kind == SketchEnt::WireSeg::Cubic ? 4 : 0);
                const std::string edgeName = (source.name.empty() ? "entity." + std::to_string(entityIndex) : source.name) + "." + std::to_string(i);
                if (segment.kind == SketchEnt::WireSeg::Line) {
                    const Vec2 end = xy(payload);
                    V item = edge("line", {cursor, end});
                    item.set("name", V::makeStr(edgeName));
                    entities.arr.push_back(std::move(item));
                    cursor = end;
                } else if (segment.kind == SketchEnt::WireSeg::Arc) {
                    const Vec2 end = xy(payload);
                    const double radius = source.dof.at(static_cast<size_t>(payload + 2));
                    Vec2 center; double a0 = 0, a1 = 0;
                    if (!sketchMinorArc(cursor, end, radius, segment.sweep, center, a0, a1))
                        throw std::runtime_error("Wire arc is not a minor arc");
                    V arc = V::makeObj();
                    arc.set("kind", V::makeStr("arc"));
                    arc.set("center", point(center));
                    arc.set("radius", V::makeNum(radius));
                    arc.set("a0", V::makeNum(a0));
                    arc.set("a1", V::makeNum(a1));
                    arc.set("name", V::makeStr(edgeName));
                    entities.arr.push_back(std::move(arc));
                    cursor = end;
                } else if (segment.kind == SketchEnt::WireSeg::Cubic) {
                    const Vec2 end = xy(payload + 4);
                    V item = edge("bezier", {cursor, xy(payload), xy(payload + 2), end});
                    item.set("name", V::makeStr(edgeName));
                    entities.arr.push_back(std::move(item));
                    cursor = end;
                } else throw std::runtime_error("Wire segment kind must be line, arc or cubic");
            }
            }
        }
        if (origins) while (origins->size() < entities.arr.size()) origins->push_back(static_cast<size_t>(&source - named->sk.ents.data()));
        ++entityIndex;
    }
    if (entities.arr.empty()) {
        const SketchEnt* only = nullptr;
        int points = 0;
        for (const SketchEnt& source : named->sk.ents) {
            if (source.construction || source.kind != SketchEnt::Pnt) continue;
            only = &source;
            ++points;
        }
        if (points != 1 || !only || only->dof.size() < 2)
            throw std::runtime_error("Native sketch has no profile edges");
        profile.set("point", point({only->dof[0], only->dof[1]}));
        return profile;
    }
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
        if (key == "grain_axis" || key == "source_vertex" || key == "source_topology" || key == "kind" || key == "join" || key == "sketch" || key == "name" || key == "op" || key == "reference" || key == "second_reference" || key == "mode" || key == "end" || key == "edges" || key == "faces" || key == "face" || key == "source_face" || key == "limit_face" || key == "plane" || key == "keep" || key == "selection" || key == "direction" || key == "axis" || key == "neutral" || key == "reference_face" || key == "tool_face" || key == "equivalence" || key == "interpolation" || key == "references" || key == "pitch_mode" || key == "orientation" || key == "support_faces" || key == "reference_scheme" || key == "entity") return value;
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

V profileArgument(const Feature& feature, const ExprEnv& env, Document& document, const std::vector<Body>& preceding, const char* key);

// 046B profile regions. A single connected sketch without native.regions keeps
// the unchanged legacy payload (byte-identical requests). Several closed loops
// or an authored selection add the region contract; the OCCT worker resolves
// loops, containment depth and seeds from exact edges.
V regionProfileFromSketch(Document& document, const std::string& name, const V* regions, const ExprEnv& env) {
    std::vector<size_t> origins;
    V profile = profileFromSketch(document, name, nullptr, &origins);
    const NamedSketch* named = document.findSketch(name);
    if (!regions) {
        size_t components = 1;
        try { components = sketchProfileComponents(named->sk); } catch (const std::exception&) {}
        if (components < 2) return profile;
    }
    if (!profile.find("entities")) throw std::runtime_error("Profile regions need closed sketch curves; sketch " + name + " has none");
    V spec = V::makeObj();
    if (regions) {
        if (regions->t != V::Arr || regions->arr.empty() || regions->arr.size() > 64)
            throw std::runtime_error("native.regions needs 1..64 seeds: {point:[x,y]} in sketch coordinates or {entity:name} naming an outer-loop curve");
        V select = V::makeArr();
        for (size_t index = 0; index < regions->arr.size(); ++index) {
            const V& seed = regions->arr[index];
            const std::string label = "native.regions[" + std::to_string(index) + "]";
            if (seed.t != V::Obj || seed.obj.size() != 1) throw std::runtime_error(label + " must be exactly one of {point:[x,y]} or {entity:name}");
            V row = V::makeObj();
            if (const V* at = seed.find("point")) {
                V xy = numericNative(*at, env);
                if (xy.t != V::Arr || xy.arr.size() != 2 || xy.arr[0].t != V::Num || xy.arr[1].t != V::Num)
                    throw std::runtime_error(label + ".point must be [x,y] numbers or expressions in sketch coordinates");
                row.set("point", std::move(xy));
            } else if (const V* entity = seed.find("entity")) {
                if (entity->t != V::Str || entity->str.empty()) throw std::runtime_error(label + ".entity must be a sketch entity name");
                int found = -1;
                for (size_t i = 0; i < named->sk.ents.size(); ++i) if (named->sk.ents[i].name == entity->str) {
                    if (found >= 0) throw std::runtime_error(label + " entity '" + entity->str + "' is ambiguous in sketch " + name);
                    found = static_cast<int>(i);
                }
                if (found < 0) throw std::runtime_error(label + " entity '" + entity->str + "' is not in sketch " + name);
                const SketchEnt& source = named->sk.ents[static_cast<size_t>(found)];
                if (source.construction || source.kind == SketchEnt::Pnt) throw std::runtime_error(label + " entity '" + entity->str + "' is construction or a point, not a profile curve");
                row.set("entity_index", V::makeNum(found));
                row.set("entity", *entity);
            } else throw std::runtime_error(label + " must be exactly one of {point:[x,y]} or {entity:name}");
            select.arr.push_back(std::move(row));
        }
        spec.set("select", std::move(select));
    }
    for (auto& field : profile.obj) if (field.first == "entities")
        for (size_t i = 0; i < field.second.arr.size() && i < origins.size(); ++i) {
            const SketchEnt& source = named->sk.ents[origins[i]];
            field.second.arr[i].set("source", V::makeNum(static_cast<double>(origins[i])));
            field.second.arr[i].set("entity", V::makeStr(source.name.empty() ? "sketch entity #" + std::to_string(origins[i]) : source.name));
        }
    profile.set("regions", std::move(spec));
    return profile;
}

bool bitParam(const Feature& feature, const ExprEnv& env, const char* key, double fallback) {
    const double value = feature.p.count(key) ? param(feature, env, key) : fallback;
    if (value != 0.0 && value != 1.0) throw std::runtime_error(std::string(key) + " must be 0 or 1");
    return value == 1.0;
}

const Body* precedingBody(const std::vector<Body>& preceding, u64 id) {
    for (const Body& body : preceding) if (body.id == id) return &body;
    return nullptr;
}

V brepOf(const Body& body, const char* what) {
    const V* brep = body.nativeData.find("brep_base64");
    if (!brep || brep->t != V::Str || brep->str.empty())
        throw std::runtime_error(std::string(what) + " has no native B-rep");
    return *brep;
}

const Feature* uniqueFeature(const Document& document, const std::string& name) {
    const Feature* found = nullptr;
    for (const auto& feature : document.features) if (feature.name == name) {
        if (found) throw std::runtime_error("Ambiguous feature name: " + name);
        found = &feature;
    }
    return found;
}

V profileFromCurve(const std::vector<Body>& preceding, const Feature& feature) {
    const Body* body = precedingBody(preceding, feature.id);
    if (!body) throw std::runtime_error("Curve feature must precede its consumer");
    V profile = V::makeObj();
    profile.set("wire_brep", brepOf(*body, "Curve"));
    return profile;
}

V resolveProfileName(Document& document, const std::vector<Body>& preceding, const std::string& name, const char* role) {
    if (name.empty()) throw std::runtime_error(std::string(role) + " name is empty");
    if (document.findSketch(name)) return profileFromSketch(document, name);
    for (const auto& plane : document.planes) if (plane.name == name) {
        if (std::string(role) == "section" && plane.kind == "point") {
            V profile = V::makeObj(), frame = V::makeArr(), tip = V::makeArr();
            for (double value : resolvePlane(document, name).m) frame.arr.push_back(V::makeNum(value));
            tip.arr.push_back(V::makeNum(0));
            tip.arr.push_back(V::makeNum(0));
            profile.set("frame", std::move(frame));
            profile.set("point", std::move(tip));
            return profile;
        }
        break;
    }
    if (const Feature* feature = uniqueFeature(document, name); feature && isCurveFeature(feature->type))
        return profileFromCurve(preceding, *feature);
    if (std::string(role) == "guide") throw std::runtime_error("Loft guide must be a sketch or curve name");
    if (std::string(role) == "section") throw std::runtime_error("Loft section must be a sketch, point or curve name");
    throw std::runtime_error(std::string(role) + " must be a sketch name or a profile object");
}

V profileArgument(const Feature& feature, const ExprEnv& env, Document& document, const std::vector<Body>& preceding, const char* key) {
    const V& value = required(feature.nativeSpec, key);
    if (value.t == V::Str) {
        if (document.findSketch(value.str)) return profileFromSketch(document, value.str);
        if (const Feature* named = uniqueFeature(document, value.str); named && isCurveFeature(named->type))
            return profileFromCurve(preceding, *named);
        throw std::runtime_error(std::string(key) + " must be a sketch name or a profile object");
    }
    if (value.t == V::Obj) return numericNative(value, env);
    throw std::runtime_error(std::string(key) + " must be a sketch name or a profile object");
}

V spatialProjectionResult(const Feature& feature,Document& document){
            rejectDerivedSpatialHandles(feature);
            if(const V* operation=feature.nativeSpec.find("op");operation&&operation->str=="project"){
                const V* mode=feature.nativeSpec.find("mode");if(!mode||mode->t!=V::Str||mode->str!="3d")throw std::runtime_error("Spatial included geometry requires mode 3d");
                if(const V* source=feature.nativeSpec.find("source_sketch")){
                    const V& entity=required(feature.nativeSpec,"source_entity");
                    if(source->t!=V::Str||entity.t!=V::Str||!feature.inputs.empty()||feature.nativeSpec.find("reference")||feature.nativeSpec.find("source_instance")||feature.nativeSpec.find("source_copy"))throw std::runtime_error("Choose one sketch/entity source or one native feature/reference");
                    return projectSketchGeometry(document,source->str,entity.str);
                }
                if(feature.inputs.size()!=1)throw std::runtime_error("3D included geometry requires one earlier source feature input");
                const V& reference=required(feature.nativeSpec,"reference");if(reference.t!=V::Str||reference.str.empty())throw std::runtime_error("Projection reference must be a producer name");
                u64 instance=0;int copy=0;
                if(auto value=feature.nativeSpec.find("source_instance")){if(value->t!=V::Num||value->num<1||value->num>9007199254740990.||value->num!=std::floor(value->num))throw std::runtime_error("Invalid projection source_instance");instance=(u64)value->num;}
                if(auto value=feature.nativeSpec.find("source_copy")){if(!instance||value->t!=V::Num||value->num<0||value->num>31||value->num!=std::floor(value->num))throw std::runtime_error("Invalid projection source_copy");copy=(int)value->num;}
                V result=projectGeometry(document,feature.inputs.front(),reference.str,"",instance,copy);
                return result;
            }
    throw std::runtime_error("Spatial projection definition missing");
}

V surfaceRequest(const Feature& feature, const ExprEnv& env, Document& document,
                 const std::vector<Body>& preceding) {
    V request = V::makeObj();
    const V* regions = feature.nativeSpec.find("regions");
    if (regions && feature.type != FeatureType::ExtrudeSurface && feature.type != FeatureType::RevolveSurface && feature.type != FeatureType::Sweep)
        throw std::runtime_error("native.regions applies to extrude, extrude_surface, revolve_surface and sweep sketch profiles only");
    auto supportInput = [&](const V& authored, const char* label) -> const Body& {
        if(authored.t!=V::Num||!std::isfinite(authored.num)||authored.num<1||authored.num!=std::floor(authored.num)||authored.num>9007199254740990.)throw std::runtime_error(std::string(label)+" must be an explicit feature id");
        const u64 id=static_cast<u64>(authored.num);
        if(std::find(feature.inputs.begin(),feature.inputs.end(),id)==feature.inputs.end())throw std::runtime_error(std::string(label)+" must also appear in feature.inputs");
        const auto* body=precedingBody(preceding,id);if(!body)throw std::runtime_error(std::string(label)+" input is missing");return *body;
    };
    if (feature.type == FeatureType::Loft) {
        request.set("op", V::makeStr("loft"));
        const V& sections = required(feature.nativeSpec, "sections");
        if (sections.t != V::Arr || sections.arr.size() < 2 || sections.arr.size() > 32)
            throw std::runtime_error("Loft needs 2..32 sketch sections");
        V profiles = V::makeArr();
        for (const V& name : sections.arr) {
            if (name.t != V::Str || name.str.empty()) throw std::runtime_error("Loft section must be a sketch, point or curve name");
            profiles.arr.push_back(resolveProfileName(document, preceding, name.str, "section"));
        }
        request.set("profiles", std::move(profiles));
        const double solid = param(feature, env, "solid");
        if (solid != 0 && solid != 1) throw std::runtime_error("Loft solid must be 0 or 1");
        // The kernel treats an absent ruled flag as a smooth (non-ruled) loft.
        request.set("solid", V::makeBool(solid == 1));
        request.set("ruled", V::makeBool(bitParam(feature, env, "ruled", 0)));
        if (feature.p.count("max_degree")) request.set("max_degree", V::makeNum(param(feature, env, "max_degree")));
        if (feature.p.count("smooth")) request.set("smooth", V::makeBool(bitParam(feature, env, "smooth", 0)));
        if (feature.p.count("compatible")) request.set("compatible", V::makeBool(bitParam(feature, env, "compatible", 0)));
        if (feature.p.count("continuity")) {
            const double continuity = param(feature, env, "continuity");
            if (continuity != 0 && continuity != 1 && continuity != 2)
                throw std::runtime_error("Loft continuity must be 0, 1, or 2");
            request.set("continuity", V::makeNum(continuity));
        }
        if (const V* guides = feature.nativeSpec.find("guides")) {
            if (guides->t != V::Arr || guides->arr.empty() || guides->arr.size() > 8)
                throw std::runtime_error("Loft guides need 1..8 sketch or curve names");
            V curves = V::makeArr();
            for (const V& name : guides->arr) {
                if (name.t != V::Str || name.str.empty()) throw std::runtime_error("Loft guide must be a sketch or curve name");
                curves.arr.push_back(resolveProfileName(document, preceding, name.str, "guide"));
            }
            request.set("guides", std::move(curves));
        }
        if (const V* controls=feature.nativeSpec.find("controls")) request.set("controls",numericNative(*controls,env));
    } else if (feature.type == FeatureType::SurfaceFill) {
        request.set("op", V::makeStr("surface_fill"));
        if (const V* profile = feature.nativeSpec.find("profile"))
            request.set("profile", numericNative(*profile, env));
        else request.set("profile", profileFromSketch(document, feature.get("sketch")));
        if(const V* constraints=feature.nativeSpec.find("constraints")){
            if(constraints->t!=V::Arr||constraints->arr.empty()||constraints->arr.size()>128)throw std::runtime_error("Constrained fill needs 1..128 explicit boundary supports");
            V resolved=V::makeArr();
            for(const auto& constraint:constraints->arr){
                if(constraint.t!=V::Obj)throw std::runtime_error("Boundary support must be an object");
                for(const auto& field:constraint.obj)if(field.first!="edge"&&field.first!="support"&&field.first!="face"&&field.first!="continuity")throw std::runtime_error("Unknown boundary support field: "+field.first);
                const auto& body=supportInput(required(constraint,"support"),"Fill support");V item=V::makeObj();
                item.set("edge",numericNative(required(constraint,"edge"),env));item.set("continuity",numericNative(required(constraint,"continuity"),env));
                item.set("face",required(constraint,"face"));item.set("support_brep",brepOf(body,"fill support"));item.set("topology",required(body.nativeData,"topology"));resolved.arr.push_back(item);
            }
            request.set("constraints",resolved);
        }
        if(const V* validation=feature.nativeSpec.find("validation"))request.set("validation",numericNative(*validation,env));
        if(const V* mode=feature.nativeSpec.find("op")){if(mode->t!=V::Str||mode->str!="blend")throw std::runtime_error("surface_fill native.op supports blend only");request.set("mode",*mode);}
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
    } else if (feature.type == FeatureType::Sweep) {
        request.set("op", V::makeStr("sweep"));
        if(const V* sections=feature.nativeSpec.find("sections")){
            if(feature.nativeSpec.find("section"))throw std::runtime_error("Sweep chooses either section or varying sections");if(sections->t!=V::Arr||sections->arr.size()<2||sections->arr.size()>32)throw std::runtime_error("Varying sweep needs 2..32 section stations");V resolved=V::makeArr();double previous=-1;
            for(const V& station:sections->arr){if(station.t!=V::Obj)throw std::runtime_error("Sweep station must be an object");for(auto& field:station.obj)if(field.first!="section"&&field.first!="fraction"&&field.first!="roll"&&field.first!="x_axis")throw std::runtime_error("Unknown sweep station field: "+field.first);const V& fractionValue=required(station,"fraction");double fraction=fractionValue.t==V::Num?fractionValue.num:fractionValue.t==V::Str?expressionValue(fractionValue.str,env):-1;if(!std::isfinite(fraction)||fraction<0||fraction>1||fraction<=previous)throw std::runtime_error("Sweep fractions must increase in 0..1");previous=fraction;V item=V::makeObj();const V& section=required(station,"section");item.set("section",section.t==V::Str?resolveProfileName(document,preceding,section.str,"section"):numericNative(section,env));item.set("fraction",V::makeNum(fraction));if(const V* roll=station.find("roll"))item.set("roll",numericNative(*roll,env));if(const V* axis=station.find("x_axis"))item.set("x_axis",numericNative(*axis,env));resolved.arr.push_back(item);}if(resolved.arr.front().find("fraction")->num!=0||resolved.arr.back().find("fraction")->num!=1)throw std::runtime_error("Varying sweep sections must explicitly include fractions 0 and 1");request.set("sections",resolved);
        }else if(regions){const V& section=required(feature.nativeSpec,"section");if(section.t!=V::Str||!document.findSketch(section.str))throw std::runtime_error("native.regions selects regions of a sweep section sketch; the section must be a sketch name");request.set("section",regionProfileFromSketch(document,section.str,regions,env));}
        else if(const V* section=feature.nativeSpec.find("section");section&&section->t==V::Str&&document.findSketch(section->str))request.set("section",regionProfileFromSketch(document,section->str,nullptr,env));
        else request.set("section", profileArgument(feature, env, document, preceding, "section"));
        if(regions&&feature.nativeSpec.find("sections"))throw std::runtime_error("native.regions requires one section; varying sweep sections keep one loop per station");
        request.set("path", profileArgument(feature, env, document, preceding, "path"));
        request.set("solid", V::makeBool(bitParam(feature, env, "solid", 0)));
        request.set("correction", V::makeBool(bitParam(feature, env, "correction", 1)));
        request.set("contact", V::makeBool(bitParam(feature, env, "contact", 0)));
        if(const V* orientation=feature.nativeSpec.find("orientation")){
            if(orientation->t!=V::Obj)throw std::runtime_error("Sweep orientation must be an object");
            const auto& mode=required(*orientation,"mode");if(mode.t!=V::Str)throw std::runtime_error("Sweep orientation mode must be text");
            V resolved=V::makeObj();resolved.set("mode",mode);
            for(const auto& field:orientation->obj){
                if(field.first=="mode")continue;
                if(mode.str=="fixed_binormal"&&field.first=="binormal")resolved.set("binormal",numericNative(field.second,env));
                else if(mode.str=="support"&&field.first=="support")resolved.set("support_brep",brepOf(supportInput(field.second,"Sweep orientation support"),"sweep support"));
                else if(mode.str=="auxiliary"&&field.first=="guide")resolved.set("guide",field.second.t==V::Str?resolveProfileName(document,preceding,field.second.str,"guide"):numericNative(field.second,env));
                else if(mode.str=="auxiliary"&&field.first=="equivalence")resolved.set("equivalence",field.second);
                else throw std::runtime_error("Unsupported sweep orientation field: "+field.first);
            }
            request.set("orientation",resolved);
        }
        if(const V* laws=feature.nativeSpec.find("laws"))request.set("laws",numericNative(*laws,env));
    } else if (feature.type == FeatureType::ExtrudeSurface || feature.type == FeatureType::RevolveSurface) {
        const bool revolve = feature.type == FeatureType::RevolveSurface;
        request.set("op", V::makeStr(revolve ? "revolve_surface" : "extrude_surface"));
        if (const V* profile = feature.nativeSpec.find("profile")) {
            if (regions) throw std::runtime_error("native.regions selects regions of p.sketch; an inline native.profile has none");
            request.set("profile", numericNative(*profile, env));
        } else request.set("profile", regionProfileFromSketch(document, feature.get("sketch"), regions, env));
        request.set("solid", V::makeBool(bitParam(feature, env, "solid", 0)));
        if (revolve) {
            const double angle = param(feature, env, "angle");
            if (!(angle > 0.0 && angle <= 360.0)) throw std::runtime_error("revolve angle must be in (0, 360] degrees");
            request.set("angle", V::makeNum(angle));
            std::string axis = "Y";
            if (const V* authored = feature.nativeSpec.find("axis")) {
                if (authored->t != V::Str || (authored->str != "X" && authored->str != "Y" && authored->str != "Z"))
                    throw std::runtime_error("revolve axis must be X, Y or Z");
                axis = authored->str;
            }
            request.set("axis", V::makeStr(axis));
        } else {
            const double distance = param(feature, env, "distance");
            if (std::abs(distance) <= 1.0e-6) throw std::runtime_error("extrude distance must exceed 1e-6 mm");
            request.set("distance", V::makeNum(distance));
        }
    } else if (feature.type == FeatureType::SurfaceEdit) {
        const std::string editOp=required(feature.nativeSpec,"op").asStr();
        const bool paired=editOp=="trim_wire"||editOp=="mutual_trim"||editOp=="trim_tool"||editOp=="ruled";
        if(feature.inputs.size()!=(paired?2u:1u)||(paired&&feature.inputs[0]==feature.inputs[1]))throw std::runtime_error("surface_edit needs one source; trim_wire, mutual_trim, trim_tool and ruled require two distinct inputs");
        const Body* source=precedingBody(preceding,feature.inputs.front());
        if(!source)throw std::runtime_error("surface_edit input missing");
        request.set("input_brep",brepOf(*source,"surface_edit input"));
        const auto& operation=required(feature.nativeSpec,"op");
        if(operation.t!=V::Str)throw std::runtime_error("surface_edit op must be extract_face or trim_plane");
        if(operation.str=="mirror"){
            for(const auto& field:feature.nativeSpec.obj)if(field.first!="op"&&field.first!="plane"&&field.first!="origin"&&field.first!="normal"&&field.first!="continuity"&&field.first!="tolerance")throw std::runtime_error("Unknown surface mirror field: "+field.first);
            request.set("op",V::makeStr("surface_mirror"));request.set("continuity",required(feature.nativeSpec,"continuity"));
            if(const auto* plane=feature.nativeSpec.find("plane")){
                if(plane->t!=V::Str||feature.nativeSpec.find("origin")||feature.nativeSpec.find("normal"))throw std::runtime_error("Surface mirror needs one explicit plane selector");
                const auto frame=resolvePlane(document,plane->str);V origin=V::makeArr(),normal=V::makeArr();for(int i:{12,13,14})origin.arr.push_back(V::makeNum(frame.m[i]));for(int i:{8,9,10})normal.arr.push_back(V::makeNum(frame.m[i]));request.set("origin",origin);request.set("normal",normal);
            }else{request.set("origin",numericNative(required(feature.nativeSpec,"origin"),env));request.set("normal",numericNative(required(feature.nativeSpec,"normal"),env));}
            if(const auto* tolerance=feature.nativeSpec.find("tolerance"))request.set("tolerance",numericNative(*tolerance,env));
        }else if(operation.str=="trim_tool"){
            for(const auto& field:feature.nativeSpec.obj)if(field.first!="op"&&field.first!="tool_kind"&&field.first!="keep_point"&&field.first!="tolerance")throw std::runtime_error("Unknown surface trim tool field: "+field.first);
            const auto* tool=precedingBody(preceding,feature.inputs[1]);if(!tool)throw std::runtime_error("Surface trim tool missing");request.set("op",V::makeStr("surface_trim_tool"));request.set("tool_brep",brepOf(*tool,"surface trim tool"));request.set("tool_kind",required(feature.nativeSpec,"tool_kind"));request.set("keep_point",numericNative(required(feature.nativeSpec,"keep_point"),env));if(const auto* tolerance=feature.nativeSpec.find("tolerance"))request.set("tolerance",numericNative(*tolerance,env));
        }else if(operation.str=="ruled"){
            for(const auto& field:feature.nativeSpec.obj)if(field.first!="op"&&field.first!="reverse_second"&&field.first!="tolerance")throw std::runtime_error("Unknown ruled surface field: "+field.first);
            const auto* second=precedingBody(preceding,feature.inputs[1]);if(!second)throw std::runtime_error("Ruled surface second curve missing");request.obj.erase(std::remove_if(request.obj.begin(),request.obj.end(),[](const auto& item){return item.first=="input_brep";}),request.obj.end());request.set("op",V::makeStr("surface_ruled"));request.set("first_brep",brepOf(*source,"first ruled curve"));request.set("second_brep",brepOf(*second,"second ruled curve"));request.set("reverse_second",required(feature.nativeSpec,"reverse_second"));if(const auto* tolerance=feature.nativeSpec.find("tolerance"))request.set("tolerance",numericNative(*tolerance,env));
        }else if(operation.str=="repair"){
            for(const auto& field:feature.nativeSpec.obj)if(field.first!="op"&&field.first!="mode"&&field.first!="normal")throw std::runtime_error("repair_invalid_request: Unknown repair field: "+field.first);
            request.set("op",V::makeStr("surface_repair"));
            request.set("mode",required(feature.nativeSpec,"mode"));
            request.set("topology",required(source->nativeData,"topology"));
            if(const auto* normal=feature.nativeSpec.find("normal"))request.set("normal",numericNative(*normal,env));
        }else if(operation.str=="extract_face"){
            request.set("op",V::makeStr("surface_extract_face"));
            request.set("reference",required(feature.nativeSpec,"face"));
            request.set("topology",required(source->nativeData,"topology"));
        }else if(operation.str=="trim_plane"){
            request.set("op",V::makeStr("surface_trim_plane"));
            request.set("keep",required(feature.nativeSpec,"keep"));
            if(const auto* name=feature.nativeSpec.find("plane")){
                if(name->t!=V::Str||feature.nativeSpec.find("normal")||feature.nativeSpec.find("origin"))throw std::runtime_error("Surface trim needs one plane selector");
                const auto plane=resolvePlane(document,name->str);V origin=V::makeArr(),normal=V::makeArr();
                for(int i:{12,13,14})origin.arr.push_back(V::makeNum(plane.m[i]));
                for(int i:{8,9,10})normal.arr.push_back(V::makeNum(plane.m[i]));
                request.set("origin",origin);request.set("normal",normal);
            }else{
                const auto spec=numericNative(feature.nativeSpec,env);
                request.set("origin",required(spec,"origin"));request.set("normal",required(spec,"normal"));
            }
        }else if(operation.str=="trim_wire"){
            const Body* curve=precedingBody(preceding,feature.inputs[1]);if(!curve)throw std::runtime_error("Surface trim curve input missing");request.set("op",V::makeStr("surface_trim_wire"));request.set("wire_brep",brepOf(*curve,"trim curve"));request.set("keep_point",numericNative(required(feature.nativeSpec,"keep_point"),env));
        }else if(operation.str=="untrim"||operation.str=="extend"){
            for(const auto& field:feature.nativeSpec.obj)if(field.first!="op"&&field.first!="bounds"&&field.first!="extension")throw std::runtime_error("Unknown surface domain edit field: "+field.first);
            request.set("op",V::makeStr(operation.str=="extend"?"surface_extend":"surface_untrim"));
            if(const auto* bounds=feature.nativeSpec.find("bounds"))request.set("bounds",numericNative(*bounds,env));
            if(const auto* extension=feature.nativeSpec.find("extension"))request.set("extension",numericNative(*extension,env));
        }else if(operation.str=="mutual_trim"){
            for(const auto& field:feature.nativeSpec.obj)if(field.first!="op"&&field.first!="keep_points")throw std::runtime_error("Unknown mutual trim field: "+field.first);
            const auto* other=precedingBody(preceding,feature.inputs[1]);if(!other)throw std::runtime_error("Mutual trim second surface missing");
            request.set("op",V::makeStr("surface_mutual_trim"));request.set("other_brep",brepOf(*other,"mutual trim second surface"));request.set("keep_points",numericNative(required(feature.nativeSpec,"keep_points"),env));
        }else throw std::runtime_error("surface_edit op must be extract_face, trim_plane, trim_wire, trim_tool, mirror, ruled, repair, untrim, extend or mutual_trim");
    } else if (feature.type == FeatureType::OffsetSurface) {
        request.set("op", V::makeStr("offset_surface"));
        if (feature.inputs.size() != 1) throw std::runtime_error("offset_surface requires one native surface input");
        const Body* source = precedingBody(preceding, feature.inputs.front());
        if (!source) throw std::runtime_error("offset_surface input missing");
        request.set("input_brep", brepOf(*source, "offset_surface input"));
        const double distance = param(feature, env, "distance");
        if (std::abs(distance) <= 1.0e-6) throw std::runtime_error("offset distance must exceed 1e-6 mm");
        request.set("distance", V::makeNum(distance));
    } else if (feature.type == FeatureType::Sew) {
        request.set("op", V::makeStr("sew"));
        if (feature.inputs.size() < 2 || feature.inputs.size() > 16)
            throw std::runtime_error("sew needs 2..16 native surface inputs");
        V inputs = V::makeArr();
        for (u64 input : feature.inputs) {
            const Body* source = precedingBody(preceding, input);
            if (!source) throw std::runtime_error("sew input missing");
            inputs.arr.push_back(brepOf(*source, "sew input"));
        }
        request.set("inputs", std::move(inputs));
        request.set("solid", V::makeBool(bitParam(feature, env, "solid", 0)));
        for(const auto& field:feature.nativeSpec.obj)if(field.first!="require_connected")throw std::runtime_error("Unknown sew field: "+field.first);
        if(const V* connected=feature.nativeSpec.find("require_connected")){if(connected->t!=V::Bool)throw std::runtime_error("Sew require_connected must be boolean");request.set("require_connected",*connected);}
    } else if (isCurveFeature(feature.type)) {
        if (feature.type == FeatureType::Curve) {
            rejectDerivedSpatialHandles(feature);
            if(const auto* op=feature.nativeSpec.find("op");op&&op->asStr()=="helix"){
                if(!feature.inputs.empty())throw std::runtime_error("Helix is an authored source and takes no feature inputs");
                request=numericNative(feature.nativeSpec,env);
                // axis is numeric XYZ here, never a symbolic solid-axis selector.
                if(const V* axis=feature.nativeSpec.find("axis"))request.set("axis",numericNative(*axis,env,"vector"));
                return request;
            }
            if(const auto* op=feature.nativeSpec.find("op");op&&op->asStr()=="equation")return sampleEquationCurve(feature.nativeSpec,env);
            if(const auto* op=feature.nativeSpec.find("op");op&&op->asStr()=="project_surface"){
                if(feature.inputs.size()!=2)throw std::runtime_error("Surface projection requires source curve and target surface inputs");const Body* source=precedingBody(preceding,feature.inputs[0]);const Body* target=precedingBody(preceding,feature.inputs[1]);if(!source||!target)throw std::runtime_error("Surface projection inputs missing");request.set("op",V::makeStr("curve_project_surface"));request.set("input_brep",brepOf(*source,"projection curve"));request.set("target_brep",brepOf(*target,"projection surface"));request.set("direction",numericNative(required(feature.nativeSpec,"direction"),env,"vector"));return request;
            }
            if(const auto* op=feature.nativeSpec.find("op");op&&op->asStr()=="surface_spline"){
                if(feature.inputs.size()!=1)throw std::runtime_error("Surface spline requires one exact surface input");const Body* source=precedingBody(preceding,feature.inputs[0]);if(!source)throw std::runtime_error("Surface spline input missing");request.set("op",V::makeStr("surface_spline"));request.set("input_brep",brepOf(*source,"surface spline"));request.set("controls",numericNative(required(feature.nativeSpec,"controls"),env));if(const V* weights=feature.nativeSpec.find("weights"))request.set("weights",numericNative(*weights,env));if(const V* face=feature.nativeSpec.find("face")){request.set("reference",*face);request.set("topology",required(source->nativeData,"topology"));}return request;
            }
            request.set("op", V::makeStr("curve"));
            V authored=feature.nativeSpec;if(authored.find("constraints")||authored.find("start_ref")||authored.find("segments")&&std::any_of(authored.find("segments")->arr.begin(),authored.find("segments")->arr.end(),[](const V& segment){return segment.find("to_ref")||segment.find("through_ref");})){auto solution=solveSpatialCurve(authored,document);authored=std::move(solution.native);request.set("spatial_constraint_diagnostics",solution.diagnostics);}
            const V spec = numericNative(authored, env);
            if (const V* start = spec.find("start")) request.set("start", *start);
            request.set("segments", required(spec, "segments"));
        } else if (feature.type == FeatureType::IntersectionCurve) {
            if (feature.inputs.size() != 1) throw std::runtime_error("Intersection curve needs one solid input");
            const Body* source = precedingBody(preceding, feature.inputs.front());
            if (!source) throw std::runtime_error("Intersection input missing");
            const V* plane = feature.nativeSpec.find("plane");
            if (!plane || plane->t != V::Str || plane->str.empty()) throw std::runtime_error("Intersection plane must be a datum name");
            request.set("op", V::makeStr("intersection_curve"));
            request.set("input_brep", brepOf(*source, "Intersection"));
            V frame = V::makeArr();
            for (double value : resolvePlane(document, plane->str).m) frame.arr.push_back(V::makeNum(value));
            request.set("frame", std::move(frame));
        } else {
            auto endpoint = [&](const char* key, const char* originKey, const char* tangentKey) {
                const V& spec = required(feature.nativeSpec, key);
                if (spec.t != V::Obj) throw std::runtime_error(std::string("Bridge ") + key + " must be an object");
                const V& featureValue = required(spec, "feature");
                const V& reference = required(spec, "reference");
                if (reference.t != V::Str || reference.str.empty()) throw std::runtime_error("Bridge reference must be a name");
                double fraction = 0;
                if (const V* authored = spec.find("fraction")) {
                    if (authored->t == V::Num) fraction = authored->num;
                    else if (authored->t == V::Str) fraction = expressionValue(authored->str, env);
                    else throw std::runtime_error("Bridge fraction must be a number");
                }
                u64 id = 0;
                if (featureValue.t == V::Num) {
                    if (!std::isfinite(featureValue.num) || featureValue.num < 1 || featureValue.num != std::floor(featureValue.num))
                        throw std::runtime_error("Feature reference must be an id or a unique name");
                    id = static_cast<u64>(featureValue.num);
                } else if (featureValue.t == V::Str) {
                    const Feature* named = uniqueFeature(document, featureValue.str);
                    if (!named) {
                        std::string names;size_t listed = 0;
                        for (const Feature& feature : document.features) {
                            if (feature.name.empty()) continue;
                            if (listed == 24 || names.size() > 480) { names += ", ..."; break; }
                            names += (listed++ ? ", " : "") + feature.name;
                        }
                        throw std::runtime_error("Feature name not found: " + featureValue.str + "; existing feature names: " + (names.empty() ? std::string("none") : names));
                    }
                    id = named->id;
                } else throw std::runtime_error("Feature reference must be an id or a unique name");
                const GeometryReference ref = resolveGeometryReference(document, id, reference.str, fraction);
                if (ref.kind != "edge" && ref.kind != "axis") throw std::runtime_error("Bridge needs an edge or axis");
                const Vec3 origin = ref.frame.transformPoint({});
                const Vec3 tangent = ref.frame.transformDir({0, 0, 1});
                auto xyz = [](Vec3 p) { V value = V::makeArr(); for (double n : {p.x, p.y, p.z}) value.arr.push_back(V::makeNum(n)); return value; };
                request.set(originKey, xyz(origin));
                request.set(tangentKey, xyz(tangent));
            };
            request.set("op", V::makeStr("bridge"));
            endpoint("a", "p0", "t0");
            endpoint("b", "p1", "t1");
            request.set("continuity", V::makeNum(feature.p.count("continuity") ? param(feature, env, "continuity") : 1));
        }
    }
    return request;
}

V sheetRequest(const Feature& feature, const ExprEnv& env,const Document& document) {
    if (feature.nativeSpec.find("base")) {
        V request = feature.nativeSpec;
        for (const char* key : {"width", "thickness", "k_factor"})
            request.set(key, V::makeNum(param(feature, env, key)));
        V parameters = V::makeObj();
        for (const auto& [name, value] : env.vars)
            parameters.set(name, V::makeNum(value));
        request.set("parameters", std::move(parameters));
        if(const auto* bend=feature.nativeSpec.find("sketch_bend")){
            if(bend->t!=V::Obj)throw std::runtime_error("Sheet sketch_bend must be an object");const auto& sketchName=required(*bend,"sketch"),entityName=required(*bend,"entity");if(sketchName.t!=V::Str||entityName.t!=V::Str)throw std::runtime_error("Sketch bend requires named sketch and line entity");const NamedSketch* sketch=nullptr;for(const auto& candidate:document.sketches)if(candidate.name==sketchName.str)sketch=&candidate;if(!sketch||!sketch->error.empty()||sketch->sk.plane!="XZ")throw std::runtime_error("Sketch bend requires a solved XZ sketch in the sheet's original local blank coordinates");const SketchEnt* line=nullptr;for(const auto& candidate:sketch->sk.ents)if(candidate.name==entityName.str){if(line)throw std::runtime_error("Ambiguous sketch bend line");line=&candidate;}
            const double width=param(feature,env,"width");if(!line||line->kind!=SketchEnt::Line||line->dof.size()!=4||std::abs(line->dof[0]-line->dof[2])>1e-6||std::abs(std::min(line->dof[1],line->dof[3]))>1e-6||std::abs(std::max(line->dof[1],line->dof[3])-width)>1e-6)throw std::runtime_error("Sketch bend supports one full-width straight line parallel to the blank width; partial, diagonal, curved and multiple lines reject");V resolved=*bend;resolved.set("station",V::makeNum(line->dof[0]));request.set("sketch_bend",resolved);
        }
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
    if (!feature.get("sketch").empty()) return regionProfileFromSketch(document, feature.get("sketch"), nullptr, env);
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

V tubeAuthority(const Document& document,u64 selected,const ExprEnv& env,bool allowFits){
    const Feature* source=document.find(selected);Mat4 placed=Mat4::identity();unsigned depth=0;
    while(source&&source->type!=FeatureType::Tube){
        if(++depth>64||source->inputs.empty())throw std::runtime_error("Tube end fit requires authored Tube lineage");
        const auto op=featureOperation(*source);
        if(allowFits&&op=="tube_end_fit"){source=document.find(source->inputs[0]);continue;}
        if(source->inputs.size()!=1||(source->type!=FeatureType::Transform&&!(source->type==FeatureType::NativeSolid&&(op=="transform"||op=="rigid_transform"))))
            throw std::runtime_error("Tube end fit supports authored Tube and rigid placements only");
        Mat4 local;
        if(const auto* authored=source->nativeSpec.find("frame")){
            const auto numeric=numericNative(*authored,env);if(numeric.t!=V::Arr||numeric.arr.size()!=16)throw std::runtime_error("Tube placement frame needs 16 numbers");
            for(size_t i=0;i<16;++i){if(numeric.arr[i].t!=V::Num)throw std::runtime_error("Tube placement frame must be numeric");local.m[i]=numeric.arr[i].num;}
        }else local=Mat4::translation({source->num(env,"tx"),source->num(env,"ty"),source->num(env,"tz")})*Mat4::rotZ(deg2rad(source->num(env,"rz")))*Mat4::rotY(deg2rad(source->num(env,"ry")))*Mat4::rotX(deg2rad(source->num(env,"rx")));
        placed=placed*local;source=document.find(source->inputs[0]);
    }
    if(!source)throw std::runtime_error("Tube end fit source missing");
    V program=V::makeObj(),segments=V::makeArr(),authority=V::makeObj();const bool rect=source->get("section","round")=="rect";
    auto dimension=[&](const char* key,double fallback){return source->p.count(key)?param(*source,env,key):fallback;};
    program.set("op",V::makeStr(rect?"rect_tube":"round_tube"));program.set("wall",V::makeNum(dimension("wall",2)));
    if(rect){program.set("width",V::makeNum(dimension("width",40)));program.set("height",V::makeNum(dimension("height",30)));}else program.set("od",V::makeNum(dimension("od",30)));
    for(const auto& bend:source->bends){const double feed=expressionValue(bend.feed,env);if(feed>1e-9){V line=V::makeObj();line.set("kind",V::makeStr("line"));line.set("length",V::makeNum(feed));segments.arr.push_back(line);}V arc=V::makeObj();arc.set("kind",V::makeStr("arc"));for(const auto& item:std::vector<std::pair<const char*,std::string>>{{"radius",bend.radius},{"angle",bend.angle},{"roll",bend.roll}})arc.set(item.first,V::makeNum(expressionValue(item.second,env)));segments.arr.push_back(arc);}
    const double tail=dimension("tail",100);if(tail>1e-9){V line=V::makeObj();line.set("kind",V::makeStr("line"));line.set("length",V::makeNum(tail));segments.arr.push_back(line);}
    program.set("segments",segments);authority.set("program",program);authority.set("frame",frameValue(placed));authority.set("origin_id",V::makeNum((double)source->id));return authority;
}

// Derived discovery data only. Eligibility describes retained authoring and
// semantic cap bindings; a particular source/host join still needs exact checks.
void appendTubeEndFitOptions(V& result,const Document& document,u64 selected,const ExprEnv& env){
    auto* storedReport=result.find("report");if(!storedReport)return;
    V options=V::makeObj(),faces=V::makeArr();
    options.set("feature_input",V::makeNum((double)selected));
    options.set("coordinates",V::makeStr("feature definition coordinates; assembly placement is separate"));
    options.set("joint_feasibility_checked",V::makeBool(false));
    const auto* topology=result.find("topology");const auto* references=topology?topology->find("references"):nullptr;
    if(references&&references->t==V::Arr)for(const auto& ref:references->arr){
        const auto* kind=ref.find("kind"),*status=ref.find("status"),*descriptor=ref.find("descriptor"),*indices=ref.find("indices");
        if(!kind||kind->str!="face"||!status||(status->str!="bound"&&status->str!="merged")||!descriptor||!descriptor->find("normal")||!descriptor->find("origin")||!indices||indices->arr.size()!=1)continue;
        V face=V::makeObj();face.set("reference",required(ref,"name"));face.set("descriptor",*descriptor);faces.arr.push_back(std::move(face));
    }
    options.set("planar_host_references",faces);
    try{
        const auto authority=tubeAuthority(document,selected,env,true);const auto& program=required(authority,"program"),segments=required(program,"segments");
        options.set("source_lineage_supported",V::makeBool(true));options.set("source_tube",required(authority,"origin_id"));
        options.set("section",V::makeStr(required(program,"op").str=="rect_tube"?"rect":"round"));
        options.set("source_frame_column_major",required(authority,"frame"));
        V endpoints=V::makeArr();for(const char* end:{"start","end"}){
            const bool start=std::string(end)=="start";const std::string name=std::string("cap.")+end;const V* cap=nullptr;
            if(references&&references->t==V::Arr)for(const auto& ref:references->arr)if(required(ref,"name").str==name)cap=&ref;
            const V* terminal=segments.arr.empty()?nullptr:(start?&segments.arr.front():&segments.arr.back());
            const bool straight=terminal&&required(*terminal,"kind").str=="line";
            const bool bound=cap&&(required(*cap,"status").str=="bound"||required(*cap,"status").str=="merged")&&required(*cap,"indices").arr.size()==1;
            V row=V::makeObj();row.set("end",V::makeStr(end));row.set("cap_reference",V::makeStr(name));row.set("cap_status",cap?required(*cap,"status"):V::makeStr("missing"));
            row.set("straight_terminal",V::makeBool(straight));row.set("candidate",V::makeBool(straight&&bound));
            if(straight)row.set("authored_terminal_length_mm",required(*terminal,"length"));
            if(bound)if(const auto* descriptor=cap->find("descriptor"))row.set("cap_descriptor",*descriptor);
            if(!straight)row.set("reason",V::makeStr("Selected end has no authored straight terminal segment"));
            else if(!bound)row.set("reason",V::makeStr("Original selected cap is missing, split or deleted; inspect earlier endpoint operations"));
            endpoints.arr.push_back(std::move(row));
        }
        options.set("endpoints",endpoints);
        bool envelopeHost=false;std::string hostReason;
        try{const auto host=tubeAuthority(document,selected,env,false);const auto& hostSegments=required(required(host,"program"),"segments");envelopeHost=hostSegments.arr.size()==1&&required(hostSegments.arr.front(),"kind").str=="line";if(!envelopeHost)hostReason="Envelope host must be one straight authored Tube; planar host faces are listed separately";}
        catch(const std::exception& error){hostReason=error.what();}
        options.set("envelope_host_available",V::makeBool(envelopeHost));if(!envelopeHost)options.set("envelope_host_reason",V::makeStr(hostReason));
        options.set("extension_scope",V::makeStr("plane and plane_trim require explicit max_extension; extension revalidates original untrimmed stock"));
    }catch(const std::exception& error){options.set("source_lineage_supported",V::makeBool(false));options.set("source_reason",V::makeStr(error.what()));options.set("envelope_host_available",V::makeBool(false));}
    options.set("host_selection",V::makeStr("Choose a source endpoint and a distinct physical host explicitly; envelope uses filled outer straight Tube stock, plane needs full finite-face seating, plane_trim allows measured partial seating"));
    V report=*storedReport;report.set("tube_end_fit_options",options);result.set("report",report);
}

V solidFeatureRequest(const Feature& feature, const ExprEnv& env,
                     Document& document, std::vector<Body>& preceding) {
    if(featureOperation(feature)=="imported_cad"){
        std::string error;if(!validateCadPackageAuthoring(feature,error))throw std::runtime_error(error);
        V request=V::makeObj();request.set("op",V::makeStr("imported_cad"));request.set("package",required(feature.nativeSpec,"package"));return request;
    }
    if(featureOperation(feature)=="imported_brep"){
        std::string error;if(!validateImportedComponentDefinition(feature,error))throw std::runtime_error(error);
        V request=V::makeObj();request.set("op",V::makeStr("imported_brep"));request.set("package",required(feature.nativeSpec,"package"));return request;
    }
    V model;
    if(featureOperation(feature)=="holes_orchestrator"){
        if(feature.inputs.size()!=1)throw std::runtime_error("Holes Orchestrator requires one exact source body");
        model=feature.nativeSpec;V recipe=required(model,"recipe");
        if(auto* points=recipe.find("points")){V numeric=*points;if(numeric.t!=V::Arr)throw std::runtime_error("Hole positions must be an array");for(auto& point:numeric.arr)for(const char* key:{"position","direction"})if(const auto* coordinates=point.find(key))point.set(key,numericNative(*coordinates,env,"vector"));recipe.set("points",numeric);}
        model.set("recipe",recipe);
    }else model=numericNative(feature.nativeSpec,env);
    if (model.t != V::Obj) model = V::makeObj();
    for (const auto& [key, expression] : feature.p) {
        if (key == "op" || key == "sketch" || key == "plane" || key == "density") continue;
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
    if (op != "extrude" && model.find("regions")) throw std::runtime_error("native.regions applies to extrude, extrude_surface, revolve_surface and sweep sketch profiles only");
    if(op=="split_line"){
        const std::set<std::string> allowed{"op","sketch","source_face","source_topology","direction"};for(const auto& item:feature.nativeSpec.obj)if(!allowed.count(item.first))throw std::runtime_error("Unknown native split-line field: "+item.first);
        for(const auto& item:feature.p)if(item.first!="density")throw std::runtime_error("Unknown native split-line parameter: "+item.first);
        if(feature.inputs.size()!=1)throw std::runtime_error("Split line requires one exact solid source input");
        for(const char* key:{"sketch","source_face","source_topology"}){const auto& value=required(feature.nativeSpec,key);if(value.t!=V::Str||value.str.empty())throw std::runtime_error(std::string("Split line requires typed ")+key);}
        const auto& vector=required(model,"direction");if(vector.t!=V::Arr||vector.arr.size()!=3)throw std::runtime_error("Split-line direction must contain three expressions");
        model.set("profile",profileFromSketch(document,required(feature.nativeSpec,"sketch").str));
    }
    if(op=="vertex_pull"){
        for(const auto& item:feature.nativeSpec.obj)if(item.first!="op"&&item.first!="source_vertex"&&item.first!="source_topology")throw std::runtime_error("Unknown native vertex-pull field: "+item.first);
        for(const auto& item:feature.p)if(item.first!="dx"&&item.first!="dy"&&item.first!="dz"&&item.first!="density"&&item.first!="op")throw std::runtime_error("Unknown native vertex-pull parameter: "+item.first);
        for(const char* key:{"dx","dy","dz"})if(!feature.p.count(key))throw std::runtime_error(std::string("Vertex pull requires explicit ")+key);
        for(const char* key:{"source_vertex","source_topology"}){const auto& value=required(feature.nativeSpec,key);if(value.t!=V::Str||value.str.empty())throw std::runtime_error(std::string("Vertex pull requires typed ")+key);}
    }
    if(op=="tube_end_fit"){
        if(feature.inputs.size()!=2||feature.inputs[0]==feature.inputs[1])throw std::runtime_error("Tube end fit needs distinct source and host inputs");
        const std::set<std::string> allowed{"op","end","mode","reference"};for(const auto& item:feature.nativeSpec.obj)if(!allowed.count(item.first))throw std::runtime_error("Unsupported tube end fit field: "+item.first);
        for(const auto& item:feature.p)if(item.first!="max_extension"&&item.first!="density")throw std::runtime_error("Unsupported tube end fit parameter: "+item.first);
        if(!feature.p.count("max_extension"))throw std::runtime_error("Tube end fit needs explicit max_extension");
        const auto* fitMode=model.find("mode");if(!fitMode||fitMode->t!=V::Str||(fitMode->str!="envelope"&&fitMode->str!="plane"&&fitMode->str!="plane_trim"))throw std::runtime_error("Tube end fit mode must be envelope, plane or plane_trim");
        model.set("tube_authority",tubeAuthority(document,feature.inputs[0],env,true));
        if(const auto* mode=model.find("mode");mode&&mode->t==V::Str&&mode->str=="envelope")model.set("host_authority",tubeAuthority(document,feature.inputs[1],env,false));
    }
    if ((op == "hole_pattern" && feature.inputs.size() != 2) || (op == "curve_pattern" && (feature.inputs.size() < 2 || feature.inputs.size() > 3)))
        throw std::runtime_error("Curve pattern needs seed and path feature inputs, plus an optional support input");
    if (op == "vertex_pull" && feature.inputs.size() != 1)
        throw std::runtime_error("Vertex pull requires one exact polyhedral solid input");
    if (op == "shell" && feature.inputs.size() != 1)
        throw std::runtime_error("Shell needs one solid input");
    if (op == "split_plane" && feature.inputs.size() != 1)
        throw std::runtime_error("Split plane needs one exact solid input");
    if((op=="split_surface"||op=="split_tool")&&(feature.inputs.size()!=2||feature.inputs[0]==feature.inputs[1]))
        throw std::runtime_error("Surface/tool split requires exactly two distinct source and tool inputs");
    if(op=="rib"||op=="gusset"||op=="mount_pad"||op=="boss"||op=="screw_support"||op=="nut_seat"||op=="lid_lip"||op=="lid_groove"){
        if(feature.inputs.size()!=1)throw std::runtime_error("Face-owned solid recipe requires exactly one source input");
        const auto& face=required(feature.nativeSpec,"source_face");if(face.t!=V::Str||face.str.empty())throw std::runtime_error("Face-owned solid recipe requires a semantic source_face");
    }
    if ((op == "mirror" || op == "split_plane") && !model.find("face") && !model.find("normal")) {
        if (const V* plane = model.find("plane")) {
            if (plane->t != V::Str || plane->str.empty()) throw std::runtime_error("Mirror plane must be a datum name");
            if (plane->str != "XY" && plane->str != "XZ" && plane->str != "YZ") {
                const Mat4 placed = resolvePlane(document, plane->str);
                const double length = std::sqrt(placed.m[8] * placed.m[8] + placed.m[9] * placed.m[9] + placed.m[10] * placed.m[10]);
                if (length <= 1e-12) throw std::runtime_error("Mirror plane normal is zero");
                V origin = V::makeArr(), normal = V::makeArr();
                for (int index : {12, 13, 14}) origin.arr.push_back(V::makeNum(placed.m[index]));
                for (int index : {8, 9, 10}) normal.arr.push_back(V::makeNum(placed.m[index]));
                model.set("origin", std::move(origin));
                model.set("normal", std::move(normal));
                if (op == "split_plane")
                    model.obj.erase(std::remove_if(model.obj.begin(), model.obj.end(),
                        [](const auto& item) { return item.first == "plane"; }), model.obj.end());
            }
        }
    }
    if (op == "transform" && !model.find("frame")) {
        Mat4 matrix = Mat4::translation({feature.num(env,"tx"),feature.num(env,"ty"),feature.num(env,"tz")})
                    * Mat4::rotZ(deg2rad(feature.num(env,"rz")))
                    * Mat4::rotY(deg2rad(feature.num(env,"ry")))
                    * Mat4::rotX(deg2rad(feature.num(env,"rx")));
        model.set("frame", frameValue(matrix));
    }
    if (op == "extrude") {
        const bool sourceFace = model.find("source_face") != nullptr;
        const std::string mode = model.find("mode") ? required(model,"mode").asStr() : "distance";
        if (mode != "distance" && mode != "up_to_face") throw std::runtime_error("Extrusion mode must be distance or up_to_face");
        const size_t expectedInputs = (sourceFace ? 1u : 0u) + (mode == "up_to_face" ? 1u : 0u);
        if (feature.inputs.size() != expectedInputs) throw std::runtime_error("Extrusion requires its exact source face and/or limit face inputs only");
        if (sourceFace && (model.find("profile") || !feature.get("sketch").empty())) throw std::runtime_error("Extrusion needs one saved sketch or one exact source face");
        const V* regions = feature.nativeSpec.find("regions");
        if (regions && (sourceFace || model.find("profile"))) throw std::runtime_error("native.regions selects regions of p.sketch; an exact source_face or inline profile has none");
        if (!sourceFace && !model.find("profile")) model.set("profile", regionProfileFromSketch(document, feature.get("sketch"), regions, env));
        model.obj.erase(std::remove_if(model.obj.begin(), model.obj.end(), [](const auto& item) { return item.first == "regions"; }), model.obj.end());
        if (mode == "distance" && model.find("limit_face")) throw std::runtime_error("Distance extrusion cannot retain an inactive limit face");
    }
    if(op=="rubber_body"&&!feature.inputs.empty())throw std::runtime_error("Rubber body is an authored profile and accepts no inputs");
    if((op=="wood"&&!feature.inputs.empty())||(op=="vent"&&feature.inputs.size()>1))throw std::runtime_error("Wood needs no input; vent accepts at most one exact local XY stock input");
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
        if (feature.inputs.size() != 2 && !(op == "curve_pattern" && feature.inputs.size() == 3)) throw std::runtime_error("Native solid supports at most two inputs");
        ensureNativeBody(document, feature.inputs[1], preceding);
        const Body* tool = nullptr;
        for (const auto& body : preceding) if (body.id == feature.inputs[1]) tool = &body;
        if (!tool) throw std::runtime_error("Native solid tool missing");
        model.set("tool_brep", required(tool->nativeData, "brep_base64"));
        if (const V* topology = tool->nativeData.find("topology")) model.set("tool_topology", *topology);
        model.set("tool_namespace", V::makeStr("tool." + std::to_string(feature.inputs[1])));
        if (op == "curve_pattern") curvePatternHostInputs044B(feature, document, preceding, *tool, model);
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

std::string normalizedSolidOp(const Feature& feature) {
    std::string op = feature.get("op");
    if (op.empty()) {
        if (const V* authored = feature.nativeSpec.find("op"); authored && authored->t == V::Str) op = authored->str;
    }
    if (op == "boolean_union") op = "union";
    if (op == "boolean_cut") op = "cut";
    if (op == "boolean_common" || op == "intersect") op = "common";
    return op;
}

bool spansExactBoolean(const Feature& feature) {
    return feature.type == FeatureType::NativeSolid &&
           (feature.nativeSpec.find("instances") || (feature.inputs.size() > 2 && normalizedSolidOp(feature) != "curve_pattern"));
}

struct ExactPiece { std::string brep; V topology; Mat4 frame; bool framed=false; std::string nameSpace; };

u64 instanceRef(const Document& document, const V& value) {
    if (value.t == V::Num) {
        if (!std::isfinite(value.num) || value.num < 1 || value.num > 9007199254740990. || value.num != std::floor(value.num))
            throw std::runtime_error("Assembly instance id must be a positive integer");
        return (u64)value.num;
    }
    if (value.t == V::Str) {
        u64 found = 0; int count = 0;
        for (const auto& node : document.assembly) if (node.name == value.str) { found = node.id; ++count; }
        if (count != 1) throw std::runtime_error(count ? "Ambiguous assembly instance name: " + value.str : "Assembly instance name not found: " + value.str);
        return found;
    }
    throw std::runtime_error("Assembly instance reference must be an id or a unique name");
}

ExactPiece pieceFromBody(const Body& body, u64 featureId) {
    const V* brep = body.nativeData.find("brep_base64");
    if (!brep || brep->t != V::Str)
        throw std::runtime_error("Exact boolean needs a B-rep on feature #" + std::to_string(featureId) + ". Display meshes are not exact solids.");
    ExactPiece piece;
    piece.brep = brep->str;
    if (const V* topology = body.nativeData.find("topology")) piece.topology = *topology;
    piece.nameSpace = "feature." + std::to_string(featureId);
    return piece;
}

const Body* preparedBody(Document& document, u64 featureId, std::vector<Body>& preceding) {
    try { ensureNativeBody(document, featureId, preceding); }
    catch (const std::exception& error) {
        throw std::runtime_error("Exact boolean needs a B-rep on feature #" + std::to_string(featureId) + ": " + error.what());
    }
    for (const auto& body : preceding) if (body.id == featureId) return &body;
    throw std::runtime_error("Exact boolean input feature #" + std::to_string(featureId) + " is not evaluated yet");
}

V combineExactBoolean(const Feature& feature, Document& document, std::vector<Body>& preceding) {
    const std::string op = normalizedSolidOp(feature);
    if (op != "union" && op != "cut" && op != "common")
        throw std::runtime_error("Placed-instance booleans and unions of more than two solids require op union, cut or common");
    std::vector<ExactPiece> pieces;
    for (u64 input : feature.inputs) pieces.push_back(pieceFromBody(*preparedBody(document, input, preceding), input));
    if (const V* instances = feature.nativeSpec.find("instances")) {
        if (instances->t != V::Arr || instances->arr.empty() || instances->arr.size() > 16)
            throw std::runtime_error("native.instances needs 1..16 assembly nodes");
        std::vector<AssemblyPlacement> placements;
        std::string error;
        if (!resolveAssembly(document, placements, error)) throw std::runtime_error(error);
        std::set<u64> seen;
        for (const V& item : instances->arr) {
            const u64 node = instanceRef(document, item);
            if (!seen.insert(node).second) throw std::runtime_error("Repeated assembly instance in native.instances");
            int found = 0;
            for (const auto& placement : placements) if (placement.node == node) {
                ExactPiece piece = pieceFromBody(*preparedBody(document, placement.feature, preceding), placement.feature);
                piece.framed = true;
                piece.frame = placement.world;
                piece.nameSpace = "instance." + std::to_string(placement.node) + "." + std::to_string(placement.copy);
                pieces.push_back(std::move(piece));
                ++found;
            }
            if (!found) throw std::runtime_error("Assembly instance #" + std::to_string(node) + " is not a placed part");
        }
    }
    if (pieces.size() < 2 || pieces.size() > 16) throw std::runtime_error("Exact boolean needs 2..16 solids");
    V sequence=V::makeObj(),inputs=V::makeArr(),envelope=V::makeObj();
    sequence.set("schema",V::makeNum(1));sequence.set("op",V::makeStr(op));
    for(const auto& piece:pieces){V input=V::makeObj();input.set("brep_base64",V::makeStr(piece.brep));if(piece.topology.t==V::Obj)input.set("topology",piece.topology);input.set("namespace",V::makeStr(piece.nameSpace));if(piece.framed)input.set("frame",frameValue(piece.frame));inputs.arr.push_back(std::move(input));}
    sequence.set("pieces",std::move(inputs));envelope.set("operation",V::makeStr("model_boolean_sequence"));envelope.set("request",std::move(sequence));
    return nativeBooleanSequenceRequest(envelope);
}

V roundTubeSolid(const Feature& feature, const ExprEnv& env) {
    if (feature.get("section", "round") != "round")
        throw std::runtime_error("Rectangular tube has no exact B-rep");
    const TubeResult preview = buildTube(feature, env);
    if (!preview.error.empty()) throw std::runtime_error(preview.error);
    auto number = [&](const std::string& text) { return expressionValue(text, env); };
    auto paramOr = [&](const char* key, double fallback) {
        const auto found = feature.p.find(key);
        return found == feature.p.end() ? fallback : number(found->second);
    };
    V model = V::makeObj(), segments = V::makeArr();
    model.set("op", V::makeStr("round_tube"));
    model.set("od", V::makeNum(paramOr("od", 30)));
    model.set("wall", V::makeNum(paramOr("wall", 2)));
    for (const TubeBend& bend : feature.bends) {
        const double feed = number(bend.feed);
        if (feed > 1e-9) {
            V line = V::makeObj();
            line.set("kind", V::makeStr("line"));
            line.set("length", V::makeNum(feed));
            segments.arr.push_back(std::move(line));
        }
        V arc = V::makeObj();
        arc.set("kind", V::makeStr("arc"));
        arc.set("radius", V::makeNum(number(bend.radius)));
        arc.set("angle", V::makeNum(number(bend.angle)));
        arc.set("roll", V::makeNum(number(bend.roll)));
        segments.arr.push_back(std::move(arc));
    }
    const double tail = paramOr("tail", 100);
    if (tail > 1e-9) {
        V line = V::makeObj();
        line.set("kind", V::makeStr("line"));
        line.set("length", V::makeNum(tail));
        segments.arr.push_back(std::move(line));
    }
    if (segments.arr.empty()) throw std::runtime_error("Tube must have positive developed length");
    model.set("segments", std::move(segments));
    return solidWorker(std::move(model));
}

V rectTubeSolid(const Feature& feature, const ExprEnv& env) {
    const TubeResult preview = buildTube(feature, env);
    if (!preview.error.empty()) throw std::runtime_error(preview.error);
    auto number = [&](const std::string& text) { return expressionValue(text, env); };
    auto paramOr = [&](const char* key, double fallback) {
        const auto found = feature.p.find(key);
        return found == feature.p.end() ? fallback : number(found->second);
    };
    V model = V::makeObj(), segments = V::makeArr();
    model.set("op", V::makeStr("rect_tube"));
    model.set("width", V::makeNum(paramOr("width", 40)));
    model.set("height", V::makeNum(paramOr("height", 30)));
    model.set("wall", V::makeNum(paramOr("wall", 2)));
    for (const TubeBend& bend : feature.bends) {
        const double feed = number(bend.feed);
        if (feed > 1e-9) {
            V line = V::makeObj();
            line.set("kind", V::makeStr("line"));
            line.set("length", V::makeNum(feed));
            segments.arr.push_back(std::move(line));
        }
        V arc = V::makeObj();
        arc.set("kind", V::makeStr("arc"));
        arc.set("radius", V::makeNum(number(bend.radius)));
        arc.set("angle", V::makeNum(number(bend.angle)));
        arc.set("roll", V::makeNum(number(bend.roll)));
        segments.arr.push_back(std::move(arc));
    }
    const double tail = paramOr("tail", 100);
    if (tail > 1e-9) {
        V line = V::makeObj();
        line.set("kind", V::makeStr("line"));
        line.set("length", V::makeNum(tail));
        segments.arr.push_back(std::move(line));
    }
    if (segments.arr.empty()) throw std::runtime_error("Tube must have positive developed length");
    model.set("segments", std::move(segments));
    return solidWorker(std::move(model));
}
} // namespace

bool isNativeFeature(FeatureType type) {
    return isNativeSurfaceFeature(type) || isCurveFeature(type) || type == FeatureType::SheetMetal ||
           type == FeatureType::SourceSheet || type == FeatureType::NativeSolid ||
           type == FeatureType::ConstructionFrame || type == FeatureType::PCB || type == FeatureType::CableRoute;
}

bool validateImportedComponentDefinition(const Feature& feature,std::string& error){
    const auto* op=feature.nativeSpec.find("op");
    if(featureOperation(feature)!="imported_brep"&&(!op||op->t!=V::Str||op->str!="imported_brep"))return true;
    auto fail=[&](const char* message){error=message;return false;};
    if(feature.type!=FeatureType::NativeSolid||featureOperation(feature)!="imported_brep"||!feature.inputs.empty())return fail("Imported component must be an immutable native_solid without feature inputs");
    for(const auto& item:feature.p)if(item.first!="density")return fail("Imported component geometry has no editable dimensions; use downstream exact operations or a rigid placement");
    const auto* package=feature.nativeSpec.find("package");if(!package||package->t!=V::Obj)return fail("Imported component requires a source-owned package");
    const V* format=package->find("format"),*version=package->find("version"),*body=package->find("body_index"),*count=package->find("source_solid_count");
    if(!format||format->t!=V::Str||format->str!="ark-source-component"||!version||version->t!=V::Num||version->num!=1)return fail("Unsupported imported component package");
    if(!body||body->t!=V::Num||!std::isfinite(body->num)||(body->num<0&&body->num!=-2)||body->num!=std::floor(body->num)||!count||count->t!=V::Num||count->num<1||count->num!=std::floor(count->num)||count->num>10000||body->num>=count->num)return fail("Invalid imported component body selection");
    for(const char* key:{"source_path","source_sha256","source_step_base64","brep_base64","brep_sha256"}){const auto* value=package->find(key);if(!value||value->t!=V::Str||value->str.empty()||value->str.size()>96u*1024u*1024u)return fail("Imported component source/hash/B-rep field missing or oversized");}
    for(const char* key:{"source_sha256","brep_sha256"}){const auto& value=package->find(key)->str;if(value.size()!=64||value.find_first_not_of("0123456789abcdef")!=std::string::npos)return fail("Imported component needs SHA-256 digests");}
    if(const auto* component=feature.nativeSpec.find("component_id")){const std::string expected="sha256:"+package->find("source_sha256")->str+":body:"+(body->num==-2?std::string("all"):std::to_string((int)body->num));if(component->t!=V::Str||component->str!=expected)return fail("Imported component identity does not match its original STEP and selected bodies");}
    return true;
}
void validateNativeComponentCustody(const Document& document){
    const auto loaded=assemblyLoadedFeatures(document);std::set<u64> active;bool prefix=true;for(const auto& feature:document.features){if(prefix&&loaded.count(feature.id))active.insert(feature.id);if(feature.id==document.historyTip)prefix=false;}
    for(const auto& feature:document.features)if(!feature.suppressed&&feature.type==FeatureType::NativeSolid&&featureOperation(feature)=="imported_cad"){std::string error;if(!validateCadPackageAuthoring(feature,error))throw std::runtime_error(error);if(!active.count(feature.id))continue;V request=V::makeObj();request.set("operation",V::makeStr("cad_custody"));request.set("package",*feature.nativeSpec.find("package"));nativeWorkerRequest(request);}
    for(const auto& feature:document.features)if(!feature.suppressed&&feature.type==FeatureType::NativeSolid&&featureOperation(feature)=="imported_brep"){
        std::string error;if(!validateImportedComponentDefinition(feature,error))throw std::runtime_error(error);
        if(!active.count(feature.id))continue;
        V request=V::makeObj();request.set("operation",V::makeStr("component_custody"));request.set("package",*feature.nativeSpec.find("package"));nativeWorkerRequest(request);
    }
}

arkjson::Value sketchEntityNativeProfile(Document& document,const std::string& name,const std::string& entity){
    auto source=document.findSketch(name);if(!source||!source->error.empty())throw std::runtime_error("Projection source sketch is missing or invalid: "+name);
    const SketchEnt* found=nullptr;for(const auto& item:source->sk.ents)if(item.name==entity){if(found)throw std::runtime_error("Ambiguous projection source entity");found=&item;}
    if(!found||found->kind==SketchEnt::Pnt||found->kind==SketchEnt::Offset)throw std::runtime_error("Projection source must be a named exact line, circle, arc, spline or wire");
    if(found->kind==SketchEnt::Project&&found->projectionBrep.empty())throw std::runtime_error("Projection source sketch entity is not evaluated yet");
    NamedSketch selected=*source;selected.sk.ents={*found};selected.sk.ents.front().construction=false;return profileFromSketch(document,name,&selected);
}

Body evaluateNativeFeature(const Feature& feature, const ExprEnv& env,
                           Document& document, std::vector<Body>& preceding) {
    V result;
    if(feature.type==FeatureType::ConstructionFrame)return evaluateFrameFeature(feature,env,document,preceding);
    if(feature.type==FeatureType::PCB||feature.type==FeatureType::CableRoute){
        V request=V::makeObj();request.set("operation",V::makeStr("model_electronics"));
        V occurrenceBindingReport;
        auto occurrenceBound=[&](){
            for(const char* key:{"start","end"})if(const auto* endpoint=feature.nativeSpec.find(key);endpoint&&endpoint->t==V::Obj&&endpoint->find("node"))return true;
            for(const char* key:{"waypoints","obstacles"})if(const auto* entries=feature.nativeSpec.find(key);entries&&entries->t==V::Arr)for(const auto& entry:entries->arr)if(entry.t==V::Obj&&entry.find("node"))return true;
            return false;
        };
        if(feature.type==FeatureType::CableRoute&&occurrenceBound()){
            Feature bound=feature;
            {
                // Resolve against the caller's current evaluation frontier, keeping
                // exact-reference memoization without copying all preceding B-reps.
                auto* previous=document.evaluatingBodies;
                struct BindingScope {Document& document;std::vector<Body>* previous;~BindingScope(){document.evaluatingBodies=previous;}} scope{document,previous};
                document.evaluatingBodies=&preceding;
                bound.nativeSpec=resolveCableBindings031B(feature,env,document);
            }
            if(const auto* binding=bound.nativeSpec.find("occurrence_binding_report"))occurrenceBindingReport=*binding;
            request.set("request",electronicsRequest(bound,env));
        }else request.set("request",electronicsRequest(feature,env));
        result=nativeWorkerRequest(request);
        if(occurrenceBindingReport.t==V::Obj){V report=required(result,"report");report.set("occurrence_binding_report",occurrenceBindingReport);result.set("report",report);if(const auto* electronics=result.find("electronics")){V routed=*electronics;routed.set("occurrence_binding_report",occurrenceBindingReport);result.set("electronics",routed);}}
        Body body;body.nativeData=std::move(result);body.baked=std::make_unique<Mesh>(nativeResultMesh(body.nativeData));return body;
    }
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
        request.set("request", sheetRequest(feature, env,document));
        result = nativeWorkerRequest(request);
    } else if (feature.type == FeatureType::NativeSolid) {
        result = spansExactBoolean(feature) ? combineExactBoolean(feature, document, preceding)
                                             : solidWorker(solidFeatureRequest(feature, env, document, preceding));
    } else if(feature.type==FeatureType::Curve&&feature.nativeSpec.find("op")&&feature.nativeSpec.find("op")->str=="project") result=spatialProjectionResult(feature,document);
    else result = nativeModelRequest(surfaceRequest(feature, env, document, preceding));
    Body body;
    if(feature.type==FeatureType::NativeSolid)appendTubeEndFitOptions(result,document,feature.id,env);
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
    if (feature->type == FeatureType::Tube) {
        body->nativeData = feature->get("section", "round") == "rect" ? rectTubeSolid(*feature, env) : roundTubeSolid(*feature, env);
        body->nativeData.mutate([&](V& data){appendTubeEndFitOptions(data,document,featureId,env);});
        return;
    }
    if (feature->type == FeatureType::Transform || feature->type == FeatureType::Boolean) {
        body->nativeData = legacyOperationResult(*feature, env, document, bodies);
    } else body->nativeData = exactPrimitive(*feature, env, document, *body);
    body->nativeData.mutate([&](V& data){appendTubeEndFitOptions(data,document,featureId,env);});
}

Body evaluateExactLegacyOperation(const Feature& feature, const ExprEnv& env,
                                  Document& document, std::vector<Body>& preceding) {
    if(feature.type==FeatureType::LinearPattern){
        if(feature.inputs.size()!=1)throw std::runtime_error("Exact linear pattern requires one native input");
        const double count=expressionValue(feature.get("count","3"),env);
        if(count<1||count>64||count!=std::floor(count))
            throw std::runtime_error("Exact linear pattern count must be an integer in 1..64");
        // Keep the authored legacy feature and its expression sources intact.
        // Only this evaluation request uses the native solid operation; a
        // rejected exact pattern cannot fall back to its display triangles.
        Feature native=feature;native.type=FeatureType::NativeSolid;
        native.nativeSpec=V::makeObj();native.p.clear();
        native.nativeSpec.set("op",V::makeStr(count==1?"transform":"linear_pattern"));
        if(count==1)native.nativeSpec.set("frame",frameValue(Mat4::identity()));
        else{
            native.p={{"count",feature.get("count","3")},{"dx",feature.get("dx","20")},
                      {"dy",feature.get("dy","0")},{"dz",feature.get("dz","0")}};
        }
        return evaluateNativeFeature(native,env,document,preceding);
    }
    Body result;
    result.nativeData = legacyOperationResult(feature, env, document, preceding);
    result.nativeData.mutate([&](V& data){appendTubeEndFitOptions(data,document,feature.id,env);});
    result.baked = std::make_unique<Mesh>(nativeResultMesh(result.nativeData));
    return result;
}
} // namespace ark
