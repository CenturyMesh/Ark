{
    using V=arkjson::Value;
    auto check=[](bool value,const std::string& why){if(!value)throw std::runtime_error(why);};
    auto json=[](const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid HUD fixture");return value;};
    auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);glFinish();};
    auto capture=[&](const char* suffix){draw();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> raw(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<winH;++y)std::copy_n(raw.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);check(writePNG((shotPath+suffix).c_str(),image),"HUD screenshot failed");};
    auto zoneOf=[&](int id){draw();return std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});};
    auto press=[&](int id,bool expectError=false){auto zone=zoneOf(id);check(zone!=g_zones.end(),"HUD control unavailable: "+std::to_string(id));const int x=(int)(zone->x+zone->w*.5f),y=(int)(zone->y+zone->h*.5f);g_error=false;SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));if(!expectError)check(!g_error,g_status);};
    auto row=[&](const char* name){draw();for(size_t r=0;r<g_parametricHud.rowParams.size();++r)if(g_doc.params[g_parametricHud.rowParams[r]].name==name)return (int)r;throw std::runtime_error(std::string("HUD row missing: ")+name);};
    auto rowZone=[&](const char* name,int part){const int r=row(name);for(int guard=0;guard<70&&zoneOf(48100+4*r+part)==g_zones.end();++guard)press(r<g_parametricHud.scroll?48006:48007);return 48100+4*r+part;};
    auto value=[&](const char* name){for(const auto& p:g_doc.params)if(p.name==name)return p.value;throw std::runtime_error(std::string("parameter missing: ")+name);};
    auto inside=[&](const char* stage){draw();RECT hud{};bool any=false;for(const auto& z:g_zones)if(z.id>=48000&&z.id<48400&&z.y>=viewY){any=true;check(z.x>=viewX-1&&z.x+z.w<=viewX+viewW+1&&z.y>=viewY-1&&z.y+z.h<=viewY+viewH+1,std::string("HUD control escaped the viewport ")+stage+": "+std::to_string(z.id)+" at "+std::to_string((int)z.x)+","+std::to_string((int)z.y)+" "+std::to_string((int)z.w)+"x"+std::to_string((int)z.h)+" view "+std::to_string(viewX)+","+std::to_string(viewY)+" "+std::to_string(viewW)+"x"+std::to_string(viewH));if(z.id==48009)hud={(LONG)z.x,(LONG)z.y,(LONG)(z.x+z.w),(LONG)(z.y+z.h)};}
        check(any&&hud.right>hud.left,std::string("HUD not drawn ")+stage);
        for(const auto& z:g_zones)if(z.id==179||z.id==191||z.id==455||z.id==580||(z.id>=560&&z.id<=562)||(z.id>=960&&z.id<=969)){RECT other{(LONG)z.x,(LONG)z.y,(LONG)(z.x+z.w),(LONG)(z.y+z.h)},overlap{};check(!IntersectRect(&overlap,&hud,&other),std::string("HUD overlaps a viewport control ")+stage+": "+std::to_string(z.id));}
        RECT overlap{};check(!IntersectRect(&overlap,&hud,&g_compassBounds),std::string("HUD overlaps the navigation triad ")+stage);};
    try{
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);g_assistantDockOpen=false;g_toolsOpen=false;g_toolModule=1;g_sidebarTab=0;g_showViewCube=true;g_parametricHud=ParametricHudState{};g_bakedMotions.open=false;layout();
        V model=json(R"({"commands":[{"op":"param.set","name":"width","expr":60,"hud":{"label":"Width","min":20,"max":120,"step":5,"unit":"mm","group":"Body","order":1}},
          {"op":"param.set","name":"height","expr":40,"hud":{"label":"Height","min":10,"max":100,"step":5,"unit":"mm","group":"Body","order":2}},
          {"op":"param.set","name":"slots","expr":3,"hud":{"label":"Slots","min":1,"max":6,"unit":"count","group":"Pattern"}},
          {"op":"param.set","name":"inner","expr":"width-10","hud":{"label":"Inner width","min":0,"max":200,"group":"Body","order":3}},
          {"op":"param.set","name":"depth","expr":30},
          {"op":"feature.add","type":"box","name":"Block","p":{"dx":"width","dy":"depth","dz":"height-20"}},
          {"op":"feature.add","type":"box","name":"Lid","p":{"dx":"inner","dy":"depth","dz":4}}]})");
        model.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",model);check(!g_error,g_status);selBody=-1;frameScene();
        check(zoneOf(48000)!=g_zones.end(),"Model ribbon has no HUD entry");press(48000);check(g_parametricHud.open,"HUD entry did not open the overlay");
        inside("open");capture(".parametric-hud-open.png");
        const auto baseline=documentToJson(g_doc);const u64 revision=g_session.revision;
        press(rowZone("width",2));press(rowZone("height",0));press(rowZone("height",0));
        check(g_parametricHud.staged.size()==2&&g_session.revision==revision&&documentToJson(g_doc)==baseline,"Staged edits changed the document before Rebuild");
        check(*parametricHudStagedValue("width")==65&&*parametricHudStagedValue("height")==30,"Step buttons did not stage width +5 and height -10");
        check(zoneOf(48100+4*row("inner"))==g_zones.end(),"A driven HUD row offered step controls");
        inside("staged");capture(".parametric-hud-staged.png");
        press(48002);check(g_session.revision==revision+1&&value("width")==65&&value("height")==30&&value("inner")==55&&g_parametricHud.staged.empty()&&!g_parametricHud.resultError,"Rebuild did not publish both staged values in one revision: "+g_parametricHud.result);
        check(g_parametricHud.result.find("Rebuilt 2 parameters")!=std::string::npos&&g_parametricHud.result.find(" ms")!=std::string::npos,"Rebuild result lacks the measured time: "+g_parametricHud.result);
        {const auto history=g_session.call("history_info",json(R"({"limit":1})"));const auto& node=history.find("structuredContent")->find("nodes")->arr.front();check(node.find("actor")->find("kind")->str=="human"&&node.find("tool")->str=="doc_apply","HUD rebuild was not journaled with the human actor");}
        // Typing clamps to the declared range and says so; Reset discards.
        g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"500"});press(rowZone("width",1));g_dialogSmoke=0;
        check(parametricHudStagedValue("width")&&*parametricHudStagedValue("width")==120&&g_parametricHud.result.find("Clamped to the HUD maximum 120 mm")!=std::string::npos,"Typed value was not clamped with a visible limit: "+g_parametricHud.result);
        press(48004);check(g_parametricHud.staged.empty()&&value("width")==65,"Reset did not discard the staged value");
        // In range but the Block depth becomes negative: rejected atomically, previous model kept, failure shown.
        const auto beforeFailure=documentToJson(g_doc);const u64 failureRevision=g_session.revision;const size_t bodies=g_doc.evalBodies.size();
        g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"15"});press(rowZone("height",1));g_dialogSmoke=0;check(*parametricHudStagedValue("height")==15,"Typed in-range value was not staged");
        press(48002,true);check(g_error&&g_parametricHud.resultError&&g_parametricHud.result.find("Feature #1")!=std::string::npos,"Failed rebuild was not shown in the HUD: "+g_parametricHud.result);
        check(documentToJson(g_doc)==beforeFailure&&g_session.revision==failureRevision&&g_doc.evalBodies.size()==bodies&&value("height")==30&&g_parametricHud.staged.size()==1,"Failed rebuild changed the model or dropped the staged value");
        inside("failed");capture(".parametric-hud-failed.png");press(48004);g_error=false;
        // Auto publishes each edit at once.
        press(48003);check(g_parametricHud.autoRebuild,"Auto toggle failed");const u64 autoRevision=g_session.revision;press(rowZone("slots",2));
        check(g_session.revision==autoRevision+1&&value("slots")==4&&g_parametricHud.staged.empty(),"Auto did not rebuild the edit at once");press(48003);
        // An agent param.set refreshes the HUD rows from the committed document.
        {V args=json(R"({"commands":[{"op":"param.set","name":"width","expr":90}],"actor":{"kind":"agent","id":"hud-selftest"}})");args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);check(!g_error,g_status);}
        check(value("width")==90&&value("inner")==80,"Agent edit not applied");press(rowZone("width",2));check(*parametricHudStagedValue("width")==95,"HUD did not stage from the agent's new value");press(48004);
        // Show in HUD from the Parameters explorer row.
        g_parametersOpen=true;size_t depth=0;for(size_t i=0;i<g_doc.params.size();++i)if(g_doc.params[i].name=="depth")depth=i;
        g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"Depth","10","80","5","mm","Body","4","1"});press(48400+(int)depth);g_dialogSmoke=0;
        check(hudExposed(g_doc.params[depth])&&hudSpec(g_doc.params[depth]).max==80&&hudSpec(g_doc.params[depth]).label=="Depth",std::string("Show in HUD did not expose the parameter: ")+g_status);row("depth");
        press(48001);check(g_parametricHud.collapsed,"Collapse failed");inside("collapsed");capture(".parametric-hud-collapsed.png");press(48001);
        // Narrow window: the overlay must stay clear of the cube, triad and dock.
        auto resize=[&](int width,int height){RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,(DWORD)GetWindowLongPtr(hwnd,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtr(hwnd,GWL_EXSTYLE));SetWindowPos(hwnd,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=height;layout();};
        const int savedW=winW,savedH=winH;resize(1100,700);inside("narrow");capture(".parametric-hud-narrow.png");resize(savedW,savedH);
        g_dialogSmoke=0;g_error=false;g_status="Parametric HUD selftest";
        std::fprintf(stderr,"Parametric HUD UI passed: ribbon toggle, grouped rows, staged -/+ edits, one-revision Rebuild with human actor and time, typed clamp, Reset, failed rebuild kept model, Auto, agent refresh, Show in HUD, collapse, bounds\n");
    }catch(const std::exception& error){g_dialogSmoke=0;std::fprintf(stderr,"Parametric HUD UI failed: %s\n",error.what());return 4701;}
}
