#include "ark/occt_modeling.hpp"
#include "../src/occt/common.hpp"

#include <BRep_Tool.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <Approx_MCurvesToBSpCurve.hxx>
#include <AppParCurves_MultiPoint.hxx>
#include <NCollection_Sequence.hxx>
#include <Standard_ConstructionError.hxx>
#include <StdFail_NotDone.hxx>

#include <AppParCurves_MultiBSpCurve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepApprox_Approx.hxx>
#include <BRepApprox_ApproxLine.hxx>
#include <BRep_Builder.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Plane.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <GeomAdaptor_TransformedSurface.hxx>
#include <GeomInt_WLApprox.hxx>
#include <IntPatch_WLine.hxx>
#include <IntSurf_LineOn2S.hxx>
#include <IntSurf_PntOn2S.hxx>
#include <IntSurf_Quadric.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec2d.hxx>
#include <algorithm>
#include <limits>
#include <vector>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using arkjson::Value;

Value parse(const std::string& text) {
    arkjson::Parser parser(text);
    Value value;
    if (!parser.parse(value)) throw std::runtime_error("invalid test JSON");
    return value;
}

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

void rejected(const std::string& request) {
    const Value value = parse(request);
    try { (void)ark::occt::modelSurfaceNative(value); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("invalid surfacing request was accepted");
}

Value nurbsPlane() {
    return ark::occt::modelSurfaceNative(parse(R"({
      "op":"surface_nurbs", "u_degree":3, "v_degree":3,
      "poles":[
        [[0,0,0],[0,1,0],[0,2,0],[0,3,0]],
        [[1,0,0],[1,1,0],[1,2,0],[1,3,0]],
        [[2,0,0],[2,1,0],[2,2,0],[2,3,0]],
        [[3,0,0],[3,1,0],[3,2,0],[3,3,0]]
      ]
    })"));
}

void testRuledConeAndStep() {
    const Value result = ark::occt::modelSurfaceNative(parse(R"({
      "op":"loft", "solid":true, "ruled":true,
      "profiles":[
        {"entities":[{"kind":"circle","center":[0,0],"radius":5}]},
        {"frame":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,10,1],
         "entities":[{"kind":"circle","center":[0,0],"radius":3}]}
      ]
    })"));
    const auto shape = ark::occt::readBrep(result.find("brep_base64")->str);
    check(ark::occt::shapes(shape, TopAbs_SOLID).size() == 1, "loft has no solid");
    const double expected = std::acos(-1.0) * 10.0 * (25.0 + 15.0 + 9.0) / 3.0;
    check(close(ark::occt::properties(shape), expected, 1.0e-4), "cone frustum volume");
    const auto path = std::filesystem::temp_directory_path() / "ark-occt-surface-roundtrip.step";
    ark::occt::writeStep(shape, path);
    const auto recovered = ark::occt::readStep(path);
    std::filesystem::remove(path);
    check(close(ark::occt::properties(recovered), expected, 1.0e-4), "STEP volume roundtrip");
}

void testOpenLoftAndFill() {
    const Value open = ark::occt::modelSurfaceNative(parse(R"({
      "op":"loft", "ruled":true, "profiles":[
        {"entities":[{"kind":"line","points":[[0,0],[10,0]]}]},
        {"frame":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,5,1],
         "entities":[{"kind":"line","points":[[0,0],[10,0]]}]}
      ]
    })"));
    const auto openShape = ark::occt::readBrep(open.find("brep_base64")->str);
    check(ark::occt::shapes(openShape, TopAbs_SOLID).empty(), "open loft gained solid authority");
    check(!ark::occt::shapes(openShape, TopAbs_FACE).empty(), "open loft has no surface");

    const Value filled = ark::occt::modelSurfaceNative(parse(R"({
      "op":"surface_fill", "profile":{"entities":[
        {"kind":"line","points":[[0,0,0],[10,0,0]]},
        {"kind":"line","points":[[10,0,0],[10,10,1]]},
        {"kind":"line","points":[[10,10,1],[0,10,0]]},
        {"kind":"line","points":[[0,10,0],[0,0,0]]}
      ]}
    })"));
    const auto filledShape = ark::occt::readBrep(filled.find("brep_base64")->str);
    check(ark::occt::shapes(filledShape, TopAbs_FACE).size() == 1, "nonplanar fill face");
    check(ark::occt::shapes(filledShape, TopAbs_SOLID).empty(), "fill gained solid authority");
}

