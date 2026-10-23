#pragma once
#include "ark/agent.hpp"
#include <array>

namespace ark {
// Derived simulation only. CAD coordinates are mm; solver coordinates are SI.
// No pose in this module is an authored placement or manufacturing evidence.
struct InertialShape {
    std::vector<Vec3> vertices, normals, edges; // local metres, unit directions
    std::vector<std::array<unsigned,2>> segments; // actual non-coplanar boundary edges
    std::string digest;
    size_t sourceVertices=0;
};
struct InertialBody {
    u64 feature=0,node=0; int copy=0; std::string name;
    size_t shape=0; double mass=1;
    std::array<double,9> inertia{1,0,0,0,1,0,0,0,1};
    Vec3 localCenterMm, position, velocity, omega, force, torque;
    std::array<double,4> orientation{0,0,0,1};
    bool fixed=false;
};
struct InertialJoint {
    size_t a=0,b=0; Vec3 anchorA,anchorB,axisA,axisB;
    bool hinge=false;
};
struct InertialOptions {
    double duration=1, step=1.0/240, restitution=.1, friction=.5;
    Vec3 gravity{0,0,-9.80665};
    bool ground=false,contacts=true; double groundZ=0;
    unsigned iterations=16, outputFps=30;
    std::string backend="gpu";
};
struct InertialResult {
    std::vector<std::vector<InertialBody>> frames;
    std::vector<double> times;
    arkjson::Value diagnostics;
};
InertialShape prepareInertialShape(const Mesh&,Vec3 localCenterMm);
InertialResult simulateInertial(const std::vector<InertialShape>&,
    const std::vector<InertialBody>&,const std::vector<InertialJoint>&,const InertialOptions&);
arkjson::Value inertialCapabilities();
arkjson::Value inertialSimulationSchema();
arkjson::Value inspectInertial(AgentSession&,const arkjson::Value&);
Mat4 inertialDisplayPose(const InertialBody&);
}
