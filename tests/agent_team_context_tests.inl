static void oversizedHistoryResume(){
    ark::AgentTeam team;ark::AgentTeamConfig config;auto author=role("engineer",true);
    author.assistant.authMode=ark::AssistantAuthMode::ChatGptPlan;int rounds=0,writes=0;bool sawProjection=false;
    const auto large=std::string(3*1024*1024,'q');ark::AgentSession session;
    author.transport=[&](auto&,const V& request,auto*){
        const auto wire=request.dump();check(wire.size()<=ark::chatGptRequestByteLimit,"all resumed team requests fit configured transport cap");
        const int n=++rounds;
        if(n==1)return call("committed_initial","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":240}]})");
        if(n==2||n==3)return call("large_inspection_"+std::to_string(n),"doc_info","{}");
        if(n==4){sawProjection=wire.find("ARK_REQUEST_HISTORY_PROJECTION_V1")!=std::string::npos;throw ark::AssistantHttpError(400,false);}
        if(n==5)return call("blocked_resume_write","doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"width","expr":999}]})");
        if(n==6)return call("fresh_resume_read","doc_info","{}");
        if(n==7)return call("remaining_only","doc_apply",R"({"expected_revision":1,"commands":[{"op":"param.set","name":"height","expr":80}]})");
        return done("Resumed with original wheelbase and remaining height");
    };
    config.roles={author};
    team.start(config,"Keep wheelbase 240 mm; complete robot height",ark::AgentSession::tools(),[&](const auto& name,const V& args){
        if(name=="doc_apply")++writes;
        auto result=session.call(name,args);
        if(name=="doc_info"){V payload=*result.find("structuredContent");payload.set("large_historical_inventory",V::makeStr(large));result.set("structuredContent",payload);}
        return result;
    });complete(team);
    auto before=team.snapshot(true);if(!sawProjection)std::cerr<<"oversized fixture stopped before projection: "<<before.roles[0].error<<" requests="<<rounds<<"\n";check(sawProjection&&before.roles[0].state==ark::AgentTeamState::Interrupted&&writes==1&&session.revision==1,"size projection precedes transport error with first commit retained");
    check(before.roles[0].conversation.dump().find(large)!=std::string::npos,"interrupted role retains exact large tool receipt");
    check(team.resume("engineer"),"Resume accepts interrupted role with oversized original history");complete(team);
    auto after=team.snapshot(true);check(after.roles[0].state==ark::AgentTeamState::Idle&&rounds==8&&writes==2&&session.revision==2,"Resume sends bounded projection, blocks premature write, and commits only remaining step");
    check(session.document.getParam("width")==240&&session.document.getParam("height")==80,"committed intent survives request projection and Resume");
    check(after.roles[0].conversation.dump().find(large)!=std::string::npos&&after.usage.missingResponses==1,"full original evidence and unknown failed-request usage remain after recovery");
    std::cout<<"AGENT_OVERSIZED_RESUME original_receipt_bytes="<<large.size()<<" requests="<<rounds<<" writes="<<writes<<" final_revision="<<session.revision<<"\n";
}
static V semanticTeamOutput036a(const V& request){
    for(auto item=request.find("input")->arr.rbegin();item!=request.find("input")->arr.rend();++item)if(item->find("type")&&item->find("type")->asStr()=="function_call_output"){auto receipt=json(item->find("output")->asStr());return receipt.find("structuredContent")?*receipt.find("structuredContent"):receipt;}
    throw std::runtime_error("missing semantic team receipt");
}
static void semanticProgress036a(){
    // A.04/D.02: real offline provider/tool boundaries, no inferred cost/quality
    // claim. Repeated reads must not masquerade as a committed CAD checkpoint.
    ark::AgentSession session;session.suppressIterationHistory();ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer",true)};
    int round=0,writes=0;std::uint64_t firstEvidence=0;V stableRoles;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++round){
        case 1:return call("semantic_revision","doc_info","{}");
        case 2:return call("semantic_first","team_context","{}");
        case 3:{const auto result=semanticTeamOutput036a(request);check(result.find("changed")->b&&result.find("evidence_state")->asStr()=="changed","first caller read reports its semantic baseline");check(result.find("committed_steps")->num==0&&result.find("next_action")->find("action")->asStr()=="author_next_editable_checkpoint","no measured authoring gives an explicit editable-checkpoint action");stableRoles=*result.find("roles");firstEvidence=(std::uint64_t)result.find("evidence_version")->num;check(!stableRoles.arr[0].find("status")&&!stableRoles.arr[0].find("request_context")&&!stableRoles.arr[0].find("token_evidence"),"default progress omits transient tool status and request/token metrics");return call("semantic_repeat","team_context","{}");}
        case 4:{const auto result=semanticTeamOutput036a(request);check(!result.find("changed")->b&&result.find("evidence_state")->asStr()=="no_new_evidence"&&result.find("evidence_version")->num==firstEvidence&&result.find("roles")->dump()==stableRoles.dump(),"own polling and billed responses do not change semantic evidence");return call("semantic_diagnostics","team_context",R"({"detail":"diagnostics"})");}
        case 5:{const auto result=semanticTeamOutput036a(request);const auto& author=result.find("roles")->arr[0];check(!result.find("changed")->b&&author.find("status")&&author.find("request_context")&&author.find("token_evidence"),"explicit diagnostics retain measured metrics without manufacturing progress");return call("semantic_same_revision","doc_info","{}");}
        case 6:return call("semantic_after_read","team_context","{}");
        case 7:{const auto result=semanticTeamOutput036a(request);check(!result.find("changed")->b&&result.find("roles")->dump()==stableRoles.dump(),"another unchanged document read and tool status do not create evidence");return call("semantic_author","team_apply_step",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"checkpoint_width","expr":"20"},{"op":"feature.add","name":"Editable checkpoint","type":"box","p":{"dx":20,"dy":15,"dz":8}}]})");}
        case 8:{const auto result=semanticTeamOutput036a(request);check(!result.find("isError")&&result.find("revision")->num==1,"checkpoint publishes through the actual atomic CAD executor");return call("semantic_after_commit","team_context","{}");}
        case 9:{const auto result=semanticTeamOutput036a(request);check(result.find("changed")->b&&result.find("document_revision")->num==1&&result.find("committed_steps")->num==1&&result.find("roles")->arr[0].find("steps_applied")->num==1,"measured atomic commit changes semantic revision and authoring evidence");check(!session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"external_measurement","expr":"3"}]})")).find("isError")->b,"external revision fixture publishes a real edit");return call("semantic_external_revision","doc_info","{}");}
        case 10:return call("semantic_observed_revision","team_context","{}");
        case 11:{const auto result=semanticTeamOutput036a(request);check(result.find("changed")->b&&result.find("document_revision")->num==2&&result.find("committed_steps")->num==1,"new observed document revision changes evidence without inventing a team commit");return call("semantic_final_repeat","team_context","{}");}
        default:check(!semanticTeamOutput036a(request).find("changed")->b,"post-commit repeated polling returns no_new_evidence again");return done("One editable checkpoint authored; measured revision retained.");
        }
    };
    team.start(config,"Create one coherent editable concept checkpoint",ark::AgentSession::tools(),[&](const auto& name,const V& args){if(name=="doc_apply")++writes;return session.call(name,args);});complete(team);
    check(round==12&&writes==1&&session.revision==2&&session.document.getParam("checkpoint_width")==20,"semantic progress never hides or synthesizes CAD writes");
}
static void semanticHandoff036a(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true),role("reviewer")};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PhysicsReviewer;
    int lead=0,reviewer=0;std::uint64_t evidence=0;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++lead){
        case 1:return call("handoff_baseline","team_context","{}");
        case 2:return call("handoff_assignment","team_send",R"({"to":"reviewer","kind":"assignment","message":"Inspect only the current concept checkpoint and report actual evidence."})");
        case 3:{const auto result=semanticTeamOutput036a(request);check(result.find("delivered")->b&&result.find("handoff_required")->b&&result.find("recipients")->arr[0].find("state")->asStr()=="queued"&&reviewer==0,"Focused send truthfully acknowledges queued mail before recipient execution");return call("handoff_context","team_context","{}");}
        case 4:{const auto result=semanticTeamOutput036a(request);check(result.find("changed")->b&&result.find("roles")->arr[1].find("pending_messages")->num==1&&result.find("next_action")->find("action")->asStr()=="yield_after_handoff","actual assignment/mail and peer queue change evidence and demand Focused yield");evidence=(std::uint64_t)result.find("evidence_version")->num;return call("handoff_repeat","team_context","{}");}
        case 5:{const auto result=semanticTeamOutput036a(request);check(!result.find("changed")->b&&result.find("evidence_version")->num==evidence&&result.find("next_action")->find("action")->asStr()=="yield_after_handoff"&&reviewer==0,"polling cannot run a Focused recipient or manufacture progress");return done("Specific read-only assignment handed off.");}
        case 6:return call("handoff_returned","team_context","{}");
        case 7:{const auto result=semanticTeamOutput036a(request);check(result.find("changed")->b&&result.find("roles")->arr[1].find("task_state")->asStr()=="completed"&&result.find("next_action")->find("action")->asStr()!="yield_after_handoff","actual peer task/report/mail change evidence and previous-turn handoff clears");return done("Measured report received.");}
        default:return done();
        }
    };
    config.roles[1].transport=[&](auto&,const V& request,auto*){switch(++reviewer){case 1:return call("reviewer_own_baseline","team_context","{}");case 2:check(semanticTeamOutput036a(request).find("changed")->b,"each caller owns an independent semantic baseline");return call("reviewer_report","team_report_task",R"({"state":"completed","summary":"Read-only assigned checkpoint inspected; no geometry or manufacturing qualification claimed."})");case 3:return call("reviewer_evidence","team_send",R"({"to":"engineer","kind":"report","message":"Requested checkpoint inspection is complete; no CAD write was requested."})");default:return done("Measured report handed off.");}};
    team.start(config,"Review a concrete checkpoint without status loops",V::makeArr(),{});complete(team);check(lead==7&&reviewer==4&&team.snapshot().roles[0].turns==2,"focused handoff finishes before the queued reviewer runs and returns a measured report");
}
