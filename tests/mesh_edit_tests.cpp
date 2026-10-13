#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/mesh_model.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void checkNear(double actual,double expected,double tolerance,const char* message){check(std::isfinite(actual)&&std::abs(actual-expected)<=tolerance,message);}
V parse(const char* source){V result;check(arkjson::parse(source,result),"test JSON");return result;}
V accepted(const V& response){if(response.find("isError")&&response.find("isError")->b){const auto* payload=response.find("structuredContent");throw std::runtime_error(payload&&payload->find("error")?payload->find("error")->str:"agent call failed");}check(response.find("structuredContent")!=nullptr,"result payload");return *response.find("structuredContent");}
V apply(ark::AgentSession& session,const V& commands){V args=V::makeObj();args.set("expected_revision",V::makeNum(session.revision));args.set("commands",commands);return accepted(session.call("doc_apply",args));}
const ark::Body& body(const ark::AgentSession& session,ark::u64 id){for(const auto& value:session.document.evalBodies)if(value.id==id)return value;throw std::runtime_error("body absent");}
const ark::Mesh& mesh(const ark::AgentSession& session,ark::u64 id){const auto& value=body(session,id);check(value.baked!=nullptr,"evaluated edit lacks baked mesh");return *value.baked;}
V inspect(ark::AgentSession& session,ark::u64 id){V args=V::makeObj();args.set("id",V::makeNum(id));return accepted(session.call("mesh_inspect",args));}
V cubePayload(double height=10){ark::Mesh result;for(ark::Vec3 p:std::initializer_list<ark::Vec3>{{0,0,0},{10,0,0},{10,10,0},{0,10,0},{0,0,height},{10,0,height},{10,10,height},{0,10,height}})result.addVertex(p);for(ark::u32 index:{0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5})result.idx.push_back(index);return ark::meshToPayload(result);}
void sourceCube(ark::AgentSession& session){session.suppressIterationHistory();V command=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("mesh_body"));command.set("name",V::makeStr("Indexed cube"));command.set("mesh",cubePayload());commands.arr.push_back(command);apply(session,commands);}
V editCommand(ark::AgentSession& session,const char* op,V selected,const char* dz="2"){
    V command=V::makeObj(),inputs=V::makeArr(),native=V::makeObj(),p=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("mesh_edit"));command.set("name",V::makeStr("Indexed edit"));inputs.arr.push_back(V::makeNum(1));command.set("inputs",inputs);native.set("op",V::makeStr(op));native.set(std::string(op)=="displace"?"vertices":"triangles",selected);native.set("source_topology",*inspect(session,1).find("topology_fingerprint"));command.set("native",native);p.set("dz",V::makeStr(dz));command.set("p",p);return command;
}
void rejectPreserves(ark::AgentSession& session,V command,const char* message,bool omitRevision=false){const auto revision=session.revision;const auto saved=ark::documentToJson(session.document);V args=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);args.set("commands",commands);if(!omitRevision)args.set("expected_revision",V::makeNum(revision));const V result=session.call("doc_apply",args);check(result.find("isError")&&result.find("isError")->b,message);check(session.revision==revision&&ark::documentToJson(session.document)==saved,"rejected mesh edit changes document/revision");}

