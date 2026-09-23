// ============================================================================
//  ARK-Point :: ark-view â€” native Win32 + OpenGL 3.3-core viewport.
//
//  Look & feel: dark studio gradient + vignette, procedural matcap shading,
//  rim light, soft ground shadows, distance-faded grid, damped orbit camera,
//  selection glow outline, GPU-drawn UI (rounded rects, GDI font atlas).
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

// ---- MinGW gl.h is GL 1.1: declare what we need ourselves ------------------
#ifndef GL_VERSION_3_0_DEFS
typedef ptrdiff_t GLsizeiptr;
#define GL_ARRAY_BUFFER            0x8892
#define GL_ELEMENT_ARRAY_BUFFER    0x8893
#define GL_STATIC_DRAW             0x88E4
#define GL_DYNAMIC_DRAW            0x88E8
#define GL_TEXTURE0                0x84C0
#define GL_R8                      0x8229
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
#include "ark/io.hpp"
#include "ark/agent.hpp"
#include "ark/mesh_analysis.hpp"
#include "ark/notch.hpp"
#include "ark/source_tube.hpp"
#include "ark/sketch.hpp"
#include "ark/sheet_preview.hpp"
#include "ark/serialize.hpp"
#include "ark/assistant.hpp"
#include "ark/live_session.hpp"
#include <thread>
#include <future>
#include <deque>
#include <filesystem>
#include <atomic>
#include <set>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstring>
#include <cstdlib>
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


#define DECL(T,N) static T N=nullptr;
GLFUNCS(DECL)
#undef DECL

typedef BOOL (WINAPI *WGLCHOOSEPFARB)(HDC,const int*,const FLOAT*,UINT,int*,UINT*);
typedef HGLRC (WINAPI *WGLCREATECTXATTRIBS)(HDC,HGLRC,const int*);
static WGLCHOOSEPFARB      wglChoosePixelFormatARB_=nullptr;
static WGLCREATECTXATTRIBS wglCreateContextAttribsARB_=nullptr;

static GLuint mkShader(GLenum type,const char* src){
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char lg[4096]; glGetShaderInfoLog(s,4096,nullptr,lg);
             MessageBoxA(nullptr,lg,"ARK shader error",MB_OK); }
    return s;
}
static GLuint mkProg(const char* vs,const char* fs){
    GLuint p=glCreateProgram();
    glAttachShader(p,mkShader(GL_VERTEX_SHADER,vs));
    glAttachShader(p,mkShader(GL_FRAGMENT_SHADER,fs));
    glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char lg[4096]; glGetProgramInfoLog(p,4096,nullptr,lg);
             MessageBoxA(nullptr,lg,"ARK link error",MB_OK); }
    return p;
}

// ---------------------------------------------------------------------------
struct GPUMesh { GLuint vao=0,vbo=0,ibo=0; int count=0; };
struct Zone { float x,y,w,h; int id; };

struct NotchViewCache { NotchPreview preview; std::vector<GPUMesh> gpu; std::vector<MeshBVH> bvhs; std::vector<AABB> bounds; std::vector<size_t> sourceIndices; };
static std::unordered_map<u64,NotchViewCache> g_notchViews;
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
static std::vector<MeshBVH> g_bvhs;
static size_t g_triangleCount=0;
static std::string g_status="Ready. Local C++ kernel.",g_path="Harmonic ring.ark";
static bool g_error=false,g_grid=true,g_dragged=false;
static u64 g_savedRevision=0;
static int g_treeScroll=0;
static int viewX=258,viewY=76,viewW=1024,viewH=650;
static HWND g_window=nullptr,g_chatInput=nullptr,g_chatHistory=nullptr;
static bool g_lightBackground=false,g_satin=true,g_showSketches=false,g_showDatumPlanes=false,g_chatBusy=false;
static std::string g_activeSketch,g_activePlane="XY";
static int g_selectedSketchConstraint=-1,g_sketchConstraintPage=0;
static arkjson::Value g_sketchDiagnosis;
static AssistantConfig g_assistantConfig;static arkjson::Value g_conversation=arkjson::Value::makeArr();
static std::vector<std::pair<std::string,std::string>> g_chatLog;static int g_chatScroll=0;
static std::thread g_assistantWorker;static std::shared_ptr<std::atomic_bool> g_assistantCancel=std::make_shared<std::atomic_bool>(false);
static std::atomic_bool g_closing{false};static LiveSessionBridge g_liveBridge;static u64 g_documentEpoch=0;static bool g_ownAssistantTool=false;
static constexpr UINT WM_ARK_TOOL=WM_APP+20,WM_ARK_CHAT=WM_APP+21;
struct UiToolRequest {std::string tool;arkjson::Value args;LiveSessionBridge::Completion complete;std::stop_token stop;std::shared_ptr<std::atomic_bool> cancel;std::shared_ptr<std::atomic<u64>> revision;u64 epoch=0;};
struct UiChatEvent {std::string text;bool final=false,error=false;arkjson::Value history;u64 epoch=0;};
static std::deque<std::unique_ptr<UiToolRequest>> g_toolQueue;
static GLuint sketchVao=0,sketchVbo=0;static bool g_sketchGpuDirty=true;static std::vector<float> g_sketchLines,g_sketchPoints,g_constraintHighlightLines,g_datumLines;static AABB g_sketchBounds;


static std::vector<Zone>    g_zones;
static std::vector<GPUMesh> g_meshes;
static std::vector<Mesh>    g_cpuMeshes;

static f64 yaw=0.85,pitch=0.46,dist=140,tYaw=yaw,tPitch=pitch,tDist=dist;
static Vec3 target{30,12,0},tTarget=target;
static int winW=1600,winH=1000;
static void layout(){viewW=std::max(winW-558,1);viewH=std::max(winH-320,1);}
static int selBody=-1,hoverBody=-1;
static bool wireframe=false, g_pressWasButton=false;
static double g_fps=0;
static int g_hoverZone=-1;

// ---------------------------------------------------------------------------
// Font atlas -----------------------------------------------------------------------
struct FontAtlas { GLuint tex=0; float x0[128]={},x1[128]={},adv[128]={};
                   int w=1024,h=64,lineH=18; };
static FontAtlas fa;

