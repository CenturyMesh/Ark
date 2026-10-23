#pragma once
#include "ark/core.hpp"
#include "ark/json.hpp"

namespace ark {
// Derived, reduced spring-network dynamics. All quantities are SI (m, kg, s, N).
// A distance constraint has energy (length-restLength)^2/(2*compliance).
// This is not a calibrated constitutive rubber model or manufacturing evidence.
struct RubberNode {
    Vec3 position, velocity, force;
    double mass=1;
    bool fixed=false;
};
struct RubberLink {
    uint32_t a=0,b=0;
    double restLength=1,compliance=0; // m, m/N; zero is an inextensible link
};
struct RubberPulley {
    Vec3 center,axis{0,0,1};
    double radius=1,angularVelocity=0; // infinite cylinder, m and rad/s
};
struct RubberDynamicsOptions {
    double duration=1,step=1.0/240;
    unsigned iterations=8;
    Vec3 gravity{0,0,-9.80665};
    bool ground=true;
    double groundZ=0,friction=.7,damping=.02,contactRadius=0;
    // damping is velocity decay in s^-1; friction is the Coulomb coefficient.
    unsigned outputFps=30;
    std::string backend="gpu";
    std::vector<RubberPulley> pulleys;
};
struct RubberDynamicsResult {
    std::vector<std::vector<Vec3>> frames;
    std::vector<double> times;
    std::vector<Vec3> finalVelocities;
    arkjson::Value diagnostics;
};
RubberDynamicsResult simulateRubber(const std::vector<RubberNode>&,
    const std::vector<RubberLink>&,const RubberDynamicsOptions&);
arkjson::Value rubberDynamicsCapabilities();
}
