#include "ark/agent.hpp"
#include "ark/mesh_model.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
V parse(const char* source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Bad test JSON");return value;}
template<class F>void rejects(F action,const char* expected){try{action();}catch(const std::exception& e){if(std::string(e.what()).find(expected)!=std::string::npos)return;throw std::runtime_error(std::string("Unexpected rejection: ")+e.what());}throw std::runtime_error(std::string("Expected rejection: ")+expected);}
ark::Mesh mesh(std::initializer_list<ark::Vec3> points,std::initializer_list<ark::u32> indices){ark::Mesh result;for(auto p:points)result.addVertex(p);result.idx=indices;result.computeNormals();return result;}
ark::Mesh quad(double height=0){return mesh({{0,0,height},{2,0,height},{2,2,height},{0,2,height}},{0,1,2,0,2,3});}
ark::Mesh tetra(){return mesh({{0,0,0},{2,0,0},{0,2,0},{0,0,2}},{0,2,1,0,1,3,1,2,3,2,0,3});}
ark::Mesh openPyramid(){return mesh({{-1,-1,0},{1,-1,.2},{1,1,0},{-1,1,-.3},{0,0,-2}},{0,1,4,1,2,4,2,3,4,3,0,4});}
ark::Mesh grid(int n,bool bump=false){ark::Mesh m;for(int y=0;y<=n;++y)for(int x=0;x<=n;++x)m.addVertex({(double)x,(double)y,bump&&x==n/2&&y==n/2?1.:0.});for(int y=0;y<n;++y)for(int x=0;x<n;++x){const ark::u32 a=y*(n+1)+x,b=a+1,c=a+n+1,d=c+1;m.addTri3(a,b,c);m.addTri3(b,d,c);}m.computeNormals();return m;}
ark::Feature edit(const ark::Mesh& source,const char* op){ark::Feature f;f.id=2;f.type=ark::FeatureType::MeshEdit;f.name="mesh 031B";f.inputs={1};f.nativeSpec=V::makeObj();f.nativeSpec.set("op",V::makeStr(op));f.nativeSpec.set("source_topology",V::makeStr(ark::meshTopologyFingerprint(source)));return f;}
ark::Body evaluate(const ark::Mesh& source,const ark::Feature& editFeature,bool exact=false){ark::Feature f;f.type=ark::FeatureType::MeshBody;f.sourceData=ark::meshToPayload(source);ark::Body body=ark::evaluateMeshFeature(f,{}, {},{});body.id=1;if(exact)body.nativeData.set("brep_base64",V::makeStr("explicit_sampling_test_marker"));std::vector<ark::Body> preceding;preceding.push_back(std::move(body));return ark::evaluateMeshFeature(editFeature,{},preceding,{});}
V accepted(const V& response){if(const auto* error=response.find("isError"))if(error->b){const auto* data=response.find("structuredContent");throw std::runtime_error(data&&data->find("error")?data->find("error")->str:"Tool failed");}const auto* data=response.find("structuredContent");if(!data)throw std::runtime_error("Missing structuredContent");return *data;}
V addCommand(const ark::Mesh& m){V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("mesh_body"));command.set("mesh",ark::meshToPayload(m));return command;}
V editCommand(const ark::Feature& feature){V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("mesh_edit"));command.set("name",V::makeStr(feature.name));command.set("native",feature.nativeSpec);V inputs=V::makeArr();inputs.arr.push_back(V::makeNum(1));command.set("inputs",inputs);V p=V::makeObj();for(const auto& entry:feature.p)p.set(entry.first,V::makeStr(entry.second));command.set("p",p);return command;}
V apply(ark::AgentSession& session,std::initializer_list<V> commands){V args=V::makeObj(),batch=V::makeArr();for(auto command:commands)batch.arr.push_back(command);args.set("commands",batch);args.set("expected_revision",V::makeNum((double)session.revision));return accepted(session.call("doc_apply",args));}

