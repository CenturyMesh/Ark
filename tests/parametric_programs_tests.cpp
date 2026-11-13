#include "ark/parametric_programs.hpp"
#include "ark/serialize.hpp"
#include "ark/onboarding.hpp"
#include "ark/iteration_history.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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
    // A key that accepts a sketch or a feature rejects a local name used by both.
    compileRejects(program(R"([{"emit":{"op":"sketch.set","name":"rail","sketch":{"entities":[]}}},{"emit":{"op":"feature.add","type":"box","name":"rail"}},{"emit":{"op":"feature.add","type":"sweep","name":"Swept","native":{"section":"rail","path":"rail"}}}])"),V{},"PROGRAM_REFERENCE");
    compiled=compileParametricProgram(program(R"([{"emit":{"op":"sketch.set","name":"rail","sketch":{"entities":[]}}},{"emit":{"op":"feature.add","type":"box","name":"Rail body"}},{"emit":{"op":"feature.add","type":"sweep","name":"Swept","native":{"section":"rail","path":"Rail body"}}}])"),V{},"t");
    check(at(at(at(compiled,"commands").arr[2],"native"),"section").str=="t__rail"&&at(at(at(compiled,"commands").arr[2],"native"),"path").str=="t / Rail body","Distinct local sketch and feature names still rewrite by kind");
}

// doc_apply accepts a program.bind only with exactly its compiled commands.
V bindCommand(const V& source,const std::string& ns,const V& values){V b=V::makeObj();b.set("op",V::makeStr("program.bind"));b.set("namespace",V::makeStr(ns));b.set("program",source);b.set("program_sha256",V::makeStr(parametricProgramHash(source)));b.set("values",values);return b;}
V batch(const V& source,const std::string& ns,const V& values=V::makeObj()){V c=at(compileParametricProgram(source,values,ns),"commands");c.arr.push_back(bindCommand(source,ns,values));return c;}
void applyBatch(AgentSession& s,const V& commands){V a=V::makeObj();a.set("commands",commands);call(s,"doc_apply",a);}
void rejectsBatch(AgentSession& s,const V& commands,const std::string& reason){V a=V::makeObj();a.set("commands",commands);rejects(s,"doc_apply",a,reason);}
void provenance(){
    AgentSession s;apply(s,R"([{"op":"feature.add","type":"box","name":"Base","p":{"dx":5,"dy":5,"dz":5}}])");
    const auto posts=program(R"([{"emit":{"op":"plane.set","name":"top","parent":"XY","p":{"oz":"{n*10}"}}},{"for":"i","count":"n","body":[{"emit":{"op":"feature.add","type":"box","name":"Post {i+1}","p":{"dx":4,"dy":4,"dz":"{10+i}"}}}]}])",R"({"n":{"default":2,"min":1,"max":4,"integer":true}})");
    // The exact compiled commands, published by hand, bind cleanly.
    applyBatch(s,batch(posts,"posts"));const auto row=at(data(s.call("parametric_program",json(R"({"action":"inspect","namespace":"posts"})"))),"programs").arr[0];
    check(named(s.document,"posts / Post 2")&&s.document.parametricPrograms.arr.size()==1&&!at(row,"local_edits").b,"Exact hand-written compiled batch binds cleanly");
    // Different geometry under matching names is not the reviewed program.
    auto altered=batch(posts,"alt");{V post=altered.arr[1],p=at(post,"p");p.set("dz",V::makeNum(99));post.set("p",p);altered.arr[1]=post;}rejectsBatch(s,altered,"PROGRAM_PROVENANCE");
    auto missing=batch(posts,"alt");missing.arr.erase(missing.arr.begin()+2);rejectsBatch(s,missing,"PROGRAM_PROVENANCE");
    auto swapped=batch(posts,"alt");std::swap(swapped.arr[1],swapped.arr[2]);rejectsBatch(s,swapped,"PROGRAM_PROVENANCE");
    // No other command of the batch creates, renames or edits namespace objects.
    auto extra=batch(posts,"alt");extra.arr.push_back(json(R"({"op":"sketch.set","name":"alt__extra","sketch":{"entities":[]}})"));rejectsBatch(s,extra,"PROGRAM_PROVENANCE");
    auto first=batch(posts,"alt");first.arr.insert(first.arr.begin(),json(R"({"op":"material.set","name":"alt__steel","density":7.8})"));rejectsBatch(s,first,"PROGRAM_PROVENANCE");
    auto renamed=batch(posts,"alt");renamed.arr.push_back(json(R"({"op":"feature.edit","id":"Base","name":"alt / Post 9"})"));rejectsBatch(s,renamed,"PROGRAM_PROVENANCE");
    auto edited=batch(posts,"alt");{V edit=json(R"({"op":"feature.edit","p":{"dz":50}})");edit.set("id",V::makeNum((double)s.document.nextFeatureId_));edited.arr.push_back(edit);}rejectsBatch(s,edited,"PROGRAM_PROVENANCE");
    // An upsert must not merge compiled output into an existing row.
    apply(s,R"([{"op":"plane.set","name":"alt__top","parent":"XY","p":{"ox":7}}])");rejectsBatch(s,batch(posts,"alt"),"PROGRAM_PROVENANCE");apply(s,R"([{"op":"plane.remove","name":"alt__top"}])");
    // A hand-written re-run needs its clear immediately before the compiled block.
    const auto three=json(R"({"n":3})");V clear=json(R"({"op":"program.clear","namespace":"posts"})");clear.set("expected_program_sha256",V::makeStr(parametricProgramHash(posts)));
    auto apart=batch(posts,"posts",three);apart.arr.insert(apart.arr.begin(),json(R"({"op":"param.set","name":"gap","expr":1})"));apart.arr.insert(apart.arr.begin(),clear);rejectsBatch(s,apart,"PROGRAM_PROVENANCE");
    auto twice=batch(posts,"posts",three);twice.arr.insert(twice.arr.begin(),clear);for(const auto& c:batch(posts,"posts",three).arr)twice.arr.push_back(c);rejectsBatch(s,twice,"PROGRAM_PROVENANCE");
    auto rerun=batch(posts,"posts",three);rerun.arr.insert(rerun.arr.begin(),clear);applyBatch(s,rerun);
    check(named(s.document,"posts / Post 3")&&at(at(s.document.parametricPrograms.arr[0],"values"),"n").num==3&&s.document.parametricPrograms.arr.size()==1,"Adjacent clear and compiled block re-run the program by hand");
    // Prepared candidates remain the ordinary path, and a stale one is not rebased.
    const auto preview=prepare(s,"configure","posts",nullptr,json(R"({"n":4})"));apply(s,R"([{"op":"param.set","name":"gap","expr":2}])");
    {V refresh=V::makeObj();refresh.set("candidate_id",at(preview,"candidate_id"));rejects(s,"doc_preview",refresh,"CANDIDATE_REFRESH");}
    commit(s,prepare(s,"configure","posts",nullptr,json(R"({"n":4})")));check(named(s.document,"posts / Post 4")&&named(s.document,"Base"),"Prepared configure candidate still publishes");
    // Skeleton text naming an owned object through JSON escapes is a dependency.
    const auto quoted=program(R"([{"emit":{"op":"feature.add","type":"box","name":"Post \"A\"","p":{"dx":4,"dy":4,"dz":4}}}])");commit(s,prepare(s,"install","q",&quoted));
    apply(s,R"([{"op":"skeleton.set","source":"# keeps q / Post \\u0022A\\u0022\ninput span = 1"}])");rejects(s,"parametric_program",json(R"({"action":"prepare","mode":"remove","namespace":"q"})"),"PROGRAM_DEPENDANTS");
    apply(s,R"([{"op":"skeleton.remove"}])");commit(s,prepare(s,"remove","q"));check(!named(s.document,"q / Post \"A\""),"Removal proceeds once the Skeleton text no longer names it");
}
// An intermediate evaluation (a non-first assembly.drive) reseals the binding
// but does not end the check: later numeric-ID edits in the batch still reject.
void provenanceAcrossEvaluations(){
    AgentSession s;apply(s,R"([{"op":"feature.add","type":"box","name":"Fixture body","p":{"dx":10,"dy":2,"dz":2}},{"op":"assembly.add","name":"Frame","feature":"Fixture body","fixed":true},{"op":"assembly.add","name":"Fork","feature":"Fixture body"},{"op":"assembly.add","name":"Handlebar","feature":"Fixture body"},
        {"op":"mate.add","a":"Frame","b":"Fork","type":"revolute","reference_a":"Z","reference_b":"Z"},{"op":"mate.add","a":"Fork","b":"Handlebar","type":"rigid","reference_a":"origin","reference_b":"origin"}])");
    const auto posts=program(R"([{"for":"i","count":"n","body":[{"emit":{"op":"feature.add","type":"box","name":"Post {i+1}","p":{"dx":4,"dy":4,"dz":"{10+i}"}}}]}])",R"({"n":{"default":2,"min":1,"max":4,"integer":true}})");
    const auto drive=json(R"({"op":"assembly.drive","driver":"Handlebar","delta":30})");V edit=json(R"({"op":"feature.edit","p":{"dz":99}})");edit.set("id",V::makeNum((double)s.document.nextFeatureId_));
    auto after=batch(posts,"mid");after.arr.push_back(drive);after.arr.push_back(edit);rejectsBatch(s,after,"PROGRAM_PROVENANCE");
    auto between=batch(posts,"mid");between.arr.push_back(drive);between.arr.push_back(edit);between.arr.push_back(drive);rejectsBatch(s,between,"PROGRAM_PROVENANCE");
    auto before=batch(posts,"mid");before.arr.push_back(edit);before.arr.push_back(drive);rejectsBatch(s,before,"PROGRAM_PROVENANCE");
    auto clean=batch(posts,"mid");clean.arr.push_back(drive);clean.arr.push_back(drive);applyBatch(s,clean);
    auto localEdits=[&]{return at(at(data(s.call("parametric_program",json(R"({"action":"inspect","namespace":"mid"})"))),"programs").arr[0],"local_edits").b;};
    check(named(s.document,"mid / Post 2")&&!localEdits()&&s.document.programBindingsToSeal.empty(),"Repeated evaluations in one batch reseal the unchanged compiled output without a false rejection");
    // After publication the binding is ordinary source: a later batch may edit it.
    V later=V::makeArr();later.arr.push_back(edit);applyBatch(s,later);check(localEdits(),"A numeric-ID edit in a later batch is a local edit, not a provenance failure");
}