void testNurbsAndThicken() {
    const Value plane = nurbsPlane();
    const auto shape = ark::occt::readBrep(plane.find("brep_base64")->str);
    check(ark::occt::shapes(shape, TopAbs_FACE).size() == 1, "NURBS face count");
    check(close(ark::occt::properties(shape, 'a'), 9.0, 1.0e-5), "NURBS plane area");
    const auto face = TopoDS::Face(ark::occt::shapes(shape, TopAbs_FACE).front());
    const auto surface = BRep_Tool::Surface(face);
    check(surface->Value(0, 0).Distance(gp_Pnt(0, 0, 0)) < 1.0e-8, "NURBS first pole");
    check(surface->Value(1, 1).Distance(gp_Pnt(3, 3, 0)) < 1.0e-8, "NURBS last pole");

    Value request = Value::makeObj();
    request.set("op", Value::makeStr("thicken"));
    request.set("input_brep", *plane.find("brep_base64"));
    request.set("thickness", Value::makeNum(2));
    const Value thick = ark::occt::modelSurfaceNative(request);
    const auto solid = ark::occt::readBrep(thick.find("brep_base64")->str);
    check(ark::occt::shapes(solid, TopAbs_SOLID).size() == 1, "thickening solid count");
    check(close(ark::occt::properties(solid), 18.0, 1.0e-4), "thickened plane volume");
}

std::string square(double x0, double x1, double y0, double y1) {
    return std::string(R"({"op":"surface_fill","profile":{"entities":[)") +
        R"({"kind":"line","points":[[)" + std::to_string(x0) + "," + std::to_string(y0) + "],[" + std::to_string(x1) + "," + std::to_string(y0) + "]]}," +
        R"({"kind":"line","points":[[)" + std::to_string(x1) + "," + std::to_string(y0) + "],[" + std::to_string(x1) + "," + std::to_string(y1) + "]]}," +
        R"({"kind":"line","points":[[)" + std::to_string(x1) + "," + std::to_string(y1) + "],[" + std::to_string(x0) + "," + std::to_string(y1) + "]]}," +
        R"({"kind":"line","points":[[)" + std::to_string(x0) + "," + std::to_string(y1) + "],[" + std::to_string(x0) + "," + std::to_string(y0) + "]]}]}}";
}

