static void blitClaudeCapture071(Image& into,const Image& from,int left,int top){
    for(int y=0;y<from.h;++y){const int dy=top+y;if(dy<0||dy>=into.h)continue;for(int x=0;x<from.w;++x){const int dx=left+x;if(dx<0||dx>=into.w)continue;std::memcpy(into.px.data()+((size_t)dy*into.w+dx)*3,from.px.data()+((size_t)y*from.w+x)*3,3);}}
}
static Image captureClaudePopup071(HWND menu){
    RECT bounds{};GetWindowRect(menu,&bounds);const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    if(width<150||height<60||width>4096||height>4096)throw std::runtime_error("Invalid owned popup bounds");
    HDC source=GetDC(menu),dc=CreateCompatibleDC(source);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!source||!dc||!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(source)ReleaseDC(menu,source);throw std::runtime_error("Cannot allocate owned popup render");}
    auto old=SelectObject(dc,bitmap);std::fill_n((unsigned*)pixels,(size_t)width*height,0x00fd00fdU);
    // Ask the actual #32768 HWND to render. Desktop pixels are never a source.
    const char* renderer="PrintWindow";PrintWindow(menu,dc,2); // PW_RENDERFULLCONTENT
    auto glyphEdges=[&]{size_t edges=0,painted=0;const auto* p=(const unsigned char*)pixels;for(int y=12;y<height-8;++y)for(int x=24;x<width-12;++x){const size_t at=((size_t)y*width+x)*4;if(p[at]!=253||p[at+1]!=0||p[at+2]!=253)++painted;const int diff=std::abs((int)p[at]-(int)p[at+4])+std::abs((int)p[at+1]-(int)p[at+5])+std::abs((int)p[at+2]-(int)p[at+6]);if(diff>100)++edges;}return std::pair<size_t,size_t>{edges,painted};};
    auto evidence=glyphEdges();
    if(evidence.first<150||evidence.second<(size_t)(width-36)*(height-20)*9/10){
        std::fill_n((unsigned*)pixels,(size_t)width*height,0x00fd00fdU);
        renderer="WM_PRINT";
        SendMessageW(menu,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND|PRF_CHILDREN);
        evidence=glyphEdges();
    }
    Image result;result.w=width;result.h=height;result.px.resize((size_t)width*height*3);for(size_t i=0;i<(size_t)width*height;++i){const auto* p=(const unsigned char*)pixels+4*i;result.px[3*i]=p[2];result.px[3*i+1]=p[1];result.px[3*i+2]=p[0];}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(menu,source);
    if(evidence.first<150||evidence.second<(size_t)(width-36)*(height-20)*9/10)throw std::runtime_error("Actual popup renderer supplied no verified text pixels");
    std::fprintf(stderr,"Claude owned popup: renderer=%s; width=%d; height=%d; interior_edges=%zu; painted=%zu\n",renderer,width,height,evidence.first,evidence.second);
    return result;
}
static Image captureClaudeOwnedApp071(){
    const bool swap=g_swap;g_swap=false;HDC dc=GetDC(g_window);drawFrame(dc);glFinish();std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());ReleaseDC(g_window,dc);g_swap=swap;
    Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);
    // The viewport/toolbar are GL; native chat/edit controls render through
    // their real HWND procedures, including children of offscreen windows.
    for(HWND child=GetWindow(g_window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(GetWindowLongPtr(child,GWL_STYLE)&WS_VISIBLE){RECT rect{};GetClientRect(child,&rect);if(rect.right<1||rect.bottom<1)continue;POINT at{};MapWindowPoints(child,g_window,&at,1);blitClaudeCapture071(image,captureChat(child,""),at.x,at.y);}
    return image;
}
struct ClaudeMenuCapture071 {HWND owner=nullptr;std::string path;bool captured=false;};
static ClaudeMenuCapture071* g_claudeMenuCapture071=nullptr;
static void CALLBACK captureClaudeMenuTimer071(HWND owner,UINT,UINT_PTR timer,DWORD){
    KillTimer(owner,timer);auto* capture=g_claudeMenuCapture071;
    if(capture&&capture->owner==owner){try{
        HWND menu=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM out)->BOOL{wchar_t name[64]{};GetClassNameW(candidate,name,64);DWORD pid=0;GetWindowThreadProcessId(candidate,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(candidate)&&std::wstring(name)==L"#32768"){*(HWND*)out=candidate;return FALSE;}return TRUE;},(LPARAM)&menu);
        if(!menu)throw std::runtime_error("Actual owned popup was not found");
        RECT menuBounds{};GetWindowRect(menu,&menuBounds);
        auto menuImage=captureClaudePopup071(menu);auto appImage=captureClaudeOwnedApp071();POINT origin{};ClientToScreen(owner,&origin);blitClaudeCapture071(appImage,menuImage,menuBounds.left-origin.x,menuBounds.top-origin.y);if(!writePNG((capture->path+".window.png").c_str(),appImage)||!writePNG((capture->path+".menu.png").c_str(),menuImage))throw std::runtime_error("Cannot write verified native capture");capture->captured=true;
    }catch(const std::exception& error){std::fprintf(stderr,"Claude owned capture failed: %s\n",error.what());}catch(...){std::fprintf(stderr,"Claude owned capture failed unexpectedly\n");} }
    // Dismiss only this thread's test popup after the native pixels were read.
    PostMessage(owner,WM_CANCELMODE,0,0);EndMenu();
}
static bool captureClaudeAccountMenu071(const std::string& path){
    DWORD ownerPid=0;GetWindowThreadProcessId(g_window,&ownerPid);if(ownerPid!=GetCurrentProcessId())return false;
    WINDOWPLACEMENT placement{sizeof placement};if(!GetWindowPlacement(g_window,&placement))return false;
    const bool visible=IsWindowVisible(g_window)!=FALSE,topmost=(GetWindowLongPtr(g_window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    const HWND foreground=GetForegroundWindow();POINT cursor{};GetCursorPos(&cursor);
    struct Restore {std::function<void()> fn;~Restore(){fn();}} restore{[&]{g_claudeMenuCapture071=nullptr;KillTimer(g_window,1571);SetWindowPos(g_window,topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowPlacement(g_window,&placement);if(!visible)ShowWindow(g_window,SW_HIDE);SetCursorPos(cursor.x,cursor.y);if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);}};
    ShowWindow(g_window,SW_RESTORE);MONITORINFO monitor{sizeof monitor};GetMonitorInfo(MonitorFromWindow(g_window,MONITOR_DEFAULTTONEAREST),&monitor);SetWindowPos(g_window,HWND_TOPMOST,monitor.rcWork.left+8,monitor.rcWork.top+8,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(g_window);UpdateWindow(g_window);layout();assistant_chat::sync(true);RedrawWindow(g_window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    RECT bounds{};GetWindowRect(g_window,&bounds);SetCursorPos(bounds.left+30,bounds.top+100);
    ClaudeMenuCapture071 capture{g_window,path,false};g_claudeMenuCapture071=&capture;
    if(!SetTimer(g_window,1571,300,captureClaudeMenuTimer071))return false;
    showAssistantAccountMenu();return capture.captured;
}
static int testClaudeReconnect071(const std::string& path){
    using namespace claudeReconnect071;
    const auto savedConfig=g_assistantConfig;const auto savedLog=g_chatLog;const auto savedStatus=g_status;
    const auto savedExpired=expiredEndpoints;const auto savedEndpoint=activeEndpoint;const unsigned savedSeen=seenGeneration;
    const auto savedTeamRoutes=teamRoutes;const auto savedTeamErrors=teamErrorSeen;const auto savedTeamUsage=teamUsageSeen;
    const bool savedBusy=g_chatBusy,savedAuthBusy=g_authBusy;auto savedAccount=std::move(account);
    struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{if(account)account->shutdown();account=std::move(savedAccount);g_assistantConfig=savedConfig;g_chatLog=savedLog;g_status=savedStatus;expiredEndpoints=savedExpired;activeEndpoint=savedEndpoint;seenGeneration=savedSeen;teamRoutes=savedTeamRoutes;teamErrorSeen=savedTeamErrors;teamUsageSeen=savedTeamUsage;g_chatBusy=savedBusy;g_authBusy=savedAuthBusy;}};
    auto fail=[&](int code,const char* message){std::fprintf(stderr,"Claude reconnect UI %d: %s\n",code,message);return code;};
    const auto revision=g_session.revision,epoch=g_documentEpoch;const auto history=g_conversation.dump();
    std::atomic_int logins=0,inference=0;std::atomic_bool hold=false,entered=false;
    ark::ClaudeAuthDependencies071 dependencies;
    dependencies.run=[&](const std::string&,const std::vector<std::string>& args,bool interactive,const std::atomic_bool* cancel){if(interactive){if(args!=std::vector<std::string>{"auth","login"})throw std::runtime_error("Unexpected login command");++logins;entered=true;while(hold&&!cancel->load())Sleep(2);if(cancel->load())return ark::ClaudeAuthProcessResult071{-1,true,{}};return ark::ClaudeAuthProcessResult071{0,false,{}};}return ark::ClaudeAuthProcessResult071{0,false,R"({"loggedIn":true,"authMethod":"claude.ai","email":"offline-private@example.invalid"})"};};
    dependencies.probe=[](const AssistantConfig&,const std::atomic_bool*){arkjson::Value value;arkjson::parse(R"({"models":[{"value":"offline"}],"sdk_mcp_listed":true,"prompt_sent":false})",value);return value;};
    account=std::make_unique<ark::AssistantClaudeAuth071>(dependencies);seenGeneration=0;expiredEndpoints.clear();
    g_assistantConfig=assistantProviderPreset("claude-code");wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);auto bytes=std::filesystem::path(executable).u8string();g_assistantConfig.endpoint=std::string(bytes.begin(),bytes.end());
    observeError("Injected text Claude Code / authentication_expired / api_error",g_assistantConfig);observeError("Claude Code / authentication_expired_extra",g_assistantConfig);if(expired())return fail(861,"Arbitrary category substring changed account state");
    observeError("Claude Code / authentication_expired. Legacy diagnostic",g_assistantConfig);if(!expired())return fail(862,"Terminal-absent expiry was not recognized");
    AssistantUsage reported;observeSuccess(g_assistantConfig,reported);if(!expired())return fail(863,"Missing usage was accepted as repaired authentication");reported.reportedResponses=1;reported.outputTokens=3;observeSuccess(g_assistantConfig,reported);if(expired())return fail(864,"A real successful subsequent response did not clear the old label");
    AgentTeamConfig captured;AgentTeamRole capturedRole;capturedRole.id="owned-route";capturedRole.assistant=g_assistantConfig;captured.roles.push_back(capturedRole);rememberTeam(captured);AgentTeamRoleSnapshot failed;failed.id=capturedRole.id;failed.error="Claude Code / authentication_expired. Captured route";observeTeamError(failed);
    AgentTeamEvent recovered;recovered.roleId=capturedRole.id;recovered.kind=AgentTeamEvent::Usage;recovered.usage=reported;arkjson::parse(R"({"schema":"ark.claude-result.v1","failed":false})",recovered.providerOutcome);observeTeamEvent(recovered);observeTeamError(failed);if(expired())return fail(865,"Old Team error re-latched after a later successful response");
    observeError("Claude Code / authentication_expired / terminal_reason=api_error",g_assistantConfig);
    if(!expired()||assistantAccountLabel071().find("sign in")==std::string::npos)return fail(851,"Observed expiry did not expose reconnect state");
    HMENU menu=CreatePopupMenu();appendMenu(menu);char caption[128]{};GetMenuStringA(menu,50100,caption,sizeof caption,MF_BYCOMMAND);const bool found=std::string(caption)=="Reconnect Claude Code...";DestroyMenu(menu);if(!found)return fail(852,"Human reconnect action is absent");
    g_chatBusy=true;menu=CreatePopupMenu();appendMenu(menu);const UINT blockedLogin=GetMenuState(menu,50100,MF_BYCOMMAND),blockedCheck=GetMenuState(menu,50101,MF_BYCOMMAND);DestroyMenu(menu);if(blockedLogin==(UINT)-1||blockedCheck==(UINT)-1||!(blockedLogin&MF_GRAYED)||!(blockedCheck&MF_GRAYED))return fail(866,"Native reconnect/check menu entries stayed enabled during assistant work");startWith(g_assistantConfig,true);if(busy()||logins)return fail(853,"Re-login started during assistant work");g_chatBusy=false;
    g_authBusy=true;startWith(g_assistantConfig,true);if(busy()||logins)return fail(854,"Re-login overlapped another account operation");g_authBusy=false;
    auto wait=[&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(busy()&&std::chrono::steady_clock::now()<deadline){poll();Sleep(2);}poll();return !busy();};
    startWith(g_assistantConfig,false);if(!wait()||account->snapshot().phase!=ark::ClaudeAuthPhase071::Ready||!expired()||logins)return fail(855,"Metadata-only status cleared a real authentication failure");
    g_chatLog={{"Error","Claude Code authentication expired. Use the Claude Code account menu to reconnect; the current drawing and committed edits are retained."},{"Account",g_status}};
    layout();assistant_chat::sync(true);captureChat(assistant_chat::window,path+".claude-auth-expired.png");
    if(!captureClaudeAccountMenu071(path+".claude-account-expired"))return fail(867,"Full owned account menu capture failed");
    hold=true;entered=false;startWith(g_assistantConfig,true);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(!entered&&std::chrono::steady_clock::now()<deadline)Sleep(2);if(!entered)return fail(856,"Owned offline login did not begin");
    menu=CreatePopupMenu();appendMenu(menu);const UINT loginBusy=GetMenuState(menu,50100,MF_BYCOMMAND),cancelReady=GetMenuState(menu,50102,MF_BYCOMMAND);DestroyMenu(menu);if(loginBusy==(UINT)-1||!(loginBusy&MF_GRAYED)||cancelReady==(UINT)-1||(cancelReady&(MF_DISABLED|MF_GRAYED)))return fail(868,"Busy native menu did not disable login and retain owned-operation cancellation");
    if(!captureClaudeAccountMenu071(path+".claude-account-busy"))return fail(869,"Owned busy account menu capture failed");
    AssistantTransport forbidden=[&](auto&,auto&,auto*)->arkjson::Value{++inference;throw std::runtime_error("No inference is allowed by this UI test");};
    sendAssistant("Preserve this unsent prompt.",forbidden);std::array<AssistantTransport,teamCapacity> team;team.fill(forbidden);sendTeam("Preserve this unsent team prompt.",team);
    if(inference||g_chatBusy||g_session.revision!=revision||g_documentEpoch!=epoch||g_conversation.dump()!=history)return fail(857,"Account operation launched/replayed CAD work or changed history");
    hold=false;if(!wait()||account->snapshot().phase!=ark::ClaudeAuthPhase071::Ready||logins!=1)return fail(858,"Owned login did not finish with verified metadata");
    if(expired()==account->snapshot().renewedSavedLogin)return fail(859,"Expiry latch ignored credential provenance");
    layout();assistant_chat::sync(true);captureChat(assistant_chat::window,path+".claude-auth-ready.png");
    if(g_session.revision!=revision||g_documentEpoch!=epoch||g_conversation.dump()!=history||inference)return fail(860,"Re-authentication changed CAD/history or auto-resumed work");
    return 0;
}
