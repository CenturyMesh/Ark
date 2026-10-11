#include "ark/agent_activity.hpp"
#include "ark/bom.hpp"
#include "ark/model.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <chrono>
#include <ctime>

namespace ark {
namespace {
using V=arkjson::Value;
std::string text(const V& value,const char* key){const auto* p=value.find(key);return p?p->asStr():"";}
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
std::string clipped(std::string value,size_t size){if(value.size()<=size)return value;while(size&&(static_cast<unsigned char>(value[size])&0xc0)==0x80)--size;return value.substr(0,size);}
V envelope(V response,const char* key,const V& value){
    if(const auto* data=response.find("structuredContent")){V payload=*data;payload.set(key,value);response.set("structuredContent",payload);if(const auto* content=response.find("content")){V parts=*content;for(auto& part:parts.arr)if(text(part,"type")=="text")part.set("text",V::makeStr(payload.dump()));response.set("content",parts);}}
    else response.set(key,value);return response;
}
V pendingIds(const AgentActivityLedger& ledger,const V& actor){V out=V::makeArr();for(const auto& row:ledger.entries)if(!row.find("feedback")&&text(*row.find("actor"),"id")==text(actor,"id")){if(out.arr.size()>=64)break;out.arr.push_back(*row.find("id"));}return out;}
int score(const V& value,const char* key){const auto* n=value.find(key);require(n&&n->t==V::Num&&std::isfinite(n->num)&&n->num>=1&&n->num<=5&&n->num==std::floor(n->num),"Feedback scores must be integers 1..5");return (int)n->num;}
}
std::uint64_t agentActivitySerial(const Document& d){return d.agentActivity?d.agentActivity->serial:0;}
V agentActivityJson(const std::shared_ptr<AgentActivityLedger>& ledger){
    V out=V::makeObj();out.set("schema",V::makeNum(1));out.set("id",V::makeStr(ledger->id));out.set("serial",V::makeNum((double)ledger->serial));out.set("unrecorded_actions",V::makeNum((double)ledger->unrecordedActions));V rows=V::makeArr();rows.arr=ledger->entries;out.set("entries",rows);return out;
}
std::shared_ptr<AgentActivityLedger> readAgentActivity(const V& value){
    const auto* schema=value.find("schema"),*rows=value.find("entries"),*serial=value.find("serial"),*missing=value.find("unrecorded_actions");
    require(value.t==V::Obj&&schema&&schema->t==V::Num&&schema->num==1&&validBomDocumentId(text(value,"id"))&&rows&&rows->t==V::Arr&&rows->arr.size()<=4096&&serial&&serial->t==V::Num&&serial->num>=0&&serial->num<=9007199254740991.&&serial->num==std::floor(serial->num)&&missing&&missing->t==V::Num&&missing->num>=0&&missing->num<=9007199254740991.&&missing->num==std::floor(missing->num),"Invalid project agent activity ledger");
    auto ledger=std::make_shared<AgentActivityLedger>();ledger->id=text(value,"id");ledger->serial=(std::uint64_t)serial->num;ledger->unrecordedActions=(std::uint64_t)missing->num;std::set<std::string> ids;
    for(const auto& row:rows->arr){require(row.t==V::Obj&&row.find("utc")&&row.find("utc")->t==V::Str&&!row.find("utc")->str.empty()&&row.find("is_error")&&row.find("is_error")->t==V::Bool&&row.dump().size()<=8192&&!text(row,"id").empty()&&ids.insert(text(row,"id")).second&&!text(row,"tool").empty()&&row.find("actor")&&row.find("actor")->t==V::Obj&&row.find("elapsed_ms")&&row.find("elapsed_ms")->t==V::Num&&std::isfinite(row.find("elapsed_ms")->num)&&row.find("elapsed_ms")->num>=0&&row.find("feedback_required")&&row.find("feedback_required")->t==V::Bool,"Invalid measured activity entry");if(const auto* feedback=row.find("feedback")){score(*feedback,"utility");score(*feedback,"difficulty");require(!text(*feedback,"action").empty(),"Feedback needs an action description");}ledger->entries.push_back(row);}
    return ledger;
}
V recordAgentActivity(Document& d,const std::string& tool,const V& args,const V& actor,V result,std::uint64_t before,std::uint64_t after,double elapsed){
    if(!d.agentActivity){d.agentActivity=std::make_shared<AgentActivityLedger>();d.agentActivity->id=newBomDocumentId();}
    auto& log=*d.agentActivity;++log.serial;
    // Logging does not stop CAD work or discard old evidence when capacity is
    // reached; a measured gap counter explicitly invalidates completeness.
    if(log.entries.size()>=4096){++log.unrecordedActions;return envelope(std::move(result),"activity_capture_error",V::makeStr("Project activity capacity reached; prior records retained and unrecorded_actions increased."));}
    V row=V::makeObj(),identity=V::makeObj(),operations=V::makeArr();const auto id=log.id+"/action/"+newBomDocumentId().substr(4);
    const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc,&now);
#else
    gmtime_r(&now,&utc);
#endif
    char stamp[32]{};std::strftime(stamp,sizeof stamp,"%Y-%m-%dT%H:%M:%SZ",&utc);row.set("utc",V::makeStr(stamp));
    row.set("id",V::makeStr(id));row.set("tool",V::makeStr(tool));for(const auto* key:{"kind","id","role","model"})if(const auto* value=actor.find(key);value&&value->t==V::Str)identity.set(key,V::makeStr(clipped(value->str,256)));row.set("actor",identity);row.set("revision_before",V::makeNum((double)before));row.set("revision_after",V::makeNum((double)after));row.set("elapsed_ms",V::makeNum(std::max(0.,elapsed)));row.set("timing_scope",V::makeStr("host core call; excludes provider and UI queue wait"));
    const auto* payload=result.find("structuredContent");if(!payload)payload=&result;const auto* error=result.find("isError");const bool actionFailed=error&&error->t==V::Bool&&error->b;row.set("is_error",V::makeBool(actionFailed));for(const auto* key:{"error_code","error","geometry_recomputed","geometry_changed"})if(const auto* value=payload->find(key)){if(value->t==V::Str)row.set(key,V::makeStr(clipped(value->str,512)));else if(value->t==V::Bool)row.set(key,*value);}
    if(const auto* computed=payload->find("geometry_recomputed");computed&&computed->t==V::Bool){row.set("evaluation_ms",V::makeNum(computed->b&&payload->find("recompute_ms")?payload->find("recompute_ms")->asNum():0));if(computed->b)for(const auto* key:{"features_evaluated","features_reused"})if(const auto* value=payload->find(key))row.set(key,*value);}
    if(const auto* commands=args.find("commands");commands&&commands->t==V::Arr){row.set("command_count",V::makeNum((double)commands->arr.size()));for(const auto& c:commands->arr)if(operations.arr.size()<16)operations.arr.push_back(V::makeStr(clipped(text(c,"op"),80)));}row.set("operations",operations);row.set("feedback_required",V::makeBool(true));log.entries.push_back(row);
    result=envelope(std::move(result),"pending_feedback_ids",pendingIds(log,actor));result=envelope(std::move(result),"activity_updated",V::makeBool(true));result=envelope(std::move(result),"activity_id",V::makeStr(id));result=envelope(std::move(result),"activity_project_id",V::makeStr(log.id));if((tool=="doc_load"||tool=="doc_new")&&!actionFailed)result=envelope(std::move(result),"activity_boundary",V::makeBool(true));return envelope(std::move(result),"feedback_required",V::makeBool(true));
}
V inspectAgentActivity(const Document& d,const V& args){
    require(args.t==V::Obj,"Activity inspection needs an object");size_t offset=0,limit=32;bool pending=false;std::string selectedActor;
    for(const auto& [key,value]:args.obj){if(key=="actor_id"){require(value.t==V::Str&&!value.str.empty()&&value.str.size()<=256,"actor_id must be bounded text");selectedActor=value.str;}else if(key=="pending_only"){require(value.t==V::Bool,"pending_only must be boolean");pending=value.b;}else if(key=="offset"||key=="limit"){require(value.t==V::Num&&std::isfinite(value.num)&&value.num==std::floor(value.num)&&value.num>=(key=="limit"?1:0)&&value.num<=(key=="limit"?100:1000000),"Invalid activity pagination");(key=="limit"?limit:offset)=(size_t)value.num;}else require(false,"Unknown activity inspection argument");}
    V out=V::makeObj(),rows=V::makeArr();size_t selected=0,unrated=0;if(d.agentActivity)for(const auto& row:d.agentActivity->entries){if(!selectedActor.empty()&&text(*row.find("actor"),"id")!=selectedActor)continue;const bool missing=!row.find("feedback");unrated+=missing;if(pending&&!missing)continue;if(selected>=offset&&rows.arr.size()<limit)rows.arr.push_back(row);++selected;}
    out.set("schema",V::makeStr("ark.agent-activity.v1"));out.set("entries",rows);out.set("total_matching",V::makeNum((double)selected));out.set("pending_feedback",V::makeNum((double)unrated));out.set("offset",V::makeNum((double)offset));out.set("partial",V::makeBool(rows.arr.size()!=selected));out.set("serial",V::makeNum((double)agentActivitySerial(d)));out.set("unrecorded_actions",V::makeNum(d.agentActivity?(double)d.agentActivity->unrecordedActions:0));out.set("feedback_authority",V::makeStr("subjective agent self-report; measured outcomes/times are separate"));return out;
}
V rateAgentActivity(Document& d,const V& args,const V& actor){
    const auto* ratings=args.find("ratings");require(args.t==V::Obj&&args.obj.size()==1&&ratings&&ratings->t==V::Arr&&!ratings->arr.empty()&&ratings->arr.size()<=64,"tool_feedback requires 1..64 ratings");require(d.agentActivity&&!text(actor,"id").empty(),"Feedback requires the recorded actor identity");
    std::vector<std::pair<size_t,V>> updates;std::set<std::string> unique;
    for(const auto& rating:ratings->arr){require(rating.t==V::Obj,"Each rating must be an object");for(const auto& field:rating.obj)require(field.first=="activity_id"||field.first=="utility"||field.first=="difficulty"||field.first=="action"||field.first=="issue"||field.first=="wished_tool"||field.first=="wished_options","Unknown feedback field");const auto id=text(rating,"activity_id");require(unique.insert(id).second,"Repeated feedback ID");auto found=std::find_if(d.agentActivity->entries.begin(),d.agentActivity->entries.end(),[&](const V& row){return text(row,"id")==id;});require(found!=d.agentActivity->entries.end(),"Feedback action not found in this project");require(text(*found->find("actor"),"id")==text(actor,"id"),"An agent can only rate its own actual tool use");require(!found->find("feedback"),"Feedback already recorded; original rating is immutable");score(rating,"utility");score(rating,"difficulty");require(!text(rating,"action").empty(),"Describe the actual action being rated");V feedback=V::makeObj();for(const auto& [key,value]:rating.obj)if(key!="activity_id"){if(key!="utility"&&key!="difficulty")require(value.t==V::Str&&value.str.size()<=512,"Feedback text must be at most 512 bytes");feedback.set(key,value);}feedback.set("rated_by",actor);feedback.set("provenance",V::makeStr(text(actor,"model")==text(*found->find("actor"),"model")?"agent_self_report":"same_role_review_with_changed_model"));updates.emplace_back((size_t)(found-d.agentActivity->entries.begin()),feedback);}
    V out=V::makeObj(),rated=V::makeArr();for(auto& update:updates){auto& row=d.agentActivity->entries[update.first];rated.arr.push_back(*row.find("id"));row.set("feedback",update.second);row.set("feedback_required",V::makeBool(false));}++d.agentActivity->serial;out.set("rated_ids",rated);out.set("pending_feedback_ids",pendingIds(*d.agentActivity,actor));out.set("geometry_unchanged",V::makeBool(true));out.set("serial",V::makeNum((double)d.agentActivity->serial));return out;
}
void appendAgentActivityTools(V& tools){
    for(const auto& entry:std::vector<std::pair<const char*,const char*>>{{"activity_info",R"({"type":"object","properties":{"actor_id":{"type":"string","minLength":1,"maxLength":256},"offset":{"type":"integer","minimum":0,"maximum":1000000},"limit":{"type":"integer","minimum":1,"maximum":100},"pending_only":{"type":"boolean"}},"additionalProperties":false})"},{"tool_feedback",R"({"type":"object","properties":{"ratings":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"object","properties":{"activity_id":{"type":"string"},"utility":{"type":"integer","minimum":1,"maximum":5},"difficulty":{"type":"integer","minimum":1,"maximum":5},"action":{"type":"string","minLength":1,"maxLength":512},"issue":{"type":"string","maxLength":512},"wished_tool":{"type":"string","maxLength":512},"wished_options":{"type":"string","maxLength":512}},"required":["activity_id","utility","difficulty","action"],"additionalProperties":false}}},"required":["ratings"],"additionalProperties":false})"}}){V tool=V::makeObj(),schema,annotations=V::makeObj();arkjson::parse(entry.second,schema);tool.set("name",V::makeStr(entry.first));tool.set("description",V::makeStr(std::string(entry.first)=="activity_info"?"Read paginated project action timings/outcomes and pending agent feedback; no geometry or prompt payload. Ratings are subjective and do not certify design quality.":"Record mandatory self-feedback for actual activity IDs returned by your tools, in batches before task completion. Utility 1=unhelpful,5=effective; difficulty 1=easy,5=very difficult. Describe the action and optionally issues, wished tools/options. Only your own unscored project actions; immutable ratings, no geometry revision change."));tool.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(std::string(entry.first)=="activity_info"));tool.set("annotations",annotations);tools.arr.push_back(tool);}
}
}
