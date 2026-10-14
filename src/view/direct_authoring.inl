// Direct edits are recipes evaluated in an isolated session. Pixel gestures
// choose displacement; native parameters or explicit mesh source indices remain
// authoritative. Publication rechecks the exact captured document/revision.
struct DirectAuthoring {
    bool active=false,dragging=false,moved=false,requireClosed=false,symmetric=false,limitPicking=false,handleStart=false;int mode=0,body=-1,x=0,y=0,direction=1;
    u64 revision=0,epoch=0,feature=0,gesture=0,generation=0,resolvedGeneration=0;std::shared_ptr<const Document> baseline;Feature source;
    bool pending=false,commitRequested=false,nativeVertex=false;
    std::string sourceVertex;
    std::unique_ptr<AgentSession::PreparedTransfer> receipt;
    std::vector<float> previewFill,previewLines;
    Mat4 world;Vec3 origin{},axis{0,0,1},delta{};double amount=0,depthScale=1,dragStartAmount=0;
    u64 limitFeature=0;std::string sourceFace,limitFace;
    std::string key,expression,name,error,topology;std::array<std::string,3> coordinateExpressions{};std::vector<u32> vertices,triangles;
    arkjson::Value commands,nativeReport;Mesh preview;std::vector<float> selection;
};
static DirectAuthoring g_directAuthoring;
static std::atomic_bool g_directPreviewTestGate=false,g_directPreviewGateEntered=false;
static DirectPreviewWorker* g_directPreviewInstance=nullptr;
static DirectPreviewWorker& directPreviewWorker(){static DirectPreviewWorker worker([]{if(g_directPreviewTestGate){g_directPreviewGateEntered=true;while(g_directPreviewTestGate)std::this_thread::sleep_for(std::chrono::milliseconds(1));}});g_directPreviewInstance=&worker;return worker;}
static bool g_directPreviewTestWait=false;
static void applyDirectAuthoring();
static bool directCurrent();
static std::string directDisplayDistance(double);
static void processDirectAuthoringPreview(){
    auto result=directPreviewWorker().take();auto& edit=g_directAuthoring;
    if(result&&edit.active&&directCurrent()&&result->gesture==edit.gesture&&result->generation==edit.generation&&result->revision==edit.revision&&result->epoch==edit.epoch){
        edit.pending=false;edit.resolvedGeneration=result->generation;edit.error=std::move(result->error);edit.preview=std::move(result->mesh);edit.previewFill=std::move(result->fill);edit.previewLines=std::move(result->lines);edit.nativeReport=std::move(result->report);edit.receipt=std::move(result->receipt);
        if(edit.mode==5&&edit.limitFeature)if(auto* receipt=edit.nativeReport.find("extrusion");receipt&&receipt->find("depth_mm"))edit.amount=receipt->find("depth_mm")->num;
        if(!edit.error.empty()){edit.commitRequested=false;g_status="Direct edit rejected / "+edit.error;}
        else g_status="Exact preview ready / "+directDisplayDistance(edit.amount)+" / release applies / Tab value / Esc cancels";
    }
    if(edit.active&&edit.commitRequested&&!edit.pending&&!edit.dragging&&!edit.limitPicking&&!g_busy&&directCurrent()&&edit.resolvedGeneration==edit.generation&&edit.receipt){
        edit.commitRequested=false;try{applyDirectAuthoring();}catch(const std::exception& error){g_error=true;g_status=error.what();}
    }
}
static void awaitDirectAuthoringPreview(){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(g_directAuthoring.active&&g_directAuthoring.pending&&std::chrono::steady_clock::now()<deadline){processDirectAuthoringPreview();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    processDirectAuthoringPreview();
}
struct DirectHandleOverlay {float x=0,y=0,w=0,h=0;bool visible=false;};static DirectHandleOverlay g_directHandleOverlay;
static const char* directModeName(int mode){static const char* names[]={"Push / pull source face","Move body","Extrude mesh patch","Pull vertex (CAD / mesh)","Move mesh edge","Extrude selected profile / face"};return names[std::clamp(mode,0,5)];}
static std::string directDisplayDistance(double amount){std::ostringstream value;value<<std::fixed<<std::setprecision(3)<<amount/DisplayUnits::lengthFactor(g_displayUnits.length)<<" "<<g_displayUnits.length;return value.str();}
static bool directCurrent(){return g_directAuthoring.revision==g_session.revision&&g_directAuthoring.epoch==g_documentEpoch;}
static void cancelDirectAuthoring(){directPreviewWorker().cancel();g_directAuthoring={};g_dragged=true;g_status="Direct preview cancelled / drawing retained";}
static Vec3 directMeshPoint(const Mesh& mesh,u32 vertex){size_t at=(size_t)vertex*3;if(at+2>=mesh.pos.size())throw std::runtime_error("Mesh source vertex is outside the captured topology");return {mesh.pos[at],mesh.pos[at+1],mesh.pos[at+2]};}
static Vec3 directTriangleNormal(const Mesh& mesh,u32 triangle){size_t at=(size_t)triangle*3;if(at+2>=mesh.idx.size())throw std::runtime_error("Mesh source triangle is outside the captured topology");auto a=directMeshPoint(mesh,mesh.idx[at]);return (directMeshPoint(mesh,mesh.idx[at+1])-a).cross(directMeshPoint(mesh,mesh.idx[at+2])-a).normalized();}
static std::vector<u32> directMeshPatch(const Mesh& mesh,u32 seed){
    // Coplanar indexed neighbors only, bounded relative to the seed normal.
    // Comparing every triangle to the seed prevents walking around a sphere.
    using Edge=std::pair<u32,u32>;std::map<Edge,std::vector<u32>> edges;
    for(u32 triangle=0;triangle<mesh.triCount();++triangle)for(int k=0;k<3;++k){u32 a=mesh.idx[(size_t)triangle*3+k],b=mesh.idx[(size_t)triangle*3+(k+1)%3];if(b<a)std::swap(a,b);edges[{a,b}].push_back(triangle);}
    const auto normal=directTriangleNormal(mesh,seed),origin=directMeshPoint(mesh,mesh.idx[(size_t)seed*3]);std::vector<u32> patch{seed};std::set<u32> seen{seed};
    for(size_t cursor=0;cursor<patch.size()&&patch.size()<=4096;++cursor){auto triangle=patch[cursor];for(int k=0;k<3;++k){u32 a=mesh.idx[(size_t)triangle*3+k],b=mesh.idx[(size_t)triangle*3+(k+1)%3];if(b<a)std::swap(a,b);for(u32 neighbor:edges[{a,b}])if(!seen.count(neighbor)){auto n=directTriangleNormal(mesh,neighbor);auto p=directMeshPoint(mesh,mesh.idx[(size_t)neighbor*3]);if(n.dot(normal)>=std::cos(deg2rad(3.))&&std::abs(normal.dot(p-origin))<=1e-4){seen.insert(neighbor);patch.push_back(neighbor);}}}}
    if(patch.size()>4096)throw std::runtime_error("This coplanar mesh patch exceeds the 4096-triangle direct-edit limit; split the source patch before extrusion");
    return patch;
}
static void beginDirectAuthoring(int mode){
    if(g_busy)throw std::runtime_error("Wait for the current drawing operation");cancelSketchEditing();cancelSketchDrawing();cancelComponentMove();g_directAuthoring={};auto& edit=g_directAuthoring;edit.active=true;edit.mode=mode;edit.revision=g_session.revision;edit.epoch=g_documentEpoch;edit.baseline=DirectPreviewWorker::capture(g_doc);edit.gesture=directPreviewWorker().begin(edit.baseline,edit.revision,edit.epoch);g_pickKind=0;g_toolModule=1;g_navMode=0;g_activeSketch.clear();g_showSketches=false;
    if(!g_doc.assembly.empty()&&mode==1){cancelDirectAuthoring();armComponentMove();return;}
    g_status=std::string(directModeName(mode))+" / drag a compatible body, face or mesh element / Tab value / Esc cancel";
}
static bool directPlanarFace(u64 feature,const std::string& name){for(const auto& body:g_doc.evalBodies)if(body.id==feature){const auto* topology=body.nativeData.find("topology"),*rows=topology?topology->find("references"):nullptr;if(rows)for(const auto& row:rows->arr)if(row.find("name")&&row.find("name")->asStr()==name){const auto* descriptor=row.find("descriptor");return descriptor&&descriptor->find("normal");}}return false;}
static bool directSelectedProfile(std::string& sketch,SelectionItem& face){
    if(g_picked.size()==1&&g_picked[0].kind=="face"&&!g_picked[0].instance&&directPlanarFace(g_picked[0].feature,g_picked[0].name)){face=g_picked[0];return true;}
    if(!g_picked.empty()&&g_picked[0].kind=="curve"&&g_doc.findSketch(g_picked[0].owner)){sketch=g_picked[0].owner;return true;}
    if(g_showSketches&&g_doc.findSketch(g_activeSketch)){sketch=g_activeSketch;return true;}return false;
}
static Vec3 directProfileCenter(const NamedSketch& sketch,const Mat4& frame){AABB bounds;for(const auto& entity:sketch.sk.ents)if(!entity.construction)try{for(const auto& path:sampleSketchEntityPaths(entity,sketch.sk.tolerance,&sketch.sk))for(const auto& point:path)bounds.grow(frame.transformPoint({point.x,point.y,0}));}catch(const std::exception&){}return bounds.valid()?bounds.center():frame.transformPoint({});}
static void beginSketchExtrusion(bool selectedOnly=false){
    std::string name;SelectionItem face;const bool selected=directSelectedProfile(name,face);
    if(!selected)name=g_activeSketch.empty()?(g_doc.sketches.empty()?"":g_doc.sketches.front().name):g_activeSketch;
    const u64 revision=g_session.revision,epoch=g_documentEpoch;
    std::vector<EditorField> fields{{"Closed source sketch",name}};
    if(!selected){if(selectedOnly)throw std::runtime_error("Select a saved profile or exact planar face");if(!editFields("Drag exact sketch extrusion", "Choose an authored closed sketch. Drag the normal handle; Tab enters a depth expression.",fields))return;name=trimField(fields[0].value);}
    if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while choosing an extrusion source");
    if(!face.name.empty()){
        if(face.instance)throw std::runtime_error("Edit the part definition to extrude a placed face");auto* source=g_doc.find(face.feature);if(!source||!isNativeFeature(source->type))throw std::runtime_error("Extrusion needs an exact planar source face");
        const auto reference=resolveGeometryReference(g_doc,face.feature,face.name);if(reference.kind!="face")throw std::runtime_error("Extrusion source must be a face");
        // The kernel validates planarity against the source B-rep; this frame
        // only locates the on-face handle and chooses the gesture direction.
        Vec3 origin=reference.frame.transformPoint({});for(size_t body=0;body<g_bodyIds.size();++body)if(g_bodyIds[body]==face.feature&&!g_instanceIds[body]){const auto part=g_instancePart[body];auto found=g_partPicks[part].faceReferences.find(face.name);if(found!=g_partPicks[part].faceReferences.end()){AABB bounds;auto span=g_partPicks[part].faces[(size_t)found->second];for(int triangle=span.first;triangle<span.first+span.second;++triangle)for(int k=0;k<3;++k)bounds.grow(directMeshPoint(g_cpuMeshes[part],g_cpuMeshes[part].idx[(size_t)triangle*3+k]));if(bounds.valid())origin=bounds.center();}break;}
        Feature snapshot=*source;beginDirectAuthoring(5);auto& edit=g_directAuthoring;edit.feature=face.feature;edit.source=snapshot;edit.sourceFace=face.name;edit.name="Exact face extrusion / "+snapshot.name;edit.origin=origin;edit.axis=reference.frame.transformDir({0,0,1}).normalized();edit.world=Mat4::identity();g_status="Extrude selected face / drag the normal handle / Tab depth / choose Reverse, Symmetric or Up to face";return;
    }
    auto* sketch=g_doc.findSketch(name);if(!sketch)throw std::runtime_error("Choose an existing closed source sketch");
    const auto frame=resolvePlane(g_doc,sketch->sk.plane);
    const auto center=directProfileCenter(*sketch,frame);beginDirectAuthoring(5);auto& edit=g_directAuthoring;edit.source.name=name;edit.source.p["sketch"]=name;edit.name="Exact "+name+" extrusion";edit.world=Mat4::identity();edit.axis=frame.transformDir({0,0,1}).normalized();edit.origin=center;
    g_activeSketch=name;g_showSketches=true;rebuildSketchOverlay();g_status="Extrude selected profile / drag the normal handle / Tab depth / choose Reverse, Symmetric or Up to face";
}
static arkjson::Value directCommands(){
    using V=arkjson::Value;auto& edit=g_directAuthoring;V command=V::makeObj(),p=V::makeObj(),native=V::makeObj(),inputs=V::makeArr(),commands=V::makeArr();inputs.arr.push_back(V::makeNum((double)edit.feature));
    const std::string amount=edit.expression.empty()?drawingNumber(edit.amount):edit.expression;
    if((edit.mode==0&&edit.key.empty())||(edit.mode==2&&edit.triangles.empty())||(edit.mode>=3&&edit.mode<=4&&edit.vertices.empty()&&!edit.nativeVertex))throw std::runtime_error("Select a compatible source face or vertex before entering dimensions");
    if(edit.mode==5){command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("native_solid"));command.set("name",V::makeStr(edit.name));p.set("op",V::makeStr("extrude"));p.set("depth",V::makeStr(edit.limitFeature&&edit.amount<=0&&edit.expression.empty()?"1":amount));p.set("direction",V::makeStr(std::to_string(edit.direction)));p.set("symmetric",V::makeStr(edit.symmetric?"1":"0"));inputs=V::makeArr();if(edit.sourceFace.empty())p.set("sketch",V::makeStr(edit.source.get("sketch")));else{native.set("source_face",V::makeStr(edit.sourceFace));inputs.arr.push_back(V::makeNum((double)edit.feature));}if(edit.limitFeature){native.set("mode",V::makeStr("up_to_face"));native.set("limit_face",V::makeStr(edit.limitFace));inputs.arr.push_back(V::makeNum((double)edit.limitFeature));}if(!inputs.arr.empty())command.set("inputs",inputs);command.set("native",native);command.set("p",p);}
    else if(edit.mode==0){command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)edit.feature));const auto original=edit.source.get(edit.key.c_str(),"0");p.set(edit.key,V::makeStr("("+original+")+("+amount+")"+(edit.depthScale==1?"":"*"+drawingNumber(edit.depthScale))));command.set("p",p);}
    else{command.set("op",V::makeStr("feature.add"));command.set("inputs",inputs);command.set("name",V::makeStr(edit.name.empty()?directModeName(edit.mode):edit.name));
        if(edit.nativeVertex){command.set("type",V::makeStr("native_solid"));native.set("op",V::makeStr("vertex_pull"));native.set("source_vertex",V::makeStr(edit.sourceVertex));native.set("source_topology",V::makeStr(edit.topology));}
        else if(edit.mode==1&&isNativeFeature(edit.source.type)){command.set("type",V::makeStr("native_solid"));native.set("op",V::makeStr("transform"));}
        else if(edit.mode==1&&edit.source.type!=FeatureType::MeshBody&&edit.source.type!=FeatureType::MeshEdit&&edit.source.type!=FeatureType::MeshWeld&&edit.source.type!=FeatureType::MeshSmooth&&edit.source.type!=FeatureType::MeshDecimate&&edit.source.type!=FeatureType::MeshOffset&&edit.source.type!=FeatureType::MeshFill){command.set("type",V::makeStr("transform"));}
        else{command.set("type",V::makeStr("mesh_edit"));native.set("op",V::makeStr(edit.mode==2?"extrude":"displace"));native.set("source_topology",V::makeStr(edit.topology));V selected=V::makeArr();if(edit.mode==2){for(auto index:edit.triangles)selected.arr.push_back(V::makeNum(index));native.set("triangles",selected);}else{for(auto index:edit.vertices)selected.arr.push_back(V::makeNum(index));native.set("vertices",selected);}native.set("require_closed",V::makeBool(edit.requireClosed));}
        auto local=edit.world.inverted().transformDir(edit.delta);const char* keys[]={(edit.mode==1&&command.find("type")->str!="mesh_edit")?"tx":"dx",(edit.mode==1&&command.find("type")->str!="mesh_edit")?"ty":"dy",(edit.mode==1&&command.find("type")->str!="mesh_edit")?"tz":"dz"};
        Vec3 localAxis=edit.world.inverted().transformDir(edit.axis);double values[]={local.x,local.y,local.z},factors[]={localAxis.x,localAxis.y,localAxis.z};for(int i=0;i<3;++i)p.set(keys[i],V::makeStr(edit.mode==0||edit.mode==2?"("+drawingNumber(factors[i])+")*("+amount+")":edit.coordinateExpressions[(size_t)i].empty()?drawingNumber(values[i]):edit.coordinateExpressions[(size_t)i]));command.set("p",p);command.set("native",native);
    }
    commands.arr.push_back(command);return commands;
}
static bool directFaceHandle(u64 feature,const std::string& name,Vec3& origin,Vec3& axis){
    using V=arkjson::Value;bool bound=false;
    for(const auto& body:g_doc.evalBodies)if(body.id==feature){auto* topology=body.nativeData.find("topology"),*rows=topology?topology->find("references"):nullptr;if(rows)for(const auto& row:rows->arr)if(row.find("name")&&row.find("name")->asStr()==name){auto* descriptor=row.find("descriptor"),*normal=descriptor?descriptor->find("normal"):nullptr,*point=descriptor?descriptor->find("origin"):nullptr;if(normal&&normal->t==V::Arr&&normal->arr.size()==3&&point&&point->t==V::Arr&&point->arr.size()==3){axis={normal->arr[0].num,normal->arr[1].num,normal->arr[2].num};origin={point->arr[0].num,point->arr[1].num,point->arr[2].num};bound=axis.len()>.9;}break;}break;}
    if(!bound)return false;
    for(size_t body=0;body<g_bodyIds.size();++body)if(g_bodyIds[body]==feature&&!g_instanceIds[body]){auto part=g_instancePart[body];auto found=g_partPicks[part].faceReferences.find(name);if(found==g_partPicks[part].faceReferences.end())return false;AABB bounds;auto span=g_partPicks[part].faces[(size_t)found->second];for(int triangle=span.first;triangle<span.first+span.second;++triangle)for(int k=0;k<3;++k)bounds.grow(directMeshPoint(g_cpuMeshes[part],g_cpuMeshes[part].idx[(size_t)triangle*3+k]));if(bounds.valid())origin=bounds.center();return true;}return false;
}
static Feature* directResizeConsumer(){
    if(!g_picked.empty()||(g_showSketches&&!g_activeSketch.empty()))return nullptr;
    auto* feature=selectedPartFeature();if(!feature||feature->type!=FeatureType::NativeSolid||featureOperation(*feature)!="extrude")return nullptr;
    if(auto* mode=feature->nativeSpec.find("mode");mode&&mode->asStr()=="up_to_face")return nullptr;
    return feature;
}
static Vec3 directResizeAxis(const Feature& source){
    using V=arkjson::Value;auto number=[&](const char* key,double fallback){auto text=source.get(key);if(text.empty())if(const auto* value=source.nativeSpec.find(key)){if(value->t==V::Bool)return value->b?1.:0.;text=value->t==V::Str?value->str:value->dump();}return text.empty()?fallback:formNumber(text);};
    Mat4 frame=Mat4::identity();
    if(const auto* face=source.nativeSpec.find("source_face"))frame=resolveGeometryReference(g_doc,source.inputs.at(0),face->asStr()).frame;
    else if(const auto* sketch=g_doc.findSketch(source.get("sketch")))frame=resolvePlane(g_doc,sketch->sk.plane);
    else if(const auto* profile=source.nativeSpec.find("profile"))if(const auto* authored=profile->find("frame")){if(authored->t!=V::Arr||authored->arr.size()!=16)throw std::runtime_error("Extrusion profile needs an explicit rigid source frame");for(int i=0;i<16;++i)frame.m[i]=authored->arr[(size_t)i].t==V::Str?formNumber(authored->arr[(size_t)i].str):authored->arr[(size_t)i].num;}
    return frame.transformDir({0,0,1}).normalized()*number("direction",1);
}
static void beginDirectHandle(){
    if(auto* feature=directResizeConsumer()){
        Feature source=*feature;Vec3 origin,axis;if(!directFaceHandle(source.id,"cap.end",origin,axis))throw std::runtime_error("The extrusion end cap is unavailable; inspect current source references");axis=directResizeAxis(source);
        beginDirectAuthoring(0);auto& edit=g_directAuthoring;edit.feature=source.id;edit.source=source;edit.origin=origin;edit.axis=axis.normalized();edit.world=Mat4::identity();edit.key="depth";edit.depthScale=source.num(g_doc.buildEnv(),"symmetric",0)?2:1;edit.name="Resize / "+source.name;
        SelectionItem face;face.kind="face";face.feature=source.id;face.name="cap.end";g_picked={face};refreshSelectionLines();g_status="Resize the existing extrusion / drag end cap / Tab dimension / Esc cancel";
    }else beginSketchExtrusion(true);
}
static void previewDirectAuthoring(){
    auto& edit=g_directAuthoring;edit.error.clear();edit.receipt.reset();edit.commitRequested=false;
    try{if(!directCurrent())throw std::runtime_error("Drawing changed / cancel and select current geometry");edit.commands=directCommands();edit.pending=true;edit.generation=directPreviewWorker().request(edit.commands,edit.mode==0?edit.feature:0,edit.world);g_status=std::string(directModeName(edit.mode))+" / "+directDisplayDistance(edit.amount)+" / calculating exact preview / Esc cancels";if(g_directPreviewTestWait)awaitDirectAuthoringPreview();
    }catch(const std::exception& error){edit.error=error.what();g_status="Direct edit rejected / "+edit.error;}
}
static void invalidateDirectGesture(){
    auto& edit=g_directAuthoring;edit.gesture=directPreviewWorker().begin(edit.baseline,edit.revision,edit.epoch);edit.generation=edit.resolvedGeneration=0;edit.pending=edit.commitRequested=false;edit.receipt.reset();edit.preview={};edit.previewFill.clear();edit.previewLines.clear();
}
static bool beginDirectAuthoringDrag(int x,int y){
    using V=arkjson::Value;
    auto& edit=g_directAuthoring;if(!edit.active)return false;if(!directCurrent())throw std::runtime_error("Direct authoring is stale; cancel and select the current drawing");
    invalidateDirectGesture();
    if(edit.mode==0&&edit.handleStart&&!edit.key.empty()){edit.handleStart=false;edit.dragging=true;edit.moved=false;edit.x=x;edit.y=y;edit.amount=0;edit.delta={};edit.expression.clear();edit.coordinateExpressions={};return true;}
    if(edit.mode==5){
        if(edit.limitPicking){auto hit=pickSurface(x,y);if(!hit.hit)throw std::runtime_error("Select an exact planar limit face");if(g_instanceIds[(size_t)hit.body])throw std::runtime_error("Select the limit face in part coordinates, not a placed occurrence");const auto& picks=g_partPicks[g_instancePart[(size_t)hit.body]];std::string face;for(size_t i=0;i<picks.faces.size();++i)if((int)hit.triangle>=picks.faces[i].first&&(int)hit.triangle<picks.faces[i].first+picks.faces[i].second)face=picks.faceNames[i];if(face.empty())throw std::runtime_error("Limit face has no exact source reference");edit.limitFeature=g_bodyIds[(size_t)hit.body];edit.limitFace=face;edit.limitPicking=false;edit.symmetric=false;edit.moved=true;g_dragged=true;previewDirectAuthoring();return true;}
        edit.dragStartAmount=edit.handleStart?edit.amount*edit.direction:0;edit.handleStart=false;edit.dragging=true;edit.moved=false;edit.x=x;edit.y=y;edit.expression.clear();edit.amount=std::abs(edit.dragStartAmount);edit.limitFeature=0;edit.limitFace.clear();edit.delta={};edit.error.clear();edit.preview={};edit.commands={};return true;
    }auto hit=pickSurface(x,y);if(!hit.hit){g_status="Click visible geometry to start a direct edit";return true;}if(g_instanceIds[(size_t)hit.body]&&edit.mode!=1)throw std::runtime_error("This is a placed occurrence. Edit its part definition to change source geometry");
    edit.dragging=false;edit.moved=false;edit.error.clear();edit.commands={};edit.preview={};edit.key.clear();edit.body=hit.body;edit.feature=g_bodyIds[(size_t)hit.body];auto* feature=g_doc.find(edit.feature);if(!feature)throw std::runtime_error("Selected source was removed");edit.source=*feature;edit.world=g_instanceWorld[(size_t)hit.body];edit.origin=edit.world.transformPoint(hit.local);edit.x=x;edit.y=y;edit.amount=0;edit.delta={};edit.expression.clear();edit.coordinateExpressions={};edit.vertices.clear();edit.triangles.clear();edit.selection.clear();edit.nativeVertex=false;edit.sourceVertex.clear();edit.name=std::string(directModeName(edit.mode))+" / "+feature->name;selBody=hit.body;
    const Body* input=nullptr;for(const auto& body:g_doc.evalBodies)if(body.id==edit.feature)input=&body;if(!input)throw std::runtime_error("Selected source has no evaluated body");Mesh mesh=input->baked&&input->baked->triCount()?*input->baked:input->tessellate(TessOptions{});edit.topology=meshTopologyFingerprint(mesh);edit.requireClosed=analyzeMesh(mesh).closedOriented();
    if(edit.mode==0){size_t part=g_instancePart[(size_t)hit.body];const auto& pick=g_partPicks[part];int face=-1;for(size_t i=0;i<pick.faces.size();++i)if((int)hit.triangle>=pick.faces[i].first&&(int)hit.triangle<pick.faces[i].first+pick.faces[i].second)face=(int)i;if(face<0||pick.faceNames[(size_t)face].empty())throw std::runtime_error("This face has no exact source reference; use Mesh patch or source dimensions");const auto name=pick.faceNames[(size_t)face];const auto* topology=input->nativeData.find("topology"),*rows=topology?topology->find("references"):nullptr;Vec3 normal{};if(rows)for(const auto& row:rows->arr)if(row.find("name")&&row.find("name")->asStr()==name){auto* descriptor=row.find("descriptor"),*n=descriptor?descriptor->find("normal"):nullptr;if(n&&n->t==V::Arr&&n->arr.size()==3)normal={n->arr[0].num,n->arr[1].num,n->arr[2].num};}
        auto operation=featureOperation(*feature);if(feature->type==FeatureType::NativeSolid&&operation=="extrude"&&name=="cap.end"){auto* sketch=g_doc.findSketch(feature->get("sketch"));auto* faceSource=feature->nativeSpec.find("source_face");if(!sketch&&!faceSource)throw std::runtime_error("Extrusion source sketch was removed");if(auto* mode=feature->nativeSpec.find("mode");mode&&mode->asStr()=="up_to_face")throw std::runtime_error("This cap follows an associative limit face; edit its source or limit face");normal=(faceSource?resolveGeometryReference(g_doc,feature->inputs.at(0),faceSource->asStr()).frame:resolvePlane(g_doc,sketch->sk.plane)).transformDir({0,0,1}).normalized()*feature->num(g_doc.buildEnv(),"direction",1);edit.depthScale=feature->num(g_doc.buildEnv(),"symmetric",0)?2:1;edit.key="depth";}else if(feature->type==FeatureType::NativeSolid&&operation=="extrude")throw std::runtime_error("Drag the extrusion end cap; its source profile and start cap stay anchored");if(feature->type!=FeatureType::NativeSolid||normal.len()<.9)throw std::runtime_error("Push / pull supports source end caps of exact extrusions and planar faces of native box, cylinder and cone primitives. Use Source dimensions for this recipe");if(operation=="extrude"&&name=="cap.end"){}else if(operation=="box"){if(normal.x>.999)edit.key="width";else if(normal.y>.999)edit.key="depth";else if(normal.z>.999)edit.key="height";else throw std::runtime_error("Select the positive source face; negative faces are fixed anchors in this primitive recipe");}else if((operation=="cylinder"||operation=="cone")&&normal.z>.999)edit.key="height";else throw std::runtime_error("This source face is fixed or curved; edit source dimensions");edit.axis=edit.world.transformDir(normal).normalized();SelectionItem selected;selected.kind="face";selected.feature=edit.feature;selected.name=name;g_picked={selected};refreshSelectionLines();
    }else if(edit.mode==3&&isNativeFeature(feature->type)){
        const auto* topology=input->nativeData.find("topology"),*vertices=topology?topology->find("vertices"):nullptr;
        if(!vertices||vertices->t!=V::Arr)throw std::runtime_error("This exact source has no topological vertex inventory; inspect supported polyhedral source geometry");
        double best=18;const V* chosen=nullptr;Vec3 sourcePoint;
        for(const auto& vertex:vertices->arr){const auto* point=vertex.find("point_mm");if(!point||point->t!=V::Arr||point->arr.size()!=3)continue;Vec3 local{point->arr[0].num,point->arr[1].num,point->arr[2].num};float sx,sy;if(!projectScreen(edit.world.transformPoint(local),sx,sy))continue;double distance=std::hypot(sx-x,sy-y);if(distance<best){best=distance;chosen=&vertex;sourcePoint=local;}}
        if(!chosen)throw std::runtime_error("Click within 18 pixels of an exact source vertex");
        edit.nativeVertex=true;edit.sourceVertex=chosen->find("name")->str;edit.topology=chosen->find("source_topology")->str;edit.origin=edit.world.transformPoint(sourcePoint);edit.selection={(float)edit.origin.x,(float)edit.origin.y,(float)edit.origin.z};g_picked.clear();refreshSelectionLines();
    }else if(edit.mode>=2){if(isNativeFeature(feature->type))throw std::runtime_error("Mesh editing needs an explicit sampled source. Use Sample and weld mesh, then select that result; exact B-rep source stays preserved");if(edit.mode==2){edit.triangles=directMeshPatch(mesh,hit.triangle);edit.axis=edit.world.transformDir(directTriangleNormal(mesh,hit.triangle)).normalized();std::set<u32> vertices;for(auto triangle:edit.triangles)for(int k=0;k<3;++k)vertices.insert(mesh.idx[(size_t)triangle*3+k]);edit.vertices.assign(vertices.begin(),vertices.end());}else{double best=1e30;u32 nearest=0;for(int k=0;k<3;++k){u32 vertex=mesh.idx[(size_t)hit.triangle*3+k];float sx,sy;if(projectScreen(edit.world.transformPoint(directMeshPoint(mesh,vertex)),sx,sy)){double distance=std::hypot(sx-x,sy-y);if(distance<best){best=distance;nearest=vertex;}}}if(edit.mode==3){if(best>18)throw std::runtime_error("Click within 18 pixels of a mesh vertex");edit.vertices={nearest};}else{double edgeBest=1e30;for(int k=0;k<3;++k){u32 a=mesh.idx[(size_t)hit.triangle*3+k],b=mesh.idx[(size_t)hit.triangle*3+(k+1)%3];float ax,ay,bx,by;if(!projectScreen(edit.world.transformPoint(directMeshPoint(mesh,a)),ax,ay)||!projectScreen(edit.world.transformPoint(directMeshPoint(mesh,b)),bx,by))continue;double dx=bx-ax,dy=by-ay,t=clamp(((x-ax)*dx+(y-ay)*dy)/std::max(1e-9,dx*dx+dy*dy),0.,1.),distance=std::hypot(ax+t*dx-x,ay+t*dy-y);if(distance<edgeBest){edgeBest=distance;edit.vertices={a,b};}}if(edgeBest>18)throw std::runtime_error("Click within 18 pixels of a mesh edge");}}for(auto vertex:edit.vertices){auto point=edit.world.transformPoint(directMeshPoint(mesh,vertex));edit.selection.insert(edit.selection.end(),{(float)point.x,(float)point.y,(float)point.z});}
    }else{edit.axis=cameraFrame(yaw,pitch,cameraRoll).right;for(u32 vertex=0;vertex<mesh.pos.size()/3;++vertex)edit.vertices.push_back(vertex);g_picked.clear();refreshSelectionLines();}
    edit.dragging=true;edit.moved=false;g_status=std::string(directModeName(edit.mode))+" / drag preview / Tab exact value / release applies / Esc cancels";return true;
}
static void moveDirectAuthoringDrag(int x,int y){
    auto& edit=g_directAuthoring;if(!edit.dragging)return;if(!edit.moved&&std::abs(x-edit.x)+std::abs(y-edit.y)<4)return;edit.moved=true;edit.expression.clear();edit.coordinateExpressions={};auto frame=cameraFrame(yaw,pitch,cameraRoll);double scale=2*dist*std::tan(deg2rad(35)*.5)/std::max(viewH,1);
    if(edit.mode==0||edit.mode==2||edit.mode==5){float ax,ay,bx,by;double measure=std::max(10.,dist*.08);if(projectScreen(edit.origin,ax,ay)&&projectScreen(edit.origin+edit.axis*measure,bx,by)&&std::hypot(bx-ax,by-ay)>3){double dx=bx-ax,dy=by-ay;edit.amount=((x-edit.x)*dx+(y-edit.y)*dy)/(dx*dx+dy*dy)*measure;}else edit.amount=(edit.y-y)*scale;if(edit.mode==5){double signedDepth=edit.dragStartAmount+edit.amount*(edit.symmetric?2:1);edit.direction=signedDepth<0?-1:1;edit.amount=std::abs(signedDepth);}edit.delta=edit.axis*(edit.amount*(edit.mode==5?edit.direction:1));}
    else{edit.delta=frame.right*((x-edit.x)*scale)+frame.up*((edit.y-y)*scale);edit.amount=edit.delta.len();}previewDirectAuthoring();
}
static void directDimensionDialog(){
    auto& edit=g_directAuthoring;if(!edit.active||(!edit.feature&&edit.mode!=5))throw std::runtime_error("Drag a source face or mesh element before entering its dimensions");if(!directCurrent())throw std::runtime_error("Direct preview is stale; cancel and select current geometry");invalidateDirectGesture();std::vector<EditorField> fields;if(edit.mode==0||edit.mode==2||edit.mode==5)fields={{edit.mode==5?"Positive depth / expression":"Signed distance / expression",edit.expression.empty()?drawingNumber(edit.amount):edit.expression}};else{auto local=edit.world.inverted().transformDir(edit.delta);fields={{"X displacement / mm",drawingNumber(local.x)},{"Y displacement / mm",drawingNumber(local.y)},{"Z displacement / mm",drawingNumber(local.z)}};}if(!editFields("Live direct edit dimensions","Values remain an unsaved recipe until Apply or drag release. Expressions may use named source parameters.",fields)){if(edit.moved&&directCurrent())previewDirectAuthoring();return;}if(!directCurrent())throw std::runtime_error("Drawing changed during dimension entry");if(fields.size()==1){edit.expression=trimField(fields[0].value);edit.amount=formNumber(edit.expression);if(edit.mode==5){edit.limitPicking=false;edit.limitFeature=0;edit.limitFace.clear();}edit.delta=edit.axis*(edit.amount*(edit.mode==5?edit.direction:1));}else{for(size_t i=0;i<3;++i)edit.coordinateExpressions[i]=trimField(fields[i].value);edit.delta=edit.world.transformDir({formNumber(fields[0].value),formNumber(fields[1].value),formNumber(fields[2].value)});edit.amount=edit.delta.len();}edit.moved=true;previewDirectAuthoring();if(!edit.error.empty())throw std::runtime_error(edit.error);
}
static void applyDirectAuthoring(){
    using V=arkjson::Value;auto& edit=g_directAuthoring;if(!edit.active||(!edit.feature&&edit.mode!=5)||!edit.moved)throw std::runtime_error("Move the preview or enter a dimension before applying");if(!directCurrent())throw std::runtime_error("Direct preview is stale; drawing retained");if(!edit.error.empty())throw std::runtime_error(edit.error);
    edit.dragging=false;if(edit.pending||edit.resolvedGeneration!=edit.generation){edit.commitRequested=true;g_status="Finishing the latest exact preview / Esc cancels";return;}
    if(!edit.receipt)throw std::runtime_error("The current recipe has no validated geometry receipt");
    const auto candidateId=edit.receipt->id();g_session.adoptPreparedCandidate(std::move(*edit.receipt),edit.revision);edit.receipt.reset();
    V args=V::makeObj(),actor=V::makeObj();actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));args.set("actor",actor);args.set("candidate_id",V::makeStr(candidateId));args.set("expected_revision",V::makeNum((double)edit.revision));runTool("doc_apply",args);if(g_error)return;
    int mode=edit.mode;u64 feature=mode==0?edit.feature:g_doc.features.back().id;directPreviewWorker().cancel();g_directAuthoring={};for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==feature){selBody=(int)i;break;}
    g_activeSketch.clear();g_showSketches=false;g_picked.clear();rebuildSketchOverlay();refreshSelectionLines();g_status="Direct recipe saved atomically / exact preview reused / r"+std::to_string(g_session.revision);
}
static void directSourceDimensions(){
    using V=arkjson::Value;auto* feature=selectedPartFeature();if(!feature)throw std::runtime_error("Select a part to edit its source dimensions");Feature snapshot=*feature;const u64 revision=g_session.revision,epoch=g_documentEpoch;std::vector<EditorField> fields;std::vector<std::string> keys;for(const auto& parameter:snapshot.p)if(parameter.first!="op"&&parameter.first!="sketch"){bool ok=false;evalExpr(parameter.second,g_doc.buildEnv(),&ok);if(ok){keys.push_back(parameter.first);fields.push_back({parameter.first+" / expression",parameter.second});}}if(fields.empty())throw std::runtime_error("This feature is driven by a sketch or native recipe. Use Edit driving sketch or Edit feature");if(!editFields("Source dimensions",snapshot.name.c_str(),fields))return;V p=V::makeObj(),command=V::makeObj();for(size_t i=0;i<fields.size();++i)p.set(keys[i],V::makeStr(trimField(fields[i].value)));command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)snapshot.id));command.set("p",p);applyUiCommandAtRevision(command,revision,epoch);
}
static void sampleWeldDirectMesh(){
    using V=arkjson::Value;auto* source=selectedPartFeature();if(!source)throw std::runtime_error("Select an exact or sampled body to convert");const u64 revision=g_session.revision,epoch=g_documentEpoch,id=source->id;const bool sample=isNativeFeature(source->type);std::vector<EditorField> fields{{"Mesh name","Editable mesh / "+source->name},{"Explicit weld tolerance / mm","0.0001"},{"Use display tessellation as mesh authority","1",true}};if(!editFields("Sample and weld mesh","Creates a separate sampled recipe and joins only vertices within the authored tolerance. Exact source remains in history. Mesh validity is checked; self intersections are unqualified.",fields)||fields[2].value!="1")return;V command=V::makeObj(),p=V::makeObj(),inputs=V::makeArr();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("mesh_weld"));command.set("name",V::makeStr(fields[0].value));inputs.arr.push_back(V::makeNum((double)id));command.set("inputs",inputs);p.set("epsilon",V::makeStr(fields[1].value));if(sample)p.set("sample",V::makeStr("1"));command.set("p",p);applyUiCommandAtRevision(command,revision,epoch);if(!g_error)selBody=(int)g_bodyIds.size()-1;
}
static void directExtrusionOption(int id){auto& edit=g_directAuthoring;if(!edit.active||edit.mode!=5)throw std::runtime_error("Select a profile and start an exact extrusion");if(!directCurrent())throw std::runtime_error("Extrusion preview is stale; select the current source");if(id==689)edit.direction=-edit.direction;else if(id==690){edit.symmetric=!edit.symmetric;edit.limitFeature=0;edit.limitFace.clear();edit.limitPicking=false;}else if(id==691){invalidateDirectGesture();edit.limitPicking=true;edit.symmetric=false;g_status="Up to face / click an exact planar face parallel to and covering the complete profile / Esc cancels";return;}else if(id==692){edit.limitFeature=0;edit.limitFace.clear();edit.limitPicking=false;}if(edit.moved)previewDirectAuthoring();}
static bool handleDirectAuthoringAction(int id){if(id>=650&&id<=654){beginDirectAuthoring(id-650);return true;}if(id==687){beginSketchExtrusion();return true;}if(id==688){beginDirectHandle();return true;}if(id>=689&&id<=692){directExtrusionOption(id);return true;}if(id==655){directDimensionDialog();return true;}if(id==656){applyDirectAuthoring();return true;}if(id==657){cancelDirectAuthoring();return true;}if(id==659){directSourceDimensions();return true;}if(id==754){sampleWeldDirectMesh();return true;}return false;}
static bool directHandlePosition(Vec3& origin,Vec3& axis){
    if(g_directAuthoring.active){auto& edit=g_directAuthoring;if((edit.mode!=5&&!(edit.mode==0&&!edit.key.empty()))||!directCurrent()||edit.limitPicking)return false;origin=edit.origin;axis=edit.axis*(edit.mode==5?edit.direction:1);return true;}
    if(g_busy||measurementActive()||g_sketchEditing.active||g_sketchDrawing.active||g_componentMove.armed)return false;
    std::string name;SelectionItem face;auto* resize=directResizeConsumer();if(resize){face.feature=resize->id;face.name="cap.end";}else if(!directSelectedProfile(name,face))return false;
    // Cache by source revision and selection. No worker query in the paint loop.
    struct Cache {u64 revision=~u64(0),epoch=0,feature=0;std::string face,sketch;Vec3 origin,axis;bool valid=false;};static Cache cache;
    if(cache.revision!=g_session.revision||cache.epoch!=g_documentEpoch||cache.feature!=face.feature||cache.face!=face.name||cache.sketch!=name){
        cache={g_session.revision,g_documentEpoch,face.feature,face.name,name,{},{},false};
        try{if(!face.name.empty()){cache.valid=directFaceHandle(face.feature,face.name,cache.origin,cache.axis);if(resize&&cache.valid)cache.axis=directResizeAxis(*resize);}else if(auto* sketch=g_doc.findSketch(name)){auto frame=resolvePlane(g_doc,sketch->sk.plane);cache.origin=directProfileCenter(*sketch,frame);cache.axis=frame.transformDir({0,0,1});cache.valid=true;}}catch(const std::exception&){}
    }
    origin=cache.origin;axis=cache.axis;return cache.valid;
}
template<class Rect,class Button>static void drawExtrusionHandle(Rect& rect,Button& button){
    g_directHandleOverlay={};
    Vec3 origin,axis;if(!directHandlePosition(origin,axis))return;auto& edit=g_directAuthoring;if(edit.active)origin+=axis*(edit.amount*(edit.mode==5&&edit.symmetric?.5:1));float x,y;if(!projectScreen(origin,x,y)||x<viewX+28||x>viewX+viewW-135||y<viewY+185||y>viewY+viewH-42)return;
    button("^",x-17,y-16,34,688,true);std::string label=edit.active?directDisplayDistance(edit.mode==0?edit.source.num(g_doc.buildEnv(),edit.key.c_str(),0)+edit.amount*edit.depthScale:edit.amount):directResizeConsumer()?"Drag to resize":"Drag to extrude";float width=std::min(180.f,textWidth(label,.56f)+20);rect(x+24,y-14,width,28,6,.035f,.13f,.15f,.96f,.15f,.81f,.77f,.8f,1);textOut(label,x+34,y-8,.56f,.69f,.97f,.94f);g_directHandleOverlay={x-17,y-16,width+41,32,true};
}
static void avoidExtrusionHandle(float x,float& y,float width,float height){const auto& handle=g_directHandleOverlay;if(!handle.visible||x>=handle.x+handle.w||x+width<=handle.x||y>=handle.y+handle.h||y+height<=handle.y)return;float below=handle.y+handle.h+10;y=below+height<=viewY+viewH-8?below:std::max((float)viewY+185,handle.y-height-10);}
static void drawDirectAuthoringPreview(){
    auto& edit=g_directAuthoring;if(!edit.active||!directCurrent())return;
    static GLuint vao[4]={},vbo[4]={};static u64 uploadedGesture=0,uploadedGeneration=0;static GLsizei counts[3]={};static std::vector<float> selected;
    if(!vao[0]){glGenVertexArrays(4,vao);glGenBuffers(4,vbo);for(int i=0;i<4;++i){glBindVertexArray(vao[i]);glBindBuffer(GL_ARRAY_BUFFER,vbo[i]);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);}}
    if(uploadedGesture!=edit.gesture||uploadedGeneration!=edit.resolvedGeneration){const std::vector<float>* data[]={&edit.previewFill,&edit.previewLines,&edit.selection};for(int i=0;i<3;++i){glBindBuffer(GL_ARRAY_BUFFER,vbo[i]);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(data[i]->size()*sizeof(float)),data[i]->data(),GL_STATIC_DRAW);counts[i]=(GLsizei)data[i]->size()/3;}uploadedGesture=edit.gesture;uploadedGeneration=edit.resolvedGeneration;}
    if(selected!=edit.selection){selected=edit.selection;glBindBuffer(GL_ARRAY_BUFFER,vbo[2]);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(selected.size()*sizeof(float)),selected.data(),GL_STATIC_DRAW);counts[2]=(GLsizei)selected.size()/3;}
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pSketch);auto matrix=cameraProjection()*cameraView();float vp[16];for(int i=0;i<16;++i)vp[i]=(float)matrix.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,vp);
    auto draw=[&](int buffer,GLenum mode,float alpha){if(!counts[buffer])return;glBindVertexArray(vao[buffer]);glUniform4f(glGetUniformLocation(pSketch,"tint"),.13f,.9f,.87f,alpha);glDrawArrays(mode,0,counts[buffer]);};
    if(edit.error.empty()){draw(0,GL_TRIANGLES,edit.pending?.08f:.16f);draw(1,GL_LINES,edit.pending?.28f:.64f);}glPointSize(9);draw(2,GL_POINTS,1);glPointSize(1);
    if(edit.mode==5||edit.mode==0){auto end=edit.origin+edit.axis*(edit.mode==5?edit.direction:1)*std::max(20.,std::abs(edit.amount)*(edit.mode==5&&edit.symmetric?.5:1));float guide[]={(float)edit.origin.x,(float)edit.origin.y,(float)edit.origin.z,(float)end.x,(float)end.y,(float)end.z};glBindVertexArray(vao[3]);glBindBuffer(GL_ARRAY_BUFFER,vbo[3]);glBufferData(GL_ARRAY_BUFFER,sizeof(guide),guide,GL_DYNAMIC_DRAW);glUniform4f(glGetUniformLocation(pSketch,"tint"),.13f,.9f,.87f,.85f);glDrawArrays(GL_LINES,0,2);}
    glViewport(0,0,winW,winH);
}
