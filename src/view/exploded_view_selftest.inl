{
    using V=arkjson::Value;
    auto check=[](bool value,const std::string& why){if(!value)throw std::runtime_error(why);};
    auto json=[](const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid exploded view fixture");return value;};
    auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);glFinish();};
    auto capture=[&](const char* suffix){draw();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> raw(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<winH;++y)std::copy_n(raw.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);check(writePNG((shotPath+suffix).c_str(),image),"Exploded views screenshot failed");};
    auto zoneOf=[&](int id,bool panel){draw();return std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id&&(!panel||z.x>=viewX+viewW);});};
    auto press=[&](int id,bool panel=true){auto zone=zoneOf(id,panel);check(zone!=g_zones.end(),"Exploded views control unavailable: "+std::to_string(id));const int x=(int)(zone->x+zone->w*.5f),y=(int)(zone->y+zone->h*.5f);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));check(!g_error,g_status);};
    auto apply=[&](const char* commands){V args=json(commands);args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);check(!g_error,g_status);};
    auto bodyOf=[&](const char* name){for(size_t i=0;i<g_instanceNames.size();++i)if(g_instanceNames[i]==name)return i;throw std::runtime_error(std::string("Display instance missing: ")+name);};
    auto offset=[&](const char* name){const size_t i=bodyOf(name);return Vec3{g_instanceWorld[i].m[12]-g_formedInstanceWorld[i].m[12],g_instanceWorld[i].m[13]-g_formedInstanceWorld[i].m[13],g_instanceWorld[i].m[14]-g_formedInstanceWorld[i].m[14]};};
    auto pngSize=[&](const std::string& path,unsigned& w,unsigned& h){std::ifstream in(std::filesystem::u8path(path),std::ios::binary);unsigned char head[24]{};in.read((char*)head,24);w=(unsigned)head[16]<<24|(unsigned)head[17]<<16|(unsigned)head[18]<<8|head[19];h=(unsigned)head[20]<<24|(unsigned)head[21]<<16|(unsigned)head[22]<<8|head[23];return in.gcount()==24&&head[1]=='P'&&head[2]=='N'&&head[3]=='G'&&std::filesystem::file_size(std::filesystem::u8path(path))>2000;};
    auto slide=[&](double fraction){auto zone=zoneOf(47008,true);check(zone!=g_zones.end(),"Explode slider missing");const int y=(int)(zone->y+zone->h*.5f);const int x0=(int)(zone->x+zone->w*.1f),x1=(int)std::lround(zone->x+zone->w*fraction);
        SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x0,y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM((x0+x1)/2,y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x1,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x1,y));check(!g_error,g_status);draw();};
    try{
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);g_assistantDockOpen=false;g_toolsOpen=false;g_toolModule=1;g_sidebarTab=0;g_showViewCube=true;g_bakedMotions=BakedMotionsPanel{};g_explode=ExplodedViewsPanel{};layout();
        apply(R"({"commands":[{"op":"param.set","name":"lid_lift","expr":90},
          {"op":"feature.add","type":"box","name":"Base body","p":{"dx":160,"dy":120,"dz":60}},{"op":"feature.add","type":"box","name":"Lid body","p":{"dx":160,"dy":120,"dz":12}},
          {"op":"feature.add","type":"box","name":"Screw body","p":{"dx":8,"dy":8,"dz":30}},{"op":"feature.add","type":"box","name":"Cartridge body","p":{"dx":60,"dy":50,"dz":40}},
          {"op":"feature.add","type":"box","name":"Rail body","p":{"dx":12,"dy":12,"dz":140}},{"op":"feature.add","type":"box","name":"Slider body","p":{"dx":30,"dy":30,"dz":20}},
          {"op":"assembly.add","name":"Base","feature":"Base body","fixed":true},{"op":"assembly.add","name":"Cover","feature":0,"p":{"tz":60}},
          {"op":"assembly.add","name":"Lid","parent":"Cover","feature":"Lid body"},{"op":"assembly.add","name":"Screw A","parent":"Cover","feature":"Screw body","p":{"tx":10,"ty":10}},{"op":"assembly.add","name":"Screw B","parent":"Cover","feature":"Screw body","p":{"tx":142,"ty":102}},
          {"op":"assembly.add","name":"Cartridge","feature":"Cartridge body","p":{"tx":50,"ty":35,"tz":10}},
          {"op":"assembly.add","name":"Rail","feature":"Rail body","fixed":true,"p":{"tx":-80}},{"op":"assembly.add","name":"Slider","feature":"Slider body","p":{"tx":-80}},
          {"op":"mate.add","type":"prismatic","a":"Rail","b":"Slider","reference_a":"Z","reference_b":"Z","limits":{"min":0,"max":100}},
          {"op":"bom.initialize"},{"op":"feature.edit","id":"Screw body","part":{"part_number":"M8-30","description":"Cap screw","procurement":"buy"}}]})");
        // An agent-authored view opens the panel on that view, collapsed.
        apply(R"({"commands":[{"op":"explode.set","name":"Service","steps":[{"instances":["Cover"],"direction":"+Z","distance":"lid_lift","label":"Cover"},{"instances":["Screw A","Screw B"],"direction":"+Z","distance":60,"with_previous":true},{"instances":["Cartridge"],"direction":"+Y","distance":160}]}]})");
        check(g_explode.open&&!g_explode.visualize&&g_explode.selected=="Service"&&g_explode.factor==0,"An agent explode.set did not open the panel on the new view");
        check(viewW==winW-viewX-explodeDockWidth(),"Exploded views dock did not reserve the right side");
        selBody=-1;g_picked.clear();refreshSelectionLines();frameScene();tDist*=1.35;capture(".exploded-collapsed.png");
        const auto document=documentToJson(g_doc);const u64 revision=g_session.revision;
        // Slider: half way through the first slice, then fully exploded; display transforms only.
        slide(.25);check(std::fabs(g_explode.factor-.25)<.02&&g_explode.applied,"Slider did not set the explode factor");const auto lid=offset("Lid");check(lid.z>30&&lid.z<60&&offset("Cartridge").lensq()<1e-12,"Slice math: cover half way, cartridge waiting");
        slide(1.);check(g_explode.factor>.99,"Slider did not reach the exploded end");check(std::fabs(offset("Screw A").z-150)<1e-6&&std::fabs(offset("Cartridge").y-160)<1e-6,"Full explode offsets");frameScene();capture(".exploded-full.png");
        check(g_session.revision==revision&&documentToJson(g_doc)==document,"Moving the explode factor changed the document or revision");
        bool measureBlocked=false;try{beginMeasurement(3);}catch(const std::exception&){measureBlocked=true;}check(measureBlocked&&!measurementActive(),"Measurement started on exploded display positions");
        // Collapse, then Animate plays collapsed to exploded.
        press(47012);draw();check(!g_explode.applied&&offset("Lid").lensq()<1e-12,"Collapse did not restore the assembled display");
        press(47007);check(g_explode.animating&&g_explode.factor==0,"Animate did not start from the collapsed state");double previous=0;bool monotonic=true;int steps=0;
        while(g_explode.animating&&steps<200){advanceExplodedView(.05);monotonic&=g_explode.factor>=previous;previous=g_explode.factor;++steps;if(steps==6)capture(".exploded-animating.png");}
        check(!g_explode.animating&&g_explode.factor==1&&monotonic&&steps>4,"Animate did not play monotonically to the exploded end");draw();
        // Step editor: pick components in the viewport, then Add step through the native editor.
        press(47012);explodedViewPicked((int)bodyOf("Slider"));check(g_explode.pending.size()==1,"Viewport pick did not collect the component");
        g_dialogOverrideQueue={{"Slider","-X","40","0","Slider out"}};g_dialogSmoke=3;press(47014);g_dialogSmoke=0;
        const auto* view=findExplodedView(g_doc,"Service");check(view&&view->find("steps")->arr.size()==4&&g_explode.pending.empty()&&g_session.revision==revision+1,"Add step did not commit one revision-checked step");
        capture(".exploded-steps.png");
        runTool("doc_undo",V::makeObj());check(!g_error&&findExplodedView(g_doc,"Service")->find("steps")->arr.size()==3,"Undo did not remove the added step");
        // Motion link: an explode_view channel animates the explosion with the joint channel during baked playback.
        apply(R"({"commands":[{"op":"motion.graph.set","name":"Slide and explode","graph":{"channels":[{"name":"slide","mate":1,"from":0,"curve":{"type":"ease","duration":2,"to":80}},{"name":"explode","explode_view":"Service","from":0,"curve":{"type":"linear","duration":2,"to":1}}]}},{"op":"motion.bake","name":"Slide and explode","fps":20}]})");
        draw();check(g_bakedMotions.open&&!g_explode.open,"Baked Motions did not take the dock from the exploded views panel");
        g_bakedMotions.time=1.0;startBakedMotionDisplay();draw();check(std::fabs(g_explode.motionFactor-.5)<1e-3&&g_explode.motionView=="Service","Baked playback did not carry the explode factor");
        check(offset("Lid").z>80&&offset("Cartridge").lensq()<1e-9&&offset("Slider").z>30,"Baked frame did not combine joint pose and exploded offsets");capture(".exploded-motion.png");
        stopBakedMotion();draw();check(offset("Lid").lensq()<1e-12,"Stopping playback did not restore the assembled display");
        // Visualize: presets, progressive samples, balloons/BOM preview and PNG export.
        uiAction(47000);check(g_explode.open&&!g_bakedMotions.open,"Explode panel did not reopen");slide(1.);const bool gridBefore=g_grid;
        uiAction(47003);check(g_explode.visualize&&!g_grid,"Visualize did not apply its display preset");press(47025);press(47021);
        frameScene();for(int i=0;i<64;++i)draw();capture(".visualize-panel.png");
        g_explode.exportPath=shotPath+".visualize-viewport.png";press(47037);unsigned w=0,h=0;check(pngSize(g_explode.exportPath,w,h)&&(int)w==viewW&&(int)h==viewH,"Viewport export has the wrong size: "+g_status);
        press(47028);g_explode.exportPath=shotPath+".visualize-720p.png";press(47037);check(pngSize(g_explode.exportPath,w,h)&&w==1280&&h==720,"1280x720 export has the wrong size");
        press(47039);press(47029);press(47023);press(47026);g_explode.exportPath=shotPath+".visualize-software-1080p.png";press(47037);check(pngSize(g_explode.exportPath,w,h)&&w==1920&&h==1080,"Software 1920x1080 export has the wrong size");
        check(g_session.revision==revision+3&&!g_error,"Visualize changed the document revision");
        uiAction(47001);check(!g_explode.open&&g_grid==gridBefore&&!g_whiteBackground,"Closing Visualize did not restore display settings");
        std::fprintf(stderr,"Exploded views UI passed: agent-opened panel, slider slices, revision unchanged, measurement blocked, collapse, animate, viewport pick and add step, undo, baked explode channel, Visualize presets, viewport/720p/software 1080p PNG export (%s)\n",g_explode.lastExport.c_str());
    }catch(const std::exception& error){g_dialogSmoke=0;std::fprintf(stderr,"Exploded views UI failed: %s\n",error.what());return 4701;}
}
