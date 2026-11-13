// Right assistant dock: rail / normal / expanded layout at 96, 144 and 192 dpi
// with a mock conversation carrying provider reasoning (and a provider without
// it), a two-role team and a real offline team read by one late poll.
// Captures composite the GL frame with the real native conversation controls.
static Image captureChatDockFrame(const std::string& file){
    g_swap=false;HDC frameDc=GetDC(g_window);drawFrame(frameDc);glFinish();ReleaseDC(g_window,frameDc);
    std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);
    // Native controls are outside OpenGL. Render them through WM_PRINTCLIENT /
    // WM_PRINT (valid for a hidden test window) and composite at their position.
    auto composite=[&](HWND control,bool edit){
        if(!control||!(GetWindowLongPtr(control,GWL_STYLE)&WS_VISIBLE))return;RECT bounds{};GetWindowRect(control,&bounds);const int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;if(w<=0||h<=0)return;
        POINT origin{bounds.left,bounds.top};ScreenToClient(g_window,&origin);HDC screen=GetDC(control),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);
        if(edit){RedrawWindow(control,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_FRAME);SendMessageW(control,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);}
        else{std::function<void(HWND)> render=[&](HWND item){RECT r{};GetClientRect(item,&r);int saved=SaveDC(dc);IntersectClipRect(dc,0,0,r.right,r.bottom);SendMessageW(item,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND);for(HWND child=GetWindow(item,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){if(!(GetWindowLongPtr(child,GWL_STYLE)&WS_VISIBLE))continue;POINT offset{};MapWindowPoints(child,item,&offset,1);int state=SaveDC(dc);OffsetViewportOrgEx(dc,offset.x,offset.y,nullptr);render(child);RestoreDC(dc,state);}RestoreDC(dc,saved);};render(control);}
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int dx=origin.x+x,dy=origin.y+y;if(dx<0||dy<0||dx>=winW||dy>=winH)continue;auto* src=(u8*)bits+((size_t)y*w+x)*4;auto* dst=image.px.data()+((size_t)dy*winW+dx)*3;dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];}
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(control,screen);};
    composite(assistant_chat::window,false);composite(g_chatInput,true);for(auto edit:g_assistantImageDimensionEdits)composite(edit,true);
    writePNG(file.c_str(),image);return image;
}
static int testChatDock(const std::string& path){
    using namespace assistant_chat;
    int dpiNow=96;
    auto fail=[&](int code,const std::string& text){const std::string detail=text+" at "+std::to_string(dpiNow)+" dpi";std::fprintf(stderr,"Chat dock check %d: %s\n",code,detail.c_str());std::ofstream(path+".chat-dock-test.log")<<"FAIL "<<code<<": "<<detail<<'\n';g_chatDockDpiOverride=0;return code;};
    auto resize=[&](int w,int h){RECT frame{0,0,w,h};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=w;winH=h;layout();};
    auto zone=[&](int id)->const Zone*{auto found=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});return found==g_zones.end()?nullptr:&*found;};
    auto draw=[&]{HDC dc=GetDC(g_window);g_swap=false;drawFrame(dc);ReleaseDC(g_window,dc);};
    auto click=[&](int id){draw();const Zone* z=zone(id);if(!z)return false;const int x=(int)(z->x+z->w*.5f),y=(int)(z->y+z->h*.5f);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
    auto windowRect=[&](HWND control){RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,g_window,(POINT*)&r,2);return r;};
    auto panelText=[&]{std::wstring text((size_t)GetWindowTextLengthW(thoughtPanel)+1,L'\0');GetWindowTextW(thoughtPanel,text.data(),(int)text.size());text.resize(wcslen(text.c_str()));return text;};
    // Dock widths derived independently from the documented rule: DPI-scaled
    // preferences (rail 44, minimum 340, normal 400) inside physical limits (a
    // 460 px viewport reserve beside the sidebar and tool docks, 340 px floor).
    struct Expected {int rail=0,min=0,max=0,normal=0,expanded=0,room=0;};
    auto expected=[&](int dpi){Expected e;const int sidebar=std::clamp((int)(winW*.21),280,352);e.room=winW-sidebar-chatDockToolDocks()-460;e.rail=MulDiv(44,dpi,96);
        e.min=std::clamp(MulDiv(340,dpi,96),340,std::max(340,e.room));e.max=std::max(e.min,e.room);
        e.normal=std::clamp(g_chatDockUserWidth>0?(int)std::lround(g_chatDockUserWidth*dpi/96.):MulDiv(400,dpi,96),e.min,e.max);e.expanded=std::clamp((int)(winW*.48),e.normal,e.max);return e;};
    testing=true;g_testPointer=POINT{-50,-50};g_assistantDockOpen=true;g_toolsOpen=false;g_teamMode=false;g_teamStarted=false;g_chatBusy=false;g_assistantResponseFinished=true;g_assistantResponseError=false;*g_assistantCancel=false;g_activeToolName.clear();
    g_chatDockUserWidth=0;setChatDockMode(1);g_chatReasoningAll=false;g_chatReasoningAgent=-1;g_chatAgentDetails=false;g_sidebarTab=0;g_toolModule=1;
    HFONT inputFont=nullptr;const bool ownInput=!g_chatInput;
    if(ownInput){g_chatInput=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,1,1,g_window,(HMENU)499,GetModuleHandleW(nullptr),nullptr);inputFont=CreateFontW(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");SendMessage(g_chatInput,WM_SETFONT,(WPARAM)inputFont,TRUE);SendMessage(g_chatInput,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));}
    SetWindowTextW(g_chatInput,L"Add a gusset and keep the two M6 holes.");
    const std::string summary="Read the current revision before writing.\n\nA 4 mm flange keeps the bracket stiff for the 12 kg load; the M6 holes stay 40 mm apart as requested.";
    const char* modes[]={"rail","normal","expanded"};std::ostringstream receipt;
    // Every layout check runs at 96, 144 and 192 dpi through the self-test DPI hook.
    for(const int dpi:{96,144,192}){
    g_chatDockDpiOverride=dpiNow=dpi;const std::string dpiTag=dpi==96?"":"-"+std::to_string(dpi)+"dpi";
    // Mock single conversation: one connector without readable reasoning, then a
    // reply whose provider returned a reasoning summary. Nothing is synthesized
    // by the UI: the notes below stand in for provider-returned text.
    g_teamMode=false;g_teamStarted=false;g_teamSnapshot={};g_chatAgentDetails=false;g_chatDockUserWidth=0;g_chatReasoningAll=false;g_chatReasoningAgent=-1;
    g_singleGoal="Design a wall bracket for a 12 kg shelf with two M6 mounting holes.";
    g_chatLog={{"You","Design a wall bracket for a 12 kg shelf with two M6 mounting holes."},{"Action","Tool: doc_info"},{"ARK","Created an editable L-bracket concept with two M6 holes.\n\nAssumption: steel, 3 mm, not yet production-qualified."},{"You","Make the flange thicker and add a gusset."},{"Action","Tool: doc_apply"},{"ARK","Flange is now 4 mm and a centred gusset was added. The hole pattern is unchanged."}};
    g_chatReasoningNotes.clear();attachChatReasoning(2,{},chatReasoningHidden);attachChatReasoning(5,summary,chatReasoningHidden);
    g_assistantRunStarted=true;g_assistantReasoningRun=summary;++g_chatReasoningSerial;
    for(auto size:std::vector<std::pair<int,int>>{{1280,800},{1920,1080}}){
        resize(size.first,size.second);
        for(int mode=0;mode<3;++mode){
            setChatDockMode(mode);g_chatReasoningAgent=mode==1?0:-1;layout();sync(true);
            if(mode==1&&!bubbles.empty()&&!bubbles.back().open)toggleReasoning(bubbles.size()-1);
            const std::string tag=std::string(modes[mode])+"-"+std::to_string(size.first)+"x"+std::to_string(size.second)+dpiTag;
            captureChatDockFrame(path+".dock-"+tag+".png");
            const auto dock=chatDockGeometry();const auto e=expected(dpi);
            if(!dock.open||dock.x!=winW-assistantDockWidth()||dock.y!=viewY||dock.y+dock.h!=winH-32)return fail(1,"Dock must span from the ribbon to the status bar at the right edge ("+tag+")");
            if(viewX+viewW!=dock.x||viewH!=winH-viewY-32||viewW<(mode==2?300:400))return fail(2,"Viewport must end at the dock and stay usable ("+tag+", "+std::to_string(viewW)+"x"+std::to_string(viewH)+")");
            if(mode==0&&assistantDockWidth()!=e.rail)return fail(3,"Rail width ("+tag+")");
            if(mode==1&&assistantDockWidth()!=e.normal)return fail(41,"Normal width "+std::to_string(assistantDockWidth())+" differs from the rule's "+std::to_string(e.normal)+" ("+tag+")");
            if(mode==2&&assistantDockWidth()!=e.expanded)return fail(42,"Expanded width "+std::to_string(assistantDockWidth())+" differs from the rule's "+std::to_string(e.expanded)+" ("+tag+")");
            if(mode==2&&e.expanded>e.normal&&assistantDockWidth()<=chatDockNormalWidth())return fail(4,"Expanded dock must be wider than normal ("+tag+")");
            if(mode>0&&e.room>=e.min&&viewW<460)return fail(44,"Viewport lost its 460 px reserve ("+tag+", "+std::to_string(viewW)+")");
            // Dock controls stay inside the dock and never overlap one another.
            const std::set<int> dockIds{470,471,57,367,472,44013,44300,44301,44302,730,74,75,58,473,566,44010,44014,44011,44012,490,989};std::vector<Zone> owned;
            // Header icons reuse some action ids (Account 57); only below-ribbon zones belong to the dock.
            for(const auto& z:g_zones)if(z.y+z.h>viewY&&(dockIds.count(z.id)||(z.id>=44310&&z.id<44310+(int)teamCapacity)||(z.id>=44000&&z.id<44020+(int)teamCapacity))){owned.push_back(z);if(z.x<dock.x||z.x+z.w>winW||z.y<viewY||z.y+z.h>winH-32)return fail(5,"Dock control "+std::to_string(z.id)+" escapes the dock ("+tag+")");}
            for(size_t a=0;a<owned.size();++a)for(size_t b=a+1;b<owned.size();++b){const auto& p=owned[a];const auto& q=owned[b];if(p.x<q.x+q.w&&q.x<p.x+p.w&&p.y<q.y+q.h&&q.y<p.y+p.h)return fail(6,"Dock controls "+std::to_string(p.id)+" and "+std::to_string(q.id)+" overlap ("+tag+")");}
            // Navigation, viewport dock and status bar stay outside the conversation.
            for(const auto& z:g_zones)if((z.id==179||z.id==191||z.id==182||z.id==23||z.id==180||z.id==960||z.id==455||(z.id>=560&&z.id<=562))&&z.y>=viewY&&z.y<winH-32&&z.x+z.w>dock.x)return fail(7,"Viewport control "+std::to_string(z.id)+" overlaps the dock ("+tag+")");
            for(int id:{179,191})if(!zone(id))return fail(8,"View cube or Home control missing beside the dock ("+tag+")");
            {const auto bar=viewportBarLayout();if(!bar.visible||bar.x+bar.width>dock.x)return fail(8,"Viewport display bar hidden or under the dock ("+tag+", viewport "+std::to_string(viewW)+")");}
            if(!(zone(400)))return fail(9,"Header Assistant toggle missing ("+tag+")");
            const bool inputVisible=(GetWindowLongPtr(g_chatInput,GWL_STYLE)&WS_VISIBLE)!=0,chatVisible=window&&(GetWindowLongPtr(window,GWL_STYLE)&WS_VISIBLE);
            if(mode==0){if(inputVisible||chatVisible||!zone(44300))return fail(10,"Rail must hide the conversation and composer ("+tag+")");continue;}
            if(!inputVisible||!chatVisible)return fail(11,"Conversation or composer hidden ("+tag+")");
            // Expand is offered only when it widens the dock; otherwise both controls are inert.
            if(mode==1){const bool expandable=e.expanded>e.normal,nativeEnabled=IsWindowEnabled(expandButton)!=FALSE;
                if((zone(44302)!=nullptr)!=expandable||nativeEnabled!=expandable)return fail(43,std::string("Expand control must be ")+(expandable?"enabled":"disabled")+" when expanded is "+std::to_string(e.expanded)+" and normal "+std::to_string(e.normal)+" ("+tag+")");
                if(!expandable){uiAction(44302);SendMessage(window,WM_COMMAND,4,0);if(g_chatExpanded||assistantDockWidth()!=e.normal)return fail(43,"Inert Expand changed the dock ("+tag+")");layout();sync(true);}}
            if(mode==2&&!zone(44302))return fail(43,"Restore control missing in the expanded dock ("+tag+")");
            const RECT chat=windowRect(window),input=windowRect(g_chatInput);
            if(chat.left<dock.x||chat.right>winW||chat.top<dock.chatTop||chat.bottom>dock.chatBottom)return fail(12,"Native conversation escapes its dock rows ("+tag+")");
            if(input.left<dock.x||input.right>winW||input.top<dock.inputY||input.bottom>dock.inputY+dock.inputH)return fail(13,"Composer escapes its field ("+tag+")");
            if(pageHeight()<(size.second>=1000?250:100))return fail(14,"Message area too small: "+std::to_string(pageHeight())+" ("+tag+")");
            for(HWND control:{copyButton,latestButton,detailsButton,expandButton,reasoningButton}){const RECT r=windowRect(control);if(r.left<chat.left||r.right>chat.right||r.top<chat.top||r.bottom>chat.top+g_chatHeaderHeight)return fail(15,"Header control clipped ("+tag+")");}
            if(mode==1){
                const auto& replied=bubbles.back();if(!replied.hasNote||replied.reasoning.empty()||!replied.open||!replied.thought)return fail(16,"Readable reasoning section did not expand inside the reply ("+tag+")");
                if(!bubbles[2].hasNote||!bubbles[2].reasoning.empty())return fail(17,"Provider without readable reasoning must show the explicit note only ("+tag+")");
                if(!panelShown||!(GetWindowLongPtr(thoughtPanel,GWL_STYLE)&WS_VISIBLE))return fail(18,"Latest reasoning panel hidden ("+tag+")");
                if(panelText().find(L"4 mm flange")==std::wstring::npos)return fail(19,"Panel text is not the provider summary ("+tag+")");
            }
            receipt<<tag<<": dock "<<assistantDockWidth()<<" px (rule "<<(mode==0?e.rail:mode==1?e.normal:e.expanded)<<", expand "<<(e.expanded>e.normal?"available":"inert")<<"), viewport "<<viewW<<"x"<<viewH<<", page "<<(mode?pageHeight():0)<<"\n";
        }
    }
    // Reasoning toggles: per bubble click, show-all, and collapsed default.
    resize(1280,800);setChatDockMode(1);g_chatReasoningAgent=-1;layout();sync(true);
    {auto& b=bubbles.back();if(b.open)toggleReasoning(bubbles.size()-1);sync(true);if(b.open||b.thought)return fail(20,"Reasoning did not collapse");
     RECT row=toggleRect(b);SendMessage(viewport,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((row.left+row.right)/2,(row.top+row.bottom)/2-scroll));if(!b.open)return fail(21,"Clicking the reasoning row did not expand it");
     SendMessage(window,WM_COMMAND,5,0);if(!g_chatReasoningAll)return fail(22,"Show reasoning toggle");SendMessage(window,WM_COMMAND,5,0);if(g_chatReasoningAll||std::any_of(bubbles.begin(),bubbles.end(),[](const Bubble& x){return x.open;}))return fail(23,"Hide reasoning must collapse every section");}
    // Team with two roles: chips select which role's latest reasoning is shown.
    g_teamMode=true;g_teamStarted=true;g_teamRoleCount=2;g_teamGoal="Review the bracket and add a gusset where stress concentrates.";g_teamSnapshot={};resetTeamView();g_teamGoal="Review the bracket and add a gusset where stress concentrates.";g_teamStarted=true;g_teamSnapshot.running=true;
    const std::string engineerReasoning="The gusset should sit at the inner corner where the bending moment peaks.\n\nAsk the designer for the 4 mm variant before release.";
    {AgentTeamRoleSnapshot engineer;engineer.id="engineer";engineer.label="Engineer";engineer.state=AgentTeamState::Working;engineer.turns=2;engineer.reasoningTurn=2;engineer.reasoning=engineerReasoning;engineer.status="Tool: doc_info";
     AgentTeamRoleSnapshot designer;designer.id="designer";designer.label="Designer";designer.state=AgentTeamState::Idle;designer.turns=1;designer.status="Idle";
     g_teamSnapshot.roles={engineer,designer};}
    g_chatLog={{"You","Review the bracket and add a gusset where stress concentrates."},{"engineer","Assigned the gusset to the designer; the load path is through the inner corner."},{"designer","Added a 4 mm triangular gusset at the inner corner."}};g_chatReasoningNotes.clear();
    // Reasoning travels on each role's Text event (as emitted), never from a later snapshot.
    {AgentTeamEvent engineerReply;engineerReply.kind=AgentTeamEvent::Text;engineerReply.roleId="engineer";engineerReply.reasoning=engineerReasoning;AgentTeamEvent designerReply;designerReply.kind=AgentTeamEvent::Text;designerReply.roleId="designer";
     attachTeamReasoning(1,engineerReply);attachTeamReasoning(2,designerReply);}sync(true);
    if(bubbles.size()!=3||!bubbles[1].hasNote||bubbles[1].reasoning.find(L"bending moment")==std::wstring::npos||!bubbles[2].hasNote||!bubbles[2].reasoning.empty())return fail(24,"Team reasoning notes must follow each role's own provider text");
    for(auto size:std::vector<std::pair<int,int>>{{1280,800},{1920,1080}}){resize(size.first,size.second);setChatDockMode(1);g_chatAgentDetails=false;g_chatReasoningAgent=-1;layout();sync(true);
        const std::string sizeTag=std::to_string(size.first)+"x"+std::to_string(size.second)+dpiTag;
        SendMessage(window,WM_COMMAND,10,0);sync(true);std::wstring text=panelText();
        if(g_chatReasoningAgent!=0||!panelShown||text.find(L"bending moment")==std::wstring::npos||panelCaption.find(L"engineer")==std::wstring::npos)return fail(25,"Engineer chip must show its id, label and latest reasoning (agent "+std::to_string(g_chatReasoningAgent)+", panel "+std::to_string(panelShown)+", text "+std::to_string(text.size())+")");
        captureChatDockFrame(path+".dock-team-engineer-"+sizeTag+".png");
        SendMessage(window,WM_COMMAND,11,0);sync(true);text=panelText();
        if(g_chatReasoningAgent!=1||text.find(L"not exposed")==std::wstring::npos||panelCaption.find(L"designer")==std::wstring::npos)return fail(26,"Designer without readable reasoning must say so explicitly");
        captureChatDockFrame(path+".dock-team-designer-"+sizeTag+".png");
        g_chatAgentDetails=true;layout();sync(true);g_verifyTeamCardBounds=true;captureChatDockFrame(path+".dock-team-cards-"+sizeTag+".png");g_verifyTeamCardBounds=false;
        if(!g_teamCardContentFits)return fail(27,"Team card text exceeds its card: "+g_teamCardBoundsFailure);
        // Every role's card controls are reachable across card pages and stay inside the dock.
        {const auto dock=chatDockGeometry();std::set<int> seen;const unsigned pageSize=(unsigned)(teamCardColumns()*teamCardRows()),pages=(g_teamRoleCount+pageSize-1)/pageSize;
         for(unsigned page=0;page<pages;++page){g_teamPage=page;draw();
             for(const auto& z:g_zones)if((z.id>=44310&&z.id<44312)||(z.id>=44000&&z.id<44002)||z.id==44014){seen.insert(z.id);if(z.x<dock.x||z.x+z.w>winW||z.y<dock.chatTop||z.y+z.h>dock.chatBottom)return fail(28,"Team card control "+std::to_string(z.id)+" clipped");}}
         g_teamPage=0;if(seen.size()!=5)return fail(28,"Team card controls missing across pages ("+std::to_string(seen.size())+")");}
        if(!click(44310)||g_chatReasoningAgent!=0||g_chatAgentDetails)return fail(29,"Card Reasoning link must open that role's reasoning in the chat");
    }
    // A tool dock sits between the viewport and the Assistant; neither overlaps.
    for(auto size:std::vector<std::pair<int,int>>{{1280,800},{1920,1080}}){resize(size.first,size.second);g_teamMode=false;g_teamStarted=false;g_chatAgentDetails=false;setChatDockMode(1);g_bakedMotions.open=true;layout();sync(true);
        captureChatDockFrame(path+".dock-with-tool-dock-"+std::to_string(size.first)+"x"+std::to_string(size.second)+dpiTag+".png");const auto dock=chatDockGeometry();
        if(viewX+viewW+bakedMotionsDockWidth()!=dock.x||bakedMotionsDockWidth()<200||viewW<(size.first>=1920?460:300))return fail(37,"Tool dock must sit between the viewport and the Assistant dock (viewport "+std::to_string(viewW)+", dock "+std::to_string(assistantDockWidth())+")");
        if(assistantDockWidth()!=expected(dpi).normal)return fail(41,"Normal width beside a tool dock differs from the rule");
        for(const auto& z:g_zones)if(z.id>=560&&z.id<=562&&z.y>=viewY&&z.x+z.w>viewX+viewW)return fail(40,"Appearance controls run under the tool dock");
        const RECT chat=windowRect(window);if(chat.left<dock.x)return fail(38,"Conversation overlaps the tool dock");
        for(HWND control:{copyButton,latestButton,detailsButton,expandButton,reasoningButton}){const RECT r=windowRect(control);if(r.left<chat.left||r.right>chat.right-8)return fail(39,"Header control clipped at the minimum dock width");}
        g_bakedMotions.open=false;layout();}
    // Drag handle resizes the normal width; the stored width restores exactly;
    // rail click restores; settings persist.
    resize(1280,800);g_teamMode=false;g_teamStarted=false;g_chatDockUserWidth=0;setChatDockMode(1);layout();draw();
    {const Zone* handle=zone(44303);if(!handle)return fail(30,"Resize handle missing");const auto e=expected(dpi);const int want=std::clamp(520,e.min,e.max),startX=(int)(handle->x+handle->w*.5f),y=viewY+200,target=winW-want;
     SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(startX,y));if(!g_chatDockResizing)return fail(31,"Handle press did not start resizing");
     SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(target,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(target,y));
     if(g_chatDockResizing||assistantDockWidth()!=want||viewX+viewW!=target)return fail(32,"Drag did not set the dock width ("+std::to_string(assistantDockWidth())+" of "+std::to_string(want)+")");
     {saveUiSettings();const double stored=g_chatDockUserWidth;g_chatDockUserWidth=0;setChatDockMode(2);loadUiSettings();layout();
      if(g_chatDockUserWidth!=stored||chatDockMode()!=1||assistantDockWidth()!=want)return fail(45,"Dragged width did not restore exactly ("+std::to_string(assistantDockWidth())+" of "+std::to_string(want)+")");}
     draw();
     SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(target+1,y));if(!g_chatDockResizing)return fail(33,"Moved handle press did not start resizing");SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(10,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(10,y));
     if(assistantDockWidth()!=chatDockMaxWidth()||viewW<360)return fail(33,"Drag must clamp to keep the viewport usable");
     captureChatDockFrame(path+".dock-resized-1280x800"+dpiTag+".png");}
    {const double width=g_chatDockUserWidth;uiAction(44301);if(!g_chatDockRail)return fail(34,"Collapse to rail");saveUiSettings();setChatDockMode(2);g_chatDockUserWidth=0;loadUiSettings();
     if(!g_chatDockRail||g_chatExpanded||g_chatDockUserWidth!=width)return fail(35,"Dock mode and width were not restored from settings");
     if(!click(44300)||g_chatDockRail||assistantDockWidth()!=chatDockNormalWidth())return fail(36,"Rail click must restore the normal dock");}
    }
    g_chatDockDpiOverride=0;dpiNow=96;
    // A real offline team read by one late poll after two replies in the same
    // turn: each reply keeps the reasoning returned before it.
    {resize(1280,800);g_chatDockUserWidth=0;setChatDockMode(1);g_chatAgentDetails=false;g_chatReasoningAgent=-1;
     using V=arkjson::Value;
     auto reply=[](const std::string& reasoning,const std::string& text,bool call){V output=V::makeArr(),thought=V::makeObj(),summaryParts=V::makeArr(),part=V::makeObj(),message=V::makeObj(),content=V::makeArr(),body=V::makeObj();
         thought.set("type",V::makeStr("reasoning"));thought.set("id",V::makeStr(call?"rs_a":"rs_b"));part.set("type",V::makeStr("summary_text"));part.set("text",V::makeStr(reasoning));summaryParts.arr.push_back(part);thought.set("summary",summaryParts);output.arr.push_back(thought);
         message.set("type",V::makeStr("message"));message.set("role",V::makeStr("assistant"));body.set("type",V::makeStr("output_text"));body.set("text",V::makeStr(text));content.arr.push_back(body);message.set("content",content);output.arr.push_back(message);
         if(call){V item=V::makeObj();item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr("late-poll-read"));item.set("name",V::makeStr("doc_info"));item.set("arguments",V::makeStr("{}"));output.arr.push_back(item);}
         V response=V::makeObj();response.set("status",V::makeStr("completed"));response.set("output",output);return response;};
     AgentTeamConfig config;AgentTeamRole lead;lead.id="engineer";lead.label="Engineer";lead.assistant.model="offline-reasoning-test";int rounds=0;
     lead.transport=[&](auto&,auto&,auto*){return ++rounds==1?reply("Inspect the revision before choosing a flange.","Reading the current revision first.",true):reply("The 4 mm flange keeps the deflection below the stated limit.","Flange set to 4 mm.",false);};config.roles.push_back(lead);
     g_teamMode=true;g_teamRoleCount=2;resetTeamView();g_chatLog.clear();g_chatReasoningNotes.clear();g_teamStarted=true;g_teamEpoch=g_documentEpoch;g_teamEventSequence=0;for(auto& id:g_teamReceiptIds)id=newUsageReceiptId();g_teamGoal="Review the bracket flange";
     g_agentTeam.start(config,g_teamGoal,AgentSession::tools(),[](auto&,auto&){V result;arkjson::parse(R"({"revision":0})",result);return result;});
     if(!g_agentTeam.waitIdleFor(std::chrono::seconds(5)))return fail(50,"Offline team did not finish");
     pollTeam(true);size_t first=g_chatLog.size(),second=g_chatLog.size();
     for(size_t i=0;i<g_chatLog.size();++i)if(g_chatLog[i].first=="engineer"){if(g_chatLog[i].second=="Reading the current revision first.")first=i;if(g_chatLog[i].second=="Flange set to 4 mm.")second=i;}
     const auto* a=chatReasoningNote(first);const auto* b=chatReasoningNote(second);
     if(!a||!b||a->text!="Inspect the revision before choosing a flange."||b->text!="The 4 mm flange keeps the deflection below the stated limit.")return fail(51,"A late poll moved reasoning between replies of one turn");
     layout();sync(true);captureChatDockFrame(path+".dock-team-late-poll.png");
     g_agentTeam.cancel();g_agentTeam.waitIdle();g_teamStarted=false;resetTeamView();g_teamMode=false;}
    // Byte limits cut on UTF-8 code-point boundaries (256 KiB UI note and run text).
    {std::string euros;for(int i=0;i<110000;++i)euros+="\xE2\x82\xAC";
     auto valid=[](const std::string& text){return text.empty()||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0)>0;};
     g_chatLog={{"ARK","Long reasoning"}};g_chatReasoningNotes.clear();attachChatReasoning(0,euros,chatReasoningHidden);const auto* note=chatReasoningNote(0);
     if(!note||!valid(note->text)||note->text.rfind("[earlier reasoning truncated] ",0)!=0||note->text.size()>chatReasoningLimit+64)return fail(53,"Truncated reasoning note split a UTF-8 character");
     g_assistantReasoningRun.clear();appendAssistantReasoning("x"+euros);
     if(!valid(g_assistantReasoningRun)||g_assistantReasoningRun.rfind("[earlier reasoning truncated] ",0)!=0)return fail(54,"Truncated run reasoning split a UTF-8 character");}
    // A replaced document starts without the previous drawing's reasoning.
    {g_teamMode=false;g_chatLog={{"ARK","Previous drawing reply"}};g_chatReasoningNotes.clear();attachChatReasoning(0,"Previous drawing reasoning",chatReasoningHidden);g_assistantReasoningRun="Previous drawing reasoning";g_assistantRunStarted=true;g_chatReasoningAgent=0;
     arkjson::Value fresh=arkjson::Value::makeObj();fresh.set("expected_revision",arkjson::Value::makeNum((double)g_session.revision));runTool("doc_new",fresh);
     if(g_error||!g_chatReasoningNotes.empty()||!g_assistantReasoningRun.empty()||g_assistantRunStarted)return fail(55,"Document change kept the previous drawing's reasoning");
     layout();sync(true);const auto text=panelText();if(text.find(L"Previous drawing")!=std::wstring::npos||text.find(L"No run yet")==std::wstring::npos)return fail(56,"Latest reasoning still shows the previous drawing");
     captureChatDockFrame(path+".dock-after-document-change.png");
     // A sheet STEP import replaces the drawing outside runTool's document
     // branch: the cancelled run's reasoning goes, the visible rows stay.
     g_chatLog={{"ARK","Previous drawing reply"}};g_chatReasoningNotes.clear();attachChatReasoning(0,"Previous drawing reasoning",chatReasoningHidden);g_assistantReasoningRun="Previous drawing reasoning";g_assistantRunStarted=true;
     g_dialogSmoke=3;uiAction(80);g_dialogSmoke=0;if(g_error||!selectedSheet())return fail(57,"Sheet fixture for the STEP import failed: "+g_status);
     const std::string step=path+".reset-sheet.step";exportNativeStep(step);if(g_error||!std::filesystem::is_regular_file(step))return fail(57,"Sheet STEP export failed: "+g_status);
     const u64 epoch=g_documentEpoch;g_savedRevision=g_session.revision;g_savedUsageSerial=g_doc.usage.serial;g_savedActivitySerial=agentActivitySerial(g_doc);
     g_dialogSmoke=3;importSheetStep(step);g_dialogSmoke=0;
     if(g_error||g_doc.features.empty()||g_doc.features.back().type!=FeatureType::SourceSheet)return fail(57,"Sheet STEP import failed: "+g_status);
     if(g_documentEpoch!=epoch+1||!g_assistantReasoningRun.empty()||g_assistantRunStarted)return fail(58,"Sheet STEP import kept the cancelled run's reasoning");
     const auto* kept=chatReasoningNote(0);if(g_chatLog.size()!=1||g_chatLog[0].second!="Previous drawing reply"||!kept||kept->text!="Previous drawing reasoning")return fail(59,"Sheet STEP import dropped the visible messages");
     layout();sync(true);const auto imported=panelText();if(imported.find(L"Previous drawing")!=std::wstring::npos||imported.find(L"No run yet")==std::wstring::npos)return fail(60,"Latest reasoning still shows the run cancelled by the STEP import");
     captureChatDockFrame(path+".dock-after-step-import.png");g_chatReasoningAgent=-1;}
    std::ofstream(path+".chat-dock-test.log")<<"PASS\n"<<receipt.str();
    std::printf("Chat dock UI: rail/normal/expanded at 1280x800 and 1920x1080 for 96/144/192 dpi, inert Expand, reasoning sections, team role reasoning, late-poll attribution, UTF-8 limits, document and sheet STEP import reset, drag resize and exact persistence passed.\n%s",receipt.str().c_str());
    g_testPointer.reset();testing=false;if(window)DestroyWindow(window);if(ownInput){DestroyWindow(g_chatInput);g_chatInput=nullptr;if(inputFont)DeleteObject(inputFont);}return 0;
}
