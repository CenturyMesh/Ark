static arkjson::Value prepareAuthoringWithProgress(const arkjson::Value& args){
    if(!g_authoringWorker)g_authoringWorker=std::make_unique<AsyncAuthoringWorker>([](const std::atomic_bool& cancelled){for(unsigned wait=0;wait<g_authoringTestDelayMs&&!cancelled.load();wait+=5)std::this_thread::sleep_for(std::chrono::milliseconds(5));});
    const auto captured=std::chrono::steady_clock::now();const auto epoch=g_objectDocumentEpoch;
    g_authoringWorker->start(g_doc,g_session.revision,epoch,args);g_progressTiming030.captureMs=realtimeMs030(captured);
    HDC hdc=GetDC(g_window);if(!hdc){g_authoringWorker->cancel();auto result=g_authoringWorker->take();if(result)return AsyncAuthoringWorker::publish(std::move(*result),g_session,g_objectDocumentEpoch);return g_session.completePreparedApply(args,nullptr);}
    struct Finish {HDC dc;~Finish(){g_preparingEdit=false;g_cameraDrag.active=false;ReleaseCapture();ReleaseDC(g_window,dc);if(g_busyClosePending){g_busyClosePending=false;PostMessage(g_window,WM_CLOSE,0,0);}}} finish{hdc};
    g_preparingEdit=true;g_busyStarted=std::chrono::steady_clock::now();g_busyLabel="Preparing edit / drawing remains interactive";g_componentDragIntent={};
    {std::lock_guard lock(g_busyProgressMutex);g_busyFeatureProgress={};}
    std::optional<AsyncAuthoringWorker::Result> completed;
    for(;;){
        if(g_closing||(g_authoringExternalCancel&&g_authoringExternalCancel()))g_authoringWorker->cancel();
        MSG message{};while(PeekMessageA(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT){g_busyClosePending=true;g_authoringWorker->cancel();continue;}
            if(message.hwnd==g_window){TranslateMessage(&message);DispatchMessageA(&message);++g_busyPumpCount;}
            else if(message.message==WM_PAINT&&message.hwnd)ValidateRect(message.hwnd,nullptr);
        }
        if((completed=g_authoringWorker->take()))break;
        const auto progress=g_authoringWorker->progress();{std::lock_guard lock(g_busyProgressMutex);g_busyFeatureProgress={progress.feature,progress.name,progress.index,progress.total,progress.completed,progress.reused};}
        drawBusyFrame(hdc);
        if(g_authoringPumpTest)g_authoringPumpTest();
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    g_progressTiming030.workerMs=completed->milliseconds;const auto publishing=std::chrono::steady_clock::now();
    // This is the sole live publication point; the worker never sees g_session.
    const bool cancelled=g_closing||(g_authoringExternalCancel&&g_authoringExternalCancel());
    auto response=AsyncAuthoringWorker::publish(std::move(*completed),g_session,g_objectDocumentEpoch,cancelled);g_progressTiming030.resumeMs=realtimeMs030(publishing);return response;
}

// During private evaluation this handler deliberately exposes only view state.
// Existing edit modes, tool buttons and drop/open/save actions cannot re-enter
// the session. Incoming external calls are queued by the normal window handler.
static bool preparingAuthoringInput(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto cancel=[](){if(g_authoringWorker)g_authoringWorker->cancel();};
    if(message==WM_CLOSE){g_busyClosePending=true;cancel();return true;}
    if(message==WM_COMMAND||message==WM_CHAR||message==WM_LBUTTONDBLCLK||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP)return true;
    if(message==WM_DROPFILES){DragFinish((HDROP)wp);return true;}
    if(message==WM_KEYDOWN){if(wp==VK_ESCAPE)cancel();else if(wp=='F'&&!(GetKeyState(VK_CONTROL)&0x8000))frameScene();return true;}
    if(message==WM_KEYUP)return true;
    if(message==WM_MOUSEWHEEL){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&p);if(p.x>=viewX&&p.x<viewX+viewW&&p.y>=viewY&&p.y<viewY+viewH)tDist=clamp(tDist*(GET_WHEEL_DELTA_WPARAM(wp)>0?.87:1.15),2.,1e9);return true;}
    if(message==WM_MBUTTONDOWN){const int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);if(x>=viewX&&x<viewX+viewW&&y>=viewY&&y<viewY+viewH){beginCameraDrag(x,y);SetCapture(window);}return true;}
    if(message==WM_MOUSEMOVE){g_pointerX=GET_X_LPARAM(lp);g_pointerY=GET_Y_LPARAM(lp);if((wp&MK_MBUTTON)&&g_cameraDrag.active)moveCameraDrag(g_pointerX,g_pointerY,(wp&MK_SHIFT)!=0,false,(GetKeyState(VK_MENU)&0x8000)!=0);else hoverBody=pickBody(g_pointerX,g_pointerY);return true;}
    if(message==WM_MBUTTONUP||message==WM_CAPTURECHANGED){g_cameraDrag.active=false;if(message==WM_MBUTTONUP)ReleaseCapture();return true;}
    if(message==WM_LBUTTONDOWN){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(PtInRect(&g_authoringCancelRect,p))cancel();return true;}
    if(message==WM_LBUTTONUP){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(PtInRect(&g_authoringCancelRect,p))return true;if(p.x>=viewX&&p.x<viewX+viewW&&p.y>=viewY&&p.y<viewY+viewH){selBody=pickBody(p.x,p.y);g_viewportContainer=0;g_explorerFeature=0;g_picked.clear();refreshSelectionLines();}return true;}
    return false;
}
