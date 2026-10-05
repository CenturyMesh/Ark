#include "ark/mesh_model.hpp"
#include "ark/mesh_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ark {
namespace {
using V = arkjson::Value;

Vec3 at(const Mesh& mesh, u32 index) {
    return {mesh.pos[index * 3], mesh.pos[index * 3 + 1], mesh.pos[index * 3 + 2]};
}

V number(double value) { return V::makeNum(value); }

V modelReport(const char* operation, const MeshReport& before, const MeshReport& after, bool sampled) {
    V report = V::makeObj();
    report.set("mesh_operation", V::makeStr(operation));
    report.set("mesh_authoritative", V::makeBool(true));
    report.set("exact_geometry", V::makeBool(false));
    report.set("self_intersections_checked", V::makeBool(false));
    report.set("sampled_from_brep", V::makeBool(sampled));
    report.set("input_vertices", number((double)before.vertices));
    report.set("input_triangles", number((double)before.triangles));
    report.set("output_vertices", number((double)after.vertices));
    report.set("output_triangles", number((double)after.triangles));
    return report;
}

void requireFiniteManifold(const MeshReport& report, const char* operation) {
    if (report.invalidIndices || report.nonFiniteVertices)
        throw std::runtime_error(std::string(operation) + " input has invalid or non-finite vertices");
    if (report.degenerateTriangles)
        throw std::runtime_error(std::string(operation) + " input has degenerate triangles; weld it first");
    if (report.nonManifoldEdges || report.inconsistentEdges)
        throw std::runtime_error(std::string(operation) + " requires a manifold triangle mesh");
}

constexpr size_t kOperationTriangles = 12000;

const Body* findBody(const std::vector<Body>& bodies, u64 id) {
    for (const Body& body : bodies) if (body.id == id) return &body;
    return nullptr;
}

Mesh inputMesh(const Feature& feature, const ExprEnv& env, const std::vector<Body>& preceding,
               const TessOptions& tessellation, bool& sampled) {
    if (feature.inputs.size() != 1) throw std::runtime_error("Mesh operation requires one input feature");
    const Body* body = findBody(preceding, feature.inputs.front());
    if (!body) throw std::runtime_error("Mesh operation input is missing");
    sampled = false;
    if (body->nativeData.find("brep_base64")) {
        if (feature.num(env, "sample", 0) != 1.0)
            throw std::runtime_error("Exact B-rep cannot enter a mesh operation without p.sample=1. The result is a sampled mesh, not an exact surface");
        sampled = true;
    }
    Mesh mesh = body->baked && body->baked->triCount() ? *body->baked : body->tessellate(tessellation);
    if (!mesh.triCount()) throw std::runtime_error("Mesh operation input has no triangles");
    if (mesh.triCount() > kOperationTriangles)
        throw std::runtime_error("Mesh operation accepts at most 12000 triangles");
    return mesh;
}

struct Quadric {
    double q[10]{};
    void addPlane(const Vec3& normal, const Vec3& point) {
        const double d = -normal.dot(point);
        const double r[4] = {normal.x, normal.y, normal.z, d};
        int k = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = i; j < 4; ++j) q[k++] += r[i] * r[j];
    }
    Quadric& operator+=(const Quadric& other) {
        for (int i = 0; i < 10; ++i) q[i] += other.q[i];
        return *this;
    }
    double error(const Vec3& point) const {
        const double v[4] = {point.x, point.y, point.z, 1};
        double sum = 0;
        int k = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = i; j < 4; ++j) {
                const double term = q[k++] * v[i] * v[j];
                sum += i == j ? term : 2 * term;
            }
        return sum;
    }
};

