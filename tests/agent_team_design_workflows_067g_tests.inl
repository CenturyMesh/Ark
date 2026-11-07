// Candidate custody contracts independent of the native producers exercised by
// design_variants/design_review tests. The executor supplies a real cached preview.
static void designWorkflowCandidateCustody067g(){
    for(const auto& request:std::vector<std::pair<std::string,std::string>>{
        {"design_variants","prepare"},{"design_variants","save"},
        {"design_variants","remove"},{"design_review","prepare_repair"}}){
        ark::AgentSession session;ark::AgentTeam team;ark::AgentTeamConfig config;
        config.roles={role("engineer",true)};int rounds=0;std::string candidate;
        config.roles[0].transport=[&](const auto&,const V& input,const auto*){
            switch(++rounds){
                case 1:return call("inspect","doc_info",R"({"detail":"revision"})");
                case 2:return call("prepare",request.first,"{\"action\":\""+request.second+"\",\"expected_revision\":0}");
                case 3:{auto receipt=lastTeamReceipt(input);candidate=receipt.find("candidate_id")?receipt.find("candidate_id")->asStr():"";
                    return call("apply","team_apply_step","{\"candidate_id\":\""+candidate+"\",\"expected_revision\":0}");}
                default:return done("Reviewed candidate applied");
            }
        };
        team.start(config,"Review and apply a design candidate",ark::AgentSession::tools(),[&](const auto& name,const V& args){
            if(name==request.first)return session.call("doc_preview",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"reviewed_width","expr":"24"}]})"));
            return session.call(name,args);
        });complete(team);
        check(!candidate.empty()&&session.revision==1&&session.document.getParam("reviewed_width")==24,"host-generated design preview retains role ownership through team_apply_step");
    }
    for(const auto& producer:std::vector<std::pair<std::string,std::string>>{{"design_variants","prepare"},{"design_review","prepare_repair"}}){
        ark::AgentSession session;ark::AgentTeam team;ark::AgentTeamConfig config;
        config.roles={role("programmer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::ParametricDesigner;
        int rounds=0;std::string preparedReceipt,publishReceipt,hostCandidate;
        config.roles[0].transport=[&](const auto&,const V& input,const auto*){
            switch(++rounds){
                case 1:return call("inspect","doc_info",R"({"detail":"revision"})");
                case 2:return call("prepare",producer.first,"{\"action\":\""+producer.second+"\",\"expected_revision\":0}");
                case 3:preparedReceipt=lastTeamReceipt(input).dump();return call("publish","team_apply_step","{\"candidate_id\":\""+hostCandidate+"\",\"expected_revision\":0}");
                default:publishReceipt=lastTeamReceipt(input).dump();return done("Rejected unrelated recipe");
            }
        };
        team.start(config,"Only compile the assigned parametric program",ark::AgentSession::tools(),[&](const auto& name,const V& args){
            if(name==producer.first){auto response=session.call("doc_preview",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Unrelated ordinary feature","p":{"dx":5,"dy":6,"dz":7}}]})"));const auto* data=response.find("structuredContent");if(data&&data->find("candidate_id"))hostCandidate=data->find("candidate_id")->asStr();return response;}
            return session.call(name,args);
        });complete(team);
        check(!hostCandidate.empty()&&preparedReceipt.find("outside this role")!=std::string::npos,"Generated candidate must satisfy the recipient role discipline before custody");
        check(session.revision==0&&session.document.features.empty()&&publishReceipt.find("no successful preview receipt")!=std::string::npos,"Even a guessed retained candidate id cannot bypass rejected role custody");
    }
    const auto tools=ark::AgentSession::tools();
    check(ark::assistantToolReadOnly(tools,"design_variants",json(R"({"action":"explore"})")),"exploration is a read-only isolated evaluation");
    check(!ark::assistantToolReadOnly(tools,"design_variants",json(R"({"action":"prepare"})")),"variant preview respects steering and reservation gates");
    check(ark::assistantToolReadOnly(tools,"design_review",json(R"({"action":"compare"})")),"revision comparison is read-only");
    check(!ark::assistantToolReadOnly(tools,"design_review",json(R"({"action":"prepare_repair"})")),"repair preview respects steering and reservation gates");
    ark::AgentSession session;
    for(const char* topic:{"variants","design_review"}){
        auto receipt=session.call("agent_onboard",json(std::string("{\"topic\":\"")+topic+"\",\"detail\":\"guide\"}"));
        check(!receipt.find("isError")->b,"new design workflow guidance is discoverable");
    }
}
