#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/serialize.hpp"
#include "../src/occt/common.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// 046B profile regions (B.02/B.03): several closed sketch loops extrude,
// revolve and sweep as faces with holes; legacy single-loop identity holds.
using arkjson::Value;
namespace occt=ark::occt;
namespace {
const double pi=std::acos(-1.0);
Value parse(const std::string& json){Value v;if(!arkjson::parse(json,v))throw std::runtime_error("Invalid test JSON: "+json);return v;}
void check(bool good,const std::string& message){if(!good)throw std::runtime_error(message);}
void close(double actual,double expected,double tol,const std::string& message){if(!(std::abs(actual-expected)<=tol))throw std::runtime_error(message+": actual "+std::to_string(actual)+" expected "+std::to_string(expected));}
Value call(ark::AgentSession& s,const std::string& commands){Value args=Value::makeObj();args.set("commands",parse(commands));args.set("expected_revision",Value::makeNum((double)s.revision));return s.call("doc_apply",args);}
void applyOk(ark::AgentSession& s,const std::string& commands){const Value r=call(s,commands);if(r.find("isError")->b)throw std::runtime_error("doc_apply rejected: "+r.find("structuredContent")->dump());}
std::string rejected(ark::AgentSession& s,const std::string& commands){
    const auto revision=s.revision;const auto before=ark::documentToJson(s.document,false,false);const Value r=call(s,commands);
    check(r.find("isError")->b,"expected rejection for "+commands);check(s.revision==revision&&ark::documentToJson(s.document,false,false)==before,"rejection leaves the document and revision unchanged");
    return r.find("structuredContent")->find("error")->str;
}
void rejects(ark::AgentSession& s,const std::string& commands,std::initializer_list<const char*> fragments,const std::string& label){
    const std::string error=rejected(s,commands);for(const char* fragment:fragments)if(error.find(fragment)==std::string::npos)throw std::runtime_error(label+": rejection text lacks '"+fragment+"': "+error);
    std::cout<<"  rejects "<<label<<": "<<error.substr(0,160)<<"\n";
}
const ark::Body& body(const ark::AgentSession& s,const char* name){for(const auto& f:s.document.features)if(f.name==name)for(const auto& b:s.document.evalBodies)if(b.id==f.id)return b;throw std::runtime_error(std::string("body missing: ")+name);}
const Value& report(const ark::AgentSession& s,const char* name){return *body(s,name).nativeData.find("report");}
double volume(const ark::AgentSession& s,const char* name){return report(s,name).find("volume_mm3")->num;}
const Value* reference(const ark::Body& b,const std::string& name){for(const auto& row:b.nativeData.find("topology")->find("references")->arr)if(row.find("name")->str==name)return &row;return nullptr;}
std::vector<std::string> semanticNames(const ark::Body& b){std::vector<std::string> names;for(const auto& row:b.nativeData.find("topology")->find("references")->arr){const auto& name=row.find("name")->str;if(name.rfind("@ark.",0)!=0)names.push_back(name);}return names;}
std::string digest(const std::string& bytes){return occt::sha256(bytes);}
std::string brepDigest(const ark::Body& b){return digest(occt::base64Decode(b.nativeData.find("brep_base64")->str));}
std::string refDigest(const ark::Body& b){return digest(b.nativeData.find("topology")->find("references")->dump());}
std::string meshDigest(const ark::Body& b){check((bool)b.baked,"legacy mesh");std::string bytes(reinterpret_cast<const char*>(b.baked->pos.data()),b.baked->pos.size()*sizeof(float));bytes.append(reinterpret_cast<const char*>(b.baked->idx.data()),b.baked->idx.size()*sizeof(b.baked->idx[0]));return digest(bytes);}
double savedVersion(const ark::Document& d){Value root;check(arkjson::parse(ark::documentToJson(d,true,false),root),"saved json");return root.find("version")->num;}
double ms(std::chrono::steady_clock::time_point start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}

std::string rectangle(const char* name,double x0,double y0,double x1,double y1){
    char text[512];std::snprintf(text,sizeof text,R"({"kind":"wire","name":"%s","start":[%.17g,%.17g],"segments":[{"kind":"line","to":[%.17g,%.17g]},{"kind":"line","to":[%.17g,%.17g]},{"kind":"line","to":[%.17g,%.17g]},{"kind":"line","to":[%.17g,%.17g]}]})",name,x0,y0,x1,y0,x1,y1,x0,y1,x0,y0);return text;
}
std::string circle(const char* name,double x,double y,double r){char text[256];std::snprintf(text,sizeof text,R"({"kind":"circle","name":"%s","dof":[%.17g,%.17g,%.17g]})",name,x,y,r);return text;}
std::string sketch(const char* name,const std::vector<std::string>& entities,const char* plane="XY"){std::string list;for(const auto& e:entities)list+=(list.empty()?"":",")+e;return std::string(R"({"op":"sketch.set","name":")")+name+R"(","sketch":{"plane":")"+plane+R"(","entities":[)"+list+"]}}";}

// Digests recorded from the unchanged 045A build (e8ca022, OCCT 8.0.1, Windows
// MSVC) for the same commands: single-loop sketches keep their exact requests
// and results. Serialized B-rep bytes are toolchain-specific (the Linux build
// differs on the symmetric circle), so only that toolchain compares them.
const char* kLegacy=R"([
  {"op":"sketch.set","name":"Plate","sketch":{"entities":[{"kind":"wire","name":"outline","start":[0,0],"segments":[{"kind":"line","to":[80,0]},{"kind":"line","to":[80,50]},{"kind":"arc","to":[70,60],"radius":10,"sweep":1},{"kind":"line","to":[0,60]},{"kind":"line","to":[0,0]}]}]}},
  {"op":"sketch.set","name":"Disc","sketch":{"entities":[{"kind":"circle","name":"rim","dof":[0,0,12]}]}},
  {"op":"sketch.set","name":"Slot","sketch":{"entities":[{"kind":"recipe","name":"slot","recipe":{"type":"straight_slot","start":[-20,0],"end":[20,0],"width":10}}]}},
  {"op":"sketch.set","name":"Ring","sketch":{"entities":[{"kind":"wire","name":"section","start":[20,0],"segments":[{"kind":"line","to":[40,0]},{"kind":"line","to":[40,30]},{"kind":"line","to":[20,30]},{"kind":"line","to":[20,0]}]}]}},
  {"op":"sketch.set","name":"Section","sketch":{"entities":[{"kind":"circle","name":"rod","dof":[0,0,5]}]}},
  {"op":"sketch.set","name":"Path","sketch":{"plane":"XZ","entities":[{"kind":"line","name":"run","dof":[0,0,0,100]}]}},
  {"op":"feature.add","type":"native_solid","name":"A","native":{"op":"extrude"},"p":{"sketch":"Plate","depth":"10"}},
  {"op":"feature.add","type":"native_solid","name":"B","native":{"op":"extrude"},"p":{"sketch":"Disc","depth":"8","symmetric":"1","direction":"-1"}},
  {"op":"feature.add","type":"extrude_surface","name":"C","p":{"sketch":"Slot","distance":"6","solid":"1"}},
  {"op":"feature.add","type":"revolve_surface","name":"D","p":{"sketch":"Ring","angle":"360","solid":"1"},"native":{"axis":"Y"}},
  {"op":"feature.add","type":"sweep","name":"E","native":{"section":"Section","path":"Path"},"p":{"solid":"1"}},
  {"op":"feature.add","type":"extrude","name":"F","p":{"sketch":"Plate","height":"5"}},
  {"op":"feature.add","type":"native_solid","name":"G","inputs":["F"],"native":{"op":"transform"},"p":{"tx":"200"}}])";
