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
#define NOMINMAX
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
#include <set>
#include <fstream>
#include <sstream>
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
static std::vector<size_t> g_instancePart;
static AABB g_sceneBounds;
static std::vector<MeshReport> g_reports;
static std::unordered_map<u64,arkjson::Value> g_tubeInfo;
static std::vector<MeshBVH> g_bvhs;
static size_t g_triangleCount=0;
static std::string g_status="Ready. Local C++ kernel.",g_path="Harmonic ring.ark";
static bool g_error=false,g_grid=true,g_dragged=false;
static u64 g_savedRevision=0;
static int g_treeScroll=0;
static int viewX=258,viewY=140,viewW=1024,viewH=650;
static HWND g_window=nullptr;


static std::vector<Zone>    g_zones;
static std::vector<GPUMesh> g_meshes;
static std::vector<Mesh>    g_cpuMeshes;

static f64 yaw=0.85,pitch=0.46,dist=140,tYaw=yaw,tPitch=pitch,tDist=dist;
static Vec3 target{30,12,0},tTarget=target;
static int winW=1600,winH=1000;
static void layout(){viewW=std::max(winW-558,1);viewH=std::max(winH-318,1);}
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
out vec4 o;uniform vec2 res;
void main(){
 vec2 uv=gl_FragCoord.xy/res;
 vec3 top=vec3(.19,.20,.22),bot=vec3(.075,.080,.089);
 vec3 c=mix(bot,top,uv.y);
 vec2 q=uv-.5;c*=1.-dot(q,q)*.55;
 c+=vec3(.045)*exp(-length(q-vec2(-.25,.25))*2.4);
 o=vec4(c,1.);
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
uniform vec3 color,eye; uniform float sel,hardSurface;
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
 base+=vec3(.90,.93,.98)*softbox*.36+vec3(.80,.83,.87)*strip*.26;
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
static void rebuildScene(bool evaluate=false){
    if(evaluate)g_doc.recompute();
    g_foldPlaying=false;
    for(auto it=g_notchViews.begin();it!=g_notchViews.end();){auto* f=g_doc.find(it->first);if(!f||f->type!=FeatureType::NotchedTube){for(auto& mesh:it->second.gpu)releaseMesh(mesh);it=g_notchViews.erase(it);}else ++it;}
    for(auto it=g_sourceViews.begin();it!=g_sourceViews.end();){auto* f=g_doc.find(it->first);if(!f||f->type!=FeatureType::SourceTube){for(auto* list:{&it->second.rigidGpu,&it->second.flatHingeGpu,&it->second.hingeGpu})for(auto& mesh:*list)releaseMesh(mesh);it=g_sourceViews.erase(it);}else ++it;}
    std::vector<AssemblyPlacement> placements;std::string error;
    if(g_doc.assembly.empty()){
        std::set<u64> consumed;for(auto& f:g_doc.features)for(auto id:f.inputs)consumed.insert(id);
        for(auto& b:g_doc.evalBodies)if(!consumed.count(b.id)){auto* f=g_doc.find(b.id);placements.push_back({0,b.id,f?f->name:"Body",Mat4{},true});}
    }else if(!resolveAssembly(g_doc,placements,error)){g_error=true;g_status=error;}
    g_cpuMeshes.clear();g_bodyIds.clear();g_instanceIds.clear();g_instanceNames.clear();g_instanceWorld.clear();g_instancePart.clear();
    g_reports.clear();g_bvhs.clear();g_tubeInfo.clear();g_triangleCount=0;g_sceneBounds=AABB{};
    for(auto& gm:g_meshes)gm.count=0;
    std::unordered_map<u64,size_t> parts;
    for(auto& placement:placements){
        if(!placement.visible)continue;
        auto part=parts.find(placement.feature);
        if(part==parts.end()){
            auto body=std::find_if(g_doc.evalBodies.begin(),g_doc.evalBodies.end(),[&](const Body& b){return b.id==placement.feature;});if(body==g_doc.evalBodies.end())continue;
            size_t i=g_cpuMeshes.size();g_cpuMeshes.push_back(body->tessellate(g_doc.tessOpt));
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
    if(auto* f=selectedNotchedTube())g_selectedNotch=std::max(0,std::min(g_selectedNotch,(int)f->notches.size()-1));
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
    if(b.valid()){tTarget=b.center();tDist=std::max(b.diagonal()*1.65,2.);}
}
static arkjson::Value runTool(const std::string& tool,const arkjson::Value& args){
    auto result=g_session.call(tool,args);g_error=result.find("isError")->b;
    if(g_error)g_status=result.find("structuredContent")->find("error")->asStr();
    else g_status=tool+" completed  /  revision "+std::to_string(g_session.revision);
    if(!g_error&&(tool=="doc_apply"||tool=="doc_load"||tool=="doc_undo"||tool=="doc_redo"||tool=="import_step"))rebuildScene();
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
static int g_dialogSmoke=0;static std::string g_dialogShot,g_dialogReplacement="32";
static HBRUSH dialogBrush=CreateSolidBrush(RGB(29,31,36)),fieldBrush=CreateSolidBrush(RGB(42,45,52));
static LRESULT CALLBACK editorProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND&&(LOWORD(w)==1||LOWORD(w)==2)){g_dialogAccept=LOWORD(w)==1;g_dialogDone=true;return 0;}
    if(m==WM_CLOSE){g_dialogDone=true;return 0;}
    if(m==WM_TIMER&&w==901){
        KillTimer(h,901);
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
struct EditorField { std::string name,value; };
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
        HWND e=control("EDIT",fields[i].value.c_str(),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|(multiline?ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN:0),multiline?26:235,y,multiline?width-52:width-263,multiline?height-184:33,3+(int)i,mono);
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
    if(g_dialogAccept)for(size_t i=0;i<fields.size();++i){int n=GetWindowTextLengthA(edits[i]);std::vector<char> b(n+1);GetWindowTextA(edits[i],b.data(),n+1);fields[i].value=b.data();}
    EnableWindow(g_window,TRUE);DestroyWindow(g_dialog);DeleteObject(font);DeleteObject(mono);SetForegroundWindow(g_window);return g_dialogAccept;
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
    if(!g_error&&!mesh){g_path=path;g_savedRevision=g_session.revision;if(!save){g_foldProgress=selectedSourceTube()?1.:0.;frameScene();}}
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
        selBody=(int)g_bodyIds.size()-1;g_foldProgress=1;g_sourceScroll=0;
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

        case 31:MessageBoxA(g_window,"ARK Geometry Studio\n\nCreate primitives from the ribbon with editable expressions.\nSelect an instance to edit its placement or duplicate it.\nSelect a tube to inspect or edit its bend program.\nLaser tube: stock, bend points, preview and straight STEP.\nNumbered hinge markers select bend points in the viewport.\n\nOrbit: drag  |  Pan: Shift + drag  |  Zoom: wheel\nFit: F  |  Wireframe: W  |  Delete: Del\nOpen: Ctrl+O  |  Save: Ctrl+S  |  Undo / redo: Ctrl+Z / Y\nAgent command: Ctrl+K\n\nThe topology audit reports mesh checks only; it does not certify manufacturing suitability.","About ARK / Workspace guide",MB_OK|MB_ICONINFORMATION);break;
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
static GLuint pBg,pSolid,pGrid,pShadow,pUI,pText;
static GLint suVp,suModel,suOutline,suColor,suEye,suSel,suHard;
static GLint uuVp,uuRc,uuRad,uuFill,uuBorder,uuBW,uuGlyph;
static GLuint quadVao,gridVao,textVao,textVbo;
static int gridCount=0;
static bool g_swap=true;   // headless readback disables the buffer swap

static void drawFrame(HDC hdc){
    static auto previous=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
    advanceFold(std::min(.10,std::chrono::duration<double>(now-previous).count()));previous=now;
    yaw+=(tYaw-yaw)*.2; pitch+=(tPitch-pitch)*.2; dist+=(tDist-dist)*.18;
    target=target+(tTarget-target)*.2;
    AABB box=g_sceneBounds;
    layout();

    glViewport(0,0,winW,winH);
    glClearColor(.044f,.052f,.058f,1.f);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);
    glDisable(GL_DEPTH_TEST);glBindVertexArray(quadVao);
    glUseProgram(pBg);
    glUniform2f(glGetUniformLocation(pBg,"res"),(float)winW,(float)winH);
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
    glUseProgram(pSolid);
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
        glUniform1f(suHard,0.f);GLfloat model[16];for(int k=0;k<16;++k)model[k]=(GLfloat)g_instanceWorld[i].m[k];glUniformMatrix4fv(suModel,1,GL_FALSE,model);
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
    panel(0,0,W,58);panel(0,58,W,82);panel(0,140,258,H-168);panel(W-300,140,300,H-168);
    rect(17,12,35,35,8,.16,.17,.19,1,.39,.42,.47,.55,1);glyph(99,21,16,27,.95f);
    textOut("ARK",64,14,1.25f);label("GEOMETRY STUDIO",64,37,.48f);
    std::string filename=g_path.substr(g_path.find_last_of("/\\")+1);
    if(g_session.revision!=g_savedRevision)filename+=" *";
    size_t maxFilename=(size_t)std::max(16.f,(W-720)/9);
    if(filename.size()>maxFilename)filename=filename.substr(0,maxFilename-3)+"...";
    textOut(filename,viewX+24,20,.86f);
    button("Open",W-374,13,70,17);button("Undo",W-294,13,62,18);button("Redo",W-222,13,62,19);button("Save",W-150,13,70,16,true);button("?",W-70,13,48,31);
    const char* titles[]={"Box","Cylinder","Sphere","Torus","Extrude","Revolve","Pattern","Union","Cut","Intersect","Tube","Laser tube","Assembly","Import STEP","Export"};
    int ids[]={1,2,3,5,6,7,11,8,9,10,26,40,27,52,12};
    float tileW=std::min(101.f,(W-44)/15.f),tileX=22;
    for(int k=0;k<15;++k){float x=tileX+k*tileW,w=tileW-7;bool over=mp.x>=x&&mp.x<x+w&&mp.y>=65&&mp.y<131;
        if(over){g_hoverZone=ids[k];rect(x,65,w,66,6,.19,.20,.22,1,.39,.41,.46,.65,1);}
        glyph(ids[k]==40?26:ids[k]==52?17:ids[k],x+(w-34)/2,68,34,k==10?.96f:.83f);
        textOut(titles[k],x+(w-textWidth(titles[k],.69f))/2,109,.69f,.80,.82,.86);
        g_zones.push_back({x,65,w,66,ids[k]});
        if(k==3||k==6||k==9||k==11)rect(x+w+3,77,1,43,0,.25,.26,.29,.6,0,0,0,0,0);
    }
    label("DOCUMENT",20,157,.66f);label(std::to_string(g_doc.features.size()),221,157,.66f);
    textOut("Model explorer",20,185,1.02f);label(std::to_string(g_doc.features.size())+" features / "+std::to_string(g_bodyIds.size())+" instances",20,215,.68f);
    float treeY=254;
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
    float ly=H-340;rect(20,ly,218,1,0,.17,.21,.22,1,0,0,0,0,0);label("PARAMETERS",20,ly+23,.68f);label("Click to edit",152,ly+23,.65f);
    for(size_t i=0;i<g_doc.params.size()&&i<4;++i){auto& p=g_doc.params[i];float y=ly+58+i*48;
        textOut(p.name.substr(0,15),22,y+5,.79f);rect(127,y-1,108,31,5,.12,.13,.15,1,.29,.31,.35,.7,1);
        textOut(p.expr.substr(0,12),139,y+5,.78f,.83,.85,.89);g_zones.push_back({15,y-2,224,34,(int)i+1000});
    }
    label("PARAMETRIC WORKSPACE",20,H-66,.62f);
    textOut("Perspective",viewX+24,162,.85f);label("SATIN STEEL  /  mm",viewX+24,187,.60f);
    button("Fit",W-486,157,55,20);button("Top",W-425,157,49,23);button("Iso",W-370,157,49,24);
    float rx=W-278;label("INSPECTOR",rx,157,.66f);label("MESH",W-66,157,.62f);
    std::string selected="No selection",kind="No body";if(selBody>=0){auto* f=g_doc.find(g_bodyIds[selBody]);if(f){selected=g_instanceNames[selBody];kind=f->type==FeatureType::ParametricSurface?"EXPRESSION SURFACE / MESH":std::string(featureTypeName(f->type))+" / TESSELLATED MESH";if(g_instanceIds[selBody])kind="INSTANCE #"+std::to_string(g_instanceIds[selBody])+" / "+featureTypeName(f->type);}}
    if(selectedNotchedTube())kind="LASER TUBE / IDEAL HINGE PREVIEW";
    if(selectedSourceTube())kind="STEP SOURCE / FINITE BENDS";
    textOut(selected.substr(0,25),rx,189,1.02f);
    rect(rx,229,254,28,5,.15,.16,.19,1,0,0,0,0,0);textOut(kind.substr(0,36),rx+11,236,.59f,.76,.79,.85);
    auto* laser=selectedNotchedTube();auto* source=selectedSourceTube();
    if(!laser&&!source)label("GEOMETRY",rx,283,.66f);
    auto metric=[&](const char* title,const std::string& value,float y){label(title,rx,y,.76f);textOut(value,W-24-textWidth(value,.80f),y,.80f);};
    if(selBody>=0&&!laser&&!source){auto& r=g_reports[g_instancePart[selBody]];
        metric("Vertices",std::to_string(r.vertices),314);metric("Triangles",std::to_string(r.triangles),343);metric("Components",std::to_string(r.components),372);
        char size[100];auto e=r.bounds.extent();std::snprintf(size,sizeof size,"%.1f / %.1f / %.1f",e.x,e.y,e.z);metric("XYZ mm",size,392);
        rect(rx,429,254,1,0,.17,.21,.22,1,0,0,0,0,0);label("TOPOLOGY AUDIT",rx,453,.66f);
        metric("Boundary edges",std::to_string(r.boundaryEdges),486);metric("Non-manifold edges",std::to_string(r.nonManifoldEdges),519);
        metric("Degenerate faces",std::to_string(r.degenerateTriangles),552);
        textOut(r.closedOriented()?"o  Closed, oriented edges":"o  Open or irregular topology",rx,587,.73f,.76,.81,.87);
        label("Self-intersections not checked",rx,613,.62f);
    }
    if(H>1000 && kind=="EXPRESSION SURFACE / MESH"){rect(rx,673,254,1,0,.17,.21,.22,1,0,0,0,0,0);label("MATHEMATICAL MODEL",rx,697,.66f);
        textOut("x(u,v)   y(u,v)   z(u,v)",rx,730,.85f,.70,.82,.79);label("Trigonometry + named parameters",rx,761,.67f);label("Periodic seams validated",rx,786,.67f);}
    if(selBody>=0&&g_tubeInfo.count(g_bodyIds[selBody])&&H>940){
        auto& report=g_tubeInfo[g_bodyIds[selBody]];char length[80];std::snprintf(length,sizeof length,"%.2f mm",report.find("centerline_length_mm")->num);
        label("TUBE PROGRAM",rx,680,.66f);metric("Developed axis",length,710);metric("Bends",std::to_string(report.find("bends")->arr.size()),743);
        button("Edit tube program",rx,H-214,252,30);
    }
    if(laser){
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
    float cy=H-178;panel(viewX,cy,(float)viewW,150);
    if(laser||source){
        label(source?"SOURCE BEND PREVIEW":"IDEAL HINGE PREVIEW",viewX+22,cy+16,.65f);
        label(std::to_string((int)std::round(g_foldProgress*100))+" %",viewX+220,cy+16,.66f);
        button("Command",viewX+viewW-120,cy+8,98,21);
        float x=viewX+22;button("Straight",x,cy+46,88,45,false,g_foldProgress==0);button("Folded",x+98,cy+46,88,46,false,g_foldProgress==1);
        button(g_foldPlaying?"Pause":"Play",x+196,cy+46,88,47,true);button("-10%",x+294,cy+46,70,48);button("+10%",x+374,cy+46,70,49);
        float length=viewW-44;rect(x,cy+92,length,5,2,.22,.24,.28,1,0,0,0,0,0);rect(x,cy+92,std::max(3.f,length*(float)g_foldProgress),5,2,.70,.75,.84,1,0,0,0,0,0);
        rect(x+length*(float)g_foldProgress-5,cy+87,10,15,4,.84,.88,.95,1,0,0,0,0,0);g_zones.push_back({x,cy+82,length,23,50});
    }else{
        label("AGENT WORKSPACE",viewX+22,cy+16,.65f);label("REV "+std::to_string(g_session.revision),viewX+200,cy+16,.65f);
        textOut("Geometry, expressed.",viewX+22,cy+48,viewW<760?.84f:1.05f);
        label("Model, inspect and replay with the agent API",viewX+22,cy+80,.66f);
        button("Run command   Ctrl+K",viewX+viewW-228,cy+43,205,21,true);
    }
    rect(viewX+22,cy+109,viewW-44,26,4,.063,.068,.079,1,0,0,0,0,0);
    textOut(g_status.substr(0,(size_t)std::max((viewW-65)/8,1)),viewX+32,cy+114,.66f,g_error?.95f:.68f,g_error?.58f:.73f,g_error?.48f:.81f);
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
        case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize={1180,840};return 0;
        case WM_SIZE:
            winW=LOWORD(lp)>0?LOWORD(lp):1;
            winH=HIWORD(lp)>0?HIWORD(lp):1;
            layout();return 0;
        case WM_MOUSEWHEEL:
            {POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hw,&p);
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
                    { g_pressWasButton=true;if(z.id==50){g_foldScrubbing=true;g_foldPlaying=false;g_foldProgress=clamp((double)(p.x-z.x)/z.w,0.,1.);} break; }
            g_dragged=false;SetCapture(hw);
            return 0;
        }
        case WM_MOUSEMOVE:{
            static int px=0,pyy=0; static bool has=false;
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            bool lbtn=wp&MK_LBUTTON,mbtn=wp&MK_MBUTTON,shift=wp&MK_SHIFT;
            if(g_foldScrubbing&&lbtn)g_foldProgress=clamp((double)(x-viewX-22)/(viewW-44),0.,1.);
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
                       (float)p.y>=z.y&&(float)p.y<=z.y+z.h){ if(z.id==50){g_foldProgress=clamp((double)(p.x-z.x)/z.w,0.,1.);g_foldPlaying=false;}else interactiveAction(z.id); break; }
            } else if(!g_dragged) {
                int b=pickBody(p.x,p.y);
                selBody=(b==selBody)?-1:b;g_selectedNotch=0;g_notchScroll=0;
            }
            g_pressWasButton=false;g_foldScrubbing=false;
            return 0;
        }
        case WM_KEYDOWN:
            if(GetKeyState(VK_CONTROL)&0x8000){if(wp=='S')uiAction(16);else if(wp=='O')uiAction(17);else if(wp=='Z')uiAction(18);else if(wp=='Y')uiAction(19);else if(wp=='K')uiAction(21);}
            else if(wp=='F')uiAction(20);else if(wp=='W')uiAction(14);else if(wp==VK_DELETE)uiAction(25);else if(wp==VK_F1)uiAction(31);
            return 0;
        case WM_CLOSE:
            if(!confirmDiscard())return 0;
            DestroyWindow(hw);return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hw,m,wp,lp);
}

