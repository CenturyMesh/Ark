#include "ark/mesh_query.hpp"
#include <numeric>
namespace ark {
namespace {
Vec3 vertex(const Mesh& m,u32 i){return {m.pos[i*3],m.pos[i*3+1],m.pos[i*3+2]};}
Vec3 centroid(const Mesh& m,u32 t){return (vertex(m,m.idx[t*3])+vertex(m,m.idx[t*3+1])+vertex(m,m.idx[t*3+2]))/3.;}
bool boxHit(const AABB& b,const Ray& ray,f64 best){
    f64 lo=0,hi=best;
    const f64 mn[]={b.mn.x,b.mn.y,b.mn.z},mx[]={b.mx.x,b.mx.y,b.mx.z};
    const f64 o[]={ray.o.x,ray.o.y,ray.o.z},d[]={ray.d.x,ray.d.y,ray.d.z};
    for(int a=0;a<3;++a){
        if(std::fabs(d[a])<1e-15){if(o[a]<mn[a]||o[a]>mx[a])return false;continue;}
        f64 t0=(mn[a]-o[a])/d[a],t1=(mx[a]-o[a])/d[a];if(t0>t1)std::swap(t0,t1);
        lo=std::max(lo,t0);hi=std::min(hi,t1);if(lo>hi)return false;
    }return true;
}
}
MeshBVH::MeshBVH(const Mesh& m){
    if(m.pos.size()%3||m.idx.size()%3)return;
    for(auto i:m.idx)if(i>=m.vertCount())return;
    for(auto x:m.pos)if(!std::isfinite(x))return;
    triangles_.resize(m.triCount());std::iota(triangles_.begin(),triangles_.end(),0);
    nodes_.reserve(m.triCount()/2+1);if(!triangles_.empty())build(m,0,(u32)triangles_.size());
}
u32 MeshBVH::build(const Mesh& m,u32 start,u32 count){
    Node n;n.start=start;n.count=count;AABB centers;
    for(u32 i=start;i<start+count;++i){u32 t=triangles_[i];centers.grow(centroid(m,t));for(int k=0;k<3;++k)n.box.grow(vertex(m,m.idx[t*3+k]));}
    u32 idx=(u32)nodes_.size();nodes_.push_back(n);
    if(count>8){
        auto e=centers.extent();int axis=e.x>=e.y&&e.x>=e.z?0:e.y>=e.z?1:2;
        auto coord=[&](u32 t){auto c=centroid(m,t);return axis==0?c.x:axis==1?c.y:c.z;};
        u32 middle=start+count/2;
        std::nth_element(triangles_.begin()+start,triangles_.begin()+middle,triangles_.begin()+start+count,[&](u32 a,u32 b){f64 x=coord(a),y=coord(b);return x==y?a<b:x<y;});
        nodes_[idx].left=build(m,start,middle-start);nodes_[idx].right=build(m,middle,start+count-middle);nodes_[idx].count=0;
    }return idx;
}
MeshHit MeshBVH::raycast(const Mesh& m,const Ray& input) const {
    MeshHit hit;hit.distance=1e300;if(nodes_.empty()||input.d.len()<1e-15)return hit;
    Ray ray{input.o,input.d.normalized()};std::vector<u32> stack{0};
    while(!stack.empty()){
        auto n=nodes_[stack.back()];stack.pop_back();if(!boxHit(n.box,ray,hit.distance))continue;
        if(!n.count){stack.push_back(n.right);stack.push_back(n.left);continue;}
        for(u32 i=n.start;i<n.start+n.count;++i){
            u32 t=triangles_[i];Vec3 a=vertex(m,m.idx[t*3]),b=vertex(m,m.idx[t*3+1]),c=vertex(m,m.idx[t*3+2]);
            Vec3 e1=b-a,e2=c-a,p=ray.d.cross(e2);f64 det=e1.dot(p);
            if(std::fabs(det)<1e-15)continue;
            Vec3 delta=ray.o-a;f64 u=delta.dot(p)/det;if(u<0||u>1)continue;
            Vec3 q=delta.cross(e1);f64 v=ray.d.dot(q)/det;if(v<0||u+v>1)continue;
            f64 distance=e2.dot(q)/det;if(distance<0||distance>=hit.distance)continue;
            hit={true,distance,t,ray.o+ray.d*distance,e1.cross(e2).normalized()};
        }
    }if(!hit.hit)hit.distance=0;return hit;
}
}