V emitAfter(const std::string& statements,const std::string& parameters="{}",const std::string& functions="{}"){return program("["+statements+R"J(,{"emit":{"op":"feature.add","type":"box","name":"B"}}])J",parameters,functions);}
V letProgram(const std::string& expression,const std::string& parameters="{}"){return emitAfter(R"J({"let":"out","value":")J"+expression+R"J("})J",parameters);}
std::string emittedName(const V& p,const V& values=V{}){return at(at(compileParametricProgram(p,values,"t"),"commands").arr[0],"name").str;}
V nested(V inner,int levels){for(int k=0;k<levels;++k){V st=V::makeObj(),then=V::makeArr();then.arr.push_back(inner);st.set("if",V::makeStr("1"));st.set("then",then);inner=st;}return inner;}
// Deep runtime nesting: `calls` nested calls, topNest+1 blocks in the body,
// fnNest+2 blocks in every call of f and a 63-level expression at the bottom,
// so the deepest point holds (topNest+1)+calls*(fnNest+2) active blocks.
V deepest(int calls,int topNest=31,int fnNest=30){
    V emit=json(R"J({"emit":{"op":"feature.add","type":"box","name":"Deep {x}"}})J"),let=V::makeObj(),inner=json(R"J({"if":"","then":[{"call":"f","args":{"d":"d+1"}}]})J"),fn=V::makeObj(),functions=V::makeObj(),body=V::makeArr(),params=V::makeArr();
    let.set("let",V::makeStr("x"));let.set("value",V::makeStr(std::string(62,'(')+"d"+std::string(62,')')));V otherwise=V::makeArr();otherwise.arr.push_back(let);otherwise.arr.push_back(emit);
    inner.set("if",V::makeStr("d<"+std::to_string(calls)));inner.set("else",otherwise);V fnBody=V::makeArr();fnBody.arr.push_back(nested(inner,fnNest));
    params.arr.push_back(V::makeStr("d"));fn.set("params",params);fn.set("body",fnBody);functions.set("f",fn);
    body.arr.push_back(nested(json(R"J({"call":"f","args":{"d":1}})J"),topNest));
    V p=program("[]");p.set("functions",functions);p.set("body",body);return p;
}