void intersections(){
    auto clean=quad();auto report=ark::inspectMeshSelfIntersections(clean);check(report.find("self_intersections_checked")->b&&report.find("self_intersection_pairs")->num==0,"Adjacent planar triangles are legal");
    check(ark::inspectMeshSelfIntersections(tetra()).find("self_intersection_pairs")->num==0,"Closed tetrahedron adjacency is legal");
    const auto cross=mesh({{-2,-2,0},{2,-2,0},{0,2,0},{0,-1,-1},{0,1,1},{0,1,-1}},{0,1,2,3,4,5});
    report=ark::inspectMeshSelfIntersections(cross);check(report.find("self_intersection_pairs")->num==1&&report.find("check_complete")->b,"Noncoplanar actual crossing diagnosed");check(report.find("self_intersection_examples")->arr.size()==1,"Crossing identifies triangles");
    const auto coplanar=mesh({{-2,0,0},{2,0,0},{0,3,0},{-2,2,0},{2,2,0},{0,-1,0}},{0,1,2,3,4,5});
    check(ark::inspectMeshSelfIntersections(coplanar).find("self_intersection_pairs")->num==1,"Coplanar crossing with no interior vertices diagnosed");
    const auto folded=mesh({{0,0,0},{2,0,0},{0,2,0},{1,1,0}},{0,1,2,1,0,3});
    check(ark::inspectMeshSelfIntersections(folded).find("self_intersection_pairs")->num==1,"Shared edge cannot hide coplanar overlapping faces");
    const auto vertexTouch=mesh({{0,0,0},{1,0,0},{0,1,0},{-1,0,1},{0,-1,1}},{0,1,2,0,3,4});
    check(ark::inspectMeshSelfIntersections(vertexTouch).find("self_intersection_pairs")->num==0,"Shared indexed vertex contact is legal");
    const auto sharedVertexOverlap=mesh({{0,0,0},{3,0,0},{0,3,0},{2,1,0},{1,2,0}},{0,1,2,0,3,4});
    check(ark::inspectMeshSelfIntersections(sharedVertexOverlap).find("self_intersection_pairs")->num==1,"Shared vertex cannot hide coplanar overlap");
    auto separated=cross;for(size_t v=3;v<separated.vertCount();++v)separated.pos[3*v]+=10;
    check(ark::inspectMeshSelfIntersections(separated).find("self_intersection_pairs")->num==0,"AABB rejection separates actual triangles");
    auto bad=clean;bad.idx[0]=999;rejects([&]{ark::inspectMeshSelfIntersections(bad);},"requires finite");
    bad=clean;bad.idx={0,0,1};rejects([&]{ark::inspectMeshSelfIntersections(bad);},"requires finite");
    // A complete diagnosis rejects pair exhaustion, rather than returning the
    // already found contacts as evidence of a completed query.
    ark::Mesh crowded;for(int i=0;i<1500;++i){const auto v=(ark::u32)crowded.vertCount();crowded.addVertex({0,0,0});crowded.addVertex({1,0,0});crowded.addVertex({0,1,0});crowded.addTri3(v,v+1,v+2);}rejects([&]{ark::inspectMeshSelfIntersections(crowded);},"1000000 candidate");
}

void remeshing(){
    const auto source=tetra();auto f=edit(source,"remesh");f.p={{"target_length","1.5"},{"iterations","3"}};auto body=evaluate(source,f);const auto& result=*body.baked;const auto audit=ark::analyzeMesh(result);
    check(result.triCount()>source.triCount()&&audit.closedOriented(),"Refinement produces closed triangle topology");check(std::abs(audit.signedVolume-ark::analyzeMesh(source).signedVolume)<1e-6,"Refinement retains measured volume");
    for(size_t i=0;i<source.pos.size();++i)check(source.pos[i]==result.pos[i],"All original crease vertices remain exact");
    check(body.nativeData.find("target_reached")->b&&body.nativeData.find("boundary_and_crease_geometry_preserved")->b,"Refinement reports edge target and protected geometry");check(body.nativeData.find("self_intersections_checked")->b,"Refinement checks actual triangle contacts");
    const auto sheet=quad();f=edit(sheet,"remesh");f.p={{"target_length",".8"},{"iterations","3"}};body=evaluate(sheet,f);check(ark::analyzeMesh(*body.baked).boundaryEdges>4,"Open border subdivides conformingly");for(size_t i=0;i<sheet.pos.size();++i)check(sheet.pos[i]==body.baked->pos[i],"Open border original vertices retained");
    f=edit(sheet,"remesh");f.p={{"target_length",".01"},{"iterations","1"}};body=evaluate(sheet,f);check(!body.nativeData.find("target_reached")->b,"Iteration limit explicitly reports unmet target");
    f.p["iterations"]="1.5";rejects([&]{evaluate(sheet,f);},"integer");f.p["iterations"]="6";rejects([&]{evaluate(sheet,f);},"4096 output");
    f=edit(sheet,"remesh");f.p={{"target_length","1"}};f.nativeSpec.set("source_topology",V::makeStr("stale"));rejects([&]{evaluate(sheet,f);},"topology changed");
    f=edit(sheet,"remesh");f.p={{"target_length","1"}};rejects([&]{evaluate(sheet,f,true);},"p.sample=1");f.p["sample"]="1";check(evaluate(sheet,f,true).nativeData.find("sampled_from_brep")->b,"Exact input requires explicit sampling and reports authority");
    const auto pinched=mesh({{0,0,0},{2,0,0},{0,2,0},{0,0,2},{-2,0,0},{0,-2,0},{0,0,-2}},{0,2,1,0,1,3,1,2,3,2,0,3,0,4,5,0,6,4,4,6,5,5,6,0});f=edit(pinched,"remesh");f.p={{"target_length","1"}};rejects([&]{evaluate(pinched,f);},"disconnected vertex fans");
}

