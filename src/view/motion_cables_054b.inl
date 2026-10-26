// Prepare exact native cable sweeps privately, then upload only the selected
// frame. CAD recipes, native bodies, history and revisions are never modified.
namespace motion_cables_view_054b {
struct Original {u64 feature=0;Mesh mesh;PartPick pick;};
struct State {
    std::future<MotionCableDisplay> worker;std::shared_ptr<std::atomic_bool> cancel;
    std::shared_ptr<std::atomic<size_t>> done,total;MotionCableDisplay display;
    std::map<size_t,Original> originals;u64 revision=~u64{},epoch=~u64{};
    std::string motion,error;bool ready=false,autoPlay=false;size_t shown=~size_t(0);
};
static State state;
static bool current(){return state.revision==g_session.revision&&state.epoch==g_objectDocumentEpoch&&state.motion==g_bakedMotions.selected;}
static void upload(size_t part,const Mesh& mesh,const PartPick& pick){
    if(part>=g_cpuMeshes.size())return;g_cpuMeshes[part]=mesh;g_partPicks[part]=pick;
    uploadMesh(g_meshes[part],g_cpuMeshes[part]);uploadEdges(part);g_reports[part]=analyzeMesh(mesh);g_bvhs[part]=MeshBVH(mesh);++g_displayMeshSerial;
}
}
static void restoreMotionCables054B(){using namespace motion_cables_view_054b;for(const auto& [part,original]:state.originals){bool belongs=false;for(size_t i=0;i<g_instancePart.size();++i)if(g_instancePart[i]==part&&g_bodyIds[i]==original.feature){belongs=true;break;}if(belongs)upload(part,original.mesh,original.pick);}state.originals.clear();state.shown=~size_t(0);}
static void cancelMotionCables054B(){using namespace motion_cables_view_054b;state.autoPlay=false;if(state.worker.valid()&&state.cancel)state.cancel->store(true);restoreMotionCables054B();}
static void shutdownMotionCables054B(){using namespace motion_cables_view_054b;state.autoPlay=false;if(state.cancel)state.cancel->store(true);if(state.worker.valid())try{state.worker.get();}catch(...){}state.display={};state.originals.clear();}
static void pollMotionCables054B(){using namespace motion_cables_view_054b;
    if(!state.worker.valid())return;if(!current()||g_closing){state.cancel->store(true);state.autoPlay=false;}
    if(state.worker.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){if(current()){g_bakedMotions.status="Preparing flexible cables / "+std::to_string(state.done->load())+" / "+std::to_string(state.total->load())+" frames / Stop cancels";g_status=g_bakedMotions.status;}return;}
    try{auto result=state.worker.get();if(!current()||state.cancel->load())return;state.display=std::move(result);state.ready=true;state.error.clear();g_bakedMotions.status="Flexible cables ready / "+std::to_string(state.display.frameCount)+" native frames";g_status=g_bakedMotions.status;if(state.autoPlay){state.autoPlay=false;g_bakedMotions.active=g_bakedMotions.playing=true;g_bakedMotions.shownFrame=~size_t(0);}}
    catch(const std::exception& e){state.ready=false;state.autoPlay=false;if(current()&&!state.cancel->load()){state.error=e.what();g_bakedMotions.status=state.error;g_status="Flexible cable preparation failed: "+state.error;}}
}
static bool ensureMotionCables054B(std::string& reason){using namespace motion_cables_view_054b;pollMotionCables054B();if(state.worker.valid()){reason="Preparing flexible cable frames / Stop cancels";return false;}if(current()&&state.ready)return true;
    if(current()&&!state.error.empty()){reason=state.error+" / correct the route and bake again";return false;}restoreMotionCables054B();state.display={};state.ready=false;state.error.clear();state.revision=g_session.revision;state.epoch=g_objectDocumentEpoch;state.motion=g_bakedMotions.selected;
    const auto features=motionCableFeatures(g_doc,g_bakedMotions.frames);if(features.empty()){state.ready=true;return true;}
    auto document=std::make_shared<Document>(g_doc);auto frames=g_bakedMotions.frames;state.cancel=std::make_shared<std::atomic_bool>(false);state.done=std::make_shared<std::atomic<size_t>>(0);state.total=std::make_shared<std::atomic<size_t>>(features.size()*frames.frameCount);
    auto cancelled=state.cancel;auto done=state.done,total=state.total;state.worker=std::async(std::launch::async,[document,frames=std::move(frames),cancelled,done,total]()mutable{return prepareMotionCableDisplay(*document,frames,*cancelled,[done,total](size_t completed,size_t count){done->store(completed);total->store(count);});});reason="Preparing native flexible cable frames / Stop cancels";return false;
}
static void applyMotionCableFrame054B(size_t frame){using namespace motion_cables_view_054b;if(!state.ready||!current()||frame==state.shown||state.display.features.empty())return;if(frame>=state.display.frameCount)return;state.shown=frame;
    for(size_t f=0;f<state.display.features.size();++f){const auto feature=state.display.features[f];std::set<size_t> parts;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==feature)parts.insert(g_instancePart[i]);for(size_t part:parts){if(!state.originals.count(part))state.originals.emplace(part,Original{feature,g_cpuMeshes[part],g_partPicks[part]});upload(part,state.display.meshes[frame*state.display.features.size()+f]->mesh(),PartPick{});}}
}