void hardening(){
    // One nesting budget covers unary, power, parenthesis, call and mixed chains:
    // text under the 2048-byte limit rejects instead of exhausting the stack.
    for(const char op:{'-','!','+'})compileRejects(letProgram(std::string(2040,op)+"1"),V{},"PROGRAM_EXPRESSION: body[0].value: nesting deeper than 64");
    std::string power="2",parens=std::string(1000,'(')+"1"+std::string(1000,')'),calls,mixed;for(int k=0;k<1000;++k)power+="^1";for(int k=0;k<300;++k)calls+="abs(";calls+="1"+std::string(300,')');for(int k=0;k<600;++k)mixed+="-(";mixed+="1"+std::string(600,')');
    for(const auto& expression:{power,parens,calls,mixed})compileRejects(letProgram(expression),V{},"nesting deeper than 64");
    compileRejects(program(R"J([{"emit":{"op":"feature.add","type":"box","name":"B","p":{"dx":"{)J"+std::string(2040,'-')+R"J(1}"}}}])J"),V{},"PROGRAM_EXPRESSION: body[0].emit.p.dx: nesting deeper than 64");
    check(emittedName(program(R"J([{"let":"x","value":")J"+std::string(63,'-')+R"J(3"},{"emit":{"op":"feature.add","type":"box","name":"C{x}"}}])J"))=="t / C-3","63 prefixes evaluate");
    compileRejects(letProgram(std::string(64,'-')+"3"),V{},"nesting deeper than 64");
    // A stored binding with such text rejects on load without a crash.
    {Document empty;V doc=json(documentToJson(empty,false,false));doc.set("version",V::makeNum(55));
        V entry=json(R"J({"namespace":"deep","name":"Probe","values":{},"owned":{"features":[],"sketches":[],"planes":[],"materials":[]},"baseline":{"features":{},"sketches":{},"planes":{},"materials":{}}})J");
        const auto hostile=letProgram(std::string(2040,'-')+"1");entry.set("program",hostile);entry.set("program_sha256",V::makeStr(parametricProgramHash(hostile)));V bindings=V::makeArr();bindings.arr.push_back(entry);doc.set("parametric_programs",bindings);
        Document target;std::string error;check(!documentFromJson(doc.dump(),target,&error)&&error.find("PROGRAM_EXPRESSION")!=std::string::npos&&target.parametricPrograms.isNull(),"Hostile stored program rejects on load: "+error);
        // Bindings saved before static name resolution still open and save; running them rejects explicitly.
        const auto legacy=emitAfter(R"J({"if":"0","then":[{"let":"y","value":"widht"}]})J");entry.set("program",legacy);entry.set("program_sha256",V::makeStr(parametricProgramHash(legacy)));bindings.arr[0]=entry;doc.set("parametric_programs",bindings);
        check(documentFromJson(doc.dump(),target,&error)&&target.parametricPrograms.arr.size()==1&&json(documentToJson(target,false,false)).find("parametric_programs"),"Legacy binding opens and saves: "+error);
        compileRejects(legacy,V{},"unknown identifier 'widht'");}
    // Blocks of all active bodies share one budget of 64, so stack use does not
    // multiply with the call depth; one more block or call level rejects explicitly.
    check(emittedName(deepest(1))=="t / Deep 1","32-deep blocks in the body and in a call (64 active) with a 63-level expression complete");
    check(emittedName(deepest(16,15,1))=="t / Deep 16","16 nested calls within 64 active blocks complete");
    compileRejects(deepest(16,16,1),V{},"PROGRAM_RECURSION: functions.f.body[0].then[0].else: blocks nest deeper than 64 across calls");
    compileRejects(deepest(16),V{},"PROGRAM_RECURSION: functions.f.body: blocks nest deeper than 64 across calls");
    compileRejects(deepest(17,0,1),V{},"PROGRAM_RECURSION: functions.f.body[0].then[0].then[0]: calls nest deeper than 16");
    // The same limits hold for tool arguments within the JSON nesting limit, where
    // 16 calls of a 28-block function would otherwise hold 476 active blocks.
    {AgentSession s;const auto arguments=[&](const V& p){V e;check(arkjson::parse(R"J({"jsonrpc":"2.0","params":{"arguments":{"action":"validate","program":)J"+p.dump()+"}}}",e),"Program fits the JSON nesting limit inside a tool call");return at(at(e,"params"),"arguments");};
        check(at(at(data(s.call("parametric_program",arguments(deepest(16,15,1)))),"commands").arr[0],"name").str=="program / Deep 16","Tool validate runs 16 nested calls within 64 active blocks");
        rejects(s,"parametric_program",arguments(deepest(16,27,26)),"blocks nest deeper than 64 across calls");}

    // Evaluated code never turns inf or NaN into a truth value or a finite number.
    const std::string equal=R"J({"a":{"default":2},"b":{"default":2}})J";
    compileRejects(emitAfter(R"J({"assert":"1/(a-b) > 0"})J",equal),V{},"PROGRAM_NONFINITE: body[0].assert: division by zero in '1/(a-b) > 0'");
    compileRejects(emitAfter(R"J({"assert":"!(sqrt(a-5) < 0)"})J",equal),V{},"PROGRAM_NONFINITE: body[0].assert: sqrt(...) is not finite in '!(sqrt(a-5) < 0)'");
    compileRejects(emitAfter(R"J({"if":"1/(a-b) || 1","then":[]})J",equal),V{},"PROGRAM_NONFINITE: body[0].if: division by zero");
    compileRejects(emitAfter(R"J({"if":"log(a-b) < 0","then":[]})J",equal),V{},"PROGRAM_NONFINITE: body[0].if: log(...) is not finite");
    compileRejects(emitAfter(R"J({"for":"i","count":"1/(a-b)","body":[]})J",equal),V{},"PROGRAM_NONFINITE: body[0].count: division by zero");
    compileRejects(emitAfter(R"J({"call":"f","args":{"k":"sqrt(a-5)"}})J",equal,R"J({"f":{"params":["k"],"body":[]}})J"),V{},"PROGRAM_NONFINITE: body[0].args.k");
    for(const char* hidden:{"sqrt(a-5) ? 1 : 2","sqrt(a-5) && 0","!sqrt(a-5)","sqrt(a-5) == sqrt(a-5)","1/(1/(a-b))","min(1/(a-b),3)","max(3,exp(1000))","atan(1e999)","1/(10^400)","1e200*1e200/1e300","7%(a-b)","mod(7,a-b)","sign(sqrt(a-5))","(a-b)^-1","0*(1/(a-b))"})
        compileRejects(letProgram(hidden,equal),V{},"PROGRAM_NONFINITE: body[0].value");
    compileRejects(program(R"J([{"emit":{"op":"feature.add","type":"box","name":"B{min(1/(a-b),3)}"}}])J",equal),V{},"PROGRAM_NONFINITE: body[0].emit.name: division by zero");
    // Unevaluated code is only syntax: validation of an untaken guard allows placeholder division by zero.
    check(emittedName(emitAfter(R"J({"if":"a>b","then":[{"let":"r","value":"1/(a-b)"}]})J",equal))=="t / B","Untaken guarded division compiles");

    // ?:, && and || evaluate only the selected operand, and still syntax-check the others.
    const auto guards=program(R"J([{"let":"y","value":"x!=0 ? 1/x : 0"},{"let":"g","value":"n>0 && total/n>2"},{"let":"h","value":"n==0 || total/n>2"},{"let":"k","value":"x==0 ? 5 : sqrt(x)"},{"let":"m","value":"n>0 && total/n>2 || x==0"},
        {"emit":{"op":"feature.add","type":"box","name":"B {y} {g} {h} {k} {m} {n==0 ? 7 : total%n}"}}])J",R"J({"x":{"default":0},"n":{"default":0},"total":{"default":9}})J");
    check(emittedName(guards)=="t / B 0 0 1 5 1 7","Guards skip the undefined operand: "+emittedName(guards));
    check(emittedName(guards,json(R"J({"x":4,"n":2})J"))=="t / B 0.25 1 1 2 1 1","Guards evaluate the selected operand: "+emittedName(guards,json(R"J({"x":4,"n":2})J")));
    for(const char* skipped:{"x!=0 ? 1/x : (","0 && sqrt(1,2)","1 || nofn(2)","x!=0 ? widht : 0","1 || 1<2<3","0 ? 1 : 2 ? 3"})compileRejects(letProgram(skipped,R"J({"x":{"default":0}})J"),V{},"PROGRAM_EXPRESSION: body[0].value");

    // Every identifier resolves in its lexical scope before anything runs.
    const std::string one=R"J({"a":{"default":1}})J";
    compileRejects(emitAfter(R"J({"if":"a>100","then":[{"let":"x","value":"widht*2"}]})J",one),V{},"PROGRAM_EXPRESSION: body[0].then[0].value: unknown identifier 'widht'");
    compileRejects(emitAfter(R"J({"if":"a>100","then":[{"emit":{"op":"feature.add","type":"box","name":"X{widht}"}}]})J",one),V{},"body[0].then[0].emit.name: unknown identifier 'widht'");
    compileRejects(emitAfter(R"J({"let":"top","value":1})J",one,R"J({"f":{"params":["k"],"body":[{"let":"y","value":"k+z"}]}})J"),V{},"functions.f.body[0].value: unknown identifier 'z'");
    compileRejects(emitAfter(R"J({"let":"top","value":1},{"call":"f","args":{"k":1}})J",one,R"J({"f":{"params":["k"],"body":[{"let":"y","value":"k+top"}]}})J"),V{},"functions.f.body[0].value: unknown identifier 'top'");
    compileRejects(emitAfter(R"J({"let":"x","value":"x+1"})J"),V{},"body[0].value: unknown identifier 'x'");
    compileRejects(emitAfter(R"J({"if":"1","then":[{"let":"inner","value":1}]},{"let":"y","value":"inner"})J"),V{},"body[1].value: unknown identifier 'inner'");
    compileRejects(emitAfter(R"J({"for":"i","count":2,"body":[]},{"let":"y","value":"i"})J"),V{},"body[1].value: unknown identifier 'i'");
    compileRejects(emitAfter(R"J({"for":"i","count":"i","body":[]})J"),V{},"body[0].count: unknown identifier 'i'");
    compileRejects(program(R"J([{"emit":{"op":"sketch.set","name":"s","sketch":{"plane":"XY","entities":[{"$for":"k","count":"k","item":{"kind":"circle","name":"c{k}","dof":["0","0","1"]}}]}}}])J"),V{},"unknown identifier 'k'");
    compileRejects(program(R"J([{"emit":{"op":"sketch.set","name":"s","sketch":{"plane":"XY","entities":[{"$for":"k","count":2,"item":{"kind":"circle","name":"c{k}","dof":["0","0","1"]}},{"kind":"circle","name":"c{k}","dof":["0","0","2"]}]}}}])J"),V{},"unknown identifier 'k'");
    compileRejects(letProgram("a>100 ? max() : 1",one),V{},"max expects 1+ argument(s)");
    compileRejects(letProgram("a>100 ? sin : 1",one),V{},"unknown identifier 'sin'");
    // Shadowing follows run-time scopes: a nested let hides and updates locally.
    check(emittedName(program(R"J([{"let":"v","value":1},{"for":"v2","count":1,"body":[{"let":"v","value":5},{"set":"v","value":"v+1"},{"emit":{"op":"feature.add","type":"box","name":"B{v}"}}]}])J"))=="t / B6","Nested let shadows and updates locally");
    // set targets an earlier let in scope only.
    compileRejects(emitAfter(R"J({"set":"s","value":1})J"),V{},"PROGRAM_SYNTAX: body[0]: set s has no earlier let in scope");
    compileRejects(emitAfter(R"J({"set":"s","value":1},{"let":"s","value":0})J"),V{},"body[0]: set s has no earlier let in scope");
    compileRejects(emitAfter(R"J({"if":"1","then":[{"let":"s","value":0}]},{"set":"s","value":1})J"),V{},"body[1]: set s has no earlier let in scope");
    compileRejects(emitAfter(R"J({"for":"i","count":2,"body":[{"set":"i","value":5}]})J"),V{},"body[0].body[0]: set i names a for variable");
    compileRejects(emitAfter(R"J({"call":"f","args":{"k":1}})J","{}",R"J({"f":{"params":["k"],"body":[{"set":"k","value":2}]}})J"),V{},"functions.f.body[0]: set k names a function argument");
    compileRejects(emitAfter(R"J({"let":"s","value":0})J","{}",R"J({"f":{"params":["k"],"body":[{"set":"s","value":2}]}})J"),V{},"functions.f.body[0]: set s has no earlier let in scope");
    const auto accumulate=program(R"J([{"call":"f","args":{"k":4}}])J","{}",R"J({"f":{"params":["k"],"body":[{"let":"acc","value":"k"},{"for":"j","count":"k","body":[{"if":"j>0","then":[{"set":"acc","value":"acc+j"}]}]},{"emit":{"op":"feature.add","type":"box","name":"F{acc}"}}]}})J");
    check(emittedName(accumulate)=="t / F10","A function updates its own let from nested blocks");
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
    // Snapshots trust validated bindings; writing a file still validates them.
    {Document corrupt=reopened;corrupt.iterationHistory=V{};V bindings=corrupt.parametricPrograms,entry=bindings.arr[0];entry.set("program_sha256",V::makeStr(std::string(64,'0')));bindings.arr[0]=entry;corrupt.parametricPrograms=bindings;DocumentSaveReport report;const auto target=dir/"corrupt.ark";
        check(!saveDocument(corrupt,target.string().c_str(),&report)&&report.error.find("PROGRAM_HASH")!=std::string::npos&&!std::filesystem::exists(target),"Save validates bindings before writing: "+report.error);}
    V serialized=json(documentToJson(reopened,false,false));check(at(serialized,"version").num==55,"Programs require version 55");serialized.set("version",V::makeNum(54));Document rejected;check(!documentFromJson(serialized.dump(),rejected,&error)&&error.find("version 55")!=std::string::npos,"Reject semantic downgrade: "+error);
    serialized.set("version",V::makeNum(55));V bindings=at(serialized,"parametric_programs");V entry=bindings.arr[0];entry.set("program_sha256",V::makeStr(std::string(64,'0')));bindings.arr[0]=entry;serialized.set("parametric_programs",bindings);check(!documentFromJson(serialized.dump(),rejected,&error)&&error.find("PROGRAM_HASH")!=std::string::npos,"Tampered program digest rejects: "+error);
    commit(s,prepare(s,"remove","fl"));check(s.document.parametricPrograms.isNull()&&!named(s.document,"fl / Flange")&&!s.document.findSketch("fl__outline"),"Remove deletes owned objects and binding");
    check(json(documentToJson(s.document,false,false)).find("parametric_programs")==nullptr,"No program key remains after removal");
    // update installs new source in the same namespace.
    auto renamed=flange;renamed.set("name",V::makeStr("Bolted flange r2"));commit(s,prepare(s,"install","fl",&flange));commit(s,prepare(s,"update","fl",&renamed,json(R"({"thickness":15})")));
    check(at(s.document.parametricPrograms.arr[0],"name").str=="Bolted flange r2"&&named(s.document,"fl / Flange")->p.at("depth")=="15","Update replaces program source and keeps unrelated values");
}

