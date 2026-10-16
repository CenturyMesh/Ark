// Native controls expose their visible field labels through MSAA/UIA's legacy
// bridge. The values remain ordinary editable Win32 controls.
struct EditorAccessibility {
    struct Item {HWND window=nullptr,parent=nullptr;RECT rect{};LOGFONTA font{};bool hasFont=false;};
    HWND dialog=nullptr;EditorScrollState* scroll=nullptr;UINT baseDpi=96,currentDpi=96;
    RECT baseClient{};std::vector<Item> items;std::vector<HFONT> fonts;
    IAccPropServices* properties=nullptr;bool comOwned=false;
    EditorAccessibility(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);comOwned=SUCCEEDED(hr);CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_IAccPropServices,(void**)&properties);}
    ~EditorAccessibility(){if(properties)properties->Release();for(auto font:fonts)DeleteObject(font);if(comOwned)CoUninitialize();}
    bool name(HWND control,const std::string& label){
        if(!properties)return false;int size=MultiByteToWideChar(CP_UTF8,0,label.c_str(),-1,nullptr,0);std::vector<wchar_t> text((size_t)size);MultiByteToWideChar(CP_UTF8,0,label.c_str(),-1,text.data(),size);
        return SUCCEEDED(properties->SetHwndPropStr(control,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,text.data()));
    }
    void capture(HWND window,EditorScrollState* viewport,UINT dpi){
        dialog=window;scroll=viewport;baseDpi=currentDpi=dpi;GetClientRect(dialog,&baseClient);
        EnumChildWindows(dialog,[](HWND child,LPARAM data)->BOOL{auto& self=*(EditorAccessibility*)data;Item item;item.window=child;item.parent=GetParent(child);if(item.parent!=self.dialog&&(!self.scroll||item.parent!=self.scroll->panel))return TRUE;GetWindowRect(child,&item.rect);MapWindowPoints(nullptr,item.parent,(POINT*)&item.rect,2);
            HFONT font=(HFONT)SendMessage(child,WM_GETFONT,0,0);item.hasFont=font&&GetObjectA(font,sizeof item.font,&item.font)>0;self.items.push_back(item);return TRUE;},(LPARAM)this);
        SetWindowSubclass(dialog,procedure,29,(DWORD_PTR)this);
        SendMessage(dialog,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS|UISF_HIDEACCEL),0);
    }
    void reflow(UINT dpi,const RECT* suggested=nullptr){
        if(!dpi)dpi=currentDpi;const UINT previousDpi=currentDpi;currentDpi=dpi;const double factor=(double)dpi/baseDpi;
        auto scaled=[&](int value){return (int)std::lround(value*factor);};auto dip=[&](int value){return MulDiv(value,(int)dpi,96);};
        RECT work=editorWorkArea();if(!g_editorWorkAreaOverride.right){MONITORINFO monitor{sizeof monitor};GetMonitorInfo(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&monitor);work=monitor.rcWork;}
        RECT old{};GetWindowRect(dialog,&old);RECT chrome{};AdjustWindowRectEx(&chrome,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
        const int extraW=chrome.right-chrome.left,extraH=chrome.bottom-chrome.top;
        const int width=std::max(1,std::min(scaled(baseClient.right),(int)(work.right-work.left)-extraW-16));
        const int height=std::max(1,std::min(scaled(baseClient.bottom),(int)(work.bottom-work.top)-extraH-16));
        const int left=std::clamp((int)(suggested?suggested->left:old.left),(int)work.left,std::max((int)work.left,(int)work.right-width-extraW));
        const int top=std::clamp((int)(suggested?suggested->top:old.top),(int)work.top,std::max((int)work.top,(int)work.bottom-height-extraH));
        SetWindowPos(dialog,nullptr,left,top,width+extraW,height+extraH,SWP_NOZORDER|SWP_NOACTIVATE);
        const int previousOffset=scroll?scroll->offset:0;if(scroll)scroll->offset=0;
        std::vector<HFONT> nextFonts;int contentBottom=0;
        const bool collapseColumns=baseClient.right>=MulDiv(960,(int)baseDpi,96)&&width<dip(960)&&scroll;
        for(const auto& item:items){
            int x=scaled(item.rect.left),y=scaled(item.rect.top),w=scaled(item.rect.right-item.rect.left),h=scaled(item.rect.bottom-item.rect.top);
            const bool panel=scroll&&item.window==scroll->panel;
            if(item.parent==dialog){
                if(panel){x=0;y=dip(110);w=width;h=std::max(dip(33),height-dip(190));}
                else if(item.rect.top>=baseClient.bottom-MulDiv(80,(int)baseDpi,96)){y=height-scaled(baseClient.bottom-item.rect.top);if(GetDlgCtrlID(item.window)==1||GetDlgCtrlID(item.window)==2)x=width-scaled(baseClient.right-item.rect.left);else w=std::max(1,width-x-dip(290));}
                else w=std::max(1,std::min(w,width-x-dip(20)));
            }else if(scroll&&item.parent==scroll->panel){
                if(collapseColumns&&item.rect.left>=MulDiv(480,(int)baseDpi,96)){x-=dip(480);y+=dip(310);}
                w=std::max(1,std::min(w,width-x-dip(32)));contentBottom=std::max(contentBottom,y+h);
            }
            MoveWindow(item.window,x,y,w,h,TRUE);
            if(item.hasFont){LOGFONTA description=item.font;description.lfHeight=scaled(description.lfHeight);auto font=CreateFontIndirectA(&description);if(font){SendMessage(item.window,WM_SETFONT,(WPARAM)font,TRUE);nextFonts.push_back(font);}}
        }
        for(auto font:fonts)DeleteObject(font);fonts=std::move(nextFonts);
        if(scroll){scroll->line=dip(62);scroll->contentHeight=contentBottom+dip(28);editorScrollTo(*scroll,MulDiv(previousOffset,(int)dpi,(int)previousDpi));editorEnsureVisible(*scroll,GetFocus());}
        SendMessage(dialog,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS|UISF_HIDEACCEL),0);RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_ERASE);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*(EditorAccessibility*)data;
        if(message==WM_DPICHANGED){self.reflow(HIWORD(wp),(const RECT*)lp);return 0;}
        if(message==WM_SETTINGCHANGE||message==WM_DISPLAYCHANGE){self.reflow(self.currentDpi);return 0;}
        return DefSubclassProc(window,message,wp,lp);
    }
};

static bool editorAccessibleNameEquals(HWND window,const std::string& expected){
    IAccessible* accessible=nullptr;if(FAILED(AccessibleObjectFromWindow(window,OBJID_CLIENT,IID_IAccessible,(void**)&accessible)))return false;
    VARIANT child{};child.vt=VT_I4;child.lVal=CHILDID_SELF;BSTR name=nullptr;const auto result=accessible->get_accName(child,&name);accessible->Release();
    bool matches=false;if(SUCCEEDED(result)&&name){const int size=WideCharToMultiByte(CP_UTF8,0,name,-1,nullptr,0,nullptr,nullptr);std::vector<char> text((size_t)size);WideCharToMultiByte(CP_UTF8,0,name,-1,text.data(),size,nullptr,nullptr);matches=expected==text.data();}SysFreeString(name);return matches;
}