void legacyIdentity(){
    ark::AgentSession s;applyOk(s,kLegacy);
#if defined(_WIN32)&&defined(_MSC_VER)
    const bool recordedToolchain=occt::kernelVersion()==std::string("8.0.1");
#else
    const bool recordedToolchain=false;
#endif
    const std::vector<std::pair<const char*,std::pair<const char*,const char*>>> recorded{
        {"A",{"df8d065a544e4d5f4e1671a4121b8a67359201c413dc5cb320de1a3963ff6598","d9ec45e41dcb499d6ebacaf55645371e4d055d838d477b3e57b584c303878ad7"}},
        {"B",{"e2609c0652cc7e9ee7990a33ca8174fe7b2a8209bc0cf021e8052d3566582076","73bccf6ee1d3763b93ac62e93f779fdf35659a9333ed7b67c9331d90c6e80529"}},
        {"C",{"9e90c1999e537ac497395d2e2735b816d32dc06ecac9a60d02fff5821d35f4d1","1cb54c8410cb5be59815867d47c4981523b779f66e988b3f593a9f3c229c54e4"}},
        {"D",{"876f02bd6e079393303a024831ef92bb877bff301112c55b8aa0cf0f0faa14c7","f842a38bd8f28648b59dab63e5abf97aebdca899f890edb9be69c0f6838995f6"}},
        {"E",{"d9c456fd2de129592a7ed4fa8ef30f04f44405bb6d4cc1cf4b95ab0aaf07eb8a","ec4d889416a66725aa280d3eb82ad7366bb7c794e00838fa2bb382792b882951"}},
        {"G",{"de0ff280c442dc6cddf0be63fce1f199ba07a2c171bef9551052a8a0b5035c60","3c6b068b69579ec6584189f401470ea43b9108af0b8686957c3804ac2dce4e10"}}};
    for(const auto& [name,digests]:recorded){const auto& b=body(s,name);check(!b.nativeData.find("report")->find("profile_regions")&&!(b.nativeData.find("report")->find("extrusion")&&b.nativeData.find("report")->find("extrusion")->find("profile_regions")),std::string("legacy ")+name+" stays on the single-loop path");
        if(!recordedToolchain)continue;check(brepDigest(b)==digests.first,std::string("legacy B-rep bytes changed: ")+name);check(refDigest(b)==digests.second,std::string("legacy references changed: ")+name);}
    if(recordedToolchain)check(meshDigest(body(s,"F"))=="e37ead2d9af9fa99c50312909155f9971a09938900a0e1039a5c2077bc245a51","legacy mesh extrude bytes changed");
    else std::cout<<"  legacy digest comparison skipped: digests were recorded with Windows MSVC and OCCT 8.0.1; kernel "<<occt::kernelVersion()<<"\n";
    check(savedVersion(s.document)<48,"legacy features keep their earlier format");
    std::cout<<"legacy single-loop native extrude/symmetric, extrude_surface, revolve_surface, sweep, mesh extrude and promotion: "<<(recordedToolchain?"B-rep, reference and mesh digests unchanged":"single-loop path kept")<<"\n";
}

