if(manufacturingUiTest){
    using V=arkjson::Value;
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Manufacturing UI: ")+message);};
    V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum(g_session.revision));runTool("doc_new",fresh);check(!g_error,"new document");
    V source;arkjson::parse(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"CNC synthetic stock","native":{"op":"box"},"p":{"width":40,"depth":30,"height":10}}]})",source);source.set("expected_revision",V::makeNum(g_session.revision));runTool("doc_apply",source);check(!g_error,"exact source");selBody=0;g_toolModule=10;frameScene();
    drawFrame(hdc);for(int id:{49600,49601,49602,49609,49615,49616}){auto zone=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});check(zone!=g_zones.end()&&zone->x>=0&&zone->x+zone->w<=winW,"manufacturing ribbon control missing or clipped");}
    g_dialogSmoke=3;interactiveAction(49601);g_dialogSmoke=0;check(!g_error&&g_doc.manufacturingPlans.arr.size()==1,"plan form did not publish through preview/apply");
    auto& state=g_manufacturing060;check(state.window&&state.list&&state.summary,"modeless manufacturing controls missing");
    const auto before=documentToJson(g_doc);const auto revision=g_session.revision;
    SendMessage(state.window,WM_COMMAND,49609,0);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(state.worker.valid()&&std::chrono::steady_clock::now()<deadline){advanceManufacturing060(0);Sleep(5);}
    check(state.active&&!state.study.moves.empty(),state.message.c_str());check(!state.study.receipt.find("blocked")->b,"synthetic machining blocked");
    SendMessage(state.window,WM_COMMAND,49610,0);advanceManufacturing060(20);check(state.frame>0,"playback did not advance");
    SendMessage(state.window,WM_COMMAND,49612,0);check(!state.playing,"step did not pause playback");SendMessage(state.window,WM_COMMAND,49613,0);
    check(g_session.revision==revision&&documentToJson(g_doc)==before,"playback mutated source CAD or graph");
    state.frame=state.study.moves.size();g_assistantDockOpen=false;layout();frameScene();target=tTarget;dist=tDist;g_swap=false;drawFrame(hdc);
    {Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> pixels(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());for(int y=0;y<winH;++y)std::copy_n(pixels.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);check(writePNG((shotPath+".playback.png").c_str(),image),"playback capture");}
    V edit;arkjson::parse(R"({"commands":[{"op":"param.set","name":"manufacturing_ui_change","expr":1}]})",edit);edit.set("expected_revision",V::makeNum(g_session.revision));runTool("doc_apply",edit);check(!g_error,"source edit fixture");advanceManufacturing060(0);check(!state.active,"changed revision retained a current simulation");
    DestroyWindow(state.window);state.window=state.list=state.summary=nullptr;g_status="Manufacturing UI passed / graph preview-apply / private simulation / playback / stale revision";
}
