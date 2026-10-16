static void cooperativeReceiptContracts032d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("owner",true)};ark::AgentSession session;session.suppressIterationHistory();
    V commands=V::makeArr();for(int i=0;i<32;++i){V command=json(R"({"op":"feature.add","type":"box","p":{"dx":3,"dy":4,"dz":5}})");command.set("name",V::makeStr("Public receipt fixture "+std::to_string(i)));commands.arr.push_back(command);}V fixture=V::makeObj();fixture.set("commands",commands);fixture.set("expected_revision",V::makeNum(0));check(!session.call("doc_apply",fixture).find("isError")->b,"public team fixture evaluates");
    int round=0;std::string first,second;double remaining=0;bool compact=false,noRenew=false,expired=false,repeated=false,newSafe=false,unknown=false,boundary=false;size_t verificationBytes=0,legacyBytes=session.call("doc_info",V::makeObj()).dump().size();
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        const auto output=writeWindowOutput(request);
        switch(++round){
        case 1:return call("current","doc_info",R"({"detail":"revision"})");
        case 2:return call("ready","team_reserve_write",R"({"expected_revision":1,"lease_ms":500})");
        case 3:{first=output.find("reservation_id")->asStr();remaining=output.find("lease_remaining_ms")->num;const auto& verification=*output.find("revision_verification");const auto* receipt=verification.find("structuredContent");compact=receipt&&!receipt->find("features")&&receipt->find("revision")->num==1&&output.find("lease_ms")->num==500&&!output.find("auto_renew")->b;verificationBytes=verification.dump().size();std::this_thread::sleep_for(70ms);return call("same_window","team_reserve_write",R"({"expected_revision":1,"lease_ms":30000})");}
        case 4:noRenew=output.find("already_reserved")->b&&!output.find("lease_renewed")->b&&output.find("reservation_id")->asStr()==first&&output.find("lease_ms")->num==500&&output.find("lease_remaining_ms")->num<remaining-30;std::this_thread::sleep_for(550ms);return call("release_expired","team_release_write",std::string("{\"reservation_id\":\"")+first+"\"}");
        case 5:expired=output.find("released")&&output.find("released")->b&&output.find("already_ended")->b&&output.find("reservation_status")->asStr()=="expired";return call("repeat_release","team_release_write",std::string("{\"reservation_id\":\"")+first+"\"}");
        case 6:repeated=output.find("released")&&output.find("already_ended")->b;return call("new_window","team_reserve_write",R"({"expected_revision":1,"lease_ms":30000})");
        case 7:second=output.find("reservation_id")->asStr();return call("old_receipt_cleanup","team_release_write",std::string("{\"reservation_id\":\"")+first+"\"}");
        case 8:return call("new_window_still_owned","team_context","{}");
        case 9:{const auto* window=output.find("write_reservation");newSafe=window&&window->find("active")->b&&window->find("reservation_id")->asStr()==second&&second!=first;return call("unknown_release","team_release_write",R"({"reservation_id":"never-issued"})");}
        case 10:unknown=output.find("isError")&&output.find("isError")->b;return call("release_new","team_release_write",std::string("{\"reservation_id\":\"")+second+"\"}");
        case 11:return call("replace_project","doc_new",R"({"expected_revision":1})");
        case 12:return call("old_project_cleanup","team_release_write",std::string("{\"reservation_id\":\"")+first+"\"}");
        default:boundary=output.find("isError")&&output.find("isError")->b;return done("Observed lease receipt checks complete.");
        }
    };
    team.start(config,"Explicit short reservation receipts",ark::AgentSession::tools(),[&](const auto& name,const V& args){return session.call(name,args);});complete(team);
    check(compact&&verificationBytes<legacyBytes,"reservation verification retains revision without duplicating the current feature inventory");
    check(noRenew,"reserving an owned live window never silently extends its initial TTL");
    check(expired&&repeated,"expired own receipt supports idempotent cleanup without another error or grant");
    check(newSafe&&unknown&&boundary,"old cleanup cannot release a fresh window, unknown tokens reject and project boundaries invalidate old receipts");
    check(session.revision==2&&session.document.features.empty(),"reservation receipts do not mutate geometry/revision; explicit doc_new remains authoritative");
    std::cout<<"team_verification full_bytes="<<legacyBytes<<" compact_bytes="<<verificationBytes<<"\n";
}
static void expiredReceiptOwnership032d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.maxWorkers=2;config.roles={role("owner",true),role("peer",true)};ark::AgentSession session;Gate held;int ownerRound=0,peerRound=0;std::string token;bool foreignRejected=false,ownerReleased=false;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++ownerRound){case 1:return call("owner_read","doc_info",R"({"detail":"revision"})");case 2:return call("short_window","team_reserve_write",R"({"expected_revision":0,"lease_ms":100})");case 3:token=writeWindowOutput(request).find("reservation_id")->asStr();std::this_thread::sleep_for(150ms);held.enter();return call("owner_expired_cleanup","team_release_write",std::string("{\"reservation_id\":\"")+token+"\"}");default:{const auto output=writeWindowOutput(request);ownerReleased=output.find("released")&&output.find("released")->b&&output.find("already_ended")->b;return done();}}};
    config.roles[1].transport=[&](auto&,const V& request,auto*){switch(++peerRound){case 1:held.await(1);return call("foreign_expired_cleanup","team_release_write",std::string("{\"reservation_id\":\"")+token+"\"}");default:{const auto output=writeWindowOutput(request);foreignRejected=output.find("isError")&&output.find("isError")->b;held.open();return done();}}};
    team.start(config,"Expired tokens keep owner custody",ark::AgentSession::tools(),[&](const auto& name,const V& args){return session.call(name,args);});complete(team);check(foreignRejected&&ownerReleased&&session.revision==0,"only the actual owner can clean up an expired issued receipt; document remains unchanged");
}
