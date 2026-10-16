if(teamBlockersUiTest){
    using V=arkjson::Value;auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto capture=[&](HWND window,const std::string& path){
        RECT bounds{};GetWindowRect(window,&bounds);Image image;image.w=bounds.right-bounds.left;image.h=bounds.bottom-bounds.top;
        HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image.w;info.bmiHeader.biHeight=-image.h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);check(bitmap!=nullptr,"Cannot allocate blocker capture");auto previous=SelectObject(dc,bitmap);BOOL drawn=PrintWindow(window,dc,0);GdiFlush();
        image.px.resize((size_t)image.w*image.h*3);const auto* source=(const u8*)pixels;for(size_t i=0;i<(size_t)image.w*image.h;++i){image.px[i*3]=source[i*4+2];image.px[i*3+1]=source[i*4+1];image.px[i*3+2]=source[i*4];}SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);check(drawn&&writePNG(path.c_str(),image),"Cannot capture blocker controls");
    };
    try{
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);check(!g_error,"Scratch document rejected");
        const auto revision=g_session.revision;
        AgentTeamConfig config;config.focusedWorkflow=true;AgentTeamRole lead;lead.id="engineer";lead.label="Engineer";lead.canWrite=true;lead.discipline=AgentTeamDiscipline::Orchestrator;lead.assistant.model="offline-blocker-test";int rounds=0;
        lead.transport=[&](auto&,auto&,auto*){
            const int round=++rounds;V response=V::makeObj(),output=V::makeArr(),item=V::makeObj();response.set("status",V::makeStr("completed"));
            if(round<=3){V arguments=V::makeObj();item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr("blocker-"+std::to_string(round)));item.set("name",V::makeStr(round==1?"bom_review":round==2?"team_report_task":"team_finish_goal"));if(round==1){arguments.set("stage",V::makeStr("detailed"));arguments.set("expected_revision",V::makeNum((double)revision));}else {arguments.set("state",V::makeStr("needs_input"));arguments.set("summary",V::makeStr("Provide the missing material source before detailed completion. The authored concept is retained."));}item.set("arguments",V::makeStr(arguments.dump()));}
            else {item.set("type",V::makeStr("message"));item.set("role",V::makeStr("assistant"));V content=V::makeArr(),text=V::makeObj();text.set("type",V::makeStr("output_text"));text.set("text",V::makeStr("Waiting for the requested input."));content.arr.push_back(text);item.set("content",content);}output.arr.push_back(item);response.set("output",output);return response;
        };config.roles.push_back(lead);
        g_teamMode=true;g_teamRoleCount=1;g_teamStarted=true;g_teamEpoch=g_documentEpoch;g_teamGoal="Inspect a synthetic concept and report missing inputs";g_teamEventSequence=0;g_assistantDockOpen=true;g_teamReceipts={};g_teamReceiptIds[0]=newUsageReceiptId();
        g_agentTeam.start(config,g_teamGoal,AgentSession::tools(),[](auto&,auto&){V result=V::makeObj(),detail=V::makeObj();detail.set("error_code",V::makeStr("REVIEW_BLOCKED"));detail.set("error",V::makeStr("A required material source remains unresolved."));result.set("isError",V::makeBool(true));result.set("structuredContent",detail);return result;});check(g_agentTeam.waitIdleFor(std::chrono::seconds(5)),"Blocker fixture did not finish");pollTeam(true);layout();
        check(g_teamSnapshot.goalDeclared&&g_teamSnapshot.goalState==AgentGoalState::NeedsInput&&g_teamSnapshot.unresolvedErrors==1,"Reported input request was mislabeled as interruption");
        g_swap=false;drawFrame(hdc);glFinish();check(std::any_of(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==989;}),"Review blockers control missing");check(std::none_of(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==490;}),"Idle input request incorrectly offers transport resume");
        // Provider-response bookkeeping is settled by pollTeam above. Only the
        // following diagnostic actions must leave that settled document intact.
        const auto source=documentToJson(g_doc);
        uiAction(989);check(g_teamDiagnosticsWindow030&&IsWindowEnabled(g_window)&&!g_dialog,"Blocker inspector blocked the owning Studio queue");
        check(GetWindowTextLengthW(g_teamDiagnosticsWindow030)>0,"Blocker window lost its title");
        auto* state=(TeamDiagnosticsWindow030*)GetWindowLongPtr(g_teamDiagnosticsWindow030,GWLP_USERDATA);check(state&&(GetWindowLongPtr(state->text,GWL_STYLE)&ES_READONLY)&&(GetWindowLongPtr(state->text,GWL_STYLE)&WS_VSCROLL),"Blocker report is not read-only and scrollable");
        const int length=GetWindowTextLengthW(state->text);std::wstring report((size_t)length+1,L'\0');GetWindowTextW(state->text,report.data(),length+1);check(report.find(L"Needs input / explicit lead report")!=std::wstring::npos&&report.find(L"bom_review [REVIEW_BLOCKED]")!=std::wstring::npos,"Inspector lost reported outcome or actual tool reason");
        SendMessageW(state->text,WM_CHAR,L'x',0);check(GetWindowTextLengthW(state->text)==length,"Blocker report accepted typing");SendMessage(g_teamDiagnosticsWindow030,WM_COMMAND,1,0);check(rounds==4&&g_session.revision==revision&&documentToJson(g_doc)==source,"Inspecting blockers triggered work or edited CAD");
        capture(g_teamDiagnosticsWindow030,shotPath+".details.png");RECT before{};GetWindowRect(g_teamDiagnosticsWindow030,&before);SetWindowPos(g_teamDiagnosticsWindow030,nullptr,before.left,before.top,640,480,SWP_NOZORDER|SWP_NOACTIVATE);UpdateWindow(g_teamDiagnosticsWindow030);capture(g_teamDiagnosticsWindow030,shotPath+".details-compact.png");
        // Closing this modeless inspector while the model worker owns CAD must
        // close only the inspector, and must not enqueue main-window shutdown.
        PostMessage(g_teamDiagnosticsWindow030,WM_CLOSE,0,0);g_busyTestMode=true;g_busyTestDelayMs=60;callSessionWithProgress("blocker_inspector_probe",V::makeObj(),[]{return V::makeObj();});g_busyTestMode=false;g_busyTestDelayMs=0;
        check(!g_teamDiagnosticsWindow030&&!g_busyClosePending&&IsWindow(g_window),"Busy pump confused inspector close with Studio shutdown");check(rounds==4&&g_session.revision==revision&&documentToJson(g_doc)==source,"Busy inspector pump modified the design or resumed agents");
        drawFrame(hdc);glFinish();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> pixels(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());for(int y=0;y<winH;++y)std::copy_n(pixels.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);check(writePNG(shotPath.c_str(),image),"Cannot capture corrected team state");g_agentTeam.cancel();g_agentTeam.waitIdle();return 0;
    }catch(const std::exception& error){if(g_teamDiagnosticsWindow030)DestroyWindow(g_teamDiagnosticsWindow030);g_agentTeam.cancel();g_agentTeam.waitIdle();std::ofstream failure(shotPath+".failure.txt");failure<<error.what();return 232;}
}
