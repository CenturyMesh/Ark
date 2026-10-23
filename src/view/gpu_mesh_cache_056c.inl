// Display representations only. Positions and index order are unchanged; exact
// picking, B-reps and simulation inputs continue to use their original sources.
// GPUMesh is an explicit handle: existing vector moves transfer ownership, and
// releaseMesh must be called once for each acquired handle.
struct PackedDisplayVertex056 {float p[3];std::int16_t n[4];};
static_assert(sizeof(PackedDisplayVertex056)==20);
struct CachedDisplayMesh056 {GPUMesh mesh;size_t references=0,bytes=0;std::uint64_t used=0;};
static std::unordered_map<std::string,CachedDisplayMesh056> g_displayMeshCache056;
static size_t g_displayMeshIdleBytes056=0;
static constexpr size_t displayMeshIdleLimit056=32*1024*1024;
static std::uint64_t g_displayMeshClock056=0,g_displayMeshHits056=0,g_displayMeshUploads056=0,g_displayMeshUploadedBytes056=0;
static void deleteDisplayMesh056(const GPUMesh& mesh){if(mesh.vbo)glDeleteBuffers(1,&mesh.vbo);if(mesh.ibo)glDeleteBuffers(1,&mesh.ibo);if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);}
static void trimDisplayMeshCache056(size_t limit=displayMeshIdleLimit056){
    while(g_displayMeshIdleBytes056>limit){auto oldest=g_displayMeshCache056.end();for(auto it=g_displayMeshCache056.begin();it!=g_displayMeshCache056.end();++it)if(!it->second.references&&(oldest==g_displayMeshCache056.end()||it->second.used<oldest->second.used))oldest=it;
        if(oldest==g_displayMeshCache056.end())break;g_displayMeshIdleBytes056-=oldest->second.bytes;deleteDisplayMesh056(oldest->second.mesh);g_displayMeshCache056.erase(oldest);}
}
static void releaseMesh(GPUMesh& mesh){
    if(!mesh.vao){mesh={};return;}
    auto found=g_displayMeshCache056.find(mesh.cacheKey);
    if(found==g_displayMeshCache056.end())deleteDisplayMesh056(mesh);
    else if(found->second.references&&!--found->second.references){found->second.used=++g_displayMeshClock056;g_displayMeshIdleBytes056+=found->second.bytes;}
    mesh={};trimDisplayMeshCache056();
}
static std::vector<PackedDisplayVertex056> packDisplayMesh056(const Mesh& mesh){
    std::vector<PackedDisplayVertex056> packed(mesh.vertCount());
    for(size_t i=0;i<packed.size();++i){auto& v=packed[i];std::memcpy(v.p,mesh.pos.data()+3*i,sizeof v.p);
        Vec3 n{0,0,1};if(mesh.nrm.size()>=3*i+3)n={mesh.nrm[3*i],mesh.nrm[3*i+1],mesh.nrm[3*i+2]};double length=n.len();if(std::isfinite(length)&&length>1e-20)n=n/length;else n={0,0,1};
        const double components[]={n.x,n.y,n.z};for(int k=0;k<3;++k)v.n[k]=(std::int16_t)std::lround(std::clamp(components[k],-1.,1.)*32767.);v.n[3]=0;}
    return packed;
}
static void uploadMesh(GPUMesh& gm,const Mesh& mesh){
    auto packed=packDisplayMesh056(mesh);
    const size_t vertexBytes=packed.size()*sizeof(PackedDisplayVertex056),indexBytes=mesh.idx.size()*sizeof(u32);
    std::string identity=std::to_string(packed.size())+":"+std::to_string(mesh.idx.size())+":";
    if(vertexBytes)identity.append(reinterpret_cast<const char*>(packed.data()),vertexBytes);
    if(indexBytes)identity.append(reinterpret_cast<const char*>(mesh.idx.data()),indexBytes);
    // Keep source-normal changes visible to scene invalidation even when two
    // values round to the same display normal after normalization/quantization.
    if(!mesh.nrm.empty())identity.append(reinterpret_cast<const char*>(mesh.nrm.data()),mesh.nrm.size()*sizeof(float));
    const std::string key=iterationSha256(identity);
    if(gm.vao&&gm.cacheKey==key){++g_displayMeshHits056;return;}
    // Acquire before releasing the old handle, so an idle target cannot be
    // evicted by that release when the cache is at its limit.
    auto found=g_displayMeshCache056.find(key);
    if(found!=g_displayMeshCache056.end()){
        if(!found->second.references)g_displayMeshIdleBytes056-=found->second.bytes;
        ++found->second.references;found->second.used=++g_displayMeshClock056;
        GPUMesh acquired=found->second.mesh;acquired.serial=++g_displayMeshSerial;releaseMesh(gm);gm=std::move(acquired);++g_displayMeshHits056;return;
    }
    GPUMesh next;next.cacheKey=key;next.serial=++g_displayMeshSerial;next.count=(int)mesh.triCount();
    glGenVertexArrays(1,&next.vao);glGenBuffers(1,&next.vbo);glGenBuffers(1,&next.ibo);
    glBindVertexArray(next.vao);glBindBuffer(GL_ARRAY_BUFFER,next.vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)vertexBytes,packed.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(PackedDisplayVertex056),nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_SHORT,GL_TRUE,sizeof(PackedDisplayVertex056),(void*)12);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,next.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)indexBytes,mesh.idx.data(),GL_STATIC_DRAW);glBindVertexArray(0);
    releaseMesh(gm);g_displayMeshCache056.emplace(key,CachedDisplayMesh056{next,1,vertexBytes+indexBytes,++g_displayMeshClock056});gm=std::move(next);
    ++g_displayMeshUploads056;g_displayMeshUploadedBytes056+=vertexBytes+indexBytes;
}
