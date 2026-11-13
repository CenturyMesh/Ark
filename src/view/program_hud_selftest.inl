// Program controls in the Parametric HUD. The built-in bookshelf is installed through
// the tool path; the HUD then changes the shelf count with prepare{configure} +
// doc_apply on the CAD worker, rejects an assertion failure, stale staging and a value
// above a narrowed max without changing the document, does not stage a value typed while
// another writer configured the program, shows local edits, cancels and confirms Detach,
// and removes a second program. Its binding rows must equal the live inspect, also with
// imported meshes in the drawing. Captures are written next to --shot.
static int testProgramHudUi(HWND hwnd,const std::string& shotPath){
    using V=arkjson::Value;using namespace program_hud;
    auto check=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    auto stage=[&](const char* step){std::ofstream(shotPath+".program-hud-progress.log",std::ios::app)<<step<<'\n';std::fprintf(stderr,"Program HUD UI: %s\n",step);std::fflush(stderr);};
    const bool oldBusyTest=g_busyTestMode;const unsigned oldDelay=g_busyTestDelayMs;g_busyTestMode=true;HDC hdc=GetDC(hwnd);std::ostringstream evidence;
    auto restore=[&]{g_dialogSmoke=0;g_dialogOverrideQueue.clear();g_dialogShot.clear();g_editorSmokeProbe=nullptr;g_busyTestMode=oldBusyTest;g_busyTestDelayMs=oldDelay;ReleaseDC(hwnd,hdc);};
    // Same order as the main loop: bindings are re-read between input and drawing.
    auto draw=[&]{programHudPoll();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);glFinish();};
    auto capture=[&](const char* suffix){draw();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> raw(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<winH;++y)std::copy_n(raw.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);check(writePNG((shotPath+suffix).c_str(),image),"Program HUD screenshot failed");};
    auto zoneOf=[&](int id){draw();return std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});};
    auto press=[&](int id){auto zone=zoneOf(id);check(zone!=g_zones.end(),"Program HUD control unavailable: "+std::to_string(id));const int x=(int)(zone->x+zone->w*.5f),y=(int)(zone->y+zone->h*.5f);g_error=false;SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));};
    auto rowId=[&](const char* name,int part){draw();for(size_t i=0;i<g_programHud.rowNames.size();++i)if(g_programHud.rowNames[i]==name){const int id=rowBase+4*(int)i+part;for(int guard=0;guard<70&&zoneOf(id)==g_zones.end();++guard)press((int)i<g_programHud.scroll?48019:48020);return id;}throw std::runtime_error(std::string("Program row missing: ")+name);};
    auto binding=[&](const char* ns)->const V*{for(const auto& p:g_doc.parametricPrograms.arr)if(field(p,"namespace").asStr()==ns)return &p;return nullptr;};
    auto value=[&](const char* ns,const char* name){const V* b=binding(ns);check(b!=nullptr,std::string("Binding missing: ")+ns);return field(field(*b,"values"),name).num;};
    auto named=[&](const char* prefix){size_t n=0;for(const auto& f:g_doc.features)if(f.name.rfind(prefix,0)==0)++n;return n;};
    auto bodies=[&](const char* prefix){size_t n=0;for(const auto& body:g_doc.evalBodies)if(const auto* f=g_doc.find(body.id);f&&f->name.rfind(prefix,0)==0)++n;return n;};
    auto staged=[&](const char* name)->double{for(const auto& e:g_programHud.staged)if(e.first==name)return e.second;throw std::runtime_error(std::string("Not staged: ")+name);};
    auto agent=[&](const char* commands){V a;check(arkjson::parse(commands,a),"Bad agent fixture");V actor=V::makeObj();actor.set("kind",V::makeStr("agent"));actor.set("id",V::makeStr("program-hud-selftest"));a.set("actor",actor);a.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",a);check(!g_error,g_status);};
    auto install=[&](const char* example,const char* ns){V a=V::makeObj();a.set("action",V::makeStr("prepare"));a.set("mode",V::makeStr("install"));a.set("namespace",V::makeStr(ns));a.set("expected_revision",V::makeNum((double)g_session.revision));
        for(const auto& row:parametricProgramExamples().arr)if(field(row,"id").asStr()==example)a.set("program",field(row,"program"));check(a.find("program")!=nullptr,std::string("Example missing: ")+example);
        const auto prepared=runTool("parametric_program",a);check(!g_error,g_status);V apply=V::makeObj();apply.set("candidate_id",field(field(prepared,"structuredContent"),"candidate_id"));apply.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",apply);check(!g_error,g_status);};
    // The HUD reads bindings from a session holding only the owned objects; the live session must give identical rows.
    double liveInspectMs=0;auto sameAsLive=[&](const char* where){V a=V::makeObj();a.set("action",V::makeStr("inspect"));const auto started=std::chrono::steady_clock::now();const auto live=g_session.call("parametric_program",a);liveInspectMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        check(!field(live,"isError").b&&g_programHud.inspectScope=="owned objects"&&g_programHud.rows.dump()==field(field(live,"structuredContent"),"programs").dump(),std::string("HUD bindings differ from the live inspect ")+where+" ("+g_programHud.inspectScope+")");};
    // Controls inside the viewport, clear of the cube, triad and viewport dock.
    auto inside=[&](const char* where){draw();RECT hud{};bool any=false;for(const auto& z:g_zones)if(z.id>=48000&&z.id<49000&&!(z.id>=48400&&z.id<48656)&&z.y>=viewY){any=true;check(z.x>=viewX-1&&z.x+z.w<=viewX+viewW+1&&z.y>=viewY-1&&z.y+z.h<=viewY+viewH+1,std::string("Program HUD control escaped the viewport ")+where+": "+std::to_string(z.id));if(z.id==48009)hud={(LONG)z.x,(LONG)z.y,(LONG)(z.x+z.w),(LONG)(z.y+z.h)};}
        check(any&&hud.right>hud.left,std::string("Program HUD not drawn ")+where);
        for(const auto& z:g_zones)if(z.id==179||z.id==191||z.id==455||z.id==580||(z.id>=560&&z.id<=562)||(z.id>=960&&z.id<=969)){RECT other{(LONG)z.x,(LONG)z.y,(LONG)(z.x+z.w),(LONG)(z.y+z.h)},overlap{};check(!IntersectRect(&overlap,&hud,&other),std::string("Program HUD overlaps a viewport control ")+where+": "+std::to_string(z.id));}
        RECT overlap{};check(!IntersectRect(&overlap,&hud,&g_compassBounds),std::string("Program HUD overlaps the navigation triad ")+where);};
    try{
        stage("new-document");
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);check(!g_error,g_status);
        g_assistantDockOpen=false;g_toolsOpen=false;g_toolModule=1;g_sidebarTab=0;g_showViewCube=true;g_parametricHud=ParametricHudState{};g_programHud=ProgramHudState{};g_bakedMotions.open=false;layout();
        stage("install-bookshelf");install("bookshelf","shelf");selBody=-1;frameScene();
        const u64 installed=g_session.revision;check(named("shelf / Shelf ")==5&&bodies("shelf / Shelf ")==5&&value("shelf","shelves")==5,"Bookshelf did not install five exact shelves");
        stage("open-hud");press(48000);check(g_parametricHud.open&&g_programHud.view,"HUD did not open on Programs when only a program is installed");draw();
        check(g_programHud.rows.arr.size()==1&&field(g_programHud.rows.arr[0],"namespace").asStr()=="shelf"&&field(g_programHud.rows.arr[0],"name").asStr()=="Bookshelf"&&!field(g_programHud.rows.arr[0],"local_edits").b,"Inspect listing lacks shelf / Bookshelf without local edits");
        check(g_programHud.rowNames==std::vector<std::string>({"depth","height","shelves","thickness","width"}),"Program rows do not list every declared parameter by name");sameAsLive("after install");
        evidence<<"inspect "<<g_programHud.inspectMs<<" ms; ";inside("open");capture(".program-hud-open.png");
        // Staging leaves the document untouched.
        stage("stage-shelves");const auto installedJson=documentToJson(g_doc);press(rowId("shelves",2));
        check(g_programHud.staged.size()==1&&staged("shelves")==6&&g_session.revision==installed&&documentToJson(g_doc)==installedJson,"+ did not stage shelves=6 without changing the document");
        {const auto shelvesRow=std::find(g_programHud.rowNames.begin(),g_programHud.rowNames.end(),"shelves")-g_programHud.rowNames.begin();check(zoneOf(rowBase+4*(int)shelvesRow+1)!=g_zones.end(),"The staged row scrolled out of view");}
        check(zoneOf(48015)!=g_zones.end(),"Apply is not offered with a staged value");inside("staged");capture(".program-hud-staged.png");
        // Apply: prepare{configure} + doc_apply on the worker while busy frames keep painting.
        stage("apply");g_busyTestDelayMs=40;const unsigned framesBefore=g_busyFrameCount;press(48015);g_busyTestDelayMs=oldDelay;
        check(!g_programHud.resultError&&!g_error,"Apply failed: "+g_programHud.result+" / "+g_status);
        check(g_session.revision==installed+1&&value("shelf","shelves")==6&&named("shelf / Shelf ")==6&&bodies("shelf / Shelf ")==6&&g_programHud.staged.empty(),"Apply did not publish shelves=6 with six shelf solids in one revision");
        check(g_busyFrameCount>=framesBefore+2,"Apply did not run on the worker path with busy frames");check(g_programHud.result.find("Applied 1 value to shelf")!=std::string::npos,"Apply result missing: "+g_programHud.result);
        {const auto history=g_session.call("history_info",[]{V a=V::makeObj();a.set("limit",V::makeNum(1));return a;}());const auto& node=field(history,"structuredContent").find("nodes")->arr.front();check(field(field(node,"actor"),"kind").asStr()=="human"&&field(node,"tool").asStr()=="doc_apply","Program configure was not journaled as a human doc_apply");}
        {V a=V::makeObj();a.set("id",V::makeStr("shelf / Shelf 6"));a.set("detail",V::makeStr("properties"));const auto inspected=g_session.call("native_inspect",a);const auto& report=field(field(inspected,"structuredContent"),"report");const double volume=field(report,"volume_mm3").num,expected=(800.-2*18)*18*300;
            check(field(report,"brep_valid").b&&std::fabs(volume-expected)<=expected*1e-6,"Sixth shelf is not the expected exact solid: "+field(inspected,"structuredContent").dump().substr(0,400));evidence<<"Shelf 6 volume "<<volume<<" mm3 (expected "<<expected<<"); ";}
        evidence<<"apply prepare "<<g_programHud.prepareMs<<" ms, publish "<<g_programHud.applyMs<<" ms, busy frames "<<(g_busyFrameCount-framesBefore)<<"; ";capture(".program-hud-applied.png");
        // Typed values honour integer and max; then an in-range combination fails the program assertion.
        stage("typed-integer-and-max");g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"7.4"});press(rowId("shelves",1));g_dialogSmoke=0;check(staged("shelves")==7&&g_programHud.result.find("Rounded to the integer 7")!=std::string::npos,"Integer parameter was not rounded with a visible note: "+g_programHud.result);
        g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"50"});press(rowId("shelves",1));g_dialogSmoke=0;check(staged("shelves")==12&&g_programHud.result.find("Clamped to the program maximum 12")!=std::string::npos,"Value above max was not clamped with a visible limit: "+g_programHud.result);
        g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"1200"});press(rowId("height",1));g_dialogSmoke=0;check(staged("height")==1200,"Typed height was not staged");
        stage("assertion-rejection");const auto beforeReject=documentToJson(g_doc);const u64 rejectRevision=g_session.revision;const size_t bodyCount=g_doc.evalBodies.size();press(48015);
        check(g_programHud.resultError&&g_programHud.code=="PROGRAM_ASSERT"&&g_programHud.result.find("Clear gap between shelves must be at least 120 mm")!=std::string::npos,"Assertion rejection was not shown with its code: "+g_programHud.code+" / "+g_programHud.result);
        check(documentToJson(g_doc)==beforeReject&&g_session.revision==rejectRevision&&g_doc.evalBodies.size()==bodyCount&&value("shelf","shelves")==6&&g_programHud.staged.size()==2,"Rejected Apply changed the document or dropped the staged values");
        inside("rejected");capture(".program-hud-rejected.png");press(48016);check(g_programHud.staged.empty()&&documentToJson(g_doc)==beforeReject,"Reset did not discard staged values");
        // Values staged before another writer's commit are reviewed, not published blindly.
        stage("stale-staging");press(rowId("shelves",2));check(staged("shelves")==7,"Could not stage shelves=7");agent(R"({"commands":[{"op":"param.set","name":"note","expr":1}]})");const u64 agentRevision=g_session.revision;
        press(48015);check(g_session.revision==agentRevision&&g_programHud.resultError&&g_programHud.result.find("Model changed since these values were staged")!=std::string::npos&&staged("shelves")==7,"Values staged before an agent commit were published without review: "+g_programHud.result);
        press(48015);check(!g_programHud.resultError&&g_session.revision==agentRevision+1&&value("shelf","shelves")==7&&bodies("shelf / Shelf ")==7,"Second Apply did not publish the reviewed value: "+g_programHud.result);
        // A configure from another window while the value editor is open: the typed value is not staged over the newer model.
        stage("typed-value-race");{const u64 before=g_session.revision;bool committed=false;g_editorSmokeProbe=[&](HWND){if(committed)return;committed=true;V a=V::makeObj(),values=V::makeObj();values.set("shelves",V::makeNum(8));a.set("action",V::makeStr("prepare"));a.set("mode",V::makeStr("configure"));a.set("namespace",V::makeStr("shelf"));a.set("values",values);a.set("expected_revision",V::makeNum((double)g_session.revision));
                const auto prepared=g_session.call("parametric_program",a);V apply=V::makeObj();apply.set("candidate_id",field(field(prepared,"structuredContent"),"candidate_id"));apply.set("expected_revision",V::makeNum((double)g_session.revision));g_session.call("doc_apply",apply);};
            g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"6"});press(rowId("shelves",1));g_dialogSmoke=0;g_editorSmokeProbe=nullptr;check(committed&&g_session.revision==before+1&&value("shelf","shelves")==8,"Concurrent configure fixture did not commit shelves=8");
            check(g_programHud.staged.empty()&&g_programHud.resultError&&g_programHud.result.find("Model changed while editing shelves")!=std::string::npos,"A value typed over an older model was staged: "+g_programHud.result);
            draw();check(g_programHud.revision==g_session.revision&&field(field(g_programHud.rows.arr[0],"values"),"shelves").num==8,"Program rows were not re-read after the concurrent configure");capture(".program-hud-typed-race.png");
            runTool("doc_undo",V::makeObj());check(!g_error&&value("shelf","shelves")==7&&bodies("shelf / Shelf ")==7,"Undo did not restore shelves=7 after the concurrent configure");}
        // An agent update narrows shelves to max 8 under a staged 9: Apply asks for review, then shows PROGRAM_VALUES.
        stage("source-update-values");g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"9"});press(rowId("shelves",1));g_dialogSmoke=0;check(staged("shelves")==9,"Could not stage shelves=9");
        {V program;for(const auto& row:parametricProgramExamples().arr)if(field(row,"id").asStr()=="bookshelf")program=field(row,"program");V parameters=field(program,"parameters"),shelves=field(parameters,"shelves");shelves.set("max",V::makeNum(8));parameters.set("shelves",shelves);program.set("parameters",parameters);
            V a=V::makeObj();a.set("action",V::makeStr("prepare"));a.set("mode",V::makeStr("update"));a.set("namespace",V::makeStr("shelf"));a.set("program",program);a.set("expected_revision",V::makeNum((double)g_session.revision));const auto prepared=runTool("parametric_program",a);check(!g_error,g_status);
            V apply=V::makeObj(),actor=V::makeObj();actor.set("kind",V::makeStr("agent"));actor.set("id",V::makeStr("program-hud-selftest"));apply.set("actor",actor);apply.set("candidate_id",field(field(prepared,"structuredContent"),"candidate_id"));apply.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",apply);check(!g_error,g_status);}
        const u64 updatedRevision=g_session.revision;draw();check(staged("shelves")==9&&value("shelf","shelves")==7&&field(field(field(g_programHud.rows.arr[0],"parameters"),"shelves"),"max").num==8,"Source update did not keep the staged value under the narrower range");
        press(48015);check(g_session.revision==updatedRevision&&g_programHud.resultError&&g_programHud.result.find("Program source of shelf changed")!=std::string::npos&&staged("shelves")==9,"Values staged before a source update were published without review: "+g_programHud.result);
        {const auto beforeValues=documentToJson(g_doc);press(48015);check(g_programHud.code=="PROGRAM_VALUES"&&g_programHud.result.find("shelves=9 exceeds max 8")!=std::string::npos,"Out-of-range value was not shown as PROGRAM_VALUES: "+g_programHud.code+" / "+g_programHud.result);
            check(documentToJson(g_doc)==beforeValues&&g_session.revision==updatedRevision&&value("shelf","shelves")==7&&bodies("shelf / Shelf ")==7&&staged("shelves")==9,"PROGRAM_VALUES rejection changed the document or dropped the staged value");}
        inside("values");capture(".program-hud-values.png");press(48016);
        // An edited owned object is shown and rejects configure with PROGRAM_LOCAL_EDITS.
        stage("local-edits");agent(R"({"commands":[{"op":"feature.edit","id":"shelf / Side 1","p":{"depth":"1790"}}]})");draw();check(field(g_programHud.rows.arr[0],"local_edits").b,"Inspect did not report the local edit after the agent revision");sameAsLive("after a local edit");
        capture(".program-hud-local-edits.png");press(rowId("shelves",2));const auto beforeLocal=documentToJson(g_doc);const u64 localRevision=g_session.revision;press(48015);
        check(g_programHud.code=="PROGRAM_LOCAL_EDITS"&&documentToJson(g_doc)==beforeLocal&&g_session.revision==localRevision,"Local edits did not reject configure atomically: "+g_programHud.code+" / "+g_programHud.result);press(48016);
        // Detach asks first: Cancel changes nothing, confirmation keeps the objects as ordinary source.
        stage("detach-cancel");g_dialogSmoke=2;g_dialogShot=shotPath+".program-hud-detach-dialog.png";press(48017);g_dialogSmoke=0;g_dialogShot.clear();
        check(binding("shelf")&&g_session.revision==localRevision&&documentToJson(g_doc)==beforeLocal&&g_programHud.result.find("Detach cancelled")!=std::string::npos,"Cancelled Detach changed the document");check(std::filesystem::is_regular_file(shotPath+".program-hud-detach-dialog.png"),"Detach confirmation capture missing");
        stage("detach-confirm");g_dialogSmoke=3;press(48017);g_dialogSmoke=0;check(!binding("shelf")&&g_session.revision==localRevision+1&&named("shelf / Shelf ")==7&&named("shelf / Side ")==2,"Detach did not keep the objects and drop the binding: "+g_programHud.result);
        check(g_programHud.result.find("Detached shelf")!=std::string::npos,"Detach result missing: "+g_programHud.result);draw();check(g_programHud.rows.arr.empty(),"Detached program still listed");capture(".program-hud-detached.png");
        // Outside source that uses an owned feature rejects Remove; without it Remove deletes only the owned objects.
        stage("remove-dependants");install("bolted_flange","flange");draw();check(g_programHud.rows.arr.size()==1&&g_programHud.selected=="flange","Second program not listed");
        agent(R"({"commands":[{"op":"feature.add","type":"transform","name":"Flange copy","inputs":["flange / Flange"],"p":{"tz":40}}]})");const auto beforeDependants=documentToJson(g_doc);const u64 dependantsRevision=g_session.revision;
        g_dialogSmoke=3;press(48018);g_dialogSmoke=0;check(g_programHud.code=="PROGRAM_DEPENDANTS"&&documentToJson(g_doc)==beforeDependants&&g_session.revision==dependantsRevision&&binding("flange"),"Dependants did not reject Remove atomically: "+g_programHud.code+" / "+g_programHud.result);
        capture(".program-hud-dependants.png");runTool("doc_undo",V::makeObj());check(!g_error&&named("Flange copy")==0&&binding("flange"),"Undo did not remove the dependant copy");
        // A commit while the confirmation is open: the confirmed step is not sent to the newer model.
        stage("remove-confirm-race");{const u64 before=g_session.revision;g_editorSmokeProbe=[&](HWND){V a;arkjson::parse(R"({"commands":[{"op":"param.set","name":"note","expr":2}]})",a);a.set("expected_revision",V::makeNum((double)g_session.revision));g_session.call("doc_apply",a);};
            g_dialogSmoke=3;press(48018);g_dialogSmoke=0;g_editorSmokeProbe=nullptr;check(binding("flange")&&named("flange / ")==1&&g_session.revision==before+1&&g_programHud.resultError&&g_programHud.result.find("Model changed while confirming")!=std::string::npos,"Remove confirmed on an older model was sent: "+g_programHud.result);}
        stage("remove");const u64 flangeRevision=g_session.revision;g_dialogSmoke=3;g_dialogShot=shotPath+".program-hud-remove-dialog.png";press(48018);g_dialogSmoke=0;g_dialogShot.clear();
        check(!binding("flange")&&named("flange / ")==0&&g_session.revision==flangeRevision+1&&named("shelf / Shelf ")==7,"Remove did not delete only the flange program: "+g_programHud.result);
        runTool("doc_undo",V::makeObj());check(!g_error&&binding("flange")&&named("flange / ")==1,"Undo did not restore the removed program");draw();check(g_programHud.rows.arr.size()==1,"Restored program not listed");
        // Variants opens the 067G configurator on the same namespace.
        stage("variants-link");press(48014);check(design_variants_view_067g::state.window&&design_variants_view_067g::state.ns=="flange","Variants did not open the configurator on the selected program");DestroyWindow(design_variants_view_067g::state.window);
        // Narrow window and the Parameters tab.
        stage("narrow");auto resize=[&](int width,int height){RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,(DWORD)GetWindowLongPtr(hwnd,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtr(hwnd,GWL_EXSTYLE));SetWindowPos(hwnd,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=height;layout();};
        const int savedW=winW,savedH=winH;resize(1100,700);inside("narrow");capture(".program-hud-narrow.png");press(48010);check(!g_programHud.view,"Parameters tab did not switch views");inside("parameters-tab");resize(savedW,savedH);
        press(48011);check(g_programHud.view,"Programs tab did not switch views");
        // Imported meshes enlarge the drawing; the HUD re-read still serializes only the owned objects.
        stage("large-drawing");{const std::string scan=shotPath+".program-hud-scan.stl";{std::ofstream out(scan,std::ios::binary);char header[80]{};out.write(header,80);const unsigned count=45*45*2;out.write((const char*)&count,4);
                auto at=[](int i,int j,float* p){p[0]=400.f+4*i;p[1]=4.f*j;p[2]=3*std::sin(.3f*i+.2f*j);};auto tri=[&](const float* a,const float* b,const float* c){const float n[3]{0,0,1};const unsigned short attribute=0;out.write((const char*)n,12);out.write((const char*)a,12);out.write((const char*)b,12);out.write((const char*)c,12);out.write((const char*)&attribute,2);};
                for(int i=0;i<45;++i)for(int j=0;j<45;++j){float a[3],b[3],c[3],d[3];at(i,j,a);at(i+1,j,b);at(i,j+1,c);at(i+1,j+1,d);tri(a,b,d);tri(a,d,c);}}
            for(int k=0;k<4;++k){V a=V::makeObj();a.set("path",V::makeStr(scan));a.set("name",V::makeStr("Scan "+std::to_string(k+1)));a.set("expected_revision",V::makeNum((double)g_session.revision));runTool("import_mesh",a);check(!g_error,g_status);}
            std::filesystem::remove(scan);draw();check(g_programHud.revision==g_session.revision,"Program rows were not re-read after the imports");sameAsLive("with imported meshes");
            evidence<<"4 imported meshes, "<<documentToJson(g_doc,false,false).size()<<" source bytes: HUD inspect "<<g_programHud.inspectMs<<" ms, live inspect "<<liveInspectMs<<" ms; ";}
        g_status="Program HUD selftest";restore();
        std::ofstream(shotPath+".program-hud-test.log")<<"PASS bookshelf configure via HUD (revision +1, six then seven shelf solids, human journal), integer/max honoured, PROGRAM_ASSERT, stale staging, value typed during a concurrent configure not staged, PROGRAM_VALUES after a source update, PROGRAM_LOCAL_EDITS and PROGRAM_DEPENDANTS rejected with document unchanged, Detach cancel/confirm, Remove not sent after a commit during confirmation, Remove + Undo, Variants link, bounds, owned-object inspect identical to the live inspect\n"<<evidence.str()<<'\n';
        std::fprintf(stderr,"Program HUD UI passed: %s\n",evidence.str().c_str());return 0;
    }catch(const std::exception& e){restore();std::ofstream(shotPath+".program-hud-test.log")<<"FAIL "<<e.what()<<'\n'<<evidence.str()<<'\n';std::fprintf(stderr,"Program HUD UI failed: %s\n",e.what());return 4801;}
}
