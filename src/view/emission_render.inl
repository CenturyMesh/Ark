// Emissive HDR mask includes the scene depth, so hidden emitters cannot leak.
// The bounded screen-space filter is appearance only; no photometric claim.
static void drawEmission030(const Mat4& view,const Mat4& proj,const GLfloat vpT[16],const Vec3& eye){
    if(wireframe||viewW<2||viewH<2)return;
    // Ordinary non-emissive designs do not scan appearances or allocate a pass
    // every frame. Material/feature edits invalidate this source-revision cache.
    static u64 sourceEpoch=~u64(0),sourceRevision=~u64(0);static bool anyEmission=false;
    if(sourceEpoch!=g_documentEpoch||sourceRevision!=g_session.revision){
        sourceEpoch=g_documentEpoch;sourceRevision=g_session.revision;anyEmission=false;
        for(const auto& feature:g_doc.features){if(resolvedAppearance(g_doc,feature).emissionIntensity>0)anyEmission=true;for(const auto& face:feature.faceAppearances)if(resolvedFaceAppearance(g_doc,feature,face.face).emissionIntensity>0)anyEmission=true;}
    }
    if(!anyEmission)return;
    bool enabled=false;double maxRadius=0;
    auto inspect=[&](const Appearance& a){if(a.emissionIntensity>0&&a.bloom>0&&a.glow>0){enabled=true;maxRadius=std::max(maxRadius,a.glow);}};
    for(size_t i=0;i<g_bodyIds.size();++i)if(displayInstance(i))if(const auto* feature=g_doc.find(g_bodyIds[i])){inspect(resolvedAppearance(g_doc,*feature));for(const auto& face:feature->faceAppearances)inspect(resolvedFaceAppearance(g_doc,*feature,face.face));}
    if(!enabled||!ensureOcclusionTargets(viewW,viewH))return;
    static GLuint texture=0,blurTexture=0,program=0;static int width=0,height=0;
    if(!program){const char* shader=R"(#version 330 core
out vec4 o;uniform sampler2D emission;uniform vec2 res,origin,axis;uniform float radius,composite;
void main(){vec2 uv=(gl_FragCoord.xy-origin)/res;vec3 sum=vec3(0.);float spread=0.;
 // Separable one-pixel Gaussian taps avoid sparse copies of narrow LED strands.
 for(int i=-64;i<=64;++i){float d=float(i);if(abs(d)>radius)continue;
  vec2 q=uv+axis*d/res;if(any(lessThan(q,vec2(0.)))||any(greaterThan(q,vec2(1.))))continue;
  vec4 source=texture(emission,q);float span=min(source.a*64.,64.);if(span<=0.||abs(d)>span)continue;
  float w=exp(-4.*d*d/(span*span))*1.128379/max(span,1.);sum+=source.rgb*w;spread=max(spread,span);}
 o=composite>.5?vec4(1.-exp(-sum),1.):vec4(sum,spread/64.);
})";program=mkProg(VS_BG,shader);if(!program)throw std::runtime_error("Light up composite shader failed");}
    if(width!=viewW||height!=viewH){for(auto* target:{&texture,&blurTexture}){if(*target)glDeleteTextures(1,target);glGenTextures(1,target);glBindTexture(GL_TEXTURE_2D,*target);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,viewW,viewH,0,GL_RGBA,GL_HALF_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}width=viewW;height=viewH;}
    glBindFramebuffer(GL_FRAMEBUFFER,gFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,gGeomTex,0);bindViewportOutput059();throw std::runtime_error("Light up framebuffer is incomplete");}
    glViewport(0,0,viewW,viewH);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    g_emissionPass=true;drawSurfaces(false,view,proj,vpT,eye);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glDepthMask(GL_FALSE);drawSurfaces(false,view,proj,vpT,eye,true);g_emissionPass=false;
    glUseProgram(pSolid);glUniform1f(glGetUniformLocation(pSolid,"emissionOnly"),0);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,blurTexture,0);
    glUseProgram(program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(glGetUniformLocation(program,"emission"),0);glUniform2f(glGetUniformLocation(program,"res"),(float)viewW,(float)viewH);glUniform2f(glGetUniformLocation(program,"origin"),0,0);glUniform2f(glGetUniformLocation(program,"axis"),1,0);glUniform1f(glGetUniformLocation(program,"composite"),0);glUniform1f(glGetUniformLocation(program,"radius"),(float)maxRadius);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,gGeomTex,0);bindViewportOutput059();glViewport(viewX,winH-viewY-viewH,viewW,viewH);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glBindTexture(GL_TEXTURE_2D,blurTexture);glUniform2f(glGetUniformLocation(program,"origin"),(float)viewX,(float)(winH-viewY-viewH));glUniform2f(glGetUniformLocation(program,"axis"),0,1);glUniform1f(glGetUniformLocation(program,"composite"),1);glDrawArrays(GL_TRIANGLES,0,3);
    glBindTexture(GL_TEXTURE_2D,0);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_DEPTH_TEST);
}
