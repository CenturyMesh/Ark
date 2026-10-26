// Real GPU evidence for A.02/A.04/D.02: source is a synthetic native fixture.
static int viewportCaptureSelfTest059(const std::string& output){
    using V=arkjson::Value;V receipt=V::makeObj();int checks=0;
    receipt.set("schema",V::makeStr("ark.viewport-capture.059c.v1"));
    receipt.set("gpu",V::makeStr((const char*)glGetString(GL_RENDERER)));
    auto check=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    auto persist=[&]{receipt.set("checks",V::makeNum(checks));std::ofstream file(output+".capture.json");file<<receipt.dump()<<'\n';};
    auto parse=[&](const char* source){V v;check(arkjson::parse(source,v),"Invalid capture fixture JSON");return v;};
    auto windowState=[](){WINDOWPLACEMENT w{};w.length=sizeof(w);GetWindowPlacement(g_window,&w);return w;};
    auto unchangedWindow=[&](const WINDOWPLACEMENT& a,const WINDOWPLACEMENT& b){return a.showCmd==b.showCmd&&a.flags==b.flags&&
        EqualRect(&a.rcNormalPosition,&b.rcNormalPosition)&&a.ptMinPosition.x==b.ptMinPosition.x&&a.ptMinPosition.y==b.ptMinPosition.y&&a.ptMaxPosition.x==b.ptMaxPosition.x&&a.ptMaxPosition.y==b.ptMaxPosition.y;};
    try{
        V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);check(!g_error,"Capture fixture reset failed");
        args=parse(R"({"commands":[
          {"op":"feature.add","type":"native_solid","name":"Capture base","native":{"op":"box"},"p":{"width":100,"depth":80,"height":8},"appearance":{"base_color":[0.14,0.18,0.22]}},
          {"op":"feature.add","type":"native_solid","name":"Capture emitter","native":{"op":"box"},"p":{"width":4,"depth":4,"height":48},"appearance":{"base_color":[0.02,0.08,0.18],"emission_color":[0.08,0.4,1],"emission_intensity":5,"bloom":2,"glow":32}},
          {"op":"feature.add","type":"native_solid","name":"Capture occluder","native":{"op":"sphere"},"p":{"radius":14},"appearance":{"base_color":[0.55,0.12,0.05]}},
          {"op":"assembly.add","name":"Base","feature":1,"p":{"tx":-50,"ty":0,"tz":40,"rx":-90}},
          {"op":"assembly.add","name":"Emitter","feature":2,"p":{"tx":-18,"ty":8,"tz":-5,"rx":-90}},
          {"op":"assembly.add","name":"Occluder","feature":3,"p":{"tx":16,"ty":22,"tz":0}}
        ]})");args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);check(!g_error&&g_bodyIds.size()==3,"Native capture fixture failed");
        g_assistantDockOpen=false;g_chatExpanded=false;g_toolsOpen=false;g_showViewCube=false;g_showEdges=true;g_grid=false;g_lightBackground=false;g_whiteBackground=false;
        g_showSketches=false;g_showDatumPlanes=false;g_cameraOrtho=false;g_pathTracing=false;wireframe=false;g_picked.clear();selBody=hoverBody=-1;
        g_antialiasing=2;g_occlusionMode=3;g_contactShadowStrength=.4f;layout();frameScene();target=tTarget;dist=tDist;yaw=tYaw=.67;pitch=tPitch=.39;cameraRoll=0;
        const auto source=documentToJson(g_doc);const u64 revision=g_session.revision;
        const double savedYaw=yaw,savedPitch=pitch,savedDist=dist;const Vec3 savedTarget=target;
        const auto initialWindow=windowState();const int initialVisible=IsWindowVisible(g_window);
        const int savedWinW=winW,savedWinH=winH,savedX=viewX,savedY=viewY,savedW=viewW,savedH=viewH;const bool savedSwap=g_swap;
        auto image=captureGpuScene059();check(image.w>100&&image.h>100,"Hidden GPU image has invalid dimensions");
        check(writePNG((output+".gpu-hidden.png").c_str(),image),"Could not save hidden GPU image");
        check(unchangedWindow(initialWindow,windowState())&&IsWindowVisible(g_window)==initialVisible,"Hidden capture altered window state");
        check(winW==savedWinW&&winH==savedWinH&&viewX==savedX&&viewY==savedY&&viewW==savedW&&viewH==savedH&&g_swap==savedSwap,"Capture leaked viewport dimensions or swap state");
        auto originalAppearance=g_doc.features[1].appearance;
        g_doc.features[1].appearance.set("bloom",V::makeNum(0));const auto withoutBloom=captureGpuScene059();g_doc.features[1].appearance=originalAppearance;
        size_t bloomPixels=0;for(size_t i=0;i<image.px.size();i+=3){int delta=0;for(int c=0;c<3;++c)delta+=std::abs((int)image.px[i+c]-withoutBloom.px[i+c]);if(delta>4)++bloomPixels;}
        check(bloomPixels>100,"Offscreen bloom produced no visible halo");receipt.set("bloom_changed_pixels",V::makeNum(bloomPixels));
        check(writePNG((output+".gpu-no-bloom.png").c_str(),withoutBloom),"Could not save bloom reference");
        // Only this synthetic selftest minimizes a window. Production capture
        // never calls ShowWindow, changes focus or changes the user's layout.
        ShowWindow(g_window,SW_SHOWMINNOACTIVE);check(IsIconic(g_window)!=0,"Test window was not minimized");
        const auto minimizedWindow=windowState();const int minimizedVisible=IsWindowVisible(g_window);
        auto minimized=captureGpuScene059();check(minimized.px==image.px&&minimized.w==image.w&&minimized.h==image.h,"Minimized GPU pixels differ from the same hidden render");
        check(IsIconic(g_window)&&unchangedWindow(minimizedWindow,windowState())&&IsWindowVisible(g_window)==minimizedVisible,"Successful capture changed minimized window state");
        check(writePNG((output+".gpu-minimized.png").c_str(),minimized),"Could not save minimized GPU image");
        g_assistantDockOpen=true;g_chatExpanded=true;layout();check(viewH==1,"Expanded-chat fixture did not collapse viewport");
        const int expandedX=viewX,expandedY=viewY,expandedW=viewW,expandedH=viewH;
        auto expanded=captureGpuScene059();check(expanded.px==image.px,"Expanded chat changed offscreen GPU pixels");
        check(g_assistantDockOpen&&g_chatExpanded&&viewX==expandedX&&viewY==expandedY&&viewW==expandedW&&viewH==expandedH,"Capture changed expanded chat layout");
        check(IsIconic(g_window)&&unchangedWindow(minimizedWindow,windowState()),"Collapsed-viewport capture restored minimized window");
        check(writePNG((output+".gpu-chat-expanded.png").c_str(),expanded),"Could not save expanded-chat GPU image");
        args=V::makeObj();args.set("expected_revision",V::makeNum((double)revision));args.set("view",V::makeStr("viewport"));
        const auto result=captureViewportSnapshot(args);check(result.find("isError")&&!result.find("isError")->b,"Viewport tool failed while minimized and chat expanded");
        const auto* structured=result.find("structuredContent");const auto* evidence=structured?structured->find("visual_evidence"):nullptr;
        check(evidence&&evidence->find("render_target")&&evidence->find("render_target")->str=="offscreen_gpu_framebuffer","GPU provenance missing");
        check(evidence->find("window_minimized")->b&&evidence->find("viewport_collapsed")->b&&!evidence->find("ui_chrome_included")->b,"Viewport state evidence is incorrect");
        bool inlinePixels=false;for(const auto& content:result.find("content")->arr)if(content.find("type")&&content.find("type")->str=="image")inlinePixels=true;check(inlinePixels,"Viewport tool omitted inline image pixels");
        args.set("expected_revision",V::makeNum((double)revision+1));const auto rejected=captureViewportSnapshot(args);check(rejected.find("isError")->b,"Stale capture revision accepted");
        check(IsIconic(g_window)&&unchangedWindow(minimizedWindow,windowState())&&g_chatExpanded&&viewH==1,"Rejected capture altered window/layout state");
        args.set("expected_revision",V::makeNum((double)revision));args.set("path",V::makeStr(output+".gpu-hidden.png/cannot-write.png"));
        const auto writeRejected=captureViewportSnapshot(args);check(writeRejected.find("isError")->b,"Post-render file-write failure was not reported");
        check(IsIconic(g_window)&&unchangedWindow(minimizedWindow,windowState())&&viewW==expandedW&&viewH==expandedH&&g_chatExpanded,"Post-render failure changed window/layout state");
        glPixelStorei(GL_PACK_ROW_LENGTH,137);glPixelStorei(GL_PACK_SKIP_ROWS,3);glPixelStorei(GL_PACK_SKIP_PIXELS,2);glDepthMask(GL_FALSE);glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
        const auto guarded=captureGpuScene059();check(guarded.px==image.px,"External pixel-pack or scissor state corrupted GPU capture");
        GLint packed=0,readFbo=0,drawFbo=0;GLboolean mask=GL_TRUE;glGetIntegerv(GL_PACK_ROW_LENGTH,&packed);check(packed==137,"Capture did not restore pixel-pack row length");
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
        check(readFbo==0&&drawFbo==0&&!mask&&glIsEnabled(GL_SCISSOR_TEST),"Capture leaked framebuffer/depth/scissor state");
        glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glDepthMask(GL_TRUE);glDisable(GL_SCISSOR_TEST);
        check(source==documentToJson(g_doc)&&revision==g_session.revision,"GPU capture altered authored document/revision");
        check(yaw==savedYaw&&pitch==savedPitch&&dist==savedDist&&target==savedTarget,"GPU capture advanced camera easing");
        check(glGetError()==GL_NO_ERROR,"GPU capture leaked an OpenGL error");
        receipt.set("width",V::makeNum(image.w));receipt.set("height",V::makeNum(image.h));receipt.set("hidden_minimized_expanded_pixels_identical",V::makeBool(true));
        receipt.set("window_camera_layout_revision_preserved",V::makeBool(true));receipt.set("passed",V::makeBool(true));persist();ShowWindow(g_window,SW_HIDE);return 0;
    }catch(const std::exception& error){receipt.set("passed",V::makeBool(false));receipt.set("error",V::makeStr(error.what()));persist();std::fprintf(stderr,"Viewport capture selftest: %s\n",error.what());ShowWindow(g_window,SW_HIDE);return 959;}
}
