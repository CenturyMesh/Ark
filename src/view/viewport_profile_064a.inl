// Explicit local diagnostic. Never saves or publishes the loaded document.
static int profileViewport064(HDC dc,const std::string& configPath,const std::string& output){
    using V=arkjson::Value;using Clock=std::chrono::steady_clock;
    V report=V::makeObj();
    try{
        std::ifstream file(configPath);std::string json((std::istreambuf_iterator<char>(file)),{});V config;
        if(!arkjson::parse(json,config))throw std::runtime_error("Invalid viewport profile configuration");
        auto number=[&](const char* key,double fallback){auto* v=config.find(key);return v?v->asNum():fallback;};
        yaw=tYaw=number("yaw",yaw);pitch=tPitch=number("pitch",pitch);cameraRoll=number("roll",0);dist=tDist=number("distance",dist);
        if(auto* p=config.find("target");p&&p->arr.size()==3)target=tTarget={p->arr[0].asNum(),p->arr[1].asNum(),p->arr[2].asNum()};
        g_cameraOrtho=number("ortho",0)!=0;g_assistantDockOpen=false;g_toolsOpen=false;g_showSketches=false;g_showDatumPlanes=false;g_grid=false;g_lightBackground=true;g_pathTracing=false;g_occlusionMode=3;g_antialiasing=2;g_contactShadowStrength=.4f;g_swap=false;layout();
        g_occlusionMode=(int)number("ao",3);g_contactShadowStrength=(float)number("shadow",.4);
        const auto source=documentToJson(g_doc);const auto revision=g_session.revision;const auto worlds=g_instanceWorld;
        report.set("gpu",V::makeStr((const char*)glGetString(GL_RENDERER)));report.set("width",V::makeNum(winW));report.set("height",V::makeNum(winH));
        auto stats=[](std::vector<double> values){std::sort(values.begin(),values.end());V r=V::makeObj();r.set("median_ms",V::makeNum(values[values.size()/2]));r.set("p95_ms",V::makeNum(values[(values.size()*95)/100]));return r;};
        V rows=V::makeArr();
        for(int mode=0;mode<3;++mode){
            g_componentMove={};g_instanceWorld=worlds;rebuildDisplayBounds();
            if(mode==2){const u64 driver=(u64)number("driver",0);auto it=std::find(g_instanceIds.begin(),g_instanceIds.end(),driver);if(it==g_instanceIds.end())continue;prepareComponentMove((int)(it-g_instanceIds.begin()),false);g_componentMove.active=true;g_componentMove.dragging=true;g_componentMove.x=0;g_componentMove.y=0;g_componentMove.radial={};}
            std::vector<double> input,frames,prep,scene,ui;
            for(int frame=0;frame<44;++frame){auto a=Clock::now();
                if(mode==1)yaw=tYaw=number("yaw",0)+frame*.006;
                if(mode==2)SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(frame*3,0));
                auto b=Clock::now();drawFrame(dc);glFinish();auto c=Clock::now();
                if(g_error)throw std::runtime_error(g_status);
                if(frame>=4){input.push_back(std::chrono::duration<double,std::milli>(b-a).count());frames.push_back(std::chrono::duration<double,std::milli>(c-b).count());prep.push_back(g_frameProfile064[0]);scene.push_back(g_frameProfile064[1]);ui.push_back(g_frameProfile064[2]);}
            }
            V row=V::makeObj();row.set("mode",V::makeStr(mode==0?"stationary":mode==1?"orbit":"joint_drag_45deg"));row.set("input",stats(input));row.set("completed_frame",stats(frames));row.set("cpu_prepare",stats(prep));row.set("cpu_scene",stats(scene));row.set("cpu_ui",stats(ui));rows.arr.push_back(row);
            yaw=tYaw=number("yaw",0);
        }
        g_componentMove={};g_instanceWorld=worlds;rebuildDisplayBounds();drawFrame(dc);glFinish();
        Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> raw(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,raw.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);
        if(!writePNG(output.c_str(),image))throw std::runtime_error("Profile capture failed");
        if(source!=documentToJson(g_doc)||revision!=g_session.revision)throw std::runtime_error("Profile modified authored source");
        report.set("rows",rows);report.set("source_unchanged",V::makeBool(true));report.set("passed",V::makeBool(glGetError()==GL_NO_ERROR));report.set("scope",V::makeStr("40 warmed frames per mode, full UI, actual WM_MOUSEMOVE for joint drag, completed CPU+GPU using diagnostic glFinish; excludes display/compositor latency."));
    }catch(const std::exception& e){report.set("passed",V::makeBool(false));report.set("error",V::makeStr(e.what()));}
    std::ofstream file(output+".profile.json");file<<report.dump()<<'\n';return report.find("passed")->b?0:964;
}