// The curved example is one exact revolved solid. Its volume matches an
// independent pi*integral(x^2 dy) over the authored profile: lines, two cubic
// spans and a quarter arc (a hemispherical head).
void curvedExample(){
    const auto knob=example("turned_knob");AgentSession s;const auto preview=prepare(s,"install","knob",&knob);check(at(preview,"valid").b,"Curved example compiles: "+preview.dump().substr(0,800));commit(s,preview);
    for(const auto& r:s.document.lastResults)check(r.ok,"Curved example evaluates: "+r.error);
    const auto* feature=named(s.document,"knob / Knob");check(feature&&feature->p.at("solid")=="1","Curved example publishes one revolve_surface solid");
    const auto report=at(call(s,"native_inspect",json(R"({"detail":"properties","id":)"+std::to_string(feature->id)+"}")),"report");
    const double height=60,base=22,waist=9,head=15,foot=5,neck=foot+(height-foot-head)/2,shoulder=height-head;
    auto cubic=[](double x0,double y0,double x1,double y1,double x2,double y2,double x3,double y3){double volume=0;const int n=20000;for(int i=0;i<n;++i){const double t=(i+.5)/n,u=1-t,x=u*u*u*x0+3*u*u*t*x1+3*u*t*t*x2+t*t*t*x3,dy=3*u*u*(y1-y0)+6*u*t*(y2-y1)+3*t*t*(y3-y2);volume+=x*x*dy/n;}return K_PI*volume;};
    const double expected=K_PI*base*base*foot+cubic(base,foot,base,(foot+neck)/2,waist,(foot+neck)/2,waist,neck)+cubic(waist,neck,waist,(neck+shoulder)/2,head,(neck+shoulder)/2,head,shoulder)+2./3*K_PI*head*head*head;
    check(at(report,"brep_valid").b&&at(report,"solids").num==1&&at(report,"faces").num==5&&std::abs(at(report,"volume_mm3").num-expected)<expected*1e-6,"Curved example is an exact revolved solid with the authored volume: "+report.dump().substr(0,600)+" expected "+std::to_string(expected));
    commit(s,prepare(s,"configure","knob",nullptr,json(R"({"head_radius":18})")));const auto* edited=named(s.document,"knob / Knob");const auto resized=at(call(s,"native_inspect",json(R"({"detail":"properties","id":)"+std::to_string(edited->id)+"}")),"report");
    check(at(resized,"brep_valid").b&&at(resized,"volume_mm3").num>at(report,"volume_mm3").num,"Curved example re-runs from its parameters");
}