// ---------------------------------------------------------------------------
static void seedDocument(){g_doc=makeDemoDocument();rebuildScene();frameScene();target=tTarget;dist=tDist;}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR cmdLine,int){
    cpu_detect();

    SetProcessDPIAware();
    bool headless=false,selfTest=false,assemblyTest=false,dialogTest=false,bendTest=false,bendExportTest=false,sourceTest=false;int requestedOpening=-1;double requestedFold=-1;std::string shotPath="ark_studio.png",loadPath,scriptPath,importPath;
    int sw=1600,sh=1000,argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    auto utf8=[](const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;};
    for(int i=1;i<argc;++i){auto a=utf8(argv[i]);if(a=="--shot"&&i+1<argc){headless=true;shotPath=utf8(argv[++i]);}
        else if(a=="--self-test-ui")selfTest=true;
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
    LocalFree(argv);winW=std::max(sw,1180);winH=std::max(sh,800);layout();

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
    HWND hwnd=CreateWindowA("ARKVIEW","ARK 0.6 / Geometry Studio",WS_OVERLAPPEDWINDOW,
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

    MSG msg{};
    auto fpsT=std::chrono::steady_clock::now();
    int frames=0;
    for(;;){
        while(PeekMessageA(&msg,nullptr,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT)return (int)msg.wParam;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        drawFrame(hdc);
        ++frames;
        auto now=std::chrono::steady_clock::now();
        f64 el=std::chrono::duration<f64,std::milli>(now-fpsT).count();
        if(el>500){ g_fps=frames*1000.0/el; frames=0; fpsT=now; }
        Sleep(1);
    }
}
