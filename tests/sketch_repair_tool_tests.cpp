#include "ark/agent.hpp"
#include "ark/sketch.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
V json(const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Fixture JSON invalid");return value;}
const V& field(const V& value,const char* key){auto found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing field: ")+key);return *found;}
V accepted(V response){if(field(response,"isError").b)throw std::runtime_error(response.dump());return field(response,"structuredContent");}
V seed(){return json(R"({"plane":"XY","tolerance":0.025,"entities":[{"kind":"point","name":"mount","dof":[4,"height/2"]}],"constraints":[{"id":"left","type":"fixedX","a":0,"val":"zero"},{"id":"right","type":"fixedX","a":0,"val":"width"}]})");}
V request(const AgentSession& session,const V& source){V args=V::makeObj();args.set("name",V::makeStr("Fixture"));args.set("sketch",source);args.set("expected_revision",V::makeNum((double)session.revision));return args;}
void unchanged(const AgentSession& session,u64 revision,const std::string& before){check(session.revision==revision&&documentToJson(session.document)==before,"Read/rejection changed document or revision");}
}
int main(){try{
    AgentSession session;session.suppressIterationHistory();
    accepted(session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"zero","expr":"0"},{"op":"param.set","name":"width","expr":"10"},{"op":"param.set","name":"height","expr":"6"}]})")));
    const auto revision=session.revision;const auto before=documentToJson(session.document);const auto source=seed();auto args=request(session,source);
    check(field(session.call("sketch_repair",json(R"({"name":"Fixture"})")),"isError").b,"Repair generation requires observed revision");
    auto stale=args;stale.set("expected_revision",V::makeNum((double)revision-1));check(field(session.call("sketch_repair",stale),"isError").b,"Stale generation is rejected");unchanged(session,revision,before);
    const auto report=accepted(session.call("sketch_repair",args));unchanged(session,revision,before);
    check(field(report,"document_mutated").b==false&&field(report,"source_sha256").str.size()==64,"Read-only proposal identifies its source digest");
    const auto alternatives=field(report,"proposals");check(alternatives.arr.size()==2,"Two alternative deletions are available for review");
    for(const auto& alternative:alternatives.arr){const auto& commands=field(alternative,"commands");check(commands.arr.size()==1,"One ordinary sketch transaction per alternative");
        const auto& recipe=field(commands.arr[0],"sketch");check(field(recipe,"entities").dump()==field(source,"entities").dump(),"Original numeric seeds and exact driven expressions remain in proposed source");
        check(field(recipe,"constraints").arr.size()==1&&field(field(recipe,"constraints").arr[0],"val").t==V::Str,"Surviving dimensional expression remains authored source");
        check(field(alternative,"expected_revision").num==(double)revision,"Each choice binds observed revision");
    }
    // Ignoring the report is the explicit reject path: there is no write call.
    unchanged(session,revision,before);
    V previewArgs=V::makeObj();previewArgs.set("expected_revision",V::makeNum((double)revision));previewArgs.set("commands",field(alternatives.arr[0],"commands"));
    const auto preview=accepted(session.call("doc_preview",previewArgs));check(field(preview,"valid").b&&field(preview,"candidate_cached").b,"Selected alternative prepares an atomic full-document candidate");unchanged(session,revision,before);
    accepted(session.call("doc_apply",field(preview,"apply")));check(session.revision==revision+1,"Explicit prepared acceptance advances revision once");
    const auto* sketch=session.document.findSketch("Fixture");check(sketch&&sketch->error.empty()&&sketch->sk.cons.size()==1&&sketch->sk.ents[0].expressions[1]=="height/2","Accepted sketch retains expressions and converges");
    const auto committed=documentToJson(session.document);check(field(session.call("doc_apply",field(preview,"apply")),"isError").b,"Prepared proposal cannot be replayed at a new revision");unchanged(session,revision+1,committed);
    check(field(session.call("doc_preview",previewArgs),"isError").b,"Stale alternative preview rejects");unchanged(session,revision+1,committed);
    V triple=source;V constraints=field(triple,"constraints");constraints.arr.push_back(json(R"({"id":"third","type":"fixedX","a":0,"val":20})"));triple.set("constraints",constraints);
    const auto noRepair=accepted(session.call("sketch_repair",request(session,triple)));check(field(noRepair,"status").str=="no_single_deletion_repair"&&field(noRepair,"proposals").arr.empty(),"Search discloses when multiple deletions would be needed");unchanged(session,revision+1,committed);
    auto invalid=source;auto entities=field(invalid,"entities");entities.arr[0].set("dof",json(R"([4,"missing_dimension"])"));invalid.set("entities",entities);
    check(field(session.call("sketch_repair",request(session,invalid)),"isError").b,"Unevaluated expression cannot produce a numeric repair proposal");unchanged(session,revision+1,committed);
    session.document.skeleton=json(R"({"manifest":{"sketches":[{"name":"Fixture"}]}})");const auto owned=documentToJson(session.document);
    check(field(session.call("sketch_repair",request(session,source)),"isError").b,"Source-owned sketch must be edited through its producer");unchanged(session,revision+1,owned);
    std::cout<<"sketch repair tool checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
