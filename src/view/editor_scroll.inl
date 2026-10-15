// Native form fields live in a clipped scrolling child. Their HWNDs and values
// survive scrolling; the title and commit/cancel controls stay fixed.
struct EditorScrollState {HWND panel=nullptr;int contentHeight=0,offset=0,line=62;};
static RECT g_editorWorkAreaOverride{};
static UINT g_editorDpiOverride=0;
static std::function<void(HWND)> g_editorSmokeProbe;
static UINT editorDpi(){
    if(g_editorDpiOverride)return g_editorDpiOverride;
    using Query=UINT(WINAPI*)(HWND);static auto query=(Query)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    if(query){const auto dpi=query(g_window);if(dpi)return dpi;}HDC dc=GetDC(g_window);const auto dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(g_window,dc);return dpi>0?(UINT)dpi:96;
}
static RECT editorWorkArea(){
    if(g_editorWorkAreaOverride.right>g_editorWorkAreaOverride.left)return g_editorWorkAreaOverride;
    MONITORINFO monitor{sizeof monitor};GetMonitorInfo(MonitorFromWindow(g_window,MONITOR_DEFAULTTONEAREST),&monitor);return monitor.rcWork;
}
static void editorScrollTo(EditorScrollState& state,int offset){
    RECT bounds{};GetClientRect(state.panel,&bounds);const int height=bounds.bottom-bounds.top;
    const int next=std::clamp(offset,0,std::max(0,state.contentHeight-height));const int delta=state.offset-next;state.offset=next;
    if(delta)ScrollWindowEx(state.panel,0,delta,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);
    SCROLLINFO info{sizeof info,SIF_RANGE|SIF_PAGE|SIF_POS};info.nMin=0;info.nMax=std::max(0,state.contentHeight-1);info.nPage=(UINT)std::max(0,height);info.nPos=state.offset;SetScrollInfo(state.panel,SB_VERT,&info,TRUE);
}
static void editorEnsureVisible(EditorScrollState& state,HWND child){
    if(!child||!IsChild(state.panel,child))return;RECT bounds{},viewport{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,state.panel,(POINT*)&bounds,2);GetClientRect(state.panel,&viewport);
    const int margin=std::max(3,state.line/10);
    if(bounds.top<margin)editorScrollTo(state,state.offset+bounds.top-margin);
    else if(bounds.bottom>viewport.bottom-margin)editorScrollTo(state,state.offset+bounds.bottom-viewport.bottom+margin);
}
static bool editorScrollKey(EditorScrollState& state,WPARAM key){
    RECT bounds{};GetClientRect(state.panel,&bounds);const int page=std::max(state.line,(int)bounds.bottom-state.line);
    if(key==VK_PRIOR){editorScrollTo(state,state.offset-page);return true;}
    if(key==VK_NEXT){editorScrollTo(state,state.offset+page);return true;}
    return false;
}
static LRESULT CALLBACK editorFieldScrollProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto& state=*(EditorScrollState*)data;
    if(message==WM_SETFOCUS){auto result=DefSubclassProc(window,message,wp,lp);editorEnsureVisible(state,window);return result;}
    if(message==WM_KEYDOWN&&!SendMessage(window,CB_GETDROPPEDSTATE,0,0)&&editorScrollKey(state,wp))return 0;
    if(message==WM_MOUSEWHEEL&&!SendMessage(window,CB_GETDROPPEDSTATE,0,0))return SendMessage(state.panel,message,wp,lp);
    return DefSubclassProc(window,message,wp,lp);
}
static LRESULT CALLBACK editorScrollProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* state=(EditorScrollState*)GetWindowLongPtr(window,GWLP_USERDATA);
    if(state){
        if(message==WM_MOUSEWHEEL){editorScrollTo(*state,state->offset-GET_WHEEL_DELTA_WPARAM(wp)*state->line/120);return 0;}
        if(message==WM_VSCROLL){RECT bounds{};GetClientRect(window,&bounds);SCROLLINFO info{sizeof info,SIF_TRACKPOS};GetScrollInfo(window,SB_VERT,&info);int next=state->offset;
            switch(LOWORD(wp)){case SB_LINEUP:next-=state->line;break;case SB_LINEDOWN:next+=state->line;break;case SB_PAGEUP:next-=bounds.bottom;break;case SB_PAGEDOWN:next+=bounds.bottom;break;case SB_TOP:next=0;break;case SB_BOTTOM:next=state->contentHeight;break;case SB_THUMBPOSITION:case SB_THUMBTRACK:next=info.nTrackPos;break;}
            editorScrollTo(*state,next);return 0;}
        if(message==WM_SIZE){editorScrollTo(*state,state->offset);return 0;}
        if(message==WM_COMMAND&&(HIWORD(wp)==EN_SETFOCUS||HIWORD(wp)==CBN_SETFOCUS||HIWORD(wp)==BN_SETFOCUS))editorEnsureVisible(*state,(HWND)lp);
    }
    if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX||message==WM_DRAWITEM||message==WM_COMMAND)return SendMessage(GetParent(window),message,wp,lp);
    return DefWindowProcA(window,message,wp,lp);
}
static HWND editorFieldControl(HWND dialog,int id){
    if(auto direct=GetDlgItem(dialog,id))return direct;
    return GetDlgItem(GetDlgItem(dialog,9020),id);
}