Mesh decimate(Mesh mesh, double ratio) {
    if (!(ratio >= 0.05 && ratio <= 1.0)) throw std::runtime_error("decimate ratio must be in [0.05, 1]");
    const MeshReport before = analyzeMesh(mesh);
    requireFiniteManifold(before, "mesh_decimate");
    const size_t target = std::max<size_t>(1, (size_t)std::floor(ratio * (double)before.triangles + 1.0e-9));
    struct Face { int v[3]; bool alive = true; Vec3 n; double area = 0; };
    std::vector<Vec3> points(mesh.vertCount());
    for (size_t i = 0; i < points.size(); ++i) points[i] = at(mesh, (u32)i);
    std::vector<Face> faces(mesh.triCount());
    for (size_t t = 0; t < faces.size(); ++t) {
        faces[t].v[0] = (int)mesh.idx[t * 3];
        faces[t].v[1] = (int)mesh.idx[t * 3 + 1];
        faces[t].v[2] = (int)mesh.idx[t * 3 + 2];
        const Vec3 crossed = (points[faces[t].v[1]] - points[faces[t].v[0]]).cross(points[faces[t].v[2]] - points[faces[t].v[0]]);
        faces[t].area = crossed.len();
        faces[t].n = crossed.normalized();
    }
    auto aliveCount = [&] {
        size_t count = 0;
        for (const Face& face : faces) count += face.alive;
        return count;
    };
    auto compact = [&] {
        std::vector<int> remap(points.size(), -1);
        Mesh out;
        auto mapVertex = [&](int index) {
            if (remap[index] < 0) {
                remap[index] = (int)out.vertCount();
                out.addVertex(points[index]);
            }
            return (u32)remap[index];
        };
        for (const Face& face : faces) if (face.alive)
            out.addTri3(mapVertex(face.v[0]), mapVertex(face.v[1]), mapVertex(face.v[2]));
        return out;
    };
    std::vector<std::pair<int, int>> banned;
    auto bannedEdge = [&](int a, int b) {
        if (a > b) std::swap(a, b);
        return std::find(banned.begin(), banned.end(), std::pair{a, b}) != banned.end();
    };
    while (aliveCount() > target) {
        const int n = (int)points.size();
        std::vector<std::vector<int>> neighbors(n), incident(n);
        std::unordered_map<unsigned long long, int> edgeCount;
        auto edgeKey = [](int a, int b) {
            if (a > b) std::swap(a, b);
            return (unsigned long long)a << 32 | (unsigned)b;
        };
        std::vector<Quadric> quadrics(n);
        for (int f = 0; f < (int)faces.size(); ++f) {
            const Face& face = faces[f];
            if (!face.alive) continue;
            for (int k = 0; k < 3; ++k) {
                const int a = face.v[k], b = face.v[(k + 1) % 3];
                neighbors[a].push_back(b);
                incident[a].push_back(f);
                ++edgeCount[edgeKey(a, b)];
                quadrics[a].addPlane(face.n, points[a]);
            }
        }
        std::vector<char> boundary(n, 0);
        for (const auto& item : edgeCount) if (item.second == 1) {
            boundary[(int)(item.first >> 32)] = 1;
            boundary[(int)(item.first & 0xffffffffu)] = 1;
        }
        for (auto& list : neighbors) {
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }
        int bestA = -1, bestB = -1;
        double bestError = 1.0e300;
        Vec3 bestPoint;
        for (const auto& item : edgeCount) {
            if (item.second != 2) continue;
            int a = (int)(item.first >> 32), b = (int)(item.first & 0xffffffffu);
            if (boundary[a] || boundary[b] || bannedEdge(a, b)) continue;
            int shared = 0;
            for (int nb : neighbors[a])
                if (std::binary_search(neighbors[b].begin(), neighbors[b].end(), nb)) ++shared;
            if (shared != 2) continue;
            const Vec3 mid = (points[a] + points[b]) * 0.5;
            bool flipped = false;
            for (int vertex : {a, b}) for (int f : incident[vertex]) {
                const Face& face = faces[f];
                const bool hasA = face.v[0] == a || face.v[1] == a || face.v[2] == a;
                const bool hasB = face.v[0] == b || face.v[1] == b || face.v[2] == b;
                if (hasA && hasB) continue;
                Vec3 tri[3];
                for (int k = 0; k < 3; ++k) {
                    const int v = face.v[k];
                    tri[k] = v == a || v == b ? mid : points[v];
                }
                const Vec3 crossed = (tri[1] - tri[0]).cross(tri[2] - tri[0]);
                if (crossed.len() <= 1.0e-16 || crossed.normalized().dot(face.n) <= 1.0e-8) flipped = true;
            }
            if (flipped) continue;
            Quadric sum = quadrics[a];
            sum += quadrics[b];
            const double error = sum.error(mid);
            if (error < bestError) { bestError = error; bestA = a; bestB = b; bestPoint = mid; }
        }
        if (bestA < 0) break;
        const auto savedFaces = faces;
        const Vec3 saved = points[bestA];
        points[bestA] = bestPoint;
        for (Face& face : faces) {
            const bool hasA = face.v[0] == bestA || face.v[1] == bestA || face.v[2] == bestA;
            const bool hasB = face.v[0] == bestB || face.v[1] == bestB || face.v[2] == bestB;
            if (!face.alive) continue;
            if (hasA && hasB) { face.alive = false; continue; }
            for (int& v : face.v) if (v == bestB) v = bestA;
            if (face.v[0] == face.v[1] || face.v[1] == face.v[2] || face.v[0] == face.v[2]) face.alive = false;
            else {
                const Vec3 crossed = (points[face.v[1]] - points[face.v[0]]).cross(points[face.v[2]] - points[face.v[0]]);
                face.area = crossed.len();
                face.n = face.area > 1.0e-16 ? crossed.normalized() : face.n;
            }
        }
        const Mesh trial = compact();
        const MeshReport audit = analyzeMesh(trial);
        const bool preserved = before.closedOriented() ? audit.closedOriented() : audit.boundaryEdges == before.boundaryEdges;
        if (!preserved || audit.nonManifoldEdges || audit.inconsistentEdges || audit.degenerateTriangles) {
            faces = savedFaces;
            points[bestA] = saved;
            int a = bestA, b = bestB;
            if (a > b) std::swap(a, b);
            banned.push_back({a, b});
        }
    }
    Mesh out = compact();
    out.computeNormals();
    const MeshReport after = analyzeMesh(out);
    if (before.closedOriented() && !after.closedOriented())
        throw std::runtime_error("mesh_decimate could not preserve closed oriented edges");
    if (!before.closedOriented() && after.boundaryEdges != before.boundaryEdges)
        throw std::runtime_error("mesh_decimate changed the boundary; only interior edges are collapsed");
    return out;
}

