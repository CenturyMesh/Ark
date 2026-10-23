// Display-only controls, rendered in the CAD viewport. Drawing creation remains explicit.
static arkjson::Value viewportDrawingSource(){
    using V=arkjson::Value;V source=V::makeObj();
    if(g_viewportContainer){for(const auto& node:g_doc.assembly)if(node.id==g_viewportContainer&&!node.feature){source.set("kind",V::makeStr("assembly"));source.set("assembly",V::makeNum((double)node.id));return source;}}
    const u64 feature=selBody>=0&&(size_t)selBody<g_bodyIds.size()?g_bodyIds[(size_t)selBody]:g_explorerFeature;
    if(feature&&g_doc.find(feature)){source.set("kind",V::makeStr("part"));source.set("feature",V::makeNum((double)feature));return source;}return {};
}
static void openViewportSelectionDrawing(){
    const auto source=viewportDrawingSource();if(source.t!=arkjson::Value::Obj)throw std::runtime_error("Select a body or container to open its 2D drawing");
    beginDrawingViewport();auto& state=g_drawingViewport;state.focusSource=source;state.assemblies=sheetText(source,"kind")=="assembly";state.sheet.name.clear();state.selectedSource={};drawingViewportRead();
    g_status=state.names.empty()?"No drawing for this selection / New sheet creates one explicitly":"2D drawing of the selected source / Esc returns to design";
}
static bool viewportDockAction(int id){
    if(id<960||id>969)return false;
    switch(id){
    case 960:g_showDatumPlanes=!g_showDatumPlanes;if(g_showDatumPlanes)g_hiddenDatumNames.clear();rebuildSketchOverlay();saveUiSettings();g_status=g_showDatumPlanes?"All construction planes and datums shown":"Construction planes and datums hidden";break;
    case 961:g_showSketches=!g_showSketches;rebuildSketchOverlay();g_status=g_showSketches?"Sketch overlays shown":"Sketch overlays hidden";break;
    case 962:g_grid=!g_grid;saveUiSettings();break;
    case 963:g_showEdges=wireframe||!g_showEdges;wireframe=false;g_pathTracing=false;saveUiSettings();g_status=g_showEdges?"Shaded with edges":"Shaded";break;
    case 964:frameScene();break;
    case 965:if(!g_busy&&!g_directAuthoring.active&&!g_sketchEditing.active&&!g_sketchDrawing.active)openViewportSelectionDrawing();break;
    default:break; // The panel also consumes clicks between controls.
    }return true;
}
template<class Rect,class Glyph>static void drawViewportDock(Rect rect,Glyph glyph,POINT pointer){
    if(g_drawingViewport.active||g_busy||g_toolsOpen||viewW<440||viewH<150)return;
    const float cell=viewW>=720?44.f:36.f,width=10*cell+64,height=68;
    float x=viewX+(viewW-width)*.5f;
    float y=viewY+viewH-height-18;
    if(g_motionActive)y-=92;
    if(g_compactNavigation)y-=48;
    if(x+width>g_compassBounds.left&&x<g_compassBounds.right&&y+height>g_compassBounds.top){
        const float beside=(float)g_compassBounds.left-width-12;
        if(beside>=viewX+12)x=beside;else y=(float)g_compassBounds.top-height-10;
    }
    if(y<viewY+(viewH<190?40:90))return;
    rect(x-2,y+3,width+4,height+4,10,0,0,0,.20f,0,0,0,0,0);
    rect(x,y,width,height,8,.075f,.094f,.117f,.96f,.25f,.30f,.36f,.90f,1);
    rect(x+9,y+1,width-18,1,0,.52f,.61f,.70f,.14f,0,0,0,0,0);
    const bool drawingReady=viewportDrawingSource().t==arkjson::Value::Obj&&!g_directAuthoring.active&&!g_sketchEditing.active&&!g_sketchDrawing.active;
    const int ids[]={462,963,464,960,961,962,175,964,965,455};
    const bool active[]={!g_pathTracing&&!wireframe&&!g_showEdges,!g_pathTracing&&!wireframe&&g_showEdges,!g_pathTracing&&wireframe,g_showDatumPlanes,g_showSketches,g_grid,g_cameraOrtho,false,false,false};
    const char* tips[]={"Shaded surfaces",active[1]?"Shaded with edges / click to hide edges":"Shaded with edges","Wireframe",g_showDatumPlanes?"Hide all construction planes and datums":"Show all construction planes and datums",g_showSketches?"Hide sketch overlays":"Show sketch overlays",g_grid?"Hide grid":"Show grid",g_cameraOrtho?"Orthographic projection / click for perspective":"Perspective projection / click for orthographic","Fit document",drawingReady?"Open drawings for the selected body or container":"Select a body or container; finish active edits to open its drawing","Display settings and named views"};
    const char* style=g_pathTracing?"Path trace":wireframe?"Wireframe":g_showEdges?"Shaded + edges":"Shaded";
    const char* headings[]={style,"Overlays","View","2D","More"};const int groupSizes[]={3,3,2,1,1};
    std::vector<Zone> zones;std::string tip;float bx=x+8,tipCenter=x+width*.5f;int index=0;
    for(int group=0;group<5;++group){const float groupWidth=cell*groupSizes[group];
        const float scale=.64f;textOut(headings[group],bx+(groupWidth-textWidth(headings[group],scale))*.5f,y+7,scale,.66f,.73f,.81f);
        for(int j=0;j<groupSizes[group];++j,++index,bx+=cell){const float by=y+28,bw=cell-4,bh=32;
            const bool enabled=ids[index]!=965||drawingReady,hover=pointer.x>=bx&&pointer.x<bx+bw&&pointer.y>=by&&pointer.y<by+bh;
            if(active[index])rect(bx,by,bw,bh,5,.16f,.28f,.40f,1,.33f,.53f,.72f,.85f,1);
            if(hover){rect(bx,by,bw,bh,5,.22f,.34f,.46f,enabled?.90f:.22f,.42f,.61f,.80f,enabled?.8f:.2f,1);tip=tips[index];tipCenter=bx+bw*.5f;}
            glyph(ids[index],bx+(bw-22)*.5f,by+4,22,enabled?(active[index]?1.f:.84f):.32f);
            if(active[index])rect(bx+bw*.5f-5,by+bh-4,10,2,1,.56f,.77f,.96f,1,0,0,0,0,0);
            zones.push_back({bx,by,bw,bh,enabled?ids[index]:969});
        }
        if(group<4){rect(bx+3,y+13,1,height-26,0,.30f,.36f,.43f,.60f,0,0,0,0,0);bx+=12;}
    }
    zones.push_back({x,y,width,height,969});g_zones.insert(g_zones.begin(),zones.begin(),zones.end());
    if(!tip.empty()){const float tw=std::min((float)viewW-24,textWidth(tip,.68f)+24),tx=std::clamp(tipCenter-tw*.5f,(float)viewX+8,(float)(viewX+viewW)-tw-8);rect(tx,y-37,tw,29,5,.075f,.094f,.117f,.99f,.32f,.41f,.51f,.8f,1);textOut(fitText(tip,.68f,tw-20),tx+10,y-30,.68f,.88f,.93f,.98f);}
}
