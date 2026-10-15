#include "ark/agent.hpp"
#include "ark/assembly_authoring.hpp"
#include "ark/bom.hpp"
#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include "native_process.hpp"
#include <iostream>
using namespace ark;
using V=arkjson::Value;
namespace {
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
V json(const std::string& source){V v;check(arkjson::parse(source,v),"fixture JSON parses");return v;}
V accepted(const V& reply){check(reply.find("isError")&&!reply.find("isError")->b,reply.dump());return *reply.find("structuredContent");}
void apply(AgentSession& s,const char* text){auto args=json(text);args.set("expected_revision",V::makeNum((double)s.revision));accepted(s.call("doc_apply",args));}
void reject(AgentSession& s,const char* text,const char* reason){auto args=json(text);args.set("expected_revision",V::makeNum((double)s.revision));auto before=documentToJson(s.document);auto revision=s.revision;auto reply=s.call("doc_apply",args);check(reply.find("isError")->b&&reply.dump().find(reason)!=std::string::npos,"configuration rejection reports cause");check(s.revision==revision&&documentToJson(s.document)==before,"rejection preserves complete document/revision/history");}
void configurations(){
    AgentSession s;apply(s,R"({"commands":[{"op":"param.set","name":"width","expr":10},{"op":"param.set","name":"twice","expr":"width*2"},{"op":"feature.add","name":"Part","type":"box","p":{"dx":"width","dy":5,"dz":2}},{"op":"assembly.add","name":"Module","feature":0},{"op":"assembly.add","name":"Left","feature":1,"parent":1},{"op":"assembly.add","name":"Right","feature":1,"parent":1,"p":{"tx":"twice"}}]})");
    apply(s,R"({"commands":[{"op":"configuration.set","name":"Wide","parameters":{"width":25},"visibility":{"3":false}},{"op":"configuration.set","name":"Narrow","parameters":{"width":10},"visibility":{"3":true}},{"op":"configuration.apply","name":"Wide"}]})");check(s.document.getParam("twice")==50&&!s.document.assembly[2].visible,"configuration cascades parameters and occurrence visibility");
    std::vector<AssemblyPlacement> p;std::string error;check(resolveAssembly(s.document,p,error)&&p.size()==2&&p[1].world.m[12]==50,"one definition and all reused occurrences follow configured dimensions/placement");
    reject(s,R"({"commands":[{"op":"configuration.set","name":"Cycle","parameters":{"width":"twice"}},{"op":"configuration.apply","name":"Cycle"}]})","Unresolved parameter");
    reject(s,R"({"commands":[{"op":"configuration.set","name":"Broken","parameters":{"width":"unknown","twice":42}},{"op":"configuration.apply","name":"Broken"}]})","Unresolved parameter");
    reject(s,R"({"commands":[{"op":"configuration.set","name":"Missing","parameters":{"missing":4}}]})","missing parameter");
    reject(s,R"({"commands":[{"op":"configuration.set","name":"Unknown","unexpected":true}]})","Unknown assembly");
    reject(s,R"({"commands":[{"op":"assembly.load","roots":[999]}]})","missing");
    const auto saved=documentToJson(s.document);Document restored;check(documentFromJson(saved,restored,&error),error);check(restored.assemblyConfigurations.dump()==s.document.assemblyConfigurations.dump(),"named configurations and expressions survive journaled persistence");auto parsed=json(saved);parsed.set("version",V::makeNum(39));check(!documentFromJson(parsed.dump(),restored,&error),"format downgrade rejects new constructs");
    apply(s,R"({"commands":[{"op":"configuration.apply","name":"Narrow"}]})");check(s.document.getParam("width")==10&&s.document.assembly[2].visible,"explicit configuration switch restores declared values");accepted(s.call("doc_undo",V::makeObj()));check(s.document.getParam("width")==25,"undo retains configuration history");accepted(s.call("doc_redo",V::makeObj()));check(s.document.getParam("width")==10,"redo retains configuration history");
}
void sourceOwnedConfigurations(){
    AgentSession s;apply(s,R"({"commands":[{"op":"skeleton.set","source":"input width = 10\nrelation height = width/2\n"},{"op":"feature.add","type":"box","name":"Source-controlled part","p":{"dx":"width","dy":"height","dz":2}},{"op":"configuration.set","name":"Wide","parameters":{"width":25}},{"op":"configuration.apply","name":"Wide"}]})");
    check(s.document.getParam("width")==25&&s.document.getParam("height")==12.5,"configuration persists source input override and recomputes source-owned relations");
    check(s.document.skeleton.find("overrides")->find("width")->str=="25","configuration updates the authoritative Skeleton input override");
    s.document.recompute();check(s.document.getParam("width")==25,"ordinary recomputation cannot reset configured Skeleton input");
    const auto saved=documentToJson(s.document);Document restored;std::string error;check(documentFromJson(saved,restored,&error),error);restored.recompute();
    check(restored.getParam("width")==25&&restored.getParam("height")==12.5&&restored.skeleton.dump()==s.document.skeleton.dump(),"configured source input survives complete persistence and refresh");
    reject(s,R"({"commands":[{"op":"configuration.set","name":"Invalid relation edit","parameters":{"width":40,"height":99}},{"op":"configuration.apply","name":"Invalid relation edit"}]})","source-owned");
    check(s.document.getParam("width")==25&&s.document.skeleton.find("overrides")->find("width")->str=="25","derived relation rejection preserves the previous source input override");
}
void selectiveLargeFixture(){
    Document d;for(int i=0;i<8;++i){Feature f;f.name="Definition "+std::to_string(i);f.type=FeatureType::Box;f.p={{"dx",std::to_string(i+1)},{"dy","2"},{"dz","3"}};d.addFeature(f);}
    // 3000 placed occurrences share eight definitions across 50 subassemblies.
    for(int group=0;group<50;++group){u64 root=(u64)group*61+1;d.assembly.push_back({root,0,0,"Module "+std::to_string(group),{},true});for(int n=0;n<60;++n)d.assembly.push_back({root+n+1,root,(u64)(group%8+1),"Occurrence",{{"tx",std::to_string(n*10)}},true});}d.nextAssemblyId_=3051;d.recompute();check(d.evalBodies.size()==8,"large fixture initially evaluates eight reused definitions");
    std::string error;check(applyAssemblyAuthoringCommand(d,json(R"({"op":"assembly.load","roots":[1]})"),error)&&error.empty(),error);d.recompute();check(d.features.size()==8&&d.evalBodies.size()==1,"selective loading skips seven definition evaluations while retaining all authoring");check(d.recomputeEvaluated+d.recomputeReused==1,"selective load does not run excluded geometry jobs");check(isAssemblyNodeLoaded(d,2)&&!isAssemblyNodeLoaded(d,63),"node subtree loading distinguishes shared definitions and occurrences");
    std::vector<AssemblyPlacement> p;check(resolveAssembly(d,p,error),error);check(p.size()==3000&&std::count_if(p.begin(),p.end(),[](auto& p){return p.visible;})==60,"selective viewport scope includes exactly selected 60 occurrences");auto bom=inspectBom(d,42);size_t quantity=0,unloaded=0;for(const auto& row:bom.find("items")->arr)quantity+=(size_t)row.find("quantity")->num;for(const auto& row:bom.find("occurrences")->arr)unloaded+=!row.find("loaded")->b;check(quantity==3000&&unloaded==2940,"BOM retains physical quantities and explicitly flags unloaded occurrences");
    auto saved=documentToJson(d);Document restored;check(documentFromJson(saved,restored,&error),error);restored.recompute();check(restored.evalBodies.size()==1&&restored.features.size()==8,"selective authoring roundtrip preserves lazy scope");check(applyAssemblyAuthoringCommand(restored,json(R"({"op":"assembly.load","roots":null})"),error)&&error.empty(),error);restored.recompute();check(restored.evalBodies.size()==8,"reset load selection regenerates every retained definition");
    d.assemblyLoadSelection=json("[]");d.mates.push_back({1,2,63,"distance","origin","origin","0"});auto loaded=assemblyLoadedFeatures(d);check(loaded.count(1)&&loaded.count(2),"mate endpoint definitions survive an otherwise empty selection");
    d.mates.push_back({2,1,62,"distance","origin","origin","0"});loaded=assemblyLoadedFeatures(d);check(!loaded.count(0)&&loaded.count(1)&&loaded.count(2),"Group mate endpoints retain their constraint without inventing a feature-zero dependency");
}
void selectiveInventory(){
    AgentSession s;
    apply(s,R"({"commands":[{"op":"feature.add","type":"box","name":"Source","p":{"dx":10,"dy":5,"dz":2}},{"op":"feature.add","type":"transform","name":"Selected","inputs":[1],"p":{"tx":20}},{"op":"feature.add","type":"transform","name":"Unloaded consumer","inputs":[2],"p":{"tx":40}},{"op":"feature.add","type":"box","name":"Other","p":{"dx":2,"dy":3,"dz":4}},{"op":"assembly.add","name":"Selected occurrence","feature":2},{"op":"assembly.add","name":"Other consumer occurrence","feature":3},{"op":"assembly.add","name":"Other occurrence","feature":4},{"op":"assembly.load","roots":[1]}]})");
    auto inventory=[&](const char* expected){
        const auto before=documentToJson(s.document);const auto revision=s.revision;
        const auto generation=s.document.evaluationGeneration;const auto evaluated=s.document.recomputeEvaluated,reused=s.document.recomputeReused;
        const auto report=accepted(s.call("doc_info",V::makeObj()));const auto rows=json(expected);const auto& features=report.find("features")->arr;
        check(features.size()==4&&report.find("feature_count")->num==4,"selective inventory retains loaded and unloaded authored definitions");
        for(size_t i=0;i<features.size();++i){
            const auto& row=features[i];const auto& state=rows.arr[i].arr;
            check(row.find("id")->num==i+1&&row.find("history_active")->b,"selective loading preserves full authored order and history activity");
            check(row.find("evaluation_active")->b==state[0].b&&row.find("output")->b==state[1].b,"selective inventory retains dependencies and ignores unloaded consumers");
        }
        check(s.revision==revision&&documentToJson(s.document)==before&&s.document.evaluationGeneration==generation&&s.document.recomputeEvaluated==evaluated&&s.document.recomputeReused==reused,"selective inventory does not mutate authoring, revision or evaluation");
    };
    check(s.document.evalBodies.size()==2,"selected transformed occurrence retains its source dependency");
    inventory(R"([[true,false],[true,true],[false,false],[false,false]])");
    apply(s,R"({"commands":[{"op":"assembly.load","roots":[3]}]})");
    inventory(R"([[false,false],[false,false],[false,false],[true,true]])");
    apply(s,R"({"commands":[{"op":"assembly.load","roots":[]}]})");
    inventory(R"([[false,false],[false,false],[false,false],[false,false]])");
    apply(s,R"({"commands":[{"op":"assembly.load","roots":null}]})");
    inventory(R"([[true,false],[true,false],[true,true],[true,true]])");
}
void pins(){
    native_test::TempDirectory temp("ark-pinned-source-");auto path=temp.path()/"source.step";std::string bytes="isolated source hash fixture; no geometry qualification";{std::ofstream out(path,std::ios::binary);out<<bytes;}
    Document d;Feature f;f.type=FeatureType::NativeSolid;f.nativeSpec=json(R"({"op":"imported_brep","package":{}})");V package=V::makeObj();package.set("source_sha256",V::makeStr(iterationSha256(bytes)));f.nativeSpec.set("package",package);d.addFeature(f);V c=json(R"({"op":"assembly.external.set","feature":1,"source_revision":"fixture-1"})");c.set("path",V::makeStr(native_test::pathUtf8(path)));c.set("source_sha256",V::makeStr(iterationSha256(bytes)));std::string error;check(applyAssemblyAuthoringCommand(d,c,error)&&error.empty(),error);verifyAssemblyExternalReferences(d);{std::ofstream out(path,std::ios::binary);out<<bytes<<"changed";}bool rejected=false;try{verifyAssemblyExternalReferences(d);}catch(const std::exception& e){rejected=std::string(e.what()).find("EXTERNAL_REFERENCE_DRIFT")!=std::string::npos;}check(rejected,"external source byte drift rejects without replacing imported geometry");check(d.assemblyExternalReferences.arr.size()==1,"drift retains original reference recipe");
}
void semanticSelectiveClosure(){
    Document d;for(int i=0;i<8;++i){Feature f;f.name="Definition "+std::to_string(i+1);f.type=FeatureType::Box;f.p={{"dx","3"},{"dy","4"},{"dz","5"}};d.addFeature(f);}
    DatumPlane datum;datum.name="Raised";datum.p={{"oz","7"}};d.planes.push_back(datum);
    NamedSketch profile;profile.name="Profile";profile.sk.plane="Raised";SketchEnt project;project.kind=SketchEnt::Project;project.name="Projected";project.sourceFeature=1;project.sourceInstance=3;profile.sk.ents.push_back(project);d.sketches.push_back(profile);
    NamedSketch unrelated;unrelated.name="Unrelated";d.sketches.push_back(unrelated);d.skeleton=json(R"({"source":"input width = 3\n"})");
    d.find(5)->p["sketch"]="Profile";
    d.assembly={{1,0,5,"Selected",{},true},{2,0,2,"Unselected patterned",{},true},{3,0,1,"Projection occurrence",{},true},{4,0,3,"Native boolean occurrence",{},true}};d.assemblyLoadSelection=json("[1]");
    auto loaded=assemblyLoadedFeatures(d);check(loaded.size()==2&&loaded.count(1)&&loaded.count(5),"Sketch projection, named datum and Skeleton presence retain exact source closure without loading unrelated definitions");
    d.assembly[1].pattern.active=true;d.assembly[1].pattern.type="linear";loaded=assemblyLoadedFeatures(d);check(loaded.size()==2,"Simple unselected patterns do not force every definition into memory");
    d.assembly[1].pattern.type="chain";d.assembly[1].pattern.source=json(R"({"feature":4,"alternate_feature":3})");loaded=assemblyLoadedFeatures(d);check(loaded.size()==4&&loaded.count(4)&&loaded.count(3)&&!loaded.count(2),"Unselected pattern inventory retains its guide and alternate definition without loading unrelated seed geometry");
    d.assembly[1].pattern.active=false;d.find(5)->nativeSpec=json(R"({"instances":[{"node":4}]})");loaded=assemblyLoadedFeatures(d);check(loaded.size()==3&&loaded.count(3),"Placed native input adds its source definition to selected feature closure");
    d.find(5)->nativeSpec=json(R"({"instances":[999]})");bool rejected=false;try{assemblyLoadedFeatures(d);}catch(const std::exception&){rejected=true;}check(rejected,"Missing semantic occurrence rejects rather than silently dropping the input");
    d.find(5)->nativeSpec=json(R"({"instances":[4]})");d.assembly[3].pattern.active=true;d.assembly[3].pattern.source=json(R"({"node":4})");rejected=false;try{assemblyLoadedFeatures(d);}catch(const std::exception&){rejected=true;}check(rejected,"Cyclic occurrence source rejects rather than recursing or widening the selection");
}
void historyVisibility(){
    Document d;for(int i=0;i<2;++i){Feature f;f.type=FeatureType::Box;f.p={{"dx","4"},{"dy","3"},{"dz","2"}};d.addFeature(f);d.assembly.push_back({(u64)i+1,0,(u64)i+1,"Placed",{},true});}
    d.historyTip=1;d.recompute();std::vector<AssemblyPlacement> placements;std::string error;check(resolveAssembly(d,placements,error),error);
    check(placements.size()==2&&placements[0].visible&&!placements[1].visible&&d.assembly[1].visible,"rollback hides inactive occurrence without rewriting authored visibility");
    auto bom=inspectBom(d,10);check(bom.find("items")->arr.size()==2&&bom.find("occurrences")->arr[0].find("history_active")->b&&!bom.find("occurrences")->arr[1].find("history_active")->b,"assembly BOM retains physical quantities and flags inactive history sources");
}
}
int main(){try{configurations();sourceOwnedConfigurations();selectiveLargeFixture();selectiveInventory();pins();semanticSelectiveClosure();historyVisibility();std::cout<<"B.04 configurations, source input ownership, 3000 shared occurrences, semantic selective evaluation, persistence, rollback visibility and pin rejection passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
