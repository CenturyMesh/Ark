// Right assistant dock: rail / normal / expanded layout with a mock conversation
// carrying provider reasoning (and a provider without it) plus a two-role team.
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
    auto fail=[&](int code,const std::string& text){std::fprintf(stderr,"Chat dock check %d: %s\n",code,text.c_str());std::ofstream(path+".chat-dock-test.log")<<"FAIL "<<code<<": "<<text<<'\n';return code;};
    auto resize=[&](int w,int h){RECT frame{0,0,w,h};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=w;winH=h;layout();};
    auto zone=[&](int id)->const Zone*{auto found=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});return found==g_zones.end()?nullptr:&*found;};
    auto click=[&](int id){HDC dc=GetDC(g_window);g_swap=false;drawFrame(dc);ReleaseDC(g_window,dc);const Zone* z=zone(id);if(!z)return false;const int x=(int)(z->x+z->w*.5f),y=(int)(z->y+z->h*.5f);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
    auto windowRect=[&](HWND control){RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,g_window,(POINT*)&r,2);return r;};
    testing=true;g_testPointer=POINT{-50,-50};g_assistantDockOpen=true;g_toolsOpen=false;g_teamMode=false;g_teamStarted=false;g_chatBusy=false;g_assistantResponseFinished=true;g_assistantResponseError=false;*g_assistantCancel=false;g_activeToolName.clear();
    g_chatDockUserWidth=0;setChatDockMode(1);g_chatReasoningAll=false;g_chatReasoningAgent=-1;g_chatAgentDetails=false;g_sidebarTab=0;g_toolModule=1;
    HFONT inputFont=nullptr;const bool ownInput=!g_chatInput;
    if(ownInput){g_chatInput=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,1,1,g_window,(HMENU)499,GetModuleHandleW(nullptr),nullptr);inputFont=CreateFontW(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");SendMessage(g_chatInput,WM_SETFONT,(WPARAM)inputFont,TRUE);SendMessage(g_chatInput,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));}
    SetWindowTextW(g_chatInput,L"Add a gusset and keep the two M6 holes.");
    // Mock single conversation: one connector without readable reasoning, then a
    // reply whose provider returned a reasoning summary. Nothing is synthesized
    // by the UI: the notes below stand in for provider-returned text.
    g_singleGoal="Design a wall bracket for a 12 kg shelf with two M6 mounting holes.";
    const std::string summary="Read the current revision before writing.\n\nA 4 mm flange keeps the bracket stiff for the 12 kg load; the M6 holes stay 40 mm apart as requested.";
    g_chatLog={{"You","Design a wall bracket for a 12 kg shelf with two M6 mounting holes."},{"Action","Tool: doc_info"},{"ARK","Created an editable L-bracket concept with two M6 holes.\n\nAssumption: steel, 3 mm, not yet production-qualified."},{"You","Make the flange thicker and add a gusset."},{"Action","Tool: doc_apply"},{"ARK","Flange is now 4 mm and a centred gusset was added. The hole pattern is unchanged."}};
    g_chatReasoningNotes.clear();attachChatReasoning(2,{},chatReasoningHidden);attachChatReasoning(5,summary,chatReasoningHidden);
    g_assistantRunStarted=true;g_assistantReasoningRun=summary;++g_chatReasoningSerial;
    const char* modes[]={"rail","normal","expanded"};std::ostringstream receipt;
    for(auto size:std::vector<std::pair<int,int>>{{1280,800},{1920,1080}}){
        resize(size.first,size.second);
        for(int mode=0;mode<3;++mode){
            setChatDockMode(mode);g_chatReasoningAgent=mode==1?0:-1;layout();sync(true);
            if(mode==1&&!bubbles.empty()&&!bubbles.back().open)toggleReasoning(bubbles.size()-1);
            const std::string tag=std::string(modes[mode])+"-"+std::to_string(size.first)+"x"+std::to_string(size.second);
            captureChatDockFrame(path+".dock-"+tag+".png");
            const auto dock=chatDockGeometry();
            if(!dock.open||dock.x!=winW-assistantDockWidth()||dock.y!=viewY||dock.y+dock.h!=winH-32)return fail(1,"Dock must span from the ribbon to the status bar at the right edge ("+tag+")");
            if(viewX+viewW!=dock.x||viewH!=winH-viewY-32||viewW<(mode==2?300:400))return fail(2,"Viewport must end at the dock and stay usable ("+tag+", "+std::to_string(viewW)+"x"+std::to_string(viewH)+")");
            if(mode==0&&assistantDockWidth()!=chatDockRailWidth())return fail(3,"Rail width ("+tag+")");
            if(mode==2&&assistantDockWidth()<=chatDockNormalWidth())return fail(4,"Expanded dock must be wider than normal ("+tag+")");
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
            const RECT chat=windowRect(window),input=windowRect(g_chatInput);
            if(chat.left<dock.x||chat.right>winW||chat.top<dock.chatTop||chat.bottom>dock.chatBottom)return fail(12,"Native conversation escapes its dock rows ("+tag+")");
            if(input.left<dock.x||input.right>winW||input.top<dock.inputY||input.bottom>dock.inputY+dock.inputH)return fail(13,"Composer escapes its field ("+tag+")");
            if(pageHeight()<(size.second>=1000?250:100))return fail(14,"Message area too small: "+std::to_string(pageHeight())+" ("+tag+")");
            for(HWND control:{copyButton,latestButton,detailsButton,expandButton,reasoningButton}){const RECT r=windowRect(control);if(r.left<chat.left||r.right>chat.right||r.top<chat.top||r.bottom>chat.top+g_chatHeaderHeight)return fail(15,"Header control clipped ("+tag+")");}
            if(mode==1){
                const auto& replied=bubbles.back();if(!replied.hasNote||replied.reasoning.empty()||!replied.open||!replied.thought)return fail(16,"Readable reasoning section did not expand inside the reply ("+tag+")");
                if(!bubbles[2].hasNote||!bubbles[2].reasoning.empty())return fail(17,"Provider without readable reasoning must show the explicit note only ("+tag+")");
                if(!panelShown||!(GetWindowLongPtr(thoughtPanel,GWL_STYLE)&WS_VISIBLE))return fail(18,"Latest reasoning panel hidden ("+tag+")");
                const int length=GetWindowTextLengthW(thoughtPanel);std::wstring shown((size_t)length+1,L'\0');GetWindowTextW(thoughtPanel,shown.data(),length+1);shown.resize((size_t)length);
                if(shown.find(L"4 mm flange")==std::wstring::npos)return fail(19,"Panel text is not the provider summary ("+tag+")");
            }
            receipt<<tag<<": dock "<<assistantDockWidth()<<" px, viewport "<<viewW<<"x"<<viewH<<", page "<<(mode?pageHeight():0)<<"\n";
        }
    }
    // Reasoning toggles: per bubble click, show-all, and collapsed default.
    resize(1280,800);setChatDockMode(1);g_chatReasoningAgent=-1;layout();sync(true);
    {auto& b=bubbles.back();if(b.open)toggleReasoning(bubbles.size()-1);sync(true);if(b.open||b.thought)return fail(20,"Reasoning did not collapse");
     RECT row=toggleRect(b);SendMessage(viewport,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((row.left+row.right)/2,(row.top+row.bottom)/2-scroll));if(!b.open)return fail(21,"Clicking the reasoning row did not expand it");
     SendMessage(window,WM_COMMAND,5,0);if(!g_chatReasoningAll)return fail(22,"Show reasoning toggle");SendMessage(window,WM_COMMAND,5,0);if(g_chatReasoningAll||std::any_of(bubbles.begin(),bubbles.end(),[](const Bubble& x){return x.open;}))return fail(23,"Hide reasoning must collapse every section");}
    // Team with two roles: chips select which role's latest reasoning is shown.
    g_teamMode=true;g_teamStarted=true;g_teamRoleCount=2;g_teamGoal="Review the bracket and add a gusset where stress concentrates.";g_teamSnapshot={};resetTeamView();g_teamGoal="Review the bracket and add a gusset where stress concentrates.";g_teamStarted=true;g_teamSnapshot.running=true;
    {AgentTeamRoleSnapshot engineer;engineer.id="engineer";engineer.label="Engineer";engineer.state=AgentTeamState::Working;engineer.turns=2;engineer.reasoningTurn=2;engineer.reasoning="The gusset should sit at the inner corner where the bending moment peaks.\n\nAsk the designer for the 4 mm variant before release.";engineer.status="Tool: doc_info";
     AgentTeamRoleSnapshot designer;designer.id="designer";designer.label="Designer";designer.state=AgentTeamState::Idle;designer.turns=1;designer.status="Idle";
     g_teamSnapshot.roles={engineer,designer};}
    g_chatLog={{"You","Review the bracket and add a gusset where stress concentrates."},{"engineer","Assigned the gusset to the designer; the load path is through the inner corner."},{"designer","Added a 4 mm triangular gusset at the inner corner."}};g_chatReasoningNotes.clear();
    attachTeamReasoning(1,0);attachTeamReasoning(2,1);sync(true);
    if(bubbles.size()!=3||!bubbles[1].hasNote||bubbles[1].reasoning.find(L"bending moment")==std::wstring::npos||!bubbles[2].hasNote||!bubbles[2].reasoning.empty())return fail(24,"Team reasoning notes must follow each role's own provider text");
    for(auto size:std::vector<std::pair<int,int>>{{1280,800},{1920,1080}}){resize(size.first,size.second);setChatDockMode(1);g_chatAgentDetails=false;g_chatReasoningAgent=-1;layout();sync(true);
        SendMessage(window,WM_COMMAND,10,0);sync(true);std::wstring text((size_t)GetWindowTextLengthW(thoughtPanel)+1,L'\0');GetWindowTextW(thoughtPanel,text.data(),(int)text.size());
        if(g_chatReasoningAgent!=0||!panelShown||text.find(L"bending moment")==std::wstring::npos||panelCaption.find(L"engineer")==std::wstring::npos)return fail(25,"Engineer chip must show its id, label and latest reasoning (agent "+std::to_string(g_chatReasoningAgent)+", panel "+std::to_string(panelShown)+", text "+std::to_string(text.size())+")");
        captureChatDockFrame(path+".dock-team-engineer-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".png");
        SendMessage(window,WM_COMMAND,11,0);sync(true);text.assign((size_t)GetWindowTextLengthW(thoughtPanel)+1,L'\0');GetWindowTextW(thoughtPanel,text.data(),(int)text.size());
        if(g_chatReasoningAgent!=1||text.find(L"not exposed")==std::wstring::npos||panelCaption.find(L"designer")==std::wstring::npos)return fail(26,"Designer without readable reasoning must say so explicitly");
        captureChatDockFrame(path+".dock-team-designer-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".png");
        g_chatAgentDetails=true;layout();sync(true);g_verifyTeamCardBounds=true;captureChatDockFrame(path+".dock-team-cards-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".png");g_verifyTeamCardBounds=false;
        if(!g_teamCardContentFits)return fail(27,"Team card text exceeds its card: "+g_teamCardBoundsFailure);
        // Every role's card controls are reachable across card pages and stay inside the dock.
        {const auto dock=chatDockGeometry();std::set<int> seen;const unsigned pageSize=(unsigned)(teamCardColumns()*teamCardRows()),pages=(g_teamRoleCount+pageSize-1)/pageSize;
         for(unsigned page=0;page<pages;++page){g_teamPage=page;HDC pageDc=GetDC(g_window);g_swap=false;drawFrame(pageDc);ReleaseDC(g_window,pageDc);
             for(const auto& z:g_zones)if((z.id>=44310&&z.id<44312)||(z.id>=44000&&z.id<44002)||z.id==44014){seen.insert(z.id);if(z.x<dock.x||z.x+z.w>winW||z.y<dock.chatTop||z.y+z.h>dock.chatBottom)return fail(28,"Team card control "+std::to_string(z.id)+" clipped");}}
         g_teamPage=0;if(seen.size()!=5)return fail(28,"Team card controls missing across pages ("+std::to_string(seen.size())+")");}
        if(!click(44310)||g_chatReasoningAgent!=0||g_chatAgentDetails)return fail(29,"Card Reasoning link must open that role's reasoning in the chat");
    }
    // A tool dock sits between the viewport and the Assistant; neither overlaps.
    for(auto size:std::vector<std::pair<int,int>>{{1280,800},{1920,1080}}){resize(size.first,size.second);g_teamMode=false;g_teamStarted=false;g_chatAgentDetails=false;setChatDockMode(1);g_bakedMotions.open=true;layout();sync(true);
        captureChatDockFrame(path+".dock-with-tool-dock-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".png");const auto dock=chatDockGeometry();
        if(viewX+viewW+bakedMotionsDockWidth()!=dock.x||bakedMotionsDockWidth()<200||viewW<(size.first>=1920?460:300))return fail(37,"Tool dock must sit between the viewport and the Assistant dock");
        for(const auto& z:g_zones)if(z.id>=560&&z.id<=562&&z.y>=viewY&&z.x+z.w>viewX+viewW)return fail(40,"Appearance controls run under the tool dock");
        const RECT chat=windowRect(window);if(chat.left<dock.x)return fail(38,"Conversation overlaps the tool dock");
        for(HWND control:{copyButton,latestButton,detailsButton,expandButton,reasoningButton}){const RECT r=windowRect(control);if(r.left<chat.left||r.right>chat.right-8)return fail(39,"Header control clipped at the minimum dock width");}
        g_bakedMotions.open=false;layout();}
    // Drag handle resizes the normal width; rail click restores; settings persist.
    resize(1280,800);g_teamMode=false;g_teamStarted=false;setChatDockMode(1);layout();HDC dc=GetDC(g_window);g_swap=false;drawFrame(dc);ReleaseDC(g_window,dc);
    {const Zone* handle=zone(44303);if(!handle)return fail(30,"Resize handle missing");const int startX=(int)(handle->x+handle->w*.5f),y=viewY+200,target=winW-520;
     SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(startX,y));if(!g_chatDockResizing)return fail(31,"Handle press did not start resizing");
     SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(target,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(target,y));
     if(g_chatDockResizing||assistantDockWidth()!=winW-target||viewX+viewW!=target)return fail(32,"Drag did not set the dock width ("+std::to_string(assistantDockWidth())+")");
     {HDC moved=GetDC(g_window);drawFrame(moved);ReleaseDC(g_window,moved);}
     SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(target+1,y));if(!g_chatDockResizing)return fail(33,"Moved handle press did not start resizing");SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(10,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(10,y));
     if(assistantDockWidth()!=chatDockMaxWidth()||viewW<360)return fail(33,"Drag must clamp to keep the viewport usable");
     captureChatDockFrame(path+".dock-resized-1280x800.png");}
    {const int width=g_chatDockUserWidth;uiAction(44301);if(!g_chatDockRail)return fail(34,"Collapse to rail");saveUiSettings();setChatDockMode(2);g_chatDockUserWidth=0;loadUiSettings();
     if(!g_chatDockRail||g_chatExpanded||g_chatDockUserWidth!=width)return fail(35,"Dock mode and width were not restored from settings");
     if(!click(44300)||g_chatDockRail||assistantDockWidth()!=chatDockNormalWidth())return fail(36,"Rail click must restore the normal dock");}
    std::ofstream(path+".chat-dock-test.log")<<"PASS\n"<<receipt.str();
    std::printf("Chat dock UI: rail/normal/expanded at 1280x800 and 1920x1080, reasoning sections, team role reasoning, drag resize and persistence passed.\n%s",receipt.str().c_str());
    g_testPointer.reset();testing=false;if(window)DestroyWindow(window);if(ownInput){DestroyWindow(g_chatInput);g_chatInput=nullptr;if(inputFont)DeleteObject(inputFont);}return 0;
}
