// Native selectable conversation. UI state describes host observations, never
// grants completion authority or changes a document/provider conversation.
namespace assistant_chat {
struct Activity {std::string title,detail;COLORREF color=RGB(159,180,200);};
static Activity activity(){
    const COLORREF blue=RGB(112,194,245),amber=RGB(245,192,103),red=RGB(250,135,132);
    if(g_teamMode&&g_teamStarted){
        const auto& s=g_teamSnapshot;
        if(s.cancelled)return {s.running?"Stopping":"Stopped","Committed changes are retained.",amber};
        if(s.paused)return {"Paused","Resume the team to continue.",amber};
        if(!g_activeToolName.empty())return {"Working",g_activeToolRole+" / "+g_activeToolName,blue};
        if(s.running){
            for(const auto& r:s.roles)if(!r.activeTool.empty())return {"Working",r.label+" / "+r.activeTool,blue};
            return {"Thinking","Waiting for a model response; the team is still active.",blue};
        }
        if(s.goalDeclared&&s.goalState==AgentGoalState::NeedsInput)return {"Waiting for your input",s.goalSummary,amber};
        if(s.goalDeclared&&s.goalState==AgentGoalState::Completed)return {"Goal reported complete",s.goalSummary,RGB(114,213,171)};
        if(s.goalState==AgentGoalState::Interrupted||std::any_of(s.roles.begin(),s.roles.end(),[](const auto& r){return r.state==AgentTeamState::Failed||r.state==AgentTeamState::Interrupted;}))return {"Interrupted","Review the conversation and blockers before resuming.",red};
        return {"Stopped / awaiting continuation","The team is idle. Goal completion has not been reported.",amber};
    }
    if(!g_teamMode){
        if(g_chatBusy){
            if(*g_assistantCancel)return {"Stopping","Waiting for the active request to stop; prior changes are retained.",amber};
            if(!g_activeToolName.empty())return {"Working",g_activeToolName,blue};
            if(g_assistantStatus.rfind("Tool: ",0)==0)return {"Working",g_assistantStatus.substr(6),blue};
            if(g_assistantStatus.find("retry")!=std::string::npos)return {"Waiting for the connection",g_assistantStatus,amber};
            return {"Thinking",g_assistantStatus.empty()?"Waiting for a model response.":g_assistantStatus,blue};
        }
        if(g_assistantResponseFinished){
            if(*g_assistantCancel)return {"Stopped","The run was cancelled. Prior changes are retained.",amber};
            if(g_assistantResponseError)return {"Interrupted","Read the error below; prior changes may already be applied.",red};
            // Only an explicit leading status declaration is a needs-input signal.
            for(auto i=g_chatLog.rbegin();i!=g_chatLog.rend();++i)if(i->first=="ARK"){
                if(i->second.rfind("review_pending:",0)==0)return {"BOM review pending",i->second.substr(15),amber};
                if(i->second.rfind("needs_input:",0)==0)return {"Waiting for your input",i->second.substr(12),amber};
                break;
            }
            return {"Reply ready","The agent has stopped. Read its result and qualifications below.",RGB(114,213,171)};
        }
    }
    return {"Ready","Send a goal to start. Ctrl+Enter sends; Enter adds a new line.",blue};
}
static std::string goal(){return g_teamMode?g_teamGoal:g_singleGoal;}
static std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring w(n,L' ');MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
static std::wstring crlf(const std::wstring& s){std::wstring out;for(size_t i=0;i<s.size();++i){if(s[i]==L'\n'&&(i==0||s[i-1]!=L'\r'))out+=L'\r';out+=s[i];}return out;}
static bool clipboard(HWND owner,const std::wstring& text){
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));if(!memory)return false;
    void* p=GlobalLock(memory);if(!p){GlobalFree(memory);return false;}std::memcpy(p,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
    if(!OpenClipboard(owner)){GlobalFree(memory);return false;}EmptyClipboard();bool ok=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;CloseClipboard();if(!ok)GlobalFree(memory);return ok;
}
static std::string messageText(const std::pair<std::string,std::string>& row){if(row.first=="ARK")for(const std::string prefix:{"needs_input:","review_pending:"})if(row.second.rfind(prefix,0)==0){auto pos=prefix.size();if(row.second.size()>pos&&row.second[pos]==' ')++pos;return row.second.substr(pos);}return row.second;}
static std::wstring transcript(){std::wstring out=L"GOAL\r\n"+crlf(wide(goal()))+L"\r\n\r\n";for(const auto& row:g_chatLog)out+=wide(row.first)+L"\r\n"+crlf(wide(messageText(row)))+L"\r\n\r\n";return out;}
static constexpr COLORREF background=RGB(19,26,35),agentBg=RGB(30,42,55),userBg=RGB(28,58,76),ink=RGB(232,239,247),muted=RGB(163,185,204);
struct Bubble {HWND edit=nullptr;RECT box{};std::wstring role,text;bool user=false;int measuredWidth=0,bodyHeight=0;};
static HWND window=nullptr,viewport=nullptr,goalEdit=nullptr,copyButton=nullptr,latestButton=nullptr,detailsButton=nullptr,expandButton=nullptr;
static HFONT font=nullptr,titleFont=nullptr,labelFont=nullptr;static HMODULE richEdit=nullptr;
static std::vector<Bubble> bubbles;static std::vector<std::pair<std::string,std::string>> cached;
static std::string lastGoal,lastActivity;static int width=0,height=0,scroll=0,total=0;static bool testing=false;
static void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
static void label(HDC dc,const std::wstring& value,RECT r,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS){auto old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),(int)value.size(),&r,flags|DT_NOPREFIX);SelectObject(dc,old);}
static int pageHeight(){RECT r{};GetClientRect(viewport,&r);return std::max(1L,r.bottom);}
static HWND edit(HWND parent,const std::wstring& text,COLORREF bg);
static int textHeight(HWND control,int lineLimit=0){
    // RichEdit defers WM_SIZE reflow while the root is minimized. Its line
    // count/positions can still describe the initial 1px width even though the
    // client rectangle has changed. Refresh screen wrapping before caching any
    // height; repainting after Restore cannot repair that stale cached height.
    SendMessage(control,EM_SETTARGETDEVICE,0,0);
    const int lines=std::max(1,(int)SendMessage(control,EM_GETLINECOUNT,0,0));
    const int lastLine=lineLimit?std::min(lines,lineLimit)-1:lines-1;
    POINT first{},last{};
    SendMessage(control,EM_POSFROMCHAR,(WPARAM)&first,0);
    SendMessage(control,EM_POSFROMCHAR,(WPARAM)&last,SendMessage(control,EM_LINEINDEX,lastLine,0));
    HDC dc=GetDC(control);auto old=SelectObject(dc,font);TEXTMETRICW metrics{};
    GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(control,dc);
    // The control owns wrapping, font metrics and paragraph spacing. A fixed
    // pixels-per-line estimate leaves a large empty tail on long responses.
    return std::max(28,(int)(last.y-first.y+metrics.tmHeight+8));
}
static void place(){
    RECT r{};GetClientRect(viewport,&r);scroll=std::clamp(scroll,0,std::max(0,total-(int)r.bottom));
    SCROLLINFO info{sizeof info,SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,total-1),(UINT)std::max(1L,r.bottom),scroll};SetScrollInfo(viewport,SB_VERT,&info,TRUE);
    for(auto& b:bubbles){int y=b.box.top-scroll;bool visible=y<(int)r.bottom&&b.box.bottom-scroll>0;if(visible&&!b.edit)b.edit=edit(viewport,b.text,b.user?userBg:agentBg);if(b.edit){if(!visible&&GetFocus()!=b.edit){DestroyWindow(b.edit);b.edit=nullptr;continue;}
        // Saved window pixels belong to the previous scroll position. Moving
        // RichEdit with MoveWindow(..., FALSE) reused those pixels without
        // invalidating the child: parent-only paints left duplicated text.
        SetWindowPos(b.edit,nullptr,b.box.left+14,y+33,b.box.right-b.box.left-28,b.box.bottom-b.box.top-45,
            SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        // Message controls contain their entire measured text. Only the outer
        // conversation scrolls; selection/measurement must not add an inner offset.
        POINT origin{};SendMessage(b.edit,EM_SETSCROLLPOS,0,(LPARAM)&origin);}}
    // Repaint the complete settled layout, including every visible text child.
    // Mouse-wheel and scrollbar events call place() without a log/layout change.
    RedrawWindow(viewport,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
}
static LRESULT CALLBACK textProc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(m==WM_KEYDOWN&&(GetKeyState(VK_CONTROL)&0x8000)&&wp=='A'){SendMessage(w,EM_SETSEL,0,-1);return 0;}
    if(m==WM_KEYDOWN&&wp==VK_ESCAPE){SetFocus(g_chatInput?g_chatInput:g_window);return 0;}
    if(m==WM_MOUSEWHEEL&&w!=goalEdit){SendMessage(viewport,m,wp,lp);return 0;}
    return DefSubclassProc(w,m,wp,lp);
}
static HWND edit(HWND parent,const std::wstring& text,COLORREF bg){
    HWND e=CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_TABSTOP|ES_MULTILINE|ES_READONLY,0,0,1,1,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!e)return nullptr;
    SendMessage(e,EM_EXLIMITTEXT,0,16*1024*1024);SendMessage(e,WM_SETFONT,(WPARAM)font,FALSE);SendMessage(e,EM_SETBKGNDCOLOR,0,bg);
    CHARFORMAT2W fmt{};fmt.cbSize=sizeof fmt;fmt.dwMask=CFM_COLOR;fmt.crTextColor=ink;SendMessage(e,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&fmt);
    SendMessage(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowTextW(e,crlf(text).c_str());SetWindowSubclass(e,textProc,1,0);return e;
}
static void measure(){
    RECT r{};GetClientRect(viewport,&r);int available=std::max(100,(int)r.right-24),bubbleWidth=std::min(1000,std::max(100,available-44));total=12;
    for(auto& b:bubbles){int x=b.user?available-bubbleWidth+12:12;
        if(b.measuredWidth!=bubbleWidth){bool temporary=!b.edit;HWND probe=b.edit?b.edit:edit(viewport,b.text,b.user?userBg:agentBg);MoveWindow(probe,0,0,bubbleWidth-28,120,FALSE);b.bodyHeight=textHeight(probe);b.measuredWidth=bubbleWidth;if(temporary)DestroyWindow(probe);}
        b.box={x,total,x+bubbleWidth,total+b.bodyHeight+45};total=b.box.bottom+12;
    }
}
static void paintViewport(HDC dc){RECT r{};GetClientRect(viewport,&r);fill(dc,r,background);
    if(bubbles.empty())label(dc,L"Your conversation will appear here.",RECT{20,18,r.right-20,55},font,muted);
    for(const auto& b:bubbles){RECT box=b.box;OffsetRect(&box,0,-scroll);if(box.bottom<0||box.top>r.bottom)continue;
        HBRUSH brush=CreateSolidBrush(b.user?userBg:agentBg);HPEN pen=CreatePen(PS_SOLID,1,b.user?RGB(60,112,139):RGB(53,72,90));auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);RoundRect(dc,box.left,box.top,box.right,box.bottom,16,16);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);DeleteObject(brush);
        label(dc,b.role,RECT{box.left+14,box.top+6,box.right-14,box.top+29},labelFont,b.user?RGB(138,215,247):RGB(161,209,234));
    }
}
static LRESULT CALLBACK viewportProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(w,&ps):(HDC)wp;paintViewport(dc);if(m==WM_PAINT)EndPaint(w,&ps);return 0;}
    if(m==WM_MOUSEWHEEL){scroll-=GET_WHEEL_DELTA_WPARAM(wp)*75/WHEEL_DELTA;place();return 0;}
    if(m==WM_VSCROLL){SCROLLINFO si{sizeof si,SIF_ALL};GetScrollInfo(w,SB_VERT,&si);switch(LOWORD(wp)){case SB_LINEUP:scroll-=26;break;case SB_LINEDOWN:scroll+=26;break;case SB_PAGEUP:scroll-=pageHeight();break;case SB_PAGEDOWN:scroll+=pageHeight();break;case SB_THUMBTRACK:scroll=si.nTrackPos;break;case SB_TOP:scroll=0;break;case SB_BOTTOM:scroll=total;break;}place();return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
static void paintHeader(HDC dc){RECT r{};GetClientRect(window,&r);fill(dc,r,background);auto a=activity();
    label(dc,L"GOAL",RECT{14,6,80,28},labelFont,RGB(127,199,230));
    label(dc,wide(a.title),RECT{85,2,r.right-375,28},titleFont,a.color);
    fill(dc,RECT{12,g_chatHeaderHeight-2,r.right-12,g_chatHeaderHeight-1},RGB(46,63,78));
}
static LRESULT CALLBACK panelProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(w,&ps):(HDC)wp;paintHeader(dc);if(m==WM_PAINT)EndPaint(w,&ps);return 0;}
    if(m==WM_COMMAND){switch(LOWORD(wp)){case 1:if(clipboard(w,transcript()))SetWindowTextW(copyButton,L"Copied");else SetWindowTextW(copyButton,L"Retry copy");return 0;case 2:scroll=total;place();return 0;case 3:if(!g_teamMode){std::string info=activity().title+"\n"+activity().detail+"\n\n"+assistantUsageLabel(g_assistantUsageRun)+"\n"+measuredPerformanceLabel(g_assistantPerformanceRun);editText("Agent run",info,true);return 0;}g_chatAgentDetails=!g_chatAgentDetails;if(g_chatAgentDetails)g_chatExpanded=true;layout();return 0;case 4:g_chatExpanded=!g_chatExpanded;layout();return 0;}}
    if(m==WM_DRAWITEM){auto* item=(DRAWITEMSTRUCT*)lp;if(item->CtlType==ODT_BUTTON){fill(item->hDC,item->rcItem,RGB(35,51,66));wchar_t text[80]{};GetWindowTextW(item->hwndItem,text,80);label(item->hDC,text,item->rcItem,labelFont,ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&item->rcItem);return TRUE;}}
    if(m==WM_NCDESTROY){window=viewport=goalEdit=copyButton=latestButton=detailsButton=expandButton=nullptr;for(auto f:{font,titleFont,labelFont})if(f)DeleteObject(f);font=titleFont=labelFont=nullptr;bubbles.clear();cached.clear();width=height=scroll=total=0;lastGoal.clear();lastActivity.clear();return DefWindowProcW(w,m,wp,lp);}
    return DefWindowProcW(w,m,wp,lp);
}
static void create(){
    if(window)return;if(!richEdit)richEdit=LoadLibraryW(L"Msftedit.dll");if(!richEdit)return;
    WNDCLASSW c{};c.hInstance=GetModuleHandle(nullptr);c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.lpfnWndProc=panelProc;c.lpszClassName=L"ArkConversation";RegisterClassW(&c);c.lpfnWndProc=viewportProc;c.lpszClassName=L"ArkConversationScroll";RegisterClassW(&c);
    window=CreateWindowExW(0,L"ArkConversation",L"Agent conversation",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,g_window,nullptr,c.hInstance,nullptr);if(!window)return;
    font=CreateFontW(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");titleFont=CreateFontW(-19,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");labelFont=CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    goalEdit=edit(window,L"Describe what you want to achieve.",background);
    viewport=CreateWindowExW(0,L"ArkConversationScroll",L"Messages",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,120,1,1,window,nullptr,c.hInstance,nullptr);
    auto button=[&](const wchar_t* title,int id){return CreateWindowExW(0,L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,(HMENU)(INT_PTR)id,c.hInstance,nullptr);};
    copyButton=button(L"Copy chat",1);latestButton=button(L"Latest",2);detailsButton=button(L"Agents",3);expandButton=button(L"Expand",4);
}
static void sync(bool force=false){
    if(!g_assistantDockOpen||g_toolsOpen||!g_window){if(window)ShowWindow(window,SW_HIDE);return;}
    if(g_headless&&!testing)return;
    create();if(!window)return;
    // Observe the old page before resizing it, or Restore/Expand can wrongly
    // treat somebody reading an earlier message as following the newest one.
    const bool following=scroll>=std::max(0,total-pageHeight()-8);
    size_t anchor=0;while(anchor+1<bubbles.size()&&bubbles[anchor].box.bottom<=scroll)++anchor;
    const int anchorOffset=bubbles.empty()?0:scroll-bubbles[anchor].box.top;
    const int y=winH-32-assistantDockHeight()+43,composer=winH-(assistantImageComposerHeight()>40?252:188);
    int w=std::max(180,viewW-24),h=std::max(74,composer-y-8);bool details=g_teamMode&&g_chatAgentDetails;
    bool resized=width!=w||height!=h;width=w;height=h;
    auto currentGoal=goal();bool goalChanged=currentGoal!=lastGoal;
    if(goalChanged){SetWindowTextW(goalEdit,crlf(wide(currentGoal.empty()?"Describe what you want to achieve.":currentGoal)).c_str());lastGoal=currentGoal;}
    if(resized||goalChanged||force){
        MoveWindow(goalEdit,14,30,w-28,120,FALSE);
        // Reserve a readable message area in the split view. The complete goal
        // remains selectable and independently scrollable when space is tight.
        int goalHeight=std::min(textHeight(goalEdit,3),std::clamp(h-36-80,28,110));
        ShowScrollBar(goalEdit,SB_VERT,textHeight(goalEdit)>goalHeight);
        int header=30+goalHeight+6;resized|=header!=g_chatHeaderHeight;g_chatHeaderHeight=header;
        MoveWindow(goalEdit,14,30,w-28,goalHeight,TRUE);
        if(goalChanged){POINT origin{};SendMessage(goalEdit,EM_SETSCROLLPOS,0,(LPARAM)&origin);}
    }
    RECT current{};GetWindowRect(window,&current);POINT origin{current.left,current.top};ScreenToClient(g_window,&origin);
    int shownHeight=details?g_chatHeaderHeight:h;if(origin.x!=viewX+12||origin.y!=y||current.right-current.left!=w||current.bottom-current.top!=shownHeight)MoveWindow(window,viewX+12,y,w,shownHeight,TRUE);
    if(resized||force){MoveWindow(copyButton,w-116,2,102,28,TRUE);MoveWindow(latestButton,w-198,2,74,28,TRUE);MoveWindow(detailsButton,w-280,2,74,28,TRUE);MoveWindow(expandButton,w-364,2,76,28,TRUE);}
    ShowWindow(detailsButton,SW_SHOWNA);
    if(resized||force)MoveWindow(viewport,0,g_chatHeaderHeight,w,std::max(1,h-g_chatHeaderHeight),TRUE);ShowWindow(viewport,details?SW_HIDE:SW_SHOWNA);ShowWindow(window,SW_SHOWNA);
    const auto now=GetTickCount64();static ULONGLONG next=0;if(!force&&!resized&&now<next)return;next=now+100;
    static std::wstring buttonDetails,buttonExpand;std::wstring d=!g_teamMode?L"Run info":details?L"Chat":L"Agents",e=g_chatExpanded?L"Restore":L"Expand";if(d!=buttonDetails){SetWindowTextW(detailsButton,d.c_str());buttonDetails=d;}if(e!=buttonExpand){SetWindowTextW(expandButton,e.c_str());buttonExpand=e;}
    auto a=activity();auto status=a.title+"\n"+a.detail;bool statusChanged=status!=lastActivity;if(statusChanged)lastActivity=status;
    bool changed=cached!=g_chatLog;
    if(changed){size_t prefix=0;while(prefix<cached.size()&&prefix<g_chatLog.size()&&cached[prefix]==g_chatLog[prefix])++prefix;
        while(bubbles.size()>g_chatLog.size()){DestroyWindow(bubbles.back().edit);bubbles.pop_back();}
        for(size_t i=prefix;i<g_chatLog.size();++i){
            const auto& row=g_chatLog[i];if(i==bubbles.size())bubbles.emplace_back();auto& b=bubbles[i];
            const bool user=row.first.rfind("You",0)==0;auto role=wide(row.first),text=wide(messageText(row));
            if(b.role==role&&b.text==text)continue;
            if(b.edit){CHARRANGE selection{};SendMessage(b.edit,EM_EXGETSEL,0,(LPARAM)&selection);
                SetWindowTextW(b.edit,crlf(text).c_str());const LONG length=GetWindowTextLengthW(b.edit);
                selection.cpMin=std::clamp(selection.cpMin,0L,length);selection.cpMax=std::clamp(selection.cpMax,selection.cpMin,length);
                SendMessage(b.edit,EM_EXSETSEL,0,(LPARAM)&selection);SendMessage(b.edit,EM_SETBKGNDCOLOR,0,user?userBg:agentBg);}
            b.user=user;b.role=std::move(role);b.text=std::move(text);b.measuredWidth=0;
        }
        cached=g_chatLog;SetWindowTextW(copyButton,L"Copy chat");
    }
    if(changed||resized){measure();if(following)scroll=total;
        else if(anchor<bubbles.size())scroll=bubbles[anchor].box.top+std::min(anchorOffset,(int)(bubbles[anchor].box.bottom-bubbles[anchor].box.top)-1);
        place();}
    // The GL parent and hidden-start document loading can validate a 1px native
    // region before the dock is shown. Explicitly repaint the parent header;
    // child invalidation is needed only for layout/content changes.
    if(changed||resized||goalChanged||force||statusChanged)
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|((changed||resized||goalChanged||force)?RDW_ALLCHILDREN:RDW_NOCHILDREN));
}
} // namespace assistant_chat