void simplification(){
    const auto source=grid(3);auto f=edit(source,"simplify");f.p={{"ratio",".5"},{"max_error","0.000001"}};auto body=evaluate(source,f);
    check(body.baked->triCount()<source.triCount(),"Interior simplification reduces geometry");check(ark::analyzeMesh(*body.baked).boundaryEdges==ark::analyzeMesh(source).boundaryEdges,"Simplification retains boundary edge count");check(body.nativeData.find("sampled_bidirectional_deviation_mm")->num<1e-6&&!body.nativeData.find("global_hausdorff_bound")->b,"Measured sampled deviation is distinguished from Hausdorff");check(body.nativeData.find("error_cap_satisfied")->b,"Explicit sampled error cap checked");
    const auto bump=grid(4,true);f=edit(bump,"simplify");f.p={{"ratio",".05"},{"max_error","0.0000001"}};rejects([&]{evaluate(bump,f);},"exceeds explicit max_error");
    f=edit(source,"simplify");f.p={{"ratio",".5"}};rejects([&]{evaluate(source,f);},"max_error");f.p["max_error"]="-1";rejects([&]{evaluate(source,f);},"outside supported range");
    const auto minimal=tetra();f=edit(minimal,"simplify");f.p={{"ratio",".05"},{"max_error","100"}};body=evaluate(minimal,f);check(!body.nativeData.find("target_reached")->b,"Protected minimal topology reports unmet target");
}

void fillingAndBridge(){
    const auto source=openPyramid();auto f=edit(source,"fill_nonplanar");f.nativeSpec.set("loop",parse("[0,1,2,3]"));f.nativeSpec.set("method",V::makeStr("centroid_fan"));f.nativeSpec.set("require_closed",V::makeBool(true));auto body=evaluate(source,f);
    ark::Feature planar;planar.type=ark::FeatureType::MeshFill;planar.inputs={1};rejects([&]{evaluate(source,planar);},"deviates");
    check(ark::analyzeMesh(*body.baked).closedOriented()&&body.baked->triCount()==8,"Explicit nonplanar fill closes selected boundary");check(body.nativeData.find("fill_model")->str=="explicit_nonplanar_centroid_fan"&&body.nativeData.find("boundary_mean_plane_deviation_mm")->num>.01,"Nonplanar fan model and measured nonplanarity disclosed");
    auto invalid=f;invalid.nativeSpec.set("method",V::makeStr("automatic"));rejects([&]{evaluate(source,invalid);},"explicit method");invalid=f;invalid.nativeSpec.set("loop",parse("[3,2,1,0]"));rejects([&]{evaluate(source,invalid);},"oriented boundary");invalid=f;invalid.nativeSpec.set("loop",parse("[0,1,1,3]"));rejects([&]{evaluate(source,invalid);},"duplicate");
    // A lone triangle is not an open volume hole: its opposing fan would
    // overlap the existing sheet, so complete geometry verification rejects.
    const auto lone=mesh({{0,0,0},{2,0,0},{0,2,0}},{0,1,2});f=edit(lone,"fill_nonplanar");f.nativeSpec.set("loop",parse("[0,1,2]"));f.nativeSpec.set("method",V::makeStr("centroid_fan"));rejects([&]{evaluate(lone,f);},"self-intersections");
    const auto caps=mesh({{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},{-1,-1,2},{1,-1,2},{1,1,2},{-1,1,2}},{0,2,1,0,3,2,4,5,6,4,6,7});f=edit(caps,"bridge");f.nativeSpec.set("loop_a",parse("[0,3,2,1]"));f.nativeSpec.set("loop_b",parse("[4,5,6,7]"));f.nativeSpec.set("require_closed",V::makeBool(true));body=evaluate(caps,f);check(ark::analyzeMesh(*body.baked).closedOriented()&&std::abs(ark::analyzeMesh(*body.baked).signedVolume-8)<1e-6,"Explicit bridge produces closed measured box volume");
    invalid=f;invalid.nativeSpec.set("loop_b",parse("[4,7,6,5]"));rejects([&]{evaluate(caps,invalid);},"oriented boundary");invalid=f;invalid.nativeSpec.set("loop_b",parse("[4,6,7]"));rejects([&]{evaluate(caps,invalid);},"oriented boundary");
    // Correct boundary order but twisted start correspondence intersects.
    invalid=f;invalid.nativeSpec.set("loop_b",parse("[6,7,4,5]"));rejects([&]{evaluate(caps,invalid);},"self-intersections");
}

