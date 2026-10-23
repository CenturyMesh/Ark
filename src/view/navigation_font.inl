// Six word tiles are rasterized once at 64 px; no GDI/font/bitmap work in paint.
struct NavigationLabel {int x=0,y=0,width=0,height=0;};
static GLuint g_navigationLabelTexture=0,g_navigationLabelProgram=0;
static std::array<NavigationLabel,6> g_navigationLabels;
static std::array<RECT,6> g_navigationLabelBounds{};
static unsigned g_navigationFontBuilds=0;
static void buildNavigationFont(){
    if(g_navigationLabelTexture)return;++g_navigationFontBuilds;
    constexpr int width=1024,height=256;HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto oldBitmap=SelectObject(dc,bitmap);
    HFONT font=CreateFontW(-64,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);
    PatBlt(dc,0,0,width,height,BLACKNESS);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    static const wchar_t* names[]={L"RIGHT",L"LEFT",L"TOP",L"BOTTOM",L"FRONT",L"BACK"};TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
    for(int face=0;face<6;++face){SIZE size{};int count=lstrlenW(names[face]);GetTextExtentPoint32W(dc,names[face],count,&size);auto& label=g_navigationLabels[face];label={face%3*340+4,face/3*128+4,(int)size.cx,(int)metrics.tmHeight};TextOutW(dc,label.x,label.y,names[face],count);}
    GdiFlush();std::vector<u8> pixels((size_t)width*height);for(size_t i=0;i<pixels.size();++i)pixels[i]=((const u8*)bits)[i*4+1];
    SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);
    glGenTextures(1,&g_navigationLabelTexture);glBindTexture(GL_TEXTURE_2D,g_navigationLabelTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_R8,width,height,0,GL_RED,GL_UNSIGNED_BYTE,pixels.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    g_navigationLabelProgram=mkProg(R"(#version 330 core
layout(location=0) in vec2 P;uniform mat4 vp;uniform vec3 center,right,up;uniform vec2 size;out vec2 uv;
void main(){uv=(P+1.)*.5;gl_Position=vp*vec4(center+right*(uv.x-.5)*size.x+up*(.5-uv.y)*size.y,1.);})",R"(#version 330 core
in vec2 uv;out vec4 o;uniform sampler2D atlas;uniform vec4 tile;
void main(){o=vec4(.035,.075,.12,texture(atlas,tile.xy+uv*tile.zw).r);})");
}
static void paintNavigationLabels(const Mat4& vp,Vec3 direction,int){
    buildNavigationFont();g_navigationLabelBounds={};glUseProgram(g_navigationLabelProgram);
    GLfloat matrix[16];for(int i=0;i<16;++i)matrix[i]=(GLfloat)vp.m[i];glUniformMatrix4fv(glGetUniformLocation(g_navigationLabelProgram,"vp"),1,GL_FALSE,matrix);
    glUniform1i(glGetUniformLocation(g_navigationLabelProgram,"atlas"),0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g_navigationLabelTexture);glBindVertexArray(quadVao);
    // The named views use +Y for Top and +Z for Front. Labels are attached to
    // these exact planes, including orbit/roll; no screen-space label can drift
    // onto a different face. Local bases match the named-view camera frames.
    static const Vec3 rights[]={{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
    static const Vec3 ups[]={{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
    for(int face=0;face<6;++face){const auto& f=g_navigationFacets[(size_t)face];if(f.normal.dot(direction)<.26)continue;
        const auto& label=g_navigationLabels[face];const Vec3 right=rights[face],up=ups[face];
        const float height=.31f,width=std::min((float)kNavigationFaceHalf*1.78f,height*label.width/label.height);
        float left=(float)g_cubeS,top=(float)g_cubeS,rightEdge=0,bottom=0;
        for(float dx:{-.5f,.5f})for(float dy:{-.5f,.5f}){Vec3 clip=vp.transformPoint(f.center+right*(dx*width)+up*(dy*height));float x=(float)((clip.x+1)*.5*g_cubeS),y=(float)((1-clip.y)*.5*g_cubeS);left=std::min(left,x);top=std::min(top,y);rightEdge=std::max(rightEdge,x);bottom=std::max(bottom,y);}
        g_navigationLabelBounds[face]={(LONG)std::floor(g_cubeX+left),(LONG)std::floor(g_cubeY+top),(LONG)std::ceil(g_cubeX+rightEdge),(LONG)std::ceil(g_cubeY+bottom)};
        auto vector=[&](const char* name,Vec3 value){glUniform3f(glGetUniformLocation(g_navigationLabelProgram,name),(float)value.x,(float)value.y,(float)value.z);};
        vector("center",f.center);vector("right",right);vector("up",up);glUniform2f(glGetUniformLocation(g_navigationLabelProgram,"size"),width,height);
        glUniform4f(glGetUniformLocation(g_navigationLabelProgram,"tile"),label.x/1024.f,label.y/256.f,label.width/1024.f,label.height/256.f);glDrawArrays(GL_TRIANGLES,0,6);
    }
}
