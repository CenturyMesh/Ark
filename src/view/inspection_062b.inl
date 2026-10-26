struct SmartEntity062 {SelectionItem item;arkjson::Value data;};
static std::vector<SmartEntity062> g_smart062;
static arkjson::Value g_smartPair062;
static arkjson::Value inspectionVec062(Vec3 p){auto a=arkjson::Value::makeArr();for(double n:{p.x,p.y,p.z})a.arr.push_back(arkjson::Value::makeNum(n));return a;}
static Vec3 inspectionPoint062(const arkjson::Value& v,const char* key){auto* a=v.find(key);return a&&a->arr.size()==3?Vec3{a->arr[0].num,a->arr[1].num,a->arr[2].num}:Vec3{};}
static arkjson::Value inspectionRequest062(arkjson::Value args){using V=arkjson::Value;V request=V::makeObj();request.set("operation",V::makeStr("measurement_pick"));request.set("request",args);auto run=[request]{return nativeWorkerRequest(request);};return g_headless?run():callSessionWithProgress("measurement_pick",V::makeObj(),run);}
static arkjson::Value inspectionSource062(size_t body){
    using V=arkjson::Value;const Body* source=nullptr;for(const auto& b:g_doc.evalBodies)if(b.id==g_bodyIds[body]){source=&b;break;}
    auto* brep=source?source->nativeData.find("brep_base64"):nullptr;if(!brep)throw std::runtime_error("Exact inspection requires a B-rep body; this display has no exact source");
    V r=V::makeObj(),m=V::makeArr();r.set("brep",*brep);for(double n:g_instanceWorld[body].m)m.arr.push_back(V::makeNum(n));r.set("placement",m);return r;
}
static void resetSmart062(){g_smart062.clear();g_smartPair062={};g_picked.clear();refreshSelectionLines();clearSelectionHover();}
static void syncSmartSelection062(){g_picked.clear();for(const auto& e:g_smart062)g_picked.push_back(e.item);refreshSelectionLines();}
static void updateSmartPair062(){
    using V=arkjson::Value;g_smartPair062={};if(g_smart062.size()!=2)return;auto r=V::makeObj();r.set("action",V::makeStr("between"));r.set("brep",*g_smart062[0].data.find("brep"));r.set("other_brep",*g_smart062[1].data.find("brep"));g_smartPair062=inspectionRequest062(r);
    const auto& a=g_smart062[0].data;const auto& b=g_smart062[1].data;
    auto ta=a.find("type")->str,tb=b.find("type")->str;
    if(a.find("direction")&&b.find("direction")&&ta!="Sphere"&&tb!="Sphere"){double cosine=std::clamp(std::abs(inspectionPoint062(a,"direction").dot(inspectionPoint062(b,"direction"))),0.,1.);double angle=std::acos(cosine)*180/K_PI;bool planeA=ta=="Plane"||ta=="Circle"||ta=="Arc",planeB=tb=="Plane"||tb=="Circle"||tb=="Arc";if(planeA!=planeB)angle=90-angle;g_smartPair062.set("angle_deg",V::makeNum(angle));}
}
static bool clickSmart062(int x,int y,bool additive){
    using V=arkjson::Value;
    try{
        if(g_measure.revision!=g_session.revision||g_measure.epoch!=g_documentEpoch){endMeasurement();throw std::runtime_error("Drawing changed; press D to measure again");}
        auto hit=namedEdgeAtPointer(x,y);if(!hit.hit){int old=g_pickKind;g_pickKind=1;hit=referenceAtPointer(x,y);g_pickKind=old;}
        if(!hit.hit)throw std::runtime_error("Point at a visible edge or face / Ctrl adds another entity");
        if(hit.body<0||(hit.item.kind!="edge"&&hit.item.kind!="face"))throw std::runtime_error("Smart dimensions require exact B-rep edges and faces");
        auto found=std::find_if(g_smart062.begin(),g_smart062.end(),[&](const SmartEntity062& e){return sameReference(e.item,hit.item);});
        if(additive&&found!=g_smart062.end())g_smart062.erase(found);
        else {auto r=inspectionSource062((size_t)hit.body);r.set("action",V::makeStr("entity"));r.set("kind",V::makeStr(hit.item.kind));r.set("index",V::makeNum(hit.edge>=0?hit.edge:hit.face));auto data=inspectionRequest062(r);if(!additive)g_smart062.clear();g_smart062.push_back({hit.item,std::move(data)});}
        syncSmartSelection062();updateSmartPair062();g_error=false;g_status="Exact measurement / Ctrl adds or removes / D or Esc closes";
    }catch(const std::exception& e){g_error=true;g_status=e.what();}return true;
}
static void smartHover062(int x,int y){int old=g_pickKind;g_pickKind=2;updateSelectionHover(x,y);if(!g_hoverReference.hit){g_pickKind=1;updateSelectionHover(x,y);}g_pickKind=old;}

