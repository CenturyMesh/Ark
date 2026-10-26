if(realtimeUiTest){
    using V=arkjson::Value;
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto json=[](const std::string& source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Invalid realtime fixture");return value;};
    try{
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);check(!g_error,"Realtime scratch document rejected");
        auto setup=json(R"({"commands":[{"op":"feature.add","type":"mesh_net","name":"Synthetic realtime network","p":{"width":"120","depth":"100","rows":"8","columns":"9","radius":"0.7","sag":"5","weave":"1"}}]})");setup.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",setup);check(!g_error,"Realtime network rejected");
        frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;g_grid=false;g_pathTracing=false;g_showAgentPresence=true;
        observePresence030("designer","mesh_inspect",json(R"({"id":1})"));
        drawFrame(hdc);glFinish();
        const auto revision=g_session.revision;const auto source=documentToJson(g_doc);
        auto preview=json(R"({"commands":[{"op":"param.set","name":"realtime_probe","expr":"1"}]})");preview.set("expected_revision",V::makeNum((double)revision));
        g_busyTestMode=true;g_busyTestDelayMs=0;
        V report=V::makeObj(),samples=V::makeArr();
        report.set("scope",V::makeStr("synthetic local UI dispatch; no provider, external MCP process or manufacturing claim"));
        report.set("width",V::makeNum(winW));report.set("height",V::makeNum(winH));
        for(const auto* tool:{"mesh_inspect","doc_preview"}){
            for(int i=0;i<24;++i){
                bool completed=false;V receipt;
                auto request=std::make_unique<UiToolRequest>();request->tool=tool;request->roleId="designer";request->args=std::string(tool)=="doc_preview"?preview:json(R"({"id":1})");
                request->complete=[&](V result){receipt=std::move(result);completed=true;};
                const auto started=RealtimeClock030::now();postTool(std::move(request));
                MSG message{};while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}drainUiTools();
                check(completed&&!receipt.find("isError")->b,"Realtime queued tool failed");
                drawFrame(hdc);glFinish();check(glGetError()==GL_NO_ERROR,"Realtime render failed");
                if(i>=4){const auto& timing=g_toolTimings030.back();V row=V::makeObj();row.set("tool",V::makeStr(tool));row.set("queue_ms",V::makeNum(timing.queueMs));row.set("host_ms",V::makeNum(timing.hostMs));row.set("capture_ms",V::makeNum(timing.progress.captureMs));row.set("worker_ms",V::makeNum(timing.progress.workerMs));row.set("resume_ms",V::makeNum(timing.progress.resumeMs));row.set("through_render_ms",V::makeNum(realtimeMs030(started)));samples.arr.push_back(std::move(row));}
            }
        }
        report.set("samples",std::move(samples));std::ofstream output(shotPath+".timings.json");output<<report.dump();output.close();check(output.good(),"Cannot write realtime measurements");
        check(g_session.revision==revision&&documentToJson(g_doc)==source,"Realtime reads/preview changed CAD");
        // Direct GPU capture must retain every RGB pixel, including orientation.
        auto capturePixels=[&]{
            std::vector<u8> expected((size_t)winW*winH*3),actual(expected.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,expected.data());
            captureBusySnapshot030();glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,actual.data());
            check(glGetError()==GL_NO_ERROR&&expected==actual,"GPU busy capture changed pixels or orientation");
            check(g_busySnapshotW==winW&&g_busySnapshotH==winH,"GPU busy capture retained stale dimensions");
        };
        capturePixels();const int savedW=winW,savedH=winH;winW=47;winH=31;capturePixels();winW=savedW;winH=savedH;drawFrame(hdc);capturePixels();
        // A slow queued tool yields to rendering; callbacks must not reenter CAD.
        unsigned completed=0;g_busyTestDelayMs=25;
        for(unsigned i=0;i<2;++i){auto request=std::make_unique<UiToolRequest>();request->tool="doc_preview";request->args=preview;request->complete=[&](V result){check(!result.find("isError")->b,"Queued preview rejected");++completed;};postTool(std::move(request));}
        MSG message{};while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}drainUiTools();check(completed==1&&g_toolQueue.size()==1,"UI dispatch starved the next viewport frame");drawFrame(hdc);drainUiTools();check(completed==2&&g_toolQueue.empty(),"UI dispatch dropped a queued tool");
        bool cancelled=false;auto stopped=std::make_unique<UiToolRequest>();stopped->tool="doc_apply";stopped->args=preview;stopped->cancel=std::make_shared<std::atomic_bool>(true);stopped->complete=[&](V result){cancelled=result.find("isError")->b;};postTool(std::move(stopped));while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}drainUiTools();check(cancelled&&g_session.revision==revision,"Cancelled queue entry edited CAD");
        auto stale=preview;stale.set("expected_revision",V::makeNum((double)revision-1));auto rejection=runTool("doc_apply",stale);check(rejection.find("isError")->b&&g_session.revision==revision,"Stale revision escaped realtime dispatch");
        check(documentToJson(g_doc)==source,"Realtime queue checks changed CAD");
        // Exercise native window commands inside the slow-operation message pump.
        // These must run while CAD mutations remain gated by wndProc's busy guard.
        WINDOWPLACEMENT savedPlacement{sizeof savedPlacement};GetWindowPlacement(hwnd,&savedPlacement);
        const bool wasVisible=IsWindowVisible(hwnd)!=0;int windowStage=0;
        struct WindowProbeCleanup {HWND window;WINDOWPLACEMENT placement;bool visible;~WindowProbeCleanup(){g_busyPumpTest={};SetWindowPlacement(window,&placement);if(!visible)ShowWindow(window,SW_HIDE);}} windowCleanup{hwnd,savedPlacement,wasVisible};
        g_busyTestDelayMs=400;
        g_busyPumpTest=[&]{
            switch(windowStage++){
                case 0:PostMessage(hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0);break;
                case 1:check(IsIconic(hwnd)!=0,"Busy pump dropped minimize command");PostMessage(hwnd,WM_SYSCOMMAND,SC_RESTORE,0);break;
                case 2:check(!IsIconic(hwnd),"Busy pump dropped restore command");PostMessage(hwnd,WM_SYSCOMMAND,SC_MAXIMIZE,0);break;
                case 3:check(IsZoomed(hwnd)!=0,"Busy pump dropped maximize command");PostMessage(hwnd,WM_SYSCOMMAND,SC_RESTORE,0);break;
                case 4:check(!IsIconic(hwnd)&&!IsZoomed(hwnd),"Busy pump did not restore normal window");break;
            }
        };
        auto windowResult=runTool("doc_preview",preview);g_busyPumpTest={};
        check(!windowResult.find("isError")->b&&windowStage>=5&&g_session.revision==revision&&documentToJson(g_doc)==source,"Window commands changed CAD or did not complete during the operation");
        SetWindowPlacement(hwnd,&savedPlacement);if(!wasVisible)ShowWindow(hwnd,SW_HIDE);drawFrame(hdc);
        // Capture the actual busy renderer with a retained spatial role marker.
        g_busyTestDelayMs=60;g_busyTestShot=shotPath+".busy.png";auto result=runTool("doc_preview",preview);check(!result.find("isError")->b,"Realtime busy preview failed");
        g_busyTestMode=false;g_busyTestDelayMs=0;g_busyTestShot.clear();
        drawFrame(hdc);glFinish();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> pixels(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());for(int y=0;y<winH;++y)std::copy_n(pixels.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);check(writePNG(shotPath.c_str(),image),"Realtime capture failed");
        return 0;
    }catch(const std::exception& error){std::ofstream failure(shotPath+".failure.txt");failure<<error.what();return 231;}
}
