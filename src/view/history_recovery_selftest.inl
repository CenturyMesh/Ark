if(featureHistoryUiTest||recoveryUiTest){
    using V=arkjson::Value;auto fail=[&](int code,const char* message){fprintf(stderr,"History/recovery UI: %s / %s\n",message,g_status.c_str());return code;};
    auto parse=[](const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid UI test fixture");return value;};
    auto invoke=[&](const char* tool,V arguments){arguments.set("expected_revision",V::makeNum((double)g_session.revision));return runTool(tool,arguments);};
    invoke("doc_new",V::makeObj());if(g_error)return fail(870,"new fixture failed");g_assistantDockOpen=false;layout();
    if(featureHistoryUiTest){
        invoke("doc_apply",parse(R"({"commands":[{"op":"feature.add","name":"Seed","type":"box","p":{"dx":20,"dy":15,"dz":10}},{"op":"feature.add","name":"Independent","type":"cylinder","p":{"r":5,"h":10}},{"op":"feature.add","name":"Derived","type":"transform","inputs":[1],"p":{"tx":30}}]})"));if(g_error)return fail(871,"history fixture rejected");
        auto action=[&](int index,int id,const char* capture){g_featureHistorySmokeIndex=index;g_featureHistorySmokeAction=id;g_dialogSmoke=3;g_dialogShot=shotPath+capture;uiAction(756);g_dialogSmoke=0;};
        const auto initial=documentToJson(g_doc);u64 before=g_session.revision;action(1,201,".history-move.png");if(g_error||g_session.revision!=before+1||g_doc.features[0].id!=2)return fail(872,"Move up did not publish valid order");
        before=g_session.revision;const auto ordered=documentToJson(g_doc);action(2,201,".history-dependency.png");if(!g_error||g_session.revision!=before||documentToJson(g_doc)!=ordered)return fail(873,"Invalid dependency reorder changed the drawing");g_error=false;
        action(1,204,".history-rollback.png");if(g_error||g_doc.historyTip!=1||g_doc.features.size()!=3)return fail(874,"Rollback did not retain source at the selected tip");
        if(std::find(g_bodyIds.begin(),g_bodyIds.end(),u64(1))==g_bodyIds.end())return fail(892,"Rollback hid its active terminal source because of an inactive consumer");
        before=g_session.revision;action(1,205,".history-resume.png");if(g_error||g_doc.historyTip||g_session.revision!=before+1)return fail(875,"Resume did not evaluate full retained history");
        before=g_session.revision;action(0,203,".history-suppress.png");if(g_error||!g_doc.find(2)->suppressed||g_session.revision!=before+1)return fail(876,"Suppress failed on independent feature");
        action(0,203,".history-unsuppress.png");if(g_error||g_doc.find(2)->suppressed)return fail(877,"Unsuppress failed");
        before=g_session.revision;const auto enabled=documentToJson(g_doc);action(1,203,".history-suppress-dependent.png");if(!g_error||g_session.revision!=before||documentToJson(g_doc)!=enabled)return fail(878,"Suppressing a required source changed the drawing");g_error=false;
        invoke("doc_undo",V::makeObj());if(g_error||!g_doc.find(2)->suppressed)return fail(879,"Undo did not restore suppression");
        g_featureHistorySmokeAction=0;g_featureHistorySmokeIndex=0;g_status="Feature history checks passed / valid reorder / dependency rejection / rollback / resume / suppression / Undo";
    }
    if(recoveryUiTest){
        const auto fixtureDirectory=std::filesystem::temp_directory_path()/("ark-recovery-ui-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(fixtureDirectory);
        g_recoveryDirectoryOverride=fixtureDirectory/"candidates";g_recoveryTestMode=true;const auto source=fixtureDirectory/"original.ark";
        auto pathValue=[](const std::filesystem::path& path){auto text=path.u8string();return V::makeStr(std::string(text.begin(),text.end()));};
        invoke("doc_new",V::makeObj());invoke("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"recover_length","expr":"10"}]})"));
        V arguments=V::makeObj();arguments.set("path",pathValue(source));runTool("doc_save",arguments);if(g_error)return fail(880,"recovery source save failed");checkDocumentRecovery();
        auto bytes=[&]{std::ifstream input(source,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};const auto original=bytes();
        invoke("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"recover_length","expr":"20"}]})"));const auto changed=documentToJson(g_doc);const auto before=g_session.revision;const auto recovery=currentRecoveryFile();
        g_busy=true;captureDocumentRecovery();g_busy=false;if(std::filesystem::exists(std::filesystem::u8path(recovery)))return fail(886,"Autosave ran during a document operation");captureDocumentRecovery();
        if(!std::filesystem::exists(std::filesystem::u8path(recovery))||bytes()!=original||g_session.revision!=before||documentToJson(g_doc)!=changed)return fail(881,"Autosave changed the source/current revision or created no candidate");
        invoke("doc_load",arguments);if(g_error||g_doc.getParam("recover_length")!=10)return fail(882,"Reload did not retain original source");g_recoveryPromptOverride=IDCANCEL;g_recoveryPromptShot=shotPath+".recovery-prompt.png";checkDocumentRecovery();
        if(!g_recoveryInhibited||!std::filesystem::exists(std::filesystem::u8path(recovery))||g_doc.getParam("recover_length")!=10||bytes()!=original)return fail(883,"Cancel failed to preserve both original and recovery");
        g_recoveryNeedsCheck=true;g_recoveryInhibited=false;g_recoveryPromptOverride=IDYES;checkDocumentRecovery();
        if(g_error||g_doc.getParam("recover_length")!=20||g_session.revision==g_savedRevision||g_path.find("recovered.ark")==std::string::npos||bytes()!=original)return fail(884,"Recover failed to create unsaved drawing with original preserved");
        V output=V::makeObj();output.set("path",pathValue(fixtureDirectory/"recovered-explicit.ark"));runTool("doc_save",output);
        if(g_error||bytes()!=original||!std::filesystem::exists(fixtureDirectory/"recovered-explicit.ark")||std::filesystem::exists(std::filesystem::u8path(recovery)))return fail(885,"Explicit Save As failed to acknowledge recovery while preserving source");
        invoke("doc_load",arguments);Document otherWriter=g_doc.authoringCopy();otherWriter.iterationHistory={};otherWriter.setParam("recover_length","30");
        const std::string writer="000-second-studio-fixture",foreign=recoveryFilePath(currentRecoveryDirectory(),pathValue(source).str,writer),unchosen=currentRecoveryFile();std::string recoveryError;
        Document unchosenDocument=g_doc.authoringCopy();unchosenDocument.iterationHistory={};unchosenDocument.setParam("recover_length","25");
        if(!saveRecovery(unchosenDocument,unchosen.c_str(),pathValue(source).str,41,g_recoveryActiveWriterToken,&recoveryError))return fail(891,"Unchosen owned candidate fixture failed");
        auto unchosenBytes=[&]{std::ifstream input(std::filesystem::u8path(unchosen),std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};const auto unchosenOriginal=unchosenBytes();
        if(!saveRecovery(otherWriter,foreign.c_str(),pathValue(source).str,42,writer,&recoveryError))return fail(887,"Independent writer fixture failed");
        auto foreignBytes=[&]{std::ifstream input(std::filesystem::u8path(foreign),std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};const auto foreignOriginal=foreignBytes();
        g_recoveryPromptOverride=IDYES;checkDocumentRecovery();if(g_error||g_doc.getParam("recover_length")!=30||currentRecoveryFile()==foreign)return fail(888,"Foreign writer candidate was not discovered separately");
        invoke("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"recover_length","expr":"32"}]})"));captureDocumentRecovery();
        if(foreignBytes()!=foreignOriginal||unchosenBytes()!=unchosenOriginal||!std::filesystem::exists(std::filesystem::u8path(currentRecoveryFile()))||currentRecoveryFile()==unchosen)return fail(889,"Autosave replaced another writer's or an unchosen owned candidate");
        output.set("path",pathValue(fixtureDirectory/"foreign-recovered-explicit.ark"));runTool("doc_save",output);if(g_error||foreignBytes()!=foreignOriginal||unchosenBytes()!=unchosenOriginal||bytes()!=original)return fail(890,"Save As removed another writer's or an unchosen candidate or changed the original");
        g_recoveryPromptOverride=0;g_recoveryTestMode=false;g_recoveryDirectoryOverride.clear();std::filesystem::remove_all(fixtureDirectory);g_status="Recovery UI checks passed / separate autosave / Cancel retains both / Recover unsaved / explicit Save As preserves source";
    }
}
