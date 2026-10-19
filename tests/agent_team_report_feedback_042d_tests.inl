// 042D: activity ratings ride on team reports instead of costing a separate
// tool_feedback provider round before completion.
static V ratingResult042d(std::set<std::string>& ledger,const std::set<std::string>& known,const V& args){
    const auto* ratings=args.find("ratings");std::set<std::string> ids;
    for(const auto& item:ratings->arr){const auto id=item.find("activity_id")->asStr();const auto utility=item.find("utility")->num;if(!known.count(id)||ledger.count(id)||!ids.insert(id).second||utility<1||utility>5)return json(R"({"isError":true,"structuredContent":{"error":"Invalid rating","error_code":"ACTIVITY_VALIDATION_FAILED"}})");}
    V rated=V::makeArr();for(const auto& id:ids){ledger.insert(id);rated.arr.push_back(V::makeStr(id));}
    V pending=V::makeArr();for(const auto& id:known)if(!ledger.count(id))pending.arr.push_back(V::makeStr(id));
    V result=json(R"({"isError":false,"structuredContent":{}})");V data=V::makeObj();data.set("rated_ids",rated);data.set("pending_feedback_ids",pending);result.set("structuredContent",data);return result;
}
static void reportRatingsReplaceFeedbackRound042d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("owner",true)};int round=0,feedbackCalls=0;bool schema=false,finishSchema=false,reported=false,cleared=false;std::set<std::string> ledger;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++round){
        case 1:for(const auto& tool:request.find("tools")->arr){const auto name=tool.find("name")->asStr();if(name!="team_report_task"&&name!="team_finish_goal")continue;const bool rated=tool.find("parameters")->find("properties")->find("ratings")!=nullptr;(name=="team_report_task"?schema:finishSchema)=rated;}return call("read","doc_info","{}");
        case 2:return call("finish","team_report_task",R"({"state":"completed","summary":"Inspected revision 0.","ratings":[{"activity_id":"a1","utility":4,"difficulty":2,"action":"Read the current revision"}]})");
        default:{const auto out=writeWindowOutput(request);reported=out.find("reported")&&out.find("reported")->b;cleared=out.find("rated_ids")&&out.find("rated_ids")->arr.size()==1&&out.find("rated_ids")->arr[0].asStr()=="a1";return done();}}};
    team.start(config,"Rate inside the report",ark::AgentSession::tools(),[&](const auto& name,const V& args){
        if(name=="doc_info")return json(R"({"isError":false,"structuredContent":{"revision":0,"activity_id":"a1","feedback_required":true}})");
        if(name=="tool_feedback"){++feedbackCalls;return ratingResult042d(ledger,{"a1"},args);}
        return V::makeObj();});
    complete(team);
    check(schema&&finishSchema,"team reports and goal reports advertise the tool_feedback rating items");
    check(reported&&cleared&&feedbackCalls==1&&ledger.count("a1"),"a completed report records its ratings through one executor call");
    check(round==3&&team.snapshot().roles[0].taskState=="completed","no separate feedback round or completion reminder follows a rated report");
}
static void rejectedReportRatingsRecordNothing042d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("owner",true)};int round=0,reads=0,feedbackCalls=0;bool partial=false,duplicate=false,rejected=false,untouched=false,listed=false,reported=false;std::set<std::string> ledger;
    const auto rating=[](const char* id,int utility){return std::string(R"({"activity_id":")")+id+R"(","utility":)"+std::to_string(utility)+R"(,"difficulty":2,"action":"Read the current revision"})";};
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++round){
        case 1:return call("read1","doc_info","{}");
        case 2:return call("read2","doc_info",R"({"detail":"revision"})");
        case 3:return call("partial","team_report_task",R"({"state":"completed","summary":"Partial ratings.","ratings":[)"+rating("a1",4)+"]}");
        case 4:{const auto out=writeWindowOutput(request);const auto* pending=out.find("unrated_activity_ids");partial=out.find("isError")->b&&out.find("error")->asStr().find("tool_feedback")!=std::string::npos&&pending&&pending->arr.size()==1&&pending->arr[0].asStr()=="a2";
            return call("duplicate","team_report_task",R"({"state":"completed","summary":"Duplicate ratings.","ratings":[)"+rating("a1",4)+","+rating("a1",3)+"]}");}
        case 5:duplicate=writeWindowOutput(request).find("isError")->b;
            return call("invalid","team_report_task",R"({"state":"completed","summary":"One invalid rating.","ratings":[)"+rating("a1",4)+","+rating("a2",9)+"]}");
        case 6:{const auto out=writeWindowOutput(request);rejected=out.find("isError")->b&&out.find("error_code")&&out.find("error_code")->asStr()=="ACTIVITY_VALIDATION_FAILED";untouched=ledger.empty();const auto* pending=out.find("unrated_activity_ids");listed=pending&&pending->arr.size()==2;
            return call("valid","team_report_task",R"({"state":"completed","summary":"Both reads rated.","ratings":[)"+rating("a1",4)+","+rating("a2",3)+"]}");}
        default:{const auto out=writeWindowOutput(request);reported=out.find("reported")&&out.find("reported")->b;return done();}}};
    team.start(config,"Reject incomplete or invalid report ratings",ark::AgentSession::tools(),[&](const auto& name,const V& args){
        if(name=="doc_info")return json(++reads==1?R"({"isError":false,"structuredContent":{"revision":0,"activity_id":"a1","feedback_required":true}})":R"({"isError":false,"structuredContent":{"revision":0,"activity_id":"a2","feedback_required":true}})");
        if(name=="tool_feedback"){++feedbackCalls;return ratingResult042d(ledger,{"a1","a2"},args);}
        return V::makeObj();});
    complete(team);
    check(partial,"a completed report that rates only some pending IDs is rejected and lists the rest");
    check(duplicate,"one report cannot rate the same activity twice");
    check(rejected&&untouched&&listed,"an invalid rating rejects the whole report without recording any rating");
    check(reported&&feedbackCalls==2&&ledger.size()==2&&round==7,"only the invalid and the corrected reports reach the rating executor");
    check(team.snapshot().roles[0].taskState=="completed","the corrected report completes the task");
}
static void progressReportCarriesRatings042d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("owner",true)};int round=0;bool rated=false;std::set<std::string> ledger;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++round){
        case 1:return call("read","doc_info","{}");
        case 2:return call("progress","team_report_task",R"({"state":"in_progress","summary":"Still shaping the frame.","ratings":[{"activity_id":"a1","utility":3,"difficulty":3,"action":"Read the current revision"}]})");
        default:{const auto out=writeWindowOutput(request);rated=out.find("reported")&&out.find("reported")->b&&out.find("rated_ids")&&out.find("rated_ids")->arr.size()==1;return done();}}};
    team.start(config,"Rate during a progress report",ark::AgentSession::tools(),[&](const auto& name,const V& args){
        if(name=="doc_info")return json(R"({"isError":false,"structuredContent":{"revision":0,"activity_id":"a1","feedback_required":true}})");
        if(name=="tool_feedback")return ratingResult042d(ledger,{"a1"},args);
        return V::makeObj();});
    complete(team);
    check(rated&&ledger.count("a1")&&round==3&&team.snapshot().roles[0].taskState=="in_progress","a progress report records ratings and keeps its state");
}
static void handoffNoticeCarriesReport042d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("lead",true),role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PartDesigner;
    int leadRound=0,designerRound=0;bool reported=false,carried=false,noPolling=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++leadRound){
        case 1:return call("assign","team_send",R"({"to":"designer","kind":"assignment","message":"Design the bracket."})");
        case 2:return done("Assigned the bracket.");
        default:{const auto input=request.find("input")->dump();carried=input.find("Explicit task report: completed / Bracket ready at revision 0.")!=std::string::npos&&input.find("Committed steps by this role: 0")!=std::string::npos;noPolling=input.find("Inspect team_context and current doc_info")==std::string::npos;return done("Integrated the reported bracket.");}}};
    config.roles[1].transport=[&](auto&,const V& request,auto*){
        switch(++designerRound){
        case 1:return call("inspect","doc_info",R"({"detail":"revision"})");
        case 2:return call("report","team_report_task",R"({"state":"completed","summary":"Bracket ready at revision 0."})");
        default:{const auto out=writeWindowOutput(request);reported=out.find("reported")&&out.find("reported")->b;return done("Bracket reported.");}}};
    team.start(config,"Design a bracket",ark::AgentSession::tools(),[](const auto& name,const V&){return name=="doc_info"?json(R"({"isError":false,"structuredContent":{"revision":0}})"):V::makeObj();});
    complete(team);
    check(reported,"the designer's completed report is accepted");
    check(carried&&leadRound>=3,"the lead's handoff notice carries the designer's explicit report and measured counts");
    check(noPolling,"the handoff notice no longer asks for a team_context round");
}