Mesh smooth(Mesh mesh, int iterations, double lambda, bool pinBoundary) {
    if (iterations < 1 || iterations > 64) throw std::runtime_error("smooth iterations must be an integer in 1..64");
    if (!(lambda > 0.0 && lambda <= 1.0)) throw std::runtime_error("smooth lambda must be in (0, 1]");
    const MeshReport before = analyzeMesh(mesh);
    requireFiniteManifold(before, "mesh_smooth");
    const size_t n = mesh.vertCount();
    std::vector<std::vector<int>> neighbors(n);
    std::unordered_map<unsigned long long, int> edgeCount;
    for (size_t t = 0; t < mesh.idx.size(); t += 3) {
        for (int k = 0; k < 3; ++k) {
            u32 a = mesh.idx[t + k], b = mesh.idx[t + (k + 1) % 3];
            neighbors[a].push_back((int)b);
            unsigned long long key = ((unsigned long long)std::min(a, b) << 32) | std::max(a, b);
            ++edgeCount[key];
        }
    }
    std::vector<char> boundary(n, 0);
    for (const auto& item : edgeCount) if (item.second == 1) {
        boundary[(size_t)(item.first >> 32)] = 1;
        boundary[item.first & 0xffffffffu] = 1;
    }
    for (auto& list : neighbors) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    std::vector<Vec3> current(n), next(n);
    for (size_t i = 0; i < n; ++i) current[i] = at(mesh, (u32)i);
    for (int iter = 0; iter < iterations; ++iter) {
        for (size_t i = 0; i < n; ++i) {
            if ((pinBoundary && boundary[i]) || neighbors[i].empty()) { next[i] = current[i]; continue; }
            Vec3 average;
            for (int nb : neighbors[i]) average += current[nb];
            average = average / (double)neighbors[i].size();
            next[i] = current[i] * (1.0 - lambda) + average * lambda;
            if (!std::isfinite(next[i].x) || !std::isfinite(next[i].y) || !std::isfinite(next[i].z))
                throw std::runtime_error("mesh_smooth produced a non-finite vertex");
        }
        current.swap(next);
    }
    for (size_t i = 0; i < n; ++i) {
        mesh.pos[i * 3] = (f32)current[i].x;
        mesh.pos[i * 3 + 1] = (f32)current[i].y;
        mesh.pos[i * 3 + 2] = (f32)current[i].z;
    }
    mesh.computeNormals();
    return mesh;
}