static void buildFontAtlas(){
    HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=fa.w; bi.bmiHeader.biHeight=-fa.h;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    void* bits=nullptr;
    HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    HGDIOBJ ob=SelectObject(dc,bmp);
    HFONT fnt=CreateFontA(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY,DEFAULT_PITCH,"Segoe UI");
    HGDIOBJ of=SelectObject(dc,fnt);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
    PatBlt(dc,0,0,fa.w,fa.h,BLACKNESS);
    TEXTMETRICA tm; GetTextMetricsA(dc,&tm);
    fa.lineH=tm.tmHeight+2;
    int x=1;
    for(int ch=32;ch<127;++ch){
        char s=(char)ch; SIZE sz; GetTextExtentPoint32A(dc,&s,1,&sz);
        if(x+sz.cx+2>=fa.w) break;
        TextOutA(dc,x,4,&s,1);
        fa.x0[ch]=(float)x; fa.x1[ch]=(float)(x+sz.cx); fa.adv[ch]=(float)sz.cx;
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
static void textOut(const std::string& s,float x,float y,float sc=1.f,float red=.87f,float green=.89f,float blue=.88f){
    float cx=x;
    for(unsigned char c:s){
        if(c<32||c>126)c=' ';
        float u0=fa.x0[c]/fa.w,u1=fa.x1[c]/fa.w;
        float wd=(fa.x1[c]-fa.x0[c])*sc,hh=(float)fa.lineH*sc;
        float v0=4.f/fa.h,v1=(4.f+fa.lineH)/fa.h;
        const float q[6][4]={
            {cx,y+hh,u0,v1},{cx+wd,y+hh,u1,v1},{cx+wd,y,u1,v0},
            {cx,y+hh,u0,v1},{cx+wd,y,u1,v0},{cx,y,u0,v0}};
        for(auto&r:q){g_textVerts.insert(g_textVerts.end(),r,r+4);g_textVerts.insert(g_textVerts.end(),{red,green,blue});}
        cx+=fa.adv[c]*sc;
    }
}
static float textWidth(const std::string& s,float sc=1.f){
    float w=0; for(unsigned char c:s){if(c<32||c>126)c=' ';w+=fa.adv[c]*sc;} return w;
}

// ---------------------------------------------------------------------------
// Shaders -----------------------------------------------------------------------
static const char* VS_BG=R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);})";
static const char* FS_BG=R"(#version 330 core
out vec4 o;uniform vec2 res;uniform float lightBackground;
void main(){
 vec2 uv=gl_FragCoord.xy/res;
 vec3 top=vec3(.19,.20,.22),bot=vec3(.075,.080,.089);
 vec3 c=mix(bot,top,uv.y);
 vec2 q=uv-.5;c*=1.-dot(q,q)*.55;
 c+=vec3(.045)*exp(-length(q-vec2(-.25,.25))*2.4);
 c=mix(c,vec3(.70,.72,.75)+vec3(.10)*uv.y,lightBackground);o=vec4(c,1.);
})";
static const char* VS_SOLID=R"(#version 330 core
layout(location=0) in vec3 P; layout(location=1) in vec3 N;
uniform mat4 vp,model; uniform float outline;
out vec3 nrm,wpos;
void main(){
 vec3 p=P; if(outline>0.)p+=N*outline;
 vec4 w=model*vec4(p,1.); wpos=w.xyz; nrm=mat3(model)*N;
 gl_Position=vp*w;
})";
static const char* FS_SOLID=R"(#version 330 core
in vec3 nrm,wpos; out vec4 o;
uniform vec3 color,eye; uniform float sel,hardSurface,satin;
void main(){
 vec3 n=hardSurface>0.?normalize(cross(dFdx(wpos),dFdy(wpos))):normalize(nrm); vec3 v=normalize(eye-wpos);
 if(dot(n,v)<0.)n=-n;
 // Broad studio softboxes and neutral steel reflections, independent of part IDs.
 vec3 l1=normalize(vec3(-.45,.85,.65)),l2=normalize(vec3(.75,.25,-.55));
 float d1=max(dot(n,l1),0.),d2=max(dot(n,l2),0.);
 vec3 base=color*(.24+.57*d1+.19*d2);
 vec3 r=reflect(-v,n);
 float softbox=pow(max(dot(r,normalize(vec3(-.45,.85,.55))),0.),18.);
 float strip=pow(max(dot(r,normalize(vec3(.85,.35,-.45))),0.),48.);
 base+=(vec3(.90,.93,.98)*softbox*.36+vec3(.80,.83,.87)*strip*.26)*satin;
 float fr=pow(1.-max(dot(n,v),0.),4.);
 base+=vec3(.63,.68,.76)*fr*.16;
 base=mix(base,base+vec3(.045,.055,.075),sel);
 o=vec4(base,1.);
})";
static const char* VS_LINE=R"(#version 330 core
layout(location=0) in vec3 P; uniform mat4 vp; out vec2 gridPos;
void main(){gl_Position=vp*vec4(P,1.);gridPos=P.xz;})";
static const char* FS_LINE=R"(#version 330 core
in vec2 gridPos; out vec4 o;
void main(){o=vec4(.40,.42,.46,exp(-dot(gridPos,gridPos)*.00012)*.28);})";
static const char* VS_SHADOW=R"(#version 330 core
layout(location=0) in vec2 P; uniform mat4 vp; uniform vec3 ctr; uniform vec2 rad;
out vec2 uv;
void main(){uv=P;gl_Position=vp*vec4(ctr+vec3(P.x*rad.x,0,P.y*rad.y),1.);})";
static const char* FS_SHADOW=R"(#version 330 core
in vec2 uv; out vec4 o;
void main(){o=vec4(0.,0.,0.,smoothstep(1.,.1,length(uv))*.42);})";
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
    g_sketchGpuDirty=true;g_sketchLines.clear();g_sketchPoints.clear();g_constraintHighlightLines.clear();g_datumLines.clear();g_sketchBounds=AABB{};g_sketchDiagnosis=arkjson::Value{};
    auto put=[](std::vector<float>& into,Vec3 p){into.insert(into.end(),{(float)p.x,(float)p.y,(float)p.z});};
    for(auto& named:g_doc.sketches){if(!g_activeSketch.empty()&&named.name!=g_activeSketch)continue;auto frame=resolvePlane(g_doc,named.sk.plane);
        if(named.name==g_activeSketch){g_sketchDiagnosis=diagnoseSketch(named.sk);if(g_selectedSketchConstraint>=(int)named.sk.cons.size())g_selectedSketchConstraint=-1;g_sketchConstraintPage=std::min(g_sketchConstraintPage,(int)((named.sk.cons.size()+3)/4)-1);g_sketchConstraintPage=std::max(g_sketchConstraintPage,0);}
        const SketchCon* selected=(named.name==g_activeSketch&&g_selectedSketchConstraint>=0&&g_selectedSketchConstraint<(int)named.sk.cons.size())?&named.sk.cons[(size_t)g_selectedSketchConstraint]:nullptr;
        for(size_t ei=0;ei<named.sk.ents.size();++ei){auto& entity=named.sk.ents[ei];auto sampled=sampleSketchEntity(entity,named.sk.tolerance);for(size_t i=1;i<sampled.size();++i){auto a=frame.transformPoint({sampled[i-1].x,sampled[i-1].y,0}),b=frame.transformPoint({sampled[i].x,sampled[i].y,0});put(g_sketchLines,a);put(g_sketchLines,b);g_sketchBounds.grow(a);g_sketchBounds.grow(b);if(selected&&((int)ei==selected->a||(int)ei==selected->b)){put(g_constraintHighlightLines,a);put(g_constraintHighlightLines,b);}}
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
}
static void rebuildScene(bool evaluate=false){
    if(evaluate)g_doc.recompute();
    g_foldPlaying=false;g_sheetPlayingId=0;
    for(auto it=g_notchViews.begin();it!=g_notchViews.end();){auto* f=g_doc.find(it->first);if(!f||f->type!=FeatureType::NotchedTube){for(auto& mesh:it->second.gpu)releaseMesh(mesh);it=g_notchViews.erase(it);}else ++it;}
    for(auto it=g_sourceViews.begin();it!=g_sourceViews.end();){auto* f=g_doc.find(it->first);if(!f||f->type!=FeatureType::SourceTube){for(auto* list:{&it->second.rigidGpu,&it->second.flatHingeGpu,&it->second.hingeGpu})for(auto& mesh:*list)releaseMesh(mesh);it=g_sourceViews.erase(it);}else ++it;}
    std::vector<AssemblyPlacement> placements;std::string error;
    g_assemblyDiagnostics={};
    if(g_doc.assembly.empty()){
        std::set<u64> consumed;for(auto& f:g_doc.features)for(auto id:f.inputs)consumed.insert(id);
        for(auto& b:g_doc.evalBodies)if(!consumed.count(b.id)){auto* f=g_doc.find(b.id);placements.push_back({0,b.id,f?f->name:"Body",Mat4{},true});}
    }else if(!resolveAssembly(g_doc,placements,error,&g_assemblyDiagnostics)){g_error=true;g_status=error;}
    if(g_selectedMate>=(int)g_doc.mates.size())g_selectedMate=-1;
    g_cpuMeshes.clear();g_bodyIds.clear();g_instanceIds.clear();g_instanceNames.clear();g_instanceWorld.clear();g_instancePart.clear();
    g_reports.clear();g_bvhs.clear();g_tubeInfo.clear();g_triangleCount=0;g_sceneBounds=AABB{};
    for(auto& gm:g_meshes)gm.count=0;
    std::unordered_map<u64,size_t> parts;
    for(auto& placement:placements){
        if(!placement.visible)continue;
        auto part=parts.find(placement.feature);
        if(part==parts.end()){
            auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& b){return b.id==placement.feature;});if(body==g_doc.evalBodies.end())continue;
            size_t i=g_cpuMeshes.size();Mesh display=body->tessellate(g_doc.tessOpt);
            if(auto* f=g_doc.find(body->id);f&&(f->type==FeatureType::SheetMetal||f->type==FeatureType::SourceSheet)){
                double progress=g_sheetProgress.count(body->id)?g_sheetProgress[body->id]:g_sheetFlatViews[body->id]?0.:1.;
                if(progress<=0){if(auto* flat=body->nativeData.find("flat_mesh"))display=nativePreviewMesh(*flat);}
                else if(progress<1&&f->type==FeatureType::SheetMetal&&body->nativeData.find("animation"))display=buildSheetPreview(body->nativeData,progress).mesh;
            }
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
            if(i>=g_meshes.size())g_meshes.emplace_back();uploadMesh(g_meshes[i],g_cpuMeshes.back());part=parts.emplace(placement.feature,i).first;
        }
        auto i=part->second;g_bodyIds.push_back(placement.feature);g_instanceIds.push_back(placement.node);g_instanceNames.push_back(placement.name);g_instanceWorld.push_back(placement.world);g_instancePart.push_back(i);
        g_sceneBounds.grow(g_reports[i].bounds.transformed(placement.world));g_triangleCount+=g_cpuMeshes[i].triCount();
    }
    if(selBody<0||selBody>=(int)g_bodyIds.size())selBody=g_bodyIds.empty()?-1:0;
    hoverBody=-1;
    if(!g_activeSketch.empty()&&!g_doc.findSketch(g_activeSketch))g_activeSketch.clear();
    if(g_activePlane!="XY"&&g_activePlane!="XZ"&&g_activePlane!="YZ"&&std::none_of(g_doc.planes.begin(),g_doc.planes.end(),[](auto& p){return p.name==g_activePlane;}))g_activePlane="XY";
    rebuildSketchOverlay();
    if(auto* f=selectedNotchedTube())g_selectedNotch=std::max(0,std::min(g_selectedNotch,(int)f->notches.size()-1));
}
static double sheetDisplayProgress(u64 id){auto it=g_sheetProgress.find(id);return it==g_sheetProgress.end()?(g_sheetFlatViews[id]?0.:1.):it->second;}
static void updateSheetDisplay(u64 id){
    auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& candidate){return candidate.id==id;});
    if(body==g_doc.evalBodies.end())return;
    auto* feature=g_doc.find(id);if(!feature||!(feature->type==FeatureType::SheetMetal||feature->type==FeatureType::SourceSheet))return;
    double progress=sheetDisplayProgress(id);Mesh mesh;
    if(progress<=0){auto* flat=body->nativeData.find("flat_mesh");if(!flat)throw std::runtime_error("Native flat sheet preview is unavailable");mesh=nativePreviewMesh(*flat);}
    else if(progress>=1)mesh=body->tessellate(g_doc.tessOpt);
    else {if(feature->type!=FeatureType::SheetMetal||!body->nativeData.find("animation"))throw std::runtime_error("This sheet has no authored bend animation");mesh=buildSheetPreview(body->nativeData,progress).mesh;}
    std::set<size_t> changed;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id)changed.insert(g_instancePart[i]);
    for(size_t part:changed){g_cpuMeshes[part]=mesh;uploadMesh(g_meshes[part],g_cpuMeshes[part]);g_reports[part]=analyzeMesh(g_cpuMeshes[part]);g_bvhs[part]=MeshBVH(g_cpuMeshes[part]);}
    g_sceneBounds=AABB{};g_triangleCount=0;
    for(size_t i=0;i<g_bodyIds.size();++i){size_t part=g_instancePart[i];g_sceneBounds.grow(g_reports[part].bounds.transformed(g_instanceWorld[i]));g_triangleCount+=g_cpuMeshes[part].triCount();}
}
static void sheetPreviewSet(u64 id,double progress){
    progress=clamp(progress,0.,1.);double before=sheetDisplayProgress(id);bool wasFlat=g_sheetFlatViews[id];
    g_sheetProgress[id]=progress;g_sheetFlatViews[id]=progress==0.;
    try{updateSheetDisplay(id);}catch(...){g_sheetProgress[id]=before;g_sheetFlatViews[id]=wasFlat;throw;}
}
static void advanceSheet(double seconds){
    if(!g_sheetPlayingId)return;
    auto* selected=selectedSheet();if(!selected||selected->id!=g_sheetPlayingId||selected->type!=FeatureType::SheetMetal){g_sheetPlayingId=0;return;}
    double progress=std::min(1.,sheetDisplayProgress(selected->id)+seconds*.22);
    try{sheetPreviewSet(selected->id,progress);if(progress>=1)g_sheetPlayingId=0;}
    catch(const std::exception& error){g_sheetPlayingId=0;g_error=true;g_status=error.what();}
}
static void frameScene(bool animation=false){
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
    if(b.valid()){tTarget=b.center();tDist=std::max(b.diagonal()*1.65,2.);}
}
static arkjson::Value runTool(const std::string& tool,const arkjson::Value& args){
    auto result=g_session.call(tool,args);g_error=result.find("isError")->b;
    if(g_error)g_status=result.find("structuredContent")->find("error")->asStr();
    else g_status=tool+" completed  /  revision "+std::to_string(g_session.revision);
    if(!g_error&&(tool=="doc_load"||tool=="doc_new"||tool=="import_step"||tool=="import_sheet_step")){g_sheetFlatViews.clear();g_sheetProgress.clear();g_sheetPlayingId=0;}
    if(!g_error&&(tool=="doc_apply"||tool=="doc_load"||tool=="doc_undo"||tool=="doc_redo"||tool=="import_step"||tool=="import_sheet_step"||tool=="doc_new"))rebuildScene();
    if(!g_error&&(tool=="doc_save"||tool=="doc_load")){if(auto* path=args.find("path"))g_path=path->asStr();g_savedRevision=g_session.revision;}
    if(!g_error&&(tool=="doc_load"||tool=="doc_new")){if(!g_ownAssistantTool){++g_documentEpoch;*g_assistantCancel=true;g_conversation=arkjson::Value::makeArr();g_chatLog.clear();if(g_chatHistory)SetWindowTextW(g_chatHistory,L"Document changed. Describe what you want to create.");}if(tool=="doc_new"){g_path="Untitled.ark";g_savedRevision=g_session.revision;g_activeSketch.clear();g_activePlane="XY";}g_foldProgress=selectedSourceTube()?1.:0.;frameScene();}
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
static HBRUSH dialogBrush=CreateSolidBrush(RGB(29,31,36)),fieldBrush=CreateSolidBrush(RGB(42,45,52));
static LRESULT CALLBACK editorProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND&&(LOWORD(w)==1||LOWORD(w)==2)){g_dialogAccept=LOWORD(w)==1;g_dialogDone=true;return 0;}
    if(m==WM_CLOSE){g_dialogDone=true;return 0;}
    if(m==WM_TIMER&&w==901){
        KillTimer(h,901);
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
    if(g_session.revision==g_savedRevision)return true;
    int answer=MessageBoxA(g_window,"Save changes to this ARK document?","Unsaved document",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(answer==IDCANCEL)return false;if(answer==IDYES){fileAction(true);return g_session.revision==g_savedRevision;}return true;
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
static void addNativeFeature(FeatureType type,const std::string& name,arkjson::Value p,arkjson::Value native=arkjson::Value::makeObj(),std::vector<u64> inputs={}){
    using V=arkjson::Value;V command=V::makeObj(),parents=V::makeArr();
    command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr(featureTypeName(type)));command.set("name",V::makeStr(name));command.set("p",std::move(p));
    command.set("native",std::move(native));for(u64 id:inputs)parents.arr.push_back(V::makeNum((double)id));command.set("inputs",std::move(parents));
    applyCommand(std::move(command));if(!g_error){selBody=(int)g_bodyIds.size()-1;frameScene();}
}
static void loftDialog(){
    using V=arkjson::Value;std::string initial=g_doc.sketches.size()>=2?g_doc.sketches[0].name+"; "+g_doc.sketches[1].name:"Section 1; Section 2";
    std::vector<EditorField> fields{{"Section sketches",initial},{"Solid","1",true},{"Ruled","0",true}};
    std::string hint=sketchNames()+"  /  List 2 or more sections in order, separated by semicolons.";
    if(!editFields("Loft through sketches",hint.c_str(),fields))return;
    V sections=V::makeArr();std::istringstream names(fields[0].value);std::string name;
    while(std::getline(names,name,';')){name=trimField(name);if(name.empty())continue;if(!g_doc.findSketch(name))throw std::runtime_error("Unknown loft section: "+name);sections.arr.push_back(V::makeStr(name));}
    if(sections.arr.size()<2||sections.arr.size()>32)throw std::runtime_error("Loft needs 2 to 32 saved sketches");
    V native=V::makeObj(),p=V::makeObj();native.set("sections",std::move(sections));p.set("solid",V::makeStr(fields[1].value));p.set("ruled",V::makeStr(fields[2].value));
    addNativeFeature(FeatureType::Loft,"Loft",std::move(p),std::move(native));
}
static void surfaceFillDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Closed sketch",g_activeSketch.empty()?"Profile":g_activeSketch}};
    auto hint=sketchNames();if(!editFields("Fill sketch boundary",hint.c_str(),fields))return;
    auto name=trimField(fields[0].value);if(!g_doc.findSketch(name))throw std::runtime_error("Choose an existing closed sketch");
    V p=V::makeObj();p.set("sketch",V::makeStr(name));addNativeFeature(FeatureType::SurfaceFill,"Surface fill",std::move(p));
}
static void nurbsSurfaceDialog(){
    using V=arkjson::Value;
    std::string specification=R"({
  "u_degree": 3, "v_degree": 3,
  "poles": [
    [[0,0,0], [0,15,0], [0,30,0], [0,45,0]],
    [[15,0,0], [15,15,8], [15,30,8], [15,45,0]],
    [[30,0,0], [30,15,8], [30,30,8], [30,45,0]],
    [[45,0,0], [45,15,0], [45,30,0], [45,45,0]]
  ]
})";
    if(!editText("NURBS surface / pole grid JSON / Ctrl+Enter to apply",specification,true))return;
    V spec;if(!arkjson::parse(specification,spec)||spec.t!=V::Obj||!spec.find("poles"))throw std::runtime_error("NURBS grid needs JSON poles");
    V native=V::makeObj(),p=V::makeObj();native.set("poles",*spec.find("poles"));if(auto* weights=spec.find("weights"))native.set("weights",*weights);
    for(auto key:{"u_degree","v_degree"}){auto* degree=spec.find(key);if(degree&&degree->t!=V::Num&&degree->t!=V::Str)throw std::runtime_error(std::string(key)+" must be a degree");p.set(key,V::makeStr(degree?(degree->t==V::Str?degree->str:degree->dump()):"3"));}
    addNativeFeature(FeatureType::BSplineSurface,"NURBS surface",std::move(p),std::move(native));
}
static void thickenDialog(){
    using V=arkjson::Value;if(selBody<0){g_status="Select a native open surface to thicken.";return;}
    auto* source=g_doc.find(g_bodyIds[selBody]);if(!source||(source->type!=FeatureType::SurfaceFill&&source->type!=FeatureType::BSplineSurface&&source->type!=FeatureType::Loft)){
        g_status="Select a loft, surface fill, or NURBS surface.";return;
    }
    std::vector<EditorField> fields{{"Thickness / mm","2"}};
    if(!editFields("Thicken native surface","Signed thickness is measured from the selected OCCT face or shell.",fields))return;
    V p=V::makeObj();p.set("thickness",V::makeStr(fields[0].value));addNativeFeature(FeatureType::Thicken,"Thicken",std::move(p),V::makeObj(),{source->id});
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
    std::string kind=get("kind","circle");if(!edit){std::vector<EditorField> pick{{"Opening: circle / polygon",kind}};if(!editFields("Choose sheet opening","Circle or a polygonal opening in panel coordinates.",pick))return;kind=trimField(pick[0].value);}
    if(kind!="circle"&&kind!="polygon")throw std::runtime_error("Opening kind must be circle or polygon");
    std::vector<EditorField> fields{{"Panel ID",get("panel",sheetBaseId(*feature).c_str())}};
    if(kind=="circle"){fields.push_back({"X / mm",get("x","25")});fields.push_back({"Y / mm",get("y","25")});fields.push_back({"Radius / mm",get("radius","5")});}
    else{auto* points=row.find("points");fields.push_back({"Points: x,y; x,y; ...",points?sheetPolygonText(*points):"15,15; 35,15; 35,30; 15,30"});}
    if(!editFields(edit?"Edit sheet opening":"Add sheet opening","Panel local coordinates accept parameters and panel_length / panel_width expressions.",fields))return;
    V values=V::makeObj(),command=V::makeObj();values.set("panel",V::makeStr(trimField(fields[0].value)));values.set("kind",V::makeStr(kind));
    if(kind=="circle"){values.set("x",V::makeStr(trimField(fields[1].value)));values.set("y",V::makeStr(trimField(fields[2].value)));values.set("radius",V::makeStr(trimField(fields[3].value)));}
    else values.set("points",sheetPolygonPoints(fields[1].value));
    command.set("op",V::makeStr(edit?"sheet.opening.edit":"sheet.opening.add"));command.set("id",V::makeNum((double)feature->id));
    if(edit){command.set("entity",V::makeStr(entity));command.set("values",std::move(values));}else command.set("opening",std::move(values));applyCommand(command);
}
static void removeSheetOpeningDialog(){
    using V=arkjson::Value;auto* feature=authoredSheet();auto entity=sheetEntityIdDialog(*feature,"openings","Remove sheet opening");if(entity.empty())return;
    V command=V::makeObj();command.set("op",V::makeStr("sheet.opening.remove"));command.set("id",V::makeNum((double)feature->id));command.set("entity",V::makeStr(entity));applyCommand(command);
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
    bool surface=f->type==FeatureType::Loft||f->type==FeatureType::SurfaceFill||f->type==FeatureType::BSplineSurface||f->type==FeatureType::Thicken||f->type==FeatureType::NativeSolid;
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
static void sheetDisplay(bool flat){
    auto* sheet=selectedSheet();auto* body=selectedNativeBody();if(!sheet||!body)return;
    if(flat&&!body->nativeData.find("flat_mesh")){g_error=true;g_status="Native flat preview is unavailable for this sheet.";return;}
    g_sheetPlayingId=0;sheetPreviewSet(sheet->id,flat?0.:1.);frameScene();g_error=false;g_status=flat?"Flat sheet display / document unchanged":"Formed sheet display / document unchanged";
}
static void toggleSheetPlayback(){
    auto* sheet=selectedSheet();auto* body=selectedNativeBody();if(!sheet||sheet->type!=FeatureType::SheetMetal||!body||!body->nativeData.find("animation"))throw std::runtime_error("Select an authored sheet with a bend program");
    if(g_sheetPlayingId==sheet->id){g_sheetPlayingId=0;return;}
    if(sheetDisplayProgress(sheet->id)>=1)sheetPreviewSet(sheet->id,0);
    g_sheetPlayingId=sheet->id;g_status="Playing authored bend preview / document unchanged";
}
static u64 featureIdField(const std::string& value){
    double parsed=formNumber(value);if(parsed<1||parsed>9007199254740990.||std::floor(parsed)!=parsed||!g_doc.find((u64)parsed))throw std::runtime_error("Choose an existing feature ID");return (u64)parsed;
}
static std::string selectedFeatureId(){return selBody>=0&&selBody<(int)g_bodyIds.size()?std::to_string(g_bodyIds[(size_t)selBody]):"1";}
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
static void nativeEdgeBlendDialog(){
    using V=arkjson::Value;std::vector<EditorField> fields{{"Operation: fillet / chamfer","fillet"},{"Base feature ID",selectedFeatureId()},{"Semantic edges (semicolon separated)","edge.x.ymin.zmin"},{"Radius or distance / mm","2"}};
    auto hint=nativeReferenceHint("edge");if(!editFields("Exact edge treatment",hint.c_str(),fields))return;
    auto op=trimField(fields[0].value);if(op!="fillet"&&op!="chamfer")throw std::runtime_error("Choose fillet or chamfer");
    auto base=featureIdField(fields[1].value);V edges=V::makeArr();std::istringstream names(fields[2].value);std::string name;
    while(std::getline(names,name,';'))if(!(name=trimField(name)).empty())edges.arr.push_back(V::makeStr(name));
    if(edges.arr.empty())throw std::runtime_error("Enter at least one semantic edge name");
    V native=V::makeObj(),p=V::makeObj();native.set("edges",std::move(edges));p.set("op",V::makeStr(op));p.set(op=="fillet"?"radius":"distance",V::makeStr(trimField(fields[3].value)));
    addNativeFeature(FeatureType::NativeSolid,"Exact "+op,std::move(p),std::move(native),{base});
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
    using V=arkjson::Value;V j=V::makeObj();j.set("endpoint",V::makeStr(g_assistantConfig.endpoint));j.set("model",V::makeStr(g_assistantConfig.model));j.set("api_key_env",V::makeStr(g_assistantConfig.apiKeyEnv));j.set("grid",V::makeBool(g_grid));j.set("wireframe",V::makeBool(wireframe));j.set("light_background",V::makeBool(g_lightBackground));j.set("satin",V::makeBool(g_satin));j.set("show_datum_planes",V::makeBool(g_showDatumPlanes));
    auto path=uiSettingsPath();std::filesystem::create_directories(path.parent_path());std::ofstream out(path);out<<j.dump();if(!out)throw std::runtime_error("Could not save Studio settings");
}
static void loadUiSettings(){
    try{std::ifstream in(uiSettingsPath());if(!in)return;std::string data((std::istreambuf_iterator<char>(in)),{});arkjson::Value j;if(!arkjson::parse(data,j))return;
        if(auto* v=j.find("endpoint"))g_assistantConfig.endpoint=v->asStr();if(auto* v=j.find("model"))g_assistantConfig.model=v->asStr();if(auto* v=j.find("api_key_env"))g_assistantConfig.apiKeyEnv=v->asStr();
        if(auto* v=j.find("grid"))g_grid=v->b;if(auto* v=j.find("wireframe"))wireframe=v->b;if(auto* v=j.find("light_background"))g_lightBackground=v->b;if(auto* v=j.find("satin"))g_satin=v->b;if(auto* v=j.find("show_datum_planes");v&&v->t==arkjson::Value::Bool)g_showDatumPlanes=v->b;
    }catch(...){g_status="Studio settings unavailable; defaults loaded";}
}
static void graphicsDialog(){
    std::vector<EditorField> fields{{"Show ground grid",g_grid?"1":"0",true},{"Wireframe",wireframe?"1":"0",true},{"Light background",g_lightBackground?"1":"0",true},{"Satin highlights",g_satin?"1":"0",true}};
    if(!editFields("Graphics settings","Viewport preferences. These do not modify document geometry.",fields))return;
    bool values[4];for(int i=0;i<4;++i){double n=formNumber(fields[i].value);if(n!=0&&n!=1)throw std::runtime_error("Graphics toggles accept 0 or 1");values[i]=n!=0;}
    g_grid=values[0];wireframe=values[1];g_lightBackground=values[2];g_satin=values[3];if(!g_dialogSmoke)saveUiSettings();
}
static void connectorDialog(){
    std::vector<EditorField> fields{{"Responses endpoint",g_assistantConfig.endpoint},{"Model",g_assistantConfig.model},{"API key environment",g_assistantConfig.apiKeyEnv}};
    if(!editFields("Assistant connector","Use a Responses-compatible API. Enter an environment-variable NAME, never the secret key.",fields))return;
    auto config=g_assistantConfig;config.endpoint=fields[0].value;config.model=fields[1].value;config.apiKeyEnv=fields[2].value;validateAssistantConfig(config);g_assistantConfig=config;*g_assistantCancel=true;++g_documentEpoch;g_conversation=arkjson::Value::makeArr();if(!g_dialogSmoke)saveUiSettings();
    g_status=assistantKeyAvailable(config)?"Connector configured; credential present. No request sent.":"Connector configured; credential environment variable is unavailable.";
}
static arkjson::Value cancelledTool(){using V=arkjson::Value;V r=V::makeObj(),e=V::makeObj();e.set("error",V::makeStr("Request cancelled before execution"));r.set("isError",V::makeBool(true));r.set("structuredContent",e);return r;}
static void refreshChatHistory();
static void drainUiTools(){
    if(g_dialog)return;
    while(!g_toolQueue.empty()){auto request=std::move(g_toolQueue.front());g_toolQueue.pop_front();
        if(g_closing||request->stop.stop_requested()||(request->cancel&&*request->cancel)){request->complete(cancelledTool());continue;}
        if(request->revision&&(request->epoch!=g_documentEpoch||*request->revision!=g_session.revision)){auto result=cancelledTool();arkjson::Value error=arkjson::Value::makeObj();g_status="Document changed during assistant turn. Inspect and resend from the current revision.";error.set("error",arkjson::Value::makeStr(g_status));result.set("structuredContent",error);g_chatLog.push_back({"Studio",g_status});refreshChatHistory();request->complete(result);if(request->cancel)*request->cancel=true;continue;}
        try{g_ownAssistantTool=(bool)request->revision;auto result=runTool(request->tool,request->args);g_ownAssistantTool=false;if(request->revision)*request->revision=g_session.revision;request->complete(result);}catch(...){g_ownAssistantTool=false;request->complete(cancelledTool());}
    }
}
static void postTool(std::unique_ptr<UiToolRequest> request){
    if(g_closing||!PostMessage(g_window,WM_ARK_TOOL,0,(LPARAM)request.get())){request->complete(cancelledTool());return;}request.release();
}
static void postChat(UiChatEvent event){auto* pointer=new UiChatEvent(std::move(event));if(g_closing||!PostMessage(g_window,WM_ARK_CHAT,0,(LPARAM)pointer))delete pointer;}
static void refreshChatHistory(){
    if(!g_chatHistory)return;std::string text;
    for(auto& entry:g_chatLog)text+=entry.first+": "+entry.second+"\r\n\r\n";
    if(text.empty())text="Describe a part, request an inspection, or connect Codex to this document.";
    int size=MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,nullptr,0);std::wstring wide(size,L'\0');MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,wide.data(),size);SetWindowTextW(g_chatHistory,wide.c_str());SendMessage(g_chatHistory,EM_SETSEL,-1,-1);SendMessage(g_chatHistory,EM_SCROLLCARET,0,0);
}
static void sendAssistant(const std::string& explicitPrompt="",const AssistantTransport& transport={}){
    if(g_chatBusy)return;std::string prompt=explicitPrompt;
    if(prompt.empty()&&g_chatInput){int n=GetWindowTextLengthW(g_chatInput);std::vector<wchar_t> buffer(n+1);GetWindowTextW(g_chatInput,buffer.data(),n+1);int size=WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,nullptr,0,nullptr,nullptr);prompt.resize(size);WideCharToMultiByte(CP_UTF8,0,buffer.data(),n,prompt.data(),size,nullptr,nullptr);}
    if(prompt.find_first_not_of(" \r\n\t")==std::string::npos)return;validateAssistantConfig(g_assistantConfig);
    if(g_assistantWorker.joinable())g_assistantWorker.join();
    using V=arkjson::Value;V input=g_conversation;if(input.t!=V::Arr)input=V::makeArr();V user=V::makeObj();user.set("role",V::makeStr("user"));user.set("content",V::makeStr(prompt));input.arr.push_back(user);
    g_chatLog.push_back({"You",prompt});refreshChatHistory();g_chatScroll=0;g_chatBusy=true;g_assistantCancel=std::make_shared<std::atomic_bool>(false);auto cancel=g_assistantCancel;auto config=g_assistantConfig;auto schema=AgentSession::tools();if(g_chatInput)SetWindowTextA(g_chatInput,"");
    auto epoch=g_documentEpoch;auto revision=std::make_shared<std::atomic<u64>>(g_session.revision);
    g_assistantWorker=std::thread([config,input,schema,cancel,transport,epoch,revision]{
        try{auto result=runAssistant(config,input,schema,[cancel,epoch,revision](const std::string& name,const V& args){
            auto promise=std::make_shared<std::promise<V>>();auto future=promise->get_future();auto request=std::make_unique<UiToolRequest>();request->tool=name;request->args=args;request->cancel=cancel;request->epoch=epoch;request->revision=revision;request->complete=[promise](V result){promise->set_value(std::move(result));};postTool(std::move(request));
            while(future.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready)if(*cancel||g_closing)throw std::runtime_error("Assistant cancelled");
            return future.get();
        },[epoch](const AssistantEvent& event){if(event.kind==AssistantEvent::ToolStart)postChat({"Tool: "+event.text,false,false,{},epoch});},cancel.get(),transport);
            if(*cancel)throw std::runtime_error("Assistant cancelled");postChat({result.text,true,false,result.conversation,epoch});
        }catch(const std::exception& e){postChat({e.what(),true,true,{},epoch});}
    });
}
static void startLiveBridge(){
    if(!g_liveBridge.start([](const std::string& tool,const arkjson::Value& args,LiveSessionBridge::Completion complete,std::stop_token stop){
        auto request=std::make_unique<UiToolRequest>();request->tool=tool;request->args=args;request->complete=std::move(complete);request->stop=stop;postTool(std::move(request));
    }))g_status="Live MCP unavailable: "+g_liveBridge.lastError();
}
static void stopIntegrations(){
    g_closing=true;*g_assistantCancel=true;g_liveBridge.stop();
    MSG msg{};while(PeekMessage(&msg,g_window,WM_ARK_TOOL,WM_ARK_CHAT,PM_REMOVE)){if(msg.message==WM_ARK_TOOL){std::unique_ptr<UiToolRequest> request((UiToolRequest*)msg.lParam);request->complete(cancelledTool());}else delete (UiChatEvent*)msg.lParam;}
    while(!g_toolQueue.empty()){g_toolQueue.front()->complete(cancelledTool());g_toolQueue.pop_front();}
    if(g_assistantWorker.joinable())g_assistantWorker.join();
    while(PeekMessage(&msg,g_window,WM_ARK_TOOL,WM_ARK_CHAT,PM_REMOVE)){if(msg.message==WM_ARK_TOOL){std::unique_ptr<UiToolRequest> request((UiToolRequest*)msg.lParam);request->complete(cancelledTool());}else delete (UiChatEvent*)msg.lParam;}
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
static void commandPalette(){
    HMENU menu=CreatePopupMenu();
    auto group=[&](const char* title,std::initializer_list<std::pair<int,const char*>> items){HMENU sub=CreatePopupMenu();for(auto item:items)AppendMenuA(sub,MF_STRING,item.first,item.second);AppendMenuA(menu,MF_POPUP,(UINT_PTR)sub,title);};
    group("Document",{{55,"New document"},{17,"Open ARK"},{16,"Save ARK"},{52,"Import source STEP"},{12,"Export STL"},{51,"Export straight tube STEP"},{82,"Export native STEP"}});
    group("Sketch & planes",{{60,"Create / edit plane"},{113,"Attach plane to feature"},{69,"Select plane"},{72,"Toggle datum frames"},{61,"Rectangle"},{62,"Circle / opening"},{63,"Interpolating spline"},{70,"Edit entity coordinates"},{73,"Add constraint"},{89,"Edit selected constraint"},{90,"Remove selected constraint"},{64,"Edit raw sketch JSON"},{65,"Extrude sketch"},{66,"Cut from sketch"},{67,"Named parameter"},{68,"Toggle sketch overlay"}});
    group("Solids",{{1,"Box"},{2,"Cylinder"},{3,"Sphere"},{4,"Cone"},{5,"Torus"},{6,"Polygon extrusion"},{7,"Revolve"},{11,"Linear pattern"}});
    group("Exact solids",{{100,"Native primitive"},{101,"Extrude saved sketch"},{102,"Union / cut / common"},{103,"Hole / slot / profile pocket"},{104,"Fillet / chamfer named edges"},{105,"Rigid transform"}});
    group("Modify",{{8,"Union"},{9,"Subtract bodies"},{10,"Intersect"},{25,"Delete selection"},{18,"Undo"},{19,"Redo"}});
    group("Tube & assembly",{{26,"Bend-program tube"},{40,"Laser tube"},{27,"Create assembly"},{28,"Duplicate instance"},{29,"Instance placement"},{93,"Ground / unground instance"},{94,"Add assembly mate"},{95,"Edit selected mate"},{96,"Remove selected mate"},{30,"Edit tube program"}});
    group("Native surfaces",{{76,"Loft sketches"},{77,"Fill closed sketch"},{78,"NURBS surface"},{79,"Thicken selected surface"}});
    group("Sheet metal",{{120,"Create sheet base"},{127,"Edit sheet base"},{121,"Add flange"},{122,"Edit flange"},{123,"Remove flange"},{124,"Add opening"},{125,"Edit opening"},{126,"Remove opening"},{128,"Play / pause bend preview"},{80,"Create L bend"},{81,"Import sheet STEP"},{83,"Show formed"},{84,"Show flat"}});
    group("View & agents",{{20,"Fit document"},{71,"Normal to sketch plane"},{23,"Top view"},{24,"Isometric view"},{14,"Wireframe"},{22,"Grid"},{56,"Graphics settings"},{57,"Assistant connector"},{58,"Codex / live MCP"},{21,"Raw agent command"},{31,"Help"}});
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
static void uiAction(int id){
    using V=arkjson::Value;
    if(id>=14000&&id<15000){size_t index=(size_t)(id-14000);if(index<g_doc.planes.size()){
        g_activePlane=g_doc.planes[index].name;g_activeSketch.clear();g_showDatumPlanes=true;rebuildSketchOverlay();alignSketchCamera();tTarget=resolvePlane(g_doc,g_activePlane).transformPoint({});tDist=120;g_status="Datum "+g_activePlane+" / "+g_doc.planes[index].kind;
    }return;}
    if(id==97){g_matePage=std::max(0,g_matePage-1);return;}
    if(id==98){++g_matePage;return;}
    if(id>=13000&&id<14000){int index=id-13000;if(index<(int)g_doc.mates.size())g_selectedMate=index;return;}
    if(id>=12000&&id<13000){int index=id-12000;if(auto* named=editableSketch();named&&index<(int)named->sk.cons.size()){g_selectedSketchConstraint=index;rebuildSketchOverlay();}return;}
    if(id>=11000&&id<12000){size_t index=id-11000;if(index<g_doc.sketches.size()){g_activeSketch=g_doc.sketches[index].name;g_activePlane=g_doc.sketches[index].sk.plane;g_selectedSketchConstraint=-1;g_sketchConstraintPage=0;g_showSketches=true;rebuildSketchOverlay();alignSketchCamera();}return;}
    if(id>=10000){focusSourceOpening(id-10000);return;}
    if(id>=9000){g_selectedNotch=id-9000;return;}
    if(id>=2000){selBody=id-2000;g_selectedNotch=0;g_notchScroll=0;g_sourceScroll=0;g_foldPlaying=false;return;}
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
        case 55:newDocument();break;case 56:graphicsDialog();break;case 57:connectorDialog();break;case 58:liveConnectionDialog();break;case 59:commandPalette();break;
        case 60:planeDialog();break;case 61:shapeSketchDialog(0);break;case 62:shapeSketchDialog(1);break;case 63:shapeSketchDialog(2);break;case 64:editSketchDialog();break;case 65:sketchFeatureDialog(false);break;case 66:sketchFeatureDialog(true);break;case 67:parameterDialog();break;
        case 70:editSketchEntityFields();break;case 72:g_showDatumPlanes=!g_showDatumPlanes;rebuildSketchOverlay();saveUiSettings();break;case 73:editSketchConstraintFields(true);break;
        case 89:editSketchConstraintFields(false);break;case 90:removeSketchConstraint();break;
        case 91:g_sketchConstraintPage=std::max(0,g_sketchConstraintPage-1);break;
        case 92:if(auto* named=editableSketch())g_sketchConstraintPage=std::min((int)(named->sk.cons.size()/4),g_sketchConstraintPage+1);break;
        case 100:nativePrimitiveDialog();break;case 101:nativeExtrudeDialog();break;case 102:nativeBooleanDialog();break;
        case 103:nativeHoleDialog();break;case 104:nativeEdgeBlendDialog();break;case 105:nativeTransformDialog();break;case 113:supportedDatumDialog();break;
        case 68:g_showSketches=!g_showSketches;rebuildSketchOverlay();break;
        case 69:{std::string available="XY, XZ, YZ";for(auto& plane:g_doc.planes)available+=", "+plane.name;std::vector<EditorField> fields{{"Plane name",g_activePlane}};if(editFields("Select construction plane",available.c_str(),fields)){resolvePlane(g_doc,fields[0].value);g_activePlane=fields[0].value;g_activeSketch.clear();g_showSketches=true;rebuildSketchOverlay();}}break;
        case 71:alignSketchCamera();break;
        case 74:sendAssistant();break;case 75:*g_assistantCancel=true;break;
        case 76:loftDialog();break;case 77:surfaceFillDialog();break;case 78:nurbsSurfaceDialog();break;case 79:thickenDialog();break;
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
        case 23:tYaw=0;tPitch=1.52;break;
        case 24:tYaw=.85;tPitch=.65;frameScene();break;
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

        case 31:MessageBoxA(g_window,"ARK 0.9 / Agent-Ready Kernel\nParametric CAD for people and agents.\n\nUse Commands or the tool dock to model with editable expressions.\nNative solids and surfaces use OCCT B-reps. Sheets have formed/flat poses and a read-only bend preview.\nGround an assembly instance, add a mate, and inspect remaining degrees of freedom.\nSketch diagnosis and exact reference inspection are available through the agent tools.\n\nOrbit: drag  |  Pan: Shift + drag  |  Zoom: wheel\nFit: F  |  Wireframe: W  |  Delete: Del\nOpen: Ctrl+O  |  Save: Ctrl+S  |  Undo / redo: Ctrl+Z / Y\nCommands: Ctrl+K  |  New: Ctrl+N\n\nThe native inspector reports OCCT B-rep checks. It does not certify manufacturing suitability.","About ARK / Workspace guide",MB_OK|MB_ICONINFORMATION);break;
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
    AABB all=g_sceneBounds;
    Vec3 ctr=target;
    Vec3 dw{std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};
    return ctr+dw*dist;
}
static int pickBody(int mx,int my){
    if(g_cpuMeshes.empty()||mx<viewX||my<viewY||mx>=viewX+viewW||my>=viewY+viewH)return -1;
    Vec3 eye=camEye(),forward=(target-eye).normalized(),right=forward.cross({0,1,0}).normalized(),up=right.cross(forward);
    f64 nx=(f64)(mx-viewX)/viewW*2-1,ny=1-(f64)(my-viewY)/viewH*2;
    f64 h=std::tan(deg2rad(35)*.5);
    Vec3 rd=(forward+right*(nx*h*viewW/viewH)+up*(ny*h)).normalized();
    int best=-1; f64 bt=1e300;
    for(size_t bi=0;bi<g_bodyIds.size();++bi){
        if(auto* f=g_doc.find(g_bodyIds[bi]);f&&f->type==FeatureType::SourceTube){
            auto& cache=g_sourceViews.at(f->id);auto matrices=displayedSourceTransforms(bi);double progress=(int)bi==selBody?g_foldProgress:0.;updateSourceHinges(cache,progress);
            auto cast=[&](const Mesh& mesh,const MeshBVH& bvh,const Mat4& world){auto inv=world.inverted();auto hit=bvh.raycast(mesh,{inv.transformPoint(eye),inv.transformDir(rd)});if(hit.hit&&hit.distance<bt){bt=hit.distance;best=(int)bi;}};
            for(size_t k=0;k<cache.preview.rigid.size();++k)cast(cache.preview.rigid[k],cache.rigidBvhs[k],g_instanceWorld[bi]*matrices[k]);
            for(size_t k=0;k<cache.preview.bends.size();++k)cast(progress>0?cache.hinges[k]:cache.flatHinges[k],progress>0?cache.hingeBvhs[k]:cache.flatHingeBvhs[k],g_instanceWorld[bi]*matrices[k]);
            continue;
        }
        auto cached=g_notchViews.find(g_bodyIds[bi]);
        if(cached!=g_notchViews.end()&&g_doc.find(g_bodyIds[bi])->type==FeatureType::NotchedTube){
            auto transforms=displayedNotchTransforms(bi);auto& cache=cached->second;
            for(size_t segment=0;segment<transforms.size();++segment){auto inv=(g_instanceWorld[bi]*transforms[segment]).inverted();
                auto hit=cache.bvhs[segment].raycast(cache.preview.segments[segment],{inv.transformPoint(eye),inv.transformDir(rd)});
                if(hit.hit&&hit.distance<bt){bt=hit.distance;best=(int)bi;}
            }
        }else{size_t part=g_instancePart[bi];Mat4 inv=g_instanceWorld[bi].inverted();
            auto hit=g_bvhs[part].raycast(g_cpuMeshes[part],{inv.transformPoint(eye),inv.transformDir(rd)});
            if(hit.hit&&hit.distance<bt){bt=hit.distance;best=(int)bi;}
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
static GLuint pBg,pSolid,pGrid,pShadow,pUI,pText,pSketch;
static GLint suVp,suModel,suOutline,suColor,suEye,suSel,suHard;
static GLint uuVp,uuRc,uuRad,uuFill,uuBorder,uuBW,uuGlyph;
static GLuint quadVao,gridVao,textVao,textVbo;
static int gridCount=0;
static bool g_swap=true;   // headless readback disables the buffer swap

static void drawFrame(HDC hdc){
    static auto previous=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
    double elapsed=std::min(.10,std::chrono::duration<double>(now-previous).count());advanceFold(elapsed);advanceSheet(elapsed);previous=now;
    yaw+=(tYaw-yaw)*.2; pitch+=(tPitch-pitch)*.2; dist+=(tDist-dist)*.18;
    target=target+(tTarget-target)*.2;
    AABB box=g_sceneBounds;
    layout();

    glViewport(0,0,winW,winH);
    glClearColor(.044f,.052f,.058f,1.f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);
    glDisable(GL_DEPTH_TEST);glBindVertexArray(quadVao);
    glUseProgram(pBg);
    glUniform2f(glGetUniformLocation(pBg,"res"),(float)winW,(float)winH);glUniform1f(glGetUniformLocation(pBg,"lightBackground"),g_lightBackground?1.f:0.f);
    glDrawArrays(GL_TRIANGLES,0,3);

    Vec3 eye=camEye();
    Mat4 vp=Mat4::perspective(35*K_PI/180,(f64)viewW/viewH,0.05,4000)
           *Mat4::lookAt(eye,target,{0,1,0});
    GLfloat vpT[16];for(int i=0;i<16;++i)vpT[i]=(GLfloat)vp.m[i];

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(pShadow);
    glUniformMatrix4fv(glGetUniformLocation(pShadow,"vp"),1,GL_FALSE,vpT);
    glBindVertexArray(quadVao);
    for(size_t i=0;i<g_bodyIds.size();++i){
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
    glBindVertexArray(gridVao);
    if(g_grid)glDrawArrays(GL_LINES,0,gridCount);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glUseProgram(pSolid);glUniform1f(glGetUniformLocation(pSolid,"satin"),g_satin?1.f:0.f);
    glUniformMatrix4fv(suVp,1,GL_FALSE,vpT);
    glUniform3f(suEye,(float)eye.x,(float)eye.y,(float)eye.z);
    static const GLfloat ident[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(suModel,1,GL_FALSE,ident);
    for(size_t i=0;i<g_bodyIds.size();++i){
        if(auto* f=g_doc.find(g_bodyIds[i]);f&&f->type==FeatureType::SourceTube){
            auto& cache=g_sourceViews.at(f->id);auto matrices=displayedSourceTransforms(i);double progress=(int)i==selBody?g_foldProgress:0.;updateSourceHinges(cache,progress);
            glUniform1f(suHard,0.f);glUniform1f(suOutline,0.f);glUniform1f(suSel,(int)i==selBody?1.f:0.f);glUniform3f(suColor,.59f,.605f,.63f);glPolygonMode(GL_FRONT_AND_BACK,wireframe?GL_LINE:GL_FILL);
            auto drawPart=[&](const GPUMesh& mesh,const Mat4& world){GLfloat m[16];for(int k=0;k<16;++k)m[k]=(GLfloat)world.m[k];glUniformMatrix4fv(suModel,1,GL_FALSE,m);glBindVertexArray(mesh.vao);glDrawElements(GL_TRIANGLES,mesh.count*3,GL_UNSIGNED_INT,nullptr);};
            for(size_t k=0;k<cache.preview.rigid.size();++k)drawPart(cache.rigidGpu[k],g_instanceWorld[i]*matrices[k]);
            for(size_t k=0;k<cache.preview.bends.size();++k)drawPart(progress>0?cache.hingeGpu[k]:cache.flatHingeGpu[k],g_instanceWorld[i]*matrices[k]);
            continue;
        }
        if(auto* f=g_doc.find(g_bodyIds[i]);f&&f->type==FeatureType::NotchedTube){
            auto& cache=g_notchViews.at(f->id);auto transforms=displayedNotchTransforms(i);
            glUniform1f(suHard,wireframe?0.f:1.f);glUniform1f(suOutline,0.f);glUniform1f(suSel,(int)i==selBody?1.f:0.f);
            glPolygonMode(GL_FRONT_AND_BACK,wireframe?GL_LINE:GL_FILL);
            for(size_t segment=0;segment<transforms.size();++segment){auto world=g_instanceWorld[i]*transforms[segment];GLfloat m[16];for(int k=0;k<16;++k)m[k]=(GLfloat)world.m[k];
                glUniformMatrix4fv(suModel,1,GL_FALSE,m);float c=.57f+(segment%2)*.025f;glUniform3f(suColor,c,c+.015f,c+.035f);
                glBindVertexArray(cache.gpu[segment].vao);glDrawElements(GL_TRIANGLES,cache.gpu[segment].count*3,GL_UNSIGNED_INT,nullptr);
            }continue;
        }
        auto* materialFeature=g_doc.find(g_bodyIds[i]);bool hard=materialFeature&&(materialFeature->type==FeatureType::Extrude||materialFeature->type==FeatureType::SketchCut);glUniform1f(suHard,hard&&!wireframe?1.f:0.f);GLfloat model[16];for(int k=0;k<16;++k)model[k]=(GLfloat)g_instanceWorld[i].m[k];glUniformMatrix4fv(suModel,1,GL_FALSE,model);
        auto&gm=g_meshes[g_instancePart[i]]; if(!gm.count)continue;
        float sel=i==(size_t)selBody?1.f:i==(size_t)hoverBody?.3f:0.f;
        float hue=((i*53)%97)/97.f;
        glUniform3f(suColor,.55f+.07f*hue,.565f+.07f*hue,.59f+.07f*hue);
        glUniform1f(suSel,sel);
        glUniform1f(suOutline,0.f);
        glBindVertexArray(gm.vao);
        if(false && sel>0&&!wireframe){
            glDepthMask(GL_FALSE);
            glUniform1f(suOutline,(float)(box.diagonal()*0.006+0.02));
            glDrawElements(GL_TRIANGLES,gm.count*3,GL_UNSIGNED_INT,nullptr);
            glDepthMask(GL_TRUE);
            glUniform1f(suOutline,0.f);
        }
        glPolygonMode(GL_FRONT_AND_BACK,wireframe?GL_LINE:GL_FILL);
        glDrawElements(GL_TRIANGLES,gm.count*3,GL_UNSIGNED_INT,nullptr);
    }
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);

    if((g_showSketches&&!g_sketchLines.empty())||(g_showDatumPlanes&&!g_datumLines.empty())){
        glDisable(GL_DEPTH_TEST);glUseProgram(pSketch);glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,vpT);glBindVertexArray(sketchVao);glBindBuffer(GL_ARRAY_BUFFER,sketchVbo);
        if(g_sketchGpuDirty){std::vector<float> buffers=g_sketchLines;buffers.insert(buffers.end(),g_sketchPoints.begin(),g_sketchPoints.end());buffers.insert(buffers.end(),g_constraintHighlightLines.begin(),g_constraintHighlightLines.end());buffers.insert(buffers.end(),g_datumLines.begin(),g_datumLines.end());glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(buffers.size()*sizeof(float)),buffers.data(),GL_DYNAMIC_DRAW);g_sketchGpuDirty=false;}
        if(g_showSketches){glUniform4f(glGetUniformLocation(pSketch,"tint"),.62,.77,.98,.85);glDrawArrays(GL_LINES,0,(GLsizei)g_sketchLines.size()/3);
            if(!g_sketchPoints.empty()){glPointSize(6);glDrawArrays(GL_POINTS,(GLint)g_sketchLines.size()/3,(GLsizei)g_sketchPoints.size()/3);glPointSize(1);}
            glUniform4f(glGetUniformLocation(pSketch,"tint"),1.,.66,.22,1.);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size())/3,(GLsizei)g_constraintHighlightLines.size()/3);}
        if(g_showDatumPlanes){glUniform4f(glGetUniformLocation(pSketch,"tint"),.55,.77,.68,.48);glDrawArrays(GL_LINES,(GLint)(g_sketchLines.size()+g_sketchPoints.size()+g_constraintHighlightLines.size())/3,(GLsizei)g_datumLines.size()/3);}
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
    std::string filename=g_path.substr(g_path.find_last_of("/\\")+1);if(g_session.revision!=g_savedRevision)filename+=" *";
    size_t maxFilename=(size_t)std::max(16.f,(W-760)/9);if(filename.size()>maxFilename)filename=filename.substr(0,maxFilename-3)+"...";
    textOut(filename,viewX+24,18,.86f);label("LOCAL DOCUMENT  /  REV "+std::to_string(g_session.revision),viewX+24,45,.54f);
    button("Undo",W-352,20,64,18);button("Redo",W-280,20,64,19);button("Save",W-208,20,76,16,true);button("Commands",W-124,20,106,59);
    rect(viewX+14,150,100,348,12,.10,.11,.13,.98,.31,.34,.39,.75,1);
    button("Plane",viewX+23,161,82,60);button("Rectangle",viewX+23,207,82,61);button("Spline",viewX+23,253,82,63);
    button("Extrude",viewX+23,299,82,65);button("Cut sketch",viewX+23,345,82,66);button("All tools",viewX+23,391,82,59);button("Graphics",viewX+23,437,82,56);
    label("DOCUMENT",20,101,.66f);label(std::to_string(g_doc.features.size()),221,101,.66f);
    textOut("Model explorer",20,132,1.02f);label(std::to_string(g_doc.features.size())+" features / "+std::to_string(g_bodyIds.size())+" instances",20,162,.68f);
    float treeY=200;
    size_t rows=g_doc.assembly.empty()?g_doc.features.size():g_doc.assembly.size();
    for(size_t i=(size_t)g_treeScroll;i<rows&&treeY<H-385;i++){
        std::string name,subtitle;int body=-1,depth=0;bool group=false;
        if(g_doc.assembly.empty()){
            auto& f=g_doc.features[i];name=f.name;subtitle=std::string(featureTypeName(f.type))+" #"+std::to_string(f.id);
            for(size_t j=0;j<g_bodyIds.size();++j)if(g_bodyIds[j]==f.id)body=(int)j;
        }else{
            auto& node=g_doc.assembly[i];name=node.name;group=node.feature==0;subtitle=group?"Subassembly":"Part #"+std::to_string(node.feature)+" / node "+std::to_string(node.id);
            u64 parent=node.parent;while(parent&&depth<5){++depth;auto it=std::find_if(g_doc.assembly.begin(),g_doc.assembly.end(),[&](const AssemblyNode& n){return n.id==parent;});if(it==g_doc.assembly.end())break;parent=it->parent;}
            for(size_t j=0;j<g_instanceIds.size();++j)if(g_instanceIds[j]==node.id)body=(int)j;
        }
        bool selected=body>=0&&selBody==body;
        if(selected)rect(12,treeY-7,234,53,7,.175,.19,.22,1,.39,.43,.50,.7,1);
        textOut(group?"+":body>=0?"<>":"-",20+depth*10,treeY+2,.8f,.75,.79,.86);
        textOut(name.substr(0,22-depth),48+depth*10,treeY,.78f);label(subtitle,48+depth*10,treeY+23,.60f);
        if(body>=0)g_zones.push_back({12,treeY-7,234,53,2000+body});treeY+=62;
    }
    for(size_t k=0;k<g_doc.sketches.size()&&treeY<H-385;++k){auto& sketch=g_doc.sketches[k];
        if(sketch.name==g_activeSketch&&g_showSketches)rect(12,treeY-7,234,48,7,.16,.19,.25,1,.34,.40,.52,.8,1);
        textOut("~",20,treeY,.8f,.67,.76,.92);textOut(sketch.name.substr(0,21),48,treeY,.78f);label("Sketch / "+sketch.sk.plane,48,treeY+22,.60f);g_zones.push_back({12,treeY-7,234,48,11000+(int)k});treeY+=55;
    }
    for(size_t k=0;k<g_doc.planes.size()&&treeY<H-385;++k){auto& datum=g_doc.planes[k];
        if(datum.name==g_activePlane&&g_showDatumPlanes)rect(12,treeY-7,234,42,7,.14,.20,.18,1,.29,.51,.42,.8,1);
        textOut("+",20,treeY,.8f,.58,.80,.69);textOut(datum.name.substr(0,21),48,treeY,.76f);
        label("Datum / "+datum.kind,48,treeY+19,.58f);g_zones.push_back({12,treeY-7,234,42,14000+(int)k});treeY+=48;
    }
    float ly=H-340;rect(20,ly,218,1,0,.17,.21,.22,1,0,0,0,0,0);label("PARAMETERS",20,ly+23,.68f);label("Click to edit",152,ly+23,.65f);
    for(size_t i=0;i<g_doc.params.size()&&i<4;++i){auto& p=g_doc.params[i];float y=ly+58+i*48;
        textOut(p.name.substr(0,15),22,y+5,.79f);rect(127,y-1,108,31,5,.12,.13,.15,1,.29,.31,.35,.7,1);
        textOut(p.expr.substr(0,12),139,y+5,.78f,.83,.85,.89);g_zones.push_back({15,y-2,224,34,(int)i+1000});
    }
    label("PARAMETRIC WORKSPACE",20,H-66,.62f);
    textOut("Perspective",viewX+24,96,.85f);label("SATIN STEEL  /  mm",viewX+24,121,.60f);
    button("Fit",W-486,92,55,20);button(g_showSketches?"Plane":"Top",W-425,92,49,g_showSketches?71:23);button("Iso",W-370,92,49,24);
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
    auto* laser=selectedNotchedTube();auto* source=selectedSourceTube();if(activeSketch){laser=nullptr;source=nullptr;}
    const bool assemblySelected=selBody>=0&&g_instanceIds[selBody]&&!activeSketch;
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
                    metric("Panels",panels&&panels->t==arkjson::Value::Arr?std::to_string(panels->arr.size()):"n/a",553);
                    if(H>940)metric("Bends",bends&&bends->t==arkjson::Value::Arr?std::to_string(bends->arr.size()):"n/a",584);}
                if(H>940){if(sheet->type==FeatureType::SheetMetal){char preview[70];std::snprintf(preview,sizeof preview,"Display-only bend preview %.0f%%",sheetDisplayProgress(sheet->id)*100);label(preview,rx,675,.62f);}else label(g_sheetFlatViews[sheet->id]?"Displaying flat mesh":"Displaying formed mesh",rx,675,.62f);}
            }else label("Exact native shape / tessellated display",rx,492,.61f);
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
            g_matePage=std::min(g_matePage,std::max(0,((int)related.size()-1)/4));
            int visible=0;
            for(size_t k=(size_t)g_matePage*4;k<related.size()&&visible<4;++k){
                const size_t i=related[k];const auto& mate=g_doc.mates[i];
                const float y=548.f+visible*42;const bool picked=(int)i==g_selectedMate;
                if(picked)rect(rx-3,y-5,258,38,5,.22,.20,.17,1,.77,.53,.25,.8,1);
                textOut(("#"+std::to_string(mate.id)+" "+mate.type).substr(0,27),rx+4,y,.72f);
                std::string detail=std::to_string(mate.a)+" : "+mate.reference_a+"  /  "+std::to_string(mate.b)+" : "+mate.reference_b;
                label(detail.substr(0,37),rx+4,y+20,.58f);
                g_zones.push_back({rx-3,y-5,258,38,13000+(int)i});++visible;
            }
            if(!visible)label("No mates on this instance",rx+4,553,.65f);
            if(related.size()>4){
                button("<",rx,721,32,97);button(">",rx+37,721,32,98);
                label(std::to_string(g_matePage+1)+" / "+std::to_string((related.size()+3)/4),rx+84,729,.58f);
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
            button("Export native STEP",rx,H-126,252,82,true);button("Delete selected",rx,H-78,252,25);
        }else{
            button("Formed",rx,H-210,121,83,false,!g_sheetFlatViews[sheet->id]);button("Flat",rx+131,H-210,121,84,false,g_sheetFlatViews[sheet->id]);
            button("Export native STEP",rx,H-162,252,82,true);
            button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
        }
    }else if(nativeReport&&!laser&&!source){
        if(auto* selectedFeature=selBody>=0?g_doc.find(g_bodyIds[(size_t)selBody]):nullptr;selectedFeature&&selectedFeature->type==FeatureType::NativeSolid){
            button("Hole / pocket",rx,H-246,121,103);button("Fillet / edge",rx+131,H-246,121,104);
            button("Datum face",rx,H-204,121,113);button("Exact Boolean",rx+131,H-204,121,102);
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
        button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
        button(selBody>=0&&!g_instanceIds[selBody]&&g_tubeInfo.count(g_bodyIds[selBody])?"Bend program":"Placement",rx,H-162,121,29);button("Duplicate",rx+131,H-162,121,28);
        button("Delete selected",rx,H-78,252,25);
    }
    float cy=H-244;panel(viewX,cy,(float)viewW,216);
    if(laser||source){float py=cy-46;rect(viewX+126,py-5,viewW-142,43,7,.10,.11,.13,.96,.25,.27,.31,.8,1);
        float x=viewX+136;button("Straight",x,py,76,45,false,g_foldProgress==0);button("Folded",x+84,py,76,46,false,g_foldProgress==1);button(g_foldPlaying?"Pause":"Play",x+168,py,62,47,true);
        float length=std::max(80.f,(float)viewW-408);rect(x+244,py+14,length,5,2,.23,.26,.31,1,0,0,0,0,0);rect(x+244,py+14,std::max(3.f,length*(float)g_foldProgress),5,2,.72,.78,.88,1,0,0,0,0,0);g_zones.push_back({x+244,py+2,length,29,50});}
    else if(sheet&&sheet->type==FeatureType::SheetMetal&&nativeBody&&nativeBody->nativeData.find("animation")){
        float py=cy-46;rect(viewX+126,py-5,viewW-142,43,7,.10,.11,.13,.96,.25,.27,.31,.8,1);
        float x=viewX+136;double progress=sheetDisplayProgress(sheet->id);
        button("Flat",x,py,76,84,false,progress<=0);button("Formed",x+84,py,76,83,false,progress>=1);button(g_sheetPlayingId==sheet->id?"Pause":"Play",x+168,py,62,128,true);
        float length=std::max(80.f,(float)viewW-408);rect(x+244,py+14,length,5,2,.23,.26,.31,1,0,0,0,0,0);rect(x+244,py+14,std::max(3.f,length*(float)progress),5,2,.72,.78,.88,1,0,0,0,0,0);g_zones.push_back({x+244,py+2,length,29,53});}
    label("ASSISTANT",viewX+22,cy+15,.65f);
    std::string connector=g_chatBusy?"Working...":g_assistantConfig.model.empty()?"Connector not configured":"Configured: "+g_assistantConfig.model;
    label(connector.substr(0,43),viewX+132,cy+15,.60f);button("Connect",viewX+viewW-98,cy+7,76,57);
    std::vector<std::string> chatLines;size_t cols=(size_t)std::max(30,(viewW-55)/7);
    for(auto& entry:g_chatLog){std::string line=entry.first+": "+entry.second;for(size_t i=0;i<line.size();i+=cols)chatLines.push_back(line.substr(i,cols));}
    if(chatLines.empty())chatLines.push_back("Describe a part, ask for an inspection, or connect Codex to this document.");
    int last=std::max(0,(int)chatLines.size()-g_chatScroll),first=std::max(0,last-3);float chatY=cy+49;
    for(int i=first;i<last;++i){textOut(chatLines[i],viewX+22,chatY,.66f,.73,.77,.84);chatY+=22;}
    rect(viewX+22,cy+124,viewW-244,62,7,.055,.062,.078,1,.30,.35,.43,.8,1);
    label("Message the agent...  Ctrl+Enter to send",viewX+34,cy+142,.64f);
    button(g_chatBusy?"Running":"Send",viewX+viewW-206,cy+124,84,74,true);button("Cancel",viewX+viewW-112,cy+124,90,75);
    button("Codex MCP",viewX+viewW-206,cy+164,184,58);
    textOut(g_status.substr(0,(size_t)std::max((viewW-248)/7,1)),viewX+22,cy+193,.58f,g_error?.94f:.58f,g_error?.57f:.65f,g_error?.48f:.74f);
    if(g_chatInput){MoveWindow(g_chatInput,viewX+30,(int)cy+132,viewW-260,44,TRUE);MoveWindow(g_chatHistory,viewX+22,(int)cy+43,viewW-44,68,TRUE);}
    panel(0,H-28,W,28);
    textOut("o",17,H-22,.65f,.73,.79,.87);label("ENGINE READY",34,H-21,.61f);
    char st[180];std::snprintf(st,sizeof st,"%zu triangles   /   %.2f ms recompute   /   %.0f fps",g_triangleCount,g_doc.recomputeMs,g_fps);
    label(st,viewX+16,H-21,.61f);label("Orbit: drag   Pan: Shift+drag   Zoom: scroll",W-298,H-21,.58f);

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

// ---------------------------------------------------------------------------
static LRESULT CALLBACK wndProc(HWND hw,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
        case WM_ARK_TOOL:g_toolQueue.emplace_back((UiToolRequest*)lp);return 0;
        case WM_ARK_CHAT:{std::unique_ptr<UiChatEvent> event((UiChatEvent*)lp);
            if(event->final){g_chatBusy=false;if(event->epoch==g_documentEpoch&&!event->error&&!*g_assistantCancel)g_conversation=event->history;else g_conversation=arkjson::Value::makeArr();}
            if(event->epoch==g_documentEpoch){g_chatLog.push_back({event->error?"Error":event->final?"ARK":"Action",event->text.substr(0,16000)});if(g_chatLog.size()>200)g_chatLog.erase(g_chatLog.begin());g_chatScroll=0;refreshChatHistory();}return 0;
        }
        case WM_COMMAND:if(LOWORD(wp)==500){interactiveAction(74);return 0;}break;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,RGB(219,226,238));SetBkColor((HDC)wp,RGB(14,17,23));return (LRESULT)fieldBrush;
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
            if(p.x<viewX){g_treeScroll=std::max(0,std::min((int)(g_doc.assembly.empty()?g_doc.features.size():g_doc.assembly.size())-1,g_treeScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1)));return 0;}}
            tDist=clamp(tDist*(GET_WHEEL_DELTA_WPARAM(wp)>0?0.87:1.15),2.,4000.);
            return 0;
        case WM_LBUTTONDOWN:{
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            g_pressWasButton=p.x<viewX||p.x>viewX+viewW||p.y<viewY||p.y>viewY+viewH;
            for(auto&z:g_zones)
                if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&(float)p.y>=z.y&&(float)p.y<=z.y+z.h)
                    { g_pressWasButton=true;if(z.id==50){g_foldScrubbing=true;g_foldPlaying=false;g_foldProgress=clamp((double)(p.x-z.x)/z.w,0.,1.);}
                        else if(z.id==53){g_sheetScrubbing=true;g_sheetPlayingId=0;if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}} break; }
            g_dragged=false;SetCapture(hw);
            return 0;
        }
        case WM_MOUSEMOVE:{
            static int px=0,pyy=0; static bool has=false;
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            bool lbtn=wp&MK_LBUTTON,mbtn=wp&MK_MBUTTON,shift=wp&MK_SHIFT;
            if(g_foldScrubbing&&lbtn)for(auto& zone:g_zones)if(zone.id==50){g_foldProgress=clamp((double)(x-zone.x)/zone.w,0.,1.);break;}
            if(g_sheetScrubbing&&lbtn)for(auto& zone:g_zones)if(zone.id==53){if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(x-zone.x)/zone.w);}catch(const std::exception& error){g_error=true;g_status=error.what();g_sheetScrubbing=false;}break;}
            if(has&&(lbtn||mbtn)){
                int dx=x-px,dy=y-pyy;if(std::abs(dx)+std::abs(dy)>2)g_dragged=true;
                if((mbtn||(lbtn&&shift))&&!g_pressWasButton){
                    f64 s=tDist*0.0016;
                    Vec3 rv{std::cos(yaw),0,-std::sin(yaw)};
                    tTarget=tTarget-rv*((f64)dx*s);
                    tTarget.y+=(f64)dy*s;
                } else if(lbtn&&!shift&&!g_pressWasButton){
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
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(g_pressWasButton){
                for(auto&z:g_zones)
                    if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&
                       (float)p.y>=z.y&&(float)p.y<=z.y+z.h){ if(z.id==50){g_foldProgress=clamp((double)(p.x-z.x)/z.w,0.,1.);g_foldPlaying=false;}
                           else if(z.id==53){g_sheetPlayingId=0;if(auto* sheet=selectedSheet())try{sheetPreviewSet(sheet->id,(double)(p.x-z.x)/z.w);}catch(const std::exception& error){g_error=true;g_status=error.what();}}
                           else interactiveAction(z.id); break; }
            } else if(!g_dragged) {
                int b=pickBody(p.x,p.y);
                selBody=(b==selBody)?-1:b;g_selectedNotch=0;g_notchScroll=0;
            }
            g_pressWasButton=false;g_foldScrubbing=false;g_sheetScrubbing=false;
            return 0;
        }
        case WM_KEYDOWN:
            if(GetKeyState(VK_CONTROL)&0x8000){if(wp=='S')uiAction(16);else if(wp=='O')uiAction(17);else if(wp=='Z')uiAction(18);else if(wp=='Y')uiAction(19);else if(wp=='K')interactiveAction(59);else if(wp=='N')interactiveAction(55);}
            else if(wp=='F')uiAction(20);else if(wp=='W')uiAction(14);else if(wp==VK_DELETE)uiAction(25);else if(wp==VK_F1)uiAction(31);
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
    bool headless=false,selfTest=false,assemblyTest=false,dialogTest=false,bendTest=false,bendExportTest=false,sourceTest=false,sketchTest=false,assistantTest=false,nativeUiTest=false;unsigned exitAfterMs=0;std::string endpointFile,requestedSketch;int requestedOpening=-1;double requestedFold=-1;std::string shotPath="ark_studio.png",loadPath,scriptPath,importPath;
    int sw=1600,sh=1000,argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    auto utf8=[](const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;};
    for(int i=1;i<argc;++i){auto a=utf8(argv[i]);if(a=="--shot"&&i+1<argc){headless=true;shotPath=utf8(argv[++i]);}
        else if(a=="--self-test-ui")selfTest=true;
        else if(a=="--self-test-sketch-ui")sketchTest=true;
        else if(a=="--self-test-assistant-ui")assistantTest=true;
        else if(a=="--self-test-native-ui")nativeUiTest=true;
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
    LocalFree(argv);if(!headless||!g_settingsOverride.empty())loadUiSettings();winW=std::max(sw,1180);winH=std::max(sh,800);layout();

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
    using SwapInterval=BOOL (WINAPI*)(int);
    auto swapInterval=(SwapInterval)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if(swapInterval)swapInterval(headless?0:1);
    ShowWindow(hwnd,headless?SW_HIDE:SW_SHOW);

    // ---- resources ----
    pBg=mkProg(VS_BG,FS_BG);
    pSolid=mkProg(VS_SOLID,FS_SOLID);
    pGrid=mkProg(VS_LINE,FS_LINE);
    pShadow=mkProg(VS_SHADOW,FS_SHADOW);
    pUI=mkProg(VS_UI,FS_UI);
    pText=mkProg(VS_TEXT,FS_TEXT);
    pSketch=mkProg(VS_LINE,"#version 330 core\nout vec4 o;uniform vec4 tint;void main(){o=tint;}");
    glGenVertexArrays(1,&sketchVao);glGenBuffers(1,&sketchVbo);glBindVertexArray(sketchVao);glBindBuffer(GL_ARRAY_BUFFER,sketchVbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
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

    seedDocument();
    if(!loadPath.empty()){arkjson::Value a=arkjson::Value::makeObj();a.set("path",arkjson::Value::makeStr(loadPath));runTool("doc_load",a);g_path=loadPath;if(!g_error){g_savedRevision=g_session.revision;g_foldProgress=selectedSourceTube()?1.:0.;}frameScene();}
    if(!scriptPath.empty()){
        std::ifstream input(scriptPath);std::string line;
        if(!input)return 2;
        while(std::getline(input,line)){arkjson::Value c;if(!arkjson::parse(line,c)||!c.find("tool")||!c.find("arguments"))return 2;
            runTool(c.find("tool")->asStr(),*c.find("arguments"));if(g_error)return 3;}
        frameScene();
    }
    if(!importPath.empty()){importSourceStep(importPath);if(g_error)return 80;}
    if(sketchTest){
        using V=arkjson::Value;g_savedRevision=g_session.revision;uiAction(55);if(g_error||!g_doc.features.empty())return 100;
        g_dialogSmoke=3;g_dialogShot=shotPath+".plane.png";uiAction(60);if(g_error||g_doc.planes.size()!=1)return 101;
        uiAction(67);if(g_error||g_doc.params.empty())return 102;
        g_dialogOverrides={"Profile","Workplane","0","0","width","60"};g_dialogShot=shotPath+".rectangle.png";uiAction(61);if(g_error||!g_doc.findSketch("Profile"))return 103;
        g_dialogShot=shotPath+".extrude.png";uiAction(65);if(g_error||g_bodyIds.empty())return 104;
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

        auto pump=[&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(g_chatBusy&&std::chrono::steady_clock::now()<deadline){MSG event{};while(PeekMessage(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessage(&event);}drainUiTools();Sleep(2);}return !g_chatBusy;};
        sendAssistant("Offline integration test: new document, then create a box.",transport);if(!pump()||g_doc.getParam("agent_ui_probe")!=17||g_conversation.arr.empty()||g_doc.features.size()!=1||g_doc.features[0].type!=FeatureType::Box||g_path!="Untitled.ark")return 120;
        auto gate=std::make_shared<std::atomic_bool>(false);auto staleTransport=[gate,parse](const auto&,const auto&,const auto*){while(!*gate)std::this_thread::sleep_for(std::chrono::milliseconds(1));return parse(R"({"status":"completed","output":[{"type":"function_call","call_id":"stale_ui_test","name":"doc_apply","arguments":"{\"commands\":[{\"op\":\"param.set\",\"name\":\"stale_ui_probe\",\"expr\":\"99\"}]}"}]})");};
        sendAssistant("Offline stale-write test.",staleTransport);V command=parse(R"({"op":"param.set","name":"human_ui_probe","expr":"1"})");applyCommand(command);*gate=true;
        if(!pump()||!g_conversation.arr.empty()||std::any_of(g_doc.params.begin(),g_doc.params.end(),[](auto& p){return p.name=="stale_ui_probe";}))return 121;
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
        uiAction(23);if(tPitch!=1.52)return 16;uiAction(24);
        g_status="UI checks passed: create / undo / redo / delete / wire / grid / views";
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

    if(headless){
        g_swap=false;
        drawFrame(hdc);
        glFinish();
        if(glGetError()!=GL_NO_ERROR)return 20;
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

    if(!headless||!endpointFile.empty()){startLiveBridge();if(!endpointFile.empty()){std::ofstream endpoint(endpointFile);endpoint<<g_liveBridge.endpoint();}}
    if(!headless){g_chatInput=CreateWindowExA(0,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,viewX+30,winH-112,viewW-260,44,hwnd,(HMENU)499,hi,nullptr);
        HFONT chatFont=CreateFontA(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");SendMessage(g_chatInput,WM_SETFONT,(WPARAM)chatFont,TRUE);SendMessage(g_chatInput,EM_SETLIMITTEXT,12000,0);
        g_chatHistory=CreateWindowExA(0,"EDIT","",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,viewX+22,winH-201,viewW-44,68,hwnd,(HMENU)498,hi,nullptr);SendMessage(g_chatHistory,WM_SETFONT,(WPARAM)chatFont,TRUE);refreshChatHistory();
    }
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
