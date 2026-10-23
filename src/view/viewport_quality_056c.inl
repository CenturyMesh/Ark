// FXAA operates on the model viewport before labels/widgets are composited.
// Resources are reused until the viewport changes size; no CPU pixel readback.
static GLuint g_viewportAaProgram056=0,g_viewportAaTexture056=0;
static int g_viewportAaWidth056=0,g_viewportAaHeight056=0;
static bool g_viewportAaReady056=false;
static const char* FS_VIEWPORT_AA056=R"(#version 330 core
out vec4 o;uniform sampler2D scene;uniform vec2 res,origin;uniform int quality;
float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
void main(){
 vec2 uv=(gl_FragCoord.xy-origin)/res,px=1./res;vec3 c=texture(scene,uv).rgb;
 float nw=luma(texture(scene,uv+vec2(-1,1)*px).rgb),ne=luma(texture(scene,uv+vec2(1,1)*px).rgb);
 float sw=luma(texture(scene,uv+vec2(-1,-1)*px).rgb),se=luma(texture(scene,uv+vec2(1,-1)*px).rgb),m=luma(c);
 float lo=min(m,min(min(nw,ne),min(sw,se))),hi=max(m,max(max(nw,ne),max(sw,se)));
 if(hi-lo<max(.018,hi*(quality==2?.063:.125))){o=vec4(c,1.);return;}
 vec2 direction=vec2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
 float reduce=max((nw+ne+sw+se)*.03125,.0078125);
 direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(quality==2?-8.:-4.),vec2(quality==2?8.:4.))*px;
 vec3 a=.5*(texture(scene,uv-direction/6.).rgb+texture(scene,uv+direction/6.).rgb);
 vec3 b=.5*a+.25*(texture(scene,uv-direction*.5).rgb+texture(scene,uv+direction*.5).rgb);
 float lb=luma(b);o=vec4(lb<lo||lb>hi?a:b,1.);
})";
static void applyViewportAntialiasing056(){
    if(!g_antialiasing||viewW<2||viewH<2)return;
    if(!g_viewportAaProgram056){g_viewportAaProgram056=mkProg(VS_BG,FS_VIEWPORT_AA056);GLint linked=0;glGetProgramiv(g_viewportAaProgram056,GL_LINK_STATUS,&linked);g_viewportAaReady056=linked!=0;}
    if(!g_viewportAaReady056)return;
    if(!g_viewportAaTexture056)glGenTextures(1,&g_viewportAaTexture056);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g_viewportAaTexture056);
    if(g_viewportAaWidth056!=viewW||g_viewportAaHeight056!=viewH){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,viewW,viewH,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        g_viewportAaWidth056=viewW;g_viewportAaHeight056=viewH;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewX,winH-viewY-viewH,viewW,viewH);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(g_viewportAaProgram056);
    glUniform1i(glGetUniformLocation(g_viewportAaProgram056,"scene"),0);glUniform1i(glGetUniformLocation(g_viewportAaProgram056,"quality"),g_antialiasing);
    glUniform2f(glGetUniformLocation(g_viewportAaProgram056,"res"),(float)viewW,(float)viewH);glUniform2f(glGetUniformLocation(g_viewportAaProgram056,"origin"),(float)viewX,(float)(winH-viewY-viewH));
    glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
