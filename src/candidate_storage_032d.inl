// Allocation accounting for compact retained previews. Shared immutable bodies
// are charged once across receipts, and only while not owned by the live cache.
size_t jsonAllocationBytes(const V& value){
    size_t bytes=sizeof(V)+value.str.capacity()+1+value.arr.capacity()*sizeof(V)+value.obj.capacity()*sizeof(std::pair<std::string,V>);
    for(const auto& item:value.arr)bytes+=jsonAllocationBytes(item)-sizeof(V);
    for(const auto& item:value.obj)bytes+=item.first.capacity()+1+jsonAllocationBytes(item.second)-sizeof(V);
    return bytes;
}
size_t bodyAllocationBytes(const Body& body,std::set<const Surface*>& surfaces,std::set<const void*>& nativeOwners,std::set<const Mesh*>& meshes){
    size_t bytes=sizeof(Body)+64+body.kind.capacity()+body.faces.capacity()*sizeof(Face)+body.profile.segs.capacity()*sizeof(ProfileSeg)+body.prismHoles.capacity()*sizeof(std::vector<Vec2>);
    if(nativeOwners.insert(body.nativeData.ownerIdentity()).second)bytes+=body.nativeData.maximumRetainedBytes();
    if(body.baked&&meshes.insert(body.baked.get()).second)bytes+=sizeof(Mesh)+64+body.baked->pos.capacity()*sizeof(f32)+body.baked->nrm.capacity()*sizeof(f32)+body.baked->idx.capacity()*sizeof(u32);
    for(const auto& hole:body.prismHoles)bytes+=hole.capacity()*sizeof(Vec2);
    for(const auto& face:body.faces){
        bytes+=face.role.capacity()+face.loops.capacity()*sizeof(TrimLoop);for(const auto& loop:face.loops)bytes+=loop.uv.capacity()*sizeof(Vec2);
        if(face.surf&&surfaces.insert(face.surf.get()).second){
            // All analytic implementations are smaller than this bound.
            bytes+=512;
            if(face.surf->kind()==SurfKind::Nurbs){const auto& surface=static_cast<const NurbsSurface&>(*face.surf);bytes+=surface.U.capacity()*sizeof(f64)+surface.V.capacity()*sizeof(f64)+surface.P.capacity()*sizeof(Vec3)+surface.W.capacity()*sizeof(f64);}
        }
    }
    return bytes;
}
V compactCandidateResult(const V& full){
    V result=V::makeObj();
    for(const char* name:{"revision","feature_count","recompute_ms","features_reused","features_evaluated","parallel_features","peak_parallel_features","parameter_error","derived_geometry","created_ids","created_id","checks","conditions","created_node_ids","created_mate_ids","sheet_edits","units","history_tip","geometry_recomputed","geometry_changed","valid","error","error_code","document_unchanged"})if(const auto* value=full.find(name))result.set(name,*value);
    result.set("response_scope",V::makeStr("candidate_status"));return result;
}
