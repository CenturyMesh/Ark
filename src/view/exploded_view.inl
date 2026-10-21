// 048B Exploded Views panel (Explode and Visualize tabs), exploded display transforms
// and Visualize capture. Exploded poses replace display transforms only.
static const ExplodedViewPlan* explodedPlan(const std::string& name){
    struct Cached {u64 revision=~u64(0),epoch=~u64(0);ExplodedViewPlan plan;bool valid=false;};static std::map<std::string,Cached> cache;
    auto& entry=cache[name];if(entry.revision!=g_session.revision||entry.epoch!=g_documentEpoch){entry.revision=g_session.revision;entry.epoch=g_documentEpoch;std::string error;const auto* view=findExplodedView(g_doc,name);entry.valid=view&&compileExplodedView(g_doc,*view,entry.plan,error);}
    if(cache.size()>64)for(auto it=cache.begin();it!=cache.end();)if(it->second.revision!=g_session.revision&&it->first!=name)it=cache.erase(it);else ++it;
    return entry.valid?&cache[name].plan:nullptr;
}
static void applyVisualizePresets(bool enter);
static void applyExplodeOffsets(const ExplodedViewPlan& plan,double t){const auto offsets=explodedOffsets(g_doc,plan,g_instanceIds,t);for(size_t i=0;i<offsets.size()&&i<g_instanceWorld.size();++i)if(offsets[i].lensq()>0)g_instanceWorld[i]=Mat4::translation(offsets[i])*g_instanceWorld[i];}
static bool explodedDisplayAvailable(std::string& reason){
    if(g_doc.assembly.empty()){reason="Exploded views need assembly instances";return false;}
    if(g_allSheetsFlat||g_allSheetsPlaying||g_stockMode){reason="Return to the formed assembly before exploding";return false;}
    if(g_drawingViewport.active){reason="Return to the 3D design before exploding";return false;}
    if(g_componentMove.armed||g_componentMove.active){reason="Finish or cancel the component move first";return false;}
    if(g_motionActive){reason="Stop the mechanical motion first";return false;}
    if(measurementActive()){reason="Close the measurement first; it measures the assembled design";return false;}
    if(g_formedInstanceWorld.size()!=g_instanceWorld.size()){reason="Assembly display poses are unavailable";return false;}
    return true;
}
static void collapseExplodedDisplay(const char* reason){
    auto& p=g_explode;const bool shown=p.applied;p.factor=0;p.animating=p.sliding=false;p.applied=false;p.shownFactor=-1;
    if(shown&&!g_bakedMotions.active&&g_formedInstanceWorld.size()==g_instanceWorld.size()){g_instanceWorld=g_formedInstanceWorld;rebuildDisplayBounds();refreshSelectionLines();}
    if(shown&&reason)g_status=std::string(reason)+" / exploded display collapsed to the assembled design";
}
// Baked playback owns display transforms; a motion's explode_view channels apply on top of its frame.
static void explodedBakedFrame(size_t frame){
    auto& p=g_explode;p.motionFactor=-1;p.motionView.clear();const auto& f=g_bakedMotions.frames;
    for(size_t c=0;c<f.explodeViews.size()&&c<f.channels.size();++c)if(!f.explodeViews[c].empty()&&frame<f.frameCount){const double t=std::clamp((double)f.coordinates[frame*f.channels.size()+c],0.,1.);
        if(const auto* plan=explodedPlan(f.explodeViews[c]))applyExplodeOffsets(*plan,t);p.motionView=f.explodeViews[c];p.motionFactor=t;}
}
static void advanceExplodedView(double seconds){
    auto& p=g_explode;
    if(p.open&&g_bakedMotions.open){p.open=false;applyVisualizePresets(false);layout();} // Baked Motions took the dock
    if(g_bakedMotions.active){if(p.applied||p.factor>0||p.animating){p.applied=p.animating=false;p.factor=0;p.shownFactor=-1;}return;}
    if(p.shownEpoch!=~u64(0)&&p.shownEpoch!=g_documentEpoch){p.factor=0;p.animating=false;p.pending.clear();} // another document
    p.motionFactor=-1;p.motionView.clear();const ExplodedViewPlan* plan=p.selected.empty()?nullptr:explodedPlan(p.selected);
    if(p.animating&&plan){p.factor=std::min(1.,p.factor+seconds/std::max(.8,.75*(double)plan->slices));if(p.factor>=1){p.animating=false;g_status="Exploded "+p.selected+" / display only, document unchanged";}}
    const bool want=plan&&p.factor>0;std::string reason;
    if(want&&!explodedDisplayAvailable(reason)){if(p.applied||p.factor>0){p.applied=p.animating=false;p.factor=0;p.shownFactor=-1;g_status=reason+" / exploded display collapsed";}return;}
    if(p.shownRevision==g_session.revision&&p.shownEpoch==g_documentEpoch&&p.shownSerial==g_displayMeshSerial&&p.shownFactor==p.factor&&p.shownView==p.selected)return;
    p.shownRevision=g_session.revision;p.shownEpoch=g_documentEpoch;p.shownSerial=g_displayMeshSerial;p.shownFactor=p.factor;p.shownView=p.selected;
    if(!want){if(p.applied){p.applied=false;if(g_formedInstanceWorld.size()==g_instanceWorld.size())g_instanceWorld=g_formedInstanceWorld;rebuildDisplayBounds();refreshSelectionLines();}return;}
    g_instanceWorld=g_formedInstanceWorld;applyExplodeOffsets(*plan,p.factor);p.applied=true;rebuildDisplayBounds();refreshSelectionLines();
}
static void setExplodeFactor(double t){
    auto& p=g_explode;if(p.selected.empty()){g_status="Create or select an exploded view first";return;}
    if(g_bakedMotions.active)stopBakedMotion();std::string reason;if(!explodedDisplayAvailable(reason)){g_status=reason;return;}
    p.animating=false;p.factor=std::clamp(t,0.,1.);char text[160];std::snprintf(text,sizeof text,"Exploded view %s at %.2f / display only, revision unchanged",p.selected.c_str(),p.factor);g_status=text;
}
static void refreshExplodedViews(){
    auto& p=g_explode;using V=arkjson::Value;const bool any=g_doc.explodedViews.t==V::Arr&&!g_doc.explodedViews.arr.empty();
    if(!any){if(!p.selected.empty()){p.selected.clear();collapseExplodedDisplay();}return;}
    if(p.selected.empty()||!findExplodedView(g_doc,p.selected)){collapseExplodedDisplay();p.selected=g_doc.explodedViews.arr.front().find("name")->str;p.step=-1;}
    const auto* view=findExplodedView(g_doc,p.selected);const int steps=(int)view->find("steps")->arr.size();if(p.step>=steps)p.step=steps-1;
    p.scroll=std::clamp(p.scroll,0,std::max(0,(int)g_doc.explodedViews.arr.size()-1));
}
static std::string explodedInstanceName(u64 id){int count=0;std::string name;for(const auto& n:g_doc.assembly)if(n.id==id){name=n.name;}for(const auto& n:g_doc.assembly)if(n.name==name)++count;return name.empty()?"#"+std::to_string(id)+" (removed)":count==1?name:name+" #"+std::to_string(id);}
static std::string explodedStepSummary(const arkjson::Value& step,const ExplodedStep* resolved=nullptr){
    std::string names;size_t count=0;for(const auto& id:step.find("instances")->arr){if(count<3)names+=(names.empty()?"":", ")+explodedInstanceName((u64)id.num);++count;}if(count>3)names+=" +"+std::to_string(count-3);
    const auto& d=*step.find("direction");std::string direction=d.t==arkjson::Value::Str?d.str:"vector";const auto& distance=*step.find("distance");
    char text[96];if(distance.t==arkjson::Value::Num)std::snprintf(text,sizeof text,"%g mm",distance.num);else if(resolved)std::snprintf(text,sizeof text,"%s = %g mm",distance.str.substr(0,24).c_str(),resolved->distance);else std::snprintf(text,sizeof text,"%s",distance.str.substr(0,24).c_str());
    return names+"  "+direction+" "+text+(step.find("with_previous")&&step.find("with_previous")->b?"  / with previous":"");
}
// Step editor: instances (names or ids), direction (axis, camera-relative or x,y,z), distance expression.
static bool explodedStepDialog(const char* title,arkjson::Value& step,bool first,bool newView,std::string* name=nullptr){
    using V=arkjson::Value;std::string instances,direction="+Z",distance="50",label;bool withPrevious=false;
    if(step.t==V::Obj){for(const auto& id:step.find("instances")->arr){const auto n=explodedInstanceName((u64)id.num);instances+=(instances.empty()?"":", ")+(n.find(" #")!=std::string::npos||n[0]=='#'?std::to_string((u64)id.num):n);}
        const auto& d=*step.find("direction");if(d.t==V::Str)direction=d.str;else{char text[96];std::snprintf(text,sizeof text,"%g, %g, %g",d.arr[0].num,d.arr[1].num,d.arr[2].num);direction=text;}
        distance=step.find("distance")->t==V::Num?step.find("distance")->dump():step.find("distance")->str;withPrevious=step.find("with_previous")&&step.find("with_previous")->b;if(const auto* l=step.find("label"))label=l->str;}
    else for(u64 id:g_explode.pending){const auto n=explodedInstanceName(id);instances+=(instances.empty()?"":", ")+(n.find(" #")!=std::string::npos?std::to_string(id):n);}
    std::vector<EditorField> fields;if(newView)fields.push_back({"Exploded view name",name&&!name->empty()?*name:"Exploded view"});
    fields.push_back({"Instances / names or ids, comma separated",instances});EditorField axis{"Direction",direction,false,{"+X","-X","+Y","-Y","+Z","-Z","Toward camera","Screen right","Screen up"}};axis.editableChoice=true;fields.push_back(axis);
    fields.push_back({"Distance / mm or parameter expression",distance});fields.push_back({"Run with previous step",withPrevious?"1":"0",true});fields.push_back({"Label (optional)",label});
    if(!editFields(title,"Steps move instances along a world direction while the explode factor sweeps their slice. Display only: placements and B-rep never change. Click components in the viewport first to fill Instances.",fields))return false;
    size_t at=0;if(newView&&name)*name=trimField(fields[at++].value);
    V ids=V::makeArr();std::string list=fields[at++].value+",";for(size_t start=0,comma;(comma=list.find(',',start))!=std::string::npos;start=comma+1){const auto token=trimField(list.substr(start,comma-start));if(token.empty())continue;
        if(token.find_first_not_of("0123456789")==std::string::npos)ids.arr.push_back(V::makeNum(std::stod(token)));else ids.arr.push_back(V::makeStr(token));}
    if(ids.arr.empty())throw std::runtime_error("Pick components in the viewport or name at least one instance");
    std::string d=trimField(fields[at++].value);V dir;const CameraFrame frame=cameraFrame(yaw,pitch,cameraRoll);auto rounded=[](Vec3 v){V a=V::makeArr();for(double x:{v.x,v.y,v.z})a.arr.push_back(V::makeNum(std::round(x*1000)/1000));return a;};
    if(d=="Toward camera")dir=rounded(frame.outward);else if(d=="Screen right")dir=rounded(frame.right);else if(d=="Screen up")dir=rounded(frame.up);
    else if(d.find(',')!=std::string::npos||d.find(' ')!=std::string::npos){V a=V::makeArr();std::string s=d;for(char& c:s)if(c==','||c=='['||c==']')c=' ';std::istringstream in(s);double x;while(in>>x)a.arr.push_back(V::makeNum(x));if(a.arr.size()!=3)throw std::runtime_error("Custom direction needs three numbers: x, y, z");dir=a;}
    else dir=V::makeStr(d);
    const std::string dist=trimField(fields[at++].value);V next=V::makeObj();next.set("instances",ids);next.set("direction",dir);
    char* end=nullptr;const double number=std::strtod(dist.c_str(),&end);next.set("distance",!dist.empty()&&end&&*end=='\0'?V::makeNum(number):V::makeStr(dist));
    if(fields[at++].value=="1"&&!first)next.set("with_previous",V::makeBool(true));const std::string l=trimField(fields[at++].value);if(!l.empty())next.set("label",V::makeStr(l));step=next;return true;
}
static void commitExplodedSteps(const arkjson::Value& steps,const char* what){
    using V=arkjson::Value;auto& p=g_explode;const auto* view=findExplodedView(g_doc,p.selected);if(!view)return;V c=V::makeObj();c.set("op",V::makeStr("explode.set"));c.set("name",V::makeStr(p.selected));c.set("steps",steps);if(const auto* d=view->find("description"))c.set("description",*d);
    applyCommand(c);if(!g_error)g_status=std::string(what)+" / "+p.selected+" revision "+std::to_string(g_session.revision)+" / undo restores it";
}
static void explodedViewsAfterApply(const arkjson::Value& args){
    const auto* commands=args.find("commands");if(!commands||commands->t!=arkjson::Value::Arr)return;
    for(const auto& c:commands->arr){const auto* op=c.find("op");const auto* name=c.find("name");if(!op||!name||op->t!=arkjson::Value::Str||name->t!=arkjson::Value::Str)continue;
        if(op->str=="explode.set"&&findExplodedView(g_doc,name->str)){if(g_explode.selected!=name->str){collapseExplodedDisplay();g_explode.selected=name->str;g_explode.step=-1;}if(!g_explode.open&&!g_drawingViewport.active){if(g_bakedMotions.open){g_bakedMotions.open=false;stopBakedMotion();}g_explode.open=true;layout();}}
        else if(op->str=="explode.rename"&&g_explode.selected==name->str)if(const auto* next=c.find("new_name"))g_explode.selected=next->asStr();}
}
static void explodedViewPicked(int body){
    auto& p=g_explode;if(!p.open||p.visualize||body<0||(size_t)body>=g_instanceIds.size()||!g_instanceIds[(size_t)body])return;const u64 id=g_instanceIds[(size_t)body];
    auto it=std::find(p.pending.begin(),p.pending.end(),id);if(it==p.pending.end())p.pending.push_back(id);else p.pending.erase(it);g_status="Picked "+std::to_string(p.pending.size())+" component"+(p.pending.size()==1?"":"s")+" for the next explode step / Add step";
}
static bool explodedViewPress(int zone,double fraction){if(zone!=47008)return false;g_explode.sliding=true;setExplodeFactor(fraction);return true;}
static bool explodedViewDrag(int x,bool button){if(!g_explode.sliding||!button)return false;for(const auto& zone:g_zones)if(zone.id==47008){setExplodeFactor((double)(x-zone.x)/zone.w);break;}return true;}

