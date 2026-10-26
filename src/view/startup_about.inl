// Native startup and product information. No document/settings writes or GL state.
namespace arkstudio {
static std::wstring productWide(const std::string& text){
    int n=MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,nullptr,0);
    if(n<=0)return {};std::wstring result((size_t)n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,result.data(),n);result.pop_back();return result;
}
static std::wstring productIdentity(){return L"ARK "+productWide(app_iteration_code());}
static std::filesystem::path runtimeDirectory(){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);return std::filesystem::path(path).parent_path();}
static void panelFill(HDC dc,RECT rect,COLORREF color){HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
static void panelText(HDC dc,const wchar_t* text,RECT rect,int size,COLORREF color,int weight=FW_NORMAL,UINT flags=DT_LEFT|DT_WORDBREAK){
    HFONT font=CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text,-1,&rect,flags);SelectObject(dc,old);DeleteObject(font);
}
static void panelBrand(HDC dc,int x,int y,int size){HICON icon=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_SHARED);if(icon)DrawIconEx(dc,x,y,icon,size,size,0,nullptr,DI_NORMAL);}
static void paintSplash(HDC dc,int width,int height,double seconds){
    int saved=SaveDC(dc);SetGraphicsMode(dc,GM_ADVANCED);XFORM scale{(FLOAT)width/620,0,0,(FLOAT)height/340,0,0};SetWorldTransform(dc,&scale);
    panelFill(dc,{0,0,620,340},RGB(16,24,33));panelFill(dc,{0,0,620,2},RGB(106,209,235));
    panelFill(dc,{0,278,620,340},RGB(20,30,40));
    panelBrand(dc,43,39,56);panelText(dc,L"ARK",{119,36,360,108},54,RGB(242,247,252),FW_SEMIBOLD);
    auto identity=productWide(app_iteration_code());panelText(dc,identity.c_str(),{123,109,340,140},18,RGB(116,214,237),FW_SEMIBOLD);
    panelText(dc,L"Ideas, shaped precisely.",{45,171,485,212},25,RGB(219,231,241));
    panelText(dc,L"Native parametric CAD for people and agents",{46,217,554,251},15,RGB(145,165,183));
    panelText(dc,L"Created by MeshSphere",{45,302,383,326},14,RGB(178,194,208));
    auto version=L"v"+productWide(app_version());panelText(dc,version.c_str(),{442,302,575,326},13,RGB(133,155,174),FW_NORMAL,DT_RIGHT|DT_SINGLELINE);
    // A subtle moving highlight communicates activity, never fabricated progress.
    panelFill(dc,{45,264,575,265},RGB(41,59,75));
    double phase=std::fmod(std::max(0.,seconds)*.62,1.);int center=45+(int)(530*phase);
    for(int i=-35;i<=35;++i){int x=center+i;if(x<45||x>=575)continue;double a=(1.-std::abs(i)/36.)*.8;panelFill(dc,{x,264,x+1,266},RGB((int)(41+65*a),(int)(59+150*a),(int)(75+160*a)));}
    // Retained code geometry: a quiet isometric outline, not a CAD authority claim.
    HPEN pen=CreatePen(PS_SOLID,1,RGB(55,89,111));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    POINT outline[]={{504,46},{551,72},{551,124},{504,151},{457,124},{457,72},{504,46}};Polyline(dc,outline,7);
    MoveToEx(dc,457,72,nullptr);LineTo(dc,504,99);LineTo(dc,551,72);MoveToEx(dc,504,99,nullptr);LineTo(dc,504,151);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);RestoreDC(dc,saved);
}
class StartupSplash {
    HANDLE stop_=nullptr,done_=nullptr;std::thread thread_;bool skipped_=true;
    std::atomic<ULONGLONG> visibleMs_{0};std::atomic_bool created_{false};
    struct PaintState{ULONGLONG started;};
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_NCCREATE){auto* create=(CREATESTRUCTW*)lp;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)create->lpCreateParams);}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PAINT){PAINTSTRUCT paint;HDC target=BeginPaint(window,&paint);RECT rect;GetClientRect(window,&rect);HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,rect.right,rect.bottom);auto old=SelectObject(dc,bitmap);auto* state=(PaintState*)GetWindowLongPtrW(window,GWLP_USERDATA);paintSplash(dc,rect.right,rect.bottom,state?(GetTickCount64()-state->started)/1000.:0);BitBlt(target,0,0,rect.right,rect.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window,&paint);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
public:
    static constexpr ULONGLONG maximumMs=3000;
    explicit StartupSplash(bool headless,bool visible=true){
        if(headless)return;stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);done_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!stop_||!done_)return;skipped_=false;
        try{thread_=std::thread([this,visible]{
            HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"ARK_STARTUP_PANEL";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
            RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);HDC screen=GetDC(nullptr);int dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(nullptr,screen);int width=MulDiv(620,dpi,96),height=MulDiv(340,dpi,96);
            PaintState state{GetTickCount64()};HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,productIdentity().c_str(),WS_POPUP,(work.left+work.right-width)/2,(work.top+work.bottom-height)/2,width,height,nullptr,nullptr,instance,&state);
            if(window)created_=true;
            if(window&&WaitForSingleObject(stop_,0)!=WAIT_OBJECT_0){
                state.started=GetTickCount64();if(visible){ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);}
                // 50 ms margin keeps the nominal three-second upper bound even
                // when a frame or scheduler tick arrives slightly late.
                const ULONGLONG deadline=state.started+maximumMs-50;
                for(;;){ULONGLONG now=GetTickCount64();if(now>=deadline||WaitForSingleObject(stop_,0)==WAIT_OBJECT_0)break;
                    DWORD result=MsgWaitForMultipleObjects(1,&stop_,FALSE,(DWORD)std::min<ULONGLONG>(16,deadline-now),QS_ALLINPUT);if(result==WAIT_OBJECT_0)break;
                    MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}InvalidateRect(window,nullptr,FALSE);if(visible)UpdateWindow(window);
                }
                ShowWindow(window,SW_HIDE);visibleMs_=GetTickCount64()-state.started;
            }
            if(window)DestroyWindow(window);SetEvent(done_);
        });}catch(...){skipped_=true;SetEvent(done_);}
    }
    ~StartupSplash(){finish();if(stop_)CloseHandle(stop_);if(done_)CloseHandle(done_);}
    StartupSplash(const StartupSplash&)=delete;StartupSplash& operator=(const StartupSplash&)=delete;
    void finish(){if(stop_)SetEvent(stop_);if(thread_.joinable())thread_.join();}
    bool skipped()const{return skipped_;}ULONGLONG visibleMilliseconds()const{return visibleMs_.load();}
    bool created()const{return created_.load();}
    bool waitForDeadline(DWORD timeout){return done_&&WaitForSingleObject(done_,timeout)==WAIT_OBJECT_0;}
};