void partialClosedPatchExtrusion(){
    ark::AgentSession session;sourceCube(session);V commands=V::makeArr();commands.arr.push_back(editCommand(session,"extrude",parse("[2]")));apply(session,commands);
    const auto& result=mesh(session,2);const auto audit=ark::analyzeMesh(result);check(result.vertCount()==11&&result.triCount()==18&&audit.closedOriented(),"single triangle patch extrusion preserves closed indexed boundary");checkNear(audit.signedVolume,1100,1e-6,"partial top triangle extrusion adds analytic prism volume");checkNear(ark::analyzeMesh(mesh(session,1)).signedVolume,1000,1e-6,"partial edit preserves source mesh");check(body(session,2).nativeData.find("mesh_authoritative")->b&&!body(session,2).nativeData.find("exact_geometry")->b&&!body(session,2).nativeData.find("self_intersections_checked")->b,"indexed edit retains triangle authority");
    const V report=inspect(session,2);check(report.find("closed_oriented_edges")->b&&report.find("mesh_model")->find("closed_boundary_preserved")->b,"closed boundary preservation is measured");
    ark::AgentSession face;sourceCube(face);commands.arr.clear();commands.arr.push_back(editCommand(face,"extrude",parse("[2,3]")));apply(face,commands);checkNear(ark::analyzeMesh(mesh(face,2)).signedVolume,1200,1e-6,"two-triangle face patch analytic added volume");check(mesh(face,2).vertCount()==12&&mesh(face,2).triCount()==20,"face patch duplicates four vertices and adds eight side triangles");
}
void displacementAndAssociativeTopology(){
    ark::AgentSession session;sourceCube(session);const std::string sourceFingerprint=inspect(session,1).find("topology_fingerprint")->str;V commands=V::makeArr();commands.arr.push_back(editCommand(session,"displace",parse("[4,5,6,7]")));apply(session,commands);const auto& displaced=mesh(session,2);check(displaced.vertCount()==8&&displaced.triCount()==12,"displacement preserves indexed topology");checkNear(ark::analyzeMesh(displaced).signedVolume,1200,1e-6,"top vertices displacement analytic volume");check(ark::meshTopologyFingerprint(displaced)==sourceFingerprint,"topology fingerprint ignores coordinate-only edit");
    V update=V::makeObj();update.set("op",V::makeStr("feature.edit"));update.set("id",V::makeNum(1));update.set("mesh",cubePayload(20));commands.arr={update};apply(session,commands);checkNear(ark::analyzeMesh(mesh(session,2)).signedVolume,2200,1e-6,"coordinate change follows retained indexed selection");check(inspect(session,1).find("topology_fingerprint")->str==sourceFingerprint,"coordinate-only source mutation preserves fingerprint");
    V changed=cubePayload(20),indices=*changed.find("indices");for(size_t i=0;i<3;++i)std::swap(indices.arr[i],indices.arr[i+3]);changed.set("indices",indices);update.set("mesh",changed);rejectPreserves(session,update,"triangle reorder silently rebases indexed selection");
    V bump=V::makeObj(),params=V::makeObj();bump.set("op",V::makeStr("feature.edit"));bump.set("id",V::makeNum(2));params.set("dz",V::makeStr("3"));bump.set("p",params);commands.arr={bump};apply(session,commands);checkNear(ark::analyzeMesh(mesh(session,2)).signedVolume,2300,1e-6,"stored displacement remains editable");
}
void invalidSelectionsRollback(){
    ark::AgentSession session;sourceCube(session);
    for(const char* selected:{"[]","[8]","[-1]","[1.5]","[4,4]"})rejectPreserves(session,editCommand(session,"displace",parse(selected)),"invalid vertex selection accepted");
    for(const char* selected:{"[]","[12]","[-1]","[0,0]"})rejectPreserves(session,editCommand(session,"extrude",parse(selected)),"invalid triangle selection accepted");
    rejectPreserves(session,editCommand(session,"displace",parse("[4]"),"0"),"zero displacement accepted");
    rejectPreserves(session,editCommand(session,"displace",parse("[4,5,6,7]"),"-10"),"collapsed closed volume accepted");
    rejectPreserves(session,editCommand(session,"extrude",parse("[0,1,2,3,4,5,6,7,8,9,10,11]")),"whole closed mesh extruded without patch boundary");
    V stale=editCommand(session,"displace",parse("[4]")),spec=*stale.find("native");spec.set("source_topology",V::makeStr("old-index-topology"));stale.set("native",spec);rejectPreserves(session,stale,"stale topology fingerprint accepted");
    V mixed=editCommand(session,"displace",parse("[4]"));spec=*mixed.find("native");spec.set("triangles",parse("[2]"));mixed.set("native",spec);rejectPreserves(session,mixed,"mixed vertex/triangle selection accepted");
    rejectPreserves(session,editCommand(session,"displace",parse("[4]")),"indexed edit accepted without observed revision",true);
    V commands=V::makeArr();commands.arr.push_back(editCommand(session,"displace",parse("[4]")));V args=V::makeObj();args.set("expected_revision",V::makeNum(0));args.set("commands",commands);const auto saved=ark::documentToJson(session.document);const V response=session.call("doc_apply",args);check(response.find("isError")->b&&ark::documentToJson(session.document)==saved,"stale expected_revision applied edit");
}
void persistenceAndVersionBoundary(){
    ark::AgentSession session;sourceCube(session);V commands=V::makeArr();commands.arr.push_back(editCommand(session,"extrude",parse("[2]")));apply(session,commands);const std::string saved=ark::documentToJson(session.document,false,false);V encoded;check(arkjson::parse(saved,encoded),"parse saved indexed recipe");check(encoded.find("version")->num==35,"mesh edit requires format35");ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),"load indexed mesh recipe");check(restored.features.back().type==ark::FeatureType::MeshEdit&&restored.features.back().nativeSpec.find("source_topology"),"selection intent persists");const auto evaluations=restored.recompute();check(evaluations.size()==2&&evaluations.back().ok&&restored.evalBodies.back().baked,"restored mesh edit recomputes");checkNear(ark::analyzeMesh(*restored.evalBodies.back().baked).signedVolume,1100,1e-6,"restored patch volume");
    encoded.set("version",V::makeNum(34));ark::Document unsupported;check(!ark::documentFromJson(encoded.dump(),unsupported,&error)&&error.find("35")!=std::string::npos,"old format34 silently accepts indexed selection recipe");
}
}
int main(){try{partialClosedPatchExtrusion();displacementAndAssociativeTopology();invalidSelectionsRollback();persistenceAndVersionBoundary();std::cout<<"indexed mesh edit tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<"indexed mesh edit: "<<e.what()<<'\n';return 1;}}