// Visualize ------------------------------------------------------------------
static void applyVisualizePresets(bool enter){
    auto& p=g_explode;
    if(enter){if(!p.saved){p.saved=true;p.savedGrid=g_grid;p.savedLight=g_lightBackground;p.savedSatin=g_satin;p.savedEdges=g_showEdges;p.savedTrace=g_pathTracing;p.savedOcclusion=g_occlusionMode;p.savedCube=g_showViewCube;}
        g_grid=false;g_showViewCube=false;g_lightBackground=p.background!=0;g_whiteBackground=p.background==2;g_satin=p.look!=2;g_showEdges=p.look==2;g_occlusionMode=p.look==1?3:p.look==2?1:2;g_pathTracing=p.engine==0&&p.savedTraceRequest;
        selBody=-1;hoverBody=-1;g_picked.clear();refreshSelectionLines();}
    else if(p.saved){g_grid=p.savedGrid;g_lightBackground=p.savedLight;g_whiteBackground=false;g_satin=p.savedSatin;g_showEdges=p.savedEdges;g_pathTracing=p.savedTrace;g_occlusionMode=p.savedOcclusion;g_showViewCube=p.savedCube;p.saved=false;}
}
static void refreshVisualizeBom(){auto& p=g_explode;if(p.bomRevision==g_session.revision&&p.bomEpoch==g_documentEpoch)return;p.bomRevision=g_session.revision;p.bomEpoch=g_documentEpoch;p.bom.clear();p.bomError.clear();try{p.bom=visualizeBomItems(g_doc,g_session.revision);}catch(const std::exception& e){p.bomError=e.what();}}
// Anchors: per BOM item, a visible surface point (pickBody) of its largest displayed occurrence, nearest its projected bounds centre.
static void visualizeAnchors(std::vector<VisualizeAnchor>& anchors,double silhouette[4]){
    auto& p=g_explode;int budget=4000; // raycasts per layout; beyond it the box centre is used unverified
    silhouette[0]=silhouette[1]=1e300;silhouette[2]=silhouette[3]=-1e300;std::map<u64,std::pair<double,VisualizeAnchor>> best;std::map<u64,int> items;for(const auto& item:p.bom)items[item.feature]=item.item;
    for(size_t i=0;i<g_bodyIds.size()&&i<g_instancePart.size();++i){if(!displayInstance(i))continue;const AABB box=g_reports[g_instancePart[i]].bounds.transformed(g_instanceWorld[i]);if(!box.valid())continue;double x0=1e300,y0=1e300,x1=-1e300,y1=-1e300;bool visible=false;
        for(int c=0;c<8;++c){float x,y;if(!projectScreen({c&1?box.mx.x:box.mn.x,c&2?box.mx.y:box.mn.y,c&4?box.mx.z:box.mn.z},x,y))continue;visible=true;x0=std::min(x0,(double)x-viewX);x1=std::max(x1,(double)x-viewX);y0=std::min(y0,(double)y-viewY);y1=std::max(y1,(double)y-viewY);}
        if(!visible)continue;silhouette[0]=std::min(silhouette[0],x0);silhouette[1]=std::min(silhouette[1],y0);silhouette[2]=std::max(silhouette[2],x1);silhouette[3]=std::max(silhouette[3],y1);
        const auto item=items.find(g_bodyIds[i]);const double area=(x1-x0)*(y1-y0);auto& slot=best[g_bodyIds[i]];if(item==items.end()||area<=slot.first)continue;
        // The leader ends on a visible surface: the sample nearest the box centre that picks this occurrence.
        std::vector<std::pair<double,std::pair<double,double>>> samples;const double cx=(x0+x1)/2,cy=(y0+y1)/2;samples.push_back({0,{cx,cy}});for(int a=0;a<6;++a)for(int b=0;b<6;++b){const double sx=x0+(x1-x0)*(a+.5)/6,sy=y0+(y1-y0)*(b+.5)/6;samples.push_back({std::hypot(sx-cx,sy-cy),{sx,sy}});}
        std::sort(samples.begin(),samples.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        for(const auto& s:samples){const double sx=s.second.first,sy=s.second.second;if(sx<0||sy<0||sx>=viewW||sy>=viewH)continue;if(budget>0&&(--budget,pickBody((int)sx+viewX,(int)sy+viewY)!=(int)i))continue;slot={area,{item->second,sx,sy}};break;}}
    for(const auto& [feature,slot]:best)if(slot.first>0)anchors.push_back(slot.second);std::sort(anchors.begin(),anchors.end(),[](const VisualizeAnchor& a,const VisualizeAnchor& b){return a.item<b.item;});
    silhouette[0]=std::max(silhouette[0],0.);silhouette[1]=std::max(silhouette[1],0.);silhouette[2]=std::min(silhouette[2],(double)viewW);silhouette[3]=std::min(silhouette[3],(double)viewH);
}
// Preview only: the band [top, top+height) keeps clear of the path-tracing caption and the viewport dock.
static VisualizeOverlayLayout visualizeLayout(int width,int height,double top){
    auto& p=g_explode;std::vector<VisualizeAnchor> anchors;double silhouette[4];visualizeAnchors(anchors,silhouette);for(auto& a:anchors)a.y-=top;silhouette[1]-=top;silhouette[3]-=top;
    auto layout=layoutVisualizeOverlay(width,height,p.bom,anchors,silhouette,p.balloons,p.table);layout.tableY+=top;for(auto& b:layout.balloons){b.ay+=top;b.by+=top;}return layout;
}
// The live preview uses the export's software overlay, composited through black and white canvases for alpha.
static void drawVisualizePreview(){
    auto& p=g_explode;if(!p.open||!p.visualize||(!p.balloons&&!p.table)||viewW<64||viewH<64)return;refreshVisualizeBom();if(p.bom.empty())return;
    const auto layout=visualizeLayout(viewW,std::max(64,viewH-150),50.);std::uint64_t key=1469598103934665603ull;auto mix=[&](double v){float f=(float)v;unsigned char b[sizeof f];std::memcpy(b,&f,sizeof f);for(unsigned char c:b){key^=c;key*=1099511628211ull;}};
    mix(viewW);mix(viewH);mix(p.background);mix(layout.tableX);mix(layout.tableY);mix((double)layout.rows.size());for(const auto& b:layout.balloons){mix(b.item);mix(b.ax);mix(b.ay);mix(b.bx);mix(b.by);}
    if(!p.overlayTexture)glGenTextures(1,&p.overlayTexture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p.overlayTexture);
    if(key!=p.overlayKey||!p.overlayValid){Image black,white;black.w=white.w=viewW;black.h=white.h=viewH;black.px.assign((size_t)viewW*viewH*3,0);white.px.assign(black.px.size(),255);drawVisualizeOverlay(black,layout,p.background==0);drawVisualizeOverlay(white,layout,p.background==0);
        std::vector<unsigned char> rgba((size_t)viewW*viewH*4);for(int y=0;y<viewH;++y)for(int x=0;x<viewW;++x){const size_t s=((size_t)y*viewW+x)*3,d=((size_t)(viewH-1-y)*viewW+x)*4;int a=255;for(int k=0;k<3;++k)a=std::min(a,255-(white.px[s+k]-black.px[s+k]));
            for(int k=0;k<3;++k)rgba[d+k]=a?(unsigned char)std::min(255,black.px[s+k]*255/a):0;rgba[d+3]=(unsigned char)a;}
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,viewW,viewH,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);p.overlayKey=key;p.overlayValid=true;p.overlayW=viewW;p.overlayH=viewH;}
    glUseProgram(pImage);glUniform2f(glGetUniformLocation(pImage,"vp"),(float)winW,(float)winH);glUniform4f(glGetUniformLocation(pImage,"rc"),(float)viewX,(float)viewY,(float)viewW,(float)viewH);glUniform1i(glGetUniformLocation(pImage,"image"),0);glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,6);glUseProgram(pUI);
}
// drawFrame hook after the 3D pass, before sketches, selection lines and UI.
static void captureVisualizeScene(){
    auto& p=g_explode;if(!p.captureRequested)return;p.captureRequested=false;std::vector<u8> raw((size_t)viewW*viewH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(viewX,winH-viewY-viewH,viewW,viewH,GL_RGB,GL_UNSIGNED_BYTE,raw.data());
    p.capture.w=viewW;p.capture.h=viewH;p.capture.px.resize(raw.size());for(int y=0;y<viewH;++y)std::copy_n(raw.begin()+(size_t)(viewH-1-y)*viewW*3,(size_t)viewW*3,p.capture.px.begin()+(size_t)y*viewW*3);
}
// Centre crop to the target aspect; area average when shrinking, bilinear when enlarging.
static Image visualizeResample(const Image& src,int width,int height,double& originX,double& originY,double& scale){
    const double aspect=(double)width/height;double cw=src.w,ch=src.h;if(cw/ch>aspect)cw=ch*aspect;else ch=cw/aspect;originX=(src.w-cw)/2;originY=(src.h-ch)/2;scale=width/cw;
    Image out;out.w=width;out.h=height;out.px.resize((size_t)width*height*3);const double step=cw/width;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){double sum[3]{};
        if(step>1){const int x0=(int)(originX+x*step),x1=std::max(x0+1,(int)(originX+(x+1)*step)),y0=(int)(originY+y*step),y1=std::max(y0+1,(int)(originY+(y+1)*step));int n=0;
            for(int sy=y0;sy<y1&&sy<src.h;++sy)for(int sx=x0;sx<x1&&sx<src.w;++sx){for(int k=0;k<3;++k)sum[k]+=src.px[((size_t)sy*src.w+sx)*3+k];++n;}for(double& v:sum)v/=std::max(n,1);}
        else{const double fx=std::clamp(originX+(x+.5)*step-.5,0.,src.w-1.),fy=std::clamp(originY+(y+.5)*step-.5,0.,src.h-1.);const int ix=std::min((int)fx,src.w-2>0?src.w-2:0),iy=std::min((int)fy,src.h-2>0?src.h-2:0);const double tx=fx-ix,ty=fy-iy;
            for(int k=0;k<3;++k){auto at=[&](int a,int b){return (double)src.px[((size_t)std::min(b,src.h-1)*src.w+std::min(a,src.w-1))*3+k];};sum[k]=at(ix,iy)*(1-tx)*(1-ty)+at(ix+1,iy)*tx*(1-ty)+at(ix,iy+1)*(1-tx)*ty+at(ix+1,iy+1)*tx*ty;}}
        for(int k=0;k<3;++k)out.px[((size_t)y*width+x)*3+k]=(u8)std::clamp(std::lround(sum[k]),0l,255l);}
    return out;
}
static void exportVisualize(){
    using V=arkjson::Value;auto& p=g_explode;std::string path=p.exportPath;
    if(path.empty()){wchar_t file[32768]=L"ark-visualize.png";OPENFILENAMEW of{};of.lStructSize=sizeof of;of.hwndOwner=g_window;of.lpstrFile=file;of.nMaxFile=32768;of.lpstrFilter=L"PNG image\0*.png\0";of.lpstrDefExt=L"png";of.lpstrTitle=L"Export Visualize image";
        of.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(!GetSaveFileNameW(&of))return;int size=WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);path.resize(size);WideCharToMultiByte(CP_UTF8,0,file,-1,path.data(),size,nullptr,nullptr);path.pop_back();}
    static const int sizes[4][2]={{0,0},{1280,720},{1920,1080},{2560,1440}};refreshVisualizeBom();Image image;std::string detail;const bool dark=p.background==0;
    if(p.engine==1){V args=V::makeObj(),camera=V::makeObj();const CameraFrame frame=cameraFrame(yaw,pitch,cameraRoll);auto vec=[](Vec3 v){V a=V::makeArr();for(double x:{v.x,v.y,v.z})a.arr.push_back(V::makeNum(x));return a;};
        camera.set("direction",vec((camEye()-target).normalized()));camera.set("up",vec(frame.up));camera.set("target_mm",vec(target));camera.set("distance_mm",V::makeNum(dist));camera.set("fov_deg",V::makeNum(35));args.set("view",V::makeStr("custom"));args.set("camera",camera);
        if(g_cameraOrtho)args.set("projection",V::makeStr("orthographic"));static const char* looks[]={"studio","soft","technical"};static const char* backgrounds[]={"dark","light","white"};static const char* resolutions[]={"1280x800","1280x720","1920x1080","1920x1080"};
        args.set("lighting",V::makeStr(looks[p.look]));args.set("background",V::makeStr(backgrounds[p.background]));args.set("resolution",V::makeStr(resolutions[p.resolution]));args.set("samples",V::makeNum(p.softwareSamples));args.set("balloons",V::makeBool(p.balloons));args.set("bom_table",V::makeBool(p.table));
        if(g_bakedMotions.active){args.set("motion",V::makeStr(g_bakedMotions.selected));args.set("motion_time_s",V::makeNum(std::min(g_bakedMotions.frames.duration,(double)bakedMotionFrame()/g_bakedMotions.frames.fps)));}
        else if(p.applied){args.set("exploded_view",V::makeStr(p.selected));args.set("explode_factor",V::makeNum(p.factor));}
        args.set("expected_revision",V::makeNum((double)g_session.revision));V evidence;try{evidence=visualizeRender(g_doc,g_session.revision,args,image);}catch(const std::exception& e){g_error=true;g_status=e.what();return;}
        char text[160];std::snprintf(text,sizeof text,"software %.0f samples/pixel",evidence.find("samples_per_pixel")->num);detail=text;}
    else{
        if(IsIconic(g_window)){g_error=true;g_status="Restore the Studio window before exporting viewport pixels";return;}
        struct Scope {HDC dc;bool swap;~Scope(){if(dc)ReleaseDC(g_window,dc);g_swap=swap;}} scope{GetDC(g_window),g_swap};p.capture={};p.captureRequested=true;g_swap=false;drawFrame(scope.dc);glFinish();p.captureRequested=false;
        if(p.capture.px.empty()){g_error=true;g_status="Visualize capture failed: no viewport pixels";return;}
        int width=sizes[p.resolution][0],height=sizes[p.resolution][1];if(!width){width=p.capture.w;height=p.capture.h;}double originX=0,originY=0,scale=1;image=visualizeResample(p.capture,width,height,originX,originY,scale);
        if(p.balloons||p.table){std::vector<VisualizeAnchor> anchors;double silhouette[4];visualizeAnchors(anchors,silhouette);for(auto& a:anchors){a.x=(a.x-originX)*scale;a.y=(a.y-originY)*scale;}silhouette[0]=(silhouette[0]-originX)*scale;silhouette[2]=(silhouette[2]-originX)*scale;silhouette[1]=(silhouette[1]-originY)*scale;silhouette[3]=(silhouette[3]-originY)*scale;
            drawVisualizeOverlay(image,layoutVisualizeOverlay(width,height,p.bom,anchors,silhouette,p.balloons,p.table),dark);}
        const int s=width<800?1:width<1500?2:3,foot=std::max(1,s-1);const u8 caption[3]={(u8)(dark?205:60),(u8)(dark?212:66),(u8)(dark?220:74)};drawVisualizeText(image,"ARK Visualize / display tessellation, not geometric authority",10*s,height-10*s-8*foot,foot,caption);
        char text[200];if(g_trace.active)std::snprintf(text,sizeof text,"GPU path traced %d spp",g_trace.samples);else std::snprintf(text,sizeof text,"raster%s%s",g_pathTracing&&!g_trace.reason.empty()?" (":"",g_pathTracing&&!g_trace.reason.empty()?(g_trace.reason+")").c_str():"");detail=text;
        if(scale>1.0001){std::snprintf(text,sizeof text," / upscaled from %dx%d viewport pixels",(int)std::lround(width/scale),(int)std::lround(height/scale));detail+=text;}}
    if(!writePNG(path.c_str(),image)){g_error=true;g_status="Could not write "+path;return;}
    char text[96];std::snprintf(text,sizeof text,"%dx%d",image.w,image.h);p.lastExport=path.substr(path.find_last_of("/\\")+1)+"  "+text+"  "+detail;g_error=false;g_status="Visualize exported "+p.lastExport+" / display tessellation, not geometric authority";
}

