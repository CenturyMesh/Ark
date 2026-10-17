// D.02: real Team/Session receipts and scripted exact-reference recovery.
static V lastTeamReceipt038d(const V& request){
    const auto* input=request.find("input");
    if(input)for(auto it=input->arr.rbegin();it!=input->arr.rend();++it)if(it->find("type")&&it->find("type")->asStr()=="function_call_output")return json(it->find("output")->asStr());
    throw std::runtime_error("Missing scripted team receipt");
}
static void projectedBatchReadRecovery038d(){
    ark::AgentSession session;ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer")};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    int round=0,executed=0;bool rejected=false,retained=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++round){
        case 1:return call("mixed_projection","query_batch",R"({"queries":[{"tool":"doc_info","args":{"detail":"revision"},"fields":["revision"]},{"tool":"doc_info","args":{"detail":"summary"},"fields":["absent_projection"]}]})");
        case 2:{const auto receipt=lastTeamReceipt038d(request);const auto* rows=receipt.find("structuredContent")->find("results");retained=!receipt.find("isError")->b&&rows->arr.size()==2&&!rows->arr[0].find("isError")->b&&rows->arr[0].find("structuredContent")->find("revision")&&rows->arr[1].find("isError")->b&&rows->arr[1].find("structuredContent")->find("observed_result");check(retained&&team.snapshot(true,0,true).roles[0].unresolvedToolErrors==1,"mixed successful batch retains valid rows and tracks projected row error");return call("unrelated_projection","query_batch",R"({"queries":[{"tool":"doc_info","args":{"detail":"revision"},"fields":["revision"]}]})");}
        case 3:{const auto snapshot=team.snapshot(true,0,true);check(snapshot.roles[0].unresolvedToolErrors==1&&snapshot.roles[0].toolFailures.size()==1&&snapshot.roles[0].toolFailures[0].find("tool")->asStr()=="doc_info"&&snapshot.roles[0].toolFailures[0].find("query_index")->num==1,"unrelated successful same-name batch cannot clear the rejected nested query");return call("hide_projection","team_finish_goal",R"({"state":"completed","summary":"Attempt to hide retained projection failure."})");}
        case 4:rejected=lastTeamReceipt038d(request).find("isError")->b;return call("corrected_projection","query_batch",R"({"queries":[{"tool":"doc_info","args":{"detail":"summary"},"fields":["revision"]}]})");
        case 5:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==0,"corrected exact query clears its projection failure");return call("finish_projection","team_finish_goal",R"({"state":"completed","summary":"Corrected projection returned current measured evidence."})");
        default:return done("Read recovery verified.");
        }
    };
    team.start(config,"Inspect actual read receipts",ark::AgentSession::tools(),[&](const auto& name,const auto& args){++executed;return session.call(name,args);});complete(team);
    const auto snapshot=team.snapshot(true,0,true);check(rejected&&retained&&executed==3&&session.revision==0&&snapshot.goalState==ark::AgentGoalState::Completed&&snapshot.unresolvedErrors==0,"real Session mixed-batch recovery preserves successful output without retries, document changes or concealed completion errors");
    bool errorEvent=false;for(const auto& event:snapshot.events)if(event.kind==ark::AgentTeamEvent::ToolEnd&&event.tool=="query_batch"&&event.toolError)errorEvent=true;check(errorEvent,"nested batch rejection marks the team tool event as failed");
}
static void exactReferenceReadRecovery038d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer")};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    int round=0,leftReads=0;bool rejected=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++round){
        case 1:return call("failed_refs","query_batch",R"({"queries":[{"tool":"reference_inspect","args":{"id":7,"reference":"face.left"}},{"tool":"reference_inspect","args":{"id":7,"reference":"face.right"}}]})");
        case 2:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==2,"distinct rejected references retain two blockers");return call("other_ref","query_batch",R"({"queries":[{"tool":"reference_inspect","args":{"id":8,"reference":"face.left"}}]})");
        case 3:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==2,"another object read cannot discharge rejected references");return call("old_left","reference_inspect",R"({"reference":"face.left","id":7})");
        case 4:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==2,"older measured evidence cannot discharge current reference failure");return call("fresh_left","reference_inspect",R"({"reference":"face.left","id":7})");
        case 5:{const auto snapshot=team.snapshot(true,0,true);check(snapshot.roles[0].unresolvedToolErrors==1&&snapshot.roles[0].toolFailures.size()==1&&snapshot.roles[0].toolFailures[0].find("args")->find("reference")->asStr()=="face.right","successful exact direct read clears only its matching batch row despite argument key order");return call("hide_other_ref","team_finish_goal",R"({"state":"completed","summary":"Attempt to hide the remaining reference failure."})");}
        case 6:rejected=lastTeamReceipt038d(request).find("isError")->b;return call("fresh_right","query_batch",R"({"queries":[{"tool":"reference_inspect","args":{"reference":"face.right","id":7}}]})");
        case 7:return call("finish_refs","team_finish_goal",R"({"state":"completed","summary":"Both exact reference queries now succeeded at the observed revision."})");
        default:return done("Reference receipts checked.");
        }
    };
    team.start(config,"Retain exact rejected references",ark::AgentSession::tools(),[&](const std::string& name,const V& args){
        if(name=="query_batch"){
            if(round==1)return json(R"({"isError":false,"structuredContent":{"revision":4,"results":[{"isError":true,"structuredContent":{"error_code":"REFERENCE_MISSING","error":"Left reference unavailable","revision":4}},{"isError":true,"structuredContent":{"error_code":"REFERENCE_MISSING","error":"Right reference unavailable","revision":4}}]}})");
            return json(R"({"isError":false,"structuredContent":{"revision":4,"results":[{"isError":false,"structuredContent":{"reference_valid":true}}]}})");
        }
        check(name=="reference_inspect"&&args.find("id")->num==7,"script only executes the expected exact reference read");
        return ++leftReads==1?json(R"({"isError":false,"structuredContent":{"revision":3,"reference_valid":true}})"):json(R"({"isError":false,"structuredContent":{"revision":4,"reference_valid":true}})");
    });complete(team);const auto snapshot=team.snapshot(true,0,true);check(rejected&&leftReads==2&&snapshot.goalState==ark::AgentGoalState::Completed&&snapshot.unresolvedErrors==0,"fresh exact recovery resolves individual references and goal gate retains other blockers");
}
static void staleReadRevisionRecovery038d(){
    for(bool omitPrecondition:{false,true}){
    ark::AgentSession session;session.revision=4;ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer")};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    int round=0;bool rejected=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        switch(++round){
        case 1:return call("stale_context","object_context",R"({"expected_revision":3,"limit":1})");
        case 2:{const auto snapshot=team.snapshot(true,0,true);check(snapshot.roles[0].unresolvedToolErrors==1&&snapshot.roles[0].toolFailures.size()==1&&snapshot.roles[0].toolFailures[0].find("error_code")->asStr()=="REVISION_CONFLICT","real stale object_context retains its measured conflict");return call("other_context","object_context",R"({"expected_revision":4,"limit":2})");}
        case 3:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==1,"different successful current context query does not clear stale selection");return call("hide_stale_context","team_finish_goal",R"({"state":"completed","summary":"Attempt to hide stale context."})");
        case 4:rejected=lastTeamReceipt038d(request).find("isError")->b;return call("older_context_receipt","object_context",omitPrecondition?R"({"limit":1})":R"({"limit":1,"expected_revision":4})");
        case 5:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==1,"older context receipt cannot recover the known revision conflict even when read precondition is omitted");return call("current_context","object_context",omitPrecondition?R"({"limit":1})":R"({"limit":1,"expected_revision":4})");
        case 6:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==0,"same-query current revision recovers stale inspection with explicit or omitted optional read precondition");return call("finish_context","team_finish_goal",R"({"state":"completed","summary":"The same context query succeeded at observed revision four."})");
        default:return done("Current context inspected.");
        }
    };
    team.start(config,"Recover an actual stale read",ark::AgentSession::tools(),[&](const auto& name,const auto& args){if(round==4)return json(R"({"isError":false,"structuredContent":{"revision":3,"objects":[]}})");return session.call(name,args);});complete(team);const auto snapshot=team.snapshot();check(rejected&&session.revision==4&&snapshot.goalState==ark::AgentGoalState::Completed&&snapshot.unresolvedErrors==0,"ordinary single-tool revision recovery remains valid with measured freshness and completed-goal gating");
    }
}
static void correctedPreviewRecovery038d(){
    for(const char* tool:{"doc_preview","manufacturing_propose"}){
        ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};int round=0,executed=0;
        config.roles[0].transport=[&](auto&,const V&,auto*){
            switch(++round){
            case 1:return call("failed_preview",tool,R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"unknown_width"}]})");
            case 2:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==1,"ordinary preview rejection remains visible");return call("corrected_preview",tool,R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"10"}]})");
            case 3:check(team.snapshot(true,0,true).roles[0].unresolvedToolErrors==0,"corrected ordinary preview with changed commands retains existing same-tool recovery");return call("finish_preview","team_finish_goal",R"({"state":"completed","summary":"Corrected preview receipt inspected; no geometry publication or manufacturing qualification claimed."})");
            default:return done("Corrected preview retained.");
            }
        };
        team.start(config,"Recover a corrected preview",ark::AgentSession::tools(),[&](const auto& name,const V& args){check(name==tool&&args.find("commands"),"script executes only ordinary preview requests");return ++executed==1?json(R"({"isError":true,"structuredContent":{"revision":0,"error_code":"EVALUATION_FAILED","error":"The initial recipe was rejected."}})"):json(R"({"isError":false,"structuredContent":{"revision":0,"valid":true,"candidate_cached":false}})");});complete(team);
        const auto snapshot=team.snapshot();check(executed==2&&snapshot.unresolvedErrors==0&&snapshot.goalState==ark::AgentGoalState::Completed,"corrected preview recipe cannot leave an impossible exact-argument blocker");
    }
}
static void batchReceiptAssociationRecovery038d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer")};int round=0;
    config.roles[0].transport=[&](auto&,const V&,auto*){
        ++round;
        if(round==1)return call("initial_reference_error","reference_inspect",R"({"id":7,"reference":"face.left"})");
        if(round>=3&&round<=7)check(team.snapshot().roles[0].unresolvedToolErrors==2,"malformed batch receipt retains original reference failure plus protocol diagnostic");
        if(round>=2&&round<=7)return call("association_"+std::to_string(round),"query_batch",R"({"queries":[{"tool":"reference_inspect","args":{"id":7,"reference":"face.left"}},{"tool":"reference_inspect","args":{"id":7,"reference":"face.right"}}]})");
        if(round==8){check(team.snapshot().roles[0].unresolvedToolErrors==0,"only fully associated exact successful rows discharge retained failures");return call("finish_association","team_finish_goal",R"({"state":"completed","summary":"Validated row associations before accepting exact recovery evidence."})");}
        return done("Receipt association verified.");
    };
    team.start(config,"Validate host read associations",ark::AgentSession::tools(),[&](const std::string& name,const V&){
        if(name=="reference_inspect")return json(R"({"isError":true,"structuredContent":{"error_code":"REFERENCE_MISSING","error":"Inspect exact reference again","revision":4}})");
        V receipt=json(R"({"isError":false,"structuredContent":{"revision":4,"results":[{"isError":false,"structuredContent":{"revision":4},"query_index":0,"tool":"reference_inspect","args":{"id":7,"reference":"face.left"}},{"isError":false,"structuredContent":{"revision":4},"query_index":1,"tool":"reference_inspect","args":{"id":7,"reference":"face.right"}}]}})");
        V content=*receipt.find("structuredContent"),rows=*content.find("results");
        if(round==2)rows.arr[0].set("tool",V::makeStr("native_inspect"));
        if(round==3)rows.arr[0].set("args",json(R"({"id":8,"reference":"face.left"})"));
        if(round==4)rows.arr[0].set("query_index",V::makeNum(1));
        if(round==5)rows.arr.pop_back();
        // A correct first row cannot clear anything when a later row is wrong.
        if(round==6)rows.arr[1].set("args",json(R"({"id":8,"reference":"face.right"})"));
        content.set("results",rows);receipt.set("structuredContent",content);return receipt;
    });complete(team);const auto snapshot=team.snapshot(true,0,true);check(snapshot.unresolvedErrors==0&&snapshot.goalState==ark::AgentGoalState::Completed,"metadata/count mismatches cannot manufacture recovery or permanently block a subsequent valid batch");
}
