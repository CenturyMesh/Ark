if(datumAuthoringUiTest){
    using V=arkjson::Value;
    auto failure=[&](int code,const std::string& message){std::ofstream log(shotPath+".datum-ui-test.log");log<<message<<"\nStatus: "<<g_status<<"\nSelected: "<<g_selectedDatumName<<"\nRevision: "<<g_session.revision<<'\n';fprintf(stderr,"Datum UI test %d: %s / %s\n",code,message.c_str(),g_status.c_str());return code;};
    auto parse=[](const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid datum UI fixture");return value;};
    auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);glFinish();};
    auto capture=[&](const std::string& suffix){draw();Image shot;shot.w=winW;shot.h=winH;shot.px.resize((size_t)winW*winH*3);std::vector<u8> pixels(shot.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());for(int y=0;y<winH;++y)std::copy_n(pixels.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,shot.px.begin()+(size_t)y*winW*3);return writePNG((shotPath+suffix).c_str(),shot);};
    auto click=[&](int action){draw();auto found=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;});if(found==g_zones.end())return false;int x=(int)(found->x+found->w*.5f),y=(int)(found->y+found->h*.5f);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
    V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);
    args=parse(R"({"commands":[{"op":"plane.set","name":"Offset","kind":"plane","support":{"kind":"offset_plane","plane":"XY","offset":-20}},{"op":"plane.set","name":"CenterAxis","kind":"axis","support":{"kind":"axis_points","start":[0,0,0],"end":[0,0,50]}},{"op":"plane.set","name":"LinkedPoint","kind":"point","support":{"kind":"point","point":[30,20,10]}}]})");args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);
    if(g_error||g_doc.planes.size()!=3)return failure(850,"Datum fixture rejected");
    g_assistantDockOpen=false;g_teamMode=false;g_showViewCube=false;g_toolModule=2;g_explorerOpen[3]=true;g_treeScroll=0;g_showSketches=false;g_activeSketch.clear();g_hiddenDatumNames.clear();g_showDatumPlanes=true;g_pickKind=4;g_cameraOrtho=true;cameraRoll=0;tYaw=.65;tPitch=.5;tTarget={0,0,0};tDist=180;layout();rebuildSketchOverlay();draw();
    const u64 revision=g_session.revision;const auto original=documentToJson(g_doc);
    if(!click(14000)||g_selectedDatumName!="Offset")return failure(851,"Tree datum row did not select through mouse click");
    if(!click(32000)||datumVisible("Offset")||g_session.revision!=revision)return failure(852,"Tree eye did not hide one datum without model mutation");
    if(!capture(".datum-hidden.png"))return failure(853,"Hidden datum capture failed");
    if(!click(32000)||!datumVisible("Offset"))return failure(854,"Tree eye did not show datum again");
    for(size_t index=0;index<g_doc.planes.size();++index){
        g_hiddenDatumNames.clear();for(const char* name:{"Origin","XY","XZ","YZ"})g_hiddenDatumNames.insert(name);for(size_t other=0;other<g_doc.planes.size();++other)if(other!=index)g_hiddenDatumNames.insert(g_doc.planes[other].name);
        g_selectedDatumName.clear();g_picked.clear();g_pickKind=4;rebuildSketchOverlay();draw();
        const auto frame=resolvePlane(g_doc,g_doc.planes[index].name);Vec3 probe=frame.transformPoint(index==1?Vec3{0,0,12}:Vec3{});float x,y;if(!projectScreen(probe,x,y))return failure(855,"Datum probe cannot project to viewport");
        SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((int)std::lround(x),(int)std::lround(y)));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM((int)std::lround(x),(int)std::lround(y)));
        if(g_selectedDatumName!=g_doc.planes[index].name||g_picked.empty()||g_picked.back().kind!=g_doc.planes[index].kind)return failure(856,"Viewport mouse did not preserve datum type/name");
        if(g_doc.planes[index].kind!="plane"&&g_activePlane!="Offset")return failure(857,"Point/axis selection replaced the active sketch plane");
    }
    uiAction(642);if(!capture(".datums-selected.png")||g_session.revision!=revision||documentToJson(g_doc)!=original)return failure(858,"Visibility/selection changed authoring or capture failed");
    g_dialogOverrideQueue={{"UI offset","XY","25"}};g_dialogSmoke=3;interactiveAction(630);g_dialogSmoke=0;
    if(g_error||g_session.revision!=revision+1||!resolvePlane(g_doc,"UI offset").transformPoint({}).approx({0,0,25},1e-8))return failure(859,"Construction dialog did not publish offset plane atomically");
    g_dialogSmoke=2;g_dialogShot=shotPath+".datum-point-dialog.png";interactiveAction(637);g_dialogSmoke=0;
    if(g_error||!capture(".datums-authored.png"))return failure(860,"Point dialog/control capture failed");
    V save=V::makeObj();save.set("path",V::makeStr(shotPath+".datum-view-reset.arkjson"));runTool("doc_save",save);if(g_error)return failure(861,"View-reset fixture could not save");
    g_hiddenDatumNames.insert("LinkedPoint");g_selectedDatumName="LinkedPoint";g_activePlane="UI offset";
    V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);
    if(g_error||!g_hiddenDatumNames.empty()||!g_selectedDatumName.empty()||g_activePlane!="XY")return failure(862,"New document retained old datum visibility/selection/plane");
    g_hiddenDatumNames.insert("LinkedPoint");g_selectedDatumName="LinkedPoint";runTool("doc_load",save);
    if(g_error||!g_hiddenDatumNames.empty()||!g_selectedDatumName.empty()||!datumVisible("LinkedPoint"))return failure(863,"Load reused stale same-name datum view state");
    // Synthetic retained source only: Origin was a legal authored plane before
    // the immutable starting point was introduced. Exercise the real load/UI
    // path without reading or changing a user's drawing.
    Document legacy;DatumPlane legacyOrigin;legacyOrigin.name="Origin";legacyOrigin.p={{"ox","12"},{"oy","-4"},{"oz","6"},{"rz","90"}};legacy.planes.push_back(legacyOrigin);
    DatumPlane legacyChild;legacyChild.name="LegacyChild";legacyChild.parent="Origin";legacyChild.p={{"ox","2"},{"oz","3"}};legacy.planes.push_back(legacyChild);
    Sketch legacySketch;legacySketch.plane="Origin";SketchEnt legacyPoint;legacyPoint.name="RetainedPoint";legacyPoint.kind=SketchEnt::Pnt;legacyPoint.dof={1,2};legacySketch.ents.push_back(legacyPoint);legacy.setSketch("LegacySketch",legacySketch);legacy.recompute();
    const std::string legacyPath=shotPath+".legacy-origin.arkjson";
    {std::ofstream fixture(legacyPath);fixture<<documentToJson(legacy,false,false);if(!fixture)return failure(864,"Synthetic legacy Origin fixture could not save");}
    V legacyLoad=V::makeObj();legacyLoad.set("path",V::makeStr(legacyPath));runTool("doc_load",legacyLoad);
    auto retainedFrame=[&](Vec3 position){const auto frame=resolvePlane(g_doc,"Origin");return frame.transformPoint({}).approx(position,1e-8)&&frame.transformDir({1,0,0}).approx({0,1,0},1e-8);};
    if(g_error||!retainedFrame({12,-4,6})||!resolvePlane(g_doc,"LegacyChild").transformPoint({}).approx({12,-2,9},1e-8)||!g_doc.findSketch("LegacySketch")||g_doc.findSketch("LegacySketch")->sk.plane!="Origin")return failure(865,"Legacy Origin load rebound authored geometry or dependencies");
    const u64 legacyRevision=g_session.revision;const auto legacyOriginal=documentToJson(g_doc);
    g_explorerOpen[3]=true;g_treeScroll=0;g_showSketches=false;g_pickKind=4;tTarget={12,-4,6};tDist=180;layout();draw();
    auto zoneCount=[&](int action){return std::count_if(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;});};
    if(zoneCount(14900)||zoneCount(32300)||zoneCount(14000)!=1||zoneCount(32000)!=1)return failure(866,"Legacy Origin tree exposes duplicate authored/builtin rows or eyes");
    if(builtinDatumName("Origin")||!datumPlaneName("Origin")||!click(14000)||g_selectedDatumName!="Origin"||g_activePlane!="Origin"||g_picked.empty()||g_picked.back().kind!="plane")return failure(867,"Legacy Origin tree selection lost authored plane identity");
    if(!click(32000)||datumVisible("Origin")||!click(32000)||!datumVisible("Origin"))return failure(868,"Legacy Origin eye did not toggle its authored row");
    g_hiddenDatumNames={"XY","XZ","YZ","LegacyChild"};rebuildSketchOverlay();draw();
    std::vector<float> expectedLines,expectedFill;datumDisplayGeometry(legacyOrigin,resolvePlane(legacy,"Origin"),expectedLines,&expectedFill);
    if(g_datumLines!=expectedLines||g_datumFill!=expectedFill)return failure(869,"Legacy Origin overlay contains a builtin duplicate or changed transform");
    g_selectedDatumName.clear();g_picked.clear();g_activePlane="XY";rebuildSketchOverlay();draw();float legacyX,legacyY;
    if(!projectScreen({12,-4,6},legacyX,legacyY))return failure(870,"Legacy Origin cannot project to viewport");
    SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((int)std::lround(legacyX),(int)std::lround(legacyY)));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM((int)std::lround(legacyX),(int)std::lround(legacyY)));
    if(g_selectedDatumName!="Origin"||g_activePlane!="Origin"||g_picked.empty()||g_picked.back().kind!="plane")return failure(871,"Legacy Origin viewport picking selected the implicit point");
    g_activePlane="XY";g_dialogOverrideQueue={{"Origin"}};g_dialogSmoke=3;interactiveAction(69);g_dialogSmoke=0;
    if(g_error||g_activePlane!="Origin"||g_session.revision!=legacyRevision||documentToJson(g_doc)!=legacyOriginal||!retainedFrame({12,-4,6}))return failure(872,"Select construction plane rejected or rebound legacy Origin");
    if(!capture(".legacy-origin-selected.png"))return failure(873,"Legacy Origin selection capture failed");
    g_dialogOverrideQueue={{"Origin","XY","20","-4","6","0","0","90","plane"}};g_dialogSmoke=3;g_dialogShot=shotPath+".legacy-origin-edit-dialog.png";interactiveAction(60);g_dialogSmoke=0;
    if(g_error||g_session.revision!=legacyRevision+1||g_activePlane!="Origin"||!retainedFrame({20,-4,6})||!resolvePlane(g_doc,"LegacyChild").transformPoint({}).approx({20,-2,9},1e-8)||g_doc.findSketch("LegacySketch")->sk.plane!="Origin")return failure(874,"Legacy Origin construction dialog did not edit retained authoring/dependencies");
    if(!capture(".legacy-origin-edited.png"))return failure(875,"Legacy Origin edit capture failed");
    runTool("doc_undo",V::makeObj());
    if(g_error||g_activePlane!="Origin"||!retainedFrame({12,-4,6})||!resolvePlane(g_doc,"LegacyChild").transformPoint({}).approx({12,-2,9},1e-8))return failure(876,"Legacy Origin UI edit undo lost retained frame or active plane");
    std::ofstream log(shotPath+".datum-ui-test.log");log<<"PASS: actual tree row/eye clicks; independent show/hide without geometry revisions; actual viewport plane quad/axis/point picking retains names/kinds; point/axis preserve active plane; offset dialog atomic authoring; document new/load reset stale named view state; synthetic legacy Origin load retains transformed plane, child and sketch; no duplicate builtin row/eye/overlay; actual tree/viewport/plane-dialog selection; retained Origin construction-dialog edit and undo; screenshots captured.\n";
    g_status="Construction datum controls passed";
}