Mesh offsetAlongNormals(Mesh mesh, double distance) {
    if (!(std::abs(distance) > 1.0e-9 && std::abs(distance) <= 1.0e4))
        throw std::runtime_error("mesh offset distance must be finite, nonzero and within 1e4 mm");
    const MeshReport before = analyzeMesh(mesh);
    requireFiniteManifold(before, "mesh_offset");
    if (before.closedOriented()) meshOrientOutward(mesh);
    mesh.computeNormals();
    for (size_t i = 0; i < mesh.vertCount(); ++i) {
        mesh.pos[i * 3] += (f32)(mesh.nrm[i * 3] * distance);
        mesh.pos[i * 3 + 1] += (f32)(mesh.nrm[i * 3 + 1] * distance);
        mesh.pos[i * 3 + 2] += (f32)(mesh.nrm[i * 3 + 2] * distance);
    }
    mesh.computeNormals();
    return mesh;
}

bool sameTriangle(u32 a, u32 b, u32 c, u32 x, u32 y, u32 z) {
    u32 left[3] = {a, b, c}, right[3] = {x, y, z};
    std::sort(left, left + 3);
    std::sort(right, right + 3);
    return left[0] == right[0] && left[1] == right[1] && left[2] == right[2];
}

Mesh fillBoundaries(Mesh mesh) {
    const MeshReport before = analyzeMesh(mesh);
    requireFiniteManifold(before, "mesh_fill");
    if (!before.boundaryEdges) throw std::runtime_error("mesh_fill found no boundary");
    struct Edge { int count = 0; u32 from = 0, to = 0; };
    std::unordered_map<unsigned long long, Edge> edges;
    for (size_t t = 0; t < mesh.idx.size(); t += 3) {
        for (int k = 0; k < 3; ++k) {
            u32 a = mesh.idx[t + k], b = mesh.idx[t + (k + 1) % 3];
            unsigned long long key = ((unsigned long long)std::min(a, b) << 32) | std::max(a, b);
            Edge& edge = edges[key];
            if (!edge.count) { edge.from = a; edge.to = b; }
            ++edge.count;
        }
    }
    std::unordered_map<u32, u32> next;
    for (const auto& item : edges) {
        if (item.second.count != 1) continue;
        if (!next.emplace(item.second.from, item.second.to).second)
            throw std::runtime_error("mesh_fill boundary vertex has two outgoing edges");
    }
    std::vector<char> used(mesh.vertCount(), 0);
    int loops = 0;
    constexpr double kPlanar = 1.0e-4;
    for (const auto& start : next) {
        if (used[start.first]) continue;
        std::vector<u32> loop;
        u32 cursor = start.first;
        do {
            if (cursor >= used.size() || used[cursor] || !next.count(cursor))
                throw std::runtime_error("mesh_fill boundary loop is not simple");
            used[cursor] = 1;
            loop.push_back(cursor);
            cursor = next[cursor];
            if (loop.size() > 256) throw std::runtime_error("mesh_fill loop exceeds 256 vertices");
        } while (cursor != start.first);
        if (loop.size() < 3) throw std::runtime_error("mesh_fill loop is degenerate");
        if (loop.size() == 3) {
            for (size_t t = 0; t < mesh.idx.size(); t += 3)
                if (sameTriangle(loop[0], loop[1], loop[2], mesh.idx[t], mesh.idx[t + 1], mesh.idx[t + 2]))
                    throw std::runtime_error("mesh_fill boundary matches an existing triangle; there is no open hole");
        }
        Vec3 normal, centroid;
        for (size_t i = 0; i < loop.size(); ++i) {
            const Vec3 a = at(mesh, loop[i]), b = at(mesh, loop[(i + 1) % loop.size()]);
            normal.x += (a.y - b.y) * (a.z + b.z);
            normal.y += (a.z - b.z) * (a.x + b.x);
            normal.z += (a.x - b.x) * (a.y + b.y);
            centroid += a;
        }
        centroid = centroid / (double)loop.size();
        if (normal.len() <= 1.0e-12) throw std::runtime_error("mesh_fill loop has no measurable plane");
        normal = normal.normalized();
        double deviation = 0;
        for (u32 index : loop) deviation = std::max(deviation, std::abs((at(mesh, index) - centroid).dot(normal)));
        if (deviation > kPlanar)
            throw std::runtime_error("mesh_fill loop deviates " + std::to_string(deviation) + " mm from a plane; tolerance is 1e-4 mm. Use surface_fill for a curved boundary");
        const Vec3 axis = std::abs(normal.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const Vec3 tangent = normal.cross(axis).normalized();
        const Vec3 bitangent = normal.cross(tangent);
        std::vector<Vec2> polygon(loop.size());
        for (size_t i = 0; i < loop.size(); ++i) {
            const Vec3 delta = at(mesh, loop[i]) - centroid;
            polygon[i] = {delta.dot(tangent), delta.dot(bitangent)};
        }
        auto cross2 = [](Vec2 a, Vec2 b, Vec2 c) { return (b - a).cross(c - a); };
        for (size_t i = 0; i < polygon.size(); ++i) {
            Vec2 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            for (size_t j = i + 1; j < polygon.size(); ++j) {
                if (j == i || (j + 1) % polygon.size() == i || (i + 1) % polygon.size() == j) continue;
                Vec2 c = polygon[j], d = polygon[(j + 1) % polygon.size()];
                const double den = (b - a).cross(d - c);
                if (std::abs(den) <= 1.0e-18) continue;
                const double t = (c - a).cross(d - c) / den;
                const double u = (c - a).cross(b - a) / den;
                if (t > 1.0e-9 && t < 1.0 - 1.0e-9 && u > 1.0e-9 && u < 1.0 - 1.0e-9)
                    throw std::runtime_error("mesh_fill loop is self-intersecting");
            }
        }
        double area = 0;
        for (size_t i = 0; i < polygon.size(); ++i) area += polygon[i].cross(polygon[(i + 1) % polygon.size()]);
        const std::vector<u32> capped = triangulatePolygon(polygon);
        if (capped.size() < 3) throw std::runtime_error("mesh_fill could not triangulate a boundary loop");
        for (size_t i = 0; i < capped.size(); i += 3) {
            u32 a = loop[capped[i]], b = loop[capped[i + 1]], c = loop[capped[i + 2]];
            if (area > 0) std::swap(b, c);
            mesh.addTri3(a, b, c);
        }
        ++loops;
    }
    if (!loops) throw std::runtime_error("mesh_fill found no usable boundary loop");
    mesh.computeNormals();
    return mesh;
}

double requiredNumber(const Feature& feature, const ExprEnv& env, const char* key) {
    if (!feature.p.count(key)) throw std::runtime_error(std::string("Missing mesh parameter: ") + key);
    return feature.num(env, key);
}
}

