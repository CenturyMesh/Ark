#include "ark/agent.hpp"
#include "ark/assistant.hpp"
#include <iostream>
#include <stdexcept>
namespace {using namespace ark;using V=arkjson::Value;int checks=0;
void check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
V json(const char* text){V out;if(!arkjson::parse(text,out))throw std::runtime_error("fixture JSON");return out;}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("missing ")+key);return *found;}
V accepted(const V& response){if(field(response,"isError").b)throw std::runtime_error(response.dump());return field(response,"structuredContent");}
bool node(const V& graph,const char* key){for(const auto& value:field(graph,"nodes").arr)if(field(value,"key").str==key)return true;return false;}
bool relation(const V& graph,const char* name){for(const auto& edge:field(graph,"edges").arr)if(field(edge,"relation").str==name)return true;return false;}
void domainGraph(){
    // Read-only authoring fixture: graph receipts never pretend it was evaluated.
    AgentSession s;Feature board;board.type=FeatureType::PCB;board.name="Control board";board.nativeSpec=json(R"({"outline":[[0,0],[40,0],[40,30],[0,30]],"thickness":1.6,"components":[{"ref":"U1","value":"Controller envelope","package":"declared","x":20,"y":15,"width":4,"height":4,"height_mm":2}],"pads":[{"name":"J1.1","net":"PWR","layer":"top","x":10,"y":10,"width":2,"height":2}]})");s.document.addFeature(board);
    Feature cable;cable.type=FeatureType::CableRoute;cable.name="Smooth harness";cable.nativeSpec=json(R"({"route_mode":"spline","start":{"node":1,"copy":0,"reference":"face.z_max","connector":"J1","pin":"1","net":"PWR"},"end":{"point":[80,0,10],"connector":"J2","pin":"A","net":"PWR"},"waypoints":[[30,5,8]],"profile":{"type":"ribbon","width":4,"thickness":0.4,"up":[0,0,1]},"bend_radius":8})");cable.part=json(R"({"part_number":"CABLE-01","description":"Declared interconnect","procurement":"buy","supplier":"TraceParts candidate","supplier_part_number":"EXAMPLE","source_url":"https://www.traceparts.com/en/product/example","datasheet_url":"https://www.traceparts.com/en/document/example","sourcing_status":"unresolved"})");s.document.addFeature(cable);
    AssemblyNode placement;placement.id=1;placement.feature=1;placement.name="Board placement";s.document.assembly.push_back(placement);s.document.nextAssemblyId_=2;
    const auto before=documentToJson(s.document);const auto graph=accepted(s.call("context_graph",json(R"({"keys":["feature:2"],"depth":2,"limit":32})")));
    check(node(graph,"endpoint:feature:2:start")&&node(graph,"endpoint:feature:2:end")&&node(graph,"assembly:1"),"Cable endpoints and explicit occurrence binding are navigable");
    check(node(graph,"vendor:www.traceparts.com")&&relation(graph,"sourced_from")&&relation(graph,"documented_by"),"Declared supplier/source links are discoverable");
    check(relation(graph,"routed_from")&&relation(graph,"routed_to")&&relation(graph,"attaches_to"),"Endpoint link direction and authored basis are explicit");
    check(graph.dump().find("approval/account status is not inferred")!=std::string::npos&&!field(graph,"manufacturing_qualified").b,"Graph distinguishes provenance from vendor approval and manufacturing qualification");
    const auto pcb=accepted(s.call("context_graph",json(R"({"keys":["feature:1"],"depth":1,"limit":32})")));
    check(node(pcb,"pcb_component:feature:1:U1")&&node(pcb,"pcb_net:feature:1:PWR"),"PCB components and board-scoped net labels are navigable");
    check(documentToJson(s.document)==before&&s.revision==0,"Domain graph discovery does not evaluate or mutate the source");
    s.document.features[1].part=json(R"({"part_number":"CABLE-01","description":"Private signed URL excluded","source_url":"https://www.traceparts.com/en/model?token=DO_NOT_EXPOSE"})");
    const auto privateGraph=accepted(s.call("context_graph",json(R"({"keys":["feature:2"],"depth":2,"limit":32})")));check(privateGraph.dump().find("DO_NOT_EXPOSE")==std::string::npos&&!relation(privateGraph,"sourced_from"),"Credential-bearing source URLs never enter graph facts");
}
void discovery(){
    AgentSession s;const auto tools=AgentSession::tools();check(tools.arr.size()==98,"New vendor policy tool is discoverable");
    check(assistantToolReadOnly(tools,"library_vendors",json(R"({"operation":"list"})"))&&!assistantToolReadOnly(tools,"library_vendors",json(R"({"operation":"update"})")),"Role permissions distinguish policy reads from writes");
    auto plastics=accepted(s.call("agent_onboard",json(R"({"topic":"plastics","detail":"guide"})")));check(plastics.dump().find("Hollow walls")!=std::string::npos&&!field(plastics,"document_state_included").b,"Plastics uses natural supported part-design guidance");
    auto vendors=accepted(s.call("agent_onboard",json(R"({"topic":"vendors","detail":"guide"})")));check(vendors.dump().find("partner-required")!=std::string::npos&&vendors.dump().find("policy revision")!=std::string::npos,"Vendor onboarding discloses authentication and revision boundaries");
    auto batch=accepted(s.call("query_batch",json(R"({"queries":[{"tool":"library_vendors","args":{"operation":"list","root":"out/private/product-context-035b-vendors"}}]})")));check(!field(field(batch,"results").arr[0],"isError").b,"Read-only batches can inspect local policy");
    check(field(s.call("query_batch",json(R"({"queries":[{"tool":"library_vendors","args":{"operation":"update","vendor":"misumi","approved":false,"expected_revision":1}}]})")),"isError").b,"Read batches cannot mutate vendor policy");
}
}
int main(){try{domainGraph();discovery();std::cout<<checks<<" product context 035B checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
