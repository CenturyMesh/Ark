if(parametricGraphUiTest){
    using V=arkjson::Value;using namespace parametric_view_053b;
    auto verify=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    auto json=[&](const char* text){V v;verify(arkjson::parse(text,v),"Invalid graph fixture");return v;};
    auto apply=[&](const char* text){V args=json(text);args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);verify(!g_error,g_status);};
    auto capture=[&](const char* suffix){InvalidateRect(state.window,nullptr,FALSE);UpdateWindow(state.window);bomCapture(state.window,shotPath+suffix);verify(std::filesystem::is_regular_file(shotPath+suffix),"Graph capture failed");};
    try{
        g_session=AgentSession{};++g_documentEpoch;rebuildScene();
        apply(R"({"commands":[{"op":"param.set","name":"span","expr":44},{"op":"param.set","name":"thickness","expr":6},{"op":"param.set","name":"radius","expr":8},{"op":"plane.set","name":"Mounting face","p":{"oz":"thickness"}},{"op":"sketch.set","name":"Bearing profile","sketch":{"plane":"Mounting face","entities":[{"kind":"circle","name":"Outside","dof":[0,0,"radius"]}]}},{"op":"feature.add","type":"native_solid","name":"Mounting plate","native":{"op":"box"},"p":{"width":"span","depth":28,"height":"thickness"}},{"op":"feature.add","type":"extrude","name":"Bearing boss","p":{"sketch":"Bearing profile","height":18}},{"op":"assembly.add","name":"Bearing module","feature":0},{"op":"assembly.add","name":"Plate","feature":1,"parent":1},{"op":"assembly.add","name":"Bearing","feature":2,"parent":1} ]})");
        const auto folder=std::filesystem::absolute(std::filesystem::path(shotPath).parent_path()/("component-example-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));std::filesystem::create_directories(folder);const auto top=folder/"bearing-r1.arktop";
        // Each UI selftest gets an isolated output folder; immutable component
        // publication is exercised through the actual export dialog.
        verify(!std::filesystem::exists(top),"Use a fresh screenshot folder for immutable example output");
        const auto partFile=folder/"mounting-plate-r1.arkprt";
        g_dialogSmoke=3;g_dialogOverrideQueue={{"Mounting plate","1",partFile.string(),"span,thickness","Mounting face",""}};interactiveAction(49001);g_dialogSmoke=0;verify(!g_error&&std::filesystem::exists(partFile),g_status);
        g_dialogSmoke=3;g_dialogOverrideQueue={{"Bearing module","1",top.string(),"span,thickness,radius","Mounting face",""}};interactiveAction(49002);g_dialogSmoke=0;verify(!g_error&&std::filesystem::exists(top),g_status);
        apply(R"({"commands":[{"op":"feature.edit","id":1,"p":{"depth":32}}]})");const auto nextTop=folder/"bearing-r2.arktop";
        g_dialogSmoke=3;g_dialogOverrideQueue={{"Bearing module","1",nextTop.string(),"span,thickness,radius","Mounting face",top.string()}};interactiveAction(49002);g_dialogSmoke=0;verify(!g_error&&std::filesystem::exists(nextTop),g_status);
        g_session=AgentSession{};++g_documentEpoch;rebuildScene();
        g_dialogSmoke=3;g_dialogOverrideQueue={{"bearing",top.string(),"{\"span\":56}"}};interactiveAction(49003);g_dialogSmoke=0;verify(!g_error&&g_doc.parametricComponents.arr.size()==1,g_status);verify(g_doc.getParam("bearing__span")==56,"Native insert did not preserve public override");
        const auto saved=folder/"bearing-project.ark";verify(saveDocument(g_doc,saved.string().c_str()),"Portable example save failed");
        interactiveAction(49000);verify(state.window&&IsWindow(state.window),"Graph action failed");verify(!state.nodes.empty()&&!state.edges.empty(),"Empty graph for authored assembly");capture(".overview.png");
        const auto revision=g_session.revision;const auto source=documentToJson(g_doc,false,false);size_t parameter=state.nodes.size(),part=state.nodes.size(),group=state.nodes.size();for(size_t i=0;i<state.nodes.size();++i){if(state.nodes[i].key=="params:bearing__span")parameter=i;if(state.nodes[i].feature==1)part=i;if(state.nodes[i].assembly==1)group=i;}
        verify(parameter<state.nodes.size()&&part<state.nodes.size()&&group<state.nodes.size(),"Required graph nodes missing");select(parameter);verify(state.impact.count(part),"Parameter selection does not highlight its dependent part");capture(".parameter.png");
        auto r=nodeRect(state.nodes[part]);SendMessage(state.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2));SendMessage(state.window,WM_LBUTTONUP,0,MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2));verify(g_explorerFeature==1&&selBody>=0,"Graph selection did not reach the 3D object");
        const auto all=std::count_if(state.nodes.begin(),state.nodes.end(),[](const Node& n){return n.visible;});r=nodeRect(state.nodes[group]);SendMessage(state.window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2));verify(std::count_if(state.nodes.begin(),state.nodes.end(),[](const Node& n){return n.visible;})<all,"Group did not collapse");capture(".collapsed.png");SendMessage(state.window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2));
        const auto zoom=state.zoom;POINT wheel{450,350};ClientToScreen(state.window,&wheel);SendMessage(state.window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(wheel.x,wheel.y));verify(state.zoom>zoom,"Wheel did not zoom");SendMessage(state.window,WM_KEYDOWN,'F',0);
        SetWindowTextW(state.search,L"span");verify(std::count_if(state.nodes.begin(),state.nodes.end(),[](const Node& n){return n.visible;})<all,"Search did not filter graph");capture(".search.png");SetWindowTextW(state.search,L"");select(part);action(1);verify(state.focus,"Focus did not activate");action(1);
        verify(g_session.revision==revision&&documentToJson(g_doc,false,false)==source,"Graph navigation changed authored source");
        // Real configure form and checked-candidate review.
        g_dialogSmoke=3;g_dialogOverrideQueue={{"bearing","","{\"span\":62}"}};interactiveAction(49005);g_dialogSmoke=0;verify(!g_error&&g_doc.getParam("bearing__span")==62,g_status);SendMessage(state.window,WM_TIMER,1,0);verify(state.revision==g_session.revision,"Graph did not refresh after edit");capture(".configured.png");
        // Cancelling the native form must preserve the exact source/revision.
        const auto cancelRevision=g_session.revision;g_dialogSmoke=2;interactiveAction(49005);g_dialogSmoke=0;verify(g_session.revision==cancelRevision,"Cancelled configure changed source");
        g_dialogSmoke=3;g_dialogOverrideQueue={{"bearing",nextTop.string(),"{}"}};interactiveAction(49004);g_dialogSmoke=0;verify(!g_error&&g_doc.features[0].p.at("depth")=="32"&&g_doc.getParam("bearing__span")==62,g_status);refresh();capture(".updated.png");
        SetWindowPos(state.window,nullptr,0,0,1080,680,SWP_NOMOVE|SWP_NOZORDER);fit();capture(".compact.png");
        verify(saveDocument(g_doc,saved.string().c_str()),"Final portable example save failed");DestroyWindow(state.window);fprintf(stdout,"Parametric graph 053B UI: export, insert, configure, selection, folding, search, zoom, focus, refresh and cancel passed\n");return 0;
    }catch(const std::exception& e){g_dialogSmoke=0;g_dialogOverrideQueue.clear();if(state.window)DestroyWindow(state.window);fprintf(stderr,"Parametric graph 053B UI: %s / %s\n",e.what(),g_status.c_str());return 1553;}
}
