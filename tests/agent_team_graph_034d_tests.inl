static void sharedGraph034d(){
    ark::AgentSession session;session.suppressIterationHistory();session.suppressFailureCapture();
    auto setup=session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"bom.initialize"},{"op":"feature.add","type":"box","name":"Shared suspension","p":{"dx":20,"dy":10,"dz":5}}]})"));
    check(!setup.find("isError")->b,"shared graph fixture authors one valid public part");
    ark::AgentTeam team;ark::AgentTeamConfig config;config.maxWorkers=2;config.focusedWorkflow=false;
    config.roles={role("engineer",true),role("reviewer")};
    std::mutex graphMutex;std::condition_variable graphReady;bool firstRead=false,committed=false;int writerRounds=0,readerRounds=0,graphCalls=0;V firstGraph,lastGraph;bool crossRoleReused=false;
    config.roles[0].transport=[&](auto&,const V&,auto*){
        const int round=++writerRounds;if(round==1)return call("writer-graph","context_graph",R"({"keys":["feature:1"],"depth":0})");
        if(round==2){std::unique_lock lock(graphMutex);if(!graphReady.wait_for(lock,3s,[&]{return firstRead;}))throw std::runtime_error("reviewer graph timeout");return call("writer-edit","doc_apply",R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":36}}],"detail":"changes"})");}
        return done("Shared part edited with the observed revision");
    };
    config.roles[1].transport=[&](auto&,const V&,auto*){
        const int round=++readerRounds;if(round==1)return call("reviewer-graph","context_graph",R"({"keys":["feature:1"],"depth":0})");
        if(round==2){std::unique_lock lock(graphMutex);if(!graphReady.wait_for(lock,3s,[&]{return committed;}))throw std::runtime_error("shared publication timeout");V args=json(R"({"keys":["feature:1"],"depth":0,"expected_revision":2})");args.set("known_scope",*firstGraph.find("scope"));args.set("known_fingerprints",*firstGraph.find("fingerprints"));return call("reviewer-updated-graph","context_graph",args.dump());}
        return done("Reviewed the actual shared revision");
    };
    team.start(config,"Engineer changes the suspension; read-only reviewer inspects the shared graph",ark::AgentSession::tools(),[&](const std::string& name,const V& args){
        V result=session.call(name,args);const auto* data=result.find("structuredContent");
        if(name=="context_graph"&&!result.find("isError")->b){std::lock_guard lock(graphMutex);++graphCalls;if(session.revision==1){if(data->find("index_reused")->b)crossRoleReused=true;if(ark::AgentTeam::currentRoleId()=="reviewer"){firstGraph=*data;firstRead=true;graphReady.notify_all();}}else lastGraph=*data;}
        if(name=="doc_apply"&&!result.find("isError")->b){std::lock_guard lock(graphMutex);committed=true;graphReady.notify_all();}
        return result;
    });
    complete(team);const auto snapshot=team.snapshot(true);
    check(snapshot.roles[0].state==ark::AgentTeamState::Idle&&snapshot.roles[1].state==ark::AgentTeamState::Idle,"distinct parallel tasks can inspect the same project graph");
    check(graphCalls==3&&crossRoleReused,"different roles reuse the shared host index instead of private copies");
    check(session.revision==2&&firstGraph.find("source_sha256")->str!=lastGraph.find("source_sha256")->str&&firstGraph.find("scope")->str==lastGraph.find("scope")->str,"a peer commit invalidates the shared graph at the new revision");
    const auto& changed=lastGraph.find("nodes")->arr[0];check(changed.find("changed")->b&&changed.find("facts")->find("p")->find("dx")->str=="36","read-only peer receives updated facts rather than a stale fingerprint match");
}
