// Revision-bound history editor. The list is a view of retained feature source;
// commands still use the ordinary atomic document API.
struct FeatureHistoryEditor {HWND list=nullptr,details=nullptr;u64 selected=0;int action=0;};
static int g_featureHistorySmokeIndex=0,g_featureHistorySmokeAction=0;
static void selectFeatureHistory(FeatureHistoryEditor& editor){
    const auto index=SendMessage(editor.list,LB_GETCURSEL,0,0);editor.selected=0;
    if(index<0||(size_t)index>=g_doc.features.size())return;
    const auto& feature=g_doc.features[(size_t)index];editor.selected=feature.id;
    std::ostringstream details;details<<"#"<<feature.id<<"  "<<feature.name<<"\r\nOperation: "<<featureTypeName(feature.type)<<"\r\nState: "<<(feature.suppressed?"suppressed":"enabled")<<"\r\n";
    if(g_doc.historyTip)details<<"Evaluation tip: #"<<g_doc.historyTip<<" (later source remains editable)\r\n";
    details<<"\r\nDirect feature inputs:\r\n";
    if(feature.inputs.empty())details<<"  None\r\n";
    for(auto id:feature.inputs){const auto* input=g_doc.find(id);details<<"  #"<<id<<"  "<<(input?input->name:"MISSING")<<"\r\n";}
    details<<"\r\nDriving sketches:\r\n";const auto sketches=drivingSketchNames(feature);
    if(sketches.empty())details<<"  None\r\n";
    for(const auto& name:sketches)details<<"  "<<name<<"\r\n";
    details<<"\r\nDirect feature consumers:\r\n";bool any=false;
    for(const auto& candidate:g_doc.features)if(std::find(candidate.inputs.begin(),candidate.inputs.end(),feature.id)!=candidate.inputs.end()){details<<"  #"<<candidate.id<<"  "<<candidate.name<<"\r\n";any=true;}
    if(!any)details<<"  None\r\n";
    details<<"\r\nReordering validates all dependencies during recompute. Suppression retains the definition and rejects when an active consumer needs it. Rollback stops evaluation after the selected feature and preserves individual suppression settings. Resume evaluates the complete history. Undo restores each accepted change.";
    SetWindowTextA(editor.details,details.str().c_str());
    SetWindowTextA(GetDlgItem(g_dialog,203),feature.suppressed?"Unsuppress":"Suppress");
    EnableWindow(GetDlgItem(g_dialog,201),index>0);EnableWindow(GetDlgItem(g_dialog,202),(size_t)index+1<g_doc.features.size());
}
static LRESULT CALLBACK featureHistoryProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* editor=(FeatureHistoryEditor*)GetWindowLongPtr(window,GWLP_USERDATA);
    if(editor&&message==WM_COMMAND&&LOWORD(wp)==101&&HIWORD(wp)==LBN_SELCHANGE){selectFeatureHistory(*editor);return 0;}
    if(editor&&message==WM_COMMAND&&LOWORD(wp)>=201&&LOWORD(wp)<=205){editor->action=LOWORD(wp);return editorProc(window,WM_COMMAND,1,0);}
    if(editor&&message==WM_TIMER&&wp==901){SendMessage(editor->list,LB_SETCURSEL,g_featureHistorySmokeIndex,0);selectFeatureHistory(*editor);editor->action=g_featureHistorySmokeAction;}
    return editorProc(window,message,wp,lp);
}
static void featureHistoryDialog(){
    using V=arkjson::Value;if(g_doc.features.empty()){g_status="Feature history is empty / create a feature first";return;}
    const u64 revision=g_session.revision,epoch=g_documentEpoch;
    FeatureHistoryEditor editor;WNDCLASSA cls{};cls.lpfnWndProc=featureHistoryProc;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="ARK_FEATURE_HISTORY";cls.hbrBackground=dialogBrush;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&cls);
    // Fit the current monitor's work area. Both lists scroll, so every action
    // remains reachable at the supported laptop resolution and system DPI.
    MONITORINFO monitor{sizeof monitor};GetMonitorInfo(MonitorFromWindow(g_window,MONITOR_DEFAULTTONEAREST),&monitor);
    RECT parent{};GetWindowRect(g_window,&parent);const int width=std::min({1000,winW-40,(int)(monitor.rcWork.right-monitor.rcWork.left)-40}),height=std::min({680,winH-80,(int)(monitor.rcWork.bottom-monitor.rcWork.top)-80});
    RECT outer{0,0,width,height};AdjustWindowRectEx(&outer,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    int x=std::clamp<LONG>(parent.left+(parent.right-parent.left-(outer.right-outer.left))/2,monitor.rcWork.left,monitor.rcWork.right-(outer.right-outer.left));
    int y=std::clamp<LONG>(parent.top+(parent.bottom-parent.top-(outer.bottom-outer.top))/2,monitor.rcWork.top,monitor.rcWork.bottom-(outer.bottom-outer.top));
    g_dialog=CreateWindowExA(WS_EX_DLGMODALFRAME,cls.lpszClassName,"Feature history / dependencies and evaluation tip",WS_CAPTION|WS_SYSMENU,x,y,outer.right-outer.left,outer.bottom-outer.top,g_window,nullptr,cls.hInstance,nullptr);
    if(!g_dialog)throw std::runtime_error("Could not open feature history");
    HFONT font=CreateFontA(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");BOOL dark=TRUE;DwmSetWindowAttribute(g_dialog,20,&dark,sizeof dark);
    auto control=[&](const char* kind,const char* text,DWORD style,int cx,int cy,int w,int h,int id){auto window=CreateWindowExA(0,kind,text,WS_CHILD|WS_VISIBLE|style,cx,cy,w,h,g_dialog,(HMENU)(INT_PTR)id,cls.hInstance,nullptr);SendMessage(window,WM_SETFONT,(WPARAM)font,TRUE);return window;};
    control("STATIC","Select a feature to inspect its sources and consumers. Each action publishes one validated document revision.",0,20,18,width-40,24,0);
    const int listHeight=std::max(100,(height-160)/2),detailTop=56+listHeight;
    editor.list=control("LISTBOX","",WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,20,48,width-40,listHeight,101);
    editor.details=control("EDIT","",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,20,detailTop,width-40,height-detailTop-78,102);
    const char* captions[]={"Move up","Move down","Suppress","Rollback here","Resume all"};const int buttonWidth=(width-60)/6;
    for(int i=0;i<5;++i)control("BUTTON",captions[i],WS_TABSTOP|BS_OWNERDRAW,20+i*(buttonWidth+4),height-54,buttonWidth,34,201+i);
    control("BUTTON","Close",WS_TABSTOP|BS_OWNERDRAW,20+5*(buttonWidth+4),height-54,buttonWidth,34,2);
    int selected=0;bool afterTip=false;
    for(size_t i=0;i<g_doc.features.size();++i){const auto& feature=g_doc.features[i];if(feature.id==g_explorerFeature)selected=(int)i;
        std::string row=std::to_string(i+1)+"  #"+std::to_string(feature.id)+"  "+feature.name+"  /  "+featureTypeName(feature.type)+(feature.suppressed?"  [suppressed]":"")+(afterTip?"  [after tip]":"")+(feature.id==g_doc.historyTip?"  [evaluation tip]":"");
        SendMessageA(editor.list,LB_ADDSTRING,0,(LPARAM)row.c_str());if(feature.id==g_doc.historyTip)afterTip=true;}
    SetWindowLongPtr(g_dialog,GWLP_USERDATA,(LONG_PTR)&editor);SendMessage(editor.list,LB_SETCURSEL,selected,0);selectFeatureHistory(editor);
    g_edit=editor.list;g_dialogDone=g_dialogAccept=false;EnableWindow(g_window,FALSE);ShowWindow(g_dialog,SW_SHOW);SetFocus(editor.list);if(g_dialogSmoke)SetTimer(g_dialog,901,200,nullptr);
    MSG message{};while(!g_dialogDone&&GetMessage(&message,nullptr,0,0)>0){if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){SendMessage(g_dialog,WM_COMMAND,2,0);continue;}if(!IsDialogMessage(g_dialog,&message)){TranslateMessage(&message);DispatchMessage(&message);}}
    const bool apply=g_dialogAccept&&editor.action&&editor.selected;EnableWindow(g_window,TRUE);DestroyWindow(g_dialog);g_dialog=nullptr;DeleteObject(font);if(!g_headless)SetForegroundWindow(g_window);if(!apply)return;
    if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("Feature history changed while the editor was open; inspect the current history before retrying");
    V command=V::makeObj();command.set("id",V::makeNum((double)editor.selected));
    if(editor.action==201||editor.action==202){command.set("op",V::makeStr("feature.move"));command.set("delta",V::makeNum(editor.action==201?-1:1));}
    else if(editor.action==203){command.set("op",V::makeStr("feature.edit"));command.set("suppressed",V::makeBool(!g_doc.find(editor.selected)->suppressed));}
    else{command.set("op",V::makeStr("feature.rollback"));if(editor.action==205)command.set("id",V{});}
    applyUiCommandAtRevision(command,revision,epoch);if(!g_error){g_explorerFeature=editor.selected;g_status="Feature history updated atomically / revision "+std::to_string(g_session.revision)+" / Undo restores the prior state";}
}