std::string plateSketch(){return sketch("Holes",{rectangle("plate",0,0,100,60),R"({"kind":"circle","name":"h1","dof":["hx","hy","r"]})",R"({"kind":"circle","name":"h2","dof":["100-hx","hy","r"]})",R"({"kind":"circle","name":"h3","dof":["100-hx","60-hy","r"]})",R"({"kind":"circle","name":"h4","dof":["hx","60-hy","r"]})"});}
void plateWithHoles(){
    ark::AgentSession s;applyOk(s,R"([{"op":"param.set","name":"hx","expr":"15"},{"op":"param.set","name":"hy","expr":"15"},{"op":"param.set","name":"r","expr":"5"},)"+plateSketch()+"]");
    auto started=std::chrono::steady_clock::now();
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Plate","native":{"op":"extrude"},"p":{"sketch":"Holes","depth":"10"}}])");const double extrudeMs=ms(started);
    const double expected=(6000-4*pi*25)*10;close(volume(s,"Plate"),expected,expected*1e-9,"plate with four holes volume");
    const auto& r=report(s,"Plate");check(r.find("solids")->num==1&&r.find("faces")->num==10&&r.find("brep_valid")->b,"one valid solid with 6 plate faces and 4 hole walls");
    const auto& regions=*r.find("extrusion")->find("profile_regions");check(regions.find("selection")->str=="all"&&regions.find("loops")->arr.size()==5&&regions.find("regions")->arr.size()==1,"five loops, one region");
    const auto& loops=regions.find("loops")->arr;check(loops[0].find("entities")->str=="plate"&&loops[0].find("depth")->num==0&&loops[0].find("role")->str=="outer","loop 0 is the outer plate");
    for(int i=1;i<=4;++i)check(loops[(size_t)i].find("entities")->str=="h"+std::to_string(i)&&loops[(size_t)i].find("depth")->num==1&&loops[(size_t)i].find("parent_loop")->num==0,"holes ordered by depth then entity order");
    const auto& b=body(s,"Plate");for(const char* name:{"side.h1","side.h2","side.h3","side.h4","side.plate.0","side.plate.3","cap.start","cap.end","edge.start.h1","edge.end.h4"}){const Value* row=reference(b,name);check(row&&row->find("status")->str=="bound",std::string("semantic reference bound: ")+name);}
    const auto names=semanticNames(b);
    // Dimension edits move the holes; semantic names and their order stay identical.
    applyOk(s,R"([{"op":"param.set","name":"hx","expr":"20"},{"op":"param.set","name":"r","expr":"4"}])");close(volume(s,"Plate"),(6000-4*pi*16)*10,(6000-4*pi*16)*10*1e-9,"edited hole volume");check(semanticNames(body(s,"Plate"))==names,"hole wall references are deterministic under dimension edits");
    // Every extrude end condition: symmetric/reversed and up to a parallel limit face.
    applyOk(s,R"([{"op":"param.set","name":"hx","expr":"15"},{"op":"param.set","name":"r","expr":"5"},{"op":"feature.edit","id":"Plate","p":{"symmetric":"1","direction":"-1"}}])");
    const auto& bounds=report(s,"Plate").find("bounds_mm")->arr;close(bounds[2].num,-5,1e-7,"symmetric zmin");close(bounds[5].num,5,1e-7,"symmetric zmax");close(volume(s,"Plate"),expected,expected*1e-9,"symmetric volume");
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Stop","native":{"op":"box"},"p":{"width":"140","depth":"100","height":"4"}},{"op":"feature.add","type":"native_solid","name":"Limit","inputs":["Stop"],"native":{"op":"transform"},"p":{"tx":"-20","ty":"-20","tz":"25"}},
      {"op":"feature.add","type":"native_solid","name":"To face","inputs":["Limit"],"native":{"op":"extrude","mode":"up_to_face","limit_face":"z_min"},"p":{"sketch":"Holes","depth":"1"}}])");
    close(volume(s,"To face"),(6000-100*pi)*25,(6000-100*pi)*25*1e-9,"up-to-face volume with holes");check(report(s,"To face").find("extrusion")->find("limit_cap_coverage_checked")->b,"limit coverage checked");
    // Legacy mesh extrude (Studio Extrude sketch) uses the prismatic hole path; its exact promotion is the B-rep region face.
    applyOk(s,R"([{"op":"feature.add","type":"extrude","name":"Mesh plate","p":{"sketch":"Holes","height":"10"}},{"op":"feature.add","type":"native_solid","name":"Promoted","inputs":["Mesh plate"],"native":{"op":"transform"},"p":{"tz":"40"}}])");
    const auto& mesh=*body(s,"Mesh plate").baked;const auto audit=ark::analyzeMesh(mesh);check(audit.closedOriented()&&audit.components==1,"mesh plate closed and connected");
    close(audit.signedVolume,expected,expected*2e-3,"sampled mesh plate volume within the 0.05 mm sketch sampling");close(volume(s,"Promoted"),expected,expected*1e-9,"exact promotion of the legacy extrude keeps the holes");
    std::cout<<"plate 100x60x10 with 4 x r5 holes: exact "<<volume(s,"Promoted")<<" mm3 vs analytic "<<expected<<" (error "<<volume(s,"Promoted")-expected<<"), mesh "<<audit.signedVolume<<" mm3; native extrude "<<extrudeMs<<" ms\n";
}

