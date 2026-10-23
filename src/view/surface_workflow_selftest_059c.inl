// Native forms and exact synthetic geometry; no private document or provider.
static int surfaceWorkflowSelfTest059(HDC,const std::string& output){
    using V=arkjson::Value;using namespace surface_workflow_059c;
    V receipt=V::makeObj();int checks=0;std::string stage="fixture";
    receipt.set("schema",V::makeStr("ark.surface-workflow.059c.v1"));
    auto check=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    auto parse=[&](const char* text){V result;check(arkjson::parse(text,result),"Invalid surface UI fixture JSON");return result;};
    auto persist=[&]{receipt.set("checks",V::makeNum(checks));receipt.set("stage",V::makeStr(stage));std::ofstream file(output+".surface-workflow.json");file<<receipt.dump()<<'\n';};
    auto clearSmoke=[&]{g_dialogSmoke=0;g_dialogOverrides.clear();g_dialogOverrideQueue.clear();g_dialogShot.clear();g_editorSmokeProbe={};beforePublishTest={};};
    auto command=[&](V cmd){applyCommand(cmd);check(!g_error,"Synthetic surface command failed");};
    auto form=[&](int id,std::vector<std::vector<std::string>> rows,const char* suffix,const Feature* editing=nullptr){
        g_error=false;g_dialogSmoke=3;for(auto& row:rows)g_dialogOverrideQueue.push_back(std::move(row));
        if(suffix)g_dialogShot=output+suffix;
        bool handled=editing?surfaceWorkflowEdit059(editing):surfaceWorkflowAction059(id);
        check(handled,"Surface dialog action was not handled");check(g_dialogOverrideQueue.empty(),"Surface form did not consume expected stages");g_dialogSmoke=0;
        if(g_error)throw std::runtime_error("Surface apply failed: "+g_status);
        if(suffix)check(std::filesystem::is_regular_file(output+suffix),"Surface form capture missing");
    };
    auto report=[&](u64 id)->V {for(const auto& b:g_doc.evalBodies)if(b.id==id&&b.nativeData.find("report"))return *b.nativeData.find("report");throw std::runtime_error("Exact surface report missing");};
    auto close=[&](double actual,double expected,const char* why){check(std::abs(actual-expected)<1e-5,why);};
    auto rejected=[&](auto operation,const char* why){const auto before=documentToJson(g_doc);const auto revision=g_session.revision;bool denied=false;try{operation();denied=g_error;}catch(const std::exception&){denied=true;}clearSmoke();g_error=false;check(denied,why);check(documentToJson(g_doc)==before&&g_session.revision==revision,"Rejected surface edit changed the document");};
    auto image=[&](const char* suffix){target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;const auto pixels=captureGpuScene059();check(writePNG((output+suffix).c_str(),pixels),"Exact surface viewport capture failed");};
    try{
        V args=V::makeObj();args.set("expected_revision",V::makeNum(g_session.revision));runTool("doc_new",args);check(!g_error,"Surface UI reset failed");
        args=parse(R"JSON({"commands":[
          {"op":"feature.add","type":"bspline_surface","name":"Plane","p":{"u_degree":1,"v_degree":1},"native":{"poles":[[[0,0,0],[0,10,0]],[[10,0,0],[10,10,0]]]}},
          {"op":"feature.add","type":"bspline_surface","name":"Cutter","p":{"u_degree":1,"v_degree":1},"native":{"poles":[[[4,-1,-1],[4,11,-1]],[[4,-1,1],[4,11,1]]]}},
          {"op":"feature.add","type":"curve","name":"Rail A","native":{"start":[0,0,2],"segments":[{"kind":"line","to":[10,0,2]}]}},
          {"op":"feature.add","type":"curve","name":"Rail B","native":{"start":[0,10,2],"segments":[{"kind":"line","to":[10,10,2]}]}},
          {"op":"feature.add","type":"curve","name":"Loft guide","native":{"start":[10,0,2],"segments":[{"kind":"line","to":[10,10,2]}]}},
          {"op":"feature.add","type":"curve","name":"Trim wire","native":{"start":[4,-1,0],"segments":[{"kind":"line","to":[4,11,0]}]}},
          {"op":"feature.add","type":"curve","name":"Closed 3D boundary","native":{"start":[0,0,0],"segments":[{"kind":"line","to":[10,0,0]},{"kind":"line","to":[10,10,0]},{"kind":"line","to":[0,10,0]},{"kind":"line","to":[0,0,0]}]}},
          {"op":"feature.add","type":"curve","name":"Patch interior guide","native":{"start":[5,2,0],"segments":[{"kind":"line","to":[5,8,0]}]}},
          {"op":"feature.add","type":"bspline_surface","name":"Alternate plane","p":{"u_degree":1,"v_degree":1},"native":{"poles":[[[0,0,3],[0,10,3]],[[12,0,3],[12,10,3]]]}},
          {"op":"sketch.set","name":"Rectangle","sketch":{"plane":"XY","entities":[{"kind":"line","name":"south","dof":[0,0,10,0]},{"kind":"line","name":"east","dof":[10,0,10,10]},{"kind":"line","name":"north","dof":[10,10,0,10]},{"kind":"line","name":"west","dof":[0,10,0,0]}]}},
          {"op":"sketch.set","name":"Ring","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"circle","dof":[0,0,4]}]}},
          {"op":"plane.set","name":"Tip","kind":"point","support":{"kind":"point","point":[0,0,8]}}
        ]})JSON");args.set("expected_revision",V::makeNum(g_session.revision));runTool("doc_apply",args);if(g_error)throw std::runtime_error("Surface fixture: "+g_status);
        g_assistantDockOpen=false;g_chatExpanded=false;g_toolsOpen=false;g_showSketches=false;g_showDatumPlanes=false;g_showEdges=true;g_picked.clear();layout();
        std::string supportFace;for(const auto& body:g_doc.evalBodies)if(body.id==1)for(const auto& ref:body.nativeData.find("topology")->find("references")->arr)if(value(ref,"kind")=="face"&&value(ref,"status")=="bound"&&ref.find("indices")->arr.size()==1){supportFace=value(ref,"name");break;}
        check(!supportFace.empty(),"Synthetic support has no exact semantic face");receipt.set("support_face",V::makeStr(supportFace));

        stage="cancel all main forms";
        for(int id=49400;id<=49404;++id){const auto before=documentToJson(g_doc);const auto revision=g_session.revision;g_dialogSmoke=2;check(surfaceWorkflowAction059(id),"Cancel action absent");clearSmoke();check(before==documentToJson(g_doc)&&revision==g_session.revision,"Cancelled main form changed document");}

        stage="mirror";form(49400,{{"Symmetry","1","XZ","0,0,0","0,1,0","G1"}},".mirror-form.png");
        const u64 mirrorId=g_doc.features.back().id;close(report(mirrorId).find("area_mm2")->num,200,"Mirror did not double planar area");
        check(g_doc.features.back().nativeSpec.find("plane")->str=="XZ","Default XZ mirror plane lost");
        auto edit=parse(R"({"op":"feature.edit","native":{"op":"mirror","plane":"XZ","continuity":"G1","tolerance":0.000001}})");edit.set("id",V::makeNum(mirrorId));command(edit);
        form(49400,{{"Edited symmetry","9","","0,0,0","0,1,0","G1"}},nullptr,g_doc.find(mirrorId));
        const auto* mirrored=g_doc.find(mirrorId);check(mirrored->name=="Edited symmetry"&&mirrored->inputs==std::vector<u64>{9},"Mirror edit lost name or changed source dependency");
        check(mirrored->nativeSpec.find("tolerance")&&!mirrored->nativeSpec.find("plane")&&mirrored->nativeSpec.find("origin"),"Mirror edit lost tolerance or retained conflicting plane");close(report(mirrorId).find("area_mm2")->num,240,"Mirror source edit did not recompute geometry");
        rejected([&]{form(49400,{{"Self source",std::to_string(mirrorId),"XZ","0,0,0","0,1,0","G0"}},nullptr,g_doc.find(mirrorId));},"Mirror self input accepted");
        rejected([&]{form(49400,{{"Bad plane","1","","0,0,0","0,0,0","G0"}},nullptr);},"Zero mirror normal accepted");
        rejected([&]{form(49400,{{"Bad coordinates","1","","0,,0,0","0,1,0","G0"}},nullptr);},"Missing coordinate accepted");

        stage="trim";form(49401,{{"Surface cut","1","2","surface","8,5,0"}},".trim-form.png");close(report(g_doc.features.back().id).find("retained_area_mm2")->num,60,"Surface cutter retained wrong region");
        form(49401,{{"Curve cut","1","6","curve","1,5,0"}},nullptr);close(report(g_doc.features.back().id).find("retained_area_mm2")->num,40,"3D curve cutter retained wrong region");
        rejected([&]{form(49401,{{"Ambiguous cut","1","2","surface","4,5,0"}},nullptr);},"Trim accepted a seed on the cutter");

        stage="ruled";form(49402,{{"Exact rails","3","4","0"}},".ruled-form.png");close(report(g_doc.features.back().id).find("area_mm2")->num,100,"Ruled surface area wrong");
        rejected([&]{form(49402,{{"Bad rails","3","3","0"}},nullptr);},"Duplicate ruled rails accepted");

        stage="loft curves and guides";
        const std::vector<std::string> loftValues{"Guided 3D loft","Rail A; Rail B","Loft guide","0","0","0","0","3","1","17","0.000001","1"};
        form(49403,{loftValues},".loft-form.png");const u64 loftId=g_doc.features.back().id;
        check(g_doc.find(loftId)->inputs==std::vector<u64>({3,4,5}),"Loft exact curve and guide dependencies missing");check(report(loftId).find("guide_contact_order_checked")->b,"Loft ordered guide evidence missing");
        auto loftNative=g_doc.find(loftId)->nativeSpec;auto controls=*loftNative.find("controls");const auto alignment=parse(R"([{"reverse":false},{"reverse":false}])");controls.set("section_alignment",alignment);loftNative.set("controls",controls);edit=V::makeObj();edit.set("op",V::makeStr("feature.edit"));edit.set("id",V::makeNum(loftId));edit.set("native",loftNative);command(edit);
        auto expressionEdit=parse(R"({"op":"feature.edit","p":{"solid":"1-1","smooth":"0+0"}})");expressionEdit.set("id",V::makeNum(loftId));command(expressionEdit);
        form(49403,{loftValues},nullptr,g_doc.find(loftId));check(g_doc.find(loftId)->nativeSpec.find("controls")->find("section_alignment")->dump()==alignment.dump(),"Loft edit discarded section alignment");check(g_doc.find(loftId)->get("solid")=="1-1"&&g_doc.find(loftId)->get("smooth")=="0+0","Unchanged loft checkboxes discarded parameter expressions");
        auto optionalValues=loftValues;optionalValues[7]="";optionalValues[8]="";form(49403,{optionalValues},nullptr,g_doc.find(loftId));check(!g_doc.find(loftId)->p.count("max_degree")&&!g_doc.find(loftId)->p.count("continuity"),"Loft optional controls were not removed");
        form(49403,{{"Sketch to tip","Ring; Tip","","0","0","0","0","","","17","0.05","1"}},nullptr);check(g_doc.features.back().nativeSpec.find("sections")->arr[1].str=="Tip","Datum point loft tip lost");
        rejected([&]{form(49403,{{"Missing profile","Rail A; absent","","0","0","0","0","","","17","0.05","1"}},nullptr);},"Missing loft profile accepted");

        stage="fill cancellation";const auto beforeCancel=documentToJson(g_doc);const auto cancelRevision=g_session.revision;int forms=0;
        g_editorSmokeProbe=[&](HWND){if(++forms==2)g_dialogSmoke=2;};
        form(49404,{{"Cancelled support","sketch","Rectangle","fill","1","33","0.001","0.0001",""},{"0","1",supportFace,"G1"}},".fill-form.png");clearSmoke();check(forms==2&&beforeCancel==documentToJson(g_doc)&&cancelRevision==g_session.revision,"Cancelling a support form published a partial patch");

        stage="G1/G2 fill";int supportForms=0;g_editorSmokeProbe=[&](HWND){if(++supportForms==2)g_dialogShot=output+".support-form.png";};
        form(49404,{{"G1 support patch","sketch","Rectangle","fill","1","33","0.001","0.0001",""},{"0","1",supportFace,"G1"}},nullptr);clearSmoke();const auto fillId=g_doc.features.back().id;
        check(g_doc.find(fillId)->inputs==std::vector<u64>{1},"Fill support dependency missing");check(report(fillId).find("support_boundary_evidence")->arr.size()==1,"G1 fill lacks measured support evidence");
        form(49404,{{"G2 support patch","sketch","Rectangle","fill","1","33","0.001","0.0001",""},{"0","1",supportFace,"G2"}},nullptr,g_doc.find(fillId));check(g_doc.find(fillId)->nativeSpec.find("constraints")->arr[0].find("continuity")->num==2,"G2 support edit lost continuity");
        rejected([&]{form(49404,{{"Bad reference","sketch","Rectangle","fill","1","33","0.001","0.0001",""},{"0","1","missing-face","G1"}},nullptr);},"Unknown support face accepted");

        stage="3D boundary and interior guides";form(49404,{{"3D wire patch","curve","7","fill","0","33","0.001","0.0001","8"}},".guided-patch-form.png");const auto guidedId=g_doc.features.back().id;
        check(g_doc.find(guidedId)->inputs==std::vector<u64>({7,8}),"3D patch lost boundary/guide dependencies");check(g_doc.find(guidedId)->nativeSpec.find("guide_curves")->arr[0].num==8,"Patch guide recipe missing");
        check(report(guidedId).find("guide_curve_evidence")!=nullptr,"Interior guide verification evidence missing");
        form(49404,{{"3D wire patch edited","curve","7","fill","0","33","0.001","0.0001","8"}},nullptr,g_doc.find(guidedId));check(g_doc.find(guidedId)->nativeSpec.find("boundary_curve")->num==7,"Patch edit dropped closed 3D boundary");
        form(49404,{{"Supported guided 3D patch","curve","7","fill","1","33","0.001","0.0001","8"},{"0","1",supportFace,"G1"}},nullptr,g_doc.find(fillId));check(!g_doc.find(fillId)->p.count("sketch")&&g_doc.find(fillId)->inputs==std::vector<u64>({7,8,1}),"Patch boundary conversion retained sketch or lost dependencies");
        rejected([&]{form(49404,{{"Repeated guide","curve","7","fill","0","33","0.001","0.0001","8;8"}},nullptr);},"Repeated patch guide accepted");

        stage="revision and epoch guards";const auto count=g_doc.features.size();const auto initialRevision=g_session.revision;
        beforePublishTest=[&]{command(parse(R"({"op":"param.set","name":"ConcurrentSurfaceFixture","expr":1})"));};bool denied=false;
        try{form(49402,{{"Stale rails","3","4","0"}},nullptr);}catch(const std::exception&){denied=true;}clearSmoke();
        check(denied&&g_session.revision==initialRevision+1&&g_doc.features.size()==count,"Stale surface form overwrote a concurrent publication");
        const auto beforeEpoch=documentToJson(g_doc);const auto revision=g_session.revision;beforePublishTest=[&]{++g_documentEpoch;};denied=false;
        try{form(49402,{{"Old document rails","3","4","0"}},nullptr);}catch(const std::exception&){denied=true;}clearSmoke();
        check(denied&&g_session.revision==revision&&documentToJson(g_doc)==beforeEpoch,"Surface form crossed document epoch");
        check(!surfaceWorkflowAction059(49399)&&!surfaceWorkflowAction059(49500),"Surface dispatcher shadowed another module");

        stage="capture and persistence";frameScene();image(".surfaces.png");
        const auto document=documentToJson(g_doc);Document restored;check(documentFromJson(document,restored),"Surface document did not deserialize");restored.recompute();check(documentToJson(restored)==document,"Surface recipes changed during document round trip");
        receipt.set("native_feature_count",V::makeNum(g_doc.features.size()));receipt.set("revision",V::makeNum(g_session.revision));receipt.set("passed",V::makeBool(true));persist();return 0;
    }catch(const std::exception& error){clearSmoke();receipt.set("passed",V::makeBool(false));receipt.set("error",V::makeStr(error.what()));receipt.set("status",V::makeStr(g_status));persist();std::fprintf(stderr,"Surface workflow selftest [%s]: %s / %s\n",stage.c_str(),error.what(),g_status.c_str());return 959;}
}