// Screen-space dimension lines: continuous strokes, extension lines and arrows.
template<class Rect>static void inspectionStroke062(Rect rect,float x,float y,float u,float v,float r,float g,float b,float thickness=1.5f){
    int steps=std::min(6000,std::max(1,(int)std::ceil(std::hypot(u-x,v-y))));for(int i=0;i<=steps;++i){float t=(float)i/steps,sx=x+(u-x)*t,sy=y+(v-y)*t;if(drawingInView((int)sx,(int)sy))rect(sx-thickness/2,sy-thickness/2,thickness,thickness,thickness/2,r,g,b,1,0,0,0,0,0);}
}
template<class Rect>static void dimensionGraphic062(Rect rect,Vec3 a,Vec3 b,const std::string& label,int ordinal){
    float x,y,u,v;if(!projectScreen(a,x,y)||!projectScreen(b,u,v))return;float length=std::hypot(u-x,v-y);if(length<2)return;float dx=(u-x)/length,dy=(v-y)/length,nx=-dy,ny=dx,offset=26+ordinal*21.f;
    float ax=x+nx*offset,ay=y+ny*offset,bx=u+nx*offset,by=v+ny*offset;
    auto stroke=[&](float a,float b,float c,float d){inspectionStroke062(rect,a,b,c,d,.36f,.84f,1.f);};
    stroke(x,y,ax+nx*6,ay+ny*6);stroke(u,v,bx+nx*6,by+ny*6);stroke(ax,ay,bx,by);
    for(int end=0;end<2;++end){float px=end?bx:ax,py=end?by:ay,sign=end?-1.f:1.f;stroke(px,py,px+sign*dx*8+nx*3,py+sign*dy*8+ny*3);stroke(px,py,px+sign*dx*8-nx*3,py+sign*dy*8-ny*3);}
    float width=textWidth(label,.65f)+20,cx=std::clamp((ax+bx)/2,(float)viewX+width/2+5,(float)(viewX+viewW)-width/2-5),cy=std::clamp((ay+by)/2,(float)viewY+18,(float)(viewY+viewH)-18);
    rect(cx-width/2,cy-13,width,26,6,.025f,.075f,.105f,.98f,.25f,.65f,.8f,.9f,1);textOut(label,cx-width/2+10,cy-8,.65f,.83f,.97f,1);
}
template<class Rect,class Button>static void drawSmart062(Rect rect,Button button){
    using V=arkjson::Value;
    if(g_smart062.size()==2&&g_smartPair062.find("distance_mm"))dimensionGraphic062(rect,inspectionPoint062(g_smartPair062,"a"),inspectionPoint062(g_smartPair062,"b"),g_displayUnits.distance(g_smartPair062.find("distance_mm")->num),0);
    else for(size_t i=0;i<g_smart062.size();++i){const auto& d=g_smart062[i].data;auto* diameter=d.find("diameter_mm"),*length=d.find("length_mm");if(d.find("a")&&d.find("b"))dimensionGraphic062(rect,inspectionPoint062(d,"a"),inspectionPoint062(d,"b"),diameter?"DIA "+g_displayUnits.distance(diameter->num):length?g_displayUnits.distance(length->num):"",(int)i%3);}
    float w=std::min(370.f,(float)viewW-30),x=viewX+viewW-w-15,y=viewY+58.f,h=196+std::min<size_t>(g_smart062.size(),4)*43.f;
    g_zones.erase(std::remove_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.x<x+w&&z.x+z.w>x&&z.y<y+h&&z.y+z.h>y;}),g_zones.end());
    rect(x+3,y+5,w,h,10,0,0,0,.2f,0,0,0,0,0);rect(x,y,w,h,10,.035f,.065f,.09f,.98f,.18f,.38f,.48f,.9f,1);rect(x,y,4,h,2,.23f,.77f,.95f,1,0,0,0,0,0);
    textOut("SMART MEASURE",x+16,y+13,.78f,.83f,.95f,1);textOut("D",x+w-30,y+14,.7f,.35f,.85f,1);
    textOut("Click edge or face / Ctrl adds or removes",x+16,y+43,.54f,.64f,.76f,.83f);
    std::string main="Choose an entity",detail="Length / diameter / area / minimum distance";
    if(g_smart062.size()==2&&g_smartPair062.find("distance_mm")){main=g_displayUnits.distance(g_smartPair062.find("distance_mm")->num);detail="Minimum distance";if(auto* a=g_smartPair062.find("angle_deg"))detail+="  /  Angle "+drawingNumber(a->num)+" deg";}
    else if(g_smart062.size()==1){const auto& d=g_smart062[0].data;if(auto* n=d.find("diameter_mm")){main="DIA "+g_displayUnits.distance(n->num);detail="Radius "+g_displayUnits.distance(n->num/2);}else if(auto* n=d.find("length_mm")){main=g_displayUnits.distance(n->num);detail="Exact edge length";}else if(auto* n=d.find("area_mm2")){main=g_displayUnits.area(n->num);detail="Exact surface area";}}
    else if(g_smart062.size()>2){double total=0;for(const auto& e:g_smart062)if(auto* n=e.data.find("length_mm"))total+=n->num;main=g_displayUnits.distance(total);detail="Total selected edge length / "+std::to_string(g_smart062.size())+" entities";}
    textOut(fitText(main,.99f,w-32),x+16,y+75,.99f,.55f,.9f,1);textOut(fitText(detail,.55f,w-32),x+16,y+110,.55f,.72f,.85f,.91f);
    float row=y+139;for(size_t i=0;i<std::min<size_t>(g_smart062.size(),4);++i){const auto& d=g_smart062[i].data;std::string label=d.find("type")->str;if(auto* n=d.find("length_mm"))label+="  /  L "+g_displayUnits.distance(n->num);if(auto* n=d.find("area_mm2"))label+="  /  "+g_displayUnits.area(n->num);rect(x+12,row,w-24,35,5,.07f,.12f,.15f,1,0,0,0,0,0);textOut(fitText(label,.58f,w-44),x+22,row+10,.58f,.76f,.88f,.93f);row+=43;}
    button("Clear",x+12,y+h-43,82,49801);button("Units",x+102,y+h-43,82,587);button("Close",x+w-94,y+h-43,82,589);
    g_zones.push_back({x,y,w,h,969});
}