void testSweepExtrudeRevolveOffsetSew() {
    const double pi = std::acos(-1.0);
    const Value extruded = ark::occt::modelSurfaceNative(parse(R"({
      "op":"extrude_surface","solid":false,"distance":5,
      "profile":{"entities":[{"kind":"line","points":[[0,0],[10,0]]}]}
    })"));
    const auto extrudedShape = ark::occt::readBrep(extruded.find("brep_base64")->str);
    check(ark::occt::shapes(extrudedShape, TopAbs_SOLID).empty(), "open extrusion became a solid");
    check(close(ark::occt::properties(extrudedShape, 'a'), 50.0, 1.0e-4), "extruded line area");

    const Value revolved = ark::occt::modelSurfaceNative(parse(R"({
      "op":"revolve_surface","solid":false,"angle":360,"axis":"Y",
      "profile":{"entities":[{"kind":"line","points":[[5,0],[5,10]]}]}
    })"));
    const auto revolvedShape = ark::occt::readBrep(revolved.find("brep_base64")->str);
    check(ark::occt::shapes(revolvedShape, TopAbs_SOLID).empty(), "open revolution became a solid");
    check(close(ark::occt::properties(revolvedShape, 'a'), 100.0 * pi, 1.0e-3), "revolved line area");

    const Value spun = ark::occt::modelSurfaceNative(parse(R"({
      "op":"revolve_surface","solid":true,"angle":360,"axis":"Y",
      "profile":{"entities":[
        {"kind":"line","points":[[5,0],[8,0]]},
        {"kind":"line","points":[[8,0],[8,10]]},
        {"kind":"line","points":[[8,10],[5,10]]},
        {"kind":"line","points":[[5,10],[5,0]]}
      ]}
    })"));
    const auto spunShape = ark::occt::readBrep(spun.find("brep_base64")->str);
    check(ark::occt::shapes(spunShape, TopAbs_SOLID).size() == 1, "solid revolution count");
    check(close(ark::occt::properties(spunShape), 390.0 * pi, 1.0e-2), "solid revolution volume");

    const Value swept = ark::occt::modelSurfaceNative(parse(R"({
      "op":"sweep","solid":true,"contact":false,"correction":true,
      "section":{"entities":[{"kind":"circle","center":[0,0],"radius":2}]},
      "path":{"frame":[1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1],
              "entities":[{"kind":"line","points":[[0,0],[0,20]]}]}
    })"));
    const auto sweptShape = ark::occt::readBrep(swept.find("brep_base64")->str);
    check(ark::occt::shapes(sweptShape, TopAbs_SOLID).size() == 1, "sweep solid count");
    check(close(ark::occt::properties(sweptShape), 80.0 * pi, 1.0e-2), "swept cylinder volume");

    const Value ribbon = ark::occt::modelSurfaceNative(parse(R"({
      "op":"sweep","solid":false,"contact":false,"correction":true,
      "section":{"entities":[{"kind":"line","points":[[-2,0],[2,0]]}]},
      "path":{"frame":[1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1],
              "entities":[{"kind":"line","points":[[0,0],[0,20]]}]}
    })"));
    const auto ribbonShape = ark::occt::readBrep(ribbon.find("brep_base64")->str);
    check(ark::occt::shapes(ribbonShape, TopAbs_SOLID).empty(), "open sweep became a solid");
    check(close(ark::occt::properties(ribbonShape, 'a'), 80.0, 1.0e-3), "open sweep area");

    const Value plane = nurbsPlane();
    Value offset = Value::makeObj();
    offset.set("op", Value::makeStr("offset_surface"));
    offset.set("input_brep", *plane.find("brep_base64"));
    offset.set("distance", Value::makeNum(2));
    const Value moved = ark::occt::modelSurfaceNative(offset);
    const auto movedShape = ark::occt::readBrep(moved.find("brep_base64")->str);
    const auto box = ark::occt::bounds(movedShape);
    check(close(ark::occt::properties(movedShape, 'a'), 9.0, 1.0e-4), "offset plane area");
    check(close(box[2], box[5], 1.0e-6) && close(std::abs(box[2]), 2.0, 1.0e-5), "offset plane translation");
    check(moved.find("report")->find("offset_intersection_checked")->b == false, "offset reports no intersection check");

    const Value left = ark::occt::modelSurfaceNative(parse(square(0, 1, 0, 1)));
    const Value right = ark::occt::modelSurfaceNative(parse(square(1, 2, 0, 1)));
    Value sew = Value::makeObj(), inputs = Value::makeArr();
    inputs.arr.push_back(*left.find("brep_base64"));
    inputs.arr.push_back(*right.find("brep_base64"));
    sew.set("op", Value::makeStr("sew"));
    sew.set("solid", Value::makeBool(false));
    sew.set("inputs", std::move(inputs));
    const Value sewn = ark::occt::modelSurfaceNative(sew);
    const auto sewnShape = ark::occt::readBrep(sewn.find("brep_base64")->str);
    check(ark::occt::shapes(sewnShape, TopAbs_SOLID).empty(), "open sew became a solid");
    check(close(ark::occt::properties(sewnShape, 'a'), 2.0, 1.0e-4), "sewn square area");
    const double freeEdges = sewn.find("report")->find("free_edges")->num;
    check(freeEdges > 0 && freeEdges < 8, "adjacent squares share an edge after sewing");

    Value closed = sew;
    closed.set("solid", Value::makeBool(true));
    closed.set("inputs", *sew.find("inputs"));
    try { (void)ark::occt::modelSurfaceNative(closed); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("solid sew of an open shell was accepted");
}

bool boundEdge(const Value& result, const char* name) {
    const Value* topology = result.find("topology");
    if (!topology) return false;
    for (const auto& row : topology->find("references")->arr)
        if (row.find("name")->str == name)
            return row.find("kind")->str == "edge" && row.find("status")->str == "bound";
    return false;
}

void testLoftTipSmoothAndEdges() {
    const double pi = std::acos(-1.0);
    const Value pyramid = ark::occt::modelSurfaceNative(parse(R"({
      "op":"loft","solid":true,"ruled":true,
      "profiles":[
        {"entities":[
          {"kind":"line","name":"south","points":[[-5,-5],[5,-5]]},
          {"kind":"line","name":"east","points":[[5,-5],[5,5]]},
          {"kind":"line","name":"north","points":[[5,5],[-5,5]]},
          {"kind":"line","name":"west","points":[[-5,5],[-5,-5]]}
        ]},
        {"frame":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,10,1],"point":[0,0]}
      ]
    })"));
    const auto pyramidShape = ark::occt::readBrep(pyramid.find("brep_base64")->str);
    check(ark::occt::shapes(pyramidShape, TopAbs_SOLID).size() == 1, "point tip loft solid");
    check(close(ark::occt::properties(pyramidShape), 1000.0 / 3.0, 1.0e-4), "pyramid tip volume");
    check(boundEdge(pyramid, "edge.start.south"), "loft start edge name");
    check(!boundEdge(pyramid, "edge.end.south"), "point tip has no end edge");

    const Value prism = ark::occt::modelSurfaceNative(parse(R"({
      "op":"loft","solid":true,"ruled":false,"smooth":true,
      "profiles":[
        {"entities":[
          {"kind":"line","points":[[0,0],[10,0]]},
          {"kind":"line","points":[[10,0],[10,10]]},
          {"kind":"line","points":[[10,10],[0,10]]},
          {"kind":"line","points":[[0,10],[0,0]]}
        ]},
        {"frame":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,10,1],"entities":[
          {"kind":"line","points":[[0,0],[10,0]]},
          {"kind":"line","points":[[10,0],[10,10]]},
          {"kind":"line","points":[[10,10],[0,10]]},
          {"kind":"line","points":[[0,10],[0,0]]}
        ]}
      ]
    })"));
    check(close(ark::occt::properties(ark::occt::readBrep(prism.find("brep_base64")->str)), 1000.0, 1.0e-4), "smooth prism volume");

    const Value matched = ark::occt::modelSurfaceNative(parse(R"({
      "op":"loft","solid":true,"ruled":true,"compatible":true,
      "profiles":[
        {"entities":[{"kind":"circle","center":[0,0],"radius":5}]},
        {"frame":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,10,1],
         "entities":[{"kind":"circle","center":[0,0],"radius":3}]}
      ]
    })"));
    const double frustum = pi * 10.0 * (25.0 + 15.0 + 9.0) / 3.0;
    check(close(ark::occt::properties(ark::occt::readBrep(matched.find("brep_base64")->str)), frustum, 1.0e-3), "compatible circular frustum");
    (void)pi;
}

