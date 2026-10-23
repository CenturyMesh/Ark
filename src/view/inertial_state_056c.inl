// Session-local simulation study. Results only replace display transforms.
struct InertialIdentity056 {
    u64 feature=0,node=0;int copy=0;
    bool operator<(const InertialIdentity056& b)const{return std::tie(feature,node,copy)<std::tie(b.feature,b.node,b.copy);}
    bool operator==(const InertialIdentity056&)const=default;
};
struct InertialLoad056 {int fixing=0;Vec3 force,torque,velocity,omega;}; // 0 CAD, 1 dynamic, 2 fixed
struct InertialFrame056 {double time=0;std::vector<Mat4> poses;};
struct InertialPanel056 {
    bool open=false,running=false,cancelling=false,active=false,playing=false,scrubbing=false,stale=false;
    bool selectedOnly=false,ground=false,contacts=true,joints=true;
    double duration=1,step=1./240,groundZ=0,friction=.5,restitution=.1,time=0,speed=1;
    Vec3 gravity{0,0,-9.80665};unsigned fps=30,iterations=16;std::string backend="gpu";
    u64 revision=~u64(0),epoch=~u64(0),loadsEpoch=~u64(0),generation=0,job=0;
    size_t shownFrame=~size_t(0);std::vector<InertialIdentity056> identities;std::vector<InertialFrame056> frames;
    std::map<InertialIdentity056,InertialLoad056> loads;
    arkjson::Value report,request;std::string message="Configure a study, assign density in Material, then Run.",error;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
};
static InertialPanel056 g_inertial056;
static int inertialDockWidth056(){if(!g_inertial056.open)return 0;return std::clamp((int)(winW*.24),290,360);}
static bool inertialAction056(int);static bool inertialKey056(WPARAM);
static void stopInertialDisplay056(bool restore=true);static void advanceInertial056(double);
static void scrubInertial056(double);static void invalidateInertial056(const std::string&);
