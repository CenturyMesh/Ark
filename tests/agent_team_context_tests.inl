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
