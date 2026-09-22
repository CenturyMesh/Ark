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

static AgentSession g_session;
static Document& g_doc=g_session.document;
static std::vector<u64> g_bodyIds;
static std::vector<MeshReport> g_reports;
static std::vector<MeshBVH> g_bvhs;
static size_t g_triangleCount=0;
static std::string g_status="Ready. Local C++ kernel.",g_path="Harmonic ring.ark";
static bool g_error=false,g_grid=true,g_dragged=false;
static u64 g_savedRevision=0;
static int g_treeScroll=0;
static int viewX=258,viewY=108,viewW=1024,viewH=650;
static HWND g_window=nullptr;


static std::vector<Zone>    g_zones;
static std::vector<GPUMesh> g_meshes;
static std::vector<Mesh>    g_cpuMeshes;

static f64 yaw=0.85,pitch=0.46,dist=140,tYaw=yaw,tPitch=pitch,tDist=dist;
static Vec3 target{30,12,0},tTarget=target;
static int winW=1600,winH=1000;
static void layout(){viewW=std::max(winW-558,1);viewH=std::max(winH-286,1);}
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
 vec3 top=vec3(.095,.113,.115),bot=vec3(.037,.048,.053);
 vec3 c=mix(bot,top,uv.y);
 vec2 q=uv-.5;c*=1.-dot(q,q)*.55;
 c+=vec3(.02,.07,.06)*exp(-length(q-vec2(-.38,-.3))*2.4)*.4;
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
uniform vec3 color,eye; uniform float sel;
void main(){
 vec3 n=normalize(nrm); vec3 v=normalize(eye-wpos);
 if(dot(n,v)<0.)n=-n;
 vec3 l1=normalize(vec3(-.45,.85,.35)),l2=normalize(vec3(.65,-.2,-.55));
 float d1=max(dot(n,l1),0.),d2=max(dot(n,l2),0.);
 vec3 base=color*(.32+.70*d1)+color*(.20*d2);
 base+=vec3(1.,.98,.92)*pow(max(dot(n,normalize(l1+v)),0.),64.)*.55;
 float fr=pow(1.-max(dot(n,v),0.),3.2);
 base+=mix(vec3(.25,.60,.65),vec3(.66,.85,.70),sel)*fr*(.5+sel*.8);
 o=vec4(base,1.);
})";
static const char* VS_LINE=R"(#version 330 core
layout(location=0) in vec3 P; uniform mat4 vp; out vec2 gridPos;
void main(){gl_Position=vp*vec4(P,1.);gridPos=P.xz;})";
static const char* FS_LINE=R"(#version 330 core
in vec2 gridPos; out vec4 o;
void main(){o=vec4(.27,.36,.37,exp(-dot(gridPos,gridPos)*.00012)*.28);})";
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
uniform vec4 fill,borderCol,rc; uniform float radius,borderW;
float sdR(vec2 p,vec2 b,float r){vec2 d=abs(p)-b+r;return length(max(d,0.))+min(max(d.x,d.y),0.)-r;}
void main(){
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
static void rebuildScene(bool evaluate=false){
    if(evaluate)g_doc.recompute();
    std::set<u64> consumed;for(auto& f:g_doc.features)for(auto id:f.inputs)consumed.insert(id);
    g_cpuMeshes.clear();g_bodyIds.clear();g_reports.clear();g_bvhs.clear();g_triangleCount=0;
    // Reuse GPU allocations; inactive slots are retained for subsequent edits.
    for(auto& gm:g_meshes)gm.count=0;
    for(auto& b:g_doc.evalBodies){
        if(consumed.count(b.id))continue;
        auto i=g_cpuMeshes.size();g_cpuMeshes.push_back(b.tessellate(g_doc.tessOpt));g_bodyIds.push_back(b.id);
        g_reports.push_back(analyzeMesh(g_cpuMeshes.back()));g_bvhs.emplace_back(g_cpuMeshes.back());g_triangleCount+=g_cpuMeshes.back().triCount();
        if(i>=g_meshes.size())g_meshes.emplace_back();uploadMesh(g_meshes[i],g_cpuMeshes.back());
    }
    if(selBody<0||selBody>=(int)g_cpuMeshes.size())selBody=g_cpuMeshes.empty()?-1:0;
    hoverBody=-1;
}
static void frameScene(){
    AABB b;for(auto& m:g_cpuMeshes)b.grow(m.computeBBox());
    if(b.valid()){tTarget=b.center();tDist=std::max(b.diagonal()*1.65,2.);}
}
static arkjson::Value runTool(const std::string& tool,const arkjson::Value& args){
    auto result=g_session.call(tool,args);g_error=result.find("isError")->b;
    if(g_error)g_status=result.find("structuredContent")->find("error")->asStr();
    else g_status=tool+" completed  /  revision "+std::to_string(g_session.revision);
    if(!g_error&&(tool=="doc_apply"||tool=="doc_load"||tool=="doc_undo"||tool=="doc_redo"))rebuildScene();
    return result;
}
static void applyCommand(arkjson::Value command){
    using V=arkjson::Value;V a=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);a.set("commands",commands);runTool("doc_apply",a);
}
static void addFeat(FeatureType type, std::initializer_list<std::pair<const char*,std::string>> ps,std::vector<u64> inputs={},int mode=0){
    using V=arkjson::Value;V c=V::makeObj(),p=V::makeObj(),in=V::makeArr();c.set("op",V::makeStr("feature.add"));c.set("type",V::makeStr(featureTypeName(type)));
    for(auto& kv:ps)p.set(kv.first,V::makeStr(kv.second));for(auto x:inputs)in.arr.push_back(V::makeNum((double)x));
    c.set("p",p);c.set("inputs",in);c.set("mode",V::makeNum(mode));applyCommand(c);
}
// Small native expression/command editor; the viewport remains the source of feedback.
static HWND g_edit=nullptr,g_dialog=nullptr;static bool g_dialogDone=false,g_dialogAccept=false;
static LRESULT CALLBACK editorProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND&&(LOWORD(w)==1||LOWORD(w)==2)){g_dialogAccept=LOWORD(w)==1;g_dialogDone=true;return 0;}
    if(m==WM_CLOSE){g_dialogDone=true;return 0;}return DefWindowProcA(h,m,w,l);
}
static bool editText(const char* title,std::string& value,bool multiline=false){
    WNDCLASSA wc{};wc.lpfnWndProc=editorProc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="ARK_EDITOR";wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassA(&wc);
    RECT r;GetWindowRect(g_window,&r);int height=multiline?360:165;
    g_dialog=CreateWindowExA(WS_EX_DLGMODALFRAME,"ARK_EDITOR",title,WS_CAPTION|WS_SYSMENU,r.left+180,r.top+140,760,height,g_window,nullptr,wc.hInstance,nullptr);
    g_edit=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT",value.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|(multiline?ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL:0),20,20,700,height-110,g_dialog,(HMENU)3,wc.hInstance,nullptr);
    HFONT font=CreateFontA(-18,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Consolas");SendMessage(g_edit,WM_SETFONT,(WPARAM)font,TRUE);
    CreateWindowA("BUTTON","Apply",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,510,height-75,100,30,g_dialog,(HMENU)1,wc.hInstance,nullptr);
    CreateWindowA("BUTTON","Cancel",WS_CHILD|WS_VISIBLE,620,height-75,100,30,g_dialog,(HMENU)2,wc.hInstance,nullptr);
    g_dialogDone=g_dialogAccept=false;EnableWindow(g_window,FALSE);ShowWindow(g_dialog,SW_SHOW);SetFocus(g_edit);SendMessage(g_edit,EM_SETSEL,0,-1);
    MSG msg;while(!g_dialogDone&&GetMessage(&msg,nullptr,0,0)>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){g_dialogDone=true;continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_RETURN&&(!multiline||(GetKeyState(VK_CONTROL)&0x8000))){g_dialogDone=g_dialogAccept=true;continue;}
        if(!IsDialogMessage(g_dialog,&msg)){TranslateMessage(&msg);DispatchMessage(&msg);}
    }
    if(g_dialogAccept){int n=GetWindowTextLengthA(g_edit);std::vector<char> b(n+1);GetWindowTextA(g_edit,b.data(),n+1);value=b.data();}
    EnableWindow(g_window,TRUE);DestroyWindow(g_dialog);DeleteObject(font);SetForegroundWindow(g_window);return g_dialogAccept;
}
static void fileAction(bool save,bool mesh=false){
    char path[MAX_PATH]{};if(save)std::snprintf(path,sizeof path,"%s",mesh?"ark_export.stl":g_path.c_str());
    OPENFILENAMEA of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=path;of.nMaxFile=MAX_PATH;
    of.lpstrFilter=mesh?"STL mesh\0*.stl\0":"ARK document\0*.ark;*.arkjson\0";of.lpstrDefExt=mesh?"stl":"ark";
    of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(save?GetSaveFileNameA(&of):GetOpenFileNameA(&of)))return;
    using V=arkjson::Value;V a=V::makeObj();a.set("path",V::makeStr(path));a.set("format",V::makeStr("stl"));
    runTool(mesh?"export_mesh":save?"doc_save":"doc_load",a);
    if(!g_error&&!mesh){g_path=path;g_savedRevision=g_session.revision;if(!save)frameScene();}
}
static void uiAction(int id){
    using V=arkjson::Value;
    if(id>=2000){selBody=id-2000;return;}
    if(id>=1000){size_t i=id-1000;if(i>=g_doc.params.size())return;std::string value=g_doc.params[i].expr,name=g_doc.params[i].name;
        if(editText(("Edit parameter: "+name).c_str(),value)){V c=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(name));c.set("expr",V::makeStr(value));applyCommand(c);}return;}
    switch(id){
        case 1:addFeat(FeatureType::Box,{{"dx","24"},{"dy","24"},{"dz","16"}});break;
        case 2:addFeat(FeatureType::Cylinder,{{"r","12"},{"h","28"}});break;
        case 3:addFeat(FeatureType::Sphere,{{"r","14"}});break;
        case 4:addFeat(FeatureType::Cone,{{"rb","12"},{"rt","2"},{"h","26"}});break;
        case 5:addFeat(FeatureType::Torus,{{"R","16"},{"r","5"}});break;
        case 6:addFeat(FeatureType::Extrude,{{"shape","polygon"},{"n","6"},{"r","18"},{"height","24"}});break;
        case 7:addFeat(FeatureType::Revolve,{{"shape","rect"},{"w","8"},{"d","18"},{"cx","16"},{"angle","300"}});break;
        case 8:case 9:case 10:
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
        case 25:if(selBody>=0&&(size_t)selBody<g_bodyIds.size()){V c=V::makeObj();c.set("op",V::makeStr("feature.remove"));c.set("id",V::makeNum((double)g_bodyIds[selBody]));applyCommand(c);}break;
    }
}

