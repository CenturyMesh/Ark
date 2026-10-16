#include "ark/agent.hpp"
#include "ark/assembly_io.hpp"
#include "ark/modeling_031b.hpp"
#include "ark/serialize.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
namespace {
int checks=0;
void check(bool value,const std::string& reason){++checks;if(!value)throw std::runtime_error(reason);}
V json(const std::string& source){V value;check(arkjson::parse(source,value),"synthetic fixture JSON");return value;}
bool sameJson(const V& a,const V& b){if(a.t!=b.t)return false;if(a.t==V::Obj){if(a.obj.size()!=b.obj.size())return false;for(const auto& field:a.obj){const auto* value=b.find(field.first.c_str());if(!value||!sameJson(field.second,*value))return false;}return true;}if(a.t==V::Arr){if(a.arr.size()!=b.arr.size())return false;for(size_t i=0;i<a.arr.size();++i)if(!sameJson(a.arr[i],b.arr[i]))return false;return true;}return a.dump()==b.dump();}
V accepted(const V& reply){check(reply.find("isError")&&!reply.find("isError")->b,reply.find("structuredContent")?reply.find("structuredContent")->dump():reply.dump());return *reply.find("structuredContent");}
void apply(AgentSession& session,const char* source){auto args=json(source);args.set("expected_revision",V::makeNum((double)session.revision));accepted(session.call("doc_apply",args));}
void equal(const Mat4& a,const Mat4& b,double tolerance,const char* reason){for(int i=0;i<16;++i)check(std::isfinite(a.m[i])&&std::abs(a.m[i]-b.m[i])<=tolerance,reason);}
const AssemblyPlacement& occurrence(const std::vector<AssemblyPlacement>& rows,u64 node,int copy=0){for(const auto& row:rows)if(row.node==node&&row.copy==copy)return row;throw std::runtime_error("synthetic occurrence missing");}
AssemblyManipulationProgram program(const Document& document){AssemblyManipulationProgram result;std::string error;check(buildAssemblyManipulation(document,result,error),error);return result;}
AssemblyManipulationPreview preview(const AssemblyManipulationProgram& program,u64 driver,u64 mate,const char* mode,double delta){AssemblyManipulationPreview result;std::string error;check(previewAssemblyManipulation(program,driver,mate,mode,delta,result,error),error);return result;}
V request(const AgentSession& session,u64 driver,u64 mate,const char* mode,double delta){V args=V::makeObj(),commands=V::makeArr(),command=V::makeObj();args.set("expected_revision",V::makeNum((double)session.revision));command.set("op",V::makeStr("assembly.drive"));command.set("driver",V::makeNum((double)driver));command.set("mate",V::makeNum((double)mate));command.set("mode",V::makeStr(mode));command.set("delta",V::makeNum(delta));commands.arr.push_back(command);args.set("commands",commands);return args;}
void rejectAtomic(AgentSession& session,const V& args,const char* code){const auto before=documentToJson(session.document);const auto revision=session.revision;const auto reply=session.call("doc_apply",args);check(reply.find("isError")->b&&reply.find("structuredContent")->dump().find(code)!=std::string::npos,reply.dump());check(session.revision==revision&&documentToJson(session.document)==before,"rejection preserves document, revision and history");}
void samePlacements(const std::vector<AssemblyPlacement>& expected,const std::vector<AssemblyPlacement>& actual,const char* reason){check(expected.size()==actual.size(),"all occurrence slots retained");for(const auto& row:expected){const auto& result=occurrence(actual,row.node,row.copy);check(row.feature==result.feature&&row.visible==result.visible&&row.name==result.name,"slot definition routing, visibility and name retained");equal(row.world,result.world,2e-7,reason);}}

void fixture(AgentSession& session){apply(session,R"({"commands":[
 {"op":"param.set","name":"pitch","expr":7},
 {"op":"param.set","name":"origin_x","expr":5},
 {"op":"feature.add","type":"box","name":"Body","p":{"dx":2,"dy":2,"dz":2}},
 {"op":"sketch.set","name":"Stations","sketch":{"plane":"XY","entities":[{"kind":"point","name":"a","dof":[0,0]},{"kind":"point","name":"b","dof":["pitch",0]},{"kind":"point","name":"c","dof":[0,"2*pitch"]},{"kind":"point","name":"d","dof":["3*pitch","pitch"]}]}},
 {"op":"assembly.add","name":"Mount group","p":{"tx":31,"ty":-12,"tz":8,"rx":10,"ry":15,"rz":30}},
 {"op":"assembly.add","name":"Nested group","parent":"Mount group","p":{"tx":4,"ry":12,"rz":-9}},
 {"op":"assembly.add","name":"Ground","parent":"Nested group","feature":"Body","fixed":true,"p":{"tx":"origin_x","ty":2,"tz":3,"rz":17}},
 {"op":"assembly.add","name":"Shoulder","parent":"Nested group","feature":"Body","p":{"tx":"origin_x","ty":2,"tz":3,"rz":17}},
 {"op":"assembly.add","name":"Forearm","parent":"Nested group","feature":"Body","p":{"tx":"origin_x","ty":2,"tz":3,"rz":17}},
 {"op":"assembly.add","name":"Hand","parent":"Nested group","feature":"Body","p":{"tx":"origin_x","ty":2,"tz":3,"rz":17}},
 {"op":"assembly.add","name":"Linear hardware","parent":"Nested group","feature":"Body","p":{"tx":"origin_x","ty":2,"tz":3,"rz":17},"pattern":{"type":"linear","count":4,"dx":"pitch","dy":2,"skips":[2]}},
 {"op":"assembly.add","name":"Sketch hardware","parent":"Nested group","feature":"Body","p":{"tx":"origin_x","ty":2,"tz":3,"rz":17},"pattern":{"type":"sketch_driven","source":{"sketch":"Stations","points":["a","b","c","d"]},"skips":[2]}},
 {"op":"assembly.add","name":"Anchored hardware","parent":"Nested group","feature":"Body","p":{"tx":"origin_x","ty":2,"tz":3,"rz":17},"pattern":{"type":"linear","count":3,"dy":"pitch"}},
 {"op":"mate.add","type":"revolute","a":"Ground","b":"Shoulder","limits":{"min":-20,"max":40}},
 {"op":"mate.add","type":"revolute","a":"Shoulder","b":"Forearm","reference_a":"X","reference_b":"X"},
 {"op":"mate.add","type":"rigid","a":"Forearm","b":"Hand"},
 {"op":"mate.add","type":"rigid","a":"Shoulder","b":"Linear hardware"},
 {"op":"mate.add","type":"rigid","a":"Hand","b":"Sketch hardware"},
 {"op":"mate.add","type":"rigid","a":"Ground","b":"Anchored hardware"}
]})");}

