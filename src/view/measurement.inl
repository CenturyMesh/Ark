struct MeasurementState {int mode=0,count=0;u64 revision=0,epoch=0;Mat4 frame;std::string plane="XY";Vec3 a,b;std::string sourceA,sourceB;};
static MeasurementState g_measure;
static bool measurementPreviewActive(){return g_allSheetsFlat||g_allSheetsPlaying||g_stockMode||g_motionActive||g_foldProgress!=0||g_sheetPlayingId||g_componentMove.active||std::any_of(g_sheetProgress.begin(),g_sheetProgress.end(),[](const auto& p){return p.second<1;})||std::any_of(g_sheetFlatViews.begin(),g_sheetFlatViews.end(),[](const auto& p){return p.second;});}
static bool measurementActive(){return g_measure.mode!=0;}
static void endMeasurement(){g_measure={};g_status="Measurement closed";}
static void beginMeasurement(int mode){
    if(g_busy)throw std::runtime_error("Wait for the current operation before measuring");
    if(measurementPreviewActive())throw std::runtime_error("Return to the formed, stationary model before measuring");
    cancelSketchDrawing();cancelComponentMove();g_pickKind=0;g_navMode=0;
    MeasurementState next;next.mode=mode;next.revision=g_session.revision;next.epoch=g_documentEpoch;
    if(mode==2){next.plane=g_activePlane;if(auto* sk=g_doc.findSketch(g_activeSketch))next.plane=sk->sk.plane;next.frame=resolvePlane(g_doc,next.plane);g_activePlane=next.plane;g_showSketches=true;rebuildSketchOverlay();alignSketchCamera();}
    else next.frame=Mat4::identity();g_measure=std::move(next);g_status=mode==2?"Measure 2D / click two points on the active plane / Esc closes":"Measure 3D / click two surface points / native vertices snap / Esc closes";
}
static void acceptMeasurementPoint(Vec3 point,const std::string& source){
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||!std::isfinite(point.len())||(g_measure.count==1&&!std::isfinite((point-g_measure.a).len())))throw std::runtime_error("Measurement coordinates exceed the numeric range");
    if(g_measure.revision!=g_session.revision||g_measure.epoch!=g_documentEpoch){endMeasurement();throw std::runtime_error("Drawing changed; start a new measurement");}
    if(g_measure.count==0||g_measure.count==2){g_measure.a=point;g_measure.sourceA=source;g_measure.count=1;}
    else{g_measure.b=point;g_measure.sourceB=source;g_measure.count=2;}
    g_status=g_measure.count==1?source+" / choose second point":"Measurement ready / click starts another pair / Esc closes";
}
static bool clickMeasurement(int x,int y){
    if(!measurementActive()||!drawingInView(x,y))return false;
    try{
        Vec3 origin,ray;viewRay(x,y,origin,ray);
        if(g_measure.mode==2){auto inverse=g_measure.frame.inverted();auto local=inverse.transformPoint(origin),direction=inverse.transformDir(ray);if(std::abs(direction.z)<1e-10)throw std::runtime_error("View is parallel to the measurement plane; choose Normal view");double t=-local.z/direction.z;if(t<0)throw std::runtime_error("Measurement plane is behind the camera");Vec3 point=local+direction*t;
            // Reuse the sketch's authored endpoint/center acquisition, not a mesh.
            auto previous=g_sketchDrawing;g_sketchDrawing={};g_sketchDrawing.frame=g_measure.frame;g_sketchDrawing.name=g_activeSketch;auto snapped=drawingSnap({point.x,point.y},x,y);g_sketchDrawing=std::move(previous);
            acceptMeasurementPoint({snapped.point.x,snapped.point.y,0},snapped.label.empty()?"Plane point":snapped.label);
        }else{
            auto hit=pickSurface(x,y);if(!hit.hit)throw std::runtime_error("Click a visible surface, or use Coordinates");size_t bodyIndex=(size_t)hit.body;const Body* body=nullptr;for(const auto& b:g_doc.evalBodies)if(b.id==g_bodyIds[bodyIndex]){body=&b;break;}
            const auto* brep=body?body->nativeData.find("brep_base64"):nullptr;
            if(!brep){auto* feature=g_doc.find(g_bodyIds[bodyIndex]);if(feature&&(feature->type==FeatureType::SourceTube||feature->type==FeatureType::NotchedTube))throw std::runtime_error("Use exact formed geometry or Coordinates for this tube preview");acceptMeasurementPoint(g_instanceWorld[bodyIndex].transformPoint(hit.local),"Display mesh estimate");}
            else{using V=arkjson::Value;auto vector=[](Vec3 v){V a=V::makeArr();for(double n:{v.x,v.y,v.z})a.arr.push_back(V::makeNum(n));return a;};auto inverse=g_instanceWorld[bodyIndex].inverted();V request=V::makeObj(),args=V::makeObj();args.set("brep",*brep);args.set("origin",vector(inverse.transformPoint(origin)));args.set("direction",vector(inverse.transformDir(ray)));
                double radius=2*(g_cameraOrtho?dist:std::max(hit.distance,1.))*std::tan(35*K_PI/360.)/std::max(viewH,1)*9;args.set("snap_radius_mm",V::makeNum(radius));request.set("operation",V::makeStr("measurement_pick"));request.set("request",args);
                auto operation=[request]{return nativeWorkerRequest(request);};auto result=g_headless?operation():callSessionWithProgress("measurement_pick",V::makeObj(),operation);const auto* p=result.find("point_mm");if(!p||p->arr.size()!=3)throw std::runtime_error("Native measurement returned no point");acceptMeasurementPoint(g_instanceWorld[bodyIndex].transformPoint({p->arr[0].num,p->arr[1].num,p->arr[2].num}),"Exact B-rep / "+result.find("snap")->asStr());
            }
        }g_error=false;
    }catch(const std::exception& e){g_error=true;g_status=e.what();}return true;
}
static void measurementCoordinates(){
    if(!measurementActive())beginMeasurement(3);auto baseline=g_measure;double factor=DisplayUnits::lengthFactor(g_displayUnits.length);std::vector<EditorField> fields;
    for(auto label:{"A X","A Y","A Z","B X","B Y","B Z"})fields.push_back({std::string(label)+" / "+g_displayUnits.length,"0"});
    Vec3 points[]={baseline.a,baseline.b};for(int i=0;i<2;++i){fields[i*3].value=drawingNumber(points[i].x/factor);fields[i*3+1].value=drawingNumber(points[i].y/factor);fields[i*3+2].value=drawingNumber(points[i].z/factor);}
    if(!editFields("Measurement coordinates",baseline.mode==2?"Coordinates in the active plane. Z must be zero for 2D.":"World coordinates. Values use the selected length unit.",fields))return;
    Vec3 a{formNumber(fields[0].value)*factor,formNumber(fields[1].value)*factor,formNumber(fields[2].value)*factor},b{formNumber(fields[3].value)*factor,formNumber(fields[4].value)*factor,formNumber(fields[5].value)*factor};
    if(baseline.mode==2&&(a.z!=0||b.z!=0))throw std::runtime_error("2D coordinates require Z=0 in the selected plane");g_measure.count=0;acceptMeasurementPoint(a,"Entered coordinates");acceptMeasurementPoint(b,"Entered coordinates");
}
template<class Rect,class Button>static void drawMeasurementOverlay(Rect rect,Button button){
    if(!measurementActive())return;if(measurementPreviewActive()){endMeasurement();g_status="Measurement cleared: display preview changed";return;}if(g_measure.revision!=g_session.revision||g_measure.epoch!=g_documentEpoch){endMeasurement();g_status="Measurement cleared: document changed";return;}
    auto world=[&](Vec3 p){return g_measure.mode==2?g_measure.frame.transformPoint(p):p;};
    auto dot=[&](Vec3 p,float r,float g,float b){float x,y;if(projectScreen(world(p),x,y)&&drawingInView((int)x,(int)y))rect(x-3,y-3,6,6,2,r,g,b,1,0,0,0,0,0);};
    auto line=[&](Vec3 a,Vec3 b,float r,float g,float blue){float x,y,u,v;if(!projectScreen(world(a),x,y)||!projectScreen(world(b),u,v))return;int count=std::min(180,std::max(1,(int)(std::hypot(u-x,v-y)/5)));for(int i=0;i<=count;++i){float t=(float)i/count,sx=x+(u-x)*t,sy=y+(v-y)*t;if(drawingInView((int)sx,(int)sy))rect(sx-1,sy-1,2,2,0,r,g,blue,1,0,0,0,0,0);}};
    if(g_measure.count){dot(g_measure.a,.3f,.85f,1);if(g_measure.count==2){dot(g_measure.b,.3f,.85f,1);auto a=g_measure.a,b=g_measure.b;line(a,b,.3f,.85f,1);Vec3 x{b.x,a.y,a.z},y{b.x,b.y,a.z};line(a,x,1,.4f,.35f);line(x,y,.35f,.95f,.5f);if(g_measure.mode==3)line(y,b,.4f,.6f,1);}}
    float width=std::min(454.f,(float)viewW-30),x=(float)(viewX+viewW)-width-15,y=(float)viewY+58;g_zones.erase(std::remove_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.x<x+width&&z.x+z.w>x&&z.y<y+216&&z.y+z.h>y;}),g_zones.end());rect(x,y,width,216,8,.04f,.085f,.115f,.97f,.24f,.56f,.68f,.9f,1);
    textOut(g_measure.mode==2?"MEASURE 2D / "+g_measure.plane:"MEASURE 3D / world axes",x+12,y+12,.73f,.65f,.91f,1);
    textOut(g_measure.count==0?"Click the first point":g_measure.count==1?"First point set / click the second":"A to B / signed axis components",x+12,y+39,.59f);
    if(g_measure.count==2){auto delta=g_measure.b-g_measure.a;textOut("X  "+g_displayUnits.distance(delta.x),x+12,y+65,.70f,1,.5f,.45f);textOut("Y  "+g_displayUnits.distance(delta.y),x+12,y+89,.70f,.5f,1,.6f);if(g_measure.mode==3)textOut("Z  "+g_displayUnits.distance(delta.z),x+width/2,y+65,.70f,.55f,.73f,1);textOut("Distance  "+g_displayUnits.distance(delta.len()),x+12,y+116,.78f,.75f,.95f,1);}
    textOut(fitText(g_measure.sourceA+(g_measure.count==2?" | "+g_measure.sourceB:""),.49f,width-24),x+12,y+147,.49f,.67f,.76f,.83f);
    button("Coordinates",x+12,y+174,120,588);button("Units",x+140,y+174,80,587);button("Close",x+width-92,y+174,80,589);
}
