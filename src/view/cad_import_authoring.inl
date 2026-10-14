// Studio's ordinary import and file-drop paths share the same revision-checked tool.
static std::string cadPathUtf8(const wchar_t* value){int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);std::string out((size_t)n,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;}
static std::wstring cadPathWide(const std::string& value){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0);if(!n&&!value.empty())throw std::runtime_error("Invalid UTF-8 file path");std::wstring out((size_t)n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),out.data(),n);return out;}
static bool cadExchangePath(const std::string& path){auto extension=std::filesystem::u8path(path).extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return(char)std::tolower(c);});return extension==".step"||extension==".stp"||extension==".iges"||extension==".igs";}
static void beginArkitfy(const arkjson::Value& receipt){
    const auto* task=receipt.find("agent_prompt");if(!task||task->t!=arkjson::Value::Str)throw std::runtime_error("Imported source has no Arkitfy task");
    g_assistantDockOpen=true;layout();
    const auto wide=cadPathWide(task->str);if(g_chatInput)SetWindowTextW(g_chatInput,wide.c_str());
    if(g_chatBusy||(g_teamStarted&&g_agentTeam.snapshot().running)){g_status="CAD imported. Arkitfy task is ready in Assistant; send it after the current task finishes.";return;}
    try{validateAssistantConfig(g_assistantConfig);}catch(const std::exception&){g_status="CAD imported. Configure an agent, then send the prepared Arkitfy task in Assistant.";return;}
    sendAssistant(task->str);
}
static void importCadDialog(const std::string& explicitPath="",const std::string& testMode=""){
    using V=arkjson::Value;const u64 revision=g_session.revision,epoch=g_documentEpoch;std::string path=explicitPath;
    if(path.empty()){wchar_t file[32768]=L"";OPENFILENAMEW open{};open.lStructSize=sizeof open;open.hwndOwner=g_window;open.lpstrFile=file;open.nMaxFile=32768;open.lpstrFilter=L"Exact CAD / STEP and IGES\0*.step;*.stp;*.iges;*.igs\0STEP\0*.step;*.stp\0IGES\0*.iges;*.igs\0";open.lpstrTitle=L"Import CAD";open.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(!GetOpenFileNameW(&open))return;path=cadPathUtf8(file);}
    if(!cadExchangePath(path))throw std::runtime_error("Choose a STEP, STP, IGES or IGS file");
    auto filename=std::filesystem::u8path(path).stem().u8string();std::string name(filename.begin(),filename.end()),mode=testMode;
    if(mode.empty()){std::vector<EditorField> fields{{"What do you want to do?","Just import",false,{"Just import","Arkitfy using agents"}},{"Reference name",name}};
        if(!editFields("Import CAD","Just import keeps exact geometry. Arkitfy asks your agents to rebuild an editable feature tree and check it against the original.",fields,false,"Continue"))return;mode=fields[0].value=="Just import"?"import":"arkitfy";name=trimField(fields[1].value);}
    if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("Document changed while choosing import options; reopen Import CAD");
    V args=V::makeObj();args.set("path",V::makeStr(path));args.set("name",V::makeStr(name));args.set("mode",V::makeStr(mode));args.set("expected_revision",V::makeNum((double)revision));auto response=runTool("import_cad",args);if(g_error)return;
    frameScene();const auto& receipt=*response.find("structuredContent");if(const auto* id=receipt.find("created_id")){for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==(u64)id->num){selBody=(int)i;break;}}
    if(mode=="arkitfy")beginArkitfy(receipt);else g_status="Exact CAD imported / editable operations can use this reference";
}
static void arkitfySelectedCad(){
    using V=arkjson::Value;const u64 id=featureIdField(selectedFeatureId());const auto* feature=g_doc.find(id);if(!feature||featureOperation(*feature)!="imported_cad")throw std::runtime_error("Select an imported STEP or IGES reference");
    V args=V::makeObj();args.set("id",V::makeNum((double)id));auto result=runTool("cad_inspect",args);if(g_error)return;V receipt=*result.find("structuredContent");receipt.set("agent_prompt",V::makeStr(cadImportArkitfyPrompt(receipt,id)));beginArkitfy(receipt);
}
static void droppedStudioFiles(HDROP drop){
    std::vector<std::string> paths;const auto count=DragQueryFileW(drop,0xffffffff,nullptr,0);if(count>32){DragFinish(drop);throw std::runtime_error("Drop at most 32 files at once");}
    for(UINT i=0;i<count;++i){const UINT length=DragQueryFileW(drop,i,nullptr,0);std::vector<wchar_t> file((size_t)length+1);DragQueryFileW(drop,i,file.data(),length+1);paths.push_back(cadPathUtf8(file.data()));}DragFinish(drop);
    for(const auto& path:paths){if(cadExchangePath(path))importCadDialog(path);else{if(paths.size()!=1)throw std::runtime_error("Drop image references individually; CAD files support multiple selection");setPendingAssistantImage(path);}if(g_error)break;}
}
