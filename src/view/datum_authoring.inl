// Included in viewer.cpp after the field editor and command helpers.
// Named visibility and selected datum are view state, never model authoring.
static std::set<std::string> g_hiddenDatumNames;
static std::string g_selectedDatumName;
static void viewRay(int,int,Vec3&,Vec3&);
static bool projectScreen(Vec3,float&,float&);

static const char* builtinDatumNames[]={"Origin","XY","XZ","YZ"};
static const char* builtinDatumKinds[]={"point","plane","plane","plane"};
static bool builtinDatumName(const std::string& name){return name=="Origin"||name=="XY"||name=="XZ"||name=="YZ";}
static DatumPlane builtinDatum(const std::string& name){DatumPlane datum;datum.name=name;datum.parent="XY";datum.kind=name=="Origin"?"point":"plane";return datum;}
static void clearDatumSelection(){g_selectedDatumName.clear();g_viewportContainer=0;}
static void sanitizeDatumViewState(){
    auto stored=[](const std::string& name){return std::any_of(g_doc.planes.begin(),g_doc.planes.end(),[&](const DatumPlane& d){return d.name==name;});};
    auto exists=[&](const std::string& name){return builtinDatumName(name)||stored(name);};
    for(auto it=g_hiddenDatumNames.begin();it!=g_hiddenDatumNames.end();)if(!exists(*it))it=g_hiddenDatumNames.erase(it);else ++it;
    if(!g_selectedDatumName.empty()&&!exists(g_selectedDatumName))g_selectedDatumName.clear();
    if(g_activePlane=="Origin"||(g_activePlane!="XY"&&g_activePlane!="XZ"&&g_activePlane!="YZ"&&!stored(g_activePlane)))g_activePlane="XY";
    if(!g_activeSketch.empty()&&!g_doc.findSketch(g_activeSketch))g_activeSketch.clear();
    g_picked.erase(std::remove_if(g_picked.begin(),g_picked.end(),[&](const SelectionItem& pick){return !pick.feature&&pick.owner.empty()&&(pick.kind=="plane"||pick.kind=="axis"||pick.kind=="point")&&!exists(pick.name);}),g_picked.end());
}
static void resetDatumViewState(){
    g_hiddenDatumNames.clear();g_selectedDatumName.clear();
    g_picked.erase(std::remove_if(g_picked.begin(),g_picked.end(),[](const SelectionItem& pick){return !pick.feature&&pick.owner.empty()&&(pick.kind=="plane"||pick.kind=="axis"||pick.kind=="point");}),g_picked.end());
    sanitizeDatumViewState();
}
static bool datumVisible(const std::string& name){return g_showDatumPlanes&&!g_hiddenDatumNames.count(name);}
static double datumDisplaySpan(const Mat4& frame){
    AABB local;if(g_sceneBounds.valid())local=g_sceneBounds.transformed(frame.inverted());
    return local.valid()?std::clamp(std::max({std::abs(local.mn.x),std::abs(local.mx.x),std::abs(local.mn.y),std::abs(local.mx.y)})*1.15,40.,1e7):40.;
}
static void datumDisplayGeometry(const DatumPlane& datum,const Mat4& frame,std::vector<float>& lines,std::vector<float>* fill=nullptr){
    auto put=[](std::vector<float>& v,Vec3 p){v.insert(v.end(),{(float)p.x,(float)p.y,(float)p.z});};
    auto line=[&](Vec3 a,Vec3 b){put(lines,frame.transformPoint(a));put(lines,frame.transformPoint(b));};
    if(datum.kind=="point"){
        double r=std::max(.5,std::min(4.,dist*.008));line({-r,0,0},{r,0,0});line({0,-r,0},{0,r,0});line({0,0,-r},{0,0,r});
    }else if(datum.kind=="axis")line({0,0,-20},{0,0,20});
    else if(datum.kind=="frame"){line({-20,0,0},{20,0,0});line({0,-20,0},{0,20,0});line({0,0,-20},{0,0,20});}
    else{
        double s=datumDisplaySpan(frame);line({-s,0,0},{s,0,0});line({0,-s,0},{0,s,0});
        line({-s,-s,0},{s,-s,0});line({s,-s,0},{s,s,0});line({s,s,0},{-s,s,0});line({-s,s,0},{-s,-s,0});
        if(fill)for(Vec3 p:{Vec3{-s,-s,0},Vec3{s,-s,0},Vec3{s,s,0},Vec3{-s,-s,0},Vec3{s,s,0},Vec3{-s,s,0}})put(*fill,frame.transformPoint(p));
    }
}
static void appendOneDatum(const DatumPlane& datum,std::vector<float>& lines,std::vector<float>* fill){
    try{datumDisplayGeometry(datum,resolvePlane(g_doc,datum.name),lines,fill);}catch(const std::exception&){}
}
static void appendDatumOverlay(){
    for(int i=0;i<4;++i){auto datum=builtinDatum(builtinDatumNames[i]);if(datumVisible(datum.name))appendOneDatum(datum,g_datumLines,&g_datumFill);}
    for(const auto& datum:g_doc.planes)if(datumVisible(datum.name))appendOneDatum(datum,g_datumLines,&g_datumFill);
}
static void appendDatumSelectionLines(){
    if(g_selectedDatumName.empty()||!datumVisible(g_selectedDatumName))return;
    if(builtinDatumName(g_selectedDatumName)){appendOneDatum(builtinDatum(g_selectedDatumName),g_selectionLines,nullptr);return;}
    for(const auto& datum:g_doc.planes)if(datum.name==g_selectedDatumName){appendOneDatum(datum,g_selectionLines,nullptr);return;}
}
static void selectNamedDatum(const std::string& name,const std::string& kind,bool toggle){
    g_explorerFeature=0;g_selectedExplorerEntity.reset();g_selectedDatumName=name;
    if(kind=="plane"||kind=="frame")g_activePlane=name;
    g_activeSketch.clear();g_showDatumPlanes=true;g_hiddenDatumNames.erase(name);
    SelectionItem item;item.kind=kind=="frame"?"plane":kind;item.name=name;
    if(toggle){if(g_pickKind==4||g_pickKind==1||g_pickKind==2)toggleSelection(item);}
    else{g_viewportContainer=0;g_picked.clear();g_picked.push_back(item);g_pickKind=4;selBody=-1;}
    rebuildSketchOverlay();g_status="Datum "+name+" / "+kind+" / associative construction reference";
}
static void selectDatumTree(size_t index){
    if(index>=g_doc.planes.size())return;const auto& datum=g_doc.planes[index];selectNamedDatum(datum.name,datum.kind,true);
}
static void selectBuiltinDatum(int index){if(index<0||index>3)return;selectNamedDatum(builtinDatumNames[index],builtinDatumKinds[index],true);}
static void applyAgentDatumSelection(const std::string& name,const std::string& kind){selectNamedDatum(name,kind,false);}
static void toggleNamedDatum(const std::string& name){
    if(g_hiddenDatumNames.count(name)){g_hiddenDatumNames.erase(name);g_showDatumPlanes=true;}else g_hiddenDatumNames.insert(name);
    rebuildSketchOverlay();g_status=(datumVisible(name)?"Shown datum ":"Hidden datum ")+name;
}
static void toggleDatumTree(size_t index){if(index>=g_doc.planes.size())return;toggleNamedDatum(g_doc.planes[index].name);}
static bool pickDatumScreen(int mx,int my){
    if(!g_showDatumPlanes)return false;Vec3 eye,ray;viewRay(mx,my,eye,ray);
    struct Candidate{std::string name,kind;bool builtin;size_t index;};std::vector<Candidate> candidates;
    for(int i=0;i<4;++i)candidates.push_back({builtinDatumNames[i],builtinDatumKinds[i],true,(size_t)i});
    for(size_t i=0;i<g_doc.planes.size();++i)candidates.push_back({g_doc.planes[i].name,g_doc.planes[i].kind,false,i});
    size_t selected=candidates.size();double nearest=std::numeric_limits<double>::infinity();
    auto screenSegment=[&](Vec3 a,Vec3 b)->double{float ax,ay,bx,by;if(!projectScreen(a,ax,ay)||!projectScreen(b,bx,by))return 1e9;
        double dx=bx-ax,dy=by-ay,l=dx*dx+dy*dy,t=l>1e-9?std::clamp(((mx-ax)*dx+(my-ay)*dy)/l,0.,1.):0.;return std::hypot(ax+t*dx-mx,ay+t*dy-my);};
    for(size_t i=0;i<candidates.size();++i){const auto& datum=candidates[i];if(!datumVisible(datum.name))continue;
        if(g_pickKind==1&&datum.kind!="plane"&&datum.kind!="frame")continue;
        if(g_pickKind==2&&datum.kind!="axis"&&datum.kind!="point"&&datum.kind!="frame")continue;
        Mat4 frame;try{frame=resolvePlane(g_doc,datum.name);}catch(const std::exception&){continue;}
        const Vec3 origin=frame.transformPoint({});double travel=(origin-eye).dot(ray);if(travel<0)continue;bool hit=false;
        if(datum.kind=="plane"){
            const auto normal=frame.transformDir({0,0,1});const double denominator=ray.dot(normal);if(std::abs(denominator)<1e-9)continue;
            travel=(origin-eye).dot(normal)/denominator;if(travel<0)continue;auto local=frame.inverted().transformPoint(eye+ray*travel);double span=datumDisplaySpan(frame);hit=std::abs(local.x)<=span&&std::abs(local.y)<=span;
        }else if(datum.kind=="point")hit=screenSegment(origin,origin)<=10.;
        else {hit=screenSegment(frame.transformPoint({0,0,-20}),frame.transformPoint({0,0,20}))<=9.;
            if(datum.kind=="frame")hit|=screenSegment(frame.transformPoint({-20,0,0}),frame.transformPoint({20,0,0}))<=9.||screenSegment(frame.transformPoint({0,-20,0}),frame.transformPoint({0,20,0}))<=9.;}
        if(hit&&travel<nearest){nearest=travel;selected=i;}
    }
    if(selected==candidates.size())return false;const auto& hit=candidates[selected];if(hit.builtin)selectBuiltinDatum((int)hit.index);else selectDatumTree(hit.index);return true;
}
static arkjson::Value datumAnchorField(const std::string& value){
    using V=arkjson::Value;const auto text=trimField(value);V result;
    if(!text.empty()&&(text[0]=='{'||text[0]=='[')){if(!arkjson::parse(text,result))throw std::runtime_error("Datum anchor JSON is invalid");}
    else result=V::makeStr(text);return result;
}
static void constructionDatumDialog(int action,bool editing=false){
    using V=arkjson::Value;const u64 revision=g_session.revision,epoch=g_documentEpoch;
    std::string type,output="plane",label;std::vector<EditorField> fields{{"Datum name","Construction"}};
    auto add=[&](const char* title,const std::string& value){fields.push_back({title,value});};
    switch(action){
        case 630:type="offset_plane";label="Offset plane";add("Source plane",g_activePlane);add("Normal offset / mm","10");break;
        case 631:type="angle_plane";label="Plane at angle";add("Source plane",g_activePlane);add("Axis name or reference JSON",R"({"datum":"XY","axis":"x"})");add("Angle / degrees","45");break;
        case 632:type="plane_three_points";label="Plane through three points";add("First point name or XYZ JSON","[0,0,0]");add("Second point","[50,0,0]");add("Third point","[0,50,0]");break;
        case 633:type="plane_axis_point";label="Plane through axis and point";add("Axis name or reference JSON",R"({"datum":"XY","axis":"x"})");add("Point name or XYZ JSON","[0,50,0]");break;
        case 634:type="plane_point_normal";label="Plane normal to axis at point";add("Axis name or reference JSON",R"({"datum":"XY","axis":"z"})");add("Point name or XYZ JSON","[0,0,20]");break;
        case 635:type="axis_points";output="axis";label="Axis through two points";add("Start point name or XYZ JSON","[0,0,0]");add("End point name or XYZ JSON","[0,0,50]");break;
        case 636:type="axis_planes";output="axis";label="Axis at plane intersection";add("First plane","XY");add("Second plane","XZ");break;
        case 637:type="point";output="point";label="Construction point";add("X / mm","0");add("Y / mm","0");add("Z / mm","0");break;
        case 638:type="point";output="point";label="Linked point / curve endpoint";add("Source point, plane, endpoint or JSON",g_selectedDatumName.empty()?R"({"sketch":"Path","entity":"line1","endpoint":"end"})":g_selectedDatumName);break;
        case 639:type="between_points";output="frame";label="Frame between two points";add("Start point name or XYZ JSON","[0,0,0]");add("End point name or XYZ JSON","[0,0,50]");add("Fraction / 0..1","0.5");add("Axial roll / degrees","0");add("X guide axis / optional","");break;
        case 645:type="bisector";label="Bisector plane";add("First plane","XY");add("Second plane","XZ");add("Side sum or difference","sum");break;
        default:return;
    }
    if(editing){
        const auto found=std::find_if(g_doc.planes.begin(),g_doc.planes.end(),[](const DatumPlane& d){return d.name==g_selectedDatumName;});
        if(found==g_doc.planes.end())throw std::runtime_error("Select the construction datum to edit");fields[0].value=found->name;
        const auto& support=found->support;auto take=[&](const char* key,size_t index){if(auto p=support.find(key))fields[index].value=p->t==V::Str?p->str:p->dump();};
        if(action==630){take("plane",1);take("offset",2);}else if(action==631){take("plane",1);take("axis",2);take("angle",3);}
        else if(action==632){take("a",1);take("b",2);take("c",3);}else if(action==633||action==634){take("axis",1);take("point",2);}
        else if(action==635||action==639){take("start",1);take("end",2);if(action==639){take("fraction",3);take("roll",4);take("x_axis",5);}}
        else if(action==636||action==645){take("first",1);take("second",2);if(action==645)take("side",3);}else if(action==638)take("point",1);
    }
    if(!editFields(label.c_str(),"Named references follow source edits. XYZ arrays and reference objects accept JSON; dimensions accept expressions.",fields))return;
    if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed during datum editing; inspect the current source and retry");
    V support=V::makeObj();support.set("kind",V::makeStr(type));auto set=[&](const char* key,size_t i){support.set(key,datumAnchorField(fields[i].value));};
    if(action==630){set("plane",1);support.set("offset",V::makeStr(fields[2].value));}
    else if(action==631){set("plane",1);set("axis",2);support.set("angle",V::makeStr(fields[3].value));}
    else if(action==632){set("a",1);set("b",2);set("c",3);}
    else if(action==633||action==634){set("axis",1);set("point",2);}
    else if(action==635||action==639){set("start",1);set("end",2);if(action==639){support.set("fraction",V::makeStr(fields[3].value));support.set("roll",V::makeStr(fields[4].value));if(!trimField(fields[5].value).empty())set("x_axis",5);}}
    else if(action==636){set("first",1);set("second",2);}
    else if(action==645){set("first",1);set("second",2);auto side=trimField(fields[3].value);if(side.empty())side="sum";support.set("side",V::makeStr(side));}
    else if(action==637){V xyz=V::makeArr();for(size_t i=1;i<=3;++i)xyz.arr.push_back(V::makeStr(fields[i].value));support.set("point",xyz);}
    else set("point",1);
    bindDatumSupportProducers(g_doc,support);
    V command=V::makeObj(),commands=V::makeArr(),args=V::makeObj();command.set("op",V::makeStr("plane.set"));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("kind",V::makeStr(output));command.set("support",support);command.set("p",V::makeObj());commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)revision));runTool("doc_apply",args);
    if(!g_error){g_selectedDatumName=trimField(fields[0].value);g_showDatumPlanes=true;g_hiddenDatumNames.erase(g_selectedDatumName);rebuildSketchOverlay();g_status=label+" created / "+g_selectedDatumName;}
}
static bool handleDatumAuthoringAction(int action){
    if(action>=32300&&action<32304){toggleNamedDatum(builtinDatumNames[action-32300]);return true;}
    if(action>=32000&&action<32128){toggleDatumTree((size_t)(action-32000));return true;}
    if((action>=630&&action<=639)||action==645){constructionDatumDialog(action);return true;}
    if(action==640){g_pickKind=4;g_showDatumPlanes=true;rebuildSketchOverlay();g_status="Select construction plane, axis, frame or point in the viewport or tree";return true;}
    if(action==641){if(builtinDatumName(g_selectedDatumName)){toggleNamedDatum(g_selectedDatumName);return true;}auto it=std::find_if(g_doc.planes.begin(),g_doc.planes.end(),[](const DatumPlane& d){return d.name==g_selectedDatumName;});if(it!=g_doc.planes.end())toggleDatumTree((size_t)std::distance(g_doc.planes.begin(),it));return true;}
    if(action==642){g_hiddenDatumNames.clear();g_showDatumPlanes=true;rebuildSketchOverlay();return true;}
    if(action==643){for(const char* name:builtinDatumNames)g_hiddenDatumNames.insert(name);for(const auto& d:g_doc.planes)g_hiddenDatumNames.insert(d.name);rebuildSketchOverlay();return true;}
    if(action==644){
        if(builtinDatumName(g_selectedDatumName))throw std::runtime_error("Origin, XY, XZ and YZ are immutable starting references");
        const auto found=std::find_if(g_doc.planes.begin(),g_doc.planes.end(),[](const DatumPlane& d){return d.name==g_selectedDatumName;});
        if(found==g_doc.planes.end())throw std::runtime_error("Select a construction datum to edit");const auto* kind=found->support.find("kind");
        const std::vector<std::pair<std::string,int>> actions={{"offset_plane",630},{"angle_plane",631},{"plane_three_points",632},{"plane_axis_point",633},{"plane_point_normal",634},{"axis_points",635},{"axis_planes",636},{"point",638},{"between_points",639},{"bisector",645}};
        if(kind)for(const auto& item:actions)if(item.first==kind->str){constructionDatumDialog(item.second,true);return true;}
        throw std::runtime_error("Use Construction plane or Supported construction datum to edit this legacy definition");
    }
    return false;
}
