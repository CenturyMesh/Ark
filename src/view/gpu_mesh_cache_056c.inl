// Display representations only. Positions and index order are unchanged; exact
// picking, B-reps and simulation inputs continue to use their original sources.
// GPUMesh is an explicit handle: existing vector moves transfer ownership, and
// releaseMesh must be called once for each acquired handle.
#include "gpu_cache_policy_070a.hpp"
#include "ark/sha256_074b.hpp"
struct PackedDisplayVertex056 {float p[3];std::int16_t n[4];};
static_assert(sizeof(PackedDisplayVertex056)==20);
struct CachedDisplayMesh056 {GPUMesh mesh;size_t references=0,bytes=0;std::uint64_t used=0;};
static std::unordered_map<std::string,CachedDisplayMesh056> g_displayMeshCache056;
static size_t g_displayMeshIdleBytes056=0;
static size_t displayMeshIdleLimit056=32*1024*1024;
static std::uint64_t g_displayMeshPacks070=0,g_displayMeshEvictions070=0;
static size_t g_displayMeshResidentBytes078=0,g_displayMeshPeakResidentBytes078=0,g_displayMeshPeakStagingBytes078=0;
static std::uint64_t g_displayMeshUploadChunks078=0,g_displayMeshAllocationFailures078=0;
static std::string g_displayMeshAllocationError078;
static bool g_displayFailNextAllocation078=false; // private native self-test fault injection
static constexpr size_t displayUploadVertices078=65536;
static bool g_displayMemoryQueried070=false,g_displayMemorySupported070=false;
static std::uint64_t g_displayMemoryDedicatedKB070=0,g_displayMemoryFreeKB070=0;
static size_t displayMeshBudget070(bool force=false){
    static ULONGLONG last=0;const auto tick=GetTickCount64();
    if(!force&&g_displayMemoryQueried070&&tick-last<2000)return displayMeshIdleLimit056;
    last=tick;
    if(!g_displayMemoryQueried070){
        g_displayMemoryQueried070=true;
        if(auto getStringAt=reinterpret_cast<studio_shader_cache::StringAt>(studio_shader_cache::address("glGetStringi"))){
            GLint count=0;glGetIntegerv(0x821D,&count);for(GLint i=0;i<count;++i){const auto* extension=getStringAt(GL_EXTENSIONS,i);
                if(extension&&std::strcmp((const char*)extension,"GL_NVX_gpu_memory_info")==0){g_displayMemorySupported070=true;break;}}
        }
    }
    size_t requested=256;if(const char* value=std::getenv("ARK_GPU_MESH_IDLE_MB");value&&*value){char* end=nullptr;const auto n=std::strtoul(value,&end,10);if(end!=value&&!*end&&n<=1024)requested=(size_t)n;}
    if(g_displayMemorySupported070){GLint dedicated=0,free=0;glGetIntegerv(0x9047,&dedicated);glGetIntegerv(0x9049,&free);
        g_displayMemoryDedicatedKB070=dedicated>0?(std::uint64_t)dedicated:0;g_displayMemoryFreeKB070=free>0?(std::uint64_t)free:0;}
    displayMeshIdleLimit056=displayIdleBudget070(g_displayMemoryDedicatedKB070,g_displayMemoryFreeKB070,requested);return displayMeshIdleLimit056;
}
static std::uint64_t g_displayMeshClock056=0,g_displayMeshHits056=0,g_displayMeshUploads056=0,g_displayMeshUploadedBytes056=0;
static void deleteDisplayMesh056(const GPUMesh& mesh){if(mesh.vbo)glDeleteBuffers(1,&mesh.vbo);if(mesh.ibo)glDeleteBuffers(1,&mesh.ibo);if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);}
static void trimDisplayMeshCache056(size_t limit=std::numeric_limits<size_t>::max()){
    if(limit==std::numeric_limits<size_t>::max())limit=displayMeshBudget070();
    while(g_displayMeshIdleBytes056>limit){auto oldest=g_displayMeshCache056.end();for(auto it=g_displayMeshCache056.begin();it!=g_displayMeshCache056.end();++it)if(!it->second.references&&(oldest==g_displayMeshCache056.end()||it->second.used<oldest->second.used))oldest=it;
        if(oldest==g_displayMeshCache056.end())break;g_displayMeshIdleBytes056-=oldest->second.bytes;g_displayMeshResidentBytes078-=oldest->second.bytes;deleteDisplayMesh056(oldest->second.mesh);g_displayMeshCache056.erase(oldest);++g_displayMeshEvictions070;}
}
static void releaseMesh(GPUMesh& mesh){
    if(!mesh.vao){mesh={};return;}
    auto found=g_displayMeshCache056.find(mesh.cacheKey);
    if(found==g_displayMeshCache056.end())deleteDisplayMesh056(mesh);
    else if(found->second.references&&!--found->second.references){found->second.used=++g_displayMeshClock056;g_displayMeshIdleBytes056+=found->second.bytes;}
    mesh={};trimDisplayMeshCache056();
}
static void packDisplayMeshRange078(const Mesh& mesh,size_t first,size_t count,std::vector<PackedDisplayVertex056>& packed){
    packed.resize(count);
    for(size_t j=0;j<count;++j){const size_t i=first+j;auto& v=packed[j];std::memcpy(v.p,mesh.pos.data()+3*i,sizeof v.p);
        Vec3 n{0,0,1};if(mesh.nrm.size()>=3*i+3)n={mesh.nrm[3*i],mesh.nrm[3*i+1],mesh.nrm[3*i+2]};double length=n.len();if(std::isfinite(length)&&length>1e-20)n=n/length;else n={0,0,1};
        const double components[]={n.x,n.y,n.z};for(int k=0;k<3;++k)v.n[k]=(std::int16_t)std::lround(std::clamp(components[k],-1.,1.)*32767.);v.n[3]=0;}
}
static std::vector<PackedDisplayVertex056> packDisplayMesh056(const Mesh& mesh){++g_displayMeshPacks070;std::vector<PackedDisplayVertex056> packed;packDisplayMeshRange078(mesh,0,mesh.vertCount(),packed);return packed;}
static std::string displayMeshIdentity078(const Mesh& mesh){
    const std::string prefix="ark-display-source-070a:"+std::to_string(mesh.pos.size())+":"+std::to_string(mesh.nrm.size())+":"+std::to_string(mesh.idx.size())+":";
    const auto view=[](const auto& data){return data.empty()?std::string_view{}:std::string_view(reinterpret_cast<const char*>(data.data()),data.size()*sizeof(data[0]));};
    std::string key;if(ark::platformSha256Segments078B({prefix,view(mesh.pos),view(mesh.idx),view(mesh.nrm)},key))return key;
    // Keep the exact legacy digest when platform acceleration is unavailable.
    std::string identity=prefix;for(auto bytes:{view(mesh.pos),view(mesh.idx),view(mesh.nrm)})if(!bytes.empty())identity.append(bytes);return iterationSha256(identity);
}
static bool uploadMesh(GPUMesh& gm,const Mesh& mesh){
    GLint previousVao=0,previousArray=0;bool bindingsKnown=false;
    auto failed=[&](const char* reason){++g_displayMeshAllocationFailures078;g_displayMeshAllocationError078=reason;
        // Consume this failed operation's remaining errors, not a successful
        // frame's evidence. Retry starts explicitly with a fresh upload.
        for(int i=0;i<32&&glGetError()!=GL_NO_ERROR;++i){}
        if(bindingsKnown){glBindVertexArray((GLuint)previousVao);glBindBuffer(GL_ARRAY_BUFFER,(GLuint)previousArray);}return false;};
    const auto priorError=glGetError();
    if(priorError!=GL_NO_ERROR)return failed("OpenGL error existed before mesh upload; display unchanged. Open Resources to retry rendering.");
    glGetIntegerv(0x85B5,&previousVao);glGetIntegerv(0x8894,&previousArray);bindingsKnown=true;
    try{
    const size_t vertexBytes=mesh.vertCount()*sizeof(PackedDisplayVertex056),indexBytes=mesh.idx.size()*sizeof(u32);
    // Source normals are hashed before display quantization, exactly as before.
    const std::string key=displayMeshIdentity078(mesh);
    if(gm.vao&&gm.cacheKey==key){++g_displayMeshHits056;return true;}
    // Acquire before releasing the old handle, so an idle target cannot be
    // evicted by that release when the cache is at its limit.
    auto found=g_displayMeshCache056.find(key);
    if(found!=g_displayMeshCache056.end()){
        GPUMesh acquired=found->second.mesh;
        if(!found->second.references)g_displayMeshIdleBytes056-=found->second.bytes;
        ++found->second.references;found->second.used=++g_displayMeshClock056;
        acquired.serial=++g_displayMeshSerial;releaseMesh(gm);gm=std::move(acquired);++g_displayMeshHits056;return true;
    }
    const auto idleLimit=displayMeshBudget070(true);
    trimDisplayMeshCache056(displayUploadIdleBudget078(g_displayMemoryDedicatedKB070,g_displayMemoryFreeKB070,vertexBytes+indexBytes,g_displayMeshIdleBytes056,idleLimit));
    // An idle buffer may have been the last GL binding even though nobody owns
    // it. Reclamation can reset that binding; never recreate its deleted name
    // while restoring state after a later failed allocation.
    glGetIntegerv(0x85B5,&previousVao);glGetIntegerv(0x8894,&previousArray);
    GPUMesh next;next.cacheKey=key;next.serial=++g_displayMeshSerial;next.count=(int)mesh.triCount();
    struct Pending {GPUMesh& mesh;bool committed=false;~Pending(){if(!committed)deleteDisplayMesh056(mesh);}} pending{next};
    const char* allocationError="GPU display allocation failed; exact document retained. Open Resources to retry rendering.";
    using SubData=void(APIENTRY*)(GLenum,GLsizeiptr,GLsizeiptr,const void*);
    static const auto subData=reinterpret_cast<SubData>(studio_shader_cache::address("glBufferSubData"));
    if(!subData||g_displayFailNextAllocation078){g_displayFailNextAllocation078=false;return failed(allocationError);}
    ++g_displayMeshPacks070;
    glGenVertexArrays(1,&next.vao);glGenBuffers(1,&next.vbo);glGenBuffers(1,&next.ibo);
    glBindVertexArray(next.vao);glBindBuffer(GL_ARRAY_BUFFER,next.vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)vertexBytes,nullptr,GL_STATIC_DRAW);
    if(!next.vao||!next.vbo||!next.ibo||glGetError()!=GL_NO_ERROR)return failed(allocationError);
    std::vector<PackedDisplayVertex056> packed;packed.reserve(std::min(mesh.vertCount(),displayUploadVertices078));
    for(size_t first=0;first<mesh.vertCount();first+=displayUploadVertices078){const auto count=std::min(displayUploadVertices078,mesh.vertCount()-first);packDisplayMeshRange078(mesh,first,count,packed);g_displayMeshPeakStagingBytes078=std::max(g_displayMeshPeakStagingBytes078,packed.capacity()*sizeof(PackedDisplayVertex056));subData(GL_ARRAY_BUFFER,(GLsizeiptr)(first*sizeof(PackedDisplayVertex056)),(GLsizeiptr)(count*sizeof(PackedDisplayVertex056)),packed.data());++g_displayMeshUploadChunks078;if(glGetError()!=GL_NO_ERROR)return failed(allocationError);}
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(PackedDisplayVertex056),nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_SHORT,GL_TRUE,sizeof(PackedDisplayVertex056),(void*)12);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,next.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)indexBytes,mesh.idx.data(),GL_STATIC_DRAW);glBindVertexArray(0);
    if(glGetError()!=GL_NO_ERROR)return failed(allocationError);
    g_displayMeshCache056.emplace(key,CachedDisplayMesh056{next,1,vertexBytes+indexBytes,++g_displayMeshClock056});pending.committed=true;
    g_displayMeshResidentBytes078+=vertexBytes+indexBytes;g_displayMeshPeakResidentBytes078=std::max(g_displayMeshPeakResidentBytes078,g_displayMeshResidentBytes078);
    releaseMesh(gm);gm=std::move(next);
    ++g_displayMeshUploads056;g_displayMeshUploadedBytes056+=vertexBytes+indexBytes;
    return true;
    }catch(const std::exception&){return failed("CPU memory preparation for GPU display failed; exact document retained. Open Resources to retry rendering.");}
}
