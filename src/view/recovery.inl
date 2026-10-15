// Separate autosave container: source documents are written only by explicit
// doc_save. Never overwrite a candidate before the user has reviewed it.
static std::string g_recoverySourcePath,g_recoveryConsumedPath;
static std::string g_recoveryActiveWriterToken=recoveryWriterToken();
static u64 g_recoveryBranchSequence=0;
static bool g_recoveryNeedsCheck=true,g_recoveryInhibited=false,g_recoveryLoading=false,g_recoveryTestMode=false;
static int g_recoveryPromptOverride=0;
static std::string g_recoveryPromptShot;
static void CALLBACK recoveryPromptCapture(HWND,UINT,UINT_PTR timer,DWORD){
    HWND dialog=GetLastActivePopup(g_window);char kind[32]{};GetClassNameA(dialog,kind,sizeof kind);if(std::strcmp(kind,"#32770")!=0)return;
    KillTimer(nullptr,timer);RECT bounds{};GetWindowRect(dialog,&bounds);const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    HDC screen=GetDC(dialog),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bytes=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bytes,nullptr,0);const auto previous=SelectObject(dc,bitmap);
    if(PrintWindow(dialog,dc,0)){Image image;image.w=width;image.h=height;image.px.resize((size_t)width*height*3);const auto* pixels=(const u8*)bytes;for(size_t i=0;i<(size_t)width*height;++i){image.px[i*3]=pixels[i*4+2];image.px[i*3+1]=pixels[i*4+1];image.px[i*3+2]=pixels[i*4];}writePNG(g_recoveryPromptShot.c_str(),image);}
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(dialog,screen);g_recoveryPromptShot.clear();PostMessage(dialog,WM_COMMAND,g_recoveryPromptOverride,0);
}
static u64 g_recoveryLastRevision=~u64(0),g_recoveryLastUsage=~u64(0),g_recoveryLastActivity=~u64(0);
static ULONGLONG g_recoveryLastAttempt=0;
static std::filesystem::path g_recoveryDirectoryOverride;
static std::string currentRecoveryDirectory(){
    const auto directory=g_recoveryDirectoryOverride.empty()?uiSettingsPath().parent_path()/"recovery":g_recoveryDirectoryOverride;
    const auto utf8=directory.u8string();return std::string(utf8.begin(),utf8.end());
}
static std::string currentRecoveryFile(){return recoveryFilePath(currentRecoveryDirectory(),g_recoverySourcePath,g_recoveryActiveWriterToken);}
static void recoveryDocumentEvent(const std::string& tool,const arkjson::Value& arguments){
    if(g_recoveryLoading||(g_headless&&!g_recoveryTestMode))return;
    if(tool=="doc_new"||tool=="doc_load"){
        auto* source=arguments.find("path");g_recoverySourcePath=tool=="doc_load"&&source?source->asStr():"";
        g_recoveryNeedsCheck=true;g_recoveryInhibited=false;g_recoveryLastRevision=~u64(0);g_recoveryConsumedPath.clear();
    }else if(tool=="doc_save"){
        std::error_code error;const auto previous=currentRecoveryFile();
        // Saving acknowledges the current state. Only known recovery filenames
        // are removed; the source and other candidates remain untouched.
        if(!g_recoveryNeedsCheck&&!g_recoveryInhibited)std::filesystem::remove(std::filesystem::u8path(previous),error);
        if(!g_recoveryConsumedPath.empty())std::filesystem::remove(std::filesystem::u8path(g_recoveryConsumedPath),error);
        if(auto* source=arguments.find("path"))g_recoverySourcePath=source->asStr();
        g_recoveryConsumedPath.clear();g_recoveryNeedsCheck=true;g_recoveryInhibited=false;g_recoveryLastRevision=g_session.revision;
    }
}
static void checkDocumentRecovery(){
    if((g_headless&&!g_recoveryTestMode)||!g_recoveryNeedsCheck||g_busy||g_chatBusy||g_dialog||(g_teamStarted&&g_agentTeam.snapshot().running))return;
    g_recoveryNeedsCheck=false;std::error_code ec;std::string discoveryError;
    const auto candidates=recoveryCandidates(currentRecoveryDirectory(),g_recoverySourcePath,&discoveryError);
    if(!discoveryError.empty()){g_recoveryInhibited=true;g_status="Recovery discovery failed / candidates retained / "+discoveryError;return;}
    for(size_t index=0;index<candidates.size();++index){const auto& recovery=candidates[index];
    Document candidate;RecoveryInfo info;std::string error;
    if(!loadRecovery(recovery.c_str(),candidate,info,&error)){
        g_recoveryInhibited=true;g_status="Recovery candidate retained for inspection / "+error;continue;
    }
    if(documentToJson(candidate)==documentToJson(g_doc))continue;
    const std::string message="Recovery candidate "+std::to_string(index+1)+" of "+std::to_string(candidates.size())+" from "+info.savedUtc+".\n\nSource: "+(info.sourcePath.empty()?"Untitled drawing":info.sourcePath)+"\nSaved revision: "+std::to_string(info.revision)+"\n\nYes: open this candidate unsaved; other candidates are retained.\nNo: discard only this candidate and inspect the next one.\nCancel: keep all remaining candidates and pause autosave for this drawing.\n\nSave As chooses the recovered drawing's destination. The original source file is preserved.";
    const bool capturePrompt=g_recoveryTestMode&&!g_recoveryPromptShot.empty();
    const UINT_PTR timer=capturePrompt?SetTimer(nullptr,0,200,recoveryPromptCapture):0;
    const int choice=g_recoveryPromptOverride&&!capturePrompt?g_recoveryPromptOverride:MessageBoxA(g_window,message.c_str(),"ARK / recover unsaved work",MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON3);
    if(timer)KillTimer(nullptr,timer);
    if(choice==IDCANCEL){g_recoveryInhibited=true;g_status="Recovery retained / original drawing open / autosave paused until another document is opened";return;}
    if(choice==IDNO){if(!std::filesystem::remove(std::filesystem::u8path(recovery),ec)||ec){g_recoveryInhibited=true;g_status="Recovery discard failed / "+ec.message();}continue;}
    using V=arkjson::Value;V arguments=V::makeObj();arguments.set("path",V::makeStr(recovery));arguments.set("expected_revision",V::makeNum((double)g_session.revision));
    g_recoveryLoading=true;runTool("doc_load",arguments);g_recoveryLoading=false;
    if(g_error){g_recoveryInhibited=true;return;}
    const auto source=std::filesystem::u8path(info.sourcePath);const auto filename=source.empty()?std::filesystem::path("Untitled.ark"):source.filename();
    const auto recovered=source.parent_path()/(filename.stem().wstring()+L" recovered.ark");const auto display=recovered.u8string();g_path=std::string(display.begin(),display.end());
    // The source for future recovery remains the original lineage until Save
    // As succeeds. The recovery container itself is never a save destination.
    g_recoverySourcePath=info.sourcePath;g_recoveryConsumedPath=info.writerToken==g_recoveryActiveWriterToken?recovery:"";
    // Start a separate capture lineage even when the chosen candidate belonged
    // to this process. Unchosen candidates must never become the write target.
    g_recoveryActiveWriterToken=recoveryWriterToken()+"-"+std::to_string(++g_recoveryBranchSequence);g_savedRevision=g_session.revision-1;g_recoveryLastRevision=g_session.revision;
    g_status="Recovered unsaved drawing / original and other writers' candidates retained / Save As chooses the new destination";return;
    }
}
static void captureDocumentRecovery(){
    if((g_headless&&!g_recoveryTestMode)||g_recoveryNeedsCheck||g_recoveryInhibited||g_busy||g_chatBusy||g_dialog||(g_teamStarted&&g_agentTeam.snapshot().running))return;
    if(g_session.revision==g_savedRevision&&g_doc.usage.serial==g_savedUsageSerial&&agentActivitySerial(g_doc)==g_savedActivitySerial)return;
    if(g_recoveryLastRevision==g_session.revision&&g_recoveryLastUsage==g_doc.usage.serial&&g_recoveryLastActivity==agentActivitySerial(g_doc))return;
    const auto now=GetTickCount64();if(!g_recoveryTestMode&&now-g_recoveryLastAttempt<30000)return;g_recoveryLastAttempt=now;
    std::string error;const auto destination=currentRecoveryFile();
    std::error_code directoryError;std::filesystem::create_directories(std::filesystem::u8path(destination).parent_path(),directoryError);
    if(directoryError){g_status="Autosave directory unavailable / original drawing retained / "+directoryError.message();return;}
    if(!saveRecovery(g_doc,destination.c_str(),g_recoverySourcePath,g_session.revision,g_recoveryActiveWriterToken,&error)){g_status="Autosave failed / original drawing retained / "+error;return;}
    g_recoveryLastRevision=g_session.revision;g_recoveryLastUsage=g_doc.usage.serial;g_recoveryLastActivity=agentActivitySerial(g_doc);
}
