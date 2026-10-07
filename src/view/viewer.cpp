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
#include <shellapi.h>
#include <dwmapi.h>
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
#include "ark/native_features.hpp"
#include "ark/io.hpp"
#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/notch.hpp"
#include "ark/source_tube.hpp"
#include "ark/sketch.hpp"
#include "ark/sheet_preview.hpp"
#include "ark/stock_preview.hpp"
#include "ark/suspension.hpp"
#include "ark/usage.hpp"
#include "ark/serialize.hpp"
#include "ark/assistant.hpp"
#include "ark/chatgpt_auth.hpp"
#include "ark/live_session.hpp"
#include <thread>
#include <future>
#include <exception>
#include <optional>
#include <deque>
#include <filesystem>
#include <atomic>
#include <set>
#include <fstream>
#include <sstream>
#include <algorithm>
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


#define DECL(T,N) static T N=nullptr;
GLFUNCS(DECL)
#undef DECL

typedef BOOL (WINAPI *WGLCHOOSEPFARB)(HDC,const int*,const FLOAT*,UINT,int*,UINT*);
typedef HGLRC (WINAPI *WGLCREATECTXATTRIBS)(HDC,HGLRC,const int*);
static WGLCHOOSEPFARB      wglChoosePixelFormatARB_=nullptr;
static WGLCREATECTXATTRIBS wglCreateContextAttribsARB_=nullptr;

static bool g_headless=false;
static GLuint mkShader(GLenum type,const char* src){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char lg[4096]; glGetShaderInfoLog(s,4096,nullptr,lg);
             if(g_headless)fprintf(stderr,"ARK shader error: %s\n",lg);
             else MessageBoxA(nullptr,lg,"ARK shader error",MB_OK); }
    return s;
}
static GLuint mkProg(const char* vs,const char* fs){
    GLuint p=glCreateProgram();
    glAttachShader(p,mkShader(GL_VERTEX_SHADER,vs));
    glAttachShader(p,mkShader(GL_FRAGMENT_SHADER,fs));
    glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char lg[4096]; glGetProgramInfoLog(p,4096,nullptr,lg);
             if(g_headless)fprintf(stderr,"ARK link error: %s\n",lg);
             else MessageBoxA(nullptr,lg,"ARK link error",MB_OK); }
    return p;
}

// ---------------------------------------------------------------------------
struct GPUMesh { GLuint vao=0,vbo=0,ibo=0; int count=0; };
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
static std::vector<u64> g_bodyIds,g_instanceIds;
static std::vector<bool> g_hiddenBodies,g_beforeSpotlightHidden;
static bool g_spotlightActive=false;static int g_spotlightBody=-1;
static std::vector<std::string> g_instanceNames;
static std::vector<Mat4> g_instanceWorld;
static AssemblyDiagnostics g_assemblyDiagnostics;
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
struct DisplayCameraPose{double yaw=0,pitch=0;bool orthographic=false;};
static DisplayCameraPose g_beforeFlatCamera,g_beforeStockCamera;
static std::vector<MeshBVH> g_bvhs;
static size_t g_triangleCount=0;
static std::string g_status="Ready. Local C++ kernel.",g_path="Harmonic ring.ark";
static bool g_error=false,g_grid=true,g_dragged=false;
static u64 g_savedRevision=0;
static u64 g_savedUsageSerial=0;
static int g_treeScroll=0;static bool g_eatNextClick=false;
static int viewX=258,viewY=76,viewW=1024,viewH=650;
static HWND g_window=nullptr,g_chatInput=nullptr;
static bool g_lightBackground=true,g_satin=true,g_showSketches=false,g_showDatumPlanes=false,g_chatBusy=false;
static bool g_showEdges=false,g_cameraOrtho=false;
static int g_occlusionMode=2; // 0 off, 1 contact, 2 ambient, 3 horizon
static float g_occlusionStrength=.72f,g_occlusionRadius=1.f;
static int g_navMode=0,g_pointerX=0,g_pointerY=0,g_cubeX=0,g_cubeY=0,g_cubeS=210;
static int g_toolModule=0;
static bool g_cubePress=false;
static std::string g_activeSketch,g_activePlane="XY";
static int g_selectedSketchConstraint=-1,g_sketchConstraintPage=0;
static arkjson::Value g_sketchDiagnosis;
static AssistantConfig g_assistantConfig;static arkjson::Value g_conversation=arkjson::Value::makeArr();
static std::vector<std::pair<std::string,std::string>> g_chatLog;static int g_chatScroll=0;
static std::thread g_assistantWorker;static std::shared_ptr<std::atomic_bool> g_assistantCancel=std::make_shared<std::atomic_bool>(false);
static std::atomic_bool g_closing{false};static LiveSessionBridge g_liveBridge;static u64 g_documentEpoch=0;static bool g_ownAssistantTool=false;
static constexpr UINT WM_ARK_TOOL=WM_APP+20,WM_ARK_CHAT=WM_APP+21,WM_ARK_AUTH=WM_APP+22;
struct UiToolRequest {std::string tool;arkjson::Value args;LiveSessionBridge::Completion complete;std::stop_token stop;std::shared_ptr<std::atomic_bool> cancel;std::shared_ptr<std::atomic<u64>> revision;u64 epoch=0;};
struct UiChatEvent {std::string text;bool final=false,error=false;arkjson::Value history;u64 epoch=0;AssistantUsage usage{};bool hasUsage=false;};
enum class AuthJob{SignIn,SignOut,Models};
struct UiAuthEvent{AuthJob job=AuthJob::Models;ChatGptAuthStatus status;std::vector<ChatGptModel> models;std::string error;};
static ChatGptAuthStatus g_authStatus;
static std::vector<ChatGptModel> g_authModels;
static std::thread g_authWorker;
static std::shared_ptr<std::atomic_bool> g_authCancel=std::make_shared<std::atomic_bool>(false);
static bool g_authBusy=false;
static bool g_authCancelRequested=false;
static void postAuth(UiAuthEvent event);
static void interactiveAction(int id);
static AssistantUsage g_assistantUsageDocument{},g_assistantUsageRun{};
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
static arkjson::Value callSessionWithProgress(const std::string& tool,const arkjson::Value& args);
static GLuint sketchVao=0,sketchVbo=0,canvasVao=0,canvasVbo=0,g_canvasMissing=0;static bool g_sketchGpuDirty=true;static std::vector<float> g_sketchLines,g_sketchPoints,g_constraintHighlightLines,g_datumLines;static AABB g_sketchBounds;
struct CanvasTexture { std::string path; unsigned long long stamp=0; bool missing=true; GLuint tex=0; };
static std::vector<CanvasTexture> g_canvasTextures;


static std::vector<Zone>    g_zones;
static std::vector<GPUMesh> g_meshes;
static std::vector<Mesh>    g_cpuMeshes;

