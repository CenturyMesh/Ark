// Included after ResidentWorker. A lease owns one isolated process until its
// response is consumed. Idle processes migrate between calling threads.
class ResidentPool078B {
    struct Slot {std::unique_ptr<ResidentWorker> worker;bool busy=false;std::uint64_t used=0;std::chrono::steady_clock::time_point released=std::chrono::steady_clock::now();};
    std::mutex mutex;std::vector<std::unique_ptr<Slot>> slots;std::uint64_t clock=0,reuses=0,evictions=0;size_t peak=0;
    static size_t retention(const ResourcePolicy071D& policy){return policy.memoryKnown&&policy.availableHeadroomBytes<256ull*1024*1024?0:policy.featureWorkers;}
    void trim(size_t limit,std::vector<std::unique_ptr<Slot>>& garbage){
        const auto now=std::chrono::steady_clock::now();
        for(auto it=slots.begin();it!=slots.end();)if(!(*it)->busy&&(!(*it)->worker||!(*it)->worker->alive()||now-(*it)->released>=std::chrono::seconds(20))){garbage.push_back(std::move(*it));it=slots.erase(it);++evictions;}else ++it;
        while(slots.size()>limit){auto oldest=slots.end();for(auto it=slots.begin();it!=slots.end();++it)if(!(*it)->busy&&(oldest==slots.end()||(*it)->used<(*oldest)->used))oldest=it;if(oldest==slots.end())break;garbage.push_back(std::move(*oldest));slots.erase(oldest);++evictions;}
    }
    void release(Slot* slot,bool healthy) noexcept {try{
        std::vector<std::unique_ptr<Slot>> garbage;const auto policy=currentResourcePolicy071D();
        {std::lock_guard lock(mutex);slot->busy=false;slot->used=++clock;slot->released=std::chrono::steady_clock::now();if(!healthy){for(auto it=slots.begin();it!=slots.end();++it)if(it->get()==slot){garbage.push_back(std::move(*it));slots.erase(it);++evictions;break;}}trim(retention(policy),garbage);}
        // Job termination/file cleanup cannot block unrelated pool bookkeeping.
    }catch(...){std::unique_ptr<ResidentWorker> failed;{std::lock_guard lock(mutex);for(auto& current:slots)if(current.get()==slot){current->busy=false;if(!healthy)failed=std::move(current->worker);break;}}}}
public:
    V call(const fs::path& worker,const std::string& identity,const V& request,const ResourcePolicy071D& policy){
        Slot* selected=nullptr;bool create=false;std::vector<std::unique_ptr<Slot>> garbage;
        {std::lock_guard lock(mutex);trim(std::max<size_t>(1,policy.featureWorkers),garbage);
            for(auto& slot:slots)if(!slot->busy&&slot->worker->matches(worker,identity)){selected=slot.get();++reuses;break;}
            if(!selected){
                // Admission already bounds simultaneous calls. Evict an idle
                // different producer rather than retaining a process per thread.
                trim(std::max<size_t>(1,policy.featureWorkers)-1,garbage);
                auto slot=std::make_unique<Slot>();selected=slot.get();slots.push_back(std::move(slot));create=true;
            }selected->busy=true;
        }
        bool healthy=false;
        struct Return {ResidentPool078B& pool;Slot* slot;bool& healthy;~Return(){pool.release(slot,healthy);}} returned{*this,selected,healthy};
        garbage.clear();
        if(create){auto process=std::make_unique<ResidentWorker>(worker,identity);std::lock_guard lock(mutex);selected->worker=std::move(process);++nativeStarts;++totalNativeStarts;size_t live=0;for(const auto& slot:slots)live+=bool(slot->worker);peak=std::max(peak,live);}
        auto response=selected->worker->call(request);healthy=true;return response;
    }
    V info(){std::vector<std::unique_ptr<Slot>> garbage;const auto policy=currentResourcePolicy071D();V value=V::makeObj();
        {std::lock_guard lock(mutex);trim(retention(policy),garbage);size_t busy=0,live=0;for(const auto& slot:slots){busy+=slot->busy;live+=bool(slot->worker);}
            value.set("resident_processes",V::makeNum(live));value.set("busy_processes",V::makeNum(busy));value.set("retention_limit",V::makeNum(retention(policy)));value.set("peak_resident_processes",V::makeNum(peak));value.set("cross_request_reuses",V::makeNum(reuses));value.set("evictions",V::makeNum(evictions));}
        value.set("host_idle_recycle_seconds",V::makeNum(20));value.set("lifetime",V::makeStr("exclusive per-request leases; host idle reuse margin20s before native30s expiry; RAM-pressure idle eviction; process jobs terminate on host shutdown"));return value;
    }
};
// Empty ownership is established before main. The lazily created ThreadPool
// therefore drains first at shutdown, even if its last jobs first use native
// services while main is returning. No process is started by this initializer.
static const auto residentPoolStorage078=std::make_shared<ResidentPool078B>();
static std::shared_ptr<ResidentPool078B> residentPool078(){return residentPoolStorage078;}