static void buildSection062(Vec3 origin,Vec3 normal,const std::string& label){
    using V=arkjson::Value;if(g_busy)throw std::runtime_error("Wait for the current operation");if(measurementPreviewActive())throw std::runtime_error("Return to the stationary formed model before sectioning");
    std::map<size_t,V> results;u64 revision=g_session.revision,epoch=g_documentEpoch;
    // Prepare every visible part before replacing the current view (all-or-nothing).
    for(size_t i=0;i<g_bodyIds.size();++i)if(displayInstance(i)){auto r=inspectionSource062(i);r.set("action",V::makeStr("section"));r.set("origin",inspectionVec062(origin));r.set("normal",inspectionVec062(normal));results.emplace(i,inspectionRequest062(r));}
    if(results.empty())throw std::runtime_error("No visible exact bodies to section");if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed during section preparation");
    if(measurementActive())endMeasurement();endSection062();cancelDirectAuthoring();cancelSketchDrawing();cancelSketchEditing();cancelComponentMove();g_pickKind=0;g_picked.clear();refreshSelectionLines();clearSelectionHover();g_solidDimensions={};
    g_section062.origin=origin;g_section062.normal=normal.normalized();g_section062.label=label;g_section062.revision=revision;g_section062.epoch=epoch;
    try{for(auto& entry:results){auto& p=g_section062.parts[entry.first];if(auto* mesh=entry.second.find("mesh")){p.mesh=nativePreviewMesh(*mesh);p.bvh=MeshBVH(p.mesh);uploadMesh(p.gpu,p.mesh);}if(auto* cap=entry.second.find("cap"))uploadMesh(p.cap,nativePreviewMesh(*cap));g_section062.area+=entry.second.find("section_area_mm2")->num;}}catch(...){endSection062();throw;}
    g_section062.hidden=g_hiddenBodies;g_section062.families=g_familyFilter054B;g_section062.active=true;g_error=false;g_status="Section view / source unchanged / Flip, Offset or Close";
}
static void sectionPlaneDialog062(){
    std::vector<EditorField> fields{{"Plane / XY, XZ, YZ or named datum","XY"},{"Offset / mm","0"}};
    if(!editFields("Section view","Inspection plane and signed offset. The saved design is unchanged.",fields))return;
    auto frame=resolvePlane(g_doc,fields[0].value);auto n=frame.transformDir({0,0,1}).normalized();buildSection062(frame.transformPoint({0,0,0})+n*formNumber(fields[1].value),n*-1,fields[0].value);
}
static void beginSectionLine062(){if(measurementPreviewActive())throw std::runtime_error("Return to the stationary formed model before sectioning");if(measurementActive())endMeasurement();endSection062();cancelDirectAuthoring();cancelSketchDrawing();cancelSketchEditing();cancelComponentMove();g_solidDimensions={};g_navMode=0;g_pickKind=0;g_section062.drawing=true;g_section062.revision=g_session.revision;g_section062.epoch=g_documentEpoch;g_status="Section line / click two endpoints across the model / Esc cancels";}
static bool clickSectionLine062(int x,int y){
    validateSection062();if(!g_section062.drawing||!drawingInView(x,y))return false;
    if(!g_section062.first){g_section062.first=true;g_section062.x=x;g_section062.y=y;return true;}
    try{if(std::hypot(x-g_section062.x,y-g_section062.y)<8)throw std::runtime_error("Draw a section line at least 8 pixels long");
        Vec3 o,a,p,b;viewRay(g_section062.x,g_section062.y,o,a);viewRay(x,y,p,b);auto basis=cameraFrame(yaw,pitch,cameraRoll);Vec3 normal=g_cameraOrtho?(p-o).cross(basis.outward).normalized():a.cross(b).normalized();
        if(normal.len()<1e-10)throw std::runtime_error("Section line does not define a plane");buildSection062(o,normal,"Screen line");
    }catch(const std::exception& e){g_error=true;g_status=e.what();}return true;
}
static void sectionAction062(int action){
    if(action==49815){
        const auto* datum=authoredDatum(g_selectedDatumName);
        const bool plane=datum?datum->kind=="plane":g_selectedDatumName=="XY"||g_selectedDatumName=="XZ"||g_selectedDatumName=="YZ";
        if(!plane)throw std::runtime_error("Create section view requires a construction plane");
        const auto frame=resolvePlane(g_doc,g_selectedDatumName);
        buildSection062(frame.transformPoint({}),frame.transformDir({0,0,-1}).normalized(),g_selectedDatumName);
    }
    else if(action==49810)sectionPlaneDialog062();else if(action==49811)beginSectionLine062();else if(action==49814){endSection062();g_status="Full model restored / design unchanged";}
    else if(g_section062.active&&action==49812)buildSection062(g_section062.origin,g_section062.normal*-1,g_section062.label);
    else if(g_section062.active&&action==49813){std::vector<EditorField> fields{{"Move section / mm","0"}};if(editFields("Section offset","Move along the current section normal",fields))buildSection062(g_section062.origin+g_section062.normal*formNumber(fields[0].value),g_section062.normal,g_section062.label);}
}
static SurfaceHit sectionPick062(int mx,int my,std::vector<SurfaceHit>* hits){
    SurfaceHit best;Vec3 eye,direction;viewRay(mx,my,eye,direction);double closest=1e300;for(const auto& entry:g_section062.parts){if(!displayInstance(entry.first)||entry.second.mesh.idx.empty())continue;auto hit=entry.second.bvh.raycast(entry.second.mesh,{eye,direction});if(!hit.hit)continue;SurfaceHit s{(int)entry.first,hit.triangle,g_instanceWorld[entry.first].inverted().transformPoint(hit.point),true,hit.distance};if(hits)hits->push_back(s);if(s.distance<closest){closest=s.distance;best=s;}}return best;
}
template<class Rect,class Button>static void drawSection062(Rect rect,Button button){
    validateSection062();if(!g_section062.active&&!g_section062.drawing)return;
    if(g_section062.drawing&&g_section062.first){inspectionStroke062(rect,(float)g_section062.x,(float)g_section062.y,(float)g_pointerX,(float)g_pointerY,.98f,.68f,.3f,2);}
    float w=std::min(370.f,(float)viewW-30),x=viewX+viewW-w-15,y=viewY+58.f;
    g_zones.erase(std::remove_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.x<x+w&&z.x+z.w>x&&z.y<y+164&&z.y+z.h>y;}),g_zones.end());rect(x,y,w,164,10,.065f,.065f,.075f,.98f,.67f,.43f,.22f,.9f,1);
    textOut("SECTION VIEW",x+15,y+14,.8f,1,.8f,.51f);textOut(g_section062.drawing?"Click two endpoints across the model":g_section062.label+"  /  "+g_displayUnits.area(g_section062.area),x+15,y+48,.62f,.9f,.83f,.7f);textOut("Inspection only / saved model unchanged",x+15,y+79,.53f,.7f,.75f,.8f);
    if(g_section062.active){button("Flip",x+12,y+113,80,49812);button("Offset",x+100,y+113,80,49813);}button("Close",x+w-92,y+113,80,49814);
    g_zones.push_back({x,y,w,164,969});
}
