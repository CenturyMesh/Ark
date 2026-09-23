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
        std::cout << "invalid requests\n";
        testRejectsInvalidInput();
        std::cout << "Native OCCT surfacing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native OCCT surfacing tests failed: " << error.what() << '\n';
        return 1;
    }
}