// Boolean, numeric-choice and text-choice parameters. Features use legacy boxes
// so the checks exercise compilation, binding and persistence, not the kernel.
const char* kKinds=R"J({"format":"ark.parametric-program.v1","name":"Kinds",
    "parameters":{"w":{"default":20,"min":10,"max":40,"unit":"mm"},"rail":{"boolean":true,"default":false,"description":"Add a rail"},"size":{"choices":[6,8,10],"default":8,"unit":"mm"},"finish":{"choices":["Oak","Pine","Walnut"],"default":"Oak"}},
    "functions":{"label":{"params":["k"],"body":[{"emit":{"op":"feature.add","type":"box","name":"Tag {k} {@finish}","p":{"dx":"{size}","dy":"{size}","dz":1}}}]}},
    "body":[{"emit":{"op":"feature.add","type":"box","name":"Panel {@finish} {size} #{finish}","material":"{@finish}","p":{"dx":"{w}","dy":"{size}","dz":"{rail ? 3 : 2}"}}},
        {"if":"rail","then":[{"emit":{"op":"feature.add","type":"box","name":"Rail","p":{"dx":"{w}","dy":2,"dz":2}}}]},{"call":"label","args":{"k":"finish+1"}}]})J";
void kinds(const std::filesystem::path& dir){
    const auto source=json(kKinds);auto compiled=compileParametricProgram(source,V{},"t");auto names=[](const V& c){std::string out;for(const auto& cmd:at(c,"commands").arr)out+=at(cmd,"name").str+"|";return out;};
    check(names(compiled)=="t / Panel Oak 8 #0|t / Tag 1 Oak|","Defaults: text inserted by {@finish}, index in expressions, numeric choice, false boolean skips the rail: "+names(compiled));
    const auto& panel=at(compiled,"commands").arr[0];check(at(panel,"material").str=="Oak"&&at(at(panel,"p"),"dz").num==2&&at(at(panel,"p"),"dy").num==8,"Chosen text names an existing project material unchanged: "+panel.dump());
    check(at(compiled,"values").dump()==R"({"finish":"Oak","rail":0,"size":8,"w":20})","Resolved values store text, 1/0 and the listed number: "+at(compiled,"values").dump());
    check(at(compiled,"parameter_kinds").dump()==R"({"w":"number","rail":"boolean","size":"choice","finish":"text_choice"})","Kinds are reported: "+at(compiled,"parameter_kinds").dump());
    compiled=compileParametricProgram(source,json(R"({"finish":"Walnut","rail":true,"size":10})"),"t");
    check(names(compiled)=="t / Panel Walnut 10 #2|t / Rail|t / Tag 3 Walnut|"&&at(at(at(compiled,"commands").arr[0],"p"),"dz").num==3&&at(at(compiled,"values"),"rail").num==1,"true selects the rail, text and index follow the choice: "+names(compiled));
    check(at(at(compileParametricProgram(source,json(R"({"rail":1})"),"t"),"values"),"rail").num==1&&at(at(compileParametricProgram(source,json(R"({"rail":0})"),"t"),"values"),"rail").num==0,"Booleans accept 0 and 1");
    for(const char* bad:{R"({"rail":2})",R"({"rail":"yes"})",R"({"size":7})",R"({"size":"8"})",R"({"finish":"Teak"})",R"({"finish":1})",R"({"finish":"oak"})"})compileRejects(source,json(bad),"PROGRAM_VALUES");
    compileRejects(source,json(R"({"size":7})"),"size=7 is not one of its choices [6,8,10]");
    // Declarations are typed: a choice fixes its domain and a boolean has no unit or bounds.
    const std::string box=R"J([{"emit":{"op":"feature.add","type":"box","name":"B"}}])J";
    for(const auto& bad:std::vector<std::pair<const char*,const char*>>{{R"({"b":{"boolean":true,"default":0,"min":0}})","min does not combine"},{R"({"b":{"boolean":false,"default":0}})","boolean must be true"},{R"({"b":{"boolean":true,"default":2}})","default must be true, false, 0 or 1"},
        {R"({"b":{"boolean":true,"default":true,"unit":"mm"}})","a boolean has no unit"},{R"({"c":{"choices":[1,"a"],"default":1}})","all finite numbers or all text"},{R"({"c":{"choices":[1,1],"default":1}})","choices must be distinct"},{R"({"c":{"choices":[1,2],"default":3}})","default must be one of its choices"},
        {R"({"c":{"choices":[],"default":1}})","choices must list 1..64 values"},{R"({"c":{"choices":["a{b}"],"default":"a{b}"}})","without braces"},{R"({"c":{"choices":[1,2],"default":1,"integer":true}})","integer does not combine"},{R"({"c":{"choices":["a","b"],"default":"a","unit":"mm"}})","text choices have no unit"},
        {R"({"c":{"choices":["a"]}})","needs a default"},{R"({"c":{"choices":["a","b"],"default":"a","boolean":true}})","does not combine with choices"},{R"({"c":{"choices":"a","default":"a"}})","choices must list"}})
        compileRejects(program(box,bad.first),V{},std::string("PROGRAM_PARAMETERS: parameters.")+(bad.first[2]=='b'?"b":"c")),compileRejects(program(box,bad.first),V{},bad.second);
    // {@name} is checked statically, including untaken branches and functions.
    const std::string typed=R"J({"w":{"default":1},"finish":{"choices":["Oak"],"default":"Oak"}})J";
    compileRejects(program(R"J([{"emit":{"op":"feature.add","type":"box","name":"B{@w}"}}])J",typed),V{},"PROGRAM_TEMPLATE: body[0].emit.name: {@w} needs a text-choice parameter; w is a number here");
    compileRejects(program(R"J([{"if":"0","then":[{"emit":{"op":"feature.add","type":"box","name":"B{@nope}"}}]},{"emit":{"op":"feature.add","type":"box","name":"C"}}])J",typed),V{},"body[0].then[0].emit.name: {@nope} needs a text-choice parameter; nope is unknown");
    compileRejects(program(R"J([{"emit":{"op":"feature.add","type":"box","name":"B{@}"}}])J",typed),V{},"{@name} needs a parameter name");
    compileRejects(program(R"J([{"call":"f","args":{"k":1}}])J",typed,R"J({"f":{"params":["k"],"body":[{"emit":{"op":"feature.add","type":"box","name":"B{@k}"}}]}})J"),V{},"functions.f.body[0].emit.name: {@k} needs a text-choice parameter; k is a number here");
    compileRejects(program(R"J([{"let":"x","value":"@finish"},{"emit":{"op":"feature.add","type":"box","name":"C"}}])J",typed),V{},"PROGRAM_EXPRESSION");
    check(emittedName(program(R"J([{"emit":{"op":"feature.add","type":"box","name":"{{@finish}} {@finish}{finish}"}}])J",typed))=="t / {@finish} Oak0","Doubled braces stay literal next to inserted text");

    // A binding with these kinds persists at version 60; numeric-only bindings keep 55.
    AgentSession s;apply(s,R"([{"op":"material.set","name":"Oak","density":0.7},{"op":"material.set","name":"Pine","density":0.5},{"op":"material.set","name":"Walnut","density":0.65}])");
    const auto numeric=program(R"J([{"emit":{"op":"feature.add","type":"box","name":"Block","p":{"dx":"{w}","dy":5,"dz":5}}}])J",R"J({"w":{"default":10,"min":1}})J");
    commit(s,prepare(s,"install","n",&numeric));check(at(json(documentToJson(s.document,false,false)),"version").num==55,"Numeric-only binding keeps version 55");
    commit(s,prepare(s,"install","k",&source,json(R"({"finish":"Pine"})")));for(const auto& r:s.document.lastResults)check(r.ok,"Kinds program evaluates: "+r.error);
    check(named(s.document,"k / Panel Pine 8 #1")&&named(s.document,"k / Panel Pine 8 #1")->material=="Pine"&&!named(s.document,"k / Rail"),"Installed text choice picks the material");
    V inspect=json(R"({"action":"inspect","namespace":"k"})");const auto row=at(data(s.call("parametric_program",inspect)),"programs").arr[0];check(at(at(row,"parameter_kinds"),"finish").str=="text_choice"&&at(at(row,"values"),"finish").str=="Pine"&&!at(row,"local_edits").b,"Inspect reports kinds and text values");
    commit(s,prepare(s,"configure","k",nullptr,json(R"({"rail":true,"size":10})")));const auto& values=at(s.document.parametricPrograms.arr[1],"values");
    check(values.dump()==R"({"finish":"Pine","rail":1,"size":10,"w":20})"&&named(s.document,"k / Rail")&&named(s.document,"k / Panel Pine 10 #1"),"Configure merges kinds and stores 1/0: "+values.dump());
    rejects(s,"parametric_program",json(R"({"action":"prepare","mode":"configure","namespace":"k","values":{"finish":"Teak"}})"),"is not one of its choices");
    rejects(s,"parametric_program",json(R"({"action":"prepare","mode":"configure","namespace":"k","values":{"rail":"true"}})"),"is boolean");
    // The program_configure recipe prepares the same typed call verbatim; the prepared arguments are sent unchanged.
    auto recipe=[&](const V& values){V p=V::makeObj();p.set("expected_revision",V::makeNum((double)s.revision));p.set("namespace",V::makeStr("k"));p.set("values",values);V a=json(R"({"topic":"commands","command":"program_configure"})");a.set("parameters",p);return s.call("agent_onboard",a);};
    auto viaRecipe=[&](const char* values){const auto prepared=at(data(recipe(json(values))),"prepared_call");check(at(prepared,"tool").str=="parametric_program"&&at(at(prepared,"arguments"),"values").dump()==json(values).dump(),"Recipe keeps text and boolean values: "+prepared.dump());commit(s,data(s.call("parametric_program",at(prepared,"arguments"))));return at(s.document.parametricPrograms.arr[1],"values").dump();};
    check(viaRecipe(R"({"finish":"Walnut","rail":false})")==R"({"finish":"Walnut","rail":0,"size":10,"w":20})"&&named(s.document,"k / Panel Walnut 10 #2")&&!named(s.document,"k / Rail"),"Recipe configure applies text and false");
    check(viaRecipe(R"({"finish":"Pine","rail":true})")==R"({"finish":"Pine","rail":1,"size":10,"w":20})"&&named(s.document,"k / Panel Pine 10 #1")&&named(s.document,"k / Rail"),"Recipe configure applies text and true");
    for(const auto& bad:{json(R"({"finish":{"x":1}})"),json(R"({"finish":["Oak"]})"),json(R"({"rail":null})"),json(R"({"finish":""})")}){const auto r=recipe(bad);check(at(r,"isError").b&&r.dump().find("Command")!=std::string::npos,"Recipe rejects a structured, null or empty value: "+r.dump());}
    {V longText=V::makeObj();longText.set("finish",V::makeStr(std::string(65,'a')));const auto r=recipe(longText);check(at(r,"isError").b&&r.dump().find("too long")!=std::string::npos,"Recipe bounds text values to 64 bytes: "+r.dump());}
    const auto saved=documentToJson(s.document,false,false);V serialized=json(saved);check(at(serialized,"version").num==60,"Boolean/choice bindings require version 60");
    Document reopened;std::string error;check(documentFromJson(saved,reopened,&error)&&reopened.parametricPrograms.arr.size()==2&&hash_ok(reopened)&&at(reopened.parametricPrograms.arr[1],"values").dump()==at(s.document.parametricPrograms.arr[1],"values").dump(),"Version 60 reopens: "+error);
    {const auto again=documentToJson(reopened,false,false);size_t k=0;while(k<again.size()&&k<saved.size()&&again[k]==saved[k])++k;check(again==saved,"Version 60 re-saves byte-identically; first difference at "+std::to_string(k)+": "+saved.substr(k>80?k-80:0,200)+" <> "+again.substr(k>80?k-80:0,200));}
    const auto file=(dir/"kinds.ark").string();check(saveDocument(s.document,file.c_str()),"Save version 60 file");Document loaded;check(loadDocument(file.c_str(),loaded,&error)&&documentToJson(loaded,false,false)==saved,"Version 60 file round trip: "+error);
    serialized.set("version",V::makeNum(59));Document rejected;check(!documentFromJson(serialized.dump(),rejected,&error)&&error.find("require document version 60")!=std::string::npos&&rejected.parametricPrograms.isNull(),"Downgrade below 60 rejects: "+error);
    serialized.set("version",V::makeNum(61));check(!documentFromJson(serialized.dump(),rejected,&error)&&error.find("unsupported document version")!=std::string::npos,"Version 61 is a future format: "+error);
    // Stored values still resolve to themselves on load.
    serialized.set("version",V::makeNum(60));V bindings=at(serialized,"parametric_programs");V entry=bindings.arr[1];V stored=at(entry,"values");stored.set("rail",V::makeBool(true));entry.set("values",stored);bindings.arr[1]=entry;serialized.set("parametric_programs",bindings);
    check(!documentFromJson(serialized.dump(),rejected,&error)&&error.find("PROGRAM_VALUES")!=std::string::npos,"A stored boolean must be its canonical 1/0: "+error);
    commit(s,prepare(s,"remove","k"));check(at(json(documentToJson(s.document,false,false)),"version").num==55,"Removing the typed binding returns the document to version 55");
}

