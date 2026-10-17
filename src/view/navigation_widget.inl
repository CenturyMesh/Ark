// View-only compass: the same retained 26 facets own rendering and ray picking.
// Broad edge/corner faces make diagonal directions visible, rather than a tiny bevel.
static constexpr double kNavigationExtent=.94,kNavigationFaceHalf=.60,kNavigationFrustum=1.50;
struct NavigationFacet {Vec3 normal,center;int face=-1,first=0,count=0,edgeFirst=0,edgeCount=0;};
static std::vector<NavigationFacet> g_navigationFacets;
static GLuint g_navigationEdgesVao=0,g_navigationEdgesVbo=0;
static GLuint g_navigationFbo=0,g_navigationColor=0,g_navigationDepth=0,g_navigationResolve=0;
static int g_navigationTargetSize=0,g_navigationTestSize=0;
static unsigned g_navigationGeometryBuilds=0,g_navigationTargetAllocations=0;
static bool g_navigationTargetReady=false;

static void updateNavigationBounds(){
    float expansion=std::clamp(((float)viewW-622.f)/420.f,0.f,1.f);
    g_cubeS=std::max(44,std::min((int)std::lround(136.f+24.f*expansion),viewH-26));
    if(g_navigationTestSize)g_cubeS=std::min(g_navigationTestSize,std::min(viewW-28,viewH-26));
    g_cubeX=viewX+14;g_cubeY=viewY+12;
}
static bool overViewCube(float x,float y,float padding=20.f){return g_showViewCube&&x>=g_cubeX-padding&&x<=g_cubeX+g_cubeS+padding&&y>=g_cubeY-padding&&y<=g_cubeY+g_cubeS+padding;}
static void cubeCamera(Vec3& eye,Mat4& view,Mat4& proj){
    auto frame=cameraFrame(yaw,pitch,cameraRoll);eye=frame.outward*4.2;
    view=Mat4::lookAt(eye,{},frame.up);proj=Mat4::ortho(-kNavigationFrustum,kNavigationFrustum,-kNavigationFrustum,kNavigationFrustum,.05,20);
}
static CubeHit cubeHitAt(int mx,int my){
    CubeHit hit;if(g_cubeS<8||mx<g_cubeX||my<g_cubeY||mx>=g_cubeX+g_cubeS||my>=g_cubeY+g_cubeS)return hit;
    Vec3 eye;Mat4 view,proj;cubeCamera(eye,view,proj);
    double nx=((mx-g_cubeX)/(double)g_cubeS)*2-1,ny=(1.-(my-g_cubeY)/(double)g_cubeS)*2-1;
    Vec3 right{view.m[0],view.m[4],view.m[8]},up{view.m[1],view.m[5],view.m[9]},dir{-view.m[2],-view.m[6],-view.m[10]};
    Vec3 origin=eye+right*(nx*kNavigationFrustum)+up*(ny*kNavigationFrustum);double nearest=1e100;
    for(const CubeTriangle& triangle:g_cubeTriangles){
        Vec3 ab=triangle.b-triangle.a,ac=triangle.c-triangle.a,p=dir.cross(ac);double determinant=ab.dot(p);
        if(std::abs(determinant)<1e-10)continue;double inverse=1./determinant;Vec3 from=origin-triangle.a;
        double u=from.dot(p)*inverse;if(u<0||u>1)continue;Vec3 q=from.cross(ab);double v=dir.dot(q)*inverse;
        if(v<0||u+v>1)continue;double distance=ac.dot(q)*inverse;
        if(distance>=0&&distance<nearest){nearest=distance;hit={true,triangle.face,triangle.normal,triangle.facet};}
    }
    return hit;
}
static int cubeFaceAt(int mx,int my){auto hit=cubeHitAt(mx,my);return hit.hit?hit.face:-1;}
static void orientViewCube(int mx,int my){
    static const int faceView[6]={182,183,23,184,180,181};auto hit=cubeHitAt(mx,my);if(!hit.hit)return;
    if(hit.face>=0)uiAction(faceView[hit.face]);
    else{Vec3 n=hit.normal.normalized();setOrbitView(std::atan2(n.x,n.z),std::asin(clamp(n.y,-1.,1.)),true,"Diagonal");}
}
static void buildNavigationWidget(){
    if(gCubeVao)return;++g_navigationGeometryBuilds;
    std::vector<float> vertices,edges;g_cubeTriangles.clear();g_navigationFacets.clear();
    auto with=[](Vec3 p,int axis,double value){if(axis==0)p.x=value;else if(axis==1)p.y=value;else p.z=value;return p;};
    auto facet=[&](std::initializer_list<Vec3> points,Vec3 normal,int face){
        normal=normal.normalized();int index=(int)g_navigationFacets.size();NavigationFacet f;f.normal=normal;f.face=face;f.first=(int)vertices.size()/6;f.edgeFirst=(int)edges.size()/3;
        std::vector<Vec3> polygon(points);for(auto p:polygon)f.center+=p;f.center=f.center/(double)polygon.size();
        for(size_t i=1;i+1<polygon.size();++i){Vec3 a=polygon[0],b=polygon[i],c=polygon[i+1];if((b-a).cross(c-a).dot(normal)<0)std::swap(b,c);
            g_cubeTriangles.push_back({a,b,c,normal,face,index});for(auto p:{a,b,c})vertices.insert(vertices.end(),{(float)p.x,(float)p.y,(float)p.z,(float)normal.x,(float)normal.y,(float)normal.z});}
        for(size_t i=0;i<polygon.size();++i)for(auto p:{polygon[i],polygon[(i+1)%polygon.size()]}){p+=normal*.0015;edges.insert(edges.end(),{(float)p.x,(float)p.y,(float)p.z});}
        f.count=(int)vertices.size()/6-f.first;f.edgeCount=(int)edges.size()/3-f.edgeFirst;g_navigationFacets.push_back(f);
    };
    const double s=kNavigationExtent,a=kNavigationFaceHalf;
    for(int face=0;face<6;++face){int axis=face/2,sign=face%2?-1:1,u=(axis+1)%3,v=(axis+2)%3;
        auto p=[&](double x,double y){return with(with(with({},axis,sign*s),u,x),v,y);};
        facet({p(-a,-a),p(a,-a),p(a,a),p(-a,a)},with({},axis,(double)sign),face);}
    for(int axis=0;axis<3;++axis)for(int other=axis+1;other<3;++other){int along=3-axis-other;
        for(int sa:{-1,1})for(int sb:{-1,1}){auto p=[&](double x,double y,double z){return with(with(with({},axis,x),other,y),along,z);};
            facet({p(sa*s,sb*a,-a),p(sa*s,sb*a,a),p(sa*a,sb*s,a),p(sa*a,sb*s,-a)},with(with({},axis,(double)sa),other,(double)sb),-1);}}
    for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1})facet({{x*s,y*a,z*a},{x*a,y*s,z*a},{x*a,y*a,z*s}},{(double)x,(double)y,(double)z},-1);
    glGenVertexArrays(1,&gCubeVao);glGenBuffers(1,&gCubeVbo);glBindVertexArray(gCubeVao);glBindBuffer(GL_ARRAY_BUFFER,gCubeVbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,nullptr);glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);
    glGenVertexArrays(1,&g_navigationEdgesVao);glGenBuffers(1,&g_navigationEdgesVbo);glBindVertexArray(g_navigationEdgesVao);glBindBuffer(GL_ARRAY_BUFFER,g_navigationEdgesVbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(edges.size()*sizeof(float)),edges.data(),GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
}

