// 044B Baked Motions: a panel docked right of the viewport. Playback replaces
// display transforms with baked frames; the document and its revision never change.
struct BakedMotionsRow {std::string name,detail,badge;double duration=0;bool baked=false,stale=false,invalid=false;std::map<std::string,double> starts;std::vector<std::string> units;};
struct BakedMotionsPanel {
    bool open=false,playing=false,loop=true,active=false,scrubbing=false;
    double time=0,speed=1;int scroll=0;std::string selected;
    u64 revision=~u64(0),epoch=~u64(0);std::vector<BakedMotionsRow> rows;std::string digest,note;bool digestKnown=false;
    std::string cacheName;BakedMotionFrames frames;BakedMotionFramesSource framesSource;bool framesValid=false;CompiledMotion graph;bool graphValid=false;std::string graphError,status;arkjson::Value diagnostics;
    std::vector<std::vector<float>> curves;std::vector<float> lo,hi,rangeLo,rangeHi;std::vector<std::string> units;std::vector<std::vector<MotionBezierHandle>> handles;std::vector<std::vector<double>> seams;
    std::vector<int> display;size_t displayCount=0;u64 displayRevision=~u64(0);size_t shownFrame=~size_t(0);
    // 046B clearance evidence: shown only for the motion, revision and document epoch it was measured at.
    arkjson::Value clearance;std::string clearanceMotion,clearanceError;u64 clearanceRevision=~u64(0),clearanceEpoch=~u64(0);double clearanceThreshold=1;bool clearanceRunning=false;
};
static BakedMotionsPanel g_bakedMotions;
static void stopBakedMotion(bool restore=true);static bool bakedMotionsAction(int id);
static int bakedMotionsDockWidth(){if(!g_bakedMotions.open)return 0;const int sidebar=std::clamp((int)(winW*.21),280,352);return std::clamp(std::min((int)(winW*.24),380),260,std::max(260,winW-sidebar-420));}