void nestedIsland(){
    ark::AgentSession s;applyOk(s,"["+sketch("Nest",{rectangle("frame",0,0,100,100),rectangle("window",20,20,80,80),circle("boss",50,50,15)})+"]");
    rejects(s,R"([{"op":"feature.add","type":"native_solid","name":"All","native":{"op":"extrude"},"p":{"sketch":"Nest","depth":"5"}}])",{"2 separate regions","frame","boss","native.regions"},"island without selection (two solids)");
    rejects(s,R"([{"op":"feature.add","type":"extrude","name":"Mesh","p":{"sketch":"Nest","height":"5"}}])",{"2 separate regions","frame","boss"},"mesh extrude with island");
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Frame","native":{"op":"extrude","regions":[{"entity":"frame"}]},"p":{"sketch":"Nest","depth":"5"}},
      {"op":"feature.add","type":"native_solid","name":"Boss","native":{"op":"extrude","regions":[{"point":[50,50]}]},"p":{"sketch":"Nest","depth":"8"}}])");
    close(volume(s,"Frame"),(10000-3600)*5,32000*1e-9,"frame region");close(volume(s,"Boss"),pi*225*8,pi*225*8*1e-9,"island region");
    const auto& regions=*report(s,"Boss").find("extrusion")->find("profile_regions");const auto& loops=regions.find("loops")->arr;
    check(loops.size()==3&&loops[2].find("entities")->str=="boss"&&loops[2].find("depth")->num==2&&loops[2].find("role")->str=="outer"&&loops[1].find("role")->str=="hole","even-odd depths 0/1/2");
    check(regions.find("selected_regions")->arr.size()==1&&regions.find("selected_regions")->arr[0].num==1&&regions.find("regions")->arr[1].find("selected_by")->str=="native.regions[0]","island selected by its seed");
    rejects(s,R"([{"op":"feature.add","type":"native_solid","name":"Both","native":{"op":"extrude","regions":[{"point":[50,50]},{"entity":"frame"}]},"p":{"sketch":"Nest","depth":"5"}}])",{"2 separate regions"},"two selected regions");
    std::cout<<"nested island: frame region "<<volume(s,"Frame")<<" mm3, island "<<volume(s,"Boss")<<" mm3 (depths 0/1/2)\n";
}

void disjointOuterLoops(){
    ark::AgentSession s;applyOk(s,"["+sketch("Pair",{rectangle("left",0,0,40,30),rectangle("right",60,0,100,30),circle("bore",80,15,5)})+"]");
    rejects(s,R"([{"op":"feature.add","type":"native_solid","name":"Both","native":{"op":"extrude"},"p":{"sketch":"Pair","depth":"5"}}])",{"2 separate regions","left","right"},"two disjoint outer loops");
    rejects(s,R"([{"op":"feature.add","type":"extrude_surface","name":"Both","p":{"sketch":"Pair","distance":"5","solid":"1"}}])",{"2 separate regions"},"two disjoint outer loops on extrude_surface");
    rejects(s,R"([{"op":"feature.add","type":"extrude","name":"Both","p":{"sketch":"Pair","height":"5"}}])",{"2 separate regions"},"two disjoint outer loops on the mesh extrude");
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Right","native":{"op":"extrude","regions":[{"entity":"right"}]},"p":{"sketch":"Pair","depth":"5"}},
      {"op":"feature.add","type":"extrude_surface","name":"Left","native":{"regions":[{"point":[20,15]}]},"p":{"sketch":"Pair","distance":"5","solid":"1"}}])");
    close(volume(s,"Right"),(1200-25*pi)*5,6000*1e-9,"right region keeps its bore");close(volume(s,"Left"),1200*5,6000*1e-9,"left region");
    check(report(s,"Left").find("profile_regions")->find("regions")->arr.size()==2,"extrude_surface reports both regions");
    std::cout<<"disjoint outer loops: rejected together; selected right "<<volume(s,"Right")<<" mm3 (with bore), left "<<volume(s,"Left")<<" mm3\n";
}