void testRejectsInvalidInput() {
    rejected(R"({"op":"loft","solid":true,"profiles":[
      {"entities":[{"kind":"line","points":[[0,0],[1,0]]}]},
      {"entities":[{"kind":"line","points":[[0,0,1],[1,0,1]]}]}
    ]})");
    rejected(R"({"op":"loft","profiles":[{"entities":[{"kind":"circle","center":[0,0],"radius":1}]}]})");
    rejected(R"({"op":"surface_fill","profile":{"entities":[
      {"kind":"line","points":[[0,0],[1,0]]},
      {"kind":"line","points":[[1,0],[1,1]]}
    ]}})");
    rejected(R"({"op":"surface_nurbs","u_degree":3,"v_degree":3,
      "poles":[[[0,0,0],[1,0,0]],[[0,1,0],[1,1,0]]]})");
    rejected(R"({"op":"surface_nurbs","u_degree":1,"v_degree":1,
      "poles":[[[0,0,0],[1,0,0]],[[0,1,0],[1,1,0]]],
      "weights":[[1,0],[1,1]]})");
    rejected(R"({"op":"thicken","input_brep":"not-base64","thickness":0})");
    rejected(R"({"op":"extrude_surface","distance":0,"profile":{"entities":[{"kind":"line","points":[[0,0],[1,0]]}]}})");
    rejected(R"({"op":"revolve_surface","angle":0,"axis":"Y","profile":{"entities":[{"kind":"line","points":[[1,0],[1,1]]}]}})");
    rejected(R"({"op":"sweep","solid":true,"section":{"entities":[{"kind":"line","points":[[0,0],[1,0]]}]},
      "path":{"entities":[{"kind":"line","points":[[0,0],[0,1]]}]}})");
    const Value plane = nurbsPlane();
    Value thick = Value::makeObj();
    thick.set("op", Value::makeStr("thicken"));
    thick.set("input_brep", *plane.find("brep_base64"));
    thick.set("thickness", Value::makeNum(1));
    const Value solid = ark::occt::modelSurfaceNative(thick);
    Value offset = Value::makeObj();
    offset.set("op", Value::makeStr("offset_surface"));
    offset.set("input_brep", *solid.find("brep_base64"));
    offset.set("distance", Value::makeNum(1));
    try { (void)ark::occt::modelSurfaceNative(offset); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("offset of a solid was accepted");
}

