// Native selectable conversation. UI state describes host observations, never
// grants completion authority or changes a document/provider conversation.
// Lives in the right assistant dock; reasoning is shown only as returned by
// the provider (summaries/visible thinking), collapsed by default.
namespace assistant_chat {
namespace th=ui_theme;
static constexpr COLORREF runningInk=th::rgb(th::accent),attentionInk=RGB(240,200,120),failedInk=th::rgb(th::danger),doneInk=RGB(114,213,171),idleInk=th::rgb(th::text2);
struct Activity {std::string title,detail;COLORREF color=idleInk;};
static Activity activity(){
    const COLORREF blue=runningInk,amber=attentionInk,red=failedInk;
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
        if(s.goalDeclared&&s.goalState==AgentGoalState::Completed)return {"Goal reported complete",s.goalSummary,doneInk};
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
            return {"Reply ready","The agent has stopped. Read its result and qualifications below.",doneInk};
        }
    }
    return {"Ready","Send a goal to start. Ctrl+Enter sends; Enter adds a new line.",idleInk};
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
// Shared Studio palette: graphite surfaces, white overlays, one orange accent.
static constexpr COLORREF background=th::rgb(th::chrome),agentBg=th::rgb(th::raised),userBg=th::rgb(th::mix(th::raised,th::accent,.10f)),thoughtBg=th::rgb(th::mix(th::raised,th::field,.55f)),
    ink=th::rgb(th::text),muted=th::rgb(th::text2),faint=th::rgb(th::faint),accent=th::rgb(th::accent),lineInk=th::rgb(th::over(th::chrome,th::line)),
    buttonBg=th::rgb(th::raised),buttonEdge=th::rgb(th::over(th::raised,th::border)),agentEdge=th::rgb(th::over(th::raised,th::line)),userEdge=th::rgb(th::mix(th::raised,th::accent,.32f));
