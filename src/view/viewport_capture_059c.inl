// A.02/A.04: GPU capture owns its framebuffer and never restores/shows/resizes
// the user's window. The same scene pass is used by interactive drawFrame.
static Image captureGpuScene059(){
    if(!wglGetCurrentContext()||!glGenFramebuffers||!glBindFramebuffer||!glFramebufferTexture2D||!glCheckFramebufferStatus||!glDeleteFramebuffers)
        throw std::runtime_error("Studio GPU snapshot requires the current OpenGL context and framebuffer support");
    const int sourceWidth=viewW>1&&viewH>1?viewW:g_lastSceneWidth059;
    const int sourceHeight=viewW>1&&viewH>1?viewH:g_lastSceneHeight059;
    if(sourceWidth<2||sourceHeight<2)throw std::runtime_error("Studio GPU snapshot has no valid scene dimensions");
    GLint maxTexture=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);
    const int sideLimit=std::min(maxTexture,4096);
    if(sideLimit<2)throw std::runtime_error("Studio GPU snapshot texture capacity is unavailable");
    // Bound GPU allocation independently from the window size. Image delivery
    // keeps the existing 1024x640 maximum; large monitors cannot request an
    // unbounded allocation by supplying extreme window dimensions.
    const double renderScale=std::min({1.,(double)sideLimit/sourceWidth,(double)sideLimit/sourceHeight,
                                      std::sqrt(4194304./((double)sourceWidth*sourceHeight))});
    const int width=std::max(2,(int)std::floor(sourceWidth*renderScale));
    const int height=std::max(2,(int)std::floor(sourceHeight*renderScale));
    struct CaptureScope059 {
        int oldWinW=winW,oldWinH=winH,oldX=viewX,oldY=viewY,oldW=viewW,oldH=viewH;
        GLuint oldTarget=g_viewportOutputFramebuffer059,fbo=0,color=0,depth=0;
        GLint readFbo=0,drawFbo=0,readBuffer=0,drawBuffer=0,viewport[4]{},packAlignment=4,packRowLength=0,packSkipRows=0,packSkipPixels=0,packBuffer=0;
        GLboolean depthMask=GL_TRUE,depthEnabled=GL_FALSE,blendEnabled=GL_FALSE,scissorEnabled=GL_FALSE;
        bool oldEmission=g_emissionPass;double oldProjectionFar=g_projectionFar;
        CaptureScope059(){
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);
            glGetIntegerv(GL_READ_BUFFER,&readBuffer);glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer);glGetIntegerv(GL_VIEWPORT,viewport);
            glGetIntegerv(GL_PACK_ALIGNMENT,&packAlignment);glGetIntegerv(GL_PACK_ROW_LENGTH,&packRowLength);
            glGetIntegerv(GL_PACK_SKIP_ROWS,&packSkipRows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&packSkipPixels);
            glGetIntegerv(0x88ED,&packBuffer); // GL_PIXEL_PACK_BUFFER_BINDING
            glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);depthEnabled=glIsEnabled(GL_DEPTH_TEST);blendEnabled=glIsEnabled(GL_BLEND);scissorEnabled=glIsEnabled(GL_SCISSOR_TEST);
        }
        ~CaptureScope059(){
            g_viewportOutputFramebuffer059=oldTarget;
            glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)readFbo);glReadBuffer((GLenum)readBuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)drawFbo);glDrawBuffer((GLenum)drawBuffer);
            if(fbo)glDeleteFramebuffers(1,&fbo);if(color)glDeleteTextures(1,&color);if(depth)glDeleteTextures(1,&depth);
            glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glPixelStorei(GL_PACK_ALIGNMENT,packAlignment);
            glPixelStorei(GL_PACK_ROW_LENGTH,packRowLength);glPixelStorei(GL_PACK_SKIP_ROWS,packSkipRows);glPixelStorei(GL_PACK_SKIP_PIXELS,packSkipPixels);
            glBindBuffer(0x88EB,(GLuint)packBuffer); // GL_PIXEL_PACK_BUFFER
            glDepthMask(depthMask);if(depthEnabled)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
            if(blendEnabled)glEnable(GL_BLEND);else glDisable(GL_BLEND);if(scissorEnabled)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
            winW=oldWinW;winH=oldWinH;viewX=oldX;viewY=oldY;viewW=oldW;viewH=oldH;
            g_emissionPass=oldEmission;g_projectionFar=oldProjectionFar;
        }
    } scope;
    for(int i=0;i<16;++i)if(glGetError()==GL_NO_ERROR)break;
    auto texture=[&](GLuint& id,GLint internal,GLenum format,GLenum type){
        glGenTextures(1,&id);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,id);
        glTexImage2D(GL_TEXTURE_2D,0,internal,width,height,0,format,type,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    };
    texture(scope.color,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE);texture(scope.depth,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_FLOAT);
    glGenFramebuffers(1,&scope.fbo);glBindFramebuffer(GL_FRAMEBUFFER,scope.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,scope.color,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,scope.depth,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    if(!scope.fbo||!scope.color||!scope.depth||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Studio GPU snapshot framebuffer is incomplete");
    g_viewportOutputFramebuffer059=scope.fbo;
    winW=viewW=width;winH=viewH=height;viewX=viewY=0;
    drawViewportScene059(false);glFinish();
    if(GLenum error=glGetError();error!=GL_NO_ERROR)throw std::runtime_error("Studio offscreen GPU render failed (OpenGL "+std::to_string((unsigned)error)+")");
    bindViewportOutput059();glReadBuffer(GL_COLOR_ATTACHMENT0);glBindBuffer(0x88EB,0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    std::vector<u8> pixels((size_t)width*height*3);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    if(GLenum error=glGetError();error!=GL_NO_ERROR)throw std::runtime_error("Studio offscreen GPU readback failed (OpenGL "+std::to_string((unsigned)error)+")");
    const double scale=std::min({1.,1024./width,640./height});Image image;
    image.w=std::max(1,(int)std::floor(width*scale));image.h=std::max(1,(int)std::floor(height*scale));image.px.resize((size_t)image.w*image.h*3);
    for(int y=0;y<image.h;++y)for(int x=0;x<image.w;++x){
        const int sx=std::min(width-1,(int)(x/scale)),sy=std::min(height-1,(int)(y/scale));
        std::copy_n(pixels.begin()+((size_t)(height-1-sy)*width+sx)*3,3,image.px.begin()+((size_t)y*image.w+x)*3);
    }
    if(scope.oldW>1&&scope.oldH>1){g_lastSceneWidth059=scope.oldW;g_lastSceneHeight059=scope.oldH;}
    return image;
}
