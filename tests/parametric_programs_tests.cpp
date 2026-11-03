#include "ark/parametric_programs.hpp"
#include "ark/serialize.hpp"
#include "ark/onboarding.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
static int assertions=0;
void check(bool ok,const std::string& message){++assertions;if(!ok)throw std::runtime_error(message);}
V json(const std::string& text){V v;check(arkjson::parse(text,v),"Invalid test JSON: "+text);return v;}
const V& at(const V& v,const char* key){const auto* p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing field ")+key+" in "+v.dump());return *p;}
V data(const V& r){check(!at(r,"isError").b,r.dump());return at(r,"structuredContent");}
V call(AgentSession& s,const char* tool,V args){args.set("expected_revision",V::makeNum((double)s.revision));return data(s.call(tool,args));}
void apply(AgentSession& s,const std::string& commands){V a=V::makeObj();a.set("commands",json(commands));call(s,"doc_apply",a);}
void commit(AgentSession& s,const V& preview){V a=V::makeObj();a.set("candidate_id",at(preview,"candidate_id"));call(s,"doc_apply",a);}
V example(const char* id){for(const auto& e:parametricProgramExamples().arr)if(at(e,"id").str==id)return at(e,"program");throw std::runtime_error(std::string("Missing example ")+id);}
V prepare(AgentSession& s,const char* mode,const char* ns,const V* program=nullptr,V values=V{}){V a=V::makeObj();a.set("action",V::makeStr("prepare"));a.set("mode",V::makeStr(mode));a.set("namespace",V::makeStr(ns));if(program)a.set("program",*program);if(!values.isNull())a.set("values",values);return call(s,"parametric_program",a);}
void rejects(AgentSession& s,const char* tool,V a,const std::string& reason){const auto revision=s.revision;const auto before=documentToJson(s.document,false,false);a.set("expected_revision",V::makeNum((double)revision));const auto r=s.call(tool,a);check(at(r,"isError").b&&r.dump().find(reason)!=std::string::npos,"Expected "+reason+": "+r.dump());check(s.revision==revision&&documentToJson(s.document,false,false)==before,"Rejected operation changed source");}
void compileRejects(const V& program,const V& values,const std::string& reason){bool rejected=false;try{compileParametricProgram(program,values,"t");}catch(const std::exception& e){rejected=std::string(e.what()).find(reason)!=std::string::npos;if(!rejected)std::cerr<<"unexpected: "<<e.what()<<"\n";}check(rejected,"Expected compile rejection "+reason);}
const Feature* named(const Document& d,const std::string& name){for(const auto& f:d.features)if(f.name==name)return &f;return nullptr;}
V program(const std::string& body,const std::string& parameters="{}",const std::string& functions="{}"){return json(R"({"format":"ark.parametric-program.v1","name":"Probe","parameters":)"+parameters+R"(,"functions":)"+functions+R"(,"body":)"+body+"}");}

void language(){
    // Bookshelf defaults: 2 sides + 5 shelves, all values derived inside the program.
    const auto shelf=example("bookshelf");auto compiled=compileParametricProgram(shelf,V{},"shelf");const auto& commands=at(compiled,"commands").arr;
    check(commands.size()==14,"Bookshelf emits a sketch and an exact extrusion per board");check(at(at(compiled,"owned"),"features").arr.size()==7&&at(at(compiled,"owned"),"features").arr[2].str=="shelf / Shelf 1"&&at(at(compiled,"owned"),"sketches").arr[0].str=="shelf__side1","Owned names are namespaced in emit order");
    check(at(at(commands[5],"p"),"depth").num==300&&at(at(commands[5],"p"),"sketch").str=="shelf__shelf1","Whole-string templates become numbers and local sketch references are rewritten");
    const auto& wire=at(at(at(commands[6],"sketch"),"entities").arr[0],"start").arr;check(wire[0].str=="-382"&&wire[1].str=="445.5"&&at(at(at(at(commands[0],"sketch"),"entities").arr[0],"segments").arr[1],"to").arr[1].str=="0","Sketch coordinates stay driven text");
    check(at(at(compiled,"values"),"shelves").num==5&&at(compiled,"assertions").arr.size()==1,"Resolved defaults and passed assertion are reported");
    check(parametricProgramHash(shelf)==at(compiled,"program_sha256").str,"Program digest is reported");
    compiled=compileParametricProgram(shelf,json(R"({"shelves":3,"width":600})"),"shelf");check(at(compiled,"commands").arr.size()==10&&at(at(at(at(compiled,"commands").arr[4],"sketch"),"entities").arr[0],"start").arr[0].str=="-282","Values change structure and dimensions");
    // Expression semantics: precedence, comparisons, logic, ternary, modulo, rounding and functions.
    auto probe=program(R"([{"let":"a","value":"2+3*4^2"},{"let":"b","value":"-2^2"},{"let":"c","value":"(a>40&&!(b>0))?mod(-7,3):99"},{"let":"d","value":"round(clamp(lerp(0,10,0.26),0,2.5)*10)/10"},{"let":"s","value":0},{"for":"i","count":4,"body":[{"set":"s","value":"s+i"}]},
        {"emit":{"op":"feature.add","type":"box","name":"Box {a} {b} {c} {d} {s} {{x}}","p":{"dx":"{a}","dy":"{max(1,2,3)+hypot(3,4)}","dz":"width*2"}}}])");
    compiled=compileParametricProgram(probe,V{},"t");const auto& box=at(compiled,"commands").arr[0];
    check(at(box,"name").str=="t / Box 50 -4 2 2.5 6 {x}","Arithmetic, logic, set accumulation and literal braces: "+at(box,"name").str);
    check(at(at(box,"p"),"dx").num==50&&at(at(box,"p"),"dy").num==8&&at(at(box,"p"),"dz").str=="width*2","Strings without braces stay document expressions");
    // References to program-local names are rewritten by meaning; others stay unchanged.
    auto refs=program(R"([{"emit":{"op":"plane.set","name":"top","parent":"XY","p":{"oz":"{h}"}}},{"emit":{"op":"sketch.set","name":"profile","sketch":{"plane":"top","entities":[{"kind":"circle","name":"top","dof":[0,0,5]}]}}},
        {"emit":{"op":"feature.add","type":"extrude","name":"Peg","p":{"sketch":"profile","height":"{h}"}}},{"emit":{"op":"feature.add","type":"transform","name":"Moved","inputs":["Peg"],"p":{"tx":10}}},{"emit":{"op":"feature.add","type":"transform","name":"Other","inputs":["Outside"],"p":{"tz":1}}}])",R"({"h":{"default":8,"min":1}})");
    compiled=compileParametricProgram(refs,V{},"pg");const auto& rc=at(compiled,"commands").arr;
    check(at(at(at(rc[1],"sketch"),"entities").arr[0],"name").str=="top"&&at(at(rc[1],"sketch"),"plane").str=="pg__top","Sketch plane rewritten, entity names untouched");
    check(at(at(rc[2],"p"),"sketch").str=="pg__profile"&&at(rc[3],"inputs").arr[0].str=="pg / Peg"&&at(rc[4],"inputs").arr[0].str=="Outside","Feature references rewritten only for local names");
    // $for splices array items, e.g. sketch entities of a bolt circle.
    compiled=compileParametricProgram(example("bolted_flange"),json(R"({"bolts":8})"),"fl");const auto& entities=at(at(at(compiled,"commands").arr[0],"sketch"),"entities").arr;
    check(entities.size()==10&&at(entities[9],"name").str=="bolt_8"&&at(entities[2],"dof").arr[0].str=="45"&&at(entities[0],"dof").arr[0].str=="0","Array splice expands driven bolt holes");
    // Static and runtime rejections are explicit and typed.
    compileRejects(program(R"([{"emit":{"op":"feature.add","type":"box","name":"B","p":{"dx":"{nope}"}}}])"),V{},"PROGRAM_EXPRESSION");
    compileRejects(shelf,json(R"({"shelves":40})"),"PROGRAM_VALUES");compileRejects(shelf,json(R"({"shelves":2.5})"),"PROGRAM_VALUES");compileRejects(shelf,json(R"({"color":1})"),"PROGRAM_VALUES");
    compileRejects(shelf,json(R"({"height":400,"shelves":12})"),"Clear gap between shelves");
    compileRejects(program(R"([{"for":"i","count":"1.5","body":[]}])"),V{},"PROGRAM_LOOP");
    compileRejects(program(R"([{"emit":{"op":"feature.remove","name":"x","id":1}}])"),V{},"PROGRAM_OPERATION");
    compileRejects(program(R"([{"emit":{"op":"feature.add","type":"box","name":"A"}},{"emit":{"op":"feature.add","type":"box","name":"A"}}])"),V{},"PROGRAM_DUPLICATE_NAME");
    compileRejects(program(R"([{"emit":{"op":"feature.add","type":"transform","name":"T","inputs":[1]}}])"),V{},"PROGRAM_REFERENCE");
    compileRejects(program(R"([{"set":"w","value":1}])",R"({"w":{"default":1}})"),V{},"immutable");
    compileRejects(program(R"([{"let":"x","value":"1<2<3"}])"),V{},"PROGRAM_EXPRESSION");
    compileRejects(program(R"([{"let":"x","value":"1/0"},{"emit":{"op":"feature.add","type":"box","name":"B"}}])"),V{},"PROGRAM_NONFINITE");
    compileRejects(program(R"([{"for":"i","count":1024,"body":[{"for":"j","count":1024,"body":[{"let":"x","value":1}]}]}])"),V{},"PROGRAM_STEPS");
    compileRejects(program(R"([{"for":"i","count":127,"body":[{"emit":{"op":"feature.add","type":"box","name":"B{i}"}}]}])"),V{},"PROGRAM_CAPACITY");
    compileRejects(program(R"([{"call":"f","args":{}}])","{}",R"({"f":{"params":[],"body":[{"call":"f","args":{}}]}})"),V{},"PROGRAM_RECURSION");
    compileRejects(program(R"([{"let":"x","value":1}])"),V{},"PROGRAM_EMPTY");
    compileRejects(program(R"([{"emit":{"op":"feature.add","type":"box","name":"B","p":{"dx":"{1"}}}])"),V{},"PROGRAM_TEMPLATE");
    compileRejects(json(R"({"format":"ark.parametric-program.v2","name":"x","body":[]})"),V{},"PROGRAM_FORMAT");
}

