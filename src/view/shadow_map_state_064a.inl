// Display-only key-light visibility. Native solids remain authoritative.
static GLuint g_keyShadowFbo064=0,g_keyShadowDepth064=0;
static Mat4 g_keyShadowMatrix064;
static float g_keyShadowTexel064=0;
static bool g_keyShadowReady064=false;
static constexpr int kKeyShadowSize064=2048;
static void bindKeyShadow064(){
    glUniform1f(glGetUniformLocation(pSolid,"keyShadowStrength"),g_keyShadowReady064?g_contactShadowStrength:0.f);
    GLfloat matrix[16];for(int k=0;k<16;++k)matrix[k]=(GLfloat)g_keyShadowMatrix064.m[k];
    glUniformMatrix4fv(glGetUniformLocation(pSolid,"keyShadowMatrix"),1,GL_FALSE,matrix);
    glUniform1f(glGetUniformLocation(pSolid,"keyShadowTexelWorld"),g_keyShadowTexel064);
    glUniform1i(glGetUniformLocation(pSolid,"keyShadowMap"),3);
    glActiveTexture(GL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,g_keyShadowDepth064);glActiveTexture(GL_TEXTURE0);
}
