// SPDX-License-Identifier: Apache-2.0
// 059C: A.02 validated inputs; B.04 reusable indexed placement; D.02 bounded tool context.
#include "ark/rubber_patterns.hpp"
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace ark {
namespace {
using J = arkjson::Value;
constexpr double kMinDimension = 1e-6, kMaxDimension = 1e6;
enum class Surface { Cylinder, Torus, Sphere, Racetrack };
enum class Distribution { Grid, Stagger, Helix, Chevron, Braided, Fibonacci, SphericalWeave };

[[noreturn]] void invalid(const std::string& message) {
    throw std::invalid_argument("rubber_pattern: " + message);
}
void keys(const J& value, std::initializer_list<std::string_view> allowed, const char* path) {
    if (value.t != J::Obj) invalid(std::string(path) + " must be an object");
    if (value.obj.size() > allowed.size()) invalid(std::string(path) + " has too many fields");
    for (size_t i = 0; i < value.obj.size(); ++i) {
        const auto& name = value.obj[i].first;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            invalid(std::string(path) + " has an unknown field");
        for (size_t j = 0; j < i; ++j)
            if (name == value.obj[j].first) invalid(std::string(path) + " has a duplicate field");
    }
}
double number(const J& object, const char* key, double fallback, double lo, double hi, bool required = false) {
    const J* value = object.find(key);
    if (!value) {
        if (required) invalid(std::string(key) + " is required");
        return fallback;
    }
    if (value->t != J::Num || !std::isfinite(value->num) || value->num < lo || value->num > hi)
        invalid(std::string(key) + " must be a finite number within its supported range");
    return value->num;
}
std::string name(const J& object, const char* key, const char* fallback) {
    const J* value = object.find(key);
    if (!value) {
        if (!fallback) invalid(std::string(key) + " is required");
        return fallback;
    }
    if (value->t != J::Str || value->str.size() > 32) invalid(std::string(key) + " must be a short string");
    return value->str;
}
uint32_t integer(const J& object, const char* key) {
    const double n = number(object, key, 0, 1, RUBBER_PATTERN_MAX_COUNT, true);
    if (n != std::floor(n)) invalid(std::string(key) + " must be an integer");
    return static_cast<uint32_t>(n);
}
void absent(const J& object, const char* key) {
    if (object.find(key)) invalid(std::string(key) + " is not supported by this surface/distribution");
}
Vec3 vector(const J& object, const char* key, Vec3 fallback, double maxAbs) {
    const J* value = object.find(key);
    if (!value) return fallback;
    if (value->t != J::Arr || value->arr.size() != 3) invalid(std::string(key) + " must be a 3-vector");
    double v[3];
    for (int i = 0; i < 3; ++i) {
        if (value->arr[i].t != J::Num || !std::isfinite(value->arr[i].num) || std::abs(value->arr[i].num) > maxAbs)
            invalid(std::string(key) + " contains an invalid coordinate");
        v[i] = value->arr[i].num;
    }
    return {v[0], v[1], v[2]};
}
double wrap(double radians) {
    const double result = std::fmod(radians, K_2PI);
    const double positive = result < 0 ? result + K_2PI : result;
    return positive >= K_2PI ? 0 : positive;
}
J jsonVector(Vec3 v) {
    J result = J::makeArr();
    for (double n : {v.x, v.y, v.z}) result.arr.push_back(J::makeNum(n));
    return result;
}
J parseLiteral(const char* source) {
    J value;
    if (!arkjson::parse(source, value)) throw std::logic_error("Invalid built-in rubber pattern JSON");
    return value;
}
} // namespace

struct RubberPatternPlan::Impl {
    Surface surface;
    Distribution distribution;
    std::string surfaceName, distributionName, motif;
    uint32_t count = 0, around = 0, rows = 0;
    double radius = 0, majorRadius = 0, axialRadius = 0, centerDistance = 0;
    double first = 0, last = 0, phase = 0, orientation = 0, offset = 0;
    double turns = 1, weave = 0;
    Vec3 origin, placementX{1,0,0}, placementY{0,1,0}, placementZ{0,0,1};
    Vec3 direction(Vec3 p) const { return placementX*p.x + placementY*p.y + placementZ*p.z; }
};

RubberPatternPlan::RubberPatternPlan(const J& recipe) {
    keys(recipe, {"surface", "distribution", "around", "rows", "count", "phase_deg", "turns",
                  "orientation_deg", "normal_offset_mm", "placement", "motif", "weave_deg"}, "recipe");
    auto p = std::make_shared<Impl>();
    const J* surface = recipe.find("surface");
    if (!surface || surface->t != J::Obj) invalid("surface must be an object");
    p->surfaceName = name(*surface, "kind", nullptr);
    if (p->surfaceName == "cylinder") {
        p->surface = Surface::Cylinder;
        keys(*surface, {"kind", "radius_mm", "length_mm", "axial_start_mm", "axial_end_mm"}, "surface");
        p->radius = number(*surface, "radius_mm", 0, kMinDimension, kMaxDimension, true);
        const double length = number(*surface, "length_mm", 0, kMinDimension, kMaxDimension, true);
        p->first = number(*surface, "axial_start_mm", -length/2, -length/2, length/2);
        p->last = number(*surface, "axial_end_mm", length/2, -length/2, length/2);
    } else if (p->surfaceName == "torus") {
        p->surface = Surface::Torus;
        keys(*surface, {"kind", "major_radius_mm", "radial_radius_mm", "axial_radius_mm", "v_start_deg", "v_end_deg"}, "surface");
        p->majorRadius = number(*surface, "major_radius_mm", 0, kMinDimension, kMaxDimension, true);
        p->radius = number(*surface, "radial_radius_mm", 0, kMinDimension, kMaxDimension, true);
        p->axialRadius = number(*surface, "axial_radius_mm", 0, kMinDimension, kMaxDimension, true);
        if (p->majorRadius - p->radius < kMinDimension) invalid("torus major_radius_mm must exceed radial_radius_mm by at least 1e-6 mm");
        const double firstDegrees = number(*surface, "v_start_deg", -180, -360, 360);
        const double lastDegrees = number(*surface, "v_end_deg", 180, -360, 360);
        if (lastDegrees - firstDegrees > 360) invalid("torus section interval cannot exceed 360 degrees");
        p->first = deg2rad(firstDegrees); p->last = deg2rad(lastDegrees);
    } else if (p->surfaceName == "sphere") {
        p->surface = Surface::Sphere;
        keys(*surface, {"kind", "radius_mm", "polar_start_deg", "polar_end_deg"}, "surface");
        p->radius = number(*surface, "radius_mm", 0, kMinDimension, kMaxDimension, true);
        p->first = deg2rad(number(*surface, "polar_start_deg", 0, 0, 180));
        p->last = deg2rad(number(*surface, "polar_end_deg", 180, 0, 180));
    } else if (p->surfaceName == "racetrack") {
        p->surface = Surface::Racetrack;
        keys(*surface, {"kind", "radius_mm", "center_distance_mm", "width_mm", "width_start_mm", "width_end_mm"}, "surface");
        p->radius = number(*surface, "radius_mm", 0, kMinDimension, kMaxDimension, true);
        p->centerDistance = number(*surface, "center_distance_mm", 0, 0, kMaxDimension, true);
        const double width = number(*surface, "width_mm", 0, kMinDimension, kMaxDimension, true);
        p->first = number(*surface, "width_start_mm", -width/2, -width/2, width/2);
        p->last = number(*surface, "width_end_mm", width/2, -width/2, width/2);
    } else invalid("unsupported surface kind");
    if (!(p->last > p->first)) invalid("surface interval must have increasing, distinct endpoints");
    // Avoid collapsed midpoint strata near a pole. This is a numeric bound, not a
    // manufacturing tolerance; invalid recipes are rejected rather than widened.
    if (p->surface == Surface::Sphere && std::cos(p->first) - std::cos(p->last) < 1e-8)
        invalid("sphere polar interval must span at least 1e-8 in cos(polar angle)");
    p->distributionName = name(recipe, "distribution", p->surface == Surface::Sphere ? "fibonacci" : "grid");
    if (p->distributionName == "grid") p->distribution = Distribution::Grid;
    else if (p->distributionName == "stagger") p->distribution = Distribution::Stagger;
    else if (p->distributionName == "helix") p->distribution = Distribution::Helix;
    else if (p->distributionName == "chevron") p->distribution = Distribution::Chevron;
    else if (p->distributionName == "braided") p->distribution = Distribution::Braided;
    else if (p->distributionName == "fibonacci") p->distribution = Distribution::Fibonacci;
    else if (p->distributionName == "spherical_weave") p->distribution = Distribution::SphericalWeave;
    else invalid("unsupported distribution");
    const bool sphericalDistribution = p->distribution == Distribution::Fibonacci || p->distribution == Distribution::SphericalWeave;
    if ((p->surface == Surface::Sphere && !sphericalDistribution && p->distribution != Distribution::Grid) ||
        (p->surface != Surface::Sphere && sphericalDistribution)) invalid("distribution is incompatible with surface");
    if (sphericalDistribution) {
        p->count = integer(recipe, "count");
        absent(recipe, "around"); absent(recipe, "rows");
    } else {
        p->around = integer(recipe, "around"); p->rows = integer(recipe, "rows");
        const uint64_t count = uint64_t(p->around) * p->rows;
        if (count > RUBBER_PATTERN_MAX_COUNT) invalid("around * rows exceeds max_count");
        p->count = static_cast<uint32_t>(count);
        absent(recipe, "count");
    }
    const bool winding = p->distribution == Distribution::Helix || p->distribution == Distribution::Chevron ||
                         p->distribution == Distribution::Braided || p->distribution == Distribution::SphericalWeave;
    if (winding) {
        p->turns = number(recipe, "turns", 1, -64, 64);
        if (p->turns != std::floor(p->turns)) invalid("turns must be an integer");
    } else absent(recipe, "turns");
    if (p->distribution == Distribution::SphericalWeave)
        p->weave = deg2rad(number(recipe, "weave_deg", 20, -180, 180));
    else absent(recipe, "weave_deg");
    p->phase = wrap(deg2rad(number(recipe, "phase_deg", 0, -360000, 360000)));
    p->orientation = wrap(deg2rad(number(recipe, "orientation_deg", 0, -360000, 360000)));
    p->offset = number(recipe, "normal_offset_mm", 0, -kMaxDimension, kMaxDimension);
    p->motif = name(recipe, "motif", "spike");
    if (p->motif != "spike" && p->motif != "lug" && p->motif != "traction_bar" && p->motif != "pebble") invalid("unsupported motif hint");
    if (const J* placement = recipe.find("placement")) {
        keys(*placement, {"origin_mm", "x_axis", "z_axis"}, "placement");
        p->origin = vector(*placement, "origin_mm", {}, 1e9);
        p->placementX = vector(*placement, "x_axis", {1,0,0}, 1);
        p->placementZ = vector(*placement, "z_axis", {0,0,1}, 1);
        if (std::abs(p->placementX.lensq()-1) > 1e-10 || std::abs(p->placementZ.lensq()-1) > 1e-10 ||
            std::abs(p->placementX.dot(p->placementZ)) > 1e-10)
            invalid("placement x_axis and z_axis must be orthonormal unit vectors");
        // Remove accepted roundoff, not arbitrary scale/shear. No reflection.
        p->placementZ = p->placementZ.normalized();
        p->placementX = (p->placementX - p->placementZ*p->placementZ.dot(p->placementX)).normalized();
        p->placementY = p->placementZ.cross(p->placementX);
    }
    impl_ = std::move(p);
}

uint32_t RubberPatternPlan::count() const { return impl_->count; }

RubberPatternFrame RubberPatternPlan::frame(uint32_t index) const {
    const auto& p = *impl_;
    if (index >= p.count) throw std::out_of_range("rubber_pattern: frame index exceeds count");
    RubberPatternFrame result;
    result.index = index;
    Vec3 position, normal, tangent;
    if (p.surface == Surface::Sphere) {
        // Midpoint cylindrical equal-area map with irrational golden azimuth.
        // Forward sampling only; this is not the inverse-neighbor algorithm in:
        // Keinert et al., Spherical Fibonacci Mapping (2015), doi:10.1145/2816795.2818131.
        // Equal-area construction: Hardin/Michaels/Saff (2016), section 2.1,
        // https://drna.padovauniversitypress.it/system/files/papers/HardinMichaelsSaff_2016_CPC.pdf
        constexpr double goldenConjugate = 0.6180339887498948482;
        double z, radial, longitude;
        if (p.distribution == Distribution::Grid) {
            const uint32_t column = index % p.around, row = index / p.around;
            result.v_rad = lerp(p.first, p.last, (static_cast<double>(row)+0.5)/p.rows);
            z = std::cos(result.v_rad); radial = std::sin(result.v_rad);
            longitude = K_2PI*static_cast<double>(column)/p.around+p.phase;
        } else {
            const double fraction = (static_cast<double>(index) + 0.5) / p.count;
            z = lerp(std::cos(p.first), std::cos(p.last), fraction);
            result.v_rad = std::acos(clamp(z, -1.0, 1.0));
            radial = std::sqrt(std::max(0.0, (1-z)*(1+z)));
            longitude = K_2PI * std::fmod(index * goldenConjugate, 1.0) + p.phase +
                (p.distribution == Distribution::SphericalWeave ? p.weave*std::sin(K_PI*p.turns*z) : 0);
        }
        result.u_rad = wrap(longitude);
        const double c = std::cos(result.u_rad), s = std::sin(result.u_rad);
        normal = {radial*c, radial*s, z}; position = normal*p.radius;
        tangent = {-s, c, 0};
    } else {
        // Row-major indexing: column varies fastest. Midpoint rows avoid doubled
        // torus seams and sample coverage without drifting outside a tire band.
        const uint32_t column = index % p.around, row = index / p.around;
        const double fraction = (static_cast<double>(row)+0.5) / p.rows;
        double shift = 0;
        switch (p.distribution) {
            case Distribution::Stagger: shift = (row%2)*0.5/p.around; break;
            case Distribution::Helix: shift = p.turns*fraction; break;
            case Distribution::Chevron: shift = p.turns*std::abs(2*fraction-1); break;
            case Distribution::Braided:
                // Alternating winding courses plus a half-column stagger. This
                // is an ARK recipe combination, not a claimed literature first.
                shift = (row%2 ? -1.0 : 1.0)*p.turns*fraction + (row%2)*0.5/p.around;
                break;
            default: break;
        }
        result.u_rad = wrap(K_2PI*(static_cast<double>(column)/p.around + shift) + p.phase);
        const double transverse = lerp(p.first, p.last, fraction);
        const double c = std::cos(result.u_rad), s = std::sin(result.u_rad);
        if (p.surface == Surface::Cylinder) {
            position = {p.radius*c, p.radius*s, transverse};
            normal = {c,s,0}; tangent = {-s,c,0}; result.transverse_mm = transverse;
        } else if (p.surface == Surface::Torus) {
            result.v_rad = transverse;
            const double cv = std::cos(transverse), sv = std::sin(transverse);
            position = {(p.majorRadius+p.radius*cv)*c, (p.majorRadius+p.radius*cv)*s, p.axialRadius*sv};
            normal = Vec3{cv*c/p.radius, cv*s/p.radius, sv/p.axialRadius}.normalized();
            tangent = {-s,c,0};
        } else {
            // Equal-radius racetrack, CCW from top-right: top line, left arc,
            // bottom line, right arc. x follows arc length; width is local +Z.
            const double d = p.centerDistance, r = p.radius, arc = K_PI*r;
            double distance = (2*d+2*arc)*(result.u_rad/K_2PI);
            if (distance < d) {
                position = {d/2-distance,r,transverse}; normal = {0,1,0}; tangent = {-1,0,0};
            } else if ((distance -= d) < arc) {
                const double a = K_HALF_PI + distance/r;
                normal = {std::cos(a),std::sin(a),0}; tangent = {-normal.y,normal.x,0};
                position = {-d/2+r*normal.x,r*normal.y,transverse};
            } else if ((distance -= arc) < d) {
                position = {-d/2+distance,-r,transverse}; normal = {0,-1,0}; tangent = {1,0,0};
            } else {
                distance -= d;
                const double a = 3*K_HALF_PI + distance/r;
                normal = {std::cos(a),std::sin(a),0}; tangent = {-normal.y,normal.x,0};
                position = {d/2+r*normal.x,r*normal.y,transverse};
            }
            result.transverse_mm = transverse;
        }
    }
    const Vec3 bitangent = normal.cross(tangent);
    tangent = tangent*std::cos(p.orientation) + bitangent*std::sin(p.orientation);
    result.normal = p.direction(normal);
    result.tangent = p.direction(tangent);
    result.bitangent = result.normal.cross(result.tangent);
    result.position_mm = p.origin + p.direction(position) + result.normal*p.offset;
    for (int column = 0; column < 4; ++column) {
        const Vec3 v = column == 0 ? result.tangent : column == 1 ? result.bitangent : column == 2 ? result.normal : result.position_mm;
        result.transform.m[column*4] = v.x;
        result.transform.m[column*4+1] = v.y;
        result.transform.m[column*4+2] = v.z;
    }
    return result;
}

J RubberPatternPlan::diagnostics() const {
    J out = J::makeObj();
    out.set("surface", J::makeStr(impl_->surfaceName));
    out.set("distribution", J::makeStr(impl_->distributionName));
    out.set("motif", J::makeStr(impl_->motif));
    out.set("count", J::makeNum(count()));
    out.set("geometry_authority", J::makeStr("analytical_placement_recipe_only"));
    out.set("storage", J::makeStr("constant_size_compiled_recipe"));
    out.set("indexed_access", J::makeStr("constant_work_no_instance_allocation"));
    out.set("axes", J::makeStr("x longitudinal tangent; y = z cross x; z outward normal; orientation_deg rotates x/y"));
    out.set("sampling", J::makeStr(impl_->surface == Surface::Sphere && impl_->distribution != Distribution::Grid ?
        "equal_area_polar_strata_midpoints" : "row_midpoints_and_half_open_longitude"));
    out.set("overlap_checked", J::makeBool(false));
    out.set("manufacturing_qualified", J::makeBool(false));
    out.set("surface_binding_required", J::makeBool(true));
    return out;
}

J rubberPatternFrameJson(const RubberPatternFrame& f) {
    J result = J::makeObj(), transform = J::makeArr();
    result.set("index", J::makeNum(f.index));
    result.set("position_mm", jsonVector(f.position_mm));
    result.set("tangent", jsonVector(f.tangent)); result.set("bitangent", jsonVector(f.bitangent)); result.set("normal", jsonVector(f.normal));
    for (double n : f.transform.m) transform.arr.push_back(J::makeNum(n));
    result.set("transform_column_major", std::move(transform));
    result.set("u_rad", J::makeNum(f.u_rad)); result.set("v_rad", J::makeNum(f.v_rad));
    result.set("transverse_mm", J::makeNum(f.transverse_mm));
    return result;
}

J previewRubberPattern(const J& recipe, uint32_t offset, uint32_t limit) {
    if (!limit || limit > RUBBER_PATTERN_MAX_PREVIEW) invalid("preview limit must be within 1..256");
    const RubberPatternPlan plan(recipe);
    if (offset > plan.count()) invalid("preview offset exceeds count");
    const uint32_t returned = std::min(limit, plan.count()-offset);
    J out = J::makeObj(), frames = J::makeArr();
    frames.arr.reserve(returned);
    for (uint32_t i = 0; i < returned; ++i) frames.arr.push_back(rubberPatternFrameJson(plan.frame(offset+i)));
    out.set("count", J::makeNum(plan.count())); out.set("offset", J::makeNum(offset)); out.set("returned", J::makeNum(returned));
    out.set("next_offset", offset+returned < plan.count() ? J::makeNum(offset+returned) : J{});
    out.set("frames", std::move(frames)); out.set("diagnostics", plan.diagnostics());
    return out;
}

J rubberPatternSchema() {
    return parseLiteral(R"json({
      "type":"object","additionalProperties":false,"required":["surface"],
      "properties":{
        "surface":{"oneOf":[
          {"type":"object","additionalProperties":false,"required":["kind","radius_mm","length_mm"],"properties":{"kind":{"const":"cylinder"},"radius_mm":{"$ref":"#/$defs/dimension"},"length_mm":{"$ref":"#/$defs/dimension"},"axial_start_mm":{"type":"number"},"axial_end_mm":{"type":"number"}}},
          {"type":"object","additionalProperties":false,"required":["kind","major_radius_mm","radial_radius_mm","axial_radius_mm"],"properties":{"kind":{"const":"torus"},"major_radius_mm":{"$ref":"#/$defs/dimension"},"radial_radius_mm":{"$ref":"#/$defs/dimension"},"axial_radius_mm":{"$ref":"#/$defs/dimension"},"v_start_deg":{"type":"number","minimum":-360,"maximum":360,"default":-180},"v_end_deg":{"type":"number","minimum":-360,"maximum":360,"default":180}}},
          {"type":"object","additionalProperties":false,"required":["kind","radius_mm"],"properties":{"kind":{"const":"sphere"},"radius_mm":{"$ref":"#/$defs/dimension"},"polar_start_deg":{"type":"number","minimum":0,"maximum":180,"default":0},"polar_end_deg":{"type":"number","minimum":0,"maximum":180,"default":180}}},
          {"type":"object","additionalProperties":false,"required":["kind","radius_mm","center_distance_mm","width_mm"],"properties":{"kind":{"const":"racetrack"},"radius_mm":{"$ref":"#/$defs/dimension"},"center_distance_mm":{"type":"number","minimum":0,"maximum":1000000},"width_mm":{"$ref":"#/$defs/dimension"},"width_start_mm":{"type":"number"},"width_end_mm":{"type":"number"}}}
        ]},
        "distribution":{"enum":["grid","stagger","helix","chevron","braided","fibonacci","spherical_weave"]},
        "around":{"$ref":"#/$defs/count"},"rows":{"$ref":"#/$defs/count"},"count":{"$ref":"#/$defs/count"},
        "phase_deg":{"type":"number","minimum":-360000,"maximum":360000,"default":0},
        "orientation_deg":{"type":"number","minimum":-360000,"maximum":360000,"default":0},
        "turns":{"type":"integer","minimum":-64,"maximum":64,"default":1},
        "weave_deg":{"type":"number","minimum":-180,"maximum":180,"default":20},
        "normal_offset_mm":{"type":"number","minimum":-1000000,"maximum":1000000,"default":0},
        "motif":{"enum":["spike","lug","traction_bar","pebble"],"default":"spike"},
        "placement":{"type":"object","additionalProperties":false,"properties":{"origin_mm":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number","minimum":-1000000000,"maximum":1000000000}},"x_axis":{"$ref":"#/$defs/axis"},"z_axis":{"$ref":"#/$defs/axis"}}}
      },
      "allOf":[{"if":{"properties":{"surface":{"properties":{"kind":{"const":"sphere"}}}}},"then":{"oneOf":[{"required":["count"],"properties":{"distribution":{"enum":["fibonacci","spherical_weave"]}},"not":{"anyOf":[{"required":["around"]},{"required":["rows"]}]}},{"required":["around","rows","distribution"],"properties":{"distribution":{"const":"grid"}},"not":{"required":["count"]}}]},"else":{"required":["around","rows"],"properties":{"distribution":{"enum":["grid","stagger","helix","chevron","braided"]}},"not":{"required":["count"]}}}],
      "$defs":{"dimension":{"type":"number","minimum":0.000001,"maximum":1000000},"count":{"type":"integer","minimum":1,"maximum":1000000},"axis":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number","minimum":-1,"maximum":1}}},
      "description":"Units mm/degrees. Runtime also validates increasing coverage within surface bounds, torus R-a >= 1e-6, section span <= 360 degrees, sphere cos(polar) span >= 1e-8, orthonormal placement axes and count product <= 1000000. turns only for helix/chevron/braided/spherical_weave; weave_deg only for spherical_weave. No B-rep construction or overlap certification."
    })json");
}

J rubberPatternCapabilities() {
    J out = parseLiteral(R"json({
      "capability":"rubber.pattern.indexed.v1",
      "surfaces":["cylinder","torus","sphere","racetrack"],
      "distributions":{"cylinder":["grid","stagger","helix","chevron","braided"],"torus":["grid","stagger","helix","chevron","braided"],"racetrack":["grid","stagger","helix","chevron","braided"],"sphere":["grid","fibonacci","spherical_weave"]},
      "motifs":["spike","lug","traction_bar","pebble"],
      "max_count":1000000,"max_preview":256,
      "preview_plan":["Validate recipe and authored surface dimensions/placement.","Compile once; request count, frame(index), or a bounded preview page.","Reuse one motif with right-handed transforms for visual preview.","Explicit geometry publication requires the caller's revision and geometric validation contract."],
      "conventions":{"torus":"Around +Z; elliptical section radial a and axial b; v=0 is the outer equator. v_start_deg=-60,v_end_deg=60 samples only the outer tread band.","racetrack":"Local XY; equal-radius pulley centers at +/- center_distance_mm/2 on X; width along Z; u is normalized arc length from top-right counterclockwise.","sphere":"Polar angle measured from +Z. Default fibonacci and spherical_weave use count and equal-area cos(polar) midpoints. Explicit grid uses around/rows and angular polar midpoints, not equal-area spacing. All exclude poles and duplicate seam columns.","winding":"turns gives longitudinal revolutions across the selected transverse interval. Chevron uses absolute distance from interval center; braided alternates winding sign by row and adds half-column stagger. Motif x remains longitudinal; orientation_deg rotates in its tangent plane.","spherical_weave":"Fibonacci strata with sinusoidal longitude shear controlled by integer turns and weave_deg; equal-area latitude strata retained. No claim of optimal spacing or literature novelty."},
      "limits":["Analytical placements only; no materialized solids, collision/overlap checks, contact solver or manufacturing qualification.","Cylinder and racetrack sample lateral surfaces only, not end caps.","Placement frame is rigid and right-handed; scaling/shear/reflection are rejected.","Count bounds limit recipes; preview is independently capped at 256 frames. No measured rendering speed claim."],
      "references":["https://www.lgdv.tf.fau.de/publications/spherical-fibonacci-mapping/","https://drna.padovauniversitypress.it/system/files/papers/HardinMichaelsSaff_2016_CPC.pdf"],
      "examples":[
        {"surface":{"kind":"torus","major_radius_mm":50,"radial_radius_mm":12,"axial_radius_mm":9,"v_start_deg":-60,"v_end_deg":60},"distribution":"chevron","around":80,"rows":9,"turns":1,"motif":"traction_bar"},
        {"surface":{"kind":"sphere","radius_mm":25},"distribution":"fibonacci","count":512,"motif":"pebble"},
        {"surface":{"kind":"racetrack","radius_mm":20,"center_distance_mm":100,"width_mm":30},"distribution":"stagger","around":100,"rows":3,"motif":"lug"}
      ]
    })json");
    out.set("schema", rubberPatternSchema());
    return out;
}
} // namespace ark