#include "navigation_font.inl"

static bool ensureNavigationTarget(){
    const int size=g_cubeS*3;
    if(size==g_navigationTargetSize)return g_navigationTargetReady;
    g_navigationTargetSize=size;g_navigationTargetReady=false;++g_navigationTargetAllocations;
    if(!g_navigationFbo){glGenFramebuffers(1,&g_navigationFbo);glGenTextures(1,&g_navigationColor);glGenTextures(1,&g_navigationDepth);}
    auto texture=[&](GLuint id,GLint internal,GLenum format,GLenum type){glBindTexture(GL_TEXTURE_2D,id);glTexImage2D(GL_TEXTURE_2D,0,internal,size,size,0,format,type,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);};
    texture(g_navigationColor,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);texture(g_navigationDepth,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_FLOAT);
    glBindFramebuffer(GL_FRAMEBUFFER,g_navigationFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g_navigationColor,0);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,g_navigationDepth,0);
    g_navigationTargetReady=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(!g_navigationResolve)g_navigationResolve=mkProg(VS_UI,R"(#version 330 core
in vec2 uv;out vec4 o;uniform sampler2D image;uniform vec2 sourceSize;
void main(){vec2 q=vec2(uv.x,1.-uv.y);vec4 sum=vec4(0.);for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)sum+=texture(image,q+vec2(x,y)/sourceSize);o=sum/9.;})");
    return g_navigationTargetReady;
}
static void paintViewCube(){
    if(!gCubeVao)return;bool supersampled=ensureNavigationTarget();int factor=supersampled?3:1,size=g_cubeS*factor;
    if(supersampled){glBindFramebuffer(GL_FRAMEBUFFER,g_navigationFbo);glViewport(0,0,size,size);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
    else{glEnable(GL_SCISSOR_TEST);glScissor(g_cubeX,winH-g_cubeY-g_cubeS,g_cubeS,g_cubeS);glViewport(g_cubeX,winH-g_cubeY-g_cubeS,g_cubeS,g_cubeS);glClear(GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);}
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    Vec3 eye;Mat4 view,proj;cubeCamera(eye,view,proj);Mat4 vp=proj*view;GLfloat matrix[16];for(int i=0;i<16;++i)matrix[i]=(GLfloat)vp.m[i];
    static const GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glUseProgram(pCube);glUniform3f(glGetUniformLocation(pCube,"eye"),(float)eye.x,(float)eye.y,(float)eye.z);glUniformMatrix4fv(glGetUniformLocation(pCube,"vp"),1,GL_FALSE,matrix);glUniformMatrix4fv(glGetUniformLocation(pCube,"model"),1,GL_FALSE,identity);glUniform1f(glGetUniformLocation(pCube,"outline"),0);
    glBindVertexArray(gCubeVao);Vec3 direction=eye.normalized();
    for(size_t i=0;i<g_navigationFacets.size();++i){const auto& f=g_navigationFacets[i];bool hover=g_cubeHover.hit&&g_cubeHover.facet==(int)i,selected=f.normal.dot(direction)>.997;
        Vec3 color=f.face>=0?Vec3{.83,.89,.96}:i<18?Vec3{.61,.72,.84}:Vec3{.72,.80,.90};
        if(selected)color={.45,.85,.96};if(hover)color={.98,.77,.39};
        glUniform3f(glGetUniformLocation(pCube,"color"),(float)color.x,(float)color.y,(float)color.z);glDrawArrays(GL_TRIANGLES,f.first,f.count);}
    glUseProgram(pEdge);glUniformMatrix4fv(euVp,1,GL_FALSE,matrix);glUniformMatrix4fv(euModel,1,GL_FALSE,identity);glBindVertexArray(g_navigationEdgesVao);glLineWidth((float)factor*.70f);
    for(size_t i=0;i<g_navigationFacets.size();++i){const auto& f=g_navigationFacets[i];if(f.normal.dot(direction)<=0)continue;
        bool hover=g_cubeHover.hit&&g_cubeHover.facet==(int)i,selected=f.normal.dot(direction)>.997;
        Vec3 color=hover?Vec3{.82,.44,.10}:selected?Vec3{.12,.57,.72}:Vec3{.23,.33,.44};glUniform3f(glGetUniformLocation(pEdge,"color"),(float)color.x,(float)color.y,(float)color.z);glDrawArrays(GL_LINES,f.edgeFirst,f.edgeCount);}
    glLineWidth(1);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    paintNavigationLabels(vp,direction,factor);
    if(supersampled){glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,winW,winH);glUseProgram(g_navigationResolve);glUniform2f(glGetUniformLocation(g_navigationResolve,"vp"),(float)winW,(float)winH);glUniform4f(glGetUniformLocation(g_navigationResolve,"rc"),(float)g_cubeX,(float)g_cubeY,(float)g_cubeS,(float)g_cubeS);glUniform2f(glGetUniformLocation(g_navigationResolve,"sourceSize"),(float)size,(float)size);glUniform1i(glGetUniformLocation(g_navigationResolve,"image"),0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g_navigationColor);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    glViewport(0,0,winW,winH);
}
