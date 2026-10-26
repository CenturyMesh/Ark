// C.04/A.04 synthetic runtime comparison, with no drawing/session input.
// Matches the 059C native UI fixture: hollow sphere R30/wall3 mm, density1100,
// E3MPa, 16x8 latitude grid + two poles; 130 nodes/720 links/23 colours.
// The literal mass is the fixture's exact native B-rep mass in kg. A_eff and
// triangle lumping intentionally match the production reduced-network bridge.
// Run baseline and candidate executables alternately on the same machine.
// GPU run0 includes device/shader initialization; runs1..3 reuse both device and
// constraint cache. Compare cold and warm separately and retain all attempts.
// The digest contains scalar coordinates, velocities and forces (no Vec3
// padding), so identical hashes establish same-adapter before/after equivalence.
// Link with ark, including rubber_patterns.cpp and the D3D11 dependencies.
#include "ark/rubber_dynamics.hpp"
#include "ark/rubber_patterns.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
using namespace ark;
using V=arkjson::Value;
std::string digest(const RubberDynamicsResult& r){std::string bytes;auto add=[&](double v){bytes.append((const char*)&v,sizeof(v));};for(const auto& f:r.frames)for(const auto& p:f){add(p.x);add(p.y);add(p.z);}for(const auto& v:r.finalVelocities){add(v.x);add(v.y);add(v.z);}for(double v:r.finalLinkForces_N)add(v);return iterationSha256(bytes);}
int main(){try{
 V recipe;arkjson::parse(R"({"surface":{"kind":"sphere","radius_mm":30},"distribution":"grid","around":16,"rows":8})",recipe);RubberPatternPlan plan(recipe);
 constexpr unsigned around=16,rows=8;std::vector<Vec3> points,normals;std::vector<unsigned> triangles;
 const Vec3 translation{80,0,80};for(unsigned j=0;j<rows;++j)for(unsigned i=0;i<around;++i){auto f=plan.frame(j*around+i);points.push_back((f.position_mm+translation)*.001);normals.push_back(f.normal);}
 auto triangle=[&](unsigned x,unsigned y,unsigned z){if((points[y]-points[x]).cross(points[z]-points[x]).dot(normals[x]+normals[y]+normals[z])<0)std::swap(y,z);triangles.insert(triangles.end(),{x,y,z});};
 for(unsigned j=0;j<rows-1;++j)for(unsigned i=0;i<around;++i){const unsigned ni=(i+1)%around,nj=j+1;triangle(j*around+i,j*around+ni,nj*around+ni);triangle(j*around+i,nj*around+ni,nj*around+i);}
 const unsigned north=(unsigned)points.size(),south=north+1;points.push_back((translation+Vec3{0,0,30})*.001);points.push_back((translation-Vec3{0,0,30})*.001);normals.push_back({0,0,1});normals.push_back({0,0,-1});for(unsigned i=0;i<around;++i){triangle(north,(i+1)%around,i);triangle(south,(rows-1)*around+i,(rows-1)*around+(i+1)%around);}
 std::vector<double> weights(points.size(),0);std::set<std::pair<unsigned,unsigned>> edges;auto edge=[&](unsigned x,unsigned y){if(x!=y)edges.insert(std::minmax(x,y));};
 for(size_t t=0;t<triangles.size();t+=3){unsigned x=triangles[t],y=triangles[t+1],z=triangles[t+2];double area=(points[y]-points[x]).cross(points[z]-points[x]).len()*.5;for(auto n:{x,y,z})weights[n]+=area/3;edge(x,y);edge(y,z);edge(z,x);}
 for(unsigned j=0;j<rows;++j)for(unsigned i=0;i<around;++i){edge(j*around+i,j*around+(i+2)%around);if(j+1<rows)edge(j*around+(i+1)%around,(j+1)*around+i);if(j+2<rows)edge(j*around+i,(j+2)*around+i);}
 double area=0,totalLength=0;for(auto w:weights)area+=w;for(auto e:edges)totalLength+=(points[e.second]-points[e.first]).len();
 const double mass=0.033714315721264217,volume=mass/1100.,section=volume/totalLength;std::vector<RubberNode> nodes;for(size_t i=0;i<points.size();++i){RubberNode n;n.position=points[i];n.mass=mass*weights[i]/area;nodes.push_back(n);}
 std::vector<RubberLink> links;for(auto e:edges){const double length=(points[e.second]-points[e.first]).len();links.push_back({e.first,e.second,length,length/(3e6*section)});}
 RubberDynamicsOptions o;o.backend="cpu";o.duration=.2;o.step=1./240;o.outputFps=30;o.iterations=8;o.gravity={0,0,-9.80665};o.ground=true;o.friction=.7;o.damping=2;
 auto reference=simulateRubber(nodes,links,o);std::cout<<"CPU "<<reference.diagnostics.dump()<<'\n';o.backend="gpu";
 for(int run=0;run<4;++run){auto result=simulateRubber(nodes,links,o);double error=0;for(size_t f=0;f<result.frames.size();++f)for(size_t i=0;i<nodes.size();++i)error=std::max(error,(reference.frames[f][i]-result.frames[f][i]).len());std::cout<<"GPU "<<run<<" "<<result.diagnostics.dump()<<" hash="<<digest(result)<<" max_cpu_error_m="<<error<<'\n';if(error>3e-5)return 2;}
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
