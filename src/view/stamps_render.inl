// Embedded pixels stay in the document. GPU cache keys are validated content hashes.
struct StampDisplayState024 {
    GLboolean depthWrite=GL_TRUE;GLint blendSource=GL_SRC_ALPHA,blendTarget=GL_ONE_MINUS_SRC_ALPHA,cullMode=GL_BACK;
    GLfloat offsetFactor=0,offsetUnits=0;bool blending=false,polygonOffset=false,culling=false,depthTest=false;
    StampDisplayState024(){glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);glGetIntegerv(GL_BLEND_SRC,&blendSource);glGetIntegerv(GL_BLEND_DST,&blendTarget);glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&offsetFactor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&offsetUnits);blending=glIsEnabled(GL_BLEND)!=0;polygonOffset=glIsEnabled(GL_POLYGON_OFFSET_FILL)!=0;culling=glIsEnabled(GL_CULL_FACE)!=0;depthTest=glIsEnabled(GL_DEPTH_TEST)!=0;}
    ~StampDisplayState024(){glDepthMask(depthWrite);glBlendFunc((GLenum)blendSource,(GLenum)blendTarget);if(blending)glEnable(GL_BLEND);else glDisable(GL_BLEND);glPolygonOffset(offsetFactor,offsetUnits);if(polygonOffset)glEnable(GL_POLYGON_OFFSET_FILL);else glDisable(GL_POLYGON_OFFSET_FILL);glCullFace((GLenum)cullMode);if(culling)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);if(depthTest)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);}
};
static GLuint stampAssetTexture(const std::string& name){
    struct Texture {std::string hash;GLuint id=0;};static std::vector<Texture> textures;
    for(auto it=textures.begin();it!=textures.end();)if(std::none_of(g_doc.imageAssets.begin(),g_doc.imageAssets.end(),[&](const auto& a){return a.sha256==it->hash;})){glDeleteTextures(1,&it->id);it=textures.erase(it);}else ++it;
    const auto* asset=findImageAsset(g_doc,name);if(!asset)return 0;
    for(const auto& texture:textures)if(texture.hash==asset->sha256)return texture.id;
    auto pixels=decodeImageAssetRGBA(*asset);
    // OpenGL bottom-left image coordinates; source PNG/JPEG rows are top-down.
    const size_t row=(size_t)asset->width*4;for(size_t y=0;y<asset->height/2;++y)for(size_t x=0;x<row;++x)std::swap(pixels[y*row+x],pixels[((size_t)asset->height-1-y)*row+x]);
    GLuint id=0;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)asset->width,(GLsizei)asset->height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);textures.push_back({asset->sha256,id});return id;
}
static void setSurfaceAppearance024(const Appearance& a){
    glUniform3f(suColor,(float)a.baseColor.x,(float)a.baseColor.y,(float)a.baseColor.z);
    glUniform1f(glGetUniformLocation(pSolid,"metallic"),(float)a.metallic);glUniform1f(glGetUniformLocation(pSolid,"roughness"),(float)a.roughness);glUniform1f(glGetUniformLocation(pSolid,"opacity"),(float)a.opacity);
    glUniform2f(glGetUniformLocation(pSolid,"textureScale"),(float)(a.textureScaleU/100.),(float)(a.textureScaleV/100.));
    GLuint texture=a.textureAsset.empty()?materialTexture(a.texturePath):stampAssetTexture(a.textureAsset);glUniform1f(glGetUniformLocation(pSolid,"hasTexture"),texture?1.f:0.f);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE0);
}
static bool drawFaceAppearances024(size_t instance,const Feature* feature){
    if(!feature||feature->faceAppearances.empty())return false;
    const auto part=g_instancePart[instance];if(part>=g_partPicks.size())return false;const auto& pick=g_partPicks[part];if(pick.faces.empty())return false;
    for(size_t face=0;face<pick.faces.size();++face){auto look=resolvedAppearance(g_doc,*feature);
        for(const auto& region:feature->faceAppearances)if(faceReferenceMatches(pick,face,region.face)){look=resolvedFaceAppearance(g_doc,*feature,region.face);break;}
        setSurfaceAppearance024(look);const auto span=pick.faces[face];if(span.second>0)glDrawElements(GL_TRIANGLES,span.second*3,GL_UNSIGNED_INT,(void*)((size_t)span.first*3*sizeof(unsigned)));
    }
    setSurfaceAppearance024(resolvedAppearance(g_doc,*feature));return true;
}
static void drawBodyStamps024(size_t instance,const Feature* feature){
    if(!feature||g_doc.stamps.empty())return;const auto part=g_instancePart[instance];if(part>=g_partPicks.size())return;
    const auto& pick=g_partPicks[part];bool changed=false;StampDisplayState024 priorState;
    for(const auto& stamp:g_doc.stamps){if(stamp.feature!=feature->id)continue;
        const auto projector=resolvedStampProjector(g_doc,stamp);const GLuint texture=stampAssetTexture(stamp.asset);if(!texture)continue;
        changed=true;glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);glPolygonOffset(-1.f,-1.f);
        glUniform1f(glGetUniformLocation(pSolid,"stampMode"),1.f);glUniform1f(glGetUniformLocation(pSolid,"stampFrontOnly"),projector.frontOnly?1.f:0.f);glUniform1f(glGetUniformLocation(pSolid,"stampDepth"),(float)projector.depth);
        auto vector=[&](const char* name,Vec3 v){glUniform3f(glGetUniformLocation(pSolid,name),(float)v.x,(float)v.y,(float)v.z);};vector("stampOrigin",projector.origin);vector("stampU",projector.uAxis);vector("stampV",projector.vAxis);vector("stampNormal",projector.normal);
        glUniform2f(glGetUniformLocation(pSolid,"stampSize"),(float)projector.width,(float)projector.height);glUniform3f(suColor,1,1,1);glUniform1f(glGetUniformLocation(pSolid,"opacity"),(float)(projector.opacity*resolvedAppearance(g_doc,*feature).opacity));
        glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE0);
        std::set<size_t> drawn;for(const auto& name:stamp.faces)for(size_t face=0;face<pick.faces.size();++face)if(faceReferenceMatches(pick,face,name)&&drawn.insert(face).second){auto look=resolvedAppearance(g_doc,*feature);for(const auto& region:feature->faceAppearances)if(faceReferenceMatches(pick,face,region.face)){look=resolvedFaceAppearance(g_doc,*feature,region.face);break;}setSurfaceAppearance024(look);glUniform3f(suColor,1,1,1);glUniform1f(glGetUniformLocation(pSolid,"opacity"),(float)(projector.opacity*look.opacity));glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE0);const auto span=pick.faces[face];if(span.second>0)glDrawElements(GL_TRIANGLES,span.second*3,GL_UNSIGNED_INT,(void*)((size_t)span.first*3*sizeof(unsigned)));}
    }
    if(changed){glUniform1f(glGetUniformLocation(pSolid,"stampMode"),0);setSurfaceAppearance024(resolvedAppearance(g_doc,*feature));}
}
static void drawPlaneStamps024(const GLfloat* vp){
    if(g_doc.stamps.empty()||!pCanvas)return;
    StampDisplayState024 priorState;
    glUseProgram(pCanvas);glUniformMatrix4fv(glGetUniformLocation(pCanvas,"vp"),1,GL_FALSE,vp);glUniform1i(glGetUniformLocation(pCanvas,"img"),0);glUniform1f(glGetUniformLocation(pCanvas,"live"),1);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1.f,-1.f);glActiveTexture(GL_TEXTURE0);glBindVertexArray(canvasVao);
    for(const auto& stamp:g_doc.stamps){if(stamp.feature)continue;const auto p=resolvedStampProjector(g_doc,stamp);GLuint texture=stampAssetTexture(stamp.asset);if(!texture)continue;
        if(p.frontOnly){glEnable(GL_CULL_FACE);glCullFace(GL_BACK);}else glDisable(GL_CULL_FACE);
        Mat4 model=Mat4::identity();const Vec3 origin=p.origin-p.uAxis*(p.width*.5)-p.vAxis*(p.height*.5);for(int axis=0;axis<3;++axis){const Vec3 v=axis==0?p.uAxis*p.width:axis==1?p.vAxis*p.height:p.normal;model.m[axis*4]=v.x;model.m[axis*4+1]=v.y;model.m[axis*4+2]=v.z;}model.m[12]=origin.x;model.m[13]=origin.y;model.m[14]=origin.z;
        GLfloat matrix[16];for(int i=0;i<16;++i)matrix[i]=(GLfloat)model.m[i];glUniformMatrix4fv(glGetUniformLocation(pCanvas,"model"),1,GL_FALSE,matrix);glUniform1f(glGetUniformLocation(pCanvas,"opacity"),(float)p.opacity);glBindTexture(GL_TEXTURE_2D,texture);glDrawArrays(GL_TRIANGLES,0,6);
    }
    glBindTexture(GL_TEXTURE_2D,0);
}
