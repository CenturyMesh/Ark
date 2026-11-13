namespace claude_compaction_071d {
struct GuardState {
    std::deque<V> incoming;std::vector<V> sent,results;V catalog=V::makeArr();
    std::vector<std::string> actions;std::string trigger="auto";unsigned next=0,serial=0,users=0;
};
class GuardChannel final:public AssistantAcpChannel {
    std::shared_ptr<GuardState> s;
    void mcp(const char* method,V params){auto packet=json(R"({"type":"control_request","request":{"subtype":"mcp_message","server_name":"ark","message":{"jsonrpc":"2.0"}}})");
        const auto id=++s->serial;packet.set("request_id",V::makeStr("guard-control-"+std::to_string(id)));V request=*packet.find("request"),rpc=*request.find("message");rpc.set("id",V::makeNum(id));rpc.set("method",V::makeStr(method));if(!params.isNull())rpc.set("params",params);request.set("message",rpc);packet.set("request",request);s->incoming.push_back(packet);}
    void issue(){
        while(s->next<s->actions.size()){
            const auto action=s->actions[s->next++];
            if(action=="boundary"){auto event=json(R"({"type":"system","subtype":"compact_boundary","compact_metadata":{"pre_tokens":180000}})");V metadata=*event.find("compact_metadata");metadata.set("trigger",V::makeStr(s->trigger));event.set("compact_metadata",metadata);event.set("untrusted_summary",V::makeStr("DO_NOT_RETAIN_PROVIDER_PROMPT"));s->incoming.push_back(event);continue;}
            auto args=V::makeObj();if(action=="doc_apply")args.set("expected_revision",V::makeNum(7));auto params=V::makeObj();params.set("name",V::makeStr(action));params.set("arguments",args);mcp("tools/call",params);return;
        }
        s->incoming.push_back(json(R"({"type":"result","subtype":"success","is_error":false,"terminal_reason":"completed","result":"Guard fixture complete","usage":{"input_tokens":5,"output_tokens":2}})"));
    }
public:
    explicit GuardChannel(std::shared_ptr<GuardState> state):s(std::move(state)){}
    void send(const V& packet)override{s->sent.push_back(packet);const auto type=field(packet,"type");
        if(type=="control_request"&&field(*packet.find("request"),"subtype")=="initialize"){
            mcp("initialize",json(R"({"protocolVersion":"2025-11-25"})"));s->incoming.push_back(json(R"({"type":"control_response","response":{"subtype":"success","request_id":"ark_init","response":{"models":[{"value":"default","resolvedModel":"claude-test","supportedEffortLevels":["high"]}]}}})"));return;
        }
        if(type=="control_response"){
            const auto* body=packet.find("response");body=body?body->find("response"):nullptr;const auto* rpc=body?body->find("mcp_response"):nullptr;const auto* result=rpc?rpc->find("result"):nullptr;if(!result)return;
            if(result->find("serverInfo"))mcp("tools/list",V{});
            else if(result->find("tools"))s->catalog=*result->find("tools");
            else if(result->find("content")){s->results.push_back(*result);issue();}return;
        }
        if(type=="user"){
            ++s->users;auto init=json(R"({"type":"system","subtype":"init","permissionMode":"dontAsk","model":"claude-test","mcp_servers":[{"name":"ark","status":"connected"}]})");V names=V::makeArr();for(const auto& tool:s->catalog.arr)names.arr.push_back(V::makeStr("mcp__ark__"+field(tool,"name")));init.set("tools",names);s->incoming.push_back(init);issue();
        }
    }
    std::optional<V> receive(std::chrono::milliseconds wait)override{if(s->incoming.empty()){std::this_thread::sleep_for(wait);return {};}auto packet=s->incoming.front();s->incoming.pop_front();return packet;}
};
static void exchange(V& history,const std::string& id,const char* tool,const V& receipt){auto call=json(R"({"type":"function_call","arguments":"{}"})");call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(tool));history.arr.push_back(call);auto output=json(R"({"type":"function_call_output"})");output.set("call_id",V::makeStr(id));output.set("output",V::makeStr(receipt.dump()));history.arr.push_back(output);}
static AssistantConfig settings(std::shared_ptr<GuardState> state){auto value=assistantProviderPreset("claude-code");value.timeoutSeconds=2;value.acpChannelFactory=[state]{return std::make_unique<GuardChannel>(state);};return value;}
static V initialWire(const GuardState& state){
    for(const auto& packet:state.sent)if(field(packet,"type")=="user"){const auto content=field(*packet.find("message"),"content");const std::string prefix="Retained context and current input:\n";check(content.rfind(prefix,0)==0,"CLI sends one first-context wrapper");V parsed;check(arkjson::parse(content.substr(prefix.size()),parsed),"CLI nested initial history parses");return parsed;}throw std::runtime_error("Missing guarded CLI prompt");
}
void checks(){
    V large=json(R"([{"role":"user","content":"Synthetic selected edit only; retain all committed work and width 240 mm."}])");exchange(large,"past_committed","doc_apply",receipt(7));exchange(large,"large_read","doc_info",receipt(7,600*1024));exchange(large,"tail_read","doc_info",receipt(7));const auto original=large.dump();
    for(const char* scenario:{"host-projection","auto","manual"}){
        auto state=std::make_shared<GuardState>();const bool host=std::string(scenario)=="host-projection";state->trigger=scenario;state->actions={"doc_apply","doc_info","doc_apply","doc_info","doc_apply"};if(!host)state->actions.insert(state->actions.begin(),{"doc_info","boundary"});
        int reads=0,writes=0;V saved;std::vector<std::string> statuses;auto input=host?large:json(R"([{"role":"user","content":"Synthetic bounded edit."}])");
        const auto run=runAssistant(settings(state),input,tools(),[&](const std::string& name,const V&){if(name=="doc_apply"){++writes;return receipt(8);}++reads;if(reads==(host?1:2))return json(R"({"isError":true,"structuredContent":{"error":"Synthetic read failed; no current revision established"}})");return receipt(7);},[&](const AssistantEvent& event){if(event.kind==AssistantEvent::Status)statuses.push_back(event.text);},nullptr,{},{},[&](const V& value){saved=value;});
        check(writes==1&&reads==(host?2:3),"only one write executes after successful post-compaction doc_info; failed read cannot unlock");
        check(state->users==1&&run.rounds==1,"compaction guard never submits duplicate user input or automatically replays a turn");
        const std::size_t first=host?0:1;check(state->results[first].find("isError")->b&&state->results[first+2].find("isError")->b&&!state->results.back().find("isError")->b,"write-first and write-after-failed-read return explicit rejections");
        check(contains(saved,"ARK_ACP_INSPECT_REQUIRED")&&contains(saved,"ARK_ACP_INSPECTION_CONFIRMED"),"required and successful-inspection checkpoints survive in host history");
        check(saved.dump().find("DO_NOT_RETAIN_PROVIDER_PROMPT")==std::string::npos,"CLI summary/free text is not copied by a compaction diagnostic");
        check(std::any_of(statuses.begin(),statuses.end(),[](const auto& text){return text.find("context")!=std::string::npos&&text.find("inspection")!=std::string::npos;}),"context guard exposes a concise observed status");
        if(host){const auto wire=initialWire(*state);check(contains(wire,"ARK_REQUEST_HISTORY_PROJECTION_V1")&&contains(wire,"ARK_ACP_INSPECT_REQUIRED"),"projected first CLI prompt carries both projection and enforced fresh-read notice");check(input.dump()==original,"projection leaves input receipts intact");for(size_t i=0;i<large.arr.size();++i)check(run.conversation.arr[i].dump()==large.arr[i].dump(),"all original user and committed tool records survive without replay");}
        else check(saved.dump().find(std::string("(")+scenario+", pre_tokens 180000)")!=std::string::npos,"bounded trigger/pre-token observations are retained without inventing a summary");
    }
    // Exact references alone are lossless and must not force a fresh-read loop.
    auto state=std::make_shared<GuardState>();state->actions={"doc_apply"};V repeated=json(R"([{"role":"user","content":"Synthetic request with exact repeated historical data."}])");exchange(repeated,"exact_first","doc_info",receipt(7,12000));exchange(repeated,"exact_second","doc_info",receipt(7,12000));int writes=0;
    auto run=runAssistant(settings(state),repeated,tools(),[&](const auto&,const V&){++writes;return receipt(8);});check(writes==1&&!contains(run.conversation,"ARK_ACP_INSPECT_REQUIRED"),"lossless reference interning alone does not activate lossy-context recovery");
    const auto wire=initialWire(*state);V source,referenced;for(const auto& row:wire.arr)if(field(row,"type")=="function_call_output"){V decoded;check(arkjson::parse(field(row,"output"),decoded),"nested tool receipt parses");if(field(row,"call_id")=="exact_first")source=decoded;if(field(row,"call_id")=="exact_second")referenced=decoded;}
    check(source.find("structuredContent")&&source.find("structuredContent")->find("padding")&&referenced.find("structuredContent")&&referenced.find("structuredContent")->find("ark_receipt_reference")&&field(*referenced.find("structuredContent")->find("ark_receipt_reference"),"call_id")=="exact_first","serialized CLI history retains complete source and direct reference together");
    std::cout<<"Claude compaction guards: host projection, auto/manual boundaries, failed-read gate and exact reference custody passed\n";
}
}
