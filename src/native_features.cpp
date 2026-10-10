#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/references.hpp"
#include "ark/sketch.hpp"
#include "ark/tube.hpp"
#include "ark/frame.hpp"
#include "ark/electronics.hpp"

#include <algorithm>
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

V profileFromSketch(Document& document, const std::string& name,const NamedSketch* supplied=nullptr) {
    const NamedSketch* named = supplied?supplied:document.findSketch(name);
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
        if(source.kind==SketchEnt::Project){
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
        } else if (source.kind == SketchEnt::Wire) {
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
                const int payload = sketchVertexDof(source, i) + 2;
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
        if (key == "kind" || key == "sketch" || key == "name" || key == "op" || key == "reference" || key == "second_reference" || key == "mode" || key == "end" || key == "edges" || key == "faces" || key == "face" || key == "plane" || key == "keep" || key == "selection" || key == "direction" || key == "axis" || key == "neutral") return value;
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
        const double ruled = param(feature, env, "ruled");
        if ((solid != 0 && solid != 1) || (ruled != 0 && ruled != 1))
            throw std::runtime_error("Loft solid and ruled must be 0 or 1");
        request.set("solid", V::makeBool(solid == 1));
        request.set("ruled", V::makeBool(ruled == 1));
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
    } else if (feature.type == FeatureType::Sweep) {
        request.set("op", V::makeStr("sweep"));
        request.set("section", profileArgument(feature, env, document, preceding, "section"));
        request.set("path", profileArgument(feature, env, document, preceding, "path"));
        request.set("solid", V::makeBool(bitParam(feature, env, "solid", 0)));
        request.set("correction", V::makeBool(bitParam(feature, env, "correction", 1)));
        request.set("contact", V::makeBool(bitParam(feature, env, "contact", 0)));
    } else if (feature.type == FeatureType::ExtrudeSurface || feature.type == FeatureType::RevolveSurface) {
        const bool revolve = feature.type == FeatureType::RevolveSurface;
        request.set("op", V::makeStr(revolve ? "revolve_surface" : "extrude_surface"));
        if (const V* profile = feature.nativeSpec.find("profile")) request.set("profile", numericNative(*profile, env));
        else request.set("profile", profileFromSketch(document, feature.get("sketch")));
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
        if(feature.inputs.size()!=1)throw std::runtime_error("surface_edit requires one exact native input");
        const Body* source=precedingBody(preceding,feature.inputs.front());
        if(!source)throw std::runtime_error("surface_edit input missing");
        request.set("input_brep",brepOf(*source,"surface_edit input"));
        const auto& operation=required(feature.nativeSpec,"op");
        if(operation.t!=V::Str)throw std::runtime_error("surface_edit op must be extract_face or trim_plane");
        if(operation.str=="extract_face"){
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
        }else throw std::runtime_error("surface_edit op must be extract_face or trim_plane");
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
    } else if (isCurveFeature(feature.type)) {
        if (feature.type == FeatureType::Curve) {
            request.set("op", V::makeStr("curve"));
            const V spec = numericNative(feature.nativeSpec, env);
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
                    if (!named) throw std::runtime_error("Feature name not found: " + featureValue.str);
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

V solidFeatureRequest(const Feature& feature, const ExprEnv& env,
                     Document& document, std::vector<Body>& preceding) {
    if(featureOperation(feature)=="imported_brep"){
        std::string error;if(!validateImportedComponentDefinition(feature,error))throw std::runtime_error(error);
        V request=V::makeObj();request.set("op",V::makeStr("imported_brep"));request.set("package",required(feature.nativeSpec,"package"));return request;
    }
    V model = numericNative(feature.nativeSpec, env);
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
    if(op=="tube_end_fit"){
        if(feature.inputs.size()!=2||feature.inputs[0]==feature.inputs[1])throw std::runtime_error("Tube end fit needs distinct source and host inputs");
        const std::set<std::string> allowed{"op","end","mode","reference"};for(const auto& item:feature.nativeSpec.obj)if(!allowed.count(item.first))throw std::runtime_error("Unsupported tube end fit field: "+item.first);
        for(const auto& item:feature.p)if(item.first!="max_extension"&&item.first!="density")throw std::runtime_error("Unsupported tube end fit parameter: "+item.first);
        if(!feature.p.count("max_extension"))throw std::runtime_error("Tube end fit needs explicit max_extension");
        const auto* fitMode=model.find("mode");if(!fitMode||fitMode->t!=V::Str||(fitMode->str!="envelope"&&fitMode->str!="plane"&&fitMode->str!="plane_trim"))throw std::runtime_error("Tube end fit mode must be envelope, plane or plane_trim");
        model.set("tube_authority",tubeAuthority(document,feature.inputs[0],env,true));
        if(const auto* mode=model.find("mode");mode&&mode->t==V::Str&&mode->str=="envelope")model.set("host_authority",tubeAuthority(document,feature.inputs[1],env,false));
    }
    if ((op == "curve_pattern" || op == "hole_pattern") && feature.inputs.size() != 2)
        throw std::runtime_error("Curve pattern needs target and path feature inputs");
    if (op == "shell" && feature.inputs.size() != 1)
        throw std::runtime_error("Shell needs one solid input");
    if (op == "split_plane" && feature.inputs.size() != 1)
        throw std::runtime_error("Split plane needs one exact solid input");
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
           (feature.nativeSpec.find("instances") || feature.inputs.size() > 2);
}

struct ExactPiece { std::string brep; V topology; Mat4 frame; bool framed=false; std::string nameSpace; };

V placePiece(const ExactPiece& piece) {
    if (!piece.framed) {
        V held = V::makeObj();
        held.set("brep_base64", V::makeStr(piece.brep));
        if (piece.topology.t == V::Obj) held.set("topology", piece.topology);
        return held;
    }
    V model = V::makeObj();
    model.set("op", V::makeStr("transform"));
    model.set("frame", frameValue(piece.frame));
    model.set("input_brep", V::makeStr(piece.brep));
    if (piece.topology.t == V::Obj) model.set("input_topology", piece.topology);
    return solidWorker(std::move(model));
}

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
    V current = placePiece(pieces.front());
    for (size_t index = 1; index < pieces.size(); ++index) {
        V tool = placePiece(pieces[index]);
        V model = V::makeObj();
        model.set("op", V::makeStr(op));
        model.set("input_brep", required(current, "brep_base64"));
        if (const V* topology = current.find("topology")) model.set("input_topology", *topology);
        model.set("tool_brep", required(tool, "brep_base64"));
        if (const V* topology = tool.find("topology")) model.set("tool_topology", *topology);
        model.set("tool_namespace", V::makeStr(pieces[index].nameSpace));
        current = solidWorker(std::move(model));
    }
    return current;
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
    for(const auto& feature:document.features)if(!feature.suppressed&&feature.type==FeatureType::NativeSolid&&featureOperation(feature)=="imported_brep"){
        std::string error;if(!validateImportedComponentDefinition(feature,error))throw std::runtime_error(error);
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
        request.set("request",electronicsRequest(feature,env));result=nativeWorkerRequest(request);
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
        return;
    }
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
