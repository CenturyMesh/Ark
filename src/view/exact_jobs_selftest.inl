if(asyncExactUiTest){
    // 046B: the same freeform exact read through the actual UI queue, first with
    // the earlier dispatch (busy renderer, then a UI-thread call) and then as a
    // background job. A helper thread injects input and heartbeats identically.
    using V=arkjson::Value;using Clock=std::chrono::steady_clock;
    auto check=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    auto json=[](const std::string& source){V value;if(!arkjson::parse(source,value))throw std::runtime_error("Invalid async exact fixture");return value;};
    auto ms=[](Clock::time_point from,Clock::time_point to){return std::chrono::duration<double,std::milli>(to-from).count();};
    auto payload=[](const V& result)->V{const auto* data=result.find("structuredContent");return data?*data:V::makeObj();};
    auto shot=[&](const std::string& path){glFinish();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> pixels(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());for(int y=0;y<winH;++y)std::copy_n(pixels.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);return writePNG(path.c_str(),image);};
    try{
        _putenv_s("ARK_NATIVE_RESULT_CACHE","0"); // every repetition reaches the exact worker
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);check(!g_error,"Async exact scratch document rejected");
        V commands=V::makeArr();
        for(int body=0;body<2;++body){V sections=V::makeArr();
            for(int s=0;s<5;++s){const std::string tag=std::string(body?"B":"A")+std::to_string(s);commands.arr.push_back(json(R"({"op":"plane.set","name":"P)"+tag+R"(","p":{"oz":")"+std::to_string(18*s)+R"("}})"));
                V dof=V::makeArr();const double scale=20+8*std::sin(s*1.1),phase=s*.7+body;
                for(int i=0;i<32;++i){const double t=2*K_PI*i/32,r=scale*(1+.18*std::sin(5*t+phase)+.07*std::cos(9*t));dof.arr.push_back(V::makeNum(std::round(r*std::cos(t)*1.6*1e4)/1e4));dof.arr.push_back(V::makeNum(std::round(r*std::sin(t)*1e4)/1e4));}
                V entity=json(R"({"name":"skin","kind":"spline","closed":true})");entity.set("dof",dof);V entities=V::makeArr();entities.arr.push_back(entity);V sketch=V::makeObj();sketch.set("plane",V::makeStr("P"+tag));sketch.set("entities",entities);
                V command=json(R"({"op":"sketch.set"})");command.set("name",V::makeStr("S"+tag));command.set("sketch",sketch);commands.arr.push_back(command);sections.arr.push_back(V::makeStr("S"+tag));}
            V loft=json(R"({"op":"feature.add","type":"loft","p":{"solid":"1","ruled":"0","smooth":"1"}})");loft.set("name",V::makeStr(body?"Hull B":"Hull A"));V native=V::makeObj();native.set("sections",sections);loft.set("native",native);commands.arr.push_back(loft);}
        commands.arr.push_back(json(R"({"op":"assembly.add","name":"Hull A","feature":1})"));commands.arr.push_back(json(R"({"op":"assembly.add","name":"Hull B","feature":2,"p":{"tx":"72","ty":"6","tz":"4","rz":"7"}})"));
        V setup=V::makeObj();setup.set("commands",commands);setup.set("expected_revision",V::makeNum((double)g_session.revision));const auto fixtureStarted=Clock::now();runTool("doc_apply",setup);check(!g_error,"Freeform hull fixture rejected: "+g_status);const double fixtureMs=ms(fixtureStarted,Clock::now());
        frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;g_grid=false;g_pathTracing=false;drawFrame(hdc);glFinish();
        const auto observed=g_session.revision;const auto source=documentToJson(g_doc);
        const std::string clearance=R"({"detail":"result","clearance":{"a":1,"b":2}})";
        const V batch=json(R"({"queries":[{"tool":"assembly_info","args":)"+clearance+R"(},{"tool":"assembly_info","args":)"+clearance+R"(}]})");
        int pickX=-1,pickY=-1;for(int y=viewY+viewH/3;y<viewY+2*viewH/3&&pickX<0;y+=6)for(int x=viewX+viewW/3;x<viewX+2*viewW/3;x+=6)if(pickBody(x,y)>=0){pickX=x;pickY=y;break;}
        check(pickX>=0,"No selectable hull pixel in the viewport");const int centerX=viewX+viewW/2,centerY=viewY+viewH/2;
        auto pump=[&]{MSG message{};while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}drainUiTools();drainExactJobs046();};
        struct Probe {double heartbeatMax=0,heartbeatP95=0;size_t heartbeats=0;double secondClientMs=-1;bool zoom=false,orbit=false,select=false;double zoomMs=-1,orbitMs=-1,selectMs=-1;};
        // One measured request: helper thread posts input/heartbeats/second client on a fixed schedule.
        // path: "queue" posts a UiToolRequest as the live bridge does; "studio" is Studio
        // code calling runTool (earlier dispatch) or submitExactJob046B (background).
        auto measure=[&](const std::string& mode,const std::string& path,const std::string& tool,const V& args){
            selBody=-1;g_picked.clear();frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;drawFrame(hdc);glFinish();
            const double startDist=tDist,startYaw=tYaw,startPitch=tPitch;V reply;bool completed=false;Clock::time_point finished;
            std::atomic_bool running{true},helperDone{false};std::vector<double> beats;std::mutex beatMutex;std::atomic<double> secondDone{-1};Clock::time_point zoomAt,orbitAt,selectAt;
            auto complete=[&](V result){reply=std::move(result);completed=true;finished=Clock::now();};bool issued=false;
            auto issue=[&]{issued=true;if(path=="queue"){auto request=std::make_unique<UiToolRequest>();request->tool=tool;request->args=args;request->complete=complete;postTool(std::move(request));}
                else if(g_exactJobsMode046==1)check(submitExactJob046B(tool,args,[&complete](const V& result,u64){complete(result);},"Self-test exact check")!=0,"Studio background API rejected an exact read");
                else complete(runTool(tool,args));};
            const auto started=Clock::now();
            std::thread helper([&]{
                auto at=[&](int delay){std::this_thread::sleep_until(started+std::chrono::milliseconds(delay));};
                POINT screen{pickX,pickY};ClientToScreen(g_window,&screen);
                std::thread beat([&]{while(running){const auto sent=Clock::now();DWORD_PTR ignored=0;SendMessageTimeoutW(g_window,WM_NULL,0,0,SMTO_NORMAL,20000,&ignored);{std::lock_guard lock(beatMutex);beats.push_back(ms(sent,Clock::now()));}std::this_thread::sleep_for(std::chrono::milliseconds(20));}});
                at(300);selectAt=Clock::now();PostMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(pickX,pickY));PostMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(pickX,pickY));
                at(500);{auto second=std::make_unique<UiToolRequest>();second->tool="doc_info";second->args=json(R"({"detail":"revision"})");const auto posted=Clock::now();second->complete=[&secondDone,posted,ms](V){secondDone=ms(posted,Clock::now());};postTool(std::move(second));}
                at(700);zoomAt=Clock::now();POINT wheel{centerX,centerY};ClientToScreen(g_window,&wheel);PostMessage(g_window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(wheel.x,wheel.y));
                at(900);orbitAt=Clock::now();PostMessage(g_window,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(centerX,centerY));for(int i=1;i<=4;++i)PostMessage(g_window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(centerX+12*i,centerY));PostMessage(g_window,WM_MBUTTONUP,0,MAKELPARAM(centerX+48,centerY));
                while(running)std::this_thread::sleep_for(std::chrono::milliseconds(5));beat.join();helperDone=true;});
            Probe probe;unsigned frames=0;double maxGap=0;auto last=Clock::now();const unsigned busyBefore=g_busyFrameCount;Clock::time_point lastInput{};unsigned appliedInputs=0;
            auto applied=[&](bool& seen,double& latency,Clock::time_point posted,Clock::time_point now){seen=true;latency=ms(posted,now);lastInput=std::max(lastInput,now);++appliedInputs;};
            while((!completed||secondDone.load()<0||appliedInputs<3)&&ms(started,Clock::now())<120000){const bool wasRunning=!completed;if(!issued)issue();pump();const auto now=Clock::now();
                if(!probe.select&&selBody>=0)applied(probe.select,probe.selectMs,selectAt,now);if(!probe.zoom&&tDist!=startDist)applied(probe.zoom,probe.zoomMs,zoomAt,now);if(!probe.orbit&&(tYaw!=startYaw||tPitch!=startPitch))applied(probe.orbit,probe.orbitMs,orbitAt,now);
                // Live frames and the largest interval between them while the read runs.
                drawFrame(hdc);glFinish();const auto drawn=Clock::now();if(wasRunning){++frames;maxGap=std::max(maxGap,ms(last,drawn));}last=drawn;
                if(completed&&ms(finished,drawn)>1500)break;}
            running=false;while(!helperDone){MSG message{};while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}Sleep(1);}helper.join();
            check(completed&&!reply.find("isError")->b,mode+" exact read failed: "+reply.dump().substr(0,600));
            const bool inputDuringJob=appliedInputs==3&&lastInput<finished;
            {std::lock_guard lock(beatMutex);probe.heartbeats=beats.size();std::sort(beats.begin(),beats.end());if(!beats.empty()){probe.heartbeatMax=beats.back();probe.heartbeatP95=beats[(size_t)std::min<double>(beats.size()-1,std::ceil(.95*beats.size())-1)];}}
            probe.secondClientMs=secondDone.load();
            V row=V::makeObj();row.set("mode",V::makeStr(mode));row.set("path",V::makeStr(path));row.set("tool",V::makeStr(tool));row.set("wall_ms",V::makeNum(ms(started,finished)));
            const V replied=payload(reply);if(const auto* job=replied.find("exact_job")){row.set("run_ms",*job->find("run_ms"));row.set("queue_ms",*job->find("queue_ms"));row.set("snapshot_copy_ms",V::makeNum(g_exactSnapshotMs046));}
            row.set("max_live_frame_gap_ms",V::makeNum(maxGap));row.set("live_frames",V::makeNum(frames));row.set("busy_frames",V::makeNum(g_busyFrameCount-busyBefore));
            row.set("heartbeat_max_ms",V::makeNum(probe.heartbeatMax));row.set("heartbeat_p95_ms",V::makeNum(probe.heartbeatP95));row.set("heartbeats",V::makeNum((double)probe.heartbeats));
            row.set("second_client_doc_info_ms",V::makeNum(probe.secondClientMs));
            auto input=[&](const char* key,bool seen,double latency){V entry=V::makeObj();entry.set("applied",V::makeBool(seen));entry.set("latency_ms",seen?V::makeNum(latency):V{});row.set(key,entry);};
            input("select_input",probe.select,probe.selectMs);input("zoom_input",probe.zoom,probe.zoomMs);input("orbit_input",probe.orbit,probe.orbitMs);row.set("input_applied_during_job",V::makeBool(inputDuringJob));
            check(g_session.revision==observed&&documentToJson(g_doc)==source,mode+" exact read changed the drawing");
            return row;
        };
        V report=V::makeObj(),samples=V::makeArr();report.set("scope",V::makeStr("synthetic local Studio UI queue; one machine; no provider, network or manufacturing claim"));
        report.set("fixture",V::makeStr("two smooth lofted solids through five 32-anchor closed splines, interfering; query_batch of two exact assembly_info clearance rows; native result cache disabled"));
        report.set("fixture_apply_ms",V::makeNum(fixtureMs));report.set("width",V::makeNum(winW));report.set("height",V::makeNum(winH));
        // Warm both paths once: the busy executor thread and both job threads own separate resident workers.
        g_busyTestMode=true;g_exactJobsMode046=0;measure("warmup_busy","queue","assembly_info",json(clearance));
        g_exactJobsTestMode046=true;g_exactJobsMode046=1;{unsigned warmed=0;for(int i=0;i<2;++i)submitExactJob046B("assembly_info",json(clearance),[&](const V&,u64){++warmed;});const auto deadline=Clock::now()+std::chrono::seconds(60);while(warmed<2&&Clock::now()<deadline){pump();Sleep(2);}check(warmed==2,"Background warm-up did not complete");}
        for(int repetition=0;repetition<3;++repetition){
            for(const char* path:{"queue","studio"}){
                g_exactJobsMode046=0;g_busyTestMode=true;samples.arr.push_back(measure("before_busy_renderer",path,"query_batch",batch));
                g_exactJobsMode046=1;g_busyTestMode=false;samples.arr.push_back(measure("after_background_job",path,"query_batch",batch));}
        }
        // Earlier dispatch of a tool outside the busy list, e.g. the same rows in a
        // headless/unready UI: the whole call runs on the UI thread.
        g_exactJobsMode046=0;g_busyTestMode=false;samples.arr.push_back(measure("before_ui_thread_call","queue","query_batch",batch));
        g_exactJobsMode046=1;
        for(const auto& row:samples.arr)if(row.find("mode")->asStr()=="after_background_job"){
            check(row.find("max_live_frame_gap_ms")->num<250,"Background exact read stalled live frames: "+row.dump());
            check(row.find("heartbeat_max_ms")->num<250,"Background exact read delayed the message loop: "+row.dump());
            check(row.find("select_input")->find("applied")->b&&row.find("zoom_input")->find("applied")->b&&row.find("orbit_input")->find("applied")->b&&row.find("input_applied_during_job")->b,"Orbit/zoom/selection were not applied during the exact read: "+row.dump());
            check(row.find("second_client_doc_info_ms")->num>=0&&row.find("second_client_doc_info_ms")->num<500,"A second client waited behind the background read: "+row.dump());}
        // Allow-list rejection: writes never enter the background API.
        bool called=false;check(submitExactJob046B("doc_apply",setup,[&](const V&,u64){called=true;})==0&&!called&&exactJobsActive046()==0,"Background API accepted a write");
        // Human cancel through the visible chip, with a capture while it runs.
        V cancelReply;bool cancelled=false;Clock::time_point cancelDone;
        {auto request=std::make_unique<UiToolRequest>();request->tool="query_batch";request->args=batch;request->complete=[&](V result){cancelReply=std::move(result);cancelled=true;cancelDone=Clock::now();};postTool(std::move(request));}
        auto started=Clock::now();while(ms(started,Clock::now())<700){pump();drawFrame(hdc);glFinish();}
        check(exactJobsActive046()==1,"Exact job not running before cancel");check(shot(shotPath+".exact-job.png"),"Exact job capture failed");
        const Zone* cancelZone=nullptr;for(const auto& zone:g_zones)if(zone.id==exactJobCancelAction046)cancelZone=&zone;check(cancelZone!=nullptr,"Exact job chip has no Cancel control");
        const int cx=(int)(cancelZone->x+cancelZone->w/2),cy=(int)(cancelZone->y+cancelZone->h/2);
        {std::string hits;for(const auto& zone:g_zones)if((float)cx>=zone.x&&(float)cx<=zone.x+zone.w&&(float)cy>=zone.y&&(float)cy<=zone.y+zone.h)hits+=std::to_string(zone.id)+" ";check(hits.rfind(std::to_string(exactJobCancelAction046),0)==0,"Cancel control is covered by zones: "+hits+" at "+std::to_string(cx)+","+std::to_string(cy));}
        const auto requested=Clock::now();
        PostMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(cx,cy));PostMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(cx,cy));
        while(!cancelled&&ms(requested,Clock::now())<10000){pump();drawFrame(hdc);glFinish();}
        const double cancelMs=ms(requested,cancelDone);check(cancelled&&cancelReply.find("isError")->b&&payload(cancelReply).find("error_code")->asStr()=="CANCELLED","Cancel did not stop the exact read: "+cancelReply.dump().substr(0,400));
        check(cancelMs<1500,"Cancel latency "+std::to_string(cancelMs)+" ms");report.set("cancel_latency_ms",V::makeNum(cancelMs));report.set("cancel_after_ms",V::makeNum(ms(started,requested)));
        check(g_session.revision==observed&&documentToJson(g_doc)==source,"Cancelled read changed the drawing");
        // Concurrent write during a read: the write commits on the UI thread; the
        // read keeps its revision label; a later read observes the write.
        V readReply;bool readDone=false;{auto request=std::make_unique<UiToolRequest>();request->tool="assembly_info";request->args=json(clearance);request->complete=[&](V result){readReply=std::move(result);readDone=true;};postTool(std::move(request));}
        started=Clock::now();while(ms(started,Clock::now())<300){pump();drawFrame(hdc);glFinish();}check(exactJobsActive046()==1&&!readDone,"Integrity read not running");
        V move=json(R"({"commands":[{"op":"assembly.edit","id":2,"p":{"tx":"140"}}]})");move.set("expected_revision",V::makeNum((double)observed));V writeReply;bool writeDone=false;
        {auto request=std::make_unique<UiToolRequest>();request->tool="doc_apply";request->args=move;request->complete=[&](V result){writeReply=std::move(result);writeDone=true;};postTool(std::move(request));}
        while((!writeDone||!readDone)&&ms(started,Clock::now())<60000){pump();drawFrame(hdc);glFinish();}
        check(writeDone&&!writeReply.find("isError")->b&&g_session.revision==observed+1,"Concurrent write failed: "+writeReply.dump().substr(0,400));
        check(readDone&&!readReply.find("isError")->b,"Read during a write failed: "+readReply.dump().substr(0,400));
        const auto readData=payload(readReply);check(readData.find("revision")->num==(double)observed&&readData.find("observed_revision")->num==(double)observed&&readData.find("exact_job")->find("document_changed_since_read")->b&&readData.find("clearance_state")->asStr()=="interfere","Read was not labelled with its observed revision: "+readData.dump().substr(0,600));
        const auto afterWrite=documentToJson(g_doc);check(afterWrite!=source,"Write did not change the drawing");
        V laterReply;bool laterDone=false;{auto request=std::make_unique<UiToolRequest>();request->tool="assembly_info";request->args=json(clearance);request->complete=[&](V result){laterReply=std::move(result);laterDone=true;};postTool(std::move(request));}
        started=Clock::now();while(!laterDone&&ms(started,Clock::now())<60000){pump();drawFrame(hdc);glFinish();}
        const auto laterData=payload(laterReply);check(laterDone&&!laterReply.find("isError")->b&&laterData.find("observed_revision")->num==(double)(observed+1)&&laterData.find("clearance_mm")->num>0&&!laterData.find("exact_job")->find("document_changed_since_read")->b,"Read after the write did not observe it: "+laterData.dump().substr(0,600));
        check(documentToJson(g_doc)==afterWrite&&g_session.revision==observed+1,"Background reads changed the drawing after the write");
        report.set("samples",samples);report.set("integrity",json(R"({"write_during_read_committed":true,"read_kept_observed_revision":true,"later_read_observed_write":true})"));
        std::ofstream output(shotPath+".timings.json");output<<report.dump();output.close();check(output.good(),"Cannot write async exact timings");
        g_busyTestMode=false;g_exactJobsTestMode046=false;drawFrame(hdc);check(shot(shotPath),"Async exact capture failed");
        return 0;
    }catch(const std::exception& error){std::ofstream failure(shotPath+".failure.txt");failure<<error.what();g_busyTestMode=false;g_exactJobsTestMode046=false;return 232;}
}
