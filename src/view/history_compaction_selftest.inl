if(historyCompactionUiTest){
    using V=arkjson::Value;auto fail=[&](const char* why){fprintf(stderr,"History compaction UI: %s / %s\n",why,g_status.c_str());return 951;};
    const auto directory=std::filesystem::temp_directory_path()/("ark-history-compact-ui-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directory(directory);
    runTool("doc_new",V::makeObj());V args;arkjson::parse(R"({"commands":[{"op":"feature.add","type":"box","name":"Archive fixture","p":{"dx":"5","dy":"6","dz":"7"}}]})",args);runTool("doc_apply",args);if(g_error)return fail("fixture rejected");
    V save=V::makeObj();save.set("path",V::makeStr((directory/"drawing.ark").string()));runTool("doc_save",save);if(g_error)return fail("fixture save failed");
    const auto before=documentToJson(g_doc);const auto revision=g_session.revision;const auto archive=directory/("drawing.history-"+std::to_string(revision)+".ark");
    g_historySmokeCompact=true;g_historyCompactAnswer=IDNO;g_dialogSmoke=2;g_dialogShot=shotPath+".history-compact.png";uiAction(493);
    if(g_error||g_session.revision!=revision||documentToJson(g_doc)!=before||std::filesystem::exists(archive))return fail("declined compaction changed drawing");
    g_historyCompactAnswer=IDYES;uiAction(493);g_dialogSmoke=0;g_historySmokeCompact=false;g_historyCompactAnswer=0;
    if(g_error||g_session.revision!=revision+1||!g_doc.iterationHistory.find("compaction"))return fail("compaction control failed");
    Document original,saved;std::string error;if(!loadDocument(archive.string().c_str(),original,&error)||documentToJson(original)!=before||!loadDocument((directory/"drawing.ark").string().c_str(),saved,&error)||!saved.iterationHistory.find("compaction"))return fail("saved archive or compacted drawing failed reopen");
    g_dialogSmoke=2;g_dialogShot=shotPath+".history-compacted.png";uiAction(493);g_dialogSmoke=0;if(g_error)return fail("compacted history inspection failed");
    std::filesystem::remove_all(directory);g_status="History compaction UI passed / decline / archive / explicit retained range / save / reopen";
}