void regionSeeds(){
    ark::AgentSession s;applyOk(s,R"([{"op":"param.set","name":"W","expr":"100"},)"+sketch("Seeds",{R"({"kind":"wire","name":"plate","start":[0,0],"segments":[{"kind":"line","to":["W",0]},{"kind":"line","to":["W",60]},{"kind":"line","to":[0,60]},{"kind":"line","to":[0,0]}]})",R"({"kind":"circle","name":"hole","dof":["W/2",30,10]})",rectangle("tab",130,0,150,20),R"({"kind":"line","name":"guide","construction":true,"dof":[0,-10,150,-10]})"})+"]");
    // Expression seeds follow the dimension that moves the regions.
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Plate","native":{"op":"extrude","regions":[{"point":["W/2+20",30]}]},"p":{"sketch":"Seeds","depth":"4"}}])");
    close(volume(s,"Plate"),(6000-100*pi)*4,24000*1e-9,"seeded plate region");
    applyOk(s,R"([{"op":"param.set","name":"W","expr":"110"}])");close(volume(s,"Plate"),(6600-100*pi)*4,26400*1e-9,"seed stays in the plate after the width edit");
    const std::string base=R"([{"op":"feature.add","type":"native_solid","name":"X","native":{"op":"extrude","regions":)";const std::string tail=R"(},"p":{"sketch":"Seeds","depth":"4"}}])";
    rejects(s,base+R"([{"point":[0,30]}])"+tail,{"native.regions[0]","lies on loop plate"},"seed on a boundary");
    rejects(s,base+R"([{"point":["W/2",30]}])"+tail,{"inside hole loop hole"},"seed inside a hole");
    rejects(s,base+R"([{"point":[120,50]}])"+tail,{"outside every closed loop"},"seed outside every loop");
    rejects(s,base+R"([{"entity":"hole"}])"+tail,{"bounds hole loop hole"},"entity naming a hole");
    rejects(s,base+R"([{"entity":"guide"}])"+tail,{"construction"},"construction entity");
    rejects(s,base+R"([{"entity":"missing"}])"+tail,{"not in sketch"},"unknown entity");
    rejects(s,base+R"([{"entity":"plate"},{"point":[20,20]}])"+tail,{"native.regions[1]","already selected by native.regions[0]"},"duplicate region seeds");
    rejects(s,base+R"([{"entity":"plate","point":[1,1]}])"+tail,{"exactly one of"},"seed with two selectors");
    rejects(s,base+R"([])"+tail,{"1..64 seeds"},"empty selection");
    rejects(s,R"([{"op":"feature.add","type":"native_solid","name":"X","native":{"op":"box","regions":[{"entity":"plate"}]},"p":{"width":"1","depth":"1","height":"1"}}])",{"native.regions applies to"},"regions on another op");
    rejects(s,R"([{"op":"feature.add","type":"loft","name":"X","native":{"sections":["Seeds","Seeds"],"regions":[{"entity":"plate"}]},"p":{"solid":"1"}}])",{"native.regions applies to"},"regions on a loft");
    rejects(s,R"([{"op":"feature.add","type":"extrude_surface","name":"X","native":{"regions":[{"entity":"tab"}]},"p":{"sketch":"Seeds","distance":"4","solid":"0"}}])",{"need p.solid=1"},"regions on a surface result");
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Tab","native":{"op":"extrude","regions":[{"entity":"tab"}]},"p":{"sketch":"Seeds","depth":"4"}}])");close(volume(s,"Tab"),400*4,1600*1e-9,"entity seed");
    std::cout<<"region seeds: expression point seed stable under a width edit; entity seed; boundary/hole/outside/hole-entity/construction/unknown/duplicate/ambiguous/empty/unsupported rejections\n";
}

