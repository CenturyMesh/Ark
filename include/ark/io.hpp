// ARK-Point :: ark/io.hpp — export & offscreen rendering API.
#pragma once
#include "ark/mesh.hpp"
#include <vector>

namespace ark {

bool exportSTL(const char* path, const Mesh& m);
bool exportOBJ(const char* path, const Mesh& m);
bool exportFBX(const char* path, const Mesh& m); // ASCII FBX 7.4, millimetres, triangle mesh
// STL or OBJ, chosen from the file extension. Throws with the reason on failure.
// Vertices are stored as f32. Faces with more than three corners are fanned.
Mesh importMeshPath(const std::string& path);

struct Image { int w=0,h=0; std::vector<u8> px; /*RGB8*/ };
bool writePNG(const char* path, const Image& img);

struct SnapCam {
    Vec3 target{};
    bool useTarget=false;
    f64 dist=40, yaw=45*K_PI/180, pitch=30*K_PI/180;
    f64 fovDeg=35;
};
Image renderSnapshot(const Mesh& m, int W, int H, const SnapCam& cam);

} // namespace ark
