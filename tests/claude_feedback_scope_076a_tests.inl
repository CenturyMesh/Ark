// 076A: the tool_feedback completion gate covers the actions of the current run.
// Unrated rows that earlier conversations or providers left under the same actor
// id stay pending in the project ledger as non-blocking backlog. A reminder is
// repeated while ratings are accepted; a reminder round without any accepted
// rating ends with the reply, an explicit note and the rows still pending.
namespace claude_feedback_076a {
struct Step {std::string tool;std::function<V()> args;std::string text;};
struct Cli {
    std::deque<V> incoming;std::vector<std::string> prompts,own,rated;V catalog=V::makeArr();int mcpId=0,control=0;
    std::function<std::vector<Step>(std::size_t)> turn;std::deque<Step> steps;
    void mcp(const char* method,V params,bool notification=false){auto m=V::makeObj();m.set("jsonrpc",V::makeStr("2.0"));m.set("method",V::makeStr(method));if(!notification)m.set("id",V::makeNum(mcpId++));if(!params.isNull())m.set("params",params);auto r=V::makeObj(),packet=V::makeObj();r.set("subtype",V::makeStr("mcp_message"));r.set("server_name",V::makeStr("ark"));r.set("message",m);packet.set("type",V::makeStr("control_request"));packet.set("request_id",V::makeStr("f"+std::to_string(++control)));packet.set("request",r);incoming.push_back(packet);}
    // One scripted step at a time: a tool call waits for its host receipt.
    void advance(){if(steps.empty())return;auto step=steps.front();steps.pop_front();
        if(!step.tool.empty()){auto p=V::makeObj(),meta=V::makeObj();p.set("name",V::makeStr(step.tool));p.set("arguments",step.args?step.args():V::makeObj());meta.set("claudecode/toolUseId",V::makeStr("toolu_f"+std::to_string(mcpId)));p.set("_meta",meta);mcp("tools/call",p);return;}
        auto text=json(R"({"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":""}]}})");auto message=*text.find("message");auto content=*message.find("content");content.arr[0].set("text",V::makeStr(step.text));message.set("content",content);text.set("message",message);incoming.push_back(text);
        auto r=V::makeObj();r.set("type",V::makeStr("result"));r.set("subtype",V::makeStr("success"));r.set("is_error",V::makeBool(false));r.set("result",V::makeStr(step.text));r.set("usage",json(kUsage));incoming.push_back(r);}
    // Ratings for this script's own unrated actions only; never the backlog rows.
    V rateOwn(){V ratings=V::makeArr();for(const auto& id:own)if(std::find(rated.begin(),rated.end(),id)==rated.end()){V item=json(R"({"utility":4,"difficulty":1,"action":"Read the scratch document revision"})");item.set("activity_id",V::makeStr(id));ratings.arr.push_back(item);}V args=V::makeObj();args.set("ratings",ratings);return args;}
};
class Channel final:public AssistantAcpChannel {
    std::shared_ptr<Cli> c;
public:
    explicit Channel(std::shared_ptr<Cli> cli):c(std::move(cli)){}
    void send(const V& p)override{const auto type=field(p,"type");
        if(type=="control_request"){if(field(*p.find("request"),"subtype")=="initialize"){c->mcp("initialize",json(R"({"protocolVersion":"2025-11-25","capabilities":{}})"));auto r=V::makeObj(),inner=V::makeObj();inner.set("subtype",V::makeStr("success"));inner.set("request_id",V::makeStr("ark_init"));inner.set("response",json(kInit));r.set("type",V::makeStr("control_response"));r.set("response",inner);c->incoming.push_back(r);}return;}
        if(type=="control_response"){const auto* body=p.find("response")->find("response");const auto* m=body?body->find("mcp_response"):nullptr;const auto* result=m?m->find("result"):nullptr;if(!result)return;
            if(result->find("serverInfo")){c->mcp("notifications/initialized",V{},true);c->mcp("tools/list",V{});}
            else if(result->find("tools"))c->catalog=*result->find("tools");
            else if(result->find("content")){if(const auto* data=result->find("structuredContent")){if(const auto* id=data->find("activity_id"))c->own.push_back(id->asStr());if(const auto* ids=data->find("rated_ids"))for(const auto& id:ids->arr)c->rated.push_back(id.asStr());}c->advance();}
            return;}
        if(type=="user"){c->prompts.push_back(field(*p.find("message"),"content"));auto init=json(R"({"type":"system","subtype":"init","permissionMode":"dontAsk","model":"claude-opus-5-5","mcp_servers":[{"name":"ark","status":"connected","source":"sdk"}]})");auto names=V::makeArr();for(const auto& tool:c->catalog.arr)names.arr.push_back(V::makeStr("mcp__ark__"+field(tool,"name")));init.set("tools",names);c->incoming.push_back(init);
            auto steps=c->turn(c->prompts.size());c->steps.assign(steps.begin(),steps.end());c->advance();}}
    std::optional<V> receive(std::chrono::milliseconds wait)override{if(c->incoming.empty()){std::this_thread::sleep_for(wait);return {};}auto v=c->incoming.front();c->incoming.pop_front();return v;}
};
V activityTools(){V out=V::makeArr();for(const auto& tool:AgentSession::tools().arr){const auto name=field(tool,"name");if(name=="doc_info"||name=="tool_feedback"||name=="activity_info")out.arr.push_back(tool);}return out;}
struct Fixture {
    AgentSession session;std::shared_ptr<Cli> cli=std::make_shared<Cli>();V actor=json(R"({"kind":"agent","id":"assistant","model":"claude-opus-5-5"})");std::vector<std::string> backlog;std::vector<std::string> statuses;
    // Two unrated reads that earlier conversations recorded for the same Single actor id.
    Fixture(){for(const char* model:{"claude-opus-5-5","gpt-5.5"}){V args=V::makeObj(),earlier=actor;earlier.set("model",V::makeStr(model));args.set("actor",earlier);const auto receipt=session.call("doc_info",args);backlog.push_back(receipt.find("structuredContent")->find("activity_id")->asStr());}}
    AssistantResult run(){auto c=assistantProviderPreset("claude-code");c.timeoutSeconds=2;auto shared=cli;c.acpChannelFactory=[shared]{return std::make_unique<Channel>(shared);};
        try{return runAssistant(c,V::makeStr("Inspect the scratch document"),activityTools(),[&](const std::string& name,const V& args){V scoped=args;scoped.set("actor",actor);return session.call(name,scoped);},[&](const AssistantEvent& event){if(event.kind==AssistantEvent::Status)statuses.push_back(event.text);});}
        catch(const std::exception& error){throw std::runtime_error(std::string("Run-scoped tool feedback gate failed: ")+error.what());}}
    bool rated(const std::string& id)const{for(const auto& row:session.document.agentActivity->entries)if(row.find("id")->asStr()==id)return row.find("feedback")!=nullptr;return false;}
    bool mentionsBacklog(const std::string& text)const{for(const auto& id:backlog)if(text.find(id)!=std::string::npos)return true;return false;}
};
// The model rated every action of its own run: earlier rows cannot demand a reminder.
void ownActionsRatedBeforeFinishing(){
    Fixture f;Cli* c=f.cli.get();
    c->turn=[c](std::size_t n)->std::vector<Step>{if(n==1)return {{"doc_info",{},""},{"tool_feedback",[c]{return c->rateOwn();},""},{"",{},"Inspected and rated."}};return {{"",{},"Unexpected feedback reminder."}};};
    const auto run=f.run();
    check(c->prompts.size()==1&&run.text=="Inspected and rated.","Backlog rows from earlier conversations must not trigger a reminder after the run rated its own actions");
    check(f.rated(c->own.at(0))&&!f.rated(f.backlog[0])&&!f.rated(f.backlog[1]),"Only the run's own action is rated; earlier rows stay pending in the ledger");
    check(field(run.feedback,"state")=="complete"&&run.feedback.find("run_actions")->num==1&&run.feedback.find("backlog_pending")->num==2,"The run receipt separates this run's actions from the non-blocking backlog");
}
// The reported sequence: reads, a reminder, ratings plus a verification read, then
// a reminder for exactly that new read, while ratings are being accepted.
void progressRemindsForNewReads(){
    Fixture f;Cli* c=f.cli.get();
    c->turn=[c](std::size_t n)->std::vector<Step>{
        if(n==1)return {{"doc_info",{},""},{"doc_info",[]{return json(R"({"detail":"revision"})");},""},{"",{},"Summary: the scratch document is empty at revision 0."}};
        if(n==2)return {{"tool_feedback",[c]{return c->rateOwn();},""},{"doc_info",[]{return json(R"({"detail":"revision"})");},""},{"",{},"Feedback recorded."}};
        if(n==3)return {{"tool_feedback",[c]{return c->rateOwn();},""},{"",{},"Remaining feedback recorded."}};
        return {{"",{},"Unexpected extra reminder."}};};
    const auto run=f.run();
    check(c->prompts.size()==3,"Accepted ratings re-arm one more reminder for the read made after the first reminder");
    check(c->prompts[1].find(c->own[0])!=std::string::npos&&c->prompts[1].find(c->own[1])!=std::string::npos&&!f.mentionsBacklog(c->prompts[1]),"The first reminder lists this run's actions and no earlier conversation rows");
    check(c->prompts[2].find(c->own[2])!=std::string::npos&&c->prompts[2].find(c->own[0])==std::string::npos&&!f.mentionsBacklog(c->prompts[2]),"The next reminder lists exactly the still-pending action of this run");
    check(c->prompts[1].find("2 older unrated")!=std::string::npos&&c->prompts[1].find("activity_info")!=std::string::npos,"The reminder reports the backlog count and how to inspect it");
    check(run.text.find("Summary: the scratch document is empty at revision 0.")!=std::string::npos&&run.text.find("Remaining feedback recorded.")!=std::string::npos,"The reply held for feedback is shown with the final reply");
    check(f.rated(c->own[0])&&f.rated(c->own[1])&&f.rated(c->own[2])&&!f.rated(f.backlog[0])&&!f.rated(f.backlog[1]),"Ratings stay confined to actual actions of this run");
    check(field(run.feedback,"state")=="complete"&&run.feedback.find("reminders")->num==2&&run.feedback.find("run_rated")->num==3,"The run receipt records the reminders and the ratings actually accepted");
}
// A reminder round that records nothing ends the run without an Interrupted error.
void zeroProgressKeepsReply(){
    Fixture f;Cli* c=f.cli.get();
    c->turn=[](std::size_t n)->std::vector<Step>{if(n==1)return {{"doc_info",{},""},{"",{},"Summary: inspected revision 0."}};return {{"",{},"I did not rate the read."}};};
    const auto run=f.run();
    check(c->prompts.size()==2,"Without an accepted rating the reminder is not repeated");
    check(run.text.find("Summary: inspected revision 0.")!=std::string::npos&&run.text.find("I did not rate the read.")!=std::string::npos,"The final reply is accepted and shown");
    check(run.text.find("tool feedback is still pending for 1 action of this run")!=std::string::npos,"The reply carries an explicit non-error note about the missing feedback");
    check(std::any_of(f.statuses.begin(),f.statuses.end(),[](const std::string& s){return s.find("Feedback pending for 1 action of this run")!=std::string::npos;}),"A non-error status reports the pending feedback");
    check(!f.rated(c->own[0])&&f.session.document.agentActivity->entries.size()==3,"Pending rows are retained, never fabricated or deleted");
    check(field(run.feedback,"state")=="pending"&&run.feedback.find("run_pending")->num==1&&run.feedback.find("run_pending_ids")->arr[0].asStr()==c->own[0],"The run receipt records the missing feedback");
    check(contains(run.conversation,"ARK_FEEDBACK_PENDING_V1"),"The retained conversation records the pending feedback for the next run");
}
// Review finding: rating the listed IDs and making one new read in every
// reminder turn kept a run reminding forever (Studio has no turn budget). A
// reminder continues only while this run's unrated count falls.
void ratingAndReadingEveryTurnEnds(){
    Fixture f;Cli* c=f.cli.get();
    c->turn=[c](std::size_t n)->std::vector<Step>{
        if(n==1)return {{"doc_info",{},""},{"doc_info",[]{return json(R"({"detail":"revision"})");},""},{"",{},"Summary: inspected revision 0."}};
        if(n<=12)return {{"tool_feedback",[c]{return c->rateOwn();},""},{"doc_info",[]{return json(R"({"detail":"revision"})");},""},{"",{},"Rated and verified."}};
        return {{"",{},"Gave up after repeated reminders."}};};
    const auto run=f.run();
    check(c->prompts.size()==3,"Claude Code reminds twice, then ends once rating plus a new read leaves the unrated count unchanged");
    check(run.text.find("Gave up")==std::string::npos&&run.text.find("tool feedback is still pending for 1 action of this run")!=std::string::npos,"The run ends with the reply and the explicit pending note");
    check(field(run.feedback,"state")=="pending"&&run.feedback.find("reminders")->num==2&&run.feedback.find("run_rated")->num==3&&run.feedback.find("run_pending")->num==1,"The run receipt records the bounded reminders and the read left unrated");
    check(std::any_of(f.statuses.begin(),f.statuses.end(),[](const std::string& s){return s.find("Feedback pending for 1 action of this run")!=std::string::npos;}),"A non-error status reports the pending read");
}
// Every scenario runs, so one report names each failed contract.
void checks(){std::string failures;for(auto scenario:{ownActionsRatedBeforeFinishing,progressRemindsForNewReads,zeroProgressKeepsReply,ratingAndReadingEveryTurnEnds})try{scenario();}catch(const std::exception& error){failures+=std::string(failures.empty()?"":" | ")+error.what();}if(!failures.empty())throw std::runtime_error(failures);}
}