struct Bubble {HWND edit=nullptr,thought=nullptr;RECT box{};std::wstring role,text,reasoning,reasoningEmpty;bool user=false,hasNote=false,open=false;int measuredWidth=0,bodyHeight=0,thoughtWidth=0,thoughtHeight=0;};
static HWND window=nullptr,viewport=nullptr,goalEdit=nullptr,copyButton=nullptr,latestButton=nullptr,detailsButton=nullptr,expandButton=nullptr,reasoningButton=nullptr,moreButton=nullptr,thoughtPanel=nullptr;
static std::array<HWND,8> chips{};
static HFONT font=nullptr,titleFont=nullptr,labelFont=nullptr,thoughtFont=nullptr;static HMODULE richEdit=nullptr;
static std::vector<Bubble> bubbles;static std::vector<std::pair<std::string,std::string>> cached;
static std::string lastGoal,lastActivity;static int width=0,height=0,scroll=0,total=0;static bool testing=false;
// Header geometry (window client coordinates), recomputed by layoutHeader().
static int titleY=2,goalTop=64,reasonTop=126,captionTop=0,panelTop=0,panelHeight=0;static bool wideHeader=false,panelShown=false;
static std::wstring panelCaption,panelText;static u64 seenSerial=0;static int seenAgent=-2;
static void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
static void label(HDC dc,const std::wstring& value,RECT r,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS){auto old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),(int)value.size(),&r,flags|DT_NOPREFIX);SelectObject(dc,old);}
static void roundBox(HDC dc,RECT r,COLORREF fillColor,COLORREF edge,int radius){HBRUSH brush=CreateSolidBrush(fillColor);HPEN pen=CreatePen(PS_SOLID,1,edge);auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);DeleteObject(brush);}
static int pageHeight(){RECT r{};GetClientRect(viewport,&r);return std::max(1L,r.bottom);}
static HWND edit(HWND parent,const std::wstring& text,COLORREF bg,COLORREF color=ink,HFONT face=nullptr);
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
// Reasoning disclosure row and expanded reasoning well inside an agent bubble.
static int noteExtra(const Bubble& b){return b.hasNote?24+(b.open&&!b.reasoning.empty()?b.thoughtHeight+10:0):0;}
static RECT toggleRect(const Bubble& b){return {b.box.left+10,b.box.top+30,b.box.right-10,b.box.top+54};}
static void place(){
    RECT r{};GetClientRect(viewport,&r);scroll=std::clamp(scroll,0,std::max(0,total-(int)r.bottom));
    SCROLLINFO info{sizeof info,SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,total-1),(UINT)std::max(1L,r.bottom),scroll};SetScrollInfo(viewport,SB_VERT,&info,TRUE);
    for(auto& b:bubbles){int y=b.box.top-scroll;bool visible=y<(int)r.bottom&&b.box.bottom-scroll>0;const int extra=noteExtra(b);
        if(visible&&!b.edit)b.edit=edit(viewport,b.text,b.user?userBg:agentBg);
        const bool thoughtOpen=b.hasNote&&b.open&&!b.reasoning.empty();
        if(visible&&thoughtOpen&&!b.thought)b.thought=edit(viewport,b.reasoning,thoughtBg,muted,thoughtFont);
        if(b.thought&&(!thoughtOpen||(!visible&&GetFocus()!=b.thought))){DestroyWindow(b.thought);b.thought=nullptr;}
        if(b.thought){SetWindowPos(b.thought,nullptr,b.box.left+20,y+59,b.box.right-b.box.left-40,std::max(1,b.thoughtHeight-8),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW|SWP_SHOWWINDOW);POINT origin{};SendMessage(b.thought,EM_SETSCROLLPOS,0,(LPARAM)&origin);}
        if(b.edit){if(!visible&&GetFocus()!=b.edit){DestroyWindow(b.edit);b.edit=nullptr;continue;}
        // Saved window pixels belong to the previous scroll position. Moving
        // RichEdit with MoveWindow(..., FALSE) reused those pixels without
        // invalidating the child: parent-only paints left duplicated text.
        SetWindowPos(b.edit,nullptr,b.box.left+14,y+33+extra,b.box.right-b.box.left-28,b.box.bottom-b.box.top-45-extra,
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
    if(m==WM_MOUSEWHEEL&&w!=goalEdit&&w!=thoughtPanel){SendMessage(viewport,m,wp,lp);return 0;}
    return DefSubclassProc(w,m,wp,lp);
}
static HWND edit(HWND parent,const std::wstring& text,COLORREF bg,COLORREF color,HFONT face){
    HWND e=CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_TABSTOP|ES_MULTILINE|ES_READONLY,0,0,1,1,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!e)return nullptr;
    SendMessage(e,EM_EXLIMITTEXT,0,16*1024*1024);SendMessage(e,WM_SETFONT,(WPARAM)(face?face:font),FALSE);SendMessage(e,EM_SETBKGNDCOLOR,0,bg);
    CHARFORMAT2W fmt{};fmt.cbSize=sizeof fmt;fmt.dwMask=CFM_COLOR;fmt.crTextColor=color;SendMessage(e,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&fmt);
    SendMessage(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowTextW(e,crlf(text).c_str());SetWindowSubclass(e,textProc,1,0);return e;
}
static void measure(){
    // Speaker offset scales with the dock: a narrow column keeps its reading width.
    RECT r{};GetClientRect(viewport,&r);int available=std::max(100,(int)r.right-24),bubbleWidth=std::min(1000,std::max(100,available-std::clamp(available/12,16,44)));total=12;
    for(auto& b:bubbles){int x=b.user?available-bubbleWidth+12:12;
        if(b.measuredWidth!=bubbleWidth){bool temporary=!b.edit;HWND probe=b.edit?b.edit:edit(viewport,b.text,b.user?userBg:agentBg);MoveWindow(probe,0,0,bubbleWidth-28,120,FALSE);b.bodyHeight=textHeight(probe);b.measuredWidth=bubbleWidth;if(temporary)DestroyWindow(probe);}
        if(b.hasNote&&b.open&&!b.reasoning.empty()&&b.thoughtWidth!=bubbleWidth){bool temporary=!b.thought;HWND probe=b.thought?b.thought:edit(viewport,b.reasoning,thoughtBg,muted,thoughtFont);MoveWindow(probe,0,0,bubbleWidth-40,120,FALSE);b.thoughtHeight=textHeight(probe)+8;b.thoughtWidth=bubbleWidth;if(temporary)DestroyWindow(probe);}
        b.box={x,total,x+bubbleWidth,total+b.bodyHeight+45+noteExtra(b)};total=b.box.bottom+12;
    }
}
static void relayoutBubbles(){measure();place();}
static void paintViewport(HDC dc){RECT r{};GetClientRect(viewport,&r);fill(dc,r,background);
    if(bubbles.empty())label(dc,L"Your conversation will appear here.",RECT{20,18,r.right-20,55},font,muted);
    for(const auto& b:bubbles){RECT box=b.box;OffsetRect(&box,0,-scroll);if(box.bottom<0||box.top>r.bottom)continue;
        roundBox(dc,box,b.user?userBg:agentBg,b.user?userEdge:agentEdge,16);
        label(dc,b.role,RECT{box.left+14,box.top+6,box.right-14,box.top+29},labelFont,b.user?accent:muted);
        if(!b.hasNote)continue;
        // Collapsed by default. Empty text is an explicit, quiet provider note.
        RECT row=toggleRect(b);OffsetRect(&row,0,-scroll);row.left+=4;
        if(b.reasoning.empty())label(dc,b.reasoningEmpty.empty()?wide(chatReasoningHidden):b.reasoningEmpty,row,labelFont,faint);
        else{label(dc,b.open?L"\u25BC  Reasoning":L"\u25BA  Reasoning  \u00B7  as returned by the provider",row,labelFont,accent);
            if(b.open){RECT well{box.left+14,box.top+55,box.right-14,box.top+55+b.thoughtHeight};roundBox(dc,well,thoughtBg,thoughtBg,10);}}
    }
}
static int bubbleAt(POINT p){const int y=p.y+scroll;for(size_t i=0;i<bubbles.size();++i){const auto& b=bubbles[i];if(!b.hasNote||b.reasoning.empty())continue;RECT row=toggleRect(b);if(p.x>=row.left&&p.x<row.right&&y>=row.top&&y<row.bottom)return (int)i;}return -1;}
static void toggleReasoning(size_t index){if(index<bubbles.size()&&bubbles[index].hasNote&&!bubbles[index].reasoning.empty()){bubbles[index].open=!bubbles[index].open;relayoutBubbles();}}
static LRESULT CALLBACK viewportProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(w,&ps):(HDC)wp;paintViewport(dc);if(m==WM_PAINT)EndPaint(w,&ps);return 0;}
    if(m==WM_LBUTTONDOWN){const int hit=bubbleAt(POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(hit>=0){toggleReasoning((size_t)hit);return 0;}}
    if(m==WM_SETCURSOR&&LOWORD(lp)==HTCLIENT){POINT p{};GetCursorPos(&p);ScreenToClient(w,&p);if(bubbleAt(p)>=0){SetCursor(LoadCursor(nullptr,IDC_HAND));return TRUE;}}
    if(m==WM_MOUSEWHEEL){scroll-=GET_WHEEL_DELTA_WPARAM(wp)*75/WHEEL_DELTA;place();return 0;}
    if(m==WM_VSCROLL){SCROLLINFO si{sizeof si,SIF_ALL};GetScrollInfo(w,SB_VERT,&si);switch(LOWORD(wp)){case SB_LINEUP:scroll-=26;break;case SB_LINEDOWN:scroll+=26;break;case SB_PAGEUP:scroll-=pageHeight();break;case SB_PAGEDOWN:scroll+=pageHeight();break;case SB_THUMBTRACK:scroll=si.nTrackPos;break;case SB_TOP:scroll=0;break;case SB_BOTTOM:scroll=total;break;}place();return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
// Agents whose latest reasoning can be shown: ARK in single mode, team roles otherwise.
static int agentCount(){return g_teamMode?(int)std::min<size_t>(teamActiveRoleCount(),teamCapacity):1;}
static COLORREF agentState(int agent){
    if(!g_teamMode)return g_chatBusy?runningInk:g_assistantResponseFinished?(g_assistantResponseError?failedInk:doneInk):faint;
    const AgentTeamRoleSnapshot* role=agent>=0&&(size_t)agent<g_teamSnapshot.roles.size()?&g_teamSnapshot.roles[(size_t)agent]:nullptr;if(!role)return faint;
    if(role->state==AgentTeamState::Working)return runningInk;if(role->state==AgentTeamState::Failed||role->state==AgentTeamState::Interrupted)return failedInk;
    return role->turns?doneInk:faint;
}
static bool agentReasoning(int agent,std::wstring& caption,std::wstring& body){
    if(agent<0||agent>=agentCount())return false;
    if(!g_teamMode){caption=std::wstring(L"Latest reasoning \u00B7 ARK")+(g_chatBusy?L" \u00B7 running":L"");
        body=!g_assistantReasoningRun.empty()?wide(g_assistantReasoningRun):!g_assistantRunStarted?L"No run yet. Readable reasoning appears here when the provider returns it.":g_chatBusy?L"No readable reasoning returned yet in this run.":wide(chatReasoningHidden)+L".";return true;}
    const AgentTeamRoleSnapshot* role=(size_t)agent<g_teamSnapshot.roles.size()?&g_teamSnapshot.roles[(size_t)agent]:nullptr;
    // Short label first so the role id survives a narrow dock; the full label follows.
    const size_t slot=teamRoleSlot((size_t)agent);caption=L"Latest reasoning \u00B7 "+wide(chatAgentName((size_t)agent))+L" ("+wide(g_teamRoleIds[slot])+L")";
    if(role){if(!role->reasoning.empty())caption+=L" \u00B7 turn "+std::to_wstring(role->reasoningTurn);caption+=L" \u00B7 "+wide(agentTeamStateName(role->state));}
    caption+=L" \u00B7 "+wide(g_teamRoleLabels[slot]);
    body=role&&!role->reasoning.empty()?wide(role->reasoning):!role||role->turns==0?L"Not started. Readable reasoning appears here when the provider returns it.":role->state==AgentTeamState::Working?L"No readable reasoning returned yet in this turn.":wide(chatReasoningHidden)+L".";
    return true;
}
static void paintHeader(HDC dc){RECT r{};GetClientRect(window,&r);fill(dc,r,background);auto a=activity();
    const int titleRight=wideHeader?(int)r.right-14-4*74-3*6-12:(int)r.right-14;
    {HBRUSH dot=CreateSolidBrush(a.color);HPEN pen=CreatePen(PS_SOLID,1,a.color);auto ob=SelectObject(dc,dot),op=SelectObject(dc,pen);Ellipse(dc,14,titleY+10,23,titleY+19);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);DeleteObject(dot);}
    const auto title=wide(a.title);label(dc,title,RECT{30,titleY,titleRight,titleY+28},titleFont,a.color);
    {HDC probe=dc;auto old=SelectObject(probe,titleFont);SIZE size{};GetTextExtentPoint32W(probe,title.c_str(),(int)title.size(),&size);SelectObject(probe,old);
     const int detailLeft=30+size.cx+12;if(detailLeft<titleRight-40&&!a.detail.empty())label(dc,wide(a.detail),RECT{detailLeft,titleY+1,titleRight,titleY+28},labelFont,faint);}
    label(dc,L"GOAL",RECT{14,goalTop+3,60,goalTop+23},labelFont,faint);
    if(panelShown)label(dc,panelCaption,RECT{14,captionTop,r.right-14,captionTop+18},labelFont,muted);
    fill(dc,RECT{12,g_chatHeaderHeight-2,r.right-12,g_chatHeaderHeight-1},lineInk);
}
static void layoutHeader();
static LRESULT CALLBACK panelProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(w,&ps):(HDC)wp;paintHeader(dc);if(m==WM_PAINT)EndPaint(w,&ps);return 0;}
    if(m==WM_COMMAND){const int id=LOWORD(wp);switch(id){case 1:if(clipboard(w,transcript()))SetWindowTextW(copyButton,L"Copied");else SetWindowTextW(copyButton,L"Retry copy");return 0;case 2:scroll=total;place();return 0;case 3:if(!g_teamMode){std::string info=activity().title+"\n"+activity().detail+"\n\n"+assistantUsageLabel(g_assistantUsageRun)+"\n"+measuredPerformanceLabel(g_assistantPerformanceRun);editText("Agent run",info,true);return 0;}g_chatAgentDetails=!g_chatAgentDetails;layout();return 0;case 4:setChatDockMode(g_chatExpanded?1:2);layout();return 0;
        case 5:g_chatReasoningAll=!g_chatReasoningAll;for(auto& b:bubbles)b.open=g_chatReasoningAll;relayoutBubbles();InvalidateRect(reasoningButton,nullptr,FALSE);return 0;}
        if(id>=10&&id<18){g_chatReasoningAgent=g_chatReasoningAgent==id-10?-1:id-10;layout();return 0;}
        if(id==18){HMENU menu=CreatePopupMenu();for(int i=0;i<agentCount();++i)AppendMenuW(menu,MF_STRING|(g_chatReasoningAgent==i?MF_CHECKED:0),(UINT_PTR)(i+1),(wide(chatAgentName((size_t)i))+L"  \u00B7  "+wide(g_teamMode?g_teamRoleIds[teamRoleSlot((size_t)i)]:"ark")).c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,100,L"Hide latest reasoning");
            RECT bounds{};GetWindowRect(moreButton,&bounds);const int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,bounds.left,bounds.bottom,0,w,nullptr);DestroyMenu(menu);
            if(chosen==100)g_chatReasoningAgent=-1;else if(chosen>0)g_chatReasoningAgent=chosen-1;layout();return 0;}}
    if(m==WM_DRAWITEM){auto* item=(DRAWITEMSTRUCT*)lp;if(item->CtlType==ODT_BUTTON){const int id=(int)item->CtlID;const bool active=(id==5&&g_chatReasoningAll)||(id>=10&&id<18&&g_chatReasoningAgent==id-10)||(id==18&&g_chatReasoningAgent>=0);
        fill(item->hDC,item->rcItem,background);roundBox(item->hDC,item->rcItem,active?th::rgb(th::over(th::raised,th::active)):buttonBg,active?accent:buttonEdge,10);
        wchar_t text[80]{};GetWindowTextW(item->hwndItem,text,80);RECT r=item->rcItem;
        if(id>=10&&id<18){HBRUSH dot=CreateSolidBrush(agentState(id-10));HPEN pen=CreatePen(PS_SOLID,1,agentState(id-10));auto ob=SelectObject(item->hDC,dot),op=SelectObject(item->hDC,pen);const int cy=(r.top+r.bottom)/2;Ellipse(item->hDC,r.left+8,cy-3,r.left+15,cy+4);SelectObject(item->hDC,op);SelectObject(item->hDC,ob);DeleteObject(pen);DeleteObject(dot);r.left+=12;}
        label(item->hDC,text,r,labelFont,active?accent:ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&item->rcItem);return TRUE;}}
    if(m==WM_NCDESTROY){window=viewport=goalEdit=copyButton=latestButton=detailsButton=expandButton=reasoningButton=moreButton=thoughtPanel=nullptr;chips={};for(auto f:{font,titleFont,labelFont,thoughtFont})if(f)DeleteObject(f);font=titleFont=labelFont=thoughtFont=nullptr;bubbles.clear();cached.clear();width=height=scroll=total=0;lastGoal.clear();lastActivity.clear();panelCaption.clear();panelText.clear();seenSerial=0;seenAgent=-2;return DefWindowProcW(w,m,wp,lp);}
    return DefWindowProcW(w,m,wp,lp);
}
static void create(){
    if(window)return;if(!richEdit)richEdit=LoadLibraryW(L"Msftedit.dll");if(!richEdit)return;
    WNDCLASSW c{};c.hInstance=GetModuleHandle(nullptr);c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.lpfnWndProc=panelProc;c.lpszClassName=L"ArkConversation";RegisterClassW(&c);c.lpfnWndProc=viewportProc;c.lpszClassName=L"ArkConversationScroll";RegisterClassW(&c);
    window=CreateWindowExW(0,L"ArkConversation",L"Agent conversation",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,g_window,nullptr,c.hInstance,nullptr);if(!window)return;
    font=CreateFontW(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");titleFont=CreateFontW(-19,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");labelFont=CreateFontW(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");thoughtFont=CreateFontW(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    goalEdit=edit(window,L"Describe what you want to achieve.",background);
    viewport=CreateWindowExW(0,L"ArkConversationScroll",L"Messages",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,120,1,1,window,nullptr,c.hInstance,nullptr);
    auto button=[&](const wchar_t* title,int id){return CreateWindowExW(0,L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,(HMENU)(INT_PTR)id,c.hInstance,nullptr);};
    copyButton=button(L"Copy chat",1);latestButton=button(L"Latest",2);detailsButton=button(L"Agents",3);expandButton=button(L"Expand",4);reasoningButton=button(L"Show reasoning",5);moreButton=button(L"More",18);
    for(int i=0;i<8;++i)chips[(size_t)i]=button(L"",10+i);
    thoughtPanel=CreateWindowExW(0,L"RICHEDIT50W",L"",WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,1,1,window,nullptr,c.hInstance,nullptr);
    if(thoughtPanel){SendMessage(thoughtPanel,EM_EXLIMITTEXT,0,4*1024*1024);SendMessage(thoughtPanel,WM_SETFONT,(WPARAM)thoughtFont,FALSE);SendMessage(thoughtPanel,EM_SETBKGNDCOLOR,0,thoughtBg);
        CHARFORMAT2W format{};format.cbSize=sizeof format;format.dwMask=CFM_COLOR;format.crTextColor=muted;SendMessage(thoughtPanel,EM_SETCHARFORMAT,SCF_DEFAULT,(LPARAM)&format);SendMessage(thoughtPanel,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&format);SendMessage(thoughtPanel,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));SetWindowSubclass(thoughtPanel,textProc,1,0);}
}
static int buttonWidth(HWND control,int minimum){wchar_t text[80]{};GetWindowTextW(control,text,80);HDC dc=GetDC(window);auto old=SelectObject(dc,labelFont);SIZE size{};GetTextExtentPoint32W(dc,text,(int)wcslen(text),&size);SelectObject(dc,old);ReleaseDC(window,dc);return std::max(minimum,(int)size.cx+22);}
static void setText(HWND control,const std::wstring& text){wchar_t current[80]{};GetWindowTextW(control,current,80);if(text!=current){SetWindowTextW(control,text.c_str());InvalidateRect(control,nullptr,FALSE);}}
// Header rows: status title, actions (beside the title when wide), goal,
// reasoning controls and the optional latest-reasoning panel for one agent.
static void layoutHeader(){
    const int w=width,h=height;wideHeader=w>=560;titleY=2;
    const int actionsY=wideHeader?2:32;goalTop=wideHeader?34:62;
    {const int bw=wideHeader?74:std::clamp((w-28-3*6)/4,56,74);int x=wideHeader?w-14-4*74-3*6:14;for(HWND control:{copyButton,latestButton,detailsButton,expandButton}){MoveWindow(control,x,actionsY+1,bw,26,TRUE);x+=bw+6;}}
    MoveWindow(goalEdit,62,goalTop,std::max(1,w-76),120,FALSE);
    // The goal is also the first message: short docks keep one scrollable line.
    const int goalLines=wideHeader?3:h>=520?2:1;int natural=textHeight(goalEdit,goalLines);
    if(goalLines==1&&SendMessage(goalEdit,EM_GETLINECOUNT,0,0)>1){POINT first{},second{};SendMessage(goalEdit,EM_POSFROMCHAR,(WPARAM)&first,0);SendMessage(goalEdit,EM_POSFROMCHAR,(WPARAM)&second,SendMessage(goalEdit,EM_LINEINDEX,1,0));natural=std::max(24,(int)(second.y-first.y)+2);}
    const int goalCap=std::clamp(h-goalTop-6-28-8-80,24,110);const int goalHeight=std::min(natural,goalCap);
    ShowScrollBar(goalEdit,SB_VERT,textHeight(goalEdit)>goalHeight);MoveWindow(goalEdit,62,goalTop,std::max(1,w-76),goalHeight,TRUE);
    reasonTop=goalTop+goalHeight+6;
    // Reasoning row: show-all toggle, then one chip per agent; overflow goes to More.
    setText(reasoningButton,g_chatReasoningAll?L"Hide reasoning":L"Show reasoning");
    int x=14;const int reasonW=buttonWidth(reasoningButton,104);MoveWindow(reasoningButton,x,reasonTop,reasonW,26,TRUE);x+=reasonW+10;
    const int count=agentCount(),moreW=58;int shown=0;
    for(int i=0;i<8;++i){HWND chip=chips[(size_t)i];if(i>=count){ShowWindow(chip,SW_HIDE);continue;}setText(chip,wide(chatAgentName((size_t)i)));const int cw=buttonWidth(chip,54)+10;
        const bool fits=x+cw<=w-14-(i+1<count?moreW+6:0);if(fits&&shown==i){MoveWindow(chip,x,reasonTop,cw,26,TRUE);ShowWindow(chip,SW_SHOWNA);x+=cw+6;++shown;}else ShowWindow(chip,SW_HIDE);}
    if(shown<count){setText(moreButton,L"+"+std::to_wstring(count-shown));MoveWindow(moreButton,x,reasonTop,moreW,26,TRUE);ShowWindow(moreButton,SW_SHOWNA);}else ShowWindow(moreButton,SW_HIDE);
    int header=reasonTop+26+8;
    // The Agents view keeps its room for cards; their Reasoning links return to the chat.
    std::wstring caption,body;panelShown=thoughtPanel&&!(g_teamMode&&g_chatAgentDetails)&&agentReasoning(g_chatReasoningAgent,caption,body);
    if(panelShown){captionTop=header-2;panelTop=captionTop+20;panelCaption=caption;
        if(body!=panelText){const bool atEnd=[]{SCROLLINFO si{sizeof si,SIF_ALL};if(!GetScrollInfo(thoughtPanel,SB_VERT,&si))return true;return si.nPos+(int)si.nPage>=si.nMax-2;}();SetWindowTextW(thoughtPanel,crlf(body).c_str());panelText=body;if(atEnd)SendMessage(thoughtPanel,WM_VSCROLL,SB_BOTTOM,0);}
        MoveWindow(thoughtPanel,14,panelTop,std::max(1,w-28),120,FALSE);
        // Whole lines only (at least one); the panel scrolls for the rest.
        int lineHeight=21;{HDC dc=GetDC(thoughtPanel);auto old=SelectObject(dc,thoughtFont);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(thoughtPanel,dc);lineHeight=std::max(12,(int)metrics.tmHeight);}
        if(SendMessage(thoughtPanel,EM_GETLINECOUNT,0,0)>1){POINT first{},second{};SendMessage(thoughtPanel,EM_POSFROMCHAR,(WPARAM)&first,0);SendMessage(thoughtPanel,EM_POSFROMCHAR,(WPARAM)&second,SendMessage(thoughtPanel,EM_LINEINDEX,1,0));if(second.y>first.y)lineHeight=(int)(second.y-first.y);}
        const int room=std::max(lineHeight+3,std::min(280,(h-panelTop-8-80)*45/100)),needed=textHeight(thoughtPanel);
        panelHeight=std::clamp((std::min(needed,room)-3)/lineHeight,1,64)*lineHeight+3;
        MoveWindow(thoughtPanel,14,panelTop,std::max(1,w-28),panelHeight,TRUE);ShowWindow(thoughtPanel,SW_SHOWNA);header=panelTop+panelHeight+8;}
    else{ShowWindow(thoughtPanel,SW_HIDE);panelText.clear();}
    g_chatHeaderHeight=header;
}
// Cheap change key for the latest-reasoning panel (team roles are polled snapshots).
static std::string lastPanelKey;
static std::string panelKey(){
    const int agent=g_chatReasoningAgent;if(agent<0||agent>=agentCount())return {};
    if(!g_teamMode)return std::to_string(g_chatReasoningSerial)+(g_chatBusy?"b":"-")+(g_assistantRunStarted?"s":"-");
    const AgentTeamRoleSnapshot* role=(size_t)agent<g_teamSnapshot.roles.size()?&g_teamSnapshot.roles[(size_t)agent]:nullptr;if(!role)return "none";
    const auto& text=role->reasoning;return std::to_string(text.size())+"/"+std::to_string(role->reasoningTurn)+"/"+std::to_string((int)role->state)+"/"+std::to_string(role->turns)+"/"+text.substr(text.size()>32?text.size()-32:0);
}
static void sync(bool force=false){
    if(!g_assistantDockOpen||g_toolsOpen||g_chatDockRail||!g_window){if(window)ShowWindow(window,SW_HIDE);return;}
    if(g_headless&&!testing)return;
    create();if(!window)return;
    // Observe the old page before resizing it, or Restore/Expand can wrongly
    // treat somebody reading an earlier message as following the newest one.
    const bool following=scroll>=std::max(0,total-pageHeight()-8);
    size_t anchor=0;while(anchor+1<bubbles.size()&&bubbles[anchor].box.bottom<=scroll)++anchor;
    const int anchorOffset=bubbles.empty()?0:scroll-bubbles[anchor].box.top;
    const auto dock=chatDockGeometry();
    const int x=dock.x+8,y=dock.chatTop;
    int w=std::max(180,dock.w-16),h=std::max(74,dock.chatBottom-dock.chatTop);bool details=g_teamMode&&g_chatAgentDetails;
    bool resized=width!=w||height!=h;width=w;height=h;
    auto currentGoal=goal();bool goalChanged=currentGoal!=lastGoal;
    if(goalChanged){SetWindowTextW(goalEdit,crlf(wide(currentGoal.empty()?"Describe what you want to achieve.":currentGoal)).c_str());lastGoal=currentGoal;}
    static bool lastDetails=false;const auto key=panelKey()+(details?"#cards":"");const bool reasoningChanged=seenSerial!=g_chatReasoningSerial||seenAgent!=g_chatReasoningAgent||key!=lastPanelKey||details!=lastDetails;lastPanelKey=key;lastDetails=details;
    const int previousHeader=g_chatHeaderHeight;
    if(resized||goalChanged||force||reasoningChanged){
        layoutHeader();resized|=g_chatHeaderHeight!=previousHeader;
        if(goalChanged){POINT origin{};SendMessage(goalEdit,EM_SETSCROLLPOS,0,(LPARAM)&origin);}
    }
    RECT current{};GetWindowRect(window,&current);POINT origin{current.left,current.top};ScreenToClient(g_window,&origin);
    int shownHeight=details?g_chatHeaderHeight:h;if(origin.x!=x||origin.y!=y||current.right-current.left!=w||current.bottom-current.top!=shownHeight)MoveWindow(window,x,y,w,shownHeight,TRUE);
    ShowWindow(detailsButton,SW_SHOWNA);
    if(resized||force)MoveWindow(viewport,0,g_chatHeaderHeight,w,std::max(1,h-g_chatHeaderHeight),TRUE);ShowWindow(viewport,details?SW_HIDE:SW_SHOWNA);ShowWindow(window,SW_SHOWNA);
    const auto now=GetTickCount64();static ULONGLONG next=0;if(!force&&!resized&&!reasoningChanged&&now<next)return;next=now+100;
    static std::wstring buttonDetails,buttonExpand;std::wstring d=!g_teamMode?L"Run info":details?L"Chat":L"Agents",e=g_chatExpanded?L"Restore":L"Expand";if(d!=buttonDetails){SetWindowTextW(detailsButton,d.c_str());buttonDetails=d;}if(e!=buttonExpand){SetWindowTextW(expandButton,e.c_str());buttonExpand=e;}
    auto a=activity();auto status=a.title+"\n"+a.detail;bool statusChanged=status!=lastActivity;if(statusChanged){lastActivity=status;for(HWND chip:chips)if(chip&&IsWindowVisible(chip))InvalidateRect(chip,nullptr,FALSE);}
    // A reasoning note can arrive for an unchanged row; compare notes too.
    const bool notesChanged=seenSerial!=g_chatReasoningSerial;seenSerial=g_chatReasoningSerial;seenAgent=g_chatReasoningAgent;
    bool changed=cached!=g_chatLog||notesChanged;
    if(changed){size_t prefix=0;if(!notesChanged)while(prefix<cached.size()&&prefix<g_chatLog.size()&&cached[prefix]==g_chatLog[prefix])++prefix;
        while(bubbles.size()>g_chatLog.size()){DestroyWindow(bubbles.back().edit);if(bubbles.back().thought)DestroyWindow(bubbles.back().thought);bubbles.pop_back();}
        for(size_t i=prefix;i<g_chatLog.size();++i){
            const auto& row=g_chatLog[i];if(i==bubbles.size()){bubbles.emplace_back();bubbles.back().open=g_chatReasoningAll;}auto& b=bubbles[i];
            const bool user=row.first.rfind("You",0)==0;auto role=wide(row.first),text=wide(messageText(row));
            const auto* note=user?nullptr:chatReasoningNote(i);const bool hasNote=note!=nullptr;std::wstring reasoning=note?wide(note->text):std::wstring(),reasoningEmpty=note?wide(note->empty):std::wstring();
            if(b.hasNote!=hasNote||b.reasoning!=reasoning||b.reasoningEmpty!=reasoningEmpty){b.hasNote=hasNote;b.reasoningEmpty=std::move(reasoningEmpty);b.thoughtWidth=0;
                if(b.reasoning!=reasoning){b.reasoning=std::move(reasoning);if(b.thought){DestroyWindow(b.thought);b.thought=nullptr;}}}
            if(b.role==role&&b.text==text)continue;
            if(b.edit){CHARRANGE selection{};SendMessage(b.edit,EM_EXGETSEL,0,(LPARAM)&selection);
                SetWindowTextW(b.edit,crlf(text).c_str());const LONG length=GetWindowTextLengthW(b.edit);
                selection.cpMin=std::clamp(selection.cpMin,0L,length);selection.cpMax=std::clamp(selection.cpMax,selection.cpMin,length);
                SendMessage(b.edit,EM_EXSETSEL,0,(LPARAM)&selection);SendMessage(b.edit,EM_SETBKGNDCOLOR,0,user?userBg:agentBg);}
            b.user=user;b.role=std::move(role);b.text=std::move(text);b.measuredWidth=0;
        }
        cached=g_chatLog;SetWindowTextW(copyButton,L"Copy chat");
    }
    // Live resize keeps the settled bubbles; the release re-measures (sync(true)).
    if((changed||resized)&&!(g_chatDockResizing&&!force&&!changed)){measure();if(following)scroll=total;
        else if(anchor<bubbles.size())scroll=bubbles[anchor].box.top+std::min(anchorOffset,(int)(bubbles[anchor].box.bottom-bubbles[anchor].box.top)-1);
        place();}
    // The GL parent and hidden-start document loading can validate a 1px native
    // region before the dock is shown. Explicitly repaint the parent header;
    // child invalidation is needed only for layout/content changes.
    if(changed||resized||goalChanged||force||statusChanged||reasoningChanged)
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|((changed||resized||goalChanged||force||reasoningChanged)?RDW_ALLCHILDREN:RDW_NOCHILDREN));
}
} // namespace assistant_chat
