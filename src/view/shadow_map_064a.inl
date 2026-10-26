static void renderKeyShadow064(){
    g_keyShadowReady064=false;
    if(g_contactShadowStrength<=0||wireframe||!g_sceneBounds.valid())return;
    if(!g_keyShadowFbo064){
        glGenFramebuffers(1,&g_keyShadowFbo064);glGenTextures(1,&g_keyShadowDepth064);
        glBindTexture(GL_TEXTURE_2D,g_keyShadowDepth064);
        glTexImage2D(GL_TEXTURE_2D,0,0x8CAC /* DEPTH_COMPONENT32F */,kKeyShadowSize064,kKeyShadowSize064,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,0x884C /* TEXTURE_COMPARE_MODE */,0x884E /* COMPARE_REF_TO_TEXTURE */);
        glTexParameteri(GL_TEXTURE_2D,0x884D /* TEXTURE_COMPARE_FUNC */,GL_LEQUAL);
        glBindFramebuffer(GL_FRAMEBUFFER,g_keyShadowFbo064);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,g_keyShadowDepth064,0);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){bindViewportOutput059();return;}
    }
    auto basis=cameraFrame(yaw,pitch,cameraRoll);const auto light=(basis.right*-.55+basis.up*.72+basis.outward*.42).normalized();
    const double radius=std::max(1.,g_sceneBounds.diagonal()*.53);const Vec3 center=g_sceneBounds.center();
    const Vec3 lightEye=center+light*(radius*2.);const Mat4 view=Mat4::lookAt(lightEye,center,basis.up);
    const Mat4 proj=Mat4::ortho(-radius,radius,-radius,radius,radius*.5,radius*3.5);
    g_keyShadowMatrix064=proj*view;g_keyShadowTexel064=(float)(2*radius/kKeyShadowSize064);
    glBindFramebuffer(GL_FRAMEBUFFER,g_keyShadowFbo064);glViewport(0,0,kKeyShadowSize064,kKeyShadowSize064);
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glClear(GL_DEPTH_BUFFER_BIT);
    GLfloat matrix[16];for(int k=0;k<16;++k)matrix[k]=(GLfloat)g_keyShadowMatrix064.m[k];
    // Reuse the opaque display geometry pass, including deformed tubes and
    // section meshes. Transparent objects receive shadows but do not cast an
    // opaque silhouette. No mesh upload or CPU ray scene is needed here.
    drawSurfaces(true,view,proj,matrix,lightEye);
    g_keyShadowReady064=true;
    bindViewportOutput059();glViewport(viewX,winH-viewY-viewH,viewW,viewH);glEnable(GL_BLEND);
}
