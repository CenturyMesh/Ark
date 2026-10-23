// Shared geometry keeps navigation and display controls disjoint before paint.
struct NavigationTriadLayout {float x=0,y=0,size=0;RECT bounds{};};
static NavigationTriadLayout navigationTriadLayout(){
    NavigationTriadLayout r;r.size=std::max(88.f,std::min(std::clamp(viewH*.17f,112.f,152.f),viewH-72.f));r.x=(float)viewX+viewW-r.size-14;r.y=(float)viewY+viewH-r.size-14;
    r.bounds={(LONG)r.x-4,(LONG)r.y-4,(LONG)(r.x+r.size)+4,(LONG)(r.y+r.size)+4};return r;
}
struct ViewportBarLayout {float x=0,y=0,width=0,height=68,cell=0;bool visible=false,condensed=false;};
static ViewportBarLayout viewportBarLayout(){
    ViewportBarLayout r;if(g_drawingViewport.active||g_busy||g_toolsOpen||viewW<440||viewH<150)return r;
    r.cell=viewW>=(g_compactNavigation?1000:720)?44.f:36.f;r.width=g_compactNavigation?14*r.cell+76:10*r.cell+64;
    const auto compass=navigationTriadLayout().bounds;
    auto place=[&]{r.x=viewX+(viewW-r.width)*.5f;r.y=viewY+viewH-r.height-18;if(g_motionActive)r.y-=92;
        if(r.x+r.width>compass.left&&r.x<compass.right&&r.y+r.height>compass.top){const float beside=(float)compass.left-r.width-12;if(beside>=viewX+12)r.x=beside;else r.y=(float)compass.top-r.height-10;}};
    place();const float preferredCube=(float)std::max(44,std::min((int)std::lround(136.f+24.f*std::clamp((viewW-622.f)/420.f,0.f,1.f)),viewH-26));
    if(r.width>viewW-24||r.y<viewY+10||(g_showViewCube&&r.x<viewX+14+preferredCube+8&&r.y<viewY+12+preferredCube+8)){
        // Preserve readable orientation and direct styles in constrained panes;
        // the existing More menu exposes every collapsed action explicitly.
        r.condensed=true;r.cell=36;r.width=4*r.cell+28;place();
    }
    r.visible=r.y>=viewY+10;return r;
}
