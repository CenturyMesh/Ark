#include "ark/agent.hpp"
#include "ark/collision.hpp"
#include "ark/skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using V=arkjson::Value;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
V json(const std::string& text){V out;if(!arkjson::parse(text,out))throw std::runtime_error("Invalid test fixture JSON");return out;}
bool failed(const V& value){auto* error=value.find("isError");return error&&error->b;}
V accepted(const V& value){if(failed(value))throw std::runtime_error(value.dump());auto* data=value.find("structuredContent");if(!data)throw std::runtime_error("Missing tool result");return *data;}
V apply(ark::AgentSession& session,V commands){V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum((double)session.revision));return accepted(session.call("doc_apply",args));}
V sourceCommand(const std::string& text){V command=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("skeleton.set"));command.set("source",V::makeStr(text));commands.arr.push_back(command);return commands;}
std::string program(){return "input span = 120\ninput copies = 4\nrelation pitch = span / (copies - 1)\narray stations[copies] = [index * pitch, 0, span / 30]\nrelation station = stations[2].x\npattern Ribs from Seed count copies step [pitch, 0, 0]\n";}
void seed(ark::AgentSession& s){apply(s,json(R"([{"op":"feature.add","type":"mesh_body","name":"Seed","mesh":{"positions":[0,0,0,1,0,0,1,1,0,0,1,0,0,0,1,1,0,1,1,1,1,0,1,1],"indices":[0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7]}}])"));}
void unchangedOnFailure(ark::AgentSession& s,const char* tool,V args){const auto revision=s.revision;const auto before=ark::documentToJson(s.document,false,false);check(failed(s.call(tool,args)),"Expected rejection");check(s.revision==revision&&ark::documentToJson(s.document,false,false)==before,"Rejected request changed document");}
void tests(){
    const auto tools=ark::AgentSession::tools();bool nativeSchema=false,topicSchema=false;
    for(const auto& tool:tools.arr)if(tool.find("name")->str=="agent_onboard")for(const auto& topic:tool.find("inputSchema")->find("properties")->find("topic")->find("enum")->arr)if(topic.str=="skeleton")topicSchema=true;
    for(const auto& tool:tools.arr)if(tool.find("name")->str=="doc_apply"){const auto* properties=tool.find("inputSchema")->find("properties")->find("commands")->find("items")->find("properties");const auto* native=properties->find("native");nativeSchema=properties->find("source")&&native&&native->find("properties")&&native->find("properties")->find("controls")&&native->find("properties")->find("source_topology");}
    check(nativeSchema,"Advertised source/native controls schema survived command schema construction");
    check(topicSchema,"Onboarding schema advertises Skeleton topic");
    ark::AgentSession guideSession;auto guide=accepted(guideSession.call("agent_onboard",json(R"({"topic":"skeleton"})")));check(guide.find("rules")&&guide.find("example_source"),"Skeleton source onboarding available");
    ark::AgentSession s;s.suppressFailureCapture();seed(s);const auto baseline=ark::documentToJson(s.document,false,false);
    V compile=V::makeObj();compile.set("source",V::makeStr(program()));compile.set("expected_revision",V::makeNum((double)s.revision));auto report=accepted(s.call("skeleton_compile",compile));check(report.find("valid")->b&&!report.find("geometry_evaluated")->b,"Read-only compile contract");check(ark::documentToJson(s.document,false,false)==baseline,"Compile mutated source");
    V missing=V::makeObj();missing.set("commands",sourceCommand(program()));unchangedOnFailure(s,"doc_apply",missing);
    apply(s,sourceCommand(program()));check(s.document.features.size()==2,"Pattern generated");const auto patternId=s.document.features.back().id;
    check(s.document.getParam("station")==80&&s.document.getParam("stations_3_x")==120&&s.document.getParam("stations_count")==4,"Array expressions resolved");
    check(s.document.find(patternId)->p.at("count")=="copies","Pattern retained count expression");check(s.document.evalBodies.back().bbox().mx.x>120.9,"Pattern evaluated the span");
    apply(s,json(R"([{"op":"param.set","name":"span","expr":"240"},{"op":"param.set","name":"copies","expr":"5"}])"));
    check(s.document.features.back().id==patternId&&s.document.getParam("stations_count")==5&&s.document.getParam("station")==120,"Input changes keep pattern identity and update arrays");
    apply(s,sourceCommand(program()+"relation wall = span / 40\n"));check(s.document.getParam("span")==240&&s.document.getParam("wall")==6,"Unrelated source edit keeps input override");
    auto replacement=program();replacement.replace(replacement.find("span = 120"),10,"span = 180");apply(s,sourceCommand(replacement));check(s.document.getParam("span")==180&&s.document.getParam("copies")==5,"Changed default takes precedence only for that input");
    V readonly=V::makeObj();readonly.set("commands",json(R"([{"op":"param.set","name":"pitch","expr":"3"}])"));readonly.set("expected_revision",V::makeNum((double)s.revision));unchangedOnFailure(s,"doc_apply",readonly);
    V patternEdit=V::makeObj();patternEdit.set("commands",json(R"([{"op":"feature.edit","id":"Ribs","p":{"count":"2"}}])"));patternEdit.set("expected_revision",V::makeNum((double)s.revision));unchangedOnFailure(s,"doc_apply",patternEdit);
    V invalid=V::makeObj();invalid.set("commands",sourceCommand("input a = b\nrelation b = a\n"));invalid.set("expected_revision",V::makeNum((double)s.revision));unchangedOnFailure(s,"doc_apply",invalid);
    invalid.set("commands",sourceCommand(program()+"relation stations_0_x = 5\n"));unchangedOnFailure(s,"doc_apply",invalid);
    invalid.set("commands",sourceCommand("array oversized[257] = [index,0,0]\n"));unchangedOnFailure(s,"doc_apply",invalid);
    invalid.set("commands",sourceCommand("array capacity[100] = [index,0,0]\n"));unchangedOnFailure(s,"doc_apply",invalid);
    invalid.set("commands",sourceCommand("relation n = future[0].x\narray first[n + 1] = [index,0,0]\narray future[1] = [4,0,0]\n"));unchangedOnFailure(s,"doc_apply",invalid);
    invalid.set("commands",sourceCommand("pattern Bad from Missing count 3 step [1,0,0]\n"));unchangedOnFailure(s,"doc_apply",invalid);
    invalid.set("commands",sourceCommand(program()));invalid.set("expected_revision",V::makeNum((double)s.revision-1));unchangedOnFailure(s,"doc_apply",invalid);
    const auto saved=ark::documentToJson(s.document,false,false);auto root=json(saved);check(root.find("version")->num==35,"Skeleton semantics must use version35");ark::Document loaded;std::string error;check(ark::documentFromJson(ark::documentToJson(s.document),loaded,&error),error.c_str());check(loaded.iterationHistory.t==V::Obj,"History version35 expands");loaded.recompute();check(loaded.getParam("span")==180&&loaded.evalBodies.size()==s.document.evalBodies.size(),"Source and overrides recompute after load");
    const auto file=std::filesystem::temp_directory_path()/"ark-skeleton-019b-test.ark";check(ark::saveDocument(loaded,file.string().c_str()),"Binary skeleton persistence");ark::Document binary;check(ark::loadDocument(file.string().c_str(),binary,&error),error.c_str());binary.recompute();check(binary.getParam("copies")==5,"Binary retained overrides");std::filesystem::remove(file);
    auto hostile=root;hostile.set("version",V::makeNum(34));check(!ark::documentFromJson(hostile.dump(),loaded,&error),"Old readers must reject new semantics");
    hostile=root;auto skeleton=*hostile.find("skeleton");auto manifest=*skeleton.find("manifest");auto parameters=*manifest.find("parameters");auto& fields=parameters.arr.front().obj;fields.erase(std::remove_if(fields.begin(),fields.end(),[](const auto& field){return field.first=="kind";}),fields.end());manifest.set("parameters",parameters);skeleton.set("manifest",manifest);hostile.set("skeleton",skeleton);check(!ark::documentFromJson(hostile.dump(),loaded,&error),"Missing ownership kind rejected");
    hostile=root;skeleton=*hostile.find("skeleton");manifest=*skeleton.find("manifest");parameters=*manifest.find("parameters");parameters.arr.front().set("name",V::makeStr("unrelated"));manifest.set("parameters",parameters);skeleton.set("manifest",manifest);hostile.set("skeleton",skeleton);check(!ark::documentFromJson(hostile.dump(),loaded,&error),"Forged ownership rejected");
    V previewArgs=V::makeObj();previewArgs.set("commands",sourceCommand("input fresh = 50\n"));previewArgs.set("expected_revision",V::makeNum((double)s.revision));auto before=ark::documentToJson(s.document,false,false);auto candidate=accepted(s.call("doc_preview",previewArgs));check(ark::documentToJson(s.document,false,false)==before,"Rendered candidate preparation changed source");
    V commit=V::makeObj();commit.set("candidate_id",*candidate.find("candidate_id"));commit.set("expected_revision",V::makeNum((double)s.revision));accepted(s.call("doc_apply",commit));check(s.document.features.size()==1&&s.document.getParam("fresh")==50,"Atomic candidate replaced generated outputs");
    apply(s,json(R"([{"op":"skeleton.remove"}])"));check(!ark::skeletonDocument(s.document)&&s.document.params.empty()&&s.document.features.size()==1,"Remove releases only generated outputs");
    apply(s,sourceCommand(""));check(ark::skeletonDocument(s.document)&&s.document.params.empty(),"Empty typed source accepts clear-all authoring");apply(s,json(R"([{"op":"skeleton.remove"}])"));
    apply(s,json(R"([{"op":"param.set","name":"span","expr":"7"}])"));invalid.set("commands",sourceCommand(program()));invalid.set("expected_revision",V::makeNum((double)s.revision));unchangedOnFailure(s,"doc_apply",invalid);
    // Collision tool must expose mesh authority and retain the document.
    ark::AgentSession c;c.suppressFailureCapture();seed(c);V second=json(R"([{"op":"feature.add","type":"mesh_body","name":"Second","mesh":{"positions":[3,0,0,4,0,0,4,1,0,3,1,0,3,0,1,4,0,1,4,1,1,3,1,1],"indices":[0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7]}}])");apply(c,second);
    V collision=json(R"({"a":"Seed","b":"Second","mode":"mesh"})");collision.set("expected_revision",V::makeNum((double)c.revision));before=ark::documentToJson(c.document,false,false);report=accepted(c.call("collision_inspect",collision));check(report.find("status")->str=="separated"&&!report.find("physics_simulation")->b&&ark::documentToJson(c.document,false,false)==before,"Mesh collision read-only report");
    collision.set("b",V::makeStr("Seed"));unchangedOnFailure(c,"collision_inspect",collision);collision.set("b",V::makeStr("Second"));collision.set("mode",V::makeStr("exact"));unchangedOnFailure(c,"collision_inspect",collision);
    ark::Document indexed;indexed.setParam("stations_0_x","10");ark::Feature box;box.type=ark::FeatureType::Box;box.p={{"dx","stations[0] . x"},{"dy","3"},{"dz","2"}};indexed.addFeature(box);auto serialized=json(ark::documentToJson(indexed));check(serialized.find("version")->num==35,"Spaced indexed expressions require new format");serialized.set("version",V::makeNum(34));check(!ark::documentFromJson(serialized.dump(),loaded,&error),"Downgraded indexed expressions reject");
    indexed.features.clear();ark::Feature curve;curve.type=ark::FeatureType::Curve;curve.nativeSpec=json(R"({"start":[0,0,0],"segments":[{"kind":"line","to":["stations[0].x",0,0]}]})");indexed.addFeature(curve);serialized=json(ark::documentToJson(indexed));check(serialized.find("version")->num==35,"Nested native expressions require new format");
}
}
int main(){try{tests();std::cout<<"Skeleton source/arrays/patterns/ownership/persistence/atomic tools passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
