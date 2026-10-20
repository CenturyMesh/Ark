// 044B Baked Motions panel: list, vector graph, timeline and transport.
// Rename, delete and bake use ordinary revision-checked doc_apply transactions.
static const float kBakedMotionColors[6][3]={{.24f,.80f,.94f},{.99f,.71f,.27f},{.86f,.48f,.95f},{.46f,.88f,.54f},{.96f,.46f,.42f},{.50f,.64f,1.f}};
static void bakedMotionsRestorePose(){if(g_formedInstanceWorld.size()==g_instanceWorld.size())g_instanceWorld=g_formedInstanceWorld;rebuildDisplayBounds();refreshSelectionLines();}
static void stopBakedMotion(bool restore){auto& p=g_bakedMotions;const bool shown=p.active;p.playing=p.active=p.scrubbing=false;p.time=0;p.shownFrame=~size_t(0);if(shown&&restore)bakedMotionsRestorePose();}
static void loadBakedMotionSelection(){
    auto& p=g_bakedMotions;p.cacheName=p.selected;p.framesValid=p.graphValid=false;p.curves.clear();p.handles.clear();p.seams.clear();p.lo.clear();p.hi.clear();p.rangeLo.clear();p.rangeHi.clear();p.display.clear();p.displayRevision=~u64(0);p.shownFrame=~size_t(0);p.graphError.clear();p.diagnostics={};
    const auto* motion=findMotion(g_doc,p.selected);if(!motion)return;std::string error;
    p.framesValid=decodeBakedMotion(*motion,p.frames,error);
    std::map<std::string,double> starts;for(const auto& row:p.rows)if(row.name==p.selected)starts=row.starts;
    if(const auto* bake=motion->find("bake"))if(const auto* channels=bake->find("channels"))for(const auto& c:channels->arr)if(const auto* start=c.find("graph_start"))starts[c.find("name")->str]=start->num;
    p.graphValid=compileMotionGraph(*motion->find("graph"),g_doc.buildEnv(),starts,p.graph,error);if(!p.graphValid){p.graphError=error;return;}
    try{p.diagnostics=motionDiagnostics(p.graph);}catch(const std::exception& e){p.graphError=e.what();}
    const size_t samples=180,channels=p.graph.channels.size();p.curves.assign(channels,{});p.lo.assign(channels,1e30f);p.hi.assign(channels,-1e30f);
    try{for(size_t i=0;i<samples;++i){const auto values=evaluateMotion(p.graph,p.graph.duration*(double)i/(samples-1));for(size_t c=0;c<channels;++c){p.curves[c].push_back((float)values[c]);p.lo[c]=std::min(p.lo[c],(float)values[c]);p.hi[c]=std::max(p.hi[c],(float)values[c]);}}}
    catch(const std::exception& e){p.graphError=e.what();p.curves.clear();return;}
    // Channels sharing a unit share one vertical scale, so coupled joints stay distinguishable.
    p.units.assign(channels,"");for(const auto& row:p.rows)if(row.name==p.selected&&row.units.size()==channels)p.units=row.units;if(p.framesValid&&p.frames.units.size()==channels)p.units=p.frames.units;
    std::map<std::string,std::pair<float,float>> groups;for(size_t c=0;c<channels;++c){const std::string key=p.units[c].empty()||p.units[c]=="signal"?"#"+std::to_string(c):p.units[c];auto found=groups.emplace(key,std::pair{p.lo[c],p.hi[c]});if(!found.second){found.first->second.first=std::min(found.first->second.first,p.lo[c]);found.first->second.second=std::max(found.first->second.second,p.hi[c]);}}
    for(size_t c=0;c<channels;++c){const auto range=groups.at(p.units[c].empty()||p.units[c]=="signal"?"#"+std::to_string(c):p.units[c]);p.lo[c]=range.first;p.hi[c]=range.second;p.rangeLo.push_back(range.first);p.rangeHi.push_back(range.second);if(p.hi[c]-p.lo[c]<1e-6f){p.lo[c]-=1;p.hi[c]+=1;}const float pad=(p.hi[c]-p.lo[c])*.08f;p.lo[c]-=pad;p.hi[c]+=pad;p.handles.push_back(motionBezierHandles(p.graph,c));p.seams.push_back(motionSeams(p.graph,c));}
}
static void refreshBakedMotions(){
    auto& p=g_bakedMotions;if(p.revision==g_session.revision&&p.epoch==g_documentEpoch&&p.cacheName==p.selected)return;
    using V=arkjson::Value;const bool documentChanged=p.revision!=g_session.revision||p.epoch!=g_documentEpoch;p.revision=g_session.revision;p.epoch=g_documentEpoch;
    if(documentChanged){p.rows.clear();p.digestKnown=false;p.note.clear();
        if(g_doc.motions.t==V::Arr&&!g_doc.motions.arr.empty()){
            AssemblyManipulationProgram program;std::string error;const bool solved=buildAssemblyManipulation(g_doc,program,error);if(solved){p.digest=motionSourceDigest(g_doc,program.baseline);p.digestKnown=true;}else p.note="Assembly solve failed: "+error;
            for(const auto& motion:g_doc.motions.arr){BakedMotionsRow row;row.name=motion.find("name")->str;const auto* bake=motion.find("bake");row.baked=bake!=nullptr;std::vector<MotionJointBinding> bindings;std::string reason;CompiledMotion compiled;
                bool valid=solved&&bindMotionJoints(program,g_doc,*motion.find("graph"),bindings,reason);if(valid){for(const auto& b:bindings){if(b.mate)row.starts[b.channel]=b.position;row.units.push_back(b.mate?b.unit:"signal");}valid=compileMotionGraph(*motion.find("graph"),g_doc.buildEnv(),row.starts,compiled,reason);}
                row.invalid=!valid;row.duration=bake?bake->find("duration_s")->num:valid?compiled.duration:0;const size_t channels=bake?bake->find("channels")->arr.size():motion.find("graph")->find("channels")->arr.size();
                char detail[160];
                if(bake){const bool assembly=p.digestKnown&&bake->find("source_digest")->str!=p.digest,graph=bake->find("graph_digest")->str!=motionGraphDigest(*motion.find("graph"));row.stale=assembly||graph||!valid;row.badge=row.stale?"STALE":"BAKED";
                    std::snprintf(detail,sizeof detail,"%.2f s  /  %zu ch  /  %.0f fps%s",row.duration,channels,bake->find("fps")->num,assembly?"  /  assembly changed":graph?"  /  graph changed":!valid?"  /  graph invalid":"");}
                else{row.badge=valid?"NOT BAKED":"INVALID";std::snprintf(detail,sizeof detail,valid?"%.2f s  /  %zu ch  /  graph only":"%.2f s  /  %zu ch  /  check joints",row.duration,channels);}
                row.detail=detail;if(!valid&&!reason.empty())row.detail+="  /  "+reason;p.rows.push_back(std::move(row));}
        }}
    if(p.rows.empty()){p.selected.clear();p.cacheName.clear();if(p.active)stopBakedMotion();return;}
    if(std::none_of(p.rows.begin(),p.rows.end(),[&](const BakedMotionsRow& row){return row.name==p.selected;})){if(p.active)stopBakedMotion();p.selected=p.rows.front().name;}
    const bool wasActive=p.active;const double time=p.time;loadBakedMotionSelection();if(wasActive){if(p.framesValid){p.time=std::min(time,p.frames.duration);}else stopBakedMotion();}
    p.scroll=std::clamp(p.scroll,0,std::max(0,(int)p.rows.size()-1));
}
static size_t bakedMotionFrame(){const auto& p=g_bakedMotions;if(!p.framesValid||!p.frames.frameCount)return 0;return std::min(p.frames.frameCount-1,(size_t)std::floor(p.time*p.frames.fps+1e-6));}
static void applyBakedMotionFrame(){
    auto& p=g_bakedMotions;if(!p.active||!p.framesValid||!p.frames.frameCount||g_formedInstanceWorld.size()!=g_instanceWorld.size())return;
    if(p.displayRevision!=g_session.revision||p.displayCount!=g_instanceIds.size()){p.display.assign(p.frames.nodes.size(),-1);for(size_t i=0;i<p.frames.nodes.size();++i)for(size_t j=0;j<g_instanceIds.size();++j)if(g_instanceIds[j]==p.frames.nodes[i]&&g_instanceCopies[j]==p.frames.copies[i]){p.display[i]=(int)j;break;}p.displayRevision=g_session.revision;p.displayCount=g_instanceIds.size();p.shownFrame=~size_t(0);}
    const size_t k=bakedMotionFrame();if(k==p.shownFrame)return;p.shownFrame=k;g_instanceWorld=g_formedInstanceWorld;
    for(size_t i=0;i<p.frames.nodes.size();++i)if(p.display[i]>=0)g_instanceWorld[(size_t)p.display[i]]=p.frames.poses[k*p.frames.nodes.size()+i];
    rebuildDisplayBounds();refreshSelectionLines();
}
static bool bakedMotionPlaybackAvailable(std::string& reason){
    if(g_allSheetsFlat||g_stockMode){reason="Return to the formed assembly before playing a baked motion";return false;}
    if(g_drawingViewport.active){reason="Return to the 3D design before playing a baked motion";return false;}
    if(g_componentMove.armed||g_componentMove.active){reason="Finish or cancel the component move first";return false;}
    refreshBakedMotions();if(!g_bakedMotions.framesValid){reason=g_bakedMotions.selected.empty()?"No baked motion selected":"Bake this motion first: Bake, or ask the agent for motion.bake";return false;}return true;
}
static void advanceBakedMotion(double seconds){
    auto& p=g_bakedMotions;if(!p.active)return;std::string reason;
    if(g_motionActive||!bakedMotionPlaybackAvailable(reason)){stopBakedMotion();return;}
    if(p.playing){p.time+=seconds*p.speed;const double duration=p.frames.duration;if(p.time>=duration){if(p.loop&&duration>0)p.time=std::fmod(p.time,duration);else{p.time=duration;p.playing=false;}}}
    applyBakedMotionFrame();
}
static void startBakedMotionDisplay(){auto& p=g_bakedMotions;if(g_motionActive){g_motionActive=g_motionPlaying=false;g_instanceWorld=g_formedInstanceWorld;}p.active=true;p.shownFrame=~size_t(0);applyBakedMotionFrame();}
static void scrubBakedMotion(double fraction){auto& p=g_bakedMotions;std::string reason;if(!bakedMotionPlaybackAvailable(reason)){g_status=reason;return;}p.playing=false;p.time=std::clamp(fraction,0.,1.)*p.frames.duration;startBakedMotionDisplay();}
static void stepBakedMotion(int frames){auto& p=g_bakedMotions;std::string reason;if(!bakedMotionPlaybackAvailable(reason)){g_status=reason;return;}p.playing=false;const long next=std::clamp<long>((long)bakedMotionFrame()+frames,0,(long)p.frames.frameCount-1);p.time=std::min(p.frames.duration,(double)next/p.frames.fps);startBakedMotionDisplay();}
static bool bakedMotionsWheel(POINT point,int delta){if(!g_bakedMotions.open||point.x<viewX+viewW||point.y<viewY||point.y>winH-32)return false;g_bakedMotions.scroll=std::clamp(g_bakedMotions.scroll+(delta>0?-1:1),0,std::max(0,(int)g_bakedMotions.rows.size()-1));return true;}
static bool bakedMotionsKey(WPARAM key){
    auto& p=g_bakedMotions;if(!p.open||p.selected.empty())return false;
    if(key==VK_ESCAPE&&p.active){stopBakedMotion();g_status="Baked motion stopped / solved pose restored";return true;}
    if(key==VK_SPACE){uiAction(45001);return true;}if(key==VK_HOME){stepBakedMotion(-1000000);return true;}if(key==VK_END){stepBakedMotion(1000000);return true;}
    if(key==VK_LEFT||key==VK_RIGHT){stepBakedMotion(key==VK_LEFT?-1:1);return true;}return false;
}
static bool bakedMotionsAction(int id){
    if(id<45000||id>=45200)return false;using V=arkjson::Value;auto& p=g_bakedMotions;
    auto motionCommand=[&](const char* op){V c=V::makeObj();c.set("op",V::makeStr(op));c.set("name",V::makeStr(p.selected));return c;};
    if(id==45000){p.open=!p.open;if(!p.open)stopBakedMotion();else{p.revision=~u64(0);refreshBakedMotions();}layout();g_status=p.open?"Baked Motions / play agent-baked joint motion; display only":"Baked Motions hidden / solved pose shown";return true;}
    if(id>=45100){const size_t index=(size_t)(id-45100);if(index<p.rows.size()&&p.rows[index].name!=p.selected){stopBakedMotion();p.selected=p.rows[index].name;refreshBakedMotions();}return true;}
    if(id==45009||id==45008)return true;
    if(id==45010||id==45011){p.scroll=std::clamp(p.scroll+(id==45010?-1:1),0,std::max(0,(int)p.rows.size()-1));return true;}
    if(id==45001){std::string reason;if(!bakedMotionPlaybackAvailable(reason)){g_status=reason;return true;}if(!p.playing&&!p.loop&&p.time>=p.frames.duration-1e-9)p.time=0;p.playing=!p.playing;startBakedMotionDisplay();g_status=p.playing?"Playing baked motion / display transforms only":"Baked motion paused";return true;}
    if(id==45002){stopBakedMotion();g_status="Baked motion stopped / solved pose restored";return true;}
    if(id==45003){p.loop=!p.loop;return true;}
    if(id==45004){static const double speeds[]={.25,.5,1,2};size_t next=0;for(size_t i=0;i<4;++i)if(std::fabs(speeds[i]-p.speed)<1e-9)next=(i+1)%4;p.speed=speeds[next];return true;}
    if(id==45012||id==45013){stepBakedMotion(id==45012?-1:1);return true;}
    if(p.selected.empty()){g_status="Select a baked motion first";return true;}
    if(id==45005){std::vector<EditorField> fields{{"Motion name",p.selected}};if(!editFields("Rename baked motion","Renames the graph and its baked frames through one undoable, revision-checked transaction.",fields))return true;
        const std::string name=trimField(fields[0].value);if(name.empty()||name==p.selected)return true;V c=motionCommand("motion.rename");c.set("new_name",V::makeStr(name));const std::string previous=p.selected;applyCommand(c);if(!g_error){stopBakedMotion();p.selected=name;refreshBakedMotions();g_status="Renamed "+previous+" to "+name+" / undo restores it";}return true;}
    if(id==45006){stopBakedMotion();const std::string name=p.selected;applyCommand(motionCommand("motion.remove"));if(!g_error){p.selected.clear();refreshBakedMotions();g_status="Deleted baked motion "+name+" / undo restores it";}return true;}
    if(id==45007){stopBakedMotion();V c=motionCommand("motion.bake");if(const auto* motion=findMotion(g_doc,p.selected))if(const auto* bake=motion->find("bake"))c.set("fps",*bake->find("fps"));applyCommand(c);if(!g_error){refreshBakedMotions();g_status="Baked "+p.selected+" through the joint solver / revision "+std::to_string(g_session.revision);}return true;}
    return true;
}
static void bakedMotionLine(GLint lineAB,float ax,float ay,float bx,float by,float r,float g,float b,float a,float width){
    const float x=std::min(ax,bx)-width-1,y=std::min(ay,by)-width-1,w=std::fabs(bx-ax)+2*width+2,h=std::fabs(by-ay)+2*width+2;
    glUniform1i(uuGlyph,500);glUniform4f(uuRc,x,y,w,h);glUniform1f(uuBW,width);glUniform4f(uuFill,r,g,b,a);glUniform4f(lineAB,ax-x,ay-y,bx-x,by-y);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glUniform1i(uuGlyph,0);
}
template<class Rect,class Button>static void drawBakedMotionsPanel(Rect rect,Button button,POINT pointer,std::string& hint){
    auto& p=g_bakedMotions;if(!p.open)return;refreshBakedMotions();
    const float x0=(float)(viewX+viewW),y0=(float)viewY,w=(float)bakedMotionsDockWidth(),h=(float)(winH-32-viewY);if(w<200||h<200)return;
    const GLint lineAB=glGetUniformLocation(pUI,"lineAB");const float left=x0+14,inner=w-28;
    auto muted=[&](const std::string& text,float x,float y,float scale,float width){textOut(fitText(text,scale,width),x,y,scale,.55f,.66f,.74f);};
    rect(x0,y0,w,h,0,.050f,.068f,.084f,1,.20f,.27f,.32f,.9f,1);rect(x0,y0,2,h,0,.13f,.55f,.66f,.55f,0,0,0,0,0);
    textOut("BAKED MOTIONS",left,y0+13,.74f,.82f,.93f,.99f);const std::string count=std::to_string(p.rows.size())+(p.rows.size()==1?" motion":" motions");
    button("Hide",x0+w-74,y0+7,60,45000);muted(count,left+textWidth("BAKED MOTIONS",.74f)+12,y0+16,.56f,std::max(10.f,w-textWidth("BAKED MOTIONS",.74f)-120));
    rect(left,y0+46,inner,1,0,.20f,.29f,.35f,1,0,0,0,0,0);
    if(p.rows.empty()){
        float y=y0+64;for(const char* line:{"No motions in this document yet.","Ask the agent to author a vector motion","graph on existing joints and bake it:","motion.graph.set, then motion.bake.","Baked motions play here without","changing the saved design."}){muted(line,left,y,.60f,inner);y+=21;}
        g_zones.push_back({x0,y0,w,h,45009});return;
    }
    const float fixed=316,remaining=std::max(120.f,h-fixed),rowH=42;
    const int visible=std::clamp((int)((remaining-96-6)/rowH),1,std::min(4,(int)p.rows.size()));p.scroll=std::clamp(p.scroll,0,std::max(0,(int)p.rows.size()-visible));
    const float graphH=std::clamp(remaining-visible*rowH-6-((int)p.rows.size()>visible?12.f:0.f),90.f,240.f);
    float y=y0+54;
    for(int i=0;i<visible;++i){const size_t index=(size_t)(p.scroll+i);if(index>=p.rows.size())break;const auto& row=p.rows[index];const bool selected=row.name==p.selected;const bool hover=pointer.x>=left&&pointer.x<left+inner&&pointer.y>=y&&pointer.y<y+rowH-4;
        rect(left,y,inner,rowH-4,5,selected?.10f:hover?.09f:.065f,selected?.22f:hover?.15f:.09f,selected?.27f:hover?.18f:.11f,1,selected?.18f:.16f,selected?.62f:.24f,selected?.72f:.29f,selected?.9f:.6f,1);
        const float badgeScale=.50f,badgeW=textWidth(row.badge,badgeScale)+14;const bool warn=row.stale||row.invalid;
        rect(left+inner-badgeW-8,y+9,badgeW,18,9,row.invalid?.32f:row.stale?.34f:row.baked?.07f:.13f,row.invalid?.10f:row.stale?.22f:row.baked?.25f:.15f,row.invalid?.10f:row.stale?.05f:row.baked?.16f:.18f,1,row.invalid?.92f:row.stale?.98f:row.baked?.40f:.42f,row.invalid?.42f:row.stale?.72f:row.baked?.86f:.50f,row.invalid?.38f:row.stale?.28f:row.baked?.56f:.56f,.9f,1);
        textOut(row.badge,left+inner-badgeW-1,y+11,badgeScale,warn?1.f:.80f,warn?.86f:.94f,warn?.62f:.86f);
        textOut(fitText(row.name,.64f,inner-badgeW-30),left+10,y+4,.64f,.88f,.94f,.98f);muted(row.detail,left+10,y+21,.50f,inner-20);g_zones.push_back({left,y,inner,rowH-4,45100+(int)index});y+=rowH;}
    if((int)p.rows.size()>visible){char more[64];std::snprintf(more,sizeof more,"%d-%d of %zu / scroll",p.scroll+1,std::min((int)p.rows.size(),p.scroll+visible),p.rows.size());muted(more,left+2,y-2,.48f,inner);y+=12;}
    y+=6;rect(left,y,inner,1,0,.20f,.29f,.35f,1,0,0,0,0,0);y+=8;
    textOut(fitText(p.selected,.68f,inner),left,y,.68f,.86f,.95f,1.f);y+=22;
    const float gx=left,gy=y,gw=inner,gh=graphH;rect(gx,gy,gw,gh,4,.030f,.043f,.054f,1,.17f,.24f,.29f,.9f,1);
    const double duration=p.framesValid?p.frames.duration:p.graphValid?p.graph.duration:0;
    if(p.graphValid&&!p.curves.empty()&&duration>0){
        for(float f:{.25f,.5f,.75f})bakedMotionLine(lineAB,gx+4,gy+gh*f,gx+gw-4,gy+gh*f,.30f,.40f,.46f,.35f,1);
        const double step=duration<=2?.5:duration<=6?1:duration<=20?5:duration<=90?15:60;for(double t=step;t<duration-1e-9;t+=step){const float tx=gx+(float)(t/duration)*gw;bakedMotionLine(lineAB,tx,gy+4,tx,gy+gh-4,.30f,.40f,.46f,.28f,1);}
        for(size_t c=0;c<p.curves.size();++c){const auto* color=kBakedMotionColors[c%6];const auto& curve=p.curves[c];const float lo=p.lo[c],span=p.hi[c]-p.lo[c];auto px=[&](size_t i){return gx+6+(gw-12)*(float)i/(float)(curve.size()-1);};auto py=[&](float v){return gy+gh-6-(gh-12)*(v-lo)/span;};
            for(size_t i=1;i<curve.size();++i)bakedMotionLine(lineAB,px(i-1),py(curve[i-1]),px(i),py(curve[i]),color[0],color[1],color[2],.95f,2.f);
            for(const auto& handle:p.handles[c]){auto hx=[&](double t){return gx+6+(gw-12)*(float)(t/p.graph.duration);};bakedMotionLine(lineAB,hx(handle.t0),py((float)handle.v0),hx(handle.t1),py((float)handle.v1),color[0],color[1],color[2],.45f,1);bakedMotionLine(lineAB,hx(handle.t3),py((float)handle.v3),hx(handle.t2),py((float)handle.v2),color[0],color[1],color[2],.45f,1);
                for(auto pt:{std::pair{handle.t1,handle.v1},std::pair{handle.t2,handle.v2}})rect(hx(pt.first)-3,py((float)pt.second)-3,6,6,1,.03f,.05f,.06f,1,color[0],color[1],color[2],.9f,1);}
            if(const auto* rows=p.diagnostics.find("channels"))if(c<rows->arr.size())if(const auto* breaks=rows->arr[c].find("breaks"))for(const auto& b:breaks->arr){const float bx=gx+6+(gw-12)*(float)(b.find("time_s")->num/p.graph.duration);const bool jump=b.find("kind")->str=="C0";rect(bx-2,gy+3,4,7,1,jump?.96f:.98f,jump?.40f:.72f,jump?.36f:.27f,.9f,0,0,0,0,0);}
        }
        if(!p.units.empty()&&!p.units[0].empty()&&p.units[0]!="signal"&&!p.rangeLo.empty()){char top[48],bottom[48];std::snprintf(top,sizeof top,"%.4g %s",p.rangeHi[0],p.units[0].c_str());std::snprintf(bottom,sizeof bottom,"%.4g %s",p.rangeLo[0],p.units[0].c_str());textOut(top,gx+12,gy+4,.46f,.48f,.60f,.68f);textOut(bottom,gx+12,gy+gh-17,.46f,.48f,.60f,.68f);}
        const float playX=gx+6+(gw-12)*(float)std::clamp(p.time/duration,0.,1.);bakedMotionLine(lineAB,playX,gy+2,playX,gy+gh-2,.42f,.94f,1.f,p.active?1.f:.55f,2.f);
    }else muted(p.graphError.empty()?"Graph unavailable":p.graphError,gx+8,gy+gh*.5f-8,.52f,gw-16);
    y+=gh+6;
    const size_t frame=bakedMotionFrame();
    if(p.graphValid){const size_t shown=std::min<size_t>(p.graph.channels.size(),4);std::vector<double> now;try{now=evaluateMotion(p.graph,std::min(p.time,p.graph.duration));}catch(...){now.assign(p.graph.channels.size(),0);}
        for(size_t c=0;c<shown;++c){const auto* color=kBakedMotionColors[c%6];rect(left,y+5,10,4,1,color[0],color[1],color[2],1,0,0,0,0,0);const std::string unit=c<p.units.size()?p.units[c]:std::string();
            double value=now[c];if(p.framesValid)for(size_t b=0;b<p.frames.channels.size();++b)if(p.frames.channels[b]==p.graph.channels[c].name)value=p.frames.coordinates[frame*p.frames.channels.size()+b];std::string continuity;if(const auto* rows=p.diagnostics.find("channels"))if(c<rows->arr.size())continuity=rows->arr[c].find("continuity")->str;
            char text[200];std::snprintf(text,sizeof text,"%s  %.2f %s",p.graph.channels[c].name.c_str(),value,unit=="signal"?"":unit.c_str());textOut(fitText(text,.54f,inner-70),left+16,y,.54f,.80f,.88f,.93f);
            if(!continuity.empty()){const bool smooth=continuity=="C1";textOut(continuity=="discontinuous"?"jump":continuity,left+inner-textWidth(continuity=="discontinuous"?"jump":continuity,.52f),y,.52f,smooth?.48f:.98f,smooth?.86f:.72f,smooth?.60f:.30f);}y+=18;}
        if(p.graph.channels.size()>shown){muted("+"+std::to_string(p.graph.channels.size()-shown)+" more channels",left+16,y,.50f,inner);y+=16;}}
    y+=4;const float trackY=y+10,trackX=left+2,trackW=inner-4;rect(trackX,trackY,trackW,6,3,.10f,.15f,.18f,1,.22f,.32f,.38f,.8f,1);
    if(duration>0){const float fraction=(float)std::clamp(p.time/duration,0.,1.);rect(trackX,trackY,trackW*fraction,6,3,.16f,.66f,.78f,1,0,0,0,0,0);rect(trackX+trackW*fraction-7,trackY-4,14,14,7,.86f,.96f,1.f,1,.16f,.66f,.78f,1,1);}
    g_zones.push_back({trackX,trackY-9,trackW,24,45008});if(pointer.x>=trackX&&pointer.x<trackX+trackW&&pointer.y>=trackY-9&&pointer.y<trackY+15)hint="Drag to scrub baked frames / Left and Right step frames";
    char timeline[160];if(p.framesValid)std::snprintf(timeline,sizeof timeline,"%.2f / %.2f s   frame %zu / %zu",p.time,duration,frame+1,p.frames.frameCount);else std::snprintf(timeline,sizeof timeline,"%.2f s graph / not baked",duration);muted(timeline,left,trackY+12,.52f,inner);y=trackY+34;
    const float bw=(inner-18)/4;button(p.playing?"Pause":"Play",left,y,bw,45001,true);button("Stop",left+bw+6,y,bw,45002);button("Loop",left+2*(bw+6),y,bw,45003,false,p.loop);char speed[16];std::snprintf(speed,sizeof speed,"%gx",p.speed);button(speed,left+3*(bw+6),y,bw,45004);y+=38;
    const float mw=(inner-12)/3;button("Rename",left,y,mw,45005);button("Bake",left+mw+6,y,mw,45007);button("Delete",left+2*(mw+6),y,mw,45006);y+=40;
    for(const auto& row:p.rows)if(row.name==p.selected){
        std::string state;if(const auto* motion=findMotion(g_doc,p.selected))if(const auto* bake=motion->find("bake")){char text[160];std::snprintf(text,sizeof text,"Baked %.0f fps / %zu frames / source rev %.0f",bake->find("fps")->num,p.framesValid?p.frames.frameCount:0,bake->find("source_revision")->num);state=text;}
        if(state.empty())state="Not baked / Bake drives the joints through the solver";
        textOut(fitText(state,.52f,inner),left,y,.52f,.72f,.82f,.88f);y+=17;
        if(row.stale||row.invalid){textOut(fitText(row.invalid?"Graph invalid for the current joints / edit graph":"Stale: "+std::string(row.detail.find("graph changed")!=std::string::npos?"graph changed":"assembly changed")+" / Bake again",.52f,inner),left,y,.52f,.99f,.74f,.32f);y+=17;}
        break;}
    if(!p.note.empty()){textOut(fitText(p.note,.50f,inner),left,y,.50f,.96f,.56f,.48f);y+=16;}
    if(y<y0+h-20)muted("Display only / kinematic poses / no dynamics or collision",left,std::min(y0+h-20,std::max(y,y0+h-20)),.48f,inner);
    g_zones.push_back({x0,y0,w,h,45009});
}