static const wchar_t* licenseSummary(){return
    L"ARK / Created by MeshSphere\r\nProject lead: Bruno Santos\r\n\r\n"
    L"ARK's original code and documentation are licensed under the Apache License, Version 2.0. Copyright 2026 Bruno Santos and ARK contributors. Full terms and attribution: LICENSE and NOTICE. Separately identified third-party material retains its own terms. The license does not relicense customer designs or grant additional ARK / MeshSphere trademark rights.\r\n\r\n"
    L"Open CASCADE Technology (OCCT) 8.0.1\r\nARK makes use of facilities provided by Open CASCADE Technology for native B-rep modeling, validation and exchange. OCCT is licensed under GNU LGPL 2.1 with the Open CASCADE exception (version 1.0). Full texts: LICENSE_LGPL_21.txt and OCCT_LGPL_EXCEPTION.txt.\r\nhttps://occt3d.com/dev/doc/overview/html/occt_public_license.html\r\n\r\n"
    L"Mapbox earcut.hpp 2.2.4 / ISC\r\nCopyright (c) 2015, Mapbox. Used for sampled polygon triangulation. The full copyright, permission and warranty notice is retained in mapbox-earcut-ISC.txt.\r\nhttps://github.com/mapbox/earcut.hpp/tree/v2.2.4\r\n\r\n"
    L"SQLite 3.54.0 / Public domain\r\nSQLite powers the local component library and search. The upstream source notice and provenance are retained in sqlite-provenance.txt.\r\nhttps://www.sqlite.org/copyright.html\r\n\r\n"
    L"Zstandard 1.5.7 / BSD\r\nCopyright (c) Meta Platforms, Inc. and affiliates. Compresses the optional saved geometry and baked frames in .ark files. ARK uses the BSD alternative of its dual license; the full text is in zstd-LICENSE.txt and provenance in zstd-provenance.md.\r\nhttps://github.com/facebook/zstd/tree/v1.5.7\r\n\r\n"
    L"Windows and Visual C++ runtime\r\nWindows APIs, system fonts and the graphics driver are supplied under their respective vendor terms. Visual C++ runtime files are redistributable components of the build toolchain, subject to its terms. Segoe UI is used from the installed Windows system; no font files are bundled.\r\n\r\n"
    L"ARK artwork\r\nApplication artwork and icon atlases were generated for ARK with ImageGen; retained provenance is in ark-ui-assets.txt. The artwork is not assigned a new license here.\r\n\r\n"
    L"Complete retained notices and source references are in the bundled licenses folder. Notice retention alone does not establish completion of the product's commercial distribution audit. Customer designs and third-party component models retain their own rights and are not included by default.";}
