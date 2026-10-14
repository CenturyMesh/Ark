void testLocalModelCapabilities(){
    auto config=configured("ollama");int calls=0;
    auto metadata=assistantLocalModelInfo(config,[&](const auto& route,const V& request,auto*){++calls;check(route.endpoint=="http://127.0.0.1:11434/api/show"&&route.apiKeyEnv.empty()&&request.find("model")->str==config.model,"local metadata targets selected model without credentials/inference");return json(R"({"capabilities":["completion","tools","vision","thinking"],"details":{"parameter_size":"4B","quantization_level":"Q4"},"template":"not retained"})");});
    check(calls==1&&metadata.find("tools")->b&&metadata.find("vision")->b&&!metadata.find("template"),"capabilities are explicit and bounded, template not copied");
    auto textOnly=assistantLocalModelInfo(config,[](auto&,auto&,auto*){return json(R"({"capabilities":["completion","tools"]})");});check(!textOnly.find("vision")->b&&!textOnly.find("thinking")->b,"missing capabilities never guessed from model name");
    rejects([&]{assistantLocalModelInfo(config,[](auto&,auto&,auto*){return json(R"({"capabilities":"tools"})");});},"malformed capabilities reject");
    rejects([&]{assistantLocalModelInfo(config,[](auto&,auto&,auto*){return json(R"({"capabilities":["tools"],"remote_model":"cloud-model"})");});},"cloud alias cannot masquerade as local inference");
    auto remote=config;remote.endpoint="https://remote.invalid/v1/chat/completions";rejects([&]{assistantLocalModelInfo(remote,[](auto&,auto&,auto*){throw std::runtime_error("must not call transport");return V{};});},"local discovery refuses remote endpoint");
    auto keyed=config;keyed.apiKeyEnv="SECRET";rejects([&]{assistantLocalModelInfo(keyed);},"local capability inspection refuses credential-bearing connector");
    int remoteCalls=0;rejects([&]{assistantLocalModels(remote,[&](auto&,auto&,auto*){++remoteCalls;return V{};});},"local catalog rejects remote before transport");check(remoteCalls==0,"catalog must not contact a remote provider from Local models");
}
void liveLocalModelSmoke(const std::string& model,bool basic=false){
    // Explicit opt-in network test on a synthetic session, never the open user document.
    auto config=assistantProviderPreset("ollama");config.model=model;config.reasoningEffort="none";config.timeoutSeconds=300;config.maxRounds=8;
    const auto metadata=assistantLocalModelInfo(config);check(metadata.find("tools")->b,"chosen live model must declare tool capability");config.imageInputSupported=metadata.find("vision")->b;
    auto catalog=AgentSession::tools();if(basic)catalog.arr.erase(std::remove_if(catalog.arr.begin(),catalog.arr.end(),[](const V& tool){const auto name=tool.find("name")->str;return name!="doc_info"&&name!="doc_apply"&&name!="agent_onboard";}),catalog.arr.end());
    AgentSession session;session.suppressIterationHistory();session.suppressFailureCapture();unsigned read=0,write=0,failures=0;
    const auto start=std::chrono::steady_clock::now();
    const auto result=runAssistant(config,V::makeStr("This is an isolated CAD smoke test. Inspect doc_info to read expected_revision. Then use doc_apply to create exactly one native box called Local box, width 40 depth 30 height 20 mm. The command is {\"op\":\"feature.add\",\"type\":\"native_solid\",\"name\":\"Local box\",\"native\":{\"op\":\"box\"},\"p\":{\"width\":40,\"depth\":30,\"height\":20}}. Inspect doc_info once more and report completion. No drawings, files, BOM or other features. Use the actual tools, not a JSON answer."),catalog,[&](const std::string& name,const V& args){
        check(name=="doc_info"||name=="doc_apply"||name=="agent_onboard","live smoke allows only scratch inspection, onboarding and checked CAD writes");
        if(name=="doc_info")++read;if(name=="doc_apply"){check(read>0,"live write must follow actual document inspection");++write;check(write<=2,"live smoke stops repeated writes");}
        auto receipt=session.call(name,args);const auto* error=receipt.find("isError");if(error&&error->b)++failures;std::cerr<<"Scratch tool "<<name<<" / "<<(error&&error->b?receipt.dump():"ok")<<'\n';return receipt;
    });
    check(read>=2&&write==1&&failures==0&&session.document.features.size()==1,"live model must create and re-inspect exactly one actual feature");
    const auto& feature=session.document.features.front();check(feature.name=="Local box"&&feature.type==FeatureType::NativeSolid,"live CAD result must be a native parametric box");
    check(feature.p.at("width")=="40"&&feature.p.at("depth")=="30"&&feature.p.at("height")=="20","live result must retain the requested editable dimensions");
    V report=V::makeObj();report.set("model",V::makeStr(model));report.set("rounds",V::makeNum(result.rounds));report.set("tool_reads",V::makeNum(read));report.set("tool_writes",V::makeNum(write));report.set("tool_failures",V::makeNum(failures));report.set("revision",V::makeNum((double)session.revision));report.set("elapsed_ms",V::makeNum((double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()));report.set("usage",V::makeStr(assistantUsageLabel(result.usage)));report.set("request_bytes",V::makeNum((double)result.performance.maxRequestBytes));report.set("status",V::makeStr("native scratch box created and inspected through real tool calls"));std::cout<<report.dump()<<'\n';
}
