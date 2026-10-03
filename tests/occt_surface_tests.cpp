#include "ark/occt_modeling.hpp"
#include "../src/occt/common.hpp"

#include <BRep_Tool.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

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
        std::cout << "invalid requests\n";
        testRejectsInvalidInput();
        std::cout << "Native OCCT surfacing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native OCCT surfacing tests failed: " << error.what() << '\n';
        return 1;
    }
}
