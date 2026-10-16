#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "ark/mesh_analysis.hpp"
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
using V=arkjson::Value;
V json(const std::string& s){V value;if(!arkjson::parse(s,value))throw std::runtime_error("Invalid fixture JSON");return value;}
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
V accepted(const V& reply){if(const auto* error=reply.find("isError");error&&error->b)throw std::runtime_error(reply.dump());const auto* result=reply.find("structuredContent");check(result,"Missing result");return *result;}
V applyBatch(ark::AgentSession& session,const std::string& commands){auto args=json("{\"commands\":"+commands+"}");args.set("expected_revision",V::makeNum((double)session.revision));return accepted(session.call("doc_apply",args));}
void rejects(ark::AgentSession& session,const std::string& commands){const auto frozen=ark::documentToJson(session.document);const auto revision=session.revision;auto args=json("{\"commands\":"+commands+"}");args.set("expected_revision",V::makeNum((double)revision));const auto reply=session.call("doc_apply",args);check(reply.find("isError")&&reply.find("isError")->b,"Expected atomic rejection");check(session.revision==revision&&ark::documentToJson(session.document)==frozen,"Rejection changed source/history/revision");}
void core(){
    ark::AgentSession ordinary;applyBatch(ordinary,R"([{"op":"feature.add","type":"box","name":"Legacy","p":{"dx":5,"dy":5,"dz":5}}])");check(json(ark::documentToJson(ordinary.document)).find("version")->num<42,"Ordinary old recipe was upgraded without a need");
    ark::AgentSession s;applyBatch(s,R"([{"op":"param.set","name":"net_width","expr":"100"},{"op":"feature.add","type":"mesh_net","name":"Fabric","p":{"width":"net_width","depth":80,"rows":4,"columns":5,"radius":0.5,"sag":4,"weave":1},"appearance":{"base_color":[0.2,0.3,0.4],"opacity":0.8,"emission_color":[0.1,0.4,1],"emission_intensity":3,"bloom":1.2,"glow":24,"emission_effect":"pulse","emission_period":2}}])");
    check(s.document.evalBodies.size()==1,"Net not evaluated");const auto& body=s.document.evalBodies[0];check(!body.nativeData.find("brep_base64"),"Net acquired a false B-rep");const auto audit=ark::analyzeMesh(*body.baked);check(audit.triangles>0&&!audit.invalidIndices&&!audit.nonFiniteVertices&&!audit.degenerateTriangles&&!audit.inconsistentEdges&&!audit.boundaryEdges,"Net has invalid topology or uncapped strands");
    const auto before=s.document.evalBodies[0].baked->computeBBox();applyBatch(s,R"([{"op":"param.set","name":"net_width","expr":"140"}])");check(s.document.evalBodies[0].baked->computeBBox().mx.x>before.mx.x+39,"Net dimensions failed to recompute from expressions");
    const auto source=ark::documentToJson(s.document);auto decoded=json(source);check(decoded.find("version")->num==42,"New source has wrong format");ark::Document loaded;std::string error;const bool restored=ark::documentFromJson(source,loaded,&error);check(restored,error.c_str());const auto look=ark::resolvedAppearance(loaded,loaded.features[0]);check(look.emissionIntensity==3&&look.glow==24&&look.emissionEffect=="pulse","Emission did not persist");
    decoded.set("version",V::makeNum(41));check(!ark::documentFromJson(decoded.dump(),loaded,&error),"New recipe loaded under old format");
    const auto rev=s.revision;auto stale=json(R"({"expected_revision":0,"commands":[{"op":"feature.edit","id":1,"appearance":{"emission_intensity":1}}]})");check(s.call("doc_apply",stale).find("isError")->b&&s.revision==rev,"Stale light write accepted");
    for(const auto* patch:{"{\"glow\":65}","{\"bloom\":-1}","{\"emission_effect\":\"unknown\"}","{\"emission_period\":0}","{\"emission_color\":[1,0,2]}"})rejects(s,"[{\"op\":\"feature.edit\",\"id\":1,\"appearance\":"+std::string(patch)+"}]");
    rejects(s,R"([{"op":"feature.add","type":"mesh_net","p":{"radius":"1e-80"}}])");
    rejects(s,R"([{"op":"feature.edit","id":1,"p":{"rows":1}}])");rejects(s,R"([{"op":"feature.edit","id":1,"p":{"radius":50}}])");
    for(const char* topic:{"wood","networks","lighting"}){auto args=V::makeObj();args.set("topic",V::makeStr(topic));accepted(s.call("agent_onboard",args));}
    accepted(s.call("doc_undo",V::makeObj()));check(s.document.params[0].value==100,"Net undo failed");accepted(s.call("doc_redo",V::makeObj()));check(s.document.params[0].value==140,"Net redo failed");
    const auto lightReceipt=applyBatch(s,R"([{"op":"light_up","id":1,"appearance":{"glow":32}}])");const auto retained=ark::resolvedAppearance(s.document,s.document.features[0]);check(retained.emissionIntensity==3&&retained.emissionEffect=="pulse"&&retained.emissionColor.x==0.1&&retained.glow==32&&retained.opacity==0.8&&retained.baseColor.x==0.2&&!lightReceipt.find("geometry_recomputed")->b,"Independent glow control reset emission or rebuilt geometry");
    rejects(s,R"([{"op":"light_up","id":1,"appearance":{"base_color":[1,0,0]}}])");
    const auto directory=std::filesystem::temp_directory_path()/("ark-product-030d-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(std::filesystem::create_directory(directory),"Cannot reserve synthetic persistence directory");
    struct Cleanup {std::filesystem::path directory;~Cleanup(){std::error_code error;std::filesystem::remove(directory/"synthetic.ark",error);std::filesystem::remove(directory,error);}} cleanup{directory};
    const auto path=(directory/"synthetic.ark").string();check(ark::saveDocument(s.document,path.c_str()),"Binary .ark save failed");ark::Document binary;const bool binaryLoaded=ark::loadDocument(path.c_str(),binary,&error);check(binaryLoaded,error.c_str());check(ark::documentToJson(binary)==ark::documentToJson(s.document),"Binary .ark lost source42 or history");
    applyBatch(s,R"([{"op":"assembly.add","name":"Net workspace","feature":0},{"op":"assembly.add","name":"Fabric occurrence","feature":"Fabric","parent":"Net workspace"}])");
    V checkout=json(R"({"action":"checkout","node":"Net workspace"})");checkout.set("expected_revision",V::makeNum((double)s.revision));const auto scoped=accepted(s.call("design_containers",checkout));check(scoped.find("document")->find("version")->num==42,"Container downgraded new source recipes");ark::Document child;const bool childLoaded=ark::documentFromJson(scoped.find("document")->dump(),child,&error);check(childLoaded,error.c_str());
    const auto queried=accepted(s.call("query_batch",json(R"({"queries":[{"tool":"wood_inspect","args":{},"fields":["schema","items"]}]})")));check(!queried.find("results")->arr[0].find("isError")->b,"Wood cutlist cannot share a read-only query batch");
}
#ifdef ARK_PRODUCT_NATIVE
double volume(ark::AgentSession& session,int id){auto args=V::makeObj();args.set("id",V::makeNum(id));auto result=accepted(session.call("native_inspect",args));check(result.find("report")->find("brep_valid")->b,"Invalid B-rep");return result.find("report")->find("volume_mm3")->num;}
void native(){
    ark::AgentSession wood;applyBatch(wood,R"([{"op":"feature.add","type":"native_solid","name":"Panel","native":{"op":"wood","grain_axis":"x","joinery":[{"kind":"groove","x":10,"y":0,"width":5,"depth":80,"cut_depth":4}]},"p":{"width":100,"depth":80,"height":18}},{"op":"assembly.add","name":"Panels","feature":"Panel","visible":false,"pattern":{"type":"linear","count":3,"dx":120}}])");
    check(std::abs(volume(wood,1)-(100*80*18-5*80*4))<1e-6,"Wood groove volume differs");const auto cutlist=accepted(wood.call("wood_inspect",V::makeObj()));const auto& items=cutlist.find("items")->arr;check(items.size()==1&&items[0].find("quantity")->num==3&&items[0].find("height_mm")->num==18,"Wood physical quantities or dimensions wrong");
    rejects(wood,R"([{"op":"feature.edit","id":1,"native":{"op":"wood","grain_axis":"x","joinery":[{"kind":"pocket","x":1,"y":1,"width":4,"depth":4,"cut_depth":18}]}}])");
    ark::AgentSession vents;applyBatch(vents,R"([{"op":"feature.add","type":"native_solid","name":"Plastic grille","native":{"op":"vent","kind":"rectangle"},"p":{"width":100,"depth":80,"height":3,"rows":2,"columns":3,"pitch_x":20,"pitch_y":20,"opening_width":10,"opening_depth":5,"x":15,"y":15}}])");check(std::abs(volume(vents,1)-(100*80*3-6*10*5*3))<1e-6,"Vent removed incorrect volume");rejects(vents,R"([{"op":"feature.edit","id":1,"p":{"pitch_x":9}}])");
    for(const char* kind:{"circle","slot"}){auto command=json(R"({"op":"feature.edit","id":1,"native":{"op":"vent"}})");auto native=*command.find("native");native.set("kind",V::makeStr(kind));command.set("native",native);if(std::string(kind)=="circle")command.set("p",json(R"({"opening_width":5,"opening_depth":5})"));else command.set("p",json(R"({"opening_width":10,"opening_depth":5})"));applyBatch(vents,"["+command.dump()+"]");check(volume(vents,1)>0,"Round vent invalid");}
    ark::AgentSession sheet;applyBatch(sheet,R"([{"op":"sheet.create","name":"Sheet grille","p":{"width":80,"thickness":2,"k_factor":0.4},"base":{"id":"base","length":100}},{"op":"sheet.vent","id":1,"name":"Vents","panel":"base","rows":2,"columns":3,"pitch_x":20,"pitch_y":20,"opening":{"kind":"window","x":15,"y":15,"width":10,"height":5}}])");check(std::abs(volume(sheet,1)-(100*80*2-6*10*5*2))<1e-5,"Sheet vent exact volume wrong");check(sheet.document.features[0].nativeSpec.find("openings")->arr.size()==6,"Panel-owned openings missing");
    applyBatch(sheet,R"([{"op":"sheet.vent","id":1,"name":"Round_vents","panel":"base","rows":2,"columns":1,"pitch_x":20,"pitch_y":20,"opening":{"kind":"circle","x":75,"y":15,"radius":2}}])");
    applyBatch(sheet,R"([{"op":"sheet.vent","id":1,"name":"Slot_vent","panel":"base","rows":1,"columns":1,"pitch_x":20,"pitch_y":20,"opening":{"kind":"slot","x":75,"y":55,"length":12,"width":4}}])");check(std::abs(volume(sheet,1)-(16000-600-64-24*std::acos(-1.)))<1e-5,"Circle/slot sheet vents removed incorrect exact volume");
    rejects(sheet,R"([{"op":"sheet.vent","id":1,"name":"Vents","panel":"base","rows":1,"columns":1,"pitch_x":20,"pitch_y":20,"opening":{"kind":"window","x":15,"y":15,"width":10,"height":5}}])");
    // A face LED must retain exact source custody and leave native geometry unchanged.
    const auto& topology=*vents.document.evalBodies[0].nativeData.find("topology");const auto digest=topology.find("brep_sha256")->str;std::string face;for(const auto& ref:topology.find("references")->arr)if(ref.find("kind")->str=="face"&&ref.find("status")->str=="bound"){face=ref.find("name")->str;break;}
    V finish=json(R"({"op":"face_appearance.set","id":1,"appearance":{"roughness":0.3}})");finish.set("face",V::makeStr(face));finish.set("source_topology",V::makeStr(digest));applyBatch(vents,"["+finish.dump()+"]");
    const double before=volume(vents,1);V c=json(R"({"op":"light_up","id":1,"appearance":{"emission_intensity":5,"emission_color":[1,0.1,0.05]}})");c.set("face",V::makeStr(face));c.set("source_topology",V::makeStr(digest));applyBatch(vents,"["+c.dump()+"]");check(volume(vents,1)==before&&vents.document.features[0].faceAppearances[0].appearance.find("roughness")->num==0.3,"Face light changed geometry or discarded the existing finish");c.set("source_topology",V::makeStr(std::string(64,'0')));rejects(vents,"["+c.dump()+"]");
}
#endif
}
int main(){try{core();
#ifdef ARK_PRODUCT_NATIVE
native();
#endif
std::cout<<"030D product recipes, persistence, revision guards and authority passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