void carriedChainAndPersistence(){
    AgentSession session;fixture(session);const auto before=documentToJson(session.document);const auto revision=session.revision;const auto base=program(session.document);
    check(base.baseline.size()==13&&base.diagnostics.freeDofs==2,"multilevel two-joint chain retains all thirteen occurrences and two joint coordinates");
    check(json(documentToJson(session.document,true,false)).find("version")->num<44,"identity pattern carry does not increase source version");
    const auto linearP=session.document.assembly[6].p,sketchP=session.document.assembly[7].p;
    const auto linearRecipe=assemblyPatternToJson(session.document.assembly[6].pattern),sketchRecipe=assemblyPatternToJson(session.document.assembly[7].pattern);
    const auto moved=preview(base,4,1,"rotate",25);check(!moved.joint.constrained&&moved.joint.movingNodes==std::vector<u64>({4,5,6,7,8}),"selected shoulder carries nested joints and both rigid pattern followers");
    const Mat4 change=occurrence(moved.placements,4).world*occurrence(base.baseline,4).world.inverted();
    for(const auto& row:base.baseline){const bool moves=row.node>=4&&row.node<=8;equal(occurrence(moved.placements,row.node,row.copy).world,moves?change*row.world:row.world,2e-8,"every pattern copy follows the same rigid carrier and anchors stay unchanged");}
    check((occurrence(moved.placements,7,3).world.transformPoint({})-occurrence(moved.placements,7).world.transformPoint({})).len()>20,"copies do not collapse onto seed");
    check(documentToJson(session.document)==before&&session.revision==revision,"drive preview preserves complete authoring and history");
    rejectAtomic(session,request(session,4,1,"rotate",41),"MANIPULATION_LIMIT_VIOLATION");
    rejectAtomic(session,request(session,7,1,"rotate",1),"MANIPULATION_PATTERN_DRIVER_UNSUPPORTED");
    rejectAtomic(session,request(session,4,999,"rotate",1),"MANIPULATION_MATE_NOT_FOUND");
    rejectAtomic(session,request(session,4,4,"rotate",1),"MANIPULATION_UNSUPPORTED_JOINT");
    rejectAtomic(session,request(session,4,1,"translate",1),"MANIPULATION_JOINT_MODE_MISMATCH");
    rejectAtomic(session,request(session,9,1,"rotate",1),"MANIPULATION_FIXED_DRIVER");
    auto bad=request(session,4,1,"rotate",41);bad.find("commands");V commands=*bad.find("commands");commands.arr.insert(commands.arr.begin(),json(R"({"op":"param.set","name":"must_roll_back","expr":1})"));bad.set("commands",commands);rejectAtomic(session,bad,"MANIPULATION_LIMIT_VIOLATION");
    auto candidate=accepted(session.call("doc_preview",request(session,4,1,"rotate",25)));check(candidate.find("valid")->b,"prepared drive validates complete pattern edit recipe");
    V commit=V::makeObj();commit.set("expected_revision",V::makeNum((double)session.revision));commit.set("candidate_id",*candidate.find("candidate_id"));accepted(session.call("doc_apply",commit));
    samePlacements(moved.placements,program(session.document).baseline,"committed frames equal read-only preview");
    check(session.document.assembly[6].p==linearP&&session.document.assembly[7].p==sketchP,"drive retains original expression-based seed transforms");
    auto wantedLinear=linearRecipe,wantedSketch=sketchRecipe;wantedLinear.set("carry_frame",*assemblyPatternToJson(session.document.assembly[6].pattern).find("carry_frame"));wantedSketch.set("carry_frame",*assemblyPatternToJson(session.document.assembly[7].pattern).find("carry_frame"));
    check(sameJson(wantedLinear,assemblyPatternToJson(session.document.assembly[6].pattern))&&sameJson(wantedSketch,assemblyPatternToJson(session.document.assembly[7].pattern)),"source identity, count, expressions and skipped slots unchanged");
    auto serialized=json(documentToJson(session.document,true,false));check(serialized.find("version")->num==44,"nonidentity pattern carry conditionally selects source44");
    Document restored;std::string error;check(documentFromJson(documentToJson(session.document),restored,&error),error);restored.recompute();samePlacements(moved.placements,program(restored).baseline,"JSON roundtrip retains all carried copies");
    serialized.set("version",V::makeNum(43));const auto retained=documentToJson(restored);check(!documentFromJson(serialized.dump(),restored,&error)&&documentToJson(restored)==retained,"source43 cannot reinterpret carry semantics or replace destination");
    const auto path=std::filesystem::temp_directory_path()/("ark-pattern-motion-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".ark");
    check(saveDocument(session.document,path.string().c_str()),"binary design and history save");const bool loaded=loadDocument(path.string().c_str(),restored,&error);std::filesystem::remove(path);check(loaded,error);restored.recompute();samePlacements(moved.placements,program(restored).baseline,"binary roundtrip retains pattern slots");
    AgentSession reopened;reopened.document=std::move(restored);accepted(reopened.call("doc_undo",V::makeObj()));samePlacements(base.baseline,program(reopened.document).baseline,"persisted undo restores original whole-pattern frames");accepted(reopened.call("doc_redo",V::makeObj()));samePlacements(moved.placements,program(reopened.document).baseline,"persisted redo restores carried pattern frames");
    accepted(session.call("doc_apply",request(session,4,1,"rotate",-25)));samePlacements(base.baseline,program(session.document).baseline,"reverse drive retains every authored pattern slot");
    const auto old=program(reopened.document);apply(reopened,R"({"commands":[{"op":"param.set","name":"pitch","expr":9}]})");const auto updated=program(reopened.document);
    check(std::abs((occurrence(updated.baseline,7,1).world.transformPoint({})-occurrence(updated.baseline,7).world.transformPoint({})).len()-std::sqrt(85.))<1e-7,"carried linear pattern still evaluates its spacing expression");
    check((occurrence(updated.baseline,8,3).world.transformPoint({})-occurrence(old.baseline,8,3).world.transformPoint({})).len()>1,"carried sketch pattern remains linked to solved source points");
}

void invalidCarry(){
    AgentSession session;fixture(session);const auto initial=assemblyPatternToJson(session.document.assembly[6].pattern);
    for(int kind=0;kind<9;++kind){V recipe=initial,matrix=V::makeArr();Mat4 frame;
        if(kind==0)frame.m[0]=2;if(kind==1)frame.m[0]=-1;if(kind==2)frame.m[4]=.01;if(kind==3)frame.m[3]=1;if(kind==4)frame.m[15]=0;if(kind==5)frame.m[12]=1000001;
        for(double value:frame.m)matrix.arr.push_back(V::makeNum(value));if(kind==6)matrix.arr.pop_back();if(kind==7)matrix.arr[4]=V::makeStr("pitch");if(kind==8)matrix.arr[0]=V::makeNum(std::numeric_limits<double>::infinity());recipe.set("carry_frame",matrix);
        AssemblyPattern parsed;bool rejected=false;try{readAssemblyPattern(parsed,recipe);}catch(const std::exception& e){rejected=std::string(e.what()).find("ASSEMBLY_PATTERN_INVALID_CARRY_FRAME")!=std::string::npos;}check(rejected,"nonrigid/nonfinite/nonaffine/unbounded/malformed carry matrix rejects");
        if(kind<8){V args=V::makeObj(),command=V::makeObj(),commands=V::makeArr();args.set("expected_revision",V::makeNum((double)session.revision));command.set("op",V::makeStr("assembly.edit"));command.set("id",V::makeNum(7));command.set("pattern",recipe);commands.arr.push_back(command);args.set("commands",commands);rejectAtomic(session,args,"ASSEMBLY_PATTERN_INVALID_CARRY_FRAME");}
    }
    auto identityRecipe=initial;V matrix=V::makeArr();for(double value:Mat4{}.m)matrix.arr.push_back(V::makeNum(value));identityRecipe.set("carry_frame",matrix);AssemblyPattern identity;readAssemblyPattern(identity,identityRecipe);check(!assemblyPatternToJson(identity).find("carry_frame"),"identity frame canonicalizes to omitted legacy representation");
    auto legacy=json(documentToJson(session.document,true,false));auto* assembly=legacy.find("assembly");auto nodes=*assembly;nodes.arr[6].set("pattern",identityRecipe);legacy.set("assembly",nodes);legacy.set("version",V::makeNum(43));Document discarded;std::string error;check(!documentFromJson(legacy.dump(),discarded,&error),"old source rejects even explicit identity carry field");
}

Document coupledFixture(){
    Document d;Feature body;body.type=FeatureType::Box;body.p={{"dx","2"},{"dy","2"},{"dz","2"}};d.addFeature(body);
    for(u64 id=1;id<=6;++id){AssemblyNode n;n.id=id;n.feature=1;n.name="Coupled "+std::to_string(id);n.fixed=id==1||id==3;if(id==3||id==4||id==5)n.p["tx"]="100";if(id>=5){n.pattern.active=true;n.pattern.type="linear";n.pattern.count="4";n.pattern.dx="7";n.pattern.dy="3";n.pattern.skips={2};}d.assembly.push_back(n);}
    auto mate=[&](u64 a,u64 b,const char* type){AssemblyMate m;m.id=d.nextMateId_++;m.a=a;m.b=b;m.type=type;d.mates.push_back(m);};mate(1,2,"revolute");mate(3,4,"revolute");mate(2,4,"gear");d.mates.back().motion={{"teeth_a","48"},{"teeth_b","16"}};mate(4,5,"rigid");mate(2,6,"rigid");d.nextAssemblyId_=7;d.recompute();return d;
}
void constrainedPatternFollowers(){
    AgentSession session;session.document=coupledFixture();const auto base=program(session.document);check(base.diagnostics.freeDofs==2,"geometric rank excludes ideal gear equation as before");
    for(double angle:{-20.,5.,20.}){const auto moved=preview(base,2,1,"rotate",angle);check(moved.joint.constrained&&moved.continuationSteps>0&&moved.couplings.arr.size()==1,"pattern followers participate in actual geared continuation");
        check(std::abs(occurrence(moved.placements,4).world.m[1]-std::sin(deg2rad(-3*angle)))<1e-8,"independent output carrier follows measured gear ratio");
        for(const auto& row:base.baseline){const Mat4 delta=occurrence(moved.placements,row.node).world*occurrence(base.baseline,row.node).world.inverted();equal(occurrence(moved.placements,row.node,row.copy).world,delta*row.world,1e-8,"constrained solver carries original relative placement of each copy");}
    }
    const auto moved=preview(base,2,1,"rotate",20);accepted(session.call("doc_apply",request(session,2,1,"rotate",20)));samePlacements(moved.placements,program(session.document).baseline,"constrained commit preserves pattern hardware around both shafts");
    accepted(session.call("doc_apply",request(session,2,1,"rotate",-20)));samePlacements(base.baseline,program(session.document).baseline,"reverse constrained drive restores all stable slots");
    AgentSession linked;fixture(linked);apply(linked,R"({"commands":[{"op":"assembly.add","name":"Inherited hardware","parent":"Nested group","feature":"Body","p":{"tx":"origin_x","ty":2,"tz":3,"rz":17},"pattern":{"type":"pattern_driven","source":{"node":"Linear hardware"}}}]})");
    rejectAtomic(linked,request(linked,4,1,"rotate",10),"MANIPULATION_PATTERN_SOURCE_CONFLICT");
}

#ifdef ARK_CONTRACTS_NATIVE_TESTS
void exactSweepAndCable(){
    AgentSession session;apply(session,R"({"commands":[
     {"op":"feature.add","type":"native_solid","name":"Block","native":{"op":"box"},"p":{"width":2,"depth":2,"height":2}},
     {"op":"assembly.add","name":"Fixture","feature":"Block","fixed":true},
     {"op":"assembly.add","name":"Slider","feature":"Block"},
     {"op":"assembly.add","name":"Follower hardware","feature":"Block","pattern":{"type":"linear","count":4,"dx":5,"skips":[2]}},
     {"op":"mate.add","type":"prismatic","a":"Fixture","b":"Slider","limits":{"min":0,"max":6}},
     {"op":"mate.add","type":"rigid","a":"Slider","b":"Follower hardware"},
     {"op":"feature.add","type":"cable_route","name":"Pattern bound cable","inputs":["Block"],"native":{"start":{"node":1,"offset":[10,0,10]},"end":{"node":3,"copy":3,"offset":[20,0,10]},"radius":0.1,"bend_radius":0.3,"obstacles":[{"node":1}]}}
    ]})");
    const auto before=documentToJson(session.document);auto args=json(R"({"driver":2,"mate":1,"mode":"translate","from":0,"to":6,"samples":3,"pairs":[{"a":1,"b":3,"copy_b":1},{"a":1,"b":3,"copy_b":3}],"cables":[2]})");
    const auto result=inspectAssemblySweep031B(session.document,args);check(result.find("sample_count")->num==3&&result.find("complete_requested_samples")->b&&!result.find("between_sample_clearance_checked")->b,"sampled inspection keeps explicit finite sampling scope");
    const auto& first=result.find("samples")->arr.front();const auto& last=result.find("samples")->arr.back();const auto* measurement=last.find("pairs")->arr[0].find("measurement");
    const auto* authority=measurement?measurement->find("authority"):nullptr;const auto* distance=measurement?measurement->find("min_distance_mm"):nullptr;
    check(authority&&authority->t==V::Str&&authority->str=="exact_native_BRep_distance_and_boolean_common"&&distance&&distance->t==V::Num&&std::abs(distance->num-5)<1e-6,"exact clearance measures actual non-seed copy after carried translation");
    check(std::abs(first.find("cables")->arr[0].find("report")->find("route_length_mm")->num-25)<1e-6&&std::abs(last.find("cables")->arr[0].find("report")->find("route_length_mm")->num-std::sqrt(661.))<1e-6,"sampled exact cable uses selected stable pattern copy world transform");
    check(last.find("cables")->arr[0].find("report")->find("exact_obstacle_checked")->b,"each native cable still checks actual exact obstacle geometry");
    check(documentToJson(session.document)==before,"exact sweep is read-only");
    const auto moved=preview(program(session.document),2,1,"translate",6);accepted(session.call("doc_apply",request(session,2,1,"translate",6)));samePlacements(moved.placements,program(session.document).baseline,"native pattern translation commit matches exact sampled poses");
    const auto inspected=accepted(session.call("electronics_inspect",json("{\"id\":2}")));check(std::abs(inspected.find("route_length_mm")->num-std::sqrt(661.))<1e-6,"committed pattern carry invalidates bound cable evaluation correctly");
    auto invalid=args;invalid.set("to",V::makeNum(1));bool failed=false;try{(void)inspectAssemblySweep031B(session.document,invalid);}catch(const std::exception& e){failed=std::string(e.what()).find("LIMIT_VIOLATION")!=std::string::npos;}check(failed,"later sweep exceeding current stop rejects rather than dropping a sample");
}
#endif
}
int main(){try{carriedChainAndPersistence();invalidCarry();constrainedPatternFollowers();
#ifdef ARK_CONTRACTS_NATIVE_TESTS
    exactSweepAndCable();
#endif
    std::cout<<"032D patterned assembly motion: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"032D patterned assembly motion failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