bool hash_ok(const Document& d){for(const auto& e:d.parametricPrograms.arr)if(parametricProgramHash(at(e,"program"))!=at(e,"program_sha256").str)return false;return true;}
void session(const std::filesystem::path& dir){
    AgentSession s;apply(s,R"([{"op":"feature.add","type":"box","name":"Unrelated","p":{"dx":20,"dy":20,"dz":20}}])");
    const auto shelf=example("bookshelf");
    // validate is read-only and needs no document revision.
    const auto before=documentToJson(s.document,false,false);const auto revision=s.revision;
    V validate=V::makeObj();validate.set("action",V::makeStr("validate"));validate.set("program",shelf);const auto validated=data(s.call("parametric_program",validate));check(at(validated,"commands").arr.size()==14&&s.revision==revision&&documentToJson(s.document,false,false)==before,"Validate compiles without mutation");
    const auto preview=prepare(s,"install","shelf",&shelf);check(s.revision==revision&&documentToJson(s.document,false,false)==before,"Prepare previews without mutation");
    check(at(preview,"valid").b&&at(at(preview,"program"),"emitted").num==14&&at(preview,"recipe").find("commands"),"Install candidate is exact and carries its recipe: "+preview.dump().substr(0,1200));
    commit(s,preview);
    check(s.document.features.size()==8&&s.document.parametricPrograms.arr.size()==1,"Install publishes program objects and binding");for(const auto& r:s.document.lastResults)check(r.ok,"Program geometry evaluates: "+r.error);
    const auto* first=named(s.document,"shelf / Shelf 1");check(first&&first->p.at("depth")=="300"&&first->p.at("sketch")=="shelf__shelf1","Emitted dimensions are ordinary feature parameters");
    auto exact=[&](const char* name){const auto* f=named(s.document,name);check(f,std::string("Missing ")+name);return at(call(s,"native_inspect",json(R"({"detail":"properties","id":)"+std::to_string(f->id)+"}")),"report");};
    {const auto shelfReport=exact("shelf / Shelf 1"),sideReport=exact("shelf / Side 2");check(at(shelfReport,"brep_valid").b&&std::abs(at(shelfReport,"volume_mm3").num-764.0*300*18)<1e-3,"Independent exact volume of a generated shelf: "+shelfReport.dump());
        check(at(sideReport,"brep_valid").b&&std::abs(at(sideReport,"volume_mm3").num-18.0*300*1800)<1e-3,"Independent exact volume of a generated side: "+sideReport.dump());
        // XZ shelves extrude along -Y onto the same depth span as the XY sides; shelf ends meet the side face.
        const auto& a=at(shelfReport,"bounds_mm").arr;const auto& b=at(sideReport,"bounds_mm").arr;check(std::abs(a[1].num-b[1].num)<1e-9&&std::abs(a[4].num-b[4].num)<1e-9&&std::abs(a[3].num-b[0].num)<1e-9,"Generated boards share depth and meet at the side: "+shelfReport.dump()+" "+sideReport.dump());}
    // inspect reports binding state; configure re-runs with merged values.
    V inspect=V::makeObj();inspect.set("action",V::makeStr("inspect"));auto installed=data(s.call("parametric_program",inspect));check(at(installed,"programs").arr.size()==1&&!at(at(installed,"programs").arr[0],"local_edits").b,"Inspect lists a clean binding");
    commit(s,prepare(s,"configure","shelf",nullptr,json(R"({"shelves":3})")));
    check(s.document.features.size()==6&&named(s.document,"Unrelated")&&!named(s.document,"shelf / Shelf 4")&&named(s.document,"shelf / Shelf 3"),"Configure replaces only namespace-owned objects");
    check(at(at(s.document.parametricPrograms.arr[0],"values"),"shelves").num==3&&at(at(s.document.parametricPrograms.arr[0],"values"),"width").num==800,"Configure keeps other values");
    call(s,"doc_undo",V::makeObj());check(named(s.document,"shelf / Shelf 5")&&at(at(s.document.parametricPrograms.arr[0],"values"),"shelves").num==5,"Undo restores previous run and binding");
    call(s,"doc_redo",V::makeObj());check(!named(s.document,"shelf / Shelf 5")&&at(at(s.document.parametricPrograms.arr[0],"values"),"shelves").num==3,"Redo reapplies the run");
    // Namespace and value rejections leave the document intact.
    V again=json(R"({"action":"prepare","mode":"install","namespace":"shelf"})");again.set("program",shelf);rejects(s,"parametric_program",again,"PROGRAM_NAMESPACE");
    rejects(s,"parametric_program",json(R"({"action":"prepare","mode":"configure","namespace":"shelf","values":{"shelves":99}})"),"PROGRAM_VALUES");
    rejects(s,"parametric_program",json(R"({"action":"prepare","mode":"configure","namespace":"shelf","values":{"height":400,"shelves":12}})"),"Clear gap");
    V direct=json(R"({"commands":[{"op":"program.detach","namespace":"shelf"}]})");direct.set("commands",json(R"([{"op":"program.detach","namespace":"shelf","expected_program_sha256":"x"}])"));rejects(s,"doc_apply",direct,"PROGRAM_STALE_PROGRAM");
    {const auto r=s.call("doc_apply",json(R"({"commands":[{"op":"program.clear","namespace":"shelf"}]})"));check(at(r,"isError").b&&r.dump().find("expected_revision")!=std::string::npos,"Program commands require expected_revision");}
    // A stale candidate cannot be published after another revision.
    const auto stale=prepare(s,"configure","shelf",nullptr,json(R"({"shelves":4})"));apply(s,R"([{"op":"param.set","name":"unrelated","expr":1}])");V staleArgs=V::makeObj();staleArgs.set("candidate_id",at(stale,"candidate_id"));rejects(s,"doc_apply",staleArgs,"CANDIDATE_INVALID");
    // Outside dependants block replacement; detaching hands the objects back as ordinary source.
    const auto flange=example("bolted_flange");commit(s,prepare(s,"install","fl",&flange));const auto* plate=named(s.document,"fl / Flange");check(plate&&s.document.parametricPrograms.arr.size()==2,"Second namespace installs independently");
    {const auto report=exact("fl / Flange");const double pi=3.14159265358979323846,expected=pi*(60.0*60-20.0*20-6*4.5*4.5)*12;check(at(report,"brep_valid").b&&std::abs(at(report,"volume_mm3").num-expected)<1e-3,"Flange holes come from the generated bolt circle: "+report.dump());}
    apply(s,R"([{"op":"feature.add","type":"transform","name":"Flange copy","inputs":["fl / Flange"],"p":{"tz":40}}])");
    rejects(s,"parametric_program",json(R"({"action":"prepare","mode":"configure","namespace":"fl","values":{"bolts":4}})"),"PROGRAM_DEPENDANTS");
    apply(s,R"([{"op":"feature.remove","id":"Flange copy"}])");commit(s,prepare(s,"configure","fl",nullptr,json(R"({"bolts":4})")));
    {const auto* outline=s.document.findSketch("fl__outline");check(outline&&outline->sk.ents.size()==6,"Configured flange regenerates its sketch");}
    // Local edits are detected; re-runs reject until the program is detached.
    apply(s,R"([{"op":"feature.edit","id":"shelf / Shelf 1","p":{"depth":320}}])");inspect.set("namespace",V::makeStr("shelf"));installed=data(s.call("parametric_program",inspect));check(at(at(installed,"programs").arr[0],"local_edits").b,"Inspect reports local edits");
    rejects(s,"parametric_program",json(R"({"action":"prepare","mode":"configure","namespace":"shelf","values":{"shelves":4}})"),"PROGRAM_LOCAL_EDITS");
    commit(s,prepare(s,"detach","shelf"));check(s.document.parametricPrograms.arr.size()==1&&named(s.document,"shelf / Shelf 1")->p.at("depth")=="320","Detach keeps edited objects as ordinary source");
    // Persistence: semantic version 55 only while bindings exist; digests and downgrades reject.
    const auto saved=(dir/"programs.ark").string();check(saveDocument(s.document,saved.c_str()),"Save embedded program");Document reopened;std::string error;check(loadDocument(saved.c_str(),reopened,&error),error);reopened.recompute();
    check(reopened.parametricPrograms.arr.size()==1&&hash_ok(reopened),"Reopen keeps binding");
    V serialized=json(documentToJson(reopened,false,false));check(at(serialized,"version").num==55,"Programs require version 55");serialized.set("version",V::makeNum(54));Document rejected;check(!documentFromJson(serialized.dump(),rejected,&error)&&error.find("version 55")!=std::string::npos,"Reject semantic downgrade: "+error);
    serialized.set("version",V::makeNum(55));V bindings=at(serialized,"parametric_programs");V entry=bindings.arr[0];entry.set("program_sha256",V::makeStr(std::string(64,'0')));bindings.arr[0]=entry;serialized.set("parametric_programs",bindings);check(!documentFromJson(serialized.dump(),rejected,&error)&&error.find("PROGRAM_HASH")!=std::string::npos,"Tampered program digest rejects: "+error);
    commit(s,prepare(s,"remove","fl"));check(s.document.parametricPrograms.isNull()&&!named(s.document,"fl / Flange")&&!s.document.findSketch("fl__outline"),"Remove deletes owned objects and binding");
    check(json(documentToJson(s.document,false,false)).find("parametric_programs")==nullptr,"No program key remains after removal");
    // update installs new source in the same namespace.
    auto renamed=flange;renamed.set("name",V::makeStr("Bolted flange r2"));commit(s,prepare(s,"install","fl",&flange));commit(s,prepare(s,"update","fl",&renamed,json(R"({"thickness":15})")));
    check(at(s.document.parametricPrograms.arr[0],"name").str=="Bolted flange r2"&&named(s.document,"fl / Flange")->p.at("depth")=="15","Update replaces program source and keeps unrelated values");
}

void discovery(){
    bool published=false,readOnly=true;for(const auto& t:AgentSession::tools().arr)if(at(t,"name").str=="parametric_program"){published=true;readOnly=at(at(t,"annotations"),"readOnlyHint").b;}check(published&&!readOnly,"Public tool discovery includes the program contract");
    AgentSession s;const auto guide=agentOnboard(s,json(R"({"topic":"programs","detail":"guide"})"));check(at(guide,"examples").arr.size()==2&&at(guide,"example_prepare").find("program"),"Onboarding topic programs carries runnable examples");
    const auto overview=agentOnboard(s,json(R"({"topic":"overview","detail":"guide"})"));bool listed=false;for(const auto& t:at(overview,"available_topics").arr)listed|=t.str=="programs";check(listed,"Overview lists the programs topic");
}

int main(){try{
    const auto dir=std::filesystem::temp_directory_path()/("ark-programs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);
    language();session(dir);discovery();
    std::error_code ec;std::filesystem::remove_all(dir,ec);std::cout<<"parametric programs: "<<assertions<<" assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