void hollowSweeps(){
    ark::AgentSession s;applyOk(s,"["+sketch("Ring",{circle("outer",0,0,10),circle("inner",0,0,8)})+","+sketch("Square",{rectangle("outer",-10,-10,10,10),circle("bore",0,0,6)})+","+sketch("Straight",{R"({"kind":"line","name":"run","dof":[0,0,0,100]})"},"XZ")+","+
        sketch("Bent",{R"({"kind":"wire","name":"run","start":[0,0],"segments":[{"kind":"line","to":[0,50]},{"kind":"arc","to":[50,100],"radius":50,"sweep":1}]})"},"XZ")+"]");
    auto started=std::chrono::steady_clock::now();
    applyOk(s,R"([{"op":"feature.add","type":"sweep","name":"Tube","native":{"section":"Ring","path":"Straight"},"p":{"solid":"1"}}])");const double tubeMs=ms(started);
    const double tube=pi*(100-64)*100;close(volume(s,"Tube"),tube,tube*1e-7,"straight hollow tube volume");
    const auto& hollow=*report(s,"Tube").find("hollow_section");check(hollow.find("loops")->num==2&&hollow.find("correction")->str=="already_normal_to_initial_tangent"&&std::abs(hollow.find("conservation_error_mm3")->num)<=hollow.find("conservation_limit_mm3")->num,"hollow receipt");
    check(report(s,"Tube").find("solids")->num==1&&report(s,"Tube").find("brep_valid")->b,"one valid tube solid");
    started=std::chrono::steady_clock::now();
    applyOk(s,R"([{"op":"feature.add","type":"sweep","name":"Bend","native":{"section":"Ring","path":"Bent"},"p":{"solid":"1"}}])");const double bendMs=ms(started);
    const double bend=pi*36*(50+25*pi);close(volume(s,"Bend"),bend,bend*1e-6,"bent hollow tube volume (Pappus)");
    applyOk(s,R"([{"op":"feature.add","type":"sweep","name":"Box tube","native":{"section":"Square","path":"Straight"},"p":{"solid":"1"}}])");close(volume(s,"Box tube"),(400-36*pi)*100,(400-36*pi)*100*1e-7,"square tube with round bore");
    applyOk(s,R"([{"op":"feature.add","type":"sweep","name":"As authored","native":{"section":"Ring","path":"Straight"},"p":{"solid":"1","correction":"0"}}])");close(volume(s,"As authored"),tube,tube*1e-7,"correction off");check(report(s,"As authored").find("hollow_section")->find("correction")->str=="none","correction off is reported");
    rejects(s,R"([{"op":"feature.add","type":"sweep","name":"X","native":{"section":"Ring","path":"Straight"},"p":{"solid":"1","contact":"1"}}])",{"cannot use contact"},"hollow section with contact");
    rejects(s,R"([{"op":"feature.add","type":"sweep","name":"X","native":{"section":"Ring","path":"Straight"},"p":{"solid":"0"}}])",{"p.solid=1"},"hollow section as a surface");
    rejects(s,R"([{"op":"feature.add","type":"sweep","name":"X","native":{"sections":[{"section":"Ring","fraction":0},{"section":"Ring","fraction":1}],"path":"Straight","regions":[{"entity":"outer"}]},"p":{"solid":"1"}}])",{"one section"},"regions with varying sections");
    std::cout<<"hollow sweeps: straight tube "<<volume(s,"Tube")<<" mm3 vs "<<tube<<" (error "<<volume(s,"Tube")-tube<<", "<<tubeMs<<" ms); bent tube "<<volume(s,"Bend")<<" vs Pappus "<<bend<<" (relative error "<<std::abs(volume(s,"Bend")-bend)/bend<<", "<<bendMs<<" ms); square/bore "<<volume(s,"Box tube")<<"\n";
}

void revolveWithHole(){
    ark::AgentSession s;applyOk(s,"["+sketch("Section",{rectangle("body",20,0,40,30),circle("void",30,15,5)})+"]");
    applyOk(s,R"([{"op":"feature.add","type":"revolve_surface","name":"Ring","p":{"sketch":"Section","angle":"360","solid":"1"},"native":{"axis":"Y"}},{"op":"feature.add","type":"revolve_surface","name":"Half","p":{"sketch":"Section","angle":"180","solid":"1"},"native":{"axis":"Y"}}])");
    const double full=2*pi*30*(600-25*pi);close(volume(s,"Ring"),full,full*1e-7,"revolved ring with toroidal void (Pappus)");close(volume(s,"Half"),full/2,full*1e-7,"half revolution with an open void");
    check(report(s,"Ring").find("solids")->num==1&&report(s,"Ring").find("profile_regions"),"one revolved solid with region receipt");
    rejects(s,R"([{"op":"feature.add","type":"revolve","name":"Mesh","p":{"sketch":"Section","angle":"360"}}])",{"revolve_surface"},"mesh revolve points to revolve_surface");
    std::cout<<"revolve with hole: 360 deg "<<volume(s,"Ring")<<" mm3 vs Pappus "<<full<<" (error "<<volume(s,"Ring")-full<<"), 180 deg "<<volume(s,"Half")<<"\n";
}

