#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
static void drawAgentPresence030(bool resolve=true){
    GLint priorProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&priorProgram);
    if(!g_showAgentPresence||g_agentPresence.empty())return;
    GLint priorVao=0,priorBuffer=0,priorViewport[4]{};glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&priorVao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&priorBuffer);glGetIntegerv(GL_VIEWPORT,priorViewport);
    static GLuint vao=0,vbo=0;if(!vao){glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);}
    const float colors[8][3]={{.22f,.86f,1.f},{.80f,.53f,1.f},{.42f,1.f,.65f},{1.f,.66f,.30f},{1.f,.48f,.66f},{.45f,.67f,1.f},{.91f,.90f,.40f},{.40f,1.f,.91f}};
    const auto camera=cameraProjection()*cameraView();unsigned slot=0,unlocated=0;std::vector<std::array<float,4>> labels;
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(auto& item:g_agentPresence){auto& presence=item.second;const auto age=GetTickCount64()-presence.updated;if(age>15000)continue;
        Vec3 point{},toolDirection=presence.direction;bool located=false;std::string target;
        for(size_t i=0;resolve&&!located&&i<g_bodyIds.size();++i)if(displayInstance(i)){
            const auto* feature=g_doc.find(g_bodyIds[i]);if(!feature)continue;
            const bool instanceTarget=!presence.instances.empty();const bool matches=instanceTarget?std::find(presence.instances.begin(),presence.instances.end(),g_instanceIds[i])!=presence.instances.end()&&(presence.copy<0||presence.copy==g_instanceCopies[i]):std::find(presence.ids.begin(),presence.ids.end(),feature->id)!=presence.ids.end();
            if(matches){
                point=presence.hasPoint?g_instanceWorld[i].transformPoint(presence.point):g_reports[g_instancePart[i]].bounds.transformed(g_instanceWorld[i]).center();toolDirection=g_instanceWorld[i].transformDir(presence.direction);target=feature->name;if(instanceTarget)for(const auto& node:g_doc.assembly)if(node.id==g_instanceIds[i]){target=node.name+" / "+feature->name;break;}located=true;
            }
        }
        if(resolve&&!located)for(const auto& name:presence.sketches)if(const auto view=g_committedRoleSketches.find(name);view!=g_committedRoleSketches.end()&&view->second.lines.size()>=3){point={view->second.lines[0],view->second.lines[1],view->second.lines[2]};located=true;target=name;break;}
        if(!located)for(const auto& draft:g_draftViews)if(draft.second.role==item.first&&draft.second.lines.size()>=3){point={draft.second.lines[0],draft.second.lines[1],draft.second.lines[2]};located=true;target="uncommitted sketch";break;}
        if(resolve){presence.cachedPoint=point;presence.cachedDirection=toolDirection;presence.cachedTarget=target;presence.cachedLocated=located;}else if(!located){point=presence.cachedPoint;toolDirection=presence.cachedDirection;target=presence.cachedTarget;located=presence.cachedLocated;}
        float x=std::max(18.f,(float)viewW-340.f),y=74+unlocated*38;double w=0;Vec3 clip;
        if(located){w=camera.m[3]*point.x+camera.m[7]*point.y+camera.m[11]*point.z+camera.m[15];if(w<=1e-6)continue;clip=camera.transformPoint(point);x=(float)((clip.x/w+1)*viewW*.5);y=(float)((1-clip.y/w)*viewH*.5);if(x<8||x>viewW-8||y<8||y>viewH-8)continue;}
        else {target="no spatial target";++unlocated;}
        size_t color=0;for(size_t i=0;i<teamCapacity;++i)if(item.first==g_teamRoleIds[i])color=i;const float* tint=colors[color];
        const float alpha=age<10000?1.f:(15000-age)/5000.f;std::vector<float> lines;auto put=[&](float px,float py){lines.insert(lines.end(),{px*2/viewW-1,1-py*2/viewH,0});};
        if(located){const float r=8+(presence.phase=="applying"?2*(float)std::sin(GetTickCount64()*.008):0);for(int side=0;side<4;++side){float a=side*1.570796327f,b=(side+1)*1.570796327f;put(x+std::cos(a)*r,y+std::sin(a)*r);put(x+std::cos(b)*r,y+std::sin(b)*r);}put(x-12,y);put(x+12,y);put(x,y-12);put(x,y+12);
            if(presence.hasDirection&&toolDirection.len()>1e-9){const auto endpoint=point+toolDirection.normalized()*std::max(1.,dist*.04);const double ew=camera.m[3]*endpoint.x+camera.m[7]*endpoint.y+camera.m[11]*endpoint.z+camera.m[15];if(ew>1e-6){auto end=camera.transformPoint(endpoint);put(x,y);put((float)((end.x/ew+1)*viewW*.5),(float)((1-end.y/ew)*viewH*.5));}}
        }
        glUseProgram(pSketch);auto identity=Mat4::identity();float matrix[16];for(int i=0;i<16;++i)matrix[i]=(float)identity.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,matrix);glUniform4f(glGetUniformLocation(pSketch,"tint"),presence.error?1.f:tint[0],presence.error?.32f:tint[1],presence.error?.32f:tint[2],alpha);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(lines.size()*sizeof(float)),lines.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_LINES,0,(GLsizei)lines.size()/3);
        glViewport(0,0,winW,winH);std::string label=item.first+" / "+presence.tool+" / "+presence.phase;std::string detail=target+" / r"+std::to_string(presence.revision);while(!label.empty()&&textWidth(label,.62f)>std::min(340.f,(float)viewW-24))label.pop_back();
        float lx=std::clamp(viewX+x+16.f,(float)viewX+12,std::max((float)viewX+12,viewX+viewW-textWidth(label,.62f)-12)),ly=std::clamp(viewY+y-18.f,(float)viewY+12,(float)viewY+viewH-40);ly+=located?(slot++%3)*20.f:0;
        const float labelWidth=std::max(textWidth(label,.62f),textWidth(detail.substr(0,48),.53f))+12;lx=std::clamp(lx,(float)viewX+12,std::max((float)viewX+12,viewX+viewW-labelWidth-12));
        for(int attempt=0;attempt<8;++attempt){bool overlap=false;for(const auto& box:labels)if(lx<box[0]+box[2]&&lx+labelWidth>box[0]&&ly<box[1]+box[3]&&ly+35>box[1])overlap=true;if(!overlap)break;ly=ly+38<viewY+viewH-40?ly+38:std::max((float)viewY+12,ly-76);}
        labels.push_back({lx,ly,labelWidth,35});glViewport(viewX,winH-viewY-viewH,viewW,viewH);std::vector<float> backing;auto vertex=[&](float px,float py){backing.insert(backing.end(),{(px-viewX)*2/viewW-1,1-(py-viewY)*2/viewH,0});};
        const float left=lx-5,right=std::min((float)(viewX+viewW-5),lx+labelWidth),top=ly-4,bottom=ly+33;vertex(left,top);vertex(right,top);vertex(left,bottom);vertex(left,bottom);vertex(right,top);vertex(right,bottom);glUniform4f(glGetUniformLocation(pSketch,"tint"),.035f,.055f,.073f,.88f*alpha);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(backing.size()*sizeof(float)),backing.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,6);glViewport(0,0,winW,winH);
        ::textOut(label,lx,ly,.62f,tint[0],tint[1],tint[2]);::textOut(detail.substr(0,48),lx,ly+18,.53f,.77f,.83f,.88f);glViewport(viewX,winH-viewY-viewH,viewW,viewH);
    }
    // Busy-frame controls continue drawing with their caller-owned quad VAO.
    // In particular an unlocated agent must not leave an empty marker VBO bound.
    glBindVertexArray((GLuint)priorVao);glBindBuffer(GL_ARRAY_BUFFER,(GLuint)priorBuffer);glViewport(priorViewport[0],priorViewport[1],priorViewport[2],priorViewport[3]);glUseProgram((GLuint)priorProgram);
}
