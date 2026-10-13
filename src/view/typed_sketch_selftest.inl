if(typedSketchUiTest){
    using V=arkjson::Value;
    auto failure=[&](int code,const char* message){fprintf(stderr,"Typed authoring UI %d: %s / %s\n",code,message,g_status.c_str());return code;};
    V args=V::makeObj();args.set("expected_revision",V::makeNum(g_session.revision));runTool("doc_new",args);g_assistantDockOpen=false;g_showViewCube=false;g_toolModule=2;layout();
    auto dialog=[&](int action,std::vector<std::string> values){g_dialogOverrideQueue.push_back(std::move(values));g_dialogSmoke=3;interactiveAction(action);g_dialogSmoke=0;return !g_error&&g_dialogOverrideQueue.empty();};
    if(!dialog(661,{"Profile","XY","Section","0","0,0","30,20"}))return failure(880,"Center rectangle dialog failed");
    auto* profile=g_doc.findSketch("Profile");if(!profile||profile->sk.ents.size()!=1||profile->sk.ents[0].kind!=SketchEnt::Recipe||sketchRegionMesh(profile->sk,Mat4{}).idx.empty())return failure(881,"Typed closed profile did not shade");
    V param=V::makeObj();param.set("op",V::makeStr("param.set"));param.set("name",V::makeStr("Width"));param.set("expr",V::makeStr("80"));applyCommand(param);
    g_dialogOverrideQueue.push_back({"Profile","XY","Section","0","0,0","Width/2,20"});g_dialogSmoke=3;editSketchRecipeFields("center_rectangle",&g_doc.findSketch("Profile")->sk.ents[0]);g_dialogSmoke=0;
    profile=g_doc.findSketch("Profile");if(g_error||std::abs(profile->sk.ents[0].dof[0]+40)>1e-7||profile->sk.ents[0].entityRecipe.dump().find("Width/2")==std::string::npos)return failure(882,"Typed source edit lost parameter binding");
    if(!dialog(661,{"End","XY","Section","0","0,0","10,10"}))return failure(883,"Second section dialog failed");
    if(!dialog(683,{"Path","0,0,0; 0,0,50; 0,0,100"}))return failure(884,"Spatial spline dialog failed");
    if(!dialog(684,{"Variable sweep","Path","0, Profile, 0; 1, End, 0","1"}))return failure(885,"Varying sweep dialog failed");
    const auto* sweep=g_doc.find(g_doc.features.back().id);if(!sweep||sweep->type!=FeatureType::Sweep||!sweep->nativeSpec.find("sections"))return failure(886,"Varying section source not retained");
    const Feature sweepBefore=*sweep;g_dialogOverrideQueue.push_back({"Variable sweep","Path","0, Profile, 0; 1, End, 15","1"});g_dialogSmoke=3;varyingSweepEntityDialog(&sweepBefore);g_dialogSmoke=0;if(g_error)return failure(887,"Varying sweep typed edit failed");
    if(!dialog(685,{"Polynomial","0,1","0,0,0.01","0","0","60"}))return failure(888,"Exact polynomial dialog failed");
    const Feature polynomial=*g_doc.find(g_doc.features.back().id);g_dialogOverrideQueue.push_back({"Polynomial","0,1","0,0,0.02","0","0","60"});g_dialogSmoke=3;exactPolynomialEquationDialog(&polynomial);g_dialogSmoke=0;if(g_error||!g_doc.find(polynomial.id)->nativeSpec.find("analytic"))return failure(889,"Analytic source was lost while editing");
    const Feature path=*std::find_if(g_doc.features.begin(),g_doc.features.end(),[](const Feature& feature){return feature.name=="Path";});g_dialogOverrideQueue.push_back({"Path","0,0,0; 0,0,55; 0,0,100"});g_dialogSmoke=3;spatialSplineEntityDialog(&path);g_dialogSmoke=0;if(g_error||g_doc.find(path.id)->nativeSpec.find("segments")->arr[0].find("kind")->str!="spline")return failure(890,"Spline source was lost while editing");
    std::string error;Document restored;if(!documentFromJson(documentToJson(g_doc,false,false),restored,&error)||!restored.find(polynomial.id)->nativeSpec.find("analytic")||!restored.findSketch("Profile")->sk.ents[0].entityRecipe.find("corner"))return failure(891,"Typed source roundtrip failed");
    g_activeSketch.clear();g_activePlane="XY";g_showSketches=false;g_showEdges=true;g_showDatumPlanes=true;uiAction(24);frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_status="Typed authoring UI passed / editable recipes, spline, varying sweep and exact polynomial";
}
