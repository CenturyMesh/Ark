{
    // 050B drag chain UI: an agent-authored kiosk lift chain renders as real link occurrences,
    // its baked carriage motion rolls the links through the bend while the fixed run stays still,
    // and a selected link shows the chain state in Properties.
    using V=arkjson::Value;
    auto check=[](bool value,const std::string& why){if(!value)throw std::runtime_error(why);};
    auto json=[](const std::string& text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid drag chain fixture");return value;};
    auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);glFinish();};
    auto capture=[&](const char* suffix){draw();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> raw(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<winH;++y)std::copy_n(raw.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);check(writePNG((shotPath+suffix).c_str(),image),"Drag chain screenshot failed");};
    auto zoneOf=[&](int id,bool panel){draw();return std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id&&(!panel||z.x>=viewX+viewW);});};
    auto press=[&](int id,bool panel=true){auto zone=zoneOf(id,panel);check(zone!=g_zones.end(),"Drag chain control unavailable: "+std::to_string(id));const int x=(int)(zone->x+zone->w*.5f),y=(int)(zone->y+zone->h*.5f);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));check(!g_error,g_status);};
    auto apply=[&](const std::string& commands){V args=json(commands);args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);check(!g_error,g_status);};
    auto display=[&](u64 id){for(size_t i=0;i<g_instanceIds.size();++i)if(g_instanceIds[i]==id&&!g_instanceCopies[i])return i;throw std::runtime_error("Display instance missing: #"+std::to_string(id));};
    auto displayed=[&](){std::unordered_map<u64,Mat4> world;for(size_t i=0;i<g_instanceIds.size();++i)if(!g_instanceCopies[i])world[g_instanceIds[i]]=g_instanceWorld[i];return world;};
    try{
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);g_assistantDockOpen=false;g_toolsOpen=false;g_toolModule=1;g_sidebarTab=0;g_showViewCube=true;g_bakedMotions=BakedMotionsPanel{};g_explode=ExplodedViewsPanel{};layout();
        apply(R"({"commands":[{"op":"param.set","name":"chain_pitch","expr":15},{"op":"param.set","name":"chain_radius","expr":38},{"op":"param.set","name":"lift_min","expr":120},{"op":"param.set","name":"lift_max","expr":570},
          {"op":"feature.add","type":"box","name":"Duct back","p":{"dx":226,"dy":6,"dz":760},"appearance":{"base_color":[0.72,0.74,0.78],"opacity":0.55}},{"op":"feature.add","type":"box","name":"Duct wall","p":{"dx":6,"dy":72,"dz":760},"appearance":{"base_color":[0.72,0.74,0.78],"opacity":0.55}},
          {"op":"feature.add","type":"box","name":"Rail body","p":{"dx":12,"dy":12,"dz":700}},{"op":"feature.add","type":"box","name":"Carriage body","p":{"dx":60,"dy":40,"dz":50},"appearance":{"base_color":[0.85,0.45,0.15]}},
          {"op":"assembly.add","name":"Duct","feature":"Duct back","fixed":true,"p":{"tx":-60,"ty":20,"tz":-60}},{"op":"assembly.add","name":"Duct left","feature":"Duct wall","fixed":true,"p":{"tx":-66,"ty":-46,"tz":-60}},
          {"op":"assembly.add","name":"Rail","feature":"Rail body","fixed":true,"p":{"tx":100,"ty":-20,"tz":8}},{"op":"assembly.add","name":"Carriage","feature":"Carriage body","p":{"tx":100,"ty":-20,"tz":353}},
          {"op":"mate.add","type":"prismatic","a":"Rail","b":"Carriage","reference_a":"Z","reference_b":"Z","limits":{"min":"lift_min","max":"lift_max"}}]})");
        apply(R"({"commands":[{"op":"drag_chain.set","name":"Camera cable chain","link":{"pitch":"chain_pitch","outer_width":31.6,"outer_height":21.6,"inner_width":25,"inner_height":17},"bend_radius":"chain_radius","anchor":{"occurrence":"Duct","origin":[60,-20,405],"direction":"-Z","bend":"+X"},"moving":{"occurrence":"Carriage","attach":[-24,20,-8],"joint":1}}]})");
        const auto* record=findDragChain(g_doc,"Camera cable chain");check(record&&record->find("links")->arr.size()==25,"Chain was not created with 25 derived links");
        std::vector<u64> links;for(const auto& id:record->find("links")->arr)links.push_back((u64)id.num);for(u64 id:links)(void)display(id);
        apply(R"({"commands":[{"op":"motion.graph.set","name":"Lift cycle","graph":{"channels":[{"name":"lift","mate":1,"curve":{"type":"sequence","items":[{"type":"ease","duration":1,"to":120},{"type":"ease","duration":2,"to":570},{"type":"ease","duration":1,"to":345}]}}]}},{"op":"motion.bake","name":"Lift cycle","fps":30}]})");
        check(g_bakedMotions.open&&g_bakedMotions.selected=="Lift cycle","The carriage bake did not open the Baked Motions panel");
        const auto unchanged=documentToJson(g_doc);const auto revision=g_session.revision;
        selBody=-1;g_picked.clear();refreshSelectionLines();setOrbitView(0.35,-1.35,false,nullptr);frameScene();draw();
        press(45001);check(g_bakedMotions.playing&&g_bakedMotions.active,"Play did not start the chain playback");press(45001);
        struct Shot {double t,q;Mat4 first,last;double apex=0,fixedRun=0;};std::vector<Shot> shots{{1,120},{2,345},{3,570}};
        const char* names[]={".drag-chain-low.png",".drag-chain-mid.png",".drag-chain-high.png"};
        for(size_t s=0;s<shots.size();++s){auto& shot=shots[s];g_bakedMotions.time=shot.t;g_bakedMotions.shownFrame=~size_t(0);applyBakedMotionFrame();draw();
            shot.first=g_instanceWorld[display(links.front())];shot.last=g_instanceWorld[display(links.back())];const auto state=dragChainState(g_doc,"Camera cable chain",displayed());
            shot.apex=state.find("loop")->find("apex_along_run_mm")->num;shot.fixedRun=state.find("fixed_run_mm")->num;
            check(std::fabs(state.find("carriage_mm")->num-(345-shot.q))<0.05,"Displayed carriage does not match the baked q at t="+std::to_string(shot.t));capture(names[s]);}
        for(int e=0;e<16;++e)check(shots[0].first.m[e]==shots[1].first.m[e]&&shots[1].first.m[e]==shots[2].first.m[e],"The fixed-run end link moved during playback");
        check(std::fabs((shots[2].last.m[14]-shots[0].last.m[14])-450)<0.05,"The carriage-end link did not move rigidly with the carriage");
        check(std::fabs((shots[0].apex-shots[2].apex)-225)<0.5&&shots[0].fixedRun>shots[1].fixedRun&&shots[1].fixedRun>shots[2].fixedRun,"The loop did not roll at half the carriage speed");
        // Properties of a link in the bend, solved from the displayed frames.
        g_bakedMotions.time=2;g_bakedMotions.shownFrame=~size_t(0);applyBakedMotionFrame();
        const auto mid=dragChainState(g_doc,"Camera cable chain",displayed());const int bendLink=(int)mid.find("segments")->find("fixed_links")->num+1;
        selBody=(int)display(links[(size_t)bendLink]);g_sidebarTab=2;g_physicalVisible=false;g_inspectorScroll=150;layout();draw();
        check(g_inspectorOpen,"Properties tab is not visible");
        check(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.x<viewX&&(z.id==93||z.id==29||z.id==94||z.id==25);}),"Driven link Properties expose independent placement or removal");
        std::string title;std::vector<std::pair<std::string,std::string>> rows;check(dragChainProperties(links[(size_t)bendLink],title,rows)&&rows.size()==7&&title=="DRAG CHAIN / Camera cable chain","Properties do not describe the selected chain link");
        capture(".drag-chain-properties.png");
        g_dialogSmoke=2;press(161,false);press(577,false);g_dialogSmoke=0;
        press(45002);check(!g_bakedMotions.active,"Stop did not restore the solved pose");
        check(documentToJson(g_doc)==unchanged&&g_session.revision==revision,"Chain playback or inspection changed the document");
        g_sidebarTab=0;layout();
        // 054B: a cable bound to link pins deforms in the same recorded poses.
        auto cable=json(R"({"op":"feature.add","type":"cable_route","name":"Flexible harness","inputs":[],"appearance":{"base_color":[0.1,0.65,0.95]},"native":{"route_mode":"spline","radius":1.5,"bend_radius":5}})");
        V spec=*cable.find("native"),points=V::makeArr(),inputs=V::makeArr();inputs.arr.push_back(*findDragChain(g_doc,"Camera cable chain")->find("link_feature"));cable.set("inputs",inputs);
        auto pin=[](u64 id,double x=0){V p=V::makeObj(),offset=V::makeArr();p.set("node",V::makeNum((double)id));offset.arr={V::makeNum(x),V::makeNum(0),V::makeNum(0)};p.set("offset",offset);return p;};
        spec.set("start",pin(links.front()));spec.set("end",pin(links.back(),15));for(size_t i=2;i<links.size();i+=2)points.arr.push_back(pin(links[i]));spec.set("waypoints",points);cable.set("native",spec);
        apply("{\"commands\":["+cable.dump()+",{\"op\":\"assembly.add\",\"feature\":\"Flexible harness\",\"fixed\":true}]}");
        std::string unavailable;check(!bakedMotionPlaybackAvailable(unavailable)&&unavailable.find("stale")!=std::string::npos,"Stale motion must ask for Bake");
        apply(R"({"commands":[{"op":"motion.bake","name":"Lift cycle","fps":1}]})");
        const auto cableNode=g_doc.assembly.back().id;const auto part=g_instancePart[display(cableNode)];const auto originalMesh=g_cpuMeshes[part].pos;const auto cableSource=documentToJson(g_doc);const auto cableRevision=g_session.revision;
        auto waitCables=[&]{const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(60);while(motion_cables_view_054b::state.worker.valid()&&std::chrono::steady_clock::now()<end){pollMotionCables054B();Sleep(10);}check(!motion_cables_view_054b::state.worker.valid(),"Cable preparation timeout");};
        press(45001);check(motion_cables_view_054b::state.worker.valid(),"Cable preparation did not start");press(45002);waitCables();check(!g_bakedMotions.playing&&g_cpuMeshes[part].pos==originalMesh,"Cancellation changed cable display");
        press(45001);waitCables();check(motion_cables_view_054b::state.ready&&g_bakedMotions.playing,"Native cable preparation failed: "+motion_cables_view_054b::state.error);press(45001);
        selBody=-1;g_picked.clear();g_showEdges=false;for(size_t i=0;i<g_bodyIds.size();++i){const auto link=std::find(links.begin(),links.end(),g_instanceIds[i]);g_hiddenBodies[i]=g_bodyIds[i]<=3||(link!=links.end()&&(link-links.begin())%2==0);}rebuildDisplayBounds();setOrbitView(0,-1.45,true,nullptr);frameScene();g_status="Flexible harness / alternate links hidden for inspection";
        g_bakedMotions.time=1;g_bakedMotions.shownFrame=~size_t(0);applyBakedMotionFrame();const auto lowMesh=g_cpuMeshes[part].pos;frameScene();capture(".cable-low.png");
        g_bakedMotions.time=3;g_bakedMotions.shownFrame=~size_t(0);applyBakedMotionFrame();check(g_cpuMeshes[part].pos!=lowMesh,"Cable display remained frozen");frameScene();capture(".cable-high.png");
        press(45002);check(g_cpuMeshes[part].pos==originalMesh,"Stop did not restore cable mesh");check(documentToJson(g_doc)==cableSource&&g_session.revision==cableRevision,"Flexible display edited source geometry");
        std::fprintf(stderr,"Drag chain UI passed: 25 driven link occurrences, baked carriage playback at q=120/345/570 with a static fixed run, rigid carriage end and half-speed loop, link properties\n");
    }catch(const std::exception& error){g_dialogSmoke=0;std::fprintf(stderr,"Drag chain UI failed: %s\n",error.what());return 5001;}
}