using V = arkjson::Value;

Mesh meshFromPayload(const V& mesh) {
    if (mesh.t != V::Obj) throw std::runtime_error("mesh must be an object");
    const V* positions = mesh.find("positions");
    const V* indices = mesh.find("indices");
    const V* grid = mesh.find("grid");
    if (indices && grid) throw std::runtime_error("mesh grid and indices are mutually exclusive");
    if (!positions || positions->t != V::Arr || (!indices && !grid))
        throw std::runtime_error("mesh needs positions and indices, or positions and grid");
    if (positions->arr.size() % 3 || positions->arr.size() < 9 || positions->arr.size() / 3 > kMeshModelMaxVertices)
        throw std::runtime_error("mesh positions must contain 3..12288 vertices");
    std::vector<u32> generated;
    if (grid) {
        if (grid->t != V::Obj) throw std::runtime_error("mesh grid must be an object");
        auto count = [](const V* value) {
            if (!value || value->t != V::Num || value->num < 2 || value->num > 256 || value->num != std::floor(value->num))
                throw std::runtime_error("mesh grid nu and nv must be integers from 2 to 256");
            return (int)value->num;
        };
        const int nu = count(grid->find("nu")), nv = count(grid->find("nv"));
        if ((size_t)nu * (size_t)nv != positions->arr.size() / 3)
            throw std::runtime_error("mesh grid positions must contain nu*nv vertices");
        const size_t cells = (size_t)(nu - 1) * (size_t)(nv - 1);
        if (cells * 2 > kMeshModelMaxTriangles) throw std::runtime_error("mesh grid exceeds 4096 triangles");
        generated.reserve(cells * 6);
        for (int y = 0; y < nv - 1; ++y) for (int x = 0; x < nu - 1; ++x) {
            const u32 i00 = (u32)(y * nu + x), i10 = i00 + 1, i01 = i00 + (u32)nu, i11 = i01 + 1;
            for (u32 index : {i00, i10, i01, i10, i11, i01}) generated.push_back(index);
        }
    } else if (indices->t != V::Arr || indices->arr.size() % 3 || indices->arr.size() < 3 || indices->arr.size() / 3 > kMeshModelMaxTriangles)
        throw std::runtime_error("mesh indices must contain 1..4096 triangles");
    Mesh out;
    out.pos.reserve(positions->arr.size());
    for (const V& value : positions->arr) {
        if (value.t != V::Num || !std::isfinite(value.num) || std::abs(value.num) > 1.0e6)
            throw std::runtime_error("mesh position must be a finite number within 1e6 mm");
        out.pos.push_back((f32)value.num);
    }
    const u32 vertices = (u32)out.vertCount();
    auto push = [&](u32 a, u32 b, u32 c) {
        if (a == b || b == c || a == c) throw std::runtime_error("mesh_body rejects degenerate triangles");
        out.addTri3(a, b, c);
    };
    if (grid) {
        for (size_t i = 0; i < generated.size(); i += 3) push(generated[i], generated[i + 1], generated[i + 2]);
    } else {
        for (size_t i = 0; i < indices->arr.size(); i += 3) {
            u32 tri[3];
            for (int k = 0; k < 3; ++k) {
                const V& value = indices->arr[i + (size_t)k];
                if (value.t != V::Num || value.num < 0 || value.num >= vertices || value.num != std::floor(value.num))
                    throw std::runtime_error("mesh index is out of range");
                tri[k] = (u32)value.num;
            }
            push(tri[0], tri[1], tri[2]);
        }
    }
    out.computeNormals();
    return out;
}