void rejections(){
    ark::AgentSession s;
    auto extrude=[](const char* sketchName){return std::string(R"([{"op":"feature.add","type":"native_solid","name":"X","native":{"op":"extrude"},"p":{"sketch":")")+sketchName+R"(","depth":"5"}}])";};
    auto mesh=[](const char* sketchName){return std::string(R"([{"op":"feature.add","type":"extrude","name":"X","p":{"sketch":")")+sketchName+R"(","height":"5"}}])";};
    applyOk(s,"["+sketch("Crossing",{rectangle("plate",0,0,100,60),circle("edge hole",100,30,5)})+","+sketch("Tangent",{rectangle("plate",0,0,100,60),circle("kiss",50,10,10)})+","+
        sketch("Corner",{rectangle("a",0,0,10,10),rectangle("b",10,10,20,20)})+","+sketch("Open",{rectangle("plate",0,0,100,60),R"({"kind":"line","name":"scribe","dof":[10,10,90,10]})"})+","+
        sketch("Cross",{rectangle("plate",0,0,100,60),R"({"kind":"line","name":"slash","dof":[-10,30,50,30]})"})+","+
        sketch("Bowtie",{R"({"kind":"wire","name":"bow","start":[0,0],"segments":[{"kind":"line","to":[20,20]},{"kind":"line","to":[20,0]},{"kind":"line","to":[0,20]},{"kind":"line","to":[0,0]}]})",circle("dot",50,50,3)})+","+
        sketch("Flat",{R"({"kind":"wire","name":"flat","start":[0,0],"segments":[{"kind":"line","to":[10,0]},{"kind":"line","to":[20,0]},{"kind":"line","to":[0,0]}]})",circle("dot",50,50,3)})+"]");
    rejects(s,extrude("Crossing"),{"intersect or touch","plate","edge hole"},"crossing loops");
    rejects(s,extrude("Tangent"),{"intersect or touch","kiss"},"tangent (touching) loops");
    rejects(s,extrude("Corner"),{"meet at one point","a","b"},"loops sharing a vertex");
    rejects(s,extrude("Open"),{"open chains","scribe"},"open chain without selection");
    rejects(s,extrude("Cross"),{"slash","touches or crosses loop plate"},"open chain crossing a loop");
    rejects(s,extrude("Bowtie"),{"bow","self-intersects"},"self-intersecting loop");
    rejects(s,extrude("Flat"),{"flat"},"zero-area loop");
    rejects(s,mesh("Crossing"),{"intersect or touch"},"mesh crossing loops");
    rejects(s,mesh("Open"),{"Open sketch profile","scribe"},"mesh open chain");
    rejects(s,mesh("Bowtie"),{"Self-intersecting","bow"},"mesh self-intersection");
    // An explicit region selection ignores a disjoint open chain and reports it.
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Scribed","native":{"op":"extrude","regions":[{"entity":"plate"}]},"p":{"sketch":"Open","depth":"5"}}])");
    close(volume(s,"Scribed"),30000,30000*1e-9,"selected region ignores the disjoint open chain");check(report(s,"Scribed").find("extrusion")->find("profile_regions")->find("open_chains_ignored")->arr[0].str=="scribe","ignored open chain reported");
    std::cout<<"rejections: crossing, tangent, shared-vertex, open-chain, crossing open chain, self-intersecting and zero-area loops; mesh path parity\n";
}

void persistence(){
    ark::AgentSession s;applyOk(s,R"([{"op":"param.set","name":"hx","expr":"15"},{"op":"param.set","name":"hy","expr":"15"},{"op":"param.set","name":"r","expr":"5"},)"+plateSketch()+R"(,{"op":"feature.add","type":"native_solid","name":"Plate","native":{"op":"extrude"},"p":{"sketch":"Holes","depth":"10"}}])");
    const double implicit=savedVersion(s.document);check(implicit<48,"multi-loop sketch without native.regions keeps its earlier format");
    ark::AgentSession single;applyOk(single,"["+sketch("One",{rectangle("plate",0,0,100,60)})+R"(,{"op":"feature.add","type":"native_solid","name":"Plate","native":{"op":"extrude"},"p":{"sketch":"One","depth":"10"}}])");check(savedVersion(single.document)==implicit,"same format as the single-loop document");
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Selected","native":{"op":"extrude","regions":[{"point":["hx+10","hy"]}]},"p":{"sketch":"Holes","depth":"3"}}])");
    check(savedVersion(s.document)==48,"native.regions saves format 48");
    for(bool history:{false,true}){
        const std::string saved=ark::documentToJson(s.document,true,history);ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),"format 48 round-trip: "+error);restored.recompute();check(restored.lastResults.back().ok,"cold rebuild");
        double v=0;std::vector<std::string> names;for(const auto& f:restored.features)if(f.name=="Selected")for(const auto& b:restored.evalBodies)if(b.id==f.id){v=b.nativeData.find("report")->find("volume_mm3")->num;names=semanticNames(b);}
        close(v,volume(s,"Selected"),1e-9,"cold rebuild parity");check(names==semanticNames(body(s,"Selected")),"references reproduce after reload");
        Value older;check(arkjson::parse(saved,older),"older");older.set("version",Value::makeNum(47));ark::Document stale;check(!ark::documentFromJson(older.dump(),stale,&error)&&error.find("version 48")!=std::string::npos,"format 47 cannot carry native.regions: "+error);
        Value future;check(arkjson::parse(saved,future),"future");future.set("version",Value::makeNum(52));check(!ark::documentFromJson(future.dump(),stale,&error)&&error.find("unsupported document version")!=std::string::npos,"format 52 is a future format");
    }
    applyOk(s,R"([{"op":"feature.remove","id":"Selected"}])");check(savedVersion(s.document)==implicit,"removing the selection returns to the earlier format");
    std::cout<<"persistence: implicit multi-loop extrude keeps format "<<implicit<<", native.regions saves 48 with history round-trip, 47 rejects the field, 50 rejects as future\n";
}

