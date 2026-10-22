// Offline delegation exercises the real private workspace and lead integration.
// Reference commands are confined to this synthetic transport, never live input.
AssistantTransport scriptedPart(const AgentBenchmarkCase& test,AgentSession& source,bool lead){
    auto stage=std::make_shared<unsigned>(0);
    return [&,stage,lead](const AssistantConfig&,const V& request,const std::atomic_bool*){
        const auto n=++*stage;
        auto lastResult=[&](){const auto& input=need(request,"input");for(auto it=input.arr.rbegin();it!=input.arr.rend();++it)if(const auto* output=it->find("output");output&&output->t==V::Str){V result;if(!arkjson::parse(output->str,result))throw std::runtime_error("Invalid private benchmark receipt");if(error(result))throw std::runtime_error(result.dump());return result;}throw std::runtime_error("Missing private benchmark receipt");};
        auto done=[](){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Synthetic delegation checkpoint."}]}]})");};
        V args=V::makeObj();
        if(lead){
            if(n==1){args.set("action",V::makeStr("assign"));args.set("to",V::makeStr("designer"));args.set("node",test.partContainer);args.set("expected_revision",V::makeNum(source.revision));args.set("message",V::makeStr(test.brief));return offlineCall(n,"team_part",args);}
            if(n==2){lastResult();return done();}
            if(n==3){args.set("action",V::makeStr("review"));args.set("task",V::makeStr("part-1"));args.set("expected_revision",V::makeNum(source.revision));return offlineCall(n,"team_part",args);}
            if(n==4){const auto review=lastResult();args.set("action",V::makeStr("integrate"));args.set("task",V::makeStr("part-1"));args.set("expected_revision",need(review,"source_revision"));args.set("reviewed_child_sha256",need(review,"reviewed_child_sha256"));return offlineCall(n,"team_part",args);}
            if(n==5){lastResult();return offlineCall(n,"team_finish_goal",json(R"({"state":"completed","summary":"Private synthetic detail reviewed and integrated. External acceptance and persistence oracle still govern success."})"));}
            return done();
        }
        if(n==1)return offlineCall(n,"team_part",json(R"({"action":"query","tool":"doc_info","args":{"detail":"design"}})"));
        if(n==2){lastResult();args.set("action",V::makeStr("apply"));args.set("expected_revision",V::makeNum(0));args.set("commands",test.reference);return offlineCall(n,"team_part",args);}
        if(n==3){const auto edit=lastResult();args.set("action",V::makeStr("submit"));args.set("expected_revision",need(need(edit,"part_workspace"),"local_revision"));args.set("message",V::makeStr("Private reference detail completed; source has not been changed."));return offlineCall(n,"team_part",args);}
        return done();
    };
}
