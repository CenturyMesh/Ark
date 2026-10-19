// 042D tool layer: query_batch dataflow (references, paths, byte caps) and the
// per-revision doc_info digest. Synthetic box fixtures only.
#include "ark/agent.hpp"
#include "ark/result_path.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using namespace ark;using V=arkjson::Value;size_t checks=0;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
const V& field(const V& value,const char* key){const auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing receipt field: ")+key+" in "+value.dump().substr(0,600));return *found;}
V json(const std::string& text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid synthetic fixture JSON");return value;}
V accepted(V value){check(!field(value,"isError").b,value.dump().substr(0,800));return field(value,"structuredContent");}
V call(AgentSession& s,const char* tool,const std::string& args){return accepted(s.call(tool,json(args)));}
bool rejected(AgentSession& s,const char* tool,const std::string& args,const std::string& text){auto result=s.call(tool,json(args));return field(result,"isError").b&&result.dump().find(text)!=std::string::npos;}
V applyCommands(AgentSession& s,const std::string& commands){V args=V::makeObj();args.set("expected_revision",V::makeNum((double)s.revision));args.set("commands",json(commands));args.set("detail",V::makeStr("changes"));return accepted(s.call("doc_apply",args));}
void setup(AgentSession& s,int count){s.suppressIterationHistory();s.suppressFailureCapture();std::string commands="[";for(int i=0;i<count;++i){if(i)commands+=",";commands+=R"({"op":"feature.add","type":"box","name":"Box )"+std::to_string(i+1)+R"(","p":{"dx":4,"dy":5,"dz":6}})";}commands+=",{\"op\":\"param.set\",\"name\":\"width\",\"expr\":\"12\"}]";applyCommands(s,commands);}
const V& row(const V& batch,size_t index){return field(batch,"results").arr.at(index);}

void resultPaths(){
    const auto value=json(R"({"features":[{"id":1,"name":"A"},{"id":2},{"id":3,"name":"C"}],"count":3})");V out;std::string where;
    check(selectResultPath(value,"features[].id",out,where)&&out.dump()=="[1,2,3]","[] maps each element");
    check(selectResultPath(value,"features[1:].id",out,where)&&out.dump()=="[2,3]","[a:] selects an open range");
    check(selectResultPath(value,"features[].name",out,where)&&out.dump()==R"(["A",null,"C"])","an absent field inside a map is null");
    check(selectResultPath(value,"features[2].name",out,where)&&out.str=="C","[n] selects one element");
    check(!selectResultPath(value,"missing",out,where)&&where.find("available: features, count")!=std::string::npos,"an absent top-level field names what is available");
    check(!selectResultPath(value,"features[9].id",out,where)&&where.find("index 9")!=std::string::npos,"an out-of-range index is reported");
    for(const char* invalid:{"","a..b","a[x]","a[2:1]","[0]","a[1][2]"}){bool thrown=false;try{validateResultPath(invalid);}catch(const std::runtime_error&){thrown=true;}check(thrown,std::string("invalid path rejected: ")+invalid);}
}
void batchDataflow(){
    AgentSession s;setup(s,8);const auto revision=s.revision;
    const auto batch=call(s,"query_batch",R"({"queries":[
        {"id":"summary","tool":"doc_info","args":{},"paths":["features[0:3].id","feature_count"]},
        {"id":"context","tool":"object_context","args":{"ids":{"$ref":"summary.features[0:3].id"},"limit":3},"paths":["objects[].id","revision"]}]})");
    const auto& first=field(row(batch,0),"structuredContent");const auto ids=field(first,"features[0:3].id");
    check(ids.arr.size()==3&&field(first,"feature_count").num==8&&field(first,"revision").num==(double)revision,"paths project nested values and keep the revision");
    const auto& second=row(batch,1);
    check(!field(second,"isError").b&&field(field(second,"resolved_args"),"ids").dump()==ids.dump(),"a reference is resolved from the earlier row's result");
    check(field(field(second,"args"),"ids").find("$ref"),"the row echoes the submitted arguments, reference included");
    check(field(field(second,"structuredContent"),"objects[].id").dump()==ids.dump(),"the referenced read runs on the resolved ids");
    check(row(batch,0).dump().size()<call(s,"doc_info","{}").dump().size(),"a projected row is smaller than the full read");
}
void batchExplicitFailures(){
    AgentSession s;setup(s,4);
    auto batch=call(s,"query_batch",R"({"queries":[{"id":"bad","tool":"eval_expr","args":{"expr":"unknown_batch_parameter"}},{"tool":"object_context","args":{"ids":{"$ref":"bad.value"}}},{"tool":"doc_info","args":{"detail":"revision"}}]})");
    const auto& skipped=field(row(batch,1),"structuredContent");
    check(field(row(batch,1),"isError").b&&field(skipped,"error_code").str=="QUERY_BATCH_ROW_SKIPPED"&&field(skipped,"skipped_due_to").str=="bad","a row referencing a failed row is skipped explicitly");
    check(!field(row(batch,2),"isError").b,"independent rows still run");
    batch=call(s,"query_batch",R"({"queries":[{"id":"s","tool":"doc_info","args":{}},{"tool":"object_context","args":{"ids":{"$ref":"s.nope"}}}]})");
    const auto& unresolved=field(row(batch,1),"structuredContent");
    check(field(unresolved,"error_code").str=="QUERY_BATCH_REF_UNRESOLVED"&&field(unresolved,"error").str.find("nope")!=std::string::npos,"an unresolved reference names the missing step");
    batch=call(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{},"paths":["features[].id","no_such_field"]}]})");
    const auto& missing=field(row(batch,0),"structuredContent");
    check(field(missing,"error_code").str=="QUERY_BATCH_PATH_MISSING"&&field(missing,"tool_succeeded").b&&!missing.find("observed_result"),"a missing path is a row error without a full result dump");
    batch=call(s,"query_batch",R"({"queries":[{"tool":"doc_info","args":{},"max_bytes":256},{"tool":"doc_info","args":{},"paths":["feature_count"],"max_bytes":256}]})");
    const auto& large=field(row(batch,0),"structuredContent");
    check(field(large,"error_code").str=="QUERY_BATCH_ROW_TOO_LARGE"&&field(large,"bytes").num>256&&field(field(large,"result_keys"),"features").find("count"),"a row above max_bytes returns its shape, not its data");
    check(!field(row(batch,1),"isError").b,"a projected row within max_bytes passes");
    for(const auto& [args,text]:std::initializer_list<std::pair<const char*,const char*>>{
        {R"({"queries":[{"tool":"object_context","args":{"ids":{"$ref":"later.value"}}},{"id":"later","tool":"doc_info","args":{}}]})","earlier"},
        {R"({"queries":[{"id":"a","tool":"doc_info","args":{}},{"id":"a","tool":"doc_info","args":{}}]})","unique"},
        {R"({"queries":[{"tool":"doc_info","args":{},"fields":["revision"],"paths":["revision"]}]})","fields or paths"},
        {R"({"queries":[{"tool":"doc_info","args":{},"paths":["features[x]"]}]})","index"},
        {R"({"queries":[{"tool":"doc_info","args":{},"max_bytes":10}]})","max_bytes"}})
        check(rejected(s,"query_batch",args,text),std::string("invalid batch rejected before execution: ")+text);
}
void revisionDigest(){
    AgentSession s;setup(s,12);
    const auto first=call(s,"doc_info",R"({"detail":"digest"})"),again=call(s,"doc_info",R"({"detail":"digest"})");
    check(first.dump()==again.dump(),"the digest is deterministic for one revision");
    check(field(first,"schema").str=="ark.design-digest.v1"&&field(first,"source_sha256").str.size()==64&&field(field(first,"counts"),"features").num==12,"the digest names its schema, source hash and counts");
    const auto design=call(s,"doc_info",R"({"detail":"design"})");
    std::cout<<"doc_info digest_bytes="<<first.dump().size()<<" design_bytes="<<design.dump().size()<<'\n';
    check(first.dump().size()*2<design.dump().size(),"the digest is much smaller than detail design");
    V known=field(first,"fingerprints");V args=V::makeObj();args.set("detail",V::makeStr("digest"));args.set("known_fingerprints",known);
    auto unchanged=accepted(s.call("doc_info",args));
    check(field(unchanged,"changed").num==0&&field(unchanged,"features").arr.empty()&&!unchanged.find("parameters")&&field(unchanged,"unchanged").num==(double)known.obj.size(),"known fingerprints omit every unchanged row");
    const auto edited=(u64)field(field(first,"features").arr.at(3),"id").num;
    applyCommands(s,R"([{"op":"feature.edit","id":)"+std::to_string(edited)+R"(,"p":{"dx":9}}])");
    auto delta=accepted(s.call("doc_info",args));const auto& rows=field(delta,"features").arr;
    check(rows.size()==1&&(u64)field(rows[0],"id").num==edited&&rows[0].find("type"),"only the edited feature returns its row");
    check(field(delta,"changed").num==1&&field(delta,"revision").num==(double)s.revision,"one edit changes one digest row");
    const auto removedId=(u64)field(field(first,"features").arr.at(5),"id").num;
    applyCommands(s,R"([{"op":"feature.remove","id":)"+std::to_string(removedId)+"}]");
    delta=accepted(s.call("doc_info",args));bool removed=false;for(const auto& key:field(delta,"removed").arr)if(key.str=="feature:"+std::to_string(removedId))removed=true;
    check(removed,"a removed feature is listed as removed");
    check(rejected(s,"doc_info",R"({"detail":"digest","ids":[1]})","INVALID_ARGUMENT")&&rejected(s,"doc_info",R"({"detail":"summary","known_fingerprints":{}})","INVALID_ARGUMENT"),"digest filters and fingerprints are explicit about their scope");
}
}
int main(){try{resultPaths();batchDataflow();batchExplicitFailures();revisionDigest();std::cout<<"042D tool layer checks passed: "<<checks<<'\n';return 0;}catch(const std::exception& error){std::cerr<<"042D tool layer failure: "<<error.what()<<'\n';return 1;}}