V meshToPayload(const Mesh& mesh) {
    V payload = V::makeObj(), positions = V::makeArr(), indices = V::makeArr();
    for (float value : mesh.pos) positions.arr.push_back(V::makeNum(value));
    for (u32 value : mesh.idx) indices.arr.push_back(V::makeNum(value));
    payload.set("positions", std::move(positions));
    payload.set("indices", std::move(indices));
    return payload;
}

Body evaluateMeshFeature(const Feature& feature, const ExprEnv& env, const std::vector<Body>& preceding,
                         const TessOptions& tessellation) {
    bool sampled = false;
    Mesh mesh;
    const char* operation = featureTypeName(feature.type);
    if (feature.type == FeatureType::MeshBody) {
        mesh = meshFromPayload(feature.sourceData);
        operation = "mesh_body";
    } else {
        mesh = inputMesh(feature, env, preceding, tessellation, sampled);
        const MeshReport audit = analyzeMesh(mesh);
        if (feature.type == FeatureType::MeshWeld) {
            const double epsilon = feature.p.count("epsilon") ? feature.num(env, "epsilon") : 1.0e-4;
            if (!(epsilon > 0.0 && epsilon <= 1.0)) throw std::runtime_error("weld epsilon must be in (0, 1] mm");
            mesh.weld(epsilon);
            if (!mesh.triCount()) throw std::runtime_error("mesh_weld removed every triangle");
            mesh.computeNormals();
        } else if (feature.type == FeatureType::MeshSmooth) {
            const double iterations = requiredNumber(feature, env, "iterations");
            if (iterations < 1 || iterations > 64 || iterations != std::floor(iterations))
                throw std::runtime_error("smooth iterations must be an integer in 1..64");
            const double lambda = feature.p.count("lambda") ? feature.num(env, "lambda") : 0.5;
            const bool pin = !feature.p.count("pin_boundary") || feature.num(env, "pin_boundary") != 0.0;
            mesh = smooth(std::move(mesh), (int)iterations, lambda, pin);
        } else if (feature.type == FeatureType::MeshDecimate) {
            mesh = decimate(std::move(mesh), requiredNumber(feature, env, "ratio"));
        } else if (feature.type == FeatureType::MeshOffset) {
            mesh = offsetAlongNormals(std::move(mesh), requiredNumber(feature, env, "distance"));
        } else if (feature.type == FeatureType::MeshFill) {
            mesh = fillBoundaries(std::move(mesh));
        } else throw std::runtime_error("Unknown mesh feature");
        (void)audit;
    }
    if (!mesh.triCount()) throw std::runtime_error("Mesh feature produced no triangles");
    const MeshReport after = analyzeMesh(mesh);
    Body body;
    V report = modelReport(operation, feature.type == FeatureType::MeshBody ? after : analyzeMesh(inputMesh(feature, env, preceding, tessellation, sampled)), after, sampled);
    if (feature.type == FeatureType::MeshBody) {
        report.set("input_vertices", number((double)after.vertices));
        report.set("input_triangles", number((double)after.triangles));
    }
    if (feature.type == FeatureType::MeshDecimate) {
        const double ratio = requiredNumber(feature, env, "ratio");
        const size_t target = std::max<size_t>(1, (size_t)std::floor(ratio * report.find("input_triangles")->num + 1.0e-9));
        report.set("target_triangles", number((double)target));
        report.set("target_reached", V::makeBool(after.triangles <= target));
        report.set("boundary_preserved", V::makeBool(true));
    }
    if (feature.type == FeatureType::MeshOffset) {
        report.set("offset_model", V::makeStr("vertex_normal"));
        report.set("distance_mm", number(requiredNumber(feature, env, "distance")));
    }
    if (feature.type == FeatureType::MeshFill) {
        report.set("fill_model", V::makeStr("planar_ear_clip"));
        report.set("planarity_tolerance_mm", number(1.0e-4));
        report.set("residual_boundary_edges", number((double)after.boundaryEdges));
    }
    if (feature.type == FeatureType::MeshSmooth) {
        report.set("lambda", number(feature.p.count("lambda") ? feature.num(env, "lambda") : 0.5));
        report.set("pin_boundary", V::makeBool(!feature.p.count("pin_boundary") || feature.num(env, "pin_boundary") != 0.0));
    }
    body.nativeData = std::move(report);
    body.baked = std::make_unique<Mesh>(std::move(mesh));
    return body;
}
} // namespace ark
