#include "ark/design_disciplines.hpp"
#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
static int checks=0;
void canonical(V& v){for(auto& kv:v.obj)canonical(kv.second);for(auto& x:v.arr)canonical(x);std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}
bool equal(V a,V b){canonical(a);canonical(b);return a.dump()==b.dump();}
void check(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
void readbackDxf(const std::string& source){
    std::istringstream input(source);std::string code,value;std::map<int,std::vector<std::string>> row;size_t wireLines=0,texts=0;bool firstWire=false;std::set<std::string> layers;
    auto finish=[&]{if(row.empty())return;const auto kind=row.at(0).front();if(kind=="LAYER")layers.insert(row.at(2).front());if(kind=="LINE"){check(row.at(100)==std::vector<std::string>{"AcDbEntity","AcDbLine"},"DXF line subclass markers");if(row.at(8).front()=="WIRES"){++wireLines;const double x=std::stod(row.at(10).front()),y=std::stod(row.at(20).front()),xx=std::stod(row.at(11).front()),yy=std::stod(row.at(21).front());firstWire|=x==50&&y==170&&xx==75&&yy==170;}}if(kind=="TEXT"){++texts;check(row.at(100)==std::vector<std::string>{"AcDbEntity","AcDbText","AcDbText"}&&row.at(7).front()=="ARK_TEXT_STYLE","DXF text style/subclass markers");}row.clear();};
    while(std::getline(input,code)){check((bool)std::getline(input,value),"Truncated DXF group pair");const int key=std::stoi(code);if(key==0)finish();if(key==0||!row.empty())row[key].push_back(value);}finish();check(wireLines==3&&firstWire&&texts>=4,"DXF wire endpoints, sheet Y direction and labels");check(layers==std::set<std::string>{"0","SYMBOLS","WIRES","PINS","LABELS"},"DXF declares all used layers");
}
V json(const std::string& s){V v;if(!arkjson::parse(s,v))throw std::runtime_error("Fixture JSON");return v;}
const V& get(const V& v,const char* key){const auto* p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing field ")+key+": "+v.dump());return *p;}
V call(AgentSession& s,const char* tool,V args=V::makeObj()){args.set("expected_revision",V::makeNum((double)s.revision));auto r=s.call(tool,args);check(!get(r,"isError").b,r.dump());return get(r,"structuredContent");}
void apply(AgentSession& s,const std::string& commands){V args=V::makeObj();args.set("commands",json(commands));call(s,"doc_apply",args);}
void reject(AgentSession& s,const V& commands,bool stale=false){const auto before=documentToJson(s.document,false,false);const auto revision=s.revision;V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum((double)(stale?revision-1:revision)));const auto r=s.call("doc_apply",args);check(get(r,"isError").b,"Invalid mutation accepted");check(s.revision==revision&&documentToJson(s.document,false,false)==before,"Rejected mutation changed document");}
int main(){try{
    AgentSession s;apply(s,R"([{"op":"feature.add","type":"box","name":"Controller","p":{"dx":20,"dy":12,"dz":5}},{"op":"assembly.add","name":"Electrical cabinet","feature":0},{"op":"assembly.add","name":"Controller occurrence","feature":1,"parent":1}])");
    apply(s,R"([{"op":"classification.set","node":1,"families":["electrical"],"tags":["cabinet"]},{"op":"classification.set","feature":1,"families":["electronics"],"tags":["controller"]}])");
    check(matchesFamilyFilter(s.document,1,2,{"electrical"}),"Group family inheritance");check(matchesFamilyFilter(s.document,1,0,{"electronics"}),"Definition family");check(!matchesFamilyFilter(s.document,1,2,{"mechanical"}),"Filter did not hide other family");check(matchesFamilyFilter(s.document,1,2,{}),"Empty filter should show all");
    apply(s,R"([{"op":"classification.set","node":2,"families":["electronics"]}])");check(matchesFamilyFilter(s.document,1,2,{"electronics"}),"Occurrence overrides group");
    check(get(call(s,"classification_info",json(R"({"node":2})")),"items").arr.size()==1,"Scoped classification lookup");
    reject(s,json(R"([{"op":"classification.set","feature":1,"families":["electrical","electrical"]}])"));reject(s,json(R"([{"op":"classification.set","feature":1,"families":["magic"]}])"));reject(s,json(R"([{"op":"classification.set","node":999,"tags":[]}])"));reject(s,json(R"([{"op":"classification.set","feature":1,"tags":[]}])"),true);
    auto sheet=json(R"({"name":"Power and control","title":"Editable circuit","width":297,"height":210,"symbols":[{"id":"supply","ref":"PS1","kind":"power","label":"24 V assumption","x":40,"y":40,"pins":[{"id":"+","x":10,"y":0},{"id":"-","x":10,"y":8}],"part_number":"PS-EXAMPLE"},{"id":"controller","ref":"A1","kind":"block","label":"Controller","x":110,"y":40,"node":2,"pins":[{"id":"V+","x":-10,"y":0},{"id":"GND","x":-10,"y":8},{"id":"IO","x":10,"y":0}]}],"wires":[{"id":"w1","net":"24V","from":{"symbol":"supply","pin":"+"},"to":{"symbol":"controller","pin":"V+"},"points":[[75,40]]},{"id":"w2","net":"0V","from":{"symbol":"supply","pin":"-"},"to":{"symbol":"controller","pin":"GND"}}]})");
    auto set=[&](V candidate,bool bad=false){V c=V::makeObj(),commands=V::makeArr();c.set("op",V::makeStr("schematic.set"));c.set("sheet",candidate);commands.arr.push_back(c);if(bad)reject(s,commands);else{V a=V::makeObj();a.set("commands",commands);call(s,"doc_apply",a);}};set(sheet);
    const auto info=call(s,"schematic_inspect",json(R"({"name":"Power and control"})"));check(get(info,"bom").arr.size()==2,"Sheet BOM must contain exactly declared symbols");check(get(info,"nets").arr.size()==2,"Explicit nets");check(get(info,"unconnected_pins").arr.size()==1,"Unconnected IO must remain visible");check(!get(info,"electrical_qualified").b,"No electrical qualification inferred");
    {auto annotation=sheet;V symbols=get(annotation,"symbols");symbols.arr[0].set("bom",V::makeBool(false));annotation.set("symbols",symbols);Document local=s.document;V c=V::makeObj();c.set("op",V::makeStr("schematic.set"));c.set("sheet",annotation);applyDesignDisciplineCommand(local,c);const auto view=inspectSchematic(local,s.revision,"Power and control");check(get(view,"bom").arr.size()==1&&get(view,"nets").arr.size()==2,"BOM exclusion must preserve electrical connectivity");}
    auto duplicate=sheet;V symbols=get(duplicate,"symbols");symbols.arr.push_back(symbols.arr[0]);duplicate.set("symbols",symbols);set(duplicate,true);
    auto badNet=sheet;V wires=get(badNet,"wires");wires.arr[1].set("from",get(wires.arr[0],"from"));badNet.set("wires",wires);set(badNet,true);
    auto missing=sheet;wires=get(missing,"wires");wires.arr[0].set("to",json(R"({"symbol":"missing","pin":"1"})"));missing.set("wires",wires);set(missing,true);
    auto outside=sheet;symbols=get(outside,"symbols");symbols.arr[0].set("x",V::makeNum(1000));outside.set("symbols",symbols);set(outside,true);
    auto cycleRef=sheet;symbols=get(cycleRef,"symbols");symbols.arr[0].set("node",V::makeNum(2));cycleRef.set("symbols",symbols);set(cycleRef,true);
    auto unknown=sheet;unknown.set("auto_wire",V::makeBool(true));set(unknown,true);
    V noRevision=V::makeObj();noRevision.set("commands",json(R"([{"op":"schematic.remove","name":"Power and control"}])"));check(get(s.call("doc_apply",noRevision),"isError").b,"Missing expected_revision accepted");
    const auto saved=documentToJson(s.document);auto root=json(saved);check(get(root,"version").num==52,"Conditional semantic version");Document round;std::string error;check(documentFromJson(saved.c_str(),round,&error),error);check(equal(round.schematics,s.document.schematics),"Sheet roundtrip");check(round.find(1)->classification.dump()==s.document.find(1)->classification.dump(),"Classification roundtrip");check(equal(inspectSchematic(round,s.revision,"Power and control"),info),"BOM/net roundtrip");
    auto downgraded=root;downgraded.set("version",V::makeNum(51));check(!documentFromJson(downgraded.dump().c_str(),round,&error),"Downgraded semantics accepted");
    call(s,"doc_undo");check(!findSchematic(s.document,"Power and control"),"Undo schematic");call(s,"doc_redo");check(findSchematic(s.document,"Power and control"),"Redo schematic");
    const auto baseline=documentToJson(s.document,false,false);const auto baseRevision=s.revision;
    V proposed=json(R"({"commands":[{"op":"classification.set","node":2,"tags":["previewed"]},{"op":"schematic.remove","name":"Power and control"}]})");
    const auto preview=call(s,"doc_preview",proposed);check(s.revision==baseRevision&&documentToJson(s.document,false,false)==baseline,"Preview changed the published document");
    V candidate=V::makeObj();candidate.set("candidate_id",get(preview,"candidate_id"));call(s,"doc_apply",candidate);check(!findSchematic(s.document,"Power and control")&&s.document.assembly[1].classification.dump().find("previewed")!=std::string::npos,"Prepared metadata was lost");
    call(s,"doc_undo");check(documentToJson(s.document,false,false)==baseline,"Undo prepared metadata did not restore authoring");
    reject(s,json(R"([{"op":"classification.set","node":2,"tags":["must roll back"]},{"op":"schematic.remove","name":"missing"}])"));
    const auto directory=std::filesystem::temp_directory_path()/("ark-disciplines-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);
    for(const auto& kind:{"dxf","svg","bom_csv"}){V args=V::makeObj();const auto path=(directory/(std::string("sheet.")+kind)).string();args.set("name",V::makeStr("Power and control"));args.set("format",V::makeStr(kind));args.set("path",V::makeStr(path));const auto exported=call(s,"schematic_export",args);check(get(exported,"bytes").num>100,"Empty export");std::ifstream f(path,std::ios::binary);std::string content((std::istreambuf_iterator<char>(f)),{});check(content.find("PS1")!=std::string::npos&&content.find("A1")!=std::string::npos,"Missing exported references");if(std::string(kind)=="dxf")readbackDxf(content);if(std::string(kind)=="dxf")check(content.find("$INSUNITS\n70\n4")!=std::string::npos&&content.find("WIRES")!=std::string::npos&&content.find("EOF")!=std::string::npos,"DXF entity contract");args.set("expected_revision",V::makeNum((double)s.revision));check(get(s.call("schematic_export",args),"isError").b,"Existing export overwritten");args.set("expected_revision",V::makeNum((double)s.revision-1));args.set("path",V::makeStr(path+"-stale"));check(get(s.call("schematic_export",args),"isError").b&&!std::filesystem::exists(path+"-stale"),"Stale export published");}
    std::filesystem::remove_all(directory);
    const auto toolList=AgentSession::tools();for(const auto& name:{"classification_info","schematic_inspect","schematic_export"})check(std::any_of(toolList.arr.begin(),toolList.arr.end(),[&](const V& t){return get(t,"name").str==name;}),"Tool discovery");
    for(const auto& tool:toolList.arr)if(get(tool,"name").str=="doc_apply"){const auto& properties=get(get(get(get(get(tool,"inputSchema"),"properties"),"commands"),"items"),"properties");for(const char* field:{"families","tags","node","classification","sheet"})check(properties.find(field),"Command field missing from discovery");const auto& operations=get(get(properties,"op"),"enum");for(const char* op:{"classification.set","schematic.set","schematic.remove"})check(std::any_of(operations.arr.begin(),operations.arr.end(),[&](const V& value){return value.asStr()==op;}),"Command operation missing from discovery");}
    std::cout<<checks<<" design discipline checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