struct AboutPanel{int page=0;HWND text=nullptr;HFONT font=nullptr;HBRUSH background=nullptr;};
static void paintAbout(HDC dc,int page){
    panelFill(dc,{0,0,760,670},RGB(19,27,36));panelFill(dc,{0,0,760,152},RGB(24,35,46));panelFill(dc,{0,0,760,2},RGB(107,209,235));
    panelBrand(dc,34,33,62);panelText(dc,L"ARK",{114,25,470,79},38,RGB(240,246,251),FW_SEMIBOLD);
    auto identity=productWide(app_iteration_code())+L"  /  Version "+productWide(app_version());panelText(dc,identity.c_str(),{117,82,700,112},16,RGB(115,212,236));
    panelText(dc,L"Native parametric CAD for people and agents",{116,117,730,143},14,RGB(156,178,195));
    if(page==0){
        panelText(dc,L"Created by MeshSphere",{34,224,720,265},27,RGB(229,239,247),FW_SEMIBOLD);
        panelText(dc,L"Bruno Santos  /  Project lead",{35,272,710,304},17,RGB(153,190,212));
        panelFill(dc,{34,327,726,475},RGB(26,38,50));panelText(dc,L"BUILT FOR PRECISE, EDITABLE DESIGN",{54,345,709,370},12,RGB(104,195,220),FW_SEMIBOLD);
        panelText(dc,L"C++20 CAD and UI  /  Win32  /  OpenGL 3.3 + GLSL\nRaster + optional GPU path tracing (software BVH)\nOCCT 8.0.1 exact B-rep modeling and exchange\nSQLite (C) library  /  PowerShell update tooling",{54,379,704,470},16,RGB(215,229,240));
        panelText(dc,L"Geometry lives in the model. The viewport displays its tessellation; shading and visual effects are presentation.",{35,489,717,539},16,RGB(164,184,200));
        panelText(dc,L"Third-party notices and the current licensing status are available in the next tab.",{35,553,716,597},14,RGB(125,152,173));
    }else if(page==1){panelText(dc,L"Licensing and acknowledgements",{34,217,725,247},21,RGB(229,239,247),FW_SEMIBOLD);}
    else{
        panelText(dc,L"A focused workspace",{34,224,720,266},25,RGB(229,239,247),FW_SEMIBOLD);
        panelText(dc,L"Edit features and sketches from the tree or inspector. Editable parameters, semantic references and exact native solids remain in the document.",{35,280,718,350},17,RGB(181,201,217));
        panelText(dc,L"NAVIGATE",{35,374,716,399},12,RGB(108,196,221),FW_SEMIBOLD);
        panelText(dc,L"Middle drag  Orbit       Shift + middle  Pan\nAlt + middle  Roll       Mouse wheel  Zoom\nF  Fit                   W  Wireframe\n1  Front    3  Top    5  Right    7  Isometric",{35,413,718,510},17,RGB(216,231,241));
        panelText(dc,L"Ctrl+O  Open    Ctrl+S  Save    Ctrl+N  New\nCtrl+Z / Ctrl+Y  Undo / redo    Ctrl+K  Commands",{35,540,718,589},16,RGB(160,184,203));
    }
    panelFill(dc,{34,615,726,616},RGB(47,66,83));panelText(dc,L"ARK Studio",{35,636,351,662},13,RGB(126,153,174));
}
static LRESULT CALLBACK aboutProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);}
    auto* state=(AboutPanel*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PRINT&&state){
        // The supplied DC has a client origin. Default WM_PRINT treats it as a
        // full-window origin and shifts children by the caption/frame; hidden
        // PrintWindow requests may also produce no pixels. Paint explicitly.
        HDC dc=(HDC)wp;paintAbout(dc,state->page);
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
            if(!(GetWindowLongW(child,GWL_STYLE)&WS_VISIBLE))continue;
            RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,window,(POINT*)&rect,2);int saved=SaveDC(dc);SetViewportOrgEx(dc,rect.left,rect.top,nullptr);IntersectClipRect(dc,0,0,rect.right-rect.left,rect.bottom-rect.top);
            if((GetWindowLongW(child,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW&&GetDlgCtrlID(child)!=1005){
                DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=GetDlgCtrlID(child);item.itemAction=ODA_DRAWENTIRE;item.hwndItem=child;item.hDC=dc;item.rcItem={0,0,rect.right-rect.left,rect.bottom-rect.top};SendMessageW(window,WM_DRAWITEM,item.CtlID,(LPARAM)&item);
            }else SendMessageW(child,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
            RestoreDC(dc,saved);
        }
        return 0;
    }
    if(message==WM_PAINT||message==WM_PRINTCLIENT){PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(window,&paint):(HDC)wp;paintAbout(dc,state?state->page:0);if(message==WM_PAINT)EndPaint(window,&paint);return 0;}
    if(message==WM_CTLCOLORBTN||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORSTATIC){HDC dc=(HDC)wp;SetTextColor(dc,RGB(214,230,241));SetBkColor(dc,RGB(26,38,50));return state?(LRESULT)state->background:0;}
    if(message==WM_DRAWITEM&&state){auto* item=(DRAWITEMSTRUCT*)lp;bool selected=item->CtlID>=1001&&item->CtlID<=1003&&(int)item->CtlID==1001+state->page;bool pressed=(item->itemState&ODS_SELECTED)!=0;panelFill(item->hDC,item->rcItem,selected||pressed?RGB(43,78,99):RGB(31,46,59));wchar_t label[128]{};GetWindowTextW(item->hwndItem,label,128);RECT text=item->rcItem;panelText(item->hDC,label,text,14,selected?RGB(181,237,250):RGB(192,212,226),FW_NORMAL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(selected)panelFill(item->hDC,{text.left,text.bottom-2,text.right,text.bottom},RGB(107,209,235));if(item->itemState&ODS_FOCUS){InflateRect(&text,-4,-4);DrawFocusRect(item->hDC,&text);}return TRUE;}
    if(message==WM_COMMAND&&state){int id=LOWORD(wp);if(id>=1001&&id<=1003){state->page=id-1001;ShowWindow(state->text,state->page==1?SW_SHOW:SW_HIDE);InvalidateRect(window,nullptr,FALSE);return 0;}
        if(id==1004){auto path=runtimeDirectory()/L"licenses";if(!std::filesystem::is_directory(path))MessageBoxW(window,L"This development runtime does not contain the packaged licenses folder. Retained texts are in third_party and the pinned OCCT SDK; distributable packages include the complete notices folder.",L"Third-party notices",MB_OK|MB_ICONINFORMATION);else ShellExecuteW(window,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        if(id==IDCANCEL||id==IDOK){DestroyWindow(window);return 0;}}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}return DefWindowProcW(window,message,wp,lp);
}
static HWND createAbout(HWND owner,AboutPanel& state,bool visible){
    HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=aboutProcedure;cls.hInstance=instance;cls.lpszClassName=L"ARK_ABOUT_PANEL";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_SHARED);RegisterClassW(&cls);
    RECT parent{};if(owner)GetWindowRect(owner,&parent);else SystemParametersInfoW(SPI_GETWORKAREA,0,&parent,0);RECT outer{0,0,760,670};DWORD style=WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;AdjustWindowRectEx(&outer,style,FALSE,WS_EX_DLGMODALFRAME);int width=outer.right-outer.left,height=outer.bottom-outer.top;
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,(L"About "+productIdentity()).c_str(),style,parent.left+(parent.right-parent.left-width)/2,parent.top+(parent.bottom-parent.top-height)/2,width,height,owner,nullptr,instance,&state);if(!window)return nullptr;
    BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof dark);state.font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");state.background=CreateSolidBrush(RGB(26,38,50));
    auto control=[&](const wchar_t* kind,const wchar_t* label,int x,int y,int w,int h,int id,DWORD extra=0){if(std::wcscmp(kind,L"BUTTON")==0)extra|=BS_OWNERDRAW;HWND child=CreateWindowExW(0,kind,label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|extra,x,y,w,h,window,(HMENU)(INT_PTR)id,instance,nullptr);SendMessageW(child,WM_SETFONT,(WPARAM)state.font,TRUE);return child;};
    control(L"BUTTON",L"Overview",34,170,123,34,1001);control(L"BUTTON",L"Third-party notices",166,170,180,34,1002);control(L"BUTTON",L"Workspace guide",355,170,163,34,1003);
    state.text=control(L"EDIT",licenseSummary(),34,256,692,342,1005,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);SendMessageW(state.text,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(12,12));ShowWindow(state.text,SW_HIDE);
    control(L"BUTTON",L"Open notices folder",441,631,180,31,1004);control(L"BUTTON",L"Close",637,631,89,31,IDCANCEL);
    if(visible){ShowWindow(window,SW_SHOW);UpdateWindow(window);}return window;
}
static void destroyAboutResources(AboutPanel& state){if(state.font)DeleteObject(state.font);if(state.background)DeleteObject(state.background);}
static void showAbout(HWND owner){AboutPanel state;HWND window=createAbout(owner,state,true);if(!window)return;EnableWindow(owner,FALSE);SetFocus(GetDlgItem(window,IDCANCEL));MSG msg{};
    while(IsWindow(window)&&GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);continue;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    EnableWindow(owner,TRUE);if(IsWindow(window))DestroyWindow(window);destroyAboutResources(state);SetForegroundWindow(owner);if(msg.message==WM_QUIT)PostQuitMessage((int)msg.wParam);
}
static bool captureProductPanel(const std::string& path,int width,int height,const std::function<void(HDC)>& paint){
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}auto old=SelectObject(dc,bitmap);paint(dc);GdiFlush();Image result;result.w=width;result.h=height;result.px.resize((size_t)width*height*3);auto* source=(unsigned char*)pixels;
    size_t lit=0,bright=0;for(size_t i=0;i<(size_t)width*height;++i){result.px[i*3]=source[i*4+2];result.px[i*3+1]=source[i*4+1];result.px[i*3+2]=source[i*4];if(source[i*4]||source[i*4+1]||source[i*4+2])++lit;if(source[i*4]>120&&source[i*4+1]>120&&source[i*4+2]>120)++bright;}
    // Every product panel has a lit background and substantial actual text.
    // A successful API return without pixels must never count as visual proof.
    bool ok=lit>(size_t)width*height/2&&bright>1000&&writePNG(path.c_str(),result);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok;
}
static int startupUiSelfTest(const std::string& path){
    StartupSplash skipped(true);if(!skipped.skipped())return 701;
    StartupSplash deadline(false,false);if(!deadline.waitForDeadline(3500)||!deadline.created())return 702;deadline.finish();if(deadline.visibleMilliseconds()>StartupSplash::maximumMs)return 703;
    ULONGLONG begin=GetTickCount64();StartupSplash ready(false,false);ready.finish();ULONGLONG readyMs=GetTickCount64()-begin;if(readyMs>200)return 704;
    if(!captureProductPanel(path+".splash-start.png",620,340,[](HDC dc){paintSplash(dc,620,340,0);})||!captureProductPanel(path+".splash-animation.png",620,340,[](HDC dc){paintSplash(dc,620,340,.85);}))return 705;
    AboutPanel state;HWND about=createAbout(nullptr,state,false);if(!about)return 706;
    RECT client{};GetClientRect(about,&client);bool ok=client.right==760&&client.bottom==670;
    auto clientBounds=[&](int id){RECT rect{};GetWindowRect(GetDlgItem(about,id),&rect);MapWindowPoints(nullptr,about,(POINT*)&rect,2);return rect;};
    const auto tab=clientBounds(1001),close=clientBounds(IDCANCEL);ok&=tab.left==34&&tab.top==170&&tab.bottom==204&&close.right<=client.right&&close.bottom<=client.bottom;
    for(int page=0;page<3;++page){SendMessageW(about,WM_COMMAND,1001+page,0);ok&=state.page==page&&((GetWindowLongW(state.text,GWL_STYLE)&WS_VISIBLE)!=0)==(page==1);bool childInk=false;ok&=captureProductPanel(path+".about-"+std::to_string(page)+".png",760,670,[&](HDC dc){
        SendMessageW(about,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);GdiFlush();
        auto ink=[&](RECT rect){size_t count=0;for(int y=rect.top;y<rect.bottom;y+=2)for(int x=rect.left;x<rect.right;x+=2){COLORREF pixel=GetPixel(dc,x,y);if(pixel!=CLR_INVALID&&GetRValue(pixel)>120&&GetGValue(pixel)>120&&GetBValue(pixel)>120)++count;}return count;};
        childInk=ink(tab)>20&&ink(close)>15&&(page!=1||ink({46,268,709,586})>300);
    });ok&=childInk;}
    SendMessageW(about,WM_COMMAND,IDCANCEL,0);ok&=!IsWindow(about);destroyAboutResources(state);if(!ok)return 707;
    std::ofstream receipt(path+".startup-ui-test.log");receipt<<"PASS identity="<<app_iteration_code()<<" version="<<app_version()<<"; headless skipped; actual timeout="<<deadline.visibleMilliseconds()<<"ms <=3000ms; immediate-ready close="<<readyMs<<"ms; three About tabs and Close; no document/settings load or write.\n";return receipt?0:708;
}
} // namespace arkstudio