void shrinkwrapping(){
    const auto source=quad(1),reference=quad();auto f=edit(source,"shrinkwrap");f.nativeSpec.set("reference_mesh",ark::meshToPayload(reference));f.p={{"max_distance","2"}};auto body=evaluate(source,f);for(size_t v=0;v<body.baked->vertCount();++v)check(body.baked->pos[3*v+2]==0,"Shrinkwrap projects to retained triangle surface");check(body.nativeData.find("reference_immutable")->b&&body.nativeData.find("maximum_vertex_displacement_mm")->num==1,"Shrinkwrap reports immutable authority and displacement");
    f.p["strength"]=".5";body=evaluate(source,f);check(body.baked->pos[2]==.5f,"Shrinkwrap strength controls projection");f.nativeSpec.set("vertices",parse("[0]"));body=evaluate(source,f);check(body.baked->pos[2]==.5f&&body.baked->pos[5]==1,"Shrinkwrap explicit vertex region retained");
    f.p["max_distance"]=".1";rejects([&]{evaluate(source,f);},"exceeds max_distance");f.p["max_distance"]="2";f.nativeSpec.set("vertices",parse("[0,0]"));rejects([&]{evaluate(source,f);},"invalid/duplicate");
}

void persistenceAndRollback(){
    const auto source=quad(1);ark::AgentSession session;apply(session,{addCommand(source)});auto f=edit(source,"shrinkwrap");f.nativeSpec.set("reference_mesh",ark::meshToPayload(quad()));f.p={{"max_distance","2"}};apply(session,{editCommand(f)});
    V serialized;const auto text=ark::documentToJson(session.document,true,false);check(arkjson::parse(text,serialized),"031B document JSON parses");check(serialized.find("version")->num==43,"New mesh semantics conditionally require source43");ark::Document restored;std::string error;check(ark::documentFromJson(text,restored,&error),error.c_str());restored.recompute();check(restored.evalBodies.size()==2&&restored.evalBodies.back().baked->pos[2]==0,"Immutable reference projection survives JSON roundtrip and recompute");check(restored.features.back().nativeSpec.find("reference_mesh")->find("positions")->arr[2].num==0,"Embedded reference coordinates persist");
    serialized.set("version",V::makeNum(42));check(!ark::documentFromJson(serialized.dump(),restored,&error),"Old format rejects new mesh semantics");
    const auto before=ark::documentToJson(session.document,true,false);const auto revision=session.revision;auto bad=f;bad.p["max_distance"]=".1";
    V param=V::makeObj();param.set("op",V::makeStr("param.set"));param.set("name",V::makeStr("must_rollback"));param.set("expr",V::makeStr("100"));rejects([&]{apply(session,{param,editCommand(bad)});},"exceeds max_distance");check(session.revision==revision&&ark::documentToJson(session.document,true,false)==before,"Failed projection rolls back whole multi-command batch");
    bad=f;bad.nativeSpec.set("source_topology",V::makeStr("stale"));rejects([&]{apply(session,{editCommand(bad)});},"topology changed");check(session.revision==revision,"Stale topology does not publish revision");
    V query=V::makeObj();query.set("id",V::makeNum(2));query.set("self_intersections",V::makeBool(true));const auto inspected=accepted(session.call("mesh_inspect",query));check(inspected.find("self_intersection_diagnosis")&&inspected.find("self_intersection_diagnosis")->find("check_complete")->b,"Read-only mesh query publishes explicit complete diagnosis");check(session.revision==revision,"Self diagnosis preserves revision");
}
}

int main(){try{auto run=[](const char* name,auto test){try{test();}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}};run("intersections",intersections);run("remeshing",remeshing);run("simplification",simplification);run("fill/bridge",fillingAndBridge);run("shrinkwrap",shrinkwrapping);run("persistence/rollback",persistenceAndRollback);std::cout<<"031B mesh capabilities passed\n";return 0;}catch(const std::exception& e){std::cerr<<"031B mesh capabilities failed: "<<e.what()<<'\n';return 1;}}