static bool explodedViewsAction(int id){
    if(id<47000||id>=47400)return false;using V=arkjson::Value;auto& p=g_explode;refreshExplodedViews();
    auto openPanel=[&](bool visualize){if(g_bakedMotions.open){g_bakedMotions.open=false;if(!visualize)stopBakedMotion();}const bool wasVisualize=p.open&&p.visualize;p.open=true;p.visualize=visualize;if(visualize&&!wasVisualize)applyVisualizePresets(true);if(!visualize&&wasVisualize)applyVisualizePresets(false);layout();};
    if(id==47000){if(p.open&&!p.visualize){p.open=false;applyVisualizePresets(false);collapseExplodedDisplay();layout();g_status="Exploded views hidden / assembled design shown";}else{openPanel(false);g_status="Exploded views / select, slide or Animate; display only";}return true;}
    if(id==47001){if(p.open&&p.visualize){p.open=false;applyVisualizePresets(false);layout();g_status="Visualize closed / display settings restored";}else{openPanel(true);g_status="Visualize / presets, progressive samples and PNG export of display tessellation";}return true;}
    if(id==47002){openPanel(false);return true;}if(id==47003){openPanel(true);return true;}
    if(id==47008||id==47009)return true;
    if(id==47010||id==47011){p.scroll=std::clamp(p.scroll+(id==47010?-1:1),0,std::max(0,(int)(g_doc.explodedViews.t==V::Arr?g_doc.explodedViews.arr.size():0)-1));return true;}
    if(id>=47100&&id<47200){const size_t index=(size_t)(id-47100);if(g_doc.explodedViews.t==V::Arr&&index<g_doc.explodedViews.arr.size()){const auto name=g_doc.explodedViews.arr[index].find("name")->str;if(name!=p.selected){collapseExplodedDisplay();p.selected=name;p.step=-1;}}return true;}
    if(id>=47200&&id<47300){p.step=id-47200;return true;}
    if(id==47019){p.pending.clear();g_status="Picked components cleared";return true;}
    if(id==47004){std::string name;V step;if(!explodedStepDialog("New exploded view",step,true,true,&name))return true;if(name.empty())throw std::runtime_error("Name the exploded view");if(findExplodedView(g_doc,name))throw std::runtime_error("Exploded view "+name+" already exists");
        V c=V::makeObj(),steps=V::makeArr();steps.arr.push_back(step);c.set("op",V::makeStr("explode.set"));c.set("name",V::makeStr(name));c.set("steps",steps);c.set("replace",V::makeBool(false));applyCommand(c);
        if(!g_error){collapseExplodedDisplay();p.selected=name;p.step=0;p.pending.clear();g_status="Created exploded view "+name+" / revision "+std::to_string(g_session.revision)+" / Add step or Animate";}return true;}
    if(id>=47020&&id<47040){if(!p.visualize)openPanel(true);
        auto refresh=[&]{if(p.visualize)applyVisualizePresets(true);p.overlayValid=false;};
        if(id==47020){p.savedTraceRequest=!p.savedTraceRequest;refresh();g_status=p.savedTraceRequest?"GPU path tracing on / samples accumulate while the view is still":"Raster display";return true;}
        if(id>=47021&&id<=47023){p.look=id-47021;refresh();return true;}if(id>=47024&&id<=47026){p.background=id-47024;refresh();return true;}if(id>=47027&&id<=47030){p.resolution=id-47027;return true;}
        if(id==47031||id==47039){p.engine=id==47039?1:0;refresh();g_status=p.engine?"Software engine / exact resolution, agent-identical renderer":"GPU viewport engine / path tracer when available, raster otherwise";return true;}
        if(id>=47032&&id<=47034){static const int targets[]={64,256,1024},soft[]={4,9,16};p.samplesTarget=targets[id-47032];p.softwareSamples=soft[id-47032];return true;}
        if(id==47035){p.balloons=!p.balloons;p.overlayValid=false;return true;}if(id==47036){p.table=!p.table;p.overlayValid=false;return true;}
        if(id==47037){exportVisualize();return true;}if(id==47038){stopBakedMotion();g_status="Baked motion stopped / assembled pose";return true;}
        return true;}
    if(p.selected.empty()){g_status="Create an exploded view first: pick components, then New view";return true;}
    const auto* view=findExplodedView(g_doc,p.selected);if(!view)return true;V steps=*view->find("steps");
    if(id==47005){std::vector<EditorField> fields{{"Exploded view name",p.selected}};if(!editFields("Rename exploded view","Renames through one undoable, revision-checked transaction. A view driven by a motion channel cannot be renamed.",fields))return true;
        const auto name=trimField(fields[0].value);if(name.empty()||name==p.selected)return true;V c=V::makeObj();c.set("op",V::makeStr("explode.rename"));c.set("name",V::makeStr(p.selected));c.set("new_name",V::makeStr(name));applyCommand(c);if(!g_error){p.selected=name;g_status="Renamed exploded view to "+name+" / undo restores it";}return true;}
    if(id==47006){const auto name=p.selected;V c=V::makeObj();c.set("op",V::makeStr("explode.remove"));c.set("name",V::makeStr(name));collapseExplodedDisplay();applyCommand(c);if(!g_error){p.selected.clear();refreshExplodedViews();g_status="Deleted exploded view "+name+" / undo restores it";}return true;}
    if(id==47007){if(g_bakedMotions.active)stopBakedMotion();std::string reason;if(!explodedDisplayAvailable(reason)){g_status=reason;return true;}p.factor=0;p.animating=true;g_status="Animating "+p.selected+" from collapsed to exploded / display only";return true;}
    if(id==47012){setExplodeFactor(0);return true;}if(id==47013){setExplodeFactor(1);return true;}
    if(id==47014){if(steps.arr.size()>=kMaxExplodedSteps)throw std::runtime_error("An exploded view holds at most 64 steps");V step;if(!explodedStepDialog("Add explode step",step,false,false))return true;steps.arr.push_back(step);commitExplodedSteps(steps,"Added step");if(!g_error){p.step=(int)steps.arr.size()-1;p.pending.clear();}return true;}
    if(p.step<0||(size_t)p.step>=steps.arr.size()){g_status="Select a step in the list first";return true;}const size_t at=(size_t)p.step;
    if(id==47015){V step=steps.arr[at];if(!explodedStepDialog("Edit explode step",step,at==0,false))return true;steps.arr[at]=step;commitExplodedSteps(steps,"Edited step");return true;}
    if(id==47016){if(steps.arr.size()==1)throw std::runtime_error("A view keeps at least one step; delete the view instead");steps.arr.erase(steps.arr.begin()+(long)at);if(at==0&&steps.arr[0].find("with_previous"))steps.arr[0].obj.erase(std::remove_if(steps.arr[0].obj.begin(),steps.arr[0].obj.end(),[](const auto& f){return f.first=="with_previous";}),steps.arr[0].obj.end());commitExplodedSteps(steps,"Deleted step");if(!g_error)p.step=std::min<int>(p.step,(int)steps.arr.size()-1);return true;}
    if(id==47017||id==47018){const size_t other=id==47017?at-1:at+1;if((id==47017&&at==0)||other>=steps.arr.size())return true;std::swap(steps.arr[at],steps.arr[other]);for(auto* s:{&steps.arr[0]})s->obj.erase(std::remove_if(s->obj.begin(),s->obj.end(),[](const auto& f){return f.first=="with_previous";}),s->obj.end());commitExplodedSteps(steps,"Reordered steps");if(!g_error)p.step=(int)other;return true;}
    return true;
}
template<class Rect,class Button>static void drawExplodedViewsPanel(Rect rect,Button button,POINT pointer,std::string& hint){
    using V=arkjson::Value;auto& p=g_explode;
    if(p.open&&p.visualize)drawVisualizePreview();
    // Viewport banner: exploded positions are display transforms, never the assembled design.
    const bool motionDriven=g_bakedMotions.active&&p.motionFactor>0;
    if((p.applied||motionDriven)&&!(p.open&&p.visualize)){char text[200];std::snprintf(text,sizeof text,"EXPLODED DISPLAY  %s  t=%.2f%s",motionDriven?p.motionView.c_str():p.selected.c_str(),motionDriven?p.motionFactor:p.factor,motionDriven?"  (baked motion)":"");
        const float w=std::min((float)viewW-40,std::max(textWidth(text,.62f),textWidth("Display positions only / measure and move use the assembled design",.50f))+28),x=(float)viewX+(viewW-w)/2,y=(float)(viewY+viewH)-146;
        rect(x,y,w,46,7,.20f,.13f,.04f,.92f,.98f,.72f,.27f,.85f,1);textOut(fitText(text,.62f,w-24),x+12,y+5,.62f,1.f,.86f,.58f);textOut(fitText("Display positions only / measure and move use the assembled design",.50f,w-24),x+12,y+26,.50f,.92f,.82f,.66f);}
    if(!p.open)return;refreshExplodedViews();
    const float x0=(float)(viewX+viewW),y0=(float)viewY,w=(float)explodeDockWidth(),h=(float)(winH-32-viewY);if(w<200||h<200)return;const float left=x0+14,inner=w-28;
    auto muted=[&](const std::string& text,float x,float y,float scale,float width){textOut(fitText(text,scale,width),x,y,scale,.55f,.66f,.74f);};
    auto chip=[&](const std::string& label,float x,float y,float cw,int id,bool on,const char* tip=nullptr){const bool hover=pointer.x>=x&&pointer.x<x+cw&&pointer.y>=y-1&&pointer.y<y+21;
        rect(x,y,cw,20,10,on?.09f:hover?.15f:.10f,on?.30f:hover?.19f:.13f,on?.36f:hover?.23f:.16f,1,on?.22f:.28f,on?.70f:.36f,on?.80f:.42f,hover?1.f:.8f,1);textOut(fitText(label,.52f,cw-8),x+(cw-std::min(cw-8,textWidth(label,.52f)))/2,y+2,.52f,on?.90f:.80f,on?.98f:.88f,1.f);g_zones.push_back({x,y-1,cw,22,id});if(hover&&tip)hint=tip;};
    rect(x0,y0,w,h,0,.050f,.068f,.084f,1,.20f,.27f,.32f,.9f,1);rect(x0,y0,2,h,0,.86f,.62f,.22f,.55f,0,0,0,0,0);
    textOut(p.visualize?"VISUALIZE":"EXPLODED VIEWS",left,y0+13,.74f,.82f,.93f,.99f);button("Hide",x0+w-74,y0+7,60,p.visualize?47001:47000);
    {const float tw=(inner-6)/2;chip("Explode",left,y0+42,tw,47002,!p.visualize,"Exploded view steps, factor slider and Animate");chip("Visualize",left+tw+6,y0+42,tw,47003,p.visualize,"Assembly images: presets, progressive samples, balloons, BOM and PNG export");}
    rect(left,y0+70,inner,1,0,.20f,.29f,.35f,1,0,0,0,0,0);float y=y0+78;
    const V* views=g_doc.explodedViews.t==V::Arr?&g_doc.explodedViews:nullptr;const auto* view=p.selected.empty()?nullptr:findExplodedView(g_doc,p.selected);
    if(!p.visualize){
        if(!views){for(const char* line:{"No exploded views yet.","Click components in the viewport, then","New view to create the first step,","or ask the agent for explode.set.","Exploded positions are display only;","the saved design never moves."}){muted(line,left,y,.58f,inner);y+=20;}}
        else{const int rows=(int)views->arr.size(),visible=std::min(3,rows);p.scroll=std::clamp(p.scroll,0,std::max(0,rows-visible));
            for(int i=0;i<visible;++i){const size_t index=(size_t)(p.scroll+i);const auto& row=views->arr[index];const auto name=row.find("name")->str;const bool selected=name==p.selected;const bool hover=pointer.x>=left&&pointer.x<left+inner&&pointer.y>=y&&pointer.y<y+32;
                rect(left,y,inner,32,5,selected?.10f:hover?.09f:.065f,selected?.22f:hover?.15f:.09f,selected?.27f:hover?.18f:.11f,1,selected?.18f:.16f,selected?.62f:.24f,selected?.72f:.29f,selected?.9f:.6f,1);
                const auto drivers=motionsDrivingExplodedView(g_doc,name);char detail[96];std::snprintf(detail,sizeof detail,"%zu steps%s",row.find("steps")->arr.size(),drivers.empty()?"":"  /  driven by motion");
                textOut(fitText(name,.62f,inner*.55f),left+10,y+7,.62f,.88f,.94f,.98f);muted(detail,left+inner*.58f,y+9,.50f,inner*.40f);g_zones.push_back({left,y,inner,32,47100+(int)index});y+=36;}
            if(rows>visible){char more[64];std::snprintf(more,sizeof more,"%d-%d of %d",p.scroll+1,p.scroll+visible,rows);muted(more,left,y-2,.46f,inner*.5f);chip("Up",left+inner-96,y-4,44,47010,false);chip("Down",left+inner-46,y-4,46,47011,false);y+=20;}}
        {const float bw=(inner-12)/3;button("New view",left,y,bw,47004,!views);button("Rename",left+bw+6,y,bw,47005);button("Delete",left+2*(bw+6),y,bw,47006);y+=40;}
        rect(left,y,inner,1,0,.20f,.29f,.35f,1,0,0,0,0,0);y+=8;
        {char text[96];std::snprintf(text,sizeof text,"Explode factor  %.2f",p.factor);textOut(text,left,y,.60f,.84f,.92f,.97f);if(p.animating)muted("animating",left+inner-70,y+2,.50f,70);y+=22;
            const float trackX=left+2,trackW=inner-4,trackY=y+8;rect(trackX,trackY,trackW,6,3,.10f,.15f,.18f,1,.22f,.32f,.38f,.8f,1);
            if(view){ExplodedViewPlan plan;std::string error;if(compileExplodedView(g_doc,*view,plan,error))for(size_t s=1;s<plan.slices;++s){const float tx=trackX+trackW*(float)s/plan.slices;rect(tx-1,trackY-4,2,14,0,.40f,.52f,.58f,.8f,0,0,0,0,0);}}
            rect(trackX,trackY,trackW*(float)p.factor,6,3,.86f,.62f,.22f,1,0,0,0,0,0);rect(trackX+trackW*(float)p.factor-7,trackY-4,14,14,7,1.f,.93f,.80f,1,.86f,.62f,.22f,1,1);
            g_zones.push_back({trackX,trackY-9,trackW,24,47008});if(pointer.x>=trackX&&pointer.x<trackX+trackW&&pointer.y>=trackY-9&&pointer.y<trackY+15)hint="Drag to explode or collapse / ticks mark step slices / revision unchanged";y=trackY+20;
            const float bw=(inner-12)/3;button("Collapse",left,y,bw,47012);button("Animate",left+bw+6,y,bw,47007,true);button("Explode",left+2*(bw+6),y,bw,47013);y+=42;}
        rect(left,y,inner,1,0,.20f,.29f,.35f,1,0,0,0,0,0);y+=8;textOut("STEPS",left,y,.56f,.62f,.78f,.88f);
        if(view){const auto& steps=view->find("steps")->arr;muted(std::to_string(steps.size())+" / 64",left+inner-60,y,.50f,60);y+=20;const float bottom=y0+h-170;size_t index=0;
            for(const auto& step:steps){if(y>bottom){muted("+"+std::to_string(steps.size()-index)+" more steps",left,y,.48f,inner);y+=16;break;}const bool selected=(int)index==p.step;const bool hover=pointer.x>=left&&pointer.x<left+inner&&pointer.y>=y&&pointer.y<y+24;
                rect(left,y,inner,24,4,selected?.11f:hover?.085f:.06f,selected?.20f:hover?.13f:.08f,selected?.25f:hover?.16f:.10f,1,selected?.86f:.14f,selected?.62f:.20f,selected?.22f:.24f,selected?.8f:.5f,1);
                const bool together=step.find("with_previous")&&step.find("with_previous")->b;textOut(std::to_string(index+1),left+8,y+4,.52f,together?.55f:.86f,together?.66f:.62f,together?.74f:.22f);const auto* plan=explodedPlan(p.selected);textOut(fitText(explodedStepSummary(step,plan&&index<plan->steps.size()?&plan->steps[index]:nullptr),.52f,inner-34),left+26,y+4,.52f,.82f,.90f,.95f);
                g_zones.push_back({left,y,inner,24,47200+(int)index});y+=27;++index;}
            const float bw=(inner-18)/4;button("Edit",left,y+2,bw,47015);button("Delete",left+bw+6,y+2,bw,47016);button("Up",left+2*(bw+6),y+2,bw,47017);button("Down",left+3*(bw+6),y+2,bw,47018);y+=42;}
        else y+=24;
        std::string picked;for(u64 id:p.pending){if(picked.size()>60){picked+=" ...";break;}picked+=(picked.empty()?"":", ")+explodedInstanceName(id);}
        muted(p.pending.empty()?"Click components in the viewport to pick them":"Picked: "+picked,left,y,.50f,inner-60);if(!p.pending.empty())chip("Clear",left+inner-52,y-2,52,47019,false);y+=22;
        button("Add step",left,y,inner,47014,true);y+=42;
        muted(p.applied?"Exploded display / measure and move use the assembled design":"Display only / placements and B-rep never move",left,std::min(y,y0+h-22),.48f,inner);
    }else{
        const bool tracing=g_pathTracing&&g_trace.active;char engine[200];
        if(p.engine==1)std::snprintf(engine,sizeof engine,"Software renderer / exact %s / %d samples",p.resolution==0?"1280x800":p.resolution==1?"1280x720":p.resolution==2?"1920x1080":"1920x1080",p.softwareSamples);
        else if(tracing)std::snprintf(engine,sizeof engine,"GPU path tracer  %d / %d spp%s",g_trace.samples,p.samplesTarget,g_trace.samples>=p.samplesTarget?"  ready":"");
        else std::snprintf(engine,sizeof engine,"Raster%s%s",g_pathTracing&&!g_trace.reason.empty()?" / ":"",g_pathTracing?g_trace.reason.c_str():" / path tracing off");
        textOut(fitText(engine,.56f,inner),left,y,.56f,tracing?.52f:.84f,tracing?.90f:.88f,tracing?.64f:.94f);y+=24;
        auto row=[&](const char* title,std::initializer_list<std::pair<const char*,int>> items,int selected,const char* tip){muted(title,left,y,.50f,inner);y+=18;const float cw=(inner-6*((float)items.size()-1))/(float)items.size();float x=left;int i=0;for(const auto& item:items){chip(item.first,x,y,cw,item.second,i==selected,tip);x+=cw+6;++i;}y+=28;};
        row("ENGINE",{{"GPU viewport",47031},{"Software",47039}},p.engine,"GPU: path tracer when available, raster otherwise, at viewport pixels. Software: the agent renderer at exact size");
        if(p.engine==0){chip(p.savedTraceRequest?"Path tracing on":"Path tracing off",left,y,inner,47020,p.savedTraceRequest,"Progressive GPU path tracing; samples restart when the view or display changes");y+=28;}
        row("LOOK",{{"Studio",47021},{"Product",47022},{"Technical",47023}},p.look,"Lighting and display presets; restored when Visualize closes");
        row("BACKGROUND",{{"Dark",47024},{"Light",47025},{"White",47026}},p.background,nullptr);
        row("RESOLUTION",{{"Viewport",47027},{"1280x720",47028},{"1920x1080",47029},{"2560x1440",47030}},p.resolution,"GPU export crops the viewport to the aspect; enlarging is reported as upscaled");
        row(p.engine==0?"TARGET SAMPLES":"SAMPLES PER PIXEL",{{p.engine==0?"64":"4",47032},{p.engine==0?"256":"9",47033},{p.engine==0?"1024":"16",47034}},p.engine==0?(p.samplesTarget==64?0:p.samplesTarget==256?1:2):(p.softwareSamples==4?0:p.softwareSamples==9?1:2),nullptr);
        muted("ASSEMBLY DRAWING",left,y,.50f,inner);y+=18;{const float cw=(inner-6)/2;chip("Balloons",left,y,cw,47035,p.balloons,"BOM item numbers attached to components");chip("BOM table",left+cw+6,y,cw,47036,p.table,"Item, name and quantity from the BOM");y+=30;}
        refreshVisualizeBom();if(!p.bomError.empty()){textOut(fitText(p.bomError,.48f,inner),left,y,.48f,.98f,.56f,.44f);y+=18;}
        std::string state=g_bakedMotions.active?"Pose: baked motion "+g_bakedMotions.selected+" frame "+std::to_string(bakedMotionFrame()+1):p.applied?"Pose: exploded "+p.selected:"Pose: assembled";if(p.applied){char f[32];std::snprintf(f,sizeof f," at %.2f",p.factor);state+=f;}
        muted(state,left,y,.52f,g_bakedMotions.active?inner-60:inner);if(g_bakedMotions.active)chip("Stop",left+inner-52,y-2,52,47038,false);y+=24;
        button("Export PNG",left,y,inner,47037,true);y+=40;if(!p.lastExport.empty()){muted(p.lastExport,left,y,.48f,inner);y+=18;}
        muted("Visualization of display tessellation / not geometric authority",left,std::min(y,y0+h-22),.46f,inner);
    }
    g_zones.push_back({x0,y0,w,h,47009});
}
