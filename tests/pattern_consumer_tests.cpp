#include "ark/agent.hpp"
#include "ark/assembly.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;
using V=arkjson::Value;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
V json(const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid fixture JSON");return value;}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing result field: ")+key);return *found;}
V accepted(const V& response){if(auto error=response.find("isError");error&&error->b)throw std::runtime_error(field(response,"structuredContent").dump());return field(response,"structuredContent");}
V apply(AgentSession& s,const char* commands){V args=V::makeObj();args.set("commands",json(commands));args.set("expected_revision",V::makeNum((double)s.revision));return accepted(s.call("doc_apply",args));}
void near(double actual,double expected,const char* message){check(std::abs(actual-expected)<1e-6,message);}
void rejectedRead(AgentSession& s,const char* tool,const V& args){const auto before=documentToJson(s.document);const auto revision=s.revision;const auto response=s.call(tool,args);check(field(response,"isError").b,"Missing/skipped occurrence must reject");check(revision==s.revision&&before==documentToJson(s.document),"Rejected occurrence query must preserve document and revision");}

void alternateDefinitions(){
    AgentSession s;
    apply(s,R"([
      {"op":"param.set","name":"outer_height","expr":"10"},
      {"op":"feature.add","type":"native_solid","name":"Inner","native":{"op":"box"},"p":{"width":2,"depth":3,"height":4}},
      {"op":"feature.add","type":"native_solid","name":"Outer","native":{"op":"box"},"p":{"width":6,"depth":8,"height":"outer_height"}},
      {"op":"feature.add","type":"curve","name":"Guide","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[60,0,0]}]}},
      {"op":"assembly.add","name":"Links","feature":"Inner","pattern":{"type":"chain","count":4,"skips":[2],"source":{"feature":"Guide","reference":"path","alignment":"fixed","endpoint":"include","alternate_feature":"Outer","alternate_angle":0}}},
      {"op":"assembly.add","name":"Mover","feature":"Inner","p":{"tx":100}}
    ])");
    const auto inner=s.document.features[0].id,outer=s.document.features[1].id,links=s.document.assembly[0].id;
    const auto before=documentToJson(s.document);const auto revision=s.revision;
    V args=json(R"({"node":"Links","copy":3})");
    const V selected=accepted(s.call("object_context",args));const V row=field(selected,"objects").arr.at(0);
    check(field(row,"id").num==outer,"Occurrence context must identify the alternate definition");
    check(field(field(row,"authored"),"name").str=="Outer","Occurrence context must expose alternate authoring");
    check(field(field(field(row,"authored"),"native"),"op").str=="box","Occurrence context preserves authored operation");
    check(field(field(row,"references"),"resources").find("params:outer_height"),"Occurrence context exposes alternate expression resource");
    const auto& occurrence=field(row,"occurrences").arr.at(0);
    check(field(occurrence,"feature").num==outer&&field(occurrence,"copy").num==3,"Occurrence metadata uses actual stable slot and definition");
    auto definition=accepted(s.call("object_context",json(R"({"ids":["Outer"]})")));
    const auto& placed=field(field(definition,"objects").arr.at(0),"occurrences").arr;
    check(placed.size()==2&&field(placed[0],"copy").num==1&&field(placed[1],"copy").num==3,"Definition context enumerates actual alternate slots only");
    check(field(accepted(s.call("object_context",json(R"({"ids":["Outer"],"query":""})"))),"objects").arr.size()==1,"An empty query beside ids is treated as absent");
    definition=accepted(s.call("object_context",json(R"({"ids":["Inner"]})")));
    for(const auto& item:field(field(definition,"objects").arr.at(0),"occurrences").arr)if(field(item,"node").num==links)check(field(item,"copy").num==0&&field(item,"feature").num==inner,"Seed context excludes skipped and alternate copies");
    args.set("known_fingerprints",field(selected,"fingerprints"));
    check(!field(field(accepted(s.call("object_context",args)),"objects").arr.at(0),"changed").b,"Unchanged occurrence retains compact fingerprint response");
    rejectedRead(s,"object_context",json(R"({"node":"Links","copy":2})"));
    rejectedRead(s,"object_context",json(R"({"node":"Links","copy":4})"));
    check(before==documentToJson(s.document)&&revision==s.revision,"Context inspection must remain read-only");

    V placement=json(R"({"node":"Mover","reference":"z_max","mode":"center","target":{"node":"Links","copy":3,"reference":"z_max"}})");placement.set("expected_revision",V::makeNum((double)s.revision));
    const auto planned=accepted(s.call("placement_plan",placement));const auto& frame=field(planned,"world_frame").arr;
    near(frame.at(12).num,62,"Placement target uses alternate face X centroid");near(frame.at(13).num,2.5,"Placement target uses alternate face Y centroid");near(frame.at(14).num,6,"Placement target uses alternate face height");
    check(before==documentToJson(s.document)&&revision==s.revision,"Placement preview must preserve committed state");
    V skippedTarget=field(placement,"target");skippedTarget.set("copy",V::makeNum(2));placement.set("target",skippedTarget);rejectedRead(s,"placement_plan",placement);

    const auto info=accepted(s.call("doc_info",json(R"({"detail":"design"})")));bool found=false;
    for(const auto& feature:field(info,"features").arr)if(field(feature,"id").num==outer){found=true;check(field(feature,"role").str=="placed_definition","Alternate definition is classified as placed");check(field(feature,"instances").arr.size()==1&&field(feature,"instances").arr[0].num==links,"Alternate definition lists its pattern instance");}
    check(found,"Document info includes alternate definition");
    const auto snapshot=accepted(s.call("snapshot",json(R"({"node":"Links","copy":3})")));
    check(field(field(snapshot,"visual_evidence"),"feature_id").num==outer,"Snapshot evidence identifies the rendered alternate definition");
    const auto all=accepted(s.call("snapshot",json(R"({"node":"Links"})")));
    check(!field(all,"visual_evidence").find("feature_id"),"Mixed-definition snapshot must not claim a single definition");
    rejectedRead(s,"snapshot",json(R"({"node":"Links","copy":2})"));

    apply(s,R"([{"op":"param.set","name":"outer_height","expr":"12"}])");
    check(field(field(accepted(s.call("object_context",args)),"objects").arr.at(0),"changed").b,"Alternate resource edit invalidates occurrence fingerprint");
}
}
int main(){try{alternateDefinitions();std::cout<<"pattern consumer tests passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
