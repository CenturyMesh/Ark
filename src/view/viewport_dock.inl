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
    case 963:g_showEdges=!g_showEdges;wireframe=false;saveUiSettings();break;
    case 964:frameScene();break;
    case 965:if(!g_busy&&!g_directAuthoring.active&&!g_sketchEditing.active&&!g_sketchDrawing.active)openViewportSelectionDrawing();break;
    default:break; // The glass panel also consumes clicks between controls.
    }return true;
}
template<class Rect,class Glyph>static void drawViewportDock(Rect rect,Glyph glyph,POINT pointer){
    if(g_drawingViewport.active||g_busy||g_toolsOpen||viewW<440||viewH<150)return;
    const float width=414,height=66,x=viewX+(viewW-width)*.5f;
    float y=viewY+viewH-height-18;
    if(g_motionActive)y-=92;
    if(x+width>g_compassBounds.left&&x<g_compassBounds.right&&y+height>g_compassBounds.top)y=(float)g_compassBounds.top-height-10;
    if(y<viewY+(viewH<190?40:90))return;
    rect(x-5,y+3,width+10,height+10,19,0,0,0,.13f,0,0,0,0,0);
    // 75% transparent panel; icons and labels retain their own readable opacity.
    rect(x,y,width,height,15,.035f,.065f,.085f,.25f,.23f,.40f,.48f,.65f,1);
    rect(x+12,y+1,width-24,1,0,.39f,.75f,.86f,.30f,0,0,0,0,0);
    const bool drawingReady=viewportDrawingSource().t==arkjson::Value::Obj&&!g_directAuthoring.active&&!g_sketchEditing.active&&!g_sketchDrawing.active;
    const bool active[]={g_showDatumPlanes,g_showSketches,g_grid,g_showEdges,false,false};
    const char* labels[]={"Planes","Sketches","Grid","Edges","Fit","2D drawing"};
    const char* tips[]={g_showDatumPlanes?"Hide all construction planes and datums":"Show all construction planes and datums",g_showSketches?"Hide sketch overlays":"Show sketch overlays",g_grid?"Hide grid":"Show grid",g_showEdges?"Hide body edges":"Show body edges","Fit document",drawingReady?"Open drawings for the selected body or container":"Select a body or container; finish active edits to open its drawing"};
    std::vector<Zone> zones;std::string tip;
    for(int i=0;i<6;++i){const float bx=x+8+i*62+(i==5?12:0),bw=i==5?76:58;const bool enabled=i!=5||drawingReady,hover=pointer.x>=bx&&pointer.x<bx+bw&&pointer.y>=y+5&&pointer.y<y+height-5;
        if(active[i]){rect(bx-1,y+5,bw+2,height-10,11,.04f,.27f,.32f,.55f,.14f,.72f,.80f,.28f,1);rect(bx+17,y+height-9,bw-34,2,1,.30f,.91f,.95f,1,0,0,0,0,0);}
        if(hover){rect(bx,y+5,bw,height-10,10,.13f,.25f,.31f,enabled?.85f:.3f,.29f,.63f,.73f,enabled?.6f:.2f,1);tip=tips[i];}
        glyph(960+i,bx+(bw-27)*.5f,y+10,27,enabled?(active[i]?1.f:.86f):.33f);
        const float scale=.53f;textOut(labels[i],bx+(bw-textWidth(labels[i],scale))*.5f,y+41,scale,enabled?.77f:.36f,enabled?.88f:.42f,enabled?.93f:.46f);
        zones.push_back({bx,y+5,bw,height-10,enabled?960+i:969});
    }
    rect(x+322,y+13,1,40,0,.28f,.47f,.55f,.5f,0,0,0,0,0);
    zones.push_back({x,y,width,height,969});g_zones.insert(g_zones.begin(),zones.begin(),zones.end());
    if(!tip.empty()){const float tw=std::min((float)viewW-24,textWidth(tip,.57f)+24),tx=std::clamp(x+(width-tw)*.5f,(float)viewX+8,(float)(viewX+viewW)-tw-8);rect(tx,y-36,tw,28,7,.035f,.07f,.09f,.98f,.24f,.48f,.57f,.8f,1);textOut(fitText(tip,.57f,tw-20),tx+10,y-29,.57f,.81f,.94f,.97f);}
}