// Public synthetic data: cubic Bezier ellipses with an explicitly nonlinear taper.
// Exercise the C2 factor solve, knot refinement and fresh solver ownership between
// interleaved requests. No saved user drawing or sampled mesh supplies geometry.
Value smoothEllipseLoft(double scale, int continuity = 2, int sections = 4,
                       bool linearTaper = false, double offset = 0.) {
    Value request = Value::makeObj(), profiles = Value::makeArr();
    request.set("op", Value::makeStr("loft"));
    request.set("solid", Value::makeBool(true));
    request.set("smooth", Value::makeBool(true));
    request.set("compatible", Value::makeBool(true));
    request.set("continuity", Value::makeNum(continuity));
    const double pi = std::acos(-1.), handle = 4. / 3. * std::tan(pi / 16.);
    for (int section = 0; section < sections; ++section) {
        const double t = double(section) / (sections - 1);
        const double rx = scale * (linearTaper ? 10. - 6. * t : 10. - 2. * t - 4. * t * t);
        const double ry = rx * (linearTaper ? .6 : .6 + .15 * t);
        Value profile = Value::makeObj(), entities = Value::makeArr(), frame = Value::makeArr();
        for (double v : std::array<double,16>{1,0,0,0,0,1,0,0,0,0,1,0,offset,-2*offset,3*offset+scale*4*t,1})
            frame.arr.push_back(Value::makeNum(v));
        for (int edge = 0; edge < 8; ++edge) {
            const double a = edge * pi / 4., b = (edge + 1) * pi / 4.;
            const std::array<double,2> start{rx*std::cos(a), ry*std::sin(a)};
            const std::array<double,2> end = edge == 7 ? std::array<double,2>{rx,0.}
                                                     : std::array<double,2>{rx*std::cos(b),ry*std::sin(b)};
            Value entity = Value::makeObj(), points = Value::makeArr();
            for (const auto& point : std::array<std::array<double,2>,4>{start,
                    std::array<double,2>{start[0]-handle*rx*std::sin(a),start[1]+handle*ry*std::cos(a)},
                    std::array<double,2>{end[0]+handle*rx*std::sin(b),end[1]-handle*ry*std::cos(b)},end}) {
                Value pair = Value::makeArr();
                pair.arr = {Value::makeNum(point[0]), Value::makeNum(point[1])};
                points.arr.push_back(pair);
            }
            entity.set("kind", Value::makeStr("bezier")); entity.set("points", points);
            entities.arr.push_back(entity);
        }
        profile.set("frame", frame); profile.set("entities", entities); profiles.arr.push_back(profile);
    }
    request.set("profiles", profiles); return request;
}

double bezierProfileArea(const Value& profile) {
    // Green's theorem: cubic position times quadratic derivative has degree
    // at most five, so this three-point Gauss rule integrates it exactly.
    const double delta = std::sqrt(3. / 5.) / 2.;
    const std::array<double,3> nodes{.5-delta,.5,.5+delta}, weights{5./18.,4./9.,5./18.};
    double twiceArea = 0.;
    for (const auto& edge : profile.find("entities")->arr) {
        const auto& points = edge.find("points")->arr;
        for (int sample = 0; sample < 3; ++sample) {
            const double t = nodes[sample], u = 1.-t;
            const std::array<double,4> basis{u*u*u,3*u*u*t,3*u*t*t,t*t*t};
            const std::array<double,3> derivative{3*u*u,6*u*t,3*t*t};
            double x=0., y=0., dx=0., dy=0.;
            for (int i=0;i<4;++i) { x+=basis[i]*points[i].arr[0].num; y+=basis[i]*points[i].arr[1].num; }
            for (int i=0;i<3;++i) { dx+=derivative[i]*(points[i+1].arr[0].num-points[i].arr[0].num); dy+=derivative[i]*(points[i+1].arr[1].num-points[i].arr[1].num); }
            twiceArea += weights[sample]*(x*dy-y*dx);
        }
    }
    return std::abs(twiceArea)/2.;
}

