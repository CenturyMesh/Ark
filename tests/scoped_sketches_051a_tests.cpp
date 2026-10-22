#include "ark/agent.hpp"
#include "ark/api.h"
#include "ark/serialize.hpp"
#include "ark/sketch.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <filesystem>
#include <chrono>
using namespace ark;
using V=arkjson::Value;
static int checks=0,failures=0;
static void check(bool ok,const std::string& message){++checks;if(!ok){++failures;std::cerr<<message<<"\n";}}
static V json(const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("Invalid test JSON");return v;}
static bool failed(const V& v){return v.find("isError")&&v.find("isError")->b;}
static V apply(AgentSession& session,V commands){V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum((double)session.revision));return session.call("doc_apply",args);}
static V sketch(u64 owner,const std::string& name,double radius=2){auto c=json(R"({"op":"sketch.set","name":"Profile","sketch":{"entities":[{"kind":"circle","name":"Ring","dof":[0,0,2]}]}})");c.set("name",V::makeStr(name));if(owner)c.set("owner_feature",V::makeNum((double)owner));auto s=*c.find("sketch"),es=*s.find("entities");auto dof=*es.arr[0].find("dof");dof.arr[2]=V::makeNum(radius);es.arr[0].set("dof",dof);s.set("entities",es);c.set("sketch",s);return c;}
static V one(V command){V a=V::makeArr();a.arr.push_back(std::move(command));return a;}
static void nativeProjection(){
    AgentSession s;auto result=apply(s,json(R"([{"op":"feature.add","type":"native_solid","name":"Source definition","native":{"op":"box"},"p":{"width":5,"depth":5,"height":2}},{"op":"feature.add","type":"native_solid","name":"Target definition","native":{"op":"box"},"p":{"width":5,"depth":5,"height":2}},{"op":"sketch.set","owner_feature":1,"name":"Profile","sketch":{"entities":[{"kind":"circle","name":"Ring","dof":[0,0,3]}]}},{"op":"sketch.set","owner_feature":2,"name":"Profile","sketch":{"entities":[{"kind":"project","name":"Projected","source_sketch":"part:1::Profile","source_entity":"Ring","mode":"normal"}]}},{"op":"feature.edit","id":2,"native":{"op":"extrude"},"p":{"sketch":"part:2::Profile","depth":2}}])"));
    check(!failed(result),result.dump());if(failed(result))return;
    auto volume=[&]{auto r=s.call("native_inspect",json(R"({"id":2,"detail":"properties"})"));if(failed(r))throw std::runtime_error(r.dump());return r.find("structuredContent")->find("report")->find("volume_mm3")->num;};
    check(std::abs(volume()-18*std::acos(-1.))<1e-6,"Cross-scope projection feeds exact native extrusion");
    check(!failed(apply(s,json(R"([{"op":"sketch.patch","owner_feature":1,"name":"Profile","entity":"Ring","r":4}])"))),"Source scoped patch recomputes projected consumer");
    check(std::abs(volume()-32*std::acos(-1.))<1e-6,"Exact volume follows scoped source edit");
    const auto baseline=documentToJson(s.document);const auto revision=s.revision;
    check(failed(apply(s,json(R"([{"op":"sketch.remove","owner_feature":1,"name":"Profile"}])")))&&s.revision==revision&&documentToJson(s.document)==baseline,"Removing a referenced cross-scope source rolls back");
    check(failed(apply(s,json(R"([{"op":"sketch.entity.set","owner_feature":2,"name":"Profile","entity":{"kind":"project","name":"Projected","source_sketch":"Profile","source_entity":"Ring","mode":"normal"}}])")))&&s.revision==revision,"Unqualified cross-scope projection rejects without aliasing");
    check(failed(apply(s,json(R"([{"op":"sketch.entity.set","owner_feature":1,"name":"Profile","entity":{"kind":"project","name":"Ring","source_sketch":"part:2::Profile","source_entity":"Projected","mode":"normal"}}])")))&&s.revision==revision,"Cross-scope source cycles reject atomically");
    Document loaded;std::string error;check(documentFromJson(baseline,loaded,&error),error);loaded.recompute();check(loaded.sketches.size()==2&&loaded.sketches[1].error.empty()&&loaded.lastResults[1].ok,"Cross-scope projection survives save/reload and exact recomputation");
}
// Integration mode links against the combined 051A history implementation.
// Kept separate so the scope-only worktree still runs its own core/native tests.
static void compactedHistory(){
    namespace fs=std::filesystem;AgentSession s;std::string error;
    auto accepted=[](const V& response){if(failed(response))throw std::runtime_error(response.dump());return *response.find("structuredContent");};
    check(documentFromJson(R"({"version":5,"features":[],"sketches":[{"name":"Profile","entities":[{"kind":"circle","name":"Ring","dof":[0,0,7]}]}]})",s.document,&error),error);
    accepted(apply(s,json(R"([{"op":"feature.add","type":"extrude","name":"Left","p":{"height":2}},{"op":"feature.add","type":"extrude","name":"Right","p":{"height":3}},{"op":"sketch.set","owner_feature":1,"name":"Profile","sketch":{"entities":[{"kind":"circle","name":"Ring","dof":[0,0,2]}]}},{"op":"sketch.set","owner_feature":2,"name":"Profile","sketch":{"entities":[{"kind":"circle","name":"Ring","dof":[0,0,3]}]}},{"op":"feature.edit","id":1,"p":{"sketch":"part:1::Profile"}},{"op":"feature.edit","id":2,"p":{"sketch":"part:2::Profile"}}])")));
    const auto a=accepted(apply(s,json(R"([{"op":"sketch.patch","owner_feature":1,"name":"Profile","entity":"Ring","r":4}])")));
    const auto b=accepted(apply(s,json(R"([{"op":"sketch.patch","owner_feature":2,"name":"Profile","entity":"Ring","r":5}])")));
    V restore=V::makeObj();restore.set("id",*a.find("iteration_id"));restore.set("expected_revision",V::makeNum((double)s.revision));accepted(s.call("iteration_restore",restore));
    accepted(apply(s,json(R"([{"op":"sketch.patch","owner_feature":1,"name":"Profile","entity":"Ring","r":6}])")));
    const auto original=documentToJson(s.document),authored=documentToJson(s.document,false,false);
    const auto directory=fs::temp_directory_path()/("ark-scoped-history-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(directory);
    const auto archive=directory/"archive.ark",compacted=directory/"compacted.ark";
    V compact=V::makeObj(),retained=V::makeArr();compact.set("archive_path",V::makeStr(archive.string()));compact.set("confirm",V::makeBool(true));compact.set("expected_revision",V::makeNum((double)s.revision));compact.set("keep_recent",V::makeNum(0));retained.arr.push_back(*b.find("iteration_id"));compact.set("retain_ids",retained);
    const auto receipt=accepted(s.call("history_compact",compact));check(documentToJson(s.document,false,false)==authored,"Compaction preserves canonical scopes and legacy global authoring byte-for-byte");
    std::string mapped;for(const auto& row:receipt.find("compaction")->find("retained_ids")->arr)if(row.find("original_id")->str==b.find("iteration_id")->str)mapped=row.find("id")->str;check(!mapped.empty(),"Selected scoped branch has retained lineage mapping");
    check(saveDocument(s.document,compacted.string().c_str()),"Compacted scoped document saves");AgentSession reopened;check(loadDocument(compacted.string().c_str(),reopened.document,&error),error);
    restore.set("id",V::makeStr(mapped));restore.set("expected_revision",V::makeNum((double)reopened.revision));accepted(reopened.call("iteration_restore",restore));
    auto correct=[](const Document& doc){return doc.sketches.size()==3&&doc.sketches[0].ownerFeature==0&&doc.sketches[0].name=="Profile"&&doc.sketches[0].sk.ents[0].dof[2]==7&&doc.sketches[1].ownerFeature==1&&doc.sketches[1].name=="part:1::Profile"&&doc.sketches[1].sk.ents[0].dof[2]==4&&doc.sketches[2].ownerFeature==2&&doc.sketches[2].name=="part:2::Profile"&&doc.sketches[2].sk.ents[0].dof[2]==5&&doc.find(1)->get("sketch")=="part:1::Profile"&&doc.find(2)->get("sketch")=="part:2::Profile";};
    check(correct(reopened.document),"Compacted retained branch restores both local names/owners and untouched global sketch");
    AgentSession full;V load=V::makeObj();load.set("path",V::makeStr(archive.string()));accepted(full.call("doc_load",load));check(documentToJson(full.document)==original,"Public archive load restores complete original scoped history byte-for-byte");
    restore.set("id",*b.find("iteration_id"));restore.set("expected_revision",V::makeNum((double)full.revision));accepted(full.call("iteration_restore",restore));check(correct(full.document),"Full archived branch restores canonical scoped consumers and legacy global");
    fs::remove(compacted);fs::remove(archive);fs::remove(directory);
}
int main(int argc,char** argv){try{
    if(argc>1&&std::string(argv[1])=="--history"){compactedHistory();std::cout<<checks<<" scoped history checks / "<<failures<<" failures\n";return failures?1:0;}
    if(argc>1){nativeProjection();std::cout<<checks<<" native checks / "<<failures<<" failures\n";return failures?1:0;}
    {ArkDoc* api=ark_doc_create();const auto owner=ark_add_box(api,1,1,1);
    check(ark_sketch_set(api,"part:1::Profile","[]",nullptr)==-3,"C ABI catches reserved-reference rejection");
    check(ark_sketch_set_owned(api,owner,"Profile","[]",nullptr)==0,"C ABI creates explicit definition-owned sketch");
    check(ark_sketch_set(api,"part:1::Profile","[]",nullptr)==0,"C ABI canonical edit retains owner");
    check(ark_feature_remove(api,owner)==-1,"C ABI catches owner removal rejection");
    check(ark_sketch_remove(api,"part:1::Profile")==0&&ark_feature_remove(api,owner)==0,"C ABI explicit sketch removal permits owner removal");ark_doc_destroy(api);}
    AgentSession s;
    auto setup=apply(s,json(R"([{"op":"feature.add","type":"extrude","name":"Left","p":{"height":5,"w":10,"d":10}},{"op":"feature.add","type":"extrude","name":"Right","p":{"height":5,"w":10,"d":10}}])"));check(!failed(setup),setup.dump());
    for(u64 owner:{1,2})for(int batch=0;batch<2;++batch){V commands=V::makeArr();for(int i=0;i<128;++i)commands.arr.push_back(sketch(owner,"S"+std::to_string(batch*128+i),owner==1?2:3));auto result=apply(s,commands);check(!failed(result),result.dump());}
    check(s.document.sketches.size()==512,"512 sketches across two definition scopes");
    check(!s.document.findSketch("S0"),"Unqualified local name never aliases either scope");
    check(s.document.findSketch("part:1::S0")&&s.document.findSketch("part:2::S0"),"Canonical same-name references resolve independently");
    auto use=apply(s,json(R"([{"op":"feature.edit","id":1,"p":{"sketch":"part:1::S0"}},{"op":"feature.edit","id":2,"p":{"sketch":"part:2::S0"}},{"op":"assembly.add","name":"First","feature":1},{"op":"assembly.add","name":"Repeat","feature":1,"p":{"tx":20}}])"));check(!failed(use),use.dump());
    check(s.document.sketches.size()==512&&s.document.assembly.size()==2,"Repeated occurrences reuse the definition sketches without copying");
    check(s.document.lastResults.size()==2&&s.document.lastResults[0].ok&&s.document.lastResults[1].ok,"Self-owned extrusion profiles evaluate without ownership cycles");
    auto inspect=s.call("sketch_inspect",json(R"({"name":"part:2::S0"})"));check(!failed(inspect)&&inspect.find("structuredContent")->find("owner_feature")->num==2,"Inspection exposes stable owner");
    auto context=s.call("context_graph",json(R"({"keys":["sketches:part:2::S0"],"depth":1})"));check(!failed(context)&&context.dump().find("owned_by_definition")!=std::string::npos,"Context graph exposes ownership separately");
    auto baseline=documentToJson(s.document);auto revision=s.revision;V commands=one(json(R"({"op":"param.set","name":"must_rollback","expr":7})"));commands.arr.push_back(sketch(1,"Overflow"));
    check(failed(apply(s,commands))&&s.revision==revision&&documentToJson(s.document)==baseline,"Scope overflow rolls back the whole batch");
    check(!failed(apply(s,one(sketch(2,"S255",4)))),"Replacement at capacity remains valid");
    check(!failed(apply(s,one(sketch(0,"S0"))))&&s.document.findSketch("S0")->ownerFeature==0,"Global scope is independent of identical local names");
    const auto oldName=s.document.sketches.front().name;
    check(!failed(apply(s,json(R"([{"op":"feature.edit","id":1,"name":"Renamed"}])")))&&s.document.sketches.front().name==oldName,"Definition rename preserves sketch identities");
    baseline=documentToJson(s.document);revision=s.revision;
    check(failed(apply(s,json(R"([{"op":"feature.remove","id":1}])")))&&s.revision==revision&&documentToJson(s.document)==baseline,"Definition removal cannot orphan its sketches");
    Document loaded;std::string error;check(documentFromJson(baseline,loaded,&error),error);check(documentToJson(loaded)==baseline,"Scoped authoring/history roundtrip is exact");
    auto persisted=json(documentToJson(s.document,true,false).c_str());check(persisted.find("version")->num==50,"Owner metadata requires format50");
    auto downgraded=persisted;downgraded.set("version",V::makeNum(49));check(!documentFromJson(downgraded.dump(),loaded,&error)&&documentToJson(loaded)==baseline,"Downgraded scoped schema rejects atomically");
    auto forged=persisted;auto rows=*forged.find("sketches");rows.arr[0].set("owner_feature",V::makeNum(99));forged.set("sketches",rows);check(!documentFromJson(forged.dump(),loaded,&error),"Tampered owner/canonical reference rejects");
    Document legacy;check(documentFromJson(R"({"version":5,"features":[],"sketches":[{"name":"part:1::S0","entities":[]}]})",legacy,&error),"Legacy reserved-prefix global names load unchanged");check(legacy.sketches[0].ownerFeature==0&&json(documentToJson(legacy,false,false).c_str()).find("version")->num==5,"Legacy global ownership/version unchanged");
    Feature f;f.type=FeatureType::Box;legacy.addFeature(f);try{legacy.setSketch("S0",Sketch{},1);check(false,"Legacy canonical collision must reject");}catch(const std::exception&){check(legacy.sketches.size()==1&&legacy.sketches[0].ownerFeature==0,"Legacy collision never silently aliases");}
    Document total;for(int i=0;i<17;++i){Feature f;f.type=FeatureType::Box;total.addFeature(f);}for(int i=0;i<4096;++i)total.setSketch("S"+std::to_string(i%256),Sketch{},1+i/256);
    try{total.setSketch("Overflow",Sketch{},17);check(false,"Total overflow must reject");}catch(const std::exception&){check(total.sketches.size()==4096,"Total4096 resource bound preserves existing sketches");}
    Document resources;Feature rf;rf.type=FeatureType::Box;resources.addFeature(rf);NamedSketch many;many.name="Heavy";many.sk.ents.resize(32769);resources.sketches.push_back(many);check(!resources.validateSketchScopes(error)&&error.find("resource")!=std::string::npos,"Aggregate entity resource guard");
    AgentSession removal;check(!failed(apply(removal,json(R"([{"op":"feature.add","type":"box","name":"Disposable"},{"op":"sketch.set","owner_feature":1,"name":"Profile","sketch":{"entities":[]}},{"op":"sketch.remove","owner_feature":1,"name":"Profile"},{"op":"feature.remove","id":1}])"))),"Explicit owned sketch removal permits definition removal in one transaction");
}catch(const std::exception& e){check(false,e.what());}std::cout<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;}
