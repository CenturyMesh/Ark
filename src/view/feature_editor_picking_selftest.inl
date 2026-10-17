// Exercise real nested editor message loops and the main window's mouse route.
// All fixtures and captures belong to this private headless process.
if(featureEditorPickingUiTest){
    using V=arkjson::Value;std::string failure;
    auto check=[&](bool ok,const char* message){if(!ok&&failure.empty()){failure=message;std::fprintf(stderr,"Feature editor picking UI: %s / %s\n",message,g_status.c_str());std::ofstream log(shotPath+".feature-editor-picking-test.log");log<<"FAIL: "<<message<<"\nStatus: "<<g_status<<"\n";}return ok;};
    auto parse=[](const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid feature-editor fixture");return value;};
    auto fresh=[&]{V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);g_picked.clear();g_explorerFeature=0;g_assistantDockOpen=false;g_teamMode=false;g_pickKind=0;g_sidebarTab=0;g_toolModule=0;g_treeScroll=0;g_activeSketch.clear();layout();};
    auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);};
    auto mouse=[&](int x,int y){SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));};
    auto action=[&](int id){draw();auto found=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==id;});if(found==g_zones.end())return false;mouse((int)(found->x+found->w*.5f),(int)(found->y+found->h*.5f));return true;};
    auto field=[](HWND dialog,int id){HWND control=editorFieldControl(dialog,id);int size=GetWindowTextLengthA(control);std::vector<char> text(size+1);GetWindowTextA(control,text.data(),size+1);return std::string(text.data());};
    auto bodyClick=[&](int body){draw();const auto& mesh=g_cpuMeshes[g_instancePart[(size_t)body]];for(size_t triangle=0;triangle<mesh.idx.size()/3;++triangle){Vec3 point;for(int corner=0;corner<3;++corner){const size_t at=(size_t)mesh.idx[triangle*3+corner]*3;point=point+Vec3{mesh.pos[at],mesh.pos[at+1],mesh.pos[at+2]};}float x,y;if(!projectScreen(g_instanceWorld[(size_t)body].transformPoint(point*(1./3.)),x,y)||overViewCube(x,y))continue;const int px=(int)std::lround(x),py=(int)std::lround(y);if(pickBody(px,py)!=body)continue;mouse(px,py);return true;}return false;};
    fresh();
    applyCommand(parse(R"({"op":"feature.add","type":"native_solid","name":"Base","p":{"op":"box","width":"80","depth":"60","height":"20"}})"));
    applyCommand(parse(R"({"op":"feature.add","type":"native_solid","name":"Tool","p":{"op":"box","width":"20","depth":"20","height":"30"}})"));
    if(!check(!g_error&&g_bodyIds.size()==2,"Boolean fixture rejected"))return 930;
    frameScene();selBody=1;const u64 base=g_bodyIds[0],tool=g_bodyIds[1],beforeBoolean=g_session.revision;int stage=0;
    g_editorSmokeProbe=[&](HWND dialog){
        if(!failure.empty()){SendMessage(dialog,WM_COMMAND,2,0);return;}
        if(stage==0){check(IsWindowEnabled(g_window),"Viewport owner disabled during live editor");check(bodyClick(0),"Base could not be clicked in viewport");}
        else if(stage==1){check(field(dialog,4)==std::to_string(base),"First viewport click did not replace the initial Boolean base");check(bodyClick(1),"Tool could not be clicked in viewport");}
        else if(stage==2){check(field(dialog,4)==std::to_string(base)&&field(dialog,5)==std::to_string(tool),"Base/tool clicks did not populate separate fields");check(g_session.revision==beforeBoolean,"Picking changed document revision before Apply");g_dialogShot=shotPath+".boolean-base-tool.png";}
        else SendMessage(dialog,WM_COMMAND,failure.empty()?1:2,0);
        ++stage;
    };
    g_dialogSmoke=4;nativeBooleanDialog();g_dialogSmoke=0;g_editorSmokeProbe={};
    if(!check(failure.empty()&&!g_error&&stage==4&&g_session.revision==beforeBoolean+1,"Boolean Apply did not publish once"))return 931;
    if(!check(g_doc.features.back().inputs==std::vector<u64>{base,tool}&&g_doc.features.back().get("op")=="cut","Boolean Apply lost clicked base/tool order"))return 932;
    auto result=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& body){return body.id==g_doc.features.back().id;});
    if(!check(result!=g_doc.evalBodies.end()&&result->nativeData.find("report")&&std::abs(result->nativeData.find("report")->find("volume_mm3")->num-88000)<1e-5,"Boolean cut has wrong exact volume"))return 933;
    fresh();
    applyCommand(parse(R"({"op":"sketch.set","name":"First profile","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[0,0,5]}]}})"));
    applyCommand(parse(R"({"op":"sketch.set","name":"Picked profile","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[20,0,7]}]}})"));
    if(!check(!g_error&&g_doc.sketches.size()==2,"Sketch fixture rejected"))return 934;
    g_activeSketch="First profile";g_picked.clear();const u64 beforeSketch=g_session.revision;stage=0;
    g_editorSmokeProbe=[&](HWND dialog){
        if(!failure.empty()){SendMessage(dialog,WM_COMMAND,2,0);return;}
        if(stage==0){check(action(11001),"Browser sketch row is missing");}
        else if(stage==1){check(field(dialog,3)=="Picked profile","Browser click did not fill the open extrusion editor");check(g_session.revision==beforeSketch,"Sketch selection wrote the document");interactiveAction(100);check(g_dialog==dialog&&!g_dialogDone&&g_session.revision==beforeSketch,"Unrelated authoring action was allowed while editor open");g_dialogShot=shotPath+".browser-sketch.png";}
        else SendMessage(dialog,WM_COMMAND,failure.empty()?1:2,0);
        ++stage;
    };
    g_dialogSmoke=4;nativeExtrudeDialog();g_dialogSmoke=0;g_editorSmokeProbe={};
    if(!check(failure.empty()&&!g_error&&stage==3&&g_session.revision==beforeSketch+1,"Browser extrusion Apply did not publish once"))return 935;
    if(!check(g_doc.features.back().get("sketch")=="Picked profile","Extrusion did not retain browser-selected sketch"))return 936;
    const auto cancelledRevision=g_session.revision;const std::string cancelledDocument=documentToJson(g_doc);stage=0;g_picked.clear();
    g_editorSmokeProbe=[&](HWND dialog){if(stage++==0)check(action(11000),"Cancel test browser row missing");else{check(field(dialog,3)=="First profile","Cancel test did not exercise live field update");SendMessage(dialog,WM_COMMAND,2,0);}};
    g_dialogSmoke=4;nativeExtrudeDialog();g_dialogSmoke=0;g_editorSmokeProbe={};
    if(!check(failure.empty()&&g_session.revision==cancelledRevision&&documentToJson(g_doc)==cancelledDocument,"Cancel changed the authored document"))return 937;
    std::ofstream receipt(shotPath+".feature-editor-picking-test.log");receipt<<"PASS: Boolean base/tool viewport mouse clicks replace wrong initial selection; separate fields and exact cut inputs/volume; browser sketch mouse click fills open extrusion editor; unrelated authoring blocked; atomic Apply; Cancel preserves document.\n";
    g_status="Feature editor picking checks passed / Boolean base and tool / browser sketch / Apply / Cancel";
}