void testSmoothLoftAnalyticFrustum() {
    for (const auto& setup : std::array<std::array<double,2>,3>{{{1.,0.},{30.,0.},{30.,100000.}}}) {
        const double scale=setup[0];
        const auto request=smoothEllipseLoft(scale,2,4,true,setup[1]);
        const double area=bezierProfileArea(request.find("profiles")->arr.front()), ratio=.4;
        const double expected=area*(4.*scale)*(1.+ratio+ratio*ratio)/3.;
        const auto result=ark::occt::modelSurfaceNative(request);
        const auto shape=ark::occt::readBrep(result.find("brep_base64")->str);
        const double actual=ark::occt::properties(shape);
        std::cout << "analytic smooth frustum scale=" << scale << " offset=" << setup[1]
                  << " relative volume error=" << std::abs(actual-expected)/expected << '\n';
        check(BRepCheck_Analyzer(shape,true,false,true).IsValid(),"analytic smooth frustum exact validity");
        check(std::abs(actual-expected)<=expected*1e-8,"C2 smooth loft matches independently integrated analytic frustum");
    }
}

void testMulticurveComponentPreservation() {
    // Component one determines the old converter's derivative ratio. A second
    // component can have another ratio or another direction at the same join.
    // Every original Bezier must survive a shared knot reduction unchanged.
    for (bool mixed : {false,true}) for (int behavior : {0,1,2}) for (double origin : {0.,.123456789}) {
        NCollection_Sequence<AppParCurves_MultiCurve> pieces;
        for (int segment=0;segment<2;++segment) {
            const int degree=segment==0?2:3;
            AppParCurves_MultiCurve piece(degree+1);
            for (int pole=0;pole<=degree;++pole) {
                const double t=double(pole)/degree;
                const double first=segment==0?t:1.+2.*t;
                const double second=segment==0?2.*t:2.+(behavior==1?6.:4.)*t;
                const double turn=segment==1 && behavior==2?t:0.;
                AppParCurves_MultiPoint point(mixed?1:0,mixed?1:2);
                if (mixed) point.SetPoint(1,gp_Pnt(first+origin,origin,origin));
                else point.SetPoint2d(1,gp_Pnt2d(first+origin,origin));
                point.SetPoint2d(2,gp_Pnt2d(second+origin,turn+origin));
                piece.SetValue(pole+1,point);
            }
            pieces.Append(piece);
        }
        Approx_MCurvesToBSpCurve converter;
        converter.Perform(pieces);
        const auto& spline=converter.Value();
        check(spline.Knots().Length()==3,"two Bezier segments retain their join parameter");
        check(spline.Multiplicities()(2)==(behavior==0?spline.Degree()-1:spline.Degree()),
              "shared C1 reduction requires a common ratio in every component");
        for (int segment=1;segment<=pieces.Length();++segment) {
            const double first=spline.Knots()(segment), last=spline.Knots()(segment+1);
            for (int component=1;component<=2;++component) for (int sample=0;sample<=100;++sample) {
                const double t=sample/100., parameter=first+(last-first)*t;
                if (mixed && component==1) {
                    gp_Pnt before,after; pieces.Value(segment).Value(component,t,before); spline.Value(component,parameter,after);
                    check(before.Distance(after)<=1e-11,"concat preserves every 3D Bezier component");
                } else {
                    gp_Pnt2d before,after; pieces.Value(segment).Value(component,t,before); spline.Value(component,parameter,after);
                    check(before.Distance(after)<=1e-11,"concat preserves every UV Bezier component");
                }
            }
        }
        converter.Reset(); converter.Append(pieces.Value(1)); converter.Perform();
        check(converter.Value().Knots().Length()==2,"concat reset discards previous join state");
        // A real mismatch of one input ULP must reject, including after a
        // previous successful conversion. This is not an elevation artifact.
        auto broken=pieces;
        auto endpoint=broken.Value(2).Value(1);
        auto uv=endpoint.Point2d(2);
        uv.SetX(std::nextafter(uv.X(),INFINITY));
        endpoint.SetPoint2d(2,uv); broken.ChangeValue(2).SetValue(1,endpoint);
        bool rejectedMismatch=false;
        try { converter.Perform(broken); }
        catch (const Standard_ConstructionError&) { rejectedMismatch=true; }
        check(rejectedMismatch,"concat explicitly rejects a real one-ULP endpoint mismatch");
        bool rejectedStale=false;
        try { (void)converter.Value(); }
        catch (const StdFail_NotDone&) { rejectedStale=true; }
        check(rejectedStale,"failed concat cannot expose its previous successful spline");
    }
}