void inspection(){
    ark::AgentSession s;applyOk(s,R"([{"op":"param.set","name":"hx","expr":"15"},{"op":"param.set","name":"hy","expr":"15"},{"op":"param.set","name":"r","expr":"5"},)"+plateSketch()+"]");
    Value args=parse(R"({"name":"Holes"})");const Value r=*s.call("sketch_inspect",args).find("structuredContent");
    check(r.find("profile_ready")->b&&r.find("loops")->num==5&&r.find("profile_regions")->find("regions")->arr[0].find("holes")->arr.size()==4,"sketch_inspect reports one region with four holes");
    Value onboard=parse(R"({"topic":"sketch"})");const std::string rules=s.call("agent_onboard",onboard).find("structuredContent")->dump();check(rules.find("native.regions")!=std::string::npos&&rules.find("even-odd")!=std::string::npos,"sketch onboarding names profile regions");
    std::cout<<"inspection: sketch_inspect profile_regions and sketch onboarding rules\n";
}

void orientations(){
    // Clockwise outer and hole wires, a recipe slot hole and a circle: hole orientation is normalized from exact faces.
    ark::AgentSession s;applyOk(s,"["+sketch("Mixed",{R"({"kind":"wire","name":"cw","start":[0,0],"segments":[{"kind":"line","to":[0,50]},{"kind":"line","to":[80,50]},{"kind":"line","to":[80,0]},{"kind":"line","to":[0,0]}]})",R"({"kind":"wire","name":"cwhole","start":[10,10],"segments":[{"kind":"line","to":[10,20]},{"kind":"line","to":[20,20]},{"kind":"line","to":[20,10]},{"kind":"line","to":[10,10]}]})",R"({"kind":"recipe","name":"slot","recipe":{"type":"straight_slot","start":[40,25],"end":[60,25],"width":8}})",circle("ccw",30,38,4)})+R"(,{"op":"feature.add","type":"native_solid","name":"Mixed","native":{"op":"extrude"},"p":{"sketch":"Mixed","depth":"6"}},{"op":"feature.add","type":"extrude","name":"Mesh mixed","p":{"sketch":"Mixed","height":"6"}}])");
    const double area=4000-100-(160+16*pi)-16*pi;close(volume(s,"Mixed"),area*6,area*6*1e-9,"clockwise outer/hole wires, slot and circle holes");
    check(reference(body(s,"Mixed"),"side.slot.0")&&reference(body(s,"Mixed"),"side.cwhole.2")&&reference(body(s,"Mixed"),"side.ccw"),"recipe, wire and circle hole walls named by entity");
    const auto audit=ark::analyzeMesh(*body(s,"Mesh mixed").baked);check(audit.closedOriented(),"mesh mixed closed");close(audit.signedVolume,area*6,area*6*3e-3,"mesh mixed volume");
    std::cout<<"orientations: clockwise outer/hole wires, slot and circle holes "<<volume(s,"Mixed")<<" mm3 vs "<<area*6<<"\n";
}
void manyHoles(){
    // 10 x 10 holes: box prefilter keeps the exact loop checks near linear.
    std::vector<std::string> entities{rectangle("plate",0,0,210,210)};std::vector<std::string> names;
    for(int i=0;i<10;++i)for(int j=0;j<10;++j){names.push_back("h"+std::to_string(i)+"_"+std::to_string(j));entities.push_back(circle(names.back().c_str(),15+20.*i,15+20.*j,4));}
    ark::AgentSession s;applyOk(s,"["+sketch("Grid",entities)+"]");const auto started=std::chrono::steady_clock::now();
    applyOk(s,R"([{"op":"feature.add","type":"native_solid","name":"Grid","native":{"op":"extrude"},"p":{"sketch":"Grid","depth":"3"}}])");const double elapsed=ms(started);
    const double expected=(210.*210-100*16*pi)*3;close(volume(s,"Grid"),expected,expected*1e-9,"100-hole grid volume");check(report(s,"Grid").find("faces")->num==106,"100 hole walls");
    std::cout<<"many holes: 100 x r4 holes in 210x210x3, volume error "<<volume(s,"Grid")-expected<<" mm3, doc_apply "<<elapsed<<" ms\n";
}
}
int main(){std::cout<<std::unitbuf;try{legacyIdentity();plateWithHoles();nestedIsland();disjointOuterLoops();regionSeeds();hollowSweeps();revolveWithHole();rejections();orientations();manyHoles();persistence();inspection();std::cout<<"profile regions 046B tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