// .arkprog exchange lives in the tool; the program language never touches files.
std::string readBytes(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());}
void writeBytes(const std::filesystem::path& p,const std::string& bytes){std::ofstream out(p,std::ios::binary|std::ios::trunc);out<<bytes;}
std::string pathText(const std::filesystem::path& p){const auto s=p.u8string();return {s.begin(),s.end()};}
void exchange(const std::filesystem::path& dir){
    AgentSession s;apply(s,R"([{"op":"material.set","name":"Oak","density":0.7},{"op":"material.set","name":"Walnut","density":0.65}])");const auto source=json(kKinds);commit(s,prepare(s,"install","k",&source,json(R"({"size":6})")));const auto digest=at(s.document.parametricPrograms.arr[0],"program_sha256").str;
    const auto target=dir/"kinds.arkprog";auto exportArgs=[&](const std::filesystem::path& p){V a=json(R"({"action":"export","namespace":"k"})");a.set("path",V::makeStr(pathText(p)));return a;};
    const auto before=documentToJson(s.document,false,false);const auto revision=s.revision;
    const auto exported=call(s,"parametric_program",exportArgs(target));const auto bytes=readBytes(target);
    check(s.revision==revision&&documentToJson(s.document,false,false)==before,"Export leaves the document and revision unchanged");
    check(at(exported,"program_sha256").str==digest&&!at(exported,"replaced").b&&at(exported,"bytes").num==(double)bytes.size()&&at(exported,"file_sha256").str==iterationSha256(bytes),"Export reports the program and file digests: "+exported.dump());
    V file=json(bytes);check(at(file,"format").str=="ark.parametric-program-file.v1"&&at(file,"program_sha256").str==digest&&parametricProgramHash(at(file,"program"))==digest,"The file carries the source and its SHA-256");
    check(bytes.find('\n')==std::string::npos&&bytes.rfind(R"({"format":"ark.parametric-program-file.v1","program":{"body":)",0)==0,"Canonical JSON: sorted keys, no whitespace: "+bytes.substr(0,80));
    // Never replace an existing file unless asked; other rejections write nothing.
    writeBytes(target,"user file");rejects(s,"parametric_program",exportArgs(target),"PROGRAM_FILE_EXISTS");check(readBytes(target)=="user file","Refused export leaves the existing file intact");
    {V a=exportArgs(target);a.set("overwrite",V::makeBool(true));const auto replaced=call(s,"parametric_program",a);check(at(replaced,"replaced").b&&readBytes(target)==bytes,"Explicit overwrite replaces the file with identical bytes");}
    {V a=exportArgs(dir/"other.arkprog");a.set("overwrite",V::makeStr("yes"));rejects(s,"parametric_program",a,"overwrite must be boolean");}
    rejects(s,"parametric_program",exportArgs(std::filesystem::path("relative.arkprog")),"must be an absolute path");
    rejects(s,"parametric_program",exportArgs(dir/"kinds.json"),"must end in .arkprog");
    rejects(s,"parametric_program",exportArgs(dir/"missing"/"kinds.arkprog"),"destination directory does not exist");
    {V a=exportArgs(dir/"other.arkprog");a.set("namespace",V::makeStr("none"));rejects(s,"parametric_program",a,"PROGRAM_NAMESPACE");a=exportArgs(dir/"other.arkprog");a.set("values",json("{}"));rejects(s,"parametric_program",a,"unknown field values");
        a=exportArgs(dir/"other.arkprog");a.set("expected_revision",V::makeNum((double)s.revision+1));const auto r=s.call("parametric_program",a);check(at(r,"isError").b&&r.dump().find("PROGRAM_REVISION_CONFLICT")!=std::string::npos,"Export requires the current revision");}
    check(!std::filesystem::exists(dir/"other.arkprog")&&!std::filesystem::exists(dir/"kinds.json"),"Rejected exports write nothing");
    for(const auto& item:std::filesystem::directory_iterator(dir))check(item.path().extension()!=".tmp","No staged file remains: "+pathText(item.path()));
    // validate/prepare read program_path instead of an inline program.
    auto pathArgs=[&](const char* action,const std::filesystem::path& p){V a=V::makeObj();a.set("action",V::makeStr(action));a.set("program_path",V::makeStr(pathText(p)));return a;};
    const auto validated=data(s.call("parametric_program",pathArgs("validate",target)));check(at(validated,"program_sha256").str==digest&&at(validated,"program_path").str==pathText(target)&&at(validated,"commands").arr.size()==2,"validate reads the .arkprog file");
    {V a=pathArgs("prepare",target);a.set("mode",V::makeStr("install"));a.set("namespace",V::makeStr("copy"));a.set("values",json(R"({"finish":"Walnut"})"));const auto preview=call(s,"parametric_program",a);check(at(preview,"program_path").str==pathText(target),"prepare reports the file it read");commit(s,preview);}
    check(at(s.document.parametricPrograms.arr[1],"program_sha256").str==digest&&named(s.document,"copy / Panel Walnut 8 #2"),"A program read from .arkprog installs with the same digest");
    // Format, digest, size and option checks.
    auto tampered=file;auto changed=at(file,"program");changed.set("name",V::makeStr("Edited"));tampered.set("program",changed);writeBytes(dir/"tampered.arkprog",tampered.dump());rejects(s,"parametric_program",pathArgs("validate",dir/"tampered.arkprog"),"PROGRAM_HASH");
    tampered=file;tampered.set("format",V::makeStr("ark.parametric-program-file.v2"));writeBytes(dir/"format.arkprog",tampered.dump());rejects(s,"parametric_program",pathArgs("validate",dir/"format.arkprog"),"PROGRAM_FORMAT");
    tampered=file;tampered.set("note",V::makeStr("x"));writeBytes(dir/"extra.arkprog",tampered.dump());rejects(s,"parametric_program",pathArgs("validate",dir/"extra.arkprog"),"unknown field note");
    writeBytes(dir/"text.arkprog","not json");rejects(s,"parametric_program",pathArgs("validate",dir/"text.arkprog"),"does not hold a JSON object");
    writeBytes(dir/"large.arkprog",bytes+std::string(256*1024,' '));rejects(s,"parametric_program",pathArgs("validate",dir/"large.arkprog"),"at most 256 KiB");
    rejects(s,"parametric_program",pathArgs("validate",dir/"absent.arkprog"),"No .arkprog file");
    {V a=pathArgs("validate",target);a.set("program",source);rejects(s,"parametric_program",a,"not both");rejects(s,"parametric_program",pathArgs("inspect",target),"program_path is a validate or prepare option");
        a=pathArgs("prepare",target);a.set("mode",V::makeStr("configure"));a.set("namespace",V::makeStr("k"));rejects(s,"parametric_program",a,"configure reuses the installed program");
        a=json(R"({"action":"validate"})");a.set("program",source);a.set("path",V::makeStr(pathText(target)));rejects(s,"parametric_program",a,"path and overwrite are export options");}
}

void discovery(){
    bool published=false,readOnly=true;for(const auto& t:AgentSession::tools().arr)if(at(t,"name").str=="parametric_program"){published=true;readOnly=at(at(t,"annotations"),"readOnlyHint").b;}check(published&&!readOnly,"Public tool discovery includes the program contract");
    AgentSession s;const auto guide=agentOnboard(s,json(R"({"topic":"programs","detail":"guide"})"));check(at(guide,"examples").arr.size()==3&&at(guide,"example_prepare").find("program"),"Onboarding topic programs carries runnable examples");
    const auto overview=agentOnboard(s,json(R"({"topic":"overview","detail":"guide"})"));bool listed=false;for(const auto& t:at(overview,"available_topics").arr)listed|=t.str=="programs";check(listed,"Overview lists the programs topic");
}

int main(){try{
    const auto dir=std::filesystem::temp_directory_path()/("ark-programs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);
    language();hardening();session(dir);provenance();provenanceAcrossEvaluations();curvedExample();kinds(dir);exchange(dir);discovery();
    std::error_code ec;std::filesystem::remove_all(dir,ec);std::cout<<"parametric programs: "<<assertions<<" assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