static f64 yaw=0.85,pitch=0.46,dist=140,tYaw=yaw,tPitch=pitch,tDist=dist;
static Vec3 target{30,12,0},tTarget=target;
static int winW=1600,winH=1000;
static void layout(){viewW=std::max(winW-558,1);viewH=std::max(winH-320,1);
    if(g_chatInput)MoveWindow(g_chatInput,viewX+30,winH-112,std::max(viewW-260,1),44,TRUE);
}
static int selBody=-1,hoverBody=-1;
static bool wireframe=false, g_pressWasButton=false;
static double g_fps=0;
static int g_hoverZone=-1;
struct CurveSample { std::string sketch,name; Vec3 a,b; };
struct SelectionItem { u64 feature=0; std::string kind,name,owner; };
struct PartPick { std::vector<std::pair<int,int>> faces; std::vector<std::vector<Vec3>> edges; std::vector<std::string> faceNames,edgeNames; };
static std::vector<CurveSample> g_curveSamples;
static std::vector<SelectionItem> g_picked;
static arkjson::Value g_physicalReport;static std::string g_physicalError;
static u64 g_physicalRevision=0,g_physicalSelection=0;static bool g_physicalVisible=false,g_physicalAssembly=false;
static std::vector<PartPick> g_partPicks;
static std::vector<float> g_selectionLines;
static int g_pickKind=0;
static void refreshSelectionLines();
static std::string pickedSketches();
static PartPick partPickFrom(const Body& body){
    PartPick pick;using V=arkjson::Value;const V* mesh=body.nativeData.find("mesh");
    const V* spans=mesh?mesh->find("face_spans"):nullptr;const V* edges=mesh?mesh->find("edges"):nullptr;
    if(spans&&spans->t==V::Arr)for(const auto& span:spans->arr){if(span.t!=V::Arr||span.arr.size()!=2||span.arr[0].t!=V::Num||span.arr[1].t!=V::Num)continue;pick.faces.push_back({(int)span.arr[0].num,(int)span.arr[1].num});}
    pick.faceNames.assign(pick.faces.size(),"");
    if(edges&&edges->t==V::Arr)for(const auto& edge:edges->arr){std::vector<Vec3> points;if(edge.t==V::Arr&&edge.arr.size()%3==0)for(size_t i=0;i+2<edge.arr.size();i+=3)if(edge.arr[i].t==V::Num&&edge.arr[i+1].t==V::Num&&edge.arr[i+2].t==V::Num)points.push_back({edge.arr[i].num,edge.arr[i+1].num,edge.arr[i+2].num});pick.edges.push_back(std::move(points));}
    pick.edgeNames.assign(pick.edges.size(),"");
    const V* topology=body.nativeData.find("topology");const V* refs=topology?topology->find("references"):nullptr;
    if(refs&&refs->t==V::Arr)for(const auto& row:refs->arr){auto* name=row.find("name"),*kind=row.find("kind"),*status=row.find("status"),*indices=row.find("indices");
        if(!name||!kind||!status||!indices||status->asStr()!="bound"||indices->t!=V::Arr||indices->arr.size()!=1||indices->arr[0].t!=V::Num)continue;int index=(int)indices->arr[0].num;
        if(kind->asStr()=="face"&&index>=0&&index<(int)pick.faceNames.size())pick.faceNames[(size_t)index]=name->asStr();
        if(kind->asStr()=="edge"&&index>=0&&index<(int)pick.edgeNames.size())pick.edgeNames[(size_t)index]=name->asStr();
    }return pick;
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

// ---------------------------------------------------------------------------
// Shaders -----------------------------------------------------------------------
static const char* VS_BG=R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);})";
static const char* FS_BG=R"(#version 330 core
out vec4 o;uniform vec2 res;uniform float lightBackground;
void main(){
 vec2 uv=gl_FragCoord.xy/res;
 vec2 q=uv-vec2(.5,.54);
 vec3 dark=mix(vec3(.075,.083,.102),vec3(.19,.22,.26),uv.y);
 dark+=vec3(.035,.050,.067)*exp(-dot(q/vec2(.43,.34),q/vec2(.43,.34))*1.8);
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
 vec4 w=model*vec4(p,1.); wpos=w.xyz; nrm=mat3(model)*N; lpos=P;lnrm=N;
 gl_Position=vp*w;
})";
static const char* FS_BUSY=R"(#version 330 core
out vec4 o;uniform sampler2D frame;uniform vec2 res;
void main(){o=texture(frame,gl_FragCoord.xy/res);})";
static const char* FS_SOLID=R"(#version 330 core
in vec3 nrm,wpos,lpos,lnrm; out vec4 o;
uniform vec3 color,eye; uniform float sel,hardSurface,satin,metallic,roughness,opacity,hasTexture;
uniform vec2 textureScale;uniform sampler2D surfaceTexture;
const float PI=3.14159265359;
vec4 triplanar(){
 vec3 a=pow(abs(normalize(dot(lnrm,lnrm)>1e-8?lnrm:vec3(0.,1.,0.))),vec3(4.));a/=max(a.x+a.y+a.z,1e-5);
 vec4 xy=texture(surfaceTexture,lpos.xy*textureScale);
 vec4 xz=texture(surfaceTexture,lpos.xz*textureScale);
 vec4 yz=texture(surfaceTexture,lpos.yz*textureScale);
 return xy*a.z+xz*a.y+yz*a.x;
}
vec3 lightBRDF(vec3 n,vec3 v,vec3 l,vec3 radiance,vec3 albedo,float m,float r){
 vec3 h=normalize(v+l);float nl=max(dot(n,l),0.),nv=max(dot(n,v),.001),nh=max(dot(n,h),0.),vh=max(dot(v,h),0.);
 float a=max(r*r,.045);float a2=a*a;float d=a2/(PI*pow(nh*nh*(a2-1.)+1.,2.)+1e-5);
 float k=pow(r+1.,2.)/8.;float g=nv/(nv*(1.-k)+k)*nl/(nl*(1.-k)+k);
 vec3 f0=mix(vec3(.04),albedo,m);vec3 f=f0+(1.-f0)*pow(1.-vh,5.);
 vec3 diffuse=(1.-f)*(1.-m)*albedo/PI;
 return (diffuse+d*g*f/max(4.*nv*max(nl,.001),.001))*radiance*nl;
}
void main(){
 vec3 n=hardSurface>0.?normalize(cross(dFdx(wpos),dFdy(wpos))):normalize(nrm); vec3 v=normalize(eye-wpos);
 if(dot(n,v)<0.)n=-n;
 vec4 texel=hasTexture>.5?triplanar():vec4(1.);
 vec3 albedo=pow(clamp(color*texel.rgb,0.,1.),vec3(2.2));
 float m=clamp(metallic,0.,1.),r=clamp(roughness,.06,1.);
 vec3 key=normalize(vec3(-.40,.88,.32)),fill=normalize(vec3(.72,.38,-.49));
 vec3 c=albedo*(.17+.08*max(n.y,0.))*(1.-m*.65);
 c+=lightBRDF(n,v,key,vec3(2.9,2.75,2.55),albedo,m,r);
 c+=lightBRDF(n,v,fill,vec3(.82,.91,1.05),albedo,m,r);
 vec3 f0=mix(vec3(.04),albedo,m);float fres=pow(1.-max(dot(n,v),0.),5.);
 c+=mix(f0,vec3(.8,.86,.94),fres)*(.045+.09*(1.-r))*satin;
 vec3 environment=normalize(vec3(-.12,.91,.32));
 float softbox=pow(max(dot(reflect(-v,n),environment),0.),mix(5.,26.,1.-r));
 c+=mix(vec3(.08,.11,.15),albedo,m)*softbox*(.12+.24*(1.-r))*satin;
 c=c/(c+vec3(1.));c=pow(c,vec3(1./2.2));
 c=mix(c,min(c+vec3(.12,.07,.02),vec3(1.)),sel);
 o=vec4(c,opacity*texel.a);
})";
static const char* FS_CUBE=R"(#version 330 core
in vec3 nrm,wpos,lpos,lnrm;out vec4 o;uniform vec3 color;
void main(){float key=max(dot(normalize(nrm),normalize(vec3(-.35,.85,.48))),0.);o=vec4(color*(.88+.12*key),1.);})";
static const char* VS_GEOM=R"(#version 330 core
layout(location=0) in vec3 P; layout(location=1) in vec3 N;
uniform mat4 view,proj,model; out vec3 vpos,vn;
void main(){vec4 v=view*model*vec4(P,1.); vpos=v.xyz; vn=mat3(view)*mat3(model)*N; gl_Position=proj*v;}
)";
static const char* FS_GEOM=R"(#version 330 core
in vec3 vpos,vn; out vec4 o; uniform float hardSurface,depthFar;
void main(){
 vec3 n=hardSurface>0.?normalize(cross(dFdx(vpos),dFdy(vpos))):normalize(vn);
 if(dot(n,n)<1e-8)n=vec3(0.,0.,1.); if(dot(n,normalize(-vpos))<0.)n=-n;
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
layout(location=0) in vec3 P; uniform mat4 vp; out vec2 gridPos;
void main(){gl_Position=vp*vec4(P,1.);gridPos=P.xz;})";
static const char* FS_LINE=R"(#version 330 core
in vec2 gridPos; out vec4 o;
uniform float lightBackground;
void main(){vec3 col=mix(vec3(.40,.42,.46),vec3(.45,.48,.52),lightBackground); float a=mix(.28,.55,lightBackground); o=vec4(col,exp(-dot(gridPos,gridPos)*.00012)*a);})";
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
uniform vec4 fill,borderCol,rc; uniform float radius,borderW; uniform int glyph;
float seg(vec2 p,vec2 a,vec2 b){vec2 d=b-a;return length(p-a-d*clamp(dot(p-a,d)/dot(d,d),0.,1.));}
float box(vec2 p,vec2 b){vec2 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,q.y),0.);}
float mark(vec2 p){return min(min(seg(p,vec2(-.72,.65),vec2(0.,-.72)),seg(p,vec2(0.,-.72),vec2(.72,.65))),seg(p,vec2(-.27,.18),vec2(.51,.18)));}
float symbol(vec2 p,int g){
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

// ---------------------------------------------------------------------------
static void uploadMesh(GPUMesh& gm,const Mesh& m){
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
    if(part<g_partPicks.size())for(const auto& poly:g_partPicks[part].edges){
        if(poly.size()<2)continue;
        for(size_t k=1;k<poly.size();++k){for(const Vec3& p:{poly[k-1],poly[k]}){data.push_back((float)p.x);data.push_back((float)p.y);data.push_back((float)p.z);}}
        if(data.size()>300000)break;
    }
    glBindVertexArray(lines.vao);glBindBuffer(GL_ARRAY_BUFFER,lines.vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(data.size()*sizeof(float)),data.empty()?nullptr:data.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);glBindVertexArray(0);
    lines.verts=(int)(data.size()/3);
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
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())return nullptr;
    auto* f=g_doc.find(g_bodyIds[selBody]);
    return f&&(f->type==FeatureType::SheetMetal||f->type==FeatureType::SourceSheet)?f:nullptr;
}
static bool isSheetFeature(u64 id){const Feature* feature=g_doc.find(id);return feature&&(feature->type==FeatureType::SheetMetal||feature->type==FeatureType::SourceSheet);}
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
static void rebuildSketchOverlay(){
    g_sketchGpuDirty=true;g_sketchLines.clear();g_sketchPoints.clear();g_constraintHighlightLines.clear();g_datumLines.clear();g_curveSamples.clear();g_sketchBounds=AABB{};g_sketchDiagnosis=arkjson::Value{};
    auto put=[](std::vector<float>& into,Vec3 p){into.insert(into.end(),{(float)p.x,(float)p.y,(float)p.z});};
    for(auto& named:g_doc.sketches){if(!g_activeSketch.empty()&&named.name!=g_activeSketch)continue;auto frame=resolvePlane(g_doc,named.sk.plane);
        if(named.name==g_activeSketch){g_sketchDiagnosis=diagnoseSketch(named.sk);if(g_selectedSketchConstraint>=(int)named.sk.cons.size())g_selectedSketchConstraint=-1;g_sketchConstraintPage=std::min(g_sketchConstraintPage,(int)((named.sk.cons.size()+3)/4)-1);g_sketchConstraintPage=std::max(g_sketchConstraintPage,0);}
        const SketchCon* selected=(named.name==g_activeSketch&&g_selectedSketchConstraint>=0&&g_selectedSketchConstraint<(int)named.sk.cons.size())?&named.sk.cons[(size_t)g_selectedSketchConstraint]:nullptr;
        for(size_t ei=0;ei<named.sk.ents.size();++ei){auto& entity=named.sk.ents[ei];std::string entityName=entity.name.empty()?"entity."+std::to_string(ei):entity.name;std::vector<Vec2> sampled;try{sampled=sampleSketchEntity(entity,named.sk.tolerance,&named.sk);}catch(const std::exception&){continue;}for(size_t i=1;i<sampled.size();++i){auto a=frame.transformPoint({sampled[i-1].x,sampled[i-1].y,0}),b=frame.transformPoint({sampled[i].x,sampled[i].y,0});put(g_sketchLines,a);put(g_sketchLines,b);g_curveSamples.push_back({named.name,entityName,a,b});g_sketchBounds.grow(a);g_sketchBounds.grow(b);if(selected&&((int)ei==selected->a||(int)ei==selected->b)){put(g_constraintHighlightLines,a);put(g_constraintHighlightLines,b);}}
            size_t stride=entity.kind==SketchEnt::Circle?entity.dof.size():2;for(size_t i=0;i+1<entity.dof.size();i+=stride){auto point=frame.transformPoint({entity.dof[i],entity.dof[i+1],0});put(g_sketchPoints,point);g_sketchBounds.grow(point);}
            if(selected)for(auto ref:{std::pair{selected->a,selected->pa},std::pair{selected->b,selected->pb}})if(ref.first==(int)ei){
                size_t slot=entity.kind==SketchEnt::Line||entity.kind==SketchEnt::Spline?(size_t)std::max(ref.second,0)*2:0;
                if(slot+1<entity.dof.size()){double x=entity.dof[slot],y=entity.dof[slot+1];for(auto delta:{Vec2{2,0},Vec2{0,2}}){put(g_constraintHighlightLines,frame.transformPoint({x-delta.x,y-delta.y,0}));put(g_constraintHighlightLines,frame.transformPoint({x+delta.x,y+delta.y,0}));}}
            }
        }
    }
    if(g_doc.sketches.empty()||g_activeSketch.empty()){auto plane=resolvePlane(g_doc,g_activePlane);for(int i=-5;i<=5;++i){put(g_sketchLines,plane.transformPoint({i*10.,-50,0}));put(g_sketchLines,plane.transformPoint({i*10.,50,0}));put(g_sketchLines,plane.transformPoint({-50,i*10.,0}));put(g_sketchLines,plane.transformPoint({50,i*10.,0}));}}
    if(g_showDatumPlanes){
        for(const auto& datum:g_doc.planes){
            Mat4 frame;try{frame=resolvePlane(g_doc,datum.name);}catch(...){continue;}
            auto line=[&](Vec2 a,Vec2 b){put(g_datumLines,frame.transformPoint({a.x,a.y,0}));put(g_datumLines,frame.transformPoint({b.x,b.y,0}));};
            auto zAxis=[&](){put(g_datumLines,frame.transformPoint({0,0,-20}));put(g_datumLines,frame.transformPoint({0,0,20}));};
            if(datum.kind=="axis")zAxis();
            else if(datum.kind=="frame"){line({-20,0},{20,0});line({0,-20},{0,20});zAxis();}
            else{line({-20,0},{20,0});line({0,-20},{0,20});
                line({-20,-20},{20,-20});line({20,-20},{20,20});line({20,20},{-20,20});line({-20,20},{-20,-20});}
        }
    }
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
        std::set<u64> consumed;for(auto& f:g_doc.features)for(auto id:f.inputs)consumed.insert(id);
        for(auto& b:g_doc.evalBodies)if(!consumed.count(b.id)){auto* f=g_doc.find(b.id);placements.push_back({0,b.id,f?f->name:"Body",Mat4{},true});}
    }else if(!resolveAssembly(g_doc,placements,error,&g_assemblyDiagnostics)){g_error=true;g_status=error;}
    if(g_selectedMate>=(int)g_doc.mates.size())g_selectedMate=-1;
    g_cpuMeshes.clear();g_partPicks.clear();g_bodyIds.clear();g_instanceIds.clear();g_instanceNames.clear();g_instanceWorld.clear();g_instancePart.clear();
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
        auto i=part->second;g_bodyIds.push_back(placement.feature);g_instanceIds.push_back(placement.node);g_instanceNames.push_back(placement.name);g_instanceWorld.push_back(placement.world);g_instancePart.push_back(i);
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
    auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& candidate){return candidate.id==id;});
    if(body==g_doc.evalBodies.end())return;
    auto* feature=g_doc.find(id);if(!feature||!(feature->type==FeatureType::SheetMetal||feature->type==FeatureType::SourceSheet))return;
    double progress=sheetDisplayProgress(id);Mesh mesh;
    if(progress<=0){auto* flat=body->nativeData.find("flat_mesh");if(!flat)throw std::runtime_error("Native flat sheet preview is unavailable");mesh=nativePreviewMesh(*flat);}
    else if(progress>=1)mesh=body->tessellate(g_doc.tessOpt);
    else {if(feature->type!=FeatureType::SheetMetal||!body->nativeData.find("animation"))throw std::runtime_error("This sheet has no authored bend animation");mesh=buildSheetPreview(body->nativeData,progress).mesh;}
    std::set<size_t> changed;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id)changed.insert(g_instancePart[i]);
    for(size_t part:changed){g_cpuMeshes[part]=mesh;if(part<g_partPicks.size())g_partPicks[part]=progress>=1?partPickFrom(*body):PartPick{};uploadMesh(g_meshes[part],g_cpuMeshes[part]);uploadEdges(part);g_reports[part]=analyzeMesh(g_cpuMeshes[part]);g_bvhs[part]=MeshBVH(g_cpuMeshes[part]);}
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
    for(u64 id:g_bodyIds)if(isSheetFeature(id)&&seen.insert(id).second)ids.push_back(id);
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
    if(flat&&!oldMode)g_beforeFlatCamera={tYaw,tPitch,g_cameraOrtho};
    g_allSheetsPlaying=false;g_sheetPlayingId=0;g_instanceWorld=g_formedInstanceWorld;g_allSheetsFlat=false;
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
            tYaw=0;tPitch=1.52;g_cameraOrtho=true;
        }else{g_flatShowChassis=false;if(oldMode){tYaw=g_beforeFlatCamera.yaw;tPitch=g_beforeFlatCamera.pitch;g_cameraOrtho=g_beforeFlatCamera.orthographic;}}
        rebuildDisplayBounds();frameScene();g_error=false;
        g_status=flat?"Unfolded sheets / exploded display layout / document unchanged":"Formed assembly / document unchanged";
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
        tYaw=g_beforeStockCamera.yaw;tPitch=g_beforeStockCamera.pitch;g_cameraOrtho=g_beforeStockCamera.orthographic;
        rebuildDisplayBounds();frameScene();g_status="Formed tube or frame / document unchanged";return;
    }
    if(g_allSheetsFlat||g_allSheetsPlaying)allSheetPose(false);
    g_beforeStockCamera={tYaw,tPitch,g_cameraOrtho};
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
    g_stockMode=true;g_stockFeatureId=id;g_stockReport=std::move(preview.report);tYaw=0;tPitch=1.52;g_cameraOrtho=true;rebuildDisplayBounds();frameScene();
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
    g_status=hidden?"Body hidden in Studio / document unchanged":"Body shown in Studio / document unchanged";
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
        Vec3 outward{std::cos(tPitch)*std::sin(tYaw),std::sin(tPitch),std::cos(tPitch)*std::cos(tYaw)};
        Vec3 forward=outward*-1.,right=forward.cross({0,1,0});
        if(right.lensq()<1e-12)right={1,0,0};right=right.normalized();Vec3 up=right.cross(forward);
        const double focalPixels=(double)viewH/(2.*std::tan(35.*K_PI/360.));
        const double limitX=std::max(45.,.91*std::min(viewW*.5-126.,viewW*.5-55.));
        const bool layoutOverview=g_allSheetsFlat||g_stockMode;
        const double biasPixels=layoutOverview?std::min(60.,viewH*.11):0.;
        const bool suspensionSelection=selBody>=0&&(size_t)selBody<g_bodyIds.size()&&g_doc.find(g_bodyIds[(size_t)selBody])&&g_doc.find(g_bodyIds[(size_t)selBody])->type==FeatureType::Suspension;
        const double lowerControls=suspensionSelection?162.:(selectedNotchedTube()||selectedSourceTube()||selectedSheet())?112.:50.;
        const double limitY=layoutOverview?std::max(45.,.93*std::min(viewH*.5-185.+biasPixels,viewH*.5-55.-biasPixels)):
            std::max(45.,.82*std::min(viewH*.5-70.,viewH*.5-lowerControls));
        auto finishFit=[&](double distance){tDist=distance;if(layoutOverview)tTarget=tTarget+up*(biasPixels*distance/focalPixels);};
        // The enclosing box of a winged or sparse assembly has large empty
        // corners. Fit the actual display vertices once, analytically, so those
        // corners do not shrink the model. Animated hinges/canvases keep their
        // existing conservative envelope below. This never changes CAD geometry.
        if(!animation&&!selectedNotchedTube()&&!selectedSourceTube()&&
           g_doc.canvases.empty()&&!g_showSketches&&!g_bodyIds.empty()){
            double required=2.;
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
static arkjson::Value runTool(const std::string& tool,const arkjson::Value& args){
    flushPendingUsage();
    bool potentiallySlow=tool=="doc_load"||tool=="doc_apply"||tool=="doc_undo"||tool=="doc_redo"||tool=="import_step"||tool=="import_sheet_step"||tool=="import_mesh"||tool=="export_step"||tool=="export_iges"||tool=="export_mesh"||tool=="export_flat_dxf";
    auto result=potentiallySlow&&g_uiReady&&(!g_headless||g_busyTestMode)?callSessionWithProgress(tool,args):g_session.call(tool,args);g_error=result.find("isError")->b;
    u64 loadedUsageSerial=g_doc.usage.serial;
    if(g_error)g_status=result.find("structuredContent")->find("error")->asStr();
    else g_status=tool+" completed  /  revision "+std::to_string(g_session.revision);
    if(!g_error&&(tool=="doc_load"||tool=="doc_new"||tool=="import_step"||tool=="import_sheet_step")){g_sheetFlatViews.clear();g_sheetProgress.clear();g_sheetPlayingId=0;}
    bool appearanceOnly=false;
    if(tool=="doc_apply")if(auto* content=result.find("structuredContent"))if(auto* flag=content->find("geometry_recomputed"))appearanceOnly=flag->t==arkjson::Value::Bool&&!flag->b;
    if(!g_error&&!appearanceOnly&&(tool=="doc_apply"||tool=="doc_load"||tool=="doc_undo"||tool=="doc_redo"||tool=="import_step"||tool=="import_sheet_step"||tool=="import_mesh"||tool=="doc_new"))rebuildScene();
    if(!g_error&&(tool=="doc_load"||tool=="doc_new")){
        if(g_ownAssistantTool&&g_currentUsageReceipt)applyUsageReceipt(*g_currentUsageReceipt);
        if(!g_ownAssistantTool){g_pendingUsageReceipt.reset();g_currentUsageReceipt.reset();g_assistantReceiptId.clear();g_assistantUsageRun={};g_assistantUsageRunCounted=true;++g_documentEpoch;*g_assistantCancel=true;g_conversation=arkjson::Value::makeArr();g_chatLog.clear();g_chatLog.push_back({"Studio","Document changed. Describe what you want to create."});}
        if(tool=="doc_new"){g_path="Untitled.ark";g_savedRevision=g_session.revision;g_savedUsageSerial=0;g_activeSketch.clear();g_activePlane="XY";}
        g_foldProgress=selectedSourceTube()?1.:0.;if(tool=="doc_load")selBody=-1;frameScene();
    }
    flushPendingUsage();refreshDocumentUsage();
    if(!g_error&&(tool=="doc_save"||tool=="doc_load")){if(auto* path=args.find("path"))g_path=path->asStr();g_savedRevision=g_session.revision;g_savedUsageSerial=tool=="doc_load"?loadedUsageSerial:g_doc.usage.serial;}
    return result;
}
static void applyCommand(arkjson::Value command){
    using V=arkjson::Value;V a=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);
    if(command.find("op")&&command.find("op")->asStr()=="feature.add"&&!g_doc.assembly.empty()){
        V instance=V::makeObj();instance.set("op",V::makeStr("assembly.add"));instance.set("feature",V::makeNum((double)g_doc.nextFeatureId_));instance.set("name",V::makeStr(command.find("name")?command.find("name")->asStr():command.find("type")->asStr()));commands.arr.push_back(instance);
    }
    a.set("commands",commands);runTool("doc_apply",a);
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
        for(size_t i=0;i<g_dialogOverrides.size();++i){auto control=GetDlgItem(h,3+(int)i);char kind[32]{};GetClassNameA(control,kind,sizeof kind);if(std::strcmp(kind,"Button")==0)SendMessage(control,BM_SETCHECK,g_dialogOverrides[i]=="1"?BST_CHECKED:BST_UNCHECKED,0);else SetWindowTextA(control,g_dialogOverrides[i].c_str());}g_dialogOverrides.clear();
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
struct EditorField { std::string name,value;bool toggle=false; };
static bool editFields(const char* title,const char* hint,std::vector<EditorField>& fields,bool multiline=false){
    WNDCLASSA wc{};wc.lpfnWndProc=editorProc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="ARK_EDITOR";wc.hbrBackground=dialogBrush;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&wc);
    RECT r;GetWindowRect(g_window,&r);int width=multiline?840:620,height=multiline?520:190+(int)fields.size()*62;
    RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    int ow=bounds.right-bounds.left,oh=bounds.bottom-bounds.top;
    g_dialog=CreateWindowExA(WS_EX_DLGMODALFRAME,"ARK_EDITOR",title,WS_CAPTION|WS_SYSMENU,r.left+(r.right-r.left-ow)/2,r.top+(r.bottom-r.top-oh)/2,ow,oh,g_window,nullptr,wc.hInstance,nullptr);
    BOOL dark=TRUE;DwmSetWindowAttribute(g_dialog,20,&dark,sizeof dark);
    HFONT font=CreateFontA(-17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
    HFONT mono=CreateFontA(-17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Consolas");
    auto control=[&](const char* cls,const char* text,DWORD style,int x,int y,int w,int h,int id,HFONT f){HWND c=CreateWindowExA(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,g_dialog,(HMENU)(INT_PTR)id,wc.hInstance,nullptr);SendMessage(c,WM_SETFONT,(WPARAM)f,TRUE);return c;};
    control("STATIC",title,0,26,22,width-52,27,0,font);control("STATIC",hint,0,26,58,width-52,44,0,font);
    std::vector<HWND> edits;
    for(size_t i=0;i<fields.size();++i){int y=110+(int)i*62;
        if(!multiline)control("STATIC",fields[i].name.c_str(),0,28,y+7,205,28,0,font);
        HWND e=fields[i].toggle?control("BUTTON","",WS_TABSTOP|BS_AUTOCHECKBOX,235,y,40,33,3+(int)i,font):control("EDIT",fields[i].value.c_str(),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|(multiline?ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN:0),multiline?26:235,y,multiline?width-52:width-263,multiline?height-184:33,3+(int)i,mono);
        if(fields[i].toggle)SendMessage(e,BM_SETCHECK,fields[i].value=="1"?BST_CHECKED:BST_UNCHECKED,0);
        SendMessage(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));edits.push_back(e);
    }
    control("STATIC",multiline?"Ctrl+Enter to apply  /  Esc to cancel":"Enter to apply  /  Esc to cancel",0,26,height-52,width-290,34,0,font);
    control("BUTTON","Cancel",WS_TABSTOP|BS_OWNERDRAW,width-248,height-58,100,34,2,font);
    control("BUTTON","Apply",WS_TABSTOP|BS_OWNERDRAW,width-132,height-58,106,34,1,font);
    g_edit=edits.front();g_dialogDone=g_dialogAccept=false;EnableWindow(g_window,FALSE);ShowWindow(g_dialog,SW_SHOW);SetFocus(g_edit);SendMessage(g_edit,EM_SETSEL,0,-1);
    if(g_dialogSmoke)SetTimer(g_dialog,901,200,nullptr);
    MSG msg;while(!g_dialogDone&&GetMessage(&msg,nullptr,0,0)>0){
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
    if(g_session.revision==g_savedRevision&&g_doc.usage.serial==g_savedUsageSerial)return true;
    int answer=MessageBoxA(g_window,"Save changes to this ARK document?","Unsaved document",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(answer==IDCANCEL)return false;if(answer==IDYES){fileAction(true);return g_session.revision==g_savedRevision&&g_doc.usage.serial==g_savedUsageSerial;}return true;
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
    auto frame=resolvePlane(g_doc,g_activePlane);auto normal=frame.transformDir({0,0,1}).normalized();tYaw=std::atan2(normal.x,normal.z);tPitch=std::asin(clamp(normal.y,-.999,.999));
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
    return type==FeatureType::Loft||type==FeatureType::SurfaceFill||type==FeatureType::BSplineSurface||type==FeatureType::Sweep||type==FeatureType::ExtrudeSurface||type==FeatureType::RevolveSurface||type==FeatureType::OffsetSurface;
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
static void sheetOpeningDialog(bool edit){
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
    size_t patternStart=fields.size();if(!edit){fields.push_back({"Pattern count / 1..32","1"});fields.push_back({"Pattern step X / mm","28"});fields.push_back({"Pattern step Y / mm","0"});}
    if(!editFields(edit?"Edit sheet opening":"Add sheet opening","Dimensions accept expressions. Pattern repeats the opening without editing existing entities.",fields))return;
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
static void sheetEditMenu(){
    if(!authoredSheet())return;
    std::vector<EditorField> fields{{"Edit base, flange or opening","base"}};
    if(!editFields("Edit sheet metal","Choose which authored element to edit. The native sheet program and revision remain traceable.",fields))return;
    std::string choice=trimField(fields[0].value);
    if(choice=="base")editSheetBaseDialog();else if(choice=="flange")sheetFlangeDialog(true);
    else if(choice=="opening")sheetOpeningDialog(true);
    else throw std::runtime_error("Choose base, flange or opening");
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
    bool sheet=f->type==FeatureType::SheetMetal||f->type==FeatureType::SourceSheet;
    bool surface=isNativeSurfaceFeature(f->type)||f->type==FeatureType::NativeSolid;
    if(!sheet&&!surface){g_status="Select a native sheet or surface feature.";return;}
    std::string pose;if(sheet){std::vector<EditorField> fields{{"Pose: flat or formed","flat"}};
        if(testPath.empty()&&!editFields("Export native sheet STEP","Flat is the default developed blank. Formed exports the bent B-rep.",fields))return;
        pose=trimField(fields[0].value);if(pose!="flat"&&pose!="formed")throw std::runtime_error("Pose must be flat or formed");}
    std::string path=testPath;
    if(path.empty()){
        wchar_t file[32768]=L"ark-native.step";OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=file;of.nMaxFile=32768;
        of.lpstrFilter=L"STEP geometry\0*.step;*.stp\0";of.lpstrDefExt=L"step";of.lpstrTitle=sheet?L"Export native sheet STEP":L"Export native surface STEP";
        of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(!GetSaveFileNameW(&of))return;
        int size=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);path.resize(size);WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),size,nullptr,nullptr);path.pop_back();
    }
    V args=V::makeObj();args.set("id",V::makeNum((double)f->id));args.set("path",V::makeStr(path));if(sheet)args.set("pose",V::makeStr(pose));runTool("export_step",args);
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
    auto* sheet=selectedSheet();auto* body=selectedNativeBody();if(!sheet||!body)return;
    if(flat&&!body->nativeData.find("flat_mesh")){g_error=true;g_status="Native flat preview is unavailable for this sheet.";return;}
    g_sheetPlayingId=0;sheetPreviewSet(sheet->id,flat?0.:1.);frameScene();g_error=false;g_status=flat?"Flat sheet display / document unchanged":"Formed sheet display / document unchanged";
}
static void toggleSheetPlayback(){
    if(g_allSheetsFlat||g_allSheetsPlaying)allSheetPose(false);
    auto* sheet=selectedSheet();auto* body=selectedNativeBody();if(!sheet||sheet->type!=FeatureType::SheetMetal||!body||!body->nativeData.find("animation"))throw std::runtime_error("Select an authored sheet with a bend program");
    if(g_sheetPlayingId==sheet->id){g_sheetPlayingId=0;return;}
    if(sheetDisplayProgress(sheet->id)>=1)sheetPreviewSet(sheet->id,0);
    g_sheetPlayingId=sheet->id;g_status="Playing authored bend preview / document unchanged";
}
static u64 featureIdField(const std::string& value){
    double parsed=formNumber(value);if(parsed<1||parsed>9007199254740990.||std::floor(parsed)!=parsed||!g_doc.find((u64)parsed))throw std::runtime_error("Choose an existing feature ID");return (u64)parsed;
}
static std::string selectedFeatureId(){return selBody>=0&&selBody<(int)g_bodyIds.size()?std::to_string(g_bodyIds[(size_t)selBody]):"1";}
static void refreshSelectionLines(){
    g_selectionLines.clear();auto put=[&](Vec3 p){g_selectionLines.insert(g_selectionLines.end(),{(float)p.x,(float)p.y,(float)p.z});};
    for(const auto& item:g_picked){
        if(item.kind=="edge")for(size_t body=0;body<g_bodyIds.size()&&body<g_instancePart.size();++body)if(g_bodyIds[body]==item.feature){size_t part=g_instancePart[body];if(part>=g_partPicks.size())continue;auto& pick=g_partPicks[part];
            for(size_t edge=0;edge<pick.edgeNames.size()&&edge<pick.edges.size();++edge)if(pick.edgeNames[edge]==item.name)for(size_t k=1;k<pick.edges[edge].size();++k){put(g_instanceWorld[body].transformPoint(pick.edges[edge][k-1]));put(g_instanceWorld[body].transformPoint(pick.edges[edge][k]));}}
        else if(item.kind=="curve")for(const auto& sample:g_curveSamples)if(sample.sketch==item.owner&&(item.name.empty()||item.name==item.owner||sample.name==item.name)){put(sample.a);put(sample.b);}
    }
    g_sketchGpuDirty=true;
}
static void toggleSelection(SelectionItem item){
    auto same=[&](const SelectionItem& other){return other.kind==item.kind&&other.name==item.name&&other.owner==item.owner&&other.feature==item.feature;};
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
    if(!g_error){g_activeSketch=fields[0].value;g_activePlane=fields[1].value;g_showSketches=true;rebuildSketchOverlay();if(g_sketchBounds.valid()){tTarget=g_sketchBounds.center();tDist=std::max(80.,g_sketchBounds.diagonal()*1.8);}}
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
static void editSketchEntityFields(){
    auto* named=editableSketch();if(!named)throw std::runtime_error("Create a sketch first");
    std::string choices;for(size_t i=0;i<named->sk.ents.size();++i)choices+=(choices.empty()?"":", ")+entityChoiceLabel(named->sk,(int)i);
    std::vector<EditorField> choice{{"Entity name or #",named->sk.ents.empty()?"":entityChoiceLabel(named->sk,0)}};
    if(!editFields("Select sketch entity",choices.c_str(),choice))return;
    int index=sketchEntityChoice(named->sk,choice[0].value);auto sketch=sketchEditValue(*named);auto entities=*sketch.find("entities");auto& entity=entities.arr[(size_t)index];auto dof=*entity.find("dof");auto& coordinates=dof.arr;
    bool spline=named->sk.ents[(size_t)index].kind==SketchEnt::Spline;
    int anchor=0;
    if(spline){std::vector<EditorField> pick{{"Anchor # (1-based)","1"}};if(!editFields("Select spline anchor","Edit an interpolating anchor; the rest of the spline is preserved.",pick))return;
        try{anchor=std::stoi(trimField(pick[0].value))-1;}catch(...){throw std::runtime_error("Invalid spline anchor number");}
        if(anchor<0||2*anchor+1>=(int)coordinates.size())throw std::runtime_error("Spline anchor is outside this curve");}
    std::vector<EditorField> fields;
    static const char* pointLabels[]={"X / mm","Y / mm"};static const char* lineLabels[]={"Start X / mm","Start Y / mm","End X / mm","End Y / mm"};static const char* circleLabels[]={"Centre X / mm","Centre Y / mm","Radius / mm"};
    const char** labels=spline?pointLabels:named->sk.ents[(size_t)index].kind==SketchEnt::Line?lineLabels:named->sk.ents[(size_t)index].kind==SketchEnt::Circle?circleLabels:pointLabels;
    size_t start=spline?(size_t)anchor*2:0,count=spline?2:coordinates.size();
    for(size_t i=0;i<count;++i)fields.push_back({labels[i],sketchFieldText(coordinates[start+i])});
    if(!editFields("Edit sketch coordinates","Coordinates may be numbers or named parameter expressions.",fields))return;
    for(size_t i=0;i<count;++i)coordinates[start+i]=arkjson::Value::makeStr(trimField(fields[i].value));
    entity.set("dof",std::move(dof));sketch.set("entities",std::move(entities));
    applySketchEdit(named->name,std::move(sketch));
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
static void saveUiSettings(){
    using V=arkjson::Value;V j=V::makeObj();j.set("endpoint",V::makeStr(g_assistantConfig.endpoint));j.set("model",V::makeStr(g_assistantConfig.model));j.set("reasoning_effort",V::makeStr(g_assistantConfig.reasoningEffort));j.set("api_key_env",V::makeStr(g_assistantConfig.apiKeyEnv));j.set("assistant_auth_mode",V::makeStr(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan?"chatgpt_plan":"api_key"));j.set("grid",V::makeBool(g_grid));j.set("wireframe",V::makeBool(wireframe));j.set("light_background",V::makeBool(g_lightBackground));j.set("satin",V::makeBool(g_satin));j.set("show_datum_planes",V::makeBool(g_showDatumPlanes));j.set("occlusion_mode",V::makeNum(g_occlusionMode));j.set("occlusion_strength",V::makeNum(g_occlusionStrength));j.set("occlusion_radius",V::makeNum(g_occlusionRadius));j.set("show_edges",V::makeBool(g_showEdges));j.set("orthographic",V::makeBool(g_cameraOrtho));
    auto path=uiSettingsPath();std::filesystem::create_directories(path.parent_path());std::ofstream out(path);out<<j.dump();if(!out)throw std::runtime_error("Could not save Studio settings");
}
static void loadUiSettings(){
    try{std::ifstream in(uiSettingsPath());if(!in)return;std::string data((std::istreambuf_iterator<char>(in)),{});arkjson::Value j;if(!arkjson::parse(data,j))return;
        if(auto* v=j.find("endpoint"))g_assistantConfig.endpoint=v->asStr();if(auto* v=j.find("model"))g_assistantConfig.model=v->asStr();if(auto* v=j.find("reasoning_effort");v&&v->t==arkjson::Value::Str)g_assistantConfig.reasoningEffort=v->str;if(auto* v=j.find("api_key_env"))g_assistantConfig.apiKeyEnv=v->asStr();
        if(auto* v=j.find("assistant_auth_mode");v&&v->t==arkjson::Value::Str)g_assistantConfig.authMode=v->str=="chatgpt_plan"?AssistantAuthMode::ChatGptPlan:AssistantAuthMode::ApiKey;
        if(auto* v=j.find("grid"))g_grid=v->b;if(auto* v=j.find("wireframe"))wireframe=v->b;if(auto* v=j.find("light_background"))g_lightBackground=v->b;if(auto* v=j.find("satin"))g_satin=v->b;if(auto* v=j.find("show_datum_planes");v&&v->t==arkjson::Value::Bool)g_showDatumPlanes=v->b;
        if(auto* v=j.find("occlusion");v&&v->t==arkjson::Value::Bool)g_occlusionMode=v->b?2:0;
        if(auto* v=j.find("occlusion_mode");v&&v->t==arkjson::Value::Num)g_occlusionMode=std::clamp((int)v->num,0,3);
        if(auto* v=j.find("occlusion_strength");v&&v->t==arkjson::Value::Num)g_occlusionStrength=(float)clamp(v->num,0.,1.);
        if(auto* v=j.find("occlusion_radius");v&&v->t==arkjson::Value::Num)g_occlusionRadius=(float)clamp(v->num,.25,4.);
        if(auto* v=j.find("show_edges");v&&v->t==arkjson::Value::Bool)g_showEdges=v->b;if(auto* v=j.find("orthographic");v&&v->t==arkjson::Value::Bool)g_cameraOrtho=v->b;
    }catch(...){g_status="Studio settings unavailable; defaults loaded";}
}
static void graphicsDialog(){
    std::vector<EditorField> fields{{"Show ground grid",g_grid?"1":"0",true},{"Wireframe",wireframe?"1":"0",true},{"Light background",g_lightBackground?"1":"0",true},{"Visible edges",g_showEdges?"1":"0",true},{"Orthographic camera",g_cameraOrtho?"1":"0",true},{"AO mode / 0-3",std::to_string(g_occlusionMode)},{"AO strength / 0-1",std::to_string(g_occlusionStrength)},{"AO radius / 0.25-4",std::to_string(g_occlusionRadius)}};
    if(!editFields("Graphics settings","AO: 0 off, 1 contact, 2 ambient, 3 horizon. Display preferences stay local to Studio.",fields))return;
    bool values[5];for(int i=0;i<5;++i){double n=formNumber(fields[(size_t)i].value);if(n!=0&&n!=1)throw std::runtime_error("Graphics toggles accept 0 or 1");values[i]=n!=0;}
    double mode=formNumber(fields[5].value),strength=formNumber(fields[6].value),radius=formNumber(fields[7].value);
    if(mode<0||mode>3||std::floor(mode)!=mode||strength<0||strength>1||radius<.25||radius>4)throw std::runtime_error("AO mode is 0..3, strength is 0..1, radius is 0.25..4");
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
        {"Texture repeats U, V",scale}};
    if(!editFields("Body material","Presets are nominal. Edit every value for the actual grade. Image paths are relative to the ARK document.",fields))return;
    auto pairValues=[&](const std::string& value,int count){std::string clean=value;std::replace(clean.begin(),clean.end(),',',' ');std::istringstream input(clean);std::vector<double> values;double n;
        while(input>>n)values.push_back(n);if(!input.eof()||(int)values.size()!=count)throw std::runtime_error("Enter comma-separated numeric color or texture scale values");return values;};
    std::string name=trimField(fields[0].value);if(name.empty())throw std::runtime_error("Material name is required");
    double density=formNumber(fields[1].value),metallic=formNumber(fields[3].value),roughness=formNumber(fields[4].value),opacity=formNumber(fields[5].value);
    auto color=pairValues(fields[2].value,3),repeats=pairValues(fields[7].value,2);
    using V=arkjson::Value;V commands=V::makeArr(),material=V::makeObj(),ap=V::makeObj(),base=V::makeArr(),scaleValue=V::makeArr();
    for(double c:color)base.arr.push_back(V::makeNum(c));for(double s:repeats)scaleValue.arr.push_back(V::makeNum(s));
    ap.set("base_color",std::move(base));ap.set("metallic",V::makeNum(metallic));ap.set("roughness",V::makeNum(roughness));ap.set("opacity",V::makeNum(opacity));
    ap.set("texture_path",V::makeStr(trimField(fields[6].value)));ap.set("texture_scale",std::move(scaleValue));
    material.set("op",V::makeStr("material.set"));material.set("name",V::makeStr(name));material.set("density",V::makeNum(density));material.set("appearance",std::move(ap));commands.arr.push_back(std::move(material));
    V assign=V::makeObj();assign.set("op",V::makeStr("feature.edit"));assign.set("id",V::makeNum((double)feature->id));assign.set("material",V::makeStr(name));assign.set("appearance",V::makeObj());commands.arr.push_back(std::move(assign));
    std::string featureName=feature->name;
    V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",std::move(commands));runTool("doc_apply",args);
    if(!g_error)g_status="Material "+name+" applied to "+featureName;
}
static void physicalProperties(bool assemblyTotal=false){
    if(selBody<0&&g_doc.assembly.empty())throw std::runtime_error("Select a body to inspect physical properties");
    if(selBody>=0&&(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Selected body is unavailable");
    using V=arkjson::Value;g_physicalAssembly=assemblyTotal||selBody<0;
    if(g_physicalAssembly&&g_doc.assembly.empty())throw std::runtime_error("No assembly is active");
    V args=V::makeObj();if(!g_physicalAssembly)args.set("id",V::makeNum((double)g_bodyIds[(size_t)selBody]));
    V response=g_session.call(g_physicalAssembly?"assembly_info":"native_inspect",args);
    g_physicalError.clear();g_physicalReport=V::makeObj();
    if(auto* error=response.find("isError");error&&error->t==V::Bool&&error->b){
        auto* content=response.find("structuredContent");auto* reason=content?content->find("error"):nullptr;
        g_physicalError=reason?reason->asStr():"Physical properties unavailable";
    }else if(auto* content=response.find("structuredContent"))g_physicalReport=*content;
    g_physicalRevision=g_session.revision;g_physicalSelection=selBody>=0?(g_instanceIds[(size_t)selBody]?g_instanceIds[(size_t)selBody]:g_bodyIds[(size_t)selBody]):0;g_physicalVisible=true;
    g_status=g_physicalError.empty()?"Physical properties inspected at current revision":g_physicalError;
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
static void connectorDialog(){
    std::vector<EditorField> fields{{"Responses endpoint",g_assistantConfig.endpoint},{"Model",g_assistantConfig.model},{"API key environment",g_assistantConfig.apiKeyEnv}};
    if(!editFields("Assistant connector","Use a Responses-compatible API. Enter an environment-variable NAME, never the secret key.",fields))return;
    auto config=g_assistantConfig;if(config.authMode!=AssistantAuthMode::ApiKey||config.model!=fields[1].value)config.reasoningEffort.clear();config.authMode=AssistantAuthMode::ApiKey;config.endpoint=fields[0].value;config.model=fields[1].value;config.apiKeyEnv=fields[2].value;validateAssistantConfig(config);g_assistantConfig=config;*g_assistantCancel=true;++g_documentEpoch;g_conversation=arkjson::Value::makeArr();if(!g_dialogSmoke)saveUiSettings();
    g_status=assistantKeyAvailable(config)?"Connector configured; credential present. No request sent.":"Connector configured; credential environment variable is unavailable.";
}
static void startAuthJob(AuthJob job,std::string clientId={},bool requestPlanConsent=false){
    if(g_authBusy){g_status="ChatGPT account operation is already running";return;}
    if(g_authWorker.joinable())g_authWorker.join();
    g_authCancel=std::make_shared<std::atomic_bool>(false);auto cancel=g_authCancel;g_authBusy=true;g_authCancelRequested=false;
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
        AssistantConfig config=g_assistantConfig;config.authMode=AssistantAuthMode::ChatGptPlan;config.endpoint="https://api.openai.com/v1/responses";config.model=model;config.reasoningEffort.clear();
        validateAssistantConfig(config);g_assistantConfig=std::move(config);*g_assistantCancel=true;++g_documentEpoch;g_conversation=arkjson::Value::makeArr();saveUiSettings();
        g_status="ChatGPT plan model selected: "+model+" / reasoning effort Auto";
    }
}
static void chooseReasoningEffort(){
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
    AppendMenuA(menu,MF_STRING,357,"API key settings (advanced)");
    if(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan)AppendMenuA(menu,MF_STRING,358,"Switch to API key mode");
    POINT cursor;GetCursorPos(&cursor);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);
    DestroyMenu(menu);PostMessage(g_window,WM_NULL,0,0);if(action)interactiveAction(action);
}
static arkjson::Value cancelledTool(){using V=arkjson::Value;V r=V::makeObj(),e=V::makeObj();e.set("error",V::makeStr("Request cancelled before execution"));r.set("isError",V::makeBool(true));r.set("structuredContent",e);return r;}
static void drainUiTools(){
    if(g_dialog)return;
    while(!g_toolQueue.empty()){auto request=std::move(g_toolQueue.front());g_toolQueue.pop_front();
        if(g_closing||request->stop.stop_requested()||(request->cancel&&*request->cancel)){request->complete(cancelledTool());continue;}
        if(request->revision&&(request->epoch!=g_documentEpoch||*request->revision!=g_session.revision)){auto result=cancelledTool();arkjson::Value error=arkjson::Value::makeObj();g_status="Document changed during assistant turn. Inspect and resend from the current revision.";error.set("error",arkjson::Value::makeStr(g_status));result.set("structuredContent",error);g_chatLog.push_back({"Studio",g_status});request->complete(result);if(request->cancel)*request->cancel=true;continue;}
        try{g_ownAssistantTool=(bool)request->revision;auto result=runTool(request->tool,request->args);g_ownAssistantTool=false;if(request->revision)*request->revision=g_session.revision;request->complete(result);}catch(...){g_ownAssistantTool=false;request->complete(cancelledTool());}
    }
}
static void postTool(std::unique_ptr<UiToolRequest> request){
    if(g_closing||!PostMessage(g_window,WM_ARK_TOOL,0,(LPARAM)request.get())){request->complete(cancelledTool());return;}request.release();
}
static void postChat(UiChatEvent event){auto* pointer=new UiChatEvent(std::move(event));if(g_closing||!PostMessage(g_window,WM_ARK_CHAT,0,(LPARAM)pointer))delete pointer;}
static void postAuth(UiAuthEvent event){auto* pointer=new UiAuthEvent(std::move(event));if(g_closing||!PostMessage(g_window,WM_ARK_AUTH,0,(LPARAM)pointer))delete pointer;}
static void sendAssistant(const std::string& explicitPrompt="",const AssistantTransport& transport={}){
    if(g_chatBusy)return;std::string prompt=explicitPrompt;
    if(prompt.empty()&&g_chatInput){int n=GetWindowTextLengthW(g_chatInput);std::vector<wchar_t> buffer(n+1);GetWindowTextW(g_chatInput,buffer.data(),n+1);int size=WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,nullptr,0,nullptr,nullptr);prompt.resize(size);WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,prompt.data(),size,nullptr,nullptr);}
    if(prompt.find_first_not_of(" \r\n\t")==std::string::npos)return;validateAssistantConfig(g_assistantConfig);
    if(g_assistantWorker.joinable())g_assistantWorker.join();
    using V=arkjson::Value;V input=g_conversation;if(input.t!=V::Arr)input=V::makeArr();V user=V::makeObj();user.set("role",V::makeStr("user"));user.set("content",V::makeStr(prompt));input.arr.push_back(user);
    g_chatLog.push_back({"You",prompt});g_chatScroll=0;g_chatBusy=true;g_assistantUsageRun={};g_assistantUsageRunCounted=false;g_assistantReceiptId=newUsageReceiptId();g_assistantReceiptModel=g_assistantConfig.model;g_currentUsageReceipt.reset();g_pendingUsageReceipt.reset();g_assistantCancel=std::make_shared<std::atomic_bool>(false);auto cancel=g_assistantCancel;auto config=g_assistantConfig;auto schema=AgentSession::tools();if(g_chatInput)SetWindowTextW(g_chatInput,L"");
    auto epoch=g_documentEpoch;auto revision=std::make_shared<std::atomic<u64>>(g_session.revision);
    g_assistantWorker=std::thread([config,input,schema,cancel,transport,epoch,revision]{
        try{auto result=runAssistant(config,input,schema,[cancel,epoch,revision](const std::string& name,const V& args){
            auto promise=std::make_shared<std::promise<V>>();auto future=promise->get_future();auto request=std::make_unique<UiToolRequest>();request->tool=name;request->args=args;request->cancel=cancel;request->epoch=epoch;request->revision=revision;request->complete=[promise](V result){promise->set_value(std::move(result));};postTool(std::move(request));
            while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready)if(*cancel||g_closing)throw std::runtime_error("Assistant cancelled");
            return future.get();
        },[epoch](const AssistantEvent& event){if(event.kind==AssistantEvent::ToolStart)postChat({"Tool: "+event.text,false,false,{},epoch});else if(event.kind==AssistantEvent::Usage)postChat({"",false,false,{},epoch,event.usage,true});},cancel.get(),transport);
            if(*cancel)throw std::runtime_error("Assistant cancelled");postChat({result.text,true,false,result.conversation,epoch,result.usage,true});
        }catch(const std::exception& e){postChat({e.what(),true,true,{},epoch});}
    });
}
static void startLiveBridge(){
    if(!g_liveBridge.start([](const std::string& tool,const arkjson::Value& args,LiveSessionBridge::Completion complete,std::stop_token stop){
        auto request=std::make_unique<UiToolRequest>();request->tool=tool;request->args=args;request->complete=std::move(complete);request->stop=stop;postTool(std::move(request));
    }))g_status="Live MCP unavailable: "+g_liveBridge.lastError();
}
static void stopIntegrations(){
    g_closing=true;*g_assistantCancel=true;*g_authCancel=true;g_liveBridge.stop();
    MSG msg{};while(PeekMessage(&msg,g_window,WM_ARK_TOOL,WM_ARK_AUTH,PM_REMOVE)){if(msg.message==WM_ARK_TOOL){std::unique_ptr<UiToolRequest> request((UiToolRequest*)msg.lParam);request->complete(cancelledTool());}else if(msg.message==WM_ARK_CHAT)delete (UiChatEvent*)msg.lParam;else if(msg.message==WM_ARK_AUTH)delete (UiAuthEvent*)msg.lParam;}
    while(!g_toolQueue.empty()){g_toolQueue.front()->complete(cancelledTool());g_toolQueue.pop_front();}
    if(g_assistantWorker.joinable())g_assistantWorker.join();
    if(g_authWorker.joinable())g_authWorker.join();
    while(PeekMessage(&msg,g_window,WM_ARK_TOOL,WM_ARK_AUTH,PM_REMOVE)){if(msg.message==WM_ARK_TOOL){std::unique_ptr<UiToolRequest> request((UiToolRequest*)msg.lParam);request->complete(cancelledTool());}else if(msg.message==WM_ARK_CHAT)delete (UiChatEvent*)msg.lParam;else if(msg.message==WM_ARK_AUTH)delete (UiAuthEvent*)msg.lParam;}
}
static void liveConnectionDialog(){
    std::string text=g_liveBridge.endpoint().empty()?"Live MCP is not running.":("Connect Codex or another MCP client to this exact open document:\r\n\r\nark-mcp --connect \""+g_liveBridge.endpoint()+"\"\r\n\r\nCall agent_onboard first. This local pipe is restricted to the current Windows user.\r\n\r\nThe API assistant connector is configured separately; this endpoint is not an API credential.");
    editText("Live document / Codex MCP connection",text,true);
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
    if(editing){
        std::vector<EditorField> fields{{"Solid",editing->get("solid","0"),true}};
        std::string hint="Inputs stay "+joinedFeatureIds(editing->inputs)+". A solid requires one closed shell. This edit does not change which surfaces are sewn.";
        if(!editFields("Edit sew",hint.c_str(),fields))return;
        V p=V::makeObj();p.set("solid",V::makeStr(fields[0].value));
        commitNativeFeature(FeatureType::Sew,"Sew",std::move(p),V::makeObj(),{},editing->id,false);return;
    }
    std::string initial=selectedFeatureId();
    if(g_bodyIds.size()>=2)initial=std::to_string(g_bodyIds[g_bodyIds.size()-2])+"; "+std::to_string(g_bodyIds.back());
    std::vector<EditorField> fields{{"Surface feature IDs",initial},{"Solid","0",true}};
    if(!editFields("Sew native surfaces","List 2 to 16 face or shell IDs, separated by semicolons. A solid requires one closed shell.",fields))return;
    std::vector<u64> ids;std::istringstream names(fields[0].value);std::string token;
    while(std::getline(names,token,';')){token=trimField(token);if(token.empty())continue;ids.push_back(featureIdField(token));}
    if(ids.size()<2||ids.size()>16)throw std::runtime_error("Sew needs 2 to 16 features");
    V p=V::makeObj();p.set("solid",V::makeStr(fields[1].value));
    addNativeFeature(FeatureType::Sew,"Sew",std::move(p),V::makeObj(),ids);
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
        case FeatureType::Loft:loftDialog(feature);break;
        case FeatureType::SurfaceFill:surfaceFillDialog(feature);break;
        case FeatureType::BSplineSurface:nurbsSurfaceDialog(feature);break;
        case FeatureType::Thicken:thickenDialog(feature);break;
        case FeatureType::Sweep:sweepDialog(feature);break;
        case FeatureType::ExtrudeSurface:extrudeSurfaceDialog(feature);break;
        case FeatureType::RevolveSurface:revolveSurfaceDialog(feature);break;
        case FeatureType::OffsetSurface:offsetSurfaceDialog(feature);break;
        case FeatureType::Sew:sewDialog(feature);break;
        case FeatureType::MeshWeld:meshWeldDialog(feature);break;
        case FeatureType::MeshSmooth:meshSmoothDialog(feature);break;
        case FeatureType::MeshDecimate:meshDecimateDialog(feature);break;
        case FeatureType::MeshOffset:meshOffsetDialog(feature);break;
        case FeatureType::MeshFill:meshFillDialog(feature);break;
        case FeatureType::NativeSolid:{
            auto op=feature->get("op");
            if(op=="fillet"||op=="chamfer")nativeEdgeBlendDialog(feature);
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
static void commandPalette(){
    HMENU menu=CreatePopupMenu();
    auto group=[&](const char* title,std::initializer_list<std::pair<int,const char*>> items){HMENU sub=CreatePopupMenu();for(auto item:items)AppendMenuA(sub,MF_STRING,item.first,item.second);AppendMenuA(menu,MF_POPUP,(UINT_PTR)sub,title);};
    group("Document",{{55,"New document"},{17,"Open ARK"},{16,"Save ARK"},{52,"Import source STEP"},{213,"Export STL/OBJ/FBX/STEP/IGES"},{51,"Export straight tube STEP"},{82,"Export native STEP"}});
    group("Sketch & planes",{{60,"Create / edit plane"},{113,"Attach plane to feature"},{69,"Select plane"},{72,"Toggle datum frames"},{61,"Rectangle"},{62,"Circle / opening"},{63,"Interpolating spline"},{70,"Edit entity coordinates"},{73,"Add constraint"},{89,"Edit selected constraint"},{90,"Remove selected constraint"},{64,"Edit raw sketch JSON"},{65,"Extrude sketch"},{66,"Cut from sketch"},{67,"Named parameter"},{68,"Toggle sketch overlay"}});
    group("Solids",{{1,"Box"},{2,"Cylinder"},{3,"Sphere"},{4,"Cone"},{5,"Torus"},{6,"Polygon extrusion"},{7,"Revolve"},{11,"Linear pattern"}});
    group("Exact solids",{{100,"Native primitive"},{101,"Extrude saved sketch"},{102,"Union / cut / common"},{103,"Hole / slot / profile pocket"},{104,"Fillet / chamfer"},{106,"Draft faces"},{107,"Linear / circular pattern"},{108,"Mirror solid"},{105,"Rigid transform"}});
    group("Modify",{{8,"Union"},{9,"Subtract bodies"},{10,"Intersect"},{108,"Mirror solid"},{25,"Delete selection"},{18,"Undo"},{19,"Redo"}});
    group("Tube & assembly",{{215,"Create double-wishbone suspension"},{384,"Animate suspension corner"},{26,"Bend-program tube"},{40,"Laser tube"},{27,"Create assembly"},{28,"Duplicate instance"},{29,"Instance placement"},{93,"Ground / unground instance"},{94,"Add assembly mate"},{95,"Edit selected mate"},{96,"Remove selected mate"},{30,"Edit tube program"}});
    group("Native surfaces",{{161,"Edit selected feature"},{162,"Edit driving sketch"},{76,"Loft sketches"},{77,"Fill closed sketch"},{78,"NURBS surface"},{150,"Sweep sketches"},{151,"Extrude sketch surface"},{152,"Revolve sketch surface"},{153,"Offset selected surface"},{154,"Sew surfaces"},{79,"Thicken selected surface"}});
    group("Mesh",{{155,"Weld mesh"},{156,"Smooth mesh"},{157,"Decimate mesh"},{158,"Offset mesh"},{159,"Fill planar holes"},{160,"Import STL/OBJ"}});
    group("Sheet metal",{{120,"Create sheet base"},{127,"Edit sheet base"},{121,"Add flange"},{122,"Edit flange"},{123,"Remove flange"},{124,"Add opening or pattern"},{125,"Edit opening"},{126,"Remove opening"},{327,"Export analytic flat DXF"},{128,"Play / pause bend preview"},{80,"Create L bend"},{81,"Import sheet STEP"},{83,"Show formed"},{84,"Show flat"}});
    group("View & agents",{{20,"Fit document"},{71,"Normal to sketch plane"},{180,"Front"},{181,"Back"},{23,"Top"},{184,"Bottom"},{182,"Right"},{183,"Left"},{24,"Isometric"},{175,"Orthographic"},{174,"Visible edges"},{186,"Cycle ambient occlusion"},{187,"Edit selected body material"},{188,"Inspect mass and center of mass"},{176,"Orbit"},{177,"Pan"},{178,"Zoom"},{14,"Wireframe"},{22,"Grid"},{56,"Graphics settings"},{57,"ChatGPT account"},{357,"API key settings (advanced)"},{58,"Codex / live MCP"},{21,"Raw agent command"},{31,"Help"}});
    POINT point;GetCursorPos(&point);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,g_window,nullptr);DestroyMenu(menu);if(id)interactiveAction(id);
}
static void focusSourceOpening(int index){
    auto* f=selectedSourceTube();if(!f)return;auto& cache=g_sourceViews.at(f->id);if(index<0||index>=(int)cache.preview.holes.arr.size())return;
    auto& opening=cache.preview.holes.arr[index];auto* center=opening.find("flat_center");auto* segment=opening.find("segment");if(!center||center->arr.size()!=3||!segment)return;
    auto matrices=displayedSourceTransforms(selBody);size_t span=(size_t)segment->num;if(span>=matrices.size())return;
    Vec3 point{center->arr[0].num,center->arr[1].num,center->arr[2].num};auto world=g_instanceWorld[selBody]*matrices[span];tTarget=world.transformPoint(point);tDist=100.;
    // Align only the inspection camera; source meshes and their transforms are unchanged.
    if(auto* normal=opening.find("flat_normal");normal&&normal->arr.size()==3){Vec3 n{normal->arr[0].num,normal->arr[1].num,normal->arr[2].num};
        if(n.dot(point-g_reports[g_instancePart[selBody]].bounds.center())<0)n=-n;n=world.transformDir(n).normalized();tYaw=std::atan2(n.x,n.z);tPitch=std::asin(clamp(n.y,-.999,.999));
    }
    g_sourceOpening=index;g_foldPlaying=false;g_status="Inspecting original opening "+std::to_string(index+1)+" / F fits the complete part";
}
static void setOrbitView(double nyaw,double npitch,bool ortho,const char* name){
    tYaw=nyaw;tPitch=clamp(npitch,-1.52,1.52);g_cameraOrtho=ortho;
    if(name)g_status=std::string(name)+(ortho?" view / orthographic":" view / perspective");
}
static void uiAction(int id){
    using V=arkjson::Value;
    if(id>=14000&&id<15000){size_t index=(size_t)(id-14000);if(index<g_doc.planes.size()){
        g_activePlane=g_doc.planes[index].name;g_activeSketch.clear();g_showDatumPlanes=true;rebuildSketchOverlay();alignSketchCamera();tTarget=resolvePlane(g_doc,g_activePlane).transformPoint({});tDist=120;
        if(g_pickKind==1){SelectionItem item;item.kind="plane";item.name=g_activePlane;toggleSelection(item);}
        else g_status="Datum "+g_activePlane+" / "+g_doc.planes[index].kind;
    }return;}
    if(id==97){g_matePage=std::max(0,g_matePage-1);return;}
    if(id==98){++g_matePage;return;}
    if(id>=13000&&id<14000){int index=id-13000;if(index<(int)g_doc.mates.size())g_selectedMate=index;return;}
    if(id>=12000&&id<13000){int index=id-12000;if(auto* named=editableSketch();named&&index<(int)named->sk.cons.size()){g_selectedSketchConstraint=index;rebuildSketchOverlay();}return;}
    if(id>=11000&&id<12000){size_t index=id-11000;if(index<g_doc.sketches.size()){g_activeSketch=g_doc.sketches[index].name;g_activePlane=g_doc.sketches[index].sk.plane;g_selectedSketchConstraint=-1;g_sketchConstraintPage=0;g_showSketches=true;rebuildSketchOverlay();alignSketchCamera();if(g_pickKind==3){SelectionItem item;item.kind="curve";item.owner=g_activeSketch;item.name=g_activeSketch;toggleSelection(item);}}return;}
    if(id>=10000){focusSourceOpening(id-10000);return;}
    if(id>=9000){g_selectedNotch=id-9000;return;}
    if(id>=500&&id<532){size_t index=(size_t)(id-500);if(index>=g_authStatus.accounts.size()||g_authBusy)return;
        std::string clientId=g_authStatus.accounts[index].clientId;g_authStatus=chatGptSelectAccount(clientId);g_authModels.clear();
        g_assistantConfig.authMode=AssistantAuthMode::ApiKey;g_assistantConfig.model.clear();*g_assistantCancel=true;++g_documentEpoch;g_conversation=V::makeArr();saveUiSettings();
        if(g_authStatus.connected&&g_authStatus.planAuthorized)startAuthJob(AuthJob::Models);else startAuthJob(AuthJob::SignIn,clientId);return;}
    if(id>=2000){selBody=id-2000;g_activeSketch.clear();g_showSketches=false;rebuildSketchOverlay();g_selectedNotch=0;g_notchScroll=0;g_sourceScroll=0;g_foldPlaying=false;return;}
    if(id>=1000){size_t i=id-1000;if(i>=g_doc.params.size())return;std::string value=g_doc.params[i].expr,name=g_doc.params[i].name;
        if(editText(("Edit parameter: "+name).c_str(),value)){V c=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(name));c.set("expr",V::makeStr(value));applyCommand(c);}return;}
    switch(id){
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
        case 55:newDocument();break;case 56:graphicsDialog();break;case 57:showAssistantAccountMenu();break;case 58:liveConnectionDialog();break;case 59:commandPalette();break;case 187:materialDialog();break;case 188:physicalProperties();break;case 189:g_physicalVisible=false;break;case 190:physicalProperties(!g_physicalAssembly);break;
        case 357:connectorDialog();break;case 358:g_assistantConfig.authMode=AssistantAuthMode::ApiKey;g_assistantConfig.model.clear();g_assistantConfig.reasoningEffort.clear();*g_assistantCancel=true;++g_documentEpoch;g_conversation=V::makeArr();saveUiSettings();g_status="API key mode selected. Configure model in Advanced settings.";break;
        case 360:startAuthJob(AuthJob::SignIn);break;case 361:chooseChatGptModel();break;case 362:startAuthJob(AuthJob::SignOut);break;
        case 364:if(g_authStatus.activeClientId.empty())throw std::runtime_error("Choose a saved ChatGPT account first");startAuthJob(AuthJob::SignIn,g_authStatus.activeClientId,true);break;
        case 365:if(g_authStatus.pendingClientId.empty())throw std::runtime_error("No pending ChatGPT registration to retry");startAuthJob(AuthJob::SignIn,g_authStatus.pendingClientId);break;
        case 366:cancelAuthJob();break;
        case 367:chooseReasoningEffort();break;
        case 300:case 301:case 302:case 303:case 304:g_toolModule=id-300;break;
        case 210:createConstructionFrame();break;case 211:createPcb();break;case 212:createCableRoute();break;case 213:exportModelDialog();break;case 215:createSuspensionDialog();break;
        case 383:if(!selectedSuspension())throw std::runtime_error("Select a suspension to animate");g_suspSpinPlaying=!g_suspSpinPlaying;g_suspTick=0;g_status=g_suspSpinPlaying?"Kinematic corner preview playing / document unchanged":"Kinematic corner preview paused";break;
        case 384:{if(!selectedSuspension()){auto found=std::find_if(g_bodyIds.begin(),g_bodyIds.end(),[&](u64 id){auto* feature=g_doc.find(id);return feature&&feature->type==FeatureType::Suspension;});
                if(found==g_bodyIds.end())throw std::runtime_error("No visible suspension corner in this document");selBody=(int)std::distance(g_bodyIds.begin(),found);}
            g_toolModule=1;if(!g_spotlightActive||g_spotlightBody!=selBody)spotlightBody();g_suspSpinPlaying=true;g_suspTick=0;g_suspClock=0;
            g_status="Suspension corner Spotlight and kinematic animation / document unchanged";break;}
        case 320:allSheetPose(true);break;case 321:allSheetPose(false);break;
        case 322:toggleAllSheetPlayback();break;case 323:g_allSheetsPlaying=false;g_status="Unbend preview paused / document unchanged";break;
        case 324:if(!g_allSheetsFlat)throw std::runtime_error("Open the unfolded sheet layout first");g_flatShowChassis=!g_flatShowChassis;rebuildDisplayBounds();frameScene();g_status=g_flatShowChassis?"Unfolded sheets with chassis / document unchanged":"Unfolded sheets isolated / document unchanged";break;
        case 325:sheetEditMenu();break;case 327:exportFlatDxf();break;case 330:usageDrawer();break;
        case 340:setBodyHidden(true);break;case 341:setBodyHidden(false);break;case 342:spotlightBody();break;
        case 343:restoreSpotlight();break;case 344:showAllBodies();break;
        case 326:toggleStockPreview();break;
        case 60:planeDialog();break;case 61:shapeSketchDialog(0);break;case 62:shapeSketchDialog(1);break;case 63:shapeSketchDialog(2);break;case 64:editSketchDialog();break;case 65:sketchFeatureDialog(false);break;case 66:sketchFeatureDialog(true);break;case 67:parameterDialog();break;
        case 70:editSketchEntityFields();break;case 72:g_showDatumPlanes=!g_showDatumPlanes;rebuildSketchOverlay();saveUiSettings();break;case 73:editSketchConstraintFields(true);break;
        case 89:editSketchConstraintFields(false);break;case 90:removeSketchConstraint();break;
        case 91:g_sketchConstraintPage=std::max(0,g_sketchConstraintPage-1);break;
        case 92:if(auto* named=editableSketch())g_sketchConstraintPage=std::min((int)(named->sk.cons.size()/4),g_sketchConstraintPage+1);break;
        case 100:nativePrimitiveDialog();break;case 101:nativeExtrudeDialog();break;case 102:nativeBooleanDialog();break;
        case 103:nativeHoleDialog();break;case 104:nativeEdgeBlendDialog();break;case 105:nativeTransformDialog();break;case 106:nativeDraftDialog();break;case 107:nativePatternDialog();break;case 108:nativeMirrorDialog();break;case 113:supportedDatumDialog();break;
        case 68:g_showSketches=!g_showSketches;rebuildSketchOverlay();break;
        case 69:{std::string available="XY, XZ, YZ";for(auto& plane:g_doc.planes)available+=", "+plane.name;std::vector<EditorField> fields{{"Plane name",g_activePlane}};if(editFields("Select construction plane",available.c_str(),fields)){resolvePlane(g_doc,fields[0].value);g_activePlane=fields[0].value;g_activeSketch.clear();g_showSketches=true;rebuildSketchOverlay();}}break;
        case 71:alignSketchCamera();break;
        case 74:sendAssistant();break;case 75:*g_assistantCancel=true;break;
        case 76:loftDialog();break;case 77:surfaceFillDialog();break;case 78:nurbsSurfaceDialog();break;case 79:thickenDialog();break;
        case 150:sweepDialog();break;case 151:extrudeSurfaceDialog();break;case 152:revolveSurfaceDialog();break;case 153:offsetSurfaceDialog();break;case 154:sewDialog();break;
        case 155:meshWeldDialog();break;case 156:meshSmoothDialog();break;case 157:meshDecimateDialog();break;case 158:meshOffsetDialog();break;case 159:meshFillDialog();break;case 160:importMeshDialog();break;
        case 161:editSelectedFeature();break;case 162:editDrivingSketch();break;
        case 170:g_pickKind=0;g_picked.clear();refreshSelectionLines();g_status="Body selection / semantic picks cleared";break;
        case 171:g_pickKind=1;g_status="Face selection. Click a face, or a datum in the explorer for a mirror plane.";break;
        case 172:g_pickKind=2;g_status="Edge selection. Fillet and chamfer use these names.";break;
        case 173:g_pickKind=3;g_showSketches=true;rebuildSketchOverlay();g_status="Curve selection. Loft and sweep use the sketch of each curve.";break;
        case 174:g_showEdges=!g_showEdges;g_status=g_showEdges?"Visible edges":"Edges hidden";try{saveUiSettings();}catch(...){}break;
        case 175:g_cameraOrtho=!g_cameraOrtho;g_status=g_cameraOrtho?"Orthographic view":"Perspective view";try{saveUiSettings();}catch(...){}break;
        case 176:g_navMode=0;g_status="Orbit. Drag in the view, or drag the view cube.";break;
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
            if(!g_doc.assembly.empty()){g_error=true;g_status="Edit part geometry before assembly; instance boolean operations are not available.";break;}
            if(g_bodyIds.size()>=2)addFeat(FeatureType::Boolean,{},{g_bodyIds[g_bodyIds.size()-2],g_bodyIds.back()},id-8);
            else {g_status="Create two output bodies before a boolean operation.";g_error=true;}break;
        case 11:if(!g_bodyIds.empty())addFeat(FeatureType::LinearPattern,{{"dx","100"},{"count","3"}},{g_bodyIds.back()});break;
        case 12:fileAction(true,true);break;
        case 14:wireframe=!wireframe;break;
        case 16:fileAction(true);break;
        case 17:fileAction(false);break;
        case 18:runTool("doc_undo",V::makeObj());break;
        case 19:runTool("doc_redo",V::makeObj());break;
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
            std::vector<EditorField> fields{{"Mate type",current?current->type:"coincident"},
                {"Instance A #",a?std::to_string(a):""},{"Reference A",current?current->reference_a:"origin"},
                {"Instance B #",b?std::to_string(b):""},{"Reference B",current?current->reference_b:"origin"},
                {"Value / expression",current?current->value:"0"},
                {"Reference A fraction",current?std::to_string(current->fraction_a):"0.5"},
                {"Reference B fraction",current?std::to_string(current->fraction_b):"0.5"}};
            if(!editFields(add?"Add assembly mate":"Edit assembly mate",
                "Ground one instance first. Types: concentric, coincident, contact, distance, angle. References: origin/X/Y/Z/XY or exact face/axis names.",fields))break;
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
            applyCommand(c);if(!g_error&&add)g_selectedMate=(int)g_doc.mates.size()-1;
            break;
        }
        case 96:{
            if(g_selectedMate<0||g_selectedMate>=(int)g_doc.mates.size()){g_status="Select a mate in the inspector first.";break;}
            V c=V::makeObj();c.set("op",V::makeStr("mate.remove"));c.set("id",V::makeNum((double)g_doc.mates[(size_t)g_selectedMate].id));
            applyCommand(c);if(!g_error)g_selectedMate=-1;break;
        }

        case 31:MessageBoxA(g_window,"ARK 0.9 / Agent-Ready Kernel\nParametric CAD for people and agents.\n\nUse Commands or the tool dock to model with editable expressions.\nDouble-click a feature to edit its definition, or a sketch to edit its anchors. The inspector also has Edit feature and Edit sketch.\nNative solids and surfaces use OCCT B-reps. Sheets have formed/flat poses and a read-only bend preview.\nGround an assembly instance, add a mate, and inspect remaining degrees of freedom.\nSketch diagnosis and exact reference inspection are available through the agent tools.\n\nThe view toolbar selects Orbit, Pan, Zoom, Fit, Front, Top, Right, and Isometric.\nThe view cube snaps to a face. Dragging it orbits. Named faces are orthographic.\nAmbient occlusion and visible edges are display only and are not saved in the document.\n\nOrbit: drag  |  Pan: Shift + drag  |  Zoom: wheel\nFit: F  |  Wireframe: W  |  Views: 1 front, 3 top, 5 right, 7 isometric\nOpen: Ctrl+O  |  Save: Ctrl+S  |  Undo / redo: Ctrl+Z / Y\nCommands: Ctrl+K  |  New: Ctrl+N\n\nThe native inspector reports OCCT B-rep checks. It does not certify manufacturing suitability.","About ARK / Workspace guide",MB_OK|MB_ICONINFORMATION);break;
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
static Mat4 cameraView(){return Mat4::lookAt(camEye(),target,{0,1,0});}
static void viewRay(int mx,int my,Vec3& origin,Vec3& direction){
    Vec3 eye=camEye(),forward=(target-eye).normalized(),right=forward.cross({0,1,0});
    if(right.lensq()<1e-10)right={1,0,0};right=right.normalized();Vec3 up=right.cross(forward);
    double nx=(double)(mx-viewX)/std::max(viewW,1)*2-1,ny=1-(double)(my-viewY)/std::max(viewH,1)*2;
    if(g_cameraOrtho){double halfH=std::max(2.0,dist*std::tan(35*K_PI/360.0));double halfW=halfH*(double)viewW/std::max(viewH,1);origin=eye+right*(nx*halfW)+up*(ny*halfH);direction=forward;}
    else{double h=std::tan(deg2rad(35)*.5);origin=eye;direction=(forward+right*(nx*h*viewW/std::max(viewH,1))+up*(ny*h)).normalized();}
}
struct SurfaceHit { int body=-1; u32 triangle=0; Vec3 local; bool hit=false; };
static SurfaceHit pickSurface(int mx,int my){
    SurfaceHit best;if(g_cpuMeshes.empty()||mx<viewX||my<viewY||mx>=viewX+viewW||my>=viewY+viewH)return best;
    Vec3 eye,rd;viewRay(mx,my,eye,rd);
    f64 bt=1e300;
    for(size_t bi=0;bi<g_bodyIds.size();++bi){if(!displayInstance(bi))continue;
        if(auto* f=g_doc.find(g_bodyIds[bi]);f&&f->type==FeatureType::SourceTube){
            auto& cache=g_sourceViews.at(f->id);auto matrices=displayedSourceTransforms(bi);double progress=(int)bi==selBody?g_foldProgress:0.;updateSourceHinges(cache,progress);
            auto cast=[&](const Mesh& mesh,const MeshBVH& bvh,const Mat4& world){auto inv=world.inverted();auto hit=bvh.raycast(mesh,{inv.transformPoint(eye),inv.transformDir(rd)});if(hit.hit&&hit.distance<bt){bt=hit.distance;best={((int)bi),hit.triangle,hit.point,true};}};
            for(size_t k=0;k<cache.preview.rigid.size();++k)cast(cache.preview.rigid[k],cache.rigidBvhs[k],g_instanceWorld[bi]*matrices[k]);
            for(size_t k=0;k<cache.preview.bends.size();++k)cast(progress>0?cache.hinges[k]:cache.flatHinges[k],progress>0?cache.hingeBvhs[k]:cache.flatHingeBvhs[k],g_instanceWorld[bi]*matrices[k]);
            continue;
        }
        auto cached=g_notchViews.find(g_bodyIds[bi]);
        if(cached!=g_notchViews.end()&&g_doc.find(g_bodyIds[bi])->type==FeatureType::NotchedTube){
            auto transforms=displayedNotchTransforms(bi);auto& cache=cached->second;
            for(size_t segment=0;segment<transforms.size();++segment){auto inv=(g_instanceWorld[bi]*transforms[segment]).inverted();
                auto hit=cache.bvhs[segment].raycast(cache.preview.segments[segment],{inv.transformPoint(eye),inv.transformDir(rd)});
                if(hit.hit&&hit.distance<bt){bt=hit.distance;best={(int)bi,hit.triangle,hit.point,true};}
            }
        }else{size_t part=g_instancePart[bi];Mat4 inv=g_instanceWorld[bi].inverted();
            auto hit=g_bvhs[part].raycast(g_cpuMeshes[part],{inv.transformPoint(eye),inv.transformDir(rd)});
            if(hit.hit&&hit.distance<bt){bt=hit.distance;best={(int)bi,hit.triangle,hit.point,true};}
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
static bool pickNamedEdgeScreen(int mx,int my){
    struct Candidate{size_t body=0,edge=0;float pixels=0,sx=0,sy=0;Vec3 world;};
    std::vector<Candidate> candidates;Mat4 vp=cameraProjection()*cameraView();
    auto project=[&](Vec3 point,float& x,float& y){double w=vp.m[3]*point.x+vp.m[7]*point.y+vp.m[11]*point.z+vp.m[15];
        if(w<=1e-6)return false;Vec3 clip=vp.transformPoint(point);x=viewX+(float)((clip.x/w+1.)*.5*viewW);y=viewY+(float)((1.-clip.y/w)*.5*viewH);return true;};
    constexpr float limitPixels=11.f;
    for(size_t body=0;body<g_bodyIds.size();++body){if(!displayInstance(body))continue;size_t part=g_instancePart[body];if(part>=g_partPicks.size())continue;
        const PartPick& pick=g_partPicks[part];
        for(size_t edge=0;edge<pick.edges.size()&&edge<pick.edgeNames.size();++edge){if(pick.edgeNames[edge].empty())continue;
            const auto& points=pick.edges[edge];for(size_t segment=1;segment<points.size();++segment){
                Vec3 a=g_instanceWorld[body].transformPoint(points[segment-1]),b=g_instanceWorld[body].transformPoint(points[segment]);float ax,ay,bx,by;
                if(!project(a,ax,ay)||!project(b,bx,by))continue;float dx=bx-ax,dy=by-ay,span=dx*dx+dy*dy;
                float t=span<1e-6f?0.f:std::clamp(((mx-ax)*dx+(my-ay)*dy)/span,0.f,1.f);
                float sx=ax+t*dx,sy=ay+t*dy,px=sx-mx,py=sy-my,pixels=std::sqrt(px*px+py*py);
                if(pixels<=limitPixels)candidates.push_back({body,edge,pixels,sx,sy,a+(b-a)*t});
            }
        }
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
        if(a.pixels!=b.pixels)return a.pixels<b.pixels;return a.body!=b.body?a.body<b.body:a.edge<b.edge;
    });
    Vec3 eye=camEye();size_t probes=0;
    for(const Candidate& candidate:candidates){if(++probes>20)break;
        SurfaceHit blocker=pickSurface((int)std::lround(candidate.sx),(int)std::lround(candidate.sy));
        if(blocker.hit){Vec3 worldHit=g_instanceWorld[(size_t)blocker.body].transformPoint(blocker.local);
            double candidateDepth=(candidate.world-eye).len(),hitDepth=(worldHit-eye).len();
            double mmPerPixel=2.*(g_cameraOrtho?std::max(2.,dist*std::tan(35*K_PI/360.)):candidateDepth*std::tan(35*K_PI/360.))/std::max(viewH,1);
            if(hitDepth+std::max(.4,mmPerPixel*4.)<candidateDepth)continue;
        }
        selBody=(int)candidate.body;SelectionItem item;item.feature=g_bodyIds[candidate.body];item.kind="edge";
        item.name=g_partPicks[g_instancePart[candidate.body]].edgeNames[candidate.edge];toggleSelection(std::move(item));return true;
    }
    return false;
}
static void pickReference(int mx,int my){
    if(g_pickKind==3){float best=8.f;const CurveSample* chosen=nullptr;
        for(const auto& sample:g_curveSamples){float ax,ay,bx,by;if(!projectScreen(sample.a,ax,ay)||!projectScreen(sample.b,bx,by))continue;float abx=bx-ax,aby=by-ay,len=abx*abx+aby*aby;float t=len<1e-6?0.f:clamp(((mx-ax)*abx+(my-ay)*aby)/len,0.f,1.f);float dx=ax+abx*t-mx,dy=ay+aby*t-my;float distance=std::sqrt(dx*dx+dy*dy);if(distance<best){best=distance;chosen=&sample;}}
        if(!chosen){g_status="Click a visible sketch curve";return;}
        SelectionItem item;item.kind="curve";item.owner=chosen->sketch;item.name=chosen->name;toggleSelection(item);return;
    }
    if(g_allSheetsFlat||g_stockMode){g_status="Preview meshes have no persistent face or edge references. Return to Formed for semantic picking.";return;}
    if(g_pickKind==2){if(!pickNamedEdgeScreen(mx,my))g_status="Move closer to a visible named edge (11 px tolerance)";return;}
    SurfaceHit hit=pickSurface(mx,my);if(!hit.hit){g_status=g_pickKind==1?"Click a face":"Click an edge";return;}
    selBody=hit.body;size_t part=g_instancePart[(size_t)hit.body];if(part>=g_partPicks.size()){g_status="This body has no named faces or edges";return;}
    const PartPick& pick=g_partPicks[part];
    if(g_pickKind==1){int face=-1;for(int index=0;index<(int)pick.faces.size();++index)if((int)hit.triangle>=pick.faces[(size_t)index].first&&(int)hit.triangle<pick.faces[(size_t)index].first+pick.faces[(size_t)index].second)face=index;
        if(face<0||pick.faceNames[(size_t)face].empty()){g_status="This face has no persistent name";return;}
        SelectionItem item;item.feature=g_bodyIds[(size_t)hit.body];item.kind="face";item.name=pick.faceNames[(size_t)face];toggleSelection(item);return;
    }
}

// ---------------------------------------------------------------------------
static GLuint pBg,pBusy,pSolid,pCube,pGrid,pShadow,pUI,pText,pSketch,pGeom,pAo,pCompose,pEdge,pCanvas;
static GLint suVp,suModel,suOutline,suColor,suEye,suSel,suHard;
static GLint guView,guProj,guModel,guHard,auProj,auFocal,auOrtho,auRadius,auBias,cuOrigin,euVp,euModel;
static GLint uuVp,uuRc,uuRad,uuFill,uuBorder,uuBW,uuGlyph;
static GLuint quadVao,gridVao,textVao,textVbo,gFbo=0,gGeomTex=0,gAoTex=0,gDepthTex=0,gCubeVao=0,gCubeVbo=0,gCompassVao=0,gCompassVbo=0;
struct CubeTriangle{Vec3 a,b,c,normal;int face=-1;};
struct CubeHit{bool hit=false;int face=-1;Vec3 normal{};};
static std::vector<CubeTriangle> g_cubeTriangles;
static CubeHit g_cubeHover;
static GLuint g_busySnapshotTex=0;
static int gridCount=0,gOccW=0,gOccH=0,gOccTriedW=-1,gOccTriedH=-1;
static bool g_swap=true,gOccReady=false;   // headless readback disables the buffer swap
static GLuint canvasTexture(const std::string& path);
static GLuint materialTexture(const std::string& path){
    if(path.empty())return 0;
    std::filesystem::path file=std::filesystem::u8path(path);
    if(file.is_relative()&&!g_path.empty())file=std::filesystem::u8path(g_path).parent_path()/file;
    auto utf8=file.u8string();GLuint tex=canvasTexture(std::string(utf8.begin(),utf8.end()));
    return tex==g_canvasMissing?0:tex;
}
static void drawSurfaces(bool geometry,const Mat4& view,const Mat4& proj,const GLfloat vpT[16],const Vec3& eye,bool transparentOnly=false){
    GLint modelLoc=geometry?guModel:suModel,hardLoc=geometry?guHard:suHard;
    if(geometry){GLfloat vm[16],pm[16];for(int i=0;i<16;++i){vm[i]=(GLfloat)view.m[i];pm[i]=(GLfloat)proj.m[i];}glUseProgram(pGeom);glUniformMatrix4fv(guView,1,GL_FALSE,vm);glUniformMatrix4fv(guProj,1,GL_FALSE,pm);glUniform1f(glGetUniformLocation(pGeom,"depthFar"),(float)g_projectionFar);}
    else{glUseProgram(pSolid);glUniform1f(glGetUniformLocation(pSolid,"satin"),g_satin?1.f:0.f);glUniformMatrix4fv(suVp,1,GL_FALSE,vpT);glUniform3f(suEye,(float)eye.x,(float)eye.y,(float)eye.z);glUniform1i(glGetUniformLocation(pSolid,"surfaceTexture"),2);}
    if(!wireframe){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1.f,1.f);}else glDisable(GL_POLYGON_OFFSET_FILL);
    auto setModel=[&](const Mat4& world){GLfloat m[16];for(int k=0;k<16;++k)m[k]=(GLfloat)world.m[k];glUniformMatrix4fv(modelLoc,1,GL_FALSE,m);};
    std::vector<size_t> order;
    for(size_t i=0;i<g_bodyIds.size();++i){if(!displayInstance(i))continue;
        const Feature* feature=g_doc.find(g_bodyIds[i]);
        bool transparent=feature&&resolvedAppearance(g_doc,*feature).opacity<.999;
        if((transparentOnly&&transparent)||(!transparentOnly&&!transparent))order.push_back(i);
    }
    if(transparentOnly)std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){
        auto center=[&](size_t i){return g_reports[g_instancePart[i]].bounds.transformed(g_instanceWorld[i]).center();};
        return (center(a)-eye).lensq()>(center(b)-eye).lensq();
    });
    for(size_t i:order){
        if(!geometry){
            const Feature* feature=g_doc.find(g_bodyIds[i]);
            Appearance a=feature?resolvedAppearance(g_doc,*feature):Appearance{};
            glUniform3f(suColor,(float)a.baseColor.x,(float)a.baseColor.y,(float)a.baseColor.z);
            glUniform1f(glGetUniformLocation(pSolid,"metallic"),(float)a.metallic);
            glUniform1f(glGetUniformLocation(pSolid,"roughness"),(float)a.roughness);
            glUniform1f(glGetUniformLocation(pSolid,"opacity"),(float)a.opacity);
            glUniform2f(glGetUniformLocation(pSolid,"textureScale"),(float)(a.textureScaleU/100.),(float)(a.textureScaleV/100.));
            GLuint tex=materialTexture(a.texturePath);
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
                glUniform1f(glGetUniformLocation(pSolid,"hasTexture"),0.f);glUniform1f(glGetUniformLocation(pSolid,"opacity"),1.f);}
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
        glBindVertexArray(gm.vao);glPolygonMode(GL_FRONT_AND_BACK,!geometry&&wireframe?GL_LINE:GL_FILL);glDrawElements(GL_TRIANGLES,gm.count*3,GL_UNSIGNED_INT,nullptr);
        if(!geometry&&!wireframe&&g_instancePart[i]<g_partPicks.size()){glUniform3f(suColor,.93f,.58f,.26f);glUniform1f(suSel,1.f);glPolygonOffset(-1.f,-1.f);
            auto& pick=g_partPicks[g_instancePart[i]];
            for(const auto& item:g_picked)if(item.kind=="face"&&item.feature==g_bodyIds[i])for(size_t face=0;face<pick.faceNames.size()&&face<pick.faces.size();++face)if(pick.faceNames[face]==item.name&&pick.faces[face].second>0)glDrawElements(GL_TRIANGLES,pick.faces[face].second*3,GL_UNSIGNED_INT,(void*)((size_t)pick.faces[face].first*3*sizeof(unsigned)));
            glPolygonOffset(1.f,1.f);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    if(!geometry){glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);}
}
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
    if(g_occlusionMode==0||wireframe||viewW<2||viewH<2||!ensureOcclusionTargets(viewW,viewH))return;
    glBindFramebuffer(GL_FRAMEBUFFER,gFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,gGeomTex,0);
    glViewport(0,0,viewW,viewH);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glClearColor(0.f,0.f,1.f,1.f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    drawSurfaces(true,view,proj,vpT,eye);
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
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glUseProgram(pEdge);glUniformMatrix4fv(euVp,1,GL_FALSE,vpT);
    float c=g_lightBackground?.46f:.88f;glUniform3f(glGetUniformLocation(pEdge,"color"),c,c+.01f,c+.02f);
    for(size_t i=0;i<g_bodyIds.size();++i){if(!displayInstance(i))continue;
        if(auto* feature=g_doc.find(g_bodyIds[i]);feature&&resolvedAppearance(g_doc,*feature).opacity<.999)continue;
        size_t part=g_instancePart[i];if(part>=g_edgeLines.size()||g_edgeLines[part].verts<2)continue;GLfloat m[16];for(int k=0;k<16;++k)m[k]=(GLfloat)g_instanceWorld[i].m[k];glUniformMatrix4fv(euModel,1,GL_FALSE,m);glBindVertexArray(g_edgeLines[part].vao);glDrawArrays(GL_LINES,0,g_edgeLines[part].verts);}
}
static void cubeCamera(Vec3& eye,Mat4& view,Mat4& proj){
    Vec3 dw{std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};eye=dw*4.2;view=Mat4::lookAt(eye,{},{0,1,0});proj=Mat4::ortho(-1.5,1.5,-1.5,1.5,0.05,20);
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
    glUseProgram(pCube);glUniformMatrix4fv(glGetUniformLocation(pCube,"vp"),1,GL_FALSE,vpT);glUniformMatrix4fv(glGetUniformLocation(pCube,"model"),1,GL_FALSE,ident);glUniform1f(glGetUniformLocation(pCube,"outline"),0.f);
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
    IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;IWICBitmapScaler* scaler=nullptr;
    bool ok=false;
    if(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))
        &&SUCCEEDED(decoder->GetFrame(0,&frame))
        &&SUCCEEDED(factory->CreateFormatConverter(&converter))
        &&SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom))){
        IWICBitmapSource* source=converter;UINT srcW=0,srcH=0;converter->GetSize(&srcW,&srcH);
        if((srcW>2048||srcH>2048)&&SUCCEEDED(factory->CreateBitmapScaler(&scaler))){
            double scale=std::min(2048.0/std::max(srcW,1u),2048.0/std::max(srcH,1u));
            UINT dw=std::max(1u,(UINT)std::lround(srcW*scale)),dh=std::max(1u,(UINT)std::lround(srcH*scale));
            if(SUCCEEDED(scaler->Initialize(converter,dw,dh,WICBitmapInterpolationModeFant)))source=scaler;
        }
        UINT w=0,h=0;
        if(SUCCEEDED(source->GetSize(&w,&h))&&w>0&&h>0&&w<=2048&&h<=2048){
            rgba.resize((size_t)w*h*4);
            if(SUCCEEDED(source->CopyPixels(nullptr,w*4,(UINT)rgba.size(),rgba.data()))){
                std::vector<unsigned char> row((size_t)w*4);
                for(UINT y=0;y<h/2;++y){auto* a=rgba.data()+(size_t)y*w*4;auto* b=rgba.data()+(size_t)(h-1-y)*w*4;std::copy(a,a+w*4,row.begin());std::copy(b,b+w*4,a);std::copy(row.begin(),row.end(),b);}
                width=w;height=h;ok=true;
            }
        }
    }
    if(scaler)scaler->Release();if(converter)converter->Release();if(frame)frame->Release();if(decoder)decoder->Release();factory->Release();
    return ok;
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
        GLuint tex=canvasTexture(canvas.path);bool live=tex&&tex!=g_canvasMissing;
        glUniformMatrix4fv(glGetUniformLocation(pCanvas,"model"),1,GL_FALSE,matrix);
        glUniform1f(glGetUniformLocation(pCanvas,"opacity"),(float)clamp(canvas.opacity,0.,1.));
        glUniform1f(glGetUniformLocation(pCanvas,"live"),live?1.f:0.f);
        glBindTexture(GL_TEXTURE_2D,live?tex:g_canvasMissing);
        glDrawArrays(GL_TRIANGLES,0,6);
    }
    glBindTexture(GL_TEXTURE_2D,0);glDisable(GL_POLYGON_OFFSET_FILL);
}
static void drawFrame(HDC hdc){
    static auto previous=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
    double elapsed=std::min(.10,std::chrono::duration<double>(now-previous).count());advanceFold(elapsed);advanceSheet(elapsed);advanceSuspension(elapsed);previous=now;
    yaw+=(tYaw-yaw)*.2; pitch+=(tPitch-pitch)*.2; dist+=(tDist-dist)*.18;
    target=target+(tTarget-target)*.2;
    layout();
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

    glUseProgram(pGrid);
    glUniformMatrix4fv(glGetUniformLocation(pGrid,"vp"),1,GL_FALSE,vpT);
    glUniform1f(glGetUniformLocation(pGrid,"lightBackground"),g_lightBackground?1.f:0.f);
    glBindVertexArray(gridVao);
    if(g_grid&&!g_allSheetsFlat&&!g_stockMode)glDrawArrays(GL_LINES,0,gridCount);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    drawCanvases(vpT);
    drawSurfaces(false,view,proj,vpT,eye);
    applyOcclusion(view,proj,vpT,eye);
    if(g_showEdges&&!wireframe)drawFeatureEdges(vpT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    drawSurfaces(false,view,proj,vpT,eye,true);
    glDepthMask(GL_TRUE);

    if((g_showSketches&&!g_sketchLines.empty())||(g_showDatumPlanes&&!g_datumLines.empty())||!g_selectionLines.empty()){
        glDisable(GL_DEPTH_TEST);glUseProgram(pSketch);glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,vpT);glBindVertexArray(sketchVao);glBindBuffer(GL_ARRAY_BUFFER,sketchVbo);
        if(g_sketchGpuDirty){std::vector<float> buffers=g_sketchLines;buffers.insert(buffers.end(),g_sketchPoints.begin(),g_sketchPoints.end());buffers.insert(buffers.end(),g_constraintHighlightLines.begin(),g_constraintHighlightLines.end());buffers.insert(buffers.end(),g_datumLines.begin(),g_datumLines.end());buffers.insert(buffers.end(),g_selectionLines.begin(),g_selectionLines.end());glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(buffers.size()*sizeof(float)),buffers.empty()?nullptr:buffers.data(),GL_DYNAMIC_DRAW);g_sketchGpuDirty=false;}
        if(g_showSketches){glUniform4f(glGetUniformLocation(pSketch,"tint"),.62,.77,.98,.85);glDrawArrays(GL_LINES,0,(GLsizei)g_sketchLines.size()/3);
            if(!g_sketchPoints.empty()){glPointSize(6);glDrawArrays(GL_POINTS,(GLint)g_sketchLines.size()/3,(GLsizei)g_sketchPoints.size()/3);glPointSize(1);}
            glUniform4f(glGetUniformLocation(pSketch,"tint"),1.,.66,.22,1.);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size())/3,(GLsizei)g_constraintHighlightLines.size()/3);}
        if(g_showDatumPlanes){glUniform4f(glGetUniformLocation(pSketch,"tint"),.55,.77,.68,.48);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size()+g_constraintHighlightLines.size())/3,(GLsizei)g_datumLines.size()/3);}
        if(!g_selectionLines.empty()){glLineWidth(2.5f);glUniform4f(glGetUniformLocation(pSketch,"tint"),.97,.62,.28,1.);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size()+g_constraintHighlightLines.size()+g_datumLines.size())/3,(GLsizei)g_selectionLines.size()/3);glLineWidth(1.f);}
    }
    // ============================ UI =========================================
    glDisable(GL_DEPTH_TEST);
    glViewport(0,0,winW,winH);
    float W=(float)winW,H=(float)winH;
    glUseProgram(pUI);
    glUniform2f(uuVp,W,H);
    auto rect=[&](float x,float y,float w,float h,float rad,
                  float fr,float fg,float fb,float fa,
                  float br,float bg2,float bb,float ba,float bw){
        glUniform1i(uuGlyph,0);
        glUniform4f(uuRc,x,y,w,h);
        glUniform1f(uuRad,rad);
        glUniform4f(uuFill,fr,fg,fb,fa);
        glUniform4f(uuBorder,br,bg2,bb,ba);
        glUniform1f(uuBW,bw);
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES,0,6);
    };
    POINT mp; GetCursorPos(&mp);
    HWND hw=WindowFromDC(hdc); ScreenToClient(hw,&mp);

    textBegin();g_zones.clear();g_hoverZone=-1;
    auto panel=[&](float x,float y,float w,float h,float r=0.f){rect(x,y,w,h,r,.088f,.092f,.102f,1,.21f,.22f,.24f,.65f,1);};
    auto label=[&](const std::string& t,float x,float y,float scale=.75f){textOut(t,x,y,scale,.55f,.57f,.62f);};
    auto glyph=[&](int id,float x,float y,float size,float c=.82f){
        glUniform1i(uuGlyph,id);glUniform4f(uuRc,x,y,size,size);
        glUniform4f(uuFill,c,c+.01f,c+.025f,1);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);
        glUniform1i(uuGlyph,0);
    };
    auto button=[&](const char* t,float x,float y,float w,int id,bool accent=false,bool active=false){
        bool hover=mp.x>=x&&mp.x<x+w&&mp.y>=y&&mp.y<y+32;
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
            if(x<viewX+14||x>viewX+viewW-14||y<viewY+70||y>viewY+viewH-14)continue;
            int source=(int)cache.sourceIndices[k];bool selected=source==g_selectedNotch;
            rect(x-12,y-12,24,24,12,selected?.78f:.22f,selected?.82f:.25f,selected?.89f:.30f,1,.85,.88,.95,.9,1);
            std::string number=std::to_string(source+1);textOut(number,x-textWidth(number,.65f)/2,y-8,.65f,selected?.10f:.90f,selected?.12f:.92f,selected?.15f:.96f);
            g_zones.push_back({x-14,y-14,28,28,9000+source});
        }
    }
    panel(0,0,W,76);panel(0,76,258,H-104);panel(W-300,76,300,H-104);
    rect(17,17,35,35,8,.16,.17,.19,1,.39,.42,.47,.55,1);glyph(99,21,21,27,.95f);
    textOut("ARK",64,18,1.25f);label("STUDIO",65,43,.48f);
    button("New",132,20,54,55);button("Open",194,20,54,17);
    std::string filename=g_path.substr(g_path.find_last_of("/\\")+1);if(g_session.revision!=g_savedRevision||g_doc.usage.serial!=g_savedUsageSerial)filename+=" *";
    size_t maxFilename=(size_t)std::max(16.f,(W-1000)/9);if(filename.size()>maxFilename)filename=filename.substr(0,maxFilename-3)+"...";
    textOut(filename,viewX+24,18,.86f);label("LOCAL DOCUMENT  /  REV "+std::to_string(g_session.revision),viewX+24,45,.54f);
    float tokenX=W-588;rect(tokenX,20,130,32,8,.12f,.18f,.24f,1,.36f,.53f,.68f,.72f,1);
    std::string tokenBadge=tokenBadgeLabel();
    textOut(tokenBadge,tokenX+(130-textWidth(tokenBadge,.70f))*.5f,27,.70f,.77f,.90f,.99f);g_zones.push_back({tokenX,20,130,32,330});
    button("Undo",W-438,20,64,18);button("Redo",W-366,20,64,19);button("Save",W-294,20,76,16);button("Export",W-208,20,76,213,true);button("Commands",W-124,20,106,59);
    float ribbonScale=std::clamp(((float)viewW-622.f)/420.f,0.f,1.f);
    const float ribbonX=(float)viewX+12,ribbonY=(float)viewY+12,ribbonW=std::min((float)viewW-(110.f+35.f*ribbonScale)-36.f,704.f);
    rect(ribbonX,ribbonY,ribbonW,g_toolModule==4?151.f:106.f,12,.067f,.081f,.105f,.97f,.34f,.43f,.56f,.82f,1.2f);
    const char* moduleNames[]={"CONSTRUCTION","FRAMES","SURFACE","ELECTRONICS","SHEET METAL"};
    float tabW=(ribbonW-28.f)/5.f;
    for(int module=0;module<5;++module){float x=ribbonX+9+module*tabW;bool active=g_toolModule==module;
        rect(x,ribbonY+8,tabW-5,29,6,active?.20f:.10f,active?.31f:.13f,active?.42f:.17f,1,active?.42f:.22f,active?.67f:.31f,active?.86f:.43f,1,1);
        if(active)rect(x+8,ribbonY+34,tabW-21,2,1,.42,.78,.97,1,0,0,0,0,0);
        const char* tabTitle=module==4&&tabW<99?"SHEET":moduleNames[module];
        float textScale=tabW<100?.48f:tabW<126?.53f:.64f;textOut(tabTitle,x+(tabW-5-textWidth(tabTitle,textScale))*.5f,ribbonY+14,textScale,active?.90f:.66f,active?.95f:.75f,active?1.f:.85f);
        g_zones.push_back({x,ribbonY+8,tabW-5,29,300+module});}
    struct RibbonAction{const char* title;int id;};
    static const RibbonAction construction[]={{"Plane",60},{"Rect",61},{"Spline",63},{"Extrude",65},{"Cut",66}};
    static const RibbonAction frames[]={{"Frame",210},{"Susp.",215},{"Corner",384},{"Tube",26},{"Assembly",27},{"Mate",94}};
    static const RibbonAction surface[]={{"Loft",76},{"Fill",77},{"NURBS",78},{"Sweep",150},{"Thicken",79}};
    static const RibbonAction electronics[]={{"PCB",211},{"Cable",212},{"Material",187},{"Physics",188}};
    static const RibbonAction sheetActions[]={{"New base",120},{"Flange",121},{"Opening",124},{"Edit",325},{"Import",81}};
    static const RibbonAction poses[]={{"Formed",321},{"Flat",320},{"Unbend",322},{"Pause",323},{"Chassis",324},{"Stock",326}};
    const RibbonAction* actions=g_toolModule==1?frames:g_toolModule==2?surface:g_toolModule==3?electronics:g_toolModule==4?sheetActions:construction;
    int actionCount=g_toolModule==3?4:g_toolModule==1?6:5;float actionW=(ribbonW-30.f-(actionCount-1)*6.f)/actionCount;
    for(int i=0;i<actionCount;++i)button(actions[i].title,ribbonX+15+i*(actionW+6),ribbonY+56,actionW,actions[i].id,i==0);
    if(g_toolModule==4){float poseW=(ribbonW-30.f-5*6.f)/6.f;
        for(int i=0;i<6;++i){bool current=i==0?!g_allSheetsFlat&&!g_allSheetsPlaying&&!g_stockMode:i==1?g_allSheetsFlat:i==2?g_allSheetsPlaying:i==4?g_allSheetsFlat&&g_flatShowChassis:i==5?g_stockMode:false;
            button(poses[i].title,ribbonX+15+i*(poseW+6),ribbonY+101,poseW,poses[i].id,current,current);}}
    label("DOCUMENT",20,101,.66f);label(std::to_string(g_doc.features.size()),221,101,.66f);
    textOut("Model explorer",20,132,1.02f);label(std::to_string(g_doc.features.size())+" features / "+std::to_string(g_bodyIds.size())+" instances",20,162,.68f);
    float treeY=200;
    struct ExplorerItem{std::string name,subtitle;int zone=-1;bool selected=false;int depth=0;int kind=0;};
    std::vector<ExplorerItem> explorer;
    size_t featureRows=g_doc.assembly.empty()?g_doc.features.size():g_doc.assembly.size();
    for(size_t i=0;i<featureRows;++i){
        ExplorerItem row;
        if(g_doc.assembly.empty()){
            auto& f=g_doc.features[i];row.name=f.name;row.subtitle=std::string(featureTypeName(f.type))+" #"+std::to_string(f.id);if(f.suppressed)row.subtitle+=" / suppressed";
            for(size_t j=0;j<g_bodyIds.size();++j)if(g_bodyIds[j]==f.id){row.zone=2000+(int)j;row.selected|=selBody==(int)j;
                if(j<g_hiddenBodies.size()&&g_hiddenBodies[j])row.subtitle+=" / hidden";}
        }else{
            auto& node=g_doc.assembly[i];bool group=node.feature==0;row.kind=group?3:0;row.name=node.name;row.subtitle=group?"Subassembly":"Part #"+std::to_string(node.feature)+" / node "+std::to_string(node.id);
            u64 parent=node.parent;while(parent&&row.depth<5){++row.depth;auto it=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==parent;});if(it==g_doc.assembly.end())break;parent=it->parent;}
            int copies=0,hiddenCopies=0;for(size_t j=0;j<g_instanceIds.size();++j)if(g_instanceIds[j]==node.id){row.zone=2000+(int)j;row.selected|=selBody==(int)j;++copies;
                if(j<g_hiddenBodies.size()&&g_hiddenBodies[j])++hiddenCopies;}
            if(hiddenCopies)row.subtitle+=hiddenCopies==copies?" / hidden":" / "+std::to_string(hiddenCopies)+" hidden";
        }
        explorer.push_back(std::move(row));
    }
    for(size_t k=0;k<g_doc.sketches.size();++k){auto& sketch=g_doc.sketches[k];explorer.push_back({sketch.name,"Sketch / "+sketch.sk.plane,11000+(int)k,sketch.name==g_activeSketch&&g_showSketches,0,1});}
    for(size_t k=0;k<g_doc.planes.size();++k){auto& datum=g_doc.planes[k];explorer.push_back({datum.name,"Datum / "+datum.kind,14000+(int)k,datum.name==g_activePlane&&g_showDatumPlanes,0,2});}
    for(size_t k=0;k<g_doc.canvases.size();++k){auto& canvas=g_doc.canvases[k];explorer.push_back({canvas.name,"Canvas / "+canvas.plane,-1,false,0,2});}
    if(g_treeScroll>=(int)explorer.size())g_treeScroll=std::max(0,(int)explorer.size()-1);
    for(size_t i=(size_t)std::max(g_treeScroll,0);i<explorer.size()&&treeY<H-385;++i){
        auto& row=explorer[i];int h=row.kind==1?48:row.kind==2?42:53;
        if(row.selected){
            if(row.kind==1)rect(12,treeY-7,234,(float)h,7,.16,.19,.25,1,.34,.40,.52,.8,1);
            else if(row.kind==2)rect(12,treeY-7,234,(float)h,7,.14,.20,.18,1,.29,.51,.42,.8,1);
            else rect(12,treeY-7,234,(float)h,7,.175,.19,.22,1,.39,.43,.50,.7,1);
        }
        const char* mark=row.kind==1?"~":row.kind==2||row.kind==3?"+":row.zone>=0?"<>":"-";
        float cr=row.kind==1?.67f:row.kind==2?.58f:.75f,cg=row.kind==1?.76f:row.kind==2?.80f:.79f,cb=row.kind==1?.92f:row.kind==2?.69f:.86f;
        textOut(mark,20+row.depth*10,treeY+2,.8f,cr,cg,cb);
        textOut(row.name.substr(0,(size_t)std::max(8,22-row.depth)),48+row.depth*10,treeY,.78f);
        label(row.subtitle,48+row.depth*10,treeY+(row.kind==2?19:23),.60f);
        if(row.zone>=0)g_zones.push_back({12,treeY-7,234,(float)h,row.zone});
        treeY+=row.kind==1?55:row.kind==2?48:62;
    }
    float ly=H-340;rect(20,ly,218,1,0,.17,.21,.22,1,0,0,0,0,0);label("PARAMETERS",20,ly+14,.68f);label("Click to edit",152,ly+14,.65f);
    for(size_t i=0;i<g_doc.params.size()&&i<6;++i){auto& p=g_doc.params[i];float y=ly+36+i*32;
        textOut(p.name.substr(0,15),22,y+5,.79f);rect(127,y-1,108,31,5,.12,.13,.15,1,.29,.31,.35,.7,1);
        textOut(p.expr.substr(0,12),139,y+5,.78f,.83,.85,.89);g_zones.push_back({15,y-2,224,34,(int)i+1000});
    }
    label("PARAMETRIC WORKSPACE",20,H-66,.62f);
    float captionY=(float)viewY+(g_toolModule==4?182.f:139.f);
    textOut(g_cameraOrtho?"Orthographic":"Perspective",viewX+24,captionY,.85f,g_lightBackground?.22f:.87f,g_lightBackground?.25f:.89f,g_lightBackground?.29f:.93f);
    static const char* aoNames[]={"AO OFF","CONTACT AO","AMBIENT AO","HORIZON AO"};
    std::string viewSubtitle=std::string(aoNames[g_occlusionMode])+"  /  mm";
    if(g_allSheetsFlat)viewSubtitle="UNFOLDED SHEET LAYOUT  /  display only";
    else if(g_stockMode)viewSubtitle="NOMINAL STRAIGHT STOCK  /  display only";
    else if(g_allSheetsPlaying)viewSubtitle="SEQUENTIAL UNBEND PREVIEW  /  display only";
    textOut(viewSubtitle,viewX+24,captionY+26,.60f,g_lightBackground?.39f:.63f,g_lightBackground?.43f:.67f,g_lightBackground?.49f:.72f);
    if(g_pickKind){static const char* hints[]={"","FACE / click a visible named surface  |  Esc exits","EDGE / click within 11 px of a visible named edge  |  Esc exits","CURVE / click a sketch curve  |  Esc exits"};
        std::string hint=hints[std::clamp(g_pickKind,0,3)];float hintY=captionY+50,width=textWidth(hint,.56f)+18;
        rect(viewX+19,hintY-4,width,22,6,.09f,.14f,.20f,.87f,.35f,.55f,.76f,.62f,1);
        textOut(hint,viewX+28,hintY,.56f,.79f,.91f,.99f);}
    size_t flatIndex=0;
    if(g_allSheetsFlat)for(size_t i=0;i<g_bodyIds.size();++i)if(isSheetFeature(g_bodyIds[i])){
        const Feature* feature=g_doc.find(g_bodyIds[i]);AABB bounds=g_reports[g_instancePart[i]].bounds.transformed(g_instanceWorld[i]);float sx,sy;
        std::string marker=std::to_string(++flatIndex);if(marker.size()<2)marker="0"+marker;
        if(feature&&bounds.valid()&&projectScreen(bounds.center()+Vec3{0,std::max(8.,bounds.extent().y*2.),0},sx,sy)&&sx>viewX+8&&sx<viewX+viewW-8&&sy>viewY+165&&sy<viewY+viewH-54){
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
            if(projectScreen(g_instanceWorld[i].transformPoint({-length->num*.5,station->num,0}),sx,sy)&&sx>viewX+8&&sx<viewX+viewW-8&&sy>viewY+160&&sy<viewY+viewH-54){
                rect(sx-13,sy-12,26,22,8,.075f,.10f,.14f,.90f,.40f,.59f,.75f,.62f,1);
                textOut(marker,sx-textWidth(marker,.61f)*.5f,sy-8,.61f,.91f,.96f,.99f);
            }
        }
    }
    float rx=W-278;label("INSPECTOR",rx,101,.66f);
    std::string selected="No selection",kind="No body";if(selBody>=0){auto* f=g_doc.find(g_bodyIds[selBody]);if(f){selected=g_instanceNames[selBody];kind=f->type==FeatureType::ParametricSurface?"EXPRESSION SURFACE / MESH":std::string(featureTypeName(f->type))+" / TESSELLATED MESH";if(g_instanceIds[selBody])kind="INSTANCE #"+std::to_string(g_instanceIds[selBody])+" / "+featureTypeName(f->type);}}
    const Body* nativeBody=selectedNativeBody();const auto* nativeReport=nativeBody?nativeBody->nativeData.find("report"):nullptr;
    if(nativeReport&&nativeReport->t!=arkjson::Value::Obj)nativeReport=nullptr;
    label(nativeReport?"B-REP":"MESH",W-66,101,.62f);
    auto* sheet=selectedSheet();
    if(sheet)kind=sheet->type==FeatureType::SourceSheet?"SOURCE SHEET / NATIVE B-REP":"SHEET METAL / NATIVE B-REP";
    else if(nativeReport)kind="OCCT NATIVE SURFACE / B-REP";
    auto* activeSketch=g_showSketches?g_doc.findSketch(g_activeSketch):nullptr;
    if(selectedNotchedTube())kind="LASER TUBE / IDEAL HINGE PREVIEW";
    if(selectedSourceTube())kind="STEP SOURCE / FINITE BENDS";
    if(activeSketch){selected=activeSketch->name;kind="SKETCH / DIMENSION EXPRESSIONS";}
    textOut(selected.substr(0,25),rx,135,1.02f);
    rect(rx,173,254,28,5,.15,.16,.19,1,0,0,0,0,0);textOut(kind.substr(0,36),rx+11,180,.59f,.76,.79,.85);
    std::string pickText;for(const auto& item:g_picked){if(!pickText.empty())pickText+=", ";pickText+=item.name.empty()?item.owner:item.name;}if(pickText.size()>42)pickText=pickText.substr(0,39)+"...";
    label(pickText.empty()?"No face, edge, or curve":pickText,rx,210,.58f);
    button("Body",rx,228,60,170,false,g_pickKind==0);button("Face",rx+64,228,60,171,false,g_pickKind==1);button("Edge",rx+128,228,60,172,false,g_pickKind==2);button("Curve",rx+192,228,62,173,false,g_pickKind==3);
    auto* laser=selectedNotchedTube();auto* source=selectedSourceTube();if(activeSketch){laser=nullptr;source=nullptr;}
    const bool assemblySelected=selBody>=0&&g_instanceIds[selBody]&&!activeSketch;
    if(!g_allSheetsFlat&&!g_stockMode){
    if(!laser&&!source&&!activeSketch)label(assemblySelected?"ASSEMBLY CONSTRAINTS":nativeReport?"NATIVE GEOMETRY":"GEOMETRY",rx,283,.66f);
    auto metric=[&](const char* title,const std::string& value,float y){label(title,rx,y,.76f);textOut(value,W-24-textWidth(value,.80f),y,.80f);};
    if(selBody>=0&&!laser&&!source&&!activeSketch&&!assemblySelected){auto& r=g_reports[g_instancePart[selBody]];
        if(nativeReport){
            auto formatted=[](const arkjson::Value* value,const char* suffix){if(!value||value->t!=arkjson::Value::Num)return std::string("n/a");char text[80];std::snprintf(text,sizeof text,"%.3f %s",value->num,suffix);return std::string(text);};
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
        if(H>950){
            label("CONSTRAINTS / CLICK TO SELECT",rx,474,.62f);
            int start=g_sketchConstraintPage*4;
            auto* diagnosed=g_sketchDiagnosis.find("constraints");
            for(int row=0;row<4&&start+row<(int)activeSketch->sk.cons.size();++row){int index=start+row;auto& con=activeSketch->sk.cons[(size_t)index];float y=494.f+row*32;
                bool picked=index==g_selectedSketchConstraint;if(picked)rect(rx-3,y-4,258,29,5,.22,.20,.17,1,.77,.53,.25,.8,1);
                std::string title=std::to_string(index+1)+" "+constraintTypeName(con.type);if(!con.id.empty())title+=" / "+con.id;
                auto* diagnosis=diagnosed&&index<(int)diagnosed->arr.size()?&diagnosed->arr[(size_t)index]:nullptr;
                bool violated=diagnosis&&diagnosis->find("violated")&&diagnosis->find("violated")->b;
                bool redundant=diagnosis&&diagnosis->find("locally_redundant")&&diagnosis->find("locally_redundant")->b;
                textOut(title.substr(0,25),rx+4,y,.72f,violated?1.f:.84f,violated?.43f:.85f,violated?.42f:.88f);
                label(violated?"conflict candidate":redundant?"local redundancy":"satisfied",rx+143,y+17,.55f);
                g_zones.push_back({rx-3,y-4,258,29,12000+index});
            }
            if(activeSketch->sk.cons.size()>4){button("<",rx,625,34,91);button(">",rx+38,625,34,92);label(std::to_string(g_sketchConstraintPage+1)+" / "+std::to_string((activeSketch->sk.cons.size()+3)/4),rx+88,632,.58f);}
        }
        button("Edit entity",rx,H-238,121,70);button("Edit constraint",rx+131,H-238,121,89);
        button("Add constraint",rx,H-198,121,73);button("Remove",rx+131,H-198,121,90);
        button("Extrude",rx,H-158,121,65,true);button("Cut",rx+131,H-158,121,66);
        button("Named parameter",rx,H-118,252,67);button(g_showSketches?"Hide sketch":"Show sketch",rx,H-78,121,68);button(g_showDatumPlanes?"Hide datums":"Show datums",rx+131,H-78,121,72);
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
    if(g_physicalVisible){
        float px=W-292,py=H-465;panel(px,py,280,210,8);
        label(g_physicalAssembly?"ASSEMBLY PHYSICAL PROPERTIES":"BODY PHYSICAL PROPERTIES",px+12,py+11,.60f);
        if(!g_doc.assembly.empty()&&selBody>=0)button(g_physicalAssembly?"Part":"Assembly",px+112,py+34,91,190);
        button("Close",px+215,py+34,53,189);
        const auto* mass=g_physicalReport.find("mass_g");const auto* weight=g_physicalReport.find("weight_N");
        const auto* density=g_physicalReport.find("density_g_cm3");const auto* center=g_physicalReport.find("center_mm");
        auto fmt=[](double value,const char* unit){char buffer[96];std::snprintf(buffer,sizeof buffer,"%.4g %s",value,unit);return std::string(buffer);};
        if(mass&&mass->t==arkjson::Value::Num&&weight&&weight->t==arkjson::Value::Num){
            label("Density",px+12,py+76,.62f);textOut(density&&density->t==arkjson::Value::Num?fmt(density->num,"g/cm3"):"mixed materials",px+112,py+76,.62f);
            label("Mass",px+12,py+101,.62f);textOut(mass->num>=1000?fmt(mass->num/1000.,"kg"):fmt(mass->num,"g"),px+112,py+101,.72f);
            label("Weight",px+12,py+126,.62f);textOut(fmt(weight->num,"N @ 9.80665"),px+112,py+126,.62f);
            if(center&&center->t==arkjson::Value::Arr&&center->arr.size()==3){
                char xyz[112];std::snprintf(xyz,sizeof xyz,"%.2f, %.2f, %.2f mm",center->arr[0].num,center->arr[1].num,center->arr[2].num);
                label(g_physicalAssembly?"World COM":"Local COM",px+12,py+151,.62f);textOut(xyz,px+12,py+173,.68f);
                Vec3 point{center->arr[0].num,center->arr[1].num,center->arr[2].num};if(!g_physicalAssembly)point=g_instanceWorld[(size_t)selBody].transformPoint(point);
                double cw=vp.m[3]*point.x+vp.m[7]*point.y+vp.m[11]*point.z+vp.m[15];Vec3 clip=vp.transformPoint(point);
                if(cw>1e-5){float sx=viewX+(float)((clip.x/cw+1.)*.5*viewW),sy=viewY+(float)((1.-clip.y/cw)*.5*viewH);
                    if(sx>viewX+118&&sx<viewX+viewW-12&&sy>viewY+145&&sy<viewY+viewH-45){
                        rect(sx-13,sy-1,26,2,1,.99,.56,.21,1,0,0,0,0,0);rect(sx-1,sy-13,2,26,1,.99,.56,.21,1,0,0,0,0,0);textOut("COM",sx+14,sy-9,.65f,1.,.68,.32);
                    }
                }
            }
        }else{
            label("Mass unavailable",px+12,py+78,.72f);
            std::string reason=g_physicalError.empty()?"Assign density to every exact solid":g_physicalError;
            textOut(reason.substr(0,37),px+12,py+109,.62f,.89f,.67f,.50f);
            label("Exact B-rep volume required",px+12,py+146,.61f);
        }
    }
    if((g_allSheetsFlat||g_stockMode)&&!g_physicalVisible){
        float listY=276,listH=std::max(180.f,H-330.f);panel(W-292,listY,280,listH,8);
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
    float cy=H-244;panel(viewX,cy,(float)viewW,216);
    if(laser||source){float py=(float)viewY+(float)viewH-101.f;rect(viewX+126,py-5,viewW-142,43,7,.10,.11,.13,.96,.25,.27,.31,.8,1);
        float x=viewX+136;button("Straight",x,py,76,45,false,g_foldProgress==0);button("Folded",x+84,py,76,46,false,g_foldProgress==1);button(g_foldPlaying?"Pause":"Play",x+168,py,62,47,true);
        float length=std::max(80.f,(float)viewW-408);rect(x+244,py+14,length,5,2,.23,.26,.31,1,0,0,0,0,0);rect(x+244,py+14,std::max(3.f,length*(float)g_foldProgress),5,2,.72,.78,.88,1,0,0,0,0,0);g_zones.push_back({x+244,py+2,length,29,50});}
    else if(sheet&&sheet->type==FeatureType::SheetMetal&&nativeBody&&nativeBody->nativeData.find("animation")){
        float py=(float)viewY+(float)viewH-101.f;rect(viewX+126,py-5,viewW-142,43,7,.10,.11,.13,.96,.25,.27,.31,.8,1);
        float x=viewX+136;double progress=sheetDisplayProgress(sheet->id);
        button("Flat",x,py,76,84,false,progress<=0);button("Formed",x+84,py,76,83,false,progress>=1);button(g_sheetPlayingId==sheet->id?"Pause":"Play",x+168,py,62,128,true);
        float length=std::max(80.f,(float)viewW-408);rect(x+244,py+14,length,5,2,.23,.26,.31,1,0,0,0,0,0);rect(x+244,py+14,std::max(3.f,length*(float)progress),5,2,.72,.78,.88,1,0,0,0,0,0);g_zones.push_back({x+244,py+2,length,29,53});}
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
    label("ASSISTANT",viewX+22,cy+15,.65f);
    std::string connector=g_chatBusy?"Working...":g_authBusy?"ChatGPT sign-in in progress":
        g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan?"ChatGPT plan / "+g_assistantConfig.model:
        g_authStatus.connected?(g_authStatus.planAuthorized?"ChatGPT signed in / choose model":"ChatGPT identity connected / plan disabled"):
        g_assistantConfig.model.empty()?"Continue with ChatGPT or Advanced API key":"API key / "+g_assistantConfig.model;
    label(connector.substr(0,32),viewX+132,cy+15,.60f);
    std::string effortButton="Effort "+(g_assistantConfig.reasoningEffort.empty()?std::string("Auto"):g_assistantConfig.reasoningEffort);
    button(effortButton.c_str(),viewX+viewW-320,cy+7,105,367,false,!g_assistantConfig.reasoningEffort.empty());
    button(g_authBusy?"Connecting...":g_authStatus.connected?"ChatGPT account":"Continue with ChatGPT",viewX+viewW-206,cy+7,184,57);
    std::string runTokens="Run: "+assistantUsageLabel(g_assistantUsageRun);
    label(runTokens.substr(0,(size_t)std::max(20,(viewW-350)/5)),viewX+22,cy+33,.52f);
    std::vector<std::string> chatLines;size_t cols=(size_t)std::max(30,(viewW-55)/7);
    for(auto& entry:g_chatLog){std::string line=entry.first+": "+entry.second;
        for(char& ch:line)if(ch=='\r'||ch=='\n')ch=' ';
        for(size_t i=0;i<line.size();){size_t end=std::min(i+cols,line.size());
            while(end>i&&end<line.size()&&((unsigned char)line[end]&0xc0)==0x80)--end;
            if(end==i)end=std::min(i+cols,line.size());
            chatLines.push_back(line.substr(i,end-i));i=end;
        }}
    if(chatLines.empty())chatLines.push_back("Describe a part, ask for an inspection, or connect Codex to this document.");
    int last=std::max(0,(int)chatLines.size()-g_chatScroll),first=std::max(0,last-3);float chatY=cy+49;
    for(int i=first;i<last;++i){textOut(chatLines[i],viewX+22,chatY,.66f,.73,.77,.84);chatY+=22;}
    rect(viewX+22,cy+124,viewW-244,62,7,.055,.062,.078,1,.30,.35,.43,.8,1);
    label("MESSAGE  /  Ctrl+Enter to send",viewX+30,cy+108,.55f);
    button(g_chatBusy?"Running":"Send",viewX+viewW-206,cy+124,84,74,true);button("Cancel",viewX+viewW-112,cy+124,90,75);
    button("Codex MCP",viewX+viewW-206,cy+164,184,58);
    AssistantUsage docTokens=g_assistantUsageDocument;
    textOut(g_status.substr(0,(size_t)std::max((viewW-334)/7,1)),viewX+22,cy+193,.58f,g_error?.94f:.58f,g_error?.57f:.65f,g_error?.48f:.74f);
    std::string documentTokens="Document: "+assistantUsageLabel(docTokens);
    label(documentTokens.substr(0,46),viewX+viewW-310,cy+193,.51f);
    float navT=std::clamp(((float)viewW-622.f)/420.f,0.f,1.f);
    g_cubeS=(int)std::lround(110.f+35.f*navT);g_cubeX=viewX+viewW-g_cubeS-17;g_cubeY=viewY+20;
    g_cubeHover=cubeHitAt(mp.x,mp.y);
    paintViewCube();glUseProgram(pUI);glUniform2f(uuVp,W,H);
    {Vec3 cubeEye;Mat4 cubeView,cubeProj;cubeCamera(cubeEye,cubeView,cubeProj);Mat4 cvp=cubeProj*cubeView;Vec3 toEye=cubeEye.normalized();
        static const char* faceName[]={"Right","Left","Top","Bottom","Front","Back"};
        static const Vec3 faceNormal[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for(int face=0;face<6;++face){double visibility=faceNormal[face].dot(toEye);if(visibility<.18)continue;Vec3 p=faceNormal[face]*.88;double cw=cvp.m[3]*p.x+cvp.m[7]*p.y+cvp.m[11]*p.z+cvp.m[15];if(cw<=1e-5)continue;Vec3 clip=cvp.transformPoint(p);
            float x=g_cubeX+(float)((clip.x/cw+1)*.5*g_cubeS),y=g_cubeY+(float)((1-clip.y/cw)*.5*g_cubeS);
            float scale=(visibility>.55?.49f:.43f)+navT*.09f;textOut(faceName[face],x-textWidth(faceName[face],scale)*.5f,y-6,scale,.06f,.11f,.16f);}
    }
    float compassX=(float)g_cubeX-2,compassY=(float)g_cubeY+g_cubeS-22;
    rect(compassX,compassY,40,40,20,.07f,.10f,.14f,.68f,.37f,.53f,.67f,.42f,1);
    paintCompass((int)compassX+2,(int)compassY+1,36,36);glUseProgram(pUI);glUniform2f(uuVp,W,H);
    float homeX=(float)g_cubeX+g_cubeS-29,homeY=(float)g_cubeY+g_cubeS-19;
    bool homeHover=mp.x>=homeX&&mp.x<homeX+28&&mp.y>=homeY&&mp.y<homeY+28;
    rect(homeX,homeY,28,28,14,homeHover?.21f:.08f,homeHover?.32f:.11f,homeHover?.43f:.15f,homeHover?.94f:.72f,.41f,.59f,.73f,homeHover?.78f:.42f,1);
    glyph(29,homeX+5,homeY+5,18,homeHover?.99f:.83f);g_zones.push_back({homeX,homeY,28,28,191});
    g_zones.push_back({(float)g_cubeX,(float)g_cubeY,(float)g_cubeS,(float)g_cubeS,179});
    {const char* names[]={"Orbit","Pan","Zoom","Fit","Edges","AO","Ortho"};int ids[]={176,177,178,20,174,186,175};
        bool on[]={g_navMode==0,g_navMode==1,g_navMode==2,false,g_showEdges,g_occlusionMode!=0,g_cameraOrtho};
        float gap=4.f,bw=58.f,ty=(float)viewY+(float)viewH-44.f;
        rect((float)viewX+10,ty-5,7*bw+6*gap+16,43,9,.07f,.08f,.10f,.76f,.34f,.38f,.44f,.45f,1);
        for(int i=0;i<7;++i)button(names[i],(float)viewX+18+i*(bw+gap),ty,bw,ids[i],false,on[i]);}
    panel(0,H-28,W,28);
    textOut("o",17,H-22,.65f,.73,.79,.87);label("ENGINE READY",34,H-21,.61f);
    char st[180];std::snprintf(st,sizeof st,"%zu triangles   /   %.2f ms recompute   /   %.0f fps",g_triangleCount,g_doc.recomputeMs,g_fps);
    label(st,viewX+16,H-21,.61f);label(g_cameraOrtho?"Ortho  |  wheel zoom":"Perspective  |  wheel zoom",W-250,H-21,.58f);

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

static void drawBusyFrame(HDC hdc){
    // The worker exclusively owns g_session. This path reads only a captured GL
    // frame and UI progress state; drawFrame and document access are forbidden.
    glViewport(0,0,winW,winH);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    glUseProgram(pBusy);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g_busySnapshotTex);
    glUniform1i(glGetUniformLocation(pBusy,"frame"),0);glUniform2f(glGetUniformLocation(pBusy,"res"),(float)winW,(float)winH);
    glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pUI);
    glUniform2f(uuVp,(float)winW,(float)winH);
    auto box=[&](float x,float y,float w,float h,float radius,float r,float g,float b,float a){
        glUniform1i(uuGlyph,0);glUniform4f(uuRc,x,y,w,h);glUniform1f(uuRad,radius);
        glUniform4f(uuFill,r,g,b,a);glUniform4f(uuBorder,0,0,0,0);glUniform1f(uuBW,0);
        glDrawArrays(GL_TRIANGLES,0,6);
    };
    box(0,0,(float)winW,(float)winH,0,.025f,.038f,.057f,.43f);
    float width=std::min(492.f,(float)winW-44.f),x=((float)winW-width)*.5f,y=((float)winH-176.f)*.5f;
    box(x-2,y-2,width+4,180,14,.24f,.37f,.52f,.82f);
    box(x,y,width,176,13,.075f,.096f,.126f,.99f);
    box(x+20,y+22,5,44,2,.39f,.77f,.98f,1);
    auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-g_busyStarted).count();
    textBegin();textOut(g_busyLabel,x+43,y+21,.95f,.92f,.96f,1.f);
    char duration[80];std::snprintf(duration,sizeof duration,"Kernel operation  /  %.1f s elapsed",elapsed);
    textOut(duration,x+43,y+53,.66f,.63f,.72f,.82f);
    textOut("Document remains visible while the kernel works",x+24,y+94,.65f,.77f,.84f,.92f);
    textOut("Edits and agent tools resume when this operation finishes",x+24,y+119,.60f,.58f,.68f,.79f);
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

static arkjson::Value callSessionWithProgress(const std::string& tool,const arkjson::Value& args){
    HDC hdc=GetDC(g_window);if(!hdc)throw std::runtime_error("Could not acquire Studio viewport for kernel operation");
    struct BusyCleanup{
        HDC hdc;bool previousSwap;
        ~BusyCleanup(){g_busy=false;g_swap=previousSwap;ReleaseDC(g_window,hdc);
            if(g_busyClosePending){g_busyClosePending=false;PostMessage(g_window,WM_CLOSE,0,0);}}
    } cleanup{hdc,g_swap};
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
    g_busyLabel=tool=="doc_load"?"Loading document":tool.rfind("import",0)==0?"Importing geometry":tool.rfind("export",0)==0?"Exporting geometry":"Evaluating geometry";
    std::atomic_bool completed{false};arkjson::Value response;std::exception_ptr failure;
    std::jthread worker([&]{try{if(g_busyTestDelayMs)std::this_thread::sleep_for(std::chrono::milliseconds(g_busyTestDelayMs));response=g_session.call(tool,args);}catch(...){failure=std::current_exception();}completed=true;});
    unsigned frameBefore=g_busyFrameCount;
    while(!completed){
        MSG message{};
        while(PeekMessageA(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT||message.message==WM_CLOSE){g_busyClosePending=true;continue;}
            if(message.hwnd==g_window&&(message.message==WM_ARK_TOOL||message.message==WM_ARK_CHAT||message.message==WM_ARK_AUTH||message.message==WM_SIZE)){
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
    worker.join();
    if(failure)std::rethrow_exception(failure);
    return response;
}

enum class ContextTarget{Empty,Body,Sketch,Datum};
struct ContextEntry{int action=0;const char* title="";bool enabled=true;};
static std::vector<ContextEntry> contextEntries(ContextTarget target,int body=-1){
    std::vector<ContextEntry> menu;
    if(target==ContextTarget::Body&&body>=0&&(size_t)body<g_bodyIds.size()){
        const Feature* feature=g_doc.find(g_bodyIds[(size_t)body]);bool hidden=(size_t)body<g_hiddenBodies.size()&&g_hiddenBodies[(size_t)body];
        bool editable=feature&&feature->type!=FeatureType::SourceTube&&feature->type!=FeatureType::SourceSheet&&feature->type!=FeatureType::MeshBody;
        menu.push_back({161,"Edit part definition",editable});menu.push_back({162,"Edit driving sketch",feature&&!drivingSketchNames(*feature).empty()});
        menu.push_back({29,"Edit placement",g_instanceIds[(size_t)body]!=0});menu.push_back({187,"Material, color and texture",feature!=nullptr});
        if(feature&&feature->type==FeatureType::Suspension)menu.push_back({384,"Animate suspension corner"});
        menu.push_back({0,""});menu.push_back({hidden?341:340,hidden?"Show body":"Hide body"});
        menu.push_back({342,"Spotlight body"});menu.push_back({343,"Restore previous visibility",g_spotlightActive});
        menu.push_back({344,"Show all bodies"});menu.push_back({0,""});
        menu.push_back({170,"Select bodies"});menu.push_back({171,"Select named faces"});menu.push_back({172,"Select named edges"});
    }else if(target==ContextTarget::Sketch){
        menu={{64,"Edit sketch"},{71,"Normal to sketch plane"},{68,g_showSketches?"Hide sketches":"Show sketches"},{0,""},{173,"Select sketch curves"}};
    }else if(target==ContextTarget::Datum){
        menu={{71,"Align view to plane"},{72,g_showDatumPlanes?"Hide datums":"Show datums"},{0,""},{171,"Select planar faces"}};
    }else{
        menu={{343,"Restore Spotlight",g_spotlightActive},{344,"Show all bodies"},{0,""},{20,"Fit document"},{24,"Isometric view"},{23,"Top view"},{56,"Graphics settings"}};
    }
    return menu;
}
static ContextTarget contextTargetAt(int mx,int my,int& body){
    ContextTarget target=ContextTarget::Empty;body=-1;
    if(mx<viewX){for(const Zone& zone:g_zones)if(mx>=zone.x&&mx<zone.x+zone.w&&my>=zone.y&&my<zone.y+zone.h){
            if(zone.id>=2000&&zone.id<9000){body=zone.id-2000;target=ContextTarget::Body;}
            else if(zone.id>=11000&&zone.id<12000){size_t index=(size_t)(zone.id-11000);if(index<g_doc.sketches.size()){
                    g_activeSketch=g_doc.sketches[index].name;g_activePlane=g_doc.sketches[index].sk.plane;g_showSketches=true;rebuildSketchOverlay();target=ContextTarget::Sketch;}}
            else if(zone.id>=14000&&zone.id<15000){size_t index=(size_t)(zone.id-14000);if(index<g_doc.planes.size()){
                    g_activePlane=g_doc.planes[index].name;g_activeSketch.clear();target=ContextTarget::Datum;}}
            break;
        }}
    else if(mx>=viewX&&mx<viewX+viewW&&my>=viewY&&my<viewY+viewH){body=pickBody(mx,my);if(body>=0)target=ContextTarget::Body;}
    else return ContextTarget::Empty;
    if(target==ContextTarget::Body){selBody=body;g_activeSketch.clear();g_showSketches=false;rebuildSketchOverlay();}
    return target;
}
static void showContextMenu(int mx,int my){
    if(mx>=viewX+viewW||my<viewY||my>=winH-28)return;
    int body=-1;ContextTarget target=contextTargetAt(mx,my,body);
    HMENU menu=CreatePopupMenu();if(!menu)return;
    for(const ContextEntry& entry:contextEntries(target,body)){
        if(!entry.action)AppendMenuA(menu,MF_SEPARATOR,0,nullptr);
        else AppendMenuA(menu,MF_STRING|(entry.enabled?0:MF_GRAYED),(UINT_PTR)entry.action,entry.title);
    }
    POINT screen{mx,my};ClientToScreen(g_window,&screen);SetForegroundWindow(g_window);
    int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,g_window,nullptr);
    DestroyMenu(menu);PostMessage(g_window,WM_NULL,0,0);if(action)interactiveAction(action);
}

// ---------------------------------------------------------------------------
static LRESULT CALLBACK wndProc(HWND hw,UINT m,WPARAM wp,LPARAM lp){
    if(g_busy){
        if(m==WM_CLOSE){g_busyClosePending=true;return 0;}
        if(m==WM_COMMAND||m==WM_MOUSEWHEEL||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_LBUTTONDBLCLK||m==WM_RBUTTONDOWN||m==WM_RBUTTONUP||m==WM_MOUSEMOVE||m==WM_KEYDOWN||m==WM_KEYUP)return 0;
    }
    switch(m){
        case WM_ARK_TOOL:g_toolQueue.emplace_back((UiToolRequest*)lp);return 0;
        case WM_ARK_CHAT:{std::unique_ptr<UiChatEvent> event((UiChatEvent*)lp);
            if(event->final){g_chatBusy=false;if(event->epoch==g_documentEpoch&&!event->error&&!*g_assistantCancel)g_conversation=event->history;else g_conversation=arkjson::Value::makeArr();}
            if(event->epoch==g_documentEpoch){
                if(event->hasUsage){g_assistantUsageRun=event->usage;recordAssistantUsageSnapshot(event->usage);}
                if(event->final)g_assistantUsageRunCounted=true;
                if(!event->text.empty()){g_chatLog.push_back({event->error?"Error":event->final?"ARK":"Action",event->text.substr(0,16000)});if(g_chatLog.size()>200)g_chatLog.erase(g_chatLog.begin());g_chatScroll=0;}
            }return 0;
        }
        case WM_ARK_AUTH:{std::unique_ptr<UiAuthEvent> event((UiAuthEvent*)lp);bool cancelRequested=g_authCancelRequested;
            g_authBusy=false;g_authCancelRequested=false;g_authStatus=std::move(event->status);
            if(event->job==AuthJob::SignOut&&!g_authStatus.connected){g_authModels.clear();if(g_assistantConfig.authMode==AssistantAuthMode::ChatGptPlan){
                g_assistantConfig.authMode=AssistantAuthMode::ApiKey;g_assistantConfig.model.clear();*g_assistantCancel=true;++g_documentEpoch;g_conversation=arkjson::Value::makeArr();try{saveUiSettings();}catch(...){}}
            }else if(event->job==AuthJob::SignIn){g_authModels=std::move(event->models);}
            else if(!event->models.empty())g_authModels=std::move(event->models);
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
            return 0;}
        case WM_COMMAND:if(LOWORD(wp)==500){interactiveAction(74);return 0;}break;
        case WM_CTLCOLOREDIT:if((HWND)lp==g_chatInput){SetTextColor((HDC)wp,RGB(219,226,238));SetBkColor((HDC)wp,RGB(14,17,20));return (LRESULT)chatBrush;}break;
        case WM_ERASEBKGND:return 1; // OpenGL owns the parent surface; erase only the native EDIT child.
        case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={1180,840};return 0;
        case WM_SIZE:
            winW=LOWORD(lp)>0?LOWORD(lp):1;
            winH=HIWORD(lp)>0?HIWORD(lp):1;
            layout();return 0;
        case WM_MOUSEWHEEL:
            {POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hw,&p);
            if(p.x>=viewX&&p.x<viewX+viewW&&p.y>=winH-244){g_chatScroll=std::max(0,g_chatScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?2:-2));return 0;}
            if(p.x>winW-300&&selectedSourceTube()){g_sourceScroll=std::max(0,g_sourceScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1));return 0;}
            if(p.x>winW-300&&selectedNotchedTube()){g_notchScroll=std::max(0,g_notchScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1));return 0;}
            if(p.x<viewX){int count=(int)((g_doc.assembly.empty()?g_doc.features.size():g_doc.assembly.size())+g_doc.sketches.size()+g_doc.planes.size());g_treeScroll=std::max(0,std::min(std::max(0,count-1),g_treeScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1)));return 0;}}
            tDist=clamp(tDist*(GET_WHEEL_DELTA_WPARAM(wp)>0?0.87:1.15),2.,4000.);
            return 0;
        case WM_RBUTTONUP:showContextMenu(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;
        case WM_LBUTTONDOWN:{
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};g_pointerX=p.x;g_pointerY=p.y;g_cubePress=false;
            g_pressWasButton=p.x<viewX||p.x>viewX+viewW||p.y<viewY||p.y>viewY+viewH;
            for(auto&z:g_zones)
                if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&(float)p.y>=z.y&&(float)p.y<=z.y+z.h)
                    { if(z.id==179){g_cubePress=cubeHitAt(p.x,p.y).hit;g_pressWasButton=false;}
                        else{g_pressWasButton=true;if(z.id==50){g_foldScrubbing=true;g_foldPlaying=false;g_foldProgress=clamp((double)(p.x-z.x)/z.w,0.,1.);}
                        else if(z.id==53){g_sheetScrubbing=true;g_sheetPlayingId=0;if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}}
                        else if(z.id>=380&&z.id<=382){g_suspScrub=z.id-380;g_suspSpinPlaying=false;try{setSuspensionControl(g_suspScrub,(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}}} break; }
            g_dragged=false;SetCapture(hw);
            return 0;
        }
        case WM_MOUSEMOVE:{
            static int px=0,pyy=0; static bool has=false;
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            bool lbtn=wp&MK_LBUTTON,mbtn=wp&MK_MBUTTON,shift=wp&MK_SHIFT;
            if(g_foldScrubbing&&lbtn)for(auto& zone:g_zones)if(zone.id==50){g_foldProgress=clamp((double)(x-zone.x)/zone.w,0.,1.);break;}
            if(g_sheetScrubbing&&lbtn)for(auto& zone:g_zones)if(zone.id==53){if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(x-zone.x)/zone.w);}catch(const std::exception& error){g_error=true;g_status=error.what();g_sheetScrubbing=false;}break;}
            if(g_suspScrub>=0&&lbtn)for(auto& zone:g_zones)if(zone.id==380+g_suspScrub){try{setSuspensionControl(g_suspScrub,(x-zone.x)/zone.w);}catch(const std::exception& error){g_error=true;g_status=error.what();g_suspScrub=-1;}break;}
            if(has&&(lbtn||mbtn)){
                int dx=x-px,dy=y-pyy;if(std::abs(dx)+std::abs(dy)>2)g_dragged=true;
                bool navigate=!g_pressWasButton&&!g_foldScrubbing&&!g_sheetScrubbing;
                bool pan=navigate&&(mbtn||(lbtn&&(shift||(g_navMode==1&&!g_cubePress))));
                bool zoom=navigate&&lbtn&&!mbtn&&!shift&&g_navMode==2&&!g_cubePress;
                if(pan){
                    f64 s=tDist*0.0016;
                    Vec3 rv{std::cos(yaw),0,-std::sin(yaw)};
                    tTarget=tTarget-rv*((f64)dx*s);
                    tTarget.y+=(f64)dy*s;
                } else if(zoom){tDist=clamp(tDist*std::exp((double)dy*0.012),2.,4000.);}
                else if(navigate&&lbtn&&!shift){
                    tYaw+=dx*0.008;
                    tPitch=clamp(tPitch+dy*0.006,-1.52,1.52);
                }
            }
            px=x;pyy=y;has=true;
            size_t tot=0; for(auto&m:g_cpuMeshes)tot+=m.triCount();
            hoverBody=!lbtn?pickBody(x,y):-1;
            return 0;
        }
        case WM_LBUTTONUP:{
            ReleaseCapture();
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
                if(g_pickKind)pickReference(p.x,p.y);
                else{int b=pickBody(p.x,p.y);
                selBody=(b==selBody)?-1:b;g_selectedNotch=0;g_notchScroll=0;
                if(selBody>=0){g_activeSketch.clear();g_showSketches=false;rebuildSketchOverlay();}}
            }
            g_eatNextClick=false;g_pressWasButton=false;g_cubePress=false;g_foldScrubbing=false;g_sheetScrubbing=false;g_suspScrub=-1;
            return 0;
        }
        case WM_LBUTTONDBLCLK:{
            g_eatNextClick=true;POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            for(auto& z:g_zones)if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&(float)p.y>=z.y&&(float)p.y<=z.y+z.h){
                if(z.id>=2000&&z.id<9000){selBody=z.id-2000;g_activeSketch.clear();g_showSketches=false;interactiveAction(161);}
                else if(z.id>=11000&&z.id<12000){interactiveAction(z.id);interactiveAction(70);}
                break;
            }
            return 0;
        }
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE){g_pickKind=0;g_picked.clear();refreshSelectionLines();g_status="Body selection / semantic picks cleared";return 0;}
            if(GetKeyState(VK_CONTROL)&0x8000){if(wp=='S')uiAction(16);else if(wp=='O')uiAction(17);else if(wp=='Z')uiAction(18);else if(wp=='Y')uiAction(19);else if(wp=='K')interactiveAction(59);else if(wp=='N')interactiveAction(55);}
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

