// 046B background exact reads (A.04). Allow-listed read-only tools run on a
// private session copied here, on the UI thread, at the revision reached after
// every earlier queued request. Completions return through the UI thread; writes
// and everything else keep the serialized UI queue. See docs/ASYNC_EXACT_046B.md.
static constexpr UINT WM_ARK_EXACT=WM_APP+28;
static constexpr int exactJobCancelAction046=870;
struct ExactDelivery046 {ExactJobScheduler046B::Completion completion;std::function<void(ExactJobScheduler046B::Completion&)> finish;};
static std::mutex g_exactDoneMutex046;static std::deque<ExactDelivery046> g_exactDone046;
static std::unique_ptr<ExactJobScheduler046B> g_exactJobs046;
static bool g_exactJobsTestMode046=false; // headless self-test opt-in, like g_busyTestMode
static int g_exactJobsMode046=-1;         // ARK_STUDIO_EXACT_JOBS=0 keeps the earlier UI-thread dispatch for comparisons
static double g_exactSnapshotMs046=0;     // last UI-thread session copy, measured
static bool exactJobsEnabled046(){
    if(g_exactJobsMode046<0){const char* value=std::getenv("ARK_STUDIO_EXACT_JOBS");g_exactJobsMode046=value&&std::string(value)=="0"?0:1;}
    return g_exactJobsMode046==1&&g_uiReady&&!g_closing&&(!g_headless||g_exactJobsTestMode046);
}
static ExactJobScheduler046B& exactJobs046(){if(!g_exactJobs046)g_exactJobs046=std::make_unique<ExactJobScheduler046B>(2);return *g_exactJobs046;}
static size_t exactJobsActive046(){return g_exactJobs046?g_exactJobs046->active():0;}
// Host labels: a later change does not invalidate the read; it stays labelled.
static void labelExactDelivery046(ExactJobScheduler046B::Completion& done){
    using V=arkjson::Value;auto* data=done.result.find("structuredContent");if(!data||data->t!=V::Obj)return;V payload=*data;V job=payload.find("exact_job")?*payload.find("exact_job"):V::makeObj();
    const bool replaced=done.epoch!=g_objectDocumentEpoch;job.set("current_revision",V::makeNum((double)g_session.revision));job.set("document_replaced_since_read",V::makeBool(replaced));job.set("document_changed_since_read",V::makeBool(replaced||g_session.revision!=done.revision));
    payload.set("exact_job",job);done.result.set("structuredContent",payload);if(auto* content=done.result.find("content");content&&content->t==V::Arr){V parts=*content;for(auto& part:parts.arr)if(part.find("type")&&part.find("type")->asStr()=="text")part.set("text",V::makeStr(payload.dump()));done.result.set("content",parts);}
}
static u64 startExactJob046(const std::string& tool,const arkjson::Value& args,std::function<bool()> cancelled,const std::string& label,std::function<void(ExactJobScheduler046B::Completion&)> finish){
    if(!exactJobAllowed046B(tool,args))return 0;
    ExactJobScheduler046B::Request job;job.tool=tool;job.args=args;job.label=label.empty()?tool:label;job.revision=g_session.revision;job.epoch=g_objectDocumentEpoch;job.cancelled=std::move(cancelled);
    const auto copyStarted=RealtimeClock030::now();job.session=captureExactSession046B(g_doc,g_session.revision);g_exactSnapshotMs046=realtimeMs030(copyStarted);
    job.done=[finish=std::move(finish)](ExactJobScheduler046B::Completion completion)mutable{{std::lock_guard lock(g_exactDoneMutex046);g_exactDone046.push_back({std::move(completion),std::move(finish)});}if(!g_closing&&g_window)PostMessage(g_window,WM_ARK_EXACT,0,0);};
    return exactJobs046().submit(std::move(job));
}
// Internal Studio API: one read-only exact tool without blocking the UI thread.
// onComplete runs later on the UI thread with the labelled result and the
// revision the read observed. Returns 0, without calling onComplete, when the
// tool or arguments are outside the allow-list or background jobs are off; the
// caller then uses runTool. cancelled is polled from the job thread.
using ExactJobDone046B=std::function<void(const arkjson::Value& result,u64 observedRevision)>;
static u64 submitExactJob046B(const std::string& tool,const arkjson::Value& args,ExactJobDone046B onComplete,const std::string& label={},std::function<bool()> cancelled={}){
    if(!onComplete||!exactJobsEnabled046())return 0;
    return startExactJob046(tool,args,std::move(cancelled),label,[onComplete=std::move(onComplete)](ExactJobScheduler046B::Completion& done){
        labelExactDelivery046(done);const auto* error=done.result.find("isError");g_error=error&&error->b;const auto* data=done.result.find("structuredContent");
        g_status=g_error?(data&&data->find("error")?data->find("error")->asStr():done.tool+" failed"):done.tool+" completed off the UI thread / observed revision "+std::to_string(done.revision);onComplete(done.result,done.revision);});
}
// The job then completes as CANCELLED; a running native request is stopped.
static bool cancelExactJob046B(u64 id){return g_exactJobs046&&g_exactJobs046->cancel(id);}
// UI queue requests (Live MCP, Assistant, team). The actor, revision tracker,
// presence and activity ledger are applied here as AgentSession::call would.
static bool routeExactJob046(std::unique_ptr<UiToolRequest>& request){
    if(!exactJobsEnabled046()||!exactJobAllowed046B(request->tool,request->args))return false;
    auto holder=std::make_shared<std::unique_ptr<UiToolRequest>>(std::move(request));UiToolRequest* owned=holder->get();
    const std::string presenceActor=owned->roleId.empty()?(owned->revision?"assistant":"mcp"):owned->roleId;
    auto stop=owned->stop;auto cancel=owned->cancel;const auto hostStarted=RealtimeClock030::now();
    auto finish=[holder,presenceActor,hostStarted](ExactJobScheduler046B::Completion& done){
        auto& owned=*holder;
        using V=arkjson::Value;labelExactDelivery046(done);auto result=std::move(done.result);
        const auto* actor=owned->args.find("actor");const std::string kind=actor&&actor->t==V::Obj&&actor->find("kind")?actor->find("kind")->asStr():std::string();
        if(!done.cancelled&&actor&&done.epoch==g_objectDocumentEpoch&&(kind=="agent"||(kind=="client"&&agentActivityFailed(owned->tool,result))))result=recordAgentActivity(g_doc,owned->tool,owned->args,*actor,std::move(result),done.revision,done.revision,done.runMs);
        finishPresence030(presenceActor,result);
        // The caller observed the snapshot revision: a later write is never rebased into it.
        if(owned->revision)*owned->revision=done.revision;
        rememberToolTiming030({owned->tool,realtimeMs030(owned->posted,hostStarted),realtimeMs030(hostStarted),{g_exactSnapshotMs046,done.runMs,0}});
        const auto* error=result.find("isError");g_error=error&&error->b;const auto* data=result.find("structuredContent");
        g_status=g_error?(data&&data->find("error")?data->find("error")->asStr():owned->tool+" failed"):owned->tool+" completed off the UI thread / observed revision "+std::to_string(done.revision);
        owned->complete(std::move(result));
    };
    try{if(startExactJob046(owned->tool,owned->args,[stop,cancel]{return stop.stop_requested()||(cancel&&*cancel)||g_closing;},owned->tool,std::move(finish))){observePresence030(presenceActor,owned->tool,owned->args);return true;}}
    catch(const std::exception& error){g_status=std::string("Background exact read unavailable: ")+error.what();}
    request=std::move(*holder); // not submitted: it runs on the UI thread as before
    return false;
}
// The session executor owns g_doc while busy; deliveries wait for the UI thread.
static void drainExactJobs046(){
    if(g_busy)return;std::deque<ExactDelivery046> ready;{std::lock_guard lock(g_exactDoneMutex046);ready.swap(g_exactDone046);}
    for(auto& delivery:ready)try{delivery.finish(delivery.completion);}catch(...){}
}
static void shutdownExactJobs046(){
    if(g_exactJobs046)g_exactJobs046->shutdown();
    std::deque<ExactDelivery046> ready;{std::lock_guard lock(g_exactDoneMutex046);ready.swap(g_exactDone046);}
    for(auto& delivery:ready)try{delivery.completion.cancelled=true;delivery.completion.result=exactJobError046B(delivery.completion.tool,delivery.completion.revision,"CANCELLED","Studio closed before the exact read was delivered");delivery.finish(delivery.completion);}catch(...){}
    g_exactJobs046.reset();
}
static bool exactJobsAction046(int id){
    if(id!=exactJobCancelAction046)return false;
    if(g_exactJobs046){g_exactJobs046->cancelAll();g_status="Cancelling background exact checks / the document is unchanged";}return true;
}
static std::string exactJobsProgress046(){
    if(!g_exactJobs046)return {};const auto rows=g_exactJobs046->status();if(rows.empty())return {};
    return std::to_string(rows.size())+" exact read"+(rows.size()>1?"s":"")+" running off the Studio UI thread";
}
// Busy chip: drawn over the live viewport; orbit and selection stay active.
template<class Rect,class Button>static void drawExactJobsOverlay046(Rect rect,Button button){
    if(!g_exactJobs046)return;const auto rows=g_exactJobs046->status();if(rows.empty())return;
    const auto& first=*std::min_element(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    const bool cancelling=std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.cancelRequested;});
    // Prefer space above the display dock. In a short viewport the fallback
    // overlaps it; the foreground chip must own input in its visible bounds.
    float w=std::min(520.f,(float)viewW-32),h=74,x=(float)viewX+16,dock=(float)(viewY+viewH)-84-(g_motionActive?92.f:0.f),y=dock-h-10;
    if(y<viewY+90)y=(float)(viewY+viewH)-h-16;
    if(g_showViewCube&&y<g_cubeY+g_cubeS+20){x=(float)(g_cubeX+g_cubeS+20);w=std::min(w,(float)(viewX+viewW)-x-16);}
    rect(x+3,y+4,w,h,8,.005f,.012f,.022f,.28f,0,0,0,0,0);rect(x,y,w,h,8,.025f,.070f,.095f,.95f,.39f,.77f,.98f,.75f,1);
    char line[200];std::snprintf(line,sizeof line,"%s / %s / %.1f s%s",cancelling?"CANCELLING EXACT CHECK":first.running?"EXACT CHECK RUNNING":"EXACT CHECK QUEUED",first.label.c_str(),first.elapsedMs/1000.,rows.size()>1?(" / +"+std::to_string(rows.size()-1)+" more").c_str():"");
    textOut(fitText(line,.62f,(int)w-118),x+12,y+9,.62f,.65f,.91f,.99f);
    textOut(fitText("Revision "+std::to_string(first.revision)+" snapshot / view and selection stay live / result is labelled",.50f,(int)w-118),x+12,y+33,.50f,.57f,.77f,.86f);
    const float phase=(float)std::fmod(first.elapsedMs/1000.*1.6,1.);rect(x+12,y+58,w-128,4,2,.20f,.28f,.36f,1,0,0,0,0,0);rect(x+12+phase*(w-128-70),y+58,70,4,2,.42f,.79f,.98f,1,0,0,0,0,0);
    const size_t firstOverlayZone=g_zones.size();
    button(cancelling?"Stopping":"Cancel",x+w-100,y+(h-32)/2,88,exactJobCancelAction046);
    // The dock deliberately prepends its controls. Match this chip's later
    // paint order without disturbing either group's internal hit order.
    const size_t overlayControls=g_zones.size()-firstOverlayZone;
    std::rotate(g_zones.begin(),g_zones.begin()+firstOverlayZone,g_zones.end());
    g_zones.insert(g_zones.begin()+overlayControls,Zone{x,y,w,h,0});
}
