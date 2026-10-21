// 048B Exploded Views and Visualize: one panel docked right of the viewport,
// sharing the dock with Baked Motions (one open at a time). The explode factor,
// animation and Visualize presets are view state; views are edited only through
// revision-checked explode.set/rename/remove transactions.
struct ExplodedViewsPanel {
    bool open=false,visualize=false,animating=false,sliding=false,applied=false;
    std::string selected,motionView;double factor=0,motionFactor=-1;int scroll=0,step=-1;
    std::vector<u64> pending; // instances picked in the viewport for the next step
    u64 shownRevision=~u64(0),shownEpoch=~u64(0),shownSerial=0;double shownFactor=-1;std::string shownView;
    // Visualize: look 0 studio, 1 product, 2 technical; background 0 dark, 1 light, 2 white;
    // resolution 0 viewport, 1 1280x720, 2 1920x1080, 3 2560x1440; engine 0 GPU viewport, 1 software.
    int look=0,background=1,resolution=0,engine=0,samplesTarget=256,softwareSamples=4;bool balloons=true,table=true;
    std::string exportPath,lastExport;bool savedTraceRequest=true,saved=false,savedGrid=true,savedLight=false,savedSatin=true,savedEdges=false,savedTrace=false,savedCube=true;int savedOcclusion=2;
    u64 bomRevision=~u64(0),bomEpoch=~u64(0);std::vector<VisualizeBomItem> bom;std::string bomError;
    unsigned overlayTexture=0;std::uint64_t overlayKey=0;int overlayW=0,overlayH=0;bool overlayValid=false;
    bool captureRequested=false;Image capture;
};
static ExplodedViewsPanel g_explode;
static bool g_whiteBackground=false; // Visualize white background; raster background shader only
static int explodeDockWidth(){if(!g_explode.open)return 0;const int sidebar=std::clamp((int)(winW*.21),280,352);return std::clamp(std::min((int)(winW*.24),380),260,std::max(260,winW-sidebar-420));}
static bool explodedDisplayActive(){return g_explode.applied;}
static void explodedBakedFrame(size_t frame);
static void collapseExplodedDisplay(const char* reason=nullptr);
static void explodedViewsAfterApply(const arkjson::Value& args);
static bool explodedViewPress(int zone,double fraction);
static bool explodedViewDrag(int x,bool button);
static void explodedViewPicked(int body);
static void captureVisualizeScene();
static bool explodedViewsAction(int id);
