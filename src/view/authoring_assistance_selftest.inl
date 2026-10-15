if(authoringAssistanceUiTest){
    using V=arkjson::Value;
    auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    auto parse=[&](const char* source){V result;check(arkjson::parse(source,result),"Invalid authoring assistance fixture JSON");return result;};
    auto invoke=[&](const char* tool,V args){args.set("expected_revision",V::makeNum((double)g_session.revision));auto result=runTool(tool,args);if(g_error)throw std::runtime_error(g_status);return *result.find("structuredContent");};
    auto resetDialogs=[](){g_dialogSmoke=0;g_dialogOverrides.clear();g_dialogOverrideQueue.clear();g_editorSmokeProbe={};g_dialogShot.clear();};
    try{
        invoke("doc_new",V::makeObj());g_assistantDockOpen=false;layout();
        invoke("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"width","expr":"10"},{"op":"param.set","name":"height","expr":"6"}]})"));
        const auto candidate=parse(R"({"plane":"XY","entities":[{"kind":"point","name":"mount","dof":[4,"height/2"]}],"constraints":[{"id":"left","type":"fixedX","a":0,"val":0},{"id":"right","type":"fixedX","a":0,"val":"width"}]})");
        const auto original=documentToJson(g_doc);const auto originalRevision=g_session.revision;
        int dialogs=0;g_dialogSmoke=3;g_dialogOverrideQueue={{"Remove #1 / left"}};
        g_editorSmokeProbe=[&](HWND){++dialogs;g_dialogSmoke=2;g_dialogShot=shotPath+".sketch-cancel-choice.png";};
        check(!reviewSketchRepair("Reviewed fixture",candidate),"First repair Cancel unexpectedly accepted");resetDialogs();
        check(dialogs==1&&g_session.revision==originalRevision&&documentToJson(g_doc)==original,"First repair Cancel changed source or revision");
        dialogs=0;g_dialogSmoke=3;g_dialogOverrideQueue={{"Remove #1 / left"},{}};
        g_editorSmokeProbe=[&](HWND){++dialogs;g_dialogSmoke=dialogs==2?2:3;g_dialogShot=shotPath+(dialogs==1?".sketch-choice.png":".sketch-cancel-preview.png");};
        check(!reviewSketchRepair("Reviewed fixture",candidate),"Preview repair Cancel unexpectedly accepted");resetDialogs();
        check(dialogs==2&&g_session.revision==originalRevision&&documentToJson(g_doc)==original,"Second repair Cancel published its prepared candidate");
        // A real intervening session transaction is injected while the second
        // modal form is open. The reviewed candidate must refuse publication.
        dialogs=0;g_dialogSmoke=3;g_dialogOverrideQueue={{"Remove #1 / left"},{}};std::string afterInterveningWrite;bool staleRejected=false,interveningSucceeded=false;
        g_editorSmokeProbe=[&](HWND){if(++dialogs==2){V args=parse(R"({"commands":[{"op":"param.set","name":"intervening_review_edit","expr":"1"}]})");args.set("expected_revision",V::makeNum((double)g_session.revision));auto result=g_session.call("doc_apply",args);interveningSucceeded=!result.find("isError")->b;afterInterveningWrite=documentToJson(g_doc);}};
        try{reviewSketchRepair("Reviewed fixture",candidate);}catch(const std::exception& error){staleRejected=std::string(error.what()).find("stale")!=std::string::npos;}resetDialogs();
        check(interveningSucceeded&&staleRejected&&dialogs==2&&g_session.revision==originalRevision+1&&!g_doc.findSketch("Reviewed fixture")&&documentToJson(g_doc)==afterInterveningWrite,"Stale review failed to preserve the intervening source");
        const auto acceptRevision=g_session.revision;dialogs=0;g_dialogSmoke=3;g_dialogOverrideQueue={{"Remove #1 / left"},{}};bool validButtons=true;
        g_editorSmokeProbe=[&](HWND dialog){++dialogs;char button[64]{};GetWindowTextA(GetDlgItem(dialog,1),button,sizeof button);validButtons&=std::string(button)==(dialogs==1?"Preview removal":"Apply repair");g_dialogShot=shotPath+(dialogs==1?".sketch-reviewed-choice.png":".sketch-reviewed-apply.png");};
        check(reviewSketchRepair("Reviewed fixture",candidate),"Explicit repair acceptance failed");resetDialogs();
        const auto* repaired=g_doc.findSketch("Reviewed fixture");check(validButtons&&dialogs==2&&g_session.revision==acceptRevision+1&&repaired&&repaired->error.empty()&&repaired->sk.cons.size()==1&&repaired->sk.cons[0].id=="right"&&repaired->sk.cons[0].expression=="width"&&repaired->sk.ents[0].expressions[1]=="height/2","Accepted repair did not retain the reviewed relation and driven expressions");
        invoke("doc_apply",parse(R"({"commands":[{"op":"feature.add","name":"Surface fixture box","type":"native_solid","native":{"op":"box"},"p":{"width":20,"depth":12,"height":8}},{"op":"feature.add","name":"Original underside","type":"surface_edit","inputs":[1],"native":{"op":"extract_face","face":"z_min"}}]})"));
        auto select=[&](u64 id){selBody=-1;g_picked.clear();for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id){selBody=(int)i;break;}check(selBody>=0,"Expected surface fixture is not displayed");};
        auto normalZ=[&](u64 id){for(const auto& body:g_doc.evalBodies)if(body.id==id){const auto mesh=body.tessellate(g_doc.tessOpt);check(!mesh.nrm.empty(),"Surface has no display normals");double total=0;for(size_t i=2;i<mesh.nrm.size();i+=3)total+=mesh.nrm[i];return total/(mesh.nrm.size()/3);}throw std::runtime_error("Surface result missing");};
        check(normalZ(2)<-.99,"Extracted underside does not point toward -Z");select(2);
        g_dialogSmoke=3;g_dialogOverrideQueue={{"Reviewed surface","2","[0,0,1]"}};g_dialogShot=shotPath+".surface-repair.png";const auto repairRevision=g_session.revision;uiAction(830);resetDialogs();
        check(!g_error&&g_session.revision==repairRevision+1&&g_doc.find(3)&&normalZ(3)>.99,"Surface repair control did not create an exact +Z oriented feature");
        const auto* repairedFeature=g_doc.find(3);check(repairedFeature->type==FeatureType::SurfaceEdit&&repairedFeature->inputs==std::vector<u64>{2},"Surface repair lost editable source dependency");
        const auto beforeReopen=documentToJson(g_doc);const auto reopenRevision=g_session.revision;bool seenRetainedNormal=false;
        select(3);g_explorerFeature=3;
        g_dialogSmoke=2;g_editorSmokeProbe=[&](HWND dialog){char text[64]{};GetWindowTextA(editorFieldControl(dialog,5),text,sizeof text);seenRetainedNormal=std::string(text)=="[0,0,1]";g_dialogShot=shotPath+".surface-edit-reopen.png";};uiAction(161);resetDialogs();
        check(seenRetainedNormal&&g_session.revision==reopenRevision&&documentToJson(g_doc)==beforeReopen,"Feature edit reopen lost normal intent or Cancel changed source");
        // Reopen and explicitly edit the same feature, proving the control is
        // an editable native recipe rather than a one-off generated result.
        g_dialogSmoke=3;g_dialogOverrideQueue={{"Reviewed surface","2","[0,0,-1]"}};uiAction(161);resetDialogs();check(!g_error&&g_session.revision==reopenRevision+1&&normalZ(3)<-.99,"Surface repair feature edit did not recompute explicit normal intent");
        g_dialogSmoke=3;g_dialogOverrideQueue={{"Reviewed surface","2","[0,0,1]"}};uiAction(161);resetDialogs();check(!g_error&&normalZ(3)>.99,"Surface repair failed to restore reviewed +Z intent");
        std::ofstream source(shotPath+".authoring-source.json",std::ios::binary|std::ios::trunc);source<<documentToJson(g_doc)<<'\n';source.flush();check((bool)source,"Could not retain synthetic authoring source evidence");
        frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_status="Authoring assistance checks passed / repair choices / Cancel / stale review / explicit Apply / surface orientation / editable source";
    }catch(const std::exception& error){resetDialogs();fprintf(stderr,"Authoring assistance UI: %s\n",error.what());return 948;}
}
