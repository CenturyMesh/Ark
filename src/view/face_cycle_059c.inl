// B.01/A.04: keyboard traversal of exact, producer-bound faces. The occurrence
// remains the body selection; only the semantic highlight changes. No CAD writes.
struct FaceCycle059 {
    int body=-1,copy=0,cursor=-1;size_t part=0,source=0;
    u64 feature=0,instance=0,revision=0,epoch=0,displaySerial=0,selectionSerial=0;
    Mat4 world;NativePayload payload;std::string brepSha256;
    std::vector<std::string> faces;
};
static FaceCycle059 g_faceCycle059;
static void resetFaceCycle059(){g_faceCycle059={};}
static bool faceCyclePreview059(size_t body){
    if(g_allSheetsFlat||g_allSheetsPlaying||g_stockMode||g_motionActive||g_bakedMotions.active||g_inertial056.active||g_rubber059.active||explodedDisplayActive())return true;
    const auto* feature=g_doc.find(g_bodyIds[body]);
    if(!feature)return true;
    // These special displays use separate deformation/segment meshes, whose
    // triangles do not identify faces of the authored static B-rep.
    if(feature->type==FeatureType::Suspension||feature->type==FeatureType::NotchedTube||feature->type==FeatureType::SourceTube)return true;
    auto progress=g_sheetProgress.find(feature->id);
    return (progress!=g_sheetProgress.end()&&progress->second<1)||g_sheetPlayingId==feature->id;
}
static void armFaceCycle059(int body){
    resetFaceCycle059();if(body<0||(size_t)body>=g_bodyIds.size())return;
    auto& cycle=g_faceCycle059;const size_t index=(size_t)body;
    if(index>=g_instanceIds.size()||index>=g_instanceCopies.size()||index>=g_instancePart.size()||index>=g_instanceWorld.size())return;
    cycle.body=body;cycle.feature=g_bodyIds[index];cycle.instance=g_instanceIds[index];cycle.copy=g_instanceCopies[index];cycle.part=g_instancePart[index];cycle.world=g_instanceWorld[index];
    cycle.revision=g_session.revision;cycle.epoch=g_documentEpoch;cycle.displaySerial=g_displayMeshSerial;cycle.selectionSerial=g_editorPickSerial;
    cycle.source=g_doc.evalBodies.size();for(size_t i=0;i<g_doc.evalBodies.size();++i)if(g_doc.evalBodies[i].id==cycle.feature){cycle.source=i;cycle.payload=g_doc.evalBodies[i].nativeData;break;}
    const auto* topology=cycle.payload.find("topology");const auto* hash=topology?topology->find("brep_sha256"):nullptr;
    if(hash&&hash->t==arkjson::Value::Str&&hash->str.size()==64&&hash->str.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos)cycle.brepSha256=hash->str;
    if(!cycle.brepSha256.empty()&&!faceCyclePreview059(index))g_status="Body selected / Tab cycles exact faces / Shift+Tab reverses / click another body to restart";
}
static void rejectFaceCycle059(const char* reason){
    const auto& cycle=g_faceCycle059;
    if(cycle.cursor>=0&&(size_t)cycle.cursor<cycle.faces.size()&&g_picked.size()==1){const auto& pick=g_picked.front();
        if(pick.kind=="face"&&pick.feature==cycle.feature&&pick.instance==cycle.instance&&pick.copy==cycle.copy&&pick.name==cycle.faces[(size_t)cycle.cursor]){g_picked.clear();refreshSelectionLines();}}
    resetFaceCycle059();clearSelectionHover();g_error=false;g_status=reason;
}
static bool cycleFaceKey059(HWND window,bool reverse){
    // Native Edit/ComboBox controls and existing authoring tools own their Tab.
    if(GetFocus()!=window||g_toolsOpen||g_dialog||g_editorLivePick||g_searchEditing||g_pickKind!=0||measurementActive()||g_componentMove.armed||g_componentMove.active||g_cableGuide035.active||g_drawingViewport.active||(GetKeyState(VK_MENU)&0x8000))return false;
    auto& cycle=g_faceCycle059;
    if(cycle.body<0){g_status="Click a body in the viewport, then press Tab to cycle its exact faces.";return true;}
    const size_t body=(size_t)cycle.body;
    if(cycle.revision!=g_session.revision||cycle.epoch!=g_documentEpoch||cycle.displaySerial!=g_displayMeshSerial||cycle.selectionSerial!=g_editorPickSerial||selBody!=cycle.body||body>=g_bodyIds.size()||body>=g_instanceIds.size()||body>=g_instanceCopies.size()||body>=g_instancePart.size()||body>=g_instanceWorld.size()||g_bodyIds[body]!=cycle.feature||g_instanceIds[body]!=cycle.instance||g_instanceCopies[body]!=cycle.copy||g_instancePart[body]!=cycle.part){rejectFaceCycle059("Face cycle expired after a document, display or selection change / click the body again.");return true;}
    if(faceCyclePreview059(body)){rejectFaceCycle059("Exact face cycling needs the authored static / Formed view / exit the preview and click the body again.");return true;}
    if(!displayInstance(body)){rejectFaceCycle059("The selected occurrence is hidden / show it and click the body again.");return true;}
    for(int i=0;i<16;++i)if(cycle.world.m[i]!=g_instanceWorld[body].m[i]){rejectFaceCycle059("The occurrence display pose changed / click the body again in the authored view.");return true;}
    if(cycle.brepSha256.empty()||cycle.source>=g_doc.evalBodies.size()||g_doc.evalBodies[cycle.source].id!=cycle.feature||g_doc.evalBodies[cycle.source].nativeData.ownerIdentity()!=cycle.payload.ownerIdentity()||cycle.part>=g_partPicks.size()||cycle.part>=g_cpuMeshes.size()){
        rejectFaceCycle059("This body has no current exact face references / sampled mesh faces cannot be cycled as CAD faces.");return true;}
    const auto& pick=g_partPicks[cycle.part];
    if(cycle.faces.empty()){
        // Canonical producer order, once per body click. Aliases and repeated
        // tessellation spans refer to one face and never add extra cycle steps.
        std::map<int,std::string> ordered;
        for(size_t face=0;face<pick.faces.size()&&face<pick.faceNames.size()&&face<pick.faceCanonical.size();++face){
            const auto span=pick.faces[face];const auto& name=pick.faceNames[face];const int canonical=pick.faceCanonical[face];auto reference=pick.faceReferences.find(name);
            if(span.first<0||span.second<=0||(size_t)span.first+(size_t)span.second>g_cpuMeshes[cycle.part].triCount()||canonical<0||(size_t)canonical>=pick.faces.size()||reference==pick.faceReferences.end()||reference->second!=canonical||name.empty())continue;
            ordered.emplace(canonical,name);
        }
        for(const auto& face:ordered)cycle.faces.push_back(face.second);
    }
    if(cycle.faces.empty()){rejectFaceCycle059("This displayed body has no eligible exact faces / return to the authored Formed view and click again.");return true;}
    const int count=(int)cycle.faces.size();cycle.cursor=cycle.cursor<0?(reverse?count-1:0):(cycle.cursor+(reverse?count-1:1))%count;
    SelectionItem item;item.feature=cycle.feature;item.kind="face";item.name=cycle.faces[(size_t)cycle.cursor];item.instance=cycle.instance;item.copy=cycle.copy;
    g_picked={std::move(item)};clearDatumSelection();clearSelectionHover();refreshSelectionLines();cycle.selectionSerial=g_editorPickSerial;
    g_error=false;g_status="Exact face "+std::to_string(cycle.cursor+1)+" / "+std::to_string(count)+" : "+cycle.faces[(size_t)cycle.cursor]+" / Tab next / Shift+Tab previous / body occurrence retained";
    return true;
}
