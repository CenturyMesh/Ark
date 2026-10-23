// C.04/A.04: one session-local reduced-network study; authored CAD is untouched.
struct RubberFrame059 {double time=0;std::vector<float> positions;};
struct RubberPanel059 {
    bool open=false,running=false,cancelling=false,stale=false,active=false,playing=false,ground=true;
    std::string backend="gpu",sourceSha256,message="Create a rubber body, or select an existing one, then Run.",error;
    double duration=1,step=1./240,groundZ=0,friction=.7,damping=2,time=0;
    unsigned fps=30,iterations=8,around=32,rows=8;
    Vec3 gravity{0,0,-9.80665},velocity{},translation{0,0,100};
    u64 id=0,node=0,revision=~u64(0),epoch=~u64(0),generation=0,job=0,uploads=0;int copy=0;Mat4 world;
    std::vector<RubberFrame059> frames;Mesh mesh;GPUMesh gpu;size_t shownFrame=~size_t(0);AABB bounds;
    arkjson::Value request,report;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
};
static RubberPanel059 g_rubber059;
static int rubberDockWidth059(){return g_rubber059.open?std::clamp((int)(winW*.24),300,360):0;}
static bool rubberAction059(int);static bool rubberKey059(WPARAM);
static void advanceRubber059(double);static void stopRubberDisplay059();
static void invalidateRubber059(const std::string&);static void releaseRubberGpu059();
