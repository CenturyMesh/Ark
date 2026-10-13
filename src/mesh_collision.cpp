#include "ark/mesh_collision.hpp"
#include "ark/mesh_analysis.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr double contactTolerance=1e-6;
constexpr size_t maximumTriangles=100000,maximumVertices=300000;
constexpr size_t maximumNodePairs=4000000,maximumTrianglePairs=500000;
constexpr size_t maximumWindingTerms=4000000;
struct Box {
    Vec3 low{1e300,1e300,1e300},high{-1e300,-1e300,-1e300};
    void add(Vec3 p){low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
    void add(const Box& b){add(b.low);add(b.high);}
    bool overlaps(const Box& b)const{return low.x<=b.high.x+contactTolerance&&high.x+contactTolerance>=b.low.x&&low.y<=b.high.y+contactTolerance&&high.y+contactTolerance>=b.low.y&&low.z<=b.high.z+contactTolerance&&high.z+contactTolerance>=b.low.z;}
};
struct Triangle {std::array<Vec3,3> p;Box box;Vec3 center;};
struct Node {Box box;size_t start=0,count=0;int left=-1,right=-1;bool leaf()const{return left<0;}};
struct Shape {
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
    std::vector<size_t> order;
    std::vector<Node> nodes;
    std::vector<Vec3> representatives;
    MeshReport topology;
    Box box;
};
void validateFrame(const Mat4& m){
    constexpr double tolerance=1e-10;
    for(double x:m.m)if(!std::isfinite(x)||std::abs(x)>1e8)throw std::runtime_error("Mesh collision frame must contain bounded finite values");
    if(std::abs(m.m[3])>tolerance||std::abs(m.m[7])>tolerance||std::abs(m.m[11])>tolerance||std::abs(m.m[15]-1)>tolerance)throw std::runtime_error("Mesh collision frame must be affine");
    for(int a=0;a<3;++a)for(int b=a;b<3;++b){double dot=0;for(int r=0;r<3;++r)dot+=m.m[4*a+r]*m.m[4*b+r];if(std::abs(dot-(a==b?1.:0.))>tolerance)throw std::runtime_error("Mesh collision requires rigid orthonormal frames");}
    const Vec3 x{m.m[0],m.m[1],m.m[2]},y{m.m[4],m.m[5],m.m[6]},z{m.m[8],m.m[9],m.m[10]};
    if(std::abs(x.cross(y).dot(z)-1)>tolerance)throw std::runtime_error("Mesh collision frame must preserve orientation");
}
int buildNode(Shape& shape,size_t start,size_t count){
    const int result=(int)shape.nodes.size();shape.nodes.push_back({});Node node;node.start=start;node.count=count;
    for(size_t i=start;i<start+count;++i)node.box.add(shape.triangles[shape.order[i]].box);
    if(count>8){const Vec3 span=node.box.high-node.box.low;int axis=span.y>span.x?1:0;if(span.z>(axis?span.y:span.x))axis=2;auto coordinate=[axis](const Triangle& triangle){return axis==0?triangle.center.x:axis==1?triangle.center.y:triangle.center.z;};const size_t middle=start+count/2;std::nth_element(shape.order.begin()+start,shape.order.begin()+middle,shape.order.begin()+start+count,[&](size_t a,size_t b){const double ca=coordinate(shape.triangles[a]),cb=coordinate(shape.triangles[b]);return ca==cb?a<b:ca<cb;});node.left=buildNode(shape,start,count/2);node.right=buildNode(shape,middle,count-count/2);}
    shape.nodes[(size_t)result]=node;return result;
}
Shape prepare(const Mesh& mesh,const Mat4& world){
    validateFrame(world);
    if(mesh.pos.empty()||mesh.pos.size()%3||mesh.idx.empty()||mesh.idx.size()%3)throw std::runtime_error("Mesh collision requires complete nonempty XYZ vertices and triangle indices");
    if(mesh.triCount()>maximumTriangles||mesh.vertCount()>maximumVertices)throw std::runtime_error("Mesh collision exceeds 100000 triangles or 300000 vertices per input");
    Shape shape;shape.topology=analyzeMesh(mesh);
    if(shape.topology.nonFiniteVertices||shape.topology.invalidIndices)throw std::runtime_error("Mesh collision rejects nonfinite vertices or invalid triangle indices");
    if(shape.topology.degenerateTriangles)throw std::runtime_error("Mesh collision rejects degenerate triangles");
    shape.vertices.reserve(mesh.vertCount());for(size_t i=0;i<mesh.vertCount();++i){const Vec3 local{mesh.pos[3*i],mesh.pos[3*i+1],mesh.pos[3*i+2]};if(std::abs(local.x)>1e7||std::abs(local.y)>1e7||std::abs(local.z)>1e7)throw std::runtime_error("Mesh collision coordinates exceed 1e7 mm");const Vec3 p=world.transformPoint(local);if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::abs(p.x)>1e8||std::abs(p.y)>1e8||std::abs(p.z)>1e8)throw std::runtime_error("Mesh collision transformed coordinates exceed bounds");shape.vertices.push_back(p);}
    std::vector<size_t> parent(mesh.triCount());std::iota(parent.begin(),parent.end(),0);
    std::map<std::pair<u32,u32>,size_t> edgeOwner;
    auto leader=[&](size_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    shape.triangles.reserve(mesh.triCount());for(size_t i=0;i<mesh.triCount();++i){Triangle triangle;for(size_t j=0;j<3;++j){const u32 index=mesh.idx[3*i+j],next=mesh.idx[3*i+(j+1)%3];triangle.p[j]=shape.vertices[index];triangle.box.add(triangle.p[j]);const auto owner=edgeOwner.emplace(std::minmax(index,next),i);if(!owner.second)parent[leader(i)]=leader(owner.first->second);}triangle.center=(triangle.p[0]+triangle.p[1]+triangle.p[2])/3.;shape.box.add(triangle.box);shape.triangles.push_back(triangle);}
    std::map<size_t,Vec3> representatives;for(size_t i=0;i<mesh.triCount();++i)representatives.emplace(leader(i),shape.triangles[i].p[0]);for(const auto& entry:representatives)shape.representatives.push_back(entry.second);
    shape.order.resize(mesh.triCount());std::iota(shape.order.begin(),shape.order.end(),0);shape.nodes.reserve(2*mesh.triCount());buildNode(shape,0,mesh.triCount());return shape;
}
double pointTriangleSquared(Vec3 p,const Triangle& triangle){
    const Vec3 a=triangle.p[0],b=triangle.p[1],c=triangle.p[2],ab=b-a,ac=c-a,ap=p-a;
    const double d1=ab.dot(ap),d2=ac.dot(ap);if(d1<=0&&d2<=0)return ap.lensq();
    const Vec3 bp=p-b;const double d3=ab.dot(bp),d4=ac.dot(bp);if(d3>=0&&d4<=d3)return bp.lensq();
    const double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0){const double v=d1/(d1-d3);return (p-(a+ab*v)).lensq();}
    const Vec3 cp=p-c;const double d5=ab.dot(cp),d6=ac.dot(cp);if(d6>=0&&d5<=d6)return cp.lensq();
    const double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0){const double w=d2/(d2-d6);return (p-(a+ac*w)).lensq();}
    const double va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const double w=(d4-d3)/((d4-d3)+(d5-d6));return (p-(b+(c-b)*w)).lensq();}
    const double denominator=va+vb+vc;if(!std::isfinite(denominator)||denominator<=0)throw std::runtime_error("Mesh collision triangle distance is numerically ill-conditioned");
    const double inverse=1/denominator,v=vb*inverse,w=vc*inverse;return (p-(a+ab*v+ac*w)).lensq();
}
double segmentsSquared(Vec3 p1,Vec3 q1,Vec3 p2,Vec3 q2){
    const Vec3 d1=q1-p1,d2=q2-p2,r=p1-p2,normal=d1.cross(d2);const double a=d1.lensq(),e=d2.lensq(),f=d2.dot(r),c=d1.dot(r),b=d1.dot(d2),denominator=normal.lensq();
    if(a<=0||e<=0)throw std::runtime_error("Mesh collision encountered an empty triangle edge");
    // A cross-product denominator avoids cancelling a*e-b*b for long,
    // almost-parallel segments that can still cross within their lengths.
    double s=denominator>0?std::clamp(d2.cross(r).dot(normal)/denominator,0.,1.):0;
    double t=(b*s+f)/e;if(t<0){t=0;s=std::clamp(-c/a,0.,1.);}else if(t>1){t=1;s=std::clamp((b-c)/a,0.,1.);}return (p1+d1*s-p2-d2*t).lensq();
}
bool segmentPierces(Vec3 start,Vec3 end,const Triangle& triangle){
    const Vec3 normal=(triangle.p[1]-triangle.p[0]).cross(triangle.p[2]-triangle.p[0]);const double magnitude=normal.len();if(magnitude<=0)throw std::runtime_error("Mesh collision degenerate triangle normal");
    const double a=normal.dot(start-triangle.p[0])/magnitude,b=normal.dot(end-triangle.p[0])/magnitude;
    if((a>contactTolerance&&b>contactTolerance)||(a<-contactTolerance&&b<-contactTolerance)||std::abs(a-b)<=std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)}))return false;
    const double t=a/(a-b);if(t<0||t>1)return false;
    return pointTriangleSquared(start+(end-start)*t,triangle)<=contactTolerance*contactTolerance;
}
bool triangleContact(const Triangle& a,const Triangle& b){
    constexpr double squared=contactTolerance*contactTolerance;
    for(size_t i=0;i<3;++i){if(segmentPierces(a.p[i],a.p[(i+1)%3],b)||segmentPierces(b.p[i],b.p[(i+1)%3],a))return true;if(pointTriangleSquared(a.p[i],b)<=squared||pointTriangleSquared(b.p[i],a)<=squared)return true;}
    for(size_t i=0;i<3;++i)for(size_t j=0;j<3;++j)if(segmentsSquared(a.p[i],a.p[(i+1)%3],b.p[j],b.p[(j+1)%3])<=squared)return true;
    return false;
}
struct BoundaryResult {size_t nodePairs=0,trianglePairs=0,contacts=0;V examples=V::makeArr();};
BoundaryResult boundaries(const Shape& a,const Shape& b){
    BoundaryResult result;std::vector<std::pair<int,int>> pending{{0,0}};
    while(!pending.empty()){const auto pair=pending.back();pending.pop_back();if(++result.nodePairs>maximumNodePairs)throw std::runtime_error("Mesh collision exceeds 4000000 BVH node pairs; no partial collision report published");const Node& na=a.nodes[(size_t)pair.first];const Node& nb=b.nodes[(size_t)pair.second];if(!na.box.overlaps(nb.box))continue;
        if(na.leaf()&&nb.leaf()){for(size_t ia=na.start;ia<na.start+na.count;++ia)for(size_t ib=nb.start;ib<nb.start+nb.count;++ib){const size_t ta=a.order[ia],tb=b.order[ib];const auto& triangleA=a.triangles[ta];const auto& triangleB=b.triangles[tb];if(!triangleA.box.overlaps(triangleB.box))continue;if(++result.trianglePairs>maximumTrianglePairs)throw std::runtime_error("Mesh collision exceeds 500000 candidate triangle pairs; no partial collision report published");if(triangleContact(triangleA,triangleB)){++result.contacts;if(result.examples.arr.size()<16){V row=V::makeObj();row.set("a_triangle",V::makeNum(ta));row.set("b_triangle",V::makeNum(tb));result.examples.arr.push_back(row);}}}}
        else if(nb.leaf()||(!na.leaf()&&na.count>=nb.count)){pending.emplace_back(na.left,pair.second);pending.emplace_back(na.right,pair.second);}
        else{pending.emplace_back(pair.first,nb.left);pending.emplace_back(pair.first,nb.right);}
    }return result;
}
enum class Location {Outside,Inside,Unknown};
Location locate(Vec3 p,const Shape& target,size_t& terms){
    double sum=0,correction=0;
    for(const Triangle& triangle:target.triangles){if(++terms>maximumWindingTerms)throw std::runtime_error("Mesh collision exceeds 4000000 winding terms; containment report is not published partially");if(pointTriangleSquared(p,triangle)<=contactTolerance*contactTolerance)return Location::Unknown;const Vec3 a=triangle.p[0]-p,b=triangle.p[1]-p,c=triangle.p[2]-p;const double la=a.len(),lb=b.len(),lc=c.len();const double numerator=a.dot(b.cross(c)),denominator=la*lb*lc+a.dot(b)*lc+b.dot(c)*la+c.dot(a)*lb;const double angle=2*std::atan2(numerator,denominator);if(!std::isfinite(angle))return Location::Unknown;const double adjusted=angle-correction,next=sum+adjusted;correction=(next-sum)-adjusted;sum=next;}
    const double winding=std::abs(sum/(4*3.14159265358979323846));
    if(winding<1e-5)return Location::Outside;if(std::abs(winding-1)<1e-5)return Location::Inside;return Location::Unknown;
}
V containment(const Shape& source,const Shape& target,size_t& terms,bool& known,bool& any){
    V result=V::makeObj();result.set("source_components",V::makeNum(source.representatives.size()));result.set("target_closed_oriented",V::makeBool(target.topology.closedOriented()));
    if(!source.topology.closedOriented()||!target.topology.closedOriented()){known=false;result.set("status",V::makeStr("unknown_open_or_nonmanifold_mesh"));result.set("contained_components",V{});return result;}
    size_t count=0;for(Vec3 p:source.representatives){const auto location=locate(p,target,terms);if(location==Location::Unknown){known=false;result.set("status",V::makeStr("unknown_ambiguous_winding_or_near_boundary"));result.set("contained_components",V{});return result;}if(location==Location::Inside)++count;}
    any|=count>0;result.set("status",V::makeStr(count==0?"outside":count==source.representatives.size()?"contained":"partially_contained_components"));result.set("contained_components",V::makeNum(count));return result;
}
V boxValue(const Box& box){V result=V::makeArr();for(double x:{box.low.x,box.low.y,box.low.z,box.high.x,box.high.y,box.high.z})result.arr.push_back(V::makeNum(x));return result;}
}
V inspectMeshCollision(const Mesh& meshA,const Mat4& aWorld,const Mesh& meshB,const Mat4& bWorld){
    const Shape a=prepare(meshA,aWorld),b=prepare(meshB,bWorld);const BoundaryResult boundary=boundaries(a,b);
    V result=V::makeObj();bool known=true,inside=false;size_t terms=0;V aInB=V::makeObj(),bInA=V::makeObj();
    if(boundary.contacts){known=false;aInB.set("status",V::makeStr("unknown_boundary_contact"));bInA.set("status",V::makeStr("unknown_boundary_contact"));}
    else{aInB=containment(a,b,terms,known,inside);bInA=containment(b,a,terms,known,inside);}
    result.set("status",V::makeStr(boundary.contacts?"surface_intersection":inside?"interference":known?"separated":"surface_separated"));
    result.set("boundary_contact",V::makeBool(boundary.contacts>0));result.set("surface_intersection",V::makeBool(boundary.contacts>0));
    result.set("interference",known?V::makeBool(inside):inside?V::makeBool(true):V{});result.set("touching",V{});
    result.set("containment_checked",V::makeBool(known));result.set("a_in_b",aInB);result.set("b_in_a",bInA);
    result.set("min_distance_mm",V{});result.set("common_volume_mm3",V{});result.set("minimum_distance_computed",V::makeBool(false));
    result.set("aabb_candidate",V::makeBool(a.box.overlaps(b.box)));result.set("a_bounds_mm",boxValue(a.box));result.set("b_bounds_mm",boxValue(b.box));
    result.set("candidate_triangle_pairs",V::makeNum(boundary.trianglePairs));result.set("boundary_contact_pairs",V::makeNum(boundary.contacts));result.set("contact_examples",boundary.examples);result.set("contact_examples_complete",V::makeBool(boundary.contacts<=16));result.set("bvh_node_pairs",V::makeNum(boundary.nodePairs));result.set("winding_terms",V::makeNum(terms));
    result.set("a_topology",a.topology.json());result.set("b_topology",b.topology.json());
    result.set("contact_tolerance_mm",V::makeNum(contactTolerance));result.set("authority",V::makeStr("indexed_triangle_mesh"));result.set("exact_geometry",V::makeBool(false));
    result.set("containment_method",V::makeStr("connected_component_representatives_and_oriented_solid_angle_winding"));result.set("containment_assumes_no_self_intersections",V::makeBool(true));result.set("self_intersections_checked",V::makeBool(false));
    result.set("trajectory_checked",V::makeBool(false));result.set("response_dynamics_checked",V::makeBool(false));result.set("complete",V::makeBool(true));return result;
}
}
