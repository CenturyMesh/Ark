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
    g_navigationLabelProgram=mkProg(VS_UI,R"(#version 330 core
in vec2 uv;out vec4 o;uniform sampler2D atlas;uniform vec4 tile;
void main(){o=vec4(.035,.075,.12,texture(atlas,tile.xy+uv*tile.zw).r);})");
}
static void paintNavigationLabels(const Mat4& vp,Vec3 direction,int factor){
    buildNavigationFont();g_navigationLabelBounds={};glUseProgram(g_navigationLabelProgram);glUniform2f(glGetUniformLocation(g_navigationLabelProgram,"vp"),(float)g_cubeS*factor,(float)g_cubeS*factor);glUniform1i(glGetUniformLocation(g_navigationLabelProgram,"atlas"),0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g_navigationLabelTexture);glBindVertexArray(quadVao);
    for(int face=0;face<6;++face){const auto& f=g_navigationFacets[(size_t)face];if(f.normal.dot(direction)<.32)continue;
        Vec3 clip=vp.transformPoint(f.center);float x=(float)((clip.x+1)*.5*g_cubeS),y=(float)((1-clip.y)*.5*g_cubeS);const auto& label=g_navigationLabels[face];
        float scale=11.8f/64.f*std::clamp(g_cubeS/136.f,.72f,2.1f),w=label.width*scale,h=label.height*scale;
        // Screen-aligned words stay legible under roll, but all four corners must
        // remain inside the corresponding rendered cardinal facet.
        auto fits=[&]{for(float dx:{-.5f,.5f})for(float dy:{-.40f,.40f}){auto hit=cubeHitAt(g_cubeX+(int)std::lround(x+dx*w),g_cubeY+(int)std::lround(y+dy*h));if(hit.face!=face||!hit.hit)return false;}return true;};
        while(w>8&&!fits()){scale*=.92f;w=label.width*scale;h=label.height*scale;}
        if(scale*64<7.5f||!fits())continue;
        g_navigationLabelBounds[face]={(LONG)std::floor(g_cubeX+x-w*.5f),(LONG)std::floor(g_cubeY+y-h*.5f),(LONG)std::ceil(g_cubeX+x+w*.5f),(LONG)std::ceil(g_cubeY+y+h*.5f)};
        glUniform4f(glGetUniformLocation(g_navigationLabelProgram,"tile"),label.x/1024.f,label.y/256.f,label.width/1024.f,label.height/256.f);glUniform4f(glGetUniformLocation(g_navigationLabelProgram,"rc"),(x-w*.5f)*factor,(y-h*.5f)*factor,w*factor,h*factor);glDrawArrays(GL_TRIANGLES,0,6);
    }
}