void testSmoothLoftSolverLifecycle() {
    auto evaluate = [](const Value& request) {
        const auto authored = request.dump();
        const auto began = std::chrono::steady_clock::now();
        const auto result = ark::occt::modelSurfaceNative(request);
        const auto shape = ark::occt::readBrep(result.find("brep_base64")->str);
        check(BRepCheck_Analyzer(shape,true,false,true).IsValid(), "smooth loft exact BRep validity");
        check(ark::occt::shapes(shape,TopAbs_SOLID).size() == 1, "smooth loft has one solid");
        check(request.dump() == authored, "smooth loft preserves authored inputs");
        for (const auto& section : request.find("profiles")->arr) {
            for (const auto& edge : section.find("entities")->arr) {
                const auto& poles = edge.find("points")->arr;
                for (int i = 0; i <= 4; ++i) {
                    const double t = i / 4., u = 1. - t;
                    const std::array<double,4> basis{u*u*u,3*u*u*t,3*u*t*t,t*t*t};
                    double x = 0., y = 0.;
                    for (int j = 0; j < 4; ++j) { x += basis[j]*poles[j].arr[0].num; y += basis[j]*poles[j].arr[1].num; }
                    const gp_Pnt p(x,y,section.find("frame")->arr[14].num);
                    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),shape);
                    check(distance.IsDone() && distance.Value() <= 1e-6, "smooth loft preserves its authored sections");
                }
            }
        }
        std::cout << "smooth loft ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count() << '\n';
        return ark::occt::properties(shape);
    };
    const auto smallLoft = smoothEllipseLoft(1.), largeLoft = smoothEllipseLoft(30.);
    const double first = evaluate(smallLoft), scaled = evaluate(largeLoft);
    (void)evaluate(smoothEllipseLoft(1.,1));
    (void)evaluate(smoothEllipseLoft(1.,0));
    (void)evaluate(smoothEllipseLoft(1.,2,2));
    check(close(evaluate(smallLoft),first,1e-8), "interleaved lofts do not retain stale matrix state");
    check(close(evaluate(largeLoft),scaled,1e-5), "refined loft owns its factor lifecycle");
}
#include "occt_surface_continuity_crossings.inl"

} // namespace

int main() {
    try {
        std::cout << "ruled loft and STEP\n";
        testRuledConeAndStep();
        std::cout << "open loft and filling\n";
        testOpenLoftAndFill();
        std::cout << "NURBS and thickening\n";
        testNurbsAndThicken();
        std::cout << "sweep, extrude, revolve, offset, sew\n";
        testSweepExtrudeRevolveOffsetSew();
        std::cout << "loft tip, smoothing and edge names\n";
        testLoftTipSmoothAndEdges();
        std::cout << "smooth loft C2 solver and lifecycle\n";
        testSmoothLoftSolverLifecycle();
        std::cout << "smooth loft analytic frustum\n";
        testSmoothLoftAnalyticFrustum();
        std::cout << "multicurve component preservation\n";
        testMulticurveComponentPreservation();
        std::cout << "mandatory surface continuity crossings\n";
        testMandatorySurfaceContinuityCrossings();
        std::cout << "invalid requests\n";
        testRejectsInvalidInput();
        std::cout << "Native OCCT surfacing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native OCCT surfacing tests failed: " << error.what() << '\n';
        return 1;
    }
}
