#include "ark/mesh_analysis.hpp"
#include <unordered_map>
#include <numeric>
#include <set>
namespace ark {
bool MeshReport::closedOriented() const {
    return triangles && !boundaryEdges && !nonManifoldEdges && !inconsistentEdges
        && !degenerateTriangles && !invalidIndices && !nonFiniteVertices;
}
MeshReport analyzeMesh(const Mesh& m){
    MeshReport r; r.vertices=m.vertCount(); r.triangles=m.triCount();
    if(m.pos.size()%3 || m.idx.size()%3) ++r.invalidIndices;
    for(size_t i=0;i<r.vertices;++i){
        Vec3 p{m.pos[i*3],m.pos[i*3+1],m.pos[i*3+2]};
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))++r.nonFiniteVertices;
        else r.bounds.grow(p);
    }
    struct Edge {size_t count=0; int winding=0;};
    std::unordered_map<u64,Edge> edges; edges.reserve(m.idx.size());
    std::vector<u32> parent(r.vertices); std::iota(parent.begin(),parent.end(),0);
    std::vector<bool> used(r.vertices,false);
    auto root=[&](u32 v){while(parent[v]!=v){parent[v]=parent[parent[v]];v=parent[v];}return v;};
    Vec3 origin=r.bounds.valid()?r.bounds.center():Vec3{};
    for(size_t i=0;i+2<m.idx.size();i+=3){
        u32 a=m.idx[i],b=m.idx[i+1],c=m.idx[i+2];
        if(a>=r.vertices||b>=r.vertices||c>=r.vertices){++r.invalidIndices;continue;}
        Vec3 p{m.pos[a*3],m.pos[a*3+1],m.pos[a*3+2]};
        Vec3 q{m.pos[b*3],m.pos[b*3+1],m.pos[b*3+2]};
        Vec3 s{m.pos[c*3],m.pos[c*3+1],m.pos[c*3+2]};
        f64 twiceArea=(q-p).cross(s-p).len();
        if(!std::isfinite(twiceArea)||twiceArea<=1e-16){++r.degenerateTriangles;continue;}
        r.area+=twiceArea*.5;
        r.signedVolume+=(p-origin).dot((q-origin).cross(s-origin))/6.;
        u32 ids[]={a,b,c};
        for(int e=0;e<3;++e){
            u32 x=ids[e],y=ids[(e+1)%3]; used[x]=used[y]=true;
            parent[root(x)]=root(y);
            u64 key=((u64)std::min(x,y)<<32)|std::max(x,y);
            auto& edge=edges[key]; ++edge.count; edge.winding+=x<y?1:-1;
        }
    }
    std::set<u32> roots;
    for(u32 i=0;i<r.vertices;++i)if(used[i])roots.insert(root(i));
    r.components=roots.size(); r.edges=edges.size();
    for(auto& kv:edges){auto e=kv.second;
        if(e.count==1)++r.boundaryEdges;
        else if(e.count>2)++r.nonManifoldEdges;
        else if(e.winding!=0)++r.inconsistentEdges;
    }
    return r;
}
arkjson::Value MeshReport::json() const {
    using V=arkjson::Value; V v=V::makeObj();
    auto n=[&](const char* k,double x){v.set(k,V::makeNum(x));};
    n("vertices",vertices);n("triangles",triangles);n("edges",edges);n("components",components);
    n("boundary_edges",boundaryEdges);n("non_manifold_edges",nonManifoldEdges);
    n("inconsistent_edges",inconsistentEdges);n("degenerate_triangles",degenerateTriangles);
    n("invalid_indices",invalidIndices);n("non_finite_vertices",nonFiniteVertices);n("area_mm2",area);
    v.set("closed_oriented_edges",V::makeBool(closedOriented()));
    v.set("self_intersections_checked",V::makeBool(false));
    v.set("solid_certified",V::makeBool(false));
    if(closedOriented())n("signed_volume_mm3",signedVolume);
    else v.set("signed_volume_mm3",V{});
    if(bounds.valid()){
        V b=V::makeObj(),lo=V::makeArr(),hi=V::makeArr();
        for(auto x:{bounds.mn.x,bounds.mn.y,bounds.mn.z})lo.arr.push_back(V::makeNum(x));
        for(auto x:{bounds.mx.x,bounds.mx.y,bounds.mx.z})hi.arr.push_back(V::makeNum(x));
        b.set("min",lo);b.set("max",hi);v.set("bounds_mm",b);
    }
    return v;
}
}
