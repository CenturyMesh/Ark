// Process-local parsed results supplement the authenticated on-disk cache.
// A hit is attempted ONLY after the entire current file passes its SHA-256
// check. No path/mtime shortcut or cached success masks corruption/removal.
struct NativeDecodedCache070 {
    struct Entry {V value;std::string payloadDigest;size_t bytes=0;unsigned long long used=0;};
    std::mutex mutex;std::map<std::string,Entry> entries;
    size_t bytes=0;unsigned long long clock=0,hits=0,misses=0,evictions=0,oversized=0;
    static constexpr size_t maxEntries=128;
    static NativeDecodedCache070& instance(){static NativeDecodedCache070 cache;return cache;}
    static size_t capacity(){
        if(const char* enabled=std::getenv("ARK_NATIVE_RESULT_CACHE");enabled&&std::string(enabled)=="0")return 0;
        size_t megabytes=256;
        if(const char* value=std::getenv("ARK_NATIVE_MEMORY_CACHE_MAX_MB")){char* end=nullptr;const long long requested=std::strtoll(value,&end,10);if(end!=value&&!*end&&requested>=0&&requested<=1024)megabytes=(size_t)requested;}
        return megabytes*1024u*1024u;
    }
    // Conservative owned allocation accounting: includes inline values plus
    // vector capacities and string capacity (even SSO bytes already inline).
    // Allocator bookkeeping and the caller's returned working copy are separate.
    static size_t allocated(const V& value){
        size_t total=sizeof(V)+value.str.capacity()+1;
        total+=value.arr.capacity()*sizeof(V)+value.obj.capacity()*sizeof(std::pair<std::string,V>);
        for(const auto& child:value.arr)total+=allocated(child)-sizeof(V);
        for(const auto& field:value.obj)total+=field.first.capacity()+1+allocated(field.second)-sizeof(V);
        return total;
    }
    void trim(size_t limit,size_t count=maxEntries){
        while(!entries.empty()&&(bytes>limit||entries.size()>count)){
            auto oldest=std::min_element(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});bytes-=oldest->second.bytes;entries.erase(oldest);++evictions;
        }
    }
    static bool lookup(const fs::path& path,const std::string& digest,V& result){
        auto& cache=instance();std::lock_guard<std::mutex> lock(cache.mutex);const auto limit=capacity();cache.trim(limit);
        if(!limit)return false;const auto key=utf8(path);const auto found=cache.entries.find(key);
        if(found==cache.entries.end()){++cache.misses;return false;}
        if(found->second.payloadDigest!=digest){cache.bytes-=found->second.bytes;cache.entries.erase(found);++cache.evictions;++cache.misses;return false;}
        // Copies preserve independent callers: edits to a returned result never
        // change the immutable cached evidence seen by another native request.
        result=found->second.value;found->second.used=++cache.clock;++cache.hits;return true;
    }
    static void retain(const fs::path& path,const std::string& digest,const V& result){
        const auto limit=capacity();if(!limit)return;
        auto& cache=instance();const auto key=utf8(path);size_t estimate=allocated(result)+key.size()+digest.size()+sizeof(Entry)+256;
        if(estimate>limit){std::lock_guard<std::mutex> lock(cache.mutex);++cache.oversized;return;}
        Entry entry;entry.value=result;entry.payloadDigest=digest;
        entry.bytes=allocated(entry.value)+key.capacity()+entry.payloadDigest.capacity()+sizeof(Entry)+256;
        std::lock_guard<std::mutex> lock(cache.mutex);const auto currentLimit=capacity();cache.trim(currentLimit);
        if(!currentLimit||entry.bytes>currentLimit){++cache.oversized;return;}
        if(const auto old=cache.entries.find(key);old!=cache.entries.end()){cache.bytes-=old->second.bytes;cache.entries.erase(old);}
        cache.trim(currentLimit-entry.bytes,maxEntries-1);entry.used=++cache.clock;cache.bytes+=entry.bytes;cache.entries.emplace(key,std::move(entry));
    }
    static V info(){
        auto& cache=instance();std::lock_guard<std::mutex> lock(cache.mutex);const auto limit=capacity();cache.trim(limit);V out=V::makeObj();
        out.set("capacity_bytes",V::makeNum((double)limit));out.set("accounted_retained_bytes",V::makeNum((double)cache.bytes));out.set("entries",V::makeNum((double)cache.entries.size()));out.set("entry_limit",V::makeNum((double)maxEntries));
        out.set("hits",V::makeNum((double)cache.hits));out.set("misses",V::makeNum((double)cache.misses));out.set("evictions",V::makeNum((double)cache.evictions));out.set("oversized_skipped",V::makeNum((double)cache.oversized));
        out.set("authority",V::makeStr("parsed exact local disk-cache results; complete disk SHA256 verified on every hit"));out.set("accounting",V::makeStr("retained JSON values, vector/string capacities and conservative entry allowance; excludes allocator overhead, transient parse and caller-owned working results"));return out;
    }
};