// picking -----------------------------------------------------------------------
static Vec3 camEye(){
    AABB all; for(auto&m:g_cpuMeshes)all.grow(m.computeBBox());
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
    for(size_t bi=0;bi<g_cpuMeshes.size();++bi){
        auto hit=g_bvhs[bi].raycast(g_cpuMeshes[bi],{eye,rd});
        if(hit.hit&&hit.distance<bt){bt=hit.distance;best=(int)bi;}
    }
    return best;
}

// ---------------------------------------------------------------------------
static GLuint pBg,pSolid,pGrid,pShadow,pUI,pText;
static GLint suVp,suModel,suOutline,suColor,suEye,suSel;
static GLint uuVp,uuRc,uuRad,uuFill,uuBorder,uuBW;
static GLuint quadVao,gridVao,textVao,textVbo;
static int gridCount=0;
static bool g_swap=true;   // headless readback disables the buffer swap

static void drawFrame(HDC hdc){
    yaw+=(tYaw-yaw)*.2; pitch+=(tPitch-pitch)*.2; dist+=(tDist-dist)*.18;
    target=target+(tTarget-target)*.2;
    AABB box; for(auto&m:g_cpuMeshes)box.grow(m.computeBBox());
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
    for(auto&m:g_cpuMeshes){
        AABB b=m.computeBBox(); if(!b.valid())continue;
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
    for(size_t i=0;i<g_meshes.size();++i){
        auto&gm=g_meshes[i]; if(!gm.count)continue;
        float sel=i==(size_t)selBody?1.f:i==(size_t)hoverBody?.3f:0.f;
        float hue=((i*53)%97)/97.f;
        glUniform3f(suColor,.48f+.10f*hue,.69f+.05f*hue,.65f+.07f*hue);
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
    auto panel=[&](float x,float y,float w,float h,float r=0.f){rect(x,y,w,h,r,.065f,.078f,.084f,1,.16f,.19f,.20f,.8f,1);};
    auto label=[&](const std::string& t,float x,float y,float scale=.75f){textOut(t,x,y,scale,.47f,.55f,.56f);};
    auto button=[&](const char* t,float x,float y,float w,int id,bool accent=false,bool active=false){
        bool hover=mp.x>=x&&mp.x<x+w&&mp.y>=y&&mp.y<y+32;
        if(hover)g_hoverZone=id;
        rect(x,y,w,32,6,accent?.64f:hover?.16f:active?.14f:.095f,accent?.83f:hover?.21f:active?.24f:.12f,accent?.72f:hover?.22f:active?.22f:.13f,1,.24f,.34f,.32f,hover?1.f:.4f,1);
        textOut(t,x+(w-textWidth(t,.78f))/2,y+7,.78f,accent?.07f:.79f,accent?.13f:.85f,accent?.12f:.84f);
        g_zones.push_back({x,y,w,32,id});
    };
    panel(0,0,W,58);panel(0,58,W,50);panel(0,108,258,H-136);panel(W-300,108,300,H-136);
    rect(20,15,28,28,7,.66,.85,.74,1,0,0,0,0,0);textOut("A",26,17,1.05f,.07,.12,.10);
    textOut("ARK",60,16,1.23f);label("/",121,20,.9f);label("DESIGN WORKSPACE",141,22,.67f);
    std::string filename=g_path.substr(g_path.find_last_of("/\\")+1);
    if(g_session.revision!=g_savedRevision)filename+=" *";
    textOut(filename,viewX+24,20,.86f);label("LOCAL",viewX+24+textWidth(filename,.86f)+18,23,.62f);
    button("Open",W-342,13,70,17);button("Undo",W-262,13,70,18);button("Redo",W-182,13,70,19);button("Save .ark",W-102,13,86,16,true);
    label("CREATE",20,76,.65f);
    button("Box",83,67,57,1);button("Cylinder",146,67,80,2);button("Sphere",232,67,70,3);button("Torus",308,67,67,5);
    rect(389,72,1,22,0,.22,.27,.28,1,0,0,0,0,0);
    button("Extrude",404,67,77,6);button("Revolve",487,67,77,7);button("Pattern",570,67,77,11);
    button("Union",663,67,67,8);button("Cut",736,67,54,9);button("Intersect",796,67,85,10);
    button("Export STL",W-122,67,106,12);
    label("DOCUMENT",20,128,.68f);label("01",220,128,.68f);
    textOut("Model explorer",20,158,1.05f);label(std::to_string(g_doc.features.size())+" features / "+std::to_string(g_cpuMeshes.size())+" outputs",20,188,.72f);
    float treeY=228;
    for(size_t i=(size_t)g_treeScroll;i<g_doc.features.size()&&treeY<H-385;i++){
        auto& f=g_doc.features[i];int body=-1;for(size_t j=0;j<g_bodyIds.size();++j)if(g_bodyIds[j]==f.id)body=(int)j;
        bool selected=body>=0&&selBody==body;
        if(selected)rect(12,treeY-7,234,53,7,.115,.20,.18,1,.26,.40,.34,.7,1);
        textOut(body>=0?"<>":"-",24,treeY+2,.9f,.63,.81,.72);
        textOut(f.name.substr(0,23),54,treeY,.82f);label(std::string(featureTypeName(f.type))+"  #"+std::to_string(f.id),54,treeY+23,.65f);
        if(body>=0)g_zones.push_back({12,treeY-7,234,53,2000+body});treeY+=62;
    }
    float ly=H-340;rect(20,ly,218,1,0,.17,.21,.22,1,0,0,0,0,0);label("PARAMETERS",20,ly+23,.68f);label("Click to edit",152,ly+23,.65f);
    for(size_t i=0;i<g_doc.params.size()&&i<4;++i){auto& p=g_doc.params[i];float y=ly+58+i*48;
        textOut(p.name.substr(0,15),22,y+5,.79f);rect(127,y-1,108,31,5,.09,.115,.12,1,.22,.28,.27,.7,1);
        textOut(p.expr.substr(0,12),139,y+5,.78f,.68,.85,.76);g_zones.push_back({15,y-2,224,34,(int)i+1000});
    }
    label("C++ 20   /   PARAMETRIC",20,H-66,.62f);
    textOut("Perspective",viewX+24,131,.87f);label("Y up  /  mm",viewX+24,155,.67f);
    button("Fit",W-486,124,55,20);button("Top",W-425,124,49,23);button("Iso",W-370,124,49,24);
    float rx=W-278;label("INSPECTOR",rx,129,.68f);label("MESH",W-66,129,.65f);
    std::string selected="No selection",kind="No body";if(selBody>=0){auto* f=g_doc.find(g_bodyIds[selBody]);if(f){selected=f->name;kind=f->type==FeatureType::ParametricSurface?"EXPRESSION SURFACE / MESH":std::string(featureTypeName(f->type))+" / TESSELLATED MESH";}}
    textOut(selected.substr(0,25),rx,165,1.05f);
    rect(rx,205,254,28,5,.105,.17,.15,1,0,0,0,0,0);textOut(kind,rx+11,211,.64f,.60,.81,.69);
    label("GEOMETRY",rx,260,.66f);
    auto metric=[&](const char* title,const std::string& value,float y){label(title,rx,y,.76f);textOut(value,W-24-textWidth(value,.80f),y,.80f);};
    if(selBody>=0){auto& r=g_reports[selBody];
        metric("Vertices",std::to_string(r.vertices),293);metric("Triangles",std::to_string(r.triangles),326);metric("Components",std::to_string(r.components),359);
        char size[100];auto e=r.bounds.extent();std::snprintf(size,sizeof size,"%.1f / %.1f / %.1f",e.x,e.y,e.z);metric("XYZ mm",size,392);
        rect(rx,429,254,1,0,.17,.21,.22,1,0,0,0,0,0);label("TOPOLOGY AUDIT",rx,453,.66f);
        metric("Boundary edges",std::to_string(r.boundaryEdges),486);metric("Non-manifold edges",std::to_string(r.nonManifoldEdges),519);
        metric("Degenerate faces",std::to_string(r.degenerateTriangles),552);
        textOut(r.closedOriented()?"o  Closed, oriented edges":"o  Open or irregular topology",rx,596,.79f,.64,.85,.73);
        label("Self-intersections not checked",rx,623,.65f);
    }
    if(H>840 && kind=="EXPRESSION SURFACE / MESH"){rect(rx,673,254,1,0,.17,.21,.22,1,0,0,0,0,0);label("MATHEMATICAL MODEL",rx,697,.66f);
        textOut("x(u,v)   y(u,v)   z(u,v)",rx,730,.85f,.70,.82,.79);label("Trigonometry + named parameters",rx,761,.67f);label("Periodic seams validated",rx,786,.67f);}
    button("Wireframe",rx,H-120,121,14,false,wireframe);button("Grid",rx+131,H-120,121,22,false,g_grid);
    button("Delete selected",rx,H-78,252,25);
    float cy=H-178;panel(viewX,cy,(float)viewW,150);
    label("AGENT CONSOLE",viewX+22,cy+16,.65f);label("LOCAL SESSION / REV "+std::to_string(g_session.revision),viewX+175,cy+16,.65f);
    textOut("Geometry, expressed.",viewX+22,cy+46,1.05f);
    label("Batch operations / inspect meshes / replay documents",viewX+22,cy+77,.72f);
    button("Run command   Ctrl+K",viewX+viewW-228,cy+43,205,21,true);
    rect(viewX+22,cy+109,viewW-44,26,4,.047,.061,.066,1,0,0,0,0,0);
    textOut(g_status.substr(0,(size_t)std::max((viewW-65)/8,1)),viewX+32,cy+114,.66f,g_error?.95f:.51f,g_error?.58f:.73f,g_error?.48f:.63f);
    panel(0,H-28,W,28);
    textOut("o",17,H-22,.65f,.63,.85,.72);label("KERNEL READY",34,H-21,.61f);
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
            if(p.x<viewX){g_treeScroll=std::max(0,std::min((int)g_doc.features.size()-1,g_treeScroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1)));return 0;}}
            tDist=clamp(tDist*(GET_WHEEL_DELTA_WPARAM(wp)>0?0.87:1.15),2.,4000.);
            return 0;
        case WM_LBUTTONDOWN:{
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            g_pressWasButton=p.x<viewX||p.x>viewX+viewW||p.y<viewY||p.y>viewY+viewH;
            for(auto&z:g_zones)
                if((float)p.x>=z.x&&(float)p.x<=z.x+z.w&&(float)p.y>=z.y&&(float)p.y<=z.y+z.h)
                    { g_pressWasButton=true; break; }
            g_dragged=false;SetCapture(hw);
            return 0;
        }
        case WM_MOUSEMOVE:{
            static int px=0,pyy=0; static bool has=false;
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            bool lbtn=wp&MK_LBUTTON,mbtn=wp&MK_MBUTTON,shift=wp&MK_SHIFT;
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
                       (float)p.y>=z.y&&(float)p.y<=z.y+z.h){ uiAction(z.id); break; }
            } else if(!g_dragged) {
                int b=pickBody(p.x,p.y);
                selBody=(b==selBody)?-1:b;
            }
            g_pressWasButton=false;
            return 0;
        }
        case WM_KEYDOWN:
            if(GetKeyState(VK_CONTROL)&0x8000){if(wp=='S')uiAction(16);else if(wp=='O')uiAction(17);else if(wp=='Z')uiAction(18);else if(wp=='Y')uiAction(19);else if(wp=='K')uiAction(21);}
            else if(wp=='F')uiAction(20);else if(wp=='W')uiAction(14);else if(wp==VK_DELETE)uiAction(25);
            return 0;
        case WM_CLOSE:
            if(g_session.revision!=g_savedRevision){
                int answer=MessageBoxA(hw,"Save changes to this ARK document?","ARK",MB_YESNOCANCEL|MB_ICONQUESTION);
                if(answer==IDCANCEL)return 0;
                if(answer==IDYES){fileAction(true);if(g_session.revision!=g_savedRevision)return 0;}
            }
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
    bool headless=false,selfTest=false;std::string shotPath="ark_studio.png",loadPath,scriptPath;
    int sw=1600,sh=1000,argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    auto utf8=[](const wchar_t* w){int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;};
    for(int i=1;i<argc;++i){auto a=utf8(argv[i]);if(a=="--shot"&&i+1<argc){headless=true;shotPath=utf8(argv[++i]);}
        else if(a=="--self-test-ui")selfTest=true;
        else if(a=="--size"&&i+2<argc){sw=_wtoi(argv[++i]);sh=_wtoi(argv[++i]);}
        else if(a=="--load"&&i+1<argc)loadPath=utf8(argv[++i]);
        else if(a=="--script"&&i+1<argc)scriptPath=utf8(argv[++i]);}
    LocalFree(argv);winW=std::max(sw,1180);winH=std::max(sh,800);layout();

    // ---- window ----
    WNDCLASSA wc{}; wc.style=CS_OWNDC; wc.hInstance=hi;
    wc.lpfnWndProc=wndProc; wc.lpszClassName="ARKVIEW";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassA(&wc);
    RECT rc{0,0,winW,winH};
    AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowA("ARKVIEW","ARK / Geometry Studio",WS_OVERLAPPEDWINDOW,
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
    suOutline=glGetUniformLocation(pSolid,"outline"); suColor=glGetUniformLocation(pSolid,"color");
    suEye=glGetUniformLocation(pSolid,"eye");    suSel=glGetUniformLocation(pSolid,"sel");
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
    if(!loadPath.empty()){arkjson::Value a=arkjson::Value::makeObj();a.set("path",arkjson::Value::makeStr(loadPath));runTool("doc_load",a);g_path=loadPath;if(!g_error)g_savedRevision=g_session.revision;frameScene();}
    if(!scriptPath.empty()){
        std::ifstream input(scriptPath);std::string line;
        if(!input)return 2;
        while(std::getline(input,line)){arkjson::Value c;if(!arkjson::parse(line,c)||!c.find("tool")||!c.find("arguments"))return 2;
            runTool(c.find("tool")->asStr(),*c.find("arguments"));if(g_error)return 3;}
        frameScene();
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
    target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;

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
