// Pixel-width boundary quads guarantee a halo even where core-profile GL
// implementations clamp glLineWidth to one pixel. Geometry stays unchanged.
static void drawSelectedFaceGlow(){
    if(g_selectionFaceBoundary.empty())return;static GLuint vao=0,vbo=0;
    if(!vao){glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);}
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pSketch);
    auto identity=Mat4::identity();float matrix[16];for(int i=0;i<16;++i)matrix[i]=(float)identity.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,matrix);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
    const float widths[]={17,11,6,2.4f},alpha[]={.055f,.10f,.22f,.97f};
    for(int pass=0;pass<4;++pass){std::vector<float> triangles;
        auto put=[&](float x,float y){triangles.insert(triangles.end(),{2*(x-viewX)/viewW-1,1-2*(y-viewY)/viewH,0});};
        for(size_t at=0;at+5<g_selectionFaceBoundary.size();at+=6){float ax,ay,bx,by;if(!projectScreen({g_selectionFaceBoundary[at],g_selectionFaceBoundary[at+1],g_selectionFaceBoundary[at+2]},ax,ay)||!projectScreen({g_selectionFaceBoundary[at+3],g_selectionFaceBoundary[at+4],g_selectionFaceBoundary[at+5]},bx,by))continue;float dx=bx-ax,dy=by-ay,length=std::hypot(dx,dy);if(length<.01f)continue;float nx=-dy/length*widths[pass]*.5f,ny=dx/length*widths[pass]*.5f;put(ax+nx,ay+ny);put(ax-nx,ay-ny);put(bx+nx,by+ny);put(bx+nx,by+ny);put(ax-nx,ay-ny);put(bx-nx,by-ny);}
        glUniform4f(glGetUniformLocation(pSketch,"tint"),1,.69f,.25f,alpha[pass]);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(triangles.size()*sizeof(float)),triangles.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,(GLsizei)triangles.size()/3);
    }glViewport(0,0,winW,winH);
}
