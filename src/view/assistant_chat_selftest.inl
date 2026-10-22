// Render the actual native controls, including children of a hidden test window.
// PrintWindow alone can return an all-black success for a hidden child HWND.
static void captureChat(HWND window,const std::string& path){
    RECT r{};GetClientRect(window,&r);int w=r.right,h=r.bottom;HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,bitmap);
    std::function<void(HWND)> render=[&](HWND control){RECT bounds{};GetClientRect(control,&bounds);int saved=SaveDC(dc);IntersectClipRect(dc,0,0,bounds.right,bounds.bottom);SendMessageW(control,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND);for(HWND child=GetWindow(control,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){if(!(GetWindowLongPtr(child,GWL_STYLE)&WS_VISIBLE))continue;POINT offset{};MapWindowPoints(child,control,&offset,1);int childState=SaveDC(dc);OffsetViewportOrgEx(dc,offset.x,offset.y,nullptr);render(child);RestoreDC(dc,childState);}RestoreDC(dc,saved);};render(window);
    Image image;image.w=w;image.h=h;image.px.resize((size_t)w*h*3);for(size_t i=0;i<(size_t)w*h;++i){auto* p=(unsigned char*)pixels+i*4;image.px[i*3]=p[2];image.px[i*3+1]=p[1];image.px[i*3+2]=p[0];}writePNG(path.c_str(),image);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);
}
static LRESULT CALLBACK chatScrollPaintProbe(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){if(m==WM_PAINT)++*(int*)data;return DefSubclassProc(w,m,wp,lp);}
static int testAssistantChat(const std::string& path){
    using namespace assistant_chat;
    auto fail=[&](int code,const char* text){std::fprintf(stderr,"Conversation check %d: %s\n",code,text);return code;};
    testing=true;g_assistantDockOpen=true;g_toolsOpen=false;g_teamMode=false;g_chatBusy=false;g_assistantResponseFinished=false;g_assistantResponseError=false;*g_assistantCancel=false;g_activeToolName.clear();
    if(!assistantResumeRequest(" Resume! ")||!assistantResumeRequest("continua")||assistantResumeRequest("resume o estado do projeto")||assistantResumeRequest("summarize"))return fail(835,"Explicit continuation must differ from a summary request");
    g_singleGoal="Improve the equipment display while preserving its mechanical assembly and existing connections.";
    g_chatLog={{"You","Make the display easier to read.\n\nKeep the current mechanical assembly."},{"ARK","I will inspect the display and its connections first.\n\nThe mechanical assembly will stay editable."},{"Action","Inspecting the selected display and its authored references."},{"ARK","The display layout is ready.\n\nPlease confirm the screen dimensions before I continue.\nPortugu\xc3\xaas: a\xc3\xa7\xc3\xa3o, dimens\xc3\xb5" "es / \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e."}};
    if(activity().title!="Ready")return fail(801,"Initial state");
    g_chatBusy=true;g_assistantStatus="Thinking...";if(activity().title!="Thinking")return fail(802,"Provider phase");
    g_activeToolName="object_context";if(activity().title!="Working")return fail(803,"Tool phase");g_activeToolName.clear();
    g_assistantStatus="HTTP 503 / retry 1";if(activity().title!="Waiting for the connection")return fail(804,"Retry phase");
    *g_assistantCancel=true;if(activity().title!="Stopping")return fail(805,"Cancellation in flight");g_chatBusy=false;g_assistantResponseFinished=true;if(activity().title!="Stopped")return fail(806,"Cancellation complete");
    *g_assistantCancel=false;g_assistantResponseError=true;if(activity().title!="Interrupted")return fail(807,"Failure phase");g_assistantResponseError=false;
    if(activity().title!="Reply ready")return fail(808,"A reply is not proof of goal completion");
    g_chatLog.back().second="review_pending: BOM review pending. Two definitions lack internal identity; previous CAD work is retained.";
    if(activity().title!="BOM review pending"||messageText(g_chatLog.back()).rfind("review_pending:",0)==0)return fail(834,"Recoverable BOM stop must not appear as Interrupted or completed");
    layout();sync(true);captureChat(window,path+".bom-review-pending.png");
    g_chatLog.back().second="needs_input: What are the display width and height?\n\nThe existing mounting points have been retained.";
    if(activity().title!="Waiting for your input")return fail(809,"Explicit input request");
    layout();sync(true);if(!window||bubbles.size()!=g_chatLog.size())return fail(810,"Native controls unavailable");
    for(const auto& b:bubbles)if(b.bodyHeight>250)return fail(828,"Short messages incorrectly measured as tall blank balloons");
    // Native selection verifies Unicode/newlines without replacing the user's clipboard.
    scroll=bubbles[1].box.top;place();HWND text=bubbles[1].edit;SendMessage(text,EM_SETSEL,0,-1);std::vector<wchar_t> selected(10000);SendMessageW(text,EM_GETSELTEXT,0,(LPARAM)selected.data());
    if(std::wstring(selected.data()).find(L"mechanical assembly")==std::wstring::npos)return fail(811,"Selectable message");
    if(!(GetWindowLongPtr(text,GWL_STYLE)&ES_READONLY))return fail(812,"Transcript must be read-only");
    auto copied=transcript();if(copied.find(L"GOAL\r\n")!=0||copied.find(L"\r\n\r\nKeep the current") == std::wstring::npos)return fail(813,"Copy preserves goal and paragraphs");
    for(auto size:std::vector<std::pair<int,int>>{{1440,900},{1100,760}}){
        RECT frame{0,0,size.first,size.second};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=size.first;winH=size.second;layout();sync(true);
        if(pageHeight()<38)return fail(814,"Compact conversation viewport too small");
        RECT goalBounds{};GetClientRect(goalEdit,&goalBounds);POINT tail{};SendMessage(goalEdit,EM_POSFROMCHAR,(WPARAM)&tail,GetWindowTextLengthW(goalEdit)-1);if(tail.y+22>goalBounds.bottom)return fail(829,"Goal last line is clipped");
        scroll=0;place();RECT before{};GetWindowRect(goalEdit,&before);SendMessage(viewport,WM_VSCROLL,SB_BOTTOM,0);RECT after{};GetWindowRect(goalEdit,&after);if(!EqualRect(&before,&after)||scroll<=0)return fail(815,"Goal must remain pinned while scrolling");
        captureChat(window,path+".conversation-"+std::to_string(size.first)+".png");
    }
    scroll=0;place();int reading=scroll;g_chatLog.push_back({"ARK","New progress must not pull you away from the message you are reading."});sync(true);if(scroll!=reading)return fail(816,"Scroll was stolen by new progress");
    SendMessage(window,WM_COMMAND,2,0);if(scroll!=std::max(0,total-pageHeight()))return fail(817,"Latest button");
    g_chatLog.push_back({"ARK",std::string(18000,'W')+"\nEND OF COMPLETE MESSAGE"});sync(true);if(transcript().find(L"END OF COMPLETE MESSAGE")==std::wstring::npos||GetWindowTextLengthW(bubbles.back().edit)<18000)return fail(818,"Long message was truncated");
    g_chatLog.pop_back();sync(true);
    // Exercise real WM_PAINT after wheel/scrollbar moves. WM_PRINTCLIENT alone
    // always produces fresh pixels and cannot detect stale on-screen child text.
    {auto savedLog=g_chatLog;std::string longReply;for(int i=0;i<90;++i)longReply+="Paragraph "+std::to_string(i)+": readable scrolling text with enough width to wrap naturally.\n\n";
     g_chatLog={{"You","Inspect the scroll without changing this fixture."},{"ARK",longReply}};sync(true);scroll=bubbles.back().box.top+30;place();HWND reader=bubbles.back().edit;if(!reader)return fail(830,"Scrollable text control missing");
     bool wasVisible=IsWindowVisible(g_window)!=FALSE;ShowWindow(g_window,SW_SHOWNOACTIVATE);RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
     int paints=0;SetWindowSubclass(reader,chatScrollPaintProbe,2,(DWORD_PTR)&paints);
     for(int i=0;i<12;++i){paints=0;SendMessage(reader,WM_MOUSEWHEEL,MAKEWPARAM(0,(SHORT)(i%2?WHEEL_DELTA:-WHEEL_DELTA)),0);if(!paints)return fail(831,"Wheel left visible RichEdit pixels unpainted");}
     paints=0;SendMessage(viewport,WM_VSCROLL,SB_LINEDOWN,0);if(!paints)return fail(832,"Scrollbar left visible RichEdit pixels unpainted");
     SendMessage(reader,EM_SETSEL,0,24);CHARRANGE selection{};SendMessage(reader,EM_EXGETSEL,0,(LPARAM)&selection);SendMessage(viewport,WM_VSCROLL,SB_LINEDOWN,0);CHARRANGE after{};SendMessage(reader,EM_EXGETSEL,0,(LPARAM)&after);if(selection.cpMin!=after.cpMin||selection.cpMax!=after.cpMax)return fail(833,"Scroll repaint changed selection");
     captureChat(window,path+".scroll-long-reply.png");RemoveWindowSubclass(reader,chatScrollPaintProbe,2);if(!wasVisible)ShowWindow(g_window,SW_HIDE);g_chatLog=std::move(savedLog);sync(true);}
    for(int i=0;i<220;++i)g_chatLog.push_back({"Action","Progress message "+std::to_string(i)});sync(true);if(bubbles.size()!=g_chatLog.size()||std::count_if(bubbles.begin(),bubbles.end(),[](const auto& b){return b.edit!=nullptr;})>8)return fail(827,"Long history was truncated or native controls were not virtualized");
    g_chatLog.resize(5);sync(true);
    g_teamMode=true;g_teamStarted=true;g_teamGoal="Review the display, its mounts and the cable connections.";g_teamSnapshot={};
    if(activity().title!="Stopped / awaiting continuation")return fail(819,"Idle team falsely completed");
    g_teamSnapshot.running=true;if(activity().title!="Thinking")return fail(820,"Active team");
    g_teamSnapshot.paused=true;if(activity().title!="Paused")return fail(821,"Paused team");g_teamSnapshot.paused=false;g_teamSnapshot.running=false;
    g_teamSnapshot.goalDeclared=true;g_teamSnapshot.goalState=AgentGoalState::NeedsInput;g_teamSnapshot.goalSummary="Confirm the available clearance behind the screen.";if(activity().title!="Waiting for your input")return fail(822,"Team needs input");
    sync(true);captureChat(window,path+".team-needs-input.png");
    g_teamSnapshot.goalState=AgentGoalState::Completed;if(activity().title!="Goal reported complete")return fail(823,"Explicit team completion");
    SendMessage(window,WM_COMMAND,3,0);sync(true);if(!g_chatAgentDetails||(GetWindowLongPtr(viewport,GWL_STYLE)&WS_VISIBLE))return fail(824,"Agent controls toggle");
    SendMessage(window,WM_COMMAND,3,0);if(!g_chatExpanded)SendMessage(window,WM_COMMAND,4,0);sync(true);if(!g_chatExpanded||pageHeight()<200||winH-32-assistantDockHeight()<viewY)return fail(825,"Expanded conversation");scroll=0;place();captureChat(window,path+".expanded.png");
    g_assistantDockOpen=false;sync(true);if(GetWindowLongPtr(window,GWL_STYLE)&WS_VISIBLE)return fail(826,"Hidden dock still visible");
    g_assistantDockOpen=true;g_chatExpanded=false;g_teamStarted=false;g_teamMode=false;sync(true);g_teamSnapshot={};DestroyWindow(window);testing=false;
    std::puts("Conversation UI: status lifecycle, pinned goal, selection, Unicode, paragraphs, long reply, scroll retention, latest, agents and expansion passed.");return 0;
}
