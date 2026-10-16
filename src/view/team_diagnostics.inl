#include <new>
// Read-only and modeless: inspecting a blocker must not stop the team/UI queue.
static HWND g_teamDiagnosticsWindow030=nullptr;
static std::string teamDiagnosticsText030(const AgentTeamSnapshot& snapshot){
    std::ostringstream text;
    text<<"Goal: "<<agentGoalStateName(snapshot.goalState)<<(snapshot.goalDeclared?" / explicit lead report":" / no accepted goal report")<<"\r\n";
    text<<"Outstanding tasks: "<<snapshot.outstandingTasks<<" / unresolved tool or worker issues: "<<snapshot.unresolvedErrors<<"\r\n";
    text<<(snapshot.running?"Agent work is active.":"No agent request is active.")<<"\r\n";
    if(snapshot.paused)text<<"The team is paused.\r\n";
    if(snapshot.cancelled)text<<"Cancellation was requested.\r\n";
    if(!snapshot.goalSummary.empty())text<<"\r\nLead report:\r\n"<<snapshot.goalSummary<<"\r\n";
    text<<"\r\nTool rejections and requests for input are distinct from provider interruption. A finished turn does not prove the design is complete.\r\n";
    for(const auto& role:snapshot.roles){
        text<<"\r\n"<<role.id<<" / "<<agentTeamStateName(role.state)<<" / task "<<role.taskState<<"\r\n"<<role.status<<"\r\n";
        if(!role.taskSummary.empty())text<<role.taskSummary<<"\r\n";
        if(!role.error.empty())text<<"Worker/provider error: "<<role.error<<"\r\n";
        if(role.needsInspection)text<<"Current document inspection is required before another write.\r\n";
        if(role.unresolvedToolErrors)text<<"Unresolved tool requests: "<<role.unresolvedToolErrors<<"\r\n";
        for(const auto& failure:role.toolFailures){
            auto field=[&](const char* key){const auto* value=failure.find(key);return value?value->asStr():std::string{};};
            text<<"  "<<field("tool");if(!field("error_code").empty())text<<" ["<<field("error_code")<<"]";text<<": "<<field("error")<<"\r\n";
        }
        if(role.unresolvedToolErrors>role.toolFailures.size())text<<"  Showing "<<role.toolFailures.size()<<" retained tool diagnostics of "<<role.unresolvedToolErrors<<".\r\n";
    }
    return text.str();
}
static std::wstring teamDiagnosticsWide030(const std::string& text){
    const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),(int)text.size(),nullptr,0);std::wstring wide((size_t)count,L'\0');
    if(count)MultiByteToWideChar(CP_UTF8,0,text.data(),(int)text.size(),wide.data(),count);return wide;
}
struct TeamDiagnosticsWindow030 {HWND text=nullptr,refresh=nullptr,close=nullptr;HFONT font=nullptr;};
static void refreshTeamDiagnostics030(TeamDiagnosticsWindow030& state){
    const auto report=g_teamStarted?teamDiagnosticsText030(g_agentTeam.snapshot(false,std::numeric_limits<u64>::max(),true)):std::string("No team has started in this session.");
    const auto wide=teamDiagnosticsWide030(report);SetWindowTextW(state.text,wide.c_str());
}
static LRESULT CALLBACK teamDiagnosticsProc030(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* state=(TeamDiagnosticsWindow030*)GetWindowLongPtr(window,GWLP_USERDATA);
    if(message==WM_NCCREATE){state=new(std::nothrow) TeamDiagnosticsWindow030;if(!state)return FALSE;SetWindowLongPtr(window,GWLP_USERDATA,(LONG_PTR)state);return DefWindowProcW(window,message,wp,lp);}
    if(message==WM_GETMINMAXINFO){auto* bounds=(MINMAXINFO*)lp;bounds->ptMinTrackSize={420,260};return 0;}
    if(state&&message==WM_CREATE){
        auto instance=GetModuleHandle(nullptr);
        state->text=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,0,0,window,(HMENU)100,instance,nullptr);
        state->refresh=CreateWindowExW(0,L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,window,(HMENU)1,instance,nullptr);
        state->close=CreateWindowExW(0,L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,window,(HMENU)2,instance,nullptr);
        if(!state->text||!state->refresh||!state->close)return -1;
        state->font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(auto control:{state->text,state->refresh,state->close})SendMessage(control,WM_SETFONT,(WPARAM)(state->font?state->font:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessage(state->text,EM_SETLIMITTEXT,1024*1024,0);SendMessage(state->text,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));refreshTeamDiagnostics030(*state);return 0;
    }
    if(state&&message==WM_SIZE){const int width=LOWORD(lp),height=HIWORD(lp);MoveWindow(state->text,16,16,std::max(20,width-32),std::max(20,height-72),TRUE);MoveWindow(state->refresh,std::max(16,width-224),height-44,96,28,TRUE);MoveWindow(state->close,std::max(120,width-112),height-44,96,28,TRUE);return 0;}
    if(state&&message==WM_COMMAND){if(LOWORD(wp)==1){refreshTeamDiagnostics030(*state);return 0;}if(LOWORD(wp)==2){DestroyWindow(window);return 0;}}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_NCDESTROY){if(g_teamDiagnosticsWindow030==window)g_teamDiagnosticsWindow030=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);if(state&&state->font)DeleteObject(state->font);delete state;}
    return DefWindowProcW(window,message,wp,lp);
}
static void showTeamDiagnostics030(){
    if(g_teamDiagnosticsWindow030){refreshTeamDiagnostics030(*(TeamDiagnosticsWindow030*)GetWindowLongPtr(g_teamDiagnosticsWindow030,GWLP_USERDATA));ShowWindow(g_teamDiagnosticsWindow030,SW_SHOW);SetForegroundWindow(g_teamDiagnosticsWindow030);return;}
    WNDCLASSW cls{};cls.lpfnWndProc=teamDiagnosticsProc030;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName=L"ARK_TEAM_DIAGNOSTICS";cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
    MONITORINFO monitor{sizeof monitor};GetMonitorInfo(MonitorFromWindow(g_window,MONITOR_DEFAULTTONEAREST),&monitor);RECT owner{};GetWindowRect(g_window,&owner);
    const int width=std::min(920L,monitor.rcWork.right-monitor.rcWork.left-32),height=std::min(660L,monitor.rcWork.bottom-monitor.rcWork.top-48);
    const int x=std::clamp((int)owner.left+32,(int)monitor.rcWork.left,(int)monitor.rcWork.right-width),y=std::clamp((int)owner.top+32,(int)monitor.rcWork.top,(int)monitor.rcWork.bottom-height);
    g_teamDiagnosticsWindow030=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Team / blockers and reported outcome",WS_OVERLAPPEDWINDOW,x,y,width,height,g_window,nullptr,cls.hInstance,nullptr);
    if(!g_teamDiagnosticsWindow030)throw std::runtime_error("Could not open team diagnostics");ShowWindow(g_teamDiagnosticsWindow030,SW_SHOWNOACTIVATE);UpdateWindow(g_teamDiagnosticsWindow030);
}
static bool dispatchTeamDiagnostics030(MSG& message){
    if(!g_teamDiagnosticsWindow030||(message.hwnd!=g_teamDiagnosticsWindow030&&!IsChild(g_teamDiagnosticsWindow030,message.hwnd)))return false;
    if(!IsDialogMessage(g_teamDiagnosticsWindow030,&message)){TranslateMessage(&message);DispatchMessage(&message);}return true;
}