int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR cmdLine,int){
    cpu_detect();

    SetProcessDPIAware();
    bool selfTest=false,assemblyTest=false,dialogTest=false,bendTest=false,bendExportTest=false,sourceTest=false,sketchTest=false,assistantTest=false,nativeUiTest=false,renderUiTest=false,engineeringUiTest=false,sheetShowcaseTest=false,contextUiTest=false,suspensionUiTest=false,connectChatGpt=false;unsigned exitAfterMs=0;std::string endpointFile,requestedSketch;int requestedOpening=-1;double requestedFold=-1,showcaseProgress=-1;std::string shotPath="ark_studio.png",loadPath,scriptPath,importPath,showcaseView,spotlightName,suspensionFeatureName;std::vector<std::string> suspensionValues;
    int sw=1600,sh=1000,argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    auto utf8=[](const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;};
    for(int i=1;i<argc;++i){auto a=utf8(argv[i]);if(a=="--shot"&&i+1<argc){g_headless=true;shotPath=utf8(argv[++i]);}
        else if(a=="--self-test-ui")selfTest=true;
        else if(a=="--self-test-sketch-ui")sketchTest=true;
        else if(a=="--self-test-assistant-ui")assistantTest=true;
        else if(a=="--self-test-native-ui")nativeUiTest=true;
        else if(a=="--self-test-render-ui"){renderUiTest=true;g_headless=true;}
        else if(a=="--self-test-engineering-ui"){engineeringUiTest=true;g_headless=true;}
        else if(a=="--self-test-sheet-showcase-ui"){sheetShowcaseTest=true;g_headless=true;}
        else if(a=="--self-test-context-ui"){contextUiTest=true;g_headless=true;}
        else if(a=="--self-test-suspension-ui"){suspensionUiTest=true;g_headless=true;}
        else if(a=="--connect-chatgpt")connectChatGpt=true;
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
    LocalFree(argv);if(!g_headless||!g_settingsOverride.empty())loadUiSettings();
    if(!g_headless)try{g_authStatus=chatGptAuthStatus();}catch(...){g_status="ChatGPT account status unavailable";}
    winW=std::max(sw,1180);winH=std::max(sh,800);layout();

    // ---- window ----
    WNDCLASSA wc{}; wc.style=CS_OWNDC; wc.hInstance=hi;
    wc.lpfnWndProc=wndProc; wc.lpszClassName="ARKVIEW";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    // The same original geometric ARK mark is used in the viewport and taskbar.
    HDC iconDC=CreateCompatibleDC(nullptr);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=32;bi.bmiHeader.biHeight=-32;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    void* bits=nullptr;HBITMAP colorIcon=CreateDIBSection(iconDC,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto oldBitmap=SelectObject(iconDC,colorIcon);
    RECT iconRect{0,0,32,32};HBRUSH iconBg=CreateSolidBrush(RGB(38,41,48));FillRect(iconDC,&iconRect,iconBg);DeleteObject(iconBg);
    HPEN iconPen=CreatePen(PS_SOLID,3,RGB(229,234,245));auto oldPen=SelectObject(iconDC,iconPen);
    MoveToEx(iconDC,6,26,nullptr);LineTo(iconDC,16,5);LineTo(iconDC,26,26);MoveToEx(iconDC,12,19,nullptr);LineTo(iconDC,23,19);
    SelectObject(iconDC,oldPen);DeleteObject(iconPen);SelectObject(iconDC,oldBitmap);DeleteDC(iconDC);
    for(int k=0;k<1024;++k)((DWORD*)bits)[k]|=0xff000000;
    HBITMAP maskIcon=CreateBitmap(32,32,1,1,nullptr);ICONINFO ii{TRUE,0,0,maskIcon,colorIcon};wc.hIcon=CreateIconIndirect(&ii);DeleteObject(maskIcon);DeleteObject(colorIcon);
    RegisterClassA(&wc);
    RECT rc{0,0,winW,winH};
    AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowA("ARKVIEW","ARK 0.9 / Agent-Ready Kernel",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                            CW_USEDEFAULT,CW_USEDEFAULT,rc.right-rc.left,rc.bottom-rc.top,
                            nullptr,nullptr,hi,nullptr);
    g_window=hwnd;BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof dark);
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
    ShowWindow(hwnd,g_headless?SW_HIDE:SW_SHOW);

    // ---- resources ----
    pBg=mkProg(VS_BG,FS_BG);
    pBusy=mkProg(VS_BG,FS_BUSY);
    pSolid=mkProg(VS_SOLID,FS_SOLID);
    pCube=mkProg(VS_SOLID,FS_CUBE);
    pGrid=mkProg(VS_LINE,FS_LINE);
    pShadow=mkProg(VS_SHADOW,FS_SHADOW);
    pUI=mkProg(VS_UI,FS_UI);
    pText=mkProg(VS_TEXT,FS_TEXT);
    pSketch=mkProg(VS_LINE,"#version 330 core\nout vec4 o;uniform vec4 tint;void main(){o=tint;}");
    pCanvas=mkProg(
        "#version 330 core\nlayout(location=0) in vec3 P;layout(location=1) in vec2 T;uniform mat4 vp,model;out vec2 vuv;void main(){vuv=T;gl_Position=vp*model*vec4(P,1.);}",
        "#version 330 core\nin vec2 vuv;uniform sampler2D img;uniform float opacity,live;out vec4 o;void main(){float check=mod(floor(vuv.x*8.)+floor(vuv.y*8.),2.);vec3 paper=mix(vec3(.62,.58,.52),vec3(.78,.74,.68),check);vec4 tex=texture(img,vuv);o=vec4(mix(paper,tex.rgb,live),opacity*mix(.55,tex.a,live));}");
    pGeom=mkProg(VS_GEOM,FS_GEOM);pAo=mkProg(VS_BG,FS_AO);pCompose=mkProg(VS_BG,FS_COMPOSE);pEdge=mkProg(VS_EDGE,FS_EDGE);
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
    glGenVertexArrays(1,&gridVao);
    { GLuint gb; glGenBuffers(1,&gb);
      std::vector<float> ln;
      const int N=20,S=10;
      for(int i=-N;i<=N;++i){
          ln.insert(ln.end(),{(float)-N*S,0.f,(float)i*S,(float)N*S,0.f,(float)i*S});
          ln.insert(ln.end(),{(float)i*S,0.f,(float)-N*S,(float)i*S,0.f,(float)N*S});
      }
      gridCount=(int)ln.size()/3;
      glBindVertexArray(gridVao); glBindBuffer(GL_ARRAY_BUFFER,gb);
      glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(ln.size()*sizeof(float)),ln.data(),GL_STATIC_DRAW);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
    }
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
    if(!loadPath.empty()){arkjson::Value a=arkjson::Value::makeObj();a.set("path",arkjson::Value::makeStr(loadPath));runTool("doc_load",a);g_path=loadPath;if(!g_error){g_savedRevision=g_session.revision;g_foldProgress=selectedSourceTube()?1.:0.;}frameScene();}
    if(!scriptPath.empty()){
        std::ifstream input(scriptPath);std::string line;
        if(!input)return 2;
        while(std::getline(input,line)){arkjson::Value c;if(!arkjson::parse(line,c)||!c.find("tool")||!c.find("arguments"))return 2;
            runTool(c.find("tool")->asStr(),*c.find("arguments"));if(g_error)return 3;}
        frameScene();
    }
    if(!importPath.empty()){importSourceStep(importPath);if(g_error)return 80;}
    if(!showcaseView.empty()||showcaseProgress>=0){
        try{
            if(showcaseView=="formed"){if(!visibleSheetIds().empty())allSheetPose(false);else frameScene();g_toolModule=4;}
            else if(showcaseView=="flat"){allSheetPose(true);g_toolModule=4;}
            else if(showcaseView=="stock"){
                auto selected=std::find_if(g_bodyIds.begin(),g_bodyIds.end(),[&](u64 id){auto* feature=g_doc.find(id);return feature&&(feature->type==FeatureType::ConstructionFrame||feature->type==FeatureType::Tube);});
                if(selected==g_bodyIds.end())throw std::runtime_error("Showcase stock needs a visible frame or tube");
                selBody=(int)std::distance(g_bodyIds.begin(),selected);toggleStockPreview();g_toolModule=4;
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
                g_allSheetsPlaying=false;g_allSheetsStage=stage;g_toolModule=4;frameScene();g_status="Unbend preview at "+std::to_string((int)std::lround(showcaseProgress*100))+"% / document unchanged";
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
        selBody=(int)std::distance(g_bodyIds.begin(),sheetBody);target=tTarget;dist=tDist;dc=GetDC(g_window);drawFrame(dc);ReleaseDC(g_window,dc);
        auto playback=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==53;});
        auto nav=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==176;});
        if(playback==g_zones.end()||nav==g_zones.end())return 264;
        auto intersects=[](const Zone& a,const Zone& b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;};
        if(intersects(*playback,*nav))return 265;
        for(auto size:std::vector<std::pair<int,int>>{{1180,840},{1600,1000},{2923,1080}}){int vw=size.first-558,vh=size.second-320;
            float playTop=(float)viewY+vh-106,playBottom=(float)viewY+vh-63,navTop=(float)viewY+vh-49;
            float sliderEnd=(float)viewX+136+244+std::max(80.f,(float)vw-408.f);
            if(playBottom>=navTop||playTop<viewY||sliderEnd>viewX+vw)return 266;}
        g_status="Context UI checks passed / tree menus / Spotlight restore / named face-edge picks / separate playback rows";
    }
    if(sheetShowcaseTest){
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
    if(renderUiTest){
        using V=arkjson::Value;
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
            // Hash only the model area: exclude the view labels, left tool rail, view cube and bottom controls.
            for(int y=viewY+190;y<viewY+viewH-48;++y)for(int x=viewX+128;x<viewX+viewW-276;++x)for(int c=0;c<3;++c){digest^=image.px[((size_t)y*winW+x)*3+c];digest*=1099511628211ull;}
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
            if(!capture(".physics.png"))return 215;
        }
        {
            bool queuedCompleted=false;auto queued=std::make_unique<UiToolRequest>();queued->tool="doc_info";queued->args=V::makeObj();queued->complete=[&](V){queuedCompleted=true;};
            if(!PostMessage(g_window,WM_ARK_TOOL,0,(LPARAM)queued.get()))return 216;queued.release();
            PostMessage(g_window,WM_SIZE,0,MAKELPARAM(winW,winH));
            g_assistantReceiptId=newUsageReceiptId();g_assistantReceiptModel="offline-ui-smoke";
            AssistantUsage injected;injected.inputTokens=9;injected.outputTokens=3;injected.totalTokens=12;injected.reportedResponses=1;
            postChat({"",false,false,{},g_documentEpoch,injected,true});
            unsigned framesBefore=g_busyFrameCount,pumpedBefore=g_busyPumpCount,deferredBefore=g_deferredUsageCount;u64 revisionBefore=g_session.revision;
            g_busyTestMode=true;g_busyTestDelayMs=800;g_busyTestShot=shotPath+".loading.png";
            V args=V::makeObj(),updates=V::makeArr(),material=V::makeObj();material.set("op",V::makeStr("material.set"));material.set("name",V::makeStr("Aluminum"));material.set("density",V::makeNum(2.7));updates.arr.push_back(std::move(material));
            args.set("expected_revision",V::makeNum((double)revisionBefore));args.set("commands",std::move(updates));runTool("doc_apply",args);
            g_busyTestMode=false;g_busyTestDelayMs=0;g_busyTestShot.clear();
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
        sendAssistant("Offline integration test: new document, then create a box.",transport);if(!pump()||g_doc.getParam("agent_ui_probe")!=17||g_conversation.arr.empty()||g_doc.features.size()!=1||g_doc.features[0].type!=FeatureType::Box||g_path!="Untitled.ark")return 120;
        if(g_assistantUsageDocument.totalTokens!=375||g_assistantUsageDocument.reportedResponses!=3||!g_assistantUsageRunCounted)return 122;
        auto gate=std::make_shared<std::atomic_bool>(false);auto staleTransport=[gate,parse](const auto&,const auto&,const auto*){while(!*gate)std::this_thread::sleep_for(std::chrono::milliseconds(1));return parse(R"({"status":"completed","output":[{"type":"function_call","call_id":"stale_ui_test","name":"doc_apply","arguments":"{\"commands\":[{\"op\":\"param.set\",\"name\":\"stale_ui_probe\",\"expr\":\"99\"}]}"}]})");};
        sendAssistant("Offline stale-write test.",staleTransport);V command=parse(R"({"op":"param.set","name":"human_ui_probe","expr":"1"})");applyCommand(command);*gate=true;
        if(!pump()||!g_conversation.arr.empty()||std::any_of(g_doc.params.begin(),g_doc.params.end(),[](auto& p){return p.name=="stale_ui_probe";}))return 121;
        auto savedAuthStatus=g_authStatus;auto savedAuthModels=g_authModels;u64 authRevision=g_session.revision,authUsageSerial=g_doc.usage.serial;
        g_authStatus.accounts.push_back({"oaiapp_offline_ui","offline-subject","offline@example.test"});
        auto finishFakeAuth=[&](AuthJob job,const char* error){auto* event=new UiAuthEvent;event->job=job;event->status=g_authStatus;event->error=error;SendMessage(g_window,WM_ARK_AUTH,0,(LPARAM)event);};
        g_authBusy=true;g_authCancelRequested=false;g_authCancel=std::make_shared<std::atomic_bool>(false);
        uiAction(366);if(!*g_authCancel||!g_authBusy||!g_authCancelRequested)return 123;
        finishFakeAuth(AuthJob::SignIn,"ChatGPT sign-in cancelled");
        if(g_authBusy||g_authCancelRequested||g_status.find("fresh attempt")==std::string::npos||g_authStatus.accounts.size()!=1||g_session.revision!=authRevision||g_doc.usage.serial!=authUsageSerial)return 124;
        g_authBusy=true;g_authCancelRequested=false;
        finishFakeAuth(AuthJob::SignIn,"ChatGPT browser sign-in timed out");
        if(g_authBusy||g_status.find("callback")==std::string::npos||g_status.find("fresh attempt")==std::string::npos)return 125;
        g_authBusy=true;g_authCancelRequested=false;g_authCancel=std::make_shared<std::atomic_bool>(false);
        uiAction(366);finishFakeAuth(AuthJob::Models,"ChatGPT model loading cancelled");
        if(g_authBusy||g_status.find("model loading cancelled")==std::string::npos||g_authStatus.accounts.size()!=1)return 126;
        g_authStatus=std::move(savedAuthStatus);g_authModels=std::move(savedAuthModels);
        if(!fa.adv[0xe7]||!fa.adv[0xe3]||!fa.adv[0xf5])return 127; // cedilla and Portuguese vowels are present in the GL chat atlas.
        g_chatInput=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE,0,0,1,1,g_window,(HMENU)499,hi,nullptr);
        if(!g_chatInput)return 128;
        auto chatInputFits=[&]{RECT bounds{};GetWindowRect(g_chatInput,&bounds);POINT topLeft{bounds.left,bounds.top};ScreenToClient(g_window,&topLeft);
            return topLeft.x==viewX+30&&topLeft.y==winH-112&&bounds.right-bounds.left==std::max(viewW-260,1)&&bounds.bottom-bounds.top==44;};
        layout();if(!chatInputFits())return 129;
        const wchar_t* accent=L"Portugu\u00eas: a\u00e7\u00e3o";SetWindowTextW(g_chatInput,accent);wchar_t entered[80]{};GetWindowTextW(g_chatInput,entered,80);
        if(std::wcscmp(entered,accent)!=0)return 130;
        int originalWidth=winW,originalHeight=winH;winW=1180;winH=850;layout();if(!chatInputFits())return 131;
        winW=originalWidth;winH=originalHeight;layout();DestroyWindow(g_chatInput);g_chatInput=nullptr;
        if(g_assistantWorker.joinable())g_assistantWorker.join();g_assistantConfig=previous;g_status="Offline assistant UI checks passed / serialized tools / history / stale-write rejection";
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
        drawFrame(hdc);
        glFinish();
        if(GLenum glError=glGetError()){fprintf(stderr,"ARK GL error 0x%X\n",(unsigned)glError);return 20;}
        std::vector<u8> pix((size_t)winW*winH*3);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);
        glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pix.data());
        Image img; img.w=winW; img.h=winH; img.px.resize(pix.size());
        for(int y=0;y<winH;++y)
            memcpy(&img.px[(size_t)y*winW*3],&pix[(size_t)(winH-1-y)*winW*3],(size_t)winW*3);
        if(!writePNG(shotPath.c_str(),img))return 21;
        DestroyWindow(hwnd);
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
        drainUiTools();
        if(exitAfterMs&&std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-lifeStart).count()>=exitAfterMs){stopIntegrations();DestroyWindow(hwnd);return 0;}
        drawFrame(hdc);
        ++frames;
        auto now=std::chrono::steady_clock::now();
        f64 el=std::chrono::duration<f64,std::milli>(now-fpsT).count();
        if(el>500){ g_fps=frames*1000.0/el; frames=0; fpsT=now; }
        Sleep(1);
    }
}
