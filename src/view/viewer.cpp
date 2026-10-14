// ============================================================================
//  ARK-Point :: ark-view â€” native Win32 + OpenGL 3.3-core viewport.
//
//  Look & feel: studio gradient, clay shading, screen-space ambient occlusion,
//  visible feature edges, soft ground shadows, distance-faded grid, damped
//  orbit camera, orthographic named views, and a GPU-drawn view toolbar.
//
//  Headless:  ark-view --shot out.png [width] [height]
// ============================================================================
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>
#include <commdlg.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <objbase.h>
#include <wincodec.h>

// ---- MinGW gl.h is GL 1.1: declare what we need ourselves ------------------
#ifndef GL_VERSION_3_0_DEFS
typedef ptrdiff_t GLsizeiptr;
#define GL_ARRAY_BUFFER            0x8892
#define GL_ELEMENT_ARRAY_BUFFER    0x8893
#define GL_STATIC_DRAW             0x88E4
#define GL_DYNAMIC_DRAW            0x88E8
#define GL_TEXTURE0                0x84C0
#define GL_TEXTURE1                0x84C1
#define GL_TEXTURE2                0x84C2
#define GL_R8                      0x8229
#define GL_RGBA8                   0x8058
#define GL_RGBA16F                 0x881A
#define GL_HALF_FLOAT              0x140B
#define GL_RGBA32F                 0x8814
#define GL_TEXTURE_BUFFER          0x8C2A
#define GL_MAX_TEXTURE_BUFFER_SIZE 0x8C2B
#define GL_DEPTH_COMPONENT24       0x81A6
#define GL_FRAMEBUFFER             0x8D40
#define GL_COLOR_ATTACHMENT0       0x8CE0
#define GL_DEPTH_ATTACHMENT        0x8D00
#define GL_FRAMEBUFFER_COMPLETE    0x8CD5
#define GL_CLAMP_TO_EDGE           0x812F
#endif
#ifndef GL_VERSION_2_0_DEFS
#define GL_COMPILE_STATUS          0x8B81
#define GL_LINK_STATUS             0x8B82
#define GL_VERTEX_SHADER           0x8B31
#define GL_FRAGMENT_SHADER         0x8B30
#endif
// blend func separate not needed; plain 1.1 blend suffices
#endif
#include "ark/model.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/drawings.hpp"
#include "ark/cad_import.hpp"
#include "ark/holes_orchestrator.hpp"
#include "ark/design_containers.hpp"
#include "ark/datum_authoring.hpp"
#include "ark/spatial_sketch.hpp"
#include "ark/native_features.hpp"
#include "ark/references.hpp"
#include "ark/native_backend.hpp"
#include "ark/units.hpp"
#include "ark/io.hpp"
#include "ark/agent.hpp"
#include "ark/direct_preview.hpp"
#include "ark/agent_activity.hpp"
#include "ark/bom.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/mesh_model.hpp"
#include "ark/notch.hpp"
#include "ark/source_tube.hpp"
#include "ark/sketch.hpp"
#include "ark/sheet_preview.hpp"
#include "ark/stock_preview.hpp"
#include "ark/suspension.hpp"
#include "ark/usage.hpp"
#include "ark/serialize.hpp"
#include "ark/assistant.hpp"
#include "ark/assistant_connectors.hpp"
#include "ark/onboarding.hpp"
#include "ark/view_camera.hpp"
#include "ark/agent_team.hpp"
#include "ark/pricing.hpp"
#include "ark/chatgpt_auth.hpp"
#include "ark/live_session.hpp"
#include "ark/updates.hpp"
#include <thread>
#include <future>
#include <functional>
#include <exception>
#include <optional>
#include <deque>
#include <filesystem>
#include <atomic>
#include <set>
#include <fstream>
#include <sstream>
#include <map>
#include <future>
#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>

using namespace ark;

#include "startup_about.inl"

// ---------------------------------------------------------------------------
// Minimal GL loader -------------------------------------------------------------
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint,GLsizei,const char**,const GLint*);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint,GLenum,GLint*);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint,GLsizei,GLsizei*,char*);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint,GLuint);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint,GLenum,GLint*);
typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint,GLsizei,GLsizei*,char*);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint,const char*);
typedef void (APIENTRY *PFNGLUNIFORM1IPROC)(GLint,GLint);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint,GLfloat);
typedef void (APIENTRY *PFNGLUNIFORM2FPROC)(GLint,GLfloat,GLfloat);
typedef void (APIENTRY *PFNGLUNIFORM3FPROC)(GLint,GLfloat,GLfloat,GLfloat);
typedef void (APIENTRY *PFNGLUNIFORM4FPROC)(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
typedef void (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint,GLsizei,GLboolean,const GLfloat*);
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei,GLuint*);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei,GLuint*);
typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei,const GLuint*);
typedef void (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei,const GLuint*);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum,GLuint);
typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum,GLsizeiptr,const void*,GLenum);
typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void (APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei,GLuint*);
typedef void (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum,GLuint);
typedef void (APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum,GLenum,GLenum,GLuint,GLint);
typedef GLenum (APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void (APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei,const GLuint*);
typedef void (APIENTRY *PFNGLTEXBUFFERPROC)(GLenum,GLenum,GLuint);


#define GLFUNCS(X) \
 X(PFNGLCREATEPROGRAMPROC,       glCreateProgram) \
 X(PFNGLCREATESHADERPROC,        glCreateShader) \
 X(PFNGLSHADERSOURCEPROC,        glShaderSource) \
 X(PFNGLCOMPILESHADERPROC,       glCompileShader) \
 X(PFNGLGETSHADERIVPROC,         glGetShaderiv) \
 X(PFNGLGETSHADERINFOLOGPROC,    glGetShaderInfoLog) \
 X(PFNGLATTACHSHADERPROC,        glAttachShader) \
 X(PFNGLLINKPROGRAMPROC,         glLinkProgram) \
 X(PFNGLGETPROGRAMIVPROC,        glGetProgramiv) \
 X(PFNGLGETPROGRAMINFOLOGPROC,   glGetProgramInfoLog) \
 X(PFNGLUSEPROGRAMPROC,          glUseProgram) \
 X(PFNGLGETUNIFORMLOCATIONPROC,  glGetUniformLocation) \
 X(PFNGLUNIFORM1IPROC,           glUniform1i) \
 X(PFNGLUNIFORM1FPROC,           glUniform1f) \
 X(PFNGLUNIFORM3FPROC,           glUniform3f) \
 X(PFNGLUNIFORM2FPROC,           glUniform2f) \
 X(PFNGLUNIFORM4FPROC,           glUniform4f) \
 X(PFNGLUNIFORMMATRIX4FVPROC,    glUniformMatrix4fv) \
 X(PFNGLGENVERTEXARRAYSPROC,     glGenVertexArrays) \
 X(PFNGLBINDVERTEXARRAYPROC,     glBindVertexArray) \
 X(PFNGLGENBUFFERSPROC,          glGenBuffers) \
 X(PFNGLDELETEBUFFERSPROC,       glDeleteBuffers) \
 X(PFNGLDELETEVERTEXARRAYSPROC,  glDeleteVertexArrays) \
 X(PFNGLBINDBUFFERPROC,          glBindBuffer) \
 X(PFNGLBUFFERDATAPROC,          glBufferData) \
 X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
 X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
 X(PFNGLACTIVETEXTUREPROC,       glActiveTexture) \
 X(PFNGLGENFRAMEBUFFERSPROC,     glGenFramebuffers) \
 X(PFNGLBINDFRAMEBUFFERPROC,     glBindFramebuffer) \
 X(PFNGLFRAMEBUFFERTEXTURE2DPROC,glFramebufferTexture2D) \
 X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
 X(PFNGLDELETEFRAMEBUFFERSPROC,  glDeleteFramebuffers) \
 X(PFNGLTEXBUFFERPROC,           glTexBuffer) \


#define DECL(T,N) static T N=nullptr;
GLFUNCS(DECL)
#undef DECL

typedef BOOL (WINAPI *WGLCHOOSEPFARB)(HDC,const int*,const FLOAT*,UINT,int*,UINT*);
typedef HGLRC (WINAPI *WGLCREATECTXATTRIBS)(HDC,HGLRC,const int*);
static WGLCHOOSEPFARB      wglChoosePixelFormatARB_=nullptr;
static WGLCREATECTXATTRIBS wglCreateContextAttribsARB_=nullptr;

static bool g_headless=false;
#include "shader_cache.inl"
static GLuint mkShader(GLenum type,const char* src){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char lg[4096]{}; glGetShaderInfoLog(s,4096,nullptr,lg);
        studio_shader_cache::diagnostic("shader error",lg);
        if(studio_shader_cache::deleteShader)studio_shader_cache::deleteShader(s);return 0; }
    return s;
}
static GLuint mkProg(const char* vs,const char* fs){
    studio_shader_cache::initialize();
    GLuint p=glCreateProgram();
    const auto identity=studio_shader_cache::key(vs,fs);
    if(studio_shader_cache::load(p,identity))return p;
    if(studio_shader_cache::available)studio_shader_cache::parameter(p,0x8257,GL_TRUE);
    const GLuint vertex=mkShader(GL_VERTEX_SHADER,vs),fragment=mkShader(GL_FRAGMENT_SHADER,fs);
    if(!vertex||!fragment){
        if(studio_shader_cache::deleteShader){if(vertex)studio_shader_cache::deleteShader(vertex);if(fragment)studio_shader_cache::deleteShader(fragment);}
        return p; // Unlinked program: the caller can reject the unavailable renderer.
    }
    if(vertex)glAttachShader(p,vertex);if(fragment)glAttachShader(p,fragment);
    glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(studio_shader_cache::deleteShader){if(vertex)studio_shader_cache::deleteShader(vertex);if(fragment)studio_shader_cache::deleteShader(fragment);}
    if(!ok){ char lg[4096]{}; glGetProgramInfoLog(p,4096,nullptr,lg);studio_shader_cache::diagnostic("link error",lg); }
    else studio_shader_cache::store(p,identity);
    return p;
}

// ---------------------------------------------------------------------------
struct GPUMesh { GLuint vao=0,vbo=0,ibo=0; int count=0; };
static std::uint64_t g_displayMeshSerial=1;
struct Zone { float x,y,w,h; int id; };

struct NotchViewCache { NotchPreview preview; std::vector<GPUMesh> gpu; std::vector<MeshBVH> bvhs; std::vector<AABB> bounds; std::vector<size_t> sourceIndices; };
static std::unordered_map<u64,NotchViewCache> g_notchViews;
struct SuspensionViewCache{SuspensionState state;SuspensionPreview preview;std::vector<GPUMesh> gpu;};
static std::unordered_map<u64,SuspensionViewCache> g_suspViews;
static int g_suspScrub=-1;static bool g_suspSpinPlaying=false;static double g_suspTick=0,g_suspClock=0;
struct SourceViewCache {
    SourceTubePreview preview;std::vector<GPUMesh> rigidGpu,flatHingeGpu,hingeGpu;
    std::vector<MeshBVH> rigidBvhs,flatHingeBvhs,hingeBvhs;
    std::vector<AABB> rigidBounds;std::vector<Mesh> flatHinges,hinges;std::vector<double> phases;
};
static std::unordered_map<u64,SourceViewCache> g_sourceViews;
static size_t g_sourceBuildCount=0,g_sourceHingeUpdates=0;static int g_sourceScroll=0,g_sourceOpening=-1;
static double g_foldProgress=0;static bool g_foldPlaying=false,g_foldScrubbing=false;static int g_selectedNotch=0,g_notchScroll=0;
static size_t g_notchBuildCount=0;
static AgentSession g_session;
static Document& g_doc=g_session.document;
static std::vector<u64> g_bodyIds,g_instanceIds;static std::vector<int> g_instanceCopies;
static std::vector<bool> g_hiddenBodies,g_beforeSpotlightHidden;
static bool g_spotlightActive=false;static int g_spotlightBody=-1;
static std::vector<std::string> g_instanceNames;
static std::vector<Mat4> g_instanceWorld;
static AssemblyDiagnostics g_assemblyDiagnostics;
static AssemblyMotionProgram g_motionProgram;static u64 g_motionRevision=std::numeric_limits<u64>::max(),g_motionDriver=0;
struct ComponentMove {bool armed=false,dragging=false,active=false;u64 revision=0,epoch=0,driver=0;int x=0,y=0;double delta=0,pointerCoordinate=0,pointerAngle=0;Vec3 radial,dragPlaneNormal,pointerWorld;AssemblyManipulationProgram program;AssemblyManipulationJoint joint;std::vector<Mat4> baseline;};
static ComponentMove g_componentMove;
struct ComponentDragIntent {bool pending=false;int x=0,y=0;u64 revision=0,epoch=0;};
static ComponentDragIntent g_componentDragIntent;
static void armComponentMove();static void cancelComponentMove(bool restore=true);static void applyComponentMove();
static bool g_motionActive=false,g_motionPlaying=false;static double g_motionTime=0,g_motionRpm=60;static std::string g_motionRpmExpression="cadence_rpm";
static arkjson::Value g_motionReport;
static int g_selectedMate=-1;
static int g_matePage=0;
static std::vector<size_t> g_instancePart;
static AABB g_sceneBounds;
static std::vector<MeshReport> g_reports;
static std::unordered_map<u64,arkjson::Value> g_tubeInfo;
static std::unordered_map<u64,bool> g_sheetFlatViews; // display choice only; never serialized
static std::unordered_map<u64,double> g_sheetProgress; // display choice only
static u64 g_sheetPlayingId=0;static bool g_sheetScrubbing=false;
static bool g_allSheetsFlat=false,g_allSheetsPlaying=false,g_flatShowChassis=false;
static double g_allSheetsStage=0,g_allSheetsTick=0;
static std::vector<Mat4> g_formedInstanceWorld;
static bool g_stockMode=false;static u64 g_stockFeatureId=0;static arkjson::Value g_stockReport;
struct DisplayCameraPose{double yaw=0,pitch=0;bool orthographic=false;double roll=0;};
static DisplayCameraPose g_beforeFlatCamera,g_beforeStockCamera;
static std::vector<MeshBVH> g_bvhs;
static size_t g_triangleCount=0;
static std::string g_status="Ready. Local C++ kernel.",g_path="Harmonic ring.ark";
static bool g_error=false,g_grid=true,g_dragged=false;
static u64 g_savedRevision=0;
static u64 g_savedUsageSerial=0;static u64 g_savedActivitySerial=0;
static int g_treeScroll=0,g_explorerCount=0,g_treeVisibleRows=1,g_treeMaxScroll=0,g_treeSelectedZone=-1;
static RECT g_treeViewport{},g_treeTrack{},g_treeThumb{};
static bool g_treeDragging=false,g_treePaging=false;static int g_treeDragOffset=0;
static void moveTreeThumb(int y){const int travel=(g_treeTrack.bottom-g_treeTrack.top)-(g_treeThumb.bottom-g_treeThumb.top);if(travel>0)g_treeScroll=std::clamp((int)std::lround((double)(y-g_treeDragOffset-g_treeTrack.top)*g_treeMaxScroll/travel),0,g_treeMaxScroll);}
static bool g_eatNextClick=false;
static bool g_explorerOpen[4]={true,true,true,true};
static u64 g_explorerFeature=0;
static u64 g_viewportContainer=0;
static bool g_assistantDockOpen=true;
struct PendingObjectTarget {u64 feature=0,node=0,epoch=0;int copy=0;std::string label,invalidated;};
static std::optional<PendingObjectTarget> g_pendingObjectTarget;
static u64 g_objectDocumentEpoch=0;
static void askAgentAboutBody(int body);
static bool g_inspectorOpen=false;
static int g_sidebarTab=0;
static bool g_showViewCube=true,g_compactNavigation=false,g_searchEditing=false;
static HWND g_commandSearch=nullptr;static WNDPROC g_commandSearchProc=nullptr;
static std::set<u64> g_collapsedFeatures;
static std::vector<u64> g_treeFoldIds;
static bool g_manufacturingOpen=true;
static std::set<u64> g_closedAssemblyGroups;
static int g_inspectorScroll=0;
static int g_layersScroll=0;
static bool g_parametersOpen=false;
struct ExplorerEntity{u64 feature=0;std::string key,id;int edit=0,remove=0;};
static std::vector<ExplorerEntity> g_explorerEntities;static std::optional<ExplorerEntity> g_selectedExplorerEntity;
static int viewX=336,viewY=218,viewW=1264,viewH=750;
static HWND g_window=nullptr,g_chatInput=nullptr;
static HWND g_toolSearch=nullptr;static WNDPROC g_toolSearchProc=nullptr;
static bool g_toolsOpen=false;static int g_toolCategory=-1,g_toolScroll=0,g_toolCategoryScroll=0;static std::string g_toolFilter;
static std::string g_libraryRoot,g_libraryText="screw",g_libraryLastId;static arkjson::Value g_libraryMatches=arkjson::Value::makeArr();
static bool g_lightBackground=false,g_satin=true,g_showSketches=false,g_showDatumPlanes=false,g_chatBusy=false;
static bool g_showEdges=false,g_cameraOrtho=false;
static bool g_pathTracing=false; // Optional GPU BVH path integrator, off by default.
static int g_occlusionMode=2; // 0 off, 1 contact, 2 ambient, 3 horizon
static float g_occlusionStrength=.72f,g_occlusionRadius=1.f;
static int g_navMode=0,g_pointerX=0,g_pointerY=0,g_cubeX=0,g_cubeY=0,g_cubeS=210;
static int g_toolModule=0; // Stable IDs: Sheet Metal, Model, Sketch, Inspect, Tools, Manage, Surfaces
static bool g_cubePress=false;
static std::string g_activeSketch,g_activePlane="XY";
static int g_selectedSketchConstraint=-1,g_sketchConstraintPage=0;
static arkjson::Value g_sketchDiagnosis;
static AssistantConfig g_assistantConfig;static arkjson::Value g_conversation=arkjson::Value::makeArr();
static bool g_teamMode=false,g_teamStarted=false,g_teamFocusedWorkflow=true;
static constexpr size_t teamCapacity=8;static unsigned g_teamRoleCount=3,g_teamPage=0;
static AgentTeam g_agentTeam;static AgentTeamSnapshot g_teamSnapshot;
static std::array<std::string,8> g_teamModels{},g_teamEfforts{};
static std::array<std::optional<AssistantConfig>,8> g_teamConnectors{};
static AssistantConfig teamRoleConfig(size_t index);
static std::array<std::string,8> g_teamReceiptIds{};
static std::array<std::optional<UsageReceipt>,8> g_teamReceipts{};
static std::string g_teamGoal,g_teamTarget;
static std::array<std::string,8> g_teamActivity{};
static std::shared_ptr<std::atomic_bool> g_teamCancel=std::make_shared<std::atomic_bool>(false);
static u64 g_teamEpoch=0,g_teamEventSequence=0;
static const char* g_teamRoleIds[]={"engineer","designer","reviewer","designer2","designer3","designer4","designer5","designer6"};
static const char* g_teamRoleLabels[]={"ENGINEER / ORCHESTRATOR","PART DESIGNER 1","PHYSICS REVIEWER","PART DESIGNER 2","PART DESIGNER 3","PART DESIGNER 4","PART DESIGNER 5","PART DESIGNER 6"};
static void resetTeamView(){g_teamSnapshot={};g_teamActivity={};g_teamGoal.clear();g_teamReceipts={};}
static std::vector<std::pair<std::string,std::string>> g_chatLog;static int g_chatScroll=0;
static std::string g_assistantStatus;static bool g_assistantResponseFinished=false,g_assistantResponseError=false;
static std::thread g_assistantWorker;static std::shared_ptr<std::atomic_bool> g_assistantCancel=std::make_shared<std::atomic_bool>(false);
static std::atomic_bool g_closing{false};static LiveSessionBridge g_liveBridge;static u64 g_documentEpoch=0;static bool g_ownAssistantTool=false;
static constexpr UINT WM_ARK_TOOL=WM_APP+20,WM_ARK_CHAT=WM_APP+21,WM_ARK_AUTH=WM_APP+22;
struct UiToolRequest {std::string tool,roleId;arkjson::Value args;LiveSessionBridge::Completion complete;std::stop_token stop;std::shared_ptr<std::atomic_bool> cancel;std::shared_ptr<std::atomic<u64>> revision;std::shared_ptr<AssistantSteering> steering;u64 epoch=0,steeringVersion=0;bool teamOwned=false;};
static bool assistantSteeringToolAllowed(const UiToolRequest& request);
static arkjson::Value assistantSteeringToolRejection();
struct UiChatEvent {std::string text;bool final=false,error=false;arkjson::Value history;u64 epoch=0;AssistantUsage usage{};bool hasUsage=false;AssistantPerformance performance{};bool hasPerformance=false;bool status=false,retry=false;};
enum class AuthJob{SignIn,SignOut,Models};
struct UiAuthEvent{AuthJob job=AuthJob::Models;ChatGptAuthStatus status;std::vector<ChatGptModel> models;std::string error;};
static ChatGptAuthStatus g_authStatus;
static std::vector<ChatGptModel> g_authModels;
static int g_pendingRoleSettings=-1;static std::string g_pendingRoleAccount;static bool g_roleCatalogReady=false;
static HWND g_roleModelWindow=nullptr;static constexpr UINT WM_ARK_ROLE_CATALOG=WM_APP+24;
static AuthJob g_activeAuthJob=AuthJob::Models;
static void clearPendingRoleSettings(){g_pendingRoleSettings=-1;g_pendingRoleAccount.clear();g_roleCatalogReady=false;}
static std::thread g_authWorker;
static std::shared_ptr<std::atomic_bool> g_authCancel=std::make_shared<std::atomic_bool>(false);
static bool g_authBusy=false;
static bool g_authCancelRequested=false;
static void postAuth(UiAuthEvent event);
static void interactiveAction(int id);
static AssistantUsage g_assistantUsageDocument{},g_assistantUsageRun{};static AssistantPerformance g_assistantPerformanceRun{};
static bool g_assistantUsageRunCounted=true;
static std::string g_assistantReceiptId,g_assistantReceiptModel;
static std::optional<UsageReceipt> g_currentUsageReceipt,g_pendingUsageReceipt;
static unsigned g_deferredUsageCount=0;
static std::deque<std::unique_ptr<UiToolRequest>> g_toolQueue;
static bool g_uiReady=false,g_busy=false,g_busyClosePending=false,g_busyTestMode=false;
static unsigned g_busyTestDelayMs=0,g_busyFrameCount=0,g_busyPumpCount=0;
static std::string g_busyLabel;
static std::string g_busyTestShot;
static std::chrono::steady_clock::time_point g_busyStarted;
static arkjson::Value callSessionWithProgress(const std::string& tool,const arkjson::Value& args,std::function<arkjson::Value()> operation={});
static GLuint sketchVao=0,sketchVbo=0,canvasVao=0,canvasVbo=0,g_canvasMissing=0;static bool g_sketchGpuDirty=true;static std::vector<float> g_sketchLines,g_sketchPoints,g_constraintHighlightLines,g_datumLines,g_sketchFill,g_datumFill;static AABB g_sketchBounds;
struct CanvasTexture { std::string path; unsigned long long stamp=0; bool missing=true; GLuint tex=0; };
static std::vector<CanvasTexture> g_canvasTextures;


static std::vector<Zone>    g_zones;
static std::vector<GPUMesh> g_meshes;
static std::vector<Mesh>    g_cpuMeshes;

static f64 yaw=0.85,pitch=0.46,dist=140,tYaw=yaw,tPitch=pitch,tDist=dist;
static double cameraRoll=0;
static Vec3 target{30,12,0},tTarget=target;
static int winW=1600,winH=1000;
struct StudioResources {bool systemCpuKnown=false,studioCpuKnown=false,ramKnown=false,gpuKnown=false,vramKnown=false,privateRam=false;double systemCpu=0,studioCpu=0,gpu=0;u64 systemRam=0,totalRam=0,studioRam=0,vram=0;};
static StudioResources g_resources;
static void updateCostDisplay(bool force=false);
static std::thread g_pricingWorker;static PricingCatalog g_pricing;static bool g_pricingReady=false;static CostEstimate g_costEstimate;static bool g_costUsageMissing=false;static UsageLedger g_usageDisplayLedger;
static constexpr UINT WM_ARK_PRICE=WM_APP+26,WM_ARK_TEAM=WM_APP+27;
static std::array<AssistantUsage,8> g_teamLiveUsage{};static std::string g_activeToolRole,g_activeToolName,g_liveStepLabel;static u64 g_liveCompletedSteps=0;
struct DraftSketchView {arkjson::Value commands;std::vector<float> lines;std::string role,step;bool committed=false;std::vector<float> fill,points;};static std::map<std::pair<std::string,std::string>,DraftSketchView> g_draftViews;static std::map<std::string,DraftSketchView> g_committedRoleSketches;
static std::vector<Parameter> g_draftParameters;static std::vector<DatumPlane> g_draftPlaneDefinitions;
static ExprEnv g_draftEnv;static std::map<std::string,Mat4> g_draftPlanes;static std::map<std::string,Sketch> g_draftSketches;static GLuint g_draftVao=0,g_draftVbo=0;
static bool patchDraftEntity(Sketch& sketch,const arkjson::Value& command){
    using V=arkjson::Value;auto* name=command.find("entity");if(!name||name->t!=V::Str)return false;auto found=std::find_if(sketch.ents.begin(),sketch.ents.end(),[&](const auto& entity){return entity.name==name->str;});if(found==sketch.ents.end()||found->kind==SketchEnt::Project||found->kind==SketchEnt::Offset)return false;
    auto& entity=*found;int coordinate=-1;const V* value=nullptr;unsigned values=0;for(const char* key:{"x","y","r","a0","a1","expr"})if(command.find(key))++values;if(values!=1)return false;
    auto index=[](const V* item){return item&&item->t==V::Num&&item->num>=0&&item->num<=1024&&std::floor(item->num)==item->num?(int)item->num:-1;};
    if(command.find("segment")||command.find("role")){int segment=index(command.find("segment"));auto* role=command.find("role");if(entity.kind!=SketchEnt::Wire||segment<0||segment>=(int)entity.segments.size()||!role||role->t!=V::Str)return false;int start=2;for(int i=0;i<segment;++i){auto kind=entity.segments[(size_t)i].kind;start+=kind==SketchEnt::WireSeg::Line?2:kind==SketchEnt::WireSeg::Arc?3:6;}auto kind=entity.segments[(size_t)segment].kind;
        if(role->str=="radius"&&kind==SketchEnt::WireSeg::Arc){coordinate=start+2;value=command.find("expr");if(!value)value=command.find("x");}
        else if((role->str=="c1"||role->str=="c2")&&kind==SketchEnt::WireSeg::Cubic){bool y=command.find("y")!=nullptr;coordinate=start+(role->str=="c1"?0:2)+(y?1:0);value=command.find(y?"y":"x");}else return false;
    }else if(command.find("r")){if(entity.kind!=SketchEnt::Circle&&entity.kind!=SketchEnt::Arc)return false;coordinate=2;value=command.find("r");}
    else if(command.find("a0")||command.find("a1")){if(entity.kind!=SketchEnt::Arc)return false;coordinate=command.find("a0")?3:4;value=command.find(coordinate==3?"a0":"a1");}
    else{int anchor=index(command.find("anchor"));if(anchor<0||anchor>=sketchVertexCount(entity))return false;bool y=command.find("y")!=nullptr;coordinate=(entity.kind==SketchEnt::Wire?sketchVertexDof(entity,anchor):anchor*2)+(y?1:0);value=command.find(y?"y":"x");}
    if(!value||(value->t!=V::Str&&value->t!=V::Num)||coordinate<0||coordinate>=(int)entity.dof.size())return false;entity.expressions.resize(entity.dof.size());entity.expressions[(size_t)coordinate]=value->t==V::Str?value->str:value->dump();return true;
}
static void captureDraftContext(){g_draftParameters=g_doc.params;g_draftPlaneDefinitions=g_doc.planes;g_draftEnv=g_doc.buildEnv();g_draftPlanes.clear();g_draftSketches.clear();for(const char* name:{"XY","XZ","YZ"})g_draftPlanes[name]=resolvePlane(g_doc,name);for(const auto& sketch:g_doc.sketches){g_draftSketches[sketch.name]=sketch.sk;try{g_draftPlanes[sketch.sk.plane]=resolvePlane(g_doc,sketch.sk.plane);}catch(...){}}for(const auto& plane:g_doc.planes)try{g_draftPlanes[plane.name]=resolvePlane(g_doc,plane.name);}catch(...){} }
static void buildDraftLines(DraftSketchView& view){
    using V=arkjson::Value;view.lines.clear();view.fill.clear();view.points.clear();if(view.commands.t!=V::Arr||view.commands.arr.size()>8||view.commands.dump().size()>16*1024)return;auto sketches=g_draftSketches;auto planes=g_draftPlanes;auto draftEnv=g_draftEnv;
    Document parameters;parameters.params=g_draftParameters;parameters.planes=g_draftPlaneDefinitions;bool parameterEdits=false;
    for(const auto& command:view.commands.arr)if(command.find("op")&&command.find("op")->asStr()=="param.set"){const auto* name=command.find("name"),*expr=command.find("expr");if(name&&name->t==V::Str&&expr&&(expr->t==V::Str||expr->t==V::Num)){parameters.setParam(name->str,expr->t==V::Str?expr->str:expr->dump());parameterEdits=true;}}
    parameters.refreshParams();if(!parameters.parameterError.empty())return;draftEnv=parameters.buildEnv();
    if(parameterEdits)for(const auto& plane:parameters.planes)try{if(plane.support.t==V::Obj)planes.erase(plane.name);else planes[plane.name]=resolvePlane(parameters,plane.name);}catch(...){planes.erase(plane.name);}

    for(const auto& command:view.commands.arr){auto* operation=command.find("op"),*name=command.find("name");if(!operation||operation->t!=V::Str||!name||name->t!=V::Str)continue;V source;
        if(operation->str=="param.set")continue;
        if(operation->str=="plane.set"){
            if(command.find("support")||name->str=="XY"||name->str=="XZ"||name->str=="YZ")continue;
            const std::string parent=command.find("parent")?command.find("parent")->asStr():"XY";auto base=planes.find(parent);if(base==planes.end())continue;
            try{Document local;for(const auto& variable:draftEnv.vars)local.setParam(variable.first,V::makeNum(variable.second).dump());DatumPlane datum;datum.name=name->str;datum.parent="XY";
                if(const auto* kind=command.find("kind")){if(kind->asStr()!="plane"&&kind->asStr()!="frame")continue;datum.kind=kind->asStr();}
                if(const auto* values=command.find("p")){if(values->t!=V::Obj)continue;for(const auto& value:values->obj){if(value.second.t!=V::Str&&value.second.t!=V::Num)throw std::runtime_error("Invalid draft plane expression");datum.p[value.first]=value.second.t==V::Str?value.second.str:value.second.dump();}}
                local.planes.push_back(datum);planes[name->str]=base->second*resolvePlane(local,name->str);
            }catch(...){}continue;
        }

        if(operation->str=="sketch.set"){if(auto* sketch=command.find("sketch"))source=*sketch;else continue;}
        else if(operation->str=="sketch.entity.set"){auto existing=sketches.find(name->str);auto* entity=command.find("entity");if(existing==sketches.end()||!entity)continue;source=V::makeObj();source.set("plane",V::makeStr(existing->second.plane));V entities=V::makeArr();entities.arr.push_back(*entity);source.set("entities",entities);}
        else if(operation->str=="sketch.patch"){auto existing=sketches.find(name->str);if(existing==sketches.end())continue;auto changed=existing->second;if(!patchDraftEntity(changed,command))continue;arkjson::parse(sketchToJson(changed),source);}else continue;
        Sketch sketch;std::string error;if(!sketchFromJson(source.dump(),sketch,&error)||sketch.ents.size()>64)continue;NamedSketch evaluated;evaluated.sk=sketch;evaluateSketch(evaluated,draftEnv);if(!evaluated.error.empty())continue;sketch=evaluated.sk;if(operation->str=="sketch.entity.set"){auto& owner=sketches[name->str];for(const auto& entity:sketch.ents){auto existing=std::find_if(owner.ents.begin(),owner.ents.end(),[&](const auto& item){return item.name==entity.name;});if(existing==owner.ents.end())owner.ents.push_back(entity);else *existing=entity;}}else sketches[name->str]=sketch;auto plane=planes.find(sketch.plane);if(plane==planes.end())continue;
        for(auto& entity:sketch.ents){if(entity.kind==SketchEnt::Project||entity.kind==SketchEnt::Offset)continue;bool valid=true;for(size_t i=0;i<entity.dof.size();++i){if(i<entity.expressions.size()&&!entity.expressions[i].empty()){bool ok=false;entity.dof[i]=evalExpr(entity.expressions[i],draftEnv,&ok);valid=valid&&ok;}valid=valid&&std::isfinite(entity.dof[i])&&std::abs(entity.dof[i])<=1e7;}if(!valid)continue;
            try{auto paths=sampleSketchEntityPaths(entity,std::max(.05,sketch.tolerance),&sketch);for(const auto& points:paths){if(points.size()>8192)continue;
                auto put=[&](std::vector<float>& vertices,Vec2 point){auto world=plane->second.transformPoint({point.x,point.y,0});vertices.insert(vertices.end(),{(float)world.x,(float)world.y,(float)world.z});};
                if(!view.committed&&!points.empty()){put(view.points,points.front());if(entity.kind!=SketchEnt::Circle)put(view.points,points.back());if(entity.kind==SketchEnt::Circle&&points.size()<1024&&entity.dof.size()>=3)for(size_t i=1;i<points.size();++i){put(view.fill,{entity.dof[0],entity.dof[1]});put(view.fill,points[i-1]);put(view.fill,points[i]);}}
                for(size_t i=1;i<points.size();++i){auto a=plane->second.transformPoint({points[i-1].x,points[i-1].y,0}),b=plane->second.transformPoint({points[i].x,points[i].y,0});if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z))continue;int segments=view.committed?1:3;for(int dash=0;dash<segments;++dash){Vec3 from=a+(b-a)*(dash/(double)segments),to=a+(b-a)*((dash+(view.committed?1.:.65))/segments);for(auto point:{from,to})view.lines.insert(view.lines.end(),{(float)point.x,(float)point.y,(float)point.z});}if(view.lines.size()>49152||view.fill.size()>49152){view.lines.clear();view.fill.clear();view.points.clear();return;}}}}catch(...){}
        }
    }
}
static void rebuildRoleSketches(){using V=arkjson::Value;if(g_committedRoleSketches.empty())return;captureDraftContext();for(auto it=g_committedRoleSketches.begin();it!=g_committedRoleSketches.end();){auto* named=g_doc.findSketch(it->first);if(!named){it=g_committedRoleSketches.erase(it);continue;}V sketch;arkjson::parse(sketchToJson(named->sk),sketch);V command=V::makeObj();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(named->name));command.set("sketch",sketch);it->second.commands=V::makeArr();it->second.commands.arr.push_back(command);it->second.committed=true;buildDraftLines(it->second);++it;}}
static u64 g_teamRunSerial=0;
struct UiTeamEvent {AgentTeamEvent event;u64 epoch=0,runSerial=0;};
static void receiveTeamEvent(const AgentTeamEvent& event){
    size_t role=teamCapacity;for(size_t i=0;i<teamCapacity;++i)if(event.roleId==g_teamRoleIds[i])role=i;if(role<teamCapacity){if(event.kind==AgentTeamEvent::Usage)g_teamLiveUsage[role]=event.usage;if(event.kind!=AgentTeamEvent::Usage)g_teamActivity[role]=event.tool.empty()?event.text:event.tool+(event.toolError?" / "+event.text:"");}
    if(event.kind==AgentTeamEvent::ConstructionQueued||event.kind==AgentTeamEvent::ConstructionStart){auto key=std::make_pair(event.roleId,event.stepId);auto& draft=g_draftViews[key];draft={event.draftSketchCommands,{},event.roleId,event.stepId};buildDraftLines(draft);while(g_draftViews.size()>8)g_draftViews.erase(g_draftViews.begin());g_liveStepLabel=event.roleId+" / "+(event.kind==AgentTeamEvent::ConstructionQueued?"queued draft / ":"applying draft / ")+event.stepId;}
    if(event.kind==AgentTeamEvent::ConstructionEnd){g_draftViews.erase({event.roleId,event.stepId});if(!event.toolError&&event.hasExpectedRevision&&event.hasDocumentRevision&&event.documentRevision>event.expectedRevision){++g_liveCompletedSteps;g_liveStepLabel=event.roleId+" / committed r"+std::to_string(event.documentRevision);}else g_liveStepLabel=event.roleId+" / step rejected or unchanged";}
    updateCostDisplay();
}
static u64 fileTicks(FILETIME value){return ((u64)value.dwHighDateTime<<32)|value.dwLowDateTime;}
class StudioGpuCounters {
    HMODULE module_=nullptr;PDH_HQUERY query_=nullptr;PDH_HCOUNTER gpu_=nullptr,memory_=nullptr;
    decltype(&PdhOpenQueryW) open_=nullptr;decltype(&PdhAddEnglishCounterW) add_=nullptr;decltype(&PdhCollectQueryData) collect_=nullptr;decltype(&PdhGetFormattedCounterArrayW) values_=nullptr;decltype(&PdhCloseQuery) close_=nullptr;
public:
    StudioGpuCounters(){module_=LoadLibraryExW(L"pdh.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module_)return;open_=(decltype(open_))GetProcAddress(module_,"PdhOpenQueryW");add_=(decltype(add_))GetProcAddress(module_,"PdhAddEnglishCounterW");collect_=(decltype(collect_))GetProcAddress(module_,"PdhCollectQueryData");values_=(decltype(values_))GetProcAddress(module_,"PdhGetFormattedCounterArrayW");close_=(decltype(close_))GetProcAddress(module_,"PdhCloseQuery");if(!open_||!add_||!collect_||!values_||!close_||open_(nullptr,0,&query_)!=ERROR_SUCCESS)return;add_(query_,L"\\GPU Engine(*)\\Utilization Percentage",0,&gpu_);add_(query_,L"\\GPU Process Memory(*)\\Dedicated Usage",0,&memory_);}
    ~StudioGpuCounters(){if(query_&&close_)close_(query_);if(module_)FreeLibrary(module_);}
    void sample(){g_resources.gpuKnown=g_resources.vramKnown=false;if(!query_||!collect_||collect_(query_)!=ERROR_SUCCESS)return;std::wstring pid=L"pid_"+std::to_wstring(GetCurrentProcessId())+L"_";
        auto read=[&](PDH_HCOUNTER counter,DWORD format,bool memory){if(!counter)return;DWORD bytes=0,count=0;if(values_(counter,format,&bytes,&count,nullptr)!=PDH_MORE_DATA||!bytes||bytes>8*1024*1024)return;std::vector<unsigned char> buffer(bytes);auto* items=(PDH_FMT_COUNTERVALUE_ITEM_W*)buffer.data();if(values_(counter,format,&bytes,&count,items)!=ERROR_SUCCESS)return;for(DWORD i=0;i<count;++i){if(!items[i].szName||std::wstring(items[i].szName).find(pid)==std::wstring::npos)continue;auto& value=items[i].FmtValue;if(value.CStatus!=PDH_CSTATUS_VALID_DATA&&value.CStatus!=PDH_CSTATUS_NEW_DATA)continue;if(memory&&value.largeValue>=0){g_resources.vramKnown=true;g_resources.vram+=(u64)value.largeValue;}else if(!memory&&std::isfinite(value.doubleValue)&&value.doubleValue>=0){g_resources.gpuKnown=true;g_resources.gpu=std::max(g_resources.gpu,std::clamp(value.doubleValue,0.,100.));}}};
        g_resources.gpu=0;g_resources.vram=0;read(gpu_,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,false);read(memory_,PDH_FMT_LARGE,true);
    }
};
static void sampleStudioResources(){
    static auto next=std::chrono::steady_clock::time_point{},previous=next;static u64 oldSystem=0,oldIdle=0,oldProcess=0;auto now=std::chrono::steady_clock::now();if(now<next)return;next=now+std::chrono::seconds(1);
    FILETIME idle{},kernel{},user{},created{},exited{},processKernel{},processUser{};bool system=GetSystemTimes(&idle,&kernel,&user)!=FALSE,process=GetProcessTimes(GetCurrentProcess(),&created,&exited,&processKernel,&processUser)!=FALSE;
    u64 systemTime=fileTicks(kernel)+fileTicks(user),idleTime=fileTicks(idle),processTime=fileTicks(processKernel)+fileTicks(processUser);double seconds=std::chrono::duration<double>(now-previous).count();
    if(previous.time_since_epoch().count()&&seconds>0){if(system&&systemTime>oldSystem){g_resources.systemCpuKnown=true;g_resources.systemCpu=std::clamp(100.*(1.-(double)(idleTime-oldIdle)/(systemTime-oldSystem)),0.,100.);}if(process&&processTime>=oldProcess){auto cores=std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));g_resources.studioCpuKnown=true;g_resources.studioCpu=std::clamp((processTime-oldProcess)/1e7/seconds/cores*100.,0.,100.);}}
    previous=now;oldSystem=systemTime;oldIdle=idleTime;oldProcess=processTime;MEMORYSTATUSEX ram{};ram.dwLength=sizeof ram;if(GlobalMemoryStatusEx(&ram)){g_resources.totalRam=ram.ullTotalPhys;g_resources.systemRam=ram.ullTotalPhys-ram.ullAvailPhys;}
    using MemoryQuery=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);static auto memory=(MemoryQuery)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo");
    struct ExtendedMemory {PROCESS_MEMORY_COUNTERS_EX base;SIZE_T PrivateWorkingSetSize=0,SharedCommitUsage=0;} info{};info.base.cb=sizeof info;
    if(memory&&memory(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&info,sizeof info)&&info.PrivateWorkingSetSize){g_resources.ramKnown=g_resources.privateRam=true;g_resources.studioRam=info.PrivateWorkingSetSize;}
    else{PROCESS_MEMORY_COUNTERS_EX fallback{};fallback.cb=sizeof fallback;if(memory&&memory(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&fallback,sizeof fallback)){g_resources.ramKnown=true;g_resources.privateRam=false;g_resources.studioRam=fallback.WorkingSetSize;}}
    static StudioGpuCounters counters;counters.sample();
}
static std::string resourceSummary(){auto number=[](double value){char text[32];std::snprintf(text,sizeof text,"%.0f",value);return std::string(text);};return "CPU sys "+(g_resources.systemCpuKnown?number(g_resources.systemCpu)+"%":"N/A")+" / ARK "+(g_resources.studioCpuKnown?number(g_resources.studioCpu)+"%":"N/A")+"  RAM sys "+number(g_resources.systemRam/1073741824.)+"/"+number(g_resources.totalRam/1073741824.)+"G / ARK "+(g_resources.ramKnown?number(g_resources.studioRam/1048576.)+"M":"N/A")+"  GPU "+(g_resources.gpuKnown?number(g_resources.gpu)+"%":"N/A")+" / VRAM "+(g_resources.vramKnown?number(g_resources.vram/1048576.)+"M":"N/A");}
static constexpr int teamCardHeight=176,teamCardStride=184,teamCardTop=79;
static bool g_verifyTeamCardBounds=false,g_teamCardContentFits=true;static std::string g_teamCardBoundsFailure;
static int teamCardColumns(){return std::clamp((viewW-44)/320,2,4);}
static int teamCardRows(){return winH>=1100&&g_teamRoleCount>(unsigned)teamCardColumns()?2:1;}
static void layoutAssistantImageDimensions();
static void clearPendingAssistantImage();
static int assistantImageComposerHeight();
static int assistantDockHeight(){const int images=assistantImageComposerHeight();if(!g_teamMode)return 216+images;return std::max(0,std::min(432+(teamCardRows()-1)*teamCardStride+images,winH-viewY-32-160));}
static int teamChatRows(){return assistantDockHeight()-assistantImageComposerHeight()>=432+(teamCardRows()-1)*teamCardStride?2:0;}
static RECT toolGalleryBounds(){return {viewX+12,viewY+10,viewX+12+std::max(300,std::min(1000,viewW-24)),viewY+10+std::max(300,std::min(620,winH-viewY-56))};}
static void layout(){viewX=std::clamp((int)(winW*.21),280,352);viewW=std::max(winW-viewX,1);viewH=std::max(winH-viewY-32-(g_assistantDockOpen?assistantDockHeight():0),1);g_inspectorOpen=g_sidebarTab==2;
    if(g_chatInput)MoveWindow(g_chatInput,viewX+30,winH-112,std::max(viewW-260,1),44,TRUE);
    if(g_chatInput)ShowWindow(g_chatInput,g_assistantDockOpen&&!g_toolsOpen?SW_SHOW:SW_HIDE);
    if(g_toolSearch){auto bounds=toolGalleryBounds();MoveWindow(g_toolSearch,bounds.left+213,bounds.top+45,std::max((int)(bounds.right-bounds.left)-310,1),30,TRUE);ShowWindow(g_toolSearch,g_toolsOpen?SW_SHOW:SW_HIDE);}
    if(g_commandSearch){MoveWindow(g_commandSearch,winW-438,12,200,31,TRUE);ShowWindow(g_commandSearch,g_searchEditing?SW_SHOW:SW_HIDE);}
    layoutAssistantImageDimensions();
}
static int selBody=-1,hoverBody=-1;
static bool wireframe=false, g_pressWasButton=false;
static double g_fps=0;
static int g_hoverZone=-1;static std::array<Zone,3> g_frameRibbonBounds{};static std::string g_lastRibbonHint;static std::optional<POINT> g_testPointer;
struct CurveSample { std::string sketch,name; Vec3 a,b; };
struct SelectionItem { u64 feature=0; std::string kind,name,owner;u64 instance=0;int copy=0;std::vector<u32> meshTriangles,meshVertices;std::string topology; };
struct PartPick { std::vector<std::pair<int,int>> faces; std::vector<std::vector<Vec3>> edges; std::vector<std::string> faceNames,edgeNames; std::vector<bool> edgeVisible;std::map<std::string,int> faceReferences,edgeReferences;std::vector<int> faceCanonical,edgeCanonical; };
static bool faceReferenceMatches(const PartPick& pick,size_t face,const std::string& name){auto found=pick.faceReferences.find(name);return found!=pick.faceReferences.end()?found->second==(int)face||(face<pick.faceCanonical.size()&&found->second==pick.faceCanonical[face]):face<pick.faceNames.size()&&pick.faceNames[face]==name;}
static bool edgeReferenceMatches(const PartPick& pick,size_t edge,const std::string& name){auto found=pick.edgeReferences.find(name);return found!=pick.edgeReferences.end()?found->second==(int)edge||(edge<pick.edgeCanonical.size()&&found->second==pick.edgeCanonical[edge]):edge<pick.edgeNames.size()&&pick.edgeNames[edge]==name;}
static std::vector<CurveSample> g_curveSamples;
static std::vector<SelectionItem> g_picked;
struct ProjectionPreview {bool active=false,spatial=false;u64 revision=0,epoch=0;std::string target,plane,source;arkjson::Value commands=arkjson::Value::makeArr();std::vector<float> lines;AABB bounds;};
static ProjectionPreview g_projectionPreview;static GLuint g_projectionVao=0,g_projectionVbo=0;static bool g_projectionGpuDirty=true;
static void clearProjectionPreview(){g_projectionPreview={};g_projectionGpuDirty=true;}

static arkjson::Value g_physicalReport;static std::string g_physicalError;
static arkjson::Value g_physicalSettings;
static bool decodeCanvasFile(const std::wstring&,std::vector<unsigned char>&,unsigned&,unsigned&);
#include "action_icons.inl"
static int atlasIcon(int action){switch(action){
    case 650:return 53;case 652:case 687:return 52;case 651:return 26;case 653:case 654:return 33;case 655:return 51;case 656:return 40;case 657:case 733:return 14;case 658:return 42;case 659:return 51;case 710:return 48;case 750:return 55;case 752:return 4;case 754:return 22;case 755:return 57;case 730:case 731:return 49;case 732:return 50;
    case 570:case 584:return 33; // Existing edit-curve pencil.
    case 571:return 0;case 572:return 16;case 573:return 1;case 574:case 575:return 32;
    case 590:case 597:case 860:case 861:return 8;case 585:case 586:case 588:case 592:case 593:case 595:case 576:return 24;case 594:return 45;case 596:return 14;case 591:return 24;case 587:return 45;case 577:return 45;case 578:return 14;
    case 580:case 582:return 11;case 581:return 12;
    case 543:return 0;case 544:case 545:case 546:case 547:case 548:return 32;case 536:case 537:return 30;case 538:return 16;
    case 127:return 35;case 122:case 123:return 6;case 125:case 126:return 3;case 396:return 36;case 399:return 37;
    case 494:case 495:return 6;case 496:return 7;case 497:return 0;case 498:return 22;case 491:return 42;case 492:return 0;case 493:return 14;case 490:return 40;case 458:return 13;case 451:return 39;case 452:case 453:return 40;case 468:return 38;
    case 60:case 113:return 0;case 61:return 1;case 62:return 32;case 65:case 101:return 2;case 66:case 103:case 124:return 3;
    case 76:return 4;case 150:case 151:return 5;case 120:return 35;case 121:return 6;case 394:case 395:return 36;case 397:case 398:return 37;case 26:return 7;case 27:return 8;case 94:return 9;case 11:return 54;
    case 187:return 10;case 188:return 11;case 190:return 8;case 16:return 12;case 17:return 13;case 81:return 46;case 18:return 14;case 19:return 47;
    case 610:return 33;case 611:return 14;case 700:return 24;case 70:return 33;case 71:return 34;case 170:return 41;case 171:return 42;case 172:return 43;case 173:return 44;case 67:return 45;
    case 63:return 16;case 78:return 17;case 77:return 18;case 79:return 19;case 211:return 20;case 212:return 21;case 210:return 22;case 215:case 384:return 23;
    case 73:case 89:return 24;case 108:return 25;case 105:return 26;case 104:return 27;case 321:return 28;case 320:return 29;case 322:return 30;case 326:return 31;case 154:return 56;default:return -1;
}}
static std::filesystem::path toolAtlasPath(int sheet=0){static const char* paths[]={"assets/ui/ark-tool-atlas-v1.png","assets/ui/ark-domain-atlas-v1.png","assets/ui/ark-detail-atlas-v1.png","assets/ui/ark-authoring-atlas-v1.png"};if(sheet<0||sheet>=4)return {};wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);auto directory=std::filesystem::path(path).parent_path();for(int i=0;i<6&&!directory.empty();++i,directory=directory.parent_path()){auto atlas=directory/paths[sheet];if(std::filesystem::is_regular_file(atlas))return atlas;}return {};}
static void decorateMenu(HMENU menu){
    static std::map<int,HBITMAP> bitmaps;struct Atlas{std::vector<unsigned char> pixels;unsigned w=0,h=0;};static Atlas atlases[4];
    for(int i=0;i<GetMenuItemCount(menu);++i){MENUITEMINFOW item{sizeof item};item.fMask=MIIM_ID|MIIM_SUBMENU;GetMenuItemInfoW(menu,(UINT)i,TRUE,&item);if(item.hSubMenu)decorateMenu(item.hSubMenu);
        if(auto bitmap=actionIconMenuBitmap((int)item.wID)){MENUITEMINFOW image{sizeof image};image.fMask=MIIM_BITMAP;image.hbmpItem=bitmap;SetMenuItemInfoW(menu,(UINT)i,TRUE,&image);continue;}
        int icon=atlasIcon((int)item.wID);if(icon<0)continue;auto& atlas=atlases[icon/16];if(atlas.pixels.empty()){auto path=toolAtlasPath(icon/16);if(path.empty()||!decodeCanvasFile(path.wstring(),atlas.pixels,atlas.w,atlas.h))continue;}const auto& pixels=atlas.pixels;unsigned w=atlas.w,h=atlas.h;int cell=icon%16;
        HBITMAP bitmap=bitmaps[icon];if(!bitmap){constexpr int size=22;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* raw=nullptr;bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0);if(!bitmap)continue;auto* output=(unsigned char*)raw;
            for(int y=0;y<size;++y)for(int x=0;x<size;++x){unsigned sx=(unsigned)((cell%4+(x+.5)/size)*w/4),sy=h-1-(unsigned)((cell/4+(y+.5)/size)*h/4);const auto* source=pixels.data()+((size_t)sy*w+sx)*4;auto* dest=output+(y*size+x)*4;dest[0]=(unsigned char)(source[2]*source[3]/255);dest[1]=(unsigned char)(source[1]*source[3]/255);dest[2]=(unsigned char)(source[0]*source[3]/255);dest[3]=source[3];}bitmaps[icon]=bitmap;
        }MENUITEMINFOW image{sizeof image};image.fMask=MIIM_BITMAP;image.hbmpItem=bitmap;SetMenuItemInfoW(menu,(UINT)i,TRUE,&image);
    }
}
static u64 g_physicalRevision=0,g_physicalSelection=0;static bool g_physicalVisible=false,g_physicalAssembly=false;
static std::vector<PartPick> g_partPicks;
static std::vector<float> g_selectionLines,g_selectionEdgeLines,g_selectionFaceFill;
static int g_pickKind=0;
static DisplayUnits g_displayUnits;
static bool measurementActive();static void beginMeasurement(int mode);static void endMeasurement();static void measurementCoordinates();
static void refreshSelectionLines();
static std::string pickedSketches();
static PartPick partPickFrom(const Body& body){
    PartPick pick;using V=arkjson::Value;const V* mesh=body.nativeData.find("mesh");
    const V* spans=mesh?mesh->find("face_spans"):nullptr;const V* edges=mesh?mesh->find("edges"):nullptr;
    if(spans&&spans->t==V::Arr)for(const auto& span:spans->arr){if(span.t!=V::Arr||span.arr.size()!=2||span.arr[0].t!=V::Num||span.arr[1].t!=V::Num)continue;pick.faces.push_back({(int)span.arr[0].num,(int)span.arr[1].num});}
    pick.faceNames.assign(pick.faces.size(),"");
    if(edges&&edges->t==V::Arr)for(const auto& edge:edges->arr){std::vector<Vec3> points;if(edge.t==V::Arr&&edge.arr.size()%3==0)for(size_t i=0;i+2<edge.arr.size();i+=3)if(edge.arr[i].t==V::Num&&edge.arr[i+1].t==V::Num&&edge.arr[i+2].t==V::Num)points.push_back({edge.arr[i].num,edge.arr[i+1].num,edge.arr[i+2].num});pick.edges.push_back(std::move(points));}
    pick.edgeNames.assign(pick.edges.size(),"");
    pick.edgeVisible.assign(pick.edges.size(),true);
    if(const V* visible=mesh?mesh->find("edge_visible"):nullptr;visible&&visible->t==V::Arr&&visible->arr.size()==pick.edges.size())for(size_t i=0;i<visible->arr.size();++i)if(visible->arr[i].t==V::Bool)pick.edgeVisible[i]=visible->arr[i].b;
    auto canonical=[&](const char* key,size_t count){std::vector<int> slots(count);const auto* rows=mesh?mesh->find(key):nullptr;for(size_t index=0;index<count;++index){slots[index]=(int)index;if(rows&&rows->t==V::Arr&&index<rows->arr.size()&&rows->arr[index].t==V::Num&&std::isfinite(rows->arr[index].num)&&rows->arr[index].num>=0&&rows->arr[index].num<(double)count&&std::floor(rows->arr[index].num)==rows->arr[index].num){slots[index]=(int)rows->arr[index].num;}}return slots;};pick.faceCanonical=canonical("face_canonical",pick.faces.size());pick.edgeCanonical=canonical("edge_canonical",pick.edges.size());
    const V* topology=body.nativeData.find("topology");const V* refs=topology?topology->find("references"):nullptr;
    if(refs&&refs->t==V::Arr)for(const auto& row:refs->arr){auto* name=row.find("name"),*kind=row.find("kind"),*status=row.find("status"),*indices=row.find("indices");
        if(!name||!kind||!status||!indices||(status->asStr()!="bound"&&status->asStr()!="merged")||indices->t!=V::Arr||indices->arr.size()!=1||indices->arr[0].t!=V::Num)continue;int index=(int)indices->arr[0].num;
        auto preferred=[&](std::string& current){const auto next=name->asStr();if(current.empty()||next.rfind("@ark.",0)!=0)current=next;};
        if(kind->asStr()=="face"&&index>=0&&index<(int)pick.faceNames.size()){index=pick.faceCanonical[(size_t)index];preferred(pick.faceNames[(size_t)index]);pick.faceReferences[name->asStr()]=index;}
        if(kind->asStr()=="edge"&&index>=0&&index<(int)pick.edgeNames.size()){index=pick.edgeCanonical[(size_t)index];preferred(pick.edgeNames[(size_t)index]);pick.edgeReferences[name->asStr()]=index;}
    }
    for(size_t index=0;index<pick.faceNames.size();++index)pick.faceNames[index]=pick.faceNames[(size_t)pick.faceCanonical[index]];for(size_t index=0;index<pick.edgeNames.size();++index)pick.edgeNames[index]=pick.edgeNames[(size_t)pick.edgeCanonical[index]];
    return pick;
}

// ---------------------------------------------------------------------------
// Font atlas -----------------------------------------------------------------------
struct FontAtlas { GLuint tex=0; float x0[256]={},x1[256]={},y0[256]={},y1[256]={},adv[256]={};
                   int w=1024,h=256,lineH=18; };
static FontAtlas fa;

static void buildFontAtlas(){
    HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=fa.w; bi.bmiHeader.biHeight=-fa.h;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    void* bits=nullptr;
    HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    HGDIOBJ ob=SelectObject(dc,bmp);
    HFONT fnt=CreateFontW(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HGDIOBJ of=SelectObject(dc,fnt);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
    PatBlt(dc,0,0,fa.w,fa.h,BLACKNESS);
    TEXTMETRICW tm; GetTextMetricsW(dc,&tm);
    fa.lineH=tm.tmHeight+2;
    int x=1,y=4;
    for(int ch=32;ch<256;++ch){
        if(ch>=127&&ch<160)continue;
        wchar_t s=(wchar_t)ch;SIZE sz;GetTextExtentPoint32W(dc,&s,1,&sz);
        if(x+sz.cx+2>=fa.w){x=1;y+=fa.lineH+5;}
        if(y+fa.lineH>=fa.h)break;
        TextOutW(dc,x,y,&s,1);
        fa.x0[ch]=(float)x;fa.x1[ch]=(float)(x+sz.cx);fa.y0[ch]=(float)y;fa.y1[ch]=(float)(y+fa.lineH);fa.adv[ch]=(float)sz.cx;
        x+=sz.cx+1;
    }
    GdiFlush();
    SelectObject(dc,of); SelectObject(dc,ob);
    std::vector<u8> lum((size_t)fa.w*fa.h);
    const u8* px=(const u8*)bits;
    for(size_t i=0;i<lum.size();++i) lum[i]=px[i*4+1];
    glGenTextures(1,&fa.tex);
    glBindTexture(GL_TEXTURE_2D,fa.tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,fa.w,fa.h,0,GL_RED,GL_UNSIGNED_BYTE,lum.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    DeleteObject(fnt); DeleteObject(bmp); DeleteDC(dc);
}

static std::vector<float> g_textVerts;
static void textBegin(){ g_textVerts.clear(); }
static unsigned nextTextGlyph(const std::string& s,size_t& offset){
    unsigned char first=(unsigned char)s[offset++];if(first<128)return first;
    int following=first>=0xf0&&first<=0xf4?3:first>=0xe0&&first<=0xef?2:first>=0xc2&&first<=0xdf?1:0;
    unsigned code=following==1?first&31:following==2?first&15:following==3?first&7:'?';
    for(int i=0;i<following;++i){if(offset>=s.size()||((unsigned char)s[offset]&0xc0)!=0x80)return '?';code=(code<<6)|((unsigned char)s[offset++]&63);}
    return code;
}
static unsigned atlasGlyph(unsigned code){return code>=32&&code<256&&(code<127||code>=160)&&fa.adv[code]>0?code:'?';}
static void textOut(const std::string& s,float x,float y,float sc=1.f,float red=.87f,float green=.89f,float blue=.88f){
    float cx=x;
    for(size_t offset=0;offset<s.size();){unsigned c=atlasGlyph(nextTextGlyph(s,offset));
        float u0=fa.x0[c]/fa.w,u1=fa.x1[c]/fa.w;
        float wd=(fa.x1[c]-fa.x0[c])*sc,hh=(float)fa.lineH*sc;
        float v0=fa.y0[c]/fa.h,v1=fa.y1[c]/fa.h;
        const float q[6][4]={
            {cx,y+hh,u0,v1},{cx+wd,y+hh,u1,v1},{cx+wd,y,u1,v0},
            {cx,y+hh,u0,v1},{cx+wd,y,u1,v0},{cx,y,u0,v0}};
        for(auto&r:q){g_textVerts.insert(g_textVerts.end(),r,r+4);g_textVerts.insert(g_textVerts.end(),{red,green,blue});}
        cx+=fa.adv[c]*sc;
    }
}
static float textWidth(const std::string& s,float sc=1.f){
    float w=0;for(size_t offset=0;offset<s.size();)w+=fa.adv[atlasGlyph(nextTextGlyph(s,offset))]*sc;return w;
}
// Fit using the same glyph advances as the renderer, preserving UTF-8 boundaries.
static std::string fitText(std::string text,float scale,float width){
    for(char& ch:text)if(ch=='\r'||ch=='\n'||ch=='\t')ch=' ';
    if(width<=0)return {};if(textWidth(text,scale)<=width)return text;
    const std::string suffix="...";float remaining=width-textWidth(suffix,scale);if(remaining<0)return {};
    size_t end=0;float used=0;while(end<text.size()){size_t next=end;auto glyph=atlasGlyph(nextTextGlyph(text,next));float advance=fa.adv[glyph]*scale;if(used+advance>remaining)break;used+=advance;end=next;}
    return text.substr(0,end)+suffix;
}

// ---------------------------------------------------------------------------
// Shaders -----------------------------------------------------------------------
static const char* VS_BG=R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);})";
static const char* FS_BG=R"(#version 330 core
out vec4 o;uniform vec2 res;uniform float lightBackground;
void main(){
 vec2 uv=gl_FragCoord.xy/res;
 vec2 q=uv-vec2(.5,.54);
 vec3 dark=mix(vec3(.070,.086,.096),vec3(.115,.140,.155),uv.y);
 dark+=vec3(.018,.023,.026)*exp(-dot(q/vec2(.43,.34),q/vec2(.43,.34))*1.8);
 vec3 light=mix(vec3(.76,.79,.83),vec3(.91,.93,.96),smoothstep(0.,1.,uv.y));
 light+=vec3(.055,.064,.078)*exp(-dot(q/vec2(.34,.27),q/vec2(.34,.27))*1.5);
 light-=vec3(.045,.055,.068)*smoothstep(.0,.9,length(q));
 vec3 c=mix(dark,light,lightBackground);o=vec4(clamp(c,0.,1.),1.);
})";
static const char* VS_SOLID=R"(#version 330 core
layout(location=0) in vec3 P; layout(location=1) in vec3 N;
uniform mat4 vp,model; uniform float outline;
out vec3 nrm,wpos,lpos,lnrm;
void main(){
 vec3 p=P; if(outline>0.)p+=N*outline;
 vec4 w=model*vec4(p,1.); wpos=w.xyz; nrm=transpose(inverse(mat3(model)))*N; lpos=P;lnrm=N;
 gl_Position=vp*w;
})";
static const char* FS_BUSY=R"(#version 330 core
out vec4 o;uniform sampler2D frame;uniform vec2 res;
void main(){o=texture(frame,gl_FragCoord.xy/res);})";
#include "studio_shaders.inl"
static const char* FS_CUBE=R"(#version 330 core
in vec3 nrm,wpos,lpos,lnrm;out vec4 o;uniform vec3 color,eye;
void main(){vec3 n=normalize(nrm),v=normalize(eye-wpos),l=normalize(vec3(-.35,.85,.48));float key=max(dot(n,l),0.),fill=max(dot(n,normalize(vec3(.70,.25,-.55))),0.);float gloss=.10*pow(max(dot(n,normalize(l+v)),0.),28.);o=vec4(color*(.72+.24*key+.12*fill)+vec3(gloss),1.);})";
static const char* VS_GEOM=R"(#version 330 core
layout(location=0) in vec3 P; layout(location=1) in vec3 N;
uniform mat4 view,proj,model; out vec3 vpos,vn;
void main(){vec4 v=view*model*vec4(P,1.); vpos=v.xyz; vn=mat3(view)*transpose(inverse(mat3(model)))*N; gl_Position=proj*v;}
)";
static const char* FS_GEOM=R"(#version 330 core
in vec3 vpos,vn; out vec4 o; uniform float hardSurface,depthFar,cameraOrtho;
void main(){
 vec3 n=hardSurface>0.?normalize(cross(dFdx(vpos),dFdy(vpos))):normalize(vn);
 if(dot(n,n)<1e-8)n=vec3(0.,0.,1.); if(dot(n,cameraOrtho>.5?vec3(0.,0.,1.):normalize(-vpos))<0.)n=-n;
 o=vec4(normalize(n)*.5+.5, clamp(-vpos.z/depthFar,0.,1.));
})";
static const char* FS_AO=R"(#version 330 core
out vec4 o; uniform sampler2D gbuf; uniform vec2 res; uniform mat4 proj; uniform vec2 focal; uniform float ortho,radius,bias,depthFar;uniform int mode;
const vec3 K[16]=vec3[16](
 vec3(.05,.02,.90),vec3(-.12,.08,.70),vec3(.22,-.05,.55),vec3(-.04,-.18,.80),
 vec3(.30,.16,.40),vec3(-.28,.22,.35),vec3(.10,-.32,.45),vec3(-.20,-.12,.60),
 vec3(.42,.02,.25),vec3(-.36,-.24,.30),vec3(.18,.40,.20),vec3(-.08,.36,.28),
 vec3(.26,-.28,.22),vec3(-.44,.10,.18),vec3(.06,.14,.95),vec3(-.16,-.40,.16));
void main(){
 vec2 uv=gl_FragCoord.xy/res; vec4 g=texture(gbuf,uv);
 if(g.a>0.985){o=vec4(1);return;}
 vec3 n=normalize(g.rgb*2.-1.); vec2 ndc=uv*2.-1.; float dist=g.a*depthFar;
 vec3 pos=ortho>.5?vec3(ndc.x/focal.x,ndc.y/focal.y,-dist):vec3(ndc.x*dist/focal.x,ndc.y*dist/focal.y,-dist);
 vec3 t=normalize(cross(n,abs(n.y)<.85?vec3(0,1,0):vec3(1,0,0))); vec3 b=cross(n,t);
 float ang=fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453)*6.2831853; float ca=cos(ang),sa=sin(ang); float occ=0.;
 for(int i=0;i<16;++i){if(mode==1&&i>=8)break;
  vec3 k=K[i]; vec3 hem=t*(k.x*ca-k.y*sa)+b*(k.x*sa+k.y*ca)+n*abs(k.z);
  float span=mode==1?.24+.48*float(i)/7.:.16+.84*float(i)/15.;
  vec3 s=pos+normalize(hem)*radius*span;
  vec4 clip=proj*vec4(s,1.); vec2 suv=clip.xy/max(clip.w,1e-4)*.5+.5; if(suv.x<0.||suv.y<0.||suv.x>1.||suv.y>1.)continue;
  float raw=texture(gbuf,suv).a; if(raw>0.985)continue;
  float sampledZ=-raw*depthFar;float range=1.-smoothstep(0.,1.,abs(pos.z-sampledZ)/max(radius,1e-4));
  occ+=step(s.z+bias,sampledZ)*range;}
 if(mode==3){
  // Four long horizon rays find broad cavity shadows beyond the local hemisphere.
  for(int dir=0;dir<8;++dir){float a=ang+float(dir)*.78539816;vec3 axis=t*cos(a)+b*sin(a);float horizon=0.;
   for(int stepIndex=1;stepIndex<=3;++stepIndex){vec3 s=pos+axis*radius*(.35+.4*float(stepIndex));vec4 clip=proj*vec4(s,1.);vec2 suv=clip.xy/max(clip.w,1e-4)*.5+.5;
    if(suv.x<0.||suv.y<0.||suv.x>1.||suv.y>1.)continue;float raw=texture(gbuf,suv).a;if(raw>.985)continue;
    float z=-raw*depthFar;vec3 q=ortho>.5?vec3((suv*2.-1.)/focal,z):vec3((suv*2.-1.)*(-z)/focal,z);
    vec3 delta=q-pos;float reach=length(delta);if(reach<1e-4)continue;float lift=dot(delta/reach,n);
    horizon=max(horizon,smoothstep(.04,.34,lift)*(1.-smoothstep(0.,radius*1.8,reach)));}
   occ+=horizon*.75;}
 }
 float divisor=mode==1?5.5:mode==3?15.:11.;o=vec4(clamp(1.-occ/divisor,.35,1.));
})";
static const char* FS_COMPOSE=R"(#version 330 core
out vec4 o; uniform sampler2D ao,gbuf; uniform vec2 res,origin;uniform float strength;
void main(){
 vec2 uv=(gl_FragCoord.xy-origin)/res; float d0=texture(gbuf,uv).a; float sum=0.,wsum=0.; vec2 texel=1.6/res;
 for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){vec2 p=uv+vec2(x,y)*texel; float w=1.-smoothstep(0.,.035,abs(texture(gbuf,p).a-d0)); sum+=texture(ao,p).r*w; wsum+=w;}
 float a=mix(1.,sum/max(wsum,.001),strength); o=vec4(a,a,a,1.);
})";
static const char* VS_EDGE=R"(#version 330 core
layout(location=0) in vec3 P; uniform mat4 vp,model;
void main(){gl_Position=vp*model*vec4(P,1.); gl_Position.z-=.0008*gl_Position.w;}
)";
static const char* FS_EDGE=R"(#version 330 core
out vec4 o; uniform vec3 color; void main(){o=vec4(color,1.);}
)";
static const char* VS_LINE=R"(#version 330 core
layout(location=0) in vec3 P; layout(location=1) in float weight; uniform mat4 vp; out vec2 gridPos; out float lineWeight;
void main(){gl_Position=vp*vec4(P,1.);gridPos=P.xz;lineWeight=weight;})";
static const char* FS_LINE=R"(#version 330 core
in vec2 gridPos; in float lineWeight; out vec4 o;
uniform float lightBackground,fadeRadius;
void main(){vec3 col=mix(vec3(.40,.42,.46),vec3(.45,.48,.52),lightBackground); float a=mix(.30,.55,lightBackground)*lineWeight; vec2 q=gridPos/max(fadeRadius,.0001); o=vec4(col,exp(-dot(q,q)*1.6)*a);})";
static const char* VS_SHADOW=R"(#version 330 core
layout(location=0) in vec2 P; uniform mat4 vp; uniform vec3 ctr; uniform vec2 rad;
out vec2 uv;
void main(){uv=P;gl_Position=vp*vec4(ctr+vec3(P.x*rad.x,0,P.y*rad.y),1.);})";
static const char* FS_SHADOW=R"(#version 330 core
in vec2 uv; out vec4 o;
uniform float strength;
void main(){o=vec4(0.,0.,0.,(1.-smoothstep(.18,1.,length(uv)))*strength);})";
static const char* VS_UI=R"(#version 330 core
layout(location=0) in vec2 P;
uniform vec2 vp; uniform vec4 rc; out vec2 uv;
void main(){ uv=(P+1.)*.5; vec2 w=rc.xy+uv*rc.zw;
 vec2 c=w/vp*2.-1.;c.y=-c.y; gl_Position=vec4(c,0.,1.); })";
static const char* FS_UI=R"(#version 330 core
in vec2 uv; out vec4 o;
uniform vec4 fill,borderCol,rc,lineAB; uniform float radius,borderW; uniform int glyph;
float seg(vec2 p,vec2 a,vec2 b){vec2 d=b-a;return length(p-a-d*clamp(dot(p-a,d)/dot(d,d),0.,1.));}
float box(vec2 p,vec2 b){vec2 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,q.y),0.);}
float mark(vec2 p){return min(min(seg(p,vec2(-.72,.65),vec2(0.,-.72)),seg(p,vec2(0.,-.72),vec2(.72,.65))),seg(p,vec2(-.27,.18),vec2(.51,.18)));}
float symbol(vec2 p,int g){
 if(g==486)return min(seg(p,vec2(-.52,.52),vec2(.47,-.47))-.14,min(seg(p,vec2(-.52,.52),vec2(-.67,.67)),seg(p,vec2(-.67,.67),vec2(-.40,.61)))-.045);
 if(g==487)return min(seg(p,vec2(-.48,-.48),vec2(.48,.48)),seg(p,vec2(-.48,.48),vec2(.48,-.48)))-.11;
 if(g==484)return min(min(abs(box(p,vec2(.64,.62)))-.045,abs(p.y)-.04),min(abs(box(p-vec2(-.32,-.31),vec2(.14,.19)))-.04,min(abs(box(p-vec2(.29,-.31),vec2(.18,.19)))-.04,abs(length(p-vec2(.03,.31))-.18)-.04)));
 if(g==485)return min(abs(box(p-vec2(.18,.15),vec2(.42,.45)))-.045,min(seg(p,vec2(-.78,-.2),vec2(.18,-.2)),min(seg(p,vec2(-.06,-.45),vec2(.18,-.2)),seg(p,vec2(-.06,.05),vec2(.18,-.2))))-.06);
 if(g==459)return min(length(p-vec2(-.48,0.)),min(length(p),length(p-vec2(.48,0.))))-.10;
 if(g==55)return min(abs(box(p,vec2(.46,.65)))-.045,min(seg(p,vec2(-.23,0.),vec2(.23,0.)),seg(p,vec2(0.,-.23),vec2(0.,.23)))-.055);
 if(g==56){float a=atan(p.y,p.x),r=length(p);return min(abs(r-(.49+.055*cos(a*8.)))-.075,abs(r-.20)-.055);}
 if(g==58)return min(min(abs(box(p-vec2(-.39,-.28),vec2(.24,.25)))-.04,abs(box(p-vec2(.39,.28),vec2(.24,.25)))-.04),seg(p,vec2(-.2,-.18),vec2(.2,.18))-.06);
 if(g==213)return min(abs(box(p-vec2(-.20,.17),vec2(.43,.48)))-.04,min(seg(p,vec2(.0,.0),vec2(.65,-.65)),min(seg(p,vec2(.24,-.65),vec2(.65,-.65)),seg(p,vec2(.65,-.65),vec2(.65,-.24))))-.065);
 if(g==216)return min(seg(p,vec2(0.,-.7),vec2(0.,.22))-.065,min(min(seg(p,vec2(-.25,-.05),vec2(0.,.22)),seg(p,vec2(.25,-.05),vec2(0.,.22)))-.065,abs(box(p-vec2(0.,.51),vec2(.61,.12)))-.04));
 if(g==457){vec2 q=p-vec2(.29,-.29);return min(seg(p,vec2(-.6,.6),vec2(.29,-.29))-.11,max(abs(length(q)-.27)-.075,-q.x-q.y-.09));}
 if(g==456)return min(abs(box(p-vec2(-.14,-.16),vec2(.46)))-.04,min(length(p-vec2(.15,.53)),min(length(p-vec2(.40,.53)),length(p-vec2(.65,.53))))-.055);
 if(g==100)g=1;
 if(g==401)return min(abs(box(p,vec2(.58)))-.045,seg(p,vec2(-.48,.45),vec2(.43,-.42))-.06);
 if(g==402)return min(abs(box(p,vec2(.62,.48)))-.045,min(abs(p.x),abs(p.y))-.035);
 if(g==403)return min(min(seg(p,vec2(-.6,.5),vec2(-.6,-.5)),seg(p,vec2(-.6,.5),vec2(.6,.5))),seg(p,vec2(-.6,-.5),vec2(-.15,-.72)))-.07;
 if(g==404)return min(abs(box(p,vec2(.52)))-.04,abs(box(p-vec2(.25,-.25),vec2(.39)))-.04);
 if(g==405)return min(min(seg(p,vec2(-.65,-.35),vec2(0.,-.68)),seg(p,vec2(0.,-.68),vec2(.65,-.35))),min(abs(box(p-vec2(0.,.07),vec2(.55,.43)))-.045,seg(p,vec2(0.,-.65),vec2(0.,.5))-.045));
 if(g==406)return min(abs(box(p,vec2(.57)))-.045,abs(length(p)-.23)-.045);
 if(g==407)return min(abs(length(p-vec2(0.,-.2))-.4)+max(p.y+.2,0.),seg(p,vec2(-.55,.45),vec2(.55,.45)))-.065;
 if(g==408)return min(abs(box(p-vec2(0.,.12),vec2(.65,.40)))-.04,abs(box(p-vec2(-.31,-.30),vec2(.29,.16)))-.04);
 if(g==409)return min(seg(p,vec2(-.4,-.25),vec2(0.,.2)),seg(p,vec2(0.,.2),vec2(.4,-.25)))-.06;
 if(g==410)return min(seg(p,vec2(-.2,-.4),vec2(.25,0.)),seg(p,vec2(.25,0.),vec2(-.2,.4)))-.06;
 if(g==400)return min(abs(box(p-vec2(0.,-.1),vec2(.61,.43)))-.055,min(seg(p,vec2(-.32,.30),vec2(-.47,.66)),seg(p,vec2(-.47,.66),vec2(.08,.30)))-.05);
 if(g==57)return min(abs(length(p-vec2(0.,-.36))-.26)-.05,abs(length(p-vec2(0.,.55))-.53)+max(-p.y-.08,0.)-.055);
 if(g==413)return min(abs(length(p-vec2(-.15,-.17))-.45)-.055,seg(p,vec2(.19,.17),vec2(.69,.68))-.08);
 if(g==460)return min(min(seg(p,vec2(-.7,-.22),vec2(-.7,-.7)),seg(p,vec2(-.7,-.7),vec2(-.22,-.7))),min(min(seg(p,vec2(.22,-.7),vec2(.7,-.7)),seg(p,vec2(.7,-.7),vec2(.7,-.22))),min(min(seg(p,vec2(.7,.22),vec2(.7,.7)),seg(p,vec2(.7,.7),vec2(.22,.7))),min(seg(p,vec2(-.22,.7),vec2(-.7,.7)),seg(p,vec2(-.7,.7),vec2(-.7,.22))))))-.065;
 if(g==461)return min(min(abs(p.x),abs(p.y))-.055,min(abs(abs(p.x)+abs(p.y)-.62),abs(min(abs(p.x),abs(p.y))-.18))-.025);
 if(g==325)return min(seg(p,vec2(-.56,.50),vec2(.48,-.54))-.12,min(seg(p,vec2(-.56,.50),vec2(-.66,.72)),seg(p,vec2(-.66,.72),vec2(-.40,.65)))-.04);
 if(g==99)return mark(p)-.065;
 if(g==1||g==6||g==11||g==27||g==28){
   float a=abs(box(p,vec2(.53,.46)))-.045;
   if(g==27||g==28||g==11)a=min(abs(box(p-vec2(.24,-.24),vec2(.38,.34)))-.045,abs(box(p+vec2(.20,.22),vec2(.38,.34)))-.045);
   else a=min(a,min(seg(p,vec2(-.53,-.46),vec2(-.18,-.75)),min(seg(p,vec2(-.18,-.75),vec2(.72,-.75)),seg(p,vec2(.72,-.75),vec2(.53,-.46))))-.045);
   if(g==6)a=min(a,seg(p,vec2(0.,.10),vec2(0.,-.75))-.06);
   return a;
 }
 if(g==2)return min(abs(length(p/vec2(.56,.22)-vec2(0.,-1.8))-1.)*.22,max(abs(abs(p.x)-.56),abs(p.y)-.4))-.04;
 if(g==3||g==5||g==7){float a=abs(length(p)-.64)-.045;if(g==5)a=min(a,abs(length(p)-.30)-.045);else a=min(a,abs(length(p/vec2(.30,.64))-1.)*.25-.035);return a;}
 if(g==8||g==9||g==10){float a=abs(length(p-vec2(-.24,0.))-.46)-.045;float b=abs(length(p-vec2(.24,0.))-.46)-.045;return min(a,b);}
 if(g==26)return min(seg(p,vec2(-.53,.60),vec2(-.53,-.10)),min(abs(length(p-vec2(-.10,-.10))-.43)+max(p.x+.1,0.)+max(p.y+.1,0.),seg(p,vec2(-.1,-.53),vec2(.65,-.53))))-.11;
 if(g==12||g==16){return min(abs(box(p-vec2(0.,.20),vec2(.57,.43)))-.05,min(seg(p,vec2(0.,-.8),vec2(0.,.1)),min(seg(p,vec2(-.25,-.15),vec2(0.,.1)),seg(p,vec2(.25,-.15),vec2(0.,.1))))-.06);}
 if(g==17)return min(abs(box(p-vec2(0.,.13),vec2(.65,.43)))-.05,seg(p,vec2(-.65,-.3),vec2(-.18,-.64))-.05);
 if(g==18||g==19){if(g==19)p.x=-p.x;return min(abs(length(p-vec2(.08,.12))-.52)+max(-p.y-.22,0.),min(seg(p,vec2(-.42,-.22),vec2(-.40,-.65)),seg(p,vec2(-.42,-.22),vec2(.0,-.22))))-.05;}
 if(g==20)return min(abs(box(p,vec2(.62)))-.045,min(abs(p.x),abs(p.y))-.035);
 if(g==22||g==14){return min(abs(box(p,vec2(.61)))-.035,min(abs(p.x),abs(p.y))-.025);}
 if(g==25)return min(abs(box(p-vec2(0.,.12),vec2(.4,.48)))-.045,seg(p,vec2(-.60,-.50),vec2(.6,-.50))-.06);
 if(g==29)return min(min(seg(p,vec2(-.65,.03),vec2(0.,.65)),seg(p,vec2(0.,.65),vec2(.65,.03))),min(seg(p,vec2(-.48,.02),vec2(-.48,-.62)),min(seg(p,vec2(.48,.02),vec2(.48,-.62)),seg(p,vec2(-.48,-.62),vec2(.48,-.62)))))-.07;
 if(g==21)return min(min(seg(p,vec2(-.7,-.45),vec2(-.22,0.)),seg(p,vec2(-.22,0.),vec2(-.7,.45))),seg(p,vec2(.08,.45),vec2(.65,.45)))-.06;
 return min(abs(box(p,vec2(.57)))-.04,seg(p,vec2(-.5,.5),vec2(.5,-.5))-.04);
}
float sdR(vec2 p,vec2 b,float r){vec2 d=abs(p)-b+r;return length(max(d,0.))+min(max(d.x,d.y),0.)-r;}
void main(){
 if(glyph==500){float d=seg(uv*rc.zw,lineAB.xy,lineAB.zw);o=vec4(fill.rgb,fill.a*(1.-smoothstep(borderW*.5-.6,borderW*.5+.6,d)));return;}
 if(glyph>0){vec2 p=(uv-.5)*2.;float d=symbol(p,glyph);float a=1.-smoothstep(-.025,.045,d);float lit=.80+.20*(1.-uv.y);o=vec4(fill.rgb*lit,fill.a*a);return;}
 float d=sdR((uv-.5)*rc.zw,rc.zw*.5,radius);
 float aa=fwidth(d)*1.2;
 float inside=1.-smoothstep(-aa,aa,d);
 float band=smoothstep(-borderW-aa,-borderW+aa,d)*step(.0001,borderW);
 vec4 c=mix(fill,borderCol,band);
 o=vec4(c.rgb,c.a*inside);
})";
static const char* VS_TEXT=R"(#version 330 core
layout(location=0) in vec2 P; layout(location=1) in vec2 T; layout(location=2) in vec3 C;
uniform vec2 vp; out vec2 tc; out vec3 tint;
void main(){tc=T;tint=C;vec2 c=P/vp*2.-1.;c.y=-c.y;gl_Position=vec4(c,0.,1.);})";
static const char* FS_TEXT=R"(#version 330 core
in vec2 tc; in vec3 tint; out vec4 o; uniform sampler2D txx;
void main(){o=vec4(tint,texture(txx,tc).r);})";
static const char* FS_ICON=R"(#version 330 core
in vec2 uv;out vec4 o;uniform sampler2D atlas;uniform vec2 tile;uniform float opacity;
void main(){vec2 q=(tile+clamp(uv,vec2(.002),vec2(.998)))*.25;q.y=1.-q.y;vec4 p=texture(atlas,q);o=vec4(p.rgb,p.a*opacity);})";
static const char* FS_IMAGE=R"(#version 330 core
in vec2 uv;out vec4 o;uniform sampler2D image;void main(){o=texture(image,vec2(uv.x,1.-uv.y));})";

// ---------------------------------------------------------------------------
static void uploadMesh(GPUMesh& gm,const Mesh& m){
    ++g_displayMeshSerial;
    if(!gm.vao)glGenVertexArrays(1,&gm.vao);
    if(!gm.vbo)glGenBuffers(1,&gm.vbo);
    if(!gm.ibo)glGenBuffers(1,&gm.ibo);
    glBindVertexArray(gm.vao);
    glBindBuffer(GL_ARRAY_BUFFER,gm.vbo);
    std::vector<float> pk; pk.reserve(m.pos.size()+m.nrm.size());
    for(size_t i=0;i<m.vertCount();++i){
        pk.push_back(m.pos[i*3]);pk.push_back(m.pos[i*3+1]);pk.push_back(m.pos[i*3+2]);
        pk.push_back(m.nrm[i*3]);pk.push_back(m.nrm[i*3+1]);pk.push_back(m.nrm[i*3+2]);
    }
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(pk.size()*sizeof(float)),pk.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gm.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(m.idx.size()*sizeof(u32)),m.idx.data(),GL_STATIC_DRAW);
    glBindVertexArray(0);
    gm.count=(int)m.triCount();
}
static void releaseMesh(GPUMesh& mesh){if(mesh.vbo)glDeleteBuffers(1,&mesh.vbo);if(mesh.ibo)glDeleteBuffers(1,&mesh.ibo);if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);mesh={};}
struct GPULines { GLuint vao=0,vbo=0; int verts=0; };
static std::vector<GPULines> g_edgeLines;
static void uploadEdges(size_t part){
    if(part>=g_edgeLines.size())g_edgeLines.resize(part+1);
    auto& lines=g_edgeLines[part];
    if(!lines.vao)glGenVertexArrays(1,&lines.vao);
    if(!lines.vbo)glGenBuffers(1,&lines.vbo);
    std::vector<float> data;
    const float corners[6][2]={{0,-1},{1,-1},{1,1},{0,-1},{1,1},{0,1}};
    if(part<g_partPicks.size())for(size_t edge=0;edge<g_partPicks[part].edges.size();++edge){
        if(edge<g_partPicks[part].edgeVisible.size()&&!g_partPicks[part].edgeVisible[edge])continue;
        const auto& poly=g_partPicks[part].edges[edge];if(poly.size()<2)continue;
        for(size_t k=1;k<poly.size();++k){Vec3 a=poly[k-1],b=poly[k];if((b-a).lensq()<1e-20)continue;
            for(auto& corner:corners)data.insert(data.end(),{(float)a.x,(float)a.y,(float)a.z,(float)b.x,(float)b.y,(float)b.z,corner[0],corner[1]});}
    }
    glBindVertexArray(lines.vao);glBindBuffer(GL_ARRAY_BUFFER,lines.vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(data.size()*sizeof(float)),data.empty()?nullptr:data.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,32,nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,32,(void*)12);
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,32,(void*)24);glBindVertexArray(0);
    lines.verts=(int)(data.size()/8);
}
static Feature* selectedNotchedTube(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())return nullptr;
    auto* f=g_doc.find(g_bodyIds[selBody]);return f&&f->type==FeatureType::NotchedTube?f:nullptr;
}
static Feature* selectedSourceTube(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())return nullptr;
    auto* f=g_doc.find(g_bodyIds[selBody]);return f&&f->type==FeatureType::SourceTube?f:nullptr;
}
static Feature* selectedSheet(){
    auto* f=g_explorerFeature?g_doc.find(g_explorerFeature):(selBody>=0&&(size_t)selBody<g_bodyIds.size()?g_doc.find(g_bodyIds[selBody]):nullptr);
    if(!f)return nullptr;auto lineage=featureManufacturingLineage(g_doc,*f);return lineage.geometryRoute=="sheet_metal"?g_doc.find(lineage.origin):nullptr;
}
static bool isSheetFeature(u64 id){const Feature* feature=g_doc.find(id);return feature&&featureManufacturingLineage(g_doc,*feature).geometryRoute=="sheet_metal";}
static const Body* sheetOriginBody(u64 id){auto* feature=g_doc.find(id);if(!feature)return nullptr;auto lineage=featureManufacturingLineage(g_doc,*feature);
    auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& candidate){return candidate.id==lineage.origin;});return body==g_doc.evalBodies.end()?nullptr:&*body;}
static bool sheetHasStamp(const Feature& feature){auto* stamps=feature.nativeSpec.find("stamps");return stamps&&stamps->t==arkjson::Value::Arr&&!stamps->arr.empty();}
static Mat4 sheetDefinitionPlacement(u64 id){
    Mat4 placement=Mat4::identity();auto* feature=g_doc.find(id);if(!feature)return placement;auto lineage=featureManufacturingLineage(g_doc,*feature);auto env=g_doc.buildEnv();
    for(size_t count=0;feature&&feature->id!=lineage.origin&&count<g_doc.features.size();++count){if(feature->type!=FeatureType::NativeSolid||featureOperation(*feature)!="transform"||feature->inputs.size()!=1)throw std::runtime_error("Sheet display requires an exact rigid-transform lineage");
        Mat4 local;if(const auto* frame=feature->nativeSpec.find("frame")){if(frame->t!=arkjson::Value::Arr||frame->arr.size()!=16)throw std::runtime_error("Invalid sheet placement frame");for(int k=0;k<16;++k)local.m[k]=frame->arr[(size_t)k].num;}
        else local=Mat4::translation({feature->num(env,"tx"),feature->num(env,"ty"),feature->num(env,"tz")})*Mat4::rotZ(deg2rad(feature->num(env,"rz")))*Mat4::rotY(deg2rad(feature->num(env,"ry")))*Mat4::rotX(deg2rad(feature->num(env,"rx")));
        placement=placement*local;feature=g_doc.find(feature->inputs[0]);
    }return placement;
}
static bool displayInstance(size_t index){return index<g_bodyIds.size()&&(index>=g_hiddenBodies.size()||!g_hiddenBodies[index])&&
    (!g_stockMode||g_bodyIds[index]==g_stockFeatureId)&&(!g_allSheetsFlat||g_flatShowChassis||isSheetFeature(g_bodyIds[index]));}
static const Body* selectedNativeBody(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())return nullptr;
    auto id=g_bodyIds[selBody];auto it=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& body){return body.id==id;});
    return it==g_doc.evalBodies.end()?nullptr:&*it;
}
static Mesh nativePreviewMesh(const arkjson::Value& value){
    using V=arkjson::Value;auto* pos=value.find("pos"),*idx=value.find("idx");
    if(!pos||!idx||pos->t!=V::Arr||idx->t!=V::Arr||pos->arr.size()%3||idx->arr.size()%3||pos->arr.empty()||idx->arr.empty()||pos->arr.size()>6000000||idx->arr.size()>6000000)
        throw std::runtime_error("Invalid native display mesh");
    Mesh mesh;mesh.pos.reserve(pos->arr.size());mesh.idx.reserve(idx->arr.size());
    for(auto& item:pos->arr){if(item.t!=V::Num||!std::isfinite(item.num))throw std::runtime_error("Invalid native mesh position");mesh.pos.push_back((float)item.num);}
    for(auto& item:idx->arr){if(item.t!=V::Num||item.num<0||std::floor(item.num)!=item.num||item.num>=mesh.vertCount())throw std::runtime_error("Invalid native mesh index");mesh.idx.push_back((u32)item.num);}
    mesh.computeNormals();return mesh;
}
static Feature* selectedFoldable(){auto* f=selectedNotchedTube();return f?f:selectedSourceTube();}
static std::vector<Mat4> displayedSourceTransforms(size_t body){return sourceFoldTransforms(g_sourceViews.at(g_bodyIds[body]).preview,(int)body==selBody?g_foldProgress:0.);}
static void updateSourceHinges(SourceViewCache& cache,double progress){
    if(progress<=0)return;
    for(size_t i=0;i<cache.preview.bends.size();++i){double phase=clamp(progress*cache.preview.bends.size()-i,0.,1.);
        if(cache.phases[i]==phase)continue;cache.hinges[i]=sourceHingeMesh(cache.preview.bends[i],phase);uploadMesh(cache.hingeGpu[i],cache.hinges[i]);cache.hingeBvhs[i]=MeshBVH(cache.hinges[i]);cache.phases[i]=phase;++g_sourceHingeUpdates;
    }
}
static std::vector<Mat4> displayedNotchTransforms(size_t body){
    return notchFoldTransforms(g_notchViews.at(g_bodyIds[body]).preview,(int)body==selBody?g_foldProgress:0.);
}
static void advanceFold(double seconds){if(g_foldPlaying){g_foldProgress=std::min(1.,g_foldProgress+seconds*.20);if(g_foldProgress>=1.)g_foldPlaying=false;}}
static bool datumVisible(const std::string&);
static void appendDatumOverlay();
static void appendDatumSelectionLines();
static void rebuildSketchOverlay(){
    g_sketchGpuDirty=true;g_sketchLines.clear();g_sketchFill.clear();g_datumFill.clear();g_sketchPoints.clear();g_constraintHighlightLines.clear();g_datumLines.clear();g_curveSamples.clear();g_sketchBounds=AABB{};g_sketchDiagnosis=arkjson::Value{};
    auto put=[](std::vector<float>& into,Vec3 p){into.insert(into.end(),{(float)p.x,(float)p.y,(float)p.z});};
    for(auto& named:g_doc.sketches){if(!g_activeSketch.empty()&&named.name!=g_activeSketch)continue;auto frame=resolvePlane(g_doc,named.sk.plane);
        auto regions=sketchRegionMesh(named.sk,frame);for(u32 index:regions.idx){size_t at=(size_t)index*3;g_sketchFill.insert(g_sketchFill.end(),regions.pos.begin()+at,regions.pos.begin()+at+3);}
        if(named.name==g_activeSketch){g_sketchDiagnosis=diagnoseSketch(named.sk);if(g_selectedSketchConstraint>=(int)named.sk.cons.size())g_selectedSketchConstraint=-1;g_sketchConstraintPage=std::min(g_sketchConstraintPage,(int)((named.sk.cons.size()+3)/4)-1);g_sketchConstraintPage=std::max(g_sketchConstraintPage,0);}
        const SketchCon* selected=(named.name==g_activeSketch&&g_selectedSketchConstraint>=0&&g_selectedSketchConstraint<(int)named.sk.cons.size())?&named.sk.cons[(size_t)g_selectedSketchConstraint]:nullptr;
        for(size_t ei=0;ei<named.sk.ents.size();++ei){auto& entity=named.sk.ents[ei];std::string entityName=entity.name.empty()?"entity."+std::to_string(ei):entity.name;std::vector<Vec2> sampled;std::vector<std::vector<Vec2>> paths;try{paths=sampleSketchEntityPaths(entity,named.sk.tolerance,&named.sk);if(!paths.empty())sampled=paths.front();}catch(const std::exception&){continue;}for(const auto& path:paths)for(size_t i=1;i<path.size();++i){auto a=frame.transformPoint({path[i-1].x,path[i-1].y,0}),b=frame.transformPoint({path[i].x,path[i].y,0});put(g_sketchLines,a);put(g_sketchLines,b);g_curveSamples.push_back({named.name,entityName,a,b});g_sketchBounds.grow(a);g_sketchBounds.grow(b);if(selected&&((int)ei==selected->a||(int)ei==selected->b)){put(g_constraintHighlightLines,a);put(g_constraintHighlightLines,b);}}
            if(entity.kind==SketchEnt::Arc){
                // Radius and angles are scalars, not XY anchors. Mark the evaluated arc endpoints.
                if(!sampled.empty())for(const auto& endpoint:{sampled.front(),sampled.back()}){auto point=frame.transformPoint({endpoint.x,endpoint.y,0});put(g_sketchPoints,point);g_sketchBounds.grow(point);}
                if(entity.arcRecipe.isNull()&&entity.dof.size()>=2)put(g_sketchPoints,frame.transformPoint({entity.dof[0],entity.dof[1],0}));
            }else {size_t stride=entity.kind==SketchEnt::Circle?entity.dof.size():2;for(size_t i=0;i+1<entity.dof.size();i+=stride){auto point=frame.transformPoint({entity.dof[i],entity.dof[i+1],0});put(g_sketchPoints,point);g_sketchBounds.grow(point);}}
            if(selected)for(auto ref:{std::pair{selected->a,selected->pa},std::pair{selected->b,selected->pb}})if(ref.first==(int)ei){
                size_t slot=entity.kind==SketchEnt::Line||entity.kind==SketchEnt::Spline?(size_t)std::max(ref.second,0)*2:0;
                if(slot+1<entity.dof.size()){double x=entity.dof[slot],y=entity.dof[slot+1];for(auto delta:{Vec2{2,0},Vec2{0,2}}){put(g_constraintHighlightLines,frame.transformPoint({x-delta.x,y-delta.y,0}));put(g_constraintHighlightLines,frame.transformPoint({x+delta.x,y+delta.y,0}));}}
            }
        }
    }
    if(g_doc.sketches.empty()||g_activeSketch.empty()){auto plane=resolvePlane(g_doc,g_activePlane);for(int i=-5;i<=5;++i){put(g_sketchLines,plane.transformPoint({i*10.,-50,0}));put(g_sketchLines,plane.transformPoint({i*10.,50,0}));put(g_sketchLines,plane.transformPoint({-50,i*10.,0}));put(g_sketchLines,plane.transformPoint({50,i*10.,0}));}}
    appendDatumOverlay();
    refreshSelectionLines();
}
static void includeCanvasBounds(){
    ExprEnv env=g_doc.buildEnv();
    for(const auto& canvas:g_doc.canvases){
        bool okWidth=false,okHeight=false;
        double width=evalExpr(canvas.width,env,&okWidth),height=evalExpr(canvas.height,env,&okHeight);
        if(!okWidth||!okHeight||!(width>0)||!(height>0))continue;
        try{Mat4 plane=resolvePlane(g_doc,canvas.plane);for(Vec3 corner: {Vec3{0,0,0},{width,0,0},{width,height,0},{0,height,0}})g_sceneBounds.grow(plane.transformPoint(corner));}
        catch(...){}
    }
}
static void rebuildScene(bool evaluate=false){
    ++g_displayMeshSerial;
    auto previousIds=g_bodyIds,previousNodes=g_instanceIds;auto previousHidden=g_hiddenBodies;
    if(evaluate)g_doc.recompute();
    if(g_allSheetsFlat||g_allSheetsPlaying){g_sheetProgress.clear();g_sheetFlatViews.clear();}
    g_allSheetsFlat=false;g_allSheetsPlaying=false;g_flatShowChassis=false;g_allSheetsStage=0;g_stockMode=false;g_stockFeatureId=0;g_stockReport=arkjson::Value{};
    g_foldPlaying=false;g_sheetPlayingId=0;g_suspSpinPlaying=false;g_suspScrub=-1;g_suspClock=0;
    for(auto it=g_notchViews.begin();it!=g_notchViews.end();){auto* f=g_doc.find(it->first);if(!f||f->type!=FeatureType::NotchedTube){for(auto& mesh:it->second.gpu)releaseMesh(mesh);it=g_notchViews.erase(it);}else ++it;}
    for(auto it=g_suspViews.begin();it!=g_suspViews.end();){auto* f=g_doc.find(it->first);if(!f||f->type!=FeatureType::Suspension){for(auto& mesh:it->second.gpu)releaseMesh(mesh);it=g_suspViews.erase(it);}else ++it;}
    for(auto it=g_sourceViews.begin();it!=g_sourceViews.end();){auto* f=g_doc.find(it->first);if(!f||f->type!=FeatureType::SourceTube){for(auto* list:{&it->second.rigidGpu,&it->second.flatHingeGpu,&it->second.hingeGpu})for(auto& mesh:*list)releaseMesh(mesh);it=g_sourceViews.erase(it);}else ++it;}
    std::vector<AssemblyPlacement> placements;std::string error;
    g_assemblyDiagnostics={};
    if(g_doc.assembly.empty()){
        std::set<u64> consumed;for(auto& f:g_doc.features)if(featureConsumesInputBodies(f))for(auto id:f.inputs)consumed.insert(id);
        for(auto& b:g_doc.evalBodies)if(!consumed.count(b.id)){auto* f=g_doc.find(b.id);placements.push_back({0,b.id,f?f->name:"Body",Mat4{},true});}
    }else if(!resolveAssembly(g_doc,placements,error,&g_assemblyDiagnostics)){g_error=true;g_status=error;}
    if(g_selectedMate>=(int)g_doc.mates.size())g_selectedMate=-1;
    g_cpuMeshes.clear();g_partPicks.clear();g_bodyIds.clear();g_instanceIds.clear();g_instanceCopies.clear();g_instanceNames.clear();g_instanceWorld.clear();g_instancePart.clear();
    g_reports.clear();g_bvhs.clear();g_tubeInfo.clear();g_triangleCount=0;g_sceneBounds=AABB{};
    for(auto& gm:g_meshes)gm.count=0;
    std::unordered_map<u64,size_t> parts;
    for(auto& placement:placements){
        if(!placement.visible)continue;
        auto part=parts.find(placement.feature);
        if(part==parts.end()){
            auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& b){return b.id==placement.feature;});if(body==g_doc.evalBodies.end())continue;
            size_t i=g_cpuMeshes.size();Mesh display=body->tessellate(g_doc.tessOpt);bool nativeMesh=true;
            if(auto* f=g_doc.find(body->id);f&&(f->type==FeatureType::SheetMetal||f->type==FeatureType::SourceSheet)){
                double progress=g_sheetProgress.count(body->id)?g_sheetProgress[body->id]:g_sheetFlatViews[body->id]?0.:1.;
                if(progress<=0){if(auto* flat=body->nativeData.find("flat_mesh")){display=nativePreviewMesh(*flat);nativeMesh=false;}}
                else if(progress<1&&f->type==FeatureType::SheetMetal&&body->nativeData.find("animation")){display=buildSheetPreview(body->nativeData,progress).mesh;nativeMesh=false;}
            }
            if(auto* f=g_doc.find(body->id);f&&f->type==FeatureType::Suspension){
                auto& cache=g_suspViews[f->id];try{cache.preview=buildSuspensionPreview(*f,g_doc.buildEnv(),cache.state);}
                catch(const std::exception&){cache.state={};cache.preview=buildSuspensionPreview(*f,g_doc.buildEnv(),cache.state);}
                display=cache.preview.mesh;nativeMesh=false;
                while(cache.gpu.size()>cache.preview.parts.size()){releaseMesh(cache.gpu.back());cache.gpu.pop_back();}
                while(cache.gpu.size()<cache.preview.parts.size())cache.gpu.emplace_back();
                for(size_t part=0;part<cache.preview.parts.size();++part)uploadMesh(cache.gpu[part],cache.preview.parts[part].mesh);
            }
            g_partPicks.push_back(nativeMesh?partPickFrom(*body):PartPick{});
            g_cpuMeshes.push_back(std::move(display));
            if(auto* f=g_doc.find(body->id);f&&f->type==FeatureType::Tube){arkjson::Value a=arkjson::Value::makeObj();a.set("id",arkjson::Value::makeNum((double)body->id));auto r=g_session.call("tube_inspect",a);if(!r.find("isError")->b)g_tubeInfo[body->id]=*r.find("structuredContent");}g_reports.push_back(analyzeMesh(g_cpuMeshes.back()));g_bvhs.emplace_back(g_cpuMeshes.back());
            if(auto* f=g_doc.find(body->id);f&&f->type==FeatureType::NotchedTube){
                auto& cache=g_notchViews[f->id];cache.preview=buildNotchPreview(notchRequestForFeature(*f,g_doc.buildEnv()));
                while(cache.gpu.size()>cache.preview.segments.size()){releaseMesh(cache.gpu.back());cache.gpu.pop_back();}
                while(cache.gpu.size()<cache.preview.segments.size())cache.gpu.emplace_back();cache.bvhs.clear();cache.bounds.clear();cache.sourceIndices.clear();
                for(size_t segment=0;segment<cache.preview.segments.size();++segment){uploadMesh(cache.gpu[segment],cache.preview.segments[segment]);cache.bvhs.emplace_back(cache.preview.segments[segment]);cache.bounds.push_back(analyzeMesh(cache.preview.segments[segment]).bounds);}
                auto env=g_doc.buildEnv();for(double station:cache.preview.stations){size_t source=0;for(size_t k=0;k<f->notches.size();++k)if(std::abs(evalExpr(f->notches[k].station,env)-station)<1e-7){source=k;break;}cache.sourceIndices.push_back(source);}
                ++g_notchBuildCount;
            }
            if(auto* f=g_doc.find(body->id);f&&f->type==FeatureType::SourceTube){
                auto& cache=g_sourceViews[f->id];cache.preview=buildSourceTubePreview(f->sourceData);
                auto resizeGpu=[](std::vector<GPUMesh>& list,size_t count){while(list.size()>count){releaseMesh(list.back());list.pop_back();}while(list.size()<count)list.emplace_back();};
                resizeGpu(cache.rigidGpu,cache.preview.rigid.size());resizeGpu(cache.flatHingeGpu,cache.preview.bends.size());resizeGpu(cache.hingeGpu,cache.preview.bends.size());
                cache.rigidBvhs.clear();cache.rigidBounds.clear();cache.flatHinges.clear();cache.flatHingeBvhs.clear();
                cache.hinges.resize(cache.preview.bends.size());cache.hingeBvhs.clear();cache.phases.assign(cache.preview.bends.size(),-1.);
                for(size_t k=0;k<cache.preview.rigid.size();++k){uploadMesh(cache.rigidGpu[k],cache.preview.rigid[k]);cache.rigidBvhs.emplace_back(cache.preview.rigid[k]);cache.rigidBounds.push_back(analyzeMesh(cache.preview.rigid[k]).bounds);}
                for(size_t k=0;k<cache.preview.bends.size();++k){cache.flatHinges.push_back(sourceHingeMesh(cache.preview.bends[k],0));uploadMesh(cache.flatHingeGpu[k],cache.flatHinges.back());cache.flatHingeBvhs.emplace_back(cache.flatHinges.back());cache.hingeBvhs.emplace_back(cache.flatHinges.back());}
                ++g_sourceBuildCount;
            }
            if(i>=g_meshes.size())g_meshes.emplace_back();uploadMesh(g_meshes[i],g_cpuMeshes.back());uploadEdges(i);part=parts.emplace(placement.feature,i).first;
        }
        auto i=part->second;g_bodyIds.push_back(placement.feature);g_instanceIds.push_back(placement.node);g_instanceCopies.push_back(placement.copy);g_instanceNames.push_back(placement.name);g_instanceWorld.push_back(placement.world);g_instancePart.push_back(i);
        g_sceneBounds.grow(g_reports[i].bounds.transformed(placement.world));g_triangleCount+=g_cpuMeshes[i].triCount();
    }
    g_formedInstanceWorld=g_instanceWorld;
    if(previousIds==g_bodyIds&&previousNodes==g_instanceIds&&previousHidden.size()==g_bodyIds.size())g_hiddenBodies=std::move(previousHidden);
    else{g_hiddenBodies.assign(g_bodyIds.size(),false);g_beforeSpotlightHidden.clear();g_spotlightActive=false;g_spotlightBody=-1;}
    if(selBody<0||selBody>=(int)g_bodyIds.size())selBody=g_bodyIds.empty()?-1:0;
    hoverBody=-1;
    if(!g_activeSketch.empty()&&!g_doc.findSketch(g_activeSketch))g_activeSketch.clear();
    if(g_activePlane!="XY"&&g_activePlane!="XZ"&&g_activePlane!="YZ"&&std::none_of(g_doc.planes.begin(),g_doc.planes.end(),[](auto& p){return p.name==g_activePlane;}))g_activePlane="XY";
    rebuildSketchOverlay();
    includeCanvasBounds();
    if(auto* f=selectedNotchedTube())g_selectedNotch=std::max(0,std::min(g_selectedNotch,(int)f->notches.size()-1));
}
static double sheetDisplayProgress(u64 id){auto it=g_sheetProgress.find(id);return it==g_sheetProgress.end()?(g_sheetFlatViews[id]?0.:1.):it->second;}
static void rebuildDisplayBounds(){
    g_sceneBounds=AABB{};g_triangleCount=0;
    for(size_t i=0;i<g_bodyIds.size();++i)if(displayInstance(i)){
        size_t part=g_instancePart[i];g_sceneBounds.grow(g_reports[part].bounds.transformed(g_instanceWorld[i]));g_triangleCount+=g_cpuMeshes[part].triCount();
    }
    if(!g_allSheetsFlat)includeCanvasBounds();
}
static void updateSheetDisplay(u64 id){
    auto* body=sheetOriginBody(id);if(!body)return;
    auto* feature=g_doc.find(body->id);if(!feature)return;
    double progress=sheetDisplayProgress(id);Mesh mesh;
    if(progress<=0){auto* flat=body->nativeData.find("flat_mesh");if(!flat)throw std::runtime_error("Native flat sheet preview is unavailable");mesh=nativePreviewMesh(*flat);}
    else if(progress>=1)mesh=body->tessellate(g_doc.tessOpt);
    else {if(feature->type!=FeatureType::SheetMetal||!body->nativeData.find("animation"))throw std::runtime_error("This sheet has no authored bend animation");mesh=buildSheetPreview(body->nativeData,progress).mesh;}
    std::set<size_t> changed;for(size_t i=0;i<g_bodyIds.size();++i){auto* displayed=g_doc.find(g_bodyIds[i]);if(!displayed||featureManufacturingLineage(g_doc,*displayed).origin!=body->id||!changed.insert(g_instancePart[i]).second)continue;
        size_t part=g_instancePart[i];const Body* exact=nullptr;for(const auto& candidate:g_doc.evalBodies)if(candidate.id==displayed->id){exact=&candidate;break;}
        Mesh displayedMesh=mesh;if(progress>=1&&exact)displayedMesh=exact->tessellate(g_doc.tessOpt);else if(!g_allSheetsFlat&&displayed->id!=body->id){Mesh placed;placed.append(mesh,sheetDefinitionPlacement(displayed->id));placed.computeNormals();displayedMesh=std::move(placed);}
        g_cpuMeshes[part]=std::move(displayedMesh);if(part<g_partPicks.size())g_partPicks[part]=progress>=1&&exact?partPickFrom(*exact):PartPick{};uploadMesh(g_meshes[part],g_cpuMeshes[part]);uploadEdges(part);g_reports[part]=analyzeMesh(g_cpuMeshes[part]);g_bvhs[part]=MeshBVH(g_cpuMeshes[part]);
    }
    rebuildDisplayBounds();
}
static void sheetPreviewSet(u64 id,double progress){
    progress=clamp(progress,0.,1.);double before=sheetDisplayProgress(id);bool wasFlat=g_sheetFlatViews[id];
    g_sheetProgress[id]=progress;g_sheetFlatViews[id]=progress==0.;
    try{updateSheetDisplay(id);}catch(...){g_sheetProgress[id]=before;g_sheetFlatViews[id]=wasFlat;throw;}
}
static void frameScene(bool animation=false);
static std::vector<u64> visibleSheetIds(){
    std::vector<u64> ids;std::set<u64> seen;
    for(u64 id:g_bodyIds)if(isSheetFeature(id)){auto lineage=featureManufacturingLineage(g_doc,*g_doc.find(id));if(seen.insert(lineage.origin).second)ids.push_back(lineage.origin);}
    return ids;
}
static void toggleStockPreview();
static void allSheetPose(bool flat){
    if(g_stockMode)toggleStockPreview();
    const auto ids=visibleSheetIds();if(ids.empty())throw std::runtime_error("This scene has no visible sheet metal bodies");
    if(g_formedInstanceWorld.size()!=g_instanceWorld.size())throw std::runtime_error("Assembly display poses are unavailable");
    for(u64 id:ids)if(flat){auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& candidate){return candidate.id==id;});
        if(body==g_doc.evalBodies.end()||!body->nativeData.find("flat_mesh"))throw std::runtime_error("A visible sheet lacks its exact flat preview: "+std::to_string(id));}
    auto oldWorld=g_instanceWorld;auto oldProgress=g_sheetProgress;auto oldFlat=g_sheetFlatViews;bool oldMode=g_allSheetsFlat,oldChassis=g_flatShowChassis;
    if(flat&&!oldMode)g_beforeFlatCamera={tYaw,tPitch,g_cameraOrtho,cameraRoll};
    g_allSheetsPlaying=false;g_sheetPlayingId=0;g_instanceWorld=g_formedInstanceWorld;g_allSheetsFlat=flat;
    try{
        for(u64 id:ids)sheetPreviewSet(id,flat?0.:1.);
        if(flat){
            AABB chassis;for(size_t i=0;i<g_bodyIds.size();++i)if(!isSheetFeature(g_bodyIds[i]))
                chassis.grow(g_reports[g_instancePart[i]].bounds.transformed(g_formedInstanceWorld[i]));
            double gap=std::max(32.,chassis.valid()?chassis.diagonal()*.035:45.);
            double originX=chassis.valid()?chassis.mx.x+gap:0.,originZ=chassis.valid()?chassis.mn.z:0.,floorY=chassis.valid()?chassis.mn.y+1.:0.;
            double x=originX,z=originZ,rowDepth=0.;int column=0;
            for(size_t i=0;i<g_bodyIds.size();++i)if(isSheetFeature(g_bodyIds[i])){
                AABB local=g_reports[g_instancePart[i]].bounds;if(!local.valid())continue;
                if(column==3){column=0;x=originX;z+=rowDepth+gap;rowDepth=0.;}
                g_instanceWorld[i]=Mat4::translation({x-local.mn.x,floorY-local.mn.y,z-local.mn.z});
                x+=std::max(1.,local.extent().x)+gap;rowDepth=std::max(rowDepth,std::max(1.,local.extent().z));++column;
            }
            g_allSheetsFlat=true;selBody=-1;g_picked.clear();refreshSelectionLines();
            cameraRoll=0;tYaw=0;tPitch=1.52;g_cameraOrtho=true;
        }else{g_flatShowChassis=false;if(oldMode){cameraRoll=g_beforeFlatCamera.roll;tYaw=g_beforeFlatCamera.yaw;tPitch=g_beforeFlatCamera.pitch;g_cameraOrtho=g_beforeFlatCamera.orthographic;}}
        rebuildDisplayBounds();frameScene();g_error=false;
        bool stamped=std::any_of(ids.begin(),ids.end(),[](u64 id){auto* f=g_doc.find(id);return f&&sheetHasStamp(*f);});
        g_status=flat?(stamped?"Nominal flat reference / stamped blanks require forming development":"Unfolded sheets / exploded display layout / document unchanged"):"Formed assembly / document unchanged";
    }catch(...){
        g_instanceWorld=std::move(oldWorld);g_allSheetsFlat=oldMode;g_flatShowChassis=oldChassis;
        g_sheetProgress=std::move(oldProgress);g_sheetFlatViews=std::move(oldFlat);
        for(u64 id:ids)try{updateSheetDisplay(id);}catch(...){}
        rebuildDisplayBounds();throw;
    }
}
static void toggleAllSheetPlayback(){
    if(g_allSheetsPlaying){g_allSheetsPlaying=false;g_status="Unbend preview paused / document unchanged";return;}
    allSheetPose(false);auto ids=visibleSheetIds();
    for(u64 id:ids)if(auto* feature=g_doc.find(id);feature&&sheetHasStamp(*feature))throw std::runtime_error("Stamped sheets support exact Flat/Formed endpoints; intermediate forming strain is not modeled");
    if(std::none_of(ids.begin(),ids.end(),[](u64 id){auto* feature=g_doc.find(id);return feature&&feature->type==FeatureType::SheetMetal;}))
        throw std::runtime_error("This assembly has no authored sheet bend program");
    g_allSheetsStage=(double)ids.size();g_allSheetsTick=0;g_allSheetsPlaying=true;
    g_status="Unbending visible sheets in sequence / document unchanged";
}
static void toggleStockPreview(){
    if(g_stockMode){
        u64 id=g_stockFeatureId;auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& candidate){return candidate.id==id;});
        if(body==g_doc.evalBodies.end())throw std::runtime_error("Stock source body is unavailable");
        std::set<size_t> parts;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id)parts.insert(g_instancePart[i]);
        Mesh formed=body->tessellate(g_doc.tessOpt);
        for(size_t part:parts){g_cpuMeshes[part]=formed;g_partPicks[part]=partPickFrom(*body);uploadMesh(g_meshes[part],g_cpuMeshes[part]);uploadEdges(part);g_reports[part]=analyzeMesh(g_cpuMeshes[part]);g_bvhs[part]=MeshBVH(g_cpuMeshes[part]);}
        g_instanceWorld=g_formedInstanceWorld;g_stockMode=false;g_stockFeatureId=0;g_stockReport=arkjson::Value{};
        cameraRoll=g_beforeStockCamera.roll;tYaw=g_beforeStockCamera.yaw;tPitch=g_beforeStockCamera.pitch;g_cameraOrtho=g_beforeStockCamera.orthographic;
        rebuildDisplayBounds();frameScene();g_status="Formed tube or frame / document unchanged";return;
    }
    if(g_allSheetsFlat||g_allSheetsPlaying)allSheetPose(false);
    g_beforeStockCamera={tYaw,tPitch,g_cameraOrtho,cameraRoll};
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a tube or construction frame for nominal stock preview");
    u64 id=g_bodyIds[(size_t)selBody];Feature* feature=g_doc.find(id);
    if(!feature||(feature->type!=FeatureType::Tube&&feature->type!=FeatureType::ConstructionFrame))throw std::runtime_error("Stock preview supports a tube or construction frame");
    auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& candidate){return candidate.id==id;});
    if(body==g_doc.evalBodies.end())throw std::runtime_error("Stock source body is unavailable");
    StockPreview preview=buildStockPreview(*feature,g_doc.buildEnv(),body->nativeData);
    std::set<size_t> parts;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id)parts.insert(g_instancePart[i]);
    for(size_t part:parts){g_cpuMeshes[part]=preview.mesh;g_partPicks[part]=PartPick{};uploadMesh(g_meshes[part],g_cpuMeshes[part]);uploadEdges(part);g_reports[part]=analyzeMesh(g_cpuMeshes[part]);g_bvhs[part]=MeshBVH(g_cpuMeshes[part]);}
    Mat4 rotate=Mat4::rotX(-K_PI/2.);double cursor=0.;
    for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id){AABB bounds=g_reports[g_instancePart[i]].bounds.transformed(rotate);
        g_instanceWorld[i]=Mat4::translation({cursor-bounds.mn.x,-bounds.mn.y+4.,-bounds.center().z})*rotate;
        cursor+=std::max(1.,bounds.extent().x)+60.;}
    g_stockMode=true;g_stockFeatureId=id;g_stockReport=std::move(preview.report);cameraRoll=0;tYaw=0;tPitch=1.52;g_cameraOrtho=true;rebuildDisplayBounds();frameScene();
    g_status="Nominal straight-stock display / centerline lengths / cope trim omitted / document unchanged";
}
static void restoreSpotlight(){
    if(!g_spotlightActive){g_status="Spotlight is not active";return;}
    g_hiddenBodies=g_beforeSpotlightHidden;g_beforeSpotlightHidden.clear();g_spotlightActive=false;g_spotlightBody=-1;
    g_picked.clear();refreshSelectionLines();rebuildDisplayBounds();frameScene();g_status="Previous visibility restored / document unchanged";
}
static void setBodyHidden(bool hidden){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a body in the viewport or model explorer");
    if(g_spotlightActive)restoreSpotlight();
    if(g_hiddenBodies.size()!=g_bodyIds.size())g_hiddenBodies.assign(g_bodyIds.size(),false);
    g_hiddenBodies[(size_t)selBody]=hidden;g_picked.clear();refreshSelectionLines();rebuildDisplayBounds();frameScene();
    g_status=hidden?"Body hidden in ARK / document unchanged":"Body shown in ARK / document unchanged";
}
static void spotlightBody(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a body to Spotlight");
    int selected=selBody;if(g_spotlightActive)restoreSpotlight();
    if(g_hiddenBodies.size()!=g_bodyIds.size())g_hiddenBodies.assign(g_bodyIds.size(),false);
    g_beforeSpotlightHidden=g_hiddenBodies;g_hiddenBodies.assign(g_bodyIds.size(),true);g_hiddenBodies[(size_t)selected]=false;
    g_spotlightActive=true;g_spotlightBody=selected;g_picked.clear();refreshSelectionLines();rebuildDisplayBounds();frameScene();
    g_status="Spotlight body / Restore returns to previous visibility / document unchanged";
}
static void showAllBodies(){
    g_hiddenBodies.assign(g_bodyIds.size(),false);g_beforeSpotlightHidden.clear();g_spotlightActive=false;g_spotlightBody=-1;
    g_picked.clear();refreshSelectionLines();rebuildDisplayBounds();frameScene();g_status="All bodies shown / document unchanged";
}
static void advanceSheet(double seconds){
    if(g_allSheetsPlaying){
        g_allSheetsTick+=seconds;if(g_allSheetsTick<.05)return;
        double step=g_allSheetsTick;g_allSheetsTick=0;auto ids=visibleSheetIds();g_allSheetsStage=std::max(0.,g_allSheetsStage-step*1.1);
        try{for(size_t i=0;i<ids.size();++i){auto* feature=g_doc.find(ids[i]);double progress=clamp(g_allSheetsStage-(double)i,0.,1.);
                if(feature&&feature->type==FeatureType::SourceSheet)progress=progress>.5?1.:0.;
                if(std::abs(progress-sheetDisplayProgress(ids[i]))>.01)sheetPreviewSet(ids[i],progress);}
            if(g_allSheetsStage<=0.)allSheetPose(true);
        }catch(const std::exception& error){g_allSheetsPlaying=false;g_error=true;g_status=error.what();}
        return;
    }
    if(!g_sheetPlayingId)return;
    auto* selected=selectedSheet();if(!selected||selected->id!=g_sheetPlayingId||selected->type!=FeatureType::SheetMetal){g_sheetPlayingId=0;return;}
    double progress=std::min(1.,sheetDisplayProgress(selected->id)+seconds*.22);
    try{sheetPreviewSet(selected->id,progress);if(progress>=1)g_sheetPlayingId=0;}
    catch(const std::exception& error){g_sheetPlayingId=0;g_error=true;g_status=error.what();}
}
static void updateMotionDisplay(){
    if(!g_motionActive)return;if(g_allSheetsFlat||g_stockMode)throw std::runtime_error("Return to formed assembly before playing mechanical motion");
    if(g_motionRevision!=g_session.revision){std::string error;if(!buildAssemblyMotion(g_doc,g_motionProgram,error))throw std::runtime_error(error);g_motionRevision=g_session.revision;}
    bool ok=false;double speed=evalExpr(g_motionRpmExpression,g_doc.buildEnv(),&ok);if(!ok||!std::isfinite(speed))throw std::runtime_error("Invalid motion speed expression");g_motionRpm=speed;
    g_motionReport=inspectAssemblyMotion(g_motionProgram,g_motionDriver,g_motionRpm,g_motionTime);const auto* instances=g_motionReport.find("instances");if(!instances||instances->t!=arkjson::Value::Arr)throw std::runtime_error("Mechanical motion did not return instance frames");
    g_instanceWorld=g_formedInstanceWorld;for(const auto& row:instances->arr){const auto* moving=row.find("moving"),*node=row.find("node"),*world=row.find("world_column_major");if(!moving||!moving->b||!node||!world||world->t!=arkjson::Value::Arr||world->arr.size()!=16)continue;Mat4 pose;for(size_t k=0;k<16;++k){if(world->arr[k].t!=arkjson::Value::Num||!std::isfinite(world->arr[k].num))throw std::runtime_error("Mechanical motion returned an invalid frame");pose.m[k]=world->arr[k].num;}for(size_t i=0;i<g_instanceIds.size();++i)if(g_instanceIds[i]==(u64)node->num)g_instanceWorld[i]=pose;}
    rebuildDisplayBounds();refreshSelectionLines();
}
static void advanceMotion(double seconds){if(!g_motionActive)return;if(g_motionPlaying)g_motionTime+=seconds;try{updateMotionDisplay();}catch(const std::exception& error){g_motionPlaying=false;g_motionActive=false;g_instanceWorld=g_formedInstanceWorld;g_error=true;g_status=error.what();}}
static u64 motionInstanceId(const std::string& name){u64 result=0;for(const auto& node:g_doc.assembly)if(node.name==name||std::to_string(node.id)==name){if(result)throw std::runtime_error("Instance name is ambiguous");result=node.id;}if(!result)throw std::runtime_error("Select an existing assembly instance name or ID");return result;}
static void frameScene(bool animation){
    AABB b=g_sceneBounds;
    if(auto* f=selectedNotchedTube()){
        auto& cache=g_notchViews.at(f->id);b=AABB{};
        // A fixed envelope keeps the full rigid-hinge animation inside the view.
        for(int step=0;step<=(animation?10:0);++step){auto transforms=notchFoldTransforms(cache.preview,animation?step/10.:g_foldProgress);for(size_t i=0;i<transforms.size();++i)b.grow(cache.bounds[i].transformed(g_instanceWorld[selBody]*transforms[i]));}
    }
    if(auto* f=selectedSourceTube()){
        auto& cache=g_sourceViews.at(f->id);b=AABB{};
        for(int step=0;step<=(animation?10:0);++step){double progress=animation?step/10.:g_foldProgress;auto transforms=sourceFoldTransforms(cache.preview,progress);
            for(size_t i=0;i<cache.preview.rigid.size();++i)b.grow(cache.rigidBounds[i].transformed(g_instanceWorld[selBody]*transforms[i]));
            for(size_t i=0;i<cache.preview.bends.size();++i){auto mesh=sourceHingeMesh(cache.preview.bends[i],clamp(progress*cache.preview.bends.size()-i,0.,1.));b.grow(analyzeMesh(mesh).bounds.transformed(g_instanceWorld[selBody]*transforms[i]));}
        }
    }
    if(!b.valid()&&g_sketchBounds.valid())b=g_sketchBounds;
    if(b.valid()){
        tTarget=b.center();
        auto fitFrame=cameraFrame(tYaw,tPitch,cameraRoll);Vec3 outward=fitFrame.outward,right=fitFrame.right,up=fitFrame.up;
        const double focalPixels=(double)viewH/(2.*std::tan(35.*K_PI/360.));
        const double limitX=std::max(45.,.92*(viewW*.5-36.));
        const bool layoutOverview=g_allSheetsFlat||g_stockMode;
        const double biasPixels=0.;
        const bool suspensionSelection=selBody>=0&&(size_t)selBody<g_bodyIds.size()&&g_doc.find(g_bodyIds[(size_t)selBody])&&g_doc.find(g_bodyIds[(size_t)selBody])->type==FeatureType::Suspension;
        const double lowerControls=suspensionSelection?162.:(selectedNotchedTube()||selectedSourceTube()||selectedSheet())?112.:50.;
        const double limitY=std::max(45.,.93*(viewH*.5-(layoutOverview?55.:std::max(44.,lowerControls))));
        auto finishFit=[&](double distance){tDist=distance;if(layoutOverview)tTarget=tTarget+up*(biasPixels*distance/focalPixels);};
        // The enclosing box of a winged or sparse assembly has large empty
        // corners. Fit the actual display vertices once, analytically, so those
        // corners do not shrink the model. Animated hinges/canvases keep their
        // existing conservative envelope below. This never changes CAD geometry.
        if(!animation&&!selectedNotchedTube()&&!selectedSourceTube()&&
           g_doc.canvases.empty()&&!g_showSketches&&!g_projectionPreview.active&&!g_bodyIds.empty()){
            double required=2.;
            // Centre the projected silhouette, not the 3D bounding box. Bent
            // sheets and sparse assemblies otherwise sit low and fit too small.
            for(int fitPass=0;fitPass<6;++fitPass){required=2.;
            for(size_t instance=0;instance<g_bodyIds.size();++instance){if(!displayInstance(instance))continue;
                const Mesh& mesh=g_cpuMeshes[g_instancePart[instance]];
                const Mat4& world=g_instanceWorld[instance];
                for(size_t vertex=0;vertex+2<mesh.pos.size();vertex+=3){
                    Vec3 point=world.transformPoint({mesh.pos[vertex],mesh.pos[vertex+1],mesh.pos[vertex+2]})-tTarget;
                    double offset=g_cameraOrtho?0.:point.dot(outward);
                    required=std::max(required,offset+focalPixels*std::abs(point.dot(right))/limitX);
                    required=std::max(required,offset+focalPixels*std::abs(point.dot(up))/limitY);
                    required=std::max(required,(point.dot(outward)+.1)/.997);
                }
            }
            if(fitPass==5)break;
            double minX=INFINITY,minY=INFINITY,maxX=-INFINITY,maxY=-INFINITY;
            for(size_t instance=0;instance<g_bodyIds.size();++instance){if(!displayInstance(instance))continue;
                const Mesh& mesh=g_cpuMeshes[g_instancePart[instance]];const Mat4& world=g_instanceWorld[instance];
                for(size_t vertex=0;vertex+2<mesh.pos.size();vertex+=3){Vec3 point=world.transformPoint({mesh.pos[vertex],mesh.pos[vertex+1],mesh.pos[vertex+2]})-tTarget;
                    double depth=g_cameraOrtho?required:required-point.dot(outward);if(depth<=0)continue;
                    double x=focalPixels*point.dot(right)/depth,y=focalPixels*point.dot(up)/depth;
                    minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);}}
            if(!std::isfinite(minX)||!std::isfinite(minY))break;
            double dx=(minX+maxX)*.5,dy=(minY+maxY)*.5;if(std::hypot(dx,dy)<.25)break;
            tTarget=tTarget+(right*dx+up*dy)*(required/focalPixels);
            }
            finishFit(required);return;
        }
        auto fits=[&](double distance){
            for(int ix=0;ix<2;++ix)for(int iy=0;iy<2;++iy)for(int iz=0;iz<2;++iz){
                Vec3 corner{ix?b.mx.x:b.mn.x,iy?b.mx.y:b.mn.y,iz?b.mx.z:b.mn.z};Vec3 relative=corner-tTarget;
                double depth=g_cameraOrtho?distance:distance-relative.dot(outward);
                if(depth<=std::max(.05,distance*.003))return false;
                double px=focalPixels*relative.dot(right)/depth,py=focalPixels*relative.dot(up)/depth;
                if(std::abs(px)>limitX||std::abs(py)>limitY)return false;
            }
            return true;
        };
        double low=2.,high=std::max(10.,b.diagonal()*8.);
        while(!fits(high)&&high<1e9)high*=2.;
        for(int iteration=0;iteration<42;++iteration){double middle=(low+high)*.5;if(fits(middle))high=middle;else low=middle;}
        finishFit(high);
    }
}
static Feature* selectedSuspension(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())return nullptr;
    Feature* feature=g_doc.find(g_bodyIds[(size_t)selBody]);return feature&&feature->type==FeatureType::Suspension?feature:nullptr;
}
static std::pair<double,double> suspensionRange(const Feature& feature,int control){
    const auto* limits=feature.nativeSpec.find("limits");const char* lowKey=control==0?"jounce_min_mm":"rack_min_mm";
    const char* highKey=control==0?"jounce_max_mm":"rack_max_mm";
    if(control==2)return {0.,360.};
    auto value=[&](const char* key,double fallback){const auto* field=limits?limits->find(key):nullptr;if(!field)return fallback;
        bool ok=false;double evaluated=evalExpr(field->t==arkjson::Value::Str?field->str:field->dump(),g_doc.buildEnv(),&ok);
        return ok&&std::isfinite(evaluated)?evaluated:fallback;};
    double low=value(lowKey,control==0?-35.:-12.),high=value(highKey,control==0?35.:12.);
    return high>low?std::pair<double,double>{low,high}:std::pair<double,double>{-1.,1.};
}
static void updateSuspensionPreview(u64 id,const SuspensionState& state){
    Feature* feature=g_doc.find(id);if(!feature||feature->type!=FeatureType::Suspension)throw std::runtime_error("Select a suspension feature");
    SuspensionPreview next=buildSuspensionPreview(*feature,g_doc.buildEnv(),state);
    auto& cache=g_suspViews[id];while(cache.gpu.size()>next.parts.size()){releaseMesh(cache.gpu.back());cache.gpu.pop_back();}
    while(cache.gpu.size()<next.parts.size())cache.gpu.emplace_back();
    for(size_t part=0;part<next.parts.size();++part)uploadMesh(cache.gpu[part],next.parts[part].mesh);
    cache.state=state;cache.preview=std::move(next);
    std::set<size_t> changed;for(size_t instance=0;instance<g_bodyIds.size();++instance)if(g_bodyIds[instance]==id)changed.insert(g_instancePart[instance]);
    for(size_t part:changed){g_cpuMeshes[part]=cache.preview.mesh;uploadMesh(g_meshes[part],g_cpuMeshes[part]);g_reports[part]=analyzeMesh(g_cpuMeshes[part]);g_bvhs[part]=MeshBVH(g_cpuMeshes[part]);}
    rebuildDisplayBounds();
}
static void setSuspensionControl(int control,double fraction){
    Feature* feature=selectedSuspension();if(!feature)throw std::runtime_error("Select a suspension in the model explorer");
    auto range=suspensionRange(*feature,control);double value=range.first+(range.second-range.first)*clamp(fraction,0.,1.);
    SuspensionState state=g_suspViews[feature->id].state;
    if(control==0)state.jounceMm=value;else if(control==1)state.rackMm=value;else state.wheelSpinDeg=value;
    updateSuspensionPreview(feature->id,state);
    const auto* toe=g_suspViews[feature->id].preview.report.find("toe_deg");char message[128];
    std::snprintf(message,sizeof message,"Suspension: jounce %.1f mm / rack %.1f mm / wheel %.0f deg%s",state.jounceMm,state.rackMm,state.wheelSpinDeg,toe?" / toe solved":"");
    g_status=message;
}
static void advanceSuspension(double seconds){
    if(!g_suspSpinPlaying)return;Feature* feature=selectedSuspension();if(!feature){g_suspSpinPlaying=false;return;}
    g_suspTick+=seconds;g_suspClock+=seconds;if(g_suspTick<.08)return;double step=g_suspTick;g_suspTick=0;
    try{SuspensionState state=g_suspViews[feature->id].state;auto jounce=suspensionRange(*feature,0),rack=suspensionRange(*feature,1);
        state.jounceMm=(jounce.first+jounce.second)*.5+(jounce.second-jounce.first)*.42*std::sin(g_suspClock*1.2);
        state.rackMm=(rack.first+rack.second)*.5+(rack.second-rack.first)*.38*std::sin(g_suspClock*.8+.5);
        state.wheelSpinDeg=std::fmod(state.wheelSpinDeg+step*140.,360.);updateSuspensionPreview(feature->id,state);}
    catch(const std::exception& error){g_suspSpinPlaying=false;g_error=true;g_status=error.what();}
}
static void refreshDocumentUsage(){
    const UsageTotals totals=usageTotals(g_doc);g_assistantUsageDocument={};
    g_assistantUsageDocument.inputTokens=totals.inputTokens;g_assistantUsageDocument.outputTokens=totals.outputTokens;
    g_assistantUsageDocument.totalTokens=totals.totalTokens;g_assistantUsageDocument.cachedInputTokens=totals.cachedInputTokens;
    g_assistantUsageDocument.reasoningTokens=totals.reasoningTokens;
    g_assistantUsageDocument.reportedResponses=(unsigned)std::min<u64>(totals.reportedResponses,std::numeric_limits<unsigned>::max());
    g_assistantUsageDocument.missingResponses=(unsigned)std::min<u64>(totals.missingResponses,std::numeric_limits<unsigned>::max());
}
static std::string tokenBadgeLabel(){
    return g_assistantUsageDocument.reportedResponses?"TOKENS "+std::to_string(g_assistantUsageDocument.totalTokens):"TOKENS N/A";
}
static void applyUsageReceipt(const UsageReceipt& receipt){
    std::string error;UsageRecordResult result=updateUsageSnapshot(g_doc,receipt,&error);
    if(result==UsageRecordResult::Rejected){g_status="Usage receipt rejected: "+error;return;}
    refreshDocumentUsage();
}
static void flushPendingUsage(){if(g_busy||!g_pendingUsageReceipt)return;applyUsageReceipt(*g_pendingUsageReceipt);g_pendingUsageReceipt.reset();}
static void recordAssistantUsageSnapshot(const AssistantUsage& usage){
    if(g_assistantReceiptId.empty()||(!usage.reportedResponses&&!usage.missingResponses))return;
    UsageReceipt receipt;receipt.id=g_assistantReceiptId;receipt.source="studio_assistant";receipt.provenance="provider_response";receipt.model=g_assistantReceiptModel;
    receipt.inputTokens=usage.inputTokens;receipt.outputTokens=usage.outputTokens;receipt.totalTokens=usage.totalTokens;
    receipt.cachedInputTokens=usage.cachedInputTokens;receipt.reasoningTokens=usage.reasoningTokens;
    receipt.reportedResponses=usage.reportedResponses;receipt.missingResponses=usage.missingResponses;
    g_currentUsageReceipt=receipt;
    if(g_busy){g_pendingUsageReceipt=receipt;++g_deferredUsageCount;}else applyUsageReceipt(receipt);
}
static void invalidateRemovedObjectTarget(){
    if(!g_pendingObjectTarget||!g_pendingObjectTarget->invalidated.empty())return;
    auto& target=*g_pendingObjectTarget;
    if(target.epoch!=g_objectDocumentEpoch)return; // Replacement drawings reject through their epoch.
    if(!g_doc.find(target.feature)){target.invalidated="The selected object was deleted. Select it again before sending.";return;}
    if(!target.node)return;
    auto node=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& item){return item.id==target.node;});
    if(node==g_doc.assembly.end()){target.invalidated="The selected occurrence was removed or replaced. Select it again before sending.";return;}
    std::vector<AssemblyPlacement> placements;std::string error;
    if(!resolveAssembly(g_doc,placements,error)||std::none_of(placements.begin(),placements.end(),[&](const AssemblyPlacement& p){return p.node==target.node&&p.copy==target.copy&&p.feature==target.feature;}))target.invalidated="The selected pattern copy was removed or replaced. Select it again before sending.";
}
static std::string g_lastFailureId;
static arkjson::Value captureViewportSnapshot(const arkjson::Value& args);
static void resetDatumViewState();
static void sanitizeDatumViewState();
static void clearDatumSelection();
static arkjson::Value runTool(const std::string& tool,const arkjson::Value& args){
    const bool containerWrite=tool=="design_containers"&&args.find("action")&&args.find("action")->asStr()=="merge"&&!(args.find("preview")&&args.find("preview")->t==arkjson::Value::Bool&&args.find("preview")->b);
    size_t previousVisibleBodies=g_bodyIds.size();
    flushPendingUsage();
    auto effectiveArgs=args;static const std::set<std::string> mutations{"doc_apply","doc_new","doc_load","doc_undo","doc_redo","iteration_restore","import_step","import_sheet_step","import_mesh","import_cad","design_containers","image_asset","plane_authoring","library_insert"};const bool documentWrite=mutations.count(tool)&&(tool!="design_containers"||containerWrite)&&(tool!="image_asset"||!args.find("action")||args.find("action")->asStr()!="inspect");if(documentWrite&&!effectiveArgs.find("actor")){using V=arkjson::Value;V actor=V::makeObj();actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));effectiveArgs.set("actor",actor);}
    bool potentiallySlow=tool=="plane_authoring"||tool=="import_cad"||tool=="cad_inspect"||tool=="design_containers"||tool.rfind("library_",0)==0||tool=="failure_replay"||tool=="failure_export"||tool=="manufacturing_inspect"||tool=="doc_load"||tool=="iteration_restore"||tool=="doc_apply"||tool=="doc_preview"||tool=="design_solve"||tool=="placement_plan"||tool=="pattern_plan"||tool=="query_batch"||tool=="geometry_query"||tool=="physics_inspect"||tool=="native_inspect"||tool=="step_inspect"||tool=="reference_inspect"||tool=="assembly_info"||tool=="kernel_info"||tool=="doc_undo"||tool=="doc_redo"||tool=="import_step"||tool=="import_sheet_step"||tool=="import_mesh"||tool=="export_step"||tool=="export_iges"||tool=="export_mesh"||tool=="export_flat_dxf"||tool=="bom_export_parts"||tool=="reference_navigate"||tool=="selection_set";
    if(tool=="doc_apply"&&!effectiveArgs.find("checks")&&metadataOnlyApply(g_doc,effectiveArgs))potentiallySlow=false;
    bool viewportSnapshot=tool=="snapshot"&&!effectiveArgs.find("candidate_id")&&effectiveArgs.find("view")&&effectiveArgs.find("view")->asStr()=="viewport";
    auto result=viewportSnapshot?captureViewportSnapshot(effectiveArgs):potentiallySlow&&g_uiReady&&(!g_headless||g_busyTestMode)?callSessionWithProgress(tool,effectiveArgs):g_session.call(tool,effectiveArgs);g_error=result.find("isError")->b;
    if(auto* data=result.find("structuredContent"))if(auto* id=data->find("failure_id");id&&id->t==arkjson::Value::Str)g_lastFailureId=id->str;
    u64 loadedUsageSerial=g_doc.usage.serial;
    if(!g_error&&tool=="selection_set"){clearDatumSelection();const auto& selected=*result.find("structuredContent")->find("selection");g_picked.clear();SelectionItem item;item.feature=(u64)selected.find("feature")->num;item.kind=selected.find("kind")->str;item.name=selected.find("reference")->str;item.instance=(u64)selected.find("instance")->num;item.copy=(int)selected.find("copy")->num;g_picked.push_back(item);g_pickKind=item.kind=="face"?1:2;g_explorerFeature=item.feature;selBody=-1;rebuildScene();bool displayed=false;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==item.feature&&(!item.instance||(g_instanceIds[i]==item.instance&&g_instanceCopies[i]==item.copy))){selBody=(int)i;displayed=i>=g_hiddenBodies.size()||!g_hiddenBodies[i];break;}refreshSelectionLines();auto data=*result.find("structuredContent");data.set("view_state_applied",arkjson::Value::makeBool(true));data.set("selection_visible",arkjson::Value::makeBool(displayed));if(!displayed)data.set("visibility_note",arkjson::Value::makeStr("Reference selected in history; definition/occurrence is hidden or consumed by a later operation"));result.set("structuredContent",data);auto content=*result.find("content");for(auto& block:content.arr)if(block.find("type")&&block.find("type")->asStr()=="text")block.set("text",arkjson::Value::makeStr(data.dump()));result.set("content",content);}
    if(!g_error&&documentWrite){clearProjectionPreview();cancelComponentMove(false);}
    if(g_error)g_status=result.find("structuredContent")->find("error")->asStr();
    else g_status=tool+" completed  /  revision "+std::to_string(g_session.revision);
    if(!g_error&&(tool=="doc_load"||tool=="iteration_restore"||tool=="doc_new"||tool=="import_step"||tool=="import_sheet_step")){g_sheetFlatViews.clear();g_sheetProgress.clear();g_sheetPlayingId=0;}
    bool appearanceOnly=false;
    if(tool=="doc_apply")if(auto* content=result.find("structuredContent"))if(auto* flag=content->find("geometry_recomputed"))appearanceOnly=flag->t==arkjson::Value::Bool&&!flag->b;
    if(tool=="doc_apply")if(auto* content=result.find("structuredContent"))if(auto* prepared=content->find("applied_prepared");prepared&&prepared->b){const auto* changed=content->find("geometry_changed");appearanceOnly=changed&&changed->t==arkjson::Value::Bool&&!changed->b;}
    if(!g_error&&(tool=="doc_new"||tool=="doc_load"||tool=="iteration_restore"))resetDatumViewState();else if(!g_error&&documentWrite)sanitizeDatumViewState();
    if(!g_error&&!appearanceOnly&&(tool=="library_insert"||tool=="doc_apply"||tool=="doc_load"||tool=="iteration_restore"||tool=="doc_undo"||tool=="doc_redo"||tool=="import_step"||tool=="import_sheet_step"||tool=="import_mesh"||tool=="import_cad"||tool=="plane_authoring"||containerWrite||tool=="doc_new"))rebuildScene();
    if(!g_error&&tool=="doc_apply"){if(auto* commands=args.find("commands");commands&&commands->t==arkjson::Value::Arr)for(const auto& command:commands->arr){auto* op=command.find("op"),*name=command.find("name");if(op&&name&&op->t==arkjson::Value::Str&&name->t==arkjson::Value::Str&&op->str.rfind("sketch.",0)==0&&op->str!="sketch.remove"&&g_doc.findSketch(name->str)){g_activeSketch=name->str;g_activePlane=g_doc.findSketch(name->str)->sk.plane;g_showSketches=true;rebuildSketchOverlay();}}
        if(!previousVisibleBodies&&!g_bodyIds.empty())frameScene();}
    if(!g_error&&(!appearanceOnly)&&(tool=="doc_apply"||tool=="plane_authoring"||tool=="doc_undo"||tool=="doc_redo")){if(tool=="doc_apply"&&!g_activeToolRole.empty())if(auto* commands=args.find("commands"))for(const auto& command:commands->arr){auto* op=command.find("op"),*name=command.find("name");if(op&&name&&op->t==arkjson::Value::Str&&name->t==arkjson::Value::Str&&op->str.rfind("sketch.",0)==0&&op->str!="sketch.remove"&&g_doc.findSketch(name->str))g_committedRoleSketches[name->str].role=g_activeToolRole;}rebuildRoleSketches();}
    if(!g_error&&(tool=="doc_load"||tool=="doc_new"||tool=="iteration_restore")){
        ++g_objectDocumentEpoch;
        g_draftViews.clear();g_committedRoleSketches.clear();g_liveStepLabel.clear();g_liveCompletedSteps=0;
        g_motionActive=false;g_motionPlaying=false;g_motionRevision=std::numeric_limits<u64>::max();
        g_closedAssemblyGroups.clear();g_collapsedFeatures.clear();
        g_explorerFeature=0;g_viewportContainer=0;g_selectedExplorerEntity.reset();g_treeScroll=0;g_treeSelectedZone=-1;
        if(g_ownAssistantTool&&g_currentUsageReceipt)applyUsageReceipt(*g_currentUsageReceipt);
        if(g_ownAssistantTool&&g_teamStarted)for(const auto& receipt:g_teamReceipts)if(receipt)applyUsageReceipt(*receipt);
        if(!g_ownAssistantTool){*g_teamCancel=true;g_agentTeam.cancel();g_teamStarted=false;resetTeamView();g_pendingUsageReceipt.reset();g_currentUsageReceipt.reset();g_assistantReceiptId.clear();g_assistantUsageRun={};g_assistantPerformanceRun={};g_assistantUsageRunCounted=true;++g_documentEpoch;*g_assistantCancel=true;g_conversation=arkjson::Value::makeArr();g_chatLog.clear();g_chatLog.push_back({"ARK","Document changed. Describe what you want to create."});}
        if(tool=="doc_new"){g_path="Untitled.ark";g_savedRevision=g_session.revision;g_savedUsageSerial=0;g_savedActivitySerial=0;g_activeSketch.clear();g_activePlane="XY";}
        g_foldProgress=selectedSourceTube()?1.:0.;if(tool=="doc_load"||tool=="iteration_restore")selBody=-1;frameScene();
    }
    if(!g_error&&documentWrite)invalidateRemovedObjectTarget();
    flushPendingUsage();refreshDocumentUsage();
    if(!g_error&&(tool=="doc_save"||tool=="doc_load")){if(auto* path=args.find("path"))g_path=path->asStr();g_savedRevision=g_session.revision;g_savedUsageSerial=tool=="doc_load"?loadedUsageSerial:g_doc.usage.serial;g_savedActivitySerial=agentActivitySerial(g_doc);if(effectiveArgs.find("actor")&&effectiveArgs.find("actor")->find("kind")&&effectiveArgs.find("actor")->find("kind")->asStr()=="agent"&&g_savedActivitySerial)--g_savedActivitySerial;}
    return result;
}
static void applyCommand(arkjson::Value command){
    using V=arkjson::Value;V a=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);
    if(command.find("op")&&command.find("op")->asStr()=="feature.add"&&!g_doc.assembly.empty()){
        V instance=V::makeObj();instance.set("op",V::makeStr("assembly.add"));instance.set("feature",V::makeNum((double)g_doc.nextFeatureId_));instance.set("name",V::makeStr(command.find("name")?command.find("name")->asStr():command.find("type")->asStr()));commands.arr.push_back(instance);
    }
    a.set("commands",commands);V actor=V::makeObj();actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));a.set("actor",actor);a.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",a);
}
static void addFeat(FeatureType type, std::initializer_list<std::pair<const char*,std::string>> ps,std::vector<u64> inputs={},int mode=0){
    using V=arkjson::Value;V c=V::makeObj(),p=V::makeObj(),in=V::makeArr();c.set("op",V::makeStr("feature.add"));c.set("type",V::makeStr(featureTypeName(type)));
    for(auto& kv:ps)p.set(kv.first,V::makeStr(kv.second));for(auto x:inputs)in.arr.push_back(V::makeNum((double)x));
    c.set("p",p);c.set("inputs",in);c.set("mode",V::makeNum(mode));applyCommand(c);
}
// Native modal editors remain clients of the same transactional agent API.
static HWND g_edit=nullptr,g_dialog=nullptr;static bool g_dialogDone=false,g_dialogAccept=false;
static int g_dialogSmoke=0;static std::string g_dialogShot,g_dialogReplacement="32";static std::vector<std::string> g_dialogOverrides;
static std::deque<std::vector<std::string>> g_dialogOverrideQueue;
static HBRUSH dialogBrush=CreateSolidBrush(RGB(29,31,36)),fieldBrush=CreateSolidBrush(RGB(42,45,52)),chatBrush=CreateSolidBrush(RGB(14,17,20));
static LRESULT CALLBACK editorProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND&&(LOWORD(w)==1||LOWORD(w)==2)){g_dialogAccept=LOWORD(w)==1;g_dialogDone=true;return 0;}
    if(m==WM_CLOSE){g_dialogDone=true;return 0;}
    if(m==WM_TIMER&&w==901){
        KillTimer(h,901);
        if(g_dialogOverrides.empty()&&!g_dialogOverrideQueue.empty()){g_dialogOverrides=std::move(g_dialogOverrideQueue.front());g_dialogOverrideQueue.pop_front();}
        for(size_t i=0;i<g_dialogOverrides.size();++i){auto control=GetDlgItem(h,3+(int)i);char kind[32]{};GetClassNameA(control,kind,sizeof kind);if(std::strcmp(kind,"Button")==0)SendMessage(control,BM_SETCHECK,g_dialogOverrides[i]=="1"?BST_CHECKED:BST_UNCHECKED,0);else if(std::strcmp(kind,"ComboBox")==0){auto index=SendMessageA(control,CB_FINDSTRINGEXACT,-1,(LPARAM)g_dialogOverrides[i].c_str());if(index!=CB_ERR)SendMessage(control,CB_SETCURSEL,index,0);else SetWindowTextA(control,g_dialogOverrides[i].c_str());}else SetWindowTextA(control,g_dialogOverrides[i].c_str());}g_dialogOverrides.clear();
        if(!g_dialogShot.empty()){
            RECT r{};GetWindowRect(h,&r);int width=r.right-r.left,height=r.bottom-r.top;
            HDC screen=GetDC(h),dc=CreateCompatibleDC(screen);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
            void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);
            if(PrintWindow(h,dc,0)){Image img;img.w=width;img.h=height;img.px.resize((size_t)width*height*3);for(size_t k=0;k<(size_t)width*height;++k){auto* src=(unsigned char*)bits+4*k;img.px[3*k]=src[2];img.px[3*k+1]=src[1];img.px[3*k+2]=src[0];}writePNG(g_dialogShot.c_str(),img);}
            SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(h,screen);g_dialogShot.clear();
        }
        if(g_dialogSmoke==1)SetWindowTextA(g_edit,g_dialogReplacement.c_str());
        SendMessage(h,WM_COMMAND,g_dialogSmoke==2?2:1,0);return 0;
    }
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT){SetTextColor((HDC)w,RGB(223,226,234));SetBkColor((HDC)w,m==WM_CTLCOLOREDIT?RGB(42,45,52):RGB(29,31,36));return (LRESULT)(m==WM_CTLCOLOREDIT?fieldBrush:dialogBrush);}
    if(m==WM_DRAWITEM){auto* d=(DRAWITEMSTRUCT*)l;bool primary=d->CtlID==1;
        HBRUSH br=CreateSolidBrush(primary?RGB(193,201,216):RGB(55,59,68));FillRect(d->hDC,&d->rcItem,br);DeleteObject(br);
        SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,primary?RGB(24,27,33):RGB(231,233,239));
        char text[80];GetWindowTextA(d->hwndItem,text,sizeof text);DrawTextA(d->hDC,text,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(d->itemState&ODS_FOCUS){RECT r=d->rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d->hDC,&r);}return TRUE;
    }
    return DefWindowProcA(h,m,w,l);
}
struct EditorField { std::string name,value;bool toggle=false;std::vector<std::string> choices;bool editableChoice=false; };
static bool editFields(const char* title,const char* hint,std::vector<EditorField>& fields,bool multiline=false,const char* confirm="Apply",HBITMAP previewBitmap=nullptr,bool readOnly=false){
    WNDCLASSA wc{};wc.lpfnWndProc=editorProc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="ARK_EDITOR";wc.hbrBackground=dialogBrush;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&wc);
    bool mateColumns=!multiline&&((fields.size()==10&&(std::string(title)=="Add assembly mate"||std::string(title)=="Edit assembly mate"))||(fields.size()==9&&(std::string(title)=="Add drawing dimension"||std::string(title)=="Edit drawing dimension")));
    const int previewHeight=previewBitmap?316:0;RECT r;GetWindowRect(g_window,&r);int width=multiline||previewBitmap?840:mateColumns?980:620,height=(multiline?520:190+(int)(mateColumns?5:fields.size())*62)+previewHeight;
    RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    int ow=bounds.right-bounds.left,oh=bounds.bottom-bounds.top;
    g_dialog=CreateWindowExA(WS_EX_DLGMODALFRAME,"ARK_EDITOR",title,WS_CAPTION|WS_SYSMENU,r.left+(r.right-r.left-ow)/2,r.top+(r.bottom-r.top-oh)/2,ow,oh,g_window,nullptr,wc.hInstance,nullptr);
    BOOL dark=TRUE;DwmSetWindowAttribute(g_dialog,20,&dark,sizeof dark);
    HFONT font=CreateFontA(-17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
    HFONT mono=CreateFontA(-17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Consolas");
    auto control=[&](const char* cls,const char* text,DWORD style,int x,int y,int w,int h,int id,HFONT f){HWND c=CreateWindowExA(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,g_dialog,(HMENU)(INT_PTR)id,wc.hInstance,nullptr);SendMessage(c,WM_SETFONT,(WPARAM)f,TRUE);return c;};
    control("STATIC",title,0,26,22,width-52,27,0,font);control("STATIC",hint,0,26,58,width-52,44,0,font);
    if(previewBitmap){auto image=control("STATIC","",SS_BITMAP|SS_CENTERIMAGE,28,108,width-56,300,9010,font);SendMessage(image,STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)previewBitmap);}
    std::vector<HWND> edits;
    for(size_t i=0;i<fields.size();++i){int y=110+previewHeight+(int)(mateColumns?i%5:i)*62,x=mateColumns?(int)(i/5)*480:0;
        if(!multiline)control("STATIC",fields[i].name.c_str(),0,x+28,y+7,205,28,0,font);
        HWND e=fields[i].toggle?control("BUTTON","",WS_TABSTOP|BS_AUTOCHECKBOX,x+235,y,40,33,3+(int)i,font):!fields[i].choices.empty()?control("COMBOBOX","",WS_TABSTOP|WS_VSCROLL|(fields[i].editableChoice?CBS_DROPDOWN:CBS_DROPDOWNLIST),x+235,y,mateColumns?217:width-263,240,3+(int)i,font):control("EDIT",fields[i].value.c_str(),WS_TABSTOP|WS_BORDER|((multiline&&readOnly)?0:ES_AUTOHSCROLL)|(multiline?ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN:0),multiline?26:x+235,y,multiline?width-52:mateColumns?217:width-263,multiline?height-184:33,3+(int)i,mono);
        if(!fields[i].choices.empty()){for(const auto& choice:fields[i].choices)SendMessageA(e,CB_ADDSTRING,0,(LPARAM)choice.c_str());auto selected=SendMessageA(e,CB_FINDSTRINGEXACT,-1,(LPARAM)fields[i].value.c_str());if(selected!=CB_ERR)SendMessage(e,CB_SETCURSEL,selected,0);else if(fields[i].editableChoice)SetWindowTextA(e,fields[i].value.c_str());}
        if(!fields[i].choices.empty()&&std::string(title)=="Local models / Ollama"){HDC dc=GetDC(e);auto previousFont=SelectObject(dc,font);int dropped=width-263;for(const auto& choice:fields[i].choices){SIZE measured{};GetTextExtentPoint32A(dc,choice.c_str(),(int)choice.size(),&measured);dropped=std::max(dropped,(int)measured.cx+40);}SelectObject(dc,previousFont);ReleaseDC(e,dc);MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfo(MonitorFromWindow(g_dialog,MONITOR_DEFAULTTONEAREST),&monitor);SendMessage(e,CB_SETDROPPEDWIDTH,std::min(dropped,(int)(monitor.rcWork.right-monitor.rcWork.left)-32),0);}
        if(fields[i].toggle)SendMessage(e,BM_SETCHECK,fields[i].value=="1"?BST_CHECKED:BST_UNCHECKED,0);
        if(readOnly&&!fields[i].toggle&&fields[i].choices.empty())SendMessage(e,EM_SETREADONLY,TRUE,0);
        SendMessage(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));edits.push_back(e);
    }
    control("STATIC",readOnly?"Read-only report  /  Esc to close":multiline?"Ctrl+Enter to apply  /  Esc to cancel":"Enter to apply  /  Esc to cancel",0,26,height-52,width-290,34,0,font);
    control("BUTTON","Cancel",WS_TABSTOP|BS_OWNERDRAW,width-248,height-58,100,34,2,font);
    control("BUTTON",confirm,WS_TABSTOP|BS_OWNERDRAW,width-132,height-58,106,34,1,font);
    g_edit=edits.front();g_dialogDone=g_dialogAccept=false;EnableWindow(g_window,FALSE);ShowWindow(g_dialog,SW_SHOW);SetFocus(g_edit);SendMessage(g_edit,EM_SETSEL,0,-1);
    if(g_dialogSmoke)SetTimer(g_dialog,901,200,nullptr);
    MSG msg;while(!g_dialogDone&&GetMessage(&msg,nullptr,0,0)>0){
        char focusedClass[32]{};GetClassNameA(GetFocus(),focusedClass,sizeof focusedClass);if(std::strcmp(focusedClass,"ComboBox")==0&&SendMessage(GetFocus(),CB_GETDROPPEDSTATE,0,0)&&(msg.message==WM_KEYDOWN&&(msg.wParam==VK_ESCAPE||msg.wParam==VK_RETURN))){SendMessage(GetFocus(),CB_SHOWDROPDOWN,FALSE,0);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)&&std::strcmp(focusedClass,"Edit")==0){SendMessage(GetFocus(),EM_SETSEL,0,-1);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){g_dialogDone=true;continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&(!multiline||GetDlgCtrlID(GetFocus())<=2||(GetKeyState(VK_CONTROL)&0x8000))){g_dialogDone=true;g_dialogAccept=GetDlgCtrlID(GetFocus())!=2;continue;}
        if(!IsDialogMessage(g_dialog,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}
    }
    if(g_dialogAccept)for(size_t i=0;i<fields.size();++i){if(fields[i].toggle){fields[i].value=SendMessage(edits[i],BM_GETCHECK,0,0)==BST_CHECKED?"1":"0";continue;}int n=GetWindowTextLengthA(edits[i]);std::vector<char> b(n+1);GetWindowTextA(edits[i],b.data(),n+1);fields[i].value=b.data();}
    EnableWindow(g_window,TRUE);DestroyWindow(g_dialog);g_dialog=nullptr;DeleteObject(font);DeleteObject(mono);SetForegroundWindow(g_window);return g_dialogAccept;
}
static bool editText(const char* title,std::string& value,bool multiline=false){
    std::vector<EditorField> fields{{"Value",value}};
    bool ok=editFields(title,multiline?"Edit the program below. Changes are applied atomically through the agent API.":"Update this value, then apply to recompute the document.",fields,multiline);
    if(ok)value=fields.front().value;return ok;
}
static void fileAction(bool save,bool mesh=false);
static bool confirmDiscard(){
    if(g_session.revision==g_savedRevision&&g_doc.usage.serial==g_savedUsageSerial&&agentActivitySerial(g_doc)==g_savedActivitySerial)return true;
    int answer=MessageBoxA(g_window,"Save changes to this ARK document?","Unsaved document",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(answer==IDCANCEL)return false;if(answer==IDYES){fileAction(true);return g_session.revision==g_savedRevision&&g_doc.usage.serial==g_savedUsageSerial&&agentActivitySerial(g_doc)==g_savedActivitySerial;}return true;
}
static void fileAction(bool save,bool mesh){
    if(!save&&!confirmDiscard())return;
    char path[MAX_PATH]{};if(save)std::snprintf(path,sizeof path,"%s",mesh?"ark_export.stl":g_path.c_str());
    OPENFILENAMEA of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=path;of.nMaxFile=MAX_PATH;
    of.lpstrFilter=mesh?"STL mesh\0*.stl\0":"ARK document\0*.ark;*.arkjson\0";of.lpstrDefExt=mesh?"stl":"ark";
    of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(save?GetSaveFileNameA(&of):GetOpenFileNameA(&of)))return;
    using V=arkjson::Value;V a=V::makeObj();a.set("path",V::makeStr(path));a.set("format",V::makeStr("stl"));
    runTool(mesh?"export_mesh":save?"doc_save":"doc_load",a);
    if(!g_error&&!mesh&&!save)frameScene();
}
static void exportModelDialog(){
    char path[MAX_PATH]="ark-export.stl";
    static const char filters[]="STL display mesh\0*.stl\0OBJ display mesh\0*.obj\0FBX display mesh\0*.fbx\0STEP exact part\0*.step;*.stp\0IGES exact part\0*.iges;*.igs\0";
    OPENFILENAMEA dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=g_window;dialog.lpstrFile=path;dialog.nMaxFile=MAX_PATH;
    dialog.lpstrFilter=filters;dialog.nFilterIndex=1;dialog.lpstrTitle="Export visible mesh or selected exact part";
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
    if(!GetSaveFileNameA(&dialog))return;
    static const char* extensions[]={".stl",".obj",".fbx",".step",".iges"};
    static const char* meshFormats[]={"stl","obj","fbx"};
    unsigned index=std::clamp((unsigned)dialog.nFilterIndex,1u,5u)-1u;
    std::filesystem::path destination=path;std::string extension=destination.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});
    bool matches=extension==extensions[index]||(index==3&&extension==".stp")||(index==4&&extension==".igs");
    if(!matches)destination.replace_extension(extensions[index]);
    auto utf8=destination.u8string();std::string target(utf8.begin(),utf8.end());
    using V=arkjson::Value;V arguments=V::makeObj();arguments.set("path",V::makeStr(target));
    if(index<3){arguments.set("format",V::makeStr(meshFormats[index]));runTool("export_mesh",arguments);}
    else{
        if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select one exact part before STEP or IGES export");
        arguments.set("id",V::makeNum((double)g_bodyIds[(size_t)selBody]));runTool(index==3?"export_step":"export_iges",arguments);
    }
    if(!g_error)g_status=std::string(index<3?"Visible display mesh exported: ":"Selected exact part exported: ")+target;
}
static arkjson::Value notchFeatureEdit(const Feature& f){
    using V=arkjson::Value;V c=V::makeObj(),p=V::makeObj(),points=V::makeArr();
    c.set("op",V::makeStr("feature.edit"));c.set("id",V::makeNum((double)f.id));
    for(auto& entry:f.p)p.set(entry.first,V::makeStr(entry.second));
    for(auto& n:f.notches){V point=V::makeObj();point.set("station",V::makeStr(n.station));point.set("angle",V::makeStr(n.angle));point.set("face",V::makeStr(n.face));points.arr.push_back(point);}
    c.set("p",p);c.set("notches",points);return c;
}
static void laserStockDialog(bool create){
    using V=arkjson::Value;auto* f=selectedNotchedTube();if(!create&&!f)return;
    V c=create?V::makeObj():notchFeatureEdit(*f);
    const char* keys[]={"width_mm","height_mm","wall_mm","length_mm","hinge_offset_mm"};
    std::vector<EditorField> fields{{"Width (mm)","40"},{"Height (mm)","30"},{"Wall thickness (mm)","2"},{"Straight length (mm)","600"},{"Hinge offset (mm)","1"}};
    if(f&&!create)for(size_t i=0;i<fields.size();++i)fields[i].value=f->get(keys[i]);
    if(!editFields(create?"Create laser tube":"Laser tube stock","Rectangular stock, mm. Hinge offset lies within the retained wall; preview is ideal.",fields))return;
    V p=V::makeObj();for(size_t i=0;i<fields.size();++i)p.set(keys[i],V::makeStr(fields[i].value));c.set("p",p);
    if(create){c.set("op",V::makeStr("feature.add"));c.set("type",V::makeStr(featureTypeName(FeatureType::NotchedTube)));c.set("name",V::makeStr("Laser tube"));
        c.set("notches",V::makeArr());
    }
    applyCommand(c);if(!g_error){if(create)selBody=(int)g_bodyIds.size()-1;g_foldProgress=0;g_selectedNotch=0;frameScene();}
}
static void notchPointDialog(bool add){
    using V=arkjson::Value;auto* f=selectedNotchedTube();if(!f)return;V c=notchFeatureEdit(*f);
    std::vector<EditorField> fields{{"Station (mm)","("+f->get("length_mm")+")*"+(f->notches.empty()?"0.5":"0.75")},{"Angle (degrees)","45"},{"Retained wall","bottom"}};
    if(!add){if(g_selectedNotch<0||g_selectedNotch>=(int)f->notches.size())return;auto n=f->notches[g_selectedNotch];fields[0].value=n.station;fields[1].value=n.angle;fields[2].value=n.face;}
    if(!editFields(add?"Add bend point":"Edit bend point","Station is measured along straight stock. Wall: bottom, top, left or right.",fields))return;
    V point=V::makeObj();point.set("station",V::makeStr(fields[0].value));point.set("angle",V::makeStr(fields[1].value));point.set("face",V::makeStr(fields[2].value));
    V points=*c.find("notches");if(add)points.arr.push_back(point);else points.arr[g_selectedNotch]=point;c.set("notches",points);
    applyCommand(c);if(!g_error){if(add)g_selectedNotch=(int)selectedNotchedTube()->notches.size()-1;g_foldProgress=0;frameScene();}
}
static void drawFrame(HDC hdc);
static void importSourceStep(const std::string& testPath=""){
    using V=arkjson::Value;std::string path=testPath;
    if(path.empty()){
        wchar_t file[32768]{};OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=file;of.nMaxFile=32768;
        of.lpstrFilter=L"STEP source geometry\0*.step;*.stp\0";of.lpstrTitle=L"Import source STEP / preserve openings and bends";of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(!GetOpenFileNameW(&of))return;
        int size=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);path.resize(size);WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),size,nullptr,nullptr);path.pop_back();
    }
    if(!confirmDiscard())return;
    g_foldPlaying=false;g_error=false;g_status="Importing STEP / projecting source geometry and openings...";
    HDC dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);SetCursor(LoadCursor(nullptr,IDC_WAIT));
    V args=V::makeObj();args.set("path",V::makeStr(path));args.set("replace",V::makeBool(true));runTool("import_step",args);SetCursor(LoadCursor(nullptr,IDC_ARROW));
    if(!g_error){
        ++g_documentEpoch;*g_assistantCancel=true;g_conversation=arkjson::Value::makeArr();selBody=(int)g_bodyIds.size()-1;g_foldProgress=1;g_sourceScroll=0;
        std::string filename=path.substr(path.find_last_of("/\\")+1);auto dot=filename.find_last_of('.');g_path=filename.substr(0,dot)+".ark";g_savedRevision=g_session.revision-1;
        frameScene();g_status="Source STEP imported / original openings and finite bends retained";
    }
}
static void exportStraightStep(const std::string& testPath=""){
    using V=arkjson::Value;auto* f=selectedFoldable();if(!f)return;std::string path=testPath;
    if(path.empty()){
        wchar_t file[32768]=L"laser-tube-straight.step";OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=file;of.nMaxFile=32768;
        of.lpstrFilter=L"STEP solid\0*.step;*.stp\0";of.lpstrDefExt=L"step";of.lpstrTitle=selectedSourceTube()?L"Export straight source STEP / original openings":L"Export straight laser tube / ideal hinge model";
        of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(!GetSaveFileNameW(&of))return;
        int size=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);path.resize(size);WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),size,nullptr,nullptr);path.pop_back();
    }
    V args=V::makeObj();args.set("id",V::makeNum((double)f->id));args.set("path",V::makeStr(path));runTool("export_step",args);
    if(!g_error)g_status=selectedSourceTube()?"Straight source STEP exported / original openings retained":"Straight STEP exported with notches / ideal hinge preview";
}
static void alignSketchCamera(){
    if(auto* sketch=g_doc.findSketch(g_activeSketch))g_activePlane=sketch->sk.plane;
    auto frame=resolvePlane(g_doc,g_activePlane);auto normal=frame.transformDir({0,0,1}).normalized();cameraRoll=0;tYaw=std::atan2(normal.x,normal.z);tPitch=std::asin(clamp(normal.y,-.999,.999));
    if(g_sketchBounds.valid()){tTarget=g_sketchBounds.center();tDist=std::max(80.,g_sketchBounds.diagonal()*2.08);}else {tTarget=frame.transformPoint({0,0,0});tDist=200;}
}
static std::string sketchNames(){std::string names;for(auto& sk:g_doc.sketches)names+=(names.empty()?"":", ")+sk.name;return names.empty()?"Create a sketch first.":"Available: "+names;}
static double formNumber(const std::string& text){bool ok=false;double n=evalExpr(text,g_doc.buildEnv(),&ok);if(!ok||!std::isfinite(n))throw std::runtime_error("Invalid numeric expression: "+text);return n;}
static std::string sketchNames();
static std::string trimField(std::string value){auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");return first==std::string::npos?std::string():value.substr(first,last-first+1);}
static std::string joinedSketchList(const arkjson::Value* list){
    std::string text;if(!list||list->t!=arkjson::Value::Arr)return text;
    for(auto& item:list->arr){if(item.t!=arkjson::Value::Str||item.str.empty())continue;if(!text.empty())text+="; ";text+=item.str;}return text;
}
static std::string joinedFeatureIds(const std::vector<u64>& ids){std::string text;for(u64 id:ids){if(!text.empty())text+="; ";text+=std::to_string(id);}return text;}
static void commitNativeFeature(FeatureType type,const std::string& name,arkjson::Value p,arkjson::Value native,std::vector<u64> inputs,u64 editId,bool replaceNative){
    using V=arkjson::Value;V command=V::makeObj();
    if(editId){command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)editId));command.set("p",std::move(p));if(replaceNative)command.set("native",std::move(native));}
    else{V parents=V::makeArr();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr(featureTypeName(type)));command.set("name",V::makeStr(name));command.set("p",std::move(p));command.set("native",std::move(native));for(u64 id:inputs)parents.arr.push_back(V::makeNum((double)id));command.set("inputs",std::move(parents));}
    applyCommand(std::move(command));if(!g_error){if(!editId)selBody=(int)g_bodyIds.size()-1;frameScene();if(editId)g_status="Feature edited / revision "+std::to_string(g_session.revision);}
}
static void addNativeFeature(FeatureType type,const std::string& name,arkjson::Value p,arkjson::Value native=arkjson::Value::makeObj(),std::vector<u64> inputs={}){
    commitNativeFeature(type,name,std::move(p),std::move(native),std::move(inputs),0,true);
}
static void loftDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;
    std::string picked=pickedSketches();int pickedCount=picked.empty()?0:1+(int)std::count(picked.begin(),picked.end(),';');
    std::string initial=editing?joinedSketchList(editing->nativeSpec.find("sections")):(pickedCount>=2?picked:(g_doc.sketches.size()>=2?g_doc.sketches[0].name+"; "+g_doc.sketches[1].name:"Section 1; Section 2"));
    std::vector<EditorField> fields{{"Section sketches",initial},{"Solid",editing?editing->get("solid","1"):"1",true},{"Ruled",editing?editing->get("ruled","0"):"0",true},{"Smooth",editing?editing->get("smooth","0"):"0",true},{"Compatible sections",editing?editing->get("compatible","0"):"0",true}};
    std::string hint=sketchNames()+"  /  List 2 or more sections in order. A sketch with one point is a tip. Smooth and compatible are optional.";
    if(!editFields(editing?"Edit loft":"Loft through sketches",hint.c_str(),fields))return;
    V sections=V::makeArr();std::istringstream names(fields[0].value);std::string name;
    while(std::getline(names,name,';')){name=trimField(name);if(name.empty())continue;if(!g_doc.findSketch(name))throw std::runtime_error("Unknown loft section: "+name);sections.arr.push_back(V::makeStr(name));}
    if(sections.arr.size()<2||sections.arr.size()>32)throw std::runtime_error("Loft needs 2 to 32 saved sketches");
    V native=V::makeObj(),p=V::makeObj();native.set("sections",std::move(sections));p.set("solid",V::makeStr(fields[1].value));p.set("ruled",V::makeStr(fields[2].value));
    if(fields[3].value=="1"||(editing&&editing->p.count("smooth")))p.set("smooth",V::makeStr(fields[3].value=="1"?"1":"0"));
    if(fields[4].value=="1"||(editing&&editing->p.count("compatible")))p.set("compatible",V::makeStr(fields[4].value=="1"?"1":"0"));
    commitNativeFeature(FeatureType::Loft,"Loft",std::move(p),std::move(native),{},editing?editing->id:0,true);
}
static void surfaceFillDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;std::string current=editing?editing->get("sketch","Profile"):(g_activeSketch.empty()?"Profile":g_activeSketch);
    std::vector<EditorField> fields{{"Closed sketch",current}};
    auto hint=sketchNames();if(!editFields(editing?"Edit surface fill":"Fill sketch boundary",hint.c_str(),fields))return;
    auto name=trimField(fields[0].value);if(!g_doc.findSketch(name))throw std::runtime_error("Choose an existing closed sketch");
    V p=V::makeObj();p.set("sketch",V::makeStr(name));
    commitNativeFeature(FeatureType::SurfaceFill,"Surface fill",std::move(p),V::makeObj(),{},editing?editing->id:0,false);
}
static std::string nurbsSpecification(const Feature* editing){
    if(!editing)return std::string(R"({
  "u_degree": 3, "v_degree": 3,
  "poles": [
    [[0,0,0], [0,15,0], [0,30,0], [0,45,0]],
    [[15,0,0], [15,15,8], [15,30,8], [15,45,0]],
    [[30,0,0], [30,15,8], [30,30,8], [30,45,0]],
    [[45,0,0], [45,15,0], [45,30,0], [45,45,0]]
  ]
})");
    using V=arkjson::Value;V spec=V::makeObj();
    if(auto* poles=editing->nativeSpec.find("poles"))spec.set("poles",*poles);
    if(auto* weights=editing->nativeSpec.find("weights"))spec.set("weights",*weights);
    spec.set("u_degree",V::makeStr(editing->get("u_degree","3")));spec.set("v_degree",V::makeStr(editing->get("v_degree","3")));
    return spec.dump();
}
static void nurbsSurfaceDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;std::string specification=nurbsSpecification(editing);
    if(!editText(editing?"Edit NURBS surface / Ctrl+Enter to apply":"NURBS surface / pole grid JSON / Ctrl+Enter to apply",specification,true))return;
    V spec;if(!arkjson::parse(specification,spec)||spec.t!=V::Obj||!spec.find("poles"))throw std::runtime_error("NURBS grid needs JSON poles");
    V native=V::makeObj(),p=V::makeObj();native.set("poles",*spec.find("poles"));if(auto* weights=spec.find("weights"))native.set("weights",*weights);
    for(auto key:{"u_degree","v_degree"}){auto* degree=spec.find(key);if(degree&&degree->t!=V::Num&&degree->t!=V::Str)throw std::runtime_error(std::string(key)+" must be a degree");p.set(key,V::makeStr(degree?(degree->t==V::Str?degree->str:degree->dump()):"3"));}
    commitNativeFeature(FeatureType::BSplineSurface,"NURBS surface",std::move(p),std::move(native),{},editing?editing->id:0,true);
}
static bool thickenSource(FeatureType type){
    return type==FeatureType::Loft||type==FeatureType::SurfaceFill||type==FeatureType::BSplineSurface||type==FeatureType::Sweep||type==FeatureType::ExtrudeSurface||type==FeatureType::RevolveSurface||type==FeatureType::OffsetSurface||type==FeatureType::SurfaceEdit;
}
static void thickenDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;const Feature* source=nullptr;
    if(!editing){if(selBody<0){g_status="Select a native open surface to thicken.";return;}
        source=g_doc.find(g_bodyIds[selBody]);if(!source||!thickenSource(source->type)){g_status="Select a loft, fill, NURBS, sweep, extrusion, revolution, or offset surface.";return;}}
    std::vector<EditorField> fields{{"Thickness / mm",editing?editing->get("thickness","2"):"2"}};
    if(!editFields(editing?"Edit thicken":"Thicken native surface","Signed thickness is measured from the selected OCCT face or shell.",fields))return;
    V p=V::makeObj();p.set("thickness",V::makeStr(fields[0].value));
    if(editing)commitNativeFeature(FeatureType::Thicken,"Thicken",std::move(p),V::makeObj(),{},editing->id,false);
    else addNativeFeature(FeatureType::Thicken,"Thicken",std::move(p),V::makeObj(),{source->id});
}
static void sheetMetalDialog(){
    using V=arkjson::Value;const char* keys[]={"width","leg_a","leg_b","thickness","inner_radius","angle","k_factor"};
    std::vector<EditorField> fields{{"Width / mm","80"},{"Leg A / mm","100"},{"Leg B / mm","65"},{"Thickness / mm","2"},{"Inner radius / mm","3"},{"Bend angle / degrees","90"},{"K factor","0.42"}};
    if(!editFields("Sheet metal L bend","All dimensions accept expressions. Flat pose uses the native bend allowance.",fields))return;
    V p=V::makeObj();for(size_t i=0;i<fields.size();++i)p.set(keys[i],V::makeStr(fields[i].value));
    addNativeFeature(FeatureType::SheetMetal,"Sheet metal L bend",std::move(p));
}
static Feature* authoredSheet(){auto* feature=selectedSheet();if(!feature||feature->type!=FeatureType::SheetMetal)throw std::runtime_error("Select an authored sheet metal feature");return feature;}
static arkjson::Value sheetEntityRows(const Feature& feature,const char* key){
    using V=arkjson::Value;if(auto* existing=feature.nativeSpec.find(key)){
        V rows=*existing;if(std::string(key)=="openings"&&!feature.nativeSpec.find("base"))for(size_t i=0;i<rows.arr.size();++i){auto& row=rows.arr[i];if(!row.find("id"))row.set("id",V::makeStr("opening_"+std::to_string(i+1)));if(auto* panel=row.find("panel");panel&&panel->t==V::Num)row.set("panel",V::makeStr("panel_"+std::to_string((int)panel->num)));}return rows;
    }
    V rows=V::makeArr();if(std::string(key)=="flanges"&&!feature.nativeSpec.find("base")){
        V row=V::makeObj();row.set("id",V::makeStr("panel_1"));row.set("parent",V::makeStr("panel_0"));row.set("edge",V::makeStr("u_max"));
        row.set("length",V::makeStr(feature.get("leg_b")));row.set("inner_radius",V::makeStr(feature.get("inner_radius")));row.set("angle",V::makeStr(feature.get("angle")));rows.arr.push_back(row);
    }return rows;
}
static std::string sheetBaseId(const Feature& feature){auto* base=feature.nativeSpec.find("base");auto* id=base?base->find("id"):nullptr;return id&&id->t==arkjson::Value::Str?id->str:"panel_0";}
static std::string sheetFieldText(const arkjson::Value& value){if(value.t==arkjson::Value::Str)return value.str;if(value.t==arkjson::Value::Num){std::ostringstream out;out.precision(16);out<<value.num;return out.str();}return {};}
static std::string sheetEntityIdDialog(const Feature& feature,const char* key,const char* title){
    auto rows=sheetEntityRows(feature,key);if(rows.arr.empty())throw std::runtime_error(std::string("This sheet has no ")+key);
    if(g_selectedExplorerEntity&&g_selectedExplorerEntity->feature==feature.id&&g_selectedExplorerEntity->key==key)for(const auto& row:rows.arr)if(row.find("id")&&row.find("id")->asStr()==g_selectedExplorerEntity->id)return g_selectedExplorerEntity->id;
    std::string choices;for(auto& row:rows.arr){auto* id=row.find("id");if(id){if(!choices.empty())choices+=", ";choices+=id->asStr();}}
    std::vector<EditorField> fields{{"Stable entity ID",rows.arr.front().find("id")->asStr()}};
    if(!editFields(title,choices.c_str(),fields))return {};
    auto id=trimField(fields[0].value);for(auto& row:rows.arr)if(row.find("id")->asStr()==id)return id;
    throw std::runtime_error("Sheet entity ID is not present");
}
static void createSheetBaseDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Sheet name","Sheet"},{"Base length / mm","100"},{"Base width / mm","80"},{"Thickness / mm","2"},{"K factor","0.42"}};
    if(!editFields("Create sheet base","Start a flat, exact sheet; attach flanges and openings afterwards.",fields))return;
    V c=V::makeObj(),p=V::makeObj(),base=V::makeObj();c.set("op",V::makeStr("sheet.create"));c.set("name",V::makeStr(trimField(fields[0].value)));
    p.set("width",V::makeStr(trimField(fields[2].value)));p.set("thickness",V::makeStr(trimField(fields[3].value)));p.set("k_factor",V::makeStr(trimField(fields[4].value)));
    base.set("id",V::makeStr("base"));base.set("length",V::makeStr(trimField(fields[1].value)));c.set("p",std::move(p));c.set("base",std::move(base));
    applyCommand(c);if(!g_error){selBody=(int)g_bodyIds.size()-1;frameScene();}
}
static void editSheetBaseDialog(){
    using V=arkjson::Value;auto* feature=authoredSheet();auto* base=feature->nativeSpec.find("base");
    std::string length=base&&base->find("length")?sheetFieldText(*base->find("length")):feature->get("leg_a");
    std::vector<EditorField> fields{{"Base length / mm",length},{"Width / mm",feature->get("width")},{"Thickness / mm",feature->get("thickness")},{"K factor",feature->get("k_factor")}};
    if(!editFields("Edit sheet base","Dimensions and K factor may reference named parameters.",fields))return;
    V first=V::makeObj(),second=V::makeObj(),values=V::makeObj(),p=V::makeObj(),commands=V::makeArr(),args=V::makeObj();
    first.set("op",V::makeStr("feature.edit"));first.set("id",V::makeNum((double)feature->id));
    p.set("width",V::makeStr(trimField(fields[1].value)));p.set("thickness",V::makeStr(trimField(fields[2].value)));p.set("k_factor",V::makeStr(trimField(fields[3].value)));first.set("p",std::move(p));commands.arr.push_back(std::move(first));
    second.set("op",V::makeStr("sheet.base.edit"));second.set("id",V::makeNum((double)feature->id));values.set("length",V::makeStr(trimField(fields[0].value)));second.set("values",std::move(values));commands.arr.push_back(std::move(second));
    args.set("commands",std::move(commands));runTool("doc_apply",args);
}
static void sheetFlangeDialog(bool edit){
    using V=arkjson::Value;auto* feature=authoredSheet();V row=V::makeObj();std::string entity;
    if(edit){entity=sheetEntityIdDialog(*feature,"flanges","Select flange to edit");if(entity.empty())return;for(auto& item:sheetEntityRows(*feature,"flanges").arr)if(item.find("id")->asStr()==entity){row=item;break;}}
    auto get=[&](const char* key,const char* fallback){auto* v=row.find(key);return v?sheetFieldText(*v):std::string(fallback);};
    std::vector<EditorField> fields{{"Parent panel ID",get("parent",sheetBaseId(*feature).c_str())},{"Parent edge: u_min/u_max/v_min/v_max",get("edge","u_max")},{"Flange length / mm",get("length","45")},{"Width / mm (blank: edge span)",get("width","")},{"Offset / mm (blank: 0)",get("offset","")},{"Inner radius / mm",get("inner_radius","3")},{"Angle / degrees",get("angle","90")},{"K factor (blank: sheet)",get("k_factor","")}};
    if(!editFields(edit?"Edit sheet flange":"Add sheet flange","Parent must exist earlier. Edge length, parent_length, and parent_width are available expressions.",fields))return;
    V values=V::makeObj();const char* keys[]={"parent","edge","length","width","offset","inner_radius","angle","k_factor"};
    for(size_t i=0;i<fields.size();++i){auto input=trimField(fields[i].value);if(input.empty()){if(i==3||i==4||i==7)continue;throw std::runtime_error("Required flange field is empty");}values.set(keys[i],V::makeStr(input));}
    V command=V::makeObj();command.set("op",V::makeStr(edit?"sheet.flange.edit":"sheet.flange.add"));command.set("id",V::makeNum((double)feature->id));
    if(edit){command.set("entity",V::makeStr(entity));command.set("values",std::move(values));}else command.set("flange",std::move(values));
    applyCommand(command);
}
static void removeSheetFlangeDialog(){
    using V=arkjson::Value;auto* feature=authoredSheet();auto entity=sheetEntityIdDialog(*feature,"flanges","Remove flange");if(entity.empty())return;
    std::vector<EditorField> fields{{"Remove dependent flanges/openings","0",true}};
    if(!editFields("Remove sheet flange","A dependent opening or flange requires cascade. Undo restores the program.",fields))return;
    V command=V::makeObj();command.set("op",V::makeStr("sheet.flange.remove"));command.set("id",V::makeNum((double)feature->id));command.set("entity",V::makeStr(entity));command.set("cascade",V::makeBool(fields[0].value=="1"));applyCommand(command);
}
static arkjson::Value sheetPolygonPoints(const std::string& specification){
    using V=arkjson::Value;V points=V::makeArr();std::istringstream stream(specification);std::string pair;
    while(std::getline(stream,pair,';')){pair=trimField(pair);if(pair.empty())continue;size_t comma=std::string::npos;int depth=0;
        for(size_t i=0;i<pair.size();++i){if(pair[i]=='(')++depth;else if(pair[i]==')')--depth;else if(pair[i]==','&&depth==0){if(comma!=std::string::npos)throw std::runtime_error("Polygon point needs one comma");comma=i;}if(depth<0)throw std::runtime_error("Unbalanced polygon expression");}
        if(comma==std::string::npos||depth)throw std::runtime_error("Polygon point must be x,y");V point=V::makeArr();point.arr.push_back(V::makeStr(trimField(pair.substr(0,comma))));point.arr.push_back(V::makeStr(trimField(pair.substr(comma+1))));points.arr.push_back(std::move(point));
    }
    if(points.arr.size()<3||points.arr.size()>64)throw std::runtime_error("Polygon opening needs 3..64 points");return points;
}
static std::string sheetPolygonText(const arkjson::Value& points){using V=arkjson::Value;std::string result;if(points.t!=V::Arr)return result;for(auto& pair:points.arr)if(pair.t==V::Arr&&pair.arr.size()==2){if(!result.empty())result+="; ";result+=sheetFieldText(pair.arr[0])+","+sheetFieldText(pair.arr[1]);}return result;}
static void sheetOpeningDialog(bool edit,bool pattern=false){
    using V=arkjson::Value;auto* feature=authoredSheet();V row=V::makeObj();std::string entity;
    if(edit){entity=sheetEntityIdDialog(*feature,"openings","Select opening to edit");if(entity.empty())return;for(auto& item:sheetEntityRows(*feature,"openings").arr)if(item.find("id")->asStr()==entity){row=item;break;}}
    auto get=[&](const char* key,const char* fallback){auto* v=row.find(key);return v?sheetFieldText(*v):std::string(fallback);};
    std::string kind=get("kind","circle");if(!edit){std::vector<EditorField> pick{{"Opening: circle / slot / window / polygon",kind}};if(!editFields("Choose sheet opening","Circle, rounded slot, rotated rectangular window or polygon.",pick))return;kind=trimField(pick[0].value);}
    if(kind!="circle"&&kind!="slot"&&kind!="window"&&kind!="polygon")throw std::runtime_error("Opening kind must be circle, slot, window or polygon");
    std::vector<EditorField> fields{{"Panel ID",get("panel",sheetBaseId(*feature).c_str())}};
    if(kind=="polygon"){auto* points=row.find("points");fields.push_back({"Points: x,y; x,y; ...",points?sheetPolygonText(*points):"15,15; 35,15; 35,30; 15,30"});}
    else{fields.push_back({"Centre X / mm",get("x","25")});fields.push_back({"Centre Y / mm",get("y","25")});
        if(kind=="circle")fields.push_back({"Radius / mm",get("radius","5")});
        else if(kind=="slot"){fields.push_back({"Overall length / mm",get("length","24")});fields.push_back({"Width / mm",get("width","6")});fields.push_back({"Angle / deg",get("angle","0")});}
        else{fields.push_back({"Width / mm",get("width","18")});fields.push_back({"Height / mm",get("height","12")});fields.push_back({"Angle / deg",get("angle","0")});}}
    size_t patternStart=fields.size();if(!edit){fields.push_back({"Pattern count / 1..32",pattern?"2":"1"});fields.push_back({"Pattern step X / mm","28"});fields.push_back({"Pattern step Y / mm","0"});}
    if(!editFields(edit?"Edit sheet opening":pattern?"Pattern sheet openings":"Add sheet opening","Dimensions accept expressions. Pattern repeats the opening without editing existing entities.",fields))return;
    V values=V::makeObj(),command=V::makeObj();values.set("panel",V::makeStr(trimField(fields[0].value)));values.set("kind",V::makeStr(kind));
    if(kind=="polygon")values.set("points",sheetPolygonPoints(fields[1].value));
    else{values.set("x",V::makeStr(trimField(fields[1].value)));values.set("y",V::makeStr(trimField(fields[2].value)));
        if(kind=="circle")values.set("radius",V::makeStr(trimField(fields[3].value)));
        else if(kind=="slot"){values.set("length",V::makeStr(trimField(fields[3].value)));values.set("width",V::makeStr(trimField(fields[4].value)));values.set("angle",V::makeStr(trimField(fields[5].value)));}
        else{values.set("width",V::makeStr(trimField(fields[3].value)));values.set("height",V::makeStr(trimField(fields[4].value)));values.set("angle",V::makeStr(trimField(fields[5].value)));}}
    command.set("id",V::makeNum((double)feature->id));
    if(edit){command.set("op",V::makeStr("sheet.opening.edit"));command.set("entity",V::makeStr(entity));command.set("values",std::move(values));}
    else{double count=formNumber(fields[patternStart].value);if(count<1||count>32||std::floor(count)!=count)throw std::runtime_error("Opening pattern count must be an integer from 1 to 32");
        command.set("op",V::makeStr(count==1?"sheet.opening.add":"sheet.opening.pattern"));command.set("opening",std::move(values));
        if(count>1){command.set("count",V::makeNum(count));command.set("dx",V::makeStr(trimField(fields[patternStart+1].value)));command.set("dy",V::makeStr(trimField(fields[patternStart+2].value)));}}
    applyCommand(command);
}
static void removeSheetOpeningDialog(){
    using V=arkjson::Value;auto* feature=authoredSheet();auto entity=sheetEntityIdDialog(*feature,"openings","Remove sheet opening");if(entity.empty())return;
    V command=V::makeObj();command.set("op",V::makeStr("sheet.opening.remove"));command.set("id",V::makeNum((double)feature->id));command.set("entity",V::makeStr(entity));applyCommand(command);
}
static void sheetFormDialog(bool stamp,bool edit){
    using V=arkjson::Value;auto* feature=authoredSheet();const char* key=stamp?"stamps":"hems";V row=V::makeObj();std::string entity;
    if(edit){entity=sheetEntityIdDialog(*feature,key,stamp?"Choose emboss to edit":"Choose hem to edit");if(entity.empty())return;for(const auto& candidate:sheetEntityRows(*feature,key).arr)if(candidate.find("id")->asStr()==entity){row=candidate;break;}}
    auto get=[&](const char* name,const char* fallback){auto* value=row.find(name);return value?sheetFieldText(*value):std::string(fallback);};
    std::vector<EditorField> fields;
    if(stamp)fields={{"Panel ID",get("panel",sheetBaseId(*feature).c_str())},{"Centre X / mm",get("x","40")},{"Centre Y / mm",get("y","30")},{"Outer transition radius / mm",get("radius","12")},{"Plateau radius / mm",get("top_radius","6")},{"Depth / mm (negative: recessed)",get("depth","4")}};
    else fields={{"Parent panel ID",get("parent",sheetBaseId(*feature).c_str())},{"Parent edge: u_min/u_max/v_min/v_max",get("edge","u_max")},{"Return length / mm",get("length","12")},{"Inner bend radius / mm",get("inner_radius","2")},{"Direction / +1 or -1",get("direction","1")},{"Width / mm (blank: edge span)",get("width","")},{"Offset / mm (blank: 0)",get("offset","")},{"K factor (blank: sheet)",get("k_factor","")}};
    if(!editFields(stamp?(edit?"Edit circular emboss":"Circular emboss"):(edit?"Edit return hem":"Return hem"),stamp?"Exact sharp conical wall and planar plateau. Constant thickness. Flat is a nominal reference; forming strain and a production blank are not qualified.":"Open round 180 degree return. Positive bend radius; direction +1 or -1. This hem cannot own child flanges.",fields))return;
    const char* stampKeys[]={"panel","x","y","radius","top_radius","depth"};const char* hemKeys[]={"parent","edge","length","inner_radius","direction","width","offset","k_factor"};V values=V::makeObj();
    for(size_t i=0;i<fields.size();++i){auto value=trimField(fields[i].value);if(value.empty()){if(!stamp&&i>=5)continue;throw std::runtime_error("A required forming field is empty");}values.set(stamp?stampKeys[i]:hemKeys[i],V::makeStr(value));}
    if(stamp)values.set("kind",V::makeStr("circular_emboss"));V command=V::makeObj();command.set("op",V::makeStr(std::string("sheet.")+(stamp?"stamp.":"hem.")+(edit?"edit":"add")));command.set("id",V::makeNum((double)feature->id));
    if(edit){command.set("entity",V::makeStr(entity));command.set("values",std::move(values));}else command.set(stamp?"stamp":"hem",std::move(values));applyCommand(command);
}
static void removeSheetFormDialog(bool stamp){
    using V=arkjson::Value;auto* feature=authoredSheet();auto entity=sheetEntityIdDialog(*feature,stamp?"stamps":"hems",stamp?"Remove circular emboss":"Remove return hem");if(entity.empty())return;
    V command=V::makeObj();command.set("op",V::makeStr(stamp?"sheet.stamp.remove":"sheet.hem.remove"));command.set("id",V::makeNum((double)feature->id));command.set("entity",V::makeStr(entity));applyCommand(command);
}
static void sheetInternalBendDialog(bool sketchBend){
    using V=arkjson::Value;auto* feature=authoredSheet();const char* key=sketchBend?"sketch_bend":"jog";const V* existing=feature->nativeSpec.find(key);std::vector<EditorField> actionFields{{"Action: add / edit / remove",existing?"edit":"add"}};
    if(!editFields(sketchBend?"Original-blank sketch bend":"Original-blank jog","One unperforated rectangular blank. Select the existing sheet; this bends its own material.",actionFields))return;auto action=trimField(actionFields[0].value);if(action!="add"&&action!="edit"&&action!="remove")throw std::runtime_error("Action must be add, edit or remove");if(action!="add"&&!existing)throw std::runtime_error("This sheet has no matching internal bend program");
    V command=V::makeObj();command.set("op",V::makeStr("sheet."+std::string(key)+"."+action));command.set("id",V::makeNum((double)feature->id));if(action=="remove"){applyCommand(command);return;}
    auto get=[&](const char* name,const char* fallback){auto* value=existing?existing->find(name):nullptr;return value?(value->t==V::Str?value->str:value->dump()):std::string(fallback);};std::vector<EditorField> fields{{"Program ID",get("id",sketchBend?"fold":"jog")}};
    if(sketchBend){fields.push_back({"XZ sketch",get("sketch",g_activeSketch.c_str())});std::string entity;if(auto* sketch=g_doc.findSketch(fields.back().value))for(const auto& candidate:sketch->sk.ents)if(candidate.kind==SketchEnt::Line){entity=candidate.name;break;}fields.push_back({"Full-width straight line",get("entity",entity.c_str())});}
    else{fields.push_back({"First bend station / mm",get("station","40")});fields.push_back({"Signed offset height / mm",get("height","20")});}
    fields.push_back({"Angle / degrees",get("angle",sketchBend?"90":"45")});fields.push_back({"Inner radius / mm",get("inner_radius","3")});fields.push_back({"K factor (blank: sheet)",get("k_factor","")});
    if(!editFields(sketchBend?"Linked full-width sketch bend":"Two opposed jog bends",sketchBend?"Line must be straight, on resolved XZ, and span the full original blank width. Edits remain linked.":"Station starts the neutral bend band. Height is signed; the distal panel remains parallel. Expressions stay editable.",fields))return;
    V values=V::makeObj();const char* jogKeys[]={"id","station","height","angle","inner_radius","k_factor"},*bendKeys[]={"id","sketch","entity","angle","inner_radius","k_factor"};for(size_t i=0;i<fields.size();++i){auto value=trimField(fields[i].value);if(i==0&&action=="edit"){if(value!=get("id",""))throw std::runtime_error("Forming program IDs are immutable");continue;}if(value.empty()&&i==5)continue;if(value.empty())throw std::runtime_error("Required forming field is empty");values.set(sketchBend?bendKeys[i]:jogKeys[i],V::makeStr(value));}command.set(action=="edit"?"values":sketchBend?"bend":"jog",values);applyCommand(command);
}
static void sheetEditMenu(){
    if(!authoredSheet())return;
    std::vector<EditorField> fields{{"Edit base / flange / opening / hem / stamp","base"}};
    if(!editFields("Edit sheet metal","Choose which authored element to edit. The native sheet program and revision remain traceable.",fields))return;
    std::string choice=trimField(fields[0].value);
    if(choice=="base")editSheetBaseDialog();else if(choice=="flange")sheetFlangeDialog(true);
    else if(choice=="opening")sheetOpeningDialog(true);
    else if(choice=="hem")sheetFormDialog(false,true);else if(choice=="stamp")sheetFormDialog(true,true);
    else throw std::runtime_error("Choose base, flange, opening, hem or stamp");
}
static void importSheetStep(const std::string& testPath=""){
    using V=arkjson::Value;std::vector<EditorField> fields{{"K factor","0.42"},{"Body index (optional)",""},{"Root panel (optional)",""}};
    if(!editFields("Import sheet STEP","K factor is explicit. Body index is zero based; root panel is 0 or 1 if ambiguous.",fields))return;
    double factor=formNumber(fields[0].value);if(factor<0||factor>1)throw std::runtime_error("K factor must lie between 0 and 1");
    std::string path=testPath;
    if(path.empty()){
        wchar_t file[32768]{};OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=file;of.nMaxFile=32768;
        of.lpstrFilter=L"STEP sheet geometry\0*.step;*.stp\0";of.lpstrTitle=L"Import sheet STEP";of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(!GetOpenFileNameW(&of))return;
        int size=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);path.resize(size);WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),size,nullptr,nullptr);path.pop_back();
    }
    V args=V::makeObj();args.set("path",V::makeStr(path));args.set("k_factor",V::makeNum(factor));args.set("replace",V::makeBool(true));
    for(size_t i=1;i<fields.size();++i){auto value=trimField(fields[i].value);if(value.empty())continue;double number=formNumber(value);if(number<0||std::floor(number)!=number||(i==2&&number>1))throw std::runtime_error(i==1?"Body index must be a nonnegative integer":"Root panel must be 0 or 1");args.set(i==1?"body_index":"root_panel",V::makeNum(number));}
    if(!confirmDiscard())return;g_error=false;g_status="Importing source sheet STEP...";HDC dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);SetCursor(LoadCursor(nullptr,IDC_WAIT));
    runTool("import_sheet_step",args);SetCursor(LoadCursor(nullptr,IDC_ARROW));
    if(!g_error){++g_documentEpoch;*g_assistantCancel=true;g_conversation=V::makeArr();selBody=(int)g_bodyIds.size()-1;g_sheetFlatViews.clear();
        std::string filename=path.substr(path.find_last_of("/\\")+1);auto dot=filename.find_last_of('.');g_path=filename.substr(0,dot)+".ark";g_savedRevision=g_session.revision-1;frameScene();
        g_status="Source sheet imported / inspect native geometry and flat pose";}
}
static void exportNativeStep(const std::string& testPath=""){
    using V=arkjson::Value;if(selBody<0){g_status="Select a native body to export.";return;}
    auto* f=g_doc.find(g_bodyIds[selBody]);if(!f)return;
    bool sheet=isSheetFeature(f->id);auto lineage=featureManufacturingLineage(g_doc,*f);auto* originSheet=sheet?g_doc.find(lineage.origin):nullptr;
    bool surface=isNativeSurfaceFeature(f->type)||f->type==FeatureType::NativeSolid;
    if(!sheet&&!surface){g_status="Select a native sheet or surface feature.";return;}
    std::string pose;if(sheet){bool stamped=originSheet&&sheetHasStamp(*originSheet);std::vector<EditorField> fields{{"Pose: flat or formed",stamped?"formed":"flat"}};
        if(testPath.empty()&&!editFields("Export native sheet STEP",stamped?"Stamped sheets export their exact formed B-rep. The nominal flat reference is not a production blank.":"Flat exports the original sheet blank; formed exports the selected exact placed part.",fields))return;
        pose=trimField(fields[0].value);if(pose!="flat"&&pose!="formed")throw std::runtime_error("Pose must be flat or formed");}
    std::string path=testPath;
    if(path.empty()){
        wchar_t file[32768]=L"ark-native.step";OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=file;of.nMaxFile=32768;
        of.lpstrFilter=L"STEP geometry\0*.step;*.stp\0";of.lpstrDefExt=L"step";of.lpstrTitle=sheet?L"Export native sheet STEP":L"Export native surface STEP";
        of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(!GetSaveFileNameW(&of))return;
        int size=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);path.resize(size);WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),size,nullptr,nullptr);path.pop_back();
    }
    V args=V::makeObj();args.set("id",V::makeNum((double)(sheet&&pose=="flat"?lineage.origin:f->id)));args.set("path",V::makeStr(path));if(sheet)args.set("pose",V::makeStr(pose));runTool("export_step",args);
    if(!g_error)g_status=sheet?"Native "+pose+" sheet STEP exported":"Native surface STEP exported";
}
static void exportFlatDxf(const std::string& testPath=""){
    Feature* sheet=authoredSheet();std::string path=testPath;
    if(path.empty()){
        wchar_t file[32768]=L"ark-flat.dxf";OPENFILENAMEW picker{};picker.lStructSize=sizeof picker;picker.hwndOwner=g_window;picker.lpstrFile=file;picker.nMaxFile=32768;
        picker.lpstrFilter=L"DXF flat sheet\0*.dxf\0";picker.lpstrDefExt=L"dxf";picker.lpstrTitle=L"Export analytic flat sheet DXF";
        picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(!GetSaveFileNameW(&picker))return;
        int count=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);path.resize((size_t)count);
        WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),count,nullptr,nullptr);path.pop_back();
    }
    using V=arkjson::Value;V args=V::makeObj();args.set("id",V::makeNum((double)sheet->id));args.set("path",V::makeStr(path));
    runTool("export_flat_dxf",args);if(!g_error)g_status="Analytic flat DXF exported / fabrication qualification not claimed";
}
static void sheetDisplay(bool flat){
    if(g_allSheetsFlat||g_allSheetsPlaying)allSheetPose(false);
    auto* sheet=selectedSheet();auto* body=sheet?sheetOriginBody(sheet->id):nullptr;if(!sheet||!body)return;
    if(flat&&!body->nativeData.find("flat_mesh")){g_error=true;g_status="Native flat preview is unavailable for this sheet.";return;}
    g_sheetPlayingId=0;sheetPreviewSet(sheet->id,flat?0.:1.);frameScene();g_error=false;g_status=flat?(sheetHasStamp(*sheet)?"Nominal flat reference / stamped blank requires forming development":"Flat sheet display / document unchanged"):"Formed sheet display / document unchanged";
}
static void toggleSheetPlayback(){
    if(g_allSheetsFlat||g_allSheetsPlaying)allSheetPose(false);
    auto* sheet=selectedSheet();auto* body=sheet?sheetOriginBody(sheet->id):nullptr;if(!sheet||sheet->type!=FeatureType::SheetMetal||!body||!body->nativeData.find("animation"))throw std::runtime_error("Select an authored sheet with a bend program");
    if(sheetHasStamp(*sheet))throw std::runtime_error("Stamped sheets support exact Flat/Formed endpoints; intermediate forming strain is not modeled");
    if(g_sheetPlayingId==sheet->id){g_sheetPlayingId=0;return;}
    if(sheetDisplayProgress(sheet->id)>=1)sheetPreviewSet(sheet->id,0);
    g_sheetPlayingId=sheet->id;g_status="Playing authored bend preview / document unchanged";
}
static u64 featureIdField(const std::string& value){
    double parsed=formNumber(value);if(parsed<1||parsed>9007199254740990.||std::floor(parsed)!=parsed||!g_doc.find((u64)parsed))throw std::runtime_error("Choose an existing feature ID");return (u64)parsed;
}
static std::string selectedFeatureId(){return selBody>=0&&selBody<(int)g_bodyIds.size()?std::to_string(g_bodyIds[(size_t)selBody]):"1";}
static void applyUiCommandAtRevision(arkjson::Value command,u64 revision,u64 epoch);
#include "selection_glow.inl"
static void refreshSelectionLines(){
    ++g_selectionGeometrySerial;
    g_selectionLines.clear();g_selectionEdgeLines.clear();g_selectionFaceFill.clear();g_selectionFaceBoundary.clear();auto put=[&](Vec3 p){g_selectionLines.insert(g_selectionLines.end(),{(float)p.x,(float)p.y,(float)p.z});};auto putEdge=[&](Vec3 p){g_selectionEdgeLines.insert(g_selectionEdgeLines.end(),{(float)p.x,(float)p.y,(float)p.z});};
    for(const auto& item:g_picked){
        if(item.kind=="curve"){for(const auto& sample:g_curveSamples)if(sample.sketch==item.owner&&(item.name.empty()||item.name==item.owner||sample.name==item.name)){put(sample.a);put(sample.b);}continue;}
        if(item.kind!="edge"&&item.kind!="face"&&item.kind!="mesh_face"&&item.kind!="mesh_edge")continue;
        for(size_t body=0;body<g_bodyIds.size()&&body<g_instancePart.size();++body){
            if(body<g_hiddenBodies.size()&&g_hiddenBodies[body])continue;
            if(g_bodyIds[body]!=item.feature||(item.instance&&(g_instanceIds[body]!=item.instance||g_instanceCopies[body]!=item.copy)))continue;
            size_t part=g_instancePart[body];if(part>=g_partPicks.size()||part>=g_cpuMeshes.size())continue;const auto& pick=g_partPicks[part];const auto& mesh=g_cpuMeshes[part];
            if(item.kind=="mesh_face"||item.kind=="mesh_edge"){
                if(item.topology.empty()||meshTopologyFingerprint(mesh)!=item.topology)continue;
                if(item.kind=="mesh_edge"){if(item.meshVertices.size()!=2)continue;for(u32 vertex:item.meshVertices){size_t at=(size_t)vertex*3;if(at+2<mesh.pos.size())putEdge(g_instanceWorld[body].transformPoint({mesh.pos[at],mesh.pos[at+1],mesh.pos[at+2]}));}}
                else{appendSelectedMeshPatchBoundary(mesh,item.meshTriangles,g_instanceWorld[body]);for(u32 triangle:item.meshTriangles)for(int corner=0;corner<3;++corner){size_t slot=(size_t)triangle*3+corner;if(slot>=mesh.idx.size())continue;size_t at=(size_t)mesh.idx[slot]*3;if(at+2>=mesh.pos.size())continue;Vec3 point=g_instanceWorld[body].transformPoint({mesh.pos[at],mesh.pos[at+1],mesh.pos[at+2]});g_selectionFaceFill.insert(g_selectionFaceFill.end(),{(float)point.x,(float)point.y,(float)point.z});}}
                continue;
            }
            if(item.kind=="edge"){std::set<int> drawn;for(size_t edge=0;edge<pick.edges.size();++edge)if(edgeReferenceMatches(pick,edge,item.name)&&drawn.insert(edge<pick.edgeCanonical.size()?pick.edgeCanonical[edge]:(int)edge).second)for(size_t k=1;k<pick.edges[edge].size();++k){putEdge(g_instanceWorld[body].transformPoint(pick.edges[edge][k-1]));putEdge(g_instanceWorld[body].transformPoint(pick.edges[edge][k]));}}
            else{for(size_t face=0;face<pick.faceNames.size()&&face<pick.faces.size();++face)if(faceReferenceMatches(pick,face,item.name)){const auto span=pick.faces[face];appendSelectedFaceBoundary(mesh,span.first,span.second,g_instanceWorld[body]);for(int triangle=std::max(span.first,0);triangle<span.first+span.second;++triangle)for(int corner=0;corner<3;++corner){size_t slot=(size_t)triangle*3+corner;if(slot>=mesh.idx.size())continue;size_t at=(size_t)mesh.idx[slot]*3;if(at+2>=mesh.pos.size())continue;Vec3 point=g_instanceWorld[body].transformPoint({mesh.pos[at],mesh.pos[at+1],mesh.pos[at+2]});g_selectionFaceFill.insert(g_selectionFaceFill.end(),{(float)point.x,(float)point.y,(float)point.z});}}}
        }
    }
    appendDatumSelectionLines();g_sketchGpuDirty=true;
}
static void toggleSelection(SelectionItem item){
    auto same=[&](const SelectionItem& other){return other.kind==item.kind&&other.name==item.name&&other.owner==item.owner&&other.feature==item.feature&&other.instance==item.instance&&other.copy==item.copy&&other.meshTriangles==item.meshTriangles&&other.meshVertices==item.meshVertices&&other.topology==item.topology;};
    auto found=std::find_if(g_picked.begin(),g_picked.end(),same);
    if(found!=g_picked.end())g_picked.erase(found);
    else{if(g_picked.size()>=64){g_status="At most 64 selected references";return;}g_picked.push_back(std::move(item));}
    refreshSelectionLines();
    std::string text;for(const auto& picked:g_picked){if(!text.empty())text+=", ";text+=picked.kind+" "+(picked.name.empty()?picked.owner:picked.name);}
    g_status=text.empty()?"Selection cleared":"Selected "+text;
}
static std::string pickedNames(const char* kind){std::string text;for(const auto& item:g_picked)if(item.kind==kind&&!item.name.empty()){if(!text.empty())text+="; ";text+=item.name;}return text;}
static std::string pickedFeatureId(const char* kind){for(const auto& item:g_picked)if(item.kind==kind&&item.feature)return std::to_string(item.feature);return selectedFeatureId();}
static std::string pickedSketches(){std::string text;std::set<std::string> seen;for(const auto& item:g_picked)if(item.kind=="curve"&&!item.owner.empty()&&seen.insert(item.owner).second){if(!text.empty())text+="; ";text+=item.owner;}return text;}
static std::string suggestedMirror(){std::string faces=pickedNames("face"),planes=pickedNames("plane");auto first=[&](const std::string& text){auto mark=text.find(';');return mark==std::string::npos?text:text.substr(0,mark);};if(!faces.empty())return first(faces);if(!planes.empty())return first(planes);return g_activePlane.empty()?"XY":g_activePlane;}
static std::string nativeReferenceHint(const char* wanted){
    std::string names="Bound "+std::string(wanted)+" references: ";const Body* body=selectedNativeBody();const auto* topology=body?body->nativeData.find("topology"):nullptr;
    const auto* rows=topology?topology->find("references"):nullptr;int shown=0;
    if(rows&&rows->t==arkjson::Value::Arr)for(const auto& row:rows->arr){auto* name=row.find("name"),*kind=row.find("kind"),*status=row.find("status");
        if(name&&kind&&status&&kind->t==arkjson::Value::Str&&status->t==arkjson::Value::Str&&kind->str==wanted&&status->str=="bound"){
            if(shown++)names+=", ";names+=name->asStr();if(shown>=5){names+=", ...";break;}
        }
    }
    return shown?names:"Select a native solid to inspect its semantic references.";
}
static void nativePrimitiveDialog(){
    using V=arkjson::Value;std::vector<EditorField> choose{{"Exact primitive","box"}};
    if(!editFields("Native solid primitive","box, cylinder, sphere, cone, or torus",choose))return;
    auto op=trimField(choose[0].value);std::vector<EditorField> fields;
    if(op=="box")fields={{"Width / mm","80"},{"Depth / mm","60"},{"Height / mm","20"}};
    else if(op=="cylinder")fields={{"Radius / mm","20"},{"Height / mm","30"}};
    else if(op=="sphere")fields={{"Radius / mm","20"}};
    else if(op=="cone")fields={{"Bottom radius / mm","20"},{"Top radius / mm","10"},{"Height / mm","30"}};
    else if(op=="torus")fields={{"Major radius / mm","30"},{"Minor radius / mm","6"}};
    else throw std::runtime_error("Native primitive must be box, cylinder, sphere, cone, or torus");
    auto title="Native "+op;if(!editFields(title.c_str(),"Dimensions accept named parameter expressions.",fields))return;
    V p=V::makeObj();p.set("op",V::makeStr(op));
    std::vector<std::string> keys=op=="box"?std::vector<std::string>{"width","depth","height"}:op=="cylinder"?std::vector<std::string>{"radius","height"}:op=="sphere"?std::vector<std::string>{"radius"}:op=="cone"?std::vector<std::string>{"radius_bottom","radius_top","height"}:std::vector<std::string>{"major_radius","minor_radius"};
    for(size_t i=0;i<keys.size();++i)p.set(keys[i],V::makeStr(trimField(fields[i].value)));
    addNativeFeature(FeatureType::NativeSolid,"Native "+op,std::move(p));
}
static void nativeExtrudeDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Closed sketch",g_activeSketch.empty()?(g_doc.sketches.empty()?"":g_doc.sketches.front().name):g_activeSketch},{"Depth / mm","10"}};
    if(!editFields("Exact sketch extrusion",sketchNames().c_str(),fields))return;
    auto name=trimField(fields[0].value);if(!g_doc.findSketch(name))throw std::runtime_error("Choose an existing closed sketch");
    V p=V::makeObj();p.set("op",V::makeStr("extrude"));p.set("sketch",V::makeStr(name));p.set("depth",V::makeStr(trimField(fields[1].value)));
    addNativeFeature(FeatureType::NativeSolid,"Exact "+name+" extrusion",std::move(p));
}
static void nativeBooleanDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Operation: union / cut / common","cut"},{"Base feature ID",selectedFeatureId()},{"Tool feature ID","1"}};
    if(!editFields("Exact solid Boolean","Select two authored solids. Native topology is preserved and validated.",fields))return;
    auto op=trimField(fields[0].value);if(op!="union"&&op!="cut"&&op!="common")throw std::runtime_error("Boolean operation must be union, cut, or common");
    auto base=featureIdField(fields[1].value),tool=featureIdField(fields[2].value);if(base==tool)throw std::runtime_error("Choose a distinct tool feature");
    V p=V::makeObj();p.set("op",V::makeStr(op));addNativeFeature(FeatureType::NativeSolid,"Exact "+op,std::move(p),V::makeObj(),{base,tool});
}
static void namedBooleanDialog(int mode){
    using V=arkjson::Value;if(!g_doc.assembly.empty())throw std::runtime_error("Use exact part definitions before assembly; placed occurrence Boolean authoring requires explicit instance inputs");if(g_bodyIds.size()<2)throw std::runtime_error("Create two output bodies before combining solids");const u64 revision=g_session.revision,epoch=g_documentEpoch;const char* titles[]={"Fuse bodies","Carve body","Keep overlap"};const char* operations[]={"union","cut","common"};std::string base=selectedFeatureId(),tool;for(auto id:g_bodyIds)if(std::to_string(id)!=base)tool=std::to_string(id);if(tool.empty()){base=std::to_string(g_bodyIds[g_bodyIds.size()-2]);tool=std::to_string(g_bodyIds.back());}std::vector<EditorField> fields{{"Result name",titles[mode]},{"Base feature ID",base},{"Tool feature ID",tool}};if(!editFields(titles[mode],mode==1?"The tool removes material from the base. Both source solids and their parameters stay in history.":"Uses the existing exact solid kernel. Inputs stay as editable source history; invalid geometry rejects atomically.",fields))return;auto first=featureIdField(fields[1].value),second=featureIdField(fields[2].value);if(first==second)throw std::runtime_error("Choose two distinct source bodies");V command=V::makeObj(),p=V::makeObj(),inputs=V::makeArr();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("native_solid"));command.set("name",V::makeStr(fields[0].value));p.set("op",V::makeStr(operations[mode]));command.set("p",p);inputs.arr={V::makeNum((double)first),V::makeNum((double)second)};command.set("inputs",inputs);applyUiCommandAtRevision(command,revision,epoch);if(!g_error){selBody=(int)g_bodyIds.size()-1;frameScene();}
}
static void nativeHoleDialog(){
    using V=arkjson::Value;std::vector<EditorField> choose{{"Cutter: circle / slot / profile","circle"}};
    if(!editFields("Native hole or pocket","Circle, slot, or a saved closed sketch profile.",choose))return;
    auto kind=trimField(choose[0].value);if(kind!="circle"&&kind!="slot"&&kind!="profile")throw std::runtime_error("Hole cutter must be circle, slot, or profile");
    std::vector<EditorField> fields{{"Base feature ID",selectedFeatureId()},{"Construction plane",g_activePlane},{"Centre X / mm","20"},{"Centre Y / mm","20"},{"Offset along normal / mm","-24"},{"Cut depth / mm","25"}};
    if(kind=="circle")fields.push_back({"Radius / mm","5"});
    else if(kind=="slot"){fields.push_back({"Overall length / mm","20"});fields.push_back({"Width / mm","6"});}
    else fields.push_back({"Closed sketch",g_activeSketch.empty()?(g_doc.sketches.empty()?"":g_doc.sketches.front().name):g_activeSketch});
    auto title="Exact "+kind+" cut";if(!editFields(title.c_str(),"Place on a named construction plane. The cutter extrudes along its positive normal.",fields))return;
    auto base=featureIdField(fields[0].value);auto plane=trimField(fields[1].value);resolvePlane(g_doc,plane);
    V cutter=V::makeObj(),native=V::makeObj(),p=V::makeObj();cutter.set("kind",V::makeStr(kind));cutter.set("plane",V::makeStr(plane));
    const char* names[]={"cx","cy","offset","depth"};for(int i=0;i<4;++i)cutter.set(names[i],V::makeStr(trimField(fields[(size_t)i+2].value)));
    if(kind=="circle")cutter.set("radius",V::makeStr(trimField(fields[6].value)));
    else if(kind=="slot"){cutter.set("length",V::makeStr(trimField(fields[6].value)));cutter.set("width",V::makeStr(trimField(fields[7].value)));}
    else{auto sketch=trimField(fields[6].value);if(!g_doc.findSketch(sketch))throw std::runtime_error("Choose a saved closed sketch");p.set("sketch",V::makeStr(sketch));}
    native.set("cutter",std::move(cutter));p.set("op",V::makeStr("hole"));addNativeFeature(FeatureType::NativeSolid,"Exact "+kind+" cut",std::move(p),std::move(native),{base});
}
static std::string nativeText(const Feature* feature,const char* key,const char* fallback="");
static std::string joinedNativeNames(const Feature* feature,const char* key){
    std::string text;if(!feature)return text;auto* list=feature->nativeSpec.find(key);if(!list||list->t!=arkjson::Value::Arr)return text;
    for(auto& item:list->arr){if(item.t!=arkjson::Value::Str||item.str.empty())continue;if(!text.empty())text+="; ";text+=item.str;}return text;
}
static void nativeEdgeBlendDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;
    std::string op=editing?editing->get("op","fillet"):"fillet";
    std::string listed=editing?nativeText(editing,"selection"):"";
    if(listed.empty())listed=editing?joinedNativeNames(editing,"edges"):pickedNames("edge");
    if(listed.empty())listed="edge.x.ymin.zmin";
    std::vector<EditorField> fields{{"Operation: fillet / chamfer",op}};
    if(!editing)fields.push_back({"Base feature ID",pickedFeatureId("edge")});
    fields.push_back({"Edges, or sharp / convex / concave / all",listed});
    fields.push_back({"Radius or distance / mm",editing?editing->get(op=="chamfer"?"distance":"radius","2"):"2"});
    fields.push_back({"Minimum dihedral / deg",editing?editing->get("min_angle","15"):"15"});
    auto hint=nativeReferenceHint("edge");if(!editFields(editing?"Edit fillet / chamfer":"Exact edge treatment",hint.c_str(),fields))return;
    op=trimField(fields[0].value);if(op!="fillet"&&op!="chamfer")throw std::runtime_error("Choose fillet or chamfer");
    auto choice=trimField(fields[editing?1:2].value);
    V native=V::makeObj(),p=V::makeObj();p.set("op",V::makeStr(op));p.set(op=="fillet"?"radius":"distance",V::makeStr(trimField(fields[editing?2:3].value)));
    if(choice=="sharp"||choice=="convex"||choice=="concave"||choice=="all"){native.set("selection",V::makeStr(choice));p.set("min_angle",V::makeStr(trimField(fields[editing?3:4].value)));}
    else{V edges=V::makeArr();std::istringstream names(choice);std::string name;while(std::getline(names,name,';'))if(!(name=trimField(name)).empty())edges.arr.push_back(V::makeStr(name));if(edges.arr.empty())throw std::runtime_error("Enter edge names or a selection");native.set("edges",std::move(edges));}
    if(editing)commitNativeFeature(FeatureType::NativeSolid,"Exact "+op,std::move(p),std::move(native),{},editing->id,true);
    else addNativeFeature(FeatureType::NativeSolid,"Exact "+op,std::move(p),std::move(native),{featureIdField(fields[1].value)});
}
static void nativeDraftDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;std::vector<EditorField> fields;
    if(!editing)fields.push_back({"Base feature ID",pickedFeatureId("face")});
    fields.push_back({"Angle / deg",editing?editing->get("angle","5"):"5"});
    fields.push_back({"Pull direction X / Y / Z",editing?nativeText(editing,"direction","Z"):"Z"});
    fields.push_back({"Neutral plane XY / XZ / YZ",editing?nativeText(editing,"neutral","XY"):"XY"});
    std::string faces=editing?joinedNativeNames(editing,"faces"):pickedNames("face");if(faces.empty())faces="sides";
    fields.push_back({"Faces, or sides",faces});
    if(!editFields(editing?"Edit draft":"Draft exact faces","Planar, cylindrical, and conical faces only. A freeform loft face is rejected.",fields))return;
    auto direction=trimField(fields[editing?1:2].value),neutral=trimField(fields[editing?2:3].value),faceText=trimField(fields[editing?3:4].value);
    if(direction!="X"&&direction!="Y"&&direction!="Z")throw std::runtime_error("Draft direction must be X, Y, or Z");
    if(neutral!="XY"&&neutral!="XZ"&&neutral!="YZ")throw std::runtime_error("Neutral plane must be XY, XZ, or YZ");
    V native=V::makeObj(),p=V::makeObj();p.set("op",V::makeStr("draft"));p.set("angle",V::makeStr(trimField(fields[editing?0:1].value)));native.set("direction",V::makeStr(direction));native.set("neutral",V::makeStr(neutral));
    if(faceText!="sides"){V names=V::makeArr();std::istringstream list(faceText);std::string name;while(std::getline(list,name,';'))if(!(name=trimField(name)).empty())names.arr.push_back(V::makeStr(name));if(names.arr.empty())throw std::runtime_error("Name the faces to draft, or choose sides");native.set("faces",std::move(names));}
    if(editing)commitNativeFeature(FeatureType::NativeSolid,"Draft",std::move(p),std::move(native),{},editing->id,true);
    else addNativeFeature(FeatureType::NativeSolid,"Draft",std::move(p),std::move(native),{featureIdField(fields[0].value)});
}
static void nativePatternDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;std::string op=editing?editing->get("op","linear_pattern"):"linear_pattern";
    std::vector<EditorField> fields{{"Pattern: linear / circular",op=="circular_pattern"?"circular":"linear"}};
    if(!editing)fields.push_back({"Base feature ID",selectedFeatureId()});
    fields.push_back({"Count",editing?editing->get("count","3"):"3"});
    fields.push_back({op=="circular_pattern"?"Step / deg":"Spacing X / mm",editing?editing->get(op=="circular_pattern"?"step":"dx","20"):(op=="circular_pattern"?"90":"20")});
    fields.push_back({"Spacing Y / mm",editing?editing->get("dy","0"):"0"});
    fields.push_back({"Spacing Z / mm",editing?editing->get("dz","0"):"0"});
    fields.push_back({"Axis X / Y / Z",editing?nativeText(editing,"axis","Z"):"Z"});
    fields.push_back({"Fuse into one solid",editing?editing->get("fuse","0"):"0",true});
    if(!editFields(editing?"Edit pattern":"Exact pattern","fuse 0 keeps separate exact solids. Circular step is degrees about the axis.",fields))return;
    auto kind=trimField(fields[0].value);if(kind!="linear"&&kind!="circular")throw std::runtime_error("Pattern must be linear or circular");
    int baseField=editing?0:1;V native=V::makeObj(),p=V::makeObj();
    p.set("op",V::makeStr(kind=="circular"?"circular_pattern":"linear_pattern"));p.set("count",V::makeStr(trimField(fields[baseField+1].value)));
    if(kind=="circular"){p.set("step",V::makeStr(trimField(fields[baseField+2].value)));auto axis=trimField(fields[baseField+5].value);if(axis!="X"&&axis!="Y"&&axis!="Z")throw std::runtime_error("Pattern axis must be X, Y, or Z");native.set("axis",V::makeStr(axis));}
    else{p.set("dx",V::makeStr(trimField(fields[baseField+2].value)));p.set("dy",V::makeStr(trimField(fields[baseField+3].value)));p.set("dz",V::makeStr(trimField(fields[baseField+4].value)));}
    p.set("fuse",V::makeStr(fields[baseField+6].value=="1"?"1":"0"));
    if(editing)commitNativeFeature(FeatureType::NativeSolid,"Exact pattern",std::move(p),std::move(native),{},editing->id,kind=="circular");
    else addNativeFeature(FeatureType::NativeSolid,"Exact pattern",std::move(p),std::move(native),{featureIdField(fields[1].value)});
}
static void nativeMirrorDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;std::vector<EditorField> fields;
    if(!editing)fields.push_back({"Base feature ID",pickedFeatureId("face")});
    std::string current=editing?nativeText(editing,"face"):"";if(current.empty())current=editing?nativeText(editing,"plane","XY"):suggestedMirror();
    fields.push_back({"Plane, datum, or one planar face",current});
    fields.push_back({"Fuse with the original",editing?editing->get("fuse","0"):"0",true});
    if(!editFields(editing?"Edit mirror":"Mirror exact solid","Select a planar face or a datum, or enter XY, XZ, YZ. The seed solid is kept. fuse 1 needs one connected solid.",fields))return;
    auto target=trimField(fields[editing?0:1].value);if(target.find(';')!=std::string::npos)throw std::runtime_error("Mirror uses one plane or one planar face");
    bool principal=target=="XY"||target=="XZ"||target=="YZ";
    bool datum=!principal&&std::any_of(g_doc.planes.begin(),g_doc.planes.end(),[&](const DatumPlane& plane){return plane.name==target;});
    V native=V::makeObj(),p=V::makeObj();p.set("op",V::makeStr("mirror"));p.set("fuse",V::makeStr(fields[editing?1:2].value=="1"?"1":"0"));
    if(principal||datum)native.set("plane",V::makeStr(target));
    else{if(target.empty())throw std::runtime_error("Choose XY, XZ, YZ, a datum, or a planar face");native.set("face",V::makeStr(target));}
    if(editing)commitNativeFeature(FeatureType::NativeSolid,"Mirror",std::move(p),std::move(native),{},editing->id,true);
    else addNativeFeature(FeatureType::NativeSolid,"Mirror",std::move(p),std::move(native),{featureIdField(fields[0].value)});
}
static void nativeTransformDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Base feature ID",selectedFeatureId()},{"X translation / mm","0"},{"Y translation / mm","0"},{"Z translation / mm","0"},{"X rotation / deg","0"},{"Y rotation / deg","0"},{"Z rotation / deg","0"}};
    if(!editFields("Exact rigid transform","A rigid placement of the selected native solid.",fields))return;
    auto base=featureIdField(fields[0].value);
    Mat4 transform=Mat4::translation({formNumber(fields[1].value),formNumber(fields[2].value),formNumber(fields[3].value)})*
        Mat4::rotZ(deg2rad(formNumber(fields[6].value)))*Mat4::rotY(deg2rad(formNumber(fields[5].value)))*Mat4::rotX(deg2rad(formNumber(fields[4].value)));
    V frame=V::makeArr(),native=V::makeObj(),p=V::makeObj();for(auto value:transform.m)frame.arr.push_back(V::makeNum(value));native.set("frame",std::move(frame));p.set("op",V::makeStr("transform"));
    addNativeFeature(FeatureType::NativeSolid,"Exact transform",std::move(p),std::move(native),{base});
}
static void supportedDatumDialog(){
    using V=arkjson::Value;std::vector<EditorField> choose{{"Name","Supported plane"},{"Support: face / midplane / normal_to_curve / tube_path","face"},{"Display: plane / axis / frame","plane"}};
    if(!editFields("Supported construction datum","Attach a construction frame to a semantic source reference.",choose))return;
    auto kind=trimField(choose[1].value),name=trimField(choose[0].value);if(name.empty())throw std::runtime_error("Datum needs a name");
    if(kind!="face"&&kind!="midplane"&&kind!="normal_to_curve"&&kind!="tube_path")throw std::runtime_error("Unsupported datum kind");
    std::vector<EditorField> fields{{"Source feature ID",selectedFeatureId()}};
    if(kind!="tube_path")fields.push_back({kind=="normal_to_curve"?"Edge or axis reference":"Planar face reference",kind=="normal_to_curve"?"axis":"z_max"});
    if(kind=="midplane"){fields.push_back({"Second feature ID",selectedFeatureId()});fields.push_back({"Second planar face reference","z_min"});}
    if(kind=="normal_to_curve")fields.push_back({"Fraction along curve / 0..1","0.5"});
    if(kind=="tube_path")fields.push_back({"Station along tube / mm","0"});
    auto hint=nativeReferenceHint(kind=="normal_to_curve"?"edge":"face");
    auto title="Attach datum: "+kind;if(!editFields(title.c_str(),hint.c_str(),fields))return;
    V support=V::makeObj(),command=V::makeObj();support.set("kind",V::makeStr(kind));support.set("feature",V::makeNum((double)featureIdField(fields[0].value)));
    size_t index=1;if(kind!="tube_path")support.set("reference",V::makeStr(trimField(fields[index++].value)));
    if(kind=="midplane"){support.set("second_feature",V::makeNum((double)featureIdField(fields[index++].value)));support.set("second_reference",V::makeStr(trimField(fields[index++].value)));}
    if(kind=="normal_to_curve")support.set("fraction",V::makeStr(trimField(fields[index].value)));
    if(kind=="tube_path")support.set("station",V::makeStr(trimField(fields[index].value)));
    command.set("op",V::makeStr("plane.set"));command.set("name",V::makeStr(name));command.set("kind",V::makeStr(trimField(choose[2].value)));command.set("support",std::move(support));applyCommand(command);
    if(!g_error){g_activePlane=name;g_activeSketch.clear();g_showDatumPlanes=true;g_showSketches=true;rebuildSketchOverlay();tTarget=resolvePlane(g_doc,name).transformPoint({});tDist=130;}
}
static void planeDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Plane name","Workplane"},{"Parent plane",g_activePlane},{"Offset X / mm","0"},{"Offset Y / mm","0"},{"Offset Z / mm","0"},{"Rotation X / deg","0"},{"Rotation Y / deg","0"},{"Rotation Z / deg","0"}};
    const char* keys[]={"ox","oy","oz","rx","ry","rz"};
    std::string displayKind="plane";for(auto& plane:g_doc.planes)if(plane.name==g_activePlane){fields[0].value=plane.name;fields[1].value=plane.parent;displayKind=plane.kind;for(int i=0;i<6;++i)fields[i+2].value=plane.p.count(keys[i])?plane.p.at(keys[i]):"0";}
    fields.push_back({"Display: plane / axis / frame",displayKind});
    if(!editFields("Construction plane","Built-ins: XY, XZ, YZ. Named planes accept dimensions and parameter expressions.",fields))return;
    V c=V::makeObj(),params=V::makeObj();c.set("op",V::makeStr("plane.set"));c.set("name",V::makeStr(fields[0].value));c.set("parent",V::makeStr(fields[1].value));c.set("kind",V::makeStr(trimField(fields[8].value)));for(int i=0;i<6;++i)params.set(keys[i],V::makeStr(fields[i+2].value));c.set("p",params);applyCommand(c);
    if(!g_error){g_activePlane=fields[0].value;g_activeSketch.clear();g_showSketches=true;rebuildSketchOverlay();auto frame=resolvePlane(g_doc,g_activePlane);tTarget=frame.transformPoint({0,0,0});tDist=200;}
}
static void shapeSketchDialog(int shape){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Sketch name",shape==0?"Profile":shape==1?"Holes":"Freeform"},{"Plane",g_activePlane}};
    if(shape==0)fields.insert(fields.end(),{{"Origin X / mm","0"},{"Origin Y / mm","0"},{"Width / mm","100"},{"Height / mm","60"}});
    else if(shape==1)fields.insert(fields.end(),{{"Centre X / mm","25"},{"Centre Y / mm","30"},{"Radius / mm","4"}});
    else fields.insert(fields.end(),{{"Anchors x,y; x,y ...","-40,0; -25,-25; 25,-25; 40,0; 25,25; -25,25"},{"Closed curve","1",true},{"Tension / 0 to 1","0"},{"Tolerance / mm","0.05"}});
    if(!editFields(shape==0?"Dimensioned rectangle":shape==1?"Dimensioned circle":"Interpolating spline","Coordinates accept expressions. Existing sketch names append a shape; use a new name for cuts.",fields))return;
    V sketch=V::makeObj();if(auto* found=g_doc.findSketch(fields[0].value))arkjson::parse(sketchToJson(found->sk),sketch);
    if(!sketch.find("entities"))sketch.set("entities",V::makeArr());if(!sketch.find("constraints"))sketch.set("constraints",V::makeArr());sketch.set("plane",V::makeStr(fields[1].value));V entities=*sketch.find("entities");
    auto entity=[&](const char* kind,std::vector<std::string> coordinates){V e=V::makeObj(),dof=V::makeArr();e.set("kind",V::makeStr(kind));e.set("name",V::makeStr(std::string(kind)+std::to_string(entities.arr.size()+1)));for(auto& x:coordinates)dof.arr.push_back(V::makeStr(x));e.set("dof",dof);return e;};
    if(shape==0){auto x=fields[2].value,y=fields[3].value,x1="("+x+")+("+fields[4].value+")",y1="("+y+")+("+fields[5].value+")";
        entities.arr.push_back(entity("line",{x,y,x1,y}));entities.arr.push_back(entity("line",{x1,y,x1,y1}));entities.arr.push_back(entity("line",{x1,y1,x,y1}));entities.arr.push_back(entity("line",{x,y1,x,y}));}
    else if(shape==1)entities.arr.push_back(entity("circle",{fields[2].value,fields[3].value,fields[4].value}));
    else {std::vector<std::string> coordinates;std::istringstream pairs(fields[2].value);std::string pair;while(std::getline(pairs,pair,';')){size_t comma=std::string::npos;int depth=0;for(size_t i=0;i<pair.size();++i){if(pair[i]=='(')++depth;else if(pair[i]==')')--depth;else if(pair[i]==','&&depth==0){if(comma!=std::string::npos)throw std::runtime_error("Each anchor has exactly two coordinates");comma=i;}if(depth<0)throw std::runtime_error("Unbalanced anchor expression");}if(comma==std::string::npos||depth)throw std::runtime_error("Each anchor needs x,y; separate anchors with semicolons.");coordinates.push_back(pair.substr(0,comma));coordinates.push_back(pair.substr(comma+1));}
        auto e=entity("spline",coordinates);e.set("closed",V::makeBool(formNumber(fields[3].value)!=0));e.set("tension",V::makeNum(formNumber(fields[4].value)));sketch.set("tolerance",V::makeNum(formNumber(fields[5].value)));entities.arr.push_back(e);}
    sketch.set("entities",entities);V c=V::makeObj();c.set("op",V::makeStr("sketch.set"));c.set("name",V::makeStr(fields[0].value));c.set("sketch",sketch);applyCommand(c);
    if(!g_error){g_toolModule=2;g_activeSketch=fields[0].value;g_activePlane=fields[1].value;g_showSketches=true;rebuildSketchOverlay();if(g_sketchBounds.valid()){tTarget=g_sketchBounds.center();tDist=std::max(80.,g_sketchBounds.diagonal()*1.8);}}
}
static void basicCurveSketchDialog(int mode){
    using V=arkjson::Value;const u64 revision=g_session.revision,epoch=g_documentEpoch;
    std::vector<EditorField> fields{{"Sketch name",g_activeSketch.empty()?"Curves":g_activeSketch},{"Plane",g_activePlane}};
    if(mode==0)fields.insert(fields.end(),{{"Start X / mm","0"},{"Start Y / mm","0"},{"End X / mm","60"},{"End Y / mm","0"}});
    else if(mode==3)fields.insert(fields.end(),{{"Centre X / mm","0"},{"Centre Y / mm","0"},{"Radius / mm","30"},{"Start angle / deg","0"},{"End angle / deg","180"}});
    else fields.insert(fields.end(),{{"Start X / mm","0"},{"Start Y / mm","0"},{mode==1?"Through X / mm":"Start tangent X","30"},{mode==1?"Through Y / mm":"Start tangent Y","30"},{"End X / mm","60"},{"End Y / mm","0"}});
    if(mode==2){fields[4].value="0";fields[5].value="1";}
    const char* title=mode==0?"Sketch line":mode==1?"Sketch arc through three points":mode==2?"Sketch arc from start tangent":"Sketch arc from centre and angles";
    if(!editFields(title,"Coordinates accept named expressions. Append to an existing sketch on its plane. Arcs remain editable from their authored points or tangent.",fields))return;
    if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("Drawing changed while authoring the curve; reopen its current sketch.");
    V sketch=V::makeObj();if(auto* found=g_doc.findSketch(fields[0].value)){if(found->sk.plane!=fields[1].value)throw std::runtime_error("Existing sketch uses another plane; preserve it or choose a new sketch name.");arkjson::parse(sketchToJson(found->sk),sketch);}
    if(!sketch.find("entities"))sketch.set("entities",V::makeArr());if(!sketch.find("constraints"))sketch.set("constraints",V::makeArr());sketch.set("plane",V::makeStr(fields[1].value));V entities=*sketch.find("entities"),entity=V::makeObj();const std::string kind=mode==0?"line":"arc";
    entity.set("kind",V::makeStr(kind));unsigned serial=(unsigned)entities.arr.size()+1;std::string name;do{name=kind+std::to_string(serial++);}while(std::any_of(entities.arr.begin(),entities.arr.end(),[&](const V& e){auto* value=e.find("name");return value&&value->asStr()==name;}));entity.set("name",V::makeStr(name));
    if(mode==0||mode==3){V dof=V::makeArr();for(size_t i=2;i<fields.size();++i)dof.arr.push_back(V::makeStr(fields[i].value));entity.set("dof",dof);}
    else {V recipe=V::makeObj();recipe.set("type",V::makeStr(mode==1?"three_point":"tangent"));auto pair=[&](size_t at){V point=V::makeArr();point.arr.push_back(V::makeStr(fields[at].value));point.arr.push_back(V::makeStr(fields[at+1].value));return point;};recipe.set("start",pair(2));recipe.set(mode==1?"through":"tangent",pair(4));recipe.set("end",pair(6));entity.set("recipe",recipe);}
    entities.arr.push_back(entity);sketch.set("entities",entities);V command=V::makeObj();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(fields[0].value));command.set("sketch",sketch);applyCommand(command);
    if(!g_error){g_toolModule=2;g_activeSketch=fields[0].value;g_activePlane=fields[1].value;g_showSketches=true;rebuildSketchOverlay();alignSketchCamera();frameScene();g_status=std::string(title)+" saved / authored expressions retained";}
}
static void editSketchDialog(){
    std::vector<EditorField> select{{"Sketch name",g_activeSketch.empty()?(g_doc.sketches.empty()?"":g_doc.sketches.front().name):g_activeSketch}};
    if(!editFields("Edit saved sketch",sketchNames().c_str(),select))return;auto* found=g_doc.findSketch(select[0].value);if(!found)throw std::runtime_error("Unknown sketch name");
    std::string text=sketchToJson(found->sk);if(!editText("Sketch entities and constraints / expressions preserved",text,true))return;
    using V=arkjson::Value;V sk;if(!arkjson::parse(text,sk))throw std::runtime_error("Invalid sketch JSON");V c=V::makeObj();c.set("op",V::makeStr("sketch.set"));c.set("name",V::makeStr(select[0].value));c.set("sketch",sk);applyCommand(c);if(!g_error){g_activeSketch=select[0].value;g_showSketches=true;rebuildSketchOverlay();}
}
static NamedSketch* editableSketch(){
    if(!g_activeSketch.empty())if(auto* sketch=g_doc.findSketch(g_activeSketch))return sketch;
    return g_doc.sketches.empty()?nullptr:&g_doc.sketches.front();
}
static arkjson::Value sketchEditValue(const NamedSketch& named){arkjson::Value value;if(!arkjson::parse(sketchToJson(named.sk),value))throw std::runtime_error("Could not read sketch for editing");return value;}
static void applySketchEdit(std::string name,arkjson::Value sketch){
    using V=arkjson::Value;V probe=V::makeObj();probe.set("sketch",sketch);
    auto result=g_session.call("sketch_diagnose",probe);
    const auto* content=result.find("structuredContent");
    bool toolError=result.find("isError")&&result.find("isError")->b;
    std::string evaluationError;if(content)if(auto* error=content->find("error");error&&error->t==V::Str)evaluationError=error->str;
    int violated=0,freeDofs=0;
    if(!toolError&&content){if(auto* count=content->find("violated_constraints");count&&count->t==V::Num)violated=(int)count->num;
        if(auto* count=content->find("remaining_dofs");count&&count->t==V::Num)freeDofs=(int)count->num;}
    if(toolError||!evaluationError.empty()||violated){
        std::ostringstream detail;detail<<"Sketch edit was not applied. ";
        if(content)if(auto* constraints=content->find("constraints");constraints&&constraints->t==V::Arr){
            int shown=0;for(const auto& item:constraints->arr){auto* bad=item.find("violated");if(!bad||bad->t!=V::Bool||!bad->b)continue;
                if(shown++>=4){detail<<" More constraints are violated.";break;}
                auto* id=item.find("id"),*index=item.find("index"),*type=item.find("type"),*residual=item.find("max_residual");
                detail<<"\n  "<<(id&&id->t==V::Str?id->str:index&&index->t==V::Num?"#"+std::to_string((int)index->num+1):"constraint")
                      <<" ("<<(type&&type->t==V::Str?type->str:"unknown")<<")";
                if(residual&&residual->t==V::Num)detail<<" |residual|="<<std::setprecision(4)<<residual->num;
            }
        }
        if(!evaluationError.empty())detail<<"\n"<<evaluationError;
        detail<<"\nLocal free dimensions: "<<freeDofs<<'.';
        g_status=detail.str();g_error=true;
        if(!g_dialogSmoke)MessageBoxA(g_window,g_status.c_str(),"Sketch constraint diagnosis",MB_OK|MB_ICONWARNING);
        return;
    }
    V c=V::makeObj();c.set("op",V::makeStr("sketch.set"));c.set("name",V::makeStr(name));c.set("sketch",std::move(sketch));applyCommand(c);
    if(!g_error){g_activeSketch=name;g_showSketches=true;rebuildSketchOverlay();}
}
static std::string sketchFieldText(const arkjson::Value& value){
    if(value.t==arkjson::Value::Str)return value.str;
    if(value.t==arkjson::Value::Num){std::ostringstream s;s.precision(16);s<<value.num;return s.str();}
    return "0";
}
static int sketchEntityChoice(const Sketch& sketch,const std::string& choice){
    auto name=trimField(choice);for(size_t i=0;i<sketch.ents.size();++i)if(!sketch.ents[i].name.empty()&&sketch.ents[i].name==name)return (int)i;
    if(!name.empty()&&name[0]=='#')name.erase(0,1);
    try{size_t used=0;int number=std::stoi(name,&used);if(used==name.size()&&number>=1&&number<=(int)sketch.ents.size())return number-1;}catch(...){}
    throw std::runtime_error("Choose an existing entity by name or 1-based number");
}
static std::string entityChoiceLabel(const Sketch& sketch,int index){return index>=0&&index<(int)sketch.ents.size()&&!sketch.ents[(size_t)index].name.empty()?sketch.ents[(size_t)index].name:std::to_string(index+1);}
static void renameSketchEntity(){
    using V=arkjson::Value;auto* selected=editableSketch();for(auto it=g_picked.rbegin();it!=g_picked.rend();++it)if(it->kind=="curve"&&!it->owner.empty()){selected=g_doc.findSketch(it->owner);break;}if(!selected||selected->sk.ents.empty())throw std::runtime_error("Choose a sketch with an entity to name");NamedSketch snapshot=*selected;const u64 revision=g_session.revision,epoch=g_documentEpoch;
    std::vector<EditorField> pick{{"Entity name or #",entityChoiceLabel(snapshot.sk,0)}};if(!editFields("Choose entity to rename","Select the existing entity by its name or 1-based number.",pick))return;int index=sketchEntityChoice(snapshot.sk,pick[0].value);
    std::vector<EditorField> fields{{"Stable entity name",snapshot.sk.ents[(size_t)index].name.empty()?"Curve1":snapshot.sk.ents[(size_t)index].name}};if(!editFields("Rename sketch entity","Coordinates and constraints stay the same. Existing projection links are not silently rebound to a new name.",fields))return;auto name=trimField(fields[0].value);if(name.empty())throw std::runtime_error("Entity name cannot be empty");for(size_t i=0;i<snapshot.sk.ents.size();++i)if((int)i!=index&&snapshot.sk.ents[i].name==name)throw std::runtime_error("Choose a unique entity name");if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("The drawing changed while naming the entity. Reopen the current sketch.");
    V sketch=sketchEditValue(snapshot),entities=*sketch.find("entities");entities.arr[(size_t)index].set("name",V::makeStr(name));sketch.set("entities",entities);V command=V::makeObj(),args=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(snapshot.name));command.set("sketch",sketch);commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)revision));runTool("doc_apply",args);if(!g_error){g_picked.clear();refreshSelectionLines();g_activeSketch=snapshot.name;g_showSketches=true;rebuildSketchOverlay();g_status="Sketch entity named / select "+name+" to project it";}
}
static void editSketchRecipeFields(const char*,const SketchEnt*);
static void editSketchEntityFields(){
    auto* named=editableSketch();if(!named)throw std::runtime_error("Create a sketch first");NamedSketch snapshot=*named;named=&snapshot;const u64 revision=g_session.revision,epoch=g_documentEpoch;
    std::string choices;for(size_t i=0;i<named->sk.ents.size();++i)choices+=(choices.empty()?"":", ")+entityChoiceLabel(named->sk,(int)i);
    std::string selectedEntity=named->sk.ents.empty()?"":entityChoiceLabel(named->sk,0);for(const auto& pick:g_picked)if(pick.kind=="curve"&&pick.owner==named->name&&!pick.name.empty())selectedEntity=pick.name;
    std::vector<EditorField> choice{{"Entity name or #",selectedEntity}};
    if(!editFields("Select sketch entity",choices.c_str(),choice))return;
    int index=sketchEntityChoice(named->sk,choice[0].value);auto sketch=sketchEditValue(*named);auto entities=*sketch.find("entities");auto& entity=entities.arr[(size_t)index];
    if(named->sk.ents[(size_t)index].kind==SketchEnt::Recipe){const auto& selected=named->sk.ents[(size_t)index];auto* type=selected.entityRecipe.find("type");if(!type||type->t!=arkjson::Value::Str)throw std::runtime_error("Recipe type is missing");g_activeSketch=named->name;g_activePlane=named->sk.plane;editSketchRecipeFields(type->str.c_str(),&selected);return;}
    if(auto* authored=entity.find("recipe")){auto recipe=*authored;bool tangent=recipe.find("type")->asStr()=="tangent";std::vector<EditorField> fields;const char* keys[]={"start",tangent?"tangent":"through","end"};for(auto* key:keys){auto* point=recipe.find(key);fields.push_back({std::string(key)+" X",sketchFieldText(point->arr[0])});fields.push_back({std::string(key)+" Y",sketchFieldText(point->arr[1])});}if(!editFields("Edit authored arc",tangent?"Start, tangent and end drive this exact arc.":"Start, through and end drive this exact arc.",fields))return;for(size_t i=0;i<3;++i){auto point=*recipe.find(keys[i]);point.arr[0]=arkjson::Value::makeStr(fields[2*i].value);point.arr[1]=arkjson::Value::makeStr(fields[2*i+1].value);recipe.set(keys[i],point);}entity.set("recipe",recipe);sketch.set("entities",entities);if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("Drawing changed while editing the arc.");applySketchEdit(named->name,sketch);return;}
    if(named->sk.ents[(size_t)index].kind==SketchEnt::Project||named->sk.ents[(size_t)index].kind==SketchEnt::Offset)throw std::runtime_error("This curve is driven by its source. Edit the source geometry.");
    if(named->sk.ents[(size_t)index].kind==SketchEnt::Wire)throw std::runtime_error("Edit this segmented wire with Edit saved sketch to preserve its segment recipe.");
    auto dof=*entity.find("dof");auto& coordinates=dof.arr;
    bool spline=named->sk.ents[(size_t)index].kind==SketchEnt::Spline;
    int anchor=0;
    if(spline){std::vector<EditorField> pick{{"Anchor # (1-based)","1"}};if(!editFields("Select spline anchor","Edit an interpolating anchor; the rest of the spline is preserved.",pick))return;
        try{anchor=std::stoi(trimField(pick[0].value))-1;}catch(...){throw std::runtime_error("Invalid spline anchor number");}
        if(anchor<0||2*anchor+1>=(int)coordinates.size())throw std::runtime_error("Spline anchor is outside this curve");}
    std::vector<EditorField> fields;
    static const char* pointLabels[]={"X / mm","Y / mm"};static const char* lineLabels[]={"Start X / mm","Start Y / mm","End X / mm","End Y / mm"};static const char* circleLabels[]={"Centre X / mm","Centre Y / mm","Radius / mm"};static const char* arcLabels[]={"Centre X / mm","Centre Y / mm","Radius / mm","Start angle / deg","End angle / deg"};
    const char** labels=spline?pointLabels:named->sk.ents[(size_t)index].kind==SketchEnt::Line?lineLabels:named->sk.ents[(size_t)index].kind==SketchEnt::Circle?circleLabels:named->sk.ents[(size_t)index].kind==SketchEnt::Arc?arcLabels:pointLabels;
    size_t start=spline?(size_t)anchor*2:0,count=spline?2:coordinates.size();
    for(size_t i=0;i<count;++i)fields.push_back({labels[i],sketchFieldText(coordinates[start+i])});
    if(!editFields("Edit sketch coordinates","Coordinates may be numbers or named parameter expressions.",fields))return;
    for(size_t i=0;i<count;++i)coordinates[start+i]=arkjson::Value::makeStr(trimField(fields[i].value));
    entity.set("dof",std::move(dof));sketch.set("entities",std::move(entities));
    if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("Drawing changed while editing the sketch.");applySketchEdit(named->name,std::move(sketch));
}
static void editSketchConstraintFields(bool add){
    auto* named=editableSketch();if(!named||named->sk.ents.empty())throw std::runtime_error("Create sketch entities first");
    int index=g_selectedSketchConstraint;
    if(!add&&(index<0||index>=(int)named->sk.cons.size())){
        if(named->sk.cons.empty())throw std::runtime_error("Sketch has no constraints to edit");
        std::vector<EditorField> pick{{"Constraint # (1-based)","1"}};
        if(!editFields("Select constraint","Select a constraint from the inspector list or enter its number.",pick))return;
        try{index=std::stoi(trimField(pick[0].value))-1;}catch(...){throw std::runtime_error("Invalid constraint number");}
        if(index<0||index>=(int)named->sk.cons.size())throw std::runtime_error("Constraint number is outside this sketch");
        g_selectedSketchConstraint=index;rebuildSketchOverlay();
    }
    auto sketch=sketchEditValue(*named);auto constraints=*sketch.find("constraints");auto& list=constraints.arr;
    arkjson::Value existing=add?arkjson::Value::makeObj():list[(size_t)index];
    auto number=[&](const char* key,int fallback){auto* v=existing.find(key);return v&&v->t==arkjson::Value::Num?(int)v->num:fallback;};
    auto value=[&](const char* key){auto* v=existing.find(key);return v?sketchFieldText(*v):std::string("0");};
    std::string type=add?"fixedX":existing.find("type")->asStr();
    std::vector<EditorField> fields{{"Type",type},{"Entity A name or #",entityChoiceLabel(named->sk,std::clamp(number("a",0),0,(int)named->sk.ents.size()-1))},{"Point A # (0=start, 1=end)",std::to_string(number("pa",0))},{"Entity B name or #",entityChoiceLabel(named->sk,std::clamp(number("b",named->sk.ents.size()>1?1:0),0,(int)named->sk.ents.size()-1))},{"Point B # (0=start, 1=end)",std::to_string(number("pb",0))},{"Value / expression",value("val")},{"Second value / expression",value("val2")}};
    const char* hint="Types: fixedX, fixedY, fixed, distance, horizontal, vertical, parallel, perpendicular, radius, coincident, angle. Angle uses radians or deg(...).";
    if(!editFields(add?"Add sketch constraint":"Edit sketch constraint",hint,fields))return;
    i32 parsedType;if(!constraintTypeFromName(trimField(fields[0].value),parsedType))throw std::runtime_error("Unknown sketch constraint type");
    using V=arkjson::Value;V edited=existing;edited.set("type",V::makeStr(constraintTypeName(parsedType)));
    edited.set("a",V::makeNum(sketchEntityChoice(named->sk,fields[1].value)));
    edited.set("pa",V::makeNum(formNumber(fields[2].value)));
    edited.set("b",V::makeNum(sketchEntityChoice(named->sk,fields[3].value)));
    edited.set("pb",V::makeNum(formNumber(fields[4].value)));
    edited.set("val",V::makeStr(trimField(fields[5].value)));
    edited.set("val2",V::makeStr(trimField(fields[6].value)));
    if(add){int next=1;for(auto& con:list)if(auto* id=con.find("id");id&&id->t==V::Str&&id->str.size()>1&&id->str[0]=='c'){
            try{next=std::max(next,std::stoi(id->str.substr(1))+1);}catch(...){}}
        edited.set("id",V::makeStr("c"+std::to_string(next)));list.push_back(std::move(edited));index=(int)list.size()-1;
    }else list[(size_t)index]=std::move(edited);
    sketch.set("constraints",std::move(constraints));
    applySketchEdit(named->name,std::move(sketch));if(!g_error){g_selectedSketchConstraint=index;rebuildSketchOverlay();}
}
static void removeSketchConstraint(){
    auto* named=editableSketch();if(!named||named->sk.cons.empty())throw std::runtime_error("Sketch has no constraints to remove");
    if(g_selectedSketchConstraint<0||g_selectedSketchConstraint>=(int)named->sk.cons.size()){
        std::vector<EditorField> pick{{"Constraint # (1-based)","1"}};
        if(!editFields("Select constraint to remove","Select from the inspector list or enter its number.",pick))return;
        try{g_selectedSketchConstraint=std::stoi(trimField(pick[0].value))-1;}catch(...){throw std::runtime_error("Invalid constraint number");}
        if(g_selectedSketchConstraint<0||g_selectedSketchConstraint>=(int)named->sk.cons.size())throw std::runtime_error("Constraint number is outside this sketch");
    }
    auto sketch=sketchEditValue(*named);auto constraints=*sketch.find("constraints");auto& list=constraints.arr;list.erase(list.begin()+g_selectedSketchConstraint);sketch.set("constraints",std::move(constraints));
    auto name=named->name;applySketchEdit(name,std::move(sketch));if(!g_error){g_selectedSketchConstraint=-1;rebuildSketchOverlay();}
}
static void sketchFeatureDialog(bool cut){
    std::vector<EditorField> fields{{"Sketch name",g_activeSketch.empty()?(g_doc.sketches.empty()?"":g_doc.sketches.front().name):g_activeSketch},{cut?"Cut depth / mm":"Extrusion / mm",cut?"12":"10"}};
    if(cut){fields.push_back({"Offset / mm","-1"});fields.push_back({"Base feature ID",selBody>=0?std::to_string(g_bodyIds[selBody]):"1"});}else fields.push_back({"Symmetric extrusion","0",true});
    if(!editFields(cut?"Cut from sketch":"Extrude sketch",sketchNames().c_str(),fields))return;
    using V=arkjson::Value;V c=V::makeObj(),p=V::makeObj();c.set("op",V::makeStr("feature.add"));c.set("type",V::makeStr(cut?"sketch_cut":"extrude"));p.set("sketch",V::makeStr(fields[0].value));p.set(cut?"depth":"height",V::makeStr(fields[1].value));p.set(cut?"offset":"both",V::makeStr(fields[2].value));c.set("p",p);
    if(cut){V inputs=V::makeArr();inputs.arr.push_back(V::makeNum(formNumber(fields[3].value)));c.set("inputs",inputs);}applyCommand(c);if(!g_error){selBody=(int)g_bodyIds.size()-1;g_showSketches=false;frameScene();}
}
static void parameterDialog(){std::vector<EditorField> fields{{"Parameter name","width"},{"Expression","100"}};if(!editFields("Named parameter","Create or update a dimension shared by sketches and features.",fields))return;using V=arkjson::Value;V c=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(fields[0].value));c.set("expr",V::makeStr(fields[1].value));applyCommand(c);}
static std::filesystem::path g_settingsOverride;
static std::filesystem::path uiSettingsPath(){if(!g_settingsOverride.empty())return g_settingsOverride;auto* base=_wgetenv(L"LOCALAPPDATA");return (base?std::filesystem::path(base):std::filesystem::temp_directory_path())/"ARK"/"studio.json";}
static const char* connectorAuthName(AssistantAuthMode mode){return mode==AssistantAuthMode::ChatGptPlan?"chatgpt_plan":mode==AssistantAuthMode::CliAccount?"cli-account":"api_key";}
static AssistantAuthMode connectorAuthMode(const std::string& value){if(value=="chatgpt_plan")return AssistantAuthMode::ChatGptPlan;if(value=="cli-account")return AssistantAuthMode::CliAccount;if(value=="api_key")return AssistantAuthMode::ApiKey;throw std::runtime_error("Unknown stored connector account mode");}
static arkjson::Value connectorSettings(const AssistantConfig& config){using V=arkjson::Value;V v=V::makeObj();v.set("provider",V::makeStr(config.provider));v.set("auth_mode",V::makeStr(connectorAuthName(config.authMode)));v.set("protocol",V::makeStr(assistantProtocolName(config.protocol)));v.set("endpoint",V::makeStr(config.endpoint));v.set("api_key_env",V::makeStr(config.apiKeyEnv));v.set("max_output_tokens",V::makeNum(config.maxOutputTokens));v.set("image_input_supported",V::makeBool(config.imageInputSupported));v.set("timeout_seconds",V::makeNum(config.timeoutSeconds));return v;}
static void readConnectorSettings(const arkjson::Value& v,AssistantConfig& config){using V=arkjson::Value;if(auto* p=v.find("auth_mode")){if(p->t!=V::Str)throw std::runtime_error("Invalid stored connector account mode");config.authMode=connectorAuthMode(p->str);}if(auto* p=v.find("image_input_supported")){if(p->t!=V::Bool)throw std::runtime_error("Invalid stored image capability");config.imageInputSupported=p->b;}if(auto* p=v.find("timeout_seconds")){if(p->t!=V::Num||p->num<1||p->num>300||std::floor(p->num)!=p->num)throw std::runtime_error("Invalid stored provider timeout");config.timeoutSeconds=(unsigned)p->num;}if(auto* p=v.find("provider");p&&p->t==V::Str)config.provider=p->str;if(auto* p=v.find("protocol");p&&p->t==V::Str)config.protocol=assistantProtocolFromName(p->str);if(auto* p=v.find("endpoint");p&&p->t==V::Str)config.endpoint=p->str;if(auto* p=v.find("api_key_env");p&&p->t==V::Str)config.apiKeyEnv=p->str;if(auto* p=v.find("max_output_tokens")){if(p->t!=V::Num||p->num<1||p->num>131072||std::floor(p->num)!=p->num)throw std::runtime_error("Stored Messages response limit must be an integer from 1 to 131072");config.maxOutputTokens=(unsigned)p->num;}}
static void saveUiSettings(){
    using V=arkjson::Value;V j=V::makeObj();j.set("endpoint",V::makeStr(g_assistantConfig.endpoint));j.set("model",V::makeStr(g_assistantConfig.model));j.set("reasoning_effort",V::makeStr(g_assistantConfig.reasoningEffort));j.set("api_key_env",V::makeStr(g_assistantConfig.apiKeyEnv));j.set("assistant_auth_mode",V::makeStr(connectorAuthName(g_assistantConfig.authMode)));j.set("grid",V::makeBool(g_grid));j.set("wireframe",V::makeBool(wireframe));j.set("light_background",V::makeBool(g_lightBackground));j.set("satin",V::makeBool(g_satin));j.set("show_datum_planes",V::makeBool(g_showDatumPlanes));j.set("occlusion_mode",V::makeNum(g_occlusionMode));j.set("occlusion_strength",V::makeNum(g_occlusionStrength));j.set("occlusion_radius",V::makeNum(g_occlusionRadius));j.set("path_tracing",V::makeBool(g_pathTracing));j.set("show_edges",V::makeBool(g_showEdges));j.set("orthographic",V::makeBool(g_cameraOrtho));
    V team=V::makeArr();for(size_t i=0;i<teamCapacity;++i){V role=V::makeObj();role.set("model",V::makeStr(g_teamModels[i]));role.set("reasoning_effort",V::makeStr(g_teamEfforts[i]));team.arr.push_back(std::move(role));}j.set("team_roles",std::move(team));j.set("team_role_count",V::makeNum(g_teamRoleCount));j.set("team_focused_workflow",V::makeBool(g_teamFocusedWorkflow));j.set("team_mode",V::makeBool(g_teamMode));j.set("library_root",V::makeStr(g_libraryRoot));j.set("library_search",V::makeStr(g_libraryText));
    j.set("assistant_dock_open",V::makeBool(g_assistantDockOpen));j.set("display_units",g_displayUnits.json());j.set("connector",connectorSettings(g_assistantConfig));V connectors=V::makeArr();for(size_t i=0;i<teamCapacity;++i)connectors.arr.push_back(g_teamConnectors[i]?connectorSettings(*g_teamConnectors[i]):V{});j.set("team_connectors",connectors);
    auto path=uiSettingsPath();std::filesystem::create_directories(path.parent_path());std::ofstream out(path);out<<j.dump();if(!out)throw std::runtime_error("Could not save ARK settings");
}
static void loadUiSettings(){
    try{std::ifstream in(uiSettingsPath());if(!in)return;std::string data((std::istreambuf_iterator<char>(in)),{});arkjson::Value j;if(!arkjson::parse(data,j))return;
        if(auto* units=j.find("display_units")){try{g_displayUnits=DisplayUnits::fromJson(*units);}catch(...){g_displayUnits={};}}
        if(auto* v=j.find("endpoint"))g_assistantConfig.endpoint=v->asStr();if(auto* v=j.find("model"))g_assistantConfig.model=v->asStr();if(auto* v=j.find("reasoning_effort");v&&v->t==arkjson::Value::Str)g_assistantConfig.reasoningEffort=v->str;if(auto* v=j.find("api_key_env"))g_assistantConfig.apiKeyEnv=v->asStr();
        if(auto* v=j.find("assistant_auth_mode");v&&v->t==arkjson::Value::Str)g_assistantConfig.authMode=connectorAuthMode(v->str);
        if(auto* v=j.find("connector");v&&v->t==arkjson::Value::Obj)readConnectorSettings(*v,g_assistantConfig);
        if(auto* connectors=j.find("team_connectors");connectors&&connectors->t==arkjson::Value::Arr)for(size_t i=0;i<teamCapacity&&i<connectors->arr.size();++i)if(connectors->arr[i].t==arkjson::Value::Obj){AssistantConfig config;config.authMode=AssistantAuthMode::ApiKey;readConnectorSettings(connectors->arr[i],config);g_teamConnectors[i]=config;}
        if(auto* v=j.find("team_mode");v&&v->t==arkjson::Value::Bool)g_teamMode=v->b;
        if(auto* v=j.find("assistant_dock_open")){if(v->t!=arkjson::Value::Bool)throw std::runtime_error("Invalid saved chat visibility");g_assistantDockOpen=v->b;}
        if(auto* v=j.find("library_root");v&&v->t==arkjson::Value::Str)g_libraryRoot=v->str;if(auto* v=j.find("library_search");v&&v->t==arkjson::Value::Str)g_libraryText=v->str;
        if(auto* count=j.find("team_role_count");count&&count->t==arkjson::Value::Num&&count->num>=3&&count->num<=8)g_teamRoleCount=(unsigned)count->num;
        if(auto* focused=j.find("team_focused_workflow");focused&&focused->t==arkjson::Value::Bool)g_teamFocusedWorkflow=focused->b;
        if(g_teamFocusedWorkflow)g_teamRoleCount=3;
        if(auto* roles=j.find("team_roles");roles&&roles->t==arkjson::Value::Arr)for(size_t i=0;i<teamCapacity&&i<roles->arr.size();++i){if(auto* v=roles->arr[i].find("model");v&&v->t==arkjson::Value::Str)g_teamModels[i]=v->str;if(auto* v=roles->arr[i].find("reasoning_effort");v&&v->t==arkjson::Value::Str)g_teamEfforts[i]=v->str;}
        if(auto* v=j.find("grid"))g_grid=v->b;if(auto* v=j.find("wireframe"))wireframe=v->b;if(auto* v=j.find("light_background"))g_lightBackground=v->b;if(auto* v=j.find("satin"))g_satin=v->b;if(auto* v=j.find("show_datum_planes");v&&v->t==arkjson::Value::Bool)g_showDatumPlanes=v->b;
        if(auto* v=j.find("occlusion");v&&v->t==arkjson::Value::Bool)g_occlusionMode=v->b?2:0;
        if(auto* v=j.find("occlusion_mode");v&&v->t==arkjson::Value::Num)g_occlusionMode=std::clamp((int)v->num,0,3);
        if(auto* v=j.find("occlusion_strength");v&&v->t==arkjson::Value::Num)g_occlusionStrength=(float)clamp(v->num,0.,1.);
        if(auto* v=j.find("occlusion_radius");v&&v->t==arkjson::Value::Num)g_occlusionRadius=(float)clamp(v->num,.25,4.);
        if(auto* v=j.find("path_tracing");v&&v->t==arkjson::Value::Bool)g_pathTracing=v->b;
        if(auto* v=j.find("show_edges");v&&v->t==arkjson::Value::Bool)g_showEdges=v->b;if(auto* v=j.find("orthographic");v&&v->t==arkjson::Value::Bool)g_cameraOrtho=v->b;
    }catch(...){g_status="ARK settings unavailable; defaults loaded";}
}
static void unitsDialog(){
    std::vector<EditorField> fields{{"Length",g_displayUnits.length,false,{"mm","cm","m","in","ft"}},{"Mass",g_displayUnits.mass,false,{"mg","g","kg","lb"}},{"Volume",g_displayUnits.volume,false,{"mm3","cm3","mL","L","m3"}},{"Significant digits",std::to_string(g_displayUnits.precision),false,{"3","4","5","6","7","8","9"}}};
    if(!editFields("Units / SI defaults: mm, g, L","Display units for measurement and physical properties. Existing geometry and editor fields labelled mm keep their original units.",fields))return;
    DisplayUnits next;next.length=fields[0].value;next.mass=fields[1].value;next.volume=fields[2].value;next.precision=std::stoi(fields[3].value);next.validate();auto previous=g_displayUnits;g_displayUnits=next;try{if(!g_dialogSmoke)saveUiSettings();}catch(...){g_displayUnits=previous;throw;}g_status="Display units: "+next.length+" / "+next.mass+" / "+next.volume;
}
static void graphicsDialog(){
    std::vector<EditorField> fields{{"Show ground grid",g_grid?"1":"0",true},{"Wireframe",wireframe?"1":"0",true},{"Light background",g_lightBackground?"1":"0",true},{"Visible edges",g_showEdges?"1":"0",true},{"Orthographic camera",g_cameraOrtho?"1":"0",true},{"AO mode / 0-3",std::to_string(g_occlusionMode)},{"AO strength / 0-1",std::to_string(g_occlusionStrength)},{"AO radius / 0.25-4",std::to_string(g_occlusionRadius)},{"GPU path tracing (optional)",g_pathTracing?"1":"0",true}};
    if(!editFields("System options / Graphics","AO: 0 off, 1 contact, 2 ambient, 3 horizon. Path tracing: progressive GPU OpenGL BVH, 4 bounces; opaque untextured scenes. Applies immediately.",fields))return;
    bool values[5];for(int i=0;i<5;++i){double n=formNumber(fields[(size_t)i].value);if(n!=0&&n!=1)throw std::runtime_error("Graphics toggles accept 0 or 1");values[i]=n!=0;}
    double mode=formNumber(fields[5].value),strength=formNumber(fields[6].value),radius=formNumber(fields[7].value);
    if(mode<0||mode>3||std::floor(mode)!=mode||strength<0||strength>1||radius<.25||radius>4)throw std::runtime_error("AO mode is 0..3, strength is 0..1, radius is 0.25..4");
    double tracing=formNumber(fields[8].value);if(tracing!=0&&tracing!=1)throw std::runtime_error("Path tracing accepts 0 or 1");g_pathTracing=tracing!=0;
    g_grid=values[0];wireframe=values[1];g_lightBackground=values[2];g_showEdges=values[3];g_cameraOrtho=values[4];g_occlusionMode=(int)mode;g_occlusionStrength=(float)strength;g_occlusionRadius=(float)radius;if(!g_dialogSmoke)saveUiSettings();
}
static void materialDialog(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a body to edit its material");
    Feature* feature=g_doc.find(g_bodyIds[(size_t)selBody]);if(!feature)throw std::runtime_error("Selected body has no editable feature");
    Appearance appearance=resolvedAppearance(g_doc,*feature);
    const Material* existing=nullptr;
    for(const auto& material:g_doc.materials)if(material.name==feature->material){existing=&material;break;}
    std::string proposedName=feature->material.empty()?"Paint "+std::to_string(feature->id):feature->material;
    double proposedDensity=existing?existing->densityGcm3:0.;
    if(!existing){
        std::vector<EditorField> preset{{"Preset name or custom","custom"}};
        if(!editFields("Material catalog","Aluminum, Steel, ABS, Rubber, Glass use nominal densities. Choose custom for your own values.",preset))return;
        std::string chosen=trimField(preset[0].value);std::transform(chosen.begin(),chosen.end(),chosen.begin(),[](unsigned char c){return (char)std::tolower(c);});
        if(chosen=="aluminum"){proposedName="Aluminum";proposedDensity=2.70;appearance.baseColor={.76,.79,.83};appearance.metallic=.9;appearance.roughness=.31;}
        else if(chosen=="steel"){proposedName="Steel";proposedDensity=7.85;appearance.baseColor={.49,.54,.61};appearance.metallic=.86;appearance.roughness=.38;}
        else if(chosen=="abs"){proposedName="ABS";proposedDensity=1.04;appearance.baseColor={.12,.15,.19};appearance.metallic=0;appearance.roughness=.68;}
        else if(chosen=="rubber"){proposedName="Rubber";proposedDensity=1.10;appearance.baseColor={.045,.052,.06};appearance.metallic=0;appearance.roughness=.88;}
        else if(chosen=="glass"){proposedName="Glass";proposedDensity=2.50;appearance.baseColor={.70,.83,.92};appearance.metallic=.05;appearance.roughness=.12;appearance.opacity=.42;}
        else if(chosen!="custom"&&chosen.empty()==false)throw std::runtime_error("Choose Aluminum, Steel, ABS, Rubber, Glass, or custom");
    }
    auto number=[](double n){char buffer[48];std::snprintf(buffer,sizeof buffer,"%.5g",n);return std::string(buffer);};
    std::string rgb=number(appearance.baseColor.x)+", "+number(appearance.baseColor.y)+", "+number(appearance.baseColor.z);
    std::string scale=number(appearance.textureScaleU)+", "+number(appearance.textureScaleV);
    std::vector<EditorField> fields{{"Material name",proposedName},
        {"Density / g per cm3",number(proposedDensity)},{"Base color / R, G, B",rgb},
        {"Metallic / 0-1",number(appearance.metallic)},{"Roughness / 0-1",number(appearance.roughness)},
        {"Opacity / 0-1",number(appearance.opacity)},{"Texture image path",appearance.texturePath},
        {"Texture repeats U, V",scale},
        {"Young modulus / GPa (0 = unknown)",number(existing&&existing->physical.find("young_modulus_Pa")?existing->physical.find("young_modulus_Pa")->num/1e9:0)},
        {"Poisson ratio",number(existing&&existing->physical.find("poisson_ratio")?existing->physical.find("poisson_ratio")->num:.3)},
        {"Yield strength / MPa (0 = unknown)",number(existing&&existing->physical.find("yield_strength_Pa")?existing->physical.find("yield_strength_Pa")->num/1e6:0)}};
    if(!editFields("Body material","Presets are nominal. Use the actual grade. A frame assignment updates every profile. Image paths are relative to the document.",fields))return;
    auto pairValues=[&](const std::string& value,int count){std::string clean=value;std::replace(clean.begin(),clean.end(),',',' ');std::istringstream input(clean);std::vector<double> values;double n;
        while(input>>n)values.push_back(n);if(!input.eof()||(int)values.size()!=count)throw std::runtime_error("Enter comma-separated numeric color or texture scale values");return values;};
    std::string name=trimField(fields[0].value);if(name.empty())throw std::runtime_error("Material name is required");
    double density=formNumber(fields[1].value),metallic=formNumber(fields[3].value),roughness=formNumber(fields[4].value),opacity=formNumber(fields[5].value);
    auto color=pairValues(fields[2].value,3),repeats=pairValues(fields[7].value,2);
    using V=arkjson::Value;V commands=V::makeArr(),material=V::makeObj(),ap=V::makeObj(),base=V::makeArr(),scaleValue=V::makeArr();
    for(double c:color)base.arr.push_back(V::makeNum(c));for(double s:repeats)scaleValue.arr.push_back(V::makeNum(s));
    ap.set("base_color",std::move(base));ap.set("metallic",V::makeNum(metallic));ap.set("roughness",V::makeNum(roughness));ap.set("opacity",V::makeNum(opacity));
    ap.set("texture_path",V::makeStr(trimField(fields[6].value)));ap.set("texture_scale",std::move(scaleValue));
    material.set("op",V::makeStr("material.set"));material.set("name",V::makeStr(name));material.set("density",V::makeNum(density));V physical=existing&&existing->physical.t==V::Obj?existing->physical:V::makeObj();physical.obj.erase(std::remove_if(physical.obj.begin(),physical.obj.end(),[](const auto& item){return item.first=="young_modulus_Pa"||item.first=="poisson_ratio"||item.first=="yield_strength_Pa";}),physical.obj.end());double young=formNumber(fields[8].value),poisson=formNumber(fields[9].value),yield=formNumber(fields[10].value);if(young<0||yield<0)throw std::runtime_error("Material stiffness and strength cannot be negative");if(young>0){physical.set("young_modulus_Pa",V::makeNum(young*1e9));physical.set("poisson_ratio",V::makeNum(poisson));}if(yield>0)physical.set("yield_strength_Pa",V::makeNum(yield*1e6));material.set("physical",physical);material.set("appearance",std::move(ap));commands.arr.push_back(std::move(material));
    V assign=V::makeObj();assign.set("op",V::makeStr("feature.edit"));assign.set("id",V::makeNum((double)feature->id));assign.set("material",V::makeStr(name));assign.set("appearance",V::makeObj());
    if(feature->type==FeatureType::ConstructionFrame){V native=feature->nativeSpec;if(const auto* profiles=native.find("profiles")){V changed=*profiles;for(auto& profile:changed.arr){profile.obj.erase(std::remove_if(profile.obj.begin(),profile.obj.end(),[](const auto& item){return item.first=="density_g_cm3";}),profile.obj.end());profile.set("material",V::makeStr(name));}native.set("profiles",changed);assign.set("native",native);}}
    commands.arr.push_back(std::move(assign));
    std::string featureName=feature->name;
    V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",std::move(commands));runTool("doc_apply",args);
    if(!g_error)g_status="Material "+name+" applied to "+featureName;
}
// An opacity override belongs to the part definition; physical material data is untouched.
static void setSelectedOpacity(double opacity){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a part to change its opacity");
    auto* feature=g_doc.find(g_bodyIds[(size_t)selBody]);if(!feature)throw std::runtime_error("The selected part definition is unavailable");
    if(!std::isfinite(opacity)||opacity<.05||opacity>1.)throw std::runtime_error("Opacity must be between 5 and 100 percent");
    using V=arkjson::Value;V appearance=feature->appearance.t==V::Obj?feature->appearance:V::makeObj();appearance.set("opacity",V::makeNum(opacity));
    V command=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)feature->id));command.set("appearance",appearance);applyCommand(command);
    if(!g_error)g_status="Opacity saved for this part definition and every instance / material and density retained";
}
static void selectedOpacityDialog(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a part to change its opacity");
    auto* feature=g_doc.find(g_bodyIds[(size_t)selBody]);if(!feature)throw std::runtime_error("The selected part definition is unavailable");
    auto revision=g_session.revision,epoch=g_documentEpoch;u64 id=feature->id;
    std::vector<EditorField> fields{{"Opacity / percent",std::to_string((int)std::lround(resolvedAppearance(g_doc,*feature).opacity*100))}};
    if(editFields("Part transparency","Applies to every instance of this part definition. 5-100 percent; 100 restores opaque. Physical material and density are retained.",fields)){if(revision!=g_session.revision||epoch!=g_documentEpoch||selBody<0||(size_t)selBody>=g_bodyIds.size()||g_bodyIds[(size_t)selBody]!=id)throw std::runtime_error("The drawing or selection changed while editing opacity; reopen the part");setSelectedOpacity(formNumber(fields[0].value)/100.);}
}
static void physicalProperties(bool assemblyTotal=false){
    if(selBody<0&&g_doc.assembly.empty())throw std::runtime_error("Select a body to inspect physical properties");
    if(selBody>=0&&(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Selected body is unavailable");
    using V=arkjson::Value;g_physicalAssembly=assemblyTotal||selBody<0;
    if(g_physicalAssembly&&g_doc.assembly.empty())throw std::runtime_error("No assembly is active");
    V args=g_physicalSettings.t==V::Obj?g_physicalSettings:V::makeObj();if(!g_physicalAssembly)args.set("id",V::makeNum((double)g_bodyIds[(size_t)selBody]));
    if(!g_physicalAssembly){
        const Mat4 local=g_instanceWorld[(size_t)selBody].inverted();
        auto convert=[&](const V& value,bool point){if(value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error("Physical input requires xyz");Vec3 v{value.arr[0].asNum(),value.arr[1].asNum(),value.arr[2].asNum()};v=point?local.transformPoint(v):local.transformDir(v);V out=V::makeArr();for(double n:{v.x,v.y,v.z})out.arr.push_back(V::makeNum(n));return out;};
        V gravity,origin;arkjson::parse("[0,0,-9.80665]",gravity);arkjson::parse("[0,0,0]",origin);args.set("gravity",convert(args.find("gravity")?*args.find("gravity"):gravity,false));args.set("reference_point_mm",convert(args.find("reference_point_mm")?*args.find("reference_point_mm"):origin,true));
        for(const char* key:{"velocity_m_s","angular_velocity_rad_s","acceleration_m_s2","angular_acceleration_rad_s2"})if(auto value=args.find(key)){V transformed=convert(*value,false);args.set(key,transformed);}
        if(auto rows=args.find("loads")){V loads=*rows;for(auto& load:loads.arr){for(const char* key:{"force_N","torque_Nm"})if(auto value=load.find(key)){V transformed=convert(*value,false);load.set(key,transformed);}if(auto value=load.find("point_mm")){V transformed=convert(*value,true);load.set("point_mm",transformed);}}args.set("loads",loads);}
    }
    V response=g_uiReady&&!g_headless?callSessionWithProgress("physics_inspect",args):g_session.call("physics_inspect",args);
    g_physicalError.clear();g_physicalReport=V::makeObj();
    if(auto* error=response.find("isError");error&&error->t==V::Bool&&error->b){
        auto* content=response.find("structuredContent");auto* reason=content?content->find("error"):nullptr;
        g_physicalError=reason?reason->asStr():"Physical properties unavailable";
    }else if(auto* content=response.find("structuredContent")){g_physicalReport=*content;if(auto reason=content->find("mass_reason"))g_physicalError=reason->asStr();if(auto missing=content->find("missing");missing&&missing->t==V::Arr&&!missing->arr.empty())g_physicalError=missing->arr.front().asStr();}
    g_physicalRevision=g_session.revision;g_physicalSelection=selBody>=0?(g_instanceIds[(size_t)selBody]?g_instanceIds[(size_t)selBody]:g_bodyIds[(size_t)selBody]):0;g_physicalVisible=true;g_sidebarTab=2;layout();
    g_status=g_physicalError.empty()?"Physical properties inspected at current revision":g_physicalError;
}
static void physicsSettingsDialog(){
    using V=arkjson::Value;
    auto values=[&](const char* key,const char* fallback){const V* value=g_physicalSettings.find(key);if(!value||value->t!=V::Arr||value->arr.size()!=3)return std::string(fallback);return value->arr[0].dump()+", "+value->arr[1].dump()+", "+value->arr[2].dump();};
    std::vector<EditorField> fields{{"Gravity / m per s2",values("gravity","0, 0, -9.80665")},{"Moment reference / mm",values("reference_point_mm","0, 0, 0")},{"Velocity / m per s",values("velocity_m_s","0, 0, 0")},{"Angular speed / rad per s",values("angular_velocity_rad_s","0, 0, 0")},{"Applied force / N","0, 0, 0"},{"Force position / mm","0, 0, 0"}};
    if(!editFields("Rigid-body load case","Vectors and load points use world coordinates. Uniform gravity; instantaneous balance, no deformation or contact simulation.",fields))return;
    auto parse=[](std::string value){std::replace(value.begin(),value.end(),',',' ');std::istringstream input(value);V vector=V::makeArr();double n;while(input>>n){if(!std::isfinite(n))throw std::runtime_error("Vector must be finite");vector.arr.push_back(V::makeNum(n));}if(!input.eof()||vector.arr.size()!=3)throw std::runtime_error("Enter three vector coordinates");return vector;};
    V settings=V::makeObj();const char* keys[]={"gravity","reference_point_mm","velocity_m_s","angular_velocity_rad_s"};for(int i=0;i<4;++i)settings.set(keys[i],parse(fields[(size_t)i].value));
    V loads=V::makeArr(),load=V::makeObj();load.set("force_N",parse(fields[4].value));load.set("point_mm",parse(fields[5].value));loads.arr.push_back(load);settings.set("loads",loads);g_physicalSettings=settings;physicalProperties(g_physicalAssembly);
}
static void usageDrawer(){
    UsageTotals totals=usageTotals(g_doc);std::ostringstream report;
    report<<"Provider-reported tokens in this document\r\n";
    report<<"Total "<<totals.totalTokens<<"  |  input "<<totals.inputTokens<<"  |  output "<<totals.outputTokens<<"\r\n";
    report<<"Cached input "<<totals.cachedInputTokens<<"  |  reasoning "<<totals.reasoningTokens<<" (subsets)\r\n";
    report<<"Reported responses "<<totals.reportedResponses<<"  |  missing usage "<<totals.missingResponses<<"\r\n";
    report<<"External MCP/Codex usage is unknown unless a receipt is explicitly recorded.\r\n\r\n";
    for(const UsageReceipt& receipt:g_doc.usage.entries){
        report<<receipt.source<<" / "<<receipt.provenance<<" / "<<receipt.model<<"\r\n";
        report<<"  "<<receipt.id<<"  |  input "<<receipt.inputTokens<<"  output "<<receipt.outputTokens<<"  total "<<receipt.totalTokens;
        if(receipt.missingResponses)report<<"  |  partial: "<<receipt.missingResponses<<" response(s) omitted usage";
        report<<"\r\n  "<<receipt.recordedAtUtc<<"\r\n\r\n";
    }
    std::string text=report.str();editText("Token receipts / read-only",text,true);
}
static void showProjectActivity(){
    std::ostringstream report;report<<"PROJECT TOOL ACTIVITY / measured outcomes and agent self-reports\r\n";
    report<<"Saved with this project. Scores are subjective; they do not certify geometry or manufacture.\r\n\r\n";
    if(!g_doc.agentActivity)report<<"No agent actions recorded in this project yet.\r\n";
    else{report<<"Audit serial "<<g_doc.agentActivity->serial<<" / records "<<g_doc.agentActivity->entries.size()<<" / unrecorded "<<g_doc.agentActivity->unrecordedActions<<"\r\n\r\n";
        for(const auto& row:g_doc.agentActivity->entries){const auto& actor=*row.find("actor");report<<row.find("utc")->asStr()<<" / "<<(actor.find("id")?actor.find("id")->asStr():"unknown")<<" / "<<row.find("tool")->asStr()<<" / "<<row.find("elapsed_ms")->asNum()<<" ms / "<<(row.find("is_error")->b?"FAILED":"returned")<<"\r\n";
            if(const auto* feedback=row.find("feedback")){report<<"  Utility "<<feedback->find("utility")->asNum()<<"/5 / difficulty "<<feedback->find("difficulty")->asNum()<<"/5 / "<<feedback->find("action")->asStr()<<"\r\n";for(const auto* key:{"issue","wished_tool","wished_options"})if(const auto* text=feedback->find(key);text&&!text->asStr().empty())report<<"  "<<key<<": "<<text->asStr()<<"\r\n";}else report<<"  Agent feedback pending\r\n";
        }}
    std::vector<EditorField> fields{{"Activity",report.str()}};editFields("Project tool activity / read-only","Measured timings and outcomes, followed by agent self-ratings. Saved with the project.",fields,true,"Close",nullptr,true);
}
static void createConstructionFrame(){
    std::vector<EditorField> fields{{"Frame name","Chassis"},{"Length / mm","400"},{"Width / mm","240"},{"Round outside diameter / mm","30"},{"Wall / mm","2"}};
    if(!editFields("Construction frame","Creates four exact hollow members on a rectangular node program. Edit its native program for joints and bends.",fields))return;
    double length=formNumber(fields[1].value),width=formNumber(fields[2].value),od=formNumber(fields[3].value),wall=formNumber(fields[4].value);
    if(length<=0||width<=0||od<=0||wall<=0||wall*2>=od)throw std::runtime_error("Frame dimensions require positive spans and wall less than half the diameter");
    using V=arkjson::Value;V native=V::makeObj(),profiles=V::makeArr(),profile=V::makeObj(),nodes=V::makeArr(),members=V::makeArr();
    profile.set("name",V::makeStr("round"));profile.set("section",V::makeStr("round"));profile.set("od",V::makeStr(fields[3].value));profile.set("wall",V::makeStr(fields[4].value));profiles.arr.push_back(std::move(profile));
    auto node=[&](const char* name,double x,double y){V entry=V::makeObj(),xyz=V::makeArr();entry.set("name",V::makeStr(name));xyz.arr.push_back(V::makeNum(x));xyz.arr.push_back(V::makeNum(y));xyz.arr.push_back(V::makeNum(0));entry.set("xyz",std::move(xyz));nodes.arr.push_back(std::move(entry));};
    node("A",0,0);node("B",length,0);node("C",length,width);node("D",0,width);
    auto member=[&](const char* name,const char* start,const char* end){V entry=V::makeObj();entry.set("name",V::makeStr(name));entry.set("profile",V::makeStr("round"));entry.set("start",V::makeStr(start));entry.set("end",V::makeStr(end));members.arr.push_back(std::move(entry));};
    member("front","A","B");member("right","B","C");member("rear","C","D");member("left","D","A");
    native.set("profiles",std::move(profiles));native.set("nodes",std::move(nodes));native.set("members",std::move(members));
    V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("construction_frame"));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("native",std::move(native));applyCommand(std::move(command));if(!g_error)frameScene();
}
static void createPcb(){
    std::vector<EditorField> fields{{"Board name","Controller PCB"},{"Width / mm","40"},{"Height / mm","30"},{"Thickness / mm","1.6"},{"Pad name","J1.1"}};
    if(!editFields("PCB layout","Creates an exact board outline, top/bottom layers and one named top pad. Inspect DRC before fabrication export.",fields))return;
    double width=formNumber(fields[1].value),height=formNumber(fields[2].value),thickness=formNumber(fields[3].value);
    if(width<=0||height<=0||thickness<=0)throw std::runtime_error("PCB dimensions must be positive");
    using V=arkjson::Value;V native=V::makeObj(),outline=V::makeArr(),layers=V::makeArr(),pads=V::makeArr();
    auto point=[&](double x,double y){V pair=V::makeArr();pair.arr.push_back(V::makeNum(x));pair.arr.push_back(V::makeNum(y));outline.arr.push_back(std::move(pair));};
    point(0,0);point(width,0);point(width,height);point(0,height);
    auto layer=[&](const char* name,double z){V entry=V::makeObj();entry.set("name",V::makeStr(name));entry.set("z",V::makeNum(z));layers.arr.push_back(std::move(entry));};layer("top",thickness);layer("bottom",0);
    V pad=V::makeObj();pad.set("name",V::makeStr(trimField(fields[4].value)));pad.set("net",V::makeStr("PWR"));pad.set("layer",V::makeStr("top"));pad.set("x",V::makeNum(width*.25));pad.set("y",V::makeNum(height*.5));pad.set("width",V::makeNum(2));pad.set("height",V::makeNum(2));pads.arr.push_back(std::move(pad));
    native.set("outline",std::move(outline));native.set("thickness",V::makeNum(thickness));native.set("layers",std::move(layers));native.set("pads",std::move(pads));
    V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("pcb"));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("native",std::move(native));applyCommand(std::move(command));if(!g_error)frameScene();
}
static void createCableRoute(){
    std::vector<EditorField> fields{{"Route name","Battery cable"},{"End X / mm","80"},{"End Y / mm","0"},{"End Z / mm","0"},{"Cable radius / mm","1"},{"Minimum bend radius / mm","5"}};
    if(!editFields("Cable route","Creates a named exact cable between two points. Add waypoints and obstacles through the native program.",fields))return;
    double x=formNumber(fields[1].value),y=formNumber(fields[2].value),z=formNumber(fields[3].value),radius=formNumber(fields[4].value),bend=formNumber(fields[5].value);
    if(radius<=0||bend<radius||(Vec3{x,y,z}).len()<=1e-6)throw std::runtime_error("Cable needs distinct endpoints and a bend radius at least its cable radius");
    using V=arkjson::Value;V native=V::makeObj(),start=V::makeObj(),end=V::makeObj(),origin=V::makeArr(),destination=V::makeArr();
    for(double value:{0.,0.,0.})origin.arr.push_back(V::makeNum(value));for(double value:{x,y,z})destination.arr.push_back(V::makeNum(value));
    start.set("name",V::makeStr("start"));start.set("point",std::move(origin));end.set("name",V::makeStr("end"));end.set("point",std::move(destination));
    native.set("start",std::move(start));native.set("end",std::move(end));native.set("radius",V::makeNum(radius));native.set("bend_radius",V::makeNum(bend));native.set("auto_route",V::makeBool(true));native.set("obstacles",V::makeArr());
    V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("cable_route"));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("native",std::move(native));applyCommand(std::move(command));if(!g_error)frameScene();
}
static void createSuspensionDialog(){
    std::vector<EditorField> fields{{"Mechanism name","Front left double wishbone"},{"Side / left or right","left"},{"Wheel radius / mm","300"},{"Wheel width / mm","180"},{"Spring rate / N per mm","40"}};
    if(!editFields("Suspension mechanism","Creates editable example hardpoints with tested jounce and steering closure. Edit its native program for vehicle-specific geometry.",fields))return;
    std::string side=trimField(fields[1].value);if(side!="left"&&side!="right")throw std::runtime_error("Suspension side must be left or right");
    double radius=formNumber(fields[2].value),width=formNumber(fields[3].value),rate=formNumber(fields[4].value);
    if(radius<=0||width<=0||rate<=0)throw std::runtime_error("Wheel size and spring rate must be positive");
    using V=arkjson::Value;V native;if(!arkjson::parse(R"({"side":"left","hardpoints":{"lower_front":[-100,-300,400],"lower_rear":[100,-300,400],"lower_ball":[0,-650,380],"upper_front":[-80,-280,650],"upper_rear":[80,-280,650],"upper_ball":[0,-620,650],"tie_inner":[-60,-250,520],"tie_outer":[-80,-635,510],"wheel_center":[0,-720,520],"wheel_axis":[0,-1,0],"shock_chassis":[0,-230,800],"shock_arm":[0,-500,400]},"wheel":{"radius":300,"width":180,"hub_radius":65},"spring":{"rate_n_per_mm":40,"free_length_mm":510,"preload_n":100,"coil_diameter_mm":70,"turns":7,"min_length_mm":350,"max_length_mm":600},"limits":{"jounce_min_mm":-35,"jounce_max_mm":35,"rack_min_mm":-12,"rack_max_mm":12}})",native))throw std::runtime_error("Suspension template is invalid");
    native.set("side",V::makeStr(side));if(side=="right"){V hardpoints=*native.find("hardpoints");
        for(auto& item:hardpoints.obj)if(item.second.t==V::Arr&&item.second.arr.size()==3&&item.second.arr[1].t==V::Num)item.second.arr[1].num*=-1.;
        native.set("hardpoints",std::move(hardpoints));}
    auto wheel=*native.find("wheel");wheel.set("radius",V::makeNum(radius));wheel.set("width",V::makeNum(width));native.set("wheel",std::move(wheel));
    auto spring=*native.find("spring");spring.set("rate_n_per_mm",V::makeNum(rate));native.set("spring",std::move(spring));
    V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("suspension"));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("native",std::move(native));
    if(g_doc.features.empty()&&g_doc.assembly.empty()){
        V args=V::makeObj(),commands=V::makeArr(),group=V::makeObj(),pose=V::makeObj(),instance=V::makeObj();
        u64 featureId=g_doc.nextFeatureId_,nodeId=g_doc.nextAssemblyId_;commands.arr.push_back(std::move(command));
        group.set("op",V::makeStr("assembly.add"));group.set("name",V::makeStr("Suspension display"));group.set("feature",V::makeNum(0));pose.set("rx",V::makeStr("-90"));group.set("p",std::move(pose));commands.arr.push_back(std::move(group));
        instance.set("op",V::makeStr("assembly.add"));instance.set("parent",V::makeNum((double)nodeId));instance.set("feature",V::makeNum((double)featureId));instance.set("name",V::makeStr(trimField(fields[0].value)));commands.arr.push_back(std::move(instance));
        args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",std::move(commands));runTool("doc_apply",args);
    }else applyCommand(std::move(command));
    if(!g_error){selBody=(int)g_bodyIds.size()-1;frameScene();}
}
static std::string libraryText(const arkjson::Value& value,const char* key){auto* field=value.find(key);return field&&field->t==arkjson::Value::Str?field->str:std::string{};}
static arkjson::Value libraryArgs(){auto args=arkjson::Value::makeObj();if(!g_libraryRoot.empty())args.set("root",arkjson::Value::makeStr(g_libraryRoot));return args;}
static void insertLibraryComponent(const std::string& id){
    using V=arkjson::Value;auto args=libraryArgs();args.set("id",V::makeStr(id));auto record=runTool("library_get",args);if(g_error)return;auto* content=record.find("structuredContent");auto* metadata=content?content->find("metadata"):nullptr;std::string name=metadata?libraryText(*metadata,"name"):"Component";u64 revision=g_session.revision;
    std::vector<EditorField> fields{{"Instance name",name},{"Translate X / mm","0"},{"Translate Y / mm","0"},{"Translate Z / mm","0"},{"Rotate X / degrees","0"},{"Rotate Y / degrees","0"},{"Rotate Z / degrees","0"}};
    if(!editFields("Insert library component","Adds an instance to the open document. The stored native solid is reused when already present; edits use the observed document revision.",fields))return;
    args.set("expected_revision",V::makeNum((double)revision));args.set("instance_name",V::makeStr(trimField(fields[0].value)));V placement=V::makeObj();const char* keys[]={"tx","ty","tz","rx","ry","rz"};for(size_t i=0;i<6;++i){formNumber(fields[i+1].value);placement.set(keys[i],V::makeStr(trimField(fields[i+1].value)));}args.set("p",placement);auto inserted=runTool("library_insert",args);if(g_error)return;
    if(auto* result=inserted.find("structuredContent"))if(auto* feature=result->find("feature_id"))for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==(u64)feature->num)selBody=(int)i;frameScene();
}
static void inspectLibraryComponent(const std::string& id){
    auto args=libraryArgs();args.set("id",arkjson::Value::makeStr(id));auto result=runTool("library_get",args);if(g_error||g_dialogSmoke)return;auto* record=result.find("structuredContent");if(!record)return;auto* meta=record->find("metadata");std::string report=meta?libraryText(*meta,"name"):id;
    if(meta)for(const char* key:{"category","description","standard","material","manufacturer","part_number","provenance","source_url","license","review_state"}){auto value=libraryText(*meta,key);report+="\r\n"+std::string(key)+": "+(value.empty()?"Unknown":value);}
    report+="\r\n\r\nNative geometry: ";auto* verified=record->find("geometry_verified");report+=verified&&verified->b?"Verified stored B-rep":"Unverified";report+="\r\nMetadata authority: declared\r\nSource SHA-256: "+libraryText(*record,"source_sha256");MessageBoxA(g_window,report.c_str(),"Library component / recorded evidence",MB_OK|MB_ICONINFORMATION);
}
static void librarySearchDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Search text",g_libraryText},{"Category",""},{"Standard",""},{"Thread diameter (mm)",""},{"Pitch (mm)",""},{"Length (mm)",""},{"Material",""},{"Library folder",g_libraryRoot}};
    if(!editFields("Component library","Search stored STEP components by recorded metadata. Choose a result to inspect its evidence or insert an instance into the open document.",fields))return;
    g_libraryText=trimField(fields[0].value);g_libraryRoot=trimField(fields[7].value);auto args=libraryArgs();const char* strings[]={"text","category","standard"};for(size_t i=0;i<3;++i)if(!trimField(fields[i].value).empty())args.set(strings[i],V::makeStr(trimField(fields[i].value)));const char* dimensions[]={"thread_diameter_mm","thread_pitch_mm","length_mm"};for(size_t i=0;i<3;++i)if(!trimField(fields[i+3].value).empty())args.set(dimensions[i],V::makeNum(formNumber(fields[i+3].value)));if(!trimField(fields[6].value).empty())args.set("material",V::makeStr(trimField(fields[6].value)));args.set("limit",V::makeNum(40));
    auto result=runTool("library_search",args);if(g_error)return;auto* content=result.find("structuredContent");auto* records=content?content->find("components"):nullptr;g_libraryMatches=records?*records:V::makeArr();auto* more=content?content->find("has_more"):nullptr;g_status=std::to_string(g_libraryMatches.arr.size())+" stored components found"+(more&&more->b?" / more results: narrow the search":"");if(g_dialogSmoke)return;saveUiSettings();
    if(g_libraryMatches.arr.empty()){MessageBoxA(g_window,"No stored components match. Import a STEP into the library, or use the Advanced CAD API category to discover component sources.","Component library",MB_OK|MB_ICONINFORMATION);return;}
    HMENU menu=CreatePopupMenu();for(size_t i=0;i<g_libraryMatches.arr.size();++i){const auto& record=g_libraryMatches.arr[i];auto* meta=record.find("metadata");std::string label=meta?libraryText(*meta,"name"):libraryText(record,"id");if(meta){auto category=libraryText(*meta,"category");if(!category.empty())label+=" / "+category;}AppendMenuA(menu,MF_STRING,(UINT_PTR)(i+1),label.c_str());}POINT cursor;GetCursorPos(&cursor);int picked=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);DestroyMenu(menu);if(picked<1||(size_t)picked>g_libraryMatches.arr.size())return;std::string id=libraryText(g_libraryMatches.arr[(size_t)picked-1],"id");
    menu=CreatePopupMenu();AppendMenuA(menu,MF_STRING,1,"Inspect recorded evidence");AppendMenuA(menu,MF_STRING,2,"Insert into current document...");int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);DestroyMenu(menu);if(action==1)inspectLibraryComponent(id);else if(action==2)insertLibraryComponent(id);
}
static void ingestLibraryStep(const std::string& explicitPath={}){
    using V=arkjson::Value;std::string path=explicitPath;if(path.empty()){wchar_t selected[32768]=L"";OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=selected;of.nMaxFile=32768;of.lpstrFilter=L"STEP component\0*.step;*.stp\0";of.lpstrTitle=L"Import STEP into component library";of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(!GetOpenFileNameW(&of))return;int size=WideCharToMultiByte(CP_UTF8,0,selected,-1,nullptr,0,nullptr,nullptr);path.resize((size_t)size);WideCharToMultiByte(CP_UTF8,0,selected,-1,path.data(),size,nullptr,nullptr);path.pop_back();}
    auto stem=std::filesystem::u8path(path).stem().u8string();std::vector<EditorField> fields{{"Component name",std::string(stem.begin(),stem.end())},{"Category","component"},{"Provenance","Local STEP import"},{"Source URL",""},{"License",""},{"Material",""},{"Standard",""},{"Body (auto/all/index)","auto"},{"Library folder",g_libraryRoot}};
    if(!editFields("Store STEP component","Copies the STEP and verifies native geometry. Record known origin and license; blank metadata remains unknown. A multi-solid source needs an explicit body or all selection.",fields))return;
    g_libraryRoot=trimField(fields[8].value);auto args=libraryArgs();args.set("path",V::makeStr(path));V metadata=V::makeObj();const char* keys[]={"name","category","provenance","source_url","license","material","standard"};for(size_t i=0;i<7;++i)if(!trimField(fields[i].value).empty())metadata.set(keys[i],V::makeStr(trimField(fields[i].value)));args.set("metadata",metadata);auto body=trimField(fields[7].value);if(body=="all")args.set("all_bodies",V::makeBool(true));else if(body!="auto"){double index=formNumber(body);if(index<0||std::floor(index)!=index)throw std::runtime_error("Body selection must be auto, all or a zero-based integer.");args.set("body_index",V::makeNum(index));}g_libraryLastId.clear();auto result=runTool("library_ingest",args);if(!g_error){if(auto* record=result.find("structuredContent"))g_libraryLastId=libraryText(*record,"id");g_status="STEP component stored in library; open Library to find and reuse it.";if(!g_dialogSmoke)saveUiSettings();}
}
#include "local_models.inl"
static bool editConnector(AssistantConfig& config,bool role,bool& inherit){
    auto profileShot=g_dialogShot;
    std::vector<std::string> providers;if(role)providers.push_back("inherit");else providers.push_back("chatgpt-plan");for(const auto& preset:assistantProviderPresets())providers.push_back(preset.id);
    std::string current=inherit?"inherit":config.authMode==AssistantAuthMode::ChatGptPlan?"chatgpt-plan":config.provider;
    if(std::find(providers.begin(),providers.end(),current)==providers.end())current="custom";
    std::vector<EditorField> profile{{"Provider",current,false,providers},{"Refresh model catalog", "0",true}};
    if(!editFields(role?"Agent card connector":"Assistant connector","Grok Build uses its CLI account; API providers use environment credentials. Catalog refresh reads model IDs without inference.",profile))return false;
    auto provider=profile[0].value;inherit=provider=="inherit";if(inherit)return true;if(provider=="chatgpt-plan"){if(config.authMode!=AssistantAuthMode::ChatGptPlan){config.model.clear();config.reasoningEffort.clear();}config.authMode=AssistantAuthMode::ChatGptPlan;config.provider="openai";config.protocol=AssistantProtocol::Responses;config.endpoint="https://api.openai.com/v1/responses";return true;}
    const auto expectedAuth=provider=="grok-build"?AssistantAuthMode::CliAccount:AssistantAuthMode::ApiKey;
    if(provider!=config.provider||config.authMode!=expectedAuth)config=assistantProviderPreset(provider);
    config.authMode=expectedAuth;if(provider=="ollama")return editLocalModel(config);std::vector<std::string> models;
    if(profile[1].value=="1"){
        auto operation=[config]{auto result=arkjson::Value::makeArr();for(const auto& model:assistantProviderModels(config))result.arr.push_back(arkjson::Value::makeStr(model));return result;};
        auto result=g_headless?operation():callSessionWithProgress("assistant_model_catalog",arkjson::Value::makeObj(),operation);for(const auto& row:result.arr)models.push_back(row.asStr());
    }
    if(!config.model.empty()&&std::find(models.begin(),models.end(),config.model)==models.end())models.insert(models.begin(),config.model);if(models.empty())models.push_back("");
    if(provider=="grok-build"){
        std::vector<EditorField> fields{{"Grok CLI / grok or full path",config.endpoint},{"Model / blank: CLI selected model",config.model,false,models,true}};
        if(!profileShot.empty()&&g_dialogSmoke)g_dialogShot=profileShot+".endpoint.png";
        if(!editFields("Grok Build account and model","Sign in with grok login first. Blank model uses the CLI selected model when advertised. ARK executes its own permitted tools.",fields))return false;
        config.endpoint=trimField(fields[0].value);config.model=trimField(fields[1].value);config.apiKeyEnv.clear();config.reasoningEffort.clear();config.imageInputSupported=false;validateAssistantConfig(config);return true;
    }
    std::vector<EditorField> fields{{"Endpoint",config.endpoint},{"Model / choose or type",config.model,false,models,true},{"API key env / blank local",config.apiKeyEnv},{"Protocol",assistantProtocolName(config.protocol),false,{"responses","chat-completions","messages"}}};
    bool hasMessagesLimit=config.protocol==AssistantProtocol::Messages;if(hasMessagesLimit)fields.push_back({"Messages response limit",std::to_string(config.maxOutputTokens)});
    fields.push_back({"Reasoning effort / auto",config.reasoningEffort.empty()?"auto":config.reasoningEffort});
    if(!profileShot.empty()&&g_dialogSmoke)g_dialogShot=profileShot+".endpoint.png";
    if(!editFields("Provider endpoint and model","Choose an actual model ID. Remote providers need their own API key; Ollama can run locally. Endpoint/protocol must match the selected service.",fields))return false;
    config.endpoint=trimField(fields[0].value);config.model=trimField(fields[1].value);config.apiKeyEnv=trimField(fields[2].value);config.protocol=assistantProtocolFromName(fields[3].value);auto effort=trimField(fields[hasMessagesLimit?5:4].value);config.reasoningEffort=effort=="auto"?"":effort;if(hasMessagesLimit){double output=formNumber(fields[4].value);if(output<1||output>131072||std::floor(output)!=output)throw std::runtime_error("Messages response limit must be a positive integer up to 131072");config.maxOutputTokens=(unsigned)output;}validateAssistantConfig(config);return true;
}
static void connectorDialog(bool local=false){
    if(g_teamStarted&&g_agentTeam.snapshot().running){g_status="Finish or cancel the active team before changing connectors";return;}
    auto config=g_assistantConfig;bool inherit=false;if(local?!editLocalModel(config):!editConnector(config,false,inherit))return;
    const bool routeChanged=config.authMode!=g_assistantConfig.authMode||config.provider!=g_assistantConfig.provider||config.endpoint!=g_assistantConfig.endpoint;
    if(routeChanged)for(size_t i=0;i<teamCapacity;++i)if(!g_teamConnectors[i]){g_teamModels[i].clear();g_teamEfforts[i].clear();}
    if(g_teamStarted){*g_teamCancel=true;g_agentTeam.cancel();g_teamStarted=false;resetTeamView();}
    g_assistantConfig=config;*g_assistantCancel=true;++g_documentEpoch;g_conversation=arkjson::Value::makeArr();if(!g_dialogSmoke)saveUiSettings();clearPendingRoleSettings();
    g_status=config.authMode==AssistantAuthMode::ChatGptPlan?"ChatGPT connector selected; choose the connected account model from Account.":config.authMode==AssistantAuthMode::CliAccount?"Grok Build connector saved; the CLI keeps its account and selected model.":config.provider=="ollama"?"Local Ollama / "+config.model+(config.imageInputSupported?" / tools and images":" / tools; text only"):"Provider connector saved; no inference started.";
}
static void teamConnectorDialog(size_t index){
    if(index>=teamCapacity)return;if(g_teamStarted&&g_agentTeam.snapshot().running){g_status="Finish or cancel the active team before changing a card connector";return;}
    auto config=teamRoleConfig(index);bool inherit=!g_teamConnectors[index];
    if(!editConnector(config,true,inherit))return;
    if(g_teamStarted){*g_teamCancel=true;g_agentTeam.cancel();g_teamStarted=false;resetTeamView();}
    if(inherit){if(g_teamConnectors[index]){g_teamModels[index].clear();g_teamEfforts[index].clear();}g_teamConnectors[index].reset();}else{g_teamConnectors[index]=config;g_teamModels[index]=config.model;g_teamEfforts[index]=config.reasoningEffort.empty()?"auto":config.reasoningEffort;}
    if(!g_dialogSmoke)saveUiSettings();g_status="Agent card connector saved for the next team run; other cards retain their connectors.";
}
static void startAuthJob(AuthJob job,std::string clientId={},bool requestPlanConsent=false){
    if(job==AuthJob::SignOut)clearPendingRoleSettings();
    if(g_authBusy){g_status="ChatGPT account operation is already running";return;}
    if(g_authWorker.joinable())g_authWorker.join();
    g_authCancel=std::make_shared<std::atomic_bool>(false);auto cancel=g_authCancel;g_authBusy=true;g_authCancelRequested=false;g_activeAuthJob=job;
    g_status=job==AuthJob::SignIn?"Continue in the system browser; CAD editing remains available":job==AuthJob::SignOut?"Signing out of ChatGPT...":"Loading ChatGPT model catalog...";
    g_authWorker=std::thread([job,clientId=std::move(clientId),requestPlanConsent,cancel]{
        UiAuthEvent event;event.job=job;
        try{if(job==AuthJob::SignIn){event.status=chatGptSignIn(clientId,cancel.get(),requestPlanConsent);if(event.status.planAuthorized)event.models=chatGptListModels(cancel.get());}
            else if(job==AuthJob::SignOut)event.status=chatGptSignOut(cancel.get());
            else{event.status=chatGptAuthStatus();event.models=chatGptListModels(cancel.get());}}
        catch(const std::exception& error){event.error=error.what();event.status=chatGptAuthStatus();}
        postAuth(std::move(event));
    });
}
static void cancelAuthJob(){
    if(!g_authBusy){g_status="No ChatGPT account operation is running";return;}
    *g_authCancel=true;g_authCancelRequested=true;
    g_status="Stopping ChatGPT account operation...";
}
static void chooseChatGptModel(){
    if(!g_authStatus.connected||!g_authStatus.planAuthorized){g_status="Continue with ChatGPT and authorize the plan first";return;}
    if(g_authModels.empty()){startAuthJob(AuthJob::Models);return;}
    HMENU menu=CreatePopupMenu();if(!menu)return;
    size_t shown=std::min<size_t>(g_authModels.size(),64);
    for(size_t i=0;i<shown;++i){const auto& model=g_authModels[i];std::string label=model.displayName.empty()?model.slug:model.displayName+"  /  "+model.slug;
        AppendMenuA(menu,MF_STRING,(UINT_PTR)(700+i),label.c_str());}
    POINT cursor;GetCursorPos(&cursor);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);
    DestroyMenu(menu);PostMessage(g_window,WM_NULL,0,0);
    if(action>=700&&(size_t)(action-700)<shown){const std::string model=g_authModels[(size_t)(action-700)].slug;
        AssistantConfig config=g_assistantConfig;config.authMode=AssistantAuthMode::ChatGptPlan;config.provider="openai";config.protocol=AssistantProtocol::Responses;config.endpoint="https://api.openai.com/v1/responses";config.model=model;config.reasoningEffort.clear();
        validateAssistantConfig(config);g_assistantConfig=std::move(config);*g_assistantCancel=true;++g_documentEpoch;g_conversation=arkjson::Value::makeArr();saveUiSettings();
        g_status="ChatGPT plan model selected: "+model+" / reasoning effort Auto";
    }
}
static void chooseReasoningEffort(){
    if(g_assistantConfig.authMode==AssistantAuthMode::CliAccount){g_status="Grok Build uses its CLI reasoning configuration; no separate effort control is negotiated.";return;}
    if(g_assistantConfig.model.empty()){g_status="Choose an assistant model before setting reasoning effort";return;}
    static const std::set<std::string> vocabulary{"none","minimal","low","medium","high","xhigh","max","ultra"};
    std::vector<std::string> advertised;std::string providerDefault;
    if(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan)
        for(const auto& model:g_authModels)if(model.slug==g_assistantConfig.model){
            for(const auto& effort:model.reasoningEfforts)if(vocabulary.count(effort))advertised.push_back(effort);
            if(vocabulary.count(model.defaultReasoningEffort))providerDefault=model.defaultReasoningEffort;
            break;
        }
    HMENU menu=CreatePopupMenu();if(!menu)return;
    std::string autoLabel="Auto / provider default"+(providerDefault.empty()?std::string{}:" ("+providerDefault+")");
    AppendMenuA(menu,MF_STRING|(g_assistantConfig.reasoningEffort.empty()?MF_CHECKED:0),1,autoLabel.c_str());
    for(size_t i=0;i<advertised.size()&&i<16;++i)AppendMenuA(menu,MF_STRING|(g_assistantConfig.reasoningEffort==advertised[i]?MF_CHECKED:0),(UINT_PTR)(2+i),advertised[i].c_str());
    AppendMenuA(menu,MF_STRING,99,"Custom effort / provider validates...");
    AppendMenuA(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuA(menu,MF_STRING|MF_GRAYED,0,advertised.empty()?"No effort list published for this model":"Listed efforts come from this account's model catalog");
    POINT cursor;GetCursorPos(&cursor);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);
    DestroyMenu(menu);PostMessage(g_window,WM_NULL,0,0);if(!action)return;
    std::string effort;
    if(action>=2&&(size_t)(action-2)<std::min<size_t>(advertised.size(),16))effort=advertised[(size_t)(action-2)];
    else if(action==99){std::vector<EditorField> fields{{"Effort / auto, low, medium, high, xhigh or max",g_assistantConfig.reasoningEffort.empty()?"auto":g_assistantConfig.reasoningEffort}};
        if(!editFields("Reasoning effort","The provider validates this setting for the selected model. Auto omits the reasoning field.",fields))return;
        effort=trimField(fields[0].value);std::transform(effort.begin(),effort.end(),effort.begin(),[](unsigned char c){return (char)std::tolower(c);});if(effort=="auto")effort.clear();
    }
    AssistantConfig config=g_assistantConfig;config.reasoningEffort=effort;validateAssistantConfig(config);g_assistantConfig=std::move(config);saveUiSettings();
    g_status="Reasoning effort "+(effort.empty()?std::string("Auto / provider default"):effort+" / provider validates model support")+" for the next assistant request";
}
static void showAssistantAccountMenu(){
    HMENU menu=CreatePopupMenu();if(!menu)return;
    if(g_authBusy){AppendMenuA(menu,MF_STRING|MF_GRAYED,0,"ChatGPT account operation in progress...");
        AppendMenuA(menu,MF_STRING|(g_authCancelRequested?MF_GRAYED:0),366,g_authCancelRequested?"Stopping account operation...":"Cancel account operation");}
    else if(!g_authStatus.connected){AppendMenuA(menu,MF_STRING,360,"Continue with ChatGPT");
        if(!g_authStatus.pendingClientId.empty())AppendMenuA(menu,MF_STRING,365,"Retry existing connection");}
    else{std::string account=g_authStatus.activeEmail.empty()?"ChatGPT connected":"ChatGPT: "+g_authStatus.activeEmail;
        AppendMenuA(menu,MF_STRING|MF_GRAYED,0,account.c_str());
        if(g_authStatus.planAuthorized)AppendMenuA(menu,MF_STRING,361,"Choose plan model...");
        else AppendMenuA(menu,MF_STRING,364,"Enable plan usage...");
        AppendMenuA(menu,MF_STRING,362,"Sign out");}
    if(!g_authStatus.accounts.empty()){HMENU accounts=CreatePopupMenu();for(size_t i=0;i<g_authStatus.accounts.size()&&i<32;++i){const auto& saved=g_authStatus.accounts[i];
            std::string name=saved.email.empty()?saved.clientId:saved.email;AppendMenuA(accounts,MF_STRING|(saved.selected?MF_CHECKED:0),(UINT_PTR)(500+i),name.c_str());}
        AppendMenuA(menu,MF_POPUP,(UINT_PTR)accounts,g_authStatus.connected?"Switch account":"Reconnect saved account");}
    AppendMenuA(menu,MF_SEPARATOR,0,nullptr);
    std::string effortLabel="Reasoning effort: "+(g_assistantConfig.reasoningEffort.empty()?std::string("Auto"):g_assistantConfig.reasoningEffort);
    AppendMenuA(menu,MF_STRING|(g_assistantConfig.model.empty()?MF_GRAYED:0),367,effortLabel.c_str());
    AppendMenuA(menu,MF_STRING,368,"Local models / Ollama...");
    AppendMenuA(menu,MF_STRING,357,"Connectors / OpenAI, Claude, Grok, Ollama...");
    if(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan)AppendMenuA(menu,MF_STRING,358,"Switch to API key mode");
    decorateMenu(menu);
    POINT cursor;GetCursorPos(&cursor);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);
    DestroyMenu(menu);PostMessage(g_window,WM_NULL,0,0);if(action)interactiveAction(action);
}
static arkjson::Value cancelledTool(){using V=arkjson::Value;V r=V::makeObj(),e=V::makeObj();e.set("error",V::makeStr("Request cancelled before execution"));r.set("isError",V::makeBool(true));r.set("structuredContent",e);return r;}
static void drainUiTools(){
    if(g_dialog)return;
    while(!g_toolQueue.empty()){auto request=std::move(g_toolQueue.front());g_toolQueue.pop_front();
        if(g_closing||request->stop.stop_requested()||(request->cancel&&*request->cancel)){request->complete(cancelledTool());continue;}
        if((request->teamOwned&&request->epoch!=g_documentEpoch)||(request->revision&&(request->epoch!=g_documentEpoch||*request->revision!=g_session.revision))){auto result=cancelledTool();arkjson::Value error=arkjson::Value::makeObj();g_status="Document changed during assistant turn. Inspect and resend from the current revision.";error.set("error",arkjson::Value::makeStr(g_status));result.set("structuredContent",error);g_chatLog.push_back({"ARK",g_status});request->complete(result);if(request->cancel)*request->cancel=true;continue;}
        if(!assistantSteeringToolAllowed(*request)){request->complete(assistantSteeringToolRejection());continue;}
        if(request->teamOwned||request->revision){using V=arkjson::Value;V actor=V::makeObj();actor.set("kind",V::makeStr("agent"));actor.set("id",V::makeStr(request->roleId.empty()?"assistant":request->roleId));std::string model=g_assistantConfig.model;for(size_t i=0;i<teamCapacity;++i)if(request->roleId==g_teamRoleIds[i])model=g_teamModels[i].empty()?(g_teamConnectors[i]?g_teamConnectors[i]->model:g_assistantConfig.model):g_teamModels[i];if(!model.empty())actor.set("model",V::makeStr(model));if(!request->roleId.empty())actor.set("role",V::makeStr(request->roleId));static const std::set<std::string> writes{"doc_apply","doc_new","doc_load","doc_undo","doc_redo","iteration_restore","import_step","import_sheet_step","import_mesh","import_cad","design_containers","library_insert"};request->args.set("actor",actor);}
        if(!request->teamOwned&&!request->revision&&!request->args.find("actor")){using V=arkjson::Value;static const std::set<std::string> writes{"doc_apply","doc_new","doc_load","doc_undo","doc_redo","iteration_restore","import_step","import_sheet_step","import_mesh","import_cad","design_containers","library_insert"};if(writes.count(request->tool)){V actor=V::makeObj();actor.set("kind",V::makeStr("client"));actor.set("id",V::makeStr("live_mcp"));request->args.set("actor",actor);}}
        if((request->teamOwned||request->revision)&&request->tool=="__ark_bom_completion"){try{request->complete(g_session.bomCompletionCheck((bool)request->revision));}catch(...){request->complete(cancelledTool());}continue;}
        if(request->teamOwned&&request->tool=="__ark_activity"){
            try{const auto& a=request->args;auto result=recordAgentActivity(g_doc,a.find("tool")->asStr(),*a.find("arguments"),*a.find("actor"),*a.find("result"),g_session.revision,g_session.revision,a.find("elapsed_ms")->asNum());if(g_doc.agentActivity&&!g_doc.agentActivity->entries.empty())g_doc.agentActivity->entries.back().set("timing_scope",arkjson::Value::makeStr("team coordination wall time; revision sampled after action"));request->complete(result);}catch(...){request->complete(cancelledTool());}continue;
        }
        try{g_activeToolRole=request->roleId;g_activeToolName=request->tool;if(request->tool=="doc_apply"&&!metadataOnlyApply(g_doc,request->args)){captureDraftContext();for(auto& entry:g_draftViews)buildDraftLines(entry.second);}g_ownAssistantTool=(bool)request->revision||request->teamOwned;auto result=runTool(request->tool,request->args);g_ownAssistantTool=false;g_activeToolRole.clear();g_activeToolName.clear();if(request->revision)*request->revision=g_session.revision;request->complete(result);}catch(...){g_ownAssistantTool=false;g_activeToolRole.clear();g_activeToolName.clear();request->complete(cancelledTool());}
    }
}
static void postTool(std::unique_ptr<UiToolRequest> request){
    if(g_closing||!PostMessage(g_window,WM_ARK_TOOL,0,(LPARAM)request.get())){request->complete(cancelledTool());return;}request.release();
}
static void postChat(UiChatEvent event){auto* pointer=new UiChatEvent(std::move(event));if(g_closing||!PostMessage(g_window,WM_ARK_CHAT,0,(LPARAM)pointer))delete pointer;}
static void postAuth(UiAuthEvent event){auto* pointer=new UiAuthEvent(std::move(event));if(g_closing||!PostMessage(g_window,WM_ARK_AUTH,0,(LPARAM)pointer))delete pointer;}
static bool g_pricingBusy=false;static std::shared_ptr<std::atomic_bool> g_pricingCancel=std::make_shared<std::atomic_bool>(false);
static void refreshPricing(){if(g_pricingBusy||g_headless)return;if(g_pricingWorker.joinable())g_pricingWorker.join();g_pricingCancel=std::make_shared<std::atomic_bool>(false);auto cancel=g_pricingCancel;g_pricingBusy=true;g_pricingWorker=std::thread([cancel]{auto* result=new PricingCatalog;try{*result=fetchOpenAiPricing(cancel.get());}catch(const std::exception& error){result->error=error.what();}if(g_closing||!PostMessage(g_window,WM_ARK_PRICE,0,(LPARAM)result))delete result;});}
static void updateCostDisplay(bool force){
    static auto next=std::chrono::steady_clock::time_point{};auto now=std::chrono::steady_clock::now();if(!force&&now<next)return;next=now+std::chrono::seconds(1);
    if(!g_busy)g_usageDisplayLedger=g_doc.usage;Document display;display.usage=g_usageDisplayLedger;
    if(g_teamStarted)for(size_t i=0;i<teamCapacity;++i){const auto& usage=g_teamLiveUsage[i];if(g_teamReceiptIds[i].empty()||(!usage.reportedResponses&&!usage.missingResponses))continue;UsageReceipt receipt;receipt.id=g_teamReceiptIds[i];receipt.source="studio_team_"+std::string(g_teamRoleIds[i]);receipt.provenance="provider_response";receipt.model=i<g_teamSnapshot.roles.size()?g_teamSnapshot.roles[i].model:g_teamModels[i].empty()?g_assistantConfig.model:g_teamModels[i];receipt.inputTokens=usage.inputTokens;receipt.outputTokens=usage.outputTokens;receipt.totalTokens=usage.totalTokens;receipt.cachedInputTokens=usage.cachedInputTokens;receipt.reasoningTokens=usage.reasoningTokens;receipt.reportedResponses=usage.reportedResponses;receipt.missingResponses=usage.missingResponses;updateUsageSnapshot(display,receipt);}
    g_costEstimate=estimateDocumentCost(display,g_pricing);g_costUsageMissing=usageTotals(display).missingResponses!=0;
}
static std::string costSummary(){auto money=[](double value){char buffer[32];std::snprintf(buffer,sizeof buffer,"%.4g",value);return std::string(buffer);};u64 tokens=g_costEstimate.pricedTokens+g_costEstimate.unpricedTokens;std::string result="Drawing "+std::string(g_costUsageMissing?">=":"")+std::to_string(tokens)+" tokens";if(!g_pricingReady)return result+" / rates "+(g_pricingBusy?"loading":"N/A");result+=" / API eq std ";result+=g_costEstimate.pricedTokens?"$"+money(g_costEstimate.standardShortUsd)+"-"+money(g_costEstimate.standardLongUsd):"N/A";result+=" / credits "+(g_costEstimate.pricedCreditTokens?money(g_costEstimate.standardCredits):"N/A");if(g_costEstimate.partial||g_costEstimate.unpricedTokens||g_costEstimate.unpricedCreditTokens)result+=" (partial)";return result;}
static std::string measuredPerformanceLabel(const AssistantPerformance& performance){
    if(!performance.providerRequests&&!performance.toolCalls)return {};char text[120];std::snprintf(text,sizeof text,"Provider %.1fs / tools %.1fs",performance.providerMilliseconds/1000.,performance.toolMilliseconds/1000.);return text;
}
static void measuredActivityDetails(){
    std::ostringstream detail;detail<<"Measured request and tool activity\r\n\r\n";
    auto row=[&](const std::string& name,const AssistantPerformance& performance){detail<<name<<"\r\n";if(!performance.providerRequests&&!performance.toolCalls){detail<<"No measurements reported for this run.\r\n\r\n";return;}detail<<performance.providerRequests<<" provider requests / "<<performance.providerMilliseconds<<" ms\r\n"<<performance.toolCalls<<" tool calls / "<<performance.toolMilliseconds<<" ms\r\nRequest bytes "<<performance.requestBytes<<" / input "<<performance.inputBytes<<" / tools schema "<<performance.toolSchemaBytes<<" / largest request "<<performance.maxRequestBytes<<"\r\n\r\n";};
    if(g_teamMode)for(const auto& role:g_teamSnapshot.roles)row(role.label.empty()?role.id:role.label,role.performance);else row("Assistant",g_assistantPerformanceRun);
    detail<<"Provider time includes network/server wall time. Tool time includes the UI queue and native execution. Parallel role times may overlap; they are not total elapsed time. Bytes are serialized request measurements, not token estimates or charges.";
    if(!g_dialogSmoke)MessageBoxA(g_window,detail.str().c_str(),"Measured agent activity",MB_OK|MB_ICONINFORMATION);
}
static void pricingDetails(){
    updateCostDisplay(true);std::ostringstream text;text<<costSummary()<<"\r\n\r\nCurrent-rate standard API-equivalent USD scenarios, not a bill.\r\nPurchased standard credit equivalent is not plan allowance or actual account spending.\r\nHistorical receipts are revalued at the currently fetched published rates.\r\nCache-write charges, request context, service tier and region are unknown. Output includes reasoning once.\r\nUnpriced API tokens: "<<g_costEstimate.unpricedTokens<<" / Unpriced credit tokens: "<<g_costEstimate.unpricedCreditTokens<<"\r\nFetched UTC: "<<(g_pricing.fetchedAtUtc.empty()?"Unavailable":g_pricing.fetchedAtUtc)<<"\r\nAPI source: "<<g_pricing.sourceUrl<<"\r\nPlan/credit source: "<<g_pricing.subscriptionSourceUrl<<"\r\n\r\n"<<g_pricing.subscriptionSummary;
    std::set<std::string> selected{g_assistantConfig.model};for(size_t i=0;i<teamCapacity;++i)selected.insert(g_teamModels[i].empty()?g_assistantConfig.model:g_teamModels[i]);text<<"\r\n\r\nSelected models / standard USD per million tokens:\r\n";for(const auto& model:selected){if(model.empty())continue;auto rate=std::find_if(g_pricing.models.begin(),g_pricing.models.end(),[&](const auto& price){return price.model==model;});text<<model<<": ";if(rate==g_pricing.models.end())text<<"Unavailable";else text<<"input "<<rate->inputPerMillion<<" / cached input "<<rate->cachedInputPerMillion<<" / output "<<rate->outputPerMillion<<" (negative means unavailable)";text<<"\r\n";}
    text<<"\r\nCPU sys and RAM sys include all applications and kernel workers. ARK CPU/RAM are this process only; RAM "<<(g_resources.privateRam?"private working set":"working set including shared pages")<<". GPU is this process's busiest engine; VRAM is dedicated bytes across matching adapters. Missing counters are N/A. Sampling is once per second.\r\n";if(!g_pricing.error.empty())text<<"\r\nPricing fetch: "<<g_pricing.error;if(!g_dialogSmoke)MessageBoxA(g_window,text.str().c_str(),"Resources and drawing cost / scope and sources",MB_OK|MB_ICONINFORMATION);
}
static AssistantConfig teamRoleConfigValues(const std::string& model,const std::string& effort){
    auto config=g_assistantConfig;if(!model.empty())config.model=model;
    if(!effort.empty())config.reasoningEffort=effort=="auto"?"":effort;
    else if(!config.reasoningEffort.empty()){
        if(config.authMode==AssistantAuthMode::ChatGptPlan){auto selected=std::find_if(g_authModels.begin(),g_authModels.end(),[&](const auto& choice){return choice.slug==config.model;});if(selected==g_authModels.end()||std::find(selected->reasoningEfforts.begin(),selected->reasoningEfforts.end(),config.reasoningEffort)==selected->reasoningEfforts.end())config.reasoningEffort.clear();}
        else if(!model.empty()&&model!=g_assistantConfig.model)config.reasoningEffort.clear();
    }return config;
}
static AssistantConfig teamRoleConfig(size_t index){
    if(!g_teamConnectors[index])return teamRoleConfigValues(g_teamModels[index],g_teamEfforts[index]);
    auto config=*g_teamConnectors[index];config.maxRounds=g_assistantConfig.maxRounds;if(!g_teamModels[index].empty())config.model=g_teamModels[index];config.reasoningEffort=g_teamEfforts[index]=="auto"?"":g_teamEfforts[index];return config;
}
static std::string assistantPrompt(const std::string& explicitPrompt={}){
    if(!explicitPrompt.empty())return explicitPrompt;if(!g_chatInput)return {};
    int n=GetWindowTextLengthW(g_chatInput);std::vector<wchar_t> buffer(n+1);GetWindowTextW(g_chatInput,buffer.data(),n+1);
    int size=WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,nullptr,0,nullptr,nullptr);std::string prompt((size_t)size,' ');WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,prompt.data(),size,nullptr,nullptr);return prompt;
}
static void cancelTeam(){*g_teamCancel=true;g_agentTeam.cancel();g_status="Team cancellation requested; completed document changes remain.";}
static void pollTeam(bool force=false){
    static auto next=std::chrono::steady_clock::time_point{};auto now=std::chrono::steady_clock::now();if(g_busy||!g_teamStarted||(!force&&now<next))return;next=now+std::chrono::milliseconds(40);
    g_teamSnapshot=g_agentTeam.snapshot(false,g_teamEventSequence);
    if(g_teamEpoch!=g_documentEpoch){cancelTeam();g_teamStarted=false;resetTeamView();return;}
    for(const auto& event:g_teamSnapshot.events){if(event.sequence<=g_teamEventSequence)continue;g_teamEventSequence=event.sequence;
        size_t index=teamCapacity;for(size_t i=0;i<teamCapacity;++i)if(event.roleId==g_teamRoleIds[i])index=i;
        if(index<teamCapacity&&event.kind!=AgentTeamEvent::Usage)g_teamActivity[index]=(event.kind==AgentTeamEvent::ToolStart?"Running ":event.kind==AgentTeamEvent::ToolEnd?(event.toolError?"Failed ":"Completed "):"")+(event.tool.empty()?event.text:event.tool+(event.toolError?" / "+event.text:""));
        if(event.kind==AgentTeamEvent::Usage||(event.kind==AgentTeamEvent::Status&&!event.providerRetry))continue;
        std::string detail=event.kind==AgentTeamEvent::ToolStart?"Tool started: "+event.tool:event.kind==AgentTeamEvent::ToolEnd?(event.toolError?"Tool failed: ":"Tool finished: ")+event.tool+(event.toolError?" / "+event.text:""):event.text;
        if(!detail.empty()){g_chatLog.push_back({event.roleId.empty()?"Team":event.roleId,detail.substr(0,16000)});g_chatScroll=0;}
    }
    if(g_chatLog.size()>200)g_chatLog.erase(g_chatLog.begin(),g_chatLog.end()-200);
    for(size_t i=0;i<g_teamSnapshot.roles.size()&&i<teamCapacity;++i){const auto& role=g_teamSnapshot.roles[i];const auto& usage=role.usage;if(!usage.reportedResponses&&!usage.missingResponses)continue;
        if(g_teamReceipts[i]&&g_teamReceipts[i]->reportedResponses==usage.reportedResponses&&g_teamReceipts[i]->missingResponses==usage.missingResponses&&g_teamReceipts[i]->totalTokens==usage.totalTokens)continue;
        UsageReceipt receipt;receipt.id=g_teamReceiptIds[i];receipt.source="studio_team_"+role.id;receipt.provenance="provider_response";receipt.model=role.model;
        receipt.inputTokens=usage.inputTokens;receipt.outputTokens=usage.outputTokens;receipt.totalTokens=usage.totalTokens;receipt.cachedInputTokens=usage.cachedInputTokens;receipt.reasoningTokens=usage.reasoningTokens;receipt.reportedResponses=usage.reportedResponses;receipt.missingResponses=usage.missingResponses;g_teamReceipts[i]=receipt;applyUsageReceipt(receipt);
    }
}
struct RoleModelEditor {size_t role=0;HWND model=nullptr,effort=nullptr,status=nullptr;std::vector<std::string> models,efforts;std::string account;};
static std::string g_roleSmokeModel,g_roleSmokeEffort;static bool g_roleSmokeCheck=false,g_roleSmokePassed=false;
static bool g_roleSmokeInitialDisabled=false,g_roleSmokeKeyboardPassed=false,g_roleSmokeRefresh=false,g_roleSmokeRefreshPassed=false;static constexpr UINT WM_ARK_ROLE_SMOKE=WM_APP+25;
static std::string roleComboValue(HWND combo,const std::vector<std::string>& choices){auto selected=SendMessage(combo,CB_GETCURSEL,0,0);return selected>=0&&(size_t)selected<choices.size()?choices[(size_t)selected]:std::string{};}
static void refillRoleEfforts(RoleModelEditor& editor,const std::string& preferred){
    auto model=roleComboValue(editor.model,editor.models);auto config=teamRoleConfigValues(model,"");std::string inherited=config.reasoningEffort.empty()?"Auto":config.reasoningEffort;
    SendMessage(editor.effort,CB_RESETCONTENT,0,0);editor.efforts={"","auto"};std::string inheritLabel="Inherit Assistant effort ("+inherited+")";SendMessageA(editor.effort,CB_ADDSTRING,0,(LPARAM)inheritLabel.c_str());
    auto metadata=std::find_if(g_authModels.begin(),g_authModels.end(),[&](const auto& choice){return choice.slug==config.model;});std::string defaultLabel="Auto / provider default";if(metadata!=g_authModels.end()&&!metadata->defaultReasoningEffort.empty())defaultLabel+=" ("+metadata->defaultReasoningEffort+")";SendMessageA(editor.effort,CB_ADDSTRING,0,(LPARAM)defaultLabel.c_str());
    if(metadata!=g_authModels.end())for(const auto& effort:metadata->reasoningEfforts)if(std::find(editor.efforts.begin(),editor.efforts.end(),effort)==editor.efforts.end()){editor.efforts.push_back(effort);SendMessageA(editor.effort,CB_ADDSTRING,0,(LPARAM)effort.c_str());}
    auto found=std::find(editor.efforts.begin(),editor.efforts.end(),preferred);SendMessage(editor.effort,CB_SETCURSEL,found==editor.efforts.end()?1:(WPARAM)(found-editor.efforts.begin()),0);
    SetWindowTextA(editor.status,metadata==g_authModels.end()?"The inherited model is unavailable. Choose a model from this account's catalog.":"Reasoning choices and provider default come from this selected model's account catalog.");
    EnableWindow(editor.effort,metadata!=g_authModels.end());EnableWindow(GetDlgItem(g_roleModelWindow,1),metadata!=g_authModels.end());
}
static void refillRoleModels(RoleModelEditor& editor,const std::string& preferred,const std::string& effort){
    SendMessage(editor.model,CB_RESETCONTENT,0,0);editor.models={""};std::string inheritLabel="Inherit Assistant model ("+(g_assistantConfig.model.empty()?std::string("not selected"):g_assistantConfig.model)+")";SendMessageA(editor.model,CB_ADDSTRING,0,(LPARAM)inheritLabel.c_str());
    for(const auto& model:g_authModels){editor.models.push_back(model.slug);std::string label=(model.displayName.empty()?model.slug:model.displayName+" / "+model.slug);SendMessageA(editor.model,CB_ADDSTRING,0,(LPARAM)label.c_str());}
    bool unavailable=!preferred.empty()&&std::find(editor.models.begin(),editor.models.end(),preferred)==editor.models.end();if(unavailable){editor.models.push_back(preferred);std::string label="[Unavailable] "+preferred;SendMessageA(editor.model,CB_ADDSTRING,0,(LPARAM)label.c_str());}
    auto selected=std::find(editor.models.begin(),editor.models.end(),preferred);SendMessage(editor.model,CB_SETCURSEL,selected==editor.models.end()?0:(WPARAM)(selected-editor.models.begin()),0);refillRoleEfforts(editor,effort);
    if(unavailable)SetWindowTextA(editor.status,"The saved model is unavailable. Apply is disabled until you choose a listed model or a valid inheritance.");
}
static bool requestRoleCatalog(size_t index){
    if(g_authBusy&&g_activeAuthJob!=AuthJob::Models){g_status="Finish the current account operation before refreshing role models.";return false;}
    g_pendingRoleSettings=(int)index;g_pendingRoleAccount=g_authStatus.activeClientId;g_roleCatalogReady=false;
    if(!g_authBusy)startAuthJob(AuthJob::Models);return true;
}
static LRESULT CALLBACK roleModelProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    auto* editor=(RoleModelEditor*)GetWindowLongPtr(hwnd,GWLP_USERDATA);
    if(editor&&message==WM_ARK_ROLE_SMOKE&&g_roleSmokeCheck){bool closed=!SendMessage(GetFocus(),CB_GETDROPPEDSTATE,0,0)&&!g_dialogDone;if(wp==1){g_roleSmokeKeyboardPassed=closed;SetFocus(editor->effort);SendMessage(editor->effort,CB_SHOWDROPDOWN,TRUE,0);PostMessage(editor->effort,WM_KEYDOWN,VK_RETURN,0);PostMessage(hwnd,WM_ARK_ROLE_SMOKE,2,0);}else{g_roleSmokeKeyboardPassed=g_roleSmokeKeyboardPassed&&closed;SetFocus(editor->model);return editorProc(hwnd,WM_TIMER,901,0);}return 0;}
    if(editor&&message==WM_COMMAND&&LOWORD(wp)==101&&HIWORD(wp)==CBN_SELCHANGE){auto prior=roleComboValue(editor->effort,editor->efforts);refillRoleEfforts(*editor,prior);return 0;}
    if(editor&&message==WM_COMMAND&&LOWORD(wp)==103){if(!requestRoleCatalog(editor->role)){SetWindowTextA(editor->status,g_status.c_str());return 0;}SetWindowTextA(editor->status,"Refreshing the selected account's models...");EnableWindow(editor->model,FALSE);EnableWindow(editor->effort,FALSE);EnableWindow(GetDlgItem(hwnd,1),FALSE);EnableWindow(GetDlgItem(hwnd,103),FALSE);return 0;}
    if(editor&&message==WM_ARK_ROLE_CATALOG){auto model=roleComboValue(editor->model,editor->models),effort=roleComboValue(editor->effort,editor->efforts);refillRoleModels(*editor,model,effort);EnableWindow(editor->model,TRUE);EnableWindow(GetDlgItem(hwnd,103),TRUE);if(g_authModels.empty())SetWindowTextA(editor->status,g_status.c_str());clearPendingRoleSettings();return 0;}
    if(editor&&message==WM_COMMAND&&LOWORD(wp)==1){if(g_authBusy||!IsWindowEnabled(GetDlgItem(hwnd,1)))return 0;auto model=roleComboValue(editor->model,editor->models),effort=roleComboValue(editor->effort,editor->efforts);try{auto config=teamRoleConfigValues(model,effort);validateAssistantConfig(config);auto metadata=std::find_if(g_authModels.begin(),g_authModels.end(),[&](const auto& candidate){return candidate.slug==config.model;});if(editor->account!=g_authStatus.activeClientId||metadata==g_authModels.end())throw std::runtime_error("Choose a model available in this account's catalog.");if(!config.reasoningEffort.empty()&&std::find(metadata->reasoningEfforts.begin(),metadata->reasoningEfforts.end(),config.reasoningEffort)==metadata->reasoningEfforts.end())throw std::runtime_error("Choose an effort advertised for the selected model.");}catch(const std::exception& error){SetWindowTextA(editor->status,error.what());return 0;}clearPendingRoleSettings();}
    if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(wp)==2)){g_pendingRoleSettings=-1;g_roleCatalogReady=false;}
    if(editor&&message==WM_TIMER&&wp==901&&g_roleSmokeCheck){
        KillTimer(hwnd,901);g_roleSmokeInitialDisabled=!IsWindowEnabled(GetDlgItem(hwnd,1));
        if(g_roleSmokeRefresh){auto savedModel=g_teamModels[editor->role],savedEffort=g_teamEfforts[editor->role];g_authBusy=true;g_activeAuthJob=AuthJob::SignOut;SendMessage(hwnd,WM_COMMAND,103,0);bool rejected=IsWindowEnabled(editor->model)&&IsWindowEnabled(GetDlgItem(hwnd,1));g_activeAuthJob=AuthJob::Models;SendMessage(hwnd,WM_COMMAND,103,0);bool blocked=!IsWindowEnabled(editor->model)&&!IsWindowEnabled(GetDlgItem(hwnd,1));auto* event=new UiAuthEvent;event->job=AuthJob::Models;event->status=g_authStatus;event->models=g_authModels;SendMessage(g_window,WM_ARK_AUTH,0,(LPARAM)event);g_roleSmokeRefreshPassed=rejected&&blocked&&IsWindowEnabled(editor->model)&&IsWindowEnabled(GetDlgItem(hwnd,1))&&g_teamModels[editor->role]==savedModel&&g_teamEfforts[editor->role]==savedEffort;}
        auto picked=std::find(editor->models.begin(),editor->models.end(),g_roleSmokeModel);if(picked!=editor->models.end()){SendMessage(editor->model,CB_SETCURSEL,(WPARAM)(picked-editor->models.begin()),0);SendMessage(hwnd,WM_COMMAND,MAKEWPARAM(101,CBN_SELCHANGE),(LPARAM)editor->model);}
        auto effort=std::find(editor->efforts.begin(),editor->efforts.end(),g_roleSmokeEffort);if(effort!=editor->efforts.end())SendMessage(editor->effort,CB_SETCURSEL,(WPARAM)(effort-editor->efforts.begin()),0);
        char modelClass[32]{},effortClass[32]{};GetClassNameA(editor->model,modelClass,32);GetClassNameA(editor->effort,effortClass,32);auto metadata=std::find_if(g_authModels.begin(),g_authModels.end(),[&](const auto& choice){return choice.slug==g_roleSmokeModel;});
        g_roleSmokePassed=picked!=editor->models.end()&&effort!=editor->efforts.end()&&std::strcmp(modelClass,"ComboBox")==0&&std::strcmp(effortClass,"ComboBox")==0&&metadata!=g_authModels.end()&&editor->efforts.size()==metadata->reasoningEfforts.size()+2;
        SetFocus(editor->model);SendMessage(editor->model,CB_SHOWDROPDOWN,TRUE,0);PostMessage(editor->model,WM_KEYDOWN,VK_ESCAPE,0);PostMessage(hwnd,WM_ARK_ROLE_SMOKE,1,0);return 0;
    }
    return editorProc(hwnd,message,wp,lp);
}
static bool editRoleModel(size_t index,std::string& model,std::string& effort){
    WNDCLASSA cls{};cls.lpfnWndProc=roleModelProc;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="ARK_ROLE_MODELS";cls.hbrBackground=dialogBrush;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&cls);
    RECT parent;GetWindowRect(g_window,&parent);int width=780,height=350;RECT outer{0,0,width,height};AdjustWindowRectEx(&outer,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);int ow=outer.right-outer.left,oh=outer.bottom-outer.top;
    g_dialog=CreateWindowExA(WS_EX_DLGMODALFRAME,cls.lpszClassName,g_teamRoleLabels[index],WS_CAPTION|WS_SYSMENU,parent.left+(parent.right-parent.left-ow)/2,parent.top+(parent.bottom-parent.top-oh)/2,ow,oh,g_window,nullptr,cls.hInstance,nullptr);if(!g_dialog)throw std::runtime_error("Could not open role model settings.");g_roleModelWindow=g_dialog;
    BOOL dark=TRUE;DwmSetWindowAttribute(g_dialog,20,&dark,sizeof dark);HFONT font=CreateFontA(-17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
    auto control=[&](const char* type,const char* text,DWORD style,int x,int y,int w,int h,int id){HWND result=CreateWindowExA(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,g_dialog,(HMENU)(INT_PTR)id,cls.hInstance,nullptr);SendMessage(result,WM_SETFONT,(WPARAM)font,TRUE);return result;};
    control("STATIC",g_teamRoleLabels[index],0,26,22,728,27,0);control("STATIC","Choose from the connected account's OpenAI catalog. Changes apply together to the next team run.",0,26,60,728,44,0);
    control("STATIC","Model",0,26,119,150,28,0);control("STATIC","Reasoning effort",0,26,181,150,28,0);RoleModelEditor editor;editor.role=index;editor.account=g_authStatus.activeClientId;
    editor.model=control("COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,185,113,569,240,101);editor.effort=control("COMBOBOX","",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,185,175,569,240,102);editor.status=control("STATIC","",0,26,229,728,44,104);SetWindowLongPtr(g_dialog,GWLP_USERDATA,(LONG_PTR)&editor);refillRoleModels(editor,model,effort);
    control("BUTTON","Refresh models",WS_TABSTOP|BS_OWNERDRAW,26,292,180,34,103);control("BUTTON","Cancel",WS_TABSTOP|BS_OWNERDRAW,532,292,100,34,2);control("BUTTON","Apply",WS_TABSTOP|BS_OWNERDRAW,648,292,106,34,1);refillRoleEfforts(editor,effort);
    g_edit=editor.model;g_dialogDone=g_dialogAccept=false;EnableWindow(g_window,FALSE);ShowWindow(g_dialog,SW_SHOW);SetFocus(editor.model);if(g_dialogSmoke)SetTimer(g_dialog,901,200,nullptr);
    MSG msg;while(!g_dialogDone&&GetMessage(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&(msg.wParam==VK_ESCAPE||msg.wParam==VK_RETURN)){if(SendMessage(GetFocus(),CB_GETDROPPEDSTATE,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}else SendMessage(g_dialog,WM_COMMAND,msg.wParam==VK_ESCAPE?2:GetDlgCtrlID(GetFocus())==2?2:GetDlgCtrlID(GetFocus())==103?103:1,0);continue;}if(!IsDialogMessage(g_dialog,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}}
    if(g_dialogAccept){model=roleComboValue(editor.model,editor.models);effort=roleComboValue(editor.effort,editor.efforts);}g_pendingRoleSettings=-1;g_roleCatalogReady=false;EnableWindow(g_window,TRUE);DestroyWindow(g_dialog);g_dialog=g_roleModelWindow=nullptr;DeleteObject(font);if(!g_headless)SetForegroundWindow(g_window);return g_dialogAccept;
}
static void teamRoleDialog(size_t index){
    if(g_teamStarted&&g_agentTeam.snapshot().running){g_status="Finish or cancel the team before changing role settings. Changes apply to a new team run.";return;}
    std::string model=g_teamModels[index],effort=g_teamEfforts[index];
    if(teamRoleConfig(index).authMode==AssistantAuthMode::CliAccount){auto config=teamRoleConfig(index);bool inherit=false;if(!editConnector(config,true,inherit))return;if(g_teamStarted){cancelTeam();g_teamStarted=false;resetTeamView();}if(inherit){g_teamConnectors[index].reset();g_teamModels[index].clear();g_teamEfforts[index].clear();}else{g_teamConnectors[index]=config;g_teamModels[index]=config.model;g_teamEfforts[index].clear();}if(!g_dialogSmoke)saveUiSettings();g_status="Grok Build agent connector saved for the next run.";return;}
    if(teamRoleConfig(index).provider=="ollama"){auto config=teamRoleConfig(index);if(!editLocalModel(config))return;if(g_teamStarted){cancelTeam();g_teamStarted=false;resetTeamView();}g_teamConnectors[index]=config;g_teamModels[index]=config.model;g_teamEfforts[index]=config.reasoningEffort.empty()?"auto":config.reasoningEffort;if(!g_dialogSmoke)saveUiSettings();g_status="Local agent model saved for the next run.";return;}
    if(teamRoleConfig(index).authMode==AssistantAuthMode::ChatGptPlan){if(!g_authStatus.connected||!g_authStatus.planAuthorized){g_status="Connect and authorize ChatGPT from Account before choosing role models.";return;}if(g_authBusy||g_authModels.empty()){requestRoleCatalog(index);return;}if(!editRoleModel(index,model,effort))return;}
    else{auto config=teamRoleConfig(index);std::vector<EditorField> fields{{"Model (manual)",model.empty()?config.model:model},{"Effort (manual)",effort}};if(!editFields("Advanced role connector","Configure this card model and provider-supported effort. Blank inherits the card connector model; auto uses the provider default.",fields))return;model=trimField(fields[0].value);effort=trimField(fields[1].value);if(!model.empty())config.model=model;config.reasoningEffort=effort=="auto"?"":effort;validateAssistantConfig(config);}
    if(g_teamStarted){cancelTeam();g_teamStarted=false;resetTeamView();}g_teamModels[index]=model;g_teamEfforts[index]=effort;if(!g_dialogSmoke)saveUiSettings();g_status="Role model and reasoning saved together for the next team run.";
}
static void openPendingRoleSettings(){if(g_pendingRoleSettings<0||!g_roleCatalogReady||g_dialog||g_busy)return;int role=g_pendingRoleSettings;g_pendingRoleSettings=-1;g_roleCatalogReady=false;if(g_pendingRoleAccount==g_authStatus.activeClientId&&g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan&&!g_authModels.empty())teamRoleDialog((size_t)role);}
static void chooseTeamTarget(){
    HMENU menu=CreatePopupMenu();AppendMenuA(menu,MF_STRING|((g_teamFocusedWorkflow?g_teamTarget=="all":g_teamTarget.empty())?MF_CHECKED:0),1,"Entire team");for(unsigned i=0;i<g_teamRoleCount;++i)AppendMenuA(menu,MF_STRING|(g_teamTarget==g_teamRoleIds[i]?MF_CHECKED:0),i+2,g_teamRoleLabels[i]);
    POINT cursor;GetCursorPos(&cursor);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);DestroyMenu(menu);if(action>=1&&action<=(int)g_teamRoleCount+1)g_teamTarget=action==1?(g_teamFocusedWorkflow?"all":""):g_teamRoleIds[action-2];
}
// The pending object is an immutable identity, separate from the live selection.
static bool attachPendingObjectContext(std::string& prompt){
    if(!g_pendingObjectTarget)return true;
    using V=arkjson::Value;const auto target=*g_pendingObjectTarget;
    auto reject=[&](const std::string& reason){g_pendingObjectTarget->invalidated=reason;g_error=true;g_status=reason;g_chatLog.push_back({"ARK",reason});return false;};
    if(!target.invalidated.empty())return reject(target.invalidated);
    if(target.epoch!=g_objectDocumentEpoch)return reject("The object belongs to a different drawing. Select it again before sending.");
    const Feature* feature=g_doc.find(target.feature);
    if(!feature)return reject("The selected object was deleted. Select another object before sending.");
    if(target.node){auto node=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& item){return item.id==target.node;});
        if(node==g_doc.assembly.end())return reject("The selected occurrence was removed or replaced. Select it again before sending.");
        std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(g_doc,placements,error)||std::none_of(placements.begin(),placements.end(),[&](const AssemblyPlacement& p){return p.node==target.node&&p.copy==target.copy&&p.feature==target.feature;}))return reject("The selected occurrence copy was removed or replaced. Select it again before sending.");}
    V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("depth",V::makeNum(0));
    if(target.node){args.set("node",V::makeNum((double)target.node));args.set("copy",V::makeNum(target.copy));}
    else{V ids=V::makeArr();ids.arr.push_back(V::makeNum((double)target.feature));args.set("ids",std::move(ids));}
    auto result=g_session.call("object_context",args);auto* failed=result.find("isError");auto* context=result.find("structuredContent");
    if((failed&&failed->b)||!context)return reject("The selected object no longer resolves. Select it again before sending.");
    prompt+="\n\nSelected object_context (current drawing revision; this exact identity is the user target):\n"+context->dump();return true;
}
static void askAgentAboutBody(int body){
    if(body<0||(size_t)body>=g_bodyIds.size())throw std::runtime_error("Select a body before asking the agent");
    const Feature* feature=g_doc.find(g_bodyIds[(size_t)body]);if(!feature)throw std::runtime_error("The selected body no longer exists");
    PendingObjectTarget target;target.feature=feature->id;target.node=g_instanceIds[(size_t)body];target.copy=g_instanceCopies[(size_t)body];target.epoch=g_objectDocumentEpoch;
    target.label=target.node?g_instanceNames[(size_t)body]+" / node #"+std::to_string(target.node)+" / copy "+std::to_string(target.copy):feature->name+" / feature #"+std::to_string(target.feature);
    g_pendingObjectTarget=std::move(target);selBody=body;g_pickKind=0;g_picked.clear();refreshSelectionLines();g_explorerFeature=0;g_toolsOpen=false;g_assistantDockOpen=true;layout();
    if(g_chatInput){if(GetWindowTextLengthW(g_chatInput)==0)SetWindowTextW(g_chatInput,L"What would you like to know about this object?");SetFocus(g_chatInput);SendMessage(g_chatInput,EM_SETSEL,0,-1);}
    g_error=false;g_status="Object attached. Enter a question, then Send or Queue to ask the agent.";
}
#include "image_attachment.inl"
#include "steer_024b.inl"
#include "studio_feedback_fixture.inl"
static void sendTeam(const std::string& explicitPrompt={},const std::array<AssistantTransport,8>& transports={}){
    if(g_busy&&!g_teamStarted){g_status="Wait for the current document operation before starting a new team.";return;}
    if(g_chatBusy){g_status="Wait for the single Assistant turn to finish.";return;}std::string prompt=assistantPrompt(explicitPrompt);if(prompt.find_first_not_of(" \r\n\t")==std::string::npos&&(!explicitPrompt.empty()||!g_pendingAssistantImage))return;
    const std::string visiblePrompt=explicitPrompt.empty()?assistantImageVisiblePrompt(prompt):prompt;if(!attachPendingObjectContext(prompt))return;auto userMessage=explicitPrompt.empty()?pendingAssistantImageMessage(prompt):assistantUserMessage(prompt);
    pollTeam(true);if(g_teamStarted&&!g_teamSnapshot.cancelled){if(!g_agentTeam.steer(userMessage,g_teamTarget)){g_status="Decision could not be queued; check team state and turn limits.";return;}g_chatLog.push_back({"You -> "+(g_teamTarget.empty()?std::string("team"):g_teamTarget),visiblePrompt});g_status="Decision queued for the next provider request.";}
    else{
        if(g_teamStarted&&g_teamSnapshot.running){g_status="Team is still stopping.";return;}
        AgentTeamConfig team;team.maxWorkers=g_teamRoleCount;team.focusedWorkflow=g_teamFocusedWorkflow;team.orchestratedStart=g_teamFocusedWorkflow||std::none_of(transports.begin(),transports.end(),[](const auto& t){return (bool)t;});static const char* instructions[]={
            "Own design concept and orchestration, actual part sketches/features/materials/parameters and assembly. Start with the user's intent and a distinctive coherent concept; preserve requested curves, spatial form and proportions. Inspect the current document/revision before edits. Apply coherent atomic batches of related sketches, features and placements using one observed expected_revision, then inspect actual validation results. Delegate concrete tasks and requested checks through team_context/team_proposal. Honor user-specified dimensions, materials and process constraints from the start; undeclared routes remain unspecified during concept work. Detailed fabrication checks belong to a requested production-readiness checkpoint or process claim, not every concept step or rendering/export action. Preserve the existing drawing; load/new/import/save/export operations require task authorization. Report actual tool results and remaining decisions.",
            "Own part design and actual sketches, features, materials and spatial form. Develop the requested concept before detailing fabrication; use curves when they express the design, and do not reduce a concept to boxes just to simplify a workflow. Inspect the current document/revision before edits. Write coherent atomic batches of related sketch entities and part features using one observed expected_revision; preserve the requested geometry. Honor explicit dimensions, materials and process constraints from the start; keep undeclared routes unspecified. The engineer owns assembly placements. Do not replace, load, save or export unrelated drawings. Send assembly decisions to engineer. Never claim an unexecuted proposal is implemented.",
            "Own independent checks requested by the user or at an explicit design checkpoint. Do not impose a universal manufacturability review before concept development. Use actual math_eval, physics_inspect, materials_inspect, geometry and relevant manufacturing tools for those checks. Submit typed measured revision proposals with team_propose to engineer or designer. You are read-only. Report inspected evidence and unresolved checks; do not invent successful tests."};
        for(size_t i=0;i<g_teamRoleCount;++i){auto config=teamRoleConfig(i);try{validateAssistantConfig(config);
                if(!transports[i]&&config.authMode==AssistantAuthMode::ApiKey&&!assistantKeyAvailable(config))throw std::runtime_error("The configured credential environment variable is unavailable.");
                if(!transports[i]&&config.authMode==AssistantAuthMode::ChatGptPlan){if(!g_authStatus.connected||!g_authStatus.planAuthorized)throw std::runtime_error("Connect and authorize ChatGPT from Account first.");if(g_authModels.empty())throw std::runtime_error("Load the account model catalog from Account first.");if(std::none_of(g_authModels.begin(),g_authModels.end(),[&](const auto& model){return model.slug==config.model;}))throw std::runtime_error("The selected model is unavailable in this account's catalog.");}
            }catch(const std::exception& error){throw std::runtime_error(std::string(g_teamRoleLabels[i])+": "+error.what());}
            AgentTeamRole role;role.id=g_teamRoleIds[i];role.label=g_teamRoleLabels[i];role.instructions=instructions[i==2?2:i==0?0:1];if(!transports[i])config.onboardingPack=agentOnboardingPack(i==2?std::vector<std::string>{"overview","physics"}:std::vector<std::string>{"overview"});role.assistant=config;role.transport=feedbackFixtureTransport(transports[i]);role.canWrite=i!=2;role.discipline=i==0?AgentTeamDiscipline::Orchestrator:i==2?AgentTeamDiscipline::PhysicsReviewer:AgentTeamDiscipline::PartDesigner;team.roles.push_back(std::move(role));}
        g_teamCancel=std::make_shared<std::atomic_bool>(false);auto cancel=g_teamCancel;auto epoch=g_documentEpoch;auto runSerial=++g_teamRunSerial;g_teamEpoch=epoch;g_teamEventSequence=0;g_teamReceipts={};g_teamActivity={};g_teamLiveUsage={};g_draftViews.clear();g_liveCompletedSteps=0;g_liveStepLabel.clear();captureDraftContext();g_teamGoal=visiblePrompt;for(auto& id:g_teamReceiptIds)id=newUsageReceiptId();if(!transports[0]&&!transports[1]&&!transports[2]&&std::any_of(team.roles.begin(),team.roles.end(),[](const auto& role){return role.assistant.provider!="ollama"&&role.assistant.authMode!=AssistantAuthMode::CliAccount;}))refreshPricing();
        g_assistantReceiptId.clear();g_currentUsageReceipt.reset();g_pendingUsageReceipt.reset();
        auto teamExecutor=[cancel,epoch](const std::string& name,const arkjson::Value& args){
            auto promise=std::make_shared<std::promise<arkjson::Value>>();auto future=promise->get_future();auto request=std::make_unique<UiToolRequest>();request->tool=name;request->roleId=AgentTeam::currentRoleId();request->steeringVersion=g_agentTeam.requestVersion(request->roleId);request->args=args;request->cancel=cancel;request->epoch=epoch;request->teamOwned=true;request->complete=[promise](arkjson::Value result){promise->set_value(std::move(result));};postTool(std::move(request));
            while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready)if(*cancel||g_closing)throw std::runtime_error("Team cancelled");return future.get();
        };
        if(std::none_of(transports.begin(),transports.end(),[](const auto& t){return (bool)t;}))team.completionCheck=[teamExecutor]{return teamExecutor("__ark_bom_completion",arkjson::Value::makeObj());};
        team.recordAction=[teamExecutor](const std::string&,const std::string& name,const arkjson::Value& args,const arkjson::Value& result,double elapsed){using V=arkjson::Value;V record=V::makeObj(),summary=V::makeObj(),answer=V::makeObj();record.set("tool",V::makeStr(name));record.set("elapsed_ms",V::makeNum(elapsed));if(const auto* commands=args.find("commands")){V rows=V::makeArr();for(const auto& command:commands->arr){V row=V::makeObj();if(const auto* op=command.find("op"))row.set("op",*op);rows.arr.push_back(row);}summary.set("commands",rows);}record.set("arguments",summary);if(const auto* error=result.find("isError"))answer.set("isError",*error);const auto* payload=result.find("structuredContent");if(!payload)payload=&result;for(const auto* key:{"isError","error","error_code"})if(const auto* value=payload->find(key))answer.set(key,*value);record.set("result",answer);auto receipt=teamExecutor("__ark_activity",record);V returned=result;const auto* data=receipt.find("structuredContent");if(!data)data=&receipt;for(const auto* key:{"activity_id","activity_project_id","activity_boundary","pending_feedback_ids","feedback_required","activity_capture_error"})if(const auto* value=data->find(key))returned.set(key,*value);return returned;};
        g_agentTeam.start(team,userMessage,AgentSession::tools(),teamExecutor,[epoch,runSerial](const AgentTeamEvent& event){auto* value=new UiTeamEvent{event,epoch,runSerial};if(g_closing||!PostMessage(g_window,WM_ARK_TEAM,0,(LPARAM)value))delete value;});g_teamStarted=true;g_chatLog.push_back({"You -> team",visiblePrompt});g_status="Team started; "+std::to_string(g_teamRoleCount)+" agents, assigned parts and coordinated assembly.";
    }
    if(explicitPrompt.empty())clearPendingAssistantImage();g_pendingObjectTarget.reset();g_chatScroll=0;if(g_chatInput)SetWindowTextW(g_chatInput,L"");pollTeam(true);
}
static void sendAssistant(const std::string& explicitPrompt="",const AssistantTransport& transport={},const arkjson::Value* queuedMessage=nullptr,const std::string& queuedVisible={}){
    if(g_teamStarted&&g_agentTeam.snapshot().running){g_status="Wait for the team to finish or cancel it before starting Assistant.";return;}
    std::string prompt=explicitPrompt;
    if(prompt.empty()&&g_chatInput){int n=GetWindowTextLengthW(g_chatInput);std::vector<wchar_t> buffer(n+1);GetWindowTextW(g_chatInput,buffer.data(),n+1);int size=WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,nullptr,0,nullptr,nullptr);prompt.resize(size);WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,prompt.data(),size,nullptr,nullptr);}
    if(!queuedMessage&&prompt.find_first_not_of(" \r\n\t")==std::string::npos&&(!explicitPrompt.empty()||!g_pendingAssistantImage))return;const std::string visiblePrompt=queuedMessage?queuedVisible:explicitPrompt.empty()?assistantImageVisiblePrompt(prompt):prompt;if(!queuedMessage&&!attachPendingObjectContext(prompt))return;using V=arkjson::Value;V user=queuedMessage?*queuedMessage:explicitPrompt.empty()?pendingAssistantImageMessage(prompt):assistantUserMessage(prompt);
    if(g_chatBusy){queueActiveAssistantSteering(user,visiblePrompt,explicitPrompt.empty()&&!queuedMessage);return;}validateAssistantConfig(g_assistantConfig);
    if(g_conversation.arr.empty()&&!transport&&g_assistantConfig.provider!="ollama"&&g_assistantConfig.authMode!=AssistantAuthMode::CliAccount)refreshPricing();
    if(g_assistantWorker.joinable())g_assistantWorker.join();
    V input=g_conversation;if(input.t!=V::Arr)input=V::makeArr();input.arr.push_back(user);
    if(queuedMessage||(explicitPrompt.empty()&&g_pendingAssistantImage)){auto preflight=g_assistantConfig;if(!transport)preflight.onboardingPack=agentOnboardingPack({"overview"});assistantRequest(preflight,input,AgentSession::tools());}
    if(!queuedMessage)g_pendingObjectTarget.reset();if(!queuedMessage)g_chatLog.push_back({"You",visiblePrompt});g_chatScroll=0;g_chatBusy=true;g_assistantResponseFinished=g_assistantResponseError=false;g_assistantStatus="Working...";g_assistantUsageRun={};g_assistantPerformanceRun={};g_assistantUsageRunCounted=false;g_assistantReceiptId=newUsageReceiptId();g_assistantReceiptModel=g_assistantConfig.model;g_currentUsageReceipt.reset();g_pendingUsageReceipt.reset();g_assistantCancel=std::make_shared<std::atomic_bool>(false);auto cancel=g_assistantCancel;auto config=g_assistantConfig;if(!transport)config.onboardingPack=agentOnboardingPack({"overview"});auto schema=AgentSession::tools();if(g_chatInput&&!queuedMessage)SetWindowTextW(g_chatInput,L"");
    auto epoch=g_documentEpoch;auto revision=std::make_shared<std::atomic<u64>>(g_session.revision);
    auto steering=std::make_shared<AssistantSteering>(config,schema,input);g_assistantSteering=steering;g_assistantSteeringEpoch=epoch;g_assistantSteeringTransport=transport;
    g_assistantWorker=std::thread([config,input,schema,cancel,transport,epoch,revision,steering]{
        V retainedHistory=input;auto executor=[cancel,epoch,revision,steering](const std::string& name,const V& args){
            auto promise=std::make_shared<std::promise<V>>();auto future=promise->get_future();auto request=std::make_unique<UiToolRequest>();request->tool=name;request->args=args;request->cancel=cancel;request->epoch=epoch;request->revision=revision;request->steering=steering;request->steeringVersion=steering->requestVersion();request->complete=[promise](V result){promise->set_value(std::move(result));};postTool(std::move(request));
            while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready)if(*cancel||g_closing)throw std::runtime_error("Assistant cancelled");
            return future.get();
        };AssistantRetryHooks hooks;if(!transport)hooks.completionCheck=[executor]{return executor("__ark_bom_completion",V::makeObj());};hooks.requestCurrent=[steering]{return steering->current(steering->requestVersion());};hooks.acceptCompletion=[steering]{return steering->finish();};
        try{auto result=runAssistant(config,input,schema,executor,[epoch](const AssistantEvent& event){if(event.kind==AssistantEvent::Status){UiChatEvent progress;progress.text=event.text;progress.epoch=epoch;progress.status=true;progress.retry=event.providerRetry;postChat(std::move(progress));}else if(event.kind==AssistantEvent::ToolStart)postChat({"Tool: "+event.text,false,false,{},epoch});else if(event.kind==AssistantEvent::Usage)postChat({"",false,false,{},epoch,event.usage,true,event.performance,true});else if(event.kind==AssistantEvent::ToolEnd)postChat({"",false,false,{},epoch,{},false,event.performance,true});},cancel.get(),feedbackFixtureTransport(transport),[steering]{return steering->inbox();},[&](const V& history){retainedHistory=history;steering->checkpoint(history);},hooks);
            if(*cancel)throw std::runtime_error("Assistant cancelled");postChat({result.text,true,false,result.conversation,epoch,result.usage,true,result.performance,true});
        }catch(const std::exception& e){steering->cancel();std::set<std::string> answered;for(const auto& row:retainedHistory.arr)if(row.find("type")&&row.find("type")->asStr()=="function_call_output"&&row.find("call_id"))answered.insert(row.find("call_id")->asStr());std::vector<std::string> missing;for(const auto& row:retainedHistory.arr)if(row.find("type")&&row.find("type")->asStr()=="function_call"&&row.find("call_id")&&!answered.count(row.find("call_id")->asStr()))missing.push_back(row.find("call_id")->asStr());for(const auto& id:missing){V reply=V::makeObj(),error=V::makeObj();error.set("isError",V::makeBool(true));error.set("error",V::makeStr("Interrupted before receipt retention; execution may have completed. Inspect current doc_info and revision before further writes; do not replay prior calls."));reply.set("type",V::makeStr("function_call_output"));reply.set("call_id",V::makeStr(id));reply.set("output",V::makeStr(error.dump()));retainedHistory.arr.push_back(reply);}for(const auto& pending:steering->pending().arr)retainedHistory.arr.push_back(pending);postChat({e.what(),true,true,retainedHistory,epoch});}
    });
    if(explicitPrompt.empty()&&!queuedMessage)clearPendingAssistantImage();
}
static void startLiveBridge(){
    if(!g_liveBridge.start([](const std::string& tool,const arkjson::Value& args,LiveSessionBridge::Completion complete,std::stop_token stop){
        auto request=std::make_unique<UiToolRequest>();request->tool=tool;request->args=args;request->complete=std::move(complete);request->stop=stop;postTool(std::move(request));
    }))g_status="Live MCP unavailable: "+g_liveBridge.lastError();
}
static void stopIntegrations(){
    g_closing=true;*g_pricingCancel=true;*g_teamCancel=true;g_agentTeam.cancel();*g_assistantCancel=true;*g_authCancel=true;g_liveBridge.stop();
    MSG msg{};while(PeekMessage(&msg,g_window,WM_ARK_TOOL,WM_ARK_AUTH,PM_REMOVE)){if(msg.message==WM_ARK_TOOL){std::unique_ptr<UiToolRequest> request((UiToolRequest*)msg.lParam);request->complete(cancelledTool());}else if(msg.message==WM_ARK_CHAT)delete (UiChatEvent*)msg.lParam;else if(msg.message==WM_ARK_AUTH)delete (UiAuthEvent*)msg.lParam;}
    while(!g_toolQueue.empty()){g_toolQueue.front()->complete(cancelledTool());g_toolQueue.pop_front();}
    if(g_assistantWorker.joinable())g_assistantWorker.join();
    if(g_authWorker.joinable())g_authWorker.join();
    if(g_pricingWorker.joinable())g_pricingWorker.join();
    g_agentTeam.waitIdle();
    while(PeekMessage(&msg,g_window,WM_ARK_TOOL,WM_ARK_AUTH,PM_REMOVE)){if(msg.message==WM_ARK_TOOL){std::unique_ptr<UiToolRequest> request((UiToolRequest*)msg.lParam);request->complete(cancelledTool());}else if(msg.message==WM_ARK_CHAT)delete (UiChatEvent*)msg.lParam;else if(msg.message==WM_ARK_AUTH)delete (UiAuthEvent*)msg.lParam;}
    while(PeekMessage(&msg,g_window,WM_ARK_PRICE,WM_ARK_TEAM,PM_REMOVE)){if(msg.message==WM_ARK_PRICE)delete (PricingCatalog*)msg.lParam;else if(msg.message==WM_ARK_TEAM)delete (UiTeamEvent*)msg.lParam;}
}
static std::string externalConfigDisplay(const std::string& raw,const std::string& client){
    std::string formatted=raw;
    if(client!="codex"&&client!="grok-build"){
        using V=arkjson::Value;V config;if(!arkjson::parse(raw,config))throw std::runtime_error("Invalid exported MCP configuration");formatted.clear();
        std::function<void(const V&,int)> write=[&](const V& value,int depth){
            const bool object=value.t==V::Obj,array=value.t==V::Arr;const size_t count=object?value.obj.size():array?value.arr.size():0;
            if(!object&&!array){formatted+=value.dump();return;}formatted+=object?'{':'[';
            for(size_t i=0;i<count;++i){formatted+='\n';formatted+=std::string((depth+1)*2,' ');if(object){formatted+=V::makeStr(value.obj[i].first).dump()+": ";write(value.obj[i].second,depth+1);}else write(value.arr[i],depth+1);if(i+1<count)formatted+=',';}
            if(count){formatted+='\n';formatted+=std::string(depth*2,' ');}formatted+=object?'}':']';
        };write(config,0);
        V roundtrip;if(!arkjson::parse(formatted,roundtrip)||roundtrip.dump()!=config.dump())throw std::runtime_error("MCP display formatting changed the configuration");
    }
    std::string display;display.reserve(formatted.size()+32);for(size_t i=0;i<formatted.size();++i){if(formatted[i]=='\n'&&(i==0||formatted[i-1]!='\r'))display+='\r';display+=formatted[i];}return display;
}
static void liveConnectionDialog(){
    if(g_liveBridge.endpoint().empty()){std::string text="Live MCP is not running.";editText("Live document connection",text,true);return;}
    auto profileShot=g_dialogShot;
    std::vector<EditorField> fields{{"External client","codex",false,{"codex","grok-build","claude-code","claude-desktop","opencode"}}};if(!editFields("Connect an external agent","Export the connection to this open document. The external client keeps its own model account and MCP settings.",fields))return;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);auto file=std::filesystem::path(executable).parent_path()/"ark-mcp.exe";auto utf8=file.u8string();std::string text=assistantExternalMcpConfig(fields[0].value,std::string(utf8.begin(),utf8.end()),g_liveBridge.endpoint(),g_assistantConfig);
    // Native multiline EDIT controls require CRLF. Keep TOML line boundaries
    // intact in both the visible text and what the user copies into the client.
    std::string displayText=externalConfigDisplay(text,fields[0].value);
    const char* hint=fields[0].value=="grok-build"?"Merge into %USERPROFILE%\\.grok\\config.toml (or GROK_HOME/config.toml). Reconnect Grok Build / its VS Code GUI; its CLI keeps the login. Call agent_onboard first.":"Copy the raw configuration with Ctrl+A / Ctrl+C into this client's MCP settings. The endpoint binds this exact open document. Call agent_onboard first.";
    g_status="External MCP configuration for "+fields[0].value+" / current document / copy with Ctrl+A and Ctrl+C / call agent_onboard first";auto title="Copy "+fields[0].value+" configuration / current document MCP";std::vector<EditorField> output{{"Configuration",displayText}};if(!profileShot.empty()&&g_dialogSmoke)g_dialogShot=profileShot+".configuration.png";editFields(title.c_str(),hint,output,true,"Close",nullptr,true);
    if(g_dialogSmoke&&output.front().value!=displayText)throw std::runtime_error("External MCP configuration changed in the copy dialog");
}
static void newDocument(){
    if(!confirmDiscard())return;*g_assistantCancel=true;auto args=arkjson::Value::makeObj();args.set("expected_revision",arkjson::Value::makeNum((double)g_session.revision));runTool("doc_new",args);if(g_error)return;
    g_savedRevision=g_session.revision;g_path="Untitled.ark";g_activeSketch.clear();g_activePlane="XY";g_conversation=arkjson::Value::makeArr();g_foldProgress=0;selBody=-1;rebuildScene();tTarget={0,0,0};tDist=180;g_status="New document / create a plane or a sketch";
}

static void interactiveAction(int id);
static void uiAction(int id);
static void addMeshFeature(FeatureType type,const std::string& name,arkjson::Value p,std::vector<u64> inputs){
    using V=arkjson::Value;V command=V::makeObj(),parents=V::makeArr();
    command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr(featureTypeName(type)));command.set("name",V::makeStr(name));command.set("p",std::move(p));
    for(u64 id:inputs)parents.arr.push_back(V::makeNum((double)id));command.set("inputs",std::move(parents));
    applyCommand(std::move(command));if(!g_error){selBody=(int)g_bodyIds.size()-1;frameScene();}
}

static std::string spatialField(const arkjson::Value& value,const char* key,const char* fallback){auto* field=value.find(key);return field?(field->t==arkjson::Value::Str?field->str:field->dump()):fallback;}
static arkjson::Value spatialPoints(const std::string& text,bool list){
    using V=arkjson::Value;V value;if(!arkjson::parse(text,value)||value.t!=V::Arr)throw std::runtime_error("Enter JSON [x,y,z] coordinates, with numbers or quoted expressions.");
    auto triple=[](const V& point){return point.t==V::Arr&&point.arr.size()==3&&std::all_of(point.arr.begin(),point.arr.end(),[](const V& item){return item.t==V::Num||item.t==V::Str;});};
    if(list){if(value.arr.size()<2||value.arr.size()>64||!std::all_of(value.arr.begin(),value.arr.end(),triple))throw std::runtime_error("A spatial curve needs 2..64 [x,y,z] points.");}else if(!triple(value))throw std::runtime_error("Enter one [x,y,z] triple.");return value;
}
static void applyUiCommandAtRevision(arkjson::Value command,u64 revision,u64 epoch){if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("The drawing changed while editing. Reopen the current geometry before applying.");applyCommand(std::move(command));}
static void spatialArcRecipeDialog(bool tangent,const Feature* editing=nullptr){
    using V=arkjson::Value;const u64 revision=g_session.revision,epoch=g_documentEpoch;Feature snapshot;if(editing){snapshot=*editing;editing=&snapshot;}
    std::vector<EditorField> fields{{"Curve name",editing?editing->name:tangent?"Spatial tangent arc":"Spatial three point arc"}};
    const char* labels[]={"Start X / mm","Start Y / mm","Start Z / mm",tangent?"Tangent X":"Through X / mm",tangent?"Tangent Y":"Through Y / mm",tangent?"Tangent Z":"Through Z / mm","End X / mm","End Y / mm","End Z / mm"};
    const char* defaults[]={"0","0","0",tangent?"0":"30",tangent?"1":"30",tangent?"0":"15","60","0","30"};
    for(int i=0;i<9;++i)fields.push_back({labels[i],defaults[i]});
    if(editing){auto* segments=editing->nativeSpec.find("segments");if(!segments||segments->arr.size()!=1)throw std::runtime_error("Use the curve program editor for multiple exact segments.");auto& segment=segments->arr[0];const V* points[]={editing->nativeSpec.find("start"),segment.find(tangent?"tangent":"through"),segment.find("to")};for(int i=0;i<3;++i)if(points[i]&&points[i]->t==V::Arr&&points[i]->arr.size()==3)for(int j=0;j<3;++j)fields[1+i*3+j].value=sketchFieldText(points[i]->arr[j]);}
    if(!editFields(tangent?"Exact spatial tangent arc":"Exact spatial three point arc","XYZ coordinates accept named expressions. This exact editable 3D curve can drive a sweep path or loft guide.",fields))return;
    auto triple=[&](int at){V value=V::makeArr();for(int i=0;i<3;++i)value.arr.push_back(V::makeStr(fields[at+i].value));return value;};V spec=V::makeObj(),segment=V::makeObj(),segments=V::makeArr();spec.set("start",triple(1));segment.set("kind",V::makeStr("arc"));segment.set("mode",V::makeStr(tangent?"tangent":"three_point"));segment.set(tangent?"tangent":"through",triple(4));segment.set("to",triple(7));segments.arr.push_back(segment);spec.set("segments",segments);V command=V::makeObj();command.set("op",V::makeStr(editing?"feature.edit":"feature.add"));if(editing)command.set("id",V::makeNum((double)editing->id));else command.set("type",V::makeStr("curve"));command.set("name",V::makeStr(fields[0].value));command.set("native",spec);applyUiCommandAtRevision(command,revision,epoch);
    if(!g_error){g_activeSketch.clear();g_showSketches=false;g_showEdges=true;g_toolModule=2;auto found=std::find(g_bodyIds.begin(),g_bodyIds.end(),editing?editing->id:g_doc.features.back().id);if(found!=g_bodyIds.end())selBody=(int)(found-g_bodyIds.begin());frameScene();g_status="Exact spatial arc saved / authored XYZ expressions retained";}
}
#include "authoring_extensions.inl"
static Feature* selectedPartFeature();
#include "sketch_entities.inl"
#include "spatial_constraints.inl"
static void spatialCurveDialog(const Feature* editing=nullptr){
    if(editing){
        const auto& source=editing->nativeSpec;const auto* analytic=source.find("analytic"),*segments=source.find("segments");
        if(nativeText(editing,"op")=="surface_spline"){surfaceSplineEntityDialog(editing);return;}
        if(analytic&&analytic->find("kind")&&analytic->find("kind")->asStr()=="polynomial"){exactPolynomialEquationDialog(editing);return;}
        if(segments&&segments->arr.size()==1&&segments->arr[0].find("kind")&&segments->arr[0].find("kind")->asStr()=="spline"){spatialSplineEntityDialog(editing);return;}
        bool preserve=analytic||source.find("constraints")||source.find("start_ref");if(segments)for(const auto& segment:segments->arr)preserve|=segment.find("to_ref")||segment.find("through_ref")||segment.find("length");
        if(preserve){const Feature snapshot=*editing;const u64 revision=g_session.revision,epoch=g_documentEpoch;std::string program=source.dump();if(!editText("Edit spatial source / retain constraints and references",program,true))return;arkjson::Value value;if(!arkjson::parse(program,value)||value.t!=arkjson::Value::Obj)throw std::runtime_error("Invalid spatial source program");applyTypedNativeRecipeEdit(snapshot,value,snapshot.name,revision,epoch);return;}
    }

    if(editing&&nativeText(editing,"op")=="equation"){equationCurveDialog(editing);return;}
    if(editing&&nativeText(editing,"op")=="project_surface"){surfaceProjectionDialog(editing);return;}
    const u64 revision=g_session.revision,epoch=g_documentEpoch;Feature snapshot;if(editing){snapshot=*editing;editing=&snapshot;}
    using V=arkjson::Value;if(editing&&spatialField(editing->nativeSpec,"op","")=="project")throw std::runtime_error("Included 3D geometry is driven by its source. Edit that source to update this curve.");
    const V empty=V::makeObj();const V& native=editing?editing->nativeSpec:empty;auto* segments=native.find("segments");const V* first=segments&&segments->t==V::Arr&&!segments->arr.empty()?&segments->arr.front():nullptr;
    std::string kind=first?spatialField(*first,"kind","nurbs"):"nurbs";if(editing&&segments&&segments->arr.size()==1&&kind=="arc"){auto mode=spatialField(*first,"mode","");if(mode=="three_point"||mode=="tangent"){spatialArcRecipeDialog(mode=="tangent",editing);return;}}if(editing&&segments&&segments->arr.size()>1&&kind!="line"){std::string program=native.dump();if(!editText("Edit exact spatial curve / start and segments",program,true))return;V value;if(!arkjson::parse(program,value)||value.t!=V::Obj)throw std::runtime_error("Invalid spatial curve program");V command=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)editing->id));command.set("native",value);applyUiCommandAtRevision(command,revision,epoch);return;}
    std::vector<EditorField> choose{{"Curve name",editing?editing->name:"Spatial curve"},{"Kind: line / arc / nurbs",kind}};
    if(!editFields(editing?"Edit spatial curve":"Create spatial curve","Exact 3D wire with XYZ coordinates; usable as a sweep path or loft guide. Add spatial constraints from All tools after creating the curve.",choose))return;
    kind=trimField(choose[1].value);if(kind!="line"&&kind!="arc"&&kind!="nurbs")throw std::runtime_error("Curve kind must be line, arc or nurbs");V spec=V::makeObj(),output=V::makeArr();
    if(kind=="arc"){
        std::vector<EditorField> fields{{"Start [x,y,z] / mm",spatialField(native,"start","[0,0,0]")},{"End [x,y,z] / mm",first?spatialField(*first,"to","[50,0,30]"):"[50,0,30]"},{"Radius / mm",first?spatialField(*first,"radius","35"):"35"},{"Plane normal [x,y,z]",first?spatialField(*first,"normal","[0,1,0]"):"[0,1,0]"},{"Sweep: 1 / -1",first?spatialField(*first,"sweep","1"):"1"}};
        if(!editFields("Spatial circular arc","The normal defines the arc plane and must be perpendicular to its chord. XYZ and radius accept expressions.",fields))return;spec.set("start",spatialPoints(fields[0].value,false));V segment=V::makeObj();segment.set("kind",V::makeStr("arc"));segment.set("to",spatialPoints(fields[1].value,false));segment.set("radius",V::makeStr(fields[2].value));segment.set("normal",spatialPoints(fields[3].value,false));segment.set("sweep",V::makeStr(fields[4].value));output.arr.push_back(segment);
    }else{
        std::string points="[[0,0,0],[25,25,15],[65,-20,35],[90,0,50]]";
        if(first&&kind=="nurbs")points=spatialField(*first,"poles",points.c_str());else if(segments&&kind=="line"){V values=V::makeArr();if(auto* start=native.find("start"))values.arr.push_back(*start);for(const auto& segment:segments->arr)if(auto* to=segment.find("to"))values.arr.push_back(*to);if(values.arr.size()>1)points=values.dump();}
        std::vector<EditorField> fields{{kind=="line"?"XYZ vertices / JSON":"XYZ control points / JSON",points}};
        if(kind=="nurbs")fields.insert(fields.end(),{{"Degree / 1..8",first?spatialField(*first,"degree","3"):"3"},{"Weights / JSON, blank = 1",first?spatialField(*first,"weights",""):""}});
        if(!editFields(kind=="line"?"Spatial line chain":"Spatial NURBS curve","Coordinates retain Z. Quoted expressions bind to named parameters; NURBS points are control poles, not interpolation anchors.",fields))return;
        V values=spatialPoints(fields[0].value,true);spec.set("start",values.arr.front());if(kind=="line")for(size_t i=1;i<values.arr.size();++i){V segment=V::makeObj();segment.set("kind",V::makeStr("line"));segment.set("to",values.arr[i]);output.arr.push_back(segment);}else{V segment=V::makeObj();segment.set("kind",V::makeStr("nurbs"));segment.set("poles",values);segment.set("degree",V::makeStr(fields[1].value));if(!trimField(fields[2].value).empty()){V weights;if(!arkjson::parse(fields[2].value,weights)||weights.t!=V::Arr)throw std::runtime_error("NURBS weights must be a JSON array");segment.set("weights",weights);}output.arr.push_back(segment);}
    }
    spec.set("segments",output);V command=V::makeObj();command.set("op",V::makeStr(editing?"feature.edit":"feature.add"));if(editing)command.set("id",V::makeNum((double)editing->id));else command.set("type",V::makeStr("curve"));command.set("name",V::makeStr(trimField(choose[0].value)));command.set("native",spec);applyUiCommandAtRevision(command,revision,epoch);if(!g_error){g_activeSketch.clear();g_showSketches=false;g_showEdges=true;g_toolModule=2;u64 id=editing?editing->id:g_doc.features.back().id;auto found=std::find(g_bodyIds.begin(),g_bodyIds.end(),id);if(found!=g_bodyIds.end())selBody=(int)(found-g_bodyIds.begin());g_status="Spatial curve saved / XYZ retained / edit its coordinates or named parameters";frameScene();}
}
static void applyProjectionPreview(){
    using V=arkjson::Value;if(!g_projectionPreview.active)return;if(g_projectionPreview.revision!=g_session.revision||g_projectionPreview.epoch!=g_documentEpoch){clearProjectionPreview();throw std::runtime_error("The drawing changed after preview. Project the source again before applying.");}
    auto preview=g_projectionPreview;V args=V::makeObj();args.set("expected_revision",V::makeNum((double)preview.revision));args.set("commands",preview.commands);runTool("doc_apply",args);if(g_error)return;clearProjectionPreview();g_showEdges=true;
    if(!preview.spatial){g_activeSketch=preview.target;g_activePlane=preview.plane;g_showSketches=true;rebuildSketchOverlay();}else{g_activeSketch.clear();g_showSketches=false;auto found=std::find_if(g_bodyIds.begin(),g_bodyIds.end(),[&](u64 id){auto* feature=g_doc.find(id);return feature&&feature->name==preview.target&&isCurveFeature(feature->type);});if(found!=g_bodyIds.end())selBody=(int)(found-g_bodyIds.begin());}
    g_status=preview.spatial?"Included exact 3D curve / source XYZ retained / edit source to update":"Exact normal projection added to sketch / edit source to update";frameScene();
}
static void projectGeometryDialog(bool spatial){
    using V=arkjson::Value;if(g_busy)throw std::runtime_error("Wait for the current operation before projecting geometry.");const u64 baselineRevision=g_session.revision,baselineEpoch=g_documentEpoch,baselineNextFeature=g_doc.nextFeatureId_;if(g_allSheetsFlat||g_stockMode||g_motionActive)throw std::runtime_error("Return to the authored static / Formed view before projecting exact geometry.");
    std::vector<SelectionItem> sources;for(const auto& pick:g_picked)if(((pick.kind=="edge"||pick.kind=="face")&&pick.feature)||(pick.kind=="curve"&&!pick.owner.empty()))sources.push_back(pick);
    if(sources.empty()){g_pickKind=2;g_showEdges=true;g_status="Select one exact edge, planar face or named sketch curve, then choose Project 2D or Include 3D.";return;}if(sources.size()!=1)throw std::runtime_error("Select exactly one edge, planar face or sketch entity as the projection source.");auto source=sources.front();const bool sketchSource=source.kind=="curve";auto* feature=sketchSource?nullptr:g_doc.find(source.feature);std::string sourceDescription;
    if(sketchSource){auto* sketch=g_doc.findSketch(source.owner);if(!sketch)throw std::runtime_error("Source sketch no longer exists");if(source.name==source.owner){if(sketch->sk.ents.size()!=1)throw std::runtime_error("Select an individual source curve in the viewport; a whole multi-entity sketch is ambiguous.");source.name=sketch->sk.ents.front().name;}if(source.name.empty()||std::none_of(sketch->sk.ents.begin(),sketch->sk.ents.end(),[&](const SketchEnt& entity){return entity.name==source.name;}))throw std::runtime_error("This legacy entity has no stable name. Use Sketch > Rename, choose its entity number and give it a name, then select it again.");sourceDescription=source.owner+" / "+source.name;}
    else{if(!feature)throw std::runtime_error("Projection source no longer exists");sourceDescription=feature->name+" / "+source.name;}
    std::string target=spatial?"Included 3D curve":(sketchSource||g_activeSketch.empty())?"Projected sketch":g_activeSketch,plane=g_activePlane,entity="Projected edge";
    if(!spatial)if(auto* sketch=g_doc.findSketch(target)){plane=sketch->sk.plane;unsigned suffix=1;while(std::any_of(sketch->sk.ents.begin(),sketch->sk.ents.end(),[&](const SketchEnt& item){return item.name==entity;}))entity="Projected edge "+std::to_string(++suffix);}
    std::vector<EditorField> fields{{spatial?"3D curve name":"Target sketch",target}};if(!spatial)fields.insert(fields.end(),{{"Target plane",plane},{"Entity name",entity}});
    std::string hint="Source: "+sourceDescription;if(source.instance)hint+=" / instance "+std::to_string(source.instance)+" / copy "+std::to_string(source.copy);hint+=spatial?". Include exact linked XYZ geometry; this is a spatial curve, not a general 3D sketch solver.":". Project normally to the target plane. The linked entity follows source edits; its coordinates are driven.";
    if(!editFields(spatial?"Include geometry in 3D":"Project geometry to sketch",hint.c_str(),fields,false,"Preview"))return;
    if(baselineRevision!=g_session.revision||baselineEpoch!=g_documentEpoch)throw std::runtime_error("The drawing changed while choosing the projection. Select the current source and preview again.");
    target=trimField(fields[0].value);if(target.empty())throw std::runtime_error("Choose a destination name");if(!spatial){if(sketchSource&&target==source.owner)throw std::runtime_error("A sketch cannot project its own entity into itself. Choose a new target sketch.");plane=trimField(fields[1].value);entity=trimField(fields[2].value);resolvePlane(g_doc,plane);if(entity.empty())throw std::runtime_error("Choose a projected entity name");if(auto* sketch=g_doc.findSketch(target)){if(sketch->sk.plane!=plane)throw std::runtime_error("Existing target sketch uses "+sketch->sk.plane+"; keep its plane or choose a new sketch.");for(const auto& item:sketch->sk.ents)if(item.name==entity&&item.kind!=SketchEnt::Project)throw std::runtime_error("That name belongs to authored sketch geometry; choose a new projected entity name.");}}
    ProjectionPreview preview;preview.spatial=spatial;preview.target=target;preview.plane=spatial?"":plane;preview.source=sourceDescription;preview.revision=baselineRevision;preview.epoch=baselineEpoch;
    V binding=V::makeObj();if(sketchSource){binding.set("source_sketch",V::makeStr(source.owner));binding.set("source_entity",V::makeStr(source.name));}else binding.set("reference",V::makeStr(source.name));if(source.instance){binding.set("source_instance",V::makeNum((double)source.instance));binding.set("source_copy",V::makeNum(source.copy));}
    if(spatial){V command=V::makeObj(),inputs=V::makeArr();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("curve"));command.set("name",V::makeStr(target));if(!sketchSource)inputs.arr.push_back(V::makeNum((double)source.feature));command.set("inputs",inputs);binding.set("op",V::makeStr("project"));binding.set("mode",V::makeStr("3d"));command.set("native",binding);preview.commands.arr.push_back(command);if(!g_doc.assembly.empty()){V instance=V::makeObj();instance.set("op",V::makeStr("assembly.add"));instance.set("name",V::makeStr(target));instance.set("feature",V::makeNum((double)baselineNextFeature));preview.commands.arr.push_back(instance);}}
    else{if(!g_doc.findSketch(target)){V command=V::makeObj(),sketch=V::makeObj();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(target));sketch.set("plane",V::makeStr(plane));sketch.set("entities",V::makeArr());sketch.set("constraints",V::makeArr());command.set("sketch",sketch);preview.commands.arr.push_back(command);}binding.set("kind",V::makeStr("project"));binding.set("mode",V::makeStr("normal"));binding.set("name",V::makeStr(entity));if(!sketchSource)binding.set("feature",V::makeNum((double)source.feature));V command=V::makeObj();command.set("op",V::makeStr("sketch.entity.set"));command.set("name",V::makeStr(target));command.set("entity",binding);preview.commands.arr.push_back(command);}
    Document document=g_doc;document.evaluatingBodies=nullptr;auto operation=[document=std::move(document),source,plane=preview.plane,sketchSource]() mutable{return sketchSource?projectSketchGeometry(document,source.owner,source.name,plane,"normal"):projectGeometry(document,source.feature,source.name,plane,source.instance,source.copy,"normal");};
    clearProjectionPreview();V geometry=g_headless&&!g_busyTestMode?operation():callSessionWithProgress("ui_projection_preview",V::makeObj(),std::move(operation));
    auto* report=geometry.find("report");auto* exact=report?report->find("projection_exact"):nullptr;if(!exact||exact->t!=V::Bool||!exact->b)throw std::runtime_error("The kernel did not attest an exact projection.");auto* mesh=geometry.find("mesh");auto* edges=mesh?mesh->find("edges"):nullptr;if(!edges||edges->t!=V::Arr)throw std::runtime_error("Exact projection has no display edges");
    for(const auto& edge:edges->arr){if(edge.t!=V::Arr||edge.arr.size()%3)throw std::runtime_error("Invalid projection display edge");for(size_t i=3;i+2<edge.arr.size();i+=3)for(size_t start:{i-3,i}){Vec3 point;double* coordinates[]={&point.x,&point.y,&point.z};for(size_t coordinate=0;coordinate<3;++coordinate){const auto& value=edge.arr[start+coordinate];if(value.t!=V::Num||!std::isfinite(value.num))throw std::runtime_error("Invalid projection display point");*coordinates[coordinate]=value.num;}preview.lines.insert(preview.lines.end(),{(float)point.x,(float)point.y,(float)point.z});preview.bounds.grow(point);if(preview.lines.size()>1500000)throw std::runtime_error("Projection display exceeds the preview resource limit");}}
    if(preview.lines.empty()||!preview.bounds.valid())throw std::runtime_error("Projection has no drawable curve");if(preview.revision!=g_session.revision||preview.epoch!=g_documentEpoch)throw std::runtime_error("The drawing changed during projection. Select the current source again.");preview.active=true;g_projectionPreview=std::move(preview);g_projectionGpuDirty=true;if(g_showSketches&&g_sketchBounds.valid())g_sceneBounds.grow(g_sketchBounds);g_sceneBounds.grow(g_projectionPreview.bounds);frameScene();g_status=spatial?"Preview: linked 3D curve / XYZ preserved / Apply or Cancel":"Preview: exact normal projection / linked source / Apply or Cancel";
}

static Feature* selectedPartFeature();
static Feature* selectedFrameDefinition(){
    auto* feature=selectedPartFeature();if(!feature||feature->type!=FeatureType::ConstructionFrame)throw std::runtime_error("Select a Construction Frame feature or its assembly instance. Create it with Model > Frame; individual solids and tubes have no frame-member program.");return feature;
}
static std::string frameDefinitionHint(const Feature& feature){return "Frame definition: "+feature.name+". Changes update every instance. Member, plane and seam coordinates are local to this frame. ";}
static Feature* selectedEditableBody(){
    if(selBody<0||(size_t)selBody>=g_bodyIds.size()||g_instanceIds[selBody])throw std::runtime_error("Select a part body, not an assembly instance");
    auto* source=g_doc.find(g_bodyIds[selBody]);if(!source)throw std::runtime_error("Select a part body");return source;
}
static void requireMeshSample(const Feature& source,arkjson::Value& p,const std::string& sample){
    if(!isNativeFeature(source.type))return;
    if(sample!="1")throw std::runtime_error("Sampling an exact B-rep requires Sample = 1. The result is mesh authority");
    p.set("sample",arkjson::Value::makeStr("1"));
}
static std::string nativeText(const Feature* feature,const char* key,const char* fallback){
    if(!feature)return fallback?fallback:"";auto* value=feature->nativeSpec.find(key);return value&&value->t==arkjson::Value::Str?value->str:std::string(fallback?fallback:"");
}
static std::string defaultSketchName(){return g_activeSketch.empty()?(g_doc.sketches.empty()?std::string():g_doc.sketches.front().name):g_activeSketch;}
static void sweepDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;
    std::string picked=pickedSketches();std::string firstSketch,secondSketch;if(!picked.empty()){auto mark=picked.find(';');firstSketch=trimField(mark==std::string::npos?picked:picked.substr(0,mark));if(mark!=std::string::npos)secondSketch=trimField(picked.substr(mark+1));}
    std::string section=editing?nativeText(editing,"section"):(!firstSketch.empty()?firstSketch:(g_doc.sketches.empty()?std::string():g_doc.sketches.front().name));
    std::string path=editing?nativeText(editing,"path"):(!secondSketch.empty()?secondSketch:(g_doc.sketches.size()>1?g_doc.sketches[1].name:section));
    std::vector<EditorField> fields{{"Section sketch",section},{"Path sketch",path},{"Solid",editing?editing->get("solid","0"):"0",true}};
    if(!editFields(editing?"Edit sweep":"Sweep section along path",(sketchNames()+"  /  Corrected Frenet. Contact stays off so a centered section is not shifted onto the path.").c_str(),fields))return;
    auto sectionName=trimField(fields[0].value),pathName=trimField(fields[1].value);
    if(!g_doc.findSketch(sectionName)||!g_doc.findSketch(pathName))throw std::runtime_error("Sweep needs two existing sketches");
    V native=V::makeObj(),p=V::makeObj();native.set("section",V::makeStr(sectionName));native.set("path",V::makeStr(pathName));p.set("solid",V::makeStr(fields[2].value));
    commitNativeFeature(FeatureType::Sweep,"Sweep",std::move(p),std::move(native),{},editing?editing->id:0,true);
}
static void extrudeSurfaceDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;
    std::vector<EditorField> fields{{"Sketch",editing?editing->get("sketch"):defaultSketchName()},{"Distance / mm",editing?editing->get("distance","10"):"10"},{"Solid",editing?editing->get("solid","0"):"0",true}};
    if(!editFields(editing?"Edit extruded surface":"Extrude sketch as a surface","Distance follows the sketch plane normal. A solid needs a closed sketch.",fields))return;
    auto name=trimField(fields[0].value);if(!g_doc.findSketch(name))throw std::runtime_error("Choose an existing sketch");
    V p=V::makeObj();p.set("sketch",V::makeStr(name));p.set("distance",V::makeStr(trimField(fields[1].value)));p.set("solid",V::makeStr(fields[2].value));
    commitNativeFeature(FeatureType::ExtrudeSurface,"Extruded surface",std::move(p),V::makeObj(),{},editing?editing->id:0,false);
}
static void revolveSurfaceDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;
    std::vector<EditorField> fields{{"Sketch",editing?editing->get("sketch"):defaultSketchName()},{"Angle / degrees",editing?editing->get("angle","360"):"360"},{"Axis X Y or Z",editing?nativeText(editing,"axis"):"Y"},{"Solid",editing?editing->get("solid","0"):"0",true}};
    if(fields[2].value.empty())fields[2].value="Y";
    if(!editFields(editing?"Edit revolved surface":"Revolve sketch","The axis passes through the sketch-plane origin. Angle is in (0, 360] degrees.",fields))return;
    auto name=trimField(fields[0].value),axis=trimField(fields[2].value);
    if(!g_doc.findSketch(name))throw std::runtime_error("Choose an existing sketch");
    if(axis!="X"&&axis!="Y"&&axis!="Z")throw std::runtime_error("Axis must be X, Y or Z");
    V native=V::makeObj(),p=V::makeObj();native.set("axis",V::makeStr(axis));
    p.set("sketch",V::makeStr(name));p.set("angle",V::makeStr(trimField(fields[1].value)));p.set("solid",V::makeStr(fields[3].value));
    commitNativeFeature(FeatureType::RevolveSurface,"Revolved surface",std::move(p),std::move(native),{},editing?editing->id:0,true);
}
static void offsetSurfaceDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;const Feature* source=nullptr;
    if(!editing){source=selectedEditableBody();if(!isNativeSurfaceFeature(source->type)||source->type==FeatureType::Thicken)throw std::runtime_error("Select an open native surface. Offset does not accept a solid");}
    std::vector<EditorField> fields{{"Distance / mm",editing?editing->get("distance","2"):"2"}};
    if(!editFields(editing?"Edit offset surface":"Offset native surface","Signed distance. Intersection is not checked, and this does not thicken into a solid.",fields))return;
    V p=V::makeObj();p.set("distance",V::makeStr(trimField(fields[0].value)));
    if(editing)commitNativeFeature(FeatureType::OffsetSurface,"Offset surface",std::move(p),V::makeObj(),{},editing->id,false);
    else addNativeFeature(FeatureType::OffsetSurface,"Offset surface",std::move(p),V::makeObj(),{source->id});
}
static void sewDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;
    const u64 revision=g_session.revision,epoch=g_documentEpoch;Feature snapshot;if(editing){snapshot=*editing;editing=&snapshot;}
    if(editing){
        std::vector<EditorField> fields{{"Solid",editing->get("solid","0"),true},{"Require one connected surface patch","1",true}};
        std::string hint="Inputs stay "+joinedFeatureIds(editing->inputs)+". A solid requires one closed shell. This edit does not change which surfaces are sewn.";
        if(!editFields("Edit sew",hint.c_str(),fields))return;
        if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while editing surface glue; select current surfaces again");
        V p=V::makeObj();p.set("solid",V::makeStr(fields[0].value));
        V native=editing->nativeSpec;native.set("require_connected",V::makeBool(fields[1].value=="1"));commitNativeFeature(FeatureType::Sew,"Glue surfaces",std::move(p),std::move(native),{},editing->id,true);return;
    }
    std::string initial=selectedFeatureId();
    if(g_bodyIds.size()>=2)initial=std::to_string(g_bodyIds[g_bodyIds.size()-2])+"; "+std::to_string(g_bodyIds.back());
    std::vector<EditorField> fields{{"Surface feature IDs",initial},{"Solid","0",true},{"Require one connected surface patch","1",true}};
    if(!editFields("Glue native surfaces","List 2 to 16 face or shell IDs, separated by semicolons. Connected patches are checked; a solid additionally requires one closed shell with no free edges.",fields))return;
    if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while choosing surface glue; select current surfaces again");
    std::vector<u64> ids;std::istringstream names(fields[0].value);std::string token;
    while(std::getline(names,token,';')){token=trimField(token);if(token.empty())continue;ids.push_back(featureIdField(token));}
    if(ids.size()<2||ids.size()>16)throw std::runtime_error("Sew needs 2 to 16 features");
    V p=V::makeObj();p.set("solid",V::makeStr(fields[1].value));
    V native=V::makeObj();native.set("require_connected",V::makeBool(fields[2].value=="1"));addNativeFeature(FeatureType::Sew,"Glue surfaces",std::move(p),std::move(native),ids);
}
static std::string frameFormText(const arkjson::Value* row,const char* key,const char* fallback){
    const auto* value=row?row->find(key):nullptr;return value?(value->t==arkjson::Value::Str?value->str:value->dump()):std::string(fallback);
}
static arkjson::Value frameFormTriple(const std::string& text){
    using V=arkjson::Value;V value;if(!arkjson::parse(text,value)||value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error("Enter a JSON triple such as [0,0,1] or [\"span\",0,0]");
    for(const auto& coordinate:value.arr){if(coordinate.t==V::Str)formNumber(coordinate.str);else if(coordinate.t!=V::Num||!std::isfinite(coordinate.num))throw std::runtime_error("Triple coordinates must be finite numbers or expressions");}return value;
}
static std::string frameFormMemberHint(const Feature& feature){
    std::string hint="Members: ";if(const auto* members=feature.nativeSpec.find("members"))for(const auto& member:members->arr)hint+=(hint.back()==' '?"":", ")+frameFormText(&member,"name","");return hint;
}
static void applyFrameCommand(arkjson::Value command,u64 revision,u64 epoch){
    using V=arkjson::Value;if(g_session.revision!=revision||g_documentEpoch!=epoch)throw std::runtime_error("The frame definition changed while editing. Reopen the current frame before applying.");V args=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)revision));runTool("doc_apply",args);
}
static HBITMAP candidateReviewBitmap(const std::string& path);
static bool g_tubeFitReviewCancelTest=false;
static std::string g_failureExportOverride;
static std::string readableJson(const arkjson::Value& value){std::string source=value.dump(),result;bool quoted=false,escaped=false;int depth=0;auto newline=[&]{result+='\n';result.append((size_t)std::max(depth,0)*2,' ');};for(char c:source){if(quoted){result+=c;if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}else if(c=='"'){quoted=true;result+=c;}else if(c=='{'||c=='['){result+=c;++depth;newline();}else if(c=='}'||c==']'){--depth;newline();result+=c;}else if(c==','){result+=c;newline();}else if(c==':'){result+=": ";}else result+=c;}return result;}
#include "bom_panel.inl"
#include "drawing_workspace.inl"
#include "drawing_viewport.inl"
#include "specialized_pattern_authoring.inl"
#include "advanced_sheetmetal_authoring.inl"
#include "cad_import_authoring.inl"
#include "holes_orchestrator_authoring.inl"
#include "design_container_workspace.inl"
static void failureOperationDialog(int mode=0){
    using V=arkjson::Value;V arguments=V::makeObj();if(g_lastFailureId.empty()){arguments.set("limit",V::makeNum(1));auto latest=runTool("failure_info",arguments);if(g_error)return;auto* rows=latest.find("structuredContent")->find("records");if(!rows||rows->arr.empty())throw std::runtime_error("No failed operation was retained in this local session");g_lastFailureId=rows->arr.front().find("id")->asStr();}
    arguments=V::makeObj();arguments.set("id",V::makeStr(g_lastFailureId));auto info=runTool("failure_info",arguments);if(g_error)return;auto record=*info.find("structuredContent")->find("record");
    if(mode==0){std::string cause=record.find("error")->find("error")?record.find("error")->find("error")->asStr():record.find("error")->dump();std::string hint="Local receipt "+g_lastFailureId+" / baseline r"+std::to_string((u64)record.find("baseline_revision")->num)+". Export is explicit; replay uses an isolated copy of its original baseline.";
        std::vector<EditorField> fields{{"Cause",cause},{"Next action","Inspect commands",false,{"Inspect commands","Export bundle","Replay isolated"}}};if(!editFields("Last failed operation",hint.c_str(),fields))return;auto action=fields[1].value;if(action=="Export bundle")mode=1;else if(action=="Replay isolated")mode=2;else{std::vector<EditorField> details{{"Exact immutable receipt",readableJson(record)}};editFields("Failed operation / commands and cause","Read-only local receipt. No original project data is exported by this inspector.",details,true,"Close",nullptr,true);return;}}
    if(mode==1){char path[MAX_PATH]="ark-failure.json";if(!g_failureExportOverride.empty())std::snprintf(path,sizeof path,"%s",g_failureExportOverride.c_str());else{OPENFILENAMEA save{};save.lStructSize=sizeof save;save.hwndOwner=g_window;save.lpstrFile=path;save.nMaxFile=MAX_PATH;save.lpstrFilter="ARK local failure bundle\0*.json\0";save.lpstrDefExt="json";save.lpstrTitle="Export the selected failure and its original local baseline";save.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(!GetSaveFileNameA(&save))return;}arguments.set("path",V::makeStr(path));runTool("failure_export",arguments);return;}
    arguments.set("expected_revision",V::makeNum((double)g_session.revision));auto replay=runTool("failure_replay",arguments);if(g_error)return;std::vector<EditorField> result{{"Replay evidence",readableJson(*replay.find("structuredContent"))}};editFields("Isolated failure replay","The recorded commands ran on their retained original baseline. The current drawing was not modified.",result,true,"Close",nullptr,true);
}
static void tubeEndFitDialog(){
    using V=arkjson::Value;const u64 revision=g_session.revision,epoch=g_documentEpoch;auto* selected=selectedPartFeature();
    std::string requestedDialogShot=g_dialogShot;
    const bool concreteSelected=selBody>=0&&(size_t)selBody<g_instanceIds.size()&&g_instanceIds[(size_t)selBody]!=0;const u64 selectedOccurrence=concreteSelected?g_instanceIds[(size_t)selBody]:0;const int selectedCopy=concreteSelected?g_instanceCopies[(size_t)selBody]:0;
    const bool matchingSelected=concreteSelected&&selected&&g_bodyIds[(size_t)selBody]==selected->id&&selectedCopy==0;
    std::string source=selected?std::to_string(selected->id):"";std::vector<EditorField> fields{{"Result name","Fitted tube end"},{"Source definition ID",source},{"Host definition ID",""},{"Source end","end",false,{"start","end"}},{"Fit mode","envelope",false,{"envelope","plane","plane_trim"}},{"Host face / plane mode",""},{"Maximum extension / mm","0"},{"Placement scope",matchingSelected?"Selected occurrence":"Definition only",false,{"Selected occurrence","All source occurrences","New occurrence","Definition only"}}};
    if(!editFields("Fit exact tube end","Definition-local fit. Envelope: cope. Plane: full finite seat. Plane_trim: supporting-plane cut; partial seating is reported.",fields))return;
    V command=V::makeObj(),inputs=V::makeArr(),native=V::makeObj(),parameters=V::makeObj();inputs.arr.push_back(V::makeNum((double)featureIdField(fields[1].value)));inputs.arr.push_back(V::makeNum((double)featureIdField(fields[2].value)));command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("native_solid"));command.set("name",V::makeStr(fields[0].value));command.set("inputs",inputs);auto* sourceFeature=g_doc.find((u64)inputs.arr.front().num);if(!sourceFeature->material.empty())command.set("material",V::makeStr(sourceFeature->material));if(sourceFeature->appearance.t==V::Obj)command.set("appearance",sourceFeature->appearance);native.set("op",V::makeStr("tube_end_fit"));native.set("end",V::makeStr(fields[3].value));native.set("mode",V::makeStr(fields[4].value));if(fields[4].value=="plane"||fields[4].value=="plane_trim")native.set("reference",V::makeStr(fields[5].value));parameters.set("max_extension",V::makeStr(fields[6].value));command.set("native",native);command.set("p",parameters);
    if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while authoring the tube end fit; reopen its current definitions");
    V previewArgs=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);const u64 candidateFeature=g_doc.nextFeatureId_,sourceId=(u64)inputs.arr.front().num;const std::string placement=fields[7].value;std::vector<u64> replaced;std::string placementEvidence;
    if(placement=="Selected occurrence"){
        if(!selectedOccurrence||selectedCopy!=0)throw std::runtime_error("Select an original source occurrence before choosing Selected occurrence; a patterned copy cannot be replaced independently");auto node=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& item){return item.id==selectedOccurrence;});if(node==g_doc.assembly.end()||node->feature!=sourceId)throw std::runtime_error("The selected occurrence does not use this source definition. Select the intended source or explicitly choose another placement scope");replaced.push_back(node->id);
    }else if(placement=="All source occurrences"){for(const auto& node:g_doc.assembly)if(node.feature==sourceId)replaced.push_back(node.id);if(replaced.empty())throw std::runtime_error("This source has no placed occurrences. Choose Definition only or explicitly New occurrence");}
    else if(placement=="New occurrence"){V instance=V::makeObj();instance.set("op",V::makeStr("assembly.add"));instance.set("feature",V::makeNum((double)candidateFeature));instance.set("name",V::makeStr(fields[0].value));commands.arr.push_back(instance);placementEvidence="New occurrence at assembly root/origin; existing source occurrences retained";}
    else if(placement=="Definition only")placementEvidence="Definition only; all existing occurrences retained";else throw std::runtime_error("Choose an explicit tube fit placement scope");
    if(!replaced.empty()){placementEvidence=placement+" / replace definition on node IDs ";for(u64 id:replaced){V edit=V::makeObj();edit.set("op",V::makeStr("assembly.edit"));edit.set("id",V::makeNum((double)id));edit.set("feature",V::makeNum((double)candidateFeature));commands.arr.push_back(edit);placementEvidence+=std::to_string(id)+" ";}placementEvidence+="/ names, parents, poses, fixed flags, patterns and mates retained";}
    previewArgs.set("expected_revision",V::makeNum((double)revision));previewArgs.set("commands",commands);previewArgs.set("detail",V::makeStr("changes"));auto preview=runTool("doc_preview",previewArgs);if(g_error)return;auto data=*preview.find("structuredContent");if(!data.find("valid")||!data.find("valid")->b){g_error=true;auto* cause=data.find("candidate_result");g_status=cause&&cause->find("error")?cause->find("error")->asStr():"Tube end fit preview rejected; inspect Last failed operation";return;}if(!data.find("candidate_id"))throw std::runtime_error("Tube end fit preview could not retain a candidate; no geometry was saved");
    wchar_t temporary[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,temporary))throw std::runtime_error("Could not create a local preview image path");auto folder=std::filesystem::path(temporary)/"ARK"/"previews";std::filesystem::create_directories(folder);auto imagePath=folder/(data.find("candidate_id")->asStr()+".png");auto bytes=imagePath.u8string();std::string imageFile(bytes.begin(),bytes.end());V capture=V::makeObj();capture.set("candidate_id",*data.find("candidate_id"));capture.set("expected_revision",V::makeNum((double)revision));if(replaced.empty())capture.set("id",V::makeNum((double)candidateFeature));else{capture.set("node",V::makeNum((double)replaced.front()));capture.set("copy",V::makeNum(0));}capture.set("view",V::makeStr("iso"));capture.set("path",V::makeStr(imageFile));runTool("snapshot",capture);if(g_error)return;HBITMAP bitmap=candidateReviewBitmap(imageFile);std::error_code cleanup;std::filesystem::remove(imagePath,cleanup);if(!bitmap)throw std::runtime_error("Could not display the candidate PNG; no tube fit was saved");
    std::string evidence=data.find("end_fit_evidence")?data.find("end_fit_evidence")->dump():data.find("after")->dump();std::vector<EditorField> review{{"Occurrence changes",placementEvidence},{"Exact candidate evidence",evidence}};if(!requestedDialogShot.empty())g_dialogShot=requestedDialogShot+".candidate.png";int originalSmoke=g_dialogSmoke;if(g_tubeFitReviewCancelTest)g_dialogSmoke=2;bool accepted=editFields("Tube end fit / uncommitted candidate","Actual candidate display mesh from evaluated native geometry. Apply publishes this retained candidate; Cancel preserves the drawing. Manufacturing remains unqualified.",review,false,"Apply",bitmap,true);g_dialogSmoke=originalSmoke;DeleteObject(bitmap);if(!accepted){g_status="Tube end fit preview cancelled / drawing unchanged";return;}if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed during tube fit review; preview the current sources again");runTool("doc_apply",*data.find("apply"));
    if(!g_error){g_toolModule=1;frameScene();g_status="Linked exact tube end fit saved / "+placementEvidence+" / inspect measured gap, overlap and coverage";}
}
static void frameEndpointDialog(bool plane){
    using V=arkjson::Value;Feature definition=*selectedFrameDefinition();auto* feature=&definition;const u64 revision=g_session.revision,epoch=g_documentEpoch;
    V joints=feature->nativeSpec.find("joints")?*feature->nativeSpec.find("joints"):V::makeArr();const auto* members=feature->nativeSpec.find("members");if(!members||members->arr.empty())throw std::runtime_error("Frame has no members");
    const char* kind=plane?"trim_plane":"trim_extend";const V* first=nullptr;std::string hint=frameDefinitionHint(*feature)+frameFormMemberHint(*feature)+". Existing endpoints: ";for(const auto& row:joints.arr){hint+=frameFormText(&row,"member","")+"."+frameFormText(&row,"end","")+" ("+frameFormText(&row,"kind","")+") ";if(!first&&frameFormText(&row,"kind","")==kind)first=&row;}
    std::vector<EditorField> pick{{"Action: add / edit / remove",first?"edit":"add"},{"Member name",frameFormText(first,"member",frameFormText(&members->arr.front(),"name","").c_str())},{"Endpoint: start / end",frameFormText(first,"end","end")}};
    if(!editFields(plane?"Frame crossing trim plane":"Frame trim or extend",hint.c_str(),pick))return;const auto action=trimField(pick[0].value),member=trimField(pick[1].value),end=trimField(pick[2].value);if(action!="add"&&action!="edit"&&action!="remove")throw std::runtime_error("Action must be add, edit or remove");if(end!="start"&&end!="end")throw std::runtime_error("Endpoint must be start or end");
    size_t index=joints.arr.size();for(size_t i=0;i<joints.arr.size();++i)if(frameFormText(&joints.arr[i],"member","")==member&&frameFormText(&joints.arr[i],"end","")==end)index=i;
    const V* existing=index<joints.arr.size()?&joints.arr[index]:nullptr;if(action=="add"&&existing)throw std::runtime_error("This endpoint already owns a joint; choose edit or remove");if(action!="add"&&!existing)throw std::runtime_error("No joint exists on this selected endpoint");
    V command=V::makeObj();command.set("id",V::makeNum((double)feature->id));
    if(action=="remove"){command.set("op",V::makeStr("frame.joint.remove"));command.set("member",V::makeStr(member));command.set("end",V::makeStr(end));applyFrameCommand(command,revision,epoch);return;}
    std::vector<EditorField> fields;if(plane)fields={{"Local plane origin [x,y,z] / mm",frameFormText(existing,"origin","[0,0,0]")},{"Local plane normal [x,y,z]",frameFormText(existing,"normal","[1,0,0]")}};else fields={{"Against member",frameFormText(existing,"against",members->arr.size()>1?frameFormText(&members->arr[1],"name","").c_str():"")},{"Maximum extension / mm",frameFormText(existing,"max_extension","0")}};
    if(!editFields(plane?"Crossing plane geometry":"Bounded endpoint meeting",(frameDefinitionHint(*feature)+(plane?"The explicit plane must cross the straight member axis between its selected and retained ends. Coordinates may contain expression strings.":"Two straight members with intersecting axes. Extension needs an explicit bound; the exact outer host envelope cuts the tube.")).c_str(),fields))return;
    V joint=V::makeObj();joint.set("kind",V::makeStr(kind));joint.set("member",V::makeStr(member));joint.set("end",V::makeStr(end));if(plane){joint.set("origin",frameFormTriple(trimField(fields[0].value)));joint.set("normal",frameFormTriple(trimField(fields[1].value)));}else{joint.set("against",V::makeStr(trimField(fields[0].value)));formNumber(fields[1].value);joint.set("max_extension",V::makeStr(trimField(fields[1].value)));}
    if(action=="add"){command.set("op",V::makeStr(plane?"frame.trim_plane":"frame.trim_extend"));command.set("joint",joint);}else{joints.arr[index]=joint;V native=feature->nativeSpec;native.set("joints",joints);command.set("op",V::makeStr("feature.edit"));command.set("native",native);}applyFrameCommand(command,revision,epoch);
}
static void frameWeldDialog(){
    using V=arkjson::Value;Feature definition=*selectedFrameDefinition();auto* feature=&definition;const u64 revision=g_session.revision,epoch=g_documentEpoch;V welds=feature->nativeSpec.find("welds")?*feature->nativeSpec.find("welds"):V::makeArr();const auto* members=feature->nativeSpec.find("members");if(!members||members->arr.size()<2)throw std::runtime_error("A weld needs two existing frame members");const V* first=welds.arr.empty()?nullptr:&welds.arr.front();std::string hint=frameDefinitionHint(*feature)+frameFormMemberHint(*feature)+". Existing beads: ";for(const auto& row:welds.arr)hint+=frameFormText(&row,"id","")+" ";
    std::vector<EditorField> pick{{"Action: add / edit / remove",first?"edit":"add"},{"Stable bead ID",frameFormText(first,"id","Seam1")}};if(!editFields("Declared exact frame bead",hint.c_str(),pick))return;const auto action=trimField(pick[0].value),id=trimField(pick[1].value);if(action!="add"&&action!="edit"&&action!="remove")throw std::runtime_error("Action must be add, edit or remove");size_t index=welds.arr.size();for(size_t i=0;i<welds.arr.size();++i)if(frameFormText(&welds.arr[i],"id","")==id)index=i;const V* existing=index<welds.arr.size()?&welds.arr[index]:nullptr;if(action=="add"&&existing)throw std::runtime_error("This bead ID already exists");if(action!="add"&&!existing)throw std::runtime_error("No bead exists with this ID");V command=V::makeObj();command.set("id",V::makeNum((double)feature->id));
    if(action=="remove"){command.set("op",V::makeStr("frame.weld.remove"));command.set("entity",V::makeStr(id));applyFrameCommand(command,revision,epoch);return;}
    V path=existing&&existing->find("path")?*existing->find("path"):V::makeArr();std::vector<EditorField> fields{{"Member A",frameFormText(existing,"member_a",frameFormText(&members->arr[0],"name","").c_str())},{"Member B",frameFormText(existing,"member_b",frameFormText(&members->arr[1],"name","").c_str())},{"Local seam start [x,y,z] / mm",path.arr.size()==2?path.arr[0].dump():"[0,0,0]"},{"Local seam end [x,y,z] / mm",path.arr.size()==2?path.arr[1].dump():"[0,10,0]"},{"Local first-leg normal [x,y,z]",frameFormText(existing,"normal","[1,0,0]")},{"First leg / mm",frameFormText(existing,"leg_a","3")},{"Second leg / mm",frameFormText(existing,"leg_b","3")},{"Material (blank: explicit density)",frameFormText(existing,"material","")},{"Density / g/cm3 (blank: unchecked)",frameFormText(existing,"density_g_cm3","")}};
    if(!editFields("Straight triangular fillet bead",(frameDefinitionHint(*feature)+"Authored seam and direction; a separate exact bead must touch both members without penetrating material or overlapping another bead. Attachment does not establish strength or weld quality.").c_str(),fields))return;
    V weld=V::makeObj();weld.set("id",V::makeStr(id));weld.set("member_a",V::makeStr(trimField(fields[0].value)));weld.set("member_b",V::makeStr(trimField(fields[1].value)));path=V::makeArr();path.arr={frameFormTriple(trimField(fields[2].value)),frameFormTriple(trimField(fields[3].value))};weld.set("path",path);weld.set("normal",frameFormTriple(trimField(fields[4].value)));for(size_t i=5;i<=6;++i){formNumber(fields[i].value);weld.set(i==5?"leg_a":"leg_b",V::makeStr(trimField(fields[i].value)));}const auto material=trimField(fields[7].value),density=trimField(fields[8].value);if(!material.empty()&&!density.empty())throw std::runtime_error("Choose a material or explicit density, not both");if(!material.empty())weld.set("material",V::makeStr(material));else if(!density.empty()){formNumber(density);weld.set("density_g_cm3",V::makeStr(density));}
    if(action=="add"){command.set("op",V::makeStr("frame.weld.add"));command.set("weld",weld);}else{welds.arr[index]=weld;V native=feature->nativeSpec;native.set("welds",welds);command.set("op",V::makeStr("feature.edit"));command.set("native",native);}applyFrameCommand(command,revision,epoch);
}

static void surfaceEditDialog(bool trim,const Feature* editing=nullptr){
    using V=arkjson::Value;if(editing&&editing->inputs.size()!=1)throw std::runtime_error("Surface edits need exactly one source");u64 source=editing?editing->inputs[0]:selectedEditableBody()->id;
    std::string face;for(const auto& pick:g_picked)if(pick.feature==source&&pick.kind=="face")face=pick.name;
    if(face.empty()&&selBody>=0&&(size_t)selBody<g_instancePart.size()){size_t part=g_instancePart[(size_t)selBody];if(part<g_partPicks.size())for(const auto& name:g_partPicks[part].faceNames)if(!name.empty()){face=name;break;}}
    if(editing){if(auto* value=editing->nativeSpec.find("face"))face=value->asStr();}
    std::vector<EditorField> fields{{"Source feature name / ID",std::to_string(source)}};
    if(trim){fields.push_back({"Cut datum (blank: XYZ)",editing?nativeText(editing,"plane"):"YZ"});fields.push_back({"Keep: positive / negative",editing?nativeText(editing,"keep"):"positive"});auto* origin=editing?editing->nativeSpec.find("origin"):nullptr,*normal=editing?editing->nativeSpec.find("normal"):nullptr;fields.push_back({"Origin XYZ / JSON triple",origin?origin->dump():"[0,0,0]"});fields.push_back({"Normal XYZ / JSON triple",normal?normal->dump():"[1,0,0]"});}
    else fields.push_back({"Semantic face",face});
    if(!editFields(trim?"Trim exact surface by plane":"Extract exact face",trim?"One connected open face. Use a datum, or leave it blank and supply origin/normal triples. Keep follows its normal.":"Select one persistent face name from the exact source. The original feature stays in history.",fields))return;
    source=featureIdField(trimField(fields[0].value));if(editing&&(editing->inputs.size()!=1||source!=editing->inputs[0]))throw std::runtime_error("Editing a surface operation preserves its original source; create a new operation to change source.");
    V native=V::makeObj();native.set("op",V::makeStr(trim?"trim_plane":"extract_face"));
    if(trim){auto plane=trimField(fields[1].value),keep=trimField(fields[2].value);if(keep!="positive"&&keep!="negative")throw std::runtime_error("Keep must be positive or negative");if(!plane.empty()){resolvePlane(g_doc,plane);native.set("plane",V::makeStr(plane));}else for(size_t i=3;i<5;++i){V triple;if(!arkjson::parse(fields[i].value,triple)||triple.t!=V::Arr||triple.arr.size()!=3||std::any_of(triple.arr.begin(),triple.arr.end(),[](const V& value){return value.t!=V::Num&&value.t!=V::Str;}))throw std::runtime_error("Origin/normal must be JSON triples of numbers or expressions");native.set(i==3?"origin":"normal",triple);}native.set("keep",V::makeStr(keep));}
    else{face=trimField(fields[1].value);if(face.empty())throw std::runtime_error("Choose an exact semantic face");native.set("face",V::makeStr(face));}
    commitNativeFeature(FeatureType::SurfaceEdit,trim?"Trimmed surface":"Extracted surface",V::makeObj(),std::move(native),{source},editing?editing->id:0,true);
}
static void commitMeshFeature(FeatureType type,const std::string& name,arkjson::Value p,std::vector<u64> inputs,u64 editId){
    if(!editId){addMeshFeature(type,name,std::move(p),std::move(inputs));return;}
    using V=arkjson::Value;V command=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)editId));command.set("p",std::move(p));
    applyCommand(std::move(command));if(!g_error){frameScene();g_status="Feature edited / revision "+std::to_string(g_session.revision);}
}
static bool offerMeshSample(const Feature* editing,const Feature* source,std::vector<EditorField>& fields){
    if(editing){if(!editing->p.count("sample"))return false;fields.push_back({"Sample exact B-rep",editing->get("sample","1"),true});return true;}
    if(source&&isNativeFeature(source->type)){fields.push_back({"Sample exact B-rep","1",true});return true;}return false;
}
static void keepMeshSample(arkjson::Value& p,bool offered,const std::string& value){
    if(!offered)return;if(value!="1")throw std::runtime_error("Sampling an exact B-rep requires Sample = 1. The result is mesh authority");p.set("sample",arkjson::Value::makeStr("1"));
}
static void meshWeldDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;auto* source=editing?editing:selectedEditableBody();
    std::vector<EditorField> fields{{"Epsilon / mm",editing?editing->get("epsilon","0.0001"):"0.0001"}};
    bool sample=offerMeshSample(editing,source,fields);
    if(!editFields(editing?"Edit weld":"Weld mesh vertices","Epsilon is in (0, 1] mm. An exact surface stays exact unless Sample is 1.",fields))return;
    V p=V::makeObj();p.set("epsilon",V::makeStr(trimField(fields[0].value)));keepMeshSample(p,sample,fields.back().value);
    commitMeshFeature(FeatureType::MeshWeld,"Welded mesh",std::move(p),{source->id},editing?editing->id:0);
}
static void meshSmoothDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;auto* source=editing?editing:selectedEditableBody();
    std::vector<EditorField> fields{{"Iterations",editing?editing->get("iterations","4"):"4"},{"Lambda",editing?editing->get("lambda","0.5"):"0.5"},{"Pin boundary",editing?editing->get("pin_boundary","1"):"1",true}};
    bool sample=offerMeshSample(editing,source,fields);
    if(!editFields(editing?"Edit smooth":"Smooth mesh","Uniform Laplacian. Iterations are 1..64. Boundary vertices stay fixed while Pin boundary is on.",fields))return;
    V p=V::makeObj();p.set("iterations",V::makeStr(trimField(fields[0].value)));p.set("lambda",V::makeStr(trimField(fields[1].value)));p.set("pin_boundary",V::makeStr(fields[2].value));keepMeshSample(p,sample,fields.back().value);
    commitMeshFeature(FeatureType::MeshSmooth,"Smoothed mesh",std::move(p),{source->id},editing?editing->id:0);
}
static void meshDecimateDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;auto* source=editing?editing:selectedEditableBody();
    std::vector<EditorField> fields{{"Triangle ratio",editing?editing->get("ratio","0.5"):"0.5"}};
    bool sample=offerMeshSample(editing,source,fields);
    if(!editFields(editing?"Edit decimate":"Decimate mesh","Ratio is in [0.05, 1]. Boundary loops stay. The result remains mesh authority.",fields))return;
    V p=V::makeObj();p.set("ratio",V::makeStr(trimField(fields[0].value)));keepMeshSample(p,sample,fields.back().value);
    commitMeshFeature(FeatureType::MeshDecimate,"Decimated mesh",std::move(p),{source->id},editing?editing->id:0);
}
static void meshOffsetDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;auto* source=editing?editing:selectedEditableBody();
    std::vector<EditorField> fields{{"Distance / mm",editing?editing->get("distance","1"):"1"}};
    bool sample=offerMeshSample(editing,source,fields);
    if(!editFields(editing?"Edit mesh offset":"Offset mesh along normals","Vertex normals. A positive distance grows a closed mesh. This is not an exact shell.",fields))return;
    V p=V::makeObj();p.set("distance",V::makeStr(trimField(fields[0].value)));keepMeshSample(p,sample,fields.back().value);
    commitMeshFeature(FeatureType::MeshOffset,"Offset mesh",std::move(p),{source->id},editing?editing->id:0);
}
static void meshFillDialog(const Feature* editing=nullptr){
    using V=arkjson::Value;auto* source=editing?editing:selectedEditableBody();
    std::vector<EditorField> fields;bool sample=offerMeshSample(editing,source,fields);
    if(!fields.empty()&&!editFields(editing?"Edit planar fill":"Fill planar mesh holes","Each boundary loop must lie within 1e-4 mm of a plane. Curved boundaries need surface fill.",fields))return;
    if(fields.empty()){
        if(editing){g_status="Planar fill has no dimensions. Edit the mesh it fills.";return;}
        std::vector<EditorField> confirm{{"Fill planar holes of the selection","1",true}};
        if(!editFields("Fill planar mesh holes","Each boundary loop must lie within 1e-4 mm of a plane. Curved boundaries need surface fill.",confirm))return;
        if(confirm[0].value!="1")return;
    }
    V p=V::makeObj();if(sample)keepMeshSample(p,true,fields[0].value);
    commitMeshFeature(FeatureType::MeshFill,"Filled mesh",std::move(p),editing?std::vector<u64>{}:std::vector<u64>{source->id},editing?editing->id:0);
}
static void importMeshDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Name","Imported mesh"},{"Weld mm (blank keeps file vertices)",""}};
    if(!editFields("Import STL or OBJ","The file is copied into the document and is not linked. Caps are 12288 vertices and 4096 triangles.",fields))return;
    wchar_t file[32768]=L"mesh.stl";OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=file;of.nMaxFile=32768;
    of.lpstrFilter=L"Triangle mesh\0*.stl;*.obj\0";of.lpstrTitle=L"Import STL or OBJ";of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(!GetOpenFileNameW(&of))return;
    int size=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);std::string path(size,'\0');WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),size,nullptr,nullptr);path.pop_back();
    V args=V::makeObj();args.set("path",V::makeStr(path));args.set("name",V::makeStr(trimField(fields[0].value)));
    auto weld=trimField(fields[1].value);if(!weld.empty())args.set("weld_mm",V::makeNum(formNumber(weld)));
    runTool("import_mesh",args);if(!g_error){selBody=(int)g_bodyIds.size()-1;frameScene();g_status="Mesh imported / snapshot is mesh authority, not a STEP B-rep";}
}
static Feature* selectedPartFeature(){
    if(g_explorerFeature)if(auto* feature=g_doc.find(g_explorerFeature))return feature;
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())return nullptr;
    return g_doc.find(g_bodyIds[(size_t)selBody]);
}
static void rememberSketch(std::vector<std::string>& names,const std::string& name){
    if(name.empty()||!g_doc.findSketch(name)||std::find(names.begin(),names.end(),name)!=names.end())return;names.push_back(name);
}
static std::vector<std::string> drivingSketchNames(const Feature& feature){
    std::vector<std::string> names;
    if(auto* sections=feature.nativeSpec.find("sections");sections&&sections->t==arkjson::Value::Arr)
        for(auto& item:sections->arr)if(item.t==arkjson::Value::Str)rememberSketch(names,item.str);
    rememberSketch(names,nativeText(&feature,"section"));rememberSketch(names,nativeText(&feature,"path"));rememberSketch(names,feature.get("sketch"));
    return names;
}
static void activateSketch(const std::string& name){
    auto* sketch=g_doc.findSketch(name);if(!sketch)throw std::runtime_error("Sketch not found: "+name);
    g_activeSketch=name;g_activePlane=sketch->sk.plane;g_selectedSketchConstraint=-1;g_sketchConstraintPage=0;g_showSketches=true;rebuildSketchOverlay();alignSketchCamera();
    g_status="Sketch "+name+" / edit an anchor or a named parameter to recompute";
}
static void editFeatureParameters(const Feature& feature){
    using V=arkjson::Value;std::vector<std::string> keys;keys.reserve(feature.p.size());
    for(auto& entry:feature.p)keys.push_back(entry.first);std::sort(keys.begin(),keys.end());
    if(keys.empty()){g_status="This feature has no editable parameters.";return;}
    std::vector<EditorField> fields;fields.reserve(keys.size());
    for(auto& key:keys){bool toggle=key=="solid"||key=="ruled"||key=="sample"||key=="pin_boundary"||key=="both"||key=="smooth"||key=="compatible"||key=="fuse"||key=="correction"||key=="contact";fields.push_back({key,feature.p.at(key),toggle});}
    if(!editFields(("Edit "+feature.name).c_str(),"Expressions recompute this feature and everything that depends on it.",fields))return;
    V p=V::makeObj();for(size_t i=0;i<fields.size();++i)p.set(keys[i],V::makeStr(fields[i].value));
    V command=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)feature.id));command.set("p",std::move(p));
    applyCommand(std::move(command));if(!g_error){frameScene();g_status="Feature edited / revision "+std::to_string(g_session.revision);}
}
static void editSelectedFeature(){
    auto* feature=selectedPartFeature();if(!feature){g_status="Select a part feature in the explorer.";return;}
    switch(feature->type){
        case FeatureType::Curve:spatialCurveDialog(feature);break;
        case FeatureType::Loft:loftDialog(feature);break;
        case FeatureType::SurfaceFill:surfaceFillDialog(feature);break;
        case FeatureType::BSplineSurface:nurbsSurfaceDialog(feature);break;
        case FeatureType::Thicken:thickenDialog(feature);break;
        case FeatureType::Sweep:if(feature->nativeSpec.find("sections"))varyingSweepEntityDialog(feature);else sweepDialog(feature);break;
        case FeatureType::ExtrudeSurface:extrudeSurfaceDialog(feature);break;
        case FeatureType::RevolveSurface:revolveSurfaceDialog(feature);break;
        case FeatureType::OffsetSurface:offsetSurfaceDialog(feature);break;
        case FeatureType::Sew:sewDialog(feature);break;
        case FeatureType::SurfaceEdit:if(nativeText(feature,"op")=="trim_wire")trimSurfaceWireDialog(feature);else surfaceEditDialog(nativeText(feature,"op")=="trim_plane",feature);break;
        case FeatureType::MeshWeld:meshWeldDialog(feature);break;
        case FeatureType::MeshSmooth:meshSmoothDialog(feature);break;
        case FeatureType::MeshDecimate:meshDecimateDialog(feature);break;
        case FeatureType::MeshOffset:meshOffsetDialog(feature);break;
        case FeatureType::MeshFill:meshFillDialog(feature);break;
        case FeatureType::NativeSolid:{
            auto op=feature->get("op");
            if(featureOperation(*feature)=="holes_orchestrator")holesOrchestratorDialog(feature);else if(featureOperation(*feature)=="offset")offsetSolidDialog(feature);else if(op=="fillet"||op=="chamfer")nativeEdgeBlendDialog(feature);
            else if(op=="draft")nativeDraftDialog(feature);
            else if(op=="linear_pattern"||op=="circular_pattern")nativePatternDialog(feature);
            else if(op=="mirror")nativeMirrorDialog(feature);
            else editFeatureParameters(*feature);
            break;
        }
        case FeatureType::SheetMetal:uiAction(127);break;
        case FeatureType::Suspension:{std::string text=feature->nativeSpec.dump();u64 id=feature->id;
            if(editText("Edit suspension hardpoints, wheel and spring / JSON",text,true)){arkjson::Value native;if(!arkjson::parse(text,native)||native.t!=arkjson::Value::Obj)throw std::runtime_error("Suspension native program must be JSON object");
                arkjson::Value command=arkjson::Value::makeObj();command.set("op",arkjson::Value::makeStr("feature.edit"));command.set("id",arkjson::Value::makeNum((double)id));command.set("native",std::move(native));applyCommand(std::move(command));}
            break;}
        case FeatureType::Tube:uiAction(30);break;
        case FeatureType::NotchedTube:uiAction(41);break;
        case FeatureType::SourceTube:case FeatureType::SourceSheet:case FeatureType::MeshBody:
            g_status="Imported geometry is a baked snapshot. Edit the features built from sketches.";break;
        default:editFeatureParameters(*feature);break;
    }
}
static void editDrivingSketch(){
    auto* feature=selectedPartFeature();if(!feature){g_status="Select a feature to open its sketch.";return;}
    auto names=drivingSketchNames(*feature);if(names.empty()){g_status="This feature has no driving sketch.";return;}
    std::string choice=names.front();
    if(names.size()>1){std::string hint;for(auto& name:names)hint+=(hint.empty()?"":", ")+name;std::vector<EditorField> fields{{"Sketch",choice}};
        if(!editFields("Open driving sketch",hint.c_str(),fields))return;choice=trimField(fields[0].value);}
    activateSketch(choice);
}
struct HistoryEditor {arkjson::Value nodes;HWND list=nullptr,details=nullptr;std::string head,selected;};
static int g_historySmokeIndex=0;
static void selectHistoryNode(HistoryEditor& editor){
    using V=arkjson::Value;auto selected=SendMessage(editor.list,LB_GETCURSEL,0,0);editor.selected.clear();if(selected<0||(size_t)selected>=editor.nodes.arr.size()){EnableWindow(GetDlgItem(g_dialog,1),FALSE);return;}const auto& node=editor.nodes.arr[(size_t)selected];auto text=[&](const V& object,const char* key){auto* value=object.find(key);return value?value->asStr():std::string{};};editor.selected=text(node,"id");
    std::ostringstream detail;detail<<"Iteration: "<<editor.selected<<(editor.selected==editor.head?"  (active head)":"")<<"\r\nParent: "<<text(node,"parent_id")<<"\r\nUTC: "<<text(node,"utc")<<"\r\nTool: "<<text(node,"tool")<<"\r\nOperation: "<<text(node,"summary")<<"\r\n";if(auto* actor=node.find("actor"))detail<<"Actor: "<<text(*actor,"kind")<<" / "<<text(*actor,"id")<<" / "<<text(*actor,"role")<<" / "<<text(*actor,"model")<<" (caller declared)\r\n";detail<<"Motivation: "<<text(node,"motivation")<<"\r\nRestored from: "<<text(node,"restored_from_id")<<"\r\n\r\nBefore SHA-256: "<<text(node,"before_sha256")<<"\r\nAfter SHA-256: "<<text(node,"after_sha256")<<"\r\n\r\nRestore re-evaluates this document state and creates a new branch. Current token receipts remain in the drawing. Revision increases.";SetWindowTextA(editor.details,detail.str().c_str());EnableWindow(GetDlgItem(g_dialog,1),!editor.selected.empty());
}
static LRESULT CALLBACK historyEditorProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){auto* editor=(HistoryEditor*)GetWindowLongPtr(hwnd,GWLP_USERDATA);if(editor&&message==WM_COMMAND&&LOWORD(wp)==101&&HIWORD(wp)==LBN_SELCHANGE){selectHistoryNode(*editor);return 0;}if(editor&&message==WM_TIMER&&wp==901){SendMessage(editor->list,LB_SETCURSEL,(WPARAM)g_historySmokeIndex,0);selectHistoryNode(*editor);}if(editor&&message==WM_COMMAND&&LOWORD(wp)==1&&editor->selected.empty())return 0;return editorProc(hwnd,message,wp,lp);}
static void iterationHistoryDialog(){
    using V=arkjson::Value;if(g_chatBusy||(g_teamStarted&&g_agentTeam.snapshot().running)){g_status="Finish or cancel active assistant work before restoring an iteration.";return;}V args=V::makeObj();args.set("limit",V::makeNum(64));auto response=runTool("history_info",args);if(g_error)return;auto* info=response.find("structuredContent"),*nodes=info?info->find("nodes"):nullptr;if(!nodes||nodes->t!=V::Arr||nodes->arr.empty()){g_status="No persistent iterations have been recorded for this drawing.";return;}u64 observedRevision=g_session.revision;
    V allNodes=*nodes;const V* pageInfo=info;std::set<std::string> cursors;V page;
    while(auto* more=pageInfo->find("has_more")){if(more->t!=V::Bool||!more->b)break;auto* cursor=pageInfo->find("next_before_id");if(!cursor||cursor->t!=V::Str||cursor->str.empty()||!cursors.insert(cursor->str).second||allNodes.arr.size()>=512)throw std::runtime_error("History pagination is unavailable or exceeds the retained node bound");V pageArgs=V::makeObj();pageArgs.set("limit",V::makeNum(64));pageArgs.set("before_id",*cursor);page=runTool("history_info",pageArgs);if(g_error)return;pageInfo=page.find("structuredContent");auto* older=pageInfo?pageInfo->find("nodes"):nullptr;if(!older||older->t!=V::Arr||older->arr.empty()||allNodes.arr.size()+older->arr.size()>512)throw std::runtime_error("History page is empty or exceeds the retained node bound");allNodes.arr.insert(allNodes.arr.end(),older->arr.begin(),older->arr.end());}
    if(auto* baseline=info->find("baseline_id");baseline&&baseline->t==V::Str&&!baseline->str.empty()){V node=V::makeObj();node.set("id",*baseline);node.set("summary",V::makeStr("Initial drawing state before the first recorded edit"));node.set("tool",V::makeStr("baseline"));if(!allNodes.arr.empty())if(auto* hash=allNodes.arr.back().find("before_sha256"))node.set("after_sha256",*hash);allNodes.arr.push_back(node);}
    HistoryEditor editor;editor.nodes=std::move(allNodes);if(auto* head=info->find("head_id"))editor.head=head->asStr();WNDCLASSA cls{};cls.lpfnWndProc=historyEditorProc;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="ARK_ITERATION_HISTORY";cls.hbrBackground=dialogBrush;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&cls);
    RECT parent;GetWindowRect(g_window,&parent);int width=1000,height=620;RECT outer{0,0,width,height};AdjustWindowRectEx(&outer,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);int ow=outer.right-outer.left,oh=outer.bottom-outer.top;g_dialog=CreateWindowExA(WS_EX_DLGMODALFRAME,cls.lpszClassName,"Persistent drawing iterations / restore a branch",WS_CAPTION|WS_SYSMENU,parent.left+(parent.right-parent.left-ow)/2,parent.top+(parent.bottom-parent.top-oh)/2,ow,oh,g_window,nullptr,cls.hInstance,nullptr);if(!g_dialog)throw std::runtime_error("Could not open iteration history.");HFONT font=CreateFontA(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");BOOL dark=TRUE;DwmSetWindowAttribute(g_dialog,20,&dark,sizeof dark);
    auto control=[&](const char* kind,const char* text,DWORD style,int x,int y,int w,int h,int id){auto window=CreateWindowExA(0,kind,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,g_dialog,(HMENU)(INT_PTR)id,cls.hInstance,nullptr);SendMessage(window,WM_SETFONT,(WPARAM)font,TRUE);return window;};
    control("STATIC","Newest first. Select an iteration to inspect its actor, motivation and measured document hashes.",0,24,18,952,28,0);editor.list=control("LISTBOX","",WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,24,52,952,218,101);editor.details=control("EDIT","",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,24,286,952,258,102);control("BUTTON","Close",WS_TABSTOP|BS_OWNERDRAW,660,570,100,34,2);control("BUTTON","Restore selected",WS_TABSTOP|BS_OWNERDRAW,776,570,200,34,1);
    for(const auto& node:editor.nodes.arr){auto string=[&](const char* key){auto* value=node.find(key);return value?value->asStr():std::string{};};std::string row=(string("id")==editor.head?"HEAD  ":"           ")+string("id").substr(0,18)+"  /  "+string("utc")+"  /  ";if(auto* actor=node.find("actor")){auto* role=actor->find("role"),*id=actor->find("id");row+=(role&&!role->asStr().empty()?role->asStr():id?id->asStr():"unknown")+"  /  ";}row+=string("summary");if(!string("motivation").empty())row+=" / "+string("motivation");for(char& c:row)if(c=='\r'||c=='\n')c=' ';SendMessageA(editor.list,LB_ADDSTRING,0,(LPARAM)row.c_str());}
    SetWindowLongPtr(g_dialog,GWLP_USERDATA,(LONG_PTR)&editor);SendMessage(editor.list,LB_SETCURSEL,0,0);selectHistoryNode(editor);g_edit=editor.list;g_dialogDone=g_dialogAccept=false;EnableWindow(g_window,FALSE);ShowWindow(g_dialog,SW_SHOW);SetFocus(editor.list);if(g_dialogSmoke)SetTimer(g_dialog,901,200,nullptr);MSG msg;while(!g_dialogDone&&GetMessage(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){SendMessage(g_dialog,WM_COMMAND,2,0);continue;}if(!IsDialogMessage(g_dialog,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}}
    bool restore=g_dialogAccept&&!editor.selected.empty();EnableWindow(g_window,TRUE);DestroyWindow(g_dialog);g_dialog=nullptr;DeleteObject(font);if(!g_headless)SetForegroundWindow(g_window);if(!restore)return;args=V::makeObj();args.set("id",V::makeStr(editor.selected));args.set("expected_revision",V::makeNum((double)observedRevision));args.set("motivation",V::makeStr("Restore selected iteration from ARK history"));V actor=V::makeObj();actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));args.set("actor",actor);runTool("iteration_restore",args);if(!g_error)g_status="Restored iteration as a new branch / revision "+std::to_string(g_session.revision);
}
static void motionDriverDialog(){
    auto coupled=[&](u64 node){return node&&std::any_of(g_doc.mates.begin(),g_doc.mates.end(),[&](const AssemblyMate& mate){return isMechanicalMate(mate.type)&&(mate.a==node||mate.b==node);});};u64 candidate=g_motionActive&&coupled(g_motionDriver)?g_motionDriver:0;
    if(!candidate&&selBody>=0&&(size_t)selBody<g_instanceIds.size()&&coupled(g_instanceIds[(size_t)selBody]))candidate=g_instanceIds[(size_t)selBody];
    if(!candidate)for(const auto& mate:g_doc.mates)if(isMechanicalMate(mate.type)){candidate=mate.a;break;}
    bool hasCadence=std::any_of(g_doc.params.begin(),g_doc.params.end(),[](const Parameter& parameter){return parameter.name=="cadence_rpm";});std::string speed=g_motionActive?g_motionRpmExpression:hasCadence?"cadence_rpm":"60";
    std::string initial=candidate?std::to_string(candidate):"";std::vector<EditorField> fields{{"Driver instance name / ID",initial},{"Speed / rpm or parameter",speed},{"Time / seconds",std::to_string(g_motionTime)}};
    if(!editFields("Mechanical motion","Read-only motion along authored chain, gear, shaft, rack or cam relations. Geometry and the saved document remain unchanged.",fields))return;
    u64 driver=motionInstanceId(trimField(fields[0].value));double time=formNumber(fields[2].value);g_motionDriver=driver;g_motionRpmExpression=trimField(fields[1].value);g_motionTime=time;g_motionActive=true;g_motionPlaying=false;g_motionRevision=std::numeric_limits<u64>::max();updateMotionDisplay();frameScene();g_status="Mechanical motion / read-only pose / tooth and collision contact unchecked";
}
static void mechanicalRelationDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Relation: chain / gear / shaft / rack_pinion / cam","chain"},{"Driver instance name / ID","Front crank"},{"Driven instance name / ID","Rear hub"},{"Driver exact axis reference","Z"},{"Driven exact axis reference","Z"}};
    if(!editFields("Mechanical relation","Use instance IDs and semantic axes. Geometric positioning mates remain separate from motion relations.",fields))return;auto kind=trimField(fields[0].value);V motion=V::makeObj();
    if(kind=="chain"||kind=="gear"){std::vector<EditorField> dimensions{{"Driver tooth count / expression","front_teeth"},{"Driven tooth count / expression","rear_teeth"}};if(!editFields("Tooth ratio","Counts may reference document parameters; editing them recomputes the motion ratio.",dimensions))return;motion.set("teeth_a",V::makeStr(trimField(dimensions[0].value)));motion.set("teeth_b",V::makeStr(trimField(dimensions[1].value)));}
    else if(kind=="rack_pinion"){std::vector<EditorField> dimensions{{"Pinion pitch radius / mm","20"},{"Direction / +1 or -1","1"}};if(!editFields("Rack and pinion","Prescribed pitch motion; tooth contact is not checked.",dimensions))return;motion.set("pitch_radius",V::makeStr(trimField(dimensions[0].value)));motion.set("direction",V::makeStr(trimField(dimensions[1].value)));}
    else if(kind=="cam"){std::vector<EditorField> dimensions{{"Harmonic lift / mm","10"},{"Phase / degrees","0"}};if(!editFields("Harmonic cam","Prescribed follower lift; contact and dynamics are not checked.",dimensions))return;motion.set("lift",V::makeStr(trimField(dimensions[0].value)));motion.set("phase_deg",V::makeStr(trimField(dimensions[1].value)));}
    else if(kind!="shaft")throw std::runtime_error("Choose chain, gear, shaft, rack_pinion or cam");V command=V::makeObj();command.set("op",V::makeStr("mate.add"));command.set("type",V::makeStr(kind));command.set("a",V::makeStr(trimField(fields[1].value)));command.set("b",V::makeStr(trimField(fields[2].value)));command.set("reference_a",V::makeStr(trimField(fields[3].value)));command.set("reference_b",V::makeStr(trimField(fields[4].value)));command.set("motion",std::move(motion));applyCommand(command);
}
static void transmissionParametersDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields;for(const char* key:{"front_teeth","rear_teeth","cadence_rpm"}){auto parameter=std::find_if(g_doc.params.begin(),g_doc.params.end(),[&](const Parameter& value){return value.name==key;});if(parameter==g_doc.params.end())throw std::runtime_error("This transmission has no editable front_teeth, rear_teeth and cadence_rpm parameters");fields.push_back({key,parameter->expr});}
    if(!editFields("Transmission teeth and cadence","Updates the native sprocket geometry and the chain ratio together. Saved parameter expressions remain editable; Undo restores this change.",fields))return;V commands=V::makeArr();for(size_t i=0;i<fields.size();++i){V command=V::makeObj();command.set("op",V::makeStr("param.set"));command.set("name",V::makeStr(fields[i].name));command.set("expr",V::makeStr(trimField(fields[i].value)));commands.arr.push_back(std::move(command));}V args=V::makeObj();args.set("commands",std::move(commands));runTool("doc_apply",args);if(!g_error){g_motionRpmExpression="cadence_rpm";g_motionRevision=std::numeric_limits<u64>::max();updateMotionDisplay();frameScene();}
}
#include "stamps_authoring.inl"
#include "planes_sections_024b.inl"
struct StudioCommandGroup {const char* title;std::vector<std::pair<int,const char*>> items;};
static const std::vector<StudioCommandGroup>& studioCommandGroups(){
    static const auto groups=[](){std::vector<StudioCommandGroup> result;
        auto group=[&](const char* title,std::initializer_list<std::pair<int,const char*>> items){result.push_back({title,items});};
    group("Sketch planes and sections",{{950,"Sketch to plane"},{951,"Offset plane"},{952,"Section from plane"},{953,"Section from sketch line"}});
    group("Stamps and face regions",{{940,"Stamp on selected face"},{944,"Stamp on sketch plane"},{941,"Face / region appearance"},{942,"Split line"},{943,"Remove stamp"}});
    group("CAD exchange",{{910,"Import STEP / IGES"},{911,"Arkitfy imported reference"}});
    group("Holes Orchestrator",{{920,"Holes Orchestrator"},{921,"Edit named hole positions"}});
    group("Design containers",{{930,"Create design container"},{931,"Inspect design container"},{932,"Check out container"},{933,"Check out and open another ARK"},{934,"Merge edited container"},{935,"Inspect isolated workspace"}});
    group("Document",{{55,"New document"},{17,"Open ARK"},{16,"Save ARK"},{52,"Import source STEP"},{213,"Export STL/OBJ/FBX/STEP/IGES"},{51,"Export straight tube STEP"},{82,"Export native STEP"},{12,"Export mesh snapshot"}});
    group("Sketch entities",{{660,"Corner rectangle"},{661,"Center rectangle"},{662,"Rectangle through three points"},{663,"Parallelogram"},{664,"Straight slot"},{665,"Center slot"},{666,"Arc slot"},{667,"Arc slot through three points"},{668,"Polygon"},{669,"Circle through three perimeter points"},{670,"Ellipse"},{671,"Partial ellipse"},{672,"Parabola"},{673,"Conic"},{674,"Style spline"},{675,"Equation curve 2D"},{676,"Sketch point"},{677,"Centerline"},{678,"Midpoint line"},{679,"Route sketch"},{680,"Belt sketch"},{681,"Text engraving"},{682,"Spline on exact surface"},{683,"Interpolating spline 3D"},{684,"Sweep with varying sections"},{685,"Exact polynomial XYZ"},{686,"Center rectangle through three points"}});
    group("Construction",{{630,"Offset plane"},{631,"Plane at angle"},{632,"Plane through three points"},{633,"Plane through axis and point"},{634,"Plane normal to axis at point"},{635,"Axis through two points"},{636,"Axis at plane intersection"},{637,"Construction point XYZ"},{638,"Linked construction point"},{639,"Frame between two points"},{640,"Select construction datum"},{641,"Toggle selected datum"},{642,"Show all datums"},{643,"Hide all datums"},{644,"Edit selected datum"}});
    group("Direct authoring",{{650,"Push / pull source face"},{651,"Move body"},{652,"Extrude mesh patch"},{653,"Pull vertex (CAD / mesh)"},{654,"Move mesh edge"},{659,"Source dimensions"},{754,"Sample and weld mesh"},{658,"Select adjacent tangent faces"},{750,"Helix curve"},{752,"Loft section and guide controls"},{710,"Skeleton code editor"}});
    group("Component patterns",{{770,"Linear component pattern"},{771,"Circular component pattern"},{772,"Pattern driven component pattern"},{773,"Sketch driven component pattern"},{774,"Curve driven component pattern"},{775,"Chain component pattern"},{776,"Edit selected component pattern"},{777,"Remove pattern / keep seed"},{778,"Inspect pattern evidence"},{779,"Rebind pattern source"}});
    group("Advanced sheet metal",{{800,"Miter flange"},{801,"Edit miter flange"},{802,"Remove miter flange"},{803,"Contour flange / straight-edge sweep"},{804,"Edit contour flange"},{805,"Remove contour flange"},{806,"Corner relief / trim / break / closure"},{807,"Edit corner operation"},{808,"Remove corner operation"},{809,"Edge rip / bend notch"},{810,"Edit edge rip"},{811,"Remove edge rip"},{812,"Paired tab and slot"},{813,"Edit tab and slot"},{814,"Remove tab and slot"},{815,"Declared bend allowance table"},{816,"Remove bend table"}});
    group("Manufacturing drawings",{{860,"Drawings in viewport"},{840,"Return to 3D design"},{841,"Part drawings"},{842,"Assembly drawings"},{843,"Create source sheet"},{844,"Sheet template"},{845,"Add drawing view"},{846,"Add dimension / tolerance"},{847,"Add section view"},{848,"Refresh current drawing"},{849,"Export drawing"},{850,"Previous sheet"},{851,"Next sheet"},{852,"Fit sheet"},{855,"Create all source sheets"},{856,"Select origin in 3D"},{857,"Edit dimension"},{858,"Edit view"},{859,"Remove sheet"}});
    group("Image references",{{730,"Attach image to Assistant"},{731,"Inspire design"},{732,"Image to CAD"},{733,"Remove pending image"}});
    group("Sketch & planes",{{610,"Edit sketch in viewport"},{611,"Exit sketch edit"},{60,"Create / edit plane"},{113,"Attach plane to feature"},{69,"Select plane"},{72,"Toggle datum frames"},{61,"Rectangle"},{62,"Circle / opening"},{543,"Sketch line"},{544,"Arc through three points"},{545,"Arc from start tangent"},{546,"Arc from centre and angles"},{547,"3D arc through three points"},{548,"3D tangent arc"},{63,"Interpolating spline"},{70,"Edit entity coordinates"},{73,"Add constraint"},{89,"Edit selected constraint"},{90,"Remove selected constraint"},{542,"Rename sketch entity"},{536,"Project geometry to 2D sketch"},{537,"Include linked geometry in 3D"},{538,"Create exact spatial curve"},{700,"3D sketch constraints"},{591,"Parametric curve dimension"},{592,"XYZ equation curve"},{593,"Offset saved sketch curve"},{64,"Edit raw sketch JSON"},{65,"Extrude sketch"},{66,"Cut from sketch"},{67,"Named parameter"},{68,"Toggle sketch overlay"}});
    group("Solids",{{1,"Box"},{2,"Cylinder"},{3,"Sphere"},{4,"Cone"},{5,"Torus"},{6,"Polygon extrusion"},{7,"Revolve"},{11,"Linear pattern"}});
    group("Exact solids",{{100,"Native primitive"},{101,"Extrude saved sketch"},{102,"Union / cut / common"},{103,"Hole / slot / profile pocket"},{104,"Fillet / chamfer"},{106,"Draft faces"},{107,"Linear / circular pattern"},{108,"Mirror solid"},{105,"Rigid transform"},{594,"Offset exact solid"}});
    group("Modify",{{597,"Jump to adjacent face / edge"},{8,"Fuse bodies"},{9,"Carve body"},{10,"Keep overlap"},{108,"Mirror solid"},{25,"Delete selection"},{18,"Undo"},{19,"Redo"}});
    group("Tube & assembly",{{215,"Create double-wishbone suspension"},{384,"Spring unit controls"},{383,"Play / pause suspension"},{26,"Bend-program tube"},{40,"Laser tube"},{27,"Create assembly"},{28,"Duplicate instance"},{29,"Instance placement"},{93,"Ground / unground instance"},{94,"Add assembly mate"},{95,"Edit selected mate"},{96,"Remove selected mate"},{30,"Edit tube program"}});
    group("Native surfaces",{{161,"Edit selected feature"},{162,"Edit driving sketch"},{76,"Loft sketches"},{77,"Fill closed sketch"},{78,"NURBS surface"},{150,"Sweep sketches"},{151,"Extrude sketch surface"},{152,"Revolve sketch surface"},{153,"Offset selected surface"},{154,"Glue surfaces"},{79,"Thicken selected surface"},{491,"Extract exact semantic face"},{492,"Trim exact surface by datum plane"},{595,"Project curve onto surface"},{596,"Trim surface by closed curve"}});
    group("Mesh",{{155,"Weld mesh"},{156,"Smooth mesh"},{157,"Decimate mesh"},{158,"Offset mesh"},{159,"Fill planar holes"},{160,"Import STL/OBJ"}});
    group("Sheet metal",{{120,"Create sheet base"},{127,"Edit sheet base"},{494,"Original-blank jog add/edit/remove"},{495,"Linked sketch bend add/edit/remove"},{121,"Add flange"},{122,"Edit flange"},{123,"Remove flange"},{394,"Add return hem"},{395,"Edit return hem"},{396,"Remove return hem"},{397,"Add circular emboss"},{398,"Edit circular emboss"},{399,"Remove circular emboss"},{124,"Add opening or pattern"},{125,"Edit opening"},{126,"Remove opening"},{327,"Export analytic flat DXF"},{128,"Play / pause bend preview"},{80,"Create L bend"},{81,"Import sheet STEP"},{83,"Show formed"},{84,"Show flat"}});
    group("View & agents",{{860,"Technical drawings / associative sheets"},{590,"BOM / bill of materials"},{585,"Measure 2D / X and Y"},{586,"Measure 3D / distance and XYZ"},{587,"Units / length, mass and volume"},{20,"Fit document"},{71,"Normal to sketch plane"},{180,"Front"},{181,"Back"},{23,"Top"},{184,"Bottom"},{182,"Right"},{183,"Left"},{24,"Isometric"},{175,"Orthographic"},{174,"Visible edges"},{186,"Cycle ambient occlusion"},{187,"Edit selected body material"},{188,"Inspect mass and center of mass"},{176,"Orbit"},{177,"Pan"},{178,"Zoom"},{14,"Wireframe"},{22,"Grid"},{56,"Graphics settings"},{567,"Project tool activity"},{57,"ChatGPT account"},{357,"Connectors / OpenAI, Claude, Grok, Ollama..."},{368,"Local models / Ollama"},{58,"Codex / live MCP"},{21,"Raw agent command"},{31,"Help"},{44015,"ARK updates"}});
    group("Frames, electronics & routing",{{210,"Construction frame"},{549,"Exact tube end fit"},{496,"Frame endpoint trim/extend"},{497,"Frame crossing-plane trim"},{498,"Frame weld add/edit/remove"},{211,"PCB and mounting"},{212,"Cable route"}});
    group("Physics & loads",{{755,"Collision check"},{190,"Assembly mass"},{216,"Loads and physical assumptions"},{189,"Close mass inspector"}});
    group("Motion",{{451,"Mechanical relation"},{452,"Motion driver"},{453,"Play / pause motion"},{454,"Reset motion"},{469,"Transmission teeth and cadence"}});
    group("Selection & display",{{170,"Select body"},{171,"Select face"},{172,"Select edge"},{173,"Select curve"},{401,"Feature inspector"},{410,"Model explorer"},{411,"Display layers"},{412,"Properties inspector"},{462,"Shaded"},{463,"Shaded with edges"},{464,"Wireframe style"},{465,"Show view cube"},{461,"Navigation controls"},{460,"Full screen"},{191,"Home view"},{340,"Hide selected body"},{341,"Show selected body"},{342,"Spotlight selected body"},{343,"Exit Spotlight"},{344,"Show all bodies"}});
    group("Manufacturing previews",{{320,"Unfold all sheets"},{321,"Refold all sheets"},{322,"Play all sheet bend sequences"},{323,"Pause all sheet bend sequences"},{325,"Edit selected sheet"},{326,"Stock preview"},{468,"Sheet opening pattern"}});
    group("Tube openings & playback",{{41,"Edit laser tube stock"},{42,"Add notch / opening"},{43,"Edit notch / opening"},{44,"Remove notch / opening"},{45,"Straight tube pose"},{46,"Folded tube pose"},{47,"Play / pause tube folding"},{48,"Previous tube pose"},{49,"Next tube pose"}});
    group("Drawing evidence",{{493,"Persistent iterations and branch restore"},{488,"Local resources and drawing cost details"},{489,"Refresh public pricing rates"},{330,"Token usage receipts"},{324,"Show chassis in unfolded layout"}});
    group("Components & library",{{484,"Search reusable components"},{485,"Import STEP into component library"}});
    group("Assistant & team",{{541,"Measured agent activity"},{400,"Open Assistant"},{471,"Team mode"},{470,"Single Assistant mode"},{472,"Pause / resume team"},{473,"Choose decision recipient"},{490,"Continue yielded / interrupted team window"},{44014,"Focused / parallel team workflow"},{474,"Engineer / orchestrator model and reasoning"},{475,"Part designer model and reasoning"},{476,"Physics reviewer model and reasoning"},{367,"Assistant reasoning effort"},{75,"Cancel active Assistant or team"},{360,"Continue with ChatGPT"},{361,"Choose ChatGPT plan model"},{362,"Sign out of ChatGPT"},{364,"Enable ChatGPT plan usage"},{365,"Retry ChatGPT connection"},{366,"Cancel account operation"},{358,"Switch to API key mode"}});
        return result;}();return groups;
}
static std::string toolMatchText(std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return (char)std::tolower(c);});return text;}
struct StudioToolEntry{int action=0,category=0;std::string title,description;};
static const arkjson::Value& studioApiTools(){static const auto tools=AgentSession::tools();return tools;}
static const std::vector<StudioToolEntry>& studioToolEntries(){
    static const auto entries=[](){std::vector<StudioToolEntry> result;const auto& groups=studioCommandGroups();for(size_t i=0;i<groups.size();++i)for(const auto& item:groups[i].items)result.push_back({item.first,(int)i,item.second,groups[i].title});
        const auto& tools=studioApiTools();for(size_t i=0;i<tools.arr.size();++i){const auto& tool=tools.arr[i];auto* name=tool.find("name"),*description=tool.find("description");if(name)result.push_back({41000+(int)i,(int)groups.size(),name->asStr(),description?description->asStr():"CAD API"});}return result;}();return entries;
}
static std::vector<size_t> visibleStudioTools(){std::vector<size_t> result;std::set<int> seen;auto match=toolMatchText(g_toolFilter);const auto& tools=studioToolEntries();for(size_t i=0;i<tools.size();++i){const auto& item=tools[i];if(match.empty()&&g_toolCategory>=0&&item.category!=g_toolCategory)continue;if((match.empty()||toolMatchText(item.title+" "+item.description).find(match)!=std::string::npos)&&seen.insert(item.action).second)result.push_back(i);}return result;}
static arkjson::Value starterToolArgument(const arkjson::Value& schema,unsigned depth=0){
    using V=arkjson::Value;if(depth>5)return V::makeObj();if(auto* value=schema.find("default"))return *value;if(auto* choices=schema.find("enum");choices&&choices->t==V::Arr&&!choices->arr.empty())return choices->arr.front();auto* type=schema.find("type");std::string kind=type?type->asStr():"object";
    if(kind=="string")return V::makeStr("");if(kind=="number"||kind=="integer")return V::makeNum(0);if(kind=="boolean")return V::makeBool(false);if(kind=="array")return V::makeArr();V result=V::makeObj();auto* required=schema.find("required"),*properties=schema.find("properties");if(required&&properties&&required->t==V::Arr)for(const auto& key:required->arr)if(auto* property=properties->find(key.asStr().c_str()))result.set(key.asStr(),starterToolArgument(*property,depth+1));return result;
}
static void openStudioApiTool(size_t index){
    using V=arkjson::Value;const auto& tools=studioApiTools();if(index>=tools.arr.size())return;const auto& tool=tools.arr[index];const std::string name=tool.find("name")->asStr();V args=V::makeObj();if(auto* schema=tool.find("inputSchema")){args=starterToolArgument(*schema);if(auto* props=schema->find("properties");props&&props->find("expected_revision"))args.set("expected_revision",V::makeNum((double)g_session.revision));}
    std::string text=args.dump();if(!editText((name+" / arguments JSON").c_str(),text,true))return;if(!arkjson::parse(text,args)||args.t!=V::Obj)throw std::runtime_error("Tool arguments must be a JSON object.");auto result=runTool(name,args);if(!g_error){auto* content=result.find("structuredContent");std::string report=content?content->dump():result.dump();if(g_dialogSmoke)g_status=name+" inspected / revision "+std::to_string(g_session.revision);else MessageBoxA(g_window,report.substr(0,24000).c_str(),(name+" / result").c_str(),MB_OK|MB_ICONINFORMATION);}
}
static LRESULT CALLBACK toolSearchProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){if(message==WM_KEYDOWN&&wp==VK_ESCAPE){g_toolsOpen=false;layout();SetFocus(g_window);return 0;}return CallWindowProcW(g_toolSearchProc,hwnd,message,wp,lp);}
static void commandPalette(const std::string& filter=""){
    HMENU menu=CreatePopupMenu();std::string match=toolMatchText(filter);
    for(const auto& group:studioCommandGroups()){HMENU sub=CreatePopupMenu();for(const auto& item:group.items)if(match.empty()||toolMatchText(std::string(group.title)+" "+item.second).find(match)!=std::string::npos)AppendMenuA(sub,MF_STRING,item.first,item.second);if(GetMenuItemCount(sub))AppendMenuA(menu,MF_POPUP,(UINT_PTR)sub,group.title);else DestroyMenu(sub);}
    AppendMenuA(menu,MF_STRING,480,"All tools / searchable gallery and full CAD API");
    decorateMenu(menu);POINT point;GetCursorPos(&point);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,g_window,nullptr);DestroyMenu(menu);if(id)interactiveAction(id);
}
static LRESULT CALLBACK commandSearchProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam){
    if(message==WM_KEYDOWN&&(wParam==VK_RETURN||wParam==VK_ESCAPE)){std::string query;if(wParam==VK_RETURN){int length=GetWindowTextLengthA(hwnd);query.resize((size_t)length+1);GetWindowTextA(hwnd,query.data(),length+1);query.resize((size_t)length);}g_searchEditing=false;ShowWindow(hwnd,SW_HIDE);SetFocus(g_window);if(wParam==VK_RETURN)commandPalette(query);return 0;}
    if(message==WM_KILLFOCUS){g_searchEditing=false;ShowWindow(hwnd,SW_HIDE);}return CallWindowProcW(g_commandSearchProc,hwnd,message,wParam,lParam);
}
static void moduleCommands(int category){
    HMENU menu=CreatePopupMenu();auto group=[&](const char* title,std::initializer_list<std::pair<int,const char*>> entries){HMENU sub=CreatePopupMenu();for(auto entry:entries)AppendMenuA(sub,MF_STRING,entry.first,entry.second);AppendMenuA(menu,MF_POPUP,(UINT_PTR)sub,title);};
    if(category==0){group("Solids",{{100,"Primitive"},{101,"Extrude saved sketch"},{102,"Union / cut / common"},{920,"Holes Orchestrator"},{103,"Profile / decorative cut"},{104,"Fillet / chamfer"},{106,"Draft"},{107,"Pattern"},{108,"Mirror"},{105,"Rigid transform"}});group("Surfaces",{{76,"Loft"},{77,"Fill"},{78,"NURBS"},{150,"Sweep"},{151,"Extrude surface"},{152,"Revolve surface"},{153,"Offset"},{154,"Glue surfaces"},{79,"Thicken"}});group("Frames & motion",{{210,"Construction frame"},{26,"Tube"},{215,"Suspension"},{384,"Spring unit controls"},{27,"Assembly"},{94,"Geometric mate"},{451,"Mechanical relation"},{452,"Motion driver"}});group("Electronics",{{211,"PCB"},{212,"Cable route"}});group("Mesh",{{155,"Weld"},{156,"Smooth"},{157,"Decimate"},{158,"Offset"},{159,"Fill planar holes"},{160,"Import STL/OBJ"}});}
    else if(category==1){group("Physics",{{187,"Material and appearance"},{188,"Mass / center of gravity"},{190,"Assembly mass"},{216,"Loads"}});group("View",{{56,"Graphics settings"},{174,"Edges"},{186,"Ambient occlusion"},{175,"Orthographic"},{461,"Navigation controls"}});group("Agents",{{400,"Assistant"},{58,"Live MCP"},{21,"Agent command"},{57,"Account"},{357,"Connectors"},{368,"Local models / Ollama"}});}
    else if(category==2){group("Document",{{55,"New"},{17,"Open"},{16,"Save"},{213,"Export"},{910,"Import STEP / IGES"},{52,"Import tube STEP"},{81,"Import sheet STEP"},{160,"Import mesh"}});group("History",{{18,"Undo"},{19,"Redo"}});}
    else if(category==3){group("Display style",{{462,"Shaded"},{463,"Shaded with edges"},{464,"Wireframe"},{175,"Orthographic"},{56,"Graphics settings"},{465,"Show view cube"}});group("Named views",{{180,"Front"},{181,"Back"},{23,"Top"},{184,"Bottom"},{182,"Right"},{183,"Left"},{24,"Isometric"},{20,"Fit"}});}
    decorateMenu(menu);POINT point;GetCursorPos(&point);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,g_window,nullptr);DestroyMenu(menu);if(action)interactiveAction(action);
}
static void focusSourceOpening(int index){
    auto* f=selectedSourceTube();if(!f)return;auto& cache=g_sourceViews.at(f->id);if(index<0||index>=(int)cache.preview.holes.arr.size())return;
    auto& opening=cache.preview.holes.arr[index];auto* center=opening.find("flat_center");auto* segment=opening.find("segment");if(!center||center->arr.size()!=3||!segment)return;
    auto matrices=displayedSourceTransforms(selBody);size_t span=(size_t)segment->num;if(span>=matrices.size())return;
    Vec3 point{center->arr[0].num,center->arr[1].num,center->arr[2].num};auto world=g_instanceWorld[selBody]*matrices[span];tTarget=world.transformPoint(point);tDist=100.;
    // Align only the inspection camera; source meshes and their transforms are unchanged.
    if(auto* normal=opening.find("flat_normal");normal&&normal->arr.size()==3){Vec3 n{normal->arr[0].num,normal->arr[1].num,normal->arr[2].num};
        if(n.dot(point-g_reports[g_instancePart[selBody]].bounds.center())<0)n=-n;n=world.transformDir(n).normalized();cameraRoll=0;tYaw=std::atan2(n.x,n.z);tPitch=std::asin(clamp(n.y,-.999,.999));
    }
    g_sourceOpening=index;g_foldPlaying=false;g_status="Inspecting original opening "+std::to_string(index+1)+" / F fits the complete part";
}
static void setOrbitView(double nyaw,double npitch,bool ortho,const char* name){
    cameraRoll=0;tYaw=nyaw;tPitch=clamp(npitch,-K_PI/2,K_PI/2);g_cameraOrtho=ortho;
    if(name)g_status=std::string(name)+(ortho?" view / orthographic":" view / perspective");
}
static void beginSketchDrawing(int tool,bool choose);
static void cancelSketchDrawing();
static void cancelDirectAuthoring();
static void beginSketchEditing();
static void cancelSketchEditing();
static void refreshSketchEditing();
static void sketchDrawingDimensionDialog();
static void sourceParameterDialog();
static void editViewportSketchDimension();
static void activateSelectionMode(int mode);
#include "datum_authoring.inl"
#include "skeleton_editor.inl"
static bool handleDirectAuthoringAction(int id);
static bool handleSolidDimensionAction(int id);
static void helixDialog();
static void loftControlDialog();
static void selectTangentFaces();
static void collisionCheckDialog();
static bool viewportDockAction(int);
static void uiAction(int id){
    if(viewportDockAction(id))return;
    if(id==860){cancelDirectAuthoring();cancelSketchEditing();cancelSketchDrawing();cancelComponentMove();beginDrawingViewport();return;}if(drawingViewportCommand(id))return;
    if(specializedPatternAuthoringAction(id))return;
    if(id>=800&&id<=816){switch(id){
        case 800:case 801:advancedSheetMiterDialog(id==801);break;
        case 802:removeAdvancedSheetDialog("miters","sheet.miter","Remove miter");break;
        case 803:case 804:advancedSheetSweptDialog(id==804);break;
        case 805:removeAdvancedSheetDialog("swept_flanges","sheet.swept_flange","Remove contour flange");break;
        case 806:case 807:advancedSheetCornerDialog(id==807);break;
        case 808:removeAdvancedSheetDialog("corners","sheet.corner","Remove corner operation");break;
        case 809:case 810:advancedSheetRipDialog(id==810);break;
        case 811:removeAdvancedSheetDialog("rips","sheet.rip","Remove edge rip");break;
        case 812:case 813:advancedSheetTabDialog(id==813);break;
        case 814:removeAdvancedSheetDialog("tabs","sheet.tab","Remove tab and slot");break;
        case 815:advancedSheetBendTableDialog();break;case 816:removeAdvancedSheetBendTable();break;
    }return;}
    if(id>=950&&id<=953){planeAuthoringDialog024(id);return;}
    if(id==940||id==944){stampDialog024(id==944);return;}if(id==941){faceAppearanceDialog024();return;}if(id==942){splitLineDialog024();return;}if(id==943){removeStampDialog024();return;}
    if(handleAssistantImageAction(id)||handleSolidDimensionAction(id)||handleDirectAuthoringAction(id))return;
    if(id==710){openSkeletonEditor();return;}
    if(id==750){helixDialog();return;}if(id==752){loftControlDialog();return;}if(id==658){selectTangentFaces();return;}
    if(id==755){collisionCheckDialog();return;}
    if(handleDatumAuthoringAction(id))return;
    if(sketchRecipeAction(id)){refreshSketchEditing();return;}
    if(id==700){spatialConstraintsDialog();return;}
    if(id==567){showProjectActivity();return;}
    if(id==565){askAgentAboutBody(selBody);return;}if(id==566){g_pendingObjectTarget.reset();g_error=false;g_status="Object target removed from the next message";return;}
    if(id==562){armComponentMove();return;}if(id==563){applyComponentMove();return;}if(id==564){cancelComponentMove();g_status="Component movement cancelled / drawing unchanged";return;}
    if(id==542){renameSketchEntity();return;}
    if(id==549){tubeEndFitDialog();return;}
    if(id>=543&&id<=546){basicCurveSketchDialog(id-543);return;}if(id==547||id==548){spatialArcRecipeDialog(id==548);return;}
    if(id==536||id==537){projectGeometryDialog(id==537);return;}if(id==538){spatialCurveDialog();return;}if(id==539){applyProjectionPreview();return;}if(id==540){clearProjectionPreview();rebuildDisplayBounds();g_status="Projection preview cancelled / drawing unchanged";return;}
    if(id==597){referenceJumpDialog();return;}if(id==591){curveDimensionDialog();return;}if(id==592){equationCurveDialog();return;}if(id==593){offsetSketchDialog();return;}if(id==594){offsetSolidDialog();return;}if(id==595){surfaceProjectionDialog();return;}if(id==596){trimSurfaceWireDialog();return;}
    if(id==541){measuredActivityDetails();return;}
    if(id==44015){std::string error;if(!open_update_manager(error))throw std::runtime_error(error);g_status="ARK update manager opened. Save your drawing before restarting to activate an update.";return;}
    if(id==44014){
        if(g_teamStarted&&g_agentTeam.snapshot().running){g_status="Finish or cancel active agents before changing workflow.";return;}
        if(g_teamStarted){cancelTeam();g_teamStarted=false;resetTeamView();}
        g_teamFocusedWorkflow=!g_teamFocusedWorkflow;if(g_teamFocusedWorkflow)g_teamRoleCount=3;g_teamTarget.clear();g_teamPage=0;layout();if(!g_headless&&!g_dialogSmoke)saveUiSettings();
        g_status=g_teamFocusedWorkflow?"Focused team: Engineer coordinates one active role at a time; Designer builds and Reviewer checks.":"Parallel team: independent assigned tasks can run concurrently.";return;
    }
    if(id==496||id==497){frameEndpointDialog(id==497);return;}if(id==498){frameWeldDialog();return;}
    if(id==494||id==495){sheetInternalBendDialog(id==495);return;}
    if(id==493){iterationHistoryDialog();return;}
    if(id==861){showDrawingWorkspace();return;}
    if(id==491||id==492){surfaceEditDialog(id==492);return;}
    if(id==488){pricingDetails();return;}
    if(id==489){refreshPricing();g_status="Refreshing public OpenAI pricing and credit rates.";return;}
    if(id==490){if(!g_teamStarted){g_status="Start a team before resuming interrupted work.";return;}if(g_agentTeam.resume(g_teamTarget)){g_status="Retained team resumed; current document inspection is required before writing.";pollTeam(true);}else g_status="No yielded or interrupted role can continue for this recipient.";return;}
    if(id==480){g_toolsOpen=!g_toolsOpen;layout();if(g_toolsOpen&&g_toolSearch)SetFocus(g_toolSearch);return;}
    if(id==484){librarySearchDialog();return;}if(id==485){ingestLibraryStep();return;}
    if(id==481||id==482){g_toolScroll=std::max(0,g_toolScroll+(id==481?-8:8));return;}
    if(id==486||id==487){g_toolCategoryScroll=std::max(0,g_toolCategoryScroll+(id==486?-8:8));return;}
    if(id==483){g_toolFilter.clear();g_toolScroll=0;if(g_toolSearch)SetWindowTextW(g_toolSearch,L"");return;}
    if(id==44013){
        if(!g_headless){auto opened=ShellExecuteW(g_window,L"open",L"https://chatgpt.com/settings/usage",nullptr,nullptr,SW_SHOWNORMAL);if((INT_PTR)opened<=32)throw std::runtime_error("Could not open ChatGPT usage settings. Open https://chatgpt.com/settings/usage in your browser.");}
        g_status="ChatGPT Settings > Usage: enable credits for other apps if desired, save changes, then Resume after error.";return;
    }
    if(id>=44020&&id<44028){teamConnectorDialog((size_t)(id-44020));return;}
    if(id>=44000&&id<44008){teamRoleDialog((size_t)(id-44000));return;}
    if(id==44011||id==44012){unsigned pages=(g_teamRoleCount+teamCardColumns()*teamCardRows()-1)/(teamCardColumns()*teamCardRows());g_teamPage=(g_teamPage+(id==44012?1:pages-1))%pages;return;}
    if(id==44010){
        if(g_teamFocusedWorkflow){g_status="Focused workflow uses Engineer, Designer and Reviewer. Choose Parallel to add part designers.";return;}
        if(g_teamStarted&&g_agentTeam.snapshot().running){g_status="Finish or cancel active agents before changing team size.";return;}
        HMENU menu=CreatePopupMenu();for(unsigned n=3;n<=8;++n){std::string label=std::to_string(n)+" agents / "+std::to_string(n-2)+" part designers";AppendMenuA(menu,MF_STRING|(n==g_teamRoleCount?MF_CHECKED:0),n,label.c_str());}
        POINT point;GetCursorPos(&point);int count=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,g_window,nullptr);DestroyMenu(menu);
        if(count>=3&&count<=8){if(g_teamStarted){cancelTeam();g_teamStarted=false;resetTeamView();}g_teamRoleCount=count;g_teamPage=0;g_teamTarget.clear();saveUiSettings();layout();}return;
    }
    if(id>=40000&&id<40100){g_toolCategory=id-40001;g_toolScroll=0;return;}
    if(id>=41000&&id<42000){g_toolsOpen=false;layout();openStudioApiTool((size_t)(id-41000));return;}
    if(id>=42000&&id<43024){size_t index=(size_t)(id-42000);const auto& tools=studioToolEntries();if(index<tools.size()){g_toolsOpen=false;layout();interactiveAction(tools[index].action);}return;}
    if(id>=22000&&id<26096){size_t index=(size_t)(id-22000);if(index<g_bodyIds.size()){selBody=(int)index;g_explorerFeature=0;setBodyHidden(!(index<g_hiddenBodies.size()&&g_hiddenBodies[index]));}return;}
    using V=arkjson::Value;
    if(id==400){g_assistantDockOpen=!g_assistantDockOpen;layout();if(!g_dialogSmoke&&!g_headless)saveUiSettings();return;}
    if(id==401){g_sidebarTab=g_sidebarTab==2?0:2;g_physicalVisible=false;layout();return;}
    if(id>=410&&id<=412){g_sidebarTab=id-410;g_physicalVisible=false;layout();return;}
    if(id==450){g_searchEditing=true;layout();if(g_commandSearch){SetFocus(g_commandSearch);SendMessage(g_commandSearch,EM_SETSEL,0,-1);}else commandPalette();return;}
    if(id==451){mechanicalRelationDialog();return;}if(id==452){motionDriverDialog();return;}
    if(id==453){if(!g_motionActive){motionDriverDialog();if(!g_motionActive)return;}g_motionPlaying=!g_motionPlaying;return;}
    if(id==454){g_motionPlaying=false;g_motionActive=false;g_motionTime=0;g_instanceWorld=g_formedInstanceWorld;rebuildDisplayBounds();refreshSelectionLines();return;}
    if(id==455){moduleCommands(3);return;}if(id==456){moduleCommands(0);return;}if(id==457){moduleCommands(1);return;}if(id==458||id==459){moduleCommands(2);return;}
    if(id==461){g_compactNavigation=!g_compactNavigation;return;}
    if(id==460){static bool fullscreen=false;static WINDOWPLACEMENT placement{sizeof placement};static LONG_PTR style=0;if(!fullscreen){GetWindowPlacement(g_window,&placement);style=GetWindowLongPtr(g_window,GWL_STYLE);MONITORINFO monitor{sizeof monitor};GetMonitorInfo(MonitorFromWindow(g_window,MONITOR_DEFAULTTONEAREST),&monitor);SetWindowLongPtr(g_window,GWL_STYLE,style&~WS_OVERLAPPEDWINDOW);SetWindowPos(g_window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED);}else{SetWindowLongPtr(g_window,GWL_STYLE,style);SetWindowPlacement(g_window,&placement);SetWindowPos(g_window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);}fullscreen=!fullscreen;return;}
    if(id==462){wireframe=false;g_showEdges=false;return;}if(id==463){wireframe=false;g_showEdges=true;return;}if(id==464){wireframe=true;return;}if(id==465){g_showViewCube=true;g_status="View cube shown at the upper left of the viewport.";return;}
    if(id==403){g_parametersOpen=!g_parametersOpen;return;}
    if(id==466){g_manufacturingOpen=!g_manufacturingOpen;return;}
    if(id==468){sheetOpeningDialog(false,true);return;}
    if(id==469){transmissionParametersDialog();return;}
    if(id>=28000&&id<29024){size_t index=(size_t)(id-28000);if(index<g_treeFoldIds.size()){u64 feature=g_treeFoldIds[index];if(g_collapsedFeatures.count(feature))g_collapsedFeatures.erase(feature);else g_collapsedFeatures.insert(feature);}return;}
    if(id>=30000&&id<31024){size_t index=(size_t)(id-30000);if(index<g_doc.assembly.size()&&!g_doc.assembly[index].feature){u64 group=g_doc.assembly[index].id;clearDatumSelection();g_viewportContainer=group;selBody=-1;g_explorerFeature=0;g_picked.clear();refreshSelectionLines();if(g_closedAssemblyGroups.count(group))g_closedAssemblyGroups.erase(group);else g_closedAssemblyGroups.insert(group);}return;}
    if(id>=18000&&id<22000){clearDatumSelection();size_t index=(size_t)(id-18000);if(index<g_explorerEntities.size()){
        g_selectedExplorerEntity=g_explorerEntities[index];g_explorerFeature=g_selectedExplorerEntity->feature;selBody=-1;for(size_t i=0;i<g_bodyIds.size();++i)if(auto* f=g_doc.find(g_bodyIds[i]);f&&featureManufacturingLineage(g_doc,*f).origin==g_explorerFeature){selBody=(int)i;break;}
        g_activeSketch.clear();g_showSketches=false;g_status="Sheet "+g_selectedExplorerEntity->key+" / "+g_selectedExplorerEntity->id+" / double-click to edit";
    }return;}
    if(id>=390&&id<=393){g_explorerOpen[id-390]=!g_explorerOpen[id-390];g_treeScroll=0;return;}
    if(id>=16000&&id<17024){clearDatumSelection();g_selectedExplorerEntity.reset();size_t index=(size_t)(id-16000);if(index<g_doc.features.size()){
        g_explorerFeature=g_doc.features[index].id;selBody=-1;
        for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==g_explorerFeature){selBody=(int)i;break;}
        g_activeSketch.clear();g_showSketches=false;rebuildSketchOverlay();g_status="Feature #"+std::to_string(g_explorerFeature)+" / double-click to edit definition";
    }return;}
    if(id>=14000&&id<15000){selectDatumTree((size_t)(id-14000));return;}
    if(id==97){g_matePage=std::max(0,g_matePage-1);return;}
    if(id==98){++g_matePage;return;}
    if(id>=13000&&id<14000){int index=id-13000;if(index<(int)g_doc.mates.size())g_selectedMate=index;return;}
    if(id>=12000&&id<13000){int index=id-12000;if(auto* named=editableSketch();named&&index<(int)named->sk.cons.size()){g_selectedSketchConstraint=index;rebuildSketchOverlay();}return;}
    if(id>=11000&&id<12000){clearDatumSelection();g_explorerFeature=0;g_selectedExplorerEntity.reset();size_t index=id-11000;if(index<g_doc.sketches.size()){g_activeSketch=g_doc.sketches[index].name;g_activePlane=g_doc.sketches[index].sk.plane;g_selectedSketchConstraint=-1;g_sketchConstraintPage=0;g_showSketches=true;rebuildSketchOverlay();alignSketchCamera();if(g_pickKind==3){SelectionItem item;item.kind="curve";item.owner=g_activeSketch;item.name=g_activeSketch;toggleSelection(item);}}return;}
    if(id>=10000){focusSourceOpening(id-10000);return;}
    if(id>=9000){g_selectedNotch=id-9000;return;}
    if(id>=500&&id<532){size_t index=(size_t)(id-500);if(index>=g_authStatus.accounts.size()||g_authBusy)return;
        std::string clientId=g_authStatus.accounts[index].clientId;g_authStatus=chatGptSelectAccount(clientId);g_authModels.clear();clearPendingRoleSettings();
        g_assistantConfig.authMode=AssistantAuthMode::ApiKey;g_assistantConfig.model.clear();*g_assistantCancel=true;++g_documentEpoch;g_conversation=V::makeArr();saveUiSettings();
        if(g_authStatus.connected&&g_authStatus.planAuthorized)startAuthJob(AuthJob::Models);else startAuthJob(AuthJob::SignIn,clientId);return;}
    if(id>=2000){clearDatumSelection();g_selectedExplorerEntity.reset();g_explorerFeature=0;selBody=id-2000;g_activeSketch.clear();g_showSketches=false;rebuildSketchOverlay();g_selectedNotch=0;g_notchScroll=0;g_sourceScroll=0;g_foldPlaying=false;return;}
    if(id>=1000){size_t i=id-1000;if(i>=g_doc.params.size())return;std::string value=g_doc.params[i].expr,name=g_doc.params[i].name;
        if(editText(("Edit parameter: "+name).c_str(),value)){V c=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(name));c.set("expr",V::makeStr(value));applyCommand(c);}return;}
    if(containerWorkspaceAction(id))return;
    switch(id){
        case 910:importCadDialog();break;
        case 911:arkitfySelectedCad();break;
        case 920:holesOrchestratorDialog();break;
        case 921:holesPositionsDialog();break;
        case 40:laserStockDialog(true);break;
        case 41:laserStockDialog(false);break;
        case 42:notchPointDialog(true);break;
        case 43:notchPointDialog(false);break;
        case 44:if(auto* f=selectedNotchedTube()){
            if(g_selectedNotch<0||g_selectedNotch>=(int)f->notches.size())break;
            V c=notchFeatureEdit(*f),points=*c.find("notches");points.arr.erase(points.arr.begin()+g_selectedNotch);c.set("notches",points);applyCommand(c);g_foldProgress=0;frameScene();
        }break;
        case 45:g_foldProgress=0;g_foldPlaying=false;frameScene();break;
        case 46:g_foldProgress=1;g_foldPlaying=false;frameScene();break;
        case 47:if(selectedFoldable()){if(g_foldProgress>=1)g_foldProgress=0;g_foldPlaying=!g_foldPlaying;if(g_foldPlaying)frameScene(true);}break;
        case 48:g_foldPlaying=false;g_foldProgress=std::max(0.,g_foldProgress-.1);break;
        case 49:g_foldPlaying=false;g_foldProgress=std::min(1.,g_foldProgress+.1);break;
        case 51:exportStraightStep();break;
        case 52:importSourceStep();break;
        case 590:showBomPanel();break;case 585:beginMeasurement(2);break;case 586:beginMeasurement(3);break;case 587:unitsDialog();break;case 588:measurementCoordinates();break;case 589:endMeasurement();break;
        case 55:newDocument();break;case 56:graphicsDialog();break;case 57:showAssistantAccountMenu();break;case 58:liveConnectionDialog();break;case 59:commandPalette();break;case 187:materialDialog();break;case 188:physicalProperties();break;case 189:g_physicalVisible=false;break;case 190:physicalProperties(!g_physicalAssembly);break;case 216:physicsSettingsDialog();break;
        case 560:{if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a part for transparency");auto* feature=g_doc.find(g_bodyIds[(size_t)selBody]);setSelectedOpacity(feature&&resolvedAppearance(g_doc,*feature).opacity<.999?1.:.35);break;}
        case 561:selectedOpacityDialog();break;
        case 368:connectorDialog(true);break;case 357:connectorDialog();break;case 358:g_assistantConfig.authMode=AssistantAuthMode::ApiKey;g_assistantConfig.model.clear();g_assistantConfig.reasoningEffort.clear();*g_assistantCancel=true;++g_documentEpoch;g_conversation=V::makeArr();saveUiSettings();g_status="API key mode selected. Configure model in Advanced settings.";break;
        case 360:startAuthJob(AuthJob::SignIn);break;case 361:chooseChatGptModel();break;case 362:startAuthJob(AuthJob::SignOut);break;
        case 364:if(g_authStatus.activeClientId.empty())throw std::runtime_error("Choose a saved ChatGPT account first");startAuthJob(AuthJob::SignIn,g_authStatus.activeClientId,true);break;
        case 365:if(g_authStatus.pendingClientId.empty())throw std::runtime_error("No pending ChatGPT registration to retry");startAuthJob(AuthJob::SignIn,g_authStatus.pendingClientId);break;
        case 366:cancelAuthJob();break;
        case 367:chooseReasoningEffort();break;
        case 300:case 301:case 302:case 303:case 304:case 305:case 306:g_toolModule=id-300;break;
        case 210:createConstructionFrame();break;case 211:createPcb();break;case 212:createCableRoute();break;case 213:exportModelDialog();break;case 215:createSuspensionDialog();break;
        case 383:if(!selectedSuspension())throw std::runtime_error("Select a suspension to animate");g_suspSpinPlaying=!g_suspSpinPlaying;g_suspTick=0;g_status=g_suspSpinPlaying?"Kinematic corner preview playing / document unchanged":"Kinematic corner preview paused";break;
        case 384:{if(!selectedSuspension()){auto found=std::find_if(g_bodyIds.begin(),g_bodyIds.end(),[&](u64 id){auto* feature=g_doc.find(id);return feature&&feature->type==FeatureType::Suspension;});
                if(found==g_bodyIds.end())throw std::runtime_error("No visible suspension corner in this document");selBody=(int)std::distance(g_bodyIds.begin(),found);}
            g_toolModule=1;if(!g_spotlightActive||g_spotlightBody!=selBody)spotlightBody();g_suspSpinPlaying=true;g_suspTick=0;g_suspClock=0;
            g_status="Spring unit / suspension motion controls / document unchanged";break;}
        case 320:allSheetPose(true);break;case 321:allSheetPose(false);break;
        case 322:toggleAllSheetPlayback();break;case 323:g_allSheetsPlaying=false;g_status="Unbend preview paused / document unchanged";break;
        case 324:if(!g_allSheetsFlat)throw std::runtime_error("Open the unfolded sheet layout first");g_flatShowChassis=!g_flatShowChassis;rebuildDisplayBounds();frameScene();g_status=g_flatShowChassis?"Unfolded sheets with chassis / document unchanged":"Unfolded sheets isolated / document unchanged";break;
        case 325:sheetEditMenu();break;case 327:exportFlatDxf();break;case 330:usageDrawer();break;
        case 340:setBodyHidden(true);break;case 341:setBodyHidden(false);break;case 342:spotlightBody();break;
        case 343:restoreSpotlight();break;case 344:showAllBodies();break;
        case 326:toggleStockPreview();break;
        case 610:beginSketchEditing();break;
        case 611:cancelSketchEditing();break;
        case 570:cancelSketchEditing();beginSketchDrawing(0,true);break;
        case 571:case 572:case 573:case 574:case 575:cancelSketchEditing();beginSketchDrawing(id-571,false);break;
        case 576:sketchDrawingDimensionDialog();break;
        case 577:sourceParameterDialog();refreshSketchEditing();break;
        case 578:cancelSketchDrawing();break;
        case 579:g_grid=!g_grid;break;
        case 580:failureOperationDialog();break;
        case 581:failureOperationDialog(1);break;
        case 582:failureOperationDialog(2);break;
        case 584:editViewportSketchDimension();break;
        case 60:planeDialog();break;case 61:shapeSketchDialog(0);break;case 62:shapeSketchDialog(1);break;case 63:shapeSketchDialog(2);break;case 64:editSketchDialog();break;case 65:sketchFeatureDialog(false);break;case 66:sketchFeatureDialog(true);break;case 67:parameterDialog();break;
        case 70:editSketchEntityFields();refreshSketchEditing();break;case 72:g_showDatumPlanes=!g_showDatumPlanes;rebuildSketchOverlay();saveUiSettings();break;case 73:editSketchConstraintFields(true);refreshSketchEditing();break;
        case 89:editSketchConstraintFields(false);break;case 90:removeSketchConstraint();break;
        case 91:g_sketchConstraintPage=std::max(0,g_sketchConstraintPage-1);break;
        case 92:if(auto* named=editableSketch())g_sketchConstraintPage=std::min((int)(named->sk.cons.size()/4),g_sketchConstraintPage+1);break;
        case 100:nativePrimitiveDialog();break;case 101:nativeExtrudeDialog();break;case 102:nativeBooleanDialog();break;
        case 103:nativeHoleDialog();break;case 104:nativeEdgeBlendDialog();break;case 105:nativeTransformDialog();break;case 106:nativeDraftDialog();break;case 107:nativePatternDialog();break;case 108:nativeMirrorDialog();break;case 113:supportedDatumDialog();break;
        case 68:g_showSketches=!g_showSketches;rebuildSketchOverlay();break;
        case 69:{std::string available="XY, XZ, YZ";for(auto& plane:g_doc.planes)available+=", "+plane.name;std::vector<EditorField> fields{{"Plane name",g_activePlane}};if(editFields("Select construction plane",available.c_str(),fields)){resolvePlane(g_doc,fields[0].value);g_activePlane=fields[0].value;g_activeSketch.clear();g_showSketches=true;rebuildSketchOverlay();}}break;
        case 71:alignSketchCamera();break;
        case 74:if(g_teamMode)sendTeam();else sendAssistant();break;case 75:clearPendingAssistantImage();if(g_teamMode)cancelTeam();else {*g_assistantCancel=true;if(g_assistantSteering)g_assistantSteering->cancel();g_deferredAssistantSteering.clear();}break;
        case 470:case 471:if(g_chatBusy||(g_teamStarted&&g_agentTeam.snapshot().running)){g_status="Finish or cancel the active run before switching modes.";break;}if(id==470&&g_teamStarted){cancelTeam();g_teamStarted=false;resetTeamView();}g_teamMode=id==471;g_assistantDockOpen=true;layout();saveUiSettings();break;
        case 472:if(g_teamStarted){pollTeam(true);g_agentTeam.pause(!g_teamSnapshot.paused);pollTeam(true);g_status=g_teamSnapshot.paused?"Team paused at request/tool boundaries; an active request may finish.":"Team resumed.";}break;
        case 473:chooseTeamTarget();break;
        case 474:case 475:case 476:teamRoleDialog((size_t)(id-474));break;
        case 76:loftDialog();break;case 77:surfaceFillDialog();break;case 78:nurbsSurfaceDialog();break;case 79:thickenDialog();break;
        case 150:sweepDialog();break;case 151:extrudeSurfaceDialog();break;case 152:revolveSurfaceDialog();break;case 153:offsetSurfaceDialog();break;case 154:sewDialog();break;
        case 155:meshWeldDialog();break;case 156:meshSmoothDialog();break;case 157:meshDecimateDialog();break;case 158:meshOffsetDialog();break;case 159:meshFillDialog();break;case 160:importMeshDialog();break;
        case 161:editSelectedFeature();break;case 162:editDrivingSketch();break;
        case 170:case 171:case 172:case 173:activateSelectionMode(id-170);break;
        case 174:g_showEdges=!g_showEdges;g_status=g_showEdges?"Visible edges":"Edges hidden";try{saveUiSettings();}catch(...){}break;
        case 175:g_cameraOrtho=!g_cameraOrtho;g_status=g_cameraOrtho?"Orthographic view":"Perspective view";try{saveUiSettings();}catch(...){}break;
        case 176:g_navMode=0;g_status="Free orbit follows middle-button drag in all directions. Alt + middle spins; Shift + middle pans; wheel zooms.";break;
        case 177:g_navMode=1;g_status="Pan. Drag in the view. Shift+drag also pans.";break;
        case 178:g_navMode=2;g_status="Zoom. Drag up or down. The wheel always zooms.";break;
        case 186:{g_occlusionMode=(g_occlusionMode+1)%4;static const char* names[]={"Off","Contact","Ambient","Horizon"};g_status=std::string("Occlusion: ")+names[g_occlusionMode];try{saveUiSettings();}catch(...){}break;}
        case 180:setOrbitView(0,0,true,"Front");break;
        case 181:setOrbitView(3.141592653589793,0,true,"Back");break;
        case 182:setOrbitView(1.5707963267948966,0,true,"Right");break;
        case 183:setOrbitView(-1.5707963267948966,0,true,"Left");break;
        case 184:setOrbitView(0,-1.52,true,"Bottom");break;
        case 80:sheetMetalDialog();break;case 81:importSheetStep();break;case 82:exportNativeStep();break;
        case 83:sheetDisplay(false);break;case 84:sheetDisplay(true);break;
        case 120:createSheetBaseDialog();break;case 127:editSheetBaseDialog();break;
        case 394:sheetFormDialog(false,false);break;case 395:sheetFormDialog(false,true);break;case 396:removeSheetFormDialog(false);break;
        case 397:sheetFormDialog(true,false);break;case 398:sheetFormDialog(true,true);break;case 399:removeSheetFormDialog(true);break;
        case 121:sheetFlangeDialog(false);break;case 122:sheetFlangeDialog(true);break;case 123:removeSheetFlangeDialog();break;
        case 124:sheetOpeningDialog(false);break;case 125:sheetOpeningDialog(true);break;case 126:removeSheetOpeningDialog();break;
        case 128:toggleSheetPlayback();break;
        case 1:addFeat(FeatureType::Box,{{"dx","24"},{"dy","24"},{"dz","16"}});break;
        case 2:addFeat(FeatureType::Cylinder,{{"r","12"},{"h","28"}});break;
        case 3:addFeat(FeatureType::Sphere,{{"r","14"}});break;
        case 4:addFeat(FeatureType::Cone,{{"rb","12"},{"rt","2"},{"h","26"}});break;
        case 5:addFeat(FeatureType::Torus,{{"R","16"},{"r","5"}});break;
        case 6:addFeat(FeatureType::Extrude,{{"shape","polygon"},{"n","6"},{"r","18"},{"height","24"}});break;
        case 7:addFeat(FeatureType::Revolve,{{"shape","rect"},{"w","8"},{"d","18"},{"cx","16"},{"angle","300"}});break;
        case 8:case 9:case 10:
            namedBooleanDialog(id-8);break;
        case 11:if(!g_bodyIds.empty())addFeat(FeatureType::LinearPattern,{{"dx","100"},{"count","3"}},{g_bodyIds.back()});break;
        case 12:fileAction(true,true);break;
        case 14:wireframe=!wireframe;break;
        case 16:fileAction(true);break;
        case 17:fileAction(false);break;
        case 18:runTool("doc_undo",V::makeObj());refreshSketchEditing();break;
        case 19:runTool("doc_redo",V::makeObj());refreshSketchEditing();break;
        case 20:frameScene();break;
        case 21:{
            std::string text=R"({"tool":"doc_apply","arguments":{"commands":[{"op":"param.set","name":"waves","expr":"9"}]}})";
            if(editText("ARK command / JSON tool + arguments / Ctrl+Enter to apply",text,true)){
                V c;if(!arkjson::parse(text,c)||!c.find("tool")||!c.find("arguments")){g_status="Expected JSON with tool and arguments.";g_error=true;}
                else runTool(c.find("tool")->asStr(),*c.find("arguments"));
            }break;
        }
        case 22:g_grid=!g_grid;break;
        case 23:setOrbitView(0,1.52,true,"Top");break;
        case 24:setOrbitView(.85,.65,false,"Isometric");break;
        case 191:setOrbitView(.85,.65,false,"Home");frameScene();break;
        case 25:if(selBody>=0&&(size_t)selBody<g_bodyIds.size()){V c=V::makeObj();c.set("op",V::makeStr(g_instanceIds[selBody]?"assembly.remove":"feature.remove"));c.set("id",V::makeNum((double)(g_instanceIds[selBody]?g_instanceIds[selBody]:g_bodyIds[selBody])));applyCommand(c);}break;
        case 26:{
            V c,p,list,b;arkjson::parse(R"({"op":"feature.add","type":"tube","name":"Bent tube","p":{"od":"30","wall":"2","tail":"180"},"bends":[{"feed":"180","radius":"60","angle":"90","roll":"0"}]})",c);applyCommand(c);frameScene();break;
        }
        case 27:{
            if(!g_doc.assembly.empty()){g_status="Assembly active. Duplicate or edit an instance placement.";break;}
            V a=V::makeObj(),commands=V::makeArr(),group=V::makeObj();group.set("op",V::makeStr("assembly.add"));group.set("name",V::makeStr("Assembly"));commands.arr.push_back(group);u64 root=g_doc.nextAssemblyId_;
            for(size_t i=0;i<g_bodyIds.size();++i){V c=V::makeObj();c.set("op",V::makeStr("assembly.add"));c.set("parent",V::makeNum((double)root));c.set("feature",V::makeNum((double)g_bodyIds[i]));c.set("name",V::makeStr(g_instanceNames[i]));commands.arr.push_back(c);}
            a.set("commands",commands);runTool("doc_apply",a);break;
        }
        case 28:{
            if(selBody<0)break;
            if(g_doc.assembly.empty()){g_status="Create an assembly first.";break;}
            auto source=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==g_instanceIds[selBody];});if(source==g_doc.assembly.end())break;
            V c=V::makeObj(),p=V::makeObj();c.set("op",V::makeStr("assembly.add"));c.set("parent",V::makeNum((double)source->parent));c.set("feature",V::makeNum((double)source->feature));c.set("name",V::makeStr(source->name+" copy"));for(auto& kv:source->p)p.set(kv.first,V::makeStr(kv.second));p.set("tz",V::makeStr("("+(source->p.count("tz")?source->p.at("tz"):"0")+")+100"));c.set("p",p);applyCommand(c);frameScene();break;
        }
        case 29:{
            if(selBody>=0&&!g_instanceIds[selBody]&&g_tubeInfo.count(g_bodyIds[selBody])){uiAction(30);break;}
            if(selBody<0||!g_instanceIds[selBody]){g_status="Select an assembly instance to edit its placement.";break;}
            auto source=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==g_instanceIds[selBody];});
            V p=V::makeObj();std::vector<EditorField> fields;
            for(auto key:{"tx","ty","tz","rx","ry","rz"})fields.push_back({key,source->p.count(key)?source->p.at(key):"0"});
            u64 nodeId=source->id;
            if(editFields("Instance placement","Translations tx / ty / tz use mm. Rotations rx / ry / rz use degrees.",fields)){
                for(auto& field:fields)p.set(field.name,V::makeStr(field.value));V c=V::makeObj();c.set("op",V::makeStr("assembly.edit"));c.set("id",V::makeNum((double)nodeId));c.set("p",p);applyCommand(c);
            }break;
        }
        case 93:{
            if(selBody<0||!g_instanceIds[selBody]){g_status="Select a part instance to ground or unground.";break;}
            u64 node=g_instanceIds[selBody];auto it=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==node;});
            if(it==g_doc.assembly.end())break;
            V c=V::makeObj();c.set("op",V::makeStr("assembly.edit"));c.set("id",V::makeNum((double)node));c.set("fixed",V::makeBool(!it->fixed));applyCommand(c);
            break;
        }
        case 94:case 95:{
            const bool add=id==94;
            if(g_doc.assembly.empty()){g_status="Create an assembly first.";break;}
            const AssemblyMate* current=nullptr;
            if(!add){
                if(g_selectedMate<0||g_selectedMate>=(int)g_doc.mates.size()){g_status="Select a mate in the inspector first.";break;}
                current=&g_doc.mates[(size_t)g_selectedMate];
            }
            u64 a=0,b=0;
            if(current){a=current->a;b=current->b;}
            else if(selBody>=0&&g_instanceIds[selBody]){
                const u64 selected=g_instanceIds[selBody];
                auto chosen=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==selected;});
                auto other=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.feature&&n.id!=selected&&n.parent==chosen->parent;});
                if(other!=g_doc.assembly.end()){
                    a=chosen->fixed?selected:other->id;b=chosen->fixed?other->id:selected;
                }else a=selected;
            }
            std::vector<EditorField> fields{{"Mate type",current?current->type:"coincident",false,{"concentric","coincident","contact","distance","angle","revolute","prismatic","rigid"}},
                {"Instance A #",a?std::to_string(a):""},{"Reference A",current?current->reference_a:"origin"},
                {"Instance B #",b?std::to_string(b):""},{"Reference B",current?current->reference_b:"origin"},
                {"Value / expression",current?current->value:"0"},
                {"Reference A fraction",current?std::to_string(current->fraction_a):"0.5"},
                {"Reference B fraction",current?std::to_string(current->fraction_b):"0.5"},
                {"Min / mm or deg",current&&current->limits.enabled?current->limits.min:""},
                {"Max / mm or deg",current&&current->limits.enabled?current->limits.max:""}};
            if(!editFields(add?"Add assembly mate":"Edit assembly mate",
                "Ground one instance. Revolute rotates; prismatic slides. Optional min/max expressions persist; clear both to remove limits. References: origin/X/Y/Z or exact names.",fields))break;
            auto instanceId=[&](size_t index){double value=formNumber(fields[index].value);
                if(value<1||value>9007199254740990.||std::floor(value)!=value)throw std::runtime_error("Instance ID must be a positive integer");
                return value;};
            V c=V::makeObj();c.set("op",V::makeStr(add?"mate.add":"mate.edit"));
            if(!add)c.set("id",V::makeNum((double)current->id));
            c.set("type",V::makeStr(trimField(fields[0].value)));
            c.set("a",V::makeNum(instanceId(1)));c.set("reference_a",V::makeStr(trimField(fields[2].value)));
            c.set("b",V::makeNum(instanceId(3)));c.set("reference_b",V::makeStr(trimField(fields[4].value)));
            c.set("value",V::makeStr(trimField(fields[5].value)));
            c.set("fraction_a",V::makeNum(formNumber(fields[6].value)));
            c.set("fraction_b",V::makeNum(formNumber(fields[7].value)));
            auto minimum=trimField(fields[8].value),maximum=trimField(fields[9].value);
            if(minimum.empty()!=maximum.empty())throw std::runtime_error("Enter both minimum and maximum, or clear both to remove limits");
            if(minimum.empty())c.set("limits",V{});else{V limits=V::makeObj();limits.set("min",V::makeStr(minimum));limits.set("max",V::makeStr(maximum));c.set("limits",limits);}
            applyCommand(c);if(!g_error&&add)g_selectedMate=(int)g_doc.mates.size()-1;
            break;
        }
        case 96:{
            if(g_selectedMate<0||g_selectedMate>=(int)g_doc.mates.size()){g_status="Select a mate in the inspector first.";break;}
            V c=V::makeObj();c.set("op",V::makeStr("mate.remove"));c.set("id",V::makeNum((double)g_doc.mates[(size_t)g_selectedMate].id));
            applyCommand(c);if(!g_error)g_selectedMate=-1;break;
        }

        case 31:arkstudio::showAbout(g_window);break;
        case 30:{
            if(selBody<0)break;auto* f=g_doc.find(g_bodyIds[selBody]);if(!f||f->type!=FeatureType::Tube)break;
            V edit=V::makeObj(),p=V::makeObj(),bs=V::makeArr();for(auto& kv:f->p)p.set(kv.first,V::makeStr(kv.second));
            for(auto& b:f->bends){V j=V::makeObj();j.set("feed",V::makeStr(b.feed));j.set("radius",V::makeStr(b.radius));j.set("angle",V::makeStr(b.angle));j.set("roll",V::makeStr(b.roll));bs.arr.push_back(j);}edit.set("p",p);edit.set("bends",bs);
            std::string value=edit.dump();u64 id=f->id;
            if(editText("Tube section and bend program / mm and degrees",value,true)){if(!arkjson::parse(value,edit)||edit.t!=V::Obj){g_error=true;g_status="Invalid tube program JSON";break;}edit.set("op",V::makeStr("feature.edit"));edit.set("id",V::makeNum((double)id));applyCommand(edit);}break;
        }

    }
}

// Ribbon property sheets use expressions; programmatic smoke actions stay deterministic.
static void interactiveAction(int id){
    try{
    using V=arkjson::Value;std::vector<EditorField> fields;FeatureType type=FeatureType::Box;
    switch(id){
        case 1:type=FeatureType::Box;fields={{"dx","24"},{"dy","24"},{"dz","16"}};break;
        case 2:type=FeatureType::Cylinder;fields={{"r","12"},{"h","28"}};break;
        case 3:type=FeatureType::Sphere;fields={{"r","14"}};break;
        case 5:type=FeatureType::Torus;fields={{"R","16"},{"r","5"}};break;
        case 6:type=FeatureType::Extrude;fields={{"n","6"},{"r","18"},{"height","24"}};break;
        case 7:type=FeatureType::Revolve;fields={{"w","8"},{"d","18"},{"cx","16"},{"angle","300"}};break;
        default:uiAction(id);return;
    }
    std::string title=std::string("Create ")+featureTypeName(type);
    if(!editFields(title.c_str(),"Dimensions use mm; angles use degrees. Use expressions or named parameters.",fields))return;
    V c=V::makeObj(),p=V::makeObj();c.set("op",V::makeStr("feature.add"));c.set("type",V::makeStr(featureTypeName(type)));
    for(auto& f:fields)p.set(f.name,V::makeStr(f.value));
    if(type==FeatureType::Extrude)p.set("shape",V::makeStr("polygon"));if(type==FeatureType::Revolve)p.set("shape",V::makeStr("rect"));
    c.set("p",p);applyCommand(c);if(!g_error)frameScene();
    }catch(const std::exception& e){g_error=true;g_status=e.what();}
}

// picking -----------------------------------------------------------------------
static Vec3 camEye(){
    Vec3 dw{std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};
    return target+dw*dist;
}
static double g_projectionFar=4000.;
static Mat4 cameraProjection(){
    double aspect=(double)viewW/std::max(viewH,1);
    double extent=g_sceneBounds.valid()?g_sceneBounds.diagonal():0.;
    g_projectionFar=std::max(4000.,dist+extent*3.+100.);
    double nearPlane=std::max(.05,dist*.002);
    if(!g_cameraOrtho)return Mat4::perspective(35*K_PI/180,aspect,nearPlane,g_projectionFar);
    double halfH=std::max(2.0,dist*std::tan(35*K_PI/360.0));
    return Mat4::ortho(-halfH*aspect,halfH*aspect,-halfH,halfH,nearPlane,g_projectionFar);
}
static Mat4 cameraView(){return Mat4::lookAt(camEye(),target,cameraFrame(yaw,pitch,cameraRoll).up);}
struct CameraDrag {bool active=false;int x=0,y=0;Vec3 pivot,offset;};static CameraDrag g_cameraDrag;
static void cameraBasis(double azimuth,double elevation,Vec3& outward,Vec3& right,Vec3& up){auto frame=cameraFrame(azimuth,elevation,cameraRoll);outward=frame.outward;right=frame.right;up=frame.up;}
static void beginCameraDrag(int x,int y){
    // Capture the rendered camera, not an unfinished Fit/named-view target.
    tYaw=yaw;tPitch=pitch;tDist=dist;tTarget=target;g_cameraDrag.active=true;g_cameraDrag.x=x;g_cameraDrag.y=y;
    g_cameraDrag.pivot=g_sceneBounds.valid()?g_sceneBounds.center():g_sketchBounds.valid()?g_sketchBounds.center():target;
    Vec3 outward,right,up;cameraBasis(yaw,pitch,outward,right,up);Vec3 offset=target-g_cameraDrag.pivot;g_cameraDrag.offset={offset.dot(right),offset.dot(up),offset.dot(outward)};
}
static void moveCameraDrag(int x,int y,bool pan,bool zoom,bool roll=false){
    if(!g_cameraDrag.active){beginCameraDrag(x,y);return;}int dx=x-g_cameraDrag.x,dy=y-g_cameraDrag.y;g_cameraDrag.x=x;g_cameraDrag.y=y;if(!dx&&!dy)return;
    Vec3 outward,right,up;cameraBasis(tYaw,tPitch,outward,right,up);
    if(pan){double halfH=g_cameraOrtho?std::max(2.,tDist*std::tan(35*K_PI/360.)):tDist*std::tan(35*K_PI/360.);double scale=2*halfH/std::max(viewH,1);tTarget=tTarget-right*(dx*scale)+up*(dy*scale);Vec3 offset=tTarget-g_cameraDrag.pivot;g_cameraDrag.offset={offset.dot(right),offset.dot(up),offset.dot(outward)};}
    else if(zoom)tDist=clamp(tDist*std::exp((double)dy*.008),2.,1e9);
    else{double radiansPerPixel=2*K_PI/std::max(720,std::min(viewW,viewH));
        auto frame=orbitCameraPixels(cameraFrame(tYaw,tPitch,cameraRoll),dx,dy,radiansPerPixel,roll);cameraAngles(frame,tYaw,tPitch,cameraRoll);
        outward=frame.outward;right=frame.right;up=frame.up;tTarget=g_cameraDrag.pivot+right*g_cameraDrag.offset.x+up*g_cameraDrag.offset.y+outward*g_cameraDrag.offset.z;}
    // Pointer motion stays one-to-one; Fit and named views retain their damping.
    yaw=tYaw;pitch=tPitch;dist=tDist;target=tTarget;
}
static void viewRay(int mx,int my,Vec3& origin,Vec3& direction){
    Vec3 eye=camEye(),forward=(target-eye).normalized();auto frame=cameraFrame(yaw,pitch,cameraRoll);Vec3 right=frame.right,up=frame.up;
    double nx=(double)(mx-viewX)/std::max(viewW,1)*2-1,ny=1-(double)(my-viewY)/std::max(viewH,1)*2;
    if(g_cameraOrtho){double halfH=std::max(2.0,dist*std::tan(35*K_PI/360.0));double halfW=halfH*(double)viewW/std::max(viewH,1);origin=eye+right*(nx*halfW)+up*(ny*halfH);direction=forward;}
    else{double h=std::tan(deg2rad(35)*.5);origin=eye;direction=(forward+right*(nx*h*viewW/std::max(viewH,1))+up*(ny*h)).normalized();}
}
struct SurfaceHit { int body=-1; u32 triangle=0; Vec3 local; bool hit=false;double distance=0; };
static SurfaceHit pickSurface(int mx,int my,std::vector<SurfaceHit>* hits=nullptr){
    SurfaceHit best;if(g_cpuMeshes.empty()||mx<viewX||my<viewY||mx>=viewX+viewW||my>=viewY+viewH)return best;
    Vec3 eye,rd;viewRay(mx,my,eye,rd);
    f64 bt=1e300;
    auto record=[&](int body,u32 triangle,Vec3 local,double distance){SurfaceHit found{body,triangle,local,true,distance};if(hits)hits->push_back(found);if(distance<bt){bt=distance;best=found;}};
    for(size_t bi=0;bi<g_bodyIds.size();++bi){if(!displayInstance(bi))continue;
        if(auto* f=g_doc.find(g_bodyIds[bi]);f&&f->type==FeatureType::SourceTube){
            auto& cache=g_sourceViews.at(f->id);auto matrices=displayedSourceTransforms(bi);double progress=(int)bi==selBody?g_foldProgress:0.;updateSourceHinges(cache,progress);
            auto cast=[&](const Mesh& mesh,const MeshBVH& bvh,const Mat4& world){auto inv=world.inverted();auto hit=bvh.raycast(mesh,{inv.transformPoint(eye),inv.transformDir(rd)});if(hit.hit)record((int)bi,hit.triangle,hit.point,hit.distance);};
            for(size_t k=0;k<cache.preview.rigid.size();++k)cast(cache.preview.rigid[k],cache.rigidBvhs[k],g_instanceWorld[bi]*matrices[k]);
            for(size_t k=0;k<cache.preview.bends.size();++k)cast(progress>0?cache.hinges[k]:cache.flatHinges[k],progress>0?cache.hingeBvhs[k]:cache.flatHingeBvhs[k],g_instanceWorld[bi]*matrices[k]);
            continue;
        }
        auto cached=g_notchViews.find(g_bodyIds[bi]);
        if(cached!=g_notchViews.end()&&g_doc.find(g_bodyIds[bi])->type==FeatureType::NotchedTube){
            auto transforms=displayedNotchTransforms(bi);auto& cache=cached->second;
            for(size_t segment=0;segment<transforms.size();++segment){auto inv=(g_instanceWorld[bi]*transforms[segment]).inverted();
                auto hit=cache.bvhs[segment].raycast(cache.preview.segments[segment],{inv.transformPoint(eye),inv.transformDir(rd)});
                if(hit.hit)record((int)bi,hit.triangle,hit.point,hit.distance);
            }
        }else{size_t part=g_instancePart[bi];Mat4 inv=g_instanceWorld[bi].inverted();
            auto hit=g_bvhs[part].raycast(g_cpuMeshes[part],{inv.transformPoint(eye),inv.transformDir(rd)});
            if(hit.hit)record((int)bi,hit.triangle,hit.point,hit.distance);
        }
    }
    return best;
}
static int pickBody(int mx,int my){return pickSurface(mx,my).body;}
static bool projectScreen(Vec3 p,float& x,float& y){
    Mat4 vp=cameraProjection()*cameraView();
    double w=vp.m[3]*p.x+vp.m[7]*p.y+vp.m[11]*p.z+vp.m[15];if(w<=1e-6)return false;Vec3 clip=vp.transformPoint(p);
    x=viewX+(float)((clip.x/w+1)*viewW*0.5);y=viewY+(float)((1-clip.y/w)*viewH*0.5);return true;
}
static GLuint pBg,pBusy,pSolid,pCube,pGrid,pShadow,pUI,pText,pSketch,pGeom,pAo,pCompose,pEdge,pCanvas,pIcons,pImage,pFeatureEdge,pContour;
#include "sketch_interaction.inl"
#include "sketch_edit.inl"
#include "selection_glow_render.inl"
#include "curve_dimensions.inl"
#include "direct_authoring.inl"
#include "solid_dimensions.inl"
#include "geometry_authoring.inl"
#include "measurement.inl"
static void cancelComponentMove(bool restore){
    g_componentDragIntent={};
    bool armed=g_componentMove.armed;
    if(restore&&g_componentMove.active){if(g_componentMove.revision==g_session.revision&&g_componentMove.epoch==g_documentEpoch&&g_componentMove.baseline.size()==g_instanceWorld.size()){g_instanceWorld=g_componentMove.baseline;rebuildDisplayBounds();refreshSelectionLines();}else rebuildScene();}
    g_componentMove={};if(!restore)g_componentMove.armed=armed;
}
static void prepareComponentMove(int body,bool explicitChoice=true,AssemblyManipulationProgram* prepared=nullptr){
    if(body<0||(size_t)body>=g_instanceIds.size()||!g_instanceIds[(size_t)body])throw std::runtime_error("Select an assembly component with a supported joint");
    if(g_instanceCopies[(size_t)body]!=0)throw std::runtime_error("Pattern copies cannot be driven independently; select the original component");
    if(g_allSheetsFlat||g_stockMode)throw std::runtime_error("Return to the formed assembly before moving a component");
    if(g_motionActive){g_motionActive=false;g_motionPlaying=false;g_instanceWorld=g_formedInstanceWorld;rebuildDisplayBounds();}
    ComponentMove next;next.armed=true;next.driver=g_instanceIds[(size_t)body];next.revision=g_session.revision;next.epoch=g_documentEpoch;next.baseline=g_instanceWorld;std::string error;
    if(prepared)next.program=std::move(*prepared);else if(!buildAssemblyManipulation(g_doc,next.program,error))throw std::runtime_error(error);
    // Compare graph distance across both motion modes before choosing a joint.
    std::unordered_map<u64,int> distance;std::vector<u64> queue{next.program.rigidCluster.at(next.driver)};distance[queue[0]]=0;
    for(size_t i=0;i<queue.size();++i)for(const auto& edge:next.program.constraints){if(isMechanicalMate(edge.mate.type))continue;
        u64 a=next.program.rigidCluster.at(edge.mate.a),b=next.program.rigidCluster.at(edge.mate.b),other=a==queue[i]?b:b==queue[i]?a:0;
        if(other&&!distance.count(other)){distance[other]=distance.at(queue[i])+1;queue.push_back(other);}}
    int nearest=100000;std::vector<AssemblyManipulationJoint> candidates;
    for(const auto& candidate:next.program.joints){AssemblyManipulationJoint resolved;std::string rejected;
        if(!selectAssemblyManipulationJoint(next.program,next.driver,candidate.mate,candidate.mode,resolved,rejected))continue;
        auto relation=std::find_if(next.program.constraints.begin(),next.program.constraints.end(),[&](const AssemblyMotionRelation& edge){return edge.mate.id==candidate.mate;});if(relation==next.program.constraints.end())continue;
        u64 endpoint=std::binary_search(candidate.movingNodes.begin(),candidate.movingNodes.end(),relation->mate.a)?relation->mate.a:relation->mate.b;
        auto depth=distance.find(next.program.rigidCluster.at(endpoint));if(depth==distance.end()||depth->second>nearest)continue;
        if(depth->second<nearest){nearest=depth->second;candidates.clear();}
        auto equivalent=std::find_if(candidates.begin(),candidates.end(),[&](const AssemblyManipulationJoint& item){return item.mode==resolved.mode&&item.movingNodes==resolved.movingNodes&&item.origin.approx(resolved.origin,1e-6)&&item.axis.approx(resolved.axis,1e-8)&&item.limited==resolved.limited&&(!item.limited||(item.min==resolved.min&&item.max==resolved.max&&item.coordinateSign==resolved.coordinateSign&&item.position==resolved.position));});
        if(equivalent==candidates.end())candidates.push_back(resolved);else if(resolved.mate<equivalent->mate)*equivalent=resolved;
    }
    if(candidates.empty()){selectAssemblyManipulationJoint(next.program,next.driver,0,"translate",next.joint,error);throw std::runtime_error(error);}
    if(candidates.size()==1)next.joint=candidates.front();else{
        if(!explicitChoice)throw std::runtime_error("This component has more than one joint motion. Use Move component to choose the motion first.");
        std::vector<std::string> choices;for(const auto& joint:candidates)choices.push_back((joint.mode=="translate"?std::string("Translate"):std::string("Rotate"))+" / joint #"+std::to_string(joint.mate));
        std::vector<EditorField> fields{{"Motion / joint",choices.front(),false,choices}};
        if(!editFields("Choose component motion","More than one nearest joint can move this component. Select the intended motion explicitly.",fields))throw std::runtime_error("Component movement cancelled / no motion selected");
        auto selected=std::find(choices.begin(),choices.end(),fields[0].value);if(selected==choices.end())throw std::runtime_error("Select a supported joint");next.joint=candidates[(size_t)std::distance(choices.begin(),selected)];
        if(next.revision!=g_session.revision||next.epoch!=g_documentEpoch)throw std::runtime_error("The drawing changed while choosing a joint; select the component again");
    }
    g_componentMove=std::move(next);selBody=body;g_pickKind=0;g_picked.clear();refreshSelectionLines();
}
static void armComponentMove(){
    cancelComponentMove();prepareComponentMove(selBody);g_navMode=0;
    g_status="Move component: drag along the selected joint; release saves / Esc cancels / middle mouse orbits";
}
static bool jointPointerRadial(int x,int y,Vec3& radial){
    Vec3 eye,ray;viewRay(x,y,eye,ray);const auto& joint=g_componentMove.joint;double denominator=ray.dot(joint.axis);
    if(std::abs(denominator)<.10)return false;double travel=(joint.origin-eye).dot(joint.axis)/denominator;if(travel<0)return false;
    Vec3 radius=eye+ray*travel-joint.origin;radius=radius-joint.axis*radius.dot(joint.axis);
    if(radius.len()<std::max(1e-6,dist*.0005))return false;radial=radius.normalized();return true;
}
static bool jointPointerCoordinate(int x,int y,double& coordinate,Vec3* world=nullptr){
    Vec3 eye,ray;viewRay(x,y,eye,ray);const auto& move=g_componentMove;
    double denominator=ray.dot(move.dragPlaneNormal);if(std::abs(denominator)<1e-6)return false;
    double travel=(move.joint.origin-eye).dot(move.dragPlaneNormal)/denominator;
    Vec3 intersection=eye+ray*travel;if(world)*world=intersection;coordinate=(intersection-move.joint.origin).dot(move.joint.axis);return std::isfinite(coordinate);
}
static double boundedComponentDelta(const AssemblyManipulationJoint& joint,double delta){
    if(!joint.limited)return delta;
    double a=(joint.min-joint.position)/joint.coordinateSign,b=(joint.max-joint.position)/joint.coordinateSign;
    return std::clamp(delta,std::min(a,b),std::max(a,b));
}
static int pickComponentDragBody(int x,int y,AssemblyManipulationProgram& program,SurfaceHit& selectedHit){
    std::vector<SurfaceHit> hits;selectedHit=pickSurface(x,y,&hits);int nearest=selectedHit.body;if(nearest<0)return nearest;
    std::string error;if(!buildAssemblyManipulation(g_doc,program,error))throw std::runtime_error(error);
    std::sort(hits.begin(),hits.end(),[](const SurfaceHit& a,const SurfaceHit& b){if(a.distance!=b.distance)return a.distance<b.distance;if(a.body!=b.body)return a.body<b.body;return a.triangle<b.triangle;});std::set<int> seen;
    for(const auto& hit:hits){if(!seen.insert(hit.body).second)continue;size_t body=(size_t)hit.body;u64 driver=g_instanceIds[body];bool movable=false;
        if(driver&&g_instanceCopies[body]==0)for(const auto& candidate:program.joints){AssemblyManipulationJoint joint;std::string rejected;
            if(selectAssemblyManipulationJoint(program,driver,candidate.mate,candidate.mode,joint,rejected)){movable=true;break;}}
        if(movable){selectedHit=hit;return hit.body;}
        auto* feature=g_doc.find(g_bodyIds[body]);bool transparent=feature&&resolvedAppearance(g_doc,*feature).opacity<.999;
        if(!transparent){selectedHit=hit;return hit.body;} // An opaque immovable surface still occludes parts behind it.
    }
    return nearest;
}
static void beginComponentDrag(int x,int y,bool direct=false){
    AssemblyManipulationProgram program;SurfaceHit selectedHit;int body=pickComponentDragBody(x,y,program,selectedHit);
    if(body<0)throw std::runtime_error("Click an assembly component to move it");
    if(direct||!g_componentMove.armed||g_componentMove.driver!=g_instanceIds[(size_t)body]||g_componentMove.revision!=g_session.revision||g_componentMove.epoch!=g_documentEpoch)prepareComponentMove(body,!direct,&program);
    g_componentMove.armed=!direct;
    g_componentMove.x=x;g_componentMove.y=y;g_componentMove.delta=0;g_componentMove.pointerAngle=0;
    if(g_componentMove.joint.mode=="translate"){
        Vec3 eye,ray;viewRay(x,y,eye,ray);Vec3 normal=ray-g_componentMove.joint.axis*ray.dot(g_componentMove.joint.axis);
        // Looking down the joint axis supplies no observable screen displacement.
        float ax,ay,bx,by;double span=std::max(1.,dist*.1);
        if(normal.len()<.08||!projectScreen(g_componentMove.joint.origin,ax,ay)||!projectScreen(g_componentMove.joint.origin+g_componentMove.joint.axis*span,bx,by)||std::hypot(bx-ax,by-ay)<4)throw std::runtime_error("Translation axis points into the view. Orbit to see the travel direction before dragging.");
        g_componentMove.dragPlaneNormal=normal.normalized();
        if(!jointPointerCoordinate(x,y,g_componentMove.pointerCoordinate,&g_componentMove.pointerWorld))throw std::runtime_error("The pointer cannot resolve travel along this joint. Orbit and try again.");
    }else if(!jointPointerRadial(x,y,g_componentMove.radial)){Vec3 point=selectedHit.hit?g_instanceWorld[(size_t)body].transformPoint(selectedHit.local):g_reports[g_instancePart[(size_t)body]].bounds.transformed(g_instanceWorld[(size_t)body]).center();Vec3 radius=point-g_componentMove.joint.origin;radius=radius-g_componentMove.joint.axis*radius.dot(g_componentMove.joint.axis);g_componentMove.radial=radius.normalized();}
    g_componentMove.dragging=true;g_componentMove.active=true;
}
static void moveComponentDrag(int x,int y){
    auto& move=g_componentMove;if(!move.dragging)return;if(move.revision!=g_session.revision||move.epoch!=g_documentEpoch){cancelComponentMove();throw std::runtime_error("The drawing changed during movement; drag cancelled");}
    double delta=0;Vec3 radial;
    if(move.joint.mode=="translate"){
        bool atBound=false;
        if(move.joint.limited){double a=(move.joint.min-move.joint.position)/move.joint.coordinateSign,b=(move.joint.max-move.joint.position)/move.joint.coordinateSign;
            float sx,sy,ax,ay,bx,by;double minimum=std::min(a,b),maximum=std::max(a,b);
            if(projectScreen(move.pointerWorld,sx,sy)&&projectScreen(move.pointerWorld+move.joint.axis*minimum,ax,ay)&&projectScreen(move.pointerWorld+move.joint.axis*maximum,bx,by)){
                double dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy;
                if(length>1e-6){double coordinate=((x-ax)*dx+(y-ay)*dy)/length;
                    if(coordinate<=0){delta=minimum;atBound=true;}else if(coordinate>=1){delta=maximum;atBound=true;}}
            }
        }
        if(!atBound){double coordinate;if(!jointPointerCoordinate(x,y,coordinate))throw std::runtime_error("Travel cannot be resolved in this view; drag cancelled");delta=coordinate-move.pointerCoordinate;}
    }else{
        if(move.radial.len()>.5&&jointPointerRadial(x,y,radial))delta=std::atan2(move.joint.axis.dot(move.radial.cross(radial)),move.radial.dot(radial))*180./K_PI;
        else {Vec3 tangent=move.joint.axis.cross(move.radial).normalized();double radius=std::max(dist*.12,g_sceneBounds.valid()?g_sceneBounds.diagonal()*.12:10.);float ax,ay,bx,by;Vec3 point=move.joint.origin+move.radial*radius;
            if(tangent.len()>.5&&projectScreen(point,ax,ay)&&projectScreen(point+tangent*radius,bx,by)&&std::hypot(bx-ax,by-ay)>4){double sx=bx-ax,sy=by-ay;delta=((x-move.x)*sx+(y-move.y)*sy)/(sx*sx+sy*sy)*180./K_PI;}else delta=(x-move.x)*.35;
        }
        // Unwrap against the observed delta before clamping, including intervals crossing 180.
        while(delta-move.pointerAngle>180)delta-=360;while(delta-move.pointerAngle<-180)delta+=360;move.pointerAngle=delta;
    }
    delta=boundedComponentDelta(move.joint,delta);
    AssemblyManipulationPreview preview;std::string error;if(!previewAssemblyManipulation(move.program,move.driver,move.joint.mate,move.joint.mode,delta,preview,error))throw std::runtime_error(error);
    std::vector<Mat4> worlds=move.baseline;for(size_t i=0;i<g_instanceIds.size();++i){auto found=std::find_if(preview.placements.begin(),preview.placements.end(),[&](const AssemblyPlacement& placement){return placement.node==g_instanceIds[i]&&placement.copy==g_instanceCopies[i];});if(found==preview.placements.end())throw std::runtime_error("Joint preview did not return the selected assembly placement");worlds[i]=found->world;}
    g_instanceWorld=std::move(worlds);move.delta=delta;g_dragged=std::abs(x-move.x)+std::abs(y-move.y)>2;rebuildDisplayBounds();refreshSelectionLines();char text[240];
    bool absolute=move.joint.type=="prismatic"||move.joint.type=="revolute";
    std::snprintf(text,sizeof text,"Joint %llu / %s %.2f %s / %zu attached components / release saves / Esc cancels",(unsigned long long)move.joint.mate,absolute?"position":"displacement",absolute?move.joint.position+move.joint.coordinateSign*delta:delta,move.joint.mode=="translate"?"mm":"deg",move.joint.movingNodes.size());g_status=text;g_error=false;
}
static void applyComponentMove(){
    if(!g_componentMove.active)return;auto move=std::move(g_componentMove);g_componentMove={};g_componentMove.armed=move.armed;
    g_instanceWorld=move.baseline;rebuildDisplayBounds();refreshSelectionLines();
    if(move.revision!=g_session.revision||move.epoch!=g_documentEpoch){rebuildScene();throw std::runtime_error("The drawing changed during movement; no placement was saved");}
    if(std::abs(move.delta)<1e-8){g_status="Move component: drag a part / Esc exits";return;}
    using V=arkjson::Value;V command=V::makeObj(),commands=V::makeArr(),args=V::makeObj();command.set("op",V::makeStr("assembly.drive"));command.set("driver",V::makeNum((double)move.driver));command.set("mate",V::makeNum((double)move.joint.mate));command.set("mode",V::makeStr(move.joint.mode));command.set("delta",V::makeNum(move.delta));commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)move.revision));runTool("doc_apply",args);
    if(!g_error)g_status="Component movement saved / attached components follow / Undo restores the previous placement";
}
#include "selection_picking.inl"
#include "selection_hover.inl"

// ---------------------------------------------------------------------------

static GLuint g_drawingVao=0,g_drawingVbo=0;
static void drawManualSketchPreview(){
    auto& drawing=g_sketchDrawing;if(!drawing.active||drawing.stale)return;
    if(!drawingCurrent()){drawing.stale=true;drawing.lines.clear();drawing.fill.clear();g_status="Sketch draft is stale / Esc and restart from current revision";return;}
    if(!g_drawingVao){glGenVertexArrays(1,&g_drawingVao);glGenBuffers(1,&g_drawingVbo);}glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pSketch);
    auto vp=cameraProjection()*cameraView();GLfloat matrix[16];for(int i=0;i<16;++i)matrix[i]=(GLfloat)vp.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,matrix);glBindVertexArray(g_drawingVao);glBindBuffer(GL_ARRAY_BUFFER,g_drawingVbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
    auto draw=[&](const std::vector<float>& vertices,GLenum mode,float alpha,float size){if(vertices.empty())return;glUniform4f(glGetUniformLocation(pSketch,"tint"),.20f,.91f,.98f,alpha);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_DYNAMIC_DRAW);glLineWidth(size);glPointSize(size);glDrawArrays(mode,0,(GLsizei)vertices.size()/3);};
    if(g_grid){
        double spacing=sketchDrawingGridSpacing(),halfH=std::max(2.,dist*std::tan(35*K_PI/360.)),radius=halfH*std::max(1.,(double)viewW/std::max(viewH,1))*2.8;int halfLines=std::clamp((int)std::ceil(radius/spacing),8,110);auto center=drawing.frame.inverted().transformPoint(target);double cx=std::round(center.x/spacing)*spacing,cy=std::round(center.y/spacing)*spacing,span=halfLines*spacing;std::vector<float> minor,major,axes;
        auto segment=[&](Vec2 a,Vec2 b,double coordinate){auto& vertices=std::abs(coordinate)<spacing*.1?axes:std::fmod(std::abs(std::round(coordinate/spacing)),5.)<.1?major:minor;for(auto point:{a,b}){auto world=drawing.frame.transformPoint({point.x,point.y,0});vertices.insert(vertices.end(),{(float)world.x,(float)world.y,(float)world.z});}};
        for(int i=-halfLines;i<=halfLines;++i){double offset=i*spacing;segment({cx-span,cy+offset},{cx+span,cy+offset},cy+offset);segment({cx+offset,cy-span},{cx+offset,cy+span},cx+offset);}draw(minor,GL_LINES,.075f,1);draw(major,GL_LINES,.145f,1);draw(axes,GL_LINES,.31f,1);
    }
    draw(drawing.fill,GL_TRIANGLES,.105f,1);draw(drawing.guides,GL_LINES,.15f,1);draw(drawing.lines,GL_LINES,.10f,7);draw(drawing.lines,GL_LINES,.27f,4);draw(drawing.lines,GL_LINES,.95f,1.6f);
    // Pulse is a visual anchor affordance, not fabricated construction progress.
    float pulse=(float)(.5+.5*std::sin(GetTickCount64()*.004));draw(drawing.points,GL_POINTS,.12f,15+3*pulse);draw(drawing.points,GL_POINTS,.95f,5);glPointSize(1);glLineWidth(1);glViewport(0,0,winW,winH);
}
static void drawDraftSketches(){
    if(g_draftViews.empty()&&(!g_showSketches||g_committedRoleSketches.empty()))return;if(!g_draftVao){glGenVertexArrays(1,&g_draftVao);glGenBuffers(1,&g_draftVbo);}glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pSketch);auto matrix=cameraProjection()*cameraView();float transform[16];for(int i=0;i<16;++i)transform[i]=(float)matrix.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,transform);glBindVertexArray(g_draftVao);glBindBuffer(GL_ARRAY_BUFFER,g_draftVbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);glLineWidth(2.f);
    auto draw=[&](const DraftSketchView& sketch){if(sketch.lines.empty())return;bool designer=sketch.role=="designer";auto tint=[&](float alpha){glUniform4f(glGetUniformLocation(pSketch,"tint"),designer?.58f:.20f,designer?.76f:.91f,1.f,alpha);};auto buffer=[&](const std::vector<float>& vertices,GLenum mode){glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_DYNAMIC_DRAW);glDrawArrays(mode,0,(GLsizei)vertices.size()/3);};if(!sketch.committed){tint(.085f);buffer(sketch.fill,GL_TRIANGLES);glLineWidth(6);tint(.12f);buffer(sketch.lines,GL_LINES);glPointSize((float)(13+2*std::sin(GetTickCount64()*.004)));tint(.18f);buffer(sketch.points,GL_POINTS);glPointSize(5);tint(.95f);buffer(sketch.points,GL_POINTS);}glLineWidth(sketch.committed?2.f:1.6f);tint(sketch.committed?1.f:.86f);buffer(sketch.lines,GL_LINES);};if(g_showSketches)for(const auto& entry:g_committedRoleSketches)draw(entry.second);for(const auto& entry:g_draftViews)draw(entry.second);glPointSize(1);glLineWidth(1.f);glViewport(0,0,winW,winH);
}
static void drawProjectionPreview(){
    if(!g_projectionPreview.active||g_projectionPreview.lines.empty())return;if(!g_projectionVao){glGenVertexArrays(1,&g_projectionVao);glGenBuffers(1,&g_projectionVbo);}glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pSketch);auto matrix=cameraProjection()*cameraView();GLfloat transform[16];for(int i=0;i<16;++i)transform[i]=(GLfloat)matrix.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,transform);glUniform4f(glGetUniformLocation(pSketch,"tint"),.20f,.96f,.86f,.94f);glBindVertexArray(g_projectionVao);glBindBuffer(GL_ARRAY_BUFFER,g_projectionVbo);if(g_projectionGpuDirty){glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(g_projectionPreview.lines.size()*sizeof(float)),g_projectionPreview.lines.data(),GL_DYNAMIC_DRAW);g_projectionGpuDirty=false;}glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);glLineWidth(3.f);glDrawArrays(GL_LINES,0,(GLsizei)g_projectionPreview.lines.size()/3);glLineWidth(1.f);glViewport(0,0,winW,winH);
}
static GLint suVp,suModel,suOutline,suColor,suEye,suSel,suHard;
static GLint guView,guProj,guModel,guHard,auProj,auFocal,auOrtho,auRadius,auBias,cuOrigin,euVp,euModel;
static GLint uuVp,uuRc,uuRad,uuFill,uuBorder,uuBW,uuGlyph;
static GLuint quadVao,gridVao,gridVbo,textVao,textVbo,gFbo=0,gGeomTex=0,gAoTex=0,gDepthTex=0,gCubeVao=0,gCubeVbo=0,gCompassVao=0,gCompassVbo=0;
static void drawAssistantAttachmentThumbnail(const AssistantImageAttachment& attachment,float x,float y,float w,float h){
    if(attachment.thumbnailRGBA.empty()||!attachment.thumbnailWidth||!attachment.thumbnailHeight)return;static GLuint texture=0;static std::vector<unsigned char> uploaded;static unsigned uploadedW=0,uploadedH=0;if(!texture)glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);if(uploaded!=attachment.thumbnailRGBA||uploadedW!=attachment.thumbnailWidth||uploadedH!=attachment.thumbnailHeight){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)attachment.thumbnailWidth,(GLsizei)attachment.thumbnailHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,attachment.thumbnailRGBA.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);uploaded=attachment.thumbnailRGBA;uploadedW=attachment.thumbnailWidth;uploadedH=attachment.thumbnailHeight;}float scale=std::min(w/attachment.thumbnailWidth,h/attachment.thumbnailHeight),width=attachment.thumbnailWidth*scale,height=attachment.thumbnailHeight*scale;glUseProgram(pImage);glUniform2f(glGetUniformLocation(pImage,"vp"),(float)winW,(float)winH);glUniform4f(glGetUniformLocation(pImage,"rc"),x+(w-width)*.5f,y+(h-height)*.5f,width,height);glUniform1i(glGetUniformLocation(pImage,"image"),0);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glUseProgram(pUI);
}
struct CubeTriangle{Vec3 a,b,c,normal;int face=-1;};
struct CubeHit{bool hit=false;int face=-1;Vec3 normal{};};
static std::vector<CubeTriangle> g_cubeTriangles;
static CubeHit g_cubeHover;
static GLuint g_busySnapshotTex=0;
static int gridCount=0,gOccW=0,gOccH=0,gOccTriedW=-1,gOccTriedH=-1;
static bool g_swap=true,gOccReady=false;   // headless readback disables the buffer swap
struct AdaptiveGrid {double spacing=10,radius=200,centerX=0,centerZ=0;int halfLines=20;};static AdaptiveGrid g_adaptiveGrid;
static double niceGridSpacing(double required){
    double decade=std::pow(10.,std::floor(std::log10(std::max(required,1e-8))));
    for(double factor:{1.,2.,5.,10.})if(decade*factor>=required)return decade*factor;
    return decade*10.;
}
static void updateAdaptiveGrid(){
    double halfH=std::max(2.,dist*std::tan(35*K_PI/360.)),halfW=halfH*viewW/std::max(viewH,1);
    double unitsPerPixel=2*halfH/std::max(viewH,1),spacing=niceGridSpacing(unitsPerPixel*42.);
    double radius=std::max(halfH,halfW)*2.8;int halfLines=std::clamp((int)std::ceil(radius/spacing),8,110);
    Vec3 center=target,eye,ray;viewRay(viewX+viewW/2,viewY+viewH/2,eye,ray);
    if(std::abs(ray.y)>.08){double travel=-eye.y/ray.y;if(travel>0&&travel<dist*4)center=eye+ray*travel;}
    double centerX=std::round(center.x/spacing)*spacing,centerZ=std::round(center.z/spacing)*spacing;
    if(g_adaptiveGrid.spacing==spacing&&g_adaptiveGrid.centerX==centerX&&g_adaptiveGrid.centerZ==centerZ&&g_adaptiveGrid.halfLines==halfLines&&gridCount)return;
    g_adaptiveGrid={spacing,halfLines*spacing,centerX,centerZ,halfLines};std::vector<float> lines;
    auto segment=[&](double ax,double az,double bx,double bz,float weight){lines.insert(lines.end(),{(float)ax,0.f,(float)az,weight,(float)bx,0.f,(float)bz,weight});};
    double span=halfLines*spacing;for(int i=-halfLines;i<=halfLines;++i){double offset=i*spacing;
        auto weight=[&](double coordinate){double index=std::round(coordinate/spacing);return std::abs(coordinate)<spacing*.1?1.5f:std::fmod(std::abs(index),5.)<.1?1.f:.42f;};
        segment(-span,offset,span,offset,weight(centerZ+offset));segment(offset,-span,offset,span,weight(centerX+offset));
    }
    gridCount=(int)lines.size()/4;glBindVertexArray(gridVao);glBindBuffer(GL_ARRAY_BUFFER,gridVbo);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(lines.size()*sizeof(float)),lines.data(),GL_DYNAMIC_DRAW);
}
static double sketchDrawingGridSpacing(){return g_adaptiveGrid.spacing;}
static GLuint canvasTexture(const std::string& path);
static GLuint materialTexture(const std::string& path){
    if(path.empty())return 0;
    std::filesystem::path file=std::filesystem::u8path(path);
    if(file.is_relative()&&!g_path.empty())file=std::filesystem::u8path(g_path).parent_path()/file;
    auto utf8=file.u8string();GLuint tex=canvasTexture(std::string(utf8.begin(),utf8.end()));
    return tex==g_canvasMissing?0:tex;
}
#include "stamps_render.inl"
static void drawSurfaces(bool geometry,const Mat4& view,const Mat4& proj,const GLfloat vpT[16],const Vec3& eye,bool transparentOnly=false,bool selectionOverlay=false){
    GLint modelLoc=geometry?guModel:suModel,hardLoc=geometry?guHard:suHard;
    if(geometry){GLfloat vm[16],pm[16];for(int i=0;i<16;++i){vm[i]=(GLfloat)view.m[i];pm[i]=(GLfloat)proj.m[i];}glUseProgram(pGeom);glUniformMatrix4fv(guView,1,GL_FALSE,vm);glUniformMatrix4fv(guProj,1,GL_FALSE,pm);glUniform1f(glGetUniformLocation(pGeom,"depthFar"),(float)g_projectionFar);glUniform1f(glGetUniformLocation(pGeom,"cameraOrtho"),g_cameraOrtho?1.f:0.f);}
    else{glUseProgram(pSolid);glUniform1f(glGetUniformLocation(pSolid,"cameraOrtho"),g_cameraOrtho?1.f:0.f);glUniform1f(glGetUniformLocation(pSolid,"selectionOverlay"),selectionOverlay?1.f:0.f);glUniform1f(glGetUniformLocation(pSolid,"satin"),g_satin?1.f:0.f);glUniformMatrix4fv(suVp,1,GL_FALSE,vpT);glUniform3f(suEye,(float)eye.x,(float)eye.y,(float)eye.z);glUniform1i(glGetUniformLocation(pSolid,"surfaceTexture"),2);
        auto basis=cameraFrame(yaw,pitch,cameraRoll);auto setBasis=[&](const char* name,Vec3 v){glUniform3f(glGetUniformLocation(pSolid,name),(float)v.x,(float)v.y,(float)v.z);};
        setBasis("studioRight",basis.right);setBasis("studioUp",basis.up);setBasis("studioBack",basis.outward);}
    if(!wireframe){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.f,1.f);}else glDisable(GL_POLYGON_OFFSET_FILL);
    auto setModel=[&](const Mat4& world){GLfloat m[16];for(int k=0;k<16;++k)m[k]=(GLfloat)world.m[k];glUniformMatrix4fv(modelLoc,1,GL_FALSE,m);};
    std::vector<size_t> order;
    for(size_t i=0;i<g_bodyIds.size();++i){if(!displayInstance(i))continue;
        const Feature* feature=g_doc.find(g_bodyIds[i]);
        bool transparent=feature&&resolvedAppearance(g_doc,*feature).opacity<.999;
        if(selectionOverlay&&(int)i!=selBody&&(int)i!=hoverBody&&std::none_of(g_picked.begin(),g_picked.end(),[&](const auto& pick){return pick.kind=="face"&&pick.feature==g_bodyIds[i]&&(!pick.instance||(pick.instance==g_instanceIds[i]&&pick.copy==g_instanceCopies[i]));}))continue;
        if((transparentOnly&&transparent)||(!transparentOnly&&!transparent))order.push_back(i);
    }
    if(transparentOnly)std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){
        auto center=[&](size_t i){return g_reports[g_instancePart[i]].bounds.transformed(g_instanceWorld[i]).center();};
        return (center(a)-eye).lensq()>(center(b)-eye).lensq();
    });
    for(size_t i:order){
        if(!geometry){
            glUniform1f(glGetUniformLocation(pSolid,"stampMode"),0);
            const Feature* feature=g_doc.find(g_bodyIds[i]);
            Appearance a=feature?resolvedAppearance(g_doc,*feature):Appearance{};
            glUniform3f(suColor,(float)a.baseColor.x,(float)a.baseColor.y,(float)a.baseColor.z);
            glUniform1f(glGetUniformLocation(pSolid,"metallic"),(float)a.metallic);
            glUniform1f(glGetUniformLocation(pSolid,"roughness"),(float)a.roughness);
            glUniform1f(glGetUniformLocation(pSolid,"opacity"),(float)a.opacity);
            glUniform2f(glGetUniformLocation(pSolid,"textureScale"),(float)(a.textureScaleU/100.),(float)(a.textureScaleV/100.));
            GLuint tex=a.textureAsset.empty()?materialTexture(a.texturePath):stampAssetTexture(a.textureAsset);
            glUniform1f(glGetUniformLocation(pSolid,"hasTexture"),tex?1.f:0.f);
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,tex);glActiveTexture(GL_TEXTURE0);
        }
        if(auto* f=g_doc.find(g_bodyIds[i]);f&&f->type==FeatureType::SourceTube){
            auto& cache=g_sourceViews.at(f->id);auto matrices=displayedSourceTransforms(i);double progress=(int)i==selBody?g_foldProgress:0.;updateSourceHinges(cache,progress);
            glUniform1f(hardLoc,0.f);if(!geometry){glUniform1f(suOutline,0.f);glUniform1f(suSel,(int)i==selBody?1.f:0.f);}glPolygonMode(GL_FRONT_AND_BACK,!geometry&&wireframe?GL_LINE:GL_FILL);
            auto drawPart=[&](const GPUMesh& mesh,const Mat4& world){if(!mesh.count)return;setModel(world);glBindVertexArray(mesh.vao);glDrawElements(GL_TRIANGLES,mesh.count*3,GL_UNSIGNED_INT,nullptr);};
            for(size_t k=0;k<cache.preview.rigid.size();++k)drawPart(cache.rigidGpu[k],g_instanceWorld[i]*matrices[k]);
            for(size_t k=0;k<cache.preview.bends.size();++k)drawPart(progress>0?cache.hingeGpu[k]:cache.flatHingeGpu[k],g_instanceWorld[i]*matrices[k]);
            continue;
        }
        if(auto* f=g_doc.find(g_bodyIds[i]);f&&f->type==FeatureType::NotchedTube){
            auto& cache=g_notchViews.at(f->id);auto transforms=displayedNotchTransforms(i);
            glUniform1f(hardLoc,wireframe?0.f:1.f);if(!geometry){glUniform1f(suOutline,0.f);glUniform1f(suSel,(int)i==selBody?1.f:0.f);}glPolygonMode(GL_FRONT_AND_BACK,!geometry&&wireframe?GL_LINE:GL_FILL);
            for(size_t segment=0;segment<transforms.size();++segment){setModel(g_instanceWorld[i]*transforms[segment]);glBindVertexArray(cache.gpu[segment].vao);glDrawElements(GL_TRIANGLES,cache.gpu[segment].count*3,GL_UNSIGNED_INT,nullptr);}
            continue;
        }
        if(auto* f=g_doc.find(g_bodyIds[i]);f&&f->type==FeatureType::Suspension){
            auto found=g_suspViews.find(f->id);if(found==g_suspViews.end())continue;
            glUniform1f(hardLoc,0.f);glPolygonMode(GL_FRONT_AND_BACK,!geometry&&wireframe?GL_LINE:GL_FILL);
            if(!geometry){glUniform1f(suSel,i==(size_t)selBody?1.f:i==(size_t)hoverBody?.3f:0.f);glUniform1f(suOutline,0.f);
                glUniform1f(glGetUniformLocation(pSolid,"hasTexture"),0.f);glUniform1f(glGetUniformLocation(pSolid,"opacity"),(float)resolvedAppearance(g_doc,*f).opacity);}
            auto& cache=found->second;
            for(size_t part=0;part<cache.preview.parts.size()&&part<cache.gpu.size();++part){
                const auto& item=cache.preview.parts[part];if(!geometry){glUniform3f(suColor,(float)item.color.x,(float)item.color.y,(float)item.color.z);
                    glUniform1f(glGetUniformLocation(pSolid,"metallic"),item.name.find("tyre")!=std::string::npos?0.f:.24f);
                    glUniform1f(glGetUniformLocation(pSolid,"roughness"),item.name.find("tyre")!=std::string::npos?.86f:.43f);}
                setModel(g_instanceWorld[i]*item.pose);glBindVertexArray(cache.gpu[part].vao);glDrawElements(GL_TRIANGLES,cache.gpu[part].count*3,GL_UNSIGNED_INT,nullptr);
            }
            continue;
        }
        auto* materialFeature=g_doc.find(g_bodyIds[i]);glUniform1f(hardLoc,materialFeature&&(materialFeature->type==FeatureType::Extrude||materialFeature->type==FeatureType::SketchCut)&&!wireframe?1.f:0.f);
        auto& gm=g_meshes[g_instancePart[i]];if(!gm.count)continue;setModel(g_instanceWorld[i]);
        if(!geometry){float sel=i==(size_t)selBody?1.f:i==(size_t)hoverBody?.3f:0.f;glUniform1f(suSel,sel);glUniform1f(suOutline,0.f);}
        glBindVertexArray(gm.vao);glPolygonMode(GL_FRONT_AND_BACK,!geometry&&wireframe?GL_LINE:GL_FILL);if(geometry||wireframe||selectionOverlay||!drawFaceAppearances024(i,materialFeature))glDrawElements(GL_TRIANGLES,gm.count*3,GL_UNSIGNED_INT,nullptr);
        if(!geometry&&!wireframe&&!selectionOverlay)drawBodyStamps024(i,materialFeature);
        if(!geometry&&!wireframe&&g_instancePart[i]<g_partPicks.size()){glUniform3f(suColor,.93f,.58f,.26f);glUniform1f(suSel,1.f);glPolygonOffset(-1.f,-1.f);
            auto& pick=g_partPicks[g_instancePart[i]];
            for(const auto& item:g_picked)if(item.kind=="face"&&item.feature==g_bodyIds[i]&&(!item.instance||(item.instance==g_instanceIds[i]&&item.copy==g_instanceCopies[i])))for(size_t face=0;face<pick.faceNames.size()&&face<pick.faces.size();++face)if(faceReferenceMatches(pick,face,item.name)&&pick.faces[face].second>0)glDrawElements(GL_TRIANGLES,pick.faces[face].second*3,GL_UNSIGNED_INT,(void*)((size_t)pick.faces[face].first*3*sizeof(unsigned)));
            glPolygonOffset(1.f,1.f);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    if(!geometry){glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);}
}
#include "path_tracing.inl"

static bool ensureOcclusionTargets(int w,int h){
    if(!glGenFramebuffers||!glBindFramebuffer||!glFramebufferTexture2D||!glCheckFramebufferStatus||!glDeleteFramebuffers)return false;
    if(gOccReady&&gOccW==w&&gOccH==h)return true;
    if(!gOccReady&&gOccTriedW==w&&gOccTriedH==h)return false;
    if(gFbo){glDeleteFramebuffers(1,&gFbo);gFbo=0;}if(gGeomTex){glDeleteTextures(1,&gGeomTex);gGeomTex=0;}if(gAoTex){glDeleteTextures(1,&gAoTex);gAoTex=0;}if(gDepthTex){glDeleteTextures(1,&gDepthTex);gDepthTex=0;}
    auto tex=[&](GLuint& id,GLint internal,GLenum format,GLenum type){glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);glTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,type,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);};
    tex(gGeomTex,GL_RGBA16F,GL_RGBA,GL_HALF_FLOAT);tex(gAoTex,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);tex(gDepthTex,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_FLOAT);
    glGenFramebuffers(1,&gFbo);glBindFramebuffer(GL_FRAMEBUFFER,gFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,gGeomTex,0);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,gDepthTex,0);
    gOccReady=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;glBindFramebuffer(GL_FRAMEBUFFER,0);gOccW=w;gOccH=h;gOccTriedW=w;gOccTriedH=h;
    if(!gOccReady)while(glGetError()!=GL_NO_ERROR){}
    return gOccReady;
}
static void applyOcclusion(const Mat4& view,const Mat4& proj,const GLfloat vpT[16],const Vec3& eye){
    if((g_occlusionMode==0&&!g_showEdges)||wireframe||viewW<2||viewH<2||!ensureOcclusionTargets(viewW,viewH))return;
    glBindFramebuffer(GL_FRAMEBUFFER,gFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,gGeomTex,0);
    glViewport(0,0,viewW,viewH);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glClearColor(0.f,0.f,1.f,1.f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    drawSurfaces(true,view,proj,vpT,eye);
    if(g_occlusionMode==0||g_trace.active){glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(viewX,winH-viewY-viewH,viewW,viewH);glEnable(GL_BLEND);return;}
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,gAoTex,0);glDisable(GL_DEPTH_TEST);glUseProgram(pAo);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,gGeomTex);glUniform1i(glGetUniformLocation(pAo,"gbuf"),0);glUniform2f(glGetUniformLocation(pAo,"res"),(float)viewW,(float)viewH);glUniform1f(glGetUniformLocation(pAo,"depthFar"),(float)g_projectionFar);
    GLfloat pm[16];for(int i=0;i<16;++i)pm[i]=(GLfloat)proj.m[i];glUniformMatrix4fv(auProj,1,GL_FALSE,pm);glUniform2f(auFocal,(float)proj.m[0],(float)proj.m[5]);glUniform1f(auOrtho,g_cameraOrtho?1.f:0.f);
    double radius=g_sceneBounds.valid()?clamp(g_sceneBounds.diagonal()*0.045,1.5,24.0):6.0;
    radius*=g_occlusionRadius*(g_occlusionMode==1?.48:g_occlusionMode==3?1.5:1.);
    glUniform1f(auRadius,(float)radius);glUniform1f(auBias,(float)(radius*0.055));glUniform1i(glGetUniformLocation(pAo,"mode"),g_occlusionMode);
    glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(viewX,winH-viewY-viewH,viewW,viewH);glEnable(GL_BLEND);glBlendFunc(GL_DST_COLOR,GL_ZERO);glUseProgram(pCompose);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,gAoTex);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,gGeomTex);
    glUniform1i(glGetUniformLocation(pCompose,"ao"),0);glUniform1i(glGetUniformLocation(pCompose,"gbuf"),1);glUniform1f(glGetUniformLocation(pCompose,"strength"),g_occlusionStrength);glUniform2f(glGetUniformLocation(pCompose,"res"),(float)viewW,(float)viewH);glUniform2f(cuOrigin,(float)viewX,(float)(winH-viewY-viewH));
    glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);glActiveTexture(GL_TEXTURE0);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
static void drawFeatureEdges(const GLfloat vpT[16]){
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
    if(gOccReady&&gOccW==viewW&&gOccH==viewH){glDisable(GL_DEPTH_TEST);glUseProgram(pContour);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,gGeomTex);
        glUniform1i(glGetUniformLocation(pContour,"gbuf"),0);glUniform2f(glGetUniformLocation(pContour,"res"),(float)viewW,(float)viewH);glUniform2f(glGetUniformLocation(pContour,"origin"),(float)viewX,(float)(winH-viewY-viewH));glUniform1f(glGetUniformLocation(pContour,"depthFar"),(float)g_projectionFar);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);}
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glUseProgram(pFeatureEdge);glUniformMatrix4fv(glGetUniformLocation(pFeatureEdge,"vp"),1,GL_FALSE,vpT);
    glUniform2f(glGetUniformLocation(pFeatureEdge,"viewport"),(float)viewW,(float)viewH);glUniform1f(glGetUniformLocation(pFeatureEdge,"width"),1.22f);
    for(size_t i=0;i<g_bodyIds.size();++i){if(!displayInstance(i))continue;
        if(auto* feature=g_doc.find(g_bodyIds[i]);feature&&(resolvedAppearance(g_doc,*feature).opacity<.999||feature->type==FeatureType::NotchedTube||feature->type==FeatureType::SourceTube))continue;
        size_t part=g_instancePart[i];if(part>=g_edgeLines.size()||g_edgeLines[part].verts<6)continue;GLfloat m[16];for(int k=0;k<16;++k)m[k]=(GLfloat)g_instanceWorld[i].m[k];glUniformMatrix4fv(glGetUniformLocation(pFeatureEdge,"model"),1,GL_FALSE,m);glBindVertexArray(g_edgeLines[part].vao);glDrawArrays(GL_TRIANGLES,0,g_edgeLines[part].verts);}
    glDepthMask(GL_TRUE);
}
static void updateNavigationBounds(){
    float expansion=std::clamp(((float)viewW-622.f)/420.f,0.f,1.f);
    g_cubeS=std::max(44,std::min((int)std::lround(136.f+24.f*expansion),viewH-26));g_cubeX=viewX+14;g_cubeY=viewY+12;
}
static bool overViewCube(float x,float y,float padding=20.f){return g_showViewCube&&x>=g_cubeX-padding&&x<=g_cubeX+g_cubeS+padding&&y>=g_cubeY-padding&&y<=g_cubeY+g_cubeS+padding;}
static void cubeCamera(Vec3& eye,Mat4& view,Mat4& proj){
    Vec3 dw{std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};eye=dw*4.2;view=Mat4::lookAt(eye,{},cameraFrame(yaw,pitch,cameraRoll).up);proj=Mat4::ortho(-1.5,1.5,-1.5,1.5,0.05,20);
}
static CubeHit cubeHitAt(int mx,int my){
    CubeHit hit;if(g_cubeS<8||mx<g_cubeX||my<g_cubeY||mx>=g_cubeX+g_cubeS||my>=g_cubeY+g_cubeS)return hit;
    Vec3 eye;Mat4 view,proj;cubeCamera(eye,view,proj);
    double nx=((mx-g_cubeX)/(double)g_cubeS)*2-1,ny=(1.-(my-g_cubeY)/(double)g_cubeS)*2-1;
    Vec3 right{view.m[0],view.m[4],view.m[8]},up{view.m[1],view.m[5],view.m[9]},dir{-view.m[2],-view.m[6],-view.m[10]};
    Vec3 origin=eye+right*(nx*1.5)+up*(ny*1.5);double nearest=1e100;
    for(const CubeTriangle& triangle:g_cubeTriangles){
        Vec3 ab=triangle.b-triangle.a,ac=triangle.c-triangle.a,p=dir.cross(ac);double determinant=ab.dot(p);
        if(std::abs(determinant)<1e-10)continue;double inverse=1./determinant;Vec3 from=origin-triangle.a;
        double u=from.dot(p)*inverse;if(u<0||u>1)continue;Vec3 q=from.cross(ab);double v=dir.dot(q)*inverse;
        if(v<0||u+v>1)continue;double distance=ac.dot(q)*inverse;
        if(distance>=0&&distance<nearest){nearest=distance;hit={true,triangle.face,triangle.normal};}
    }
    return hit;
}
static int cubeFaceAt(int mx,int my){auto hit=cubeHitAt(mx,my);return hit.hit?hit.face:-1;}
static void orientViewCube(int mx,int my){static const int faceView[6]={182,183,23,184,180,181};auto hit=cubeHitAt(mx,my);if(!hit.hit)return;
    if(hit.face>=0)uiAction(faceView[hit.face]);
    else{Vec3 normal=hit.normal.normalized();setOrbitView(std::atan2(normal.x,normal.z),std::asin(clamp(normal.y,-1.,1.)),true,"Diagonal");}}
static void paintViewCube(){
    if(!gCubeVao)return;
    glEnable(GL_SCISSOR_TEST);glScissor(g_cubeX,winH-g_cubeY-g_cubeS,g_cubeS,g_cubeS);glViewport(g_cubeX,winH-g_cubeY-g_cubeS,g_cubeS,g_cubeS);glEnable(GL_DEPTH_TEST);glClear(GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    Vec3 eye;Mat4 view,proj;cubeCamera(eye,view,proj);Mat4 vp=proj*view;GLfloat vpT[16];for(int i=0;i<16;++i)vpT[i]=(GLfloat)vp.m[i];
    static const GLfloat ident[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};static const float colors[6][3]={{.77f,.84f,.91f},{.58f,.67f,.78f},{.43f,.79f,.94f},{.39f,.45f,.53f},{.91f,.94f,.98f},{.60f,.69f,.79f}};
    glUseProgram(pCube);glUniform3f(glGetUniformLocation(pCube,"eye"),(float)eye.x,(float)eye.y,(float)eye.z);glUniformMatrix4fv(glGetUniformLocation(pCube,"vp"),1,GL_FALSE,vpT);glUniformMatrix4fv(glGetUniformLocation(pCube,"model"),1,GL_FALSE,ident);glUniform1f(glGetUniformLocation(pCube,"outline"),0.f);
    glBindVertexArray(gCubeVao);for(int face=0;face<6;++face){float glow=g_cubeHover.hit&&g_cubeHover.face==face?.13f:0.f;
        glUniform3f(glGetUniformLocation(pCube,"color"),std::min(1.f,colors[face][0]+glow),std::min(1.f,colors[face][1]+glow),std::min(1.f,colors[face][2]+glow));glDrawArrays(GL_TRIANGLES,face*6,6);}
    float edgeGlow=g_cubeHover.hit&&g_cubeHover.face<0?.18f:0.f;
    glUniform3f(glGetUniformLocation(pCube,"color"),.68f+edgeGlow,.77f+edgeGlow,.86f+edgeGlow);
    glDrawArrays(GL_TRIANGLES,36,72);glUniform3f(glGetUniformLocation(pCube,"color"),.60f+edgeGlow,.70f+edgeGlow,.80f+edgeGlow);glDrawArrays(GL_TRIANGLES,108,24);
    glDisable(GL_DEPTH_TEST);glViewport(0,0,winW,winH);
}
static void paintCompass(int x,int y,int width,int height){
    if(!gCompassVao)return;
    Vec3 eye;Mat4 view,projection;cubeCamera(eye,view,projection);Mat4 vp=projection*view;
    GLfloat matrix[16];for(int i=0;i<16;++i)matrix[i]=(GLfloat)vp.m[i];
    static const GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glViewport(x,winH-y-height,width,height);glDisable(GL_DEPTH_TEST);glUseProgram(pEdge);
    glUniformMatrix4fv(euVp,1,GL_FALSE,matrix);glUniformMatrix4fv(euModel,1,GL_FALSE,identity);
    glBindVertexArray(gCompassVao);glLineWidth(3.f);
    static const float colors[3][3]={{1.f,.38f,.34f},{.45f,.87f,.60f},{.45f,.69f,1.f}};
    for(int axis=0;axis<3;++axis){glUniform3f(glGetUniformLocation(pEdge,"color"),colors[axis][0],colors[axis][1],colors[axis][2]);glDrawArrays(GL_LINES,axis*2,2);}
    glLineWidth(1.f);glViewport(0,0,winW,winH);
}
static std::array<Vec3,3> compassDirections(){
    Mat4 view=cameraView();return {view.transformDir({1,0,0}),view.transformDir({0,1,0}),view.transformDir({0,0,1})};
}
static GLuint g_triadVao=0,g_triadVbo=0;
static std::array<std::pair<int,int>,3> g_triadRanges{};
static RECT g_compassBounds{};
static void buildNavigationTriad(){
    if(g_triadVao)return;
    std::vector<float> vertices;
    auto vertex=[&](Vec3 point,Vec3 normal){vertices.insert(vertices.end(),{(float)point.x,(float)point.y,(float)point.z,(float)normal.x,(float)normal.y,(float)normal.z});};
    auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 na,Vec3 nb,Vec3 nc){vertex(a,na);vertex(b,nb);vertex(c,nc);};
    auto range=[&](int part,int first){g_triadRanges[part]={first,(int)vertices.size()/6-first};};
    int first=0;
    static const Vec3 axes[]={{1,0,0},{0,1,0},{0,0,1}};
    for(int axis=0;axis<3;++axis){first=(int)vertices.size()/6;Vec3 direction=axes[axis],u=axes[(axis+1)%3],v=direction.cross(u);
        for(int segment=0;segment<24;++segment){double a=2*K_PI*segment/24,b=2*K_PI*(segment+1)/24;Vec3 na=u*std::cos(a)+v*std::sin(a),nb=u*std::cos(b)+v*std::sin(b);
            Vec3 p=direction*(-.24)+na*.08,q=direction*(-.24)+nb*.08,r=direction*.78+nb*.08,s=direction*.78+na*.08;
            triangle(p,q,r,na,nb,nb);triangle(p,r,s,na,nb,na);triangle(direction*(-.24),q,p,-direction,-direction,-direction);
            Vec3 coneA=direction*.76+na*.18,coneB=direction*.76+nb*.18,tip=direction*1.10;
            Vec3 ca=(na*.34+direction*.18).normalized(),cb=(nb*.34+direction*.18).normalized();triangle(coneA,coneB,tip,ca,cb,(ca+cb).normalized());triangle(direction*.76,coneA,coneB,-direction,-direction,-direction);
        }range(axis,first);
    }
    glGenVertexArrays(1,&g_triadVao);glGenBuffers(1,&g_triadVbo);glBindVertexArray(g_triadVao);glBindBuffer(GL_ARRAY_BUFFER,g_triadVbo);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,nullptr);glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);
}
static void paintNavigationTriad(int x,int y,int size){
    buildNavigationTriad();Vec3 eye;Mat4 view,projection;cubeCamera(eye,view,projection);Mat4 vp=projection*view;GLfloat matrix[16];for(int i=0;i<16;++i)matrix[i]=(GLfloat)vp.m[i];
    static const GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glEnable(GL_SCISSOR_TEST);glScissor(x,winH-y-size,size,size);glViewport(x,winH-y-size,size,size);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    glUseProgram(pSolid);glUniformMatrix4fv(suVp,1,GL_FALSE,matrix);glUniformMatrix4fv(suModel,1,GL_FALSE,identity);glUniform3f(suEye,(float)eye.x,(float)eye.y,(float)eye.z);glUniform1f(suHard,0);glUniform1f(suOutline,0);glUniform1f(suSel,0);
    glUniform1f(glGetUniformLocation(pSolid,"satin"),1);glUniform1f(glGetUniformLocation(pSolid,"opacity"),1);glUniform1f(glGetUniformLocation(pSolid,"hasTexture"),0);glUniform1f(glGetUniformLocation(pSolid,"metallic"),.38f);glUniform1f(glGetUniformLocation(pSolid,"roughness"),.22f);
    static const float colors[][3]={{1.f,.32f,.28f},{.28f,.92f,.51f},{.29f,.62f,1.f}};
    glBindVertexArray(g_triadVao);for(int part=0;part<3;++part){glUniform3f(suColor,colors[part][0],colors[part][1],colors[part][2]);glDrawArrays(GL_TRIANGLES,g_triadRanges[part].first,g_triadRanges[part].second);}
    glDisable(GL_DEPTH_TEST);glViewport(0,0,winW,winH);
}
static bool navigationUiFits(std::string& detail){
    if(g_cubeS<44||g_cubeX<viewX||g_cubeX>viewX+viewW*.5||g_cubeY<viewY||g_cubeX+g_cubeS>viewX+viewW||g_cubeY+g_cubeS>viewY+viewH){detail="Upper-left cube exceeds the CAD viewport.";return false;}
    if(g_compassBounds.left<viewX||g_compassBounds.top<viewY||g_compassBounds.right>viewX+viewW||g_compassBounds.bottom>viewY+viewH){detail="Cartesian triad exceeds the CAD viewport.";return false;}
    for(const auto& zone:g_zones)if(zone.id==455&&zone.x<g_compassBounds.right&&zone.x+zone.w>g_compassBounds.left&&zone.y<g_compassBounds.bottom&&zone.y+zone.h>g_compassBounds.top){detail="Cartesian triad overlaps the display-style menu.";return false;}
    for(int action:{179,191})if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;})){detail="Cube or Home control is missing.";return false;}
    for(int action:{182,23,180})if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action&&zone.x>=g_compassBounds.left&&zone.y>=g_compassBounds.top&&zone.x+zone.w<=g_compassBounds.right+1&&zone.y+zone.h<=g_compassBounds.bottom+1;})){detail="Cartesian axis control is missing or clipped.";return false;}
    for(const auto& range:g_triadRanges)if(range.second<=0){detail="Shaded Cartesian geometry is missing.";return false;}
    return true;
}
static void paintCartesianTriad(int x,int y,int size){
    auto axes=compassDirections();static const float colors[3][3]={{1.f,.38f,.35f},{.40f,.91f,.60f},{.34f,.67f,1.f}};
    static const GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glViewport(x,winH-y-size,size,size);glUseProgram(pEdge);glUniformMatrix4fv(euVp,1,GL_FALSE,identity);glUniformMatrix4fv(euModel,1,GL_FALSE,identity);
    glBindVertexArray(gCompassVao);glBindBuffer(GL_ARRAY_BUFFER,gCompassVbo);
    std::array<int,3> order={0,1,2};std::sort(order.begin(),order.end(),[&](int a,int b){return axes[a].z<axes[b].z;});
    for(int axis:order){float dx=(float)axes[axis].x*.65f,dy=(float)axes[axis].y*.65f;float length=std::hypot(dx,dy),ux=length>.001f?dx/length:1,uy=length>.001f?dy/length:0;
        float vertices[]={-dx*.32f,-dy*.32f,0,0,0,0, 0,0,0,dx,dy,0, dx,dy,0,dx-ux*.12f-uy*.06f,dy-uy*.12f+ux*.06f,0, dx,dy,0,dx-ux*.12f+uy*.06f,dy-uy*.12f-ux*.06f,0};
        glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_DYNAMIC_DRAW);glUniform3f(glGetUniformLocation(pEdge,"color"),colors[axis][0],colors[axis][1],colors[axis][2]);glLineWidth(2.5f);glDrawArrays(GL_LINES,0,8);
    }
    glLineWidth(1.f);glViewport(0,0,winW,winH);
}
static std::string explorerOperation(const Feature& feature){
    std::string operation=featureOperation(feature);
    if(operation.empty())operation=nativeText(&feature,"op","native solid");
    for(char& c:operation)if(c=='_')c=' ';if(!operation.empty())operation[0]=(char)std::toupper((unsigned char)operation[0]);
    auto lineage=featureManufacturingLineage(g_doc,feature);
    if(lineage.geometryRoute=="sheet_metal"&&lineage.origin!=feature.id)operation+=" / sheet #"+std::to_string(lineage.origin);
    return operation;
}
static int explorerIcon(const Feature& feature){
    if(featureManufacturingLineage(g_doc,feature).geometryRoute=="sheet_metal")return 403;
    if(feature.type==FeatureType::Boolean||feature.get("op")=="cut"||feature.get("op")=="hole")return 406;
    if(feature.type==FeatureType::Transform||feature.get("op")=="transform")return 404;
    return 405;
}

static std::wstring widePath(const std::string& path){
    if(path.empty())return {};
    int count=MultiByteToWideChar(CP_UTF8,0,path.data(),(int)path.size(),nullptr,0);
    if(count<=0)return {};
    std::wstring wide((size_t)count,L'\0');
    MultiByteToWideChar(CP_UTF8,0,path.data(),(int)path.size(),wide.data(),count);
    return wide;
}
static unsigned long long fileStamp(const std::wstring& path){
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data))return 0;
    ULARGE_INTEGER stamp;stamp.LowPart=data.ftLastWriteTime.dwLowDateTime;stamp.HighPart=data.ftLastWriteTime.dwHighDateTime;
    return stamp.QuadPart;
}
static bool decodeCanvasFile(const std::wstring& path,std::vector<unsigned char>& rgba,unsigned& width,unsigned& height){
    static bool started=false;
    if(!started){HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return false;started=true;}
    const GUID factoryId={0xcacaf262,0x9370,0x4615,{0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a}};
    IWICImagingFactory* factory=nullptr;
    if(FAILED(CoCreateInstance(factoryId,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return false;
    IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;IWICFormatConverter* scaledConverter=nullptr;IWICBitmapScaler* scaler=nullptr;
    bool ok=false;
    if(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))
        &&SUCCEEDED(decoder->GetFrame(0,&frame))
        &&SUCCEEDED(factory->CreateFormatConverter(&converter))
        &&SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom))){
        IWICBitmapSource* source=converter;UINT srcW=0,srcH=0;converter->GetSize(&srcW,&srcH);
        if((srcW>2048||srcH>2048)&&SUCCEEDED(factory->CreateBitmapScaler(&scaler))){
            double scale=std::min(2048.0/std::max(srcW,1u),2048.0/std::max(srcH,1u));
            UINT dw=std::max(1u,(UINT)std::lround(srcW*scale)),dh=std::max(1u,(UINT)std::lround(srcH*scale));
            if(SUCCEEDED(scaler->Initialize(converter,dw,dh,WICBitmapInterpolationModeFant))){
                // WIC's scaler may negotiate a BGRA/premultiplied output even
                // from RGBA input. Normalize again before publishing GL_RGBA.
                if(SUCCEEDED(factory->CreateFormatConverter(&scaledConverter))&&SUCCEEDED(scaledConverter->Initialize(scaler,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom)))source=scaledConverter;
                else source=nullptr;
            }
        }
        UINT w=0,h=0;
        if(source&&SUCCEEDED(source->GetSize(&w,&h))&&w>0&&h>0&&w<=2048&&h<=2048){
            rgba.resize((size_t)w*h*4);
            if(SUCCEEDED(source->CopyPixels(nullptr,w*4,(UINT)rgba.size(),rgba.data()))){
                std::vector<unsigned char> row((size_t)w*4);
                for(UINT y=0;y<h/2;++y){auto* a=rgba.data()+(size_t)y*w*4;auto* b=rgba.data()+(size_t)(h-1-y)*w*4;std::copy(a,a+w*4,row.begin());std::copy(b,b+w*4,a);std::copy(row.begin(),row.end(),b);}
                width=w;height=h;ok=true;
            }
        }
    }
    if(scaledConverter)scaledConverter->Release();if(scaler)scaler->Release();if(converter)converter->Release();if(frame)frame->Release();if(decoder)decoder->Release();factory->Release();
    return ok;
}
static HBITMAP candidateReviewBitmap(const std::string& path){
    std::vector<unsigned char> rgba;unsigned width=0,height=0;if(!decodeCanvasFile(std::filesystem::u8path(path).wstring(),rgba,width,height))return nullptr;
    constexpr int previewW=784,previewH=300;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=previewW;info.bmiHeader.biHeight=-previewH;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap)return nullptr;auto* output=(unsigned char*)pixels;for(size_t i=0;i<(size_t)previewW*previewH;++i){output[i*4]=36;output[i*4+1]=31;output[i*4+2]=29;output[i*4+3]=255;}
    double scale=std::min((double)previewW/width,(double)previewH/height);int imageW=std::max(1,(int)std::floor(width*scale)),imageH=std::max(1,(int)std::floor(height*scale)),offsetX=(previewW-imageW)/2,offsetY=(previewH-imageH)/2;for(int y=0;y<imageH;++y)for(int x=0;x<imageW;++x){size_t source=((size_t)(height-1-std::min(height-1,(unsigned)(y/scale)))*width+std::min(width-1,(unsigned)(x/scale)))*4,destination=((size_t)(y+offsetY)*previewW+x+offsetX)*4;output[destination]=rgba[source+2];output[destination+1]=rgba[source+1];output[destination+2]=rgba[source];output[destination+3]=255;}return bitmap;
}
static GLuint canvasTexture(const std::string& path){
    std::wstring wide=widePath(path);unsigned long long stamp=wide.empty()?0:fileStamp(wide);
    CanvasTexture* cached=nullptr;
    for(auto& item:g_canvasTextures)if(item.path==path)cached=&item;
    if(!cached){g_canvasTextures.push_back({path,0,true,0});cached=&g_canvasTextures.back();}
    if(cached->stamp==stamp&&(cached->tex||cached->missing))return cached->missing?g_canvasMissing:cached->tex;
    std::vector<unsigned char> rgba;unsigned w=0,h=0;
    cached->stamp=stamp;cached->missing=true;
    if(stamp&&decodeCanvasFile(wide,rgba,w,h)){
        if(!cached->tex)glGenTextures(1,&cached->tex);
        glBindTexture(GL_TEXTURE_2D,cached->tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)w,(GLsizei)h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        cached->missing=false;
    }
    return cached->missing?g_canvasMissing:cached->tex;
}
static void drawCanvases(const GLfloat* vpT){
    if(!pCanvas||!canvasVao||g_doc.canvases.empty())return;
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.f,1.f);
    glUseProgram(pCanvas);glUniformMatrix4fv(glGetUniformLocation(pCanvas,"vp"),1,GL_FALSE,vpT);glUniform1i(glGetUniformLocation(pCanvas,"img"),0);
    glActiveTexture(GL_TEXTURE0);glBindVertexArray(canvasVao);
    ExprEnv env=g_doc.buildEnv();
    for(const auto& canvas:g_doc.canvases){
        bool okWidth=false,okHeight=false;double width=evalExpr(canvas.width,env,&okWidth),height=evalExpr(canvas.height,env,&okHeight);
        if(!okWidth||!okHeight||!(width>0)||!(height>0))continue;
        Mat4 model;
        try{model=resolvePlane(g_doc,canvas.plane)*Mat4::scale(width,height,1);}catch(...){continue;}
        GLfloat matrix[16];for(int i=0;i<16;++i)matrix[i]=(GLfloat)model.m[i];
        GLuint tex=canvas.asset.empty()?canvasTexture(canvas.path):stampAssetTexture(canvas.asset);bool live=tex&&tex!=g_canvasMissing;
        glUniformMatrix4fv(glGetUniformLocation(pCanvas,"model"),1,GL_FALSE,matrix);
        glUniform1f(glGetUniformLocation(pCanvas,"opacity"),(float)clamp(canvas.opacity,0.,1.));
        glUniform1f(glGetUniformLocation(pCanvas,"live"),live?1.f:0.f);
        glBindTexture(GL_TEXTURE_2D,live?tex:g_canvasMissing);
        glDrawArrays(GL_TRIANGLES,0,6);
    }
    glBindTexture(GL_TEXTURE_2D,0);glDisable(GL_POLYGON_OFFSET_FILL);
}
#include "viewport_dock.inl"
static void drawFrame(HDC hdc){
    processDirectAuthoringPreview();
    static auto previous=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
    double elapsed=std::min(.10,std::chrono::duration<double>(now-previous).count());advanceFold(elapsed);advanceSheet(elapsed);advanceSuspension(elapsed);advanceMotion(elapsed);previous=now;
    double cameraEase=1-std::exp(-13.4*elapsed),distanceEase=1-std::exp(-11.9*elapsed);
    yaw+=(tYaw-yaw)*cameraEase;pitch+=(tPitch-pitch)*cameraEase;dist+=(tDist-dist)*distanceEase;target=target+(tTarget-target)*cameraEase;
    layout();sampleStudioResources();updateCostDisplay();
    if(g_physicalVisible){u64 selected=selBody>=0&&(size_t)selBody<g_bodyIds.size()?(g_instanceIds[(size_t)selBody]?g_instanceIds[(size_t)selBody]:g_bodyIds[(size_t)selBody]):0;
        if(g_physicalRevision!=g_session.revision||selected!=g_physicalSelection)g_physicalVisible=false;}

    glViewport(0,0,winW,winH);
    glClearColor(.044f,.052f,.058f,1.f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);
    glDisable(GL_DEPTH_TEST);glBindVertexArray(quadVao);
    glUseProgram(pBg);
    glUniform2f(glGetUniformLocation(pBg,"res"),(float)winW,(float)winH);glUniform1f(glGetUniformLocation(pBg,"lightBackground"),g_lightBackground?1.f:0.f);
    glDrawArrays(GL_TRIANGLES,0,3);

    Vec3 eye=camEye();
    Mat4 view=cameraView(),proj=cameraProjection(),vp=proj*view;
    GLfloat vpT[16];for(int i=0;i<16;++i)vpT[i]=(GLfloat)vp.m[i];

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(pShadow);
    glUniformMatrix4fv(glGetUniformLocation(pShadow,"vp"),1,GL_FALSE,vpT);
    glUniform1f(glGetUniformLocation(pShadow,"strength"),g_lightBackground?.18f:.36f);
    glBindVertexArray(quadVao);
    for(size_t i=0;i<g_bodyIds.size()&&!g_allSheetsFlat&&!g_stockMode;++i){if(!displayInstance(i))continue;
        AABB b=g_reports[g_instancePart[i]].bounds.transformed(g_instanceWorld[i]);
        if(auto* f=g_doc.find(g_bodyIds[i]);f&&f->type==FeatureType::NotchedTube){b=AABB{};auto& cache=g_notchViews.at(f->id);auto matrices=displayedNotchTransforms(i);for(size_t k=0;k<matrices.size();++k)b.grow(cache.bounds[k].transformed(g_instanceWorld[i]*matrices[k]));}
        if(auto* f=g_doc.find(g_bodyIds[i]);f&&f->type==FeatureType::SourceTube){b=AABB{};auto& cache=g_sourceViews.at(f->id);auto matrices=displayedSourceTransforms(i);for(size_t k=0;k<cache.preview.rigid.size();++k)b.grow(cache.rigidBounds[k].transformed(g_instanceWorld[i]*matrices[k]));}
        if(!b.valid())continue;
        Vec3 e=b.extent(),c=b.center();
        glUniform3f(glGetUniformLocation(pShadow,"ctr"),(float)c.x,(float)b.mn.y-.1f,(float)c.z);
        glUniform2f(glGetUniformLocation(pShadow,"rad"),(float)e.x*.62f+1,(float)e.z*.62f+1);
        glDrawArrays(GL_TRIANGLES,0,6);
    }

    updateAdaptiveGrid();glUseProgram(pGrid);
    Mat4 gridMatrix=vp*Mat4::translation({g_adaptiveGrid.centerX,0,g_adaptiveGrid.centerZ});GLfloat gridTransform[16];for(int i=0;i<16;++i)gridTransform[i]=(GLfloat)gridMatrix.m[i];
    glUniformMatrix4fv(glGetUniformLocation(pGrid,"vp"),1,GL_FALSE,gridTransform);glUniform1f(glGetUniformLocation(pGrid,"fadeRadius"),(float)g_adaptiveGrid.radius);
    glUniform1f(glGetUniformLocation(pGrid,"lightBackground"),g_lightBackground?1.f:0.f);
    glBindVertexArray(gridVao);
    if(g_grid&&!g_sketchDrawing.active&&!g_allSheetsFlat&&!g_stockMode)glDrawArrays(GL_LINES,0,gridCount);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    drawCanvases(vpT);
    drawSurfaces(false,view,proj,vpT,eye);
    drawPathTracing(proj,eye);
    if(g_trace.active){glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);drawSurfaces(false,view,proj,vpT,eye,false,true);glDepthMask(GL_TRUE);}
    applyOcclusion(view,proj,vpT,eye);
    if(g_showEdges&&!wireframe)drawFeatureEdges(vpT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    drawSurfaces(false,view,proj,vpT,eye,true);
    drawPlaneStamps024(vpT);
    glDepthMask(GL_TRUE);

    if((g_showSketches&&!g_sketchLines.empty())||(g_showDatumPlanes&&!g_datumLines.empty())||!g_selectionLines.empty()||!g_selectionFaceFill.empty()){
        glDisable(GL_DEPTH_TEST);glUseProgram(pSketch);glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,vpT);glBindVertexArray(sketchVao);glBindBuffer(GL_ARRAY_BUFFER,sketchVbo);
        if(g_sketchGpuDirty){std::vector<float> buffers=g_sketchLines;buffers.insert(buffers.end(),g_sketchPoints.begin(),g_sketchPoints.end());buffers.insert(buffers.end(),g_constraintHighlightLines.begin(),g_constraintHighlightLines.end());buffers.insert(buffers.end(),g_datumLines.begin(),g_datumLines.end());buffers.insert(buffers.end(),g_selectionLines.begin(),g_selectionLines.end());buffers.insert(buffers.end(),g_sketchFill.begin(),g_sketchFill.end());buffers.insert(buffers.end(),g_datumFill.begin(),g_datumFill.end());buffers.insert(buffers.end(),g_selectionFaceFill.begin(),g_selectionFaceFill.end());glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(buffers.size()*sizeof(float)),buffers.empty()?nullptr:buffers.data(),GL_DYNAMIC_DRAW);g_sketchGpuDirty=false;}
        GLint fillOffset=(GLint)(g_sketchLines.size()+g_sketchPoints.size()+g_constraintHighlightLines.size()+g_datumLines.size()+g_selectionLines.size())/3;
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
        if(g_showSketches){glUniform4f(glGetUniformLocation(pSketch,"tint"),.20,.57,.92,.17);glDrawArrays(GL_TRIANGLES,fillOffset,(GLsizei)g_sketchFill.size()/3);}
        if(g_showDatumPlanes){glUniform4f(glGetUniformLocation(pSketch,"tint"),.30,.68,.60,.07);glDrawArrays(GL_TRIANGLES,fillOffset+(GLint)g_sketchFill.size()/3,(GLsizei)g_datumFill.size()/3);}
        glUniform4f(glGetUniformLocation(pSketch,"tint"),1.,.57,.16,.34);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1.f,-1.f);glDrawArrays(GL_TRIANGLES,fillOffset+(GLint)(g_sketchFill.size()+g_datumFill.size())/3,(GLsizei)g_selectionFaceFill.size()/3);glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);glDisable(GL_DEPTH_TEST);
        if(g_showSketches){glUniform4f(glGetUniformLocation(pSketch,"tint"),.62,.77,.98,.85);if(!g_sketchDrawing.active||!g_activeSketch.empty())glDrawArrays(GL_LINES,0,(GLsizei)g_sketchLines.size()/3);
            if(!g_sketchPoints.empty()){glPointSize(6);glDrawArrays(GL_POINTS,(GLint)g_sketchLines.size()/3,(GLsizei)g_sketchPoints.size()/3);glPointSize(1);}
            glUniform4f(glGetUniformLocation(pSketch,"tint"),1.,.66,.22,1.);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size())/3,(GLsizei)g_constraintHighlightLines.size()/3);}
        if(g_showDatumPlanes){glUniform4f(glGetUniformLocation(pSketch,"tint"),.55,.77,.68,.48);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size()+g_constraintHighlightLines.size())/3,(GLsizei)g_datumLines.size()/3);}
        if(!g_selectionLines.empty()){glLineWidth(2.5f);glUniform4f(glGetUniformLocation(pSketch,"tint"),.97,.62,.28,1.);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size()+g_constraintHighlightLines.size()+g_datumLines.size())/3,(GLsizei)g_selectionLines.size()/3);glLineWidth(1.f);}
    }
    updateSelectionHover(g_pointerX,g_pointerY);drawMeshPointerEdges();drawSelectedFaceGlow();drawSelectedReferenceEdges();drawSelectionHover();drawDraftSketches();drawProjectionPreview();drawManualSketchPreview();drawSketchEditPreview();drawDirectAuthoringPreview();
    // ============================ UI =========================================
    glDisable(GL_DEPTH_TEST);
    glViewport(0,0,winW,winH);
    float W=(float)winW,H=(float)winH;updateNavigationBounds();
    glUseProgram(pUI);
    glUniform2f(uuVp,W,H);
    float uiOffsetY=0;
    auto textOut=[&](const std::string& s,float x,float y,float sc=1.f,float red=.87f,float green=.89f,float blue=.88f){::textOut(s,x,y+uiOffsetY,sc,red,green,blue);};
    auto rect=[&](float x,float y,float w,float h,float rad,
                  float fr,float fg,float fb,float fa,
                  float br,float bg2,float bb,float ba,float bw){
        glUniform1i(uuGlyph,0);
        glUniform4f(uuRc,x,y+uiOffsetY,w,h);
        glUniform1f(uuRad,rad);
        glUniform4f(uuFill,fr,fg,fb,fa);
        glUniform4f(uuBorder,br,bg2,bb,ba);
        glUniform1f(uuBW,bw);
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES,0,6);
    };
    POINT mp; GetCursorPos(&mp);
    HWND hw=WindowFromDC(hdc); ScreenToClient(hw,&mp);if(g_testPointer)mp=*g_testPointer;

    textBegin();g_zones.clear();g_hoverZone=-1;std::string ribbonHint;
    auto panel=[&](float x,float y,float w,float h,float r=0.f){rect(x,y,w,h,r,.085f,.105f,.12f,1,.21f,.25f,.28f,.65f,1);};
    auto label=[&](const std::string& t,float x,float y,float scale=.75f){textOut(t,x,y,scale,.55f,.57f,.62f);};
    auto glyph=[&](int id,float x,float y,float size,float c=.82f){
        if(const auto* vector=actionVectorIcon(id)){
            glUniform1i(uuGlyph,500);glUniform4f(uuRc,x,y,size,size);glUniform1f(uuBW,std::max(1.5f,size*.06f));
            glUniform4f(uuFill,c*.75f,c*.91f,std::min(c*1.09f,1.f),1);glBindVertexArray(quadVao);auto lineAB=glGetUniformLocation(pUI,"lineAB");
            for(const auto& stroke:*vector){glUniform4f(lineAB,stroke.ax*size,stroke.ay*size,stroke.bx*size,stroke.by*size);glDrawArrays(GL_TRIANGLES,0,6);}
            glUniform1i(uuGlyph,0);return;
        }
        auto badge=[&]{bool edit=id==127||id==122||id==125||id==395||id==398;bool remove=id==123||id==126||id==396||id==399;if(!edit&&!remove)return;float bs=size*.47f,bx=x+size-bs,by=y+size-bs;rect(bx-1,by-1,bs+2,bs+2,3,.045f,.07f,.09f,1,0,0,0,0,0);glUniform1i(uuGlyph,remove?487:486);glUniform4f(uuRc,bx,by,bs,bs);glUniform4f(uuFill,remove?.96f:.91f,remove?.39f:.79f,remove?.34f:.46f,1);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glUniform1i(uuGlyph,0);};
        int icon=atlasIcon(id);static GLuint atlases[4]{};
        if(icon>=0){auto& atlas=atlases[icon/16];int cell=icon%16;if(!atlas){auto path=toolAtlasPath(icon/16);if(!path.empty()){auto encoded=path.u8string();atlas=canvasTexture(std::string(encoded.begin(),encoded.end()));if(atlas==g_canvasMissing)atlas=0;}}
            if(atlas){glUseProgram(pIcons);glUniform2f(glGetUniformLocation(pIcons,"vp"),W,H);glUniform4f(glGetUniformLocation(pIcons,"rc"),x,y,size,size);glUniform2f(glGetUniformLocation(pIcons,"tile"),(float)(cell%4),(float)(cell/4));glUniform1f(glGetUniformLocation(pIcons,"opacity"),std::clamp(c+.15f,0.f,1.f));glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,atlas);glUniform1i(glGetUniformLocation(pIcons,"atlas"),0);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glUseProgram(pUI);badge();return;}}
        glUniform1i(uuGlyph,id==579?22:id);glUniform4f(uuRc,x,y,size,size);
        glUniform4f(uuFill,c,c+.01f,c+.025f,1);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);
        glUniform1i(uuGlyph,0);
    };
    auto button=[&](const char* t,float x,float y,float w,int id,bool accent=false,bool active=false){
        bool hover=mp.x>=x&&mp.x<x+w&&mp.y>=y+uiOffsetY&&mp.y<y+uiOffsetY+32;
        if(hover)g_hoverZone=id;
        float c=accent?.74f:hover?.21f:active?.22f:.135f;
        rect(x,y+1,w,32,5,.025,.027,.032,.55,0,0,0,0,0);
        rect(x,y,w,32,5,c,c+.008f,c+.021f,1,.38f,.40f,.45f,hover?.85f:.42f,1);
        if(active)rect(x+8,y+28,w-16,2,1,.62,.67,.76,1,0,0,0,0,0);
        textOut(t,x+(w-textWidth(t,.76f))/2,y+7,.76f,accent?.12f:.86f,accent?.13f:.88f,accent?.15f:.92f);
        g_zones.push_back({x,y,w,32,id});
    };
    if(auto* f=selectedNotchedTube()){
        auto& cache=g_notchViews.at(f->id);auto matrices=displayedNotchTransforms(selBody);
        for(size_t k=0;k<cache.preview.hingePoints.size();++k){auto point=(g_instanceWorld[selBody]*matrices[k]).transformPoint(cache.preview.hingePoints[k]);auto clip=vp.transformPoint(point);
            double cw=vp.m[3]*point.x+vp.m[7]*point.y+vp.m[11]*point.z+vp.m[15];if(cw<=0)continue;
            float x=viewX+(float)(clip.x/cw+1)*viewW*.5f,y=viewY+(float)(1-clip.y/cw)*viewH*.5f;
            if(x<viewX+14||x>viewX+viewW-14||y<viewY+70||y>viewY+viewH-14||overViewCube(x,y))continue;
            int source=(int)cache.sourceIndices[k];bool selected=source==g_selectedNotch;
            rect(x-12,y-12,24,24,12,selected?.78f:.22f,selected?.82f:.25f,selected?.89f:.30f,1,.85,.88,.95,.9,1);
            std::string number=std::to_string(source+1);textOut(number,x-textWidth(number,.65f)/2,y-8,.65f,selected?.10f:.90f,selected?.12f:.92f,selected?.15f:.96f);
            g_zones.push_back({x-14,y-14,28,28,9000+source});
        }
    }
    panel(0,0,W,52);panel(0,(float)viewY,(float)viewX,H-viewY-32);
    static GLuint wordmark=0;if(!wordmark){auto atlas=toolAtlasPath();if(!atlas.empty()){auto file=atlas.parent_path()/"ark-wordmark.png";auto path=file.u8string();wordmark=canvasTexture(std::string(path.begin(),path.end()));}}
    if(wordmark&&wordmark!=g_canvasMissing){glUseProgram(pImage);glUniform2f(glGetUniformLocation(pImage,"vp"),W,H);glUniform4f(glGetUniformLocation(pImage,"rc"),16,6,120,40);glUniform1i(glGetUniformLocation(pImage,"image"),0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,wordmark);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glUseProgram(pUI);}else textOut("ARK",22,12,1.55f,.61f,.81f,.96f);
    rect(146,12,1,28,0,.29,.36,.42,1,0,0,0,0,0);
    std::string filename=g_path.substr(g_path.find_last_of("/\\")+1);bool dirty=g_session.revision!=g_savedRevision||g_doc.usage.serial!=g_savedUsageSerial||agentActivitySerial(g_doc)!=g_savedActivitySerial;
    float headerX=164,headerWidth=std::max(1.f,W-460-headerX);auto fitTop=[&](std::string text,float scale,float available){while(!text.empty()&&textWidth(text,scale)>available)text.pop_back();return text;};
    filename=fitTop(filename,.67f,std::max(60.f,headerWidth-95));textOut(filename,headerX,4,.67f,.84f,.90f,.95f);
    float savedX=headerX+textWidth(filename,.67f)+12;rect(savedX,11,4,4,2,dirty?.98f:.44f,dirty?.68f:.76f,dirty?.30f:.62f,1,0,0,0,0,0);textOut(dirty?"Modified":"Saved",savedX+10,4,.60f,.63f,.73f,.81f);
    auto resources="Local / "+resourceSummary();float resourceScale=std::min(.62f,headerWidth/std::max(1.f,textWidth(resources,1.f)));textOut(fitTop(resources,resourceScale,headerWidth),headerX,21,resourceScale,.58f,.76f,.87f);
    auto cost=costSummary();float costScale=std::min(.62f,headerWidth/std::max(1.f,textWidth(cost,1.f)));textOut(fitTop(cost,costScale,headerWidth),headerX,36,costScale,.67f,.78f,.89f);g_zones.push_back({headerX,1,headerWidth,49,488});
    float searchX=W-438;rect(searchX,10,200,33,5,.075,.094,.112,1,.29,.38,.46,.8,1);glyph(413,searchX+8,17,18,.65f);textOut("Search commands...",searchX+34,17,.72f,.57f,.66f,.74f);g_zones.push_back({searchX,10,200,33,450});
    auto iconButton=[&](int action,const char* title,float x,float y,float size=28.f){bool hover=mp.x>=x&&mp.x<x+size&&mp.y>=y&&mp.y<y+size;if(hover){rect(x-3,y-3,size+6,size+6,5,.15,.22,.28,1,0,0,0,0,0);ribbonHint=title;}glyph(action,x+3,y+3,size-6,.86f);g_zones.push_back({x-3,y-3,size+6,size+6,action});};
    iconButton(458,"Document: new, open, save and export",W-229,13);iconButton(18,"Undo",W-192,13);iconButton(19,"Redo",W-155,13);iconButton(400,"Assistant",W-118,13);iconButton(56,"System options / Graphics",W-81,13);iconButton(57,"Account",W-44,13);
    rect(0,52,W,42,0,.073,.09,.108,1,.23,.29,.35,.5,1);
    const char* tabNames[]={"Sheet Metal","Model","Sketch","Inspect","Tools","Manage","Surfaces"};float tabX=18;
    for(int module:{0,1,6,2,3,4,5}){float width=textWidth(tabNames[module],.82f)+28;bool active=g_toolModule==module;if(active)rect(tabX,56,width,35,3,.12,.17,.20,1,0,0,0,0,0);textOut(tabNames[module],tabX+14,66,.82f,active?.95f:.77f,active?.98f:.84f,active?1.f:.89f);if(active)rect(tabX,90,width,3,0,.13,.77,.91,1,0,0,0,0,0);g_zones.push_back({tabX,54,width,39,300+module});tabX+=width+7;}
    float taskX=tabX+12,taskWidth=std::max(1.f,W-taskX-316);std::string activity=!g_activeToolName.empty()?g_activeToolRole+" / "+g_activeToolName:g_teamStarted?(g_teamSnapshot.running?(!g_liveStepLabel.empty()?g_liveStepLabel:"Team active / provider requests"):std::string("Goal: ")+agentGoalStateName(g_teamSnapshot.goalState)+(g_teamSnapshot.goalDeclared?" / explicit lead report":" / completion not reported")):g_assistantResponseFinished?(g_assistantResponseError?"Response interrupted / inspect prior changes":"Response finished / review result"):"Ready / scene orbit";if(!g_draftViews.empty())activity+=" / "+std::to_string(g_draftViews.size())+" uncommitted drafts";
    auto fitHeader=[&](std::string text,float scale){while(!text.empty()&&textWidth(text,scale)>taskWidth)text.pop_back();return text;};textOut(fitHeader(activity,.52f),taskX,58,.52f,.50f,.81f,.94f);textOut(fitHeader(std::to_string(g_liveCompletedSteps)+" committed team steps / drafts are uncommitted",.49f),taskX,75,.49f,.67f,.78f,.89f);
    {const char* filters[]={"Body","Face","Edge","Curve"};for(int kind=0;kind<4;++kind)button(filters[kind],W-298+kind*70,56,66,170+kind,false,g_pickKind==kind&&!g_directAuthoring.active&&!g_sketchEditing.active&&!g_sketchDrawing.active&&!g_componentMove.armed);}
    rect(0,94,W,124,0,.105,.13,.151,1,.23,.29,.35,.60,1);
    struct RibbonAction{const char* title;int id;};struct RibbonGroup{const char* title;std::vector<RibbonAction> actions;};std::vector<RibbonGroup> groups;
    if(g_toolModule==0)groups={{"Create",{{"Base",120},{"Flange",121},{"Hem",394},{"Stamp",397},{"Opening",124}}},{"Modify",{{"Edit",325},{"Jog",494},{"Sketch bend",495},{"Unfold",320},{"Refold",321}}},{"Pattern",{{"Pattern",468},{"Mirror",108}}},{"Output",{{"Export",213}}}};
    else if(g_toolModule==1&&g_directAuthoring.active)groups={{"Direct edit",{{"Push / pull",650},{"Move body",651},{"Mesh patch",652},{"Pull vertex",653},{"Mesh edge",654},{"Value",655},{"Apply",656},{"Cancel",657}}},{"Source",{{"Dimensions",659},{"Sample mesh",754}}},{"Select",{{"Body",170},{"Face",171},{"Tangent patch",658}}},{"Combine",{{"Fuse bodies",8},{"Carve body",9},{"Keep overlap",10}}}};
    else if(g_toolModule==1&&W<1280)groups={{"Create",{{"Primitive",100},{"Extrude",101},{"Drag extrude",687},{"Loft",76},{"Helix",750},{"Tube",26}}},{"Direct",{{"Push / pull",650},{"Move body",651},{"Mesh patch",652},{"Source dims",659}}},{"Combine",{{"Fuse bodies",8},{"Carve body",9},{"Keep overlap",10}}},{"More",{{"Model tools",456}}}};
    else if(g_toolModule==1)groups={{"Create",{{"Primitive",100},{"Extrude",101},{"Drag extrude",687},{"Loft",76},{"Helix",750},{"Frame",210},{"Tube",26}}},{"Direct",{{"Push / pull",650},{"Move body",651},{"Mesh patch",652},{"Source dims",659}}},{"Combine",{{"Fuse bodies",8},{"Carve body",9},{"Keep overlap",10}}},{"Assembly",{{"Assembly",27},{"Mate",94},{"Library",484}}},{"Frame edits",{{"Trim / extend",496},{"Trim plane",497},{"Weld",498},{"Tube end fit",549}}},{"More",{{"Model tools",456}}}};
    else if(g_toolModule==2&&g_sketchEditing.active)groups={{"Edit in view",{{"Exit sketch",611},{"Coordinates",70},{"Dimensions",591},{"Source params",577},{"Constraint",73},{"Normal view",71},{"Grid",579}}},{"Sketch",{{"Draw sketch",570},{"Undo",18},{"Redo",19}}}};
    else if(g_toolModule==2&&g_sketchDrawing.active)groups={{"Draw in view",{{"Line",571},{"Polyline",572},{"Rectangle",573},{"Circle",574},{"Arc 3 pt",575},{"Dimensions",576},{"Grid",579},{"Exit sketch",578}}},{"Coordinate forms",{{"Line form",543},{"Arc form",544},{"Tangent arc",545},{"Rectangle",61},{"Circle",62},{"Spline",63}}},{"Modify",{{"Project 2D",536},{"Include 3D",537},{"Edit curve",70},{"Constraint",73},{"Plane",60},{"Source params",577}}},{"Build",{{"Extrude",65},{"Drag extrude",687},{"Cut",66}}}};
    else if(g_toolModule==2)groups={{"Interactive",{{"Draw sketch",570},{"Edit sketch",610}}},{"Draw 2D",{{"Line",543},{"Arc 3 pt",544},{"Tangent arc",545},{"Rectangle",61},{"Circle",62},{"Spline",63}}},{"Draw 3D",{{"Arc 3D",547},{"Tangent 3D",548},{"3D curve",538}}},{"Modify",{{"Project 2D",536},{"Include 3D",537},{"Edit curve",70},{"Constraint",73},{"Plane",60}}},{"Build",{{"Extrude",65},{"Drag extrude",687},{"Cut",66}}}};
    else if(g_toolModule==3)groups={{"Measure",{{"Measure 2D",585},{"Measure 3D",586},{"Units",587}}},{"Selection",{{"Body",170},{"Face",171},{"Tangent faces",658},{"Edge",172},{"Curve",173}}},{"Inspect",{{"Collision",755},{"BOM",590},{"Properties",401},{"Mass / CG",188},{"Material",187}}},{"Display",{{"Graphics",56},{"Fit",20}}}};
    else if(g_toolModule==4)groups={{"Engineering",{{"Model tools",456},{"Physics",188},{"Loads",216},{"Electronics",211},{"Routing",212},{"Library",484}}},{"Agents",{{"Assistant",400},{"Connect",58},{"Providers",357},{"Tools",457}}}};
    else if(g_toolModule==6)groups={{"Create",{{"Extrude",151},{"Revolve",152},{"Sweep",150},{"Loft",76},{"Fill",77},{"NURBS",78}}},{"Modify",{{"Loft controls",752},{"Extract face",491},{"Trim plane",492},{"Offset",153},{"Glue surfaces",154},{"Thicken",79}}}};
    else groups={{"Document",{{"New",55},{"Open",17},{"Save",16},{"Export",213}}},{"History",{{"Undo",18},{"Redo",19},{"Iterations",493},{"Last failure",580}}},{"Manage",{{"Skeleton IDE",710},{"BOM",590},{"Drawings",860},{"Parameters",67},{"Import",81},{"Connect",58},{"Account",57},{"More",459}}}};
    groups.push_back({"Discover",{{"All tools",480}}});
    int count=0;for(const auto& group:groups)count+=(int)group.actions.size();float cardW=std::min(105.f,(W-32-(groups.size()-1)*27)/std::max(count,1));float cursor=18;
    for(size_t groupIndex=0;groupIndex<groups.size();++groupIndex){auto& group=groups[groupIndex];float start=cursor;
        for(const auto& action:group.actions){if(action.id>=496&&action.id<=498)g_frameRibbonBounds[action.id-496]={cursor,100,cardW-3,92,action.id};bool hover=mp.x>=cursor&&mp.x<cursor+cardW&&mp.y>=100&&mp.y<194;bool enabled=true;
            if(action.id==187)enabled=selBody>=0;if(action.id==188||action.id==216)enabled=selBody>=0||!g_doc.assembly.empty();if(action.id==451||action.id==452)enabled=!g_doc.assembly.empty();
            if(action.id==576)enabled=g_sketchDrawing.active&&!g_sketchDrawing.anchors.empty()&&(g_sketchDrawing.tool!=4||g_sketchDrawing.anchors.size()>=2);
            if(action.id==655||action.id==656)enabled=g_directAuthoring.active&&(g_directAuthoring.feature!=0||g_directAuthoring.mode==5);
            if(g_sketchDrawing.active&&action.id>=571&&action.id<=575&&g_sketchDrawing.tool==action.id-571)rect(cursor,99,cardW-3,94,5,.055f,.18f,.22f,1,.14f,.64f,.74f,.65f,1);
            if(action.id>=496&&action.id<=498){auto* frame=selectedPartFeature();enabled=frame&&frame->type==FeatureType::ConstructionFrame;}
            if(g_toolModule==0&&action.id==108)enabled=false;
            if(hover){rect(cursor,99,cardW-3,94,5,.15,.22,.28,1,0,0,0,0,0);ribbonHint=g_toolModule==0&&action.id==108?"Sheet mirroring is unavailable; Model tools retains exact solid mirroring":enabled?action.title:action.id>=496&&action.id<=498?"Select a Construction Frame or its instance. Model > Frame creates the member program; individual solids and tubes are unsupported.":"Select a compatible part or assembly";}
            float iconSize=std::min(62.f,cardW-8);glyph(action.id==480?457:action.id,cursor+(cardW-iconSize)*.5f,103,iconSize,enabled?.9f:.35f);float scale=std::min(cardW<85?.70f:.79f,(cardW-8)/std::max(1.f,textWidth(action.title,1.f)));textOut(action.title,cursor+(cardW-textWidth(action.title,scale))*.5f,169,scale,enabled?.88f:.43f,enabled?.93f:.51f,enabled?.96f:.57f);if(enabled)g_zones.push_back({cursor,100,cardW-3,92,action.id});cursor+=cardW;
        }
        textOut(group.title,start+(cursor-start-textWidth(group.title,.70f))*.5f,198,.70f,.61f,.71f,.79f);if(groupIndex+1<groups.size()){rect(cursor+12,105,1,82,0,.28,.35,.42,.8,0,0,0,0,0);cursor+=27;}
    }
    const char* sideNames[]={"Model","Layers","Properties"};float sx=14;
    for(int tab=0;tab<3;++tab){float width=textWidth(sideNames[tab],.78f)+25;textOut(sideNames[tab],sx+9,(float)viewY+13,.78f,g_sidebarTab==tab?.94f:.61f,g_sidebarTab==tab?.97f:.70f,g_sidebarTab==tab?1.f:.78f);if(g_sidebarTab==tab)rect(sx,(float)viewY+38,width,2,0,.16,.76,.9,1,0,0,0,0,0);g_zones.push_back({sx,(float)viewY+4,width,36,410+tab});sx+=width+3;}
    if(g_sidebarTab==0){
    float treeY=(float)viewY+54;
    struct ExplorerItem{std::string name,subtitle;int zone=-1;bool selected=false;int depth=0;int kind=0;int icon=405;};
    std::vector<ExplorerItem> explorer;
    g_explorerEntities.clear();g_treeFoldIds.clear();
    explorer.push_back({g_path.substr(g_path.find_last_of("/\\")+1),"Document",-1,false,0,3,405});
    auto sheetChildren=[&](const Feature* feature,bool selected,int depth){if(!feature||g_collapsedFeatures.count(feature->id))return;auto lineage=featureManufacturingLineage(g_doc,*feature);auto* origin=g_doc.find(lineage.origin);if(!origin||!lineage.editableSheet||(feature->id!=origin->id&&!selected))return;
        struct Kind{const char* key;const char* title;int icon,edit,remove;};static const Kind kinds[]={{"flanges","Flange",403,122,123},{"hems","Return hem",407,395,396},{"stamps","Circular emboss",406,398,399},{"openings","Opening",406,125,126}};
        for(const auto& kind:kinds){auto rows=sheetEntityRows(*origin,kind.key);for(const auto& entity:rows.arr){const auto* id=entity.find("id");if(!id)continue;int zone=18000+(int)g_explorerEntities.size();g_explorerEntities.push_back({origin->id,kind.key,id->asStr(),kind.edit,kind.remove});auto* panel=entity.find(kind.key==std::string("flanges")||kind.key==std::string("hems")?"parent":"panel");
            bool active=g_selectedExplorerEntity&&g_selectedExplorerEntity->feature==origin->id&&g_selectedExplorerEntity->key==kind.key&&g_selectedExplorerEntity->id==id->asStr();explorer.push_back({std::string(kind.title)+" / "+id->asStr(),"Panel / "+(panel?sheetFieldText(*panel):"base"),zone,active,depth+1,0,kind.icon});
        }}
    };
    auto section=[&](const char* name,size_t count,int index){if(count)explorer.push_back({name,"",390+index,false,1,4,408});};
    bool assembled=!g_doc.assembly.empty();section(assembled?"Assembly":g_doc.features.size()==1&&isSheetFeature(g_doc.features.front().id)?"Sheet Metal":"Features",assembled?g_doc.assembly.size():g_doc.features.size(),assembled?0:1);
    size_t featureRows=assembled?(g_explorerOpen[0]?g_doc.assembly.size():0):(g_explorerOpen[1]?g_doc.features.size():0);
    // Authored history order need not be parent-first after grouping an existing
    // occurrence. Traverse the hierarchy for display, retaining source indices
    // for selection and fold actions; never reorder the authored document.
    std::vector<size_t> assemblyRows;
    if(assembled&&featureRows){
        std::map<u64,std::vector<size_t>> children;std::set<u64> nodeIds;
        for(size_t i=0;i<g_doc.assembly.size();++i){children[g_doc.assembly[i].parent].push_back(i);nodeIds.insert(g_doc.assembly[i].id);}
        std::vector<size_t> pending;std::vector<bool> visited(g_doc.assembly.size());
        auto traverse=[&](size_t root){pending.push_back(root);while(!pending.empty()){const auto i=pending.back();pending.pop_back();if(visited[i])continue;visited[i]=true;assemblyRows.push_back(i);if(const auto found=children.find(g_doc.assembly[i].id);found!=children.end())for(auto it=found->second.rbegin();it!=found->second.rend();++it)pending.push_back(*it);}};
        for(size_t i=0;i<g_doc.assembly.size();++i)if(!g_doc.assembly[i].parent||!nodeIds.count(g_doc.assembly[i].parent))traverse(i);
        for(size_t i=0;i<g_doc.assembly.size();++i)if(!visited[i])traverse(i);
    }
    for(size_t rowIndex=0;rowIndex<featureRows;++rowIndex){
        const size_t i=assembled?assemblyRows[rowIndex]:rowIndex;
        ExplorerItem row;
        if(g_doc.assembly.empty()){
            auto& f=g_doc.features[i];row.name=f.name;row.icon=explorerIcon(f);row.subtitle=explorerOperation(f)+" #"+std::to_string(f.id);row.zone=16000+(int)i;row.selected=g_explorerFeature==f.id;if(f.suppressed)row.subtitle+=" / suppressed";
            for(size_t j=0;j<g_bodyIds.size();++j)if(g_bodyIds[j]==f.id){row.zone=2000+(int)j;row.selected|=selBody==(int)j;
                if(j<g_hiddenBodies.size()&&g_hiddenBodies[j])row.subtitle+=" / hidden";}
        }else{
            auto& node=g_doc.assembly[i];bool group=node.feature==0;row.kind=group?3:0;row.selected=group&&g_viewportContainer==node.id;row.icon=group?404:405;row.name=node.name;row.zone=group?30000+(int)i:-1;auto* feature=g_doc.find(node.feature);if(feature)row.icon=explorerIcon(*feature);row.subtitle=group?"Subassembly":(feature?explorerOperation(*feature):"Part")+" #"+std::to_string(node.feature);
            u64 parent=node.parent;bool ancestorClosed=false;size_t traversed=0;while(parent&&traversed++<g_doc.assembly.size()){ancestorClosed|=g_closedAssemblyGroups.count(parent)!=0;row.depth=std::min(row.depth+1,5);auto it=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==parent;});if(it==g_doc.assembly.end())break;parent=it->parent;}if(ancestorClosed)continue;
            int copies=0,hiddenCopies=0;for(size_t j=0;j<g_instanceIds.size();++j)if(g_instanceIds[j]==node.id){row.zone=2000+(int)j;row.selected|=selBody==(int)j;++copies;
                if(j<g_hiddenBodies.size()&&g_hiddenBodies[j])++hiddenCopies;}
            if(hiddenCopies)row.subtitle+=hiddenCopies==copies?" / hidden":" / "+std::to_string(hiddenCopies)+" hidden";
        }
        auto* entityFeature=assembled?g_doc.find(g_doc.assembly[i].feature):&g_doc.features[i];bool selected=row.selected;row.depth+=2;int depth=row.depth;explorer.push_back(std::move(row));sheetChildren(entityFeature,selected,depth);
    }
    if(assembled){section("Feature history",g_doc.features.size(),1);if(g_explorerOpen[1])for(size_t k=0;k<g_doc.features.size();++k){auto& f=g_doc.features[k];explorer.push_back({f.name,explorerOperation(f)+" #"+std::to_string(f.id)+(f.suppressed?" / suppressed":""),16000+(int)k,g_explorerFeature==f.id,2,0,explorerIcon(f)});sheetChildren(&f,g_explorerFeature==f.id,2);}}
    section("Sketches",g_doc.sketches.size(),2);
    if(g_explorerOpen[2])for(size_t k=0;k<g_doc.sketches.size();++k){auto& sketch=g_doc.sketches[k];size_t users=0;for(const auto& f:g_doc.features){auto drivers=drivingSketchNames(f);if(std::find(drivers.begin(),drivers.end(),sketch.name)!=drivers.end())++users;}
        explorer.push_back({sketch.name,"Sketch / "+sketch.sk.plane+" / "+std::to_string(users)+" features",11000+(int)k,sketch.name==g_activeSketch&&g_showSketches,2,1,401});}
    section("Reference geometry",g_doc.planes.size()+g_doc.canvases.size(),3);
    if(g_explorerOpen[3]){for(size_t k=0;k<g_doc.planes.size();++k){auto& datum=g_doc.planes[k];explorer.push_back({datum.name,"Datum / "+datum.kind,14000+(int)k,datum.name==g_selectedDatumName,2,2,402});}
        for(size_t k=0;k<g_doc.canvases.size();++k){auto& canvas=g_doc.canvases[k];explorer.push_back({canvas.name,"Canvas / "+canvas.plane,-1,false,2,2,402});}}
    if(!g_doc.params.empty()){explorer.push_back({"Parameters","",403,false,1,4,408});if(g_parametersOpen)for(size_t i=0;i<g_doc.params.size();++i)explorer.push_back({g_doc.params[i].name+" = "+g_doc.params[i].expr,"Parameter expression",1000+(int)i,false,2,2,402});}
    if(!visibleSheetIds().empty()){explorer.push_back({"Manufacturing","",466,false,1,4,408});if(g_manufacturingOpen){explorer.push_back({"Flat pattern", "Exact unstamped blank / nominal stamped reference",320,false,2,2,403});explorer.push_back({"Bend sequence","Read-only geometric preview",322,false,2,2,403});}}
    g_explorerCount=(int)explorer.size();float treeBottom=H-48;
    g_treeViewport={8,(LONG)treeY-3,(LONG)viewX-8,(LONG)treeBottom};
    g_treeVisibleRows=std::max(1,(int)((treeBottom-(treeY-3))/28));g_treeMaxScroll=std::max(0,g_explorerCount-g_treeVisibleRows);
    int selectedZone=-1,selectedIndex=-1;for(size_t i=0;i<explorer.size();++i)if(explorer[i].selected&&explorer[i].zone>=0){selectedZone=explorer[i].zone;selectedIndex=(int)i;}
    if(selectedZone!=g_treeSelectedZone){g_treeSelectedZone=selectedZone;if(selectedIndex>=0){if(selectedIndex<g_treeScroll)g_treeScroll=selectedIndex;else if(selectedIndex>=g_treeScroll+g_treeVisibleRows)g_treeScroll=selectedIndex-g_treeVisibleRows+1;}}
    g_treeScroll=std::clamp(g_treeScroll,0,g_treeMaxScroll);
    const float treeRight=(float)viewX-(g_treeMaxScroll?25:8);const size_t treeTextStart=g_textVerts.size();
    glEnable(GL_SCISSOR_TEST);glScissor(8,winH-(int)treeBottom,(int)treeRight-8,(int)treeBottom-g_treeViewport.top);
    for(size_t i=(size_t)std::max(g_treeScroll,0);i<explorer.size()&&(int)(i-(size_t)g_treeScroll)<g_treeVisibleRows;++i){auto& row=explorer[i];float h=28.f;
        bool hover=mp.x>=8&&mp.x<treeRight&&mp.y>=treeY-3&&mp.y<treeY+h-3;
        if(row.selected)rect(8,treeY-3,treeRight-8,h,3,.16,.24,.30,1,0,0,0,0,0);else if(hover)rect(8,treeY-3,treeRight-8,h,3,.12,.17,.21,1,0,0,0,0,0);
        float branchX=17.f+row.depth*18;if(row.depth>0){rect(branchX-8,treeY-9,1,29,0,.28,.36,.42,.65f,0,0,0,0,0);rect(branchX-8,treeY+10,11,1,0,.28,.36,.42,.65f,0,0,0,0,0);}
        bool folder=row.kind==4,assemblyGroup=row.zone>=30000&&row.zone<31024;const Feature* rowFeature=row.zone>=2000&&row.zone<9000?g_doc.find(g_bodyIds[(size_t)(row.zone-2000)]):row.zone>=16000&&row.zone<17024?&g_doc.features[(size_t)(row.zone-16000)]:nullptr;bool children=assemblyGroup||(rowFeature&&featureManufacturingLineage(g_doc,*rowFeature).editableSheet);
        bool expanded=folder?(row.zone==403?g_parametersOpen:row.zone==466?g_manufacturingOpen:row.zone>=390&&row.zone<=393?g_explorerOpen[row.zone-390]:true):assemblyGroup?!g_closedAssemblyGroups.count(g_doc.assembly[(size_t)(row.zone-30000)].id):rowFeature&&!g_collapsedFeatures.count(rowFeature->id);
        if(folder||children){glUniform1i(uuGlyph,expanded?409:410);glUniform4f(uuRc,branchX-6,treeY+3,12,12);glUniform4f(uuFill,.65,.76,.84,1);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);}
        float iconX=branchX+((folder||children)?8:0);float cr=row.kind==1?.16f:folder?.96f:row.icon==406?.98f:.44f,cg=row.kind==1?.84f:folder?.72f:row.icon==406?.71f:.71f,cb=row.kind==1?.93f:folder?.22f:row.icon==406?.30f:.94f;
        glUniform1i(uuGlyph,row.icon);glUniform4f(uuRc,iconX,treeY,22,22);glUniform4f(uuFill,cr,cg,cb,1);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);
        bool datumRow=row.zone>=14000&&row.zone<15000;float titleRight=treeRight-(datumRow?29:7);float tx=iconX+28;std::string title=row.name;while(title.size()>1&&textWidth(title,.78f)>titleRight-tx)title.pop_back();if(title!=row.name&&title.size()>3)title=title.substr(0,title.size()-3)+"...";textOut(title,tx,treeY+2,.78f,.84f,.90f,.95f);
        if(hover)ribbonHint=row.name+(row.subtitle.empty()?"":" / "+row.subtitle);if(row.zone>=0)g_zones.push_back({8,treeY-3,treeRight-8,h,row.zone});
        if(row.zone>=2000&&row.zone<9000){auto* f=g_doc.find(g_bodyIds[(size_t)(row.zone-2000)]);if(f&&isSheetFeature(f->id)){int zone=28000+(int)g_treeFoldIds.size();g_treeFoldIds.push_back(f->id);g_zones.insert(g_zones.end()-1,{branchX-12,treeY-3,18,h,zone});}}
        else if(row.zone>=16000&&row.zone<17024){auto* f=&g_doc.features[(size_t)(row.zone-16000)];if(isSheetFeature(f->id)){int zone=28000+(int)g_treeFoldIds.size();g_treeFoldIds.push_back(f->id);g_zones.insert(g_zones.end()-1,{branchX-12,treeY-3,18,h,zone});}}
        if(datumRow){bool visible=datumVisible(row.name);float ex=treeRight-23;glUniform1i(uuGlyph,visible?406:410);glUniform4f(uuRc,ex,treeY+2,18,18);glUniform4f(uuFill,visible?.47f:.28f,visible?.82f:.39f,visible?.68f:.43f,1);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);g_zones.insert(g_zones.end()-1,{ex,treeY-3,23,h,32000+row.zone-14000});}
        treeY+=h;
    }
    glDisable(GL_SCISSOR_TEST);
    // Text is batched, so clip its vertices independently of the immediate GL scissor.
    for(size_t i=treeTextStart;i+41<g_textVerts.size();i+=42)for(size_t v=i;v<i+42;v+=7){g_textVerts[v]=std::clamp(g_textVerts[v],8.f,treeRight);g_textVerts[v+1]=std::clamp(g_textVerts[v+1],(float)g_treeViewport.top,treeBottom);}
    g_treeTrack=g_treeThumb={};
    if(g_treeMaxScroll){g_treeTrack={(LONG)viewX-22,g_treeViewport.top,(LONG)viewX-6,g_treeViewport.bottom};int height=g_treeTrack.bottom-g_treeTrack.top,thumb=std::max(36,(int)std::lround((double)height*g_treeVisibleRows/g_explorerCount)),travel=height-thumb,top=g_treeTrack.top+(int)std::lround((double)travel*g_treeScroll/g_treeMaxScroll);g_treeThumb={g_treeTrack.left+4,top,g_treeTrack.right-4,top+thumb};
        bool hover=PtInRect(&g_treeTrack,mp);rect((float)g_treeTrack.left,(float)g_treeTrack.top,16,(float)height,6,.075f,.10f,.13f,1,0,0,0,0,0);rect((float)g_treeThumb.left,(float)g_treeThumb.top,8,(float)thumb,4,hover||g_treeDragging?.38f:.25f,hover||g_treeDragging?.66f:.43f,hover||g_treeDragging?.80f:.54f,1,0,0,0,0,0);
        if(hover)ribbonHint="Scroll model / drag thumb, click track or use wheel";
    }
    }
    float captionY=(float)viewY+18,captionX=(float)viewX+(g_showViewCube?g_cubeS+30:20);
    std::string style=g_pathTracing?(g_trace.active?"Path trace":"Raster"):wireframe?"Wireframe":g_showEdges?"Shaded + edges":"Shaded";float styleX=W-158;
    rect(styleX,(float)viewY+16,140,34,6,.095,.13,.16,.92,.37,.46,.53,.8,1);glyph(1,styleX+9,(float)viewY+23,20,.80f);textOut(style,styleX+36,(float)viewY+24,.78f,.81f,.88f,.93f);glUniform1i(uuGlyph,409);glUniform4f(uuRc,styleX+118,(float)viewY+26,12,12);glUniform4f(uuFill,.67,.76,.84,1);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);g_zones.push_back({styleX,(float)viewY+16,140,34,455});
    if(g_pathTracing){std::string caption=g_trace.active?"GPU path tracing / "+std::to_string(g_trace.samples)+" spp / OpenGL BVH":"Path tracing requested / raster: "+g_trace.reason;caption=fitText(caption,.62f,std::max(0.f,styleX-captionX-14));textOut(caption,captionX,captionY+27,.62f,g_lightBackground?.24f:.76f,g_lightBackground?.30f:.83f,g_lightBackground?.37f:.89f);}
    if(g_allSheetsFlat)textOut("Nominal flat reference / display only",captionX,captionY,.70f,.62f,.77f,.85f);
    else if(g_stockMode)textOut("Nominal straight stock / display only",captionX,captionY,.70f,.62f,.77f,.85f);
    else if(g_allSheetsPlaying)textOut("Bend sequence / display only",captionX,captionY,.70f,.62f,.77f,.85f);
    if(g_pickKind){static const char* hints[]={"","FACE / hover previews / click replaces / Ctrl or Shift adds / Esc exits","EDGE / hover previews / click replaces / Ctrl or Shift adds / Esc exits","CURVE / click a sketch curve  |  Esc exits"};
        std::string hint=fitText((g_hoverReferenceHint.empty()?std::string{}:g_hoverReferenceHint+" / ")+hints[std::clamp(g_pickKind,0,3)],.62f,viewX+viewW-captionX-24);float hintY=captionY+50,width=textWidth(hint,.62f)+18;
        rect(captionX-1,hintY-4,width,27,6,.09f,.14f,.20f,.87f,.35f,.55f,.76f,.62f,1);
        textOut(hint,captionX+8,hintY,.62f,.79f,.91f,.99f);}
    if(g_projectionPreview.active){float x=captionX,y=(float)viewY+96,width=std::min(540.f,(float)viewW-g_cubeS-200.f);rect(x,y,width,52,8,.025f,.095f,.105f,.94f,.19f,.80f,.73f,.82f,1.2f);std::string title=std::string(g_projectionPreview.spatial?"3D INCLUDE PREVIEW / ":"2D PROJECTION PREVIEW / ")+g_projectionPreview.target;textOut(fitText(title,.70f,width-188),x+12,y+7,.70f,.64f,.98f,.93f);textOut(fitText(g_projectionPreview.source,.58f,width-188),x+12,y+29,.58f,.68f,.86f,.90f);button("Apply",x+width-169,y+10,72,539,true);button("Cancel",x+width-89,y+10,76,540);}
    size_t flatIndex=0;
    if(g_allSheetsFlat)for(size_t i=0;i<g_bodyIds.size();++i)if(isSheetFeature(g_bodyIds[i])){
        const Feature* feature=g_doc.find(g_bodyIds[i]);AABB bounds=g_reports[g_instancePart[i]].bounds.transformed(g_instanceWorld[i]);float sx,sy;
        std::string marker=std::to_string(++flatIndex);if(marker.size()<2)marker="0"+marker;
        if(feature&&bounds.valid()&&projectScreen(bounds.center()+Vec3{0,std::max(8.,bounds.extent().y*2.),0},sx,sy)&&sx>viewX+8&&sx<viewX+viewW-8&&sy>viewY+165&&sy<viewY+viewH-54&&!overViewCube(sx,sy)){
            rect(sx-13,sy-11,26,22,8,.075f,.10f,.14f,.90f,.40f,.59f,.75f,.62f,1);
            textOut(marker,sx-textWidth(marker,.61f)*.5f,sy-7,.61f,.91f,.96f,.99f);
        }
    }
    if(g_stockMode){const auto* rows=g_stockReport.find("members");
        auto instance=std::find(g_bodyIds.begin(),g_bodyIds.end(),g_stockFeatureId);
        size_t stockIndex=0;if(rows&&rows->t==arkjson::Value::Arr&&instance!=g_bodyIds.end())for(const auto& row:rows->arr){
            auto* station=row.find("row_y_mm"),*length=row.find("developed_length_mm");if(!station||!length)continue;
            size_t i=(size_t)std::distance(g_bodyIds.begin(),instance);float sx,sy;
            std::string marker=std::to_string(++stockIndex);if(marker.size()<2)marker="0"+marker;
            if(projectScreen(g_instanceWorld[i].transformPoint({-length->num*.5,station->num,0}),sx,sy)&&sx>viewX+8&&sx<viewX+viewW-8&&sy>viewY+160&&sy<viewY+viewH-54&&!overViewCube(sx,sy)){
                rect(sx-13,sy-12,26,22,8,.075f,.10f,.14f,.90f,.40f,.59f,.75f,.62f,1);
                textOut(marker,sx-textWidth(marker,.61f)*.5f,sy-8,.61f,.91f,.96f,.99f);
            }
        }
    }
    if(g_sidebarTab==1){
        const float rowTop=(float)viewY+60,rowBottom=H-44;
        label("DISPLAY VISIBILITY",20,rowTop,.66f);
        button("Show all",20,rowTop+26,(float)viewX-40,344);
        const int visibleRows=std::max(1,(int)((rowBottom-rowTop-72)/34));
        g_layersScroll=std::clamp(g_layersScroll,0,std::max(0,(int)g_bodyIds.size()-visibleRows));
        for(size_t i=(size_t)g_layersScroll;i<g_bodyIds.size()&&i<(size_t)(g_layersScroll+visibleRows);++i){
            float y=rowTop+78+(float)(i-g_layersScroll)*34;bool hidden=i<g_hiddenBodies.size()&&g_hiddenBodies[i];
            bool selected=(int)i==selBody;if(selected)rect(10,y-5,(float)viewX-20,31,4,.13,.19,.23,1,.25,.45,.56,.7,1);
            std::string name=g_instanceNames[i];while(textWidth(name,.76f)>viewX-82&&name.size()>4)name=name.substr(0,name.size()-4)+"...";
            textOut(name,21,y,.76f,hidden?.42f:.85f,hidden?.49f:.91f,hidden?.53f:.94f);
            textOut(hidden?"+":"o",(float)viewX-39,y,.86f,hidden?.42f:.41f,hidden?.50f:.80f,hidden?.57f:.97f);
            g_zones.push_back({13,y-5,(float)viewX-68,31,2000+(int)i});
            g_zones.push_back({(float)viewX-54,y-5,41,31,22000+(int)i});
        }
        label("Eye toggles visibility in this view",20,H-51,.61f);
    }
    float rx=((float)viewX-254)*.5f;
    std::string selected="No selection",kind="No body";if(selBody>=0){auto* f=g_doc.find(g_bodyIds[selBody]);if(f){selected=g_instanceNames[selBody];kind=f->type==FeatureType::ParametricSurface?"EXPRESSION SURFACE / MESH":std::string(featureTypeName(f->type))+" / TESSELLATED MESH";if(g_instanceIds[selBody])kind="INSTANCE #"+std::to_string(g_instanceIds[selBody])+" / "+featureTypeName(f->type);}}
    auto* sheet=selectedSheet();
    const Body* nativeBody=sheet?sheetOriginBody(sheet->id):selectedNativeBody();const auto* nativeReport=nativeBody?nativeBody->nativeData.find("report"):nullptr;
    if(nativeReport&&nativeReport->t!=arkjson::Value::Obj)nativeReport=nullptr;
    if(sheet)kind=sheet->type==FeatureType::SourceSheet?"SOURCE SHEET / NATIVE B-REP":"SHEET METAL / NATIVE B-REP";
    else if(nativeReport)kind="OCCT NATIVE SURFACE / B-REP";
    auto* activeSketch=g_showSketches?g_doc.findSketch(g_activeSketch):nullptr;
    if(selectedNotchedTube())kind="LASER TUBE / IDEAL HINGE PREVIEW";
    if(selectedSourceTube())kind="STEP SOURCE / FINITE BENDS";
    if(activeSketch){selected=activeSketch->name;kind="SKETCH / DIMENSION EXPRESSIONS";}
    auto* laser=selectedNotchedTube();auto* source=selectedSourceTube();if(activeSketch){laser=nullptr;source=nullptr;}
    if(g_inspectorOpen){
    const float W=rx+278;
    const float H=std::max(840.f,(float)winH-(viewY-76+42));
    const int maxScroll=std::max(0,(int)(H+viewY-76+42-winH));g_inspectorScroll=std::clamp(g_inspectorScroll,0,maxScroll);
    uiOffsetY=(float)viewY-76+42-g_inspectorScroll;
    const size_t inspectorTextStart=g_textVerts.size(),inspectorZoneStart=g_zones.size();
    glEnable(GL_SCISSOR_TEST);glScissor(0,32,viewX,std::max(1,winH-viewY-42-32));
    label("PROPERTIES",rx,101,.66f);label(nativeReport?"B-REP":"MESH",W-66,101,.62f);
    textOut(selected.substr(0,25),rx,135,1.02f);
    rect(rx,173,254,28,5,.15,.16,.19,1,0,0,0,0,0);textOut(kind.substr(0,36),rx+11,180,.59f,.76,.79,.85);
    std::string pickText;for(const auto& item:g_picked){if(!pickText.empty())pickText+=", ";pickText+=item.name.empty()?item.owner:item.name;}if(pickText.size()>42)pickText=pickText.substr(0,39)+"...";
    label(pickText.empty()?"No face, edge, or curve":pickText,rx,210,.58f);
    button("Body",rx,228,60,170,false,g_pickKind==0);button("Face",rx+64,228,60,171,false,g_pickKind==1);button("Edge",rx+128,228,60,172,false,g_pickKind==2);button("Curve",rx+192,228,62,173,false,g_pickKind==3);
    const bool assemblySelected=selBody>=0&&g_instanceIds[selBody]&&!activeSketch;
    if(!g_allSheetsFlat&&!g_stockMode){
    if(!laser&&!source&&!activeSketch)label(assemblySelected?"ASSEMBLY CONSTRAINTS":nativeReport?"NATIVE GEOMETRY":"GEOMETRY",rx,283,.66f);
    auto metric=[&](const char* title,const std::string& value,float y){label(title,rx,y,.76f);textOut(value,W-24-textWidth(value,.80f),y,.80f);};
    if(selBody>=0&&!laser&&!source&&!activeSketch&&!assemblySelected){auto& r=g_reports[g_instancePart[selBody]];
        if(nativeReport){
            auto formatted=[](const arkjson::Value* value,const char* suffix){if(!value||value->t!=arkjson::Value::Num)return std::string("n/a");if(std::string(suffix)=="mm3")return g_displayUnits.capacity(value->num);if(std::string(suffix)=="mm2")return g_displayUnits.area(value->num);if(std::string(suffix)=="mm")return g_displayUnits.distance(value->num);char text[80];std::snprintf(text,sizeof text,"%.3f %s",value->num,suffix);return std::string(text);};
            auto* kernel=nativeReport->find("kernel"),*version=nativeReport->find("kernel_version"),*valid=nativeReport->find("brep_valid");
            metric("Kernel",(kernel?kernel->asStr():"OCCT")+std::string(" ")+(version?version->asStr():""),314);
            metric("B-rep valid",valid&&valid->t==arkjson::Value::Bool?(valid->b?"yes":"no"):"n/a",343);
            metric(sheet?"Formed area":"Area",formatted(nativeReport->find("area_mm2"),"mm2"),372);
            metric(sheet?"Formed volume":"Volume",formatted(nativeReport->find("volume_mm3"),"mm3"),405);
            metric(sheet?"Flat volume":"Faces",formatted(nativeReport->find(sheet?"flat_volume_mm3":"faces"),sheet?"mm3":""),438);
            rect(rx,472,254,1,0,.17,.21,.22,1,0,0,0,0,0);
            if(sheet){
                label(sheet->type==FeatureType::SourceSheet?"SOURCE SHEET / READ ONLY":"DEVELOPED SHEET",rx,492,.66f);
                auto* holes=nativeBody->nativeData.find("holes");
                auto* openingCount=nativeReport->find("opening_count");
                metric("Openings",holes&&holes->t==arkjson::Value::Arr?std::to_string(holes->arr.size()):openingCount&&openingCount->t==arkjson::Value::Num?std::to_string((int)openingCount->num):"n/a",523);
                if(sheet->type==FeatureType::SourceSheet){
                    auto* verified=nativeReport->find("source_projection_verified");
                    metric("Source projection",verified&&verified->t==arkjson::Value::Bool?(verified->b?"verified":"unverified"):"n/a",553);
                    if(H>940){metric("Refold delta",formatted(nativeReport->find("refold_symmetric_difference_volume_mm3"),"mm3"),584);
                        metric("Flat length",formatted(nativeReport->find("flat_length_mm"),"mm"),613);
                        auto* hash=nativeReport->find("source_sha256");if(hash&&hash->t==arkjson::Value::Str)label("SOURCE SHA256  "+hash->str.substr(0,16),rx,646,.58f);}
                }else{auto* panels=nativeBody->nativeData.find("panels"),*bends=nativeBody->nativeData.find("bends");
                    if(H>900)metric("Panels",panels&&panels->t==arkjson::Value::Arr?std::to_string(panels->arr.size()):"n/a",553);
                    if(H>940)metric("Bends",bends&&bends->t==arkjson::Value::Arr?std::to_string(bends->arr.size()):"n/a",584);}
                if(H>940){if(sheet->type==FeatureType::SheetMetal){char preview[70];std::snprintf(preview,sizeof preview,"Display-only bend preview %.0f%%",sheetDisplayProgress(sheet->id)*100);label(preview,rx,675,.62f);}else label(g_sheetFlatViews[sheet->id]?"Displaying flat mesh":"Displaying formed mesh",rx,675,.62f);}
            }else label("Exact native shape / tessellated display",rx,492,.61f);
        }else if(auto* suspension=selectedSuspension()){
            const auto& report=g_suspViews[suspension->id].preview.report;
            auto measure=[&](const arkjson::Value* object,const char* key,const char* unit){const auto* value=object?object->find(key):nullptr;
                if(!value||value->t!=arkjson::Value::Num)return std::string("n/a");char output[70];std::snprintf(output,sizeof output,"%.3f %s",value->num,unit);return std::string(output);};
            label("KINEMATIC PREVIEW",rx,314,.66f);
            metric("Jounce",measure(&report,"jounce_mm","mm"),348);metric("Rack",measure(&report,"rack_mm","mm"),380);
            metric("Toe",measure(&report,"toe_deg","deg"),412);metric("Camber",measure(&report,"camber_deg","deg"),444);
            metric("Link closure",measure(&report,"closure_residual_mm_max","mm"),476);
            metric("Spring force",measure(report.find("spring"),"force_n","N"),508);
            rect(rx,545,254,1,0,.17,.21,.22,1,0,0,0,0,0);
            label("Rigid-link closure checked",rx,566,.63f);label("Vehicle dynamics not checked",rx,588,.60f);
        }else{
            metric("Vertices",std::to_string(r.vertices),314);metric("Triangles",std::to_string(r.triangles),343);metric("Components",std::to_string(r.components),372);
            char size[100];auto e=r.bounds.extent();std::snprintf(size,sizeof size,"%.1f / %.1f / %.1f",e.x,e.y,e.z);metric("XYZ mm",size,392);
            rect(rx,429,254,1,0,.17,.21,.22,1,0,0,0,0,0);label("TOPOLOGY AUDIT",rx,453,.66f);
            metric("Boundary edges",std::to_string(r.boundaryEdges),486);metric("Non-manifold edges",std::to_string(r.nonManifoldEdges),519);
            metric("Degenerate faces",std::to_string(r.degenerateTriangles),552);
            textOut(r.closedOriented()?"o  Closed, oriented edges":"o  Open or irregular topology",rx,587,.73f,.76,.81,.87);
            label("Self-intersections not checked",rx,613,.62f);
        }
    }
    if(assemblySelected){
        const u64 selectedNode=g_instanceIds[selBody];
        auto node=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==selectedNode;});
        if(node!=g_doc.assembly.end()){
            metric("Instance #",std::to_string(node->id),314);
            metric("Grounded",node->fixed?"yes":"no",343);
            metric("Parent #",std::to_string(node->parent),372);
            metric("Solver rank",std::to_string(g_assemblyDiagnostics.rank),405);
            metric("Remaining DOFs",std::to_string(g_assemblyDiagnostics.freeDofs),438);
            char residual[80];std::snprintf(residual,sizeof residual,"%.4g mm / %.4g deg",g_assemblyDiagnostics.maxPositionMm,g_assemblyDiagnostics.maxAngleDeg);
            metric("Max residual",residual,471);
            rect(rx,506,254,1,0,.17,.21,.22,1,0,0,0,0,0);
            label("MATES / CLICK TO SELECT",rx,525,.62f);
            std::vector<size_t> related;for(size_t i=0;i<g_doc.mates.size();++i)
                if(g_doc.mates[i].a==selectedNode||g_doc.mates[i].b==selectedNode)related.push_back(i);
            int maxMateRows=std::clamp((int)((H-238.f-548.f+5.f)/42.f),1,4);
            g_matePage=std::min(g_matePage,std::max(0,((int)related.size()-1)/maxMateRows));
            int visible=0;
            for(size_t k=(size_t)g_matePage*maxMateRows;k<related.size()&&visible<maxMateRows;++k){
                const size_t i=related[k];const auto& mate=g_doc.mates[i];
                const float y=548.f+visible*42;const bool picked=(int)i==g_selectedMate;
                if(picked)rect(rx-3,y-5,258,38,5,.22,.20,.17,1,.77,.53,.25,.8,1);
                textOut(("#"+std::to_string(mate.id)+" "+mate.type).substr(0,27),rx+4,y,.72f);
                std::string detail=std::to_string(mate.a)+" : "+mate.reference_a+"  /  "+std::to_string(mate.b)+" : "+mate.reference_b;
                label(detail.substr(0,37),rx+4,y+20,.58f);
                g_zones.push_back({rx-3,y-5,258,38,13000+(int)i});++visible;
            }
            if(!visible)label("No mates on this instance",rx+4,553,.65f);
            if(related.size()>(size_t)maxMateRows){
                button("<",rx+158,512,30,97);button(">",rx+191,512,30,98);
                label(std::to_string(g_matePage+1)+"/"+std::to_string((related.size()+maxMateRows-1)/maxMateRows),rx+225,520,.52f);
            }
        }
    }
    if(H>1000 && kind=="EXPRESSION SURFACE / MESH"){rect(rx,673,254,1,0,.17,.21,.22,1,0,0,0,0,0);label("MATHEMATICAL MODEL",rx,697,.66f);
        textOut("x(u,v)   y(u,v)   z(u,v)",rx,730,.85f,.70,.82,.79);label("Trigonometry + named parameters",rx,761,.67f);label("Periodic seams validated",rx,786,.67f);}
    if(selBody>=0&&g_tubeInfo.count(g_bodyIds[selBody])&&H>940){
        auto& report=g_tubeInfo[g_bodyIds[selBody]];char length[80];std::snprintf(length,sizeof length,"%.2f mm",report.find("centerline_length_mm")->num);
        label("TUBE PROGRAM",rx,680,.66f);metric("Developed axis",length,710);metric("Bends",std::to_string(report.find("bends")->arr.size()),743);
        button("Edit tube program",rx,H-214,252,30);
    }
    if(activeSketch){
        label("CONSTRUCTION REFERENCE",rx,243,.63f);metric("Plane",activeSketch->sk.plane,273);metric("Entities",std::to_string(activeSketch->sk.ents.size()),306);
        char value[64];std::snprintf(value,sizeof value,"%.4g mm",activeSketch->sk.tolerance);metric("Chord tolerance",value,339);
        metric("Free dimensions",std::to_string(activeSketch->freeDofs),372);metric("Local solver rank",std::to_string(activeSketch->rank),405);std::snprintf(value,sizeof value,"%.3g",activeSketch->residual);metric("Residual",value,438);
        {
            label("CONSTRAINTS / CLICK TO SELECT",rx,474,.62f);
            int start=g_sketchConstraintPage*4;
            auto* diagnosed=g_sketchDiagnosis.find("constraints");
            for(int row=0;row<4&&start+row<(int)activeSketch->sk.cons.size();++row){int index=start+row;auto& con=activeSketch->sk.cons[(size_t)index];float y=494.f+row*32;
                bool picked=index==g_selectedSketchConstraint;if(picked)rect(rx-3,y-4,258,29,5,.22,.20,.17,1,.77,.53,.25,.8,1);
                std::string title=std::to_string(index+1)+" "+constraintTypeName(con.type);if(con.a>=0&&con.a<(int)activeSketch->sk.ents.size())title+=" / "+entityChoiceLabel(activeSketch->sk,con.a);
                auto* diagnosis=diagnosed&&index<(int)diagnosed->arr.size()?&diagnosed->arr[(size_t)index]:nullptr;
                bool violated=diagnosis&&diagnosis->find("violated")&&diagnosis->find("violated")->b;
                bool redundant=diagnosis&&diagnosis->find("locally_redundant")&&diagnosis->find("locally_redundant")->b;
                textOut(title.substr(0,25),rx+4,y,.72f,violated?1.f:.84f,violated?.43f:.85f,violated?.42f:.88f);
                std::string binding=con.expression.empty()?drawingNumber(con.val):con.expression;label((violated?"conflict":redundant?"redundant":"satisfied")+std::string(" / ")+binding.substr(0,20),rx+4,y+17,.55f);
                g_zones.push_back({rx-3,y-4,258,29,12000+index});
            }
            if(activeSketch->sk.cons.size()>4){button("<",rx,625,34,91);button(">",rx+38,625,34,92);label(std::to_string(g_sketchConstraintPage+1)+" / "+std::to_string((activeSketch->sk.cons.size()+3)/4),rx+88,632,.58f);}
        }
        button("Edit entity",rx,H-238,121,70);button("Edit constraint",rx+131,H-238,121,89);
        button("Add constraint",rx,H-198,121,73);button("Remove",rx+131,H-198,121,90);
        button("Extrude",rx,H-158,121,65,true);button("Cut",rx+131,H-158,121,66);
        button("Source parameters",rx,H-118,252,577);button(g_showSketches?"Hide sketch":"Show sketch",rx,H-78,121,68);button(g_showDatumPlanes?"Hide datums":"Show datums",rx+131,H-78,121,72);
    }else if(assemblySelected){
        const u64 selectedNode=g_instanceIds[selBody];
        auto node=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==selectedNode;});
        button(node!=g_doc.assembly.end()&&node->fixed?"Unground":"Ground",rx,H-238,121,93);
        button("Placement",rx+131,H-238,121,29);
        button("Add mate",rx,H-198,121,94,true);
        button("Edit mate",rx+131,H-198,121,95);
        button("Remove mate",rx,H-158,121,96);
        button("Duplicate",rx+131,H-158,121,28);
        button("Delete selected",rx,H-78,252,25);
    }else if(sheet){
        if(sheet->type==FeatureType::SheetMetal){
            button("Edit base",rx,H-286,121,127);button("Add flange",rx+131,H-286,121,121,true);
            button("Edit flange",rx,H-246,121,122);button("Add opening",rx+131,H-246,121,124);
            button("Formed",rx,H-206,121,83,false,sheetDisplayProgress(sheet->id)>=1);button("Flat",rx+131,H-206,121,84,false,sheetDisplayProgress(sheet->id)<=0);
            button(g_sheetPlayingId==sheet->id?"Pause":"Play bends",rx,H-166,121,128);button("More sheet tools",rx+131,H-166,121,59);
            button("Native STEP",rx,H-126,121,82,true);button("Flat DXF",rx+131,H-126,121,327);button("Delete selected",rx,H-78,252,25);
        }else{
            button("Formed",rx,H-210,121,83,false,!g_sheetFlatViews[sheet->id]);button("Flat",rx+131,H-210,121,84,false,g_sheetFlatViews[sheet->id]);
            button("Export native STEP",rx,H-162,252,82,true);
            button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
        }
    }else if(nativeReport&&!laser&&!source){
        auto* selectedFeature=selBody>=0?g_doc.find(g_bodyIds[(size_t)selBody]):nullptr;
        if(selectedFeature&&selectedFeature->type==FeatureType::NativeSolid){
            button("Hole / pocket",rx,H-246,121,103);button("Fillet / edge",rx+131,H-246,121,104);
            button("Datum face",rx,H-204,121,113);button("Exact Boolean",rx+131,H-204,121,102);
        }
        if(selectedFeature){
            bool sketchDrive=!drivingSketchNames(*selectedFeature).empty();
            float y=selectedFeature->type==FeatureType::NativeSolid?H-286:H-246;
            button("Edit feature",rx,y,sketchDrive?121.f:252.f,161,true);
            if(sketchDrive)button("Edit sketch",rx+131,y,121,162);
        }
        button("Export native STEP",rx,H-162,252,82,true);
        button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
        button("Delete selected",rx,H-78,252,25);
    }else if(laser){
        button("Stock",rx,278,121,41);button("Add bend",rx+131,278,121,42,true);
        label(laser->get("width_mm")+" x "+laser->get("height_mm")+" / wall "+laser->get("wall_mm")+" mm",rx,322,.69f);
        label("STRAIGHT LENGTH  "+laser->get("length_mm")+" mm",rx,346,.62f);
        label("BEND POINTS / RETAINED WALL",rx,383,.62f);
        int visible=std::max(1,(int)(H-654)/45);g_notchScroll=std::max(0,std::min(g_notchScroll,(int)laser->notches.size()-visible));
        for(int k=g_notchScroll;k<(int)laser->notches.size()&&k<g_notchScroll+visible;++k){auto& n=laser->notches[k];float y=410+(k-g_notchScroll)*45;
            if(k==g_selectedNotch)rect(rx,y-6,254,41,5,.19,.21,.25,1,.38,.42,.50,.7,1);
            textOut(std::to_string(k+1)+"   "+n.station.substr(0,14)+" mm",rx+10,y,.71f);
            label(n.angle+" deg  /  "+n.face,rx+10,y+20,.62f);g_zones.push_back({rx,y-6,254,41,9000+k});
        }
        if(laser->notches.empty()){label("Straight stock. Add a bend point",rx+8,420,.67f);label("to define a retained-wall notch.",rx+8,444,.67f);}
        button("Edit point",rx,H-234,121,43);button("Remove point",rx+131,H-234,121,44);
        label("Ideal rigid hinges / visual preview",rx,H-184,.64f);label("Wall strain + collisions not certified",rx,H-161,.61f);
        button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
        button("Export straight STEP",rx,H-78,252,51,true);
    }else if(source){
        auto& cache=g_sourceViews.at(source->id);auto& report=cache.preview.report;
        label("SOURCE GEOMETRY / READ ONLY",rx,284,.62f);
        metric("Finite bends",std::to_string(cache.preview.bends.size()),315);
        auto* count=report.find("opening_count");metric("Source openings",count?std::to_string((int)count->num):std::to_string(cache.preview.holes.arr.size()),348);
        char extent[64];std::snprintf(extent,sizeof extent,"%.3f mm",g_reports[g_instancePart[selBody]].bounds.extent().x);metric("Straight span",extent,379);
        auto* projection=report.find("source_projection_verified");bool verified=projection&&projection->b;
        textOut(verified?"Source projection verified":"Source projection not verified",rx,416,.72f,verified?.75f:.94f,verified?.82f:.68f,verified?.90f:.51f);
        auto* sha=report.find("source_sha256");label("SHA256  "+(sha?sha->asStr().substr(0,18):std::string("unavailable")),rx,442,.58f);
        rect(rx,475,254,1,0,.22,.24,.28,1,0,0,0,0,0);label("ORIGINAL OPENINGS",rx,494,.62f);
        int visible=std::max(1,(int)(H-738)/43);int countHoles=(int)cache.preview.holes.arr.size();g_sourceScroll=std::max(0,std::min(g_sourceScroll,countHoles-visible));
        for(int k=g_sourceScroll;k<countHoles&&k<g_sourceScroll+visible;++k){auto& opening=cache.preview.holes.arr[k];auto* kind=opening.find("kind");auto* segment=opening.find("segment");auto* center=opening.find("flat_center");float y=525+(k-g_sourceScroll)*43;
            if(k==g_sourceOpening)rect(rx-5,y-5,260,40,5,.19,.21,.25,1,.38,.42,.50,.7,1);g_zones.push_back({rx-5,y-5,260,40,10000+k});
            textOut(std::to_string(k+1)+"  "+(kind?kind->asStr().substr(0,24):std::string("Source opening")),rx,y,.69f);
            char where[100];std::snprintf(where,sizeof where,"Span %d / flat x %.2f mm",segment?(int)segment->num+1:0,center&&center->arr.size()>=3?center->arr[0].num:0.);label(where,rx,y+20,.59f);
        }
        if(!countHoles)label("No opening records in this package",rx,526,.62f);
        if(countHoles>visible)label("Scroll / click an opening to inspect",rx,H-227,.58f);
        label("Finite-radius geometry preview",rx,H-184,.64f);label("Manufacturing qualification pending",rx,H-161,.60f);
        button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
        button("Export straight STEP",rx,H-78,252,51,true);
    }else{
        if(auto* part=selBody>=0&&!g_instanceIds[selBody]?g_doc.find(g_bodyIds[(size_t)selBody]):nullptr){
            bool sketchDrive=!drivingSketchNames(*part).empty();
            button("Edit feature",rx,H-204,sketchDrive?121.f:252.f,161,true);
            if(sketchDrive)button("Edit sketch",rx+131,H-204,121,162);
        }
        button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
        button(selBody>=0&&!g_instanceIds[selBody]&&g_tubeInfo.count(g_bodyIds[selBody])?"Bend program":"Placement",rx,H-162,121,29);button("Duplicate",rx+131,H-162,121,28);
        button("Delete selected",rx,H-78,252,25);
    }
    }
    // Keep the complete inspector accessible by scrolling, and prevent hidden
    // buttons or batched glyphs from leaking into the ribbon/status bar.
    for(size_t i=inspectorZoneStart;i<g_zones.size();++i)g_zones[i].y+=uiOffsetY;
    g_zones.erase(std::remove_if(g_zones.begin()+inspectorZoneStart,g_zones.end(),[&](const Zone& z){return z.y<viewY+42||z.y+z.h>winH-32;}),g_zones.end());
    size_t write=inspectorTextStart;
    for(size_t read=inspectorTextStart;read+41<g_textVerts.size();read+=42){float lo=1e9f,hi=-1e9f;for(int v=0;v<6;++v){lo=std::min(lo,g_textVerts[read+v*7+1]);hi=std::max(hi,g_textVerts[read+v*7+1]);}
        if(lo>=viewY+42&&hi<=winH-32){std::copy_n(g_textVerts.begin()+read,42,g_textVerts.begin()+write);write+=42;}}
    g_textVerts.resize(write);uiOffsetY=0;glDisable(GL_SCISSOR_TEST);
    }
    if(g_physicalVisible&&g_sidebarTab==2){
        float px=((float)viewX-280)*.5f,py=(float)viewY+66;
        // Text is batched after panels. Remove covered inspector glyphs and hit
        // zones so the drawer is visually opaque and cannot click through.
        std::vector<float> visibleText;visibleText.reserve(g_textVerts.size());
        for(size_t offset=0;offset+41<g_textVerts.size();offset+=42){float minX=1e9f,minY=1e9f,maxX=-1e9f,maxY=-1e9f;for(int v=0;v<6;++v){float x=g_textVerts[offset+(size_t)v*7],y=g_textVerts[offset+(size_t)v*7+1];minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);}if(maxX<px||minX>px+280||maxY<py||minY>py+300)visibleText.insert(visibleText.end(),g_textVerts.begin()+offset,g_textVerts.begin()+offset+42);}
        g_textVerts=std::move(visibleText);g_zones.erase(std::remove_if(g_zones.begin(),g_zones.end(),[&](const auto& zone){return zone.x<px+280&&zone.x+zone.w>px&&zone.y<py+300&&zone.y+zone.h>py;}),g_zones.end());panel(px,py,280,300,8);
        label(g_physicalAssembly?"ASSEMBLY PHYSICAL PROPERTIES":"BODY PHYSICAL PROPERTIES",px+12,py+11,.60f);
        if(!g_doc.assembly.empty()&&selBody>=0)button(g_physicalAssembly?"Part":"Assembly",px+112,py+34,91,190);
        button("Loads",px+12,py+34,70,216);button("Close",px+215,py+34,53,189);
        const auto* mass=g_physicalReport.find("mass_g");const auto* weight=g_physicalReport.find("weight_N");
        const auto* density=g_physicalReport.find("density_g_cm3");const auto* center=g_physicalReport.find("center_mm");
        auto fmt=[](double value,const char* unit){char buffer[96];std::snprintf(buffer,sizeof buffer,"%.4g %s",value,unit);return std::string(buffer);};
        if(mass&&mass->t==arkjson::Value::Num&&weight&&weight->t==arkjson::Value::Num){
            label("Density",px+12,py+76,.62f);textOut(density&&density->t==arkjson::Value::Num?fmt(density->num,"g/cm3"):"mixed materials",px+112,py+76,.62f);
            label("Mass",px+12,py+101,.62f);textOut(g_displayUnits.weight(mass->num),px+112,py+101,.72f);
            label("Weight",px+12,py+126,.62f);textOut(fmt(weight->num,"N"),px+112,py+126,.62f);
            if(center&&center->t==arkjson::Value::Arr&&center->arr.size()==3){
                char xyz[112];std::snprintf(xyz,sizeof xyz,"%.2f, %.2f, %.2f",center->arr[0].num/DisplayUnits::lengthFactor(g_displayUnits.length),center->arr[1].num/DisplayUnits::lengthFactor(g_displayUnits.length),center->arr[2].num/DisplayUnits::lengthFactor(g_displayUnits.length));
                label(g_physicalAssembly?"World COM":"Local COM",px+12,py+151,.62f);textOut(std::string(xyz)+" "+g_displayUnits.length,px+12,py+173,.68f);
                Vec3 point{center->arr[0].num,center->arr[1].num,center->arr[2].num};if(!g_physicalAssembly)point=g_instanceWorld[(size_t)selBody].transformPoint(point);
                double cw=vp.m[3]*point.x+vp.m[7]*point.y+vp.m[11]*point.z+vp.m[15];Vec3 clip=vp.transformPoint(point);
                if(cw>1e-5){float sx=viewX+(float)((clip.x/cw+1.)*.5*viewW),sy=viewY+(float)((1.-clip.y/cw)*.5*viewH);
                    if(sx>viewX+118&&sx<viewX+viewW-12&&sy>viewY+145&&sy<viewY+viewH-45){
                        rect(sx-12,sy-12,24,24,12,.04,.10,.15,.94,.39,.85,1,1,2);rect(sx-16,sy-1,32,2,1,.48,.88,1,1,0,0,0,0,0);rect(sx-1,sy-16,2,32,1,.48,.88,1,1,0,0,0,0,0);textOut(g_physicalReport.find("center_of_gravity_mm")&&g_physicalReport.find("center_of_gravity_mm")->t==arkjson::Value::Arr?"CG / COM":"COM",sx+19,sy-9,.65f,.32f,.78f,.96f);
                    }
                }
            }
            if(const auto* inertia=g_physicalReport.find("inertia_kg_m2");inertia&&inertia->arr.size()==9){
                rect(px+10,py+203,260,48,6,.055,.075,.095,1,.22,.35,.45,.6,1);label("INERTIA AT COM  /  kg.m2",px+19,py+210,.49f);
                char principal[120];std::snprintf(principal,sizeof principal,"X %.3g   Y %.3g   Z %.3g",inertia->arr[0].num,inertia->arr[4].num,inertia->arr[8].num);textOut(principal,px+19,py+229,.60f,.67f,.87f,.98f);
            }
            if(const auto* energy=g_physicalReport.find("kinetic_energy_J")){label("Kinetic energy",px+12,py+267,.57f);textOut(fmt(energy->num,"J"),px+150,py+267,.62f);}
        }else{
            label("Mass unavailable",px+12,py+78,.72f);
            std::string reason=g_physicalError.empty()?"Assign density to every exact solid":g_physicalError;
            for(size_t line=0;line<4&&line*37<reason.size();++line)textOut(reason.substr(line*37,37),px+12,py+109+(float)line*18,.62f,.89f,.67f,.50f);
            label("Exact geometry + declared density",px+12,py+198,.56f);
        }
    }
    if(g_inspectorOpen&&(g_allSheetsFlat||g_stockMode)&&!g_physicalVisible){
        const float W=((float)viewX-280)*.5f+292;
        float listY=(float)viewY+180,listH=std::max(120.f,H-listY-42.f);panel(W-292,listY,280,listH,8);
        label(g_allSheetsFlat?"UNFOLDED BLANKS / INDEX":"STRAIGHT STOCK / CENTERLINE",W-278,listY+12,.64f);
        rect(W-278,listY+37,252,1,0,.30,.39,.50,.65,0,0,0,0,0);
        int maxRows=std::max(1,(int)((listH-63)/22));int row=0;float y=listY+50;
        auto indexText=[](size_t n){std::string value=std::to_string(n);return value.size()<2?"0"+value:value;};
        if(g_allSheetsFlat){std::vector<u64> order;std::unordered_map<u64,size_t> counts;
            for(u64 id:g_bodyIds)if(isSheetFeature(id)){if(!counts.count(id))order.push_back(id);++counts[id];}
            size_t start=1;for(u64 id:order){size_t quantity=counts[id];if(row++<maxRows){const Feature* feature=g_doc.find(id);
                    std::string range=indexText(start);if(quantity>1)range+="-"+indexText(start+quantity-1);
                    textOut(range,W-278,y,.63f,.62f,.84f,.96f);
                    textOut(feature?feature->name.substr(0,21):"Sheet",W-225,y,.62f,.86f,.90f,.95f);
                    if(quantity>1)textOut("x"+std::to_string(quantity),W-47,y,.59f,.63f,.74f,.84f);
                    y+=22;}start+=quantity;}
            if(row>maxRows)label("+ "+std::to_string(row-maxRows)+" more patterns",W-278,listY+listH-25,.57f);
        }else{const auto* members=g_stockReport.find("members");
            if(members&&members->t==arkjson::Value::Arr){size_t index=0;for(const auto& member:members->arr){++index;if((int)index>maxRows)continue;
                    auto* name=member.find("source_name"),*length=member.find("developed_length_mm");
                    textOut(indexText(index),W-278,y,.62f,.62f,.84f,.96f);
                    textOut(name?name->asStr().substr(0,20):"Member",W-247,y,.59f,.85f,.90f,.95f);
                    if(length&&length->t==arkjson::Value::Num){char size[32];std::snprintf(size,sizeof size,"%.0f mm",length->num);
                        textOut(size,W-26-textWidth(size,.55f),y,.55f,.65f,.75f,.84f);}y+=22;}
                if((int)members->arr.size()>maxRows)label("+ "+std::to_string(members->arr.size()-(size_t)maxRows)+" more stock rows",W-278,listY+listH-25,.57f);
            }}
    }
    float cy=H-32-assistantDockHeight();
    if(laser||source){float py=(float)viewY+(float)viewH-101.f;rect(viewX+126,py-5,viewW-142,43,7,.10,.11,.13,.96,.25,.27,.31,.8,1);
        float x=viewX+136;button("Straight",x,py,76,45,false,g_foldProgress==0);button("Folded",x+84,py,76,46,false,g_foldProgress==1);button(g_foldPlaying?"Pause":"Play",x+168,py,62,47,true);
        float length=std::max(80.f,(float)viewW-408);rect(x+244,py+14,length,5,2,.23,.26,.31,1,0,0,0,0,0);rect(x+244,py+14,std::max(3.f,length*(float)g_foldProgress),5,2,.72,.78,.88,1,0,0,0,0,0);g_zones.push_back({x+244,py+2,length,29,50});}
    else if(sheet&&sheet->type==FeatureType::SheetMetal&&nativeBody&&nativeBody->nativeData.find("animation")){
        float py=(float)viewY+(float)viewH-101.f;rect(viewX+126,py-5,viewW-142,43,7,.10,.11,.13,.96,.25,.27,.31,.8,1);
        float x=viewX+136;double progress=sheetDisplayProgress(sheet->id);
        button("Flat",x,py,76,84,false,progress<=0);button("Formed",x+84,py,76,83,false,progress>=1);
        if(sheetHasStamp(*sheet))label("Stamped blank: nominal flat reference",x+170,py+9,.60f);
        else{button(g_sheetPlayingId==sheet->id?"Pause":"Play",x+168,py,62,128,true);
        float length=std::max(80.f,(float)viewW-408);rect(x+244,py+14,length,5,2,.23,.26,.31,1,0,0,0,0,0);rect(x+244,py+14,std::max(3.f,length*(float)progress),5,2,.72,.78,.88,1,0,0,0,0,0);g_zones.push_back({x+244,py+2,length,29,53});}}
    else if(auto* suspension=selectedSuspension()){
        float py=(float)viewY+(float)viewH-151.f;rect(viewX+12,py,viewW-24,82,10,.065f,.083f,.11f,.92f,.36f,.46f,.59f,.57f,1);
        const auto& state=g_suspViews[suspension->id].state;const double values[]={state.jounceMm,state.rackMm,state.wheelSpinDeg};
        const auto& report=g_suspViews[suspension->id].preview.report;auto* toe=report.find("toe_deg"),*closure=report.find("closure_residual_mm_max");
        auto* spring=report.find("spring");auto* force=spring?spring->find("force_n"):nullptr;
        char telemetry[160];std::snprintf(telemetry,sizeof telemetry,"Toe %.2f deg   /   Spring %.0f N   /   Closure %.3g mm",toe?toe->num:0.,force?force->num:0.,closure?closure->num:0.);
        textOut(telemetry,viewX+20,py+31,.53f,.64f,.76f,.88f);
        const char* titles[]={"JOUNCE","STEERING RACK","WHEEL SPIN"};float cell=(viewW-118.f)/3.f;
        for(int control=0;control<3;++control){float x=viewX+20+control*(cell+8),track=cell-12;auto range=suspensionRange(*suspension,control);
            float fraction=(float)clamp((values[control]-range.first)/(range.second-range.first),0.,1.);
            char value[45];std::snprintf(value,sizeof value,control==2?"%.0f deg":"%.1f mm",values[control]);
            textOut(titles[control],x,py+9,.55f,.72f,.84f,.95f);textOut(value,x+track-textWidth(value,.57f),py+9,.57f,.87f,.94f,1.f);
            rect(x,py+53,track,5,2,.23f,.30f,.39f,1,0,0,0,0,0);rect(x,py+53,std::max(2.f,track*fraction),5,2,.42f,.78f,.98f,1,0,0,0,0,0);
            rect(x+track*fraction-6,py+49,12,13,6,.90f,.96f,1.f,1,.35f,.55f,.75f,.75f,1);
            g_zones.push_back({x,py+32,track,38,380+control});
        }
        button(g_suspSpinPlaying?"Pause":"Play",viewX+viewW-75,py+25,58,383,false,g_suspSpinPlaying);
    }
    if(g_assistantDockOpen){
    panel(viewX,cy,(float)viewW,(float)assistantDockHeight());
    button("Single",viewX+22,cy+7,74,470,false,!g_teamMode);button("Team",viewX+104,cy+7,74,471,false,g_teamMode);
    std::string connector=!g_teamMode&&g_assistantResponseFinished?(g_assistantResponseError?"Interrupted / inspect prior changes":"Response finished / review result"):g_chatBusy?(g_assistantStatus.empty()?"Working...":g_assistantStatus):g_authBusy?"ChatGPT sign-in in progress":
        g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan?"ChatGPT plan / "+g_assistantConfig.model:
        g_assistantConfig.provider=="ollama"&&!g_assistantConfig.model.empty()?"Local Ollama / "+g_assistantConfig.model:
        g_assistantConfig.authMode==AssistantAuthMode::CliAccount?"Grok Build / "+(g_assistantConfig.model.empty()?std::string("CLI selected model"):g_assistantConfig.model):
        g_authStatus.connected?(g_authStatus.planAuthorized?"ChatGPT signed in / choose model":"ChatGPT identity connected / plan disabled"):
        g_assistantConfig.model.empty()?"Continue with ChatGPT or Advanced API key":"API key / "+g_assistantConfig.model;
    label(fitText(connector,.65f,viewW-(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan?694:526)),viewX+192,cy+15,.65f);
    if(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan)button("Manage usage",viewX+viewW-484,cy+7,154,44013);
    std::string effortButton="Effort "+(g_assistantConfig.reasoningEffort.empty()?std::string("Auto"):g_assistantConfig.reasoningEffort);
    if(g_teamMode)button(g_teamSnapshot.paused?"Resume":"Pause",viewX+viewW-320,cy+7,105,472,false,g_teamSnapshot.paused);
    else button(effortButton.c_str(),viewX+viewW-320,cy+7,105,367,false,!g_assistantConfig.reasoningEffort.empty());
    button(g_authBusy?"Connecting...":g_assistantConfig.provider=="ollama"?"Models / account":g_authStatus.connected?"ChatGPT account":"Models / connect",viewX+viewW-206,cy+7,184,57);
    std::string runTokens=g_teamMode?"Team: "+assistantUsageLabel(g_teamSnapshot.usage):"Run: "+assistantUsageLabel(g_assistantUsageRun);
    if(!g_teamMode){auto timing=measuredPerformanceLabel(g_assistantPerformanceRun);if(!timing.empty())runTokens+=" / "+timing;}
    label(fitText(runTokens,.65f,g_teamMode?viewW-652:viewW-350),viewX+22,cy+33,.65f);
    if(g_teamMode){
        const float imageTop=H-(assistantImageComposerHeight()>40?252.f:188.f);
        const float cardRoom=imageTop-cy-teamCardTop-teamCardStride*(teamCardRows()-1)-4;
        const float cardHeight=cardRoom>=teamCardHeight?teamCardHeight:cardRoom>=148?148.f:std::clamp(cardRoom,84.f,92.f);
        const bool fullCard=cardHeight>=teamCardHeight,summaryCard=cardHeight>=148;
        const bool showGoal=cardRoom>=cardHeight+30;
        if(g_teamStarted&&std::any_of(g_teamSnapshot.roles.begin(),g_teamSnapshot.roles.end(),[](const auto& role){return role.state==AgentTeamState::AwaitingContinuation||role.state==AgentTeamState::Interrupted||role.state==AgentTeamState::Failed;}))button("Resume after error",viewX+viewW-206,cy+41,184,490);
        std::string live=g_teamStarted?(g_teamSnapshot.cancelled?"CANCELLED":g_teamSnapshot.paused?"PAUSED / requests and tools gated":g_teamSnapshot.running?"LIVE / provider and tool activity":std::string("Goal: ")+agentGoalStateName(g_teamSnapshot.goalState)+(g_teamSnapshot.goalDeclared?" / lead report":" / completion not reported")):"READY / choose role settings and send a goal";
        if(g_teamStarted&&!g_teamSnapshot.running&&!g_teamSnapshot.cancelled&&std::any_of(g_teamSnapshot.roles.begin(),g_teamSnapshot.roles.end(),[](const auto& role){return role.state==AgentTeamState::AwaitingContinuation||role.state==AgentTeamState::Interrupted||role.state==AgentTeamState::Failed;}))live="AWAITING CONTINUATION / previous commits and receipts retained";
        if(g_teamStarted&&!g_teamSnapshot.goalSummary.empty())live+=" / "+g_teamSnapshot.goalSummary;
        if(!showGoal&&!g_teamGoal.empty())live="Goal: "+g_teamGoal+" / "+live;
        textOut(fitText(live,.65f,viewW-652),viewX+22,cy+51,.65f,.50f,.81f,.94f);
        int columns=teamCardColumns(),pageSize=columns*teamCardRows();unsigned pages=(g_teamRoleCount+pageSize-1)/pageSize;g_teamPage=std::min(g_teamPage,pages-1);
        button(g_teamFocusedWorkflow?"Focused":"Parallel",viewX+viewW-622,cy+41,114,44014,false,g_teamFocusedWorkflow);
        button(("Agents: "+std::to_string(g_teamRoleCount)).c_str(),viewX+viewW-500,cy+41,100,44010);
        if(pages>1){button("<",viewX+viewW-394,cy+41,30,44011);button(">",viewX+viewW-358,cy+41,30,44012);}
        float cardWidth=(viewW-44.f-8.f*(columns-1))/columns;
        g_teamCardContentFits=true;g_teamCardBoundsFailure.clear();
        for(size_t cell=0;cell<(size_t)pageSize;++cell){size_t i=g_teamPage*pageSize+cell;if(i>=g_teamRoleCount)break;float x=viewX+22+(cardWidth+8)*(cell%columns),y=cy+teamCardTop+teamCardStride*(cell/columns);const AgentTeamRoleSnapshot* role=i<g_teamSnapshot.roles.size()?&g_teamSnapshot.roles[i]:nullptr;
            bool working=role&&role->state==AgentTeamState::Working,failed=role&&role->state==AgentTeamState::Failed;float accentR=failed?.92f:i==0?.34f:i==1?.71f:.39f,accentG=failed?.40f:i==0?.75f:i==1?.63f:.83f;
            rect(x,y,cardWidth,cardHeight,8,.058f,.083f,.107f,.97f,accentR,accentG,.91f,working?.96f:.38f,working?2.f:1.f);
            size_t cardTextStart=g_textVerts.size();
            auto line=[&](const std::string& value,float offset,float scale=.80f){textOut(fitText(value,scale,cardWidth-24),x+12,y+offset,scale,.83f,.91f,.98f);};
            line(g_teamRoleLabels[i],8,.85f);std::string state=role?std::string(agentTeamStateName(role->state))+" / turn "+std::to_string(role->turns):"Ready";line(state+(i==0?" / engineering + CAD":i==2?" / read-only review":" / CAD author"),summaryCard?32.f:29.f);
            auto config=teamRoleConfig(i);if(summaryCard)line((config.authMode==AssistantAuthMode::ChatGptPlan?std::string("ChatGPT"):config.authMode==AssistantAuthMode::CliAccount?std::string("Grok Build"):config.provider)+" / "+(config.model.empty()?(config.authMode==AssistantAuthMode::CliAccount?"CLI selected model":"Select model"):config.model)+" / "+(config.reasoningEffort.empty()?"Auto":config.reasoningEffort),54);
            if(summaryCard)line(role&&!role->taskSummary.empty()?"Task "+role->taskState+": "+role->taskSummary:role?role->status:"Describe the goal to start",76);if(fullCard)line(g_teamActivity[i].empty()?(role?role->text:"Tools and messages appear live"):g_teamActivity[i],98);
            const float footer=fullCard?125.f:summaryCard?103.f:50.f;
            if(summaryCard)rect(x+12,y+footer-3,cardWidth-24,1,0,.23f,.34f,.42f,.65f,0,0,0,0,0);
            std::string tokens=role&&role->usage.reportedResponses?std::string(role->usage.missingResponses?"Tokens >= ":"Tokens ")+std::to_string(role->usage.totalTokens):"Tokens unreported";
            textOut(fitText(tokens,.76f,cardWidth-192),x+12,y+footer,.76f,.77f,.90f,1.f);button("Connector",x+cardWidth-168,y+footer,74,44020+(int)i);button("Settings",x+cardWidth-86,y+footer,74,44000+(int)i);
            std::string detail=role&&role->usage.reportedResponses?"In "+std::to_string(role->usage.inputTokens)+" / Out "+std::to_string(role->usage.outputTokens)+" / Reason "+std::to_string(role->usage.reasoningTokens):"Input / output: unreported";
            if(role){auto timing=measuredPerformanceLabel(role->performance);if(!timing.empty())detail=timing;}
            if(fullCard)textOut(fitText(detail,.72f,cardWidth-192),x+12,y+148,.72f,.83f,.91f,.98f);
            if(g_verifyTeamCardBounds)for(size_t vertex=cardTextStart;vertex+6<g_textVerts.size();vertex+=7){float tx=g_textVerts[vertex],ty=g_textVerts[vertex+1];if(tx<x+8||tx>x+cardWidth-8||ty<y+8||ty>y+cardHeight-8){g_teamCardContentFits=false;if(g_teamCardBoundsFailure.empty()){std::ostringstream detail;detail<<"Card "<<i<<" ("<<g_teamRoleIds[i]<<") glyph ("<<tx<<","<<ty<<") exceeds ["<<x+8<<","<<y+8<<".."<<x+cardWidth-8<<","<<y+cardHeight-8<<"], atlas line height "<<fa.lineH;g_teamCardBoundsFailure=detail.str();}}}
        }
        if(showGoal)label(fitText(g_teamStarted&&g_teamSnapshot.goalDeclared?std::string("GOAL ")+agentGoalStateName(g_teamSnapshot.goalState)+" / agent report: "+g_teamSnapshot.goalSummary:"GOAL: "+g_teamGoal,.70f,viewW-44),viewX+22,cy+teamCardTop+cardHeight+8+teamCardStride*(teamCardRows()-1),.70f);
    }
    std::vector<std::string> chatLines;size_t cols=(size_t)std::max(30,(viewW-55)/7);
    for(auto& entry:g_chatLog){std::string line=entry.first+": "+entry.second;
        for(char& ch:line)if(ch=='\r'||ch=='\n')ch=' ';
        for(size_t i=0;i<line.size();){size_t end=std::min(i+cols,line.size());
            while(end>i&&end<line.size()&&((unsigned char)line[end]&0xc0)==0x80)--end;
            if(end==i)end=std::min(i+cols,line.size());
            chatLines.push_back(line.substr(i,end-i));i=end;
        }}
    if(chatLines.empty())chatLines.push_back("Describe a part, ask for an inspection, or connect Codex to this document.");
    int last=std::max(0,(int)chatLines.size()-g_chatScroll),first=std::max(0,last-(g_teamMode?teamChatRows():3));float chatY=cy+(g_teamMode?285+teamCardStride*(teamCardRows()-1):49);
    for(int i=first;i<last;++i){textOut(fitText(chatLines[i],.66f,viewW-44),viewX+22,chatY,.66f,.73,.77,.84);chatY+=22;}
    float inputY=H-120;
    drawAssistantImageAttachmentUI(rect,button,glyph);
    rect(viewX+22,inputY,viewW-244,62,7,.055,.062,.078,1,.30,.35,.43,.8,1);
    if(g_pendingObjectTarget){
        std::string chip="Object: "+g_pendingObjectTarget->label;
        if(!g_pendingObjectTarget->invalidated.empty())chip="Object removed or replaced / select again";
        else if(g_pendingObjectTarget->epoch!=g_objectDocumentEpoch)chip="Object from previous drawing / select again";
        textOut(fitText(chip,.62f,viewW-338),viewX+30,inputY-20,.62f,.50f,.85f,.96f);button("Clear",viewX+viewW-302,inputY-25,66,566);
    }else label(g_teamMode?"GOAL / DECISION  /  Ctrl+Enter to queue":"MESSAGE  /  Ctrl+Enter to send",viewX+30,inputY-16,.55f);
    button(g_teamMode?(g_teamStarted&&!g_teamSnapshot.cancelled?"Queue":"Start"):(g_chatBusy?"Queue":"Send"),viewX+viewW-206,inputY,84,74,true);button("Cancel",viewX+viewW-112,inputY,90,75);
    if(g_teamMode){std::string target="To: "+(g_teamTarget.empty()?std::string(g_teamFocusedWorkflow?"Engineer":"entire team"):g_teamTarget=="all"?std::string("entire team"):g_teamTarget);button(target.c_str(),viewX+viewW-206,inputY+40,184,473);}
    else button("Live MCP",viewX+viewW-206,inputY+40,184,58);
    AssistantUsage docTokens=g_assistantUsageDocument;
    textOut(g_status.substr(0,(size_t)std::max((viewW-334)/7,1)),viewX+22,H-51,.58f,g_error?.94f:.58f,g_error?.57f:.65f,g_error?.48f:.74f);
    std::string documentTokens="Document: "+assistantUsageLabel(docTokens);
    label(documentTokens.substr(0,46),viewX+viewW-310,H-51,.51f);
    }
    if(g_motionActive){float px=(float)viewX+20,py=(float)viewY+viewH-99,width=std::min(760.f,(float)viewW-168.f);rect(px,py,width,79,7,.055,.083,.106,.95,.29,.42,.52,.65,1);
        std::string transmission,outputName="Output",outputUnit="rpm";double outputSpeed=0;u64 outputNode=0;std::set<u64> activeDrivers;
        const auto* relations=g_motionReport.find("relations");if(relations)for(const auto& relation:relations->arr)if(relation.find("active")&&relation.find("active")->b)activeDrivers.insert((u64)relation.find("a")->num);
        if(relations)for(const auto& relation:relations->arr)if(relation.find("active")&&relation.find("active")->b){u64 driven=(u64)relation.find("b")->num;if(!outputNode&&driven!=g_motionDriver&&!activeDrivers.count(driven))outputNode=driven;
            if(transmission.empty()&&relation.find("teeth_a")){char ratio[120];std::snprintf(ratio,sizeof ratio,"%.0fT / %.0fT   |   ratio %.3g:1",relation.find("teeth_a")->num,relation.find("teeth_b")->num,std::abs(relation.find("ratio")->num));transmission=ratio;}}
        if(!outputNode)for(u64 node:activeDrivers)if(node!=g_motionDriver){outputNode=node;break;}
        if(auto* instances=g_motionReport.find("instances"))for(const auto& row:instances->arr)if((u64)row.find("node")->num==outputNode){outputName=row.find("name")->asStr();if(auto* speed=row.find("speed_rpm"))outputSpeed=speed->num;else if(auto* velocity=row.find("velocity_mm_s")){outputSpeed=velocity->num;outputUnit="mm/s";}}
        bool editableTeeth=false,frontParameter=false,rearParameter=false;for(const auto& parameter:g_doc.params){frontParameter|=parameter.name=="front_teeth";rearParameter|=parameter.name=="rear_teeth";}editableTeeth=frontParameter&&rearParameter;
        char cadence[180];std::snprintf(cadence,sizeof cadence,"Driver %.3g rpm   |   %s %.3g %s   |   Time %.2f s",g_motionRpm,outputName.substr(0,22).c_str(),outputSpeed,outputUnit.c_str(),g_motionTime);textOut(transmission.empty()?"Mechanical motion":transmission,px+13,py+10,.82f,.68f,.87f,.97f);textOut(cadence,px+13,py+37,.74f,.77f,.86f,.92f);
        button(g_motionPlaying?"Pause":"Play",px+width-228,py+12,64,453);button(editableTeeth?"Teeth":"Driver",px+width-155,py+12,62,editableTeeth?469:452);button("Reset",px+width-84,py+12,70,454);g_zones.push_back({px+12,py+32,std::max(80.f,width-250),26,452});textOut("Kinematic preview / contact and dynamics unchecked",px+13,py+61,.55f,.50f,.66f,.76f);
    }
    if(g_showViewCube){
    float navT=std::clamp(((float)viewW-622.f)/420.f,0.f,1.f);
    updateNavigationBounds();
    g_cubeHover=cubeHitAt(mp.x,mp.y);
    paintViewCube();glUseProgram(pUI);glUniform2f(uuVp,W,H);
    {Vec3 cubeEye;Mat4 cubeView,cubeProj;cubeCamera(cubeEye,cubeView,cubeProj);Mat4 cvp=cubeProj*cubeView;Vec3 toEye=cubeEye.normalized();
        static const char* faceName[]={"Right","Left","Top","Bottom","Front","Back"};
        static const Vec3 faceNormal[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for(int face=0;face<6;++face){double visibility=faceNormal[face].dot(toEye);if(visibility<.18)continue;Vec3 p=faceNormal[face]*.88;double cw=cvp.m[3]*p.x+cvp.m[7]*p.y+cvp.m[11]*p.z+cvp.m[15];if(cw<=1e-5)continue;Vec3 clip=cvp.transformPoint(p);
            float x=g_cubeX+(float)((clip.x/cw+1)*.5*g_cubeS),y=g_cubeY+(float)((1-clip.y/cw)*.5*g_cubeS);
            float scale=(visibility>.55?.61f:.56f)*std::min(1.f,g_cubeS/134.f);textOut(faceName[face],x-textWidth(faceName[face],scale)*.5f,y-6,scale,.06f,.11f,.16f);}
    }
    float homeX=(float)g_cubeX+g_cubeS-29,homeY=(float)g_cubeY+g_cubeS-28;
    bool homeHover=mp.x>=homeX&&mp.x<homeX+28&&mp.y>=homeY&&mp.y<homeY+28;
    rect(homeX,homeY,28,28,14,homeHover?.21f:.08f,homeHover?.32f:.11f,homeHover?.43f:.15f,homeHover?.94f:.72f,.41f,.59f,.73f,homeHover?.78f:.42f,1);
    glyph(29,homeX+5,homeY+5,18,homeHover?.99f:.83f);g_zones.push_back({homeX,homeY,28,28,191});
    g_zones.push_back({(float)g_cubeX,(float)g_cubeY,(float)g_cubeS,(float)g_cubeS,179});
    }else g_cubeS=0;
    float compassSize=std::max(88.f,std::min(std::clamp(viewH*.17f,112.f,152.f),viewH-72.f)),compassX=(float)viewX+viewW-compassSize-14,compassY=(float)viewY+viewH-compassSize-14;
    g_compassBounds={(LONG)compassX-4,(LONG)compassY-4,(LONG)(compassX+compassSize)+4,(LONG)(compassY+compassSize)+4};
    paintNavigationTriad((int)compassX,(int)compassY,(int)compassSize);glUseProgram(pUI);glUniform2f(uuVp,W,H);
    {auto directions=compassDirections();static const char* names[]={"X","Y","Z"};static const float colors[][3]={{1.f,.38f,.35f},{.40f,.91f,.60f},{.34f,.67f,1.f}};static const int views[]={182,23,180};
        std::array<int,3> order={0,1,2};std::sort(order.begin(),order.end(),[&](int a,int b){return directions[a].z>directions[b].z;});
        float ox=compassX+compassSize*.5f,oy=compassY+compassSize*.5f,hitRadius=compassSize<100?10.f:12.f,labelScale=compassSize<100?.67f:.76f;std::array<Zone,3> axisZones{};
        for(int axis:order){float cx=ox+(float)directions[axis].x*compassSize*.42f,cy=oy-(float)directions[axis].y*compassSize*.42f;bool hover=std::hypot(mp.x-cx,mp.y-cy)<hitRadius;
            textOut(names[axis],cx-textWidth(names[axis],labelScale)*.5f,cy-fa.lineH*labelScale*.5f,labelScale,hover?1.f:colors[axis][0],hover?1.f:colors[axis][1],hover?1.f:colors[axis][2]);
            axisZones[axis]={cx-hitRadius,cy-hitRadius,hitRadius*2,hitRadius*2,views[axis]};
        }
        for(auto it=order.rbegin();it!=order.rend();++it)g_zones.push_back(axisZones[*it]);
    }
    if(g_compactNavigation){const char* names[]={"Orbit","Pan","Zoom","Fit","Edges","AO","Ortho"};int ids[]={176,177,178,20,174,186,175};
        bool on[]={g_navMode==0,g_navMode==1,g_navMode==2,false,g_showEdges,g_occlusionMode!=0,g_cameraOrtho};
        float gap=4.f,bw=58.f,ty=(float)viewY+(float)viewH-44.f;
        rect((float)viewX+10,ty-5,7*bw+6*gap+16,43,9,.07f,.08f,.10f,.76f,.34f,.38f,.44f,.45f,1);
        for(int i=0;i<7;++i)button(names[i],(float)viewX+18+i*(bw+gap),ty,bw,ids[i],false,on[i]);}
    if(selBody>=0&&(size_t)selBody<g_bodyIds.size()&&!g_toolsOpen){
        auto* feature=g_doc.find(g_bodyIds[(size_t)selBody]);bool transparent=feature&&resolvedAppearance(g_doc,*feature).opacity<.999;
        float px=g_showViewCube?(float)g_cubeX+g_cubeS+24:(float)viewX+16,py=(float)viewY+12;rect(px-5,py-5,390,57,7,.055f,.083f,.106f,.92f,.29f,.42f,.52f,.65f,1);
        button(transparent?"Opaque":"Transparent",px,py,112,560,false,transparent);button("Opacity...",px+120,py,96,561);
        if(g_instanceIds[(size_t)selBody])button(g_componentMove.armed?"Moving":"Move component",px+224,py,151,562,false,g_componentMove.armed);
        textOut("Appearance: all instances of this part definition",px+3,py+36,.55f,.64f,.75f,.82f);
    }
    if((g_componentMove.armed||g_componentMove.active)&&!g_toolsOpen){float px=g_showViewCube?(float)g_cubeX+g_cubeS+24:(float)viewX+16,py=(float)viewY+77;rect(px-5,py-5,390,58,7,.045f,.115f,.135f,.95f,.20f,.70f,.72f,.75f,1);
        char angle[180];const auto& joint=g_componentMove.joint;
        if(joint.mate){const char* unit=joint.mode=="translate"?"mm":"deg";bool absolute=joint.type=="prismatic"||joint.type=="revolute";double position=absolute?joint.position+joint.coordinateSign*g_componentMove.delta:g_componentMove.delta;
            if(joint.limited)std::snprintf(angle,sizeof angle,"Joint %llu / %.1f %s / [%.1f, %.1f]",(unsigned long long)joint.mate,position,unit,joint.min,joint.max);
            else std::snprintf(angle,sizeof angle,"Joint %llu / %s %.1f %s",(unsigned long long)joint.mate,absolute?"position":"delta",position,unit);
        }else std::snprintf(angle,sizeof angle,"Ready to drag a joint");textOut(angle,px+4,py+6,.72f,.72f,.96f,.97f);
        button("Apply",px+224,py,70,563);button("Cancel",px+302,py,73,564);textOut("Drag part / release saves / Esc cancels / middle mouse orbits",px+3,py+37,.52f,.65f,.82f,.86f);
    }
    if(g_grid&&!g_allSheetsFlat&&!g_stockMode){std::string scale="Grid "+g_displayUnits.distance(g_adaptiveGrid.spacing)+" / major "+g_displayUnits.distance(g_adaptiveGrid.spacing*5.);float gx=(float)viewX+17,gy=(float)viewY+viewH-78;rect(gx-6,gy-5,textWidth(scale,.65f)+12,24,5,.065f,.09f,.115f,.85f,0,0,0,0,0);textOut(scale,gx,gy,.65f,.68f,.81f,.89f);}
    panel(0,H-32,W,32);
    textOut(g_status.substr(0,(size_t)std::max(28,(winW-430)/7)),20,H-23,.70f,g_error?.96f:.62f,g_error?.56f:.73f,g_error?.40f:.81f);
    textOut(g_displayUnits.length,W-164,H-23,.73f,.67f,.76f,.83f);iconButton(22,"Show / hide grid",W-118,H-29,24);iconButton(461,"Navigation controls",W-81,H-29,24);iconButton(460,"Full screen",W-44,H-29,24);
    if(g_toolsOpen){
        ribbonHint.clear();auto bounds=toolGalleryBounds();float x=(float)bounds.left,y=(float)bounds.top,width=(float)(bounds.right-bounds.left),height=(float)(bounds.bottom-bounds.top);
        std::vector<float> visibleText;visibleText.reserve(g_textVerts.size());
        for(size_t offset=0;offset+41<g_textVerts.size();offset+=42){float minX=1e9f,minY=1e9f,maxX=-1e9f,maxY=-1e9f;for(int v=0;v<6;++v){float px=g_textVerts[offset+(size_t)v*7],py=g_textVerts[offset+(size_t)v*7+1];minX=std::min(minX,px);maxX=std::max(maxX,px);minY=std::min(minY,py);maxY=std::max(maxY,py);}if(maxX<x||minX>x+width||maxY<y||minY>y+height)visibleText.insert(visibleText.end(),g_textVerts.begin()+offset,g_textVerts.begin()+offset+42);}
        g_textVerts.swap(visibleText);
        g_zones.erase(std::remove_if(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.x<x+width&&zone.x+zone.w>x&&zone.y<y+height&&zone.y+zone.h>y;}),g_zones.end());
        rect(x,y,width,height,10,.041f,.064f,.086f,.99f,.26f,.48f,.61f,.97f,1.5f);textOut("ALL TOOLS",x+17,y+15,.90f,.83f,.94f,1.f);button("Close",x+width-82,y+8,67,480);textOut("Browse categories or search every tool",x+213,y+18,.63f,.65f,.79f,.88f);
        rect(x+208,y+41,width-225,38,5,.045f,.07f,.09f,1,.31f,.50f,.60f,.70f,1);textOut(g_toolFilter.empty()?"Search tools, operations, evidence...":g_toolFilter,x+220,y+51,.67f,.77f,.86f,.92f);
        button("Clear",x+width-82,y+45,62,483);textOut("CATEGORIES",x+15,y+56,.58f,.49f,.67f,.77f);
        auto categoryRow=[&](const std::string& title,int category,float rowY){bool selected=g_toolCategory==category;if(selected)rect(x+9,rowY-2,188,21,3,.12f,.27f,.34f,1,0,0,0,0,0);textOut(title.substr(0,30),x+15,rowY,.58f,selected?.89f:.63f,selected?.98f:.77f,selected?1.f:.85f);g_zones.push_back({x+9,rowY-2,188,21,40001+category});};
        const auto& categories=studioCommandGroups();int categoryCount=(int)categories.size()+2,categoryPage=std::max(1,(int)(height-132)/22);g_toolCategoryScroll=std::clamp(g_toolCategoryScroll,0,std::max(0,categoryCount-categoryPage));
        for(int row=0;row<categoryPage&&g_toolCategoryScroll+row<categoryCount;++row){int nav=g_toolCategoryScroll+row;categoryRow(nav==0?"All tools":nav==categoryCount-1?"Advanced CAD API":categories[(size_t)nav-1].title,nav-1,y+88+row*22);}
        textOut(std::to_string(g_toolCategoryScroll+1)+"-"+std::to_string(std::min(categoryCount,g_toolCategoryScroll+categoryPage))+" / "+std::to_string(categoryCount),x+15,y+73,.47f,.52f,.69f,.79f);
        auto visible=visibleStudioTools();int page=std::max(1,(int)(height-125)/38);g_toolScroll=std::clamp(g_toolScroll,0,std::max(0,(int)visible.size()-page));
        for(int row=0;row<page&&g_toolScroll+row<(int)visible.size();++row){size_t index=visible[(size_t)(g_toolScroll+row)];const auto& entry=studioToolEntries()[index];float rowY=y+91+row*38;bool hover=mp.x>=x+211&&mp.x<x+width-14&&mp.y>=rowY-3&&mp.y<rowY+32;
            rect(x+208,rowY-3,width-222,34,4,hover?.13f:.068f,hover?.25f:.108f,hover?.32f:.137f,1,0,0,0,0,0);glyph(entry.action>=41000&&entry.action<42000?457:entry.action,x+217,rowY+1,25,.78f);size_t chars=(size_t)std::max(15.f,(width-280)/6.5f);textOut(entry.title.substr(0,chars),x+252,rowY,.69f,.84f,.92f,.97f);std::string description=entry.description;for(char& ch:description)if(ch=='\r'||ch=='\n')ch=' ';textOut(description.substr(0,(size_t)std::max(15.f,(width-280)/4.7f)),x+252,rowY+17,.49f,.55f,.71f,.81f);g_zones.push_back({x+208,rowY-3,width-222,34,42000+(int)index});
        }
        std::string footer=std::to_string(visible.size())+" tools / "+std::to_string(g_toolScroll+1)+"-"+std::to_string(std::min((int)visible.size(),g_toolScroll+page))+" / scroll to browse";textOut(visible.empty()?"No matches. Clear search to browse all categories.":footer,x+213,y+height-23,.57f,.61f,.79f,.89f);button("Up",x+width-148,y+height-34,56,481);button("Down",x+width-82,y+height-34,67,482);if(categoryCount>categoryPage){button("Up",x+15,y+height-34,76,486);button("Down",x+99,y+height-34,88,487);}g_zones.push_back({x,y,width,height,0});
    }
    if(g_sketchEditing.active){float sx=(float)viewX+(g_showViewCube?g_cubeS+30:20),sy=(float)viewY+60,sw=std::min(570.f,(float)(viewX+viewW)-sx-18);rect(sx,sy,sw,57,8,.025f,.070f,.095f,.93f,.18f,.8f,.64f,.6f,1);textOut(fitText("EDIT SKETCH / "+g_sketchEditing.name,.64f,sw-24),sx+12,sy+8,.64f,.65f,.97f,.87f);textOut(fitText(!sketchEditCurrent()?"Stale edit / Exit and select current sketch":g_sketchEditing.error.empty()?"Drag anchor or curve / Tab coordinates / Exit sketch or Esc":g_sketchEditing.error,.55f,sw-24),sx+12,sy+34,.55f,.61f,.84f,.91f);}
    else if(g_sketchDrawing.active){
        const auto& drawing=g_sketchDrawing;float sx=(float)viewX+(g_showViewCube?g_cubeS+30:20),sy=(float)viewY+60,sw=std::min(570.f,(float)(viewX+viewW)-sx-18);
        // Viewport readout is deliberately passive. Interactive controls live
        // in the Sketch ribbon, so authored points never select another tool.
        rect(sx+3,sy+4,sw,57,8,.005f,.012f,.022f,.28f,0,0,0,0,0);rect(sx,sy,sw,57,8,.025f,.070f,.095f,.93f,.14f,.59f,.70f,.6f,1);
        textOut(fitText((drawing.stale?"STALE DRAFT / ":"SKETCH PREVIEW / ")+drawing.name+" / "+sketchDrawingToolName(drawing.tool)+" / "+drawing.plane,.64f,sw-24),sx+12,sy+8,.64f,.65f,.91f,.97f);
        std::string readout=drawing.stale?"Drawing changed / Esc and restart":drawing.anchors.empty()?"Click first anchor / MMB orbit / wheel zoom":"Click next anchor / Dimensions in ribbon or Tab / Esc cancel";
        if(!drawing.anchors.empty()&&drawing.tool!=4){auto compact=[](double value){char text[40];std::snprintf(text,sizeof text,"%.3f",value);return std::string(text);};readout=drawing.tool==2?"W "+compact(std::abs(drawing.pointer.point.x-drawing.anchors.front().point.x))+" / H "+compact(std::abs(drawing.pointer.point.y-drawing.anchors.front().point.y)):std::string(drawing.tool==3?"R ":"L ")+compact(drawingDistance(drawing.anchors.back().point,drawing.pointer.point));if(!drawing.dimension.empty())readout=std::string(drawing.tool==3?"R ":drawing.tool==2?"W ":"L ")+drawing.dimension+(drawing.secondDimension.empty()?"":" / H "+drawing.secondDimension);readout+=" mm / Tab to edit";}
        if(!drawing.previewError.empty())readout=drawing.previewError;else if(!drawing.pointer.label.empty())readout+=" / "+drawing.pointer.label;textOut(fitText(readout,.55f,sw-24),sx+12,sy+34,.55f,.61f,.84f,.91f);
    }else if(!g_draftViews.empty()){
        float sx=(float)viewX+(g_showViewCube?g_cubeS+30:20),sy=(float)viewY+76;rect(sx,sy,420,50,7,.025f,.07f,.095f,.95f,.16f,.61f,.75f,.7f,1);textOut("AGENT SKETCH DRAFT / uncommitted commands",sx+12,sy+8,.64f,.62f,.92f,.99f);textOut(fitText(g_liveStepLabel,.52f,392),sx+12,sy+30,.52f,.57f,.77f,.86f);
    }
    if(g_directAuthoring.active){auto& edit=g_directAuthoring;float sx=(float)viewX+(g_showViewCube?g_cubeS+30:20),sy=(float)viewY+66,sw=std::min(620.f,(float)(viewX+viewW)-sx-18);rect(sx,sy,sw,edit.mode==5?110.f:74.f,8,.025f,.070f,.095f,.96f,.15f,.81f,.77f,.7f,1);textOut(fitText(directModeName(edit.mode),.69f,sw-24),sx+12,sy+8,.69f,.65f,.97f,.90f);std::string value=!directCurrent()?"Stale preview / cancel and select current geometry":!edit.error.empty()?edit.error:(edit.feature||edit.mode==5)?edit.mode==5?(edit.limitPicking?"Click the exact planar limit face":(edit.limitFeature?"Up to "+edit.limitFace+" / ":edit.symmetric?"Symmetric total depth / ":edit.direction<0?"Reverse / ":"")+directDisplayDistance(edit.amount)+" / Tab depth / Apply saves"):directDisplayDistance(edit.amount)+" / Tab expressions / Apply saves / Esc cancels":"Drag a compatible source face or mesh element";textOut(fitText(value,.54f,sw-24),sx+12,sy+32,.54f,.70f,.85f,.92f);if(edit.feature||edit.mode==5){button("Value",sx+12,sy+50,78,655);button("Apply",sx+100,sy+50,78,656,true);button("Cancel",sx+188,sy+50,78,657);if(edit.mode==5){button("Reverse",sx+12,sy+80,88,689,false,edit.direction<0);button("Symmetric",sx+108,sy+80,100,690,false,edit.symmetric);button("Up to face",sx+216,sy+80,100,691,false,edit.limitPicking||edit.limitFeature);button("Distance",sx+324,sy+80,90,692,false,!edit.limitFeature&&!edit.limitPicking);}}}
    drawExtrusionHandle(rect,button);
    if(!measurementActive()&&!g_directAuthoring.active&&!g_sketchEditing.active&&!g_sketchDrawing.active)drawSolidDimensions(rect,button);
    drawMeasurementOverlay(rect,button);
    g_viewSketchDimension={};if(!measurementActive()&&!g_directAuthoring.active&&!g_sketchDrawing.active&&g_showSketches){g_viewSketchDimension=selectedSketchDimension();if(g_viewSketchDimension.valid){float px,py;if(projectScreen(g_viewSketchDimension.position,px,py)){auto& dimension=g_viewSketchDimension;float width=std::min(360.f,textWidth(dimension.text,.66f)+24),x=std::clamp(px+18,(float)viewX+8,(float)(viewX+viewW)-width-8),y=std::clamp(py-40,(float)viewY+185,(float)(viewY+viewH)-58);avoidExtrusionHandle(x,y,width,52);rect(x+2,y+4,width,52,7,0,0,0,.20f,0,0,0,0,0);rect(x,y,width,52,7,.045f,.105f,.135f,.97f,.24f,.57f,.69f,.8f,1);textOut(dimension.text,x+11,y+8,.66f,.71f,.94f,.99f);textOut(dimension.action==577?"Click to edit linked source parameters":dimension.action==89?"Click to edit this native constraint":dimension.action==591?"Click to edit driving dimension":"Click to edit authored coordinates",x+11,y+31,.49f,.59f,.78f,.87f);g_zones.push_back({x,y,width,52,584});}}}
    if(g_error&&!g_lastFailureId.empty())button("Inspect failed operation",(float)(viewX+viewW)-210,(float)viewY+66,190,580);
    drawViewportDock(rect,glyph,mp);
    g_lastRibbonHint=ribbonHint;
    if(!ribbonHint.empty()){float width=textWidth(ribbonHint,.60f)+22,x=std::clamp((float)mp.x+12.f,10.f,W-width-10.f),y=(float)mp.y+20;rect(x,y,width,29,6,.035,.055,.075,.98,.27,.51,.65,.95,1);textOut(ribbonHint,x+11,y+7,.60f,.87f,.94f,1.f);}
    if(g_drawingViewport.active){
        // Keep the shared explorer/ribbon, replace viewport labels with the sheet.
        std::vector<float> text;for(size_t at=0;at+20<g_textVerts.size();at+=21){float x=(g_textVerts[at]+g_textVerts[at+7]+g_textVerts[at+14])/3,y=(g_textVerts[at+1]+g_textVerts[at+8]+g_textVerts[at+15])/3;if(x<viewX||x>=viewX+viewW||y<viewY||y>=viewY+viewH)text.insert(text.end(),g_textVerts.begin()+at,g_textVerts.begin()+at+21);}g_textVerts.swap(text);
        g_drawingSourcePicked=[](u64 feature,u64 node,int copy){g_explorerFeature=feature;selBody=-1;g_picked.clear();for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==feature&&(!node||(g_instanceIds[i]==node&&g_instanceCopies[i]==copy))){selBody=(int)i;g_explorerFeature=feature;g_picked.clear();refreshSelectionLines();break;}};
        auto drawingLine=[&](float ax,float ay,float bx,float by,float r,float g,float b,float width){float x=std::min(ax,bx)-2,y=std::min(ay,by)-2,w=std::abs(bx-ax)+4,h=std::abs(by-ay)+4;glUniform1i(uuGlyph,500);glUniform4f(uuRc,x,y,w,h);glUniform1f(uuBW,width);glUniform4f(uuFill,r,g,b,1);glUniform4f(glGetUniformLocation(pUI,"lineAB"),ax-x,ay-y,bx-x,by-y);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glUniform1i(uuGlyph,0);};
        renderDrawingViewport(rect,button,drawingLine);
    }
    // flush text
    glUseProgram(pText);
    glUniform2f(glGetUniformLocation(pText,"vp"),W,H);
    glUniform4f(glGetUniformLocation(pText,"col"),.90f,.93f,.97f,.98f);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,fa.tex);
    glUniform1i(glGetUniformLocation(pText,"txx"),0);
    glBindVertexArray(textVao);
    glBindBuffer(GL_ARRAY_BUFFER,textVbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(g_textVerts.size()*sizeof(float)),
                 g_textVerts.empty()?nullptr:g_textVerts.data(),GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,28,nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,28,(void*)8);
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,28,(void*)16);
    glDrawArrays(GL_TRIANGLES,0,(GLint)g_textVerts.size()/7);

    if(g_swap)SwapBuffers(hdc);
}

static arkjson::Value captureViewportSnapshot(const arkjson::Value& args){
    using V=arkjson::Value;
    try{
        if(args.find("camera"))throw std::runtime_error("Live viewport capture uses the current Studio camera; omit camera or choose snapshot view custom");
        if(g_busy||!g_uiReady)throw std::runtime_error("Current Studio viewport is unavailable while a document operation is applying");
        if(IsIconic(g_window))throw std::runtime_error("Studio is minimized; restore its window before requesting viewport pixels, or explicitly request a software snapshot view");
        const u64 revision=g_session.revision,epoch=g_documentEpoch;if(auto* expected=args.find("expected_revision");expected&&(expected->t!=V::Num||expected->num!=(double)revision))throw std::runtime_error("Viewport snapshot revision conflict; read current doc_info before retrying");
        if(viewW<=0||viewH<=0||viewX<0||viewY<0||viewX+viewW>winW||viewY+viewH>winH)throw std::runtime_error("Studio viewport bounds are invalid for pixel readback");
        struct CaptureScope {HDC dc=nullptr;bool swap=true;~CaptureScope(){if(dc)ReleaseDC(g_window,dc);g_swap=swap;}} scope{GetDC(g_window),g_swap};
        if(!scope.dc)throw std::runtime_error("Studio viewport device context is unavailable");
        for(int i=0;i<16;++i)if(glGetError()==GL_NO_ERROR)break;
        g_swap=false;drawFrame(scope.dc);glFinish();
        if(GLenum renderError=glGetError();renderError!=GL_NO_ERROR)throw std::runtime_error("Studio viewport render failed (OpenGL "+std::to_string((unsigned)renderError)+")");
        if(!glBindFramebuffer)throw std::runtime_error("Studio viewport readback requires an OpenGL framebuffer binding");glBindFramebuffer(GL_FRAMEBUFFER,0);
        std::vector<u8> pixels((size_t)viewW*viewH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(viewX,winH-viewY-viewH,viewW,viewH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        if(GLenum readError=glGetError();readError!=GL_NO_ERROR)throw std::runtime_error("Studio viewport pixel readback failed (OpenGL "+std::to_string((unsigned)readError)+")");if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Document changed during viewport capture; inspect current revision and retry");
        double scale=std::min({1.,1024./std::max(viewW,1),640./std::max(viewH,1)});Image shot;shot.w=std::max(1,(int)std::floor(viewW*scale));shot.h=std::max(1,(int)std::floor(viewH*scale));shot.px.resize((size_t)shot.w*shot.h*3);
        for(int y=0;y<shot.h;++y)for(int x=0;x<shot.w;++x){int fromX=std::min(viewW-1,(int)(x/scale)),fromY=std::min(viewH-1,(int)(y/scale));size_t source=((size_t)(viewH-1-fromY)*viewW+fromX)*3,destination=((size_t)y*shot.w+x)*3;std::copy_n(pixels.begin()+source,3,shot.px.begin()+destination);}
        V evidence=V::makeObj(),state=V::makeObj(),camera=V::makeObj(),selected=V::makeObj();evidence.set("revision",V::makeNum((double)revision));evidence.set("current_revision",V::makeNum((double)g_session.revision));evidence.set("view",V::makeStr("viewport"));evidence.set("capture_source",V::makeStr("studio_viewport"));evidence.set("width",V::makeNum(shot.w));evidence.set("height",V::makeNum(shot.h));
        auto vector=[](Vec3 point){V value=V::makeArr();value.arr={V::makeNum(point.x),V::makeNum(point.y),V::makeNum(point.z)};return value;};camera.set("eye_mm",vector(camEye()));camera.set("target_mm",vector(target));camera.set("yaw_rad",V::makeNum(yaw));camera.set("pitch_rad",V::makeNum(pitch));camera.set("roll_rad",V::makeNum(cameraRoll));camera.set("distance_mm",V::makeNum(dist));camera.set("orthographic",V::makeBool(g_cameraOrtho));state.set("camera",camera);state.set("drafts",V::makeBool(g_sketchDrawing.active||!g_draftViews.empty()));state.set("sketches_visible",V::makeBool(g_showSketches));state.set("active_sketch",V::makeStr(g_activeSketch));state.set("flat_display",V::makeBool(g_allSheetsFlat));state.set("stock_display",V::makeBool(g_stockMode));state.set("rendering",V::makeStr(wireframe?"wireframe":g_showEdges?"shaded_edges":"shaded"));evidence.set("view_state",state);
        if(selBody>=0&&(size_t)selBody<g_bodyIds.size()){selected.set("feature_id",V::makeNum((double)g_bodyIds[(size_t)selBody]));selected.set("occurrence_id",V::makeNum((double)g_instanceIds[(size_t)selBody]));selected.set("copy",V::makeNum(g_instanceCopies[(size_t)selBody]));}else if(g_explorerFeature)selected.set("feature_id",V::makeNum((double)g_explorerFeature));if(!g_picked.empty()){selected.set("reference",V::makeStr(g_picked.back().name));selected.set("reference_owner",V::makeStr(g_picked.back().owner));}evidence.set("selected",selected);
        std::string path;if(auto* output=args.find("path");output&&output->t==V::Str)path=output->str;return snapshotImageResult(shot,evidence,path);
    }catch(const std::exception& error){V result=V::makeObj(),content=V::makeArr(),text=V::makeObj(),structured=V::makeObj();text.set("type",V::makeStr("text"));text.set("text",V::makeStr(error.what()));content.arr.push_back(text);structured.set("error",V::makeStr(error.what()));result.set("content",content);result.set("structuredContent",structured);result.set("isError",V::makeBool(true));return result;}
}
struct BusyFeatureProgress {u64 id=0;std::string name;size_t index=0,total=0;bool completed=false,reused=false;};
static std::mutex g_busyProgressMutex;static BusyFeatureProgress g_busyFeatureProgress;
static void drawBusyFrame(HDC hdc){
    // The worker exclusively owns g_session. This path reads only a captured GL
    // frame and UI progress state; drawFrame and document access are forbidden.
    glViewport(0,0,winW,winH);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    glUseProgram(pBusy);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g_busySnapshotTex);
    glUniform1i(glGetUniformLocation(pBusy,"frame"),0);glUniform2f(glGetUniformLocation(pBusy,"res"),(float)winW,(float)winH);
    glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);
    drawDraftSketches();sampleStudioResources();updateCostDisplay();
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pUI);
    glUniform2f(uuVp,(float)winW,(float)winH);
    glBindVertexArray(quadVao);
    auto box=[&](float x,float y,float w,float h,float radius,float r,float g,float b,float a){
        glUniform1i(uuGlyph,0);glUniform4f(uuRc,x,y,w,h);glUniform1f(uuRad,radius);
        glUniform4f(uuFill,r,g,b,a);glUniform4f(uuBorder,0,0,0,0);glUniform1f(uuBW,0);
        glDrawArrays(GL_TRIANGLES,0,6);
    };
    box(0,0,(float)winW,(float)winH,0,.025f,.038f,.057f,.12f);
    float width=std::min(492.f,(float)winW-44.f),x=std::max(12.f,(float)(viewX+viewW)-width-18.f),y=std::max((float)viewY+12.f,(float)(viewY+viewH)-194.f);
    box(x-2,y-2,width+4,180,14,.24f,.37f,.52f,.82f);
    box(x,y,width,176,13,.075f,.096f,.126f,.99f);
    box(x+20,y+22,5,44,2,.39f,.77f,.98f,1);
    auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-g_busyStarted).count();
    textBegin();
    if(g_teamStarted){static auto nextTeamFrame=std::chrono::steady_clock::time_point{};static AgentTeamSnapshot liveTeam;const auto now=std::chrono::steady_clock::now();if(now>=nextTeamFrame){liveTeam=g_agentTeam.snapshot(false,std::numeric_limits<u64>::max());nextTeamFrame=now+std::chrono::milliseconds(80);}float ty=(float)viewY+16;for(const auto& role:liveTeam.roles){const std::string line=role.id+" / "+agentTeamStateName(role.state)+" / "+role.status;textOut(fitText(line,.64f,std::min(720,viewW-32)),viewX+16,ty,.64f,.65f,.90f,1.f);ty+=19;}}
    textOut(g_busyLabel,x+43,y+21,.95f,.92f,.96f,1.f);
    BusyFeatureProgress progress;{std::lock_guard lock(g_busyProgressMutex);progress=g_busyFeatureProgress;}
    char duration[128];if(progress.total)std::snprintf(duration,sizeof duration,"Feature %zu/%zu / #%llu / %s / %.1f s",progress.index+1,progress.total,(unsigned long long)progress.id,progress.completed?"ready":progress.reused?"cached":"evaluating",elapsed);
    else std::snprintf(duration,sizeof duration,"Tool processing / %.1f s elapsed",elapsed);
    textOut(duration,x+43,y+53,.66f,.63f,.72f,.82f);
    textOut(g_activeToolRole.empty()?"Committed scene retained":g_activeToolRole+" / "+g_activeToolName,x+24,y+94,.65f,.77f,.84f,.92f);
    textOut("Live sketch drafts are uncommitted; the step publishes atomically",x+24,y+119,.56f,.58f,.68f,.79f);
    float phase=(float)std::fmod(elapsed*1.6,1.);
    box(x+24,y+153,width-48,4,2,.20f,.28f,.36f,1);
    box(x+24+phase*(width-138),y+153,90,4,2,.42f,.79f,.98f,1);
    glUseProgram(pText);glUniform2f(glGetUniformLocation(pText,"vp"),(float)winW,(float)winH);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,fa.tex);glUniform1i(glGetUniformLocation(pText,"txx"),0);
    glBindVertexArray(textVao);glBindBuffer(GL_ARRAY_BUFFER,textVbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(g_textVerts.size()*sizeof(float)),g_textVerts.data(),GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,28,nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,28,(void*)8);
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,28,(void*)16);
    glDrawArrays(GL_TRIANGLES,0,(GLint)g_textVerts.size()/7);
    if(g_swap)SwapBuffers(hdc);++g_busyFrameCount;
}

class StudioSessionExecutor {
    std::mutex mutex_;std::condition_variable ready_;std::function<void()> pending_;bool stop_=false;std::thread thread_;
public:
    StudioSessionExecutor():thread_([this]{for(;;){std::function<void()> job;{std::unique_lock<std::mutex> lock(mutex_);ready_.wait(lock,[&]{return stop_||(bool)pending_;});if(stop_&&!pending_)return;job=std::move(pending_);pending_={};}job();}}){}
    ~StudioSessionExecutor(){{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}ready_.notify_all();thread_.join();}
    std::future<arkjson::Value> submit(const std::string& tool,const arkjson::Value& args,unsigned delay,std::function<arkjson::Value()> operation={}){
        auto task=std::make_shared<std::packaged_task<arkjson::Value()>>([tool,args,delay,operation=std::move(operation)]() mutable{
            struct ObserverScope {EvaluationObserver previous;~ObserverScope(){setEvaluationObserver(std::move(previous));}} observer{setEvaluationObserver([](u64 id,const std::string& name,size_t index,size_t total,bool completed,bool reused){std::lock_guard lock(g_busyProgressMutex);g_busyFeatureProgress={id,name,index,total,completed,reused};})};
            if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(delay));return operation?operation():g_session.call(tool,args);});auto future=task->get_future();
        {std::lock_guard<std::mutex> lock(mutex_);if(stop_||pending_)throw std::runtime_error("ARK session executor is unavailable");pending_=[task]{(*task)();};}ready_.notify_one();return future;
    }
};
static arkjson::Value callSessionWithProgress(const std::string& tool,const arkjson::Value& args,std::function<arkjson::Value()> operation){
    HDC hdc=GetDC(g_window);if(!hdc)throw std::runtime_error("Could not acquire ARK viewport for kernel operation");
    struct BusyCleanup{
        HDC hdc;bool previousSwap;
        ~BusyCleanup(){g_busy=false;g_swap=previousSwap;ReleaseDC(g_window,hdc);
            if(g_busyClosePending){g_busyClosePending=false;PostMessage(g_window,WM_CLOSE,0,0);}}
    } cleanup{hdc,g_swap};
    {std::lock_guard lock(g_busyProgressMutex);g_busyFeatureProgress={};}
    g_swap=false;
    drawFrame(hdc);glFinish();
    std::vector<u8> stable((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,stable.data());
    if(!g_busySnapshotTex)glGenTextures(1,&g_busySnapshotTex);
    glBindTexture(GL_TEXTURE_2D,g_busySnapshotTex);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,winW,winH,0,GL_RGB,GL_UNSIGNED_BYTE,stable.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    g_swap=cleanup.previousSwap;g_busy=true;g_busyStarted=std::chrono::steady_clock::now();
    g_busyLabel=tool=="assistant_model_catalog"?"Reading provider model catalog":tool=="ui_projection_preview"?"Previewing exact projection":tool=="library_fetch"?"Downloading component":tool=="library_source_search"?"Searching component catalog":tool=="library_ingest"?"Importing component into library":tool=="library_insert"?"Inserting library component":tool.rfind("library_",0)==0?"Inspecting component library":tool=="doc_load"?"Loading document":tool=="doc_preview"?"Evaluating uncommitted preview":tool=="assembly_info"?"Inspecting assembly":tool=="native_inspect"?"Inspecting exact geometry":tool.rfind("import",0)==0?"Importing geometry":tool.rfind("export",0)==0?"Exporting geometry":"Evaluating geometry";
    static StudioSessionExecutor executor;auto response=executor.submit(tool,args,g_busyTestDelayMs,std::move(operation));
    struct JoinRequest {std::future<arkjson::Value>& response;~JoinRequest(){if(response.valid())response.wait();}} join{response};
    unsigned frameBefore=g_busyFrameCount;
    while(response.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){
        MSG message{};
        while(PeekMessageA(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT||message.message==WM_CLOSE){g_busyClosePending=true;continue;}
            if(message.hwnd==g_window&&(message.message==WM_ARK_TOOL||message.message==WM_ARK_CHAT||message.message==WM_ARK_AUTH||message.message==WM_ARK_PRICE||message.message==WM_ARK_TEAM||message.message==WM_SIZE)){
                DispatchMessageA(&message);++g_busyPumpCount;
            }else if(message.message==WM_PAINT&&message.hwnd)ValidateRect(message.hwnd,nullptr);
        }
        drawBusyFrame(hdc);Sleep(16);
        if(g_busyTestMode&&g_busyFrameCount==frameBefore+1&&!g_busyTestShot.empty()){
            std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);
            glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());
            for(int y=0;y<winH;++y)std::memcpy(&image.px[(size_t)y*winW*3],&pixels[(size_t)(winH-1-y)*winW*3],(size_t)winW*3);
            writePNG(g_busyTestShot.c_str(),image);
        }
    }
    return response.get();
}

enum class ContextTarget{Empty,Body,Sketch,Datum,Feature,SheetEntity};
struct ContextEntry{int action=0;const char* title="";bool enabled=true;const char* hint="";};
static int contextRowHeight(const ContextEntry& entry){return entry.action?32:entry.title[0]?27:8;}
static void drawContextEntry(HDC dc,RECT bounds,const ContextEntry& entry,bool selected){
    COLORREF background=selected&&entry.enabled?RGB(39,65,83):RGB(25,32,39);HBRUSH brush=CreateSolidBrush(background);FillRect(dc,&bounds,brush);DeleteObject(brush);
    SetBkMode(dc,TRANSPARENT);int left=bounds.left,top=bounds.top;
    if(!entry.action){if(!entry.title[0]){HPEN pen=CreatePen(PS_SOLID,1,RGB(59,73,85));auto old=SelectObject(dc,pen);MoveToEx(dc,left+14,top+3,nullptr);LineTo(dc,bounds.right-14,top+3);SelectObject(dc,old);DeleteObject(pen);return;}
        HFONT font=CreateFontW(-11,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto old=SelectObject(dc,font);SetTextColor(dc,RGB(145,171,190));RECT text{left+15,top+5,bounds.right-12,bounds.bottom};DrawTextA(dc,entry.title,-1,&text,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,old);DeleteObject(font);return;}
    COLORREF icon=entry.enabled?RGB(112,190,230):RGB(83,101,115);HPEN pen=CreatePen(PS_SOLID,2,icon);auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));int x=left+25,y=top+21;
    auto line=[&](int ax,int ay,int bx,int by){MoveToEx(dc,x+ax,y+ay,nullptr);LineTo(dc,x+bx,y+by);};
    if(entry.action==161||entry.action==162||entry.action==64){line(-7,6,5,-6);line(-7,6,-8,9);line(-8,9,-4,8);line(3,-7,7,-3);}
    else if(entry.action==340||entry.action==341||entry.action==68||entry.action==72||entry.action==344){Ellipse(dc,x-9,y-5,x+10,y+6);Ellipse(dc,x-3,y-3,x+4,y+4);if(entry.action==340||entry.action==68||entry.action==72)line(-9,-8,9,8);}
    else if(entry.action==342||entry.action==343||entry.action==20){Rectangle(dc,x-7,y-7,x+8,y+8);line(-10,0,10,0);line(0,-10,0,10);}
    else if(entry.action==187){Ellipse(dc,x-9,y-8,x+2,y+3);Ellipse(dc,x-1,y-3,x+10,y+8);}
    else if(entry.action==29){line(-9,0,9,0);line(0,-9,0,9);line(6,-3,9,0);line(9,0,6,3);line(-3,-6,0,-9);line(0,-9,3,-6);}
    else if(entry.action==172||entry.action==173){line(-8,6,-4,-6);line(-4,-6,8,-2);Ellipse(dc,x-10,y+4,x-6,y+8);Ellipse(dc,x+6,y-4,x+10,y);}
    else if(entry.action==394||entry.action==397){Ellipse(dc,x-7,y-7,x+8,y+8);if(entry.action==394)line(-9,7,9,7);else Ellipse(dc,x-3,y-3,x+4,y+4);}
    else{Rectangle(dc,x-7,y-6,x+7,y+7);line(-7,-6,-2,-10);line(-2,-10,11,-10);line(11,-10,7,-6);line(11,-10,11,3);line(11,3,7,7);}
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
    HFONT font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);SetTextColor(dc,entry.enabled?RGB(229,237,244):RGB(117,132,145));RECT title{left+49,top+4,bounds.right-15,bounds.bottom};DrawTextA(dc,entry.title,-1,&title,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    SelectObject(dc,oldFont);DeleteObject(font);
}
static HMENU createContextPopup(std::vector<ContextEntry>& entries){
    HMENU menu=CreatePopupMenu();if(!menu)return nullptr;
    for(auto& entry:entries){MENUITEMINFOA item{sizeof item};item.fMask=MIIM_FTYPE|MIIM_ID|MIIM_DATA|MIIM_STATE|MIIM_STRING;item.fType=MFT_OWNERDRAW;item.wID=(UINT)entry.action;item.dwItemData=(ULONG_PTR)&entry;item.fState=entry.action&&entry.enabled?MFS_ENABLED:MFS_DISABLED;item.dwTypeData=const_cast<char*>(entry.title);InsertMenuItemA(menu,(UINT)GetMenuItemCount(menu),TRUE,&item);}
    MENUINFO style{sizeof style};style.fMask=MIM_BACKGROUND;static HBRUSH background=CreateSolidBrush(RGB(25,32,39));style.hbrBack=background;SetMenuInfo(menu,&style);return menu;
}
static bool captureContextMenu(const std::vector<ContextEntry>& entries,const std::string& path){
    int height=0;for(const auto& entry:entries)height+=contextRowHeight(entry);constexpr int width=306;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
    HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}auto old=SelectObject(dc,bitmap);int y=0;
    for(const auto& entry:entries){RECT row{0,y,width,y+contextRowHeight(entry)};drawContextEntry(dc,row,entry,entry.action==161);y=row.bottom;}GdiFlush();
    Image result;result.w=width;result.h=height;result.px.resize((size_t)width*height*3);auto* source=(unsigned char*)pixels;
    for(size_t i=0;i<(size_t)width*height;++i){result.px[i*3]=source[i*4+2];result.px[i*3+1]=source[i*4+1];result.px[i*3+2]=source[i*4];}
    bool ok=writePNG(path.c_str(),result);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok;
}
static std::vector<ContextEntry> contextEntries(ContextTarget target,int body=-1){
    std::vector<ContextEntry> menu;
    if(target==ContextTarget::Body&&body>=0&&(size_t)body<g_bodyIds.size()){
        const Feature* feature=g_doc.find(g_bodyIds[(size_t)body]);bool hidden=(size_t)body<g_hiddenBodies.size()&&g_hiddenBodies[(size_t)body];
        bool editable=feature&&feature->type!=FeatureType::SourceTube&&feature->type!=FeatureType::SourceSheet&&feature->type!=FeatureType::MeshBody;
        menu.push_back({0,"Model"});menu.push_back({161,"Edit feature",editable,"Parameters and operation history"});menu.push_back({162,"Edit driving sketch",feature&&!drivingSketchNames(*feature).empty(),"Open the saved profile"});menu.push_back({577,"Edit source parameters",feature!=nullptr,"Named bindings in this definition and its driving history"});
        menu.push_back({29,"Edit placement",g_instanceIds[(size_t)body]!=0,"Assembly position and rotation"});menu.push_back({562,"Move component",g_instanceIds[(size_t)body]!=0,"Drag along a supported assembly joint"});menu.push_back({187,"Material and appearance",feature!=nullptr,"Density, color and surface"});
        bool transparent=feature&&resolvedAppearance(g_doc,*feature).opacity<.999;
        menu.push_back({560,transparent?"Restore opaque":"Make transparent",feature!=nullptr,"All instances of this part definition; material retained"});
        menu.push_back({561,"Adjust opacity...",feature!=nullptr,"Part definition / 5-100 percent"});
        if(feature&&featureManufacturingLineage(g_doc,*feature).editableSheet){menu.push_back({394,"Add return hem",true,"Open round 180 degree edge return"});menu.push_back({397,"Add circular emboss",true,"Exact constant-thickness form"});}
        if(feature&&feature->type==FeatureType::Suspension)menu.push_back({384,"Spring unit controls",true,"Suspension travel, steering and wheel spin"});
        menu.push_back({0,"Agent"});menu.push_back({565,"Ask Agent about this object",feature!=nullptr,"Attach this occurrence to your next explicit message"});
        menu.push_back({0,"Inspect"});menu.push_back({401,"Properties"});menu.push_back({188,"Mass / center of gravity"});menu.push_back({171,"Select named faces",true,"Persistent native surface names"});menu.push_back({172,"Select named edges",true,"Persistent native edge names"});
        menu.push_back({0,"View"});menu.push_back({hidden?341:340,hidden?"Show":"Hide",true,"Display only"});
        menu.push_back({342,"Spotlight",true,"Spotlight the selected instance"});menu.push_back({343,"Exit Spotlight",g_spotlightActive,"Restore the previous display set"});
        menu.push_back({344,"Show all bodies"});
    }else if(target==ContextTarget::SheetEntity&&g_selectedExplorerEntity){menu={{0,"SHEET OPERATION"},{g_selectedExplorerEntity->edit,"Edit selected operation",true,"Stable entity in the original sheet program"},{g_selectedExplorerEntity->remove,"Remove selected operation",true,"Undo restores this authored entity"}};
    }else if(target==ContextTarget::Feature){auto* feature=g_doc.find(g_explorerFeature);menu={{0,"FEATURE HISTORY"},{161,"Edit feature definition",feature!=nullptr,"Recompute this operation and its dependents"},{162,"Edit driving sketch",feature&&!drivingSketchNames(*feature).empty(),"Open the saved profile"}};
    }else if(target==ContextTarget::Sketch){
        menu={{610,"Edit sketch in viewport"},{570,"Draw sketch in view"},{64,"Edit sketch"},{577,"Edit source parameters"},{71,"Normal to sketch plane"},{68,g_showSketches?"Hide sketches":"Show sketches"},{0,""},{173,"Select sketch curves"}};
    }else if(target==ContextTarget::Datum){
        menu={{644,"Edit selected datum"},{641,"Toggle selected datum"},{640,"Select construction datum"},{71,"Align view to plane"},{72,g_showDatumPlanes?"Hide datums":"Show datums"},{0,""},{171,"Select planar faces"}};
    }else{
        menu={{343,"Restore Spotlight",g_spotlightActive},{344,"Show all bodies"},{0,""},{20,"Fit document"},{24,"Isometric view"},{23,"Top view"},{56,"Graphics settings"}};
    }
    return menu;
}
static ContextTarget contextTargetAt(int mx,int my,int& body){
    ContextTarget target=ContextTarget::Empty;body=-1;
    if(mx<viewX){for(const Zone& zone:g_zones)if(mx>=zone.x&&mx<zone.x+zone.w&&my>=zone.y&&my<zone.y+zone.h){
            if(zone.id>=22000&&zone.id<26096){body=zone.id-22000;target=ContextTarget::Body;}
            else if(zone.id>=18000&&zone.id<22000){uiAction(zone.id);target=ContextTarget::SheetEntity;}
            else if(zone.id>=16000&&zone.id<17024){uiAction(zone.id);target=ContextTarget::Feature;}
            else if(zone.id>=2000&&zone.id<9000){g_explorerFeature=0;body=zone.id-2000;target=ContextTarget::Body;}
            else if(zone.id>=11000&&zone.id<12000){size_t index=(size_t)(zone.id-11000);if(index<g_doc.sketches.size()){
                    g_activeSketch=g_doc.sketches[index].name;g_activePlane=g_doc.sketches[index].sk.plane;g_showSketches=true;rebuildSketchOverlay();target=ContextTarget::Sketch;}}
            else if(zone.id>=14000&&zone.id<15000){size_t index=(size_t)(zone.id-14000);if(index<g_doc.planes.size()){
                    g_activePlane=g_doc.planes[index].name;g_selectedDatumName=g_doc.planes[index].name;g_activeSketch.clear();target=ContextTarget::Datum;}}
            break;
        }}
    else if(mx>=viewX&&mx<viewX+viewW&&my>=viewY&&my<viewY+viewH){body=pickBody(mx,my);if(body>=0)target=ContextTarget::Body;}
    else return ContextTarget::Empty;
    if(target==ContextTarget::Body){clearDatumSelection();selBody=body;g_activeSketch.clear();g_showSketches=false;rebuildSketchOverlay();}
    return target;
}
static void showContextMenu(int mx,int my){
    if(mx>=viewX+viewW||my<viewY||my>=winH-28)return;
    int body=-1;ContextTarget target=contextTargetAt(mx,my,body);
    auto entries=contextEntries(target,body);HMENU menu=createContextPopup(entries);if(!menu)return;
    POINT screen{mx,my};ClientToScreen(g_window,&screen);SetForegroundWindow(g_window);
    int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,g_window,nullptr);
    DestroyMenu(menu);PostMessage(g_window,WM_NULL,0,0);if(action)interactiveAction(action);
}

// ---------------------------------------------------------------------------
static LRESULT CALLBACK wndProc(HWND hw,UINT m,WPARAM wp,LPARAM lp){
    if(g_busy){
        if(m==WM_CLOSE){g_busyClosePending=true;return 0;}
        if(m==WM_COMMAND||m==WM_MBUTTONDOWN||m==WM_MBUTTONUP||m==WM_MOUSEWHEEL||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_LBUTTONDBLCLK||m==WM_RBUTTONDOWN||m==WM_RBUTTONUP||m==WM_MOUSEMOVE||m==WM_KEYDOWN||m==WM_KEYUP)return 0;
    }
    if(g_uiReady&&g_drawingViewport.active){if(m==WM_KEYDOWN&&drawingViewportKey(wp))return 0;if(drawingViewportMouse(m,wp,lp))return 0;}
    switch(m){
        case WM_ARK_TEAM:{std::unique_ptr<UiTeamEvent> value((UiTeamEvent*)lp);if(value->epoch==g_documentEpoch&&value->runSerial==g_teamRunSerial&&g_teamStarted)receiveTeamEvent(value->event);return 0;}
        case WM_ARK_PRICE:{std::unique_ptr<PricingCatalog> value((PricingCatalog*)lp);g_pricing=std::move(*value);g_pricingBusy=false;g_pricingReady=!g_pricing.models.empty()||!g_pricing.subscriptionCredits.empty();updateCostDisplay(true);return 0;}
        case WM_MEASUREITEM:{auto* item=(MEASUREITEMSTRUCT*)lp;if(item->CtlType==ODT_MENU&&item->itemData){auto* entry=(ContextEntry*)item->itemData;item->itemWidth=306;item->itemHeight=contextRowHeight(*entry);return TRUE;}break;}
        case WM_DRAWITEM:{auto* item=(DRAWITEMSTRUCT*)lp;if(item->CtlType==ODT_MENU&&item->itemData){drawContextEntry(item->hDC,item->rcItem,*(ContextEntry*)item->itemData,(item->itemState&ODS_SELECTED)!=0);return TRUE;}break;}
        case WM_ARK_TOOL:g_toolQueue.emplace_back((UiToolRequest*)lp);return 0;
        case WM_ARK_CHAT:{std::unique_ptr<UiChatEvent> event((UiChatEvent*)lp);
            if(event->final){g_chatBusy=false;if(event->epoch==g_documentEpoch){g_assistantResponseFinished=true;g_assistantResponseError=event->error||*g_assistantCancel;}if(event->epoch==g_documentEpoch&&event->history.t==arkjson::Value::Arr)g_conversation=event->history;else if(event->epoch!=g_documentEpoch)g_conversation=arkjson::Value::makeArr();}
            if(event->epoch==g_documentEpoch){
                if(event->status||(!event->final&&!event->text.empty()))g_assistantStatus=event->text;
                else if(event->final)g_assistantStatus.clear();
                if(event->hasPerformance)g_assistantPerformanceRun=event->performance;
                if(event->hasUsage){g_assistantUsageRun=event->usage;recordAssistantUsageSnapshot(event->usage);}
                if(event->final)g_assistantUsageRunCounted=true;
                if(!event->text.empty()&&(!event->status||event->retry)){g_chatLog.push_back({event->error?"Error":event->final?"ARK":"Action",event->text.substr(0,16000)});if(g_chatLog.size()>200)g_chatLog.erase(g_chatLog.begin());g_chatScroll=0;}
            }
            if(event->final&&!g_deferredAssistantSteering.empty()){
                auto queued=std::move(g_deferredAssistantSteering.front());g_deferredAssistantSteering.pop_front();
                if(queued.epoch==g_documentEpoch&&!*g_assistantCancel){try{sendAssistant("",queued.transport,&queued.message,queued.visiblePrompt);}catch(const std::exception& error){g_chatLog.push_back({"Error",error.what()});g_deferredAssistantSteering.clear();}}
                else {g_deferredAssistantSteering.clear();g_chatLog.push_back({"ARK","Queued Assistant input was retained for a previous drawing or cancelled run; it was not sent."});}
            }return 0;
        }
        case WM_ARK_AUTH:{std::unique_ptr<UiAuthEvent> event((UiAuthEvent*)lp);bool cancelRequested=g_authCancelRequested;
            g_authBusy=false;g_authCancelRequested=false;g_authStatus=std::move(event->status);
            if(event->job==AuthJob::SignOut&&!g_authStatus.connected){g_authModels.clear();clearPendingRoleSettings();if(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan){
                g_assistantConfig.authMode=AssistantAuthMode::ApiKey;g_assistantConfig.model.clear();*g_assistantCancel=true;++g_documentEpoch;g_conversation=arkjson::Value::makeArr();try{saveUiSettings();}catch(...){}}
            }else if(event->job==AuthJob::SignIn||event->job==AuthJob::Models){g_authModels=std::move(event->models);}
            if(!event->error.empty()){const std::string& diagnostic=event->error;
                bool sensitive=diagnostic.find("https://")!=std::string::npos||diagnostic.find("http://")!=std::string::npos||diagnostic.find("Bearer ")!=std::string::npos||diagnostic.find("eyJ")!=std::string::npos;
                if(event->job==AuthJob::SignIn&&diagnostic=="ChatGPT browser sign-in timed out")
                    g_status="No ChatGPT browser callback arrived within 3 minutes. Close the old tab, then choose Continue with ChatGPT for a fresh attempt.";
                else if(cancelRequested&&diagnostic.find("cancel")!=std::string::npos){
                    if(event->job==AuthJob::SignIn)g_status=g_authStatus.connected?"ChatGPT connected; model loading cancelled. Choose a model from Account.":"ChatGPT sign-in cancelled. Close the old browser tab; choose Continue with ChatGPT for a fresh attempt.";
                    else if(event->job==AuthJob::Models)g_status="ChatGPT model loading cancelled. Choose a model from Account to retry.";
                    else g_status=g_authStatus.connected?"ChatGPT sign-out request stopped; account remains connected.":"Signed out locally; check Account for current status.";
                }else g_status=sensitive?"ChatGPT account operation failed. Retry from Account.":"ChatGPT: "+diagnostic.substr(0,200);
            }
            else if(event->job==AuthJob::SignOut)g_status=g_authStatus.remoteRevocationConfirmed?"Signed out and remote session revoked":"Signed out locally; remote revocation was not confirmed";
            else if(g_authStatus.connected&&g_authStatus.planAuthorized)g_status="ChatGPT plan authorized. Choose a model from Account.";
            else if(g_authStatus.connected)g_status="ChatGPT identity connected. Enable plan usage from Account if desired.";
            else g_status=g_authStatus.message.empty()?"ChatGPT sign-in was not completed":g_authStatus.message;
            if(event->job==AuthJob::Models){if(event->error.empty()&&!cancelRequested)g_status=g_authModels.empty()?"The selected account returned no available models. Refresh from Account to retry.":"Account model catalog updated: "+std::to_string(g_authModels.size())+" available models.";
                if(g_pendingRoleSettings>=0){bool owner=g_pendingRoleAccount==g_authStatus.activeClientId&&g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan;
                    if(g_roleModelWindow&&owner){SendMessage(g_roleModelWindow,WM_ARK_ROLE_CATALOG,0,0);}
                    else if(g_roleModelWindow&&!owner){SendMessage(g_roleModelWindow,WM_CLOSE,0,0);clearPendingRoleSettings();}
                    else if(owner&&event->error.empty()&&!cancelRequested&&!g_authModels.empty())g_roleCatalogReady=true;else clearPendingRoleSettings();
                }}
            return 0;}
        case WM_COMMAND:if(LOWORD(wp)==500){interactiveAction(74);return 0;}if(LOWORD(wp)==497&&HIWORD(wp)==EN_CHANGE){int length=GetWindowTextLengthA(g_toolSearch);std::string filter((size_t)length+1,' ');GetWindowTextA(g_toolSearch,filter.data(),length+1);filter.resize((size_t)length);g_toolFilter=std::move(filter);g_toolScroll=0;return 0;}break;
        case WM_DROPFILES:try{droppedStudioFiles((HDROP)wp);}catch(const std::exception& error){g_error=true;g_status=error.what();}return 0;
        case WM_CTLCOLOREDIT:if((HWND)lp==g_chatInput||(HWND)lp==g_commandSearch||(HWND)lp==g_toolSearch||isAssistantImageDimensionEdit((HWND)lp)){SetTextColor((HDC)wp,RGB(219,226,238));SetBkColor((HDC)wp,RGB(14,17,20));return (LRESULT)chatBrush;}break;
        case WM_ERASEBKGND:return 1; // OpenGL owns the parent surface; erase only the native EDIT child.
        case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={1180,840};return 0;
        case WM_SIZE:
            if(wp==SIZE_MINIMIZED)return 0; // Keep the last valid viewport while minimized.
            winW=LOWORD(lp)>0?LOWORD(lp):1;
            winH=HIWORD(lp)>0?HIWORD(lp):1;
            layout();return 0;
        case WM_MOUSEWHEEL:
            {POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hw,&p);
            if(g_toolsOpen){auto bounds=toolGalleryBounds();if(PtInRect(&bounds,p)){int& scroll=p.x<bounds.left+205?g_toolCategoryScroll:g_toolScroll;scroll=std::max(0,scroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-3:3));return 0;}}
            if(g_assistantDockOpen&&p.x>=viewX&&p.x<viewX+viewW&&p.y>=winH-32-assistantDockHeight()){g_chatScroll=std::max(0,g_chatScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?2:-2));return 0;}
            if(g_inspectorOpen&&p.x<viewX&&p.y>=viewY+42){g_inspectorScroll=std::max(0,g_inspectorScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-48:48));return 0;}
            if(g_sidebarTab==1&&p.x<viewX&&p.y>=viewY+42){g_layersScroll=std::max(0,g_layersScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-2:2));return 0;}
            if(g_sidebarTab==0&&PtInRect(&g_treeViewport,p)){g_treeScroll=std::clamp(g_treeScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-3:3),0,g_treeMaxScroll);return 0;}}
            tDist=clamp(tDist*(GET_WHEEL_DELTA_WPARAM(wp)>0?0.87:1.15),2.,1e9);
            return 0;
        case WM_RBUTTONUP:if(measurementActive()){endMeasurement();return 0;}if(g_sketchDrawing.active){if(!g_sketchDrawing.anchors.empty()){g_sketchDrawing.anchors.pop_back();rebuildDrawingPreview();g_status="Last draft anchor removed";}else cancelSketchDrawing();return 0;}showContextMenu(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;
        case WM_LBUTTONDOWN:{
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};g_pointerX=p.x;g_pointerY=p.y;g_cubePress=false;g_componentDragIntent={};
            if(g_sidebarTab==0&&g_treeMaxScroll&&PtInRect(&g_treeTrack,p)){g_pressWasButton=true;g_treeDragging=PtInRect(&g_treeThumb,p);g_treePaging=!g_treeDragging;if(g_treeDragging)g_treeDragOffset=p.y-g_treeThumb.top;else g_treeScroll=std::clamp(g_treeScroll+(p.y<g_treeThumb.top?-g_treeVisibleRows:g_treeVisibleRows),0,g_treeMaxScroll);SetCapture(hw);return 0;}
            g_pressWasButton=p.x<viewX||p.x>viewX+viewW||p.y<viewY||p.y>viewY+viewH;
            for(auto&z:g_zones)
                if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&(float)p.y>=z.y&&(float)p.y<=z.y+z.h)
                    { if(z.id==688){try{if(!g_directAuthoring.active)beginDirectHandle();g_directAuthoring.handleStart=true;g_pressWasButton=false;}catch(const std::exception& error){g_error=true;g_status=error.what();g_pressWasButton=true;}}else if(z.id==179){g_cubePress=cubeHitAt(p.x,p.y).hit;g_pressWasButton=false;}
                        else{g_pressWasButton=true;if(z.id==50){g_foldScrubbing=true;g_foldPlaying=false;g_foldProgress=clamp((double)(p.x-z.x)/z.w,0.,1.);}
                        else if(z.id==53){g_sheetScrubbing=true;g_sheetPlayingId=0;if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}}
                        else if(z.id>=380&&z.id<=382){g_suspScrub=z.id-380;g_suspSpinPlaying=false;try{setSuspensionControl(g_suspScrub,(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}}} break; }
            g_dragged=false;if(!g_pressWasButton&&!g_cubePress&&g_directAuthoring.active){try{beginDirectAuthoringDrag(p.x,p.y);}catch(const std::exception& error){g_directAuthoring.feature=0;g_directAuthoring.dragging=g_directAuthoring.moved=false;g_directAuthoring.preview={};g_directAuthoring.commands={};g_directAuthoring.error=error.what();g_error=true;g_status=error.what();}SetCapture(hw);return 0;}
            if(!g_pressWasButton&&!g_cubePress&&g_sketchEditing.active){try{beginSketchEditDrag(p.x,p.y);}catch(const std::exception& error){g_error=true;g_status=error.what();}SetCapture(hw);return 0;}
            if(!g_pressWasButton&&!g_cubePress&&g_sketchDrawing.active){SetCapture(hw);return 0;}
            if(!g_pressWasButton&&!g_cubePress&&g_componentMove.armed){try{beginComponentDrag(p.x,p.y);}catch(const std::exception& error){g_error=true;g_status=error.what();g_pressWasButton=true;}}
            else if(!g_pressWasButton&&!g_foldScrubbing&&!g_sheetScrubbing&&(g_cubePress||g_navMode!=0))beginCameraDrag(p.x,p.y);
            else if(!g_pressWasButton&&!g_cubePress&&!g_pickKind&&!measurementActive()&&!g_allSheetsFlat&&!g_stockMode&&!(g_showSketches&&!g_activeSketch.empty())&&!(wp&(MK_SHIFT|MK_CONTROL))&&!(GetKeyState(VK_MENU)&0x8000)&&pickBody(p.x,p.y)>=0)g_componentDragIntent={true,p.x,p.y,g_session.revision,g_documentEpoch};
            SetCapture(hw);
            return 0;
        }
        case WM_MBUTTONDOWN:{g_componentDragIntent={};if(g_componentMove.dragging)cancelComponentMove();POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};g_pressWasButton=p.x<viewX||p.x>=viewX+viewW||p.y<viewY||p.y>=viewY+viewH;g_dragged=false;if(!g_pressWasButton)beginCameraDrag(p.x,p.y);SetCapture(hw);return 0;}
        case WM_MBUTTONUP:g_cameraDrag.active=false;ReleaseCapture();return 0;
        case WM_CAPTURECHANGED:if(g_directAuthoring.dragging)cancelDirectAuthoring();if(g_sketchEditing.dragging)cancelSketchEditDrag();g_componentDragIntent={};g_treeDragging=g_treePaging=false;g_cameraDrag.active=false;if(g_componentMove.dragging){cancelComponentMove();g_status="Component movement cancelled after pointer capture was lost";}return 0;
        case WM_MOUSEMOVE:{
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            g_pointerX=x;g_pointerY=y;
            bool lbtn=wp&MK_LBUTTON,mbtn=wp&MK_MBUTTON,shift=wp&MK_SHIFT;
            if(g_treeDragging&&lbtn){moveTreeThumb(y);return 0;}
            if(g_directAuthoring.dragging&&lbtn){moveDirectAuthoringDrag(x,y);g_dragged=g_directAuthoring.moved;return 0;}
            if(g_sketchEditing.dragging&&lbtn){moveSketchEditDrag(x,y);g_dragged=g_sketchEditing.moved;return 0;}
            if(g_sketchDrawing.active&&!mbtn&&!g_cameraDrag.active){moveSketchDrawing(x,y);return 0;}
            if(g_componentDragIntent.pending&&lbtn){auto intent=g_componentDragIntent;
                if(wp&(MK_SHIFT|MK_CONTROL)||mbtn||(GetKeyState(VK_MENU)&0x8000))g_componentDragIntent={};
                else if(std::abs(x-intent.x)>=std::max(4,GetSystemMetrics(SM_CXDRAG))||std::abs(y-intent.y)>=std::max(4,GetSystemMetrics(SM_CYDRAG))){
                    g_componentDragIntent={};g_dragged=true;
                    try{if(intent.revision!=g_session.revision||intent.epoch!=g_documentEpoch)throw std::runtime_error("The drawing changed before movement started. Select the component again.");if(g_doc.assembly.empty()){beginDirectAuthoring(1);beginDirectAuthoringDrag(intent.x,intent.y);moveDirectAuthoringDrag(x,y);return 0;}beginComponentDrag(intent.x,intent.y,true);}
                    catch(const std::exception& error){cancelComponentMove();g_dragged=true;g_error=true;g_status=error.what();return 0;}
                }
            }
            if(g_componentMove.dragging&&lbtn){try{moveComponentDrag(x,y);}catch(const std::exception& error){cancelComponentMove();g_error=true;g_status=error.what();}return 0;}
            if(g_foldScrubbing&&lbtn)for(auto& zone:g_zones)if(zone.id==50){g_foldProgress=clamp((double)(x-zone.x)/zone.w,0.,1.);break;}
            if(g_sheetScrubbing&&lbtn)for(auto& zone:g_zones)if(zone.id==53){if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(x-zone.x)/zone.w);}catch(const std::exception& error){g_error=true;g_status=error.what();g_sheetScrubbing=false;}break;}
            if(g_suspScrub>=0&&lbtn)for(auto& zone:g_zones)if(zone.id==380+g_suspScrub){try{setSuspensionControl(g_suspScrub,(x-zone.x)/zone.w);}catch(const std::exception& error){g_error=true;g_status=error.what();g_suspScrub=-1;}break;}
            if(lbtn||mbtn){
                if(g_cameraDrag.active&&std::abs(x-g_cameraDrag.x)+std::abs(y-g_cameraDrag.y)>2)g_dragged=true;
                bool navigate=!g_pressWasButton&&!g_foldScrubbing&&!g_sheetScrubbing&&(mbtn||g_cubePress||g_navMode!=0);
                bool pan=navigate&&((mbtn&&shift)||(lbtn&&g_navMode==1&&!g_cubePress));
                bool zoom=navigate&&lbtn&&!mbtn&&!shift&&g_navMode==2&&!g_cubePress;
                if(navigate)moveCameraDrag(x,y,pan,zoom,mbtn&&(GetKeyState(VK_MENU)&0x8000));
            }
            else g_cameraDrag.active=false;
            size_t tot=0; for(auto&m:g_cpuMeshes)tot+=m.triCount();
            hoverBody=(!lbtn&&!mbtn&&!g_pickKind)?pickBody(x,y):-1;
            return 0;
        }
        case WM_LBUTTONUP:{
            g_componentDragIntent={};
            if(g_treeDragging||g_treePaging){if(g_treeDragging)moveTreeThumb(GET_Y_LPARAM(lp));g_treeDragging=g_treePaging=false;g_pressWasButton=false;ReleaseCapture();return 0;}
            if(g_directAuthoring.dragging){g_directAuthoring.dragging=false;try{if(g_directAuthoring.moved)applyDirectAuthoring();}catch(const std::exception& error){g_error=true;g_status=error.what();}g_pressWasButton=false;ReleaseCapture();return 0;}
            if(g_sketchEditing.dragging){try{finishSketchEditDrag();}catch(const std::exception& error){cancelSketchEditDrag();g_error=true;g_status=error.what();}g_pressWasButton=false;ReleaseCapture();return 0;}
            if(g_componentMove.dragging){g_componentMove.dragging=false;try{applyComponentMove();}catch(const std::exception& error){cancelComponentMove();g_error=true;g_status=error.what();}g_pressWasButton=false;ReleaseCapture();return 0;}
            g_cameraDrag.active=false;ReleaseCapture();
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};g_pointerX=p.x;g_pointerY=p.y;
            if(g_cubePress){if(!g_dragged)orientViewCube(p.x,p.y);}
            else if(g_pressWasButton){
                for(auto&z:g_zones)
                    if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&
                       (float)p.y>=z.y&&(float)p.y<=z.y+z.h){ if(z.id==50){g_foldProgress=clamp((double)(p.x-z.x)/z.w,0.,1.);g_foldPlaying=false;}
                           else if(z.id==53){g_sheetPlayingId=0;if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}}
                           else if(z.id>=380&&z.id<=382){try{setSuspensionControl(z.id-380,(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}}
                           else interactiveAction(z.id); break; }
            } else if(!g_dragged&&!g_eatNextClick) {
                if(clickMeasurement(p.x,p.y)){}
                else if(clickSketchDrawing(p.x,p.y)){}
                else if(g_sketchEditing.active){}
                else if(g_pickKind)pickReference(p.x,p.y,(wp&(MK_CONTROL|MK_SHIFT))!=0);
                else{int b=pickBody(p.x,p.y);
                g_viewportContainer=0;g_explorerFeature=0;selBody=b;g_picked.clear();refreshSelectionLines();g_selectedNotch=0;g_notchScroll=0;
                if(selBody>=0){g_activeSketch.clear();g_showSketches=false;rebuildSketchOverlay();}}
            }
            g_eatNextClick=false;g_pressWasButton=false;g_cubePress=false;g_foldScrubbing=false;g_sheetScrubbing=false;g_suspScrub=-1;
            return 0;
        }
        case WM_LBUTTONDBLCLK:{
            g_eatNextClick=true;POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            g_componentDragIntent={};bool inControl=false;
            for(auto& z:g_zones)if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&(float)p.y>=z.y&&(float)p.y<=z.y+z.h){
                inControl=true;if(z.id>=760&&z.id<768){interactiveAction(z.id);}
                else if(z.id>=2000&&z.id<9000){selBody=z.id-2000;g_activeSketch.clear();g_showSketches=false;interactiveAction(161);}
                else if(z.id>=16000&&z.id<17024){interactiveAction(z.id);interactiveAction(161);}
                else if(z.id>=18000&&z.id<22000){interactiveAction(z.id);if(g_selectedExplorerEntity)interactiveAction(g_selectedExplorerEntity->edit);}
                else if(z.id>=11000&&z.id<12000){interactiveAction(z.id);interactiveAction(70);}
                break;
            }
            if(!inControl&&!g_pickKind&&drawingInView(p.x,p.y)&&!measurementActive()&&!g_sketchDrawing.active&&!g_sketchEditing.active&&!g_directAuthoring.active){int body=pickBody(p.x,p.y);if(body>=0)exposeSolidDimensions(body);}
            return 0;
        }
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE&&g_solidDimensions.feature&&!g_directAuthoring.active&&!g_sketchEditing.active&&!g_sketchDrawing.active){g_solidDimensions={};return 0;}
            if(g_directAuthoring.active){if(wp==VK_ESCAPE){cancelDirectAuthoring();ReleaseCapture();return 0;}if(wp==VK_TAB){g_directAuthoring.dragging=false;g_dragged=true;ReleaseCapture();interactiveAction(655);return 0;}if(wp==VK_RETURN){interactiveAction(656);return 0;}}
            if(g_sketchEditing.active){if(wp==VK_ESCAPE){if(g_sketchEditing.dragging)cancelSketchEditDrag();else cancelSketchEditing();ReleaseCapture();return 0;}if(wp==VK_TAB){interactiveAction(g_viewSketchDimension.valid?584:70);return 0;}}
            if(g_sketchDrawing.active){if(wp==VK_ESCAPE){cancelSketchDrawing();ReleaseCapture();return 0;}if(wp==VK_TAB){interactiveAction(576);return 0;}if(wp==VK_RETURN){g_sketchDrawing.anchors.clear();rebuildDrawingPreview();g_status="Line chain ended / saved segments retained / click first anchor";return 0;}}
            if(wp==VK_ESCAPE){if(measurementActive()){endMeasurement();return 0;}if(g_componentDragIntent.pending){g_componentDragIntent={};g_dragged=true;ReleaseCapture();g_status="Component movement cancelled / drawing unchanged";return 0;}if(g_componentMove.armed||g_componentMove.active){cancelComponentMove();ReleaseCapture();g_status="Component movement cancelled / drawing unchanged";return 0;}if(g_toolsOpen){g_toolsOpen=false;layout();return 0;}g_pickKind=0;g_picked.clear();refreshSelectionLines();g_status="Body selection / semantic picks cleared";return 0;}
            if(GetKeyState(VK_CONTROL)&0x8000){if(wp=='S')uiAction(16);else if(wp=='O')uiAction(17);else if(wp=='Z')uiAction(18);else if(wp=='Y')uiAction(19);else if(wp=='K')interactiveAction(59);else if(wp=='N')interactiveAction(55);}
            else if(wp==VK_TAB&&g_viewSketchDimension.valid)interactiveAction(584);
            else if(wp=='F')uiAction(20);else if(wp=='W')uiAction(14);else if(wp==VK_DELETE)uiAction(25);else if(wp==VK_F1)uiAction(31);
            else if(wp=='1')uiAction(180);else if(wp=='2')uiAction(181);else if(wp=='3')uiAction(23);else if(wp=='4')uiAction(184);else if(wp=='5')uiAction(182);else if(wp=='6')uiAction(183);else if(wp=='7')uiAction(24);
            return 0;
        case WM_CLOSE:
            if(!confirmDiscard())return 0;
            stopIntegrations();DestroyWindow(hw);return 0;
        case WM_DESTROY: stopIntegrations();PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hw,m,wp,lp);
}

// ---------------------------------------------------------------------------
static void seedDocument(){g_doc=makeDemoDocument();rebuildScene();frameScene();target=tTarget;dist=tDist;}
#include "skeleton_editor_selftest.inl"

#include "studio_render_selftest.inl"

int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR cmdLine,int){
    if(redirect_to_updated_runtime())return 0;
    struct JoinDirectPreview {~JoinDirectPreview(){g_directPreviewTestGate=false;if(g_directPreviewInstance)g_directPreviewInstance->shutdown();}} joinDirectPreview;
    cpu_detect();

    SetProcessDPIAware();
    bool startupUiTest=false;bool stampsUiTest=false;bool planesSectionsUiTest=false;
    bool specializedPatternUiTest=false;bool advancedSheetmetalUiTest=false;bool nativeVertexPullUiTest=false;bool cadImportUiTest=false;bool holesOrchestratorUiTest=false;bool designContainerUiTest=false;
    bool directAuthoringUiTest=false,parametricAuthoringUiTest=false,skeletonEditorUiTest=false,imageAttachmentUiTest=false,assistantSteeringUiTest=false;
    bool localModelsUiTest=false;bool viewportDockUiTest=false;bool bomUiTest=false;bool drawingWorkspaceUiTest=false;bool drawingViewportUiTest=false;bool measurementUiTest=false;bool authoringUiTest=false;
    bool selectionPickingUiTest=false;bool interactionUiTest=false,connectorsUiTest=false,actionIconsUiTest=false,treeUiTest=false,arcUiTest=false,completionUiTest=false,sketchDrawingUiTest=false,sketchEditUiTest=false,selectionGlowUiTest=false,datumAuthoringUiTest=false,typedSketchUiTest=false;
    bool selfTest=false,assemblyTest=false,dialogTest=false,bendTest=false,bendExportTest=false,sourceTest=false,sketchTest=false,assistantTest=false,nativeUiTest=false,renderUiTest=false,engineeringUiTest=false,sheetShowcaseTest=false,contextUiTest=false,suspensionUiTest=false,connectChatGpt=false,motionUiTest=false,motionPlay=false,navigationUiTest=false,projectionUiTest=false,frameToolsUiTest=false;unsigned exitAfterMs=0;std::string endpointFile,requestedSketch;int requestedOpening=-1;double requestedFold=-1,showcaseProgress=-1;std::string shotPath="ark_studio.png",loadPath,scriptPath,importPath,showcaseView,spotlightName,suspensionFeatureName,motionDriverName,motionSpeed;double requestedMotionTime=0;std::vector<std::string> suspensionValues;
    int requestedPathSamples=1;bool studioRenderTest=false,requestedPathTracing=false;
    int sw=1600,sh=1000,argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    auto utf8=[](const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;};
    for(int i=1;i<argc;++i){auto a=utf8(argv[i]);if(a=="--shot"&&i+1<argc){g_headless=true;shotPath=utf8(argv[++i]);}
        else if(a=="--self-test-startup-ui"){startupUiTest=true;g_headless=true;}
        else if(a=="--self-test-ui")selfTest=true;
        else if(a=="--self-test-sketch-ui")sketchTest=true;
        else if(a=="--self-test-typed-sketch-ui"){typedSketchUiTest=true;g_headless=true;}
        else if(a=="--datum-authoring-ui-test"){datumAuthoringUiTest=true;g_headless=true;}
        else if(a=="--self-test-sketch-edit-ui"){sketchEditUiTest=true;g_headless=true;}
        else if(a=="--self-test-specialized-pattern-ui"){specializedPatternUiTest=true;g_headless=true;}
        else if(a=="--self-test-advanced-sheetmetal-ui"){advancedSheetmetalUiTest=true;g_headless=true;}
        else if(a=="--self-test-holes-orchestrator-ui"){holesOrchestratorUiTest=true;g_headless=true;}
        else if(a=="--self-test-design-container-ui"){designContainerUiTest=true;g_headless=true;}
        else if(a=="--self-test-cad-import-ui"){cadImportUiTest=true;g_headless=true;}
        else if(a=="--self-test-native-vertex-pull-ui"){nativeVertexPullUiTest=true;g_headless=true;}
        else if(a=="--self-test-direct-authoring-ui"){directAuthoringUiTest=true;g_headless=true;}
        else if(a=="--self-test-parametric-authoring-ui"){parametricAuthoringUiTest=true;g_headless=true;}
        else if(a=="--skeleton-editor-test"){skeletonEditorUiTest=true;g_headless=true;}
        else if(a=="--self-test-image-attachment-ui"){imageAttachmentUiTest=true;g_headless=true;}
        else if(a=="--self-test-stamps-ui"){stampsUiTest=true;g_headless=true;}
        else if(a=="--self-test-planes-sections-ui"){planesSectionsUiTest=true;g_headless=true;}
        else if(a=="--self-test-assistant-steering-ui"){assistantSteeringUiTest=true;g_headless=true;}
        else if(a=="--self-test-selection-picking-ui"){selectionPickingUiTest=true;g_headless=true;}
        else if(a=="--self-test-selection-glow-ui"){selectionGlowUiTest=true;g_headless=true;}
        else if(a=="--self-test-sketch-drawing-ui"){sketchDrawingUiTest=true;g_headless=true;}
        else if(a=="--self-test-tree-ui"){treeUiTest=true;g_headless=true;}
        else if(a=="--self-test-arc-ui"){arcUiTest=true;g_headless=true;}
        else if(a=="--self-test-completion-ui"){completionUiTest=true;g_headless=true;}
        else if(a=="--self-test-assistant-ui")assistantTest=true;
        else if(a=="--self-test-native-ui")nativeUiTest=true;
        else if(a=="--self-test-render-ui"){renderUiTest=true;g_headless=true;}
        else if(a=="--self-test-studio-render"){studioRenderTest=true;g_headless=true;}
        else if(a=="--path-trace")requestedPathTracing=true;
        else if(a=="--path-samples"&&i+1<argc)requestedPathSamples=std::clamp(_wtoi(argv[++i]),1,4096);
        else if(a=="--self-test-authoring-ui"){authoringUiTest=true;g_headless=true;}
        else if(a=="--self-test-bom-ui"){bomUiTest=true;g_headless=true;}
        else if(a=="--self-test-local-models-ui"){localModelsUiTest=true;g_headless=true;}
        else if(a=="--self-test-viewport-dock-ui"){viewportDockUiTest=true;g_headless=true;}
        else if(a=="--self-test-drawing-viewport-ui"){drawingViewportUiTest=true;g_headless=true;}
        else if(a=="--drawing-workspace-test"){drawingWorkspaceUiTest=true;g_headless=true;}
        else if(a=="--self-test-measurement-ui"){measurementUiTest=true;g_headless=true;}
        else if(a=="--self-test-interaction-ui"){interactionUiTest=true;g_headless=true;}
        else if(a=="--self-test-connectors-ui"){connectorsUiTest=true;g_headless=true;}
        else if(a=="--self-test-action-icons-ui"){actionIconsUiTest=true;g_headless=true;}
        else if(a=="--self-test-engineering-ui"){engineeringUiTest=true;g_headless=true;}
        else if(a=="--self-test-sheet-showcase-ui"){sheetShowcaseTest=true;g_headless=true;}
        else if(a=="--self-test-context-ui"){contextUiTest=true;g_headless=true;}
        else if(a=="--self-test-suspension-ui"){suspensionUiTest=true;g_headless=true;}
        else if(a=="--connect-chatgpt")connectChatGpt=true;
        else if(a=="--motion-driver"&&i+1<argc)motionDriverName=utf8(argv[++i]);
        else if(a=="--motion-rpm"&&i+1<argc)motionSpeed=utf8(argv[++i]);
        else if(a=="--motion-time"&&i+1<argc)requestedMotionTime=_wtof(argv[++i]);
        else if(a=="--motion-play")motionPlay=true;
        else if(a=="--self-test-motion-ui"){motionUiTest=true;g_headless=true;}
        else if(a=="--self-test-navigation-ui"){navigationUiTest=true;g_headless=true;}
        else if(a=="--self-test-projection-ui"){projectionUiTest=true;g_headless=true;}
        else if(a=="--self-test-frame-tools-ui"){frameToolsUiTest=true;g_headless=true;}
        else if(a=="--showcase-view"&&i+1<argc)showcaseView=utf8(argv[++i]);
        else if(a=="--showcase-progress"&&i+1<argc)showcaseProgress=_wtof(argv[++i]);
        else if(a=="--spotlight"&&i+1<argc)spotlightName=utf8(argv[++i]);
        else if(a=="--suspension-feature"&&i+1<argc)suspensionFeatureName=utf8(argv[++i]);
        else if(a=="--suspension-state"&&i+3<argc){suspensionValues={utf8(argv[++i]),utf8(argv[++i]),utf8(argv[++i])};}
        else if(a=="--sketch"&&i+1<argc)requestedSketch=utf8(argv[++i]);
        else if(a=="--live-endpoint-file"&&i+1<argc)endpointFile=utf8(argv[++i]);
        else if(a=="--exit-after-ms"&&i+1<argc)exitAfterMs=(unsigned)_wtoi(argv[++i]);
        else if(a=="--settings-file"&&i+1<argc)g_settingsOverride=std::filesystem::path(argv[++i]);
        else if(a=="--self-test-dialogs")dialogTest=true;
        else if(a=="--self-test-bend-workspace")bendTest=true;
        else if(a=="--self-test-bend-export")bendExportTest=true;
        else if(a=="--self-test-source-tube")sourceTest=true;
        else if(a=="--import-step"&&i+1<argc)importPath=utf8(argv[++i]);
        else if(a=="--fold-progress"&&i+1<argc)requestedFold=_wtof(argv[++i]);
        else if(a=="--focus-opening"&&i+1<argc)requestedOpening=_wtoi(argv[++i])-1;
        else if(a=="--self-test-assembly")assemblyTest=true;
        else if(a=="--size"&&i+2<argc){sw=_wtoi(argv[++i]);sh=_wtoi(argv[++i]);}
        else if(a=="--load"&&i+1<argc)loadPath=utf8(argv[++i]);
        else if(a=="--script"&&i+1<argc)scriptPath=utf8(argv[++i]);}
    LocalFree(argv);
    if(startupUiTest)return arkstudio::startupUiSelfTest(shotPath);
    arkstudio::StartupSplash startupSplash(g_headless);
    if(!g_headless||!g_settingsOverride.empty())loadUiSettings();if(requestedPathTracing)g_pathTracing=true;
    if(!g_headless)try{g_authStatus=chatGptAuthStatus();}catch(...){g_status="ChatGPT account status unavailable";}
    winW=std::max(sw,1180);winH=std::max(sh,800);layout();

    // ---- window ----
    WNDCLASSA wc{}; wc.style=CS_OWNDC; wc.hInstance=hi;
    wc.lpfnWndProc=wndProc; wc.lpszClassName="ARKVIEW";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    // Load embedded alpha icons at the shell's large/small metrics.
    HICON largeIcon=(HICON)LoadImageW(hi,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED);
    HICON smallIcon=(HICON)LoadImageW(hi,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED);
    if(!largeIcon||!smallIcon)return 388;
    wc.hIcon=largeIcon;
    RegisterClassA(&wc);
    RECT rc{0,0,winW,winH};
    AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowA("ARKVIEW",(std::string("ARK ")+app_iteration_code()).c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                            CW_USEDEFAULT,CW_USEDEFAULT,rc.right-rc.left,rc.bottom-rc.top,
                            nullptr,nullptr,hi,nullptr);
    if(!hwnd)return 389;
    SendMessageW(hwnd,WM_SETICON,ICON_BIG,(LPARAM)largeIcon);
    SendMessageW(hwnd,WM_SETICON,ICON_SMALL,(LPARAM)smallIcon);
    g_window=hwnd;DragAcceptFiles(hwnd,TRUE);BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof dark);
    g_commandSearch=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,winW-510,12,260,31,hwnd,(HMENU)498,hi,nullptr);
    g_toolSearch=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,hwnd,(HMENU)497,hi,nullptr);if(!g_toolSearch)return 314;g_toolSearchProc=(WNDPROC)SetWindowLongPtrW(g_toolSearch,GWLP_WNDPROC,(LONG_PTR)toolSearchProc);SendMessage(g_toolSearch,EM_SETLIMITTEXT,160,0);SendMessage(g_toolSearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(7,7));
    if(g_commandSearch){HFONT searchFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");SendMessage(g_commandSearch,WM_SETFONT,(WPARAM)searchFont,TRUE);SendMessage(g_toolSearch,WM_SETFONT,(WPARAM)searchFont,TRUE);SendMessageW(g_toolSearch,0x1501,TRUE,(LPARAM)L"Search tools, operations, evidence...");SendMessage(g_commandSearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));g_commandSearchProc=(WNDPROC)SetWindowLongPtrW(g_commandSearch,GWLP_WNDPROC,(LONG_PTR)commandSearchProc);}
    HDC hdc=GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize=sizeof pfd; pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=32; pfd.cDepthBits=24;
    pfd.iLayerType=PFD_MAIN_PLANE;
    int pf=ChoosePixelFormat(hdc,&pfd);
    SetPixelFormat(hdc,pf,&pfd);
    HGLRC ctx=wglCreateContext(hdc);
    wglMakeCurrent(hdc,ctx);
    wglChoosePixelFormatARB_=(WGLCHOOSEPFARB)(void*)wglGetProcAddress("wglChoosePixelFormatARB");
    wglCreateContextAttribsARB_=(WGLCREATECTXATTRIBS)(void*)wglGetProcAddress("wglCreateContextAttribsARB");

#define LOAD(T,N) N=(T)(void*)wglGetProcAddress(#N);
    GLFUNCS(LOAD)
#undef LOAD

    // upgrade to 3.3 core + 8xMSAA pixel format ---------------------------------
    if(wglCreateContextAttribsARB_&&wglChoosePixelFormatARB_){
        int pfAttr[]={ 0x2001,1, 0x2010,1, 0x2014,1, 0x2022,0x202B,
                       0x2027,24, 0x2041,1, 0x204A,8, 0 };
        int cand=0; UINT num=0;
        if(wglChoosePixelFormatARB_(hdc,pfAttr,nullptr,1,&cand,&num)&&num){
            DescribePixelFormat(hdc,cand,sizeof pfd,&pfd);
            if(SetPixelFormat(hdc,cand,&pfd)){
                wglMakeCurrent(hdc,nullptr);
                wglDeleteContext(ctx);
                ctx=wglCreateContext(hdc);
                wglMakeCurrent(hdc,ctx);
            }
        }
        int ca[]={0x2095,3, 0x2096,3, 0x2094,0x0001, 0};
        HGLRC modern=wglCreateContextAttribsARB_(hdc,nullptr,ca);
        if(modern){
            wglMakeCurrent(hdc,nullptr);
            wglDeleteContext(ctx);
            ctx=modern;
            wglMakeCurrent(hdc,ctx);
        }
    }
#define LOAD(T,N) N=(T)(void*)wglGetProcAddress(#N);
    GLFUNCS(LOAD)
#undef LOAD
    using SwapInterval=BOOL (WINAPI*)(int);
    auto swapInterval=(SwapInterval)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if(swapInterval)swapInterval(g_headless?0:1);
    ShowWindow(hwnd,SW_HIDE);

    // ---- resources ----
    pBg=mkProg(VS_BG,FS_BG);
    pBusy=mkProg(VS_BG,FS_BUSY);
    pSolid=mkProg(VS_SOLID,FS_SOLID);
    pCube=mkProg(VS_SOLID,FS_CUBE);
    pGrid=mkProg(VS_LINE,FS_LINE);
    pShadow=mkProg(VS_SHADOW,FS_SHADOW);
    pUI=mkProg(VS_UI,FS_UI);
    pText=mkProg(VS_TEXT,FS_TEXT);pIcons=mkProg(VS_UI,FS_ICON);pImage=mkProg(VS_UI,FS_IMAGE);
    pSketch=mkProg(VS_LINE,"#version 330 core\nout vec4 o;uniform vec4 tint;void main(){o=tint;}");
    pCanvas=mkProg(
        "#version 330 core\nlayout(location=0) in vec3 P;layout(location=1) in vec2 T;uniform mat4 vp,model;out vec2 vuv;void main(){vuv=T;gl_Position=vp*model*vec4(P,1.);}",
        "#version 330 core\nin vec2 vuv;uniform sampler2D img;uniform float opacity,live;out vec4 o;void main(){float check=mod(floor(vuv.x*8.)+floor(vuv.y*8.),2.);vec3 paper=mix(vec3(.62,.58,.52),vec3(.78,.74,.68),check);vec4 tex=texture(img,vuv);o=vec4(mix(paper,tex.rgb,live),opacity*mix(.55,tex.a,live));}");
    pGeom=mkProg(VS_GEOM,FS_GEOM);pAo=mkProg(VS_BG,FS_AO);pCompose=mkProg(VS_BG,FS_COMPOSE);pEdge=mkProg(VS_EDGE,FS_EDGE);
    pFeatureEdge=mkProg(VS_FEATURE_EDGE,FS_FEATURE_EDGE);pContour=mkProg(VS_BG,FS_CONTOUR);
    for(GLuint program:{pBg,pBusy,pSolid,pCube,pGrid,pShadow,pUI,pText,pIcons,pImage,pSketch,pCanvas,pGeom,pAo,pCompose,pEdge,pFeatureEdge,pContour}){
        GLint linked=0;if(program)glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){
            startupSplash.finish();
            if(!g_headless)MessageBoxA(hwnd,"ARK could not initialize the graphics programs. Check the graphics driver; no drawing was loaded or modified.","ARK graphics initialization",MB_OK|MB_ICONERROR);
            std::fprintf(stderr,"ARK mandatory graphics program failed to link.\n");return 22;
        }
    }
    guView=glGetUniformLocation(pGeom,"view");guProj=glGetUniformLocation(pGeom,"proj");guModel=glGetUniformLocation(pGeom,"model");guHard=glGetUniformLocation(pGeom,"hardSurface");
    auProj=glGetUniformLocation(pAo,"proj");auFocal=glGetUniformLocation(pAo,"focal");auOrtho=glGetUniformLocation(pAo,"ortho");auRadius=glGetUniformLocation(pAo,"radius");auBias=glGetUniformLocation(pAo,"bias");
    cuOrigin=glGetUniformLocation(pCompose,"origin");euVp=glGetUniformLocation(pEdge,"vp");euModel=glGetUniformLocation(pEdge,"model");
    glGenVertexArrays(1,&sketchVao);glGenBuffers(1,&sketchVbo);glBindVertexArray(sketchVao);glBindBuffer(GL_ARRAY_BUFFER,sketchVbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
    glGenVertexArrays(1,&canvasVao);glGenBuffers(1,&canvasVbo);glBindVertexArray(canvasVao);glBindBuffer(GL_ARRAY_BUFFER,canvasVbo);
    const float canvasQuad[]={0,0,0,0,0, 1,0,0,1,0, 1,1,0,1,1, 0,0,0,0,0, 1,1,0,1,1, 0,1,0,0,1};
    glBufferData(GL_ARRAY_BUFFER,sizeof canvasQuad,canvasQuad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,20,nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,20,(void*)12);
    glGenTextures(1,&g_canvasMissing);glBindTexture(GL_TEXTURE_2D,g_canvasMissing);
    const unsigned char missingPx[]={180,170,155,255, 210,200,185,255, 210,200,185,255, 180,170,155,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,missingPx);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    suVp=glGetUniformLocation(pSolid,"vp");      suModel=glGetUniformLocation(pSolid,"model");
    suHard=glGetUniformLocation(pSolid,"hardSurface");
    suOutline=glGetUniformLocation(pSolid,"outline"); suColor=glGetUniformLocation(pSolid,"color");
    suEye=glGetUniformLocation(pSolid,"eye");    suSel=glGetUniformLocation(pSolid,"sel");
    uuGlyph=glGetUniformLocation(pUI,"glyph");
    uuVp=glGetUniformLocation(pUI,"vp");         uuRc=glGetUniformLocation(pUI,"rc");
    uuRad=glGetUniformLocation(pUI,"radius");    uuFill=glGetUniformLocation(pUI,"fill");
    uuBorder=glGetUniformLocation(pUI,"borderCol"); uuBW=glGetUniformLocation(pUI,"borderW");

    buildFontAtlas();

    glGenVertexArrays(1,&quadVao);
    { GLuint qb; glGenBuffers(1,&qb);
      glBindVertexArray(quadVao); glBindBuffer(GL_ARRAY_BUFFER,qb);
      float q[]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
      glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_STATIC_DRAW);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,nullptr);
    }
    glGenVertexArrays(1,&gridVao);glGenBuffers(1,&gridVbo);glBindVertexArray(gridVao);glBindBuffer(GL_ARRAY_BUFFER,gridVbo);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,16,nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,16,(void*)(3*sizeof(float)));
    glGenVertexArrays(1,&textVao);
    glGenBuffers(1,&textVbo);
    glGenVertexArrays(1,&gCubeVao);glGenBuffers(1,&gCubeVbo);
    {std::vector<float> cube;g_cubeTriangles.clear();const double s=.85,a=.72;
        auto with=[](Vec3 v,int axis,double value){if(axis==0)v.x=value;else if(axis==1)v.y=value;else v.z=value;return v;};
        auto triangle=[&](Vec3 p,Vec3 q,Vec3 r,Vec3 normal,int face){normal=normal.normalized();
            g_cubeTriangles.push_back({p,q,r,normal,face});for(Vec3 v:{p,q,r})cube.insert(cube.end(),{(float)v.x,(float)v.y,(float)v.z,(float)normal.x,(float)normal.y,(float)normal.z});};
        for(int face=0;face<6;++face){int axis=face/2,sign=face%2?-1:1,tangent=(axis+1)%3,bitangent=(axis+2)%3;
            Vec3 normal=with({},axis,(double)sign);auto point=[&](double u,double v){return with(with(with({},axis,sign*s),tangent,u),bitangent,v);};
            Vec3 p=point(-a,-a),q=point(a,-a),r=point(a,a),v=point(-a,a);triangle(p,q,r,normal,face);triangle(p,r,v,normal,face);
        }
        for(int axis=0;axis<3;++axis)for(int other=axis+1;other<3;++other){int edgeAxis=3-axis-other;
            for(int sa:{-1,1})for(int sb:{-1,1}){auto point=[&](double first,double second,double along){return with(with(with({},axis,first),other,second),edgeAxis,along);};
                Vec3 p=point(sa*s,sb*a,-a),q=point(sa*s,sb*a,a),r=point(sa*a,sb*s,a),v=point(sa*a,sb*s,-a);
                Vec3 normal=with(with({},axis,(double)sa),other,(double)sb);triangle(p,q,r,normal,-1);triangle(p,r,v,normal,-1);
            }}
        for(int sx:{-1,1})for(int sy:{-1,1})for(int sz:{-1,1}){
            Vec3 p{sx*s,sy*a,sz*a},q{sx*a,sy*s,sz*a},r{sx*a,sy*a,sz*s};triangle(p,q,r,{(double)sx,(double)sy,(double)sz},-1);
        }
        glBindVertexArray(gCubeVao);glBindBuffer(GL_ARRAY_BUFFER,gCubeVbo);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(cube.size()*sizeof(float)),cube.data(),GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,nullptr);glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);}
    glGenVertexArrays(1,&gCompassVao);glGenBuffers(1,&gCompassVbo);
    {const float axes[]={0,0,0, 1,0,0, 0,0,0, 0,1,0, 0,0,0, 0,0,1};
        glBindVertexArray(gCompassVao);glBindBuffer(GL_ARRAY_BUFFER,gCompassVbo);glBufferData(GL_ARRAY_BUFFER,sizeof axes,axes,GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);}

    seedDocument();g_uiReady=true;
    if(!g_headless){ShowWindow(hwnd,SW_SHOW);drawFrame(hdc);startupSplash.finish();acknowledge_update_startup();}
    refreshPricing();
    if(!loadPath.empty()){arkjson::Value a=arkjson::Value::makeObj();a.set("path",arkjson::Value::makeStr(loadPath));runTool("doc_load",a);g_path=loadPath;if(!g_error){g_savedRevision=g_session.revision;g_foldProgress=selectedSourceTube()?1.:0.;}frameScene();}
    if(!scriptPath.empty()){
        std::ifstream input(scriptPath);std::string line;
        if(!input)return 2;
        while(std::getline(input,line)){arkjson::Value c;if(!arkjson::parse(line,c)||!c.find("tool")||!c.find("arguments"))return 2;
            runTool(c.find("tool")->asStr(),*c.find("arguments"));if(g_error)return 3;}
        frameScene();
    }
    if(!importPath.empty()){importSourceStep(importPath);if(g_error)return 80;}
    if(!motionDriverName.empty())try{g_motionDriver=motionInstanceId(motionDriverName);g_motionRpmExpression=motionSpeed.empty()?(std::any_of(g_doc.params.begin(),g_doc.params.end(),[](const Parameter& p){return p.name=="cadence_rpm";})?"cadence_rpm":"60"):motionSpeed;g_motionTime=requestedMotionTime;g_motionActive=true;g_motionPlaying=motionPlay;updateMotionDisplay();frameScene();g_toolModule=1;}catch(const std::exception& error){fprintf(stderr,"Mechanical motion: %s\n",error.what());return 301;}
    if(!showcaseView.empty()||showcaseProgress>=0){
        try{
            if(showcaseView=="formed"){if(!visibleSheetIds().empty())allSheetPose(false);else frameScene();g_toolModule=0;}
            else if(showcaseView=="flat"){allSheetPose(true);g_toolModule=0;}
            else if(showcaseView=="stock"){
                auto selected=std::find_if(g_bodyIds.begin(),g_bodyIds.end(),[&](u64 id){auto* feature=g_doc.find(id);return feature&&(feature->type==FeatureType::ConstructionFrame||feature->type==FeatureType::Tube);});
                if(selected==g_bodyIds.end())throw std::runtime_error("Showcase stock needs a visible frame or tube");
                selBody=(int)std::distance(g_bodyIds.begin(),selected);toggleStockPreview();g_toolModule=0;
            }else if(!showcaseView.empty())throw std::runtime_error("Showcase view must be formed, flat or stock");
            if(showcaseProgress>=0){
                if(showcaseProgress>1)throw std::runtime_error("Showcase progress must lie in 0..1");
                if(g_stockMode)toggleStockPreview();if(g_allSheetsFlat)allSheetPose(false);
                auto ids=visibleSheetIds();if(ids.empty())throw std::runtime_error("Showcase progress needs authored visible sheets");
                double stage=showcaseProgress*ids.size();for(size_t index=0;index<ids.size();++index){
                    auto* feature=g_doc.find(ids[index]);double progress=clamp(stage-(double)index,0.,1.);
                    if(feature&&feature->type==FeatureType::SourceSheet)progress=progress>.5?1.:0.;
                    sheetPreviewSet(ids[index],progress);
                }
                g_allSheetsPlaying=false;g_allSheetsStage=stage;g_toolModule=0;frameScene();g_status="Unbend preview at "+std::to_string((int)std::lround(showcaseProgress*100))+"% / document unchanged";
            }
        }catch(const std::exception& error){fprintf(stderr,"Showcase view: %s\n",error.what());return 242;}
    }
    if(!suspensionFeatureName.empty()||!suspensionValues.empty()||!spotlightName.empty()){
        try{
            auto findOccurrence=[&](const std::string& name,bool suspensionOnly){int match=-1;
                for(size_t index=0;index<g_bodyIds.size();++index){Feature* feature=g_doc.find(g_bodyIds[index]);if(!feature)continue;
                    if(suspensionOnly&&feature->type!=FeatureType::Suspension)continue;
                    if(!name.empty()&&name!=g_instanceNames[index]&&name!=feature->name&&name!=std::to_string(g_instanceIds[index])&&name!=std::to_string(g_bodyIds[index]))continue;
                    if(match>=0)throw std::runtime_error("Occurrence name is ambiguous; use a unique node or instance name");match=(int)index;
                }if(match<0)throw std::runtime_error("Requested visible occurrence was not found");return match;};
            if(!suspensionFeatureName.empty()||!suspensionValues.empty()){
                selBody=findOccurrence(suspensionFeatureName,true);Feature* suspension=selectedSuspension();
                if(suspensionValues.size()==3){auto number=[](const std::string& text){size_t used=0;double value=std::stod(text,&used);if(used!=text.size()||!std::isfinite(value))throw std::runtime_error("Suspension state must contain finite jounce, rack and spin values");return value;};
                    SuspensionState state{number(suspensionValues[0]),number(suspensionValues[1]),number(suspensionValues[2])};updateSuspensionPreview(suspension->id,state);}
                g_toolModule=1;g_suspSpinPlaying=false;frameScene();
            }
            if(!spotlightName.empty()){selBody=findOccurrence(spotlightName,false);spotlightBody();}
        }catch(const std::exception& error){fprintf(stderr,"Suspension/Spotlight view: %s\n",error.what());return 268;}
    }
    if(suspensionUiTest){
        g_compactNavigation=true;
        using V=arkjson::Value;V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);
        if(g_error)return 270;g_dialogSmoke=3;interactiveAction(215);g_dialogSmoke=0;
        if(g_error||g_doc.features.size()!=1||g_doc.features[0].type!=FeatureType::Suspension||g_bodyIds.size()!=1)return 271;
        u64 id=g_bodyIds.front(),revision=g_session.revision;selBody=0;auto& cache=g_suspViews[id];
        if(cache.preview.parts.size()<12||cache.gpu.size()!=cache.preview.parts.size()||cache.preview.mesh.triCount()<1000)return 272;
        try{setSuspensionControl(0,.8);setSuspensionControl(1,.75);setSuspensionControl(2,.25);}catch(...){return 273;}
        auto* closure=cache.preview.report.find("closure_residual_mm_max");
        if(!closure||closure->t!=V::Num||closure->num>1e-5||g_session.revision!=revision||std::abs(cache.state.wheelSpinDeg-90.)>1e-8)return 274;
        target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;HDC dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);
        for(int control=0;control<3;++control)if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==380+control;}))return 275;
        auto slider=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==380;});
        auto nav=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==176;});
        if(slider==g_zones.end()||nav==g_zones.end()||slider->y+slider->h>=nav->y)return 276;
        interactiveAction(383);double spinBefore=cache.state.wheelSpinDeg;advanceSuspension(.2);interactiveAction(383);
        if(g_error||g_suspSpinPlaying||g_session.revision!=revision||cache.state.wheelSpinDeg<=spinBefore)return 277;
        g_status="Suspension UI checks passed / colored named parts / jounce / steering rack / wheel spin / unchanged revision";
    }
    if(contextUiTest){
        using V=arkjson::Value;V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);if(g_error)return 251;
        V batch;if(!arkjson::parse(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Front block","p":{"op":"box","width":"40","depth":"30","height":"20"}},{"op":"feature.add","type":"native_solid","name":"Rear block","p":{"op":"box","width":"24","depth":"24","height":"18"}}]})",batch))return 252;
        runTool("doc_apply",batch);if(g_error||g_bodyIds.size()!=2)return 253;u64 revision=g_session.revision;
        target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;HDC dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);
        auto tree=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==2000;});if(tree==g_zones.end())return 254;
        int body=-1;ContextTarget targetKind=contextTargetAt((int)(tree->x+tree->w*.5f),(int)(tree->y+tree->h*.5f),body);
        auto hasAction=[](const std::vector<ContextEntry>& entries,int action){return std::any_of(entries.begin(),entries.end(),[&](const ContextEntry& entry){return entry.action==action&&entry.enabled;});};
        if(targetKind!=ContextTarget::Body||body!=0||!hasAction(contextEntries(targetKind,body),342)||!hasAction(contextEntries(targetKind,body),187))return 255;
        {auto entries=contextEntries(targetKind,body);HMENU popup=createContextPopup(entries);if(!popup)return 280;
            for(size_t i=0;i<entries.size();++i){MENUITEMINFOA item{sizeof item};item.fMask=MIIM_FTYPE|MIIM_DATA|MIIM_ID;if(!GetMenuItemInfoA(popup,(UINT)i,TRUE,&item)||!(item.fType&MFT_OWNERDRAW)||item.dwItemData!=(ULONG_PTR)&entries[i]||item.wID!=(UINT)entries[i].action){DestroyMenu(popup);return 281;}}
            DestroyMenu(popup);if(!captureContextMenu(entries,shotPath+".context-menu.png"))return 282;
            auto axes=compassDirections();for(const auto& axis:axes)if(!std::isfinite(axis.x)||!std::isfinite(axis.y)||std::abs(axis.lensq()-1)>1e-8)return 283;
        }
        interactiveAction(340);if(g_session.revision!=revision||displayInstance(0)||!hasAction(contextEntries(ContextTarget::Body,0),341))return 256;
        selBody=0;interactiveAction(342);if(g_session.revision!=revision||!displayInstance(0)||displayInstance(1)||!g_spotlightActive)return 257;
        interactiveAction(343);if(g_session.revision!=revision||displayInstance(0)||!displayInstance(1)||g_spotlightActive)return 258;
        interactiveAction(344);if(g_session.revision!=revision||!displayInstance(0)||!displayInstance(1))return 259;
        g_picked.clear();refreshSelectionLines();bool edgePicked=false,facePicked=false;
        for(size_t i=0;i<g_partPicks.size()&&!edgePicked;++i)for(size_t edge=0;edge<g_partPicks[i].edges.size()&&!edgePicked;++edge){
            if(edge>=g_partPicks[i].edgeNames.size()||g_partPicks[i].edgeNames[edge].empty()||g_partPicks[i].edges[edge].size()<2)continue;
            const auto& points=g_partPicks[i].edges[edge];Vec3 midpoint=(points.front()+points.back())*.5;float sx,sy;
            if(projectScreen(midpoint,sx,sy)&&pickNamedEdgeScreen((int)sx,(int)sy)){edgePicked=!g_picked.empty()&&g_picked.back().kind=="edge";g_picked.clear();refreshSelectionLines();}
        }
        for(size_t bodyIndex=0;bodyIndex<g_bodyIds.size()&&!facePicked;++bodyIndex){size_t part=g_instancePart[bodyIndex];const auto& pick=g_partPicks[part];const Mesh& mesh=g_cpuMeshes[part];
            for(size_t face=0;face<pick.faces.size()&&!facePicked;++face){if(face>=pick.faceNames.size()||pick.faceNames[face].empty()||pick.faces[face].second<=0)continue;
                size_t triangle=(size_t)pick.faces[face].first*3;if(triangle+2>=mesh.idx.size())continue;Vec3 center{};
                for(size_t corner=0;corner<3;++corner){size_t vertex=mesh.idx[triangle+corner];center=center+Vec3{mesh.pos[vertex*3],mesh.pos[vertex*3+1],mesh.pos[vertex*3+2]};}
                center=g_instanceWorld[bodyIndex].transformPoint(center*(1./3.));float sx,sy;g_pickKind=1;
                if(projectScreen(center,sx,sy)){pickReference((int)sx,(int)sy);facePicked=!g_picked.empty()&&g_picked.back().kind=="face";g_picked.clear();refreshSelectionLines();}
            }
        }
        g_pickKind=0;if(!edgePicked||!facePicked||g_session.revision!=revision)return 260;
        V sheetBatch;if(!arkjson::parse(R"({"commands":[{"op":"sheet.create","name":"Preview sheet","p":{"width":60,"thickness":2,"k_factor":0.42},"base":{"id":"base","length":80}},{"op":"sheet.flange.add","id":3,"flange":{"id":"back","parent":"base","edge":"u_max","length":24,"inner_radius":3,"angle":90}}]})",sheetBatch))return 261;
        runTool("doc_apply",sheetBatch);if(g_error)return 262;auto sheetBody=std::find(g_bodyIds.begin(),g_bodyIds.end(),(u64)3);if(sheetBody==g_bodyIds.end())return 263;
        selBody=(int)std::distance(g_bodyIds.begin(),sheetBody);target=tTarget;dist=tDist;g_compactNavigation=true;dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);
        auto playback=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==53;});
        auto nav=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==176;});
        if(playback==g_zones.end()||nav==g_zones.end())return 264;
        auto intersects=[](const Zone& a,const Zone& b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;};
        if(intersects(*playback,*nav))return 265;
        for(auto size:std::vector<std::pair<int,int>>{{1180,840},{1600,1000},{2923,1080}}){int vw=size.first-std::clamp((int)(size.first*.21),280,352),vh=size.second-viewY-32;
            float playTop=(float)viewY+vh-106,playBottom=(float)viewY+vh-63,navTop=(float)viewY+vh-49;
            float sliderEnd=(float)viewX+136+244+std::max(80.f,(float)vw-408.f);
            if(playBottom>=navTop||playTop<viewY||sliderEnd>viewX+vw)return 266;}
        V placed;if(!arkjson::parse(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Placed preview sheet","inputs":[3],"p":{"op":"transform","tx":"140","ty":"35","rx":"37","rz":"24"}}]})",placed))return 284;
        runTool("doc_apply",placed);if(g_error)return 285;auto installed=std::find(g_bodyIds.begin(),g_bodyIds.end(),(u64)4);if(installed==g_bodyIds.end())return 286;
        selBody=(int)std::distance(g_bodyIds.begin(),installed);g_explorerFeature=0;if(!selectedSheet()||selectedSheet()->id!=3||!isSheetFeature(4)||visibleSheetIds()!=std::vector<u64>{3})return 287;
        auto formedBounds=g_reports[g_instancePart[(size_t)selBody]].bounds;revision=g_session.revision;
        sheetPreviewSet(3,.5);{const auto& display=g_cpuMeshes[g_instancePart[(size_t)selBody]];if(display.pos.empty()||display.nrm.size()!=display.pos.size()||g_session.revision!=revision)return 299;}sheetPreviewSet(3,1.);
        allSheetPose(true);if(g_session.revision!=revision||!g_allSheetsFlat||!g_sceneBounds.valid())return 288;
        allSheetPose(false);installed=std::find(g_bodyIds.begin(),g_bodyIds.end(),(u64)4);selBody=(int)std::distance(g_bodyIds.begin(),installed);
        auto restoredBounds=g_reports[g_instancePart[(size_t)selBody]].bounds;if((restoredBounds.mn-formedBounds.mn).len()>1e-6||(restoredBounds.mx-formedBounds.mx).len()>1e-6||g_session.revision!=revision)return 289;
        g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"back","u_max","10","3","-1","","",""});interactiveAction(394);
        if(g_error||!g_doc.find(3)->nativeSpec.find("hems")||g_doc.find(3)->nativeSpec.find("hems")->arr.size()!=1){fprintf(stderr,"Hem UI form failed: %s\n",g_status.c_str());return 290;}
        g_dialogOverrideQueue.push_back({"base","40","30","12","6","4"});interactiveAction(397);g_dialogSmoke=0;
        if(g_error||!g_dialogOverrideQueue.empty()||!sheetHasStamp(*g_doc.find(3)))return 291;
        {int originalWidth=winW,originalHeight=winH;revision=g_session.revision;
            auto resize=[&](int width,int height){RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,(DWORD)GetWindowLongPtr(g_window,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtr(g_window,GWL_EXSTYLE));SetWindowPos(g_window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=height;layout();};
            auto screenshot=[&](const std::string& suffix){target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;HDC testDc=GetDC(g_window);drawFrame(testDc);glFinish();std::vector<unsigned char> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());ReleaseDC(g_window,testDc);Image shot;shot.w=winW;shot.h=winH;shot.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(shot.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);
                unsigned red=0,blue=0,bright=0;for(int y=12;y<42;++y)for(int x=18;x<136;++x){size_t p=((size_t)y*winW+x)*3;if(shot.px[p]>90||shot.px[p+2]>90){red+=shot.px[p];blue+=shot.px[p+2];++bright;}}
                if(bright<100||blue<=red){std::fprintf(stderr,"Wordmark must retain its blue RGBA pixels after WIC scaling\n");return false;}
                return writePNG((shotPath+suffix).c_str(),shot);};
            g_toolModule=0;g_sidebarTab=0;g_assistantDockOpen=false;g_compactNavigation=false;
            for(auto size:std::vector<std::pair<int,int>>{{1280,800},{1600,1000}}){resize(size.first,size.second);frameScene();
                if(!screenshot(".workspace-"+std::to_string(size.first)+".png"))return 292;
                for(const auto& zone:g_zones)if(zone.x<0||zone.y<0||zone.x+zone.w>winW+1||zone.y+zone.h>winH+1)return 293;
                for(int action:{394,397,400,410,411,412,450})if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;}))return 294;
                uiAction(411);if(!screenshot(".layers-"+std::to_string(size.first)+".png"))return 303;
                bool wasHidden=!g_hiddenBodies.empty()&&g_hiddenBodies[0];uiAction(22000);if(g_hiddenBodies[0]==wasHidden||g_session.revision!=revision)return 304;uiAction(22000);uiAction(410);
                uiAction(401);g_inspectorScroll=96;if(!screenshot(".properties-"+std::to_string(size.first)+".png"))return 295;
                for(const auto& zone:g_zones)if(zone.x<viewX&&zone.y>=viewY+42&&zone.y+zone.h>winH-32)return 296;
                uiAction(401);uiAction(400);if(viewH!=winH-viewY-288||!screenshot(".assistant-"+std::to_string(size.first)+".png"))return 297;uiAction(400);
                if(g_session.revision!=revision)return 298;
            }resize(originalWidth,originalHeight);g_inspectorScroll=0;
        }
        auto objectCapture=[&](const std::string& suffix){target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;HDC targetDc=GetDC(g_window);drawFrame(targetDc);glFinish();glReadBuffer(GL_BACK);
            std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());ReleaseDC(g_window,targetDc);Image shot;shot.w=winW;shot.h=winH;shot.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(shot.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);return writePNG((shotPath+suffix).c_str(),shot);};
        // Context attachment preserves feature/occurrence identity until an explicit send.
        auto freshTargetDocument=[&]{V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);return !g_error;};
        auto parseTarget=[](const char* text){V value;arkjson::parse(text,value);return value;};
        if(!freshTargetDocument())return 645;runTool("doc_apply",parseTarget(R"({"commands":[{"op":"feature.add","type":"box","name":"Target definition","p":{"dx":"20","dy":"15","dz":"10"}},{"op":"assembly.add","name":"Target copies","feature":1,"pattern":{"type":"linear","count":3,"dx":"40"}},{"op":"assembly.add","name":"Other occurrence","feature":1,"p":{"ty":"50"}}]})"));
        if(g_error||g_bodyIds.size()!=4)return 600;int copyBody=-1,otherBody=-1;
        for(size_t i=0;i<g_instanceIds.size();++i){if(g_instanceIds[i]==1&&g_instanceCopies[i]==2)copyBody=(int)i;if(g_instanceIds[i]==2)otherBody=(int)i;}
        if(copyBody<0||otherBody<0||!hasAction(contextEntries(ContextTarget::Body,copyBody),565))return 601;
        const auto models=g_teamModels,efforts=g_teamEfforts;auto config=g_assistantConfig;u64 targetRevision=g_session.revision;bool wasMode=g_teamMode;g_teamMode=false;
        selBody=copyBody;interactiveAction(565);
        if(!g_pendingObjectTarget||g_pendingObjectTarget->node!=1||g_pendingObjectTarget->copy!=2||!g_assistantDockOpen||g_chatBusy||g_teamStarted||g_session.revision!=targetRevision||g_teamModels!=models||g_teamEfforts!=efforts||g_assistantConfig.model!=config.model||g_assistantConfig.reasoningEffort!=config.reasoningEffort)return 602;
        selBody=otherBody;std::string attached="Inspect the selected occurrence";if(!attachPendingObjectContext(attached)||attached.find("occurrence:1:2")==std::string::npos||attached.find("occurrence:2:0")!=std::string::npos||g_session.revision!=targetRevision)return 603;
        if(!objectCapture(".object-target-single.png"))return 604;g_teamMode=true;layout();if(!objectCapture(".object-target-team.png"))return 605;
        applyCommand(parseTarget(R"({"op":"param.set","name":"target_revision_probe","expr":"2"})"));attached="Inspect after an unrelated edit";
        if(g_error||!attachPendingObjectContext(attached)||attached.find("\"revision\":"+std::to_string(g_session.revision))==std::string::npos||attached.find("occurrence:1:2")==std::string::npos)return 606;
        interactiveAction(566);attached="Without target";if(g_pendingObjectTarget||!attachPendingObjectContext(attached)||attached!="Without target")return 607;
        selBody=copyBody;interactiveAction(565);applyCommand(parseTarget(R"({"op":"assembly.edit","id":1,"pattern":{"type":"linear","count":2,"dx":"40"}})"));
        if(g_error||!g_pendingObjectTarget||g_pendingObjectTarget->invalidated.empty())return 640;
        uiAction(18);targetRevision=g_session.revision;attached="Copy restored after removal";
        if(g_error||attachPendingObjectContext(attached)||!g_pendingObjectTarget||g_pendingObjectTarget->invalidated.empty()||g_session.revision!=targetRevision)return 641;g_error=false;

        selBody=copyBody;interactiveAction(565);applyCommand(parseTarget(R"({"op":"assembly.remove","id":1})"));targetRevision=g_session.revision;attached="Deleted occurrence";
        if(!attachPendingObjectContext(attached)&&g_pendingObjectTarget&&!g_pendingObjectTarget->invalidated.empty()&&g_session.revision==targetRevision)g_error=false;else return 608;
        selBody=0;interactiveAction(565);if(!freshTargetDocument())return 645;runTool("doc_apply",parseTarget(R"({"commands":[{"op":"feature.add","type":"box","name":"Reused ID","p":{"dx":"10","dy":"10","dz":"10"}}]})"));targetRevision=g_session.revision;attached="Previous drawing";
        if(attachPendingObjectContext(attached)||!g_pendingObjectTarget||g_pendingObjectTarget->invalidated.empty()||g_session.revision!=targetRevision)return 609;g_error=false;
        selBody=0;interactiveAction(565);attached="Definition context";if(!attachPendingObjectContext(attached)||attached.find("feature:1")==std::string::npos||attached.find("occurrence:")!=std::string::npos)return 610;
        applyCommand(parseTarget(R"({"op":"feature.remove","id":1})"));attached="Deleted definition";if(attachPendingObjectContext(attached)||!g_pendingObjectTarget||g_pendingObjectTarget->invalidated.empty())return 611;g_error=false;
        // Current journals preserve ID counters. Legacy snapshot Undo (still supported)
        // restores its counters; exercise actual reuse through that existing path.
        struct HistoryProbeReset {~HistoryProbeReset(){g_session.suppressIterationHistory(false);}} historyProbeReset;
        if(!freshTargetDocument())return 645;g_session.suppressIterationHistory(true);
        applyCommand(parseTarget(R"({"op":"feature.add","type":"box","name":"Undo target","p":{"dx":"12","dy":"12","dz":"12"}})"));selBody=0;interactiveAction(565);
        if(g_error||!g_pendingObjectTarget)return 642;u64 recycledFeature=g_pendingObjectTarget->feature;uiAction(18);
        if(g_error||!g_pendingObjectTarget||g_pendingObjectTarget->invalidated.empty()||g_doc.find(recycledFeature))return 643;
        applyCommand(parseTarget(R"({"op":"feature.add","type":"box","name":"Different body at reused ID","p":{"dx":"25","dy":"25","dz":"25"}})"));targetRevision=g_session.revision;attached="Reused ID after Undo";
        if(g_error||!g_doc.find(recycledFeature)||attachPendingObjectContext(attached)||!g_pendingObjectTarget||g_pendingObjectTarget->invalidated.empty()||g_session.revision!=targetRevision)return 644;g_error=false;g_session.suppressIterationHistory(false);g_teamMode=wasMode;g_assistantDockOpen=false;layout();
        std::ofstream targetReceipt(shotPath+".object-target-test.log");targetReceipt<<"PASS: right-click Ask Agent action; exact patterned occurrence vs definition; no inference or configuration changes on attachment; selection cannot retarget; fresh revision context; removable chip; deleted occurrence/definition and replacement drawing reject without sending; removal latch rejects restored copy and different body reusing an ID after supported legacy snapshot Undo.\n";
        g_toolModule=0;g_status="UI checks passed / native context graphics / transformed sheet origin / hem and emboss forms / unchanged display revision";
    }
    if(motionUiTest){
        using V=arkjson::Value;V load=V::makeObj();load.set("path",V::makeStr("examples/bicycle-transmission.ark"));runTool("doc_load",load);if(g_error)return 310;
        {auto group=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[](const AssemblyNode& node){return node.name=="Bicycle"&&!node.feature;});if(group==g_doc.assembly.end())return 320;int action=30000+(int)std::distance(g_doc.assembly.begin(),group);u64 before=g_session.revision;g_toolModule=1;g_sidebarTab=0;g_treeScroll=0;g_swap=false;
            auto render=[&](){HDC dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);};render();int count=g_explorerCount;uiAction(action);render();if(!g_closedAssemblyGroups.count(group->id)||g_explorerCount>=count||g_session.revision!=before)return 321;uiAction(action);render();if(g_closedAssemblyGroups.count(group->id)||g_explorerCount!=count||g_session.revision!=before)return 322;
        }
        u64 revision=g_session.revision;g_motionDriver=motionInstanceId("Front crank");g_motionRpmExpression="cadence_rpm";g_motionTime=.125;g_motionActive=true;g_motionPlaying=false;g_toolModule=1;updateMotionDisplay();
        auto wheel=[&]()->const V*{auto* rows=g_motionReport.find("instances");if(!rows)return nullptr;for(const auto& row:rows->arr)if(auto* name=row.find("name");name&&name->asStr()=="Rear wheel")return &row;return nullptr;};
        auto* first=wheel();if(!first||!first->find("speed_rpm")||std::abs(first->find("speed_rpm")->num-180)>1e-8||std::abs(first->find("angle_deg")->num-135)>1e-8||g_session.revision!=revision)return 311;
        g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"48","24","60"});uiAction(469);g_dialogSmoke=0;if(g_error||g_session.revision!=revision+1)return 312;
        auto* changed=wheel();if(!changed||std::abs(changed->find("speed_rpm")->num-120)>1e-8||std::abs(changed->find("angle_deg")->num-90)>1e-8)return 313;
        g_motionRpmExpression="60";g_dialogSmoke=3;g_dialogOverrideQueue.push_back({"48","24","75"});uiAction(469);g_dialogSmoke=0;auto* cadenceChanged=wheel();if(g_error||g_motionRpmExpression!="cadence_rpm"||!cadenceChanged||std::abs(cadenceChanged->find("speed_rpm")->num-150)>1e-8||std::abs(g_motionRpm-75)>1e-8)return 318;
        uiAction(18);updateMotionDisplay();if(g_error||std::abs(g_motionRpm-60)>1e-8)return 319;
        uiAction(18);if(g_error||g_doc.getParam("rear_teeth")!=16)return 314;updateMotionDisplay();auto* restored=wheel();if(!restored||std::abs(restored->find("speed_rpm")->num-180)>1e-8)return 315;
        revision=g_session.revision;uiAction(453);advanceMotion(.125);uiAction(453);if(g_motionPlaying||g_session.revision!=revision||std::abs(g_motionTime-.25)>1e-8)return 316;
        frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;HDC testDc=GetDC(g_window);drawFrame(testDc);ReleaseDC(g_window,testDc);
        for(int action:{451,452,453,454,469})if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;}))return 317;
        g_status="Motion UI checks passed / 48T-16T-24T / cadence / wheel rpm / native recompute / undo / read-only playback";
    }
    if(navigationUiTest){
        u64 revision=g_session.revision;int savedW=winW,savedH=winH;bool savedDock=g_assistantDockOpen,savedMode=g_teamMode,savedCompact=g_compactNavigation;unsigned savedCount=g_teamRoleCount;double savedRoll=cameraRoll;g_assistantDockOpen=true;g_teamMode=true;g_teamRoleCount=3;g_compactNavigation=false;g_pickKind=1;g_verifyTeamCardBounds=true;
        auto failure=[&](const std::string& detail){std::ofstream log(shotPath+".navigation-test.log");log<<detail<<"\n";std::fprintf(stderr,"Navigation UI selftest: %s\n",detail.c_str());return 402;};
        auto resize=[&](int width,int height){RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,(DWORD)GetWindowLongPtr(g_window,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtr(g_window,GWL_EXSTYLE));SetWindowPos(g_window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=height;layout();};
        for(auto size:std::vector<std::pair<int,int>>{{1180,800},{1600,1000}}){resize(size.first,size.second);frameScene();target=tTarget;dist=tDist;uiAction(24);yaw=tYaw;pitch=tPitch;cameraRoll=.35;g_swap=false;HDC dc=GetDC(g_window);drawFrame(dc);glFinish();std::string detail;
            if(!navigationUiFits(detail)||!g_teamCardContentFits||viewH<160){ReleaseDC(g_window,dc);return failure(detail+" / "+g_teamCardBoundsFailure);}
            std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image shot;shot.w=winW;shot.h=winH;shot.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(shot.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);ReleaseDC(g_window,dc);
            std::array<unsigned,3> colors{};for(int y=g_compassBounds.top;y<g_compassBounds.bottom;++y)for(int x=g_compassBounds.left;x<g_compassBounds.right;++x){const u8* pixel=shot.px.data()+((size_t)y*winW+x)*3;for(int axis=0;axis<3;++axis)if(pixel[axis]>80&&pixel[axis]>pixel[(axis+1)%3]*1.2&&pixel[axis]>pixel[(axis+2)%3]*1.2)++colors[axis];}
            for(unsigned count:colors)if(count<20)return failure("Cartesian triad lost an RGB axis in the rendered image.");
            if(!writePNG((shotPath+".navigation-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".png").c_str(),shot))return failure("Could not save navigation screenshot.");
        }
        static const int faceViews[]={182,183,23,184,180,181};
        for(int face=0;face<6;++face)for(double roll:{0.,.63}){uiAction(faceViews[face]);double expectedYaw=tYaw,expectedPitch=tPitch;yaw=tYaw;pitch=tPitch;cameraRoll=roll;HDC dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);int x=g_cubeX+g_cubeS/2,y=g_cubeY+g_cubeS/2;auto hit=cubeHitAt(x,y);if(!hit.hit||hit.face!=face)return failure("Face "+std::to_string(face)+" failed picking with roll "+std::to_string(roll));orientViewCube(x,y);if(std::abs(tYaw-expectedYaw)>1e-8||std::abs(tPitch-expectedPitch)>1e-8||!g_cameraOrtho)return failure("Cube face click changed the canonical view mapping.");}
        if(g_session.revision!=revision)return failure("Navigation changed the CAD document revision.");
        resize(savedW,savedH);g_assistantDockOpen=savedDock;g_teamMode=savedMode;g_teamRoleCount=savedCount;g_compactNavigation=savedCompact;g_pickKind=0;g_verifyTeamCardBounds=false;cameraRoll=savedRoll;uiAction(24);yaw=tYaw;pitch=tPitch;frameScene();target=tTarget;dist=tDist;g_status="Navigation UI checks passed / upper-left cube / shaded RGB triad / six faces / camera roll / unchanged document";
    }

    if(actionIconsUiTest){
#include "action_icons_selftest.inl"
    }
    if(connectorsUiTest){
        using V=arkjson::Value;auto originalConfig=g_assistantConfig;auto originalConnectors=g_teamConnectors;auto originalModels=g_teamModels,originalEfforts=g_teamEfforts;auto originalSettings=g_settingsOverride;bool originalTeam=g_teamMode,originalDock=g_assistantDockOpen;u64 revision=g_session.revision,epoch=g_documentEpoch;auto schema=g_doc.materials;
        auto failure=[&](int code,const char* message){std::fprintf(stderr,"Connectors UI %d: %s / %s\n",code,message,g_status.c_str());return code;};
        auto draw=[&]{g_swap=false;drawFrame(hdc);};
        auto click=[&](int id){draw();auto z=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==id;});if(z==g_zones.end())return false;int x=(int)(z->x+z->w/2),y=(int)(z->y+z->h/2);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
        g_teamMode=true;g_assistantDockOpen=true;g_teamStarted=false;g_teamConnectors={};g_teamRoleCount=3;layout();
        g_localModelDiscoveryTestTransport=[](const auto& config,const auto&,const auto*){V v;arkjson::parse(config.endpoint.ends_with("/models")?R"({"data":[{"id":"offline-local"}]})":R"({"capabilities":["completion","tools"]})",v);return v;};
        const char* providers[]={"claude","grok","ollama"};const char* models[]={"offline-claude","offline-grok","offline-local"};
        for(size_t i=0;i<3;++i){auto preset=assistantProviderPreset(providers[i]);std::vector<std::string> values{preset.endpoint,models[i],preset.apiKeyEnv,assistantProtocolName(preset.protocol)};if(preset.protocol==AssistantProtocol::Messages)values.push_back("8192");values.push_back("auto");if(preset.provider=="ollama")values={models[i],"auto"};g_dialogOverrideQueue={{providers[i],"0"},values};g_dialogSmoke=3;g_dialogShot=shotPath+".provider-"+providers[i]+".png";bool clicked=click(44020+(int)i);g_dialogSmoke=0;if(!clicked||g_error||!g_teamConnectors[i]||teamRoleConfig(i).provider!=providers[i]||teamRoleConfig(i).model!=models[i]||teamRoleConfig(i).protocol!=preset.protocol)return failure(500,"Provider card dropdown/configuration dispatch failed");}
        if(g_assistantConfig.model!=originalConfig.model||g_assistantConfig.endpoint!=originalConfig.endpoint||g_assistantConfig.authMode!=originalConfig.authMode||g_session.revision!=revision||g_documentEpoch!=epoch)return failure(501,"Per-role provider changed global connector, drawing, or epoch");
        draw();glFinish();{std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);if(!writePNG((shotPath+".provider-mixed-cards.png").c_str(),image))return failure(505,"Mixed provider cards capture failed");}
        g_settingsOverride=std::filesystem::temp_directory_path()/("ark-connectors-ui-"+std::to_string(GetCurrentProcessId())+".json");saveUiSettings();g_teamConnectors={};g_teamModels={};g_teamEfforts={};loadUiSettings();for(size_t i=0;i<3;++i)if(!g_teamConnectors[i]||teamRoleConfig(i).provider!=providers[i]||teamRoleConfig(i).model!=models[i]||teamRoleConfig(i).maxOutputTokens!=8192)return failure(502,"Per-role provider settings did not persist");std::filesystem::remove(g_settingsOverride);
        g_dialogOverrideQueue={{"inherit","0"}};g_dialogSmoke=3;bool inherited=click(44020);g_dialogSmoke=0;if(!inherited||g_teamConnectors[0]||!g_teamModels[0].empty()||!g_teamEfforts[0].empty())return failure(503,"Card could not restore inherited connector");
        {auto config=assistantProviderPreset("claude");config.model="foreign-offline-model";config.reasoningEffort="foreign-effort";bool inherit=false;g_dialogOverrideQueue={{"chatgpt-plan","0"}};g_dialogSmoke=3;bool selected=editConnector(config,false,inherit);g_dialogSmoke=0;if(!selected||config.authMode!=AssistantAuthMode::ChatGptPlan||!config.model.empty()||!config.reasoningEffort.empty())return failure(506,"ChatGPT plan switch retained a foreign provider model/effort");}
        {
            const auto savedHeadless=g_headless;const auto savedPath=g_settingsOverride;const auto savedDock=g_assistantDockOpen;
            g_settingsOverride=std::filesystem::temp_directory_path()/("ark-chat-first-run-"+std::to_string(GetCurrentProcessId())+".json");
            std::filesystem::remove(g_settingsOverride);g_assistantDockOpen=true;loadUiSettings();
            if(!g_assistantDockOpen)return failure(512,"A fresh installation did not keep the default open chat");
            g_headless=false;uiAction(400);if(g_assistantDockOpen)return failure(513,"The chat visibility control did not close the panel");
            g_assistantDockOpen=true;loadUiSettings();if(g_assistantDockOpen)return failure(514,"The closed chat preference was not restored");
            uiAction(400);g_assistantDockOpen=false;loadUiSettings();if(!g_assistantDockOpen)return failure(515,"The open chat preference was not restored");
            std::filesystem::remove(g_settingsOverride);g_settingsOverride=savedPath;g_headless=savedHeadless;g_assistantDockOpen=savedDock;layout();
        }
        {
            auto config=assistantProviderPreset("claude");config.model="foreign-offline-model";config.reasoningEffort="high";bool inherit=false;
            g_dialogOverrideQueue={{"grok-build","0"},{"grok",""}};g_dialogSmoke=3;g_dialogShot=shotPath+".grok-account.png";
            const bool selected=editConnector(config,false,inherit);g_dialogSmoke=0;
            if(!selected||config.authMode!=AssistantAuthMode::CliAccount||config.protocol!=AssistantProtocol::Acp||!config.apiKeyEnv.empty()||!config.model.empty()||!config.reasoningEffort.empty()||config.imageInputSupported)return failure(509,"Grok CLI connector retained foreign credentials, model, effort or image capability");
            AssistantConfig restored;readConnectorSettings(connectorSettings(config),restored);
            if(restored.authMode!=AssistantAuthMode::CliAccount||restored.protocol!=AssistantProtocol::Acp||restored.provider!="grok-build"||restored.endpoint!="grok"||!restored.apiKeyEnv.empty()||restored.imageInputSupported)return failure(510,"Grok CLI account and protocol did not persist together");
            const auto savedConnector=g_teamConnectors[0];const auto savedModel=g_teamModels[0],savedEffort=g_teamEfforts[0];
            g_dialogOverrideQueue={{"grok-build","0"},{"grok","offline-grok-acp"}};g_dialogSmoke=3;g_dialogShot=shotPath+".grok-card.png";
            const bool clicked=click(44020);g_dialogSmoke=0;
            if(!clicked||!g_teamConnectors[0]||teamRoleConfig(0).authMode!=AssistantAuthMode::CliAccount||teamRoleConfig(0).model!="offline-grok-acp"||!teamRoleConfig(0).reasoningEffort.empty()||g_session.revision!=revision||g_assistantConfig.authMode!=originalConfig.authMode)return failure(511,"Grok CLI card changed the global connector/drawing or lost its own model");
            g_teamConnectors[0]=savedConnector;g_teamModels[0]=savedModel;g_teamEfforts[0]=savedEffort;
        }
        auto exported=assistantExternalMcpConfig("opencode","C:/ARK/ark-mcp.exe","ark-ui-test",assistantProviderPreset("ollama"));if(exported.find("ark-mcp.exe")==std::string::npos||exported.find("--connect")==std::string::npos||exported.find("ark-ui-test")==std::string::npos)return failure(504,"External MCP profile lost executable or live document binding");
        {const auto raw=assistantExternalMcpConfig("grok-build","C:/ARK/ark-mcp.exe","ark-ui-test",originalConfig);const auto display=externalConfigDisplay(raw,"grok-build");std::string roundtrip;for(char c:display)if(c!='\r')roundtrip+=c;if(roundtrip!=raw)return failure(507,"Grok Build copy display changed the TOML configuration");}
        startLiveBridge();if(g_liveBridge.endpoint().empty())return failure(508,"Grok Build UI check could not start its isolated live bridge");g_dialogOverrideQueue={{"grok-build"}};g_dialogSmoke=3;g_dialogShot=shotPath+".grok-build.png";liveConnectionDialog();g_dialogSmoke=0;g_liveBridge.stop();if(g_status.find("External MCP configuration for grok-build")==std::string::npos||g_session.revision!=revision)return failure(508,"Grok Build client selection failed or changed the drawing");
        std::ofstream(shotPath+".connectors-test.log")<<"PASS: real card Connector click, native provider/model/protocol controls, distinct Claude/Grok/Ollama routes, inherited global unchanged, serialized/restored per-role routes, reset inheritance, external MCP binding, Grok Build native client selection and TOML copy. Offline model IDs; no provider inference or account authentication performed.\n";
        g_localModelDiscoveryTestTransport={};g_assistantConfig=originalConfig;g_teamConnectors=originalConnectors;g_teamModels=originalModels;g_teamEfforts=originalEfforts;g_settingsOverride=originalSettings;g_teamMode=originalTeam;g_assistantDockOpen=originalDock;g_status="Connector checks passed / mixed cards / persistence / external MCP profiles";layout();
    }

#include "authoring_extensions_selftest.inl"
    if(bomUiTest){
#include "bom_panel_selftest.inl"
    }
    if(drawingWorkspaceUiTest){
#include "drawing_workspace_selftest.inl"
    }
    if(localModelsUiTest){
#include "local_models_selftest.inl"
    }
    if(viewportDockUiTest){
#include "viewport_dock_selftest.inl"
    }
    if(drawingViewportUiTest){
#include "drawing_viewport_selftest.inl"
    }
    if(measurementUiTest){
#include "measurement_selftest.inl"
    }
    if(interactionUiTest){
        using V=arkjson::Value;auto parse=[](const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("Invalid interaction fixture");return v;};
        auto fail=[&](int code,const char* message){std::fprintf(stderr,"Interaction UI %d: %s / %s\n",code,message,g_status.c_str());std::ofstream(shotPath+".interaction-test.log")<<code<<": "<<message<<"\n"<<g_status<<"\n";return code;};
        auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);};
        auto capture=[&](const std::string& suffix){draw();glFinish();std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);return writePNG((shotPath+suffix).c_str(),image);};
        auto click=[&](int id){draw();auto zone=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});if(zone==g_zones.end())return false;int x=(int)(zone->x+zone->w/2),y=(int)(zone->y+zone->h/2);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
        auto fresh=[&]{V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);g_assistantDockOpen=false;g_toolModule=1;g_showViewCube=true;g_cameraOrtho=false;g_occlusionMode=0;g_grid=true;g_navMode=0;};
        auto selectBody=[&](int body){return click(2000+body);};
        auto navigationClear=[&]{draw();if(!g_showViewCube||g_cubeS<=0||std::none_of(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==179;}))return false;for(const auto& zone:g_zones)if(zone.id>=560&&zone.id<=564&&zone.x<g_cubeX+g_cubeS&&zone.x+zone.w>g_cubeX&&zone.y<g_cubeY+g_cubeS&&zone.y+zone.h>g_cubeY)return false;return true;};
        fresh();runTool("doc_apply",parse(R"({"commands":[{"op":"material.set","name":"Steel probe","density":7.85,"appearance":{"base_color":[0.3,0.65,0.85],"metallic":0.2}},{"op":"feature.add","type":"sphere","name":"Outer inspection part","p":{"r":"22"},"material":"Steel probe"},{"op":"feature.add","type":"box","name":"Interior probe","p":{"dx":"10","dy":"10","dz":"10"},"appearance":{"base_color":[1,0.3,0.08]}}]})"));
        if(g_error||g_bodyIds.size()!=2)return fail(470,"Transparency fixture failed");frameScene();if(!selectBody(0))return fail(471,"Part could not be selected through explorer mouse click");if(!navigationClear())return fail(510,"Selected-part controls hide or overlap the view cube");u64 before=g_session.revision;auto original=g_doc.find(g_bodyIds[0])->appearance;double volume=g_reports[g_instancePart[0]].signedVolume;
        if(!capture(".inspection-opaque.png")||!click(560)||g_error||g_session.revision!=before+1||std::abs(resolvedAppearance(g_doc,*g_doc.find(g_bodyIds[0])).opacity-.35)>1e-9)return fail(472,"Transparency toggle mouse dispatch failed");
        if(g_doc.materials[0].densityGcm3!=7.85||g_doc.find(g_bodyIds[0])->material!="Steel probe"||g_reports[g_instancePart[0]].signedVolume!=volume)return fail(473,"Transparency changed physical material or geometry");if(!capture(".inspection-transparent.png"))return fail(474,"Transparent capture failed");
        g_dialogOverrideQueue={{"55"}};g_dialogSmoke=3;bool adjusted=click(561);g_dialogSmoke=0;if(!adjusted||g_error||std::abs(resolvedAppearance(g_doc,*g_doc.find(g_bodyIds[0])).opacity-.55)>1e-9)return fail(475,"Opacity form mouse dispatch failed");
        if(!click(560)||g_error||resolvedAppearance(g_doc,*g_doc.find(g_bodyIds[0])).opacity!=1.)return fail(476,"Opaque restore failed");if(!click(18)||g_error||std::abs(resolvedAppearance(g_doc,*g_doc.find(g_bodyIds[0])).opacity-.55)>1e-9)return fail(477,"Opacity undo failed");
        dist=tDist=80;draw();double smallSpacing=g_adaptiveGrid.spacing;int smallCount=gridCount;if(!capture(".grid-small.png"))return fail(478,"Small grid capture failed");
        runTool("doc_apply",parse(R"({"commands":[{"op":"feature.edit","id":1,"p":{"r":"2200"}},{"op":"feature.edit","id":2,"p":{"dx":"1900","dy":"1900","dz":"1900"}},{"op":"assembly.add","name":"Large outer","feature":1,"p":{"tx":"1000000","tz":"-750000"}},{"op":"assembly.add","name":"Large interior","feature":2,"p":{"tx":"1000000","tz":"-750000"}}]})"));if(g_error)return fail(479,"Large offset fixture failed");
        dist=tDist=20000;target=tTarget={1000000,0,-750000};g_cameraOrtho=true;draw();if(g_adaptiveGrid.spacing<=smallSpacing*100||gridCount>884||smallCount>884||std::abs(g_adaptiveGrid.centerX-1000000)>g_adaptiveGrid.radius||std::abs(g_adaptiveGrid.centerZ+750000)>g_adaptiveGrid.radius)return fail(479,"Grid did not adapt to zoom and offset");if(!capture(".grid-meters-offset.png"))return fail(480,"Offset grid capture failed");before=g_session.revision;if(!click(22)||g_grid||g_session.revision!=before||!click(22)||!g_grid)return fail(481,"Grid toggle changed revision or failed");
        fresh();runTool("doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"box","name":"Frame definition","p":{"dx":"60","dy":"8","dz":"45"}},{"op":"feature.add","type":"box","name":"Fork definition","p":{"dx":"8","dy":"8","dz":"90"}},{"op":"feature.add","type":"box","name":"Bar source","p":{"dx":"110","dy":"8","dz":"8"}},{"op":"feature.add","type":"transform","name":"Handlebar definition","inputs":[3],"p":{"tz":"95"}},{"op":"feature.add","type":"torus","name":"Wheel source","p":{"R":"28","r":"6"}},{"op":"feature.add","type":"transform","name":"Wheel definition","inputs":[5],"p":{"tz":"-55","ry":"90"}},{"op":"assembly.add","name":"Frame","feature":1,"fixed":true},{"op":"assembly.add","name":"Fork","feature":2},{"op":"assembly.add","name":"Handlebar","feature":4},{"op":"assembly.add","name":"Wheel","feature":6}]})"));
        if(g_error||g_bodyIds.size()!=4)return fail(482,"Steering fixture failed");frameScene();draw();
        const std::vector<std::vector<std::string>> jointFields{{"revolute","1","Z","2","Z","0","0.5","0.5"},{"rigid","2","origin","3","origin","0","0.5","0.5"},{"revolute","2","X","4","X","0","0.5","0.5"}};
        for(size_t i=0;i<jointFields.size();++i){g_dialogOverrideQueue={jointFields[i]};g_dialogSmoke=3;if(!i)g_dialogShot=shotPath+".steering-mate-dialog.png";bool added=click(94);g_dialogSmoke=0;if(!added||g_error||g_doc.mates.size()!=i+1||g_doc.mates.back().type!=jointFields[i][0])return fail(512,"Revolute/rigid mate dropdown mouse authoring failed");}
        int bar=2;for(size_t i=0;i<g_instanceIds.size();++i)if(g_instanceIds[i]==3)bar=(int)i;
        if(!selectBody(bar)||!click(562)||g_error||!g_componentMove.armed||g_componentMove.joint.movingNodes.size()!=3)return fail(483,"Move component toolbar dispatch or joint selection failed");if(!navigationClear())return fail(511,"Move component controls hide or overlap the view cube");
        auto pointer=[&](int body,POINT& p){draw();const auto& mesh=g_cpuMeshes[g_instancePart[(size_t)body]];for(size_t t=0;t<mesh.idx.size();t+=3){Vec3 center{};for(int k=0;k<3;++k){size_t v=(size_t)mesh.idx[t+k]*3;center+=Vec3{mesh.pos[v],mesh.pos[v+1],mesh.pos[v+2]};}float x,y;if(!projectScreen(g_instanceWorld[(size_t)body].transformPoint(center/3.),x,y)||x<viewX+20||x>viewX+viewW-20||y<viewY+145||y>viewY+viewH-30||overViewCube(x,y))continue;p={(LONG)x,(LONG)y};if(pickBody(p.x,p.y)==body)return true;}return false;};
        POINT start;if(!pointer(bar,start))return fail(484,"Handlebar could not be picked in viewport");before=g_session.revision;auto baseline=g_instanceWorld;SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(start.x+70,start.y+30));
        auto changed=[&](size_t index){double difference=0;for(int k=0;k<16;++k)difference+=std::abs(g_instanceWorld[index].m[k]-baseline[index].m[k]);return difference>1e-5;};
        if(g_error||!g_componentMove.active||g_session.revision!=before||changed(0)||!changed(1)||!changed(2)||!changed(3))return fail(485,"Mouse preview did not propagate steering to fork and wheel");if(!capture(".steering-preview.png"))return fail(486,"Steering preview capture failed");
        SendMessage(hwnd,WM_KEYDOWN,VK_ESCAPE,0);if(g_componentMove.active||g_componentMove.armed||g_session.revision!=before||changed(1)||changed(2)||changed(3))return fail(487,"Esc failed to restore canonical placements");
        if(!click(562)||!pointer(bar,start))return fail(488,"Second drag could not start");SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(start.x+70,start.y+30));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(start.x+70,start.y+30));if(g_error||g_session.revision!=before+1||!changed(1)||!changed(2)||!changed(3))return fail(489,"Drag release did not commit exact placements");if(!capture(".steering-saved.png")||!click(18)||g_error||changed(1)||changed(2)||changed(3))return fail(490,"Steering undo did not restore all connected parts");
        cancelComponentMove();if(!selectBody(0)||!click(562)||!g_error||g_componentMove.active)return fail(491,"Grounded component was allowed to move");g_error=false;
        // A pneumatic rod and a rigid follower share exact prismatic travel.
        fresh();runTool("doc_apply",parse(R"({"commands":[{"op":"param.set","name":"stroke_min","expr":"0"},{"op":"param.set","name":"stroke_max","expr":"80"},{"op":"feature.add","type":"native_solid","name":"Cylinder body","p":{"op":"cylinder","radius":"12","height":"40"}},{"op":"feature.add","type":"native_solid","name":"Piston rod","p":{"op":"cylinder","radius":"4","height":"80"}},{"op":"feature.add","type":"box","name":"Follower source","p":{"dx":"18","dy":"12","dz":"10"}},{"op":"feature.add","type":"transform","name":"Rigid follower","inputs":[3],"p":{"tx":"16","tz":"65"}},{"op":"assembly.add","name":"Cylinder","feature":1,"fixed":true},{"op":"assembly.add","name":"Rod","feature":2,"p":{"tz":"30"}},{"op":"assembly.add","name":"Follower","feature":4,"p":{"tz":"30"}}]})"));
        if(g_error||g_bodyIds.size()!=3)return fail(620,"Pneumatic slider fixture failed");frameScene();g_cameraOrtho=true;draw();
        g_dialogOverrideQueue={{"prismatic","1","Z","2","Z","0","0.5","0.5","stroke_min","stroke_max"},{"rigid","2","origin","3","origin","0","0.5","0.5","",""}};g_dialogSmoke=3;g_dialogShot=shotPath+".slider-limits-dialog.png";
        bool sliderAdded=click(94),followerAdded=click(94);g_dialogSmoke=0;
        if(!sliderAdded||!followerAdded||g_error||g_doc.mates.size()!=2||!g_doc.mates[0].limits.enabled||g_doc.mates[0].limits.min!="stroke_min"||g_doc.mates[0].limits.max!="stroke_max")return fail(621,"Persistent prismatic limit form failed");
        int rod=-1;for(size_t i=0;i<g_instanceIds.size();++i)if(g_instanceIds[i]==2)rod=(int)i;
        if(rod<0||!selectBody(rod)||!click(562)||g_error||g_componentMove.joint.mode!="translate"||g_componentMove.joint.movingNodes.size()!=2||!g_componentMove.joint.limited||std::abs(g_componentMove.joint.position-30)>1e-5)return fail(622,"Slider mode or rigid follower selection failed");
        before=g_session.revision;baseline=g_instanceWorld;
        auto startSlider=[&](POINT& at){if(!pointer(rod,at))return false;SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(at.x,at.y));return g_componentMove.dragging&&!g_error;};
        auto travelPointer=[&](POINT at,double mm){float ax,ay,bx,by;auto joint=g_componentMove.joint;if(!projectScreen(joint.origin,ax,ay)||!projectScreen(joint.origin+joint.axis*20,bx,by))return POINT{at.x,at.y};return POINT{(LONG)std::lround(at.x+(bx-ax)*mm/20),(LONG)std::lround(at.y+(by-ay)*mm/20)};};
        if(!startSlider(start))return fail(623,"Rod viewport drag could not start");POINT high=travelPointer(start,200);SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(high.x,high.y));
        if(g_error||std::abs(g_componentMove.delta-50)>1e-5||changed(0)||!changed(1)||!changed(2)||g_session.revision!=before)return fail(624,"Mouse upper travel bound failed to clamp rod and follower");
        if(!capture(".slider-upper-preview.png"))return fail(625,"Slider upper preview capture failed");POINT low=travelPointer(start,-200);SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(low.x,low.y));
        if(g_error||std::abs(g_componentMove.delta+30)>1e-5||changed(0)||!changed(1)||!changed(2))return fail(626,"Mouse lower travel bound failed");
        if(!capture(".slider-lower-preview.png"))return fail(627,"Slider lower preview capture failed");SendMessage(hwnd,WM_KEYDOWN,VK_ESCAPE,0);
        if(g_componentMove.active||g_componentMove.armed||g_session.revision!=before||changed(0)||changed(1)||changed(2))return fail(628,"Slider Esc did not restore connected parts");
        if(!click(562)||!startSlider(start))return fail(629,"Slider release probe could not start");high=travelPointer(start,200);SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(high.x,high.y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(high.x,high.y));
        if(g_error||g_session.revision!=before+1||changed(0)||!changed(1)||!changed(2)||!capture(".slider-upper-saved.png"))return fail(630,"Slider release did not persist bounded travel");
        if(!click(18)||g_error||changed(1)||changed(2))return fail(631,"Slider undo did not restore both moving parts");
        cancelComponentMove();if(!click(562)||!startSlider(start))return fail(632,"Lower slider release probe could not start");low=travelPointer(start,-200);SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(low.x,low.y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(low.x,low.y));
        if(g_error||changed(0)||!changed(1)||!changed(2)||!capture(".slider-lower-saved.png")||!click(18)||g_error||changed(1)||changed(2))return fail(633,"Lower slider release or undo failed");
        // Limit removal uses null through the same persistent editor.
        cancelComponentMove();g_selectedMate=0;g_dialogOverrideQueue={{"prismatic","1","Z","2","Z","0","0.5","0.5","",""}};g_dialogSmoke=3;uiAction(95);g_dialogSmoke=0;
        if(g_error||g_doc.mates[0].limits.enabled)return fail(634,"Clearing both bounds did not remove persisted limits");uiAction(18);if(g_error||!g_doc.mates[0].limits.enabled)return fail(635,"Undo did not restore persisted limits");
        // Signed clamps account for a grounded B; angles crossing 180 remain continuous.
        AssemblyManipulationJoint clamp;clamp.limited=true;clamp.position=30;clamp.min=0;clamp.max=80;clamp.coordinateSign=-1;
        if(boundedComponentDelta(clamp,200)!=30||boundedComponentDelta(clamp,-200)!=-50)return fail(636,"Backwards slider coordinate clamp failed");clamp.coordinateSign=1;clamp.position=170;clamp.min=120;clamp.max=240;
        if(boundedComponentDelta(clamp,100)!=70||boundedComponentDelta(clamp,-100)!=-50)return fail(637,"Bounded angular interval crossing 180 failed");
        // End-on views cannot resolve axial travel and never substitute screen-X motion.
        g_cameraOrtho=true;yaw=tYaw=0;pitch=tPitch=0;frameScene();draw();if(!selectBody(rod)||!click(562)||!pointer(rod,start))return fail(638,"End-on slider probe could not select rod");before=g_session.revision;baseline=g_instanceWorld;
        SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
        if(!g_error||g_componentMove.dragging||g_componentMove.active||g_session.revision!=before||changed(1)||changed(2)||g_status.find("axis points into the view")==std::string::npos)return fail(639,"End-on view invented slider movement");
        SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(start.x,start.y));cancelComponentMove();g_error=false;
        // Replay the actual distributed hollow/transparent cylinder and use ordinary mouse gestures.
        fresh();std::ifstream cylinderFile("examples/pneumatic-cylinder.arkops");std::string cylinderBytes((std::istreambuf_iterator<char>(cylinderFile)),std::istreambuf_iterator<char>());V cylinderEnvelope;
        if(!arkjson::parse(cylinderBytes,cylinderEnvelope)||!cylinderEnvelope.find("arguments"))return fail(650,"Distributed pneumatic example could not be read");V cylinderArgs=*cylinderEnvelope.find("arguments");cylinderArgs.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",cylinderArgs);
        if(g_error||g_bodyIds.size()!=3||g_doc.mates.size()!=2)return fail(651,"Distributed pneumatic example did not build");
        int barrel=-1,eye=-1;rod=-1;for(size_t i=0;i<g_instanceIds.size();++i){if(g_instanceIds[i]==1)barrel=(int)i;if(g_instanceIds[i]==2)rod=(int)i;if(g_instanceIds[i]==3)eye=(int)i;}
        if(barrel<0||rod<0||eye<0||std::abs(resolvedAppearance(g_doc,*g_doc.find(g_bodyIds[(size_t)barrel])).opacity-.35)>1e-9)return fail(652,"Actual transparent barrel/rod/eye identity mismatch");
        g_lightBackground=true;g_satin=true;g_showEdges=false;wireframe=false;g_pickKind=0;g_showSketches=false;g_showDatumPlanes=false;g_activeSketch.clear();g_sidebarTab=0;cameraRoll=0;uiAction(24);frameScene();draw();
        if(!selectBody(eye)||g_componentMove.armed||!pointer(eye,start)||!capture(".direct-cylinder-selected-eye.png"))return fail(653,"Selected eye default-view fixture failed");
        auto motionEnd=[&](POINT at,int body,double mm){Vec3 origin=g_instanceWorld[(size_t)body].transformPoint({0,0,0}),axis=g_instanceWorld[(size_t)body].transformDir({0,0,1});float ax,ay,bx,by;projectScreen(origin,ax,ay);projectScreen(origin+axis*mm,bx,by);return POINT{at.x+(LONG)std::lround(bx-ax),at.y+(LONG)std::lround(by-ay)};};
        before=g_session.revision;baseline=g_instanceWorld;SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(start.x+1,start.y+1));
        if(g_componentMove.active||g_componentMove.armed||!g_componentDragIntent.pending||g_session.revision!=before||changed(rod)||changed(eye))return fail(654,"Ordinary click or tiny pointer motion started component movement");
        POINT end=motionEnd(start,eye,45);SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
        if(g_error||!g_componentMove.dragging||g_componentMove.armed||g_componentMove.driver!=3||g_componentMove.joint.mode!="translate"||g_componentMove.delta<=1||g_componentMove.delta>80||changed(barrel)||!changed(rod)||!changed(eye)||g_session.revision!=before)return fail(655,"Direct eye drag did not preview rod and rigid eye together");
        if(!capture(".direct-cylinder-eye-preview.png"))return fail(656,"Direct eye preview capture failed");SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
        if(g_error||g_componentMove.armed||g_componentMove.active||g_session.revision!=before+1||changed(barrel)||!changed(rod)||!changed(eye)||!capture(".direct-cylinder-eye-saved.png"))return fail(657,"Direct eye release failed to commit one transaction or left Move mode armed");
        before=g_session.revision;if(!pointer(eye,start))return fail(658,"Saved eye could not be clicked");SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(start.x,start.y));
        if(g_session.revision!=before||g_componentMove.armed||g_componentMove.active)return fail(659,"A click after direct drag unexpectedly moved the drawing");
        if(!click(18)||g_error||changed(rod)||changed(eye))return fail(660,"Actual direct-drag undo did not restore rod and eye");
        // A simple click still selects the transparent body. Only motion intent sees the rod behind it.
        draw();POINT through{-1,-1};for(int z=16;z<116&&through.x<0;z+=8){float x,y;Vec3 point=g_instanceWorld[(size_t)rod].transformPoint({0,0,(double)z});if(!projectScreen(point,x,y)||overViewCube(x,y)||y<viewY+145||y>viewY+viewH-30)continue;std::vector<SurfaceHit> hits;auto front=pickSurface((int)x,(int)y,&hits);if(front.body==barrel&&std::any_of(hits.begin(),hits.end(),[&](const SurfaceHit& hit){return hit.body==rod;}))through={(LONG)x,(LONG)y};}
        if(through.x<0)return fail(661,"Actual rod behind transparent barrel could not be found in the default view");before=g_session.revision;baseline=g_instanceWorld;selBody=-1;
        SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(through.x,through.y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(through.x,through.y));
        if(selBody!=barrel||g_componentMove.armed||g_session.revision!=before)return fail(662,"Transparent barrel lost ordinary click selection");
        uiAction(171);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(through.x,through.y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(through.x,through.y));
        if(selBody!=barrel||g_componentMove.active||g_session.revision!=before)return fail(663,"Face picking incorrectly passed through the transparent barrel");uiAction(170);
        end=motionEnd(through,rod,40);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(through.x,through.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
        if(g_error||!g_componentMove.dragging||g_componentMove.driver!=2||g_componentMove.armed||changed(barrel)||!changed(rod)||!changed(eye)||g_session.revision!=before)return fail(664,"Direct motion through the transparent fixed barrel did not choose the rod");
        if(!capture(".direct-cylinder-through-barrel-preview.png"))return fail(665,"Through-barrel movement capture failed");SendMessage(hwnd,WM_KEYDOWN,VK_ESCAPE,0);SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
        if(g_componentMove.armed||g_componentMove.active||g_session.revision!=before||changed(rod)||changed(eye))return fail(666,"Esc did not restore direct through-barrel movement and normal selection");
        // Shift/Control retain selection semantics; middle drag still orbits without a write.
        if(!pointer(eye,start))return fail(667,"Modifier drag eye target could not be picked");end=motionEnd(start,eye,40);
        for(WPARAM modifier:{(WPARAM)MK_SHIFT,(WPARAM)MK_CONTROL}){SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON|modifier,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON|modifier,MAKELPARAM(end.x,end.y));SendMessage(hwnd,WM_LBUTTONUP,modifier,MAKELPARAM(end.x,end.y));if(g_componentMove.active||g_componentMove.armed||g_session.revision!=before||changed(rod)||changed(eye))return fail(668,"Modified selection gesture started component movement");}
        double orbitYaw=tYaw,orbitPitch=tPitch;SendMessage(hwnd,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(start.x+45,start.y+20));SendMessage(hwnd,WM_MBUTTONUP,0,MAKELPARAM(start.x+45,start.y+20));
        if((tYaw==orbitYaw&&tPitch==orbitPitch)||g_session.revision!=before||changed(rod)||changed(eye))return fail(669,"Direct dragging interfered with middle-mouse orbit");uiAction(24);frameScene();draw();
        applyCommand(parse(R"({"op":"feature.edit","id":3,"appearance":{"opacity":1}})"));if(g_error)return fail(670,"Opaque obstruction fixture failed");before=g_session.revision;baseline=g_instanceWorld;
        if(pickBody(through.x,through.y)!=barrel)return fail(671,"Opaque barrel is not in front of the rod");SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(through.x,through.y));end=motionEnd(through,rod,40);SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
        if(!g_error||g_componentMove.active||g_componentMove.armed||g_session.revision!=before||changed(barrel)||changed(rod)||changed(eye)||g_status.find("grounded")==std::string::npos)return fail(672,"Direct drag moved a hidden part through an opaque grounded body or gave no feedback");SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));g_error=false;
        // Ambiguous direct motion stays nonmodal; the explicit tool still selects the intended DOF.
        applyCommand(parse(R"({"op":"mate.edit","id":1,"type":"concentric","limits":null})"));if(g_error||!selectBody(eye)||!pointer(eye,start))return fail(673,"Ambiguous concentric fixture failed");before=g_session.revision;baseline=g_instanceWorld;end=motionEnd(start,eye,35);
        SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
        if(!g_error||g_dialog||g_componentMove.active||g_componentMove.armed||g_session.revision!=before||changed(rod)||changed(eye)||g_status.find("Use Move component")==std::string::npos)return fail(674,"Ambiguous direct drag silently chose a DOF or opened a modal selector");SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));g_error=false;
        g_dialogOverrideQueue={{"Translate / joint #1"}};g_dialogSmoke=3;g_dialogShot=shotPath+".direct-ambiguity-explicit-dialog.png";bool chosen=click(562);g_dialogSmoke=0;
        if(!chosen||g_error||!g_componentMove.armed||g_componentMove.joint.mode!="translate"||!pointer(eye,start))return fail(675,"Explicit Move component could not resolve the concentric ambiguity");end=motionEnd(start,eye,30);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
        if(g_error||!g_componentMove.armed||g_session.revision!=before+1||changed(barrel)||!changed(rod)||!changed(eye))return fail(676,"Explicit chosen motion no longer commits or retains its armed mode");cancelComponentMove();if(!click(18)||g_error||changed(rod)||changed(eye))return fail(677,"Explicit ambiguity-resolution undo failed");
        // Rotation fallback must use the moving body's hit, not a differently transformed transparent cover.
        fresh();runTool("doc_apply",parse(R"({"commands":[{"op":"feature.add","type":"box","name":"Revolute anchor","p":{"dx":"8","dy":"8","dz":"8"}},{"op":"feature.add","type":"box","name":"Revolute arm","p":{"dx":"20","dy":"10","dz":"50"}},{"op":"feature.add","type":"box","name":"Transparent cover","p":{"dx":"100","dy":"100","dz":"2"},"appearance":{"opacity":0.35}},{"op":"assembly.add","name":"Anchor","feature":1,"fixed":true,"p":{"ry":"90"}},{"op":"assembly.add","name":"Arm","feature":2,"p":{"ry":"90"}},{"op":"assembly.add","name":"Offset cover","feature":3,"fixed":true,"p":{"tx":"-25","ty":"-50","tz":"70","rx":"10","rz":"12"}},{"op":"mate.add","a":1,"b":2,"type":"revolute","reference_a":"Z","reference_b":"Z"}]})"));
        if(g_error||g_bodyIds.size()!=3)return fail(678,"Transparent offset cover rotation fixture failed");g_cameraOrtho=true;yaw=tYaw=0;pitch=tPitch=0;cameraRoll=0;frameScene();draw();SurfaceHit armHit;POINT covered{-1,-1};
        for(double z:{20.,30.,40.})for(double y:{2.,5.,8.}){float xscreen,yscreen;Vec3 point=g_instanceWorld[1].transformPoint({10,y,z});if(!projectScreen(point,xscreen,yscreen)||overViewCube(xscreen,yscreen)||yscreen<viewY+145)continue;std::vector<SurfaceHit> hits;auto front=pickSurface((int)xscreen,(int)yscreen,&hits);auto arm=std::find_if(hits.begin(),hits.end(),[](const SurfaceHit& hit){return hit.body==1;});if(front.body==2&&arm!=hits.end()){covered={(LONG)xscreen,(LONG)yscreen};armHit=*arm;}}
        if(covered.x<0)return fail(679,"Offset transparent cover did not occlude the actual rotating arm");Vec3 expectedRadial=g_instanceWorld[1].transformPoint(armHit.local);Vec3 axis=g_instanceWorld[1].transformDir({0,0,1});expectedRadial=(expectedRadial-axis*expectedRadial.dot(axis)).normalized();before=g_session.revision;baseline=g_instanceWorld;
        SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(covered.x,covered.y));SendMessage(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(covered.x+45,covered.y+25));
        if(g_error||!g_componentMove.dragging||g_componentMove.driver!=2||g_componentMove.joint.mode!="rotate"||!g_componentMove.radial.approx(expectedRadial,1e-8)||changed(0)||!changed(1)||changed(2)||g_session.revision!=before)return fail(680,"Rotation fallback used transparent cover coordinates instead of the chosen body's exact hit");
        if(!capture(".direct-covered-revolute-preview.png"))return fail(681,"Covered revolute preview capture failed");SendMessage(hwnd,WM_KEYDOWN,VK_ESCAPE,0);SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(covered.x+45,covered.y+25));if(g_componentMove.armed||g_session.revision!=before||changed(1))return fail(682,"Covered direct rotation cancel failed");
        std::ofstream receipt(shotPath+".interaction-test.log");receipt<<"PASS: real mouse opacity toggle, adjustment, restoration, physical invariance and undo; adaptive 1/2/5 grid, bounded density, meters and large offset, grid toggle; steering preview carries fork+wheel, Esc rollback, release atomic commit, undo, fixed rejection; pneumatic rod+rigid follower, persistent min/max expressions and clearing/undo, both travel endpoints, release/undo/Esc, backwards signed clamp, angular interval crossing 180, end-on view rejection; distributed hollow transparent cylinder direct eye/rod drag, threshold/click/modifiers/MMB, atomic release/undo/Esc, motion-only transparent penetration, opaque blocking, nonmodal ambiguity/explicit choice, chosen-hit revolute fallback.\n";g_status="Interaction checks passed / transparency / adaptive grid / connected joint drag / cancel / undo";
    }
    if(projectionUiTest||frameToolsUiTest){
        using V=arkjson::Value;auto parse=[](const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Invalid UI test fixture");return value;};
        auto failure=[&](int code,const std::string& message){std::ofstream log(shotPath+".geometry-ui-test.log");log<<message<<"\nStatus: "<<g_status<<"\nRevision: "<<g_session.revision<<"\nModule: "<<g_toolModule<<" / pick kind "<<g_pickKind<<" / picks "<<g_picked.size()<<"\nPreview active: "<<g_projectionPreview.active<<" / spatial "<<g_projectionPreview.spatial<<" / vertices "<<g_projectionPreview.lines.size()/3<<" / baseline "<<g_projectionPreview.revision<<" / zmin "<<g_projectionPreview.bounds.mn.z<<"\n";for(const auto& picked:g_picked)log<<"Pick: "<<picked.kind<<" "<<picked.name<<" feature="<<picked.feature<<" instance="<<picked.instance<<" copy="<<picked.copy<<" owner="<<picked.owner<<"\n";std::fprintf(stderr,"Geometry UI selftest %d: %s / %s\n",code,message.c_str(),g_status.c_str());return code;};
        auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;HDC dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);};
        auto capture=[&](const std::string& suffix){draw();glFinish();std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);return writePNG((shotPath+suffix).c_str(),image);};
        auto click=[&](int action){draw();auto found=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;});if(found==g_zones.end())return false;int x=(int)(found->x+found->w*.5f),y=(int)(found->y+found->h*.5f);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
        auto fresh=[&]{V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);g_picked.clear();g_explorerFeature=0;g_assistantDockOpen=false;g_teamMode=false;g_pickKind=0;g_motionActive=false;g_toolModule=1;layout();};
        if(frameToolsUiTest){
            fresh();draw();u64 untouched=g_session.revision;
            for(int action:{496,497,498}){if(std::any_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;}))return failure(420,"Frame tool is enabled without a frame definition");auto zone=g_frameRibbonBounds[action-496];POINT hover{(LONG)(zone.x+zone.w*.5f),(LONG)(zone.y+zone.h*.5f)};g_testPointer=hover;draw();if(g_lastRibbonHint.find("Construction Frame")==std::string::npos||g_lastRibbonHint.find("unsupported")==std::string::npos){g_testPointer.reset();return failure(421,"Disabled frame tool lost its actionable tooltip");}SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(hover.x,hover.y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(hover.x,hover.y));if(g_session.revision!=untouched){g_testPointer.reset();return failure(422,"Disabled frame tool changed the drawing");}}
            if(!capture(".frame-tools-disabled.png"))return failure(423,"Could not capture disabled frame tools");g_testPointer.reset();
            applyCommand(parse(R"({"op":"feature.add","type":"native_solid","name":"Generic tube-like solid","native":{"op":"cylinder"},"p":{"radius":"10","height":"100"}})"));if(g_error)return failure(424,"Generic solid fixture failed");draw();for(int action:{496,497,498})if(std::any_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==action;}))return failure(425,"Frame tool accepted a generic solid");
            fresh();applyCommand(parse(R"({"op":"feature.add","type":"construction_frame","name":"Instanced frame definition","native":{"profiles":[{"name":"SHS20","section":"rect","width":20,"height":20,"wall":2,"density_g_cm3":7.85}],"nodes":[{"name":"A","xyz":[0,0,0]},{"name":"B","xyz":[200,0,0]},{"name":"C","xyz":[100,0,100]},{"name":"D","xyz":[100,0,20]}],"members":[{"name":"rail","profile":"SHS20","start":"A","end":"B","normal":[0,0,1]},{"name":"brace","profile":"SHS20","start":"C","end":"D","normal":[1,0,0]}]}})"));if(g_error)return failure(426,"Frame fixture failed");u64 frameId=g_doc.features.back().id;
            V args=V::makeObj(),commands=V::makeArr();for(int copy=0;copy<2;++copy){V node=V::makeObj(),placement=V::makeObj();node.set("op",V::makeStr("assembly.add"));node.set("name",V::makeStr("Frame instance "+std::to_string(copy+1)));node.set("feature",V::makeNum((double)frameId));placement.set("tx",V::makeNum(40+copy*280));placement.set("ty",V::makeNum(20));placement.set("tz",V::makeNum(30));node.set("p",placement);commands.arr.push_back(node);}args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",commands);runTool("doc_apply",args);if(g_error||g_instanceIds.size()!=2||!g_instanceIds[0])return failure(427,"Instanced frame fixture failed");selBody=0;g_explorerFeature=0;g_toolModule=1;frameScene();u64 before=g_session.revision;
            g_dialogOverrideQueue={{"add","brace","end"},{"rail","25"}};g_dialogSmoke=3;g_dialogShot=shotPath+".frame-instance-definition-dialog.png";bool clicked=click(496);g_dialogSmoke=0;if(!clicked||g_error||g_session.revision!=before+1)return failure(428,"Trim/extend ribbon mouse dispatch rejected the assembly frame definition");
            g_dialogOverrideQueue={{"add","rail","end"},{"[180,0,0]","[1,0,0]"}};g_dialogSmoke=3;clicked=click(497);g_dialogSmoke=0;if(!clicked||g_error||g_session.revision!=before+2)return failure(429,"Trim plane ribbon mouse dispatch failed");
            g_dialogOverrideQueue={{"add","Test bead"},{"rail","brace","[90,-5,10]","[90,5,10]","[-1,0,0]","3","3","","7.85"}};g_dialogSmoke=3;clicked=click(498);g_dialogSmoke=0;auto* frame=g_doc.find(frameId);if(!clicked||g_error||g_session.revision!=before+3||!frame||!frame->nativeSpec.find("welds")||frame->nativeSpec.find("welds")->arr.size()!=1||g_doc.assembly.size()!=2)return failure(430,"Weld ribbon mouse dispatch failed or changed instance identity");
            if(!capture(".frame-instance-tools.png"))return failure(431,"Could not capture edited frame instances");if(!click(18)||g_error)return failure(432,"Frame edit undo mouse dispatch failed");frame=g_doc.find(frameId);auto* welds=frame?frame->nativeSpec.find("welds"):nullptr;if(!frame||!frame->nativeSpec.find("profiles")||!frame->nativeSpec.find("members")||(welds&&!welds->arr.empty())||g_doc.assembly.size()!=2)return failure(433,"Undo did not restore the frame definition and instances");
            // The separate Tube helper acts on feature definitions, preserves source metadata and publishes a linked result.
            fresh();V tubeFixture=parse(R"({"commands":[{"op":"material.set","name":"UI declared steel","density":7.85,"appearance":{"base_color":[0.1,0.3,0.6]}},{"op":"feature.add","type":"tube","name":"Host stock","p":{"section":"rect","width":60,"height":40,"wall":3,"tail":200}},{"op":"feature.add","type":"native_solid","name":"Host definition","inputs":["Host stock"],"native":{"op":"transform"},"p":{"tx":0,"ty":0,"tz":0}},{"op":"feature.add","type":"tube","name":"Brace stock","p":{"section":"rect","width":20,"height":16,"wall":2,"tail":100}},{"op":"feature.add","type":"native_solid","name":"Brace definition","inputs":["Brace stock"],"native":{"op":"transform"},"p":{"ry":-90,"tx":100,"ty":9,"tz":0},"material":"UI declared steel","appearance":{"roughness":0.8}}]})");tubeFixture.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",tubeFixture);if(g_error||g_doc.features.size()!=4)return failure(432,"Tube end-fit UI fixture failed");g_explorerFeature=4;g_toolModule=1;before=g_session.revision;g_dialogOverrideQueue={{"UI fitted tube","4","2","start","envelope","","0"}};g_dialogSmoke=3;g_dialogShot=shotPath+".tube-end-fit-dialog.png";clicked=click(549);g_dialogSmoke=0;
            if(!clicked||g_error||g_session.revision!=before+1||g_doc.features.size()!=5)return failure(433,"Tube end-fit scalar form mouse dispatch failed");auto* fitted=g_doc.find(5);if(!fitted||fitted->material!="UI declared steel"||fitted->appearance.dump()!=g_doc.find(4)->appearance.dump()||fitted->inputs.size()!=2||fitted->inputs[0]!=4||fitted->inputs[1]!=2||g_doc.find(3)->type!=FeatureType::Tube)return failure(434,"Tube end-fit lost source/material/appearance links");V inspect=V::makeObj();inspect.set("id",V::makeNum(5));auto measured=g_session.call("native_inspect",inspect);auto* proof=measured.find("structuredContent");proof=proof&&proof->find("report")?proof->find("report")->find("end_fit"):nullptr;if(measured.find("isError")->b||!proof||!proof->find("terminal_ownership_checked")->b||std::abs(proof->find("overlap_volume_mm3")->num)>1e-8||std::abs(proof->find("separation_mm")->num)>1e-6)return failure(435,"Tube end-fit has no measured exact contact receipt");if(!capture(".tube-end-fit.png"))return failure(436,"Could not capture fitted tube UI");
            if(!std::filesystem::is_regular_file(shotPath+".tube-end-fit-dialog.png.candidate.png"))return failure(437,"Native tube fit candidate image review was not captured");before=g_session.revision;g_explorerFeature=4;selBody=-1;g_dialogOverrideQueue={{"UI cancelled fit","4","2","start","envelope","","0"}};g_dialogSmoke=3;g_tubeFitReviewCancelTest=true;g_dialogShot=shotPath+".tube-end-fit-cancel.png";clicked=click(549);g_tubeFitReviewCancelTest=false;g_dialogSmoke=0;if(!clicked||g_error||g_session.revision!=before||g_doc.features.size()!=5||!std::filesystem::is_regular_file(shotPath+".tube-end-fit-cancel.png.candidate.png"))return failure(438,"Cancelling the real candidate review changed the drawing");
            V occurrences=parse(R"({"commands":[{"op":"assembly.add","name":"Fit group","feature":0,"p":{"tx":50,"ty":35,"tz":20}},{"op":"assembly.add","name":"Host occurrence","feature":2,"parent":1,"fixed":true,"p":{"tx":15,"ty":5,"tz":10}},{"op":"assembly.add","name":"Brace occurrence","feature":4,"parent":1,"fixed":true,"p":{"tx":15,"ty":5,"tz":10}},{"op":"mate.add","a":2,"b":3,"type":"coincident","reference_a":"origin","reference_b":"origin"}]})");occurrences.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",occurrences);if(g_error||g_doc.assembly.size()!=3||g_doc.mates.size()!=1)return failure(746,"Placed tube fit occurrence fixture rejected");
            auto savedAssembly=g_doc.assembly;auto savedMates=g_doc.mates;auto savedTransforms=g_instanceWorld;const Feature savedSource=*g_doc.find(4);g_explorerFeature=0;selBody=-1;for(size_t i=0;i<g_instanceIds.size();++i)if(g_instanceIds[i]==3&&g_instanceCopies[i]==0)selBody=(int)i;if(selBody<0)return failure(747,"Source occurrence could not be selected");
            before=g_session.revision;std::string beforeReplacement=documentToJson(g_doc);g_dialogOverrideQueue={{"UI cancelled selected fit","4","2","start","envelope","","0"}};g_dialogSmoke=3;g_tubeFitReviewCancelTest=true;g_dialogShot=shotPath+".tube-end-fit-selected-cancel.png";clicked=click(549);g_tubeFitReviewCancelTest=false;g_dialogSmoke=0;if(!clicked||g_error||g_session.revision!=before||documentToJson(g_doc)!=beforeReplacement)return failure(755,"Cancelling a selected occurrence replacement changed the live document");
            before=g_session.revision;g_dialogOverrideQueue={{"UI placed fit","4","2","start","envelope","","0"}};g_dialogSmoke=3;g_dialogShot=shotPath+".tube-end-fit-selected.png";clicked=click(549);g_dialogSmoke=0;auto* retainedSource=g_doc.find(4);if(!clicked||g_error||g_session.revision!=before+1||g_doc.features.size()!=6||g_doc.assembly.size()!=savedAssembly.size()||g_doc.mates.size()!=savedMates.size()||!retainedSource||retainedSource->name!=savedSource.name||retainedSource->type!=savedSource.type||retainedSource->p!=savedSource.p||retainedSource->inputs!=savedSource.inputs||retainedSource->nativeSpec.dump()!=savedSource.nativeSpec.dump()||retainedSource->material!=savedSource.material||retainedSource->appearance.dump()!=savedSource.appearance.dump())return failure(748,"Selected occurrence fit duplicated an occurrence or changed its source history");
            for(size_t i=0;i<savedAssembly.size();++i){const auto& original=savedAssembly[i];const auto& revised=g_doc.assembly[i];if(revised.id!=original.id||revised.parent!=original.parent||revised.name!=original.name||revised.p!=original.p||revised.fixed!=original.fixed||revised.visible!=original.visible||revised.pattern.active!=original.pattern.active||revised.feature!=(original.id==3?6:original.feature))return failure(749,"Selected fit changed occurrence identity, placement, flags or the host binding");}if(g_doc.mates.front().id!=savedMates.front().id||g_doc.mates.front().a!=savedMates.front().a||g_doc.mates.front().b!=savedMates.front().b||g_doc.mates.front().reference_a!=savedMates.front().reference_a||g_doc.mates.front().reference_b!=savedMates.front().reference_b)return failure(750,"Selected fit silently changed mate references");if(g_instanceWorld.size()!=savedTransforms.size())return failure(751,"Placed fit changed display instance multiplicity");for(size_t i=0;i<savedTransforms.size();++i)for(int k=0;k<16;++k)if(std::abs(g_instanceWorld[i].m[k]-savedTransforms[i].m[k])>1e-9)return failure(752,"Selected fit changed a solved instance pose");
            inspect.set("id",V::makeNum(6));measured=g_session.call("native_inspect",inspect);proof=measured.find("structuredContent");proof=proof&&proof->find("report")?proof->find("report")->find("end_fit"):nullptr;if(measured.find("isError")->b||!proof||!proof->find("terminal_ownership_checked")->b||std::abs(proof->find("overlap_volume_mm3")->num)>1e-8||std::abs(proof->find("separation_mm")->num)>1e-6||!capture(".tube-end-fit-replaced-occurrence.png"))return failure(753,"Selected occurrence fit has no exact contact proof or capture");
            before=g_session.revision;std::string unchanged=documentToJson(g_doc);g_dialogOverrideQueue={{"UI cancelled replacement","4","2","start","envelope","","0","All source occurrences"}};g_dialogSmoke=3;g_tubeFitReviewCancelTest=true;g_explorerFeature=4;selBody=-1;g_dialogShot=shotPath+".tube-end-fit-replacement-cancel.png";clicked=click(549);g_tubeFitReviewCancelTest=false;g_dialogSmoke=0;if(clicked&&g_error&&g_session.revision==before&&documentToJson(g_doc)==unchanged){g_error=false;}else return failure(754,"Missing source occurrence replacement was not explicitly rejected");
            g_status="Frame UI checks passed / instance definitions / trim controls / tube end-fit form / actual exact contact / metadata / linked source";
        }
        if(projectionUiTest){
            fresh();applyCommand(parse(R"({"op":"param.set","name":"RingR","expr":"12"})"));applyCommand(parse(R"({"op":"plane.set","name":"Raised","parent":"XY","p":{"oz":"40"}})"));g_dialogOverrideQueue={{"Ring source","XY","5","7","RingR"}};g_dialogSmoke=3;uiAction(62);g_dialogSmoke=0;if(g_error||!g_doc.findSketch("Ring source"))return failure(440,"Circle source UI failed");
            auto pickCurve=[&](const std::string& sketchName){g_picked.clear();uiAction(173);draw();for(const auto& sample:g_curveSamples)if(sample.sketch==sketchName){float x,y;if(!projectScreen((sample.a+sample.b)*.5,x,y)||overViewCube(x,y))continue;int sx=(int)std::lround(x),sy=(int)std::lround(y);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(sx,sy));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(sx,sy));if(g_picked.size()==1&&g_picked.front().owner==sketchName)return true;}return false;};
            if(!pickCurve("Ring source"))return failure(441,"Circle source could not be selected through viewport mouse picking");u64 before=g_session.revision;g_dialogOverrideQueue={{"Circle projection","Raised","Projected ring"}};g_dialogSmoke=3;bool clicked=click(536);g_dialogSmoke=0;if(!clicked)return failure(442,"Project 2D action 536 is missing from the current ribbon");if(g_error||!g_projectionPreview.active||g_session.revision!=before||g_projectionPreview.lines.size()<48||std::abs(g_projectionPreview.bounds.mn.z-40)>1e-6)return failure(442,"2D normal circle preview failed or changed the document");
            if(!capture(".project-circle-preview.png"))return failure(443,"Could not capture the circle projection preview");if(!click(539)||g_error||g_session.revision!=before+1)return failure(444,"2D projection Apply mouse dispatch failed");auto* projected=g_doc.findSketch("Circle projection");if(!projected||projected->sk.ents.size()!=1||projected->sk.ents[0].projectionBrep.empty()||projected->sk.ents[0].sourceSketch!="Ring source")return failure(445,"2D projection lost exact source binding");double span=g_sketchBounds.extent().x;applyCommand(parse(R"({"op":"param.set","name":"RingR","expr":"19"})"));if(g_error||g_sketchBounds.extent().x<span+10)return failure(446,"Projected circle did not update after an editable source radius changed");if(!capture(".project-circle-updated.png"))return failure(447,"Could not capture the updated projected circle");
            applyCommand(parse(R"({"op":"param.set","name":"CurveH","expr":"70"})"));g_dialogOverrideQueue={{"Spatial source","nurbs"},{"[[0,0,20],[25,25,35],[65,-20,55],[100,10,\"CurveH\"]]","3",""}};g_dialogSmoke=3;clicked=click(538);g_dialogSmoke=0;if(!clicked||g_error)return failure(448,"Editable spatial NURBS curve UI failed");u64 sourceId=g_doc.features.back().id;
            g_picked.clear();uiAction(173);draw();bool picked=false;for(size_t body=0;body<g_bodyIds.size()&&!picked;++body)if(g_bodyIds[body]==sourceId){const auto& pick=g_partPicks[g_instancePart[body]];for(const auto& edge:pick.edges)for(size_t point=1;point<edge.size()&&!picked;++point){float x,y;if(!projectScreen(g_instanceWorld[body].transformPoint((edge[point-1]+edge[point])*.5),x,y)||overViewCube(x,y))continue;int sx=(int)std::lround(x),sy=(int)std::lround(y);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(sx,sy));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(sx,sy));picked=g_picked.size()==1&&g_picked.front().feature==sourceId;}}
            if(!picked)return failure(449,"Spatial curve could not be picked through the viewport");before=g_session.revision;g_dialogOverrideQueue={{"Linked spatial result"}};g_dialogSmoke=3;clicked=click(537);g_dialogSmoke=0;if(!clicked||g_error||!g_projectionPreview.active||!g_projectionPreview.spatial||g_session.revision!=before||g_projectionPreview.bounds.extent().z<45)return failure(450,"3D Include preview flattened the spatial curve or changed the document");if(!capture(".include-spatial-preview.png"))return failure(451,"Could not capture spatial Include preview");if(!click(539)||g_error)return failure(452,"3D Include Apply mouse dispatch failed");auto* linked=g_doc.find(g_doc.features.back().id);auto* mode=linked?linked->nativeSpec.find("mode"):nullptr;if(!linked||linked->type!=FeatureType::Curve||!mode||mode->asStr()!="3d")return failure(453,"3D linked curve was not saved");u64 linkedId=linked->id;applyCommand(parse(R"({"op":"param.set","name":"CurveH","expr":"95"})"));if(g_error)return failure(454,"Linked spatial curve source update failed");bool zPreserved=false;for(size_t body=0;body<g_bodyIds.size();++body)if(g_bodyIds[body]==linkedId&&g_reports[g_instancePart[body]].bounds.extent().z>70)zPreserved=true;if(!zPreserved)return failure(455,"Linked spatial curve lost Z on editable-source recompute");if(!capture(".include-spatial-updated.png"))return failure(456,"Could not capture updated spatial curve");
            // A staged preview must not be rebased after an external mutation.
            if(!pickCurve("Ring source"))return failure(457,"Circle source reselect failed");g_dialogOverrideQueue={{"Stale target","XY","Stale ring"}};g_dialogSmoke=3;clicked=click(536);g_dialogSmoke=0;if(!clicked||!g_projectionPreview.active)return failure(458,"Stale preview fixture failed");auto pending=g_projectionPreview;applyCommand(parse(R"({"op":"param.set","name":"Unrelated","expr":"1"})"));u64 changed=g_session.revision;g_projectionPreview=std::move(pending);interactiveAction(539);if(!g_error||g_session.revision!=changed||g_doc.findSketch("Stale target"))return failure(459,"Projection silently rebased a stale preview");g_error=false;
            // A clicked patterned instance retains its concrete node and copy through preview/apply.
            fresh();applyCommand(parse(R"({"op":"feature.add","type":"native_solid","name":"Cylinder source","native":{"op":"cylinder"},"p":{"radius":"8","height":"20"}})"));if(g_error)return failure(460,"Instance projection source failed");u64 cylinder=g_doc.features.back().id;
            V node=V::makeObj(),placement=V::makeObj(),pattern=V::makeObj();node.set("op",V::makeStr("assembly.add"));node.set("name",V::makeStr("Patterned cylinder"));node.set("feature",V::makeNum((double)cylinder));placement.set("tx",V::makeNum(120));placement.set("ty",V::makeNum(30));placement.set("tz",V::makeNum(25));node.set("p",placement);pattern.set("type",V::makeStr("linear"));pattern.set("count",V::makeStr("3"));pattern.set("dx",V::makeStr("100"));node.set("pattern",pattern);applyCommand(node);if(g_error)return failure(461,"Patterned instance fixture failed");g_toolModule=2;g_picked.clear();uiAction(171);uiAction(180);frameScene();draw();picked=false;u64 selectedInstance=0;
            for(size_t body=0;body<g_bodyIds.size()&&!picked;++body)if(g_bodyIds[body]==cylinder&&g_instanceCopies[body]==2){selectedInstance=g_instanceIds[body];size_t part=g_instancePart[body];const auto& pick=g_partPicks[part];const auto& mesh=g_cpuMeshes[part];for(size_t face=0;face<pick.faces.size()&&!picked;++face)if(pick.faceNames[face]=="cap.end")for(int triangle=pick.faces[face].first;triangle<pick.faces[face].first+pick.faces[face].second&&!picked;++triangle){Vec3 point{};for(int vertex=0;vertex<3;++vertex){size_t index=(size_t)mesh.idx[(size_t)triangle*3+vertex]*3;point+=Vec3{mesh.pos[index],mesh.pos[index+1],mesh.pos[index+2]};}float x,y;if(!projectScreen(g_instanceWorld[body].transformPoint(point/3.),x,y)||overViewCube(x,y))continue;int sx=(int)std::lround(x),sy=(int)std::lround(y);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(sx,sy));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(sx,sy));picked=g_picked.size()==1&&g_picked.front().kind=="face"&&g_picked.front().name=="cap.end"&&g_picked.front().instance==selectedInstance&&g_picked.front().copy==2;}}

            if(!picked)return failure(462,"Patterned copy selection lost its concrete instance/copy");g_dialogOverrideQueue={{"Instance projection","XY","Copied circle"}};g_dialogSmoke=3;clicked=click(536);g_dialogSmoke=0;if(!clicked||g_error||!g_projectionPreview.active||g_projectionPreview.bounds.center().x<315||g_projectionPreview.bounds.center().x>325)return failure(463,"Instance projection used definition or wrong-copy coordinates");if(!capture(".project-instance-copy-preview.png"))return failure(464,"Could not capture instance-copy projection preview");if(!click(539)||g_error)return failure(465,"Instance projection Apply failed");projected=g_doc.findSketch("Instance projection");if(!projected||projected->sk.ents.front().sourceInstance!=selectedInstance||projected->sk.ents.front().sourceCopy!=2)return failure(466,"Instance binding was lost on Apply");V move=V::makeObj();move.set("op",V::makeStr("assembly.edit"));move.set("id",V::makeNum((double)selectedInstance));placement.set("tx",V::makeNum(140));move.set("p",placement);applyCommand(move);if(g_error||g_sketchBounds.center().x<335||g_sketchBounds.center().x>345)return failure(467,"Projection did not follow an edited instance placement");
            g_status="Projection UI checks passed / UI-created picked circle / exact preview / source edits / spatial NURBS / XYZ / instance copy / stale-write rejection";
        }
    }

    if(sheetShowcaseTest){
        g_showViewCube=true;
        using V=arkjson::Value;V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);if(g_error)return 230;
        g_dialogSmoke=3;interactiveAction(210);g_dialogSmoke=0;
        if(g_error||g_doc.features.size()!=1||g_doc.features.front().type!=FeatureType::ConstructionFrame)return 231;
        V args=V::makeObj(),commands=V::makeArr();
        auto sheet=[&](const char* name,u64 featureId,double length,double width,const char* edge){V create=V::makeObj(),p=V::makeObj(),base=V::makeObj();
            create.set("op",V::makeStr("sheet.create"));create.set("name",V::makeStr(name));p.set("width",V::makeNum(width));p.set("thickness",V::makeNum(2));p.set("k_factor",V::makeNum(.42));create.set("p",std::move(p));base.set("id",V::makeStr("base"));base.set("length",V::makeNum(length));create.set("base",std::move(base));commands.arr.push_back(std::move(create));
            V flange=V::makeObj(),spec=V::makeObj();flange.set("op",V::makeStr("sheet.flange.add"));flange.set("id",V::makeNum((double)featureId));
            spec.set("id",V::makeStr("return"));spec.set("parent",V::makeStr("base"));spec.set("edge",V::makeStr(edge));spec.set("length",V::makeNum(28));spec.set("inner_radius",V::makeNum(3));spec.set("angle",V::makeNum(90));flange.set("flange",std::move(spec));commands.arr.push_back(std::move(flange));};
        sheet("Port panel",2,110,66,"u_max");sheet("Starboard panel",3,95,60,"v_max");
        V root=V::makeObj();root.set("op",V::makeStr("assembly.add"));root.set("name",V::makeStr("Sheet showcase"));commands.arr.push_back(std::move(root));
        for(const char* name:{"Chassis","Port panel","Starboard panel"}){V node=V::makeObj();node.set("op",V::makeStr("assembly.add"));node.set("name",V::makeStr(name));node.set("parent",V::makeNum(1));node.set("feature",V::makeStr(name));commands.arr.push_back(std::move(node));}
        args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",std::move(commands));runTool("doc_apply",args);
        if(g_error||g_doc.features.size()!=3||g_bodyIds.size()!=3||visibleSheetIds().size()!=2)return 232;
        auto port=std::find(g_bodyIds.begin(),g_bodyIds.end(),(u64)2),starboard=std::find(g_bodyIds.begin(),g_bodyIds.end(),(u64)3);
        if(port==g_bodyIds.end()||starboard==g_bodyIds.end())return 243;
        int portIndex=(int)std::distance(g_bodyIds.begin(),port),starboardIndex=(int)std::distance(g_bodyIds.begin(),starboard);
        g_dialogSmoke=3;selBody=portIndex;g_dialogOverrideQueue.push_back({"slot"});interactiveAction(124);
        if(g_error)return 244;
        selBody=starboardIndex;g_dialogOverrideQueue.push_back({"window"});interactiveAction(124);
        if(g_error)return 245;
        selBody=portIndex;g_dialogOverrideQueue.push_back({"circle"});g_dialogOverrideQueue.push_back({"base","55","45","4","2","28","0"});interactiveAction(124);g_dialogSmoke=0;
        if(g_error||!g_dialogOverrideQueue.empty())return 246;
        auto* portOpenings=g_doc.find(2)->nativeSpec.find("openings"),*starboardOpenings=g_doc.find(3)->nativeSpec.find("openings");
        if(!portOpenings||portOpenings->arr.size()!=3||!starboardOpenings||starboardOpenings->arr.size()!=1)return 247;
        u64 exportRevision=g_session.revision;exportFlatDxf(shotPath+".sheet-flat.dxf");
        if(g_error||g_session.revision!=exportRevision||!std::filesystem::is_regular_file(shotPath+".sheet-flat.dxf"))return 248;
        u64 revision=g_session.revision;interactiveAction(304);g_swap=false;HDC testDc=GetDC(g_window);drawFrame(testDc);ReleaseDC(g_window,testDc);
        if(g_cubeTriangles.size()!=44||cubeHitAt(g_cubeX+2,g_cubeY+2).hit)return 233;
        Vec3 cubeEye;Mat4 cubeView,cubeProjection;cubeCamera(cubeEye,cubeView,cubeProjection);Mat4 cubeVp=cubeProjection*cubeView;
        std::set<int> seenFaces;bool hitBevel=false;
        for(const CubeTriangle& triangle:g_cubeTriangles){Vec3 p=(triangle.a+triangle.b+triangle.c)*(1./3.);double cw=cubeVp.m[3]*p.x+cubeVp.m[7]*p.y+cubeVp.m[11]*p.z+cubeVp.m[15];if(cw<=0)continue;Vec3 clip=cubeVp.transformPoint(p);
            int sx=g_cubeX+(int)std::lround((clip.x/cw+1.)*.5*g_cubeS),sy=g_cubeY+(int)std::lround((1.-clip.y/cw)*.5*g_cubeS);auto hit=cubeHitAt(sx,sy);
            if(hit.hit&&hit.face>=0)seenFaces.insert(hit.face);if(hit.hit&&hit.face<0)hitBevel=true;}
        if(seenFaces.size()<2||!hitBevel)return 234;
        auto capture=[&](const std::string& suffix){target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;HDC dc=GetDC(g_window);drawFrame(dc);glFinish();ReleaseDC(g_window,dc);
            std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(&image.px[(size_t)y*winW*3],&pixels[(size_t)(winH-1-y)*winW*3],(size_t)winW*3);
            return writePNG((shotPath+suffix).c_str(),image);};
        interactiveAction(320);if(g_error||!g_allSheetsFlat||g_session.revision!=revision||displayInstance(0)||!g_sceneBounds.valid())return 235;
        g_pickKind=1;size_t referencesBefore=g_picked.size();pickReference(viewX+viewW/2,viewY+viewH/2);g_pickKind=0;
        if(g_picked.size()!=referencesBefore||g_status.find("Preview meshes")==std::string::npos||!capture(".sheet-flat.png"))return 235;
        interactiveAction(324);if(g_error||!g_flatShowChassis||!displayInstance(0)||g_session.revision!=revision||!capture(".sheet-flat-chassis.png"))return 236;
        interactiveAction(321);if(g_error||g_allSheetsFlat||g_session.revision!=revision||!capture(".sheet-formed.png"))return 237;
        interactiveAction(322);advanceSheet(10.);if(g_error||!g_allSheetsFlat||g_session.revision!=revision)return 238;
        auto found=std::find(g_bodyIds.begin(),g_bodyIds.end(),g_doc.features.front().id);if(found==g_bodyIds.end())return 239;selBody=(int)std::distance(g_bodyIds.begin(),found);
        interactiveAction(326);if(g_error||!g_stockMode||g_session.revision!=revision||!g_stockReport.find("manufacturing_qualified")||g_stockReport.find("manufacturing_qualified")->b||!capture(".stock.png"))return 240;
        interactiveAction(326);interactiveAction(320);if(g_error||g_stockMode||!g_allSheetsFlat||g_session.revision!=revision)return 241;
        g_status="Sheet UI checks passed / formed / exploded flat / sequential unbend / nominal stock / unchanged revision";
    }
    if(engineeringUiTest){
        using V=arkjson::Value;V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);
        if(g_error||!g_doc.features.empty())return 220;
        g_dialogSmoke=3;interactiveAction(301);interactiveAction(210);
        if(g_error||g_doc.features.size()!=1||g_doc.features.back().type!=FeatureType::ConstructionFrame||g_toolModule!=1)return 221;
        const u64 frameId=g_doc.features.back().id;
        interactiveAction(303);interactiveAction(211);
        if(g_error||g_doc.features.size()!=2||g_doc.features.back().type!=FeatureType::PCB||g_toolModule!=3)return 222;
        const u64 boardId=g_doc.features.back().id;
        interactiveAction(212);g_dialogSmoke=0;
        if(g_error||g_doc.features.size()!=3||g_doc.features.back().type!=FeatureType::CableRoute)return 223;
        const u64 cableId=g_doc.features.back().id;
        auto inspect=[&](const char* tool,u64 id){V arguments=V::makeObj();arguments.set("id",V::makeNum((double)id));V answer=g_session.call(tool,arguments);
            return answer.find("isError")&&answer.find("isError")->t==V::Bool&&!answer.find("isError")->b?*answer.find("structuredContent"):V::makeObj();};
        V frame=inspect("frame_inspect",frameId),board=inspect("electronics_inspect",boardId),cable=inspect("electronics_inspect",cableId);
        if(!frame.find("member_count")||frame.find("member_count")->num!=4||!board.find("drc_checked")||!board.find("drc_checked")->b||!cable.find("route_length_mm")||cable.find("route_length_mm")->num<=0)return 224;
        V save=V::makeObj();save.set("path",V::makeStr(shotPath+".engineering.ark"));runTool("doc_save",save);
        if(g_error||!std::filesystem::is_regular_file(shotPath+".engineering.ark"))return 225;
        selBody=-1;g_toolModule=3;frameScene();g_status="Engineering UI checks passed / exact frame / PCB DRC / cable route / save";
    }
    if(studioRenderTest){int result=studioRenderSelfTest(hdc,shotPath);if(result)return result;}
    if(renderUiTest){
        using V=arkjson::Value;
        {HMENU probe=CreatePopupMenu();AppendMenuA(probe,MF_STRING,16,"Save");AppendMenuA(probe,MF_STRING,63,"Spline");decorateMenu(probe);for(int i=0;i<2;++i){MENUITEMINFOW item{sizeof item};item.fMask=MIIM_BITMAP;if(!GetMenuItemInfoW(probe,(UINT)i,TRUE,&item)||!item.hbmpItem){DestroyMenu(probe);return 247;}}DestroyMenu(probe);}
        if(g_doc.usage.entries.empty()&&tokenBadgeLabel()!="TOKENS N/A")return 227;
        if(g_bodyIds.empty())return 201;
        selBody=0;g_showDatumPlanes=false;g_showSketches=false;rebuildSketchOverlay();
        g_dialogSmoke=3;g_dialogOverrides={"Aluminum"};uiAction(187);g_dialogSmoke=0;
        if(g_error||g_doc.find(g_bodyIds[0])->material!="Aluminum")return 202;
        auto nominal=std::find_if(g_doc.materials.begin(),g_doc.materials.end(),[](const Material& m){return m.name=="Aluminum";});
        if(nominal==g_doc.materials.end()||std::abs(nominal->densityGcm3-2.7)>1e-9)return 203;
        Image checker;checker.w=32;checker.h=32;checker.px.resize(32*32*3);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){bool light=((x/4+y/4)&1)==0;size_t offset=(size_t)(y*32+x)*3;checker.px[offset]=light?240:36;checker.px[offset+1]=light?220:45;checker.px[offset+2]=light?185:175;}
        auto textureFile=std::filesystem::absolute(std::filesystem::u8path(shotPath+".texture-source.png"));
        auto textureUtf8=textureFile.u8string();std::string texturePath(textureUtf8.begin(),textureUtf8.end());
        if(!writePNG(texturePath.c_str(),checker)||!materialTexture(texturePath))return 204;
        auto setLook=[&](double roughness,const std::string& texture){
            V args=V::makeObj(),commands=V::makeArr(),command=V::makeObj(),ap=V::makeObj(),rgb=V::makeArr(),scale=V::makeArr();
            for(double value:{.72,.50,.36})rgb.arr.push_back(V::makeNum(value));for(double value:{3.,3.})scale.arr.push_back(V::makeNum(value));
            ap.set("base_color",std::move(rgb));ap.set("metallic",V::makeNum(.48));ap.set("roughness",V::makeNum(roughness));ap.set("opacity",V::makeNum(1.));ap.set("texture_path",V::makeStr(texture));ap.set("texture_scale",std::move(scale));
            command.set("op",V::makeStr("material.set"));command.set("name",V::makeStr("Aluminum"));command.set("density",V::makeNum(2.7));command.set("appearance",std::move(ap));commands.arr.push_back(std::move(command));
            args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",std::move(commands));runTool("doc_apply",args);return !g_error;
        };
        auto capture=[&](const std::string& suffix){
            g_swap=false;drawFrame(hdc);glFinish();if(glGetError()!=GL_NO_ERROR)return std::uint64_t{0};
            std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());
            for(int y=0;y<winH;++y)std::memcpy(&image.px[(size_t)y*winW*3],&pixels[(size_t)(winH-1-y)*winW*3],(size_t)winW*3);
            if(!writePNG((shotPath+suffix).c_str(),image))return std::uint64_t{0};
            std::uint64_t digest=1469598103934665603ull;
            // Keep the measured model region visible when the open chat reduces viewport height.
            const int modelTop=viewY+std::min(190,viewH/4);
            for(int y=modelTop;y<viewY+viewH-48;++y)for(int x=viewX+128;x<viewX+viewW-276;++x)for(int c=0;c<3;++c){digest^=image.px[((size_t)y*winW+x)*3+c];digest*=1099511628211ull;}
            return digest;
        };
        target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_occlusionMode=0;
        if(!setLook(.23,""))return 205;auto plain=capture(".plain.png");
        if(!setLook(.23,texturePath))return 206;auto textured=capture(".textured.png");
        if(!setLook(.85,texturePath))return 207;auto rough=capture(".rough.png");
        if(!plain||!textured||!rough||plain==textured||textured==rough)return 208;
        auto aoOff=capture(".ao-off.png");g_occlusionMode=1;auto contact=capture(".ao-contact.png");g_occlusionMode=2;auto ambient=capture(".ao-ambient.png");g_occlusionMode=3;auto horizon=capture(".ao-horizon.png");
        std::set<std::uint64_t> modes{aoOff,contact,ambient,horizon};
        if(!aoOff||!contact||!ambient||!horizon||modes.size()!=4)return 209;
        V batch=V::makeObj(),commands=V::makeArr(),glass=V::makeObj(),glassAppearance=V::makeObj(),glassColor=V::makeArr(),probe=V::makeObj(),parameters=V::makeObj();
        for(double c:{.68,.82,.94})glassColor.arr.push_back(V::makeNum(c));glassAppearance.set("base_color",std::move(glassColor));glassAppearance.set("roughness",V::makeNum(.12));glassAppearance.set("metallic",V::makeNum(.05));glassAppearance.set("opacity",V::makeNum(.38));
        glass.set("op",V::makeStr("material.set"));glass.set("name",V::makeStr("Glass probe"));glass.set("density",V::makeNum(2.5));glass.set("appearance",std::move(glassAppearance));commands.arr.push_back(std::move(glass));
        parameters.set("r",V::makeStr("18"));probe.set("op",V::makeStr("feature.add"));probe.set("type",V::makeStr("sphere"));probe.set("name",V::makeStr("Transparency probe"));probe.set("p",std::move(parameters));probe.set("material",V::makeStr("Glass probe"));commands.arr.push_back(std::move(probe));
        batch.set("expected_revision",V::makeNum((double)g_session.revision));batch.set("commands",std::move(commands));runTool("doc_apply",batch);
        if(g_error||g_bodyIds.size()<2)return 210;
        auto transparency=capture(".transparent.png");if(!transparency||transparency==horizon)return 211;
        if(g_doc.assembly.empty()){
            V arguments=V::makeObj(),features=V::makeArr(),solid=V::makeObj(),dimensions=V::makeObj();
            dimensions.set("op",V::makeStr("box"));dimensions.set("width",V::makeStr("10"));dimensions.set("depth",V::makeStr("10"));dimensions.set("height",V::makeStr("10"));
            solid.set("op",V::makeStr("feature.add"));solid.set("type",V::makeStr("native_solid"));solid.set("name",V::makeStr("Physical probe"));solid.set("p",std::move(dimensions));solid.set("material",V::makeStr("Aluminum"));features.arr.push_back(std::move(solid));
            arguments.set("expected_revision",V::makeNum((double)g_session.revision));arguments.set("commands",std::move(features));runTool("doc_apply",arguments);
            if(g_error)return 212;u64 physicalId=g_doc.features.back().id;
            auto selected=std::find(g_bodyIds.begin(),g_bodyIds.end(),physicalId);if(selected==g_bodyIds.end())return 213;
            selBody=(int)std::distance(g_bodyIds.begin(),selected);uiAction(188);
            const auto* mass=g_physicalReport.find("mass_g");const auto* center=g_physicalReport.find("center_mm");const auto* weight=g_physicalReport.find("weight_N");
            if(!g_physicalVisible||!mass||!weight||!center||center->t!=V::Arr||center->arr.size()!=3||std::abs(mass->num-2.7)>1e-5||std::abs(weight->num-2.7*.00980665)>1e-6)return 214;
            g_toolModule=5;if(!capture(".physics.png"))return 215;
            V plan=V::makeObj(),edits=V::makeArr(),edit=V::makeObj(),previewDimensions=V::makeObj();plan.set("expected_revision",V::makeNum((double)g_session.revision));edit.set("op",V::makeStr("feature.edit"));edit.set("id",V::makeNum((double)g_bodyIds[(size_t)selBody]));previewDimensions.set("width",V::makeNum(20));edit.set("p",previewDimensions);edits.arr.push_back(edit);plan.set("commands",edits);
            auto preview=runTool("doc_preview",plan);if(g_error||!preview.find("structuredContent")->find("valid")->b)return 248;
            auto committed=runTool("doc_apply",*preview.find("structuredContent")->find("apply"));if(g_error||!committed.find("structuredContent")->find("applied_prepared")->b)return 249;
            const auto displayed=g_cpuMeshes[g_instancePart[(size_t)selBody]].computeBBox();if(std::abs(displayed.mx.x-displayed.mn.x-20)>1e-5)return 250;
        }
        {
            bool queuedCompleted=false;auto queued=std::make_unique<UiToolRequest>();queued->tool="doc_info";queued->args=V::makeObj();queued->complete=[&](V){queuedCompleted=true;};
            if(!PostMessage(g_window,WM_ARK_TOOL,0,(LPARAM)queued.get()))return 216;queued.release();
            PostMessage(g_window,WM_SIZE,0,MAKELPARAM(winW,winH));
            g_assistantReceiptId=newUsageReceiptId();g_assistantReceiptModel="offline-ui-smoke";
            AssistantUsage injected;injected.inputTokens=9;injected.outputTokens=3;injected.totalTokens=12;injected.reportedResponses=1;
            postChat({"",false,false,{},g_documentEpoch,injected,true});
            auto oldPrices=g_pricing;bool oldPricesReady=g_pricingReady;auto* arrivedPrices=new PricingCatalog;ModelTokenPrice fixturePrice;fixturePrice.model="offline-ui-smoke";fixturePrice.inputPerMillion=1;fixturePrice.cachedInputPerMillion=.1;fixturePrice.outputPerMillion=2;arrivedPrices->models.push_back(fixturePrice);g_pricingBusy=true;PostMessage(g_window,WM_ARK_PRICE,0,(LPARAM)arrivedPrices);
            unsigned framesBefore=g_busyFrameCount,pumpedBefore=g_busyPumpCount,deferredBefore=g_deferredUsageCount;u64 revisionBefore=g_session.revision;
            g_busyTestMode=true;g_busyTestDelayMs=800;g_busyTestShot=shotPath+".loading.png";
            V args=V::makeObj(),updates=V::makeArr(),material=V::makeObj();material.set("op",V::makeStr("material.set"));material.set("name",V::makeStr("Aluminum"));material.set("density",V::makeNum(2.7));updates.arr.push_back(std::move(material));V geometryProbe=V::makeObj();geometryProbe.set("op",V::makeStr("param.set"));geometryProbe.set("name",V::makeStr("busy_geometry_probe"));geometryProbe.set("expr",V::makeNum(1));updates.arr.push_back(std::move(geometryProbe));
            args.set("expected_revision",V::makeNum((double)revisionBefore));args.set("commands",std::move(updates));runTool("doc_apply",args);
            g_busyTestMode=false;g_busyTestDelayMs=0;g_busyTestShot.clear();
            if(g_pricingBusy||!g_pricingReady||g_pricing.models.empty())return 390;g_pricing=oldPrices;g_pricingReady=oldPricesReady;updateCostDisplay(true);
            if(g_error||g_session.revision!=revisionBefore+1||queuedCompleted||g_toolQueue.empty()||g_busyFrameCount<framesBefore+2||g_busyPumpCount<pumpedBefore+3||!std::filesystem::is_regular_file(shotPath+".loading.png"))return 217;
            if(g_deferredUsageCount<=deferredBefore||g_doc.usage.entries.size()!=1||g_assistantUsageDocument.totalTokens!=12||g_doc.usage.entries.front().id!=g_assistantReceiptId||tokenBadgeLabel()!="TOKENS 12")return 219;
            u64 serial=g_doc.usage.serial;recordAssistantUsageSnapshot(injected);if(g_doc.usage.serial!=serial)return 226;
            drainUiTools();if(!queuedCompleted||g_session.revision!=revisionBefore+1)return 218;
        }
        {
            V saved=V::makeObj();saved.set("path",V::makeStr(shotPath+".usage.ark"));runTool("doc_save",saved);
            if(g_error||g_savedUsageSerial!=g_doc.usage.serial)return 249;
            u64 diskSerial=g_savedUsageSerial;UsageReceipt continued;continued.id=newUsageReceiptId();continued.source="studio_assistant";
            continued.provenance="provider_response";continued.model="offline-ui-smoke";continued.inputTokens=2;continued.outputTokens=1;
            continued.totalTokens=3;continued.reportedResponses=1;g_currentUsageReceipt=continued;
            g_ownAssistantTool=true;runTool("doc_load",saved);g_ownAssistantTool=false;
            if(g_error||g_savedUsageSerial!=diskSerial||g_doc.usage.serial==diskSerial||g_doc.usage.entries.size()!=2||g_assistantUsageDocument.totalTokens!=15)return 250;
        }
        g_status="Render UI checks passed / material / texture / roughness / occlusion / hidden datums";
    }
#include "sketch_interaction_selftest.inl"
#include "sketch_edit_selftest.inl"
#include "direct_authoring_selftest.inl"
#include "specialized_pattern_selftest.inl"
#include "advanced_sheetmetal_selftest.inl"
#include "native_vertex_pull_selftest.inl"
#include "cad_import_selftest.inl"
#include "stamps_selftest.inl"
#include "planes_sections_selftest.inl"
#include "holes_orchestrator_selftest.inl"
#include "design_container_selftest.inl"
#include "selection_glow_selftest.inl"
#include "selection_picking_selftest.inl"
#include "image_attachment_selftest.inl"
#include "steer_024b_selftest.inl"
    if(skeletonEditorUiTest){int result=runSkeletonEditorSelfTest(shotPath);if(result)return result;}
#include "datum_authoring_selftest.inl"
#include "typed_sketch_selftest.inl"
    if(treeUiTest||arcUiTest||completionUiTest){
        using V=arkjson::Value;auto failure=[&](int code,const char* detail){std::fprintf(stderr,"005B UI %d: %s / %s\n",code,detail,g_status.c_str());return code;};
        auto draw=[&]{g_swap=false;drawFrame(hdc);glFinish();};
        auto capture=[&](const std::string& suffix){draw();std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image shot;shot.w=winW;shot.h=winH;shot.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(shot.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);return writePNG((shotPath+suffix).c_str(),shot);};
        auto click=[&](int id){draw();auto zone=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});if(zone==g_zones.end())return false;int x=(int)(zone->x+zone->w*.5f),y=(int)(zone->y+zone->h*.5f);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
        V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);if(g_error)return failure(520,"Could not create isolated fixture");g_assistantDockOpen=false;g_sidebarTab=0;g_toolModule=2;g_activePlane="XY";g_activeSketch.clear();g_showSketches=true;layout();
        if(treeUiTest){
            V commands=V::makeArr();for(int i=0;i<56;++i){V command,sketch;arkjson::parse(R"({"plane":"XY","entities":[{"kind":"line","name":"edge","dof":[0,0,10,0]}],"constraints":[]})",sketch);command=V::makeObj();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr("Tree sketch "+std::to_string(i+1)));command.set("sketch",sketch);commands.arr.push_back(command);}args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);if(g_error)return failure(521,"Tree sketch fixture rejected");g_activeSketch.clear();g_treeSelectedZone=-1;g_treeScroll=0;draw();if(g_treeMaxScroll<20||g_treeThumb.bottom<=g_treeThumb.top||!capture(".tree-top.png"))return failure(522,"Overflow tree has no useful thumb");u64 unchanged=g_session.revision;
            int x=(g_treeThumb.left+g_treeThumb.right)/2,y=g_treeThumb.top+8;SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,g_treeTrack.bottom+200));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,g_treeTrack.bottom+200));draw();if(g_treeScroll!=g_treeMaxScroll||std::none_of(g_zones.begin(),g_zones.end(),[](const Zone& z){return z.id==11055;})||!capture(".tree-bottom.png"))return failure(523,"Thumb drag cannot reach the last real tree row");
            for(const auto& zone:g_zones)if(zone.id>=11000&&zone.id<12000)if(zone.y<g_treeViewport.top||zone.y+zone.h>g_treeViewport.bottom||zone.x+zone.w>g_treeTrack.left)return failure(524,"Tree hit zone escapes its clipped viewport or touches the scroll track");
            int before=g_treeScroll;y=g_treeTrack.top+2;SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));draw();if(g_treeScroll!=std::max(0,before-g_treeVisibleRows))return failure(525,"Track click did not page the tree");POINT wheel{24,g_treeViewport.top+18};ClientToScreen(g_window,&wheel);before=g_treeScroll;SendMessage(g_window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(wheel.x,wheel.y));draw();if(g_treeScroll!=std::max(0,before-3))return failure(526,"Wheel did not scroll tree content");
            g_activeSketch="Tree sketch 56";draw();if(std::none_of(g_zones.begin(),g_zones.end(),[](const Zone& z){return z.id==11055;}))return failure(527,"New selected sketch was not revealed");int savedH=winH;winH+=280;layout();draw();if(g_treeScroll>g_treeMaxScroll)return failure(528,"Resize failed to clamp the tree end");winH=savedH;layout();g_explorerOpen[2]=false;draw();if(g_treeMaxScroll!=0||g_treeScroll!=0||g_treeThumb.bottom)return failure(529,"Collapsed tree retained an invalid scroll thumb");g_explorerOpen[2]=true;g_activeSketch.clear();g_treeScroll=0;draw();if(g_session.revision!=unchanged)return failure(530,"Scrolling changed the drawing");g_status="Tree UI checks passed / thumb / page / wheel / selected row / resize / collapse / clipped hit zones";
        }
        if(completionUiTest){
            AgentTeamConfig config;config.focusedWorkflow=true;int round=0;AgentTeamRole lead;lead.id="engineer";lead.label="Lead engineer";lead.discipline=AgentTeamDiscipline::Orchestrator;lead.assistant.endpoint="http://127.0.0.1:1/v1/responses";lead.assistant.model="offline-completion-ui";lead.assistant.apiKeyEnv="";
            lead.transport=[&](auto&,auto&,auto*){V response=V::makeObj(),output=V::makeArr(),item=V::makeObj();response.set("status",V::makeStr("completed"));if(++round==2||round==4){item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr("goal-report-"+std::to_string(round)));item.set("name",V::makeStr("team_finish_goal"));V report=V::makeObj();report.set("state",V::makeStr(round==2?"completed":"needs_input"));report.set("summary",V::makeStr(round==2?"Read-only concept inspection finished; geometry and manufacture are not certified.":"Choose a material to continue the next design."));item.set("arguments",V::makeStr(report.dump()));}else {item.set("type",V::makeStr("message"));item.set("role",V::makeStr("assistant"));V content=V::makeArr(),text=V::makeObj();text.set("type",V::makeStr("output_text"));text.set("text",V::makeStr("Provider response finished; see the explicit goal report."));content.arr.push_back(text);item.set("content",content);}output.arr.push_back(item);response.set("output",output);return response;};config.roles.push_back(lead);
            g_teamMode=true;g_teamStarted=true;g_teamEpoch=g_documentEpoch;g_teamGoal="Inspect a concept in the current drawing";g_teamEventSequence=0;g_teamReceipts={};g_teamActivity={};for(auto& id:g_teamReceiptIds)id=newUsageReceiptId();g_assistantDockOpen=true;g_agentTeam.start(config,g_teamGoal,V::makeArr(),{});if(!g_agentTeam.waitIdleFor(std::chrono::seconds(5)))return failure(537,"Completion UI probe did not finish");pollTeam(true);layout();if(g_teamSnapshot.goalDeclared||g_teamSnapshot.goalState==AgentGoalState::Completed||!capture(".team-unreported.png"))return failure(538,"Idle was shown as completed");
            if(!g_agentTeam.steer("Report the inspected goal explicitly")||!g_agentTeam.waitIdleFor(std::chrono::seconds(5)))return failure(539,"Explicit completion UI report did not finish");pollTeam(true);if(!g_teamSnapshot.goalDeclared||g_teamSnapshot.goalState!=AgentGoalState::Completed||!capture(".team-completed.png"))return failure(540,"Explicit completion summary was not rendered");
            if(!g_agentTeam.steer("Next goal needs a material choice")||!g_agentTeam.waitIdleFor(std::chrono::seconds(5)))return failure(541,"Next user goal probe did not finish");pollTeam(true);if(g_teamSnapshot.goalState!=AgentGoalState::NeedsInput||!capture(".team-needs-input.png"))return failure(542,"Next goal reused earlier completion");g_agentTeam.cancel();g_agentTeam.waitIdle();pollTeam(true);if(g_teamSnapshot.goalState!=AgentGoalState::Interrupted||!capture(".team-interrupted.png"))return failure(543,"Cancellation was rendered as completion");
            g_teamStarted=false;g_teamMode=false;g_assistantResponseFinished=true;g_assistantResponseError=false;layout();if(!capture(".single-response-finished.png"))return failure(544,"Single response finish status was not captured");g_status="Completion UI checks passed / idle unreported / explicit lead summary / next goal / needs input / interruption / single response review";
        }
        if(arcUiTest){
            g_dialogOverrideQueue={{"Curves","XY","0","0","60","0"}};g_dialogSmoke=3;g_dialogShot=shotPath+".line-dialog.png";bool clicked=click(543);g_dialogSmoke=0;if(!clicked||g_error||!g_doc.findSketch("Curves"))return failure(531,"Line ribbon dispatch failed");
            g_dialogOverrideQueue={{"Curves","XY","0","0","30","30","60","0"}};g_dialogSmoke=3;g_dialogShot=shotPath+".arc-three-point-dialog.png";clicked=click(544);g_dialogSmoke=0;if(!clicked||g_error||g_doc.findSketch("Curves")->sk.ents.size()!=2||g_doc.findSketch("Curves")->sk.ents.back().arcRecipe.isNull())return failure(532,"Three point arc ribbon lost its recipe");
            g_dialogOverrideQueue={{"arc2"},{"0","0","30","45","60","0"}};g_dialogSmoke=3;uiAction(70);g_dialogSmoke=0;if(g_error||sketchToJson(g_doc.findSketch("Curves")->sk).find("45")==std::string::npos)return failure(533,"Authored arc cannot be edited through coordinate fields");
            g_dialogOverrideQueue={{"Curves","XY","0","0","0","1","60","0"}};g_dialogSmoke=3;g_dialogShot=shotPath+".arc-tangent-dialog.png";clicked=click(545);g_dialogSmoke=0;if(!clicked||g_error||g_doc.findSketch("Curves")->sk.ents.size()!=3||g_sketchPoints.size()!=18||!capture(".arcs-2d.png"))return failure(534,"Tangent arc ribbon failed");
            g_dialogOverrideQueue={{"Spatial three point","0","0","0","30","30","15","60","0","30"}};g_dialogSmoke=3;g_dialogShot=shotPath+".spatial-three-point-dialog.png";clicked=click(547);g_dialogSmoke=0;if(!clicked||g_error||g_doc.features.back().nativeSpec.find("segments")->arr[0].find("mode")->asStr()!="three_point")return failure(535,"3D arc scalar UI failed");
            g_dialogOverrideQueue={{"Spatial tangent","0","0","0","0","1","0","60","0","30"}};g_dialogSmoke=3;g_dialogShot=shotPath+".spatial-tangent-dialog.png";clicked=click(548);g_dialogSmoke=0;if(!clicked||g_error||g_doc.features.back().nativeSpec.find("segments")->arr[0].find("mode")->asStr()!="tangent"||!capture(".arcs-3d.png"))return failure(536,"3D tangent scalar UI failed");g_status="Arc UI checks passed / ribbon mouse dispatch / 2D line / three point / tangent / recipe edit / exact XYZ forms";
        }
    }
    if(sketchTest){
        using V=arkjson::Value;g_savedRevision=g_session.revision;uiAction(55);if(g_error||!g_doc.features.empty())return 100;
        g_dialogSmoke=3;g_dialogShot=shotPath+".plane.png";uiAction(60);if(g_error||g_doc.planes.size()!=1)return 101;
        uiAction(67);if(g_error||g_doc.params.empty())return 102;
        g_dialogOverrides={"Profile","Workplane","0","0","width","60"};g_dialogShot=shotPath+".rectangle.png";uiAction(61);if(g_error||!g_doc.findSketch("Profile"))return 103;
        g_dialogShot=shotPath+".extrude.png";uiAction(65);if(g_error||g_bodyIds.empty())return 104;
        g_dialogSmoke=3;uiAction(161);if(g_error||g_doc.find(g_bodyIds[(size_t)selBody])->get("height").empty())return 116;
        uiAction(62);if(g_error||!g_doc.findSketch("Holes"))return 105;
        uiAction(66);if(g_error||g_doc.features.size()!=2)return 106;
        g_dialogOverrides={"width","140"};uiAction(67);if(g_error||g_doc.getParam("width")!=140)return 107;
        g_activeSketch="Profile";uiAction(64);if(g_error||sketchToJson(g_doc.findSketch("Profile")->sk).find("width")==std::string::npos)return 108;
        g_dialogOverrides={"Freeform","Workplane","max(-40,-30),0; 0,-25; 30,0; 0,25","1","0","0.05"};g_dialogShot=shotPath+".spline.png";uiAction(63);if(g_error||!g_doc.findSketch("Freeform")||g_sketchPoints.empty()||std::abs(g_doc.findSketch("Freeform")->sk.ents[0].dof[0]+30)>1e-9)return 109;
        g_dialogOverrides={"fixedX","spline1","0","spline1","0","-30","0"};g_dialogShot=shotPath+".constraint.png";uiAction(73);if(g_error||g_doc.findSketch("Freeform")->sk.cons.size()!=1||g_selectedSketchConstraint!=0)return 112;
        uiAction(12000);uiAction(90);if(g_error||!g_doc.findSketch("Freeform")->sk.cons.empty())return 113;
        uiAction(70);if(g_error||g_doc.findSketch("Freeform")->sk.ents[0].expressions[0]!="max(-40,-30)")return 114;
        auto beforeSketch=sketchToJson(g_doc.findSketch("Freeform")->sk);auto beforeRevision=g_session.revision;
        V conflicting=sketchEditValue(*g_doc.findSketch("Freeform")),badConstraints=V::makeArr(),bad=V::makeObj();
        bad.set("id",V::makeStr("conflict"));bad.set("type",V::makeStr("fixedX"));bad.set("a",V::makeNum(0));bad.set("val",V::makeStr("999"));badConstraints.arr.push_back(bad);conflicting.set("constraints",badConstraints);
        applySketchEdit("Freeform",conflicting);
        if(!g_error||g_status.find("conflict")==std::string::npos||g_session.revision!=beforeRevision||sketchToJson(g_doc.findSketch("Freeform")->sk)!=beforeSketch)return 115;
        g_error=false;
        g_dialogShot=shotPath+".graphics.png";uiAction(56);g_dialogSmoke=0;
        V args=V::makeObj();args.set("path",V::makeStr(shotPath+".ark"));runTool("doc_save",args);runTool("doc_load",args);if(g_error||!g_doc.findSketch("Freeform"))return 110;
        g_activeSketch="Freeform";g_showSketches=true;rebuildSketchOverlay();frameScene();g_status="Sketch UI checks passed / plane / expressions / extrude / cut / parameter / spline / save";
    }
    if(nativeUiTest){
        g_savedRevision=g_session.revision;uiAction(55);if(g_error||!g_doc.features.empty())return 180;
        g_dialogSmoke=3;uiAction(80);g_dialogSmoke=0;
        if(g_error||g_doc.features.size()!=1||g_doc.features.back().type!=FeatureType::SheetMetal||!selectedSheet())return 181;
        auto* native=selectedNativeBody();if(!native||!native->nativeData.find("flat_mesh")||!native->nativeData.find("report"))return 182;
        auto formedExtent=g_reports[g_instancePart[selBody]].bounds.extent();
        auto revision=g_session.revision;uiAction(84);
        if(g_error||!g_sheetFlatViews[g_doc.features.back().id]||g_session.revision!=revision)return 183;
        auto flatExtent=g_reports[g_instancePart[selBody]].bounds.extent();
        if(std::abs(flatExtent.x-formedExtent.x)+std::abs(flatExtent.y-formedExtent.y)+std::abs(flatExtent.z-formedExtent.z)<1.)return 191;
        uiAction(83);if(g_error||g_sheetFlatViews[g_doc.features.back().id]||g_session.revision!=revision)return 184;
        exportNativeStep(shotPath+".flat.step");
        if(g_error||!std::filesystem::is_regular_file(shotPath+".flat.step"))return 185;
        g_savedRevision=g_session.revision;g_dialogSmoke=3;importSheetStep(shotPath+".flat.step");g_dialogSmoke=0;
        if(g_error||g_doc.features.size()!=1||g_doc.features.back().type!=FeatureType::SourceSheet||!selectedSheet())return 187;
        auto* imported=selectedNativeBody();auto* sourceReport=imported?imported->nativeData.find("report"):nullptr;
        if(!sourceReport||!sourceReport->find("source_projection_verified")||!sourceReport->find("source_projection_verified")->b)return 188;
        revision=g_session.revision;uiAction(84);if(g_error||!g_sheetFlatViews[g_doc.features.back().id]||g_session.revision!=revision)return 189;
        uiAction(83);if(g_error||g_sheetFlatViews[g_doc.features.back().id]||g_session.revision!=revision)return 190;
        g_dialogSmoke=3;uiAction(78);g_dialogSmoke=0;
        if(g_error||g_doc.features.size()!=2||g_doc.features.back().type!=FeatureType::BSplineSurface)return 186;
        g_dialogSmoke=3;uiAction(161);if(g_error||g_doc.features.back().type!=FeatureType::BSplineSurface)return 199;g_dialogSmoke=0;
        g_dialogSmoke=3;uiAction(100);g_dialogSmoke=0;
        if(g_error||g_doc.features.size()!=3||g_doc.features.back().type!=FeatureType::NativeSolid||!selectedNativeBody())return 192;
        g_dialogSmoke=3;uiAction(113);g_dialogSmoke=0;
        if(g_error||g_doc.planes.empty()||std::abs(resolvePlane(g_doc,"Supported plane").transformPoint({}).z-20)>1e-6)return 193;
        g_dialogSmoke=3;g_dialogShot=shotPath+".sheet-base.png";uiAction(120);
        if(g_error||g_doc.features.size()!=4||g_doc.features.back().type!=FeatureType::SheetMetal)return 194;
        auto sheetId=g_doc.features.back().id;uiAction(121);
        if(g_error||!g_doc.find(sheetId)->nativeSpec.find("flanges")||g_doc.find(sheetId)->nativeSpec.find("flanges")->arr.size()!=1)return 195;
        uiAction(124);g_dialogSmoke=0;
        if(g_error||!g_doc.find(sheetId)->nativeSpec.find("openings")||g_doc.find(sheetId)->nativeSpec.find("openings")->arr.size()!=1)return 196;
        revision=g_session.revision;uiAction(84);sheetPreviewSet(sheetId,.5);
        if(g_error||g_session.revision!=revision||std::abs(sheetDisplayProgress(sheetId)-.5)>1e-9||g_cpuMeshes[g_instancePart[selBody]].triCount()==0)return 197;
        uiAction(83);if(g_error||g_session.revision!=revision)return 198;
        selBody=0;frameScene();
        g_status="Native UI checks passed / sheet / NURBS / exact box / supported datum / sheet graph preview";
    }
    if(assistantTest){
        struct AssistantTestCleanup {~AssistantTestCleanup(){*g_assistantCancel=true;*g_teamCancel=true;g_agentTeam.cancel();g_agentTeam.waitIdle();if(g_assistantWorker.joinable())g_assistantWorker.join();}} assistantCleanup;
        auto assistantFailure=[&](int code){std::ostringstream evidence;evidence<<"Assistant UI selftest failed: "<<code<<"\nStatus: "<<g_status<<"\nError: "<<g_error<<"\nDocument revision: "<<g_session.revision<<"\nFeatures: "<<g_doc.features.size()<<"\nAssembly instances: "<<g_doc.assembly.size()<<"\nDisplayed bodies: "<<g_bodyIds.size()<<"\nTeam tokens: "<<g_teamSnapshot.usage.totalTokens<<"\nLibrary matches: "<<g_libraryMatches.arr.size()<<"\nCard bounds: "<<g_teamCardBoundsFailure<<"\n";std::ofstream diagnostic(shotPath+".assistant-test.log");diagnostic<<evidence.str();std::fprintf(stderr,"%s",evidence.str().c_str());return code;};
        for(auto pair:std::vector<std::pair<int,int>>{{61,62},{63,70},{120,121},{121,394},{397,66},{451,94},{452,73},{170,171},{170,172},{170,173},{171,172},{171,173},{172,173}})if(atlasIcon(pair.first)==atlasIcon(pair.second))return assistantFailure(301);
        using V=arkjson::Value;auto previous=g_assistantConfig;g_assistantConfig.endpoint="http://127.0.0.1:1/v1/responses";g_assistantConfig.model="offline-ui-smoke";g_assistantConfig.apiKeyEnv="";
        auto parse=[](const char* text){V v;arkjson::parse(text,v);return v;};auto rounds=std::make_shared<std::atomic_int>(0);
        auto startRevision=g_session.revision;
        AssistantTransport transport=[rounds,parse,startRevision](const auto&,const auto&,const auto*){
            int round=++*rounds;
            if(round==1){V response=V::makeObj(),outputs=V::makeArr(),call=V::makeObj(),args=V::makeObj();args.set("expected_revision",V::makeNum((double)startRevision));call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr("ui_new_document"));call.set("name",V::makeStr("doc_new"));call.set("arguments",V::makeStr(args.dump()));outputs.arr.push_back(call);response.set("status",V::makeStr("completed"));response.set("output",outputs);return response;}
            if(round==2)return parse(R"({"status":"completed","output":[{"type":"function_call","call_id":"ui_test_1","name":"doc_apply","arguments":"{\"commands\":[{\"op\":\"param.set\",\"name\":\"agent_ui_probe\",\"expr\":\"17\"},{\"op\":\"feature.add\",\"type\":\"box\",\"p\":{\"dx\":\"30\",\"dy\":\"20\",\"dz\":\"10\"}}]}"}]})");
            return parse(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Offline UI test created a new document and a box in the same turn."}]}]})");};

        auto unmetered=transport;
        transport=[unmetered,parse](const auto& config,const auto& request,const auto* cancel){
            V response=unmetered(config,request,cancel);
            response.set("usage",parse(R"({"input_tokens":100,"output_tokens":25,"total_tokens":125})"));return response;
        };
        auto pump=[&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(g_chatBusy&&std::chrono::steady_clock::now()<deadline){MSG event{};while(PeekMessage(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessage(&event);}drainUiTools();Sleep(2);}return !g_chatBusy;};
        sendAssistant("Offline integration test: new document, then create a box.",transport);if(!pump()||g_doc.getParam("agent_ui_probe")!=17||g_conversation.arr.empty()||g_doc.features.size()!=1||g_doc.features[0].type!=FeatureType::Box||g_path!="Untitled.ark")return assistantFailure(120);
        if(g_assistantUsageDocument.totalTokens!=375||g_assistantUsageDocument.reportedResponses!=4||!g_assistantUsageRunCounted)return assistantFailure(122);
        auto targetCalls=std::make_shared<std::atomic_int>(0);auto singleTargetSeen=std::make_shared<std::atomic_bool>(false);u64 objectRevision=g_session.revision;
        AssistantTransport targetTransport=[parse,targetCalls,singleTargetSeen,objectRevision](const auto&,const V& request,const auto*){++*targetCalls;auto text=request.dump();*singleTargetSeen=text.find("Selected object_context")!=std::string::npos&&text.find("feature:1")!=std::string::npos&&text.find("revision")!=std::string::npos&&text.find(std::to_string(objectRevision))!=std::string::npos;return parse(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Selected object inspected offline."}]}]})");};
        selBody=0;interactiveAction(565);if(*targetCalls||g_chatBusy||!g_pendingObjectTarget)return assistantFailure(612);
        sendAssistant("OFFLINE: inspect this exact object.",targetTransport);if(!pump()||!*singleTargetSeen||*targetCalls!=1||g_pendingObjectTarget||g_session.revision!=objectRevision)return assistantFailure(613);

        auto gate=std::make_shared<std::atomic_bool>(false);auto staleTransport=[gate,parse](const auto&,const auto&,const auto*){while(!*gate)std::this_thread::sleep_for(std::chrono::milliseconds(1));return parse(R"({"status":"completed","output":[{"type":"function_call","call_id":"stale_ui_test","name":"doc_apply","arguments":"{\"commands\":[{\"op\":\"param.set\",\"name\":\"stale_ui_probe\",\"expr\":\"99\"}]}"}]})");};
        sendAssistant("Offline stale-write test.",staleTransport);V command=parse(R"({"op":"param.set","name":"human_ui_probe","expr":"1"})");applyCommand(command);*gate=true;
        if(!pump()||g_conversation.arr.empty()||std::any_of(g_doc.params.begin(),g_doc.params.end(),[](auto& p){return p.name=="stale_ui_probe";}))return assistantFailure(121);
        try{assistantRequest(g_assistantConfig,g_conversation,AgentSession::tools());}catch(...){return assistantFailure(121);}
        auto savedAuthStatus=g_authStatus;auto savedAuthModels=g_authModels;u64 authRevision=g_session.revision,authUsageSerial=g_doc.usage.serial;
        g_authStatus.accounts.push_back({"oaiapp_offline_ui","offline-subject","offline@example.test"});
        auto finishFakeAuth=[&](AuthJob job,const char* error){auto* event=new UiAuthEvent;event->job=job;event->status=g_authStatus;event->error=error;SendMessage(g_window,WM_ARK_AUTH,0,(LPARAM)event);};
        g_authBusy=true;g_authCancelRequested=false;g_authCancel=std::make_shared<std::atomic_bool>(false);
        uiAction(366);if(!*g_authCancel||!g_authBusy||!g_authCancelRequested)return assistantFailure(123);
        finishFakeAuth(AuthJob::SignIn,"ChatGPT sign-in cancelled");
        if(g_authBusy||g_authCancelRequested||g_status.find("fresh attempt")==std::string::npos||g_authStatus.accounts.size()!=1||g_session.revision!=authRevision||g_doc.usage.serial!=authUsageSerial)return assistantFailure(124);
        g_authBusy=true;g_authCancelRequested=false;
        finishFakeAuth(AuthJob::SignIn,"ChatGPT browser sign-in timed out");
        if(g_authBusy||g_status.find("callback")==std::string::npos||g_status.find("fresh attempt")==std::string::npos)return assistantFailure(125);
        g_authBusy=true;g_authCancelRequested=false;g_authCancel=std::make_shared<std::atomic_bool>(false);
        uiAction(366);finishFakeAuth(AuthJob::Models,"ChatGPT model loading cancelled");
        if(g_authBusy||g_status.find("model loading cancelled")==std::string::npos||g_authStatus.accounts.size()!=1)return assistantFailure(126);
        g_authStatus=std::move(savedAuthStatus);g_authModels=std::move(savedAuthModels);
        if(!fa.adv[0xe7]||!fa.adv[0xe3]||!fa.adv[0xf5])return assistantFailure(127); // cedilla and Portuguese vowels are present in the GL chat atlas.
        g_chatInput=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE,0,0,1,1,g_window,(HMENU)499,hi,nullptr);
        if(!g_chatInput)return assistantFailure(128);
        auto chatInputFits=[&]{RECT bounds{};GetWindowRect(g_chatInput,&bounds);POINT topLeft{bounds.left,bounds.top};ScreenToClient(g_window,&topLeft);
            return topLeft.x==viewX+30&&topLeft.y==winH-112&&bounds.right-bounds.left==std::max(viewW-260,1)&&bounds.bottom-bounds.top==44;};
        layout();if(!chatInputFits())return assistantFailure(129);
        const wchar_t* accent=L"Portugu\u00eas: a\u00e7\u00e3o";SetWindowTextW(g_chatInput,accent);wchar_t entered[80]{};GetWindowTextW(g_chatInput,entered,80);
        if(std::wcscmp(entered,accent)!=0)return assistantFailure(130);
        int originalWidth=winW,originalHeight=winH;winW=1180;winH=850;layout();if(!chatInputFits())return assistantFailure(131);
        winW=originalWidth;winH=originalHeight;layout();DestroyWindow(g_chatInput);g_chatInput=nullptr;
        auto saveProbeShot=[&](const std::string& path){bool swap=g_swap;g_swap=false;drawFrame(hdc);glFinish();std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)memcpy(&image.px[(size_t)y*winW*3],&pixels[(size_t)(winH-1-y)*winW*3],(size_t)winW*3);g_swap=swap;return writePNG(path.c_str(),image);};
        bool previousFocusedWorkflow=g_teamFocusedWorkflow;g_teamFocusedWorkflow=false;unsigned previousTeamCount=g_teamRoleCount;g_teamRoleCount=3;bool previousTeamMode=g_teamMode;auto previousModels=g_teamModels,previousEfforts=g_teamEfforts;g_teamMode=true;g_assistantDockOpen=true;layout();g_teamModels={"offline-engineer","offline-designer","offline-reviewer"};g_teamEfforts={"low","medium","high"};
        auto teamGate=std::make_shared<std::atomic_bool>(false);auto decisionSeen=std::make_shared<std::atomic_bool>(false);auto teamTargetSeen=std::make_shared<std::atomic_bool>(false),steeringTargetSeen=std::make_shared<std::atomic_bool>(false);std::array<std::shared_ptr<std::atomic_int>,3> teamRounds{std::make_shared<std::atomic_int>(0),std::make_shared<std::atomic_int>(0),std::make_shared<std::atomic_int>(0)};std::array<AssistantTransport,8> teamTransports;auto teamRevision=g_session.revision;auto beforeTeamTokens=g_assistantUsageDocument.totalTokens;
        for(size_t i=0;i<3;++i)teamTransports[i]=[i,teamRounds,teamGate,decisionSeen,teamTargetSeen,steeringTargetSeen,teamRevision](const auto&,const V& request,const auto* cancel){
            while(!*teamGate){if(cancel&&*cancel)throw std::runtime_error("Offline team test cancelled");std::this_thread::sleep_for(std::chrono::milliseconds(1));}int round=++*teamRounds[i];if(request.dump().find("dynamic team decision")!=std::string::npos){*decisionSeen=true;if(request.dump().find("feature:1")!=std::string::npos&&request.dump().find("Selected object_context")!=std::string::npos)*steeringTargetSeen=true;}else if(request.dump().find("feature:1")!=std::string::npos&&request.dump().find("Selected object_context")!=std::string::npos)*teamTargetSeen=true;
            V response=V::makeObj(),outputs=V::makeArr();response.set("status",V::makeStr("completed"));V usage=V::makeObj();usage.set("input_tokens",V::makeNum(11));usage.set("output_tokens",V::makeNum(4));usage.set("total_tokens",V::makeNum(15));response.set("usage",usage);
            if(round==1){V call=V::makeObj(),args=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr("offline_team_"+std::to_string(i)));call.set("name",V::makeStr(i==0?"doc_info":"doc_apply"));
                if(i!=0){args.set("expected_revision",V::makeNum((double)teamRevision));V commands=V::makeArr(),command=V::makeObj();command.set("op",V::makeStr("param.set"));command.set("name",V::makeStr(i==1?"designer_ui_probe":"reviewer_ui_probe"));command.set("expr",V::makeStr("23"));commands.arr.push_back(command);args.set("commands",commands);}call.set("arguments",V::makeStr(args.dump()));outputs.arr.push_back(call);
            }else{V message=V::makeObj(),content=V::makeArr(),text=V::makeObj();message.set("type",V::makeStr("message"));message.set("role",V::makeStr("assistant"));text.set("type",V::makeStr("output_text"));text.set("text",V::makeStr("Offline deterministic role check completed."));content.arr.push_back(text);message.set("content",content);outputs.arr.push_back(message);}response.set("output",outputs);return response;
        };
        g_chatLog.clear();selBody=0;interactiveAction(565);if(!g_pendingObjectTarget||*teamRounds[0])return assistantFailure(614);sendTeam("OFFLINE TEST: implement one parameter; designer inspects; reviewer write must be denied.",teamTransports);
        auto pumpTeam=[&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);do{MSG event{};while(PeekMessage(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessage(&event);}drainUiTools();pollTeam(true);if(!g_teamSnapshot.running)return true;Sleep(2);}while(std::chrono::steady_clock::now()<deadline);return false;};
        auto workingDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(std::chrono::steady_clock::now()<workingDeadline){pollTeam(true);if(std::count_if(g_teamSnapshot.roles.begin(),g_teamSnapshot.roles.end(),[](const auto& role){return role.state==AgentTeamState::Working;})==3)break;Sleep(2);}
        if(!saveProbeShot(shotPath+".team.png"))return assistantFailure(302);*teamGate=true;if(!pumpTeam())return assistantFailure(303);
        if(g_doc.getParam("designer_ui_probe")!=23||std::any_of(g_doc.params.begin(),g_doc.params.end(),[](const auto& p){return p.name=="reviewer_ui_probe";}))return assistantFailure(304);
        if(!*teamTargetSeen||g_pendingObjectTarget)return assistantFailure(615);
        if(g_teamSnapshot.usage.totalTokens!=75||g_assistantUsageDocument.totalTokens!=beforeTeamTokens+75||g_teamSnapshot.roles.size()!=3)return assistantFailure(305);
        for(size_t i=0;i<3;++i)if(g_teamSnapshot.roles[i].model!=g_teamModels[i]||g_teamSnapshot.roles[i].reasoningEffort!=g_teamEfforts[i]||g_teamSnapshot.roles[i].usage.totalTokens!=(i==2?15:30)||!g_teamReceipts[i])return assistantFailure(306);if(g_teamSnapshot.roles[2].state!=AgentTeamState::Failed)return assistantFailure(306);
        auto serialBefore=g_doc.usage.serial;g_busy=true;pollTeam(true);g_busy=false;if(g_doc.usage.serial!=serialBefore)return assistantFailure(307);pollTeam(true);if(g_doc.usage.serial!=serialBefore)return assistantFailure(308);
        g_teamTarget="engineer";selBody=0;interactiveAction(565);sendTeam("dynamic team decision: keep parameter 23",teamTransports);if(!pumpTeam()||!*decisionSeen||!*steeringTargetSeen||g_pendingObjectTarget||g_teamSnapshot.usage.totalTokens!=90)return assistantFailure(309);
        g_agentTeam.pause(true);int designerRounds=*teamRounds[1];g_teamTarget="designer";sendTeam("Review the dynamic team decision",teamTransports);Sleep(25);pollTeam(true);if(!g_teamSnapshot.paused||*teamRounds[1]!=designerRounds)return assistantFailure(310);g_agentTeam.pause(false);if(!pumpTeam()||*teamRounds[1]!=designerRounds+1||g_teamSnapshot.usage.totalTokens!=105)return assistantFailure(311);
        cancelTeam();g_agentTeam.waitIdle();pollTeam(true);if(!g_teamSnapshot.cancelled)return assistantFailure(312);g_teamStarted=false;g_teamTarget.clear();g_teamModels=previousModels;g_teamEfforts=previousEfforts;g_teamMode=false;g_toolModule=2;layout();if(!saveProbeShot(shotPath+".sketch-icons.png"))return assistantFailure(313);
        // A real role conversation yields between atomic sketch/feature steps.
        // Its previews are rendered before the owning UI executes each request.
        unsigned savedRounds=g_assistantConfig.maxRounds;g_assistantConfig.maxRounds=1;g_teamMode=true;g_teamModels={"offline-orchestrator","offline-part-designer","offline-physics-reviewer"};g_teamEfforts={"high","xhigh","high"};g_teamTarget.clear();layout();auto constructionRevision=g_session.revision;size_t beforeConstructFeatures=g_doc.features.size();auto designerRound=std::make_shared<std::atomic_int>(0);std::array<AssistantTransport,8> constructionTransports;
        for(size_t role=0;role<3;++role)constructionTransports[role]=[role,designerRound,constructionRevision,parse](const auto&,const V&,const auto*){
            V response=V::makeObj(),output=V::makeArr();response.set("status",V::makeStr("completed"));response.set("usage",parse(R"({"input_tokens":8,"output_tokens":2,"total_tokens":10})"));int round=role==1?++*designerRound:4;
            auto call=[&](const std::string& tool,V args,const std::string& suffix){V item=V::makeObj();item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr("construction_"+std::to_string(round)+suffix));item.set("name",V::makeStr(tool));item.set("arguments",V::makeStr(args.dump()));output.arr.push_back(item);};
            if(round<=3){if(round>1)call("doc_info",parse(R"({"detail":"design"})"),"_inspect");V args=V::makeObj();args.set("expected_revision",V::makeNum((double)(constructionRevision+round-1)));args.set("commands",round==1?parse(R"([{"op":"param.set","name":"live_plane_height","expr":15},{"op":"plane.set","name":"Team live datum","p":{"oz":"live_plane_height"}},{"op":"sketch.set","name":"Team live profile","sketch":{"plane":"Team live datum","entities":[{"kind":"line","name":"rail","dof":[0,0,60,0]}],"constraints":[]}}])"):round==2?parse(R"([{"op":"sketch.patch","name":"Team live profile","entity":"rail","anchor":1,"y":"12"}])"):parse(R"([{"op":"feature.add","type":"extrude_surface","name":"Team live panel","p":{"sketch":"Team live profile","distance":"20","solid":"0"}}])"));call("team_apply_step",args,"_step");}
            else{V item=V::makeObj(),content=V::makeArr(),text=V::makeObj();item.set("type",V::makeStr("message"));item.set("role",V::makeStr("assistant"));text.set("type",V::makeStr("output_text"));text.set("text",V::makeStr("Offline actual construction finished."));content.arr.push_back(text);item.set("content",content);output.arr.push_back(item);}response.set("output",output);return response;
        };
        bool observedDraft=false,observedPatchDraft=false,savedDraft=false;sendTeam("OFFLINE TEST: designer authors an actual sketch, edits one anchor, then publishes one exact surface; resume after each bounded window.",constructionTransports);
        auto pumpConstruction=[&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);do{MSG message{};while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}for(const auto& entry:g_draftViews)if(!entry.second.lines.empty()){observedDraft=true;if(entry.second.commands.dump().find("sketch.patch")!=std::string::npos)observedPatchDraft=true;if(!savedDraft){if(!saveProbeShot(shotPath+".construction-draft.png"))return false;savedDraft=true;}}drainUiTools();pollTeam(true);if(!g_teamSnapshot.running){while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}return true;}Sleep(2);}while(std::chrono::steady_clock::now()<deadline);return false;};
        if(!pumpConstruction()||!observedDraft||g_teamSnapshot.roles[1].state!=AgentTeamState::AwaitingContinuation||g_session.revision!=constructionRevision+1||g_doc.features.size()!=beforeConstructFeatures||!g_doc.findSketch("Team live profile"))return assistantFailure(350);
        drawFrame(hdc);if(std::none_of(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==490;}))return assistantFailure(351);g_teamTarget="designer";uiAction(490);if(!pumpConstruction()||!observedPatchDraft||g_doc.findSketch("Team live profile")->sk.ents[0].dof[3]!=12||g_session.revision!=constructionRevision+2)return assistantFailure(352);
        uiAction(490);if(!pumpConstruction()||g_session.revision!=constructionRevision+3||g_doc.features.size()!=beforeConstructFeatures+1||g_doc.features.back().type!=FeatureType::ExtrudeSurface||g_liveCompletedSteps!=3||g_teamSnapshot.roles[1].stepsApplied!=3||!g_draftViews.empty()||!g_committedRoleSketches.count("Team live profile"))return assistantFailure(353);
        uiAction(490);if(!pumpConstruction())return assistantFailure(354);if(g_teamSnapshot.roles[1].state==AgentTeamState::AwaitingContinuation){uiAction(490);if(!pumpConstruction())return assistantFailure(354);}if(g_teamSnapshot.roles[1].state!=AgentTeamState::Idle||!saveProbeShot(shotPath+".construction-committed.png"))return assistantFailure(354);
        cancelTeam();g_agentTeam.waitIdle();pollTeam(true);g_teamStarted=false;g_teamTarget.clear();g_teamModels=previousModels;g_teamEfforts=previousEfforts;g_teamMode=false;g_assistantConfig.maxRounds=savedRounds;layout();
        // Returning to the previous snapshot creates a new branch, retaining cost.
        u64 restoreRevision=g_session.revision,restoreTokens=usageTotals(g_doc).totalTokens;g_historySmokeIndex=1;g_dialogSmoke=2;g_dialogShot=shotPath+".iteration-history.png";uiAction(493);g_dialogSmoke=0;if(g_error||g_session.revision!=restoreRevision)return assistantFailure(355);g_dialogSmoke=3;uiAction(493);g_dialogSmoke=0;g_historySmokeIndex=0;if(g_error||g_session.revision!=restoreRevision+1||g_doc.features.size()!=beforeConstructFeatures||usageTotals(g_doc).totalTokens!=restoreTokens)return assistantFailure(356);
        // Dedicated surface forms preserve the exact source and measured area.
        g_dialogOverrideQueue={{"box"},{"20","10","5"}};g_dialogSmoke=3;uiAction(100);g_dialogSmoke=0;if(g_error)return assistantFailure(364);auto surfaceSource=g_doc.features.back().id;g_dialogOverrides={std::to_string(surfaceSource),"z_max"};g_dialogSmoke=3;g_dialogShot=shotPath+".surface-extract.png";uiAction(491);g_dialogSmoke=0;if(g_error||g_doc.features.back().type!=FeatureType::SurfaceEdit)return assistantFailure(365);auto extracted=g_doc.features.back().id;
        g_dialogOverrides={std::to_string(extracted),"","positive","[8,0,0]","[1,0,0]"};g_dialogSmoke=3;g_dialogShot=shotPath+".surface-trim.png";uiAction(492);g_dialogSmoke=0;if(g_error||g_doc.features.back().type!=FeatureType::SurfaceEdit||nativeText(&g_doc.features.back(),"op")!="trim_plane")return assistantFailure(366);auto trimmed=g_doc.features.back().id;auto trimmedBody=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const auto& body){return body.id==trimmed;});if(trimmedBody==g_doc.evalBodies.end()||std::abs(trimmedBody->nativeData.find("report")->find("area_mm2")->num-120)>1e-7)return assistantFailure(367);g_toolModule=6;if(!saveProbeShot(shotPath+".surfaces.png"))return assistantFailure(368);g_dialogSmoke=3;uiAction(79);g_dialogSmoke=0;if(g_error||g_doc.features.back().type!=FeatureType::Thicken)return assistantFailure(369);
        g_dialogOverrideQueue={{"UI internal blank","160","50","2","0.5"}};g_dialogSmoke=3;uiAction(120);g_dialogSmoke=0;if(g_error)return assistantFailure(370);auto internalSheet=g_doc.features.back().id;g_dialogOverrideQueue={{"add"},{"ui-jog","40","20","45","3",""}};g_dialogSmoke=3;uiAction(494);g_dialogSmoke=0;if(g_error||!g_doc.find(internalSheet)->nativeSpec.find("jog"))return assistantFailure(371);g_dialogOverrideQueue={{"edit"},{"ui-jog","40","22","45","3",""}};g_dialogSmoke=3;uiAction(494);g_dialogSmoke=0;if(g_error||g_doc.find(internalSheet)->nativeSpec.find("jog")->find("height")->asStr()!="22")return assistantFailure(372);g_dialogOverrideQueue={{"remove"}};g_dialogSmoke=3;uiAction(494);g_dialogSmoke=0;if(g_error||g_doc.find(internalSheet)->nativeSpec.find("jog"))return assistantFailure(373);
        applyCommand(parse(R"({"op":"sketch.set","name":"UI full-width bend","sketch":{"plane":"XZ","entities":[{"kind":"line","name":"FoldLine","construction":true,"dof":[60,0,60,50]}],"constraints":[]}})"));if(g_error)return assistantFailure(374);g_dialogOverrideQueue={{"add"},{"ui-fold","UI full-width bend","FoldLine","90","3",""}};g_dialogSmoke=3;uiAction(495);g_dialogSmoke=0;if(g_error||!g_doc.find(internalSheet)->nativeSpec.find("sketch_bend"))return assistantFailure(375);g_toolModule=0;if(!saveProbeShot(shotPath+".sheet-internal-bend.png"))return assistantFailure(376);g_dialogOverrideQueue={{"remove"}};g_dialogSmoke=3;uiAction(495);g_dialogSmoke=0;if(g_error||g_doc.find(internalSheet)->nativeSpec.find("sketch_bend"))return assistantFailure(377);
        applyCommand(parse(R"({"op":"feature.add","type":"construction_frame","name":"UI frame meetings","native":{"profiles":[{"name":"SHS20","section":"rect","width":20,"height":20,"wall":2,"density_g_cm3":7.85}],"nodes":[{"name":"A","xyz":[0,0,0]},{"name":"B","xyz":[200,0,0]},{"name":"C","xyz":[100,0,100]},{"name":"D","xyz":[100,0,20]}],"members":[{"name":"rail","profile":"SHS20","start":"A","end":"B","normal":[0,0,1]},{"name":"brace","profile":"SHS20","start":"C","end":"D","normal":[1,0,0]}]}})"));if(g_error)return assistantFailure(378);auto formFrame=g_doc.features.back().id;auto frameBody=std::find(g_bodyIds.begin(),g_bodyIds.end(),formFrame);if(frameBody==g_bodyIds.end())return assistantFailure(379);selBody=(int)(frameBody-g_bodyIds.begin());
        g_dialogOverrideQueue={{"add","brace","end"},{"rail","25"}};g_dialogSmoke=3;uiAction(496);g_dialogSmoke=0;if(g_error||g_doc.find(formFrame)->nativeSpec.find("joints")->arr.size()!=1)return assistantFailure(380);g_dialogOverrideQueue={{"edit","brace","end"},{"rail","30"}};g_dialogSmoke=3;uiAction(496);g_dialogSmoke=0;if(g_error||!g_doc.find(formFrame)->nativeSpec.find("profiles")||g_doc.find(formFrame)->nativeSpec.find("joints")->arr[0].find("max_extension")->asStr()!="30")return assistantFailure(381);
        g_dialogOverrideQueue={{"add","rail","end"},{"[180,0,0]","[1,0,0]"}};g_dialogSmoke=3;uiAction(497);g_dialogSmoke=0;if(g_error||g_doc.find(formFrame)->nativeSpec.find("joints")->arr.size()!=2)return assistantFailure(382);g_dialogOverrideQueue={{"add","UI-bead"},{"rail","brace","[90,-5,10]","[90,5,10]","[-1,0,0]","3","3","","7.85"}};g_dialogSmoke=3;uiAction(498);g_dialogSmoke=0;if(g_error||g_doc.find(formFrame)->nativeSpec.find("welds")->arr.size()!=1)return assistantFailure(383);
        g_dialogOverrideQueue={{"edit","UI-bead"},{"rail","brace","[90,-5,10]","[90,5,10]","[-1,0,0]","4","3","","7.85"}};g_dialogSmoke=3;uiAction(498);g_dialogSmoke=0;if(g_error||!g_doc.find(formFrame)->nativeSpec.find("members")||g_doc.find(formFrame)->nativeSpec.find("welds")->arr[0].find("leg_a")->asStr()!="4")return assistantFailure(384);g_toolModule=1;frameScene();if(!saveProbeShot(shotPath+".frame-endpoints-weld.png"))return assistantFailure(385);g_dialogOverrideQueue={{"remove","UI-bead"}};g_dialogSmoke=3;uiAction(498);g_dialogSmoke=0;if(g_error||!g_doc.find(formFrame)->nativeSpec.find("welds")->arr.empty())return assistantFailure(386);g_dialogOverrideQueue={{"remove","rail","end"}};g_dialogSmoke=3;uiAction(497);g_dialogSmoke=0;if(g_error||g_doc.find(formFrame)->nativeSpec.find("joints")->arr.size()!=1)return assistantFailure(387);
        // Large-scene orbit uses a fixed scene pivot and camera-space pan.
        auto savedBounds=g_sceneBounds;Vec3 savedTarget=target,savedTTarget=tTarget;double savedRoll=cameraRoll;cameraRoll=0;double savedYaw=yaw,savedPitch=pitch,savedDistance=dist,savedTYaw=tYaw,savedTPitch=tPitch,savedTDistance=tDist;g_sceneBounds={};g_sceneBounds.grow(Vec3{-100000,-40000,-30000});g_sceneBounds.grow(Vec3{100000,40000,30000});yaw=tYaw=.7;pitch=tPitch=.8;dist=tDist=400000;target=tTarget=g_sceneBounds.center();int dragX=viewX+viewW/2,dragY=viewY+viewH/2;beginCameraDrag(dragX,dragY);moveCameraDrag(dragX,dragY,false,false);if(yaw!=.7||pitch!=.8||dist!=400000||target.lensq()!=0)return assistantFailure(357);moveCameraDrag(dragX+140,dragY+40,false,false);if(target.lensq()>1e-12||std::abs(yaw-tYaw)>1e-12||std::abs(pitch-tPitch)>1e-12)return assistantFailure(358);Vec3 outward,right,up;cameraBasis(yaw,pitch,outward,right,up);auto panStart=target;double panScale=2*dist*std::tan(35*K_PI/360.)/viewH;beginCameraDrag(dragX,dragY);moveCameraDrag(dragX+20,dragY+10,true,false);if((target-(panStart-right*(20*panScale)+up*(10*panScale))).lensq()>1e-8)return assistantFailure(359);beginCameraDrag(dragX,dragY);moveCameraDrag(dragX,dragY+50,false,true);if(dist<=400000||dist!=tDist)return assistantFailure(360);g_cameraDrag.active=false;cameraRoll=savedRoll;g_sceneBounds=savedBounds;target=savedTarget;tTarget=savedTTarget;yaw=savedYaw;pitch=savedPitch;dist=savedDistance;tYaw=savedTYaw;tPitch=savedTPitch;tDist=savedTDistance;
        {auto originalFrame=cameraFrame(yaw,pitch,cameraRoll);double originalYaw=yaw,originalPitch=pitch,originalRoll=cameraRoll;Vec3 originalTarget=target;
         int x=viewX+viewW/2,y=viewY+viewH/2;g_navMode=0;
         SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+50,y+20));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x+50,y+20));
         if((cameraFrame(yaw,pitch,cameraRoll).outward-originalFrame.outward).len()>1e-12)return assistantFailure(391);
         SendMessage(g_window,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(x+70,y+40));SendMessage(g_window,WM_MBUTTONUP,0,MAKELPARAM(x+70,y+40));
         if((cameraFrame(yaw,pitch,cameraRoll).outward-originalFrame.outward).len()<.01)return assistantFailure(392);
         {auto moved=cameraFrame(yaw,pitch,cameraRoll);if(originalFrame.outward.dot(moved.right)<=0||-originalFrame.outward.dot(moved.up)<=0)return assistantFailure(398);}
         for(int axis=0;axis<3;++axis)for(int sign:{-1,1}){
             yaw=tYaw=originalYaw;pitch=tPitch=originalPitch;cameraRoll=originalRoll;target=tTarget=originalTarget;beginCameraDrag(x,y);
             int span=std::max(720,std::min(viewW,viewH));
             for(int n=1;n<=span*2;++n)moveCameraDrag(x+(axis==1?0:sign*n),y+(axis==1?sign*n:0),false,false,axis==2);
             auto spun=cameraFrame(yaw,pitch,cameraRoll);if((spun.outward-originalFrame.outward).len()>1e-8||(spun.right-originalFrame.right).len()>1e-8)return assistantFailure(399);
         }
         g_cameraDrag.active=false;yaw=tYaw=originalYaw;pitch=tPitch=originalPitch;cameraRoll=originalRoll;target=tTarget=originalTarget;}
        // Missing rates stay unavailable and the title strip has disjoint zones.
        auto savedPricing=g_pricing;bool savedPricingReady=g_pricingReady;g_pricing={};g_pricingReady=false;updateCostDisplay(true);if(costSummary().find("N/A")==std::string::npos||g_costEstimate.unpricedTokens!=usageTotals(g_doc).totalTokens)return assistantFailure(361);g_pricing=savedPricing;g_pricingReady=savedPricingReady;updateCostDisplay(true);
        int savedHeaderW=winW,savedHeaderH=winH;for(int width:{1180,1440,3440}){RECT frame{0,0,width,1000};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=1000;layout();drawFrame(hdc);auto info=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==488;}),search=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==450;});if(info==g_zones.end()||search==g_zones.end()||info->x+info->w>search->x||search->x+search->w>width-232)return assistantFailure(362);if(!saveProbeShot(shotPath+".header-"+std::to_string(width)+".png"))return assistantFailure(363);}RECT originalFrame{0,0,savedHeaderW,savedHeaderH};AdjustWindowRect(&originalFrame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,originalFrame.right-originalFrame.left,originalFrame.bottom-originalFrame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=savedHeaderW;winH=savedHeaderH;layout();
        const auto& gallery=studioToolEntries();for(int action:{61,62,63,70,73,89,90,100,101,102,104,106,107,108,120,121,122,123,124,125,126,394,395,396,397,398,399,210,211,212,215,216,451,452,469,474,475,476,330,488,489,490,491,492,493,494,495,496,497,498})if(std::none_of(gallery.begin(),gallery.end(),[&](const auto& entry){return entry.action==action;}))return assistantFailure(315);
        if(std::count_if(gallery.begin(),gallery.end(),[](const auto& entry){return entry.action>=41000&&entry.action<42000;})!=(int)studioApiTools().arr.size())return assistantFailure(316);
        uiAction(480);if(!g_toolsOpen)return assistantFailure(317);SetWindowTextW(g_toolSearch,L"RETURN HEM");auto matches=visibleStudioTools();if(matches.empty()||std::none_of(matches.begin(),matches.end(),[&](size_t index){return gallery[index].action==394;}))return assistantFailure(318);
        if(!saveProbeShot(shotPath+".tool-search.png"))return assistantFailure(319);SetWindowTextW(g_toolSearch,L"");const auto& commandGroups=studioCommandGroups();auto surfaceCategory=std::find_if(commandGroups.begin(),commandGroups.end(),[](const StudioCommandGroup& group){return std::string(group.title)=="Native surfaces";});if(surfaceCategory==commandGroups.end())return assistantFailure(320);int surfaceCategoryIndex=(int)(surfaceCategory-commandGroups.begin());uiAction(40001+surfaceCategoryIndex);if(g_toolCategory!=surfaceCategoryIndex)return assistantFailure(320);g_toolScroll=0;if(!saveProbeShot(shotPath+".tool-gallery.png"))return assistantFailure(321);auto filtered=visibleStudioTools();if(filtered.size()!=surfaceCategory->items.size())return assistantFailure(322);
        uiAction(482);drawFrame(hdc);if(g_toolScroll<=0)return assistantFailure(323);uiAction(481);drawFrame(hdc);if(g_toolScroll!=0)return assistantFailure(324);
        auto rectangle=std::find_if(gallery.begin(),gallery.end(),[](const auto& entry){return entry.action==61;});size_t sketchCount=g_doc.sketches.size();g_dialogSmoke=3;uiAction(42000+(int)(rectangle-gallery.begin()));g_dialogSmoke=0;if(g_toolsOpen||g_error||g_doc.sketches.size()!=sketchCount+1)return assistantFailure(325);
        auto info=std::find_if(gallery.begin(),gallery.end(),[](const auto& entry){return entry.title=="doc_info";});if(info==gallery.end())return assistantFailure(326);u64 galleryRevision=g_session.revision;g_dialogSmoke=3;uiAction(info->action);g_dialogSmoke=0;if(g_error||g_session.revision!=galleryRevision)return assistantFailure(327);
        int galleryWidth=winW,galleryHeight=winH;winW=1280;winH=720;g_toolsOpen=true;g_toolCategoryScroll=1000;layout();drawFrame(hdc);auto laptopBounds=toolGalleryBounds();int advancedCategory=40001+(int)studioCommandGroups().size();if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==advancedCategory;})||std::any_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id>=40000&&zone.id<40100&&zone.y+zone.h>laptopBounds.bottom-34;}))return assistantFailure(335);if(!saveProbeShot(shotPath+".tool-gallery-laptop.png"))return assistantFailure(336);g_toolsOpen=false;g_toolCategoryScroll=0;winW=galleryWidth;winH=galleryHeight;layout();
        for(int module=0;module<7;++module){g_toolModule=module;drawFrame(hdc);if(std::none_of(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==480;}))return assistantFailure(328);}
        auto previousLibraryRoot=g_libraryRoot,previousLibraryText=g_libraryText;g_libraryRoot=shotPath+".library";g_dialogSmoke=3;uiAction(100);g_dialogSmoke=0;if(g_error)return assistantFailure(329);exportNativeStep(shotPath+".component.step");if(g_error||!std::filesystem::is_regular_file(shotPath+".component.step"))return assistantFailure(330);
        u64 beforeIngest=g_session.revision;g_dialogSmoke=3;ingestLibraryStep(shotPath+".component.step");g_dialogSmoke=0;if(g_error||g_libraryLastId.empty()||g_session.revision!=beforeIngest)return assistantFailure(331);auto testComponentId=g_libraryLastId;auto stem=std::filesystem::u8path(shotPath+".component.step").stem().u8string();g_libraryText=std::string(stem.begin(),stem.end());g_dialogSmoke=3;g_dialogShot=shotPath+".library-search.png";librarySearchDialog();g_dialogShot.clear();g_dialogSmoke=0;if(g_error||std::none_of(g_libraryMatches.arr.begin(),g_libraryMatches.arr.end(),[&](const auto& record){return libraryText(record,"id")==testComponentId;}))return assistantFailure(332);
        size_t beforeInsert=g_doc.features.size(),beforeInstances=g_doc.assembly.size();auto previousOutputs=g_bodyIds;g_dialogSmoke=3;insertLibraryComponent(testComponentId);g_dialogSmoke=0;if(g_error||g_doc.features.size()!=beforeInsert+1||g_doc.assembly.size()!=beforeInstances+1+(beforeInstances?0:previousOutputs.size()))return assistantFailure(333);
        if(!beforeInstances)for(u64 feature:previousOutputs)if(std::none_of(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const auto& node){return node.feature==feature;}))return assistantFailure(333);
        size_t afterFirstInsert=g_doc.features.size(),afterFirstInstances=g_doc.assembly.size();g_dialogSmoke=3;insertLibraryComponent(testComponentId);g_dialogSmoke=0;if(g_error||g_doc.features.size()!=afterFirstInsert||g_doc.assembly.size()!=afterFirstInstances+1)return assistantFailure(334);
        g_toolModule=1;g_toolCategory=-1;g_toolScroll=0;g_teamMode=previousTeamMode;g_libraryRoot=previousLibraryRoot;g_libraryText=previousLibraryText;layout();
        {int previousW=winW,previousH=winH;bool previousMode=g_teamMode,previousStarted=g_teamStarted;auto previousSnapshot=g_teamSnapshot;auto previousActivity=g_teamActivity;auto previousGoal=g_teamGoal;
         auto resizeTestWindow=[&](int width,int height){RECT bounds{0,0,width,height};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=height;layout();};
         g_teamMode=true;g_teamRoleCount=8;g_teamStarted=true;g_teamSnapshot={};g_teamGoal="Unicode goal: a\xc3\xa7\xc3\xa3o and WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";g_verifyTeamCardBounds=true;
         for(unsigned i=0;i<8;++i){AgentTeamRoleSnapshot role;role.status="Readable measured status WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";role.text="Portugu\xc3\xaas: a\xc3\xa7\xc3\xa3o / long tool message WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";role.usage.reportedResponses=1;role.usage.totalTokens=123456789;role.usage.inputTokens=123456789;role.usage.outputTokens=123456789;role.usage.reasoningTokens=123456789;g_teamSnapshot.roles.push_back(role);}
         for(auto size:std::vector<std::pair<int,int>>{{1180,800},{1280,800},{1600,1000},{2560,1200},{3840,2160}}){resizeTestWindow(size.first,size.second);g_teamPage=0;std::set<int> visibleRoles;unsigned pageSize=teamCardColumns()*teamCardRows(),pages=(g_teamRoleCount+pageSize-1)/pageSize;
             for(unsigned page=0;page<pages;++page){g_teamPage=page;drawFrame(hdc);std::string navigationDetail;if(!navigationUiFits(navigationDetail)){g_teamCardBoundsFailure=navigationDetail;return assistantFailure(402);}if(!g_teamCardContentFits||viewH<160){g_teamCardBoundsFailure+=" / viewport "+std::to_string(viewW)+"x"+std::to_string(viewH)+" / window "+std::to_string(winW)+"x"+std::to_string(winH)+" / page "+std::to_string(page);return assistantFailure(393);}
                 for(const auto& z:g_zones)if(z.id>=44000&&z.id<44008){visibleRoles.insert(z.id);if(z.y<viewY+viewH||z.y+z.h>winH-154)return assistantFailure(393);}
                 if(pages>1)for(int action:{44011,44012})if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==action;}))return assistantFailure(393);
                 auto workflow=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& z){return z.id==44014;});if(workflow==g_zones.end())return assistantFailure(393);
                 for(const auto& z:g_zones)if(z.id!=44014&&z.x<workflow->x+workflow->w&&z.x+z.w>workflow->x&&z.y<workflow->y+workflow->h&&z.y+z.h>workflow->y)return assistantFailure(393);
             }
             if(visibleRoles.size()!=8)return assistantFailure(393);g_teamPage=0;if(!saveProbeShot(shotPath+".agent-cards-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".png"))return assistantFailure(394);
         }
         if(textWidth(fitText("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",.80f,150),.80f)>150)return assistantFailure(393);
         g_verifyTeamCardBounds=false;resizeTestWindow(previousW,previousH);g_teamMode=previousMode;g_teamStarted=previousStarted;g_teamSnapshot=std::move(previousSnapshot);g_teamActivity=std::move(previousActivity);g_teamGoal=std::move(previousGoal);g_teamRoleCount=previousTeamCount;g_teamPage=0;layout();}

        g_teamRoleCount=previousTeamCount;auto roleAuth=g_authStatus;auto roleCatalog=g_authModels;auto roleModels=g_teamModels,roleEfforts=g_teamEfforts;
        g_authStatus.connected=g_authStatus.planAuthorized=true;g_authStatus.activeClientId="offline-role-account";g_authModels={{"catalog-a","Offline catalog A",{"low","high"},"low"},{"catalog-b","Offline catalog B",{"medium","xhigh"},"medium"}};auto catalogFixture=g_authModels;
        g_assistantConfig.authMode=AssistantAuthMode::ChatGptPlan;g_assistantConfig.endpoint="https://api.openai.com/v1/responses";g_assistantConfig.model="catalog-a";g_assistantConfig.reasoningEffort="high";g_teamModels[0]="catalog-a";g_teamEfforts[0]="high";g_roleSmokeCheck=true;
        auto exerciseRole=[&](const std::string& model,const std::string& effort,int smoke){g_roleSmokeModel=model;g_roleSmokeEffort=effort;g_roleSmokePassed=g_roleSmokeKeyboardPassed=false;g_dialogSmoke=smoke;teamRoleDialog(0);g_dialogSmoke=0;return g_roleSmokePassed&&g_roleSmokeKeyboardPassed;};
        g_roleSmokeRefresh=true;g_dialogShot=shotPath+".role-model-settings.png";if(!exerciseRole("catalog-b","xhigh",3)||!g_roleSmokeRefreshPassed||g_teamModels[0]!="catalog-b"||g_teamEfforts[0]!="xhigh")return assistantFailure(337);g_roleSmokeRefresh=false;
        if(!exerciseRole("catalog-a","low",2)||g_teamModels[0]!="catalog-b"||g_teamEfforts[0]!="xhigh")return assistantFailure(338);
        if(!exerciseRole("catalog-b","",3)||!g_teamEfforts[0].empty()||!teamRoleConfig(0).reasoningEffort.empty())return assistantFailure(339);
        {bool oldTeamMode=g_teamMode,oldTeamStarted=g_teamStarted;auto oldTeamSnapshot=g_teamSnapshot;
         g_teamMode=true;g_teamStarted=true;g_teamSnapshot={};AgentTeamRoleSnapshot failedRole;failedRole.state=AgentTeamState::Failed;failedRole.status="ChatGPT usage limit reached";failedRole.error=failedRole.status;g_teamSnapshot.roles.push_back(failedRole);layout();drawFrame(hdc);
         for(int action:{44013,490})if(std::none_of(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==action;}))return assistantFailure(395);
         if(!saveProbeShot(shotPath+".credit-recovery.png"))return assistantFailure(396);uiAction(44013);if(g_status.find("enable credits")==std::string::npos)return assistantFailure(397);
         g_teamMode=oldTeamMode;g_teamStarted=oldTeamStarted;g_teamSnapshot=std::move(oldTeamSnapshot);layout();}
        g_assistantConfig.reasoningEffort="ultra";g_teamModels[0].clear();g_teamEfforts[0].clear();if(!teamRoleConfig(0).reasoningEffort.empty())return assistantFailure(340);
        g_teamModels[0]="removed-model";g_teamEfforts[0]="high";if(!exerciseRole("catalog-a","low",2)||!g_roleSmokeInitialDisabled||g_teamModels[0]!="removed-model"||g_teamEfforts[0]!="high")return assistantFailure(341);
        auto finishCatalog=[&](std::vector<ChatGptModel> models,const std::string& error={},bool otherAccount=false){auto* event=new UiAuthEvent;event->job=AuthJob::Models;event->status=g_authStatus;if(otherAccount)event->status.activeClientId="different-offline-account";event->models=std::move(models);event->error=error;SendMessage(g_window,WM_ARK_AUTH,0,(LPARAM)event);};
        g_authModels.clear();g_authBusy=true;g_activeAuthJob=AuthJob::Models;teamRoleDialog(1);if(g_pendingRoleSettings!=1||g_roleCatalogReady)return assistantFailure(342);finishCatalog(catalogFixture);if(!g_roleCatalogReady)return assistantFailure(343);g_roleSmokeModel="catalog-a";g_roleSmokeEffort="low";g_dialogSmoke=3;openPendingRoleSettings();g_dialogSmoke=0;if(g_pendingRoleSettings>=0||g_teamModels[1]!="catalog-a"||g_teamEfforts[1]!="low")return assistantFailure(344);
        g_pendingRoleSettings=2;g_pendingRoleAccount=g_authStatus.activeClientId;g_authBusy=true;finishCatalog({},"Offline model fetch failed");if(!g_authModels.empty()||g_pendingRoleSettings>=0||g_roleCatalogReady)return assistantFailure(345);
        g_authModels=catalogFixture;g_pendingRoleSettings=2;g_pendingRoleAccount=g_authStatus.activeClientId;g_authBusy=true;finishCatalog({});if(!g_authModels.empty()||g_pendingRoleSettings>=0||g_roleCatalogReady)return assistantFailure(346);
        g_pendingRoleSettings=2;g_pendingRoleAccount=g_authStatus.activeClientId;g_authBusy=true;g_authCancelRequested=true;finishCatalog(catalogFixture);if(g_pendingRoleSettings>=0||g_roleCatalogReady)return assistantFailure(347);
        g_pendingRoleSettings=2;g_pendingRoleAccount=g_authStatus.activeClientId;g_authBusy=true;finishCatalog(catalogFixture,"",true);if(g_pendingRoleSettings>=0||g_roleCatalogReady)return assistantFailure(348);
        clearPendingRoleSettings();g_roleSmokeCheck=g_roleSmokeRefresh=false;g_authStatus=roleAuth;g_authModels=roleCatalog;g_teamModels=roleModels;g_teamEfforts=roleEfforts;
        // Exercise real worker -> Win32 status delivery, not a synthetic card label.
        // A committed tool before the HTTP rejection must execute only once.
        g_teamMode=false;g_teamStarted=false;g_chatLog.clear();g_conversation=V::makeArr();
        g_assistantConfig.endpoint="http://127.0.0.1:1/v1/responses";g_assistantConfig.model="offline-retry-ui";g_assistantConfig.apiKeyEnv="";g_assistantConfig.reasoningEffort.clear();g_assistantConfig.maxRounds=0;g_assistantConfig.authMode=AssistantAuthMode::ApiKey;
        auto retryPump=[&](const std::function<bool()>& done){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);do{MSG event{};while(PeekMessage(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessage(&event);}drainUiTools();pollTeam(true);if(done())return true;Sleep(2);}while(std::chrono::steady_clock::now()<deadline);return false;};
        auto retryLog=[&]{return std::any_of(g_chatLog.begin(),g_chatLog.end(),[](const auto& item){return item.second.find("HTTP 503 / retry")!=std::string::npos;});};
        auto thinkingLog=[&]{return std::any_of(g_chatLog.begin(),g_chatLog.end(),[](const auto& item){return item.second=="Thinking...";});};
        auto retryAttempts=std::make_shared<std::atomic_int>(0);auto retryRevision=g_session.revision;
        AssistantTransport retryTransport=[retryAttempts,retryRevision,parse](const auto&,const V&,const auto*)->V{
            int attempt=++*retryAttempts;if(attempt==2)throw AssistantHttpError(503,true,100);
            if(attempt==1){V response=parse(R"({"status":"completed"})"),output=V::makeArr(),call=parse(R"({"type":"function_call","call_id":"ui_retry_commit","name":"doc_apply"})"),args=parse(R"({"commands":[{"op":"param.set","name":"http_retry_once","expr":"31"}]})");args.set("expected_revision",V::makeNum((double)retryRevision));call.set("arguments",V::makeStr(args.dump()));output.arr.push_back(call);response.set("output",output);return response;}
            return parse(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Recovered without replaying the committed tool."}]}]})");};
        sendAssistant("OFFLINE: commit once, then recover a typed pre-output HTTP503.",retryTransport);
        if(!retryPump([&]{return retryLog();})||!g_chatBusy||g_assistantStatus.find("HTTP 503 / retry")!=0||thinkingLog())return assistantFailure(410);
        if(!saveProbeShot(shotPath+".single-http-retry.png"))return assistantFailure(411);
        if(!pump()||*retryAttempts!=3||g_session.revision!=retryRevision+1||g_doc.getParam("http_retry_once")!=31||g_assistantPerformanceRun.toolCalls!=2||g_assistantPerformanceRun.providerRequests!=4||!g_assistantStatus.empty()||thinkingLog())return assistantFailure(412);
        g_dialogSmoke=2;g_dialogShot=shotPath+".project-activity.png";uiAction(567);g_dialogSmoke=0;
        // Team retries remain Working and cancellable by the actual dock button.
        g_chatLog.clear();g_teamMode=true;g_teamFocusedWorkflow=true;g_teamRoleCount=3;g_teamModels[0]="offline-retry-ui";g_teamEfforts[0]="auto";g_teamTarget.clear();layout();
        auto teamRetryAttempts=std::make_shared<std::atomic_int>(0);std::array<AssistantTransport,8> retryTeamTransports;
        retryTeamTransports[0]=[teamRetryAttempts,parse](const auto&,const V&,const auto*){if(++*teamRetryAttempts==1)throw AssistantHttpError(503,true,100);return parse(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Team recovered."}]}]})");};
        retryRevision=g_session.revision;sendTeam("OFFLINE: recover transient HTTP503 without editing.",retryTeamTransports);
        if(!retryPump([&]{return retryLog();})||g_teamSnapshot.roles.empty()||g_teamSnapshot.roles[0].state!=AgentTeamState::Working||g_teamActivity[0].find("HTTP 503 / retry")!=0||thinkingLog())return assistantFailure(413);
        if(!saveProbeShot(shotPath+".team-http-retry.png")||!pumpTeam()||*teamRetryAttempts!=2||g_teamSnapshot.roles[0].state!=AgentTeamState::Idle||g_teamSnapshot.roles[0].status.find("retry")!=std::string::npos||g_session.revision!=retryRevision||thinkingLog())return assistantFailure(414);
        cancelTeam();g_agentTeam.waitIdle();pollTeam(true);g_teamStarted=false;g_chatLog.clear();
        teamRetryAttempts=std::make_shared<std::atomic_int>(0);
        retryTeamTransports[0]=[teamRetryAttempts](const auto&,const V&,const auto*)->V{++*teamRetryAttempts;throw AssistantHttpError(503,true,100);};
        sendTeam("OFFLINE: cancel during HTTP503 recovery wait.",retryTeamTransports);
        if(!retryPump([&]{return retryLog();}))return assistantFailure(415);
        drawFrame(hdc);auto cancelZone=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==75;});if(cancelZone==g_zones.end())return assistantFailure(416);
        int cancelX=(int)(cancelZone->x+cancelZone->w*.5f),cancelY=(int)(cancelZone->y+cancelZone->h*.5f);auto cancelledAt=std::chrono::steady_clock::now();SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(cancelX,cancelY));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(cancelX,cancelY));
        if(!pumpTeam()||!g_teamSnapshot.cancelled||*teamRetryAttempts!=1||g_session.revision!=retryRevision||std::chrono::steady_clock::now()-cancelledAt>std::chrono::seconds(2))return assistantFailure(417);
        g_agentTeam.waitIdle();g_teamStarted=false;g_teamTarget.clear();g_teamMode=previousTeamMode;g_teamRoleCount=previousTeamCount;g_teamModels=previousModels;g_teamEfforts=previousEfforts;
        if(g_assistantWorker.joinable())g_assistantWorker.join();g_assistantConfig=previous;g_teamFocusedWorkflow=previousFocusedWorkflow;g_status="Offline assistant UI checks passed / serialized tools / history / stale-write rejection / HTTP retry and cancellation";
    }
    if(selfTest){
        size_t count=g_doc.features.size();uiAction(1);
        if(g_error||g_doc.features.size()!=count+1)return 10;
        uiAction(18);if(g_error||g_doc.features.size()!=count)return 11;
        uiAction(19);if(g_error||g_doc.features.size()!=count+1)return 12;
        selBody=(int)g_bodyIds.size()-1;uiAction(25);if(g_error||g_doc.features.size()!=count)return 13;
        uiAction(14);if(!wireframe)return 14;uiAction(14);
        uiAction(22);if(g_grid)return 15;uiAction(22);
        uiAction(23);if(tPitch!=1.52||!g_cameraOrtho)return 16;uiAction(180);if(tYaw!=0||tPitch!=0||!g_cameraOrtho)return 17;uiAction(24);if(g_cameraOrtho)return 18;
        g_status="UI checks passed / create undo redo delete wire grid views";
    }
    if(dialogTest){
        size_t count=g_doc.features.size();g_dialogSmoke=1;g_dialogShot=shotPath+".properties.png";
        interactiveAction(1);if(g_error||g_doc.features.size()!=count+1||g_doc.features.back().p.at("dx")!="32")return 40;
        uiAction(18);if(g_error||g_doc.features.size()!=count)return 41;
        g_dialogSmoke=2;interactiveAction(1);if(g_doc.features.size()!=count)return 42;
        std::string command=R"({"tool":"doc_info","arguments":{}})",original=command;
        g_dialogSmoke=3;g_dialogShot=shotPath+".command.png";
        if(!editText("Agent command",command,true)||command!=original)return 43;
        g_dialogSmoke=2;if(editText("Agent command",command,true)||command!=original)return 44;
        std::vector<EditorField> placement{{"tx","0"},{"ty","0"},{"tz","0"},{"rx","0"},{"ry","0"},{"rz","0"}};
        g_dialogSmoke=3;g_dialogShot=shotPath+".placement.png";
        if(!editFields("Instance placement","Translations use mm; rotations use degrees.",placement)||placement.size()!=6)return 45;
        g_dialogSmoke=0;g_status="Dialog checks passed / create / cancel / undo / command / placement";
    }
    if(bendTest){
        using V=arkjson::Value;V a=V::makeObj(),commands=V::makeArr();
        for(auto& f:g_doc.features){V c=V::makeObj();c.set("op",V::makeStr("feature.remove"));c.set("id",V::makeNum((double)f.id));commands.arr.push_back(c);}a.set("commands",commands);runTool("doc_apply",a);if(g_error)return 60;
        g_dialogSmoke=3;g_dialogShot=shotPath+".stock.png";uiAction(40);
        auto* f=selectedNotchedTube();if(g_error||!f||!f->notches.empty())return 61;u64 id=f->id;
        g_dialogShot=shotPath+".point.png";uiAction(42);f=selectedNotchedTube();if(g_error||!f||f->notches.size()!=1)return 62;
        g_dialogSmoke=1;g_dialogReplacement="180";uiAction(43);g_dialogReplacement="32";f=selectedNotchedTube();if(g_error||f->notches[0].station!="180")return 63;
        g_dialogSmoke=3;uiAction(42);f=selectedNotchedTube();if(g_error||f->notches.size()!=2)return 64;
        uiAction(44);if(g_error||selectedNotchedTube()->notches.size()!=1)return 65;uiAction(18);if(g_error||selectedNotchedTube()->notches.size()!=2)return 66;
        g_dialogSmoke=0;a=V::makeObj();a.set("path",V::makeStr(shotPath+".ark"));runTool("doc_save",a);if(g_error)return 67;
        runTool("doc_load",a);if(g_error)return 68;g_path=shotPath+".ark";g_savedRevision=g_session.revision;
        for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id)selBody=(int)i;
        f=selectedNotchedTube();if(!f||f->notches.size()!=2||f->notches[0].station!="180")return 69;
        size_t builds=g_notchBuildCount;auto rev=g_session.revision;
        uiAction(45);auto straight=displayedNotchTransforms(selBody);uiAction(49);if(std::abs(g_foldProgress-.1)>1e-9)return 70;
        uiAction(48);uiAction(47);advanceFold(2.5);if(std::abs(g_foldProgress-.5)>1e-9||!g_foldPlaying)return 71;
        uiAction(47);if(g_foldPlaying)return 72;uiAction(46);auto folded=displayedNotchTransforms(selBody);
        if((straight.back().transformPoint({600,0,0})-folded.back().transformPoint({600,0,0})).len()<1||g_session.revision!=rev||g_notchBuildCount!=builds)return 73;
        frameScene();g_status="Bend checks passed / stock / add / edit / remove / save / reload / animate";
    }
    if(sourceTest){
        if(!selectedSourceTube())for(size_t i=0;i<g_bodyIds.size();++i)if(g_doc.find(g_bodyIds[i])->type==FeatureType::SourceTube){selBody=(int)i;break;}
        auto* f=selectedSourceTube();if(!f)return 81;auto& cache=g_sourceViews.at(f->id);
        if(cache.preview.rigid.size()!=cache.preview.bends.size()+1||cache.preview.bends.empty()||cache.preview.straight.triCount()==0)return 82;
        std::string package=f->sourceData.dump();auto revision=g_session.revision;auto builds=g_sourceBuildCount;
        uiAction(41);uiAction(42);uiAction(43);uiAction(44);if(g_session.revision!=revision||f->sourceData.dump()!=package)return 83;
        uiAction(45);auto flat=displayedSourceTransforms(selBody);uiAction(47);advanceFold(2.5);
        if(!g_foldPlaying||std::abs(g_foldProgress-.5)>1e-9)return 84;uiAction(47);
        updateSourceHinges(cache,g_foldProgress);auto updates=g_sourceHingeUpdates;updateSourceHinges(cache,g_foldProgress);if(g_sourceHingeUpdates!=updates)return 85;
        uiAction(46);auto folded=displayedSourceTransforms(selBody);updateSourceHinges(cache,1.);
        if((flat.back().transformPoint(cache.preview.bends.back().start)-folded.back().transformPoint(cache.preview.bends.back().start)).len()<1)return 86;
        if(f->sourceData.dump()!=package||g_session.revision!=revision||g_sourceBuildCount!=builds)return 87;
        frameScene();g_status="Source checks passed / original mesh / read-only / finite bends / pose cache";
    }
    if(bendExportTest){exportStraightStep(shotPath+".straight.step");if(g_error)return 74;}
    if(requestedFold>=0){g_foldProgress=clamp(requestedFold,0.,1.);g_foldPlaying=false;frameScene();}
    if(assemblyTest){
        if(g_doc.assembly.empty()||g_bodyIds.empty())return 30;
        size_t parts=g_cpuMeshes.size(),instances=g_bodyIds.size(),features=g_doc.features.size();
        uiAction(28);if(g_error||g_cpuMeshes.size()!=parts||g_bodyIds.size()!=instances+1||g_doc.features.size()!=features)return 31;
        selBody=(int)g_bodyIds.size()-1;uiAction(25);if(g_error||g_bodyIds.size()!=instances)return 32;
        uiAction(18);if(g_error||g_bodyIds.size()!=instances+1)return 33;uiAction(19);if(g_error||g_bodyIds.size()!=instances)return 34;
        g_status="Assembly checks passed / shared GPU buffers / duplicate / remove / undo / redo";frameScene();
    }
    if(!requestedSketch.empty()){if(!g_doc.findSketch(requestedSketch))return 111;g_activeSketch=requestedSketch;g_showSketches=true;rebuildSketchOverlay();alignSketchCamera();}
    if(requestedOpening>=0)focusSourceOpening(requestedOpening);
    target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;
    if(assemblyTest){
        bool hit=false;for(int y=viewY+40;y<viewY+viewH;y+=30)for(int x=viewX+20;x<viewX+viewW;x+=30)hit|=pickBody(x,y)>=0;
        if(!hit)return 35;
    }

    if(g_headless){
        g_swap=false;
        for(int sample=0;sample<(g_pathTracing?requestedPathSamples:1);++sample)drawFrame(hdc);
        glFinish();
        if(g_pathTracing)fprintf(stderr,"ARK path tracing: %s / %d spp / %d triangles / %dx%d / %s\n",g_trace.active?"GPU OpenGL BVH":"raster fallback",g_trace.samples,g_trace.triangleCount,g_trace.width,g_trace.height,g_trace.reason.c_str());
        if(GLenum glError=glGetError()){fprintf(stderr,"ARK GL error 0x%X\n",(unsigned)glError);return 20;}
        std::vector<u8> pix((size_t)winW*winH*3);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);
        glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pix.data());
        Image img; img.w=winW; img.h=winH; img.px.resize(pix.size());
        for(int y=0;y<winH;++y)
            memcpy(&img.px[(size_t)y*winW*3],&pix[(size_t)(winH-1-y)*winW*3],(size_t)winW*3);
        if(!writePNG(shotPath.c_str(),img))return 21;
        DestroyWindow(hwnd);
        studio_shader_cache::report();
        printf("shot -> %s (%dx%d)\n",shotPath.c_str(),winW,winH);
        return 0;
    }

    if(!g_headless||!endpointFile.empty()){startLiveBridge();if(!endpointFile.empty()){std::ofstream endpoint(endpointFile);endpoint<<g_liveBridge.endpoint();}}
    if(!g_headless){g_chatInput=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,viewX+30,winH-112,viewW-260,44,hwnd,(HMENU)499,hi,nullptr);
        HFONT chatFont=CreateFontA(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");SendMessage(g_chatInput,WM_SETFONT,(WPARAM)chatFont,TRUE);SendMessage(g_chatInput,EM_SETLIMITTEXT,12000,0);
        SendMessage(g_chatInput,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
    }
    if(connectChatGpt){if(g_headless)return 267;
        if(g_authStatus.connected&&g_authStatus.planAuthorized)startAuthJob(AuthJob::Models);
        else if(g_authStatus.connected&&!g_authStatus.activeClientId.empty())startAuthJob(AuthJob::SignIn,g_authStatus.activeClientId,true);
        else startAuthJob(AuthJob::SignIn,g_authStatus.pendingClientId.empty()?g_authStatus.activeClientId:g_authStatus.pendingClientId);}
    auto lifeStart=std::chrono::steady_clock::now();
    MSG msg{};
    auto fpsT=std::chrono::steady_clock::now();
    int frames=0;
    for(;;){
        while(PeekMessageA(&msg,nullptr,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT)return (int)msg.wParam;
            if(msg.hwnd==g_chatInput&&msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)){interactiveAction(74);continue;}
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        drainUiTools();pollTeam();openPendingRoleSettings();
        if(exitAfterMs&&std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-lifeStart).count()>=exitAfterMs){stopIntegrations();DestroyWindow(hwnd);return 0;}
        drawFrame(hdc);
        ++frames;
        auto now=std::chrono::steady_clock::now();
        f64 el=std::chrono::duration<f64,std::milli>(now-fpsT).count();
        if(el>500){ g_fps=frames*1000.0/el; frames=0; fpsT=now; }
        Sleep(1);
    }
}
