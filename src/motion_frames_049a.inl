// Included inside namespace ark by motion_graph.cpp. 049A derived bakes: the
// document keeps a receipt (bake fields plus frames_sha256/coordinates_sha256
// over the raw little-endian float32 bytes). Frames come from this process
// cache, which the optional derived section seeds on open, otherwise from a
// deterministic re-solve checked against the receipt. docs/DERIVED_BAKES_049A.md.
namespace {
constexpr size_t kFrameCacheBytes=64u*1024u*1024u,kFrameCacheEntries=256,kRebakeMemo=32;
const char kDerivedMagic[]="ARK-MOTION-FRAMES1\n";
struct FrameEntry {std::string frames,coordinates,origin;uint64_t used=0;};
struct FrameStore {std::mutex mutex;std::map<std::string,FrameEntry> entries;std::list<std::pair<std::string,V>> memo;size_t bytes=0;uint64_t clock=0;BakedMotionCacheStats stats;};
FrameStore& frameStore(){static FrameStore store;return store;}
bool sha256Text(const V* v){return v&&v->t==V::Str&&v->str.size()==64&&v->str.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string frameKey(const V& bake){return bake.find("frames_sha256")->str+bake.find("coordinates_sha256")->str;}
void cacheFrames(const std::string& key,std::string frames,std::string coordinates,const char* origin){
    auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);const size_t size=frames.size()+coordinates.size();
    if(auto at=s.entries.find(key);at!=s.entries.end()){at->second.used=++s.clock;return;}if(size>kFrameCacheBytes)return;
    while(!s.entries.empty()&&(s.entries.size()>=kFrameCacheEntries||s.bytes+size>kFrameCacheBytes)){auto oldest=std::min_element(s.entries.begin(),s.entries.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});s.bytes-=oldest->second.frames.size()+oldest->second.coordinates.size();s.entries.erase(oldest);++s.stats.evictions;}
    s.bytes+=size;s.entries.emplace(key,FrameEntry{std::move(frames),std::move(coordinates),origin,++s.clock});
}
bool cachedFrames(const std::string& key,std::string& frames,std::string& coordinates,std::string& origin){
    auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);auto at=s.entries.find(key);if(at==s.entries.end()){++s.stats.misses;return false;}
    at->second.used=++s.clock;++s.stats.hits;frames=at->second.frames;coordinates=at->second.coordinates;origin=at->second.origin;return true;
}
// Every bake field except the frame payloads.
void decodeBakeHeader(const V& bake,BakedMotionFrames& out){
    need(bake.t==V::Obj,"MOTION_NOT_BAKED: motion has no baked frames");
    auto number=[&](const char* key){const auto* p=bake.find(key);need(p&&p->t==V::Num&&std::isfinite(p->num),std::string("MOTION_INVALID_BAKE: missing ")+key);return p->num;};
    out.fps=number("fps");out.duration=number("duration_s");const double frames=number("frame_count");need(out.fps>=1&&out.fps<=kMaxFps&&out.duration>0&&out.duration<=kMaxDuration&&frames>=1&&frames<=(double)kMaxFrames&&frames==std::floor(frames),"MOTION_INVALID_BAKE: fps, duration or frame_count out of range");out.frameCount=(size_t)frames;
    const auto* instances=bake.find("instances");const auto* channels=bake.find("channels");need(instances&&instances->t==V::Arr&&channels&&channels->t==V::Arr&&channels->arr.size()<=kMaxChannels,"MOTION_INVALID_BAKE: instances and channels must be arrays");
    need(instances->arr.size()*out.frameCount<=kMaxInstanceFrames,"MOTION_INVALID_BAKE: instance-frames exceed 16384");
    for(const auto& row:instances->arr){const auto* node=row.find("node");const auto* copy=row.find("copy");need(node&&node->t==V::Num&&node->num>=1&&copy&&copy->t==V::Num&&copy->num>=0,"MOTION_INVALID_BAKE: instance requires node and copy");out.nodes.push_back((u64)node->num);out.copies.push_back((int)copy->num);out.names.push_back(row.find("name")?row.find("name")->asStr():std::string());}
    for(const auto& row:channels->arr){out.channels.push_back(row.find("name")?row.find("name")->asStr():std::string());out.units.push_back(row.find("unit")?row.find("unit")->asStr():std::string());out.starts.push_back(row.find("start")?row.find("start")->asNum():0);out.explodeViews.push_back(row.find("explode_view")?row.find("explode_view")->asStr():std::string());}
}
void fillFrames(BakedMotionFrames& out,const std::string& frames,const std::string& coordinates){
    const auto values=bytesFloats(frames,out.frameCount*out.nodes.size()*12,"frame matrices");out.coordinates=bytesFloats(coordinates,out.frameCount*out.channels.size(),"joint coordinates");
    out.poses.resize(out.frameCount*out.nodes.size());for(size_t i=0;i<out.poses.size();++i){Mat4 m;const float* v=&values[i*12];const int slots[12]={0,1,2,4,5,6,8,9,10,12,13,14};for(int e=0;e<12;++e)m.m[slots[e]]=v[e];m.m[3]=m.m[7]=m.m[11]=0;m.m[15]=1;out.poses[i]=m;}
}
std::string derivedKey(const std::string& name,const V& bake){return iterationSha256(std::string("ark.motion_frames.v1\n")+name+"\n"+bake.find("graph_digest")->str+"\n"+bake.find("source_digest")->str+"\n"+bake.find("frames_sha256")->str+"\n"+bake.find("coordinates_sha256")->str);}
}
bool bakeIsReceipt(const V& bake){return bake.t==V::Obj&&bake.find("frames_sha256")!=nullptr;}
bool bakeHasInlineFrames(const V& bake){return bake.t==V::Obj&&(bake.find("frames_f32le_base64")||bake.find("coordinates_f32le_base64"));}
bool bakedMotionFramesCached(const V& bake){if(!bakeIsReceipt(bake)||!sha256Text(bake.find("frames_sha256"))||!sha256Text(bake.find("coordinates_sha256")))return false;auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);return s.entries.count(frameKey(bake))!=0;}
bool motionReceiptsRequireDocumentVersion49(const Document& d){if(d.motions.t!=V::Arr)return false;for(const auto& row:d.motions.arr)if(const auto* bake=row.find("bake");bake&&bakeIsReceipt(*bake))return true;return false;}
void setMotionBakeInlineFixture(bool inlinePayloads){g_inlineBakeFixture=inlinePayloads;}
BakedMotionCacheStats bakedMotionCacheStats(){auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);auto out=s.stats;out.entries=s.entries.size();out.bytes=s.bytes;out.capacityBytes=kFrameCacheBytes;return out;}
void clearBakedMotionCache(){auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);s.entries.clear();s.memo.clear();s.bytes=0;}
V BakedMotionFramesSource::json() const{
    V out=V::makeObj();out.set("source",V::makeStr(source));out.set("verified",V::makeBool(verified));if(!mismatch.empty())out.set("mismatch",V::makeStr(mismatch));
    if(!staleReasons.empty()){V reasons=V::makeArr();for(const auto& r:staleReasons)reasons.arr.push_back(V::makeStr(r));out.set("stale_reasons",reasons);}return out;
}
bool motionReceiptFromInline(V& bake,bool cache){
    if(!bakeHasInlineFrames(bake))return false;BakedMotionFrames header;decodeBakeHeader(bake,header);
    const auto* f=bake.find("frames_f32le_base64");const auto* c=bake.find("coordinates_f32le_base64");need(f&&f->t==V::Str&&c&&c->t==V::Str,"MOTION_INVALID_BAKE: missing frame payloads");
    std::string frames=decodeBase64(f->str,header.frameCount*header.nodes.size()*48,"frame matrices"),coordinates=decodeBase64(c->str,header.frameCount*header.channels.size()*4,"joint coordinates");
    const std::string framesSha=iterationSha256(frames),coordinatesSha=iterationSha256(coordinates);
    bake.obj.erase(std::remove_if(bake.obj.begin(),bake.obj.end(),[](const auto& p){return p.first=="frames_f32le_base64"||p.first=="coordinates_f32le_base64";}),bake.obj.end());
    bake.set("frames_sha256",V::makeStr(framesSha));bake.set("coordinates_sha256",V::makeStr(coordinatesSha));canonical(bake);
    if(cache)cacheFrames(framesSha+coordinatesSha,std::move(frames),std::move(coordinates),"memory");return true;
}
bool resolveBakedMotion(Document& d,const V& motion,BakedMotionFrames& out,std::string& error,BakedMotionFramesSource* source,bool allowRebake){
    out={};error.clear();BakedMotionFramesSource info;
    try{
        const auto* bake=motion.find("bake");need(bake&&bake->t==V::Obj,"MOTION_NOT_BAKED: motion has no baked frames");
        if(!bakeIsReceipt(*bake)){need(decodeBakedMotion(motion,out,error),error);info.source="inline";info.verified=true;}
        else{
            decodeBakeHeader(*bake,out);std::string frames,coordinates,origin;const std::string key=frameKey(*bake);
            if(cachedFrames(key,frames,coordinates,origin)){fillFrames(out,frames,coordinates);info.source=origin;info.verified=true;}
            else{
                need(allowRebake,"MOTION_FRAMES_NOT_CACHED: baked frames are derived data and are not cached in this process");
                const auto* graph=motion.find("graph");need(graph&&graph->t==V::Obj,"MOTION_INVALID_GRAPH: motion has no graph");GraphCheck g;
                try{g=checkGraph(d,*graph);}catch(const std::exception& e){throw std::runtime_error(std::string("MOTION_FRAMES_UNAVAILABLE: baked frames are derived data, are not cached and cannot be re-baked because the graph no longer validates (")+e.what()+"); fix the graph and run motion.bake");}
                const std::string digest=motionSourceDigest(d,g.program.baseline),graphDigest=motionGraphDigest(*graph);
                if(bake->find("source_digest")->str!=digest)info.staleReasons.push_back("assembly_changed");if(bake->find("graph_digest")->str!=graphDigest)info.staleReasons.push_back("graph_changed");
                // A known mismatch at this exact source and graph is not solved again.
                const std::string memoKey=key+digest+graphDigest;V fresh;
                {auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);for(const auto& item:s.memo)if(item.first==memoKey)fresh=item.second;}
                if(!(fresh.t==V::Obj&&cachedFrames(frameKey(fresh),frames,coordinates,origin))){
                    const auto times=motionFrameTimes(g.compiled.duration,out.fps);MotionSolvedFrames solved;std::string reason;need(solveMotionFrames(d,g.compiled,g.bindings,times,solved,reason),reason);
                    BakeBytes raw;fresh=encodeBake(g.compiled,g.bindings,solved,out.fps,bake->find("source_revision")?(u64)bake->find("source_revision")->num:0,digest,graphDigest,raw);
                    fresh.obj.erase(std::remove_if(fresh.obj.begin(),fresh.obj.end(),[](const auto& p){return p.first=="frames_f32le_base64"||p.first=="coordinates_f32le_base64";}),fresh.obj.end());
                    fresh.set("frames_sha256",V::makeStr(iterationSha256(raw.frames)));fresh.set("coordinates_sha256",V::makeStr(iterationSha256(raw.coordinates)));
                    frames=raw.frames;coordinates=raw.coordinates;cacheFrames(frameKey(fresh),std::move(raw.frames),std::move(raw.coordinates),"memory");
                    auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);++s.stats.rebakes;}
                std::string mismatch;
                for(const char* field:{"frames_sha256","coordinates_sha256","frame_count","instances"}){V a=*fresh.find(field),b=*bake->find(field);canonical(a);canonical(b);if(a.dump()!=b.dump())mismatch+=(mismatch.empty()?"":", ")+std::string(field);}
                info.source="rebake";info.verified=mismatch.empty();
                if(info.verified)fillFrames(out,frames,coordinates);
                else{out={};decodeBakeHeader(fresh,out);fillFrames(out,frames,coordinates);info.mismatch="re-baked frames differ from the receipt ("+mismatch+"); they show the current assembly and graph, not the recorded bake. Run motion.bake to record them";
                    auto& s=frameStore();std::lock_guard<std::mutex> lock(s.mutex);++s.stats.rebakeMismatches;s.memo.remove_if([&](const auto& item){return item.first==memoKey;});s.memo.push_front({memoKey,fresh});while(s.memo.size()>kRebakeMemo)s.memo.pop_back();}
            }
        }
        if(source)*source=info;return true;
    }catch(const std::exception& e){error=e.what();out={};return false;}
}
bool motionDerivedPayload(const DocumentDerivedPayload& record){return record.bytes.compare(0,sizeof kDerivedMagic-1,kDerivedMagic)==0;}
void appendMotionDerivedPayloads(const Document& d,DocumentDerivedPayloads& records,size_t capacityBytes,size_t capacityEntries,MotionDerivedReport* report){
    MotionDerivedReport r;size_t used=0;
    if(d.motions.t==V::Arr)for(const auto& motion:d.motions.arr){const auto* bake=motion.find("bake");const auto* name=motion.find("name");if(!bake||!bakeIsReceipt(*bake)||!name||name->t!=V::Str)continue;
        std::string frames,coordinates,origin;if(!cachedFrames(frameKey(*bake),frames,coordinates,origin)){++r.notCached;continue;}
        V header=V::makeObj();header.set("format",V::makeStr("ark.motion_frames.v1"));header.set("name",*name);for(const char* key:{"graph_digest","source_digest","frames_sha256","coordinates_sha256"})header.set(key,*bake->find(key));
        header.set("frames_bytes",V::makeNum((double)frames.size()));header.set("coordinates_bytes",V::makeNum((double)coordinates.size()));std::string bytes=std::string(kDerivedMagic)+header.dump()+"\n"+frames+coordinates;
        // Optional like saved geometry: an entry that does not fit is omitted and re-bakes on demand.
        if(bytes.size()>maxDocumentDerivedEntryBytes||bytes.size()>capacityBytes-used||records.size()>=capacityEntries){++r.omittedEntries;r.omittedBytes+=bytes.size();continue;}
        used+=bytes.size();++r.entries;r.bytes+=bytes.size();records.push_back({derivedKey(name->str,*bake),std::move(bytes)});}
    if(report)*report=r;
}
V stageMotionDerivedPayloads(const Document& d,const DocumentDerivedPayloads& records){
    std::map<std::string,const V*> receipts;size_t staged=0,discarded=0,bytes=0;
    if(d.motions.t==V::Arr)for(const auto& motion:d.motions.arr)if(const auto* bake=motion.find("bake");bake&&bakeIsReceipt(*bake)&&motion.find("name"))receipts[derivedKey(motion.find("name")->str,*bake)]=bake;
    for(const auto& record:records){if(!motionDerivedPayload(record))continue;
        try{const auto found=receipts.find(record.key);need(found!=receipts.end(),"no current receipt");const size_t start=sizeof kDerivedMagic-1,end=record.bytes.find('\n',start);need(end!=std::string::npos,"header");
            V header;need(arkjson::parse(record.bytes.substr(start,end-start),header)&&header.t==V::Obj,"header");const auto* fb=header.find("frames_bytes");const auto* cb=header.find("coordinates_bytes");
            need(fb&&cb&&fb->t==V::Num&&cb->t==V::Num&&fb->num>=0&&cb->num>=0&&record.bytes.size()-end-1==(size_t)fb->num+(size_t)cb->num,"sizes");
            std::string frames=record.bytes.substr(end+1,(size_t)fb->num),coordinates=record.bytes.substr(end+1+(size_t)fb->num);const V& bake=*found->second;
            need(iterationSha256(frames)==bake.find("frames_sha256")->str&&iterationSha256(coordinates)==bake.find("coordinates_sha256")->str,"digest");
            BakedMotionFrames checked;decodeBakeHeader(bake,checked);fillFrames(checked,frames,coordinates);
            bytes+=record.bytes.size();cacheFrames(frameKey(bake),std::move(frames),std::move(coordinates),"derived");++staged;}
        catch(const std::exception&){++discarded;}}
    V out=V::makeObj();out.set("entries_staged",V::makeNum((double)staged));out.set("entries_discarded",V::makeNum((double)discarded));out.set("bytes_staged",V::makeNum((double)bytes));out.set("validation",V::makeStr("frames_sha256 and coordinates_sha256 of the current receipts"));return out;
}
