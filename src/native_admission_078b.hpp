#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace ark {
// Shared by native callers, not by feature graphs. Waiting never holds a CAD
// document lock here and cancellation predicates execute outside this mutex.
class NativeAdmission078B {
    struct Active {unsigned threads,cpuLimit,workerLimit;};
    std::mutex mutex_;std::condition_variable changed_;
    std::deque<std::uint64_t> queue_;std::map<std::uint64_t,Active> active_;
    std::uint64_t next_=0,waited_=0,cancelled_=0;unsigned threads_=0,peakThreads_=0,peakWorkers_=0;
    void release(std::uint64_t ticket) noexcept {
        std::lock_guard lock(mutex_);auto found=active_.find(ticket);if(found==active_.end())return;
        threads_-=found->second.threads;active_.erase(found);changed_.notify_all();
    }
public:
    struct Stats {unsigned activeThreads,activeWorkers,peakThreads,peakWorkers;std::size_t waiting;std::uint64_t waited,cancelled;};
    class Lease {
        NativeAdmission078B* owner_=nullptr;std::uint64_t ticket_=0;unsigned threads_=0;
        friend class NativeAdmission078B;
        Lease(NativeAdmission078B* owner,std::uint64_t ticket,unsigned threads):owner_(owner),ticket_(ticket),threads_(threads){}
    public:
        Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept:owner_(std::exchange(other.owner_,nullptr)),ticket_(other.ticket_),threads_(other.threads_){}
        ~Lease(){if(owner_)owner_->release(ticket_);}
        unsigned threads() const{return threads_;}
    };
    Lease acquire(unsigned requested,unsigned cpuLimit,unsigned workerLimit,const std::function<bool()>& cancelled){
        cpuLimit=std::max(1u,cpuLimit);workerLimit=std::max(1u,workerLimit);requested=std::clamp(requested,1u,cpuLimit);
        std::unique_lock lock(mutex_);const auto ticket=++next_;queue_.push_back(ticket);bool counted=false;
        auto remove=[&]{auto found=std::find(queue_.begin(),queue_.end(),ticket);if(found!=queue_.end())queue_.erase(found);changed_.notify_all();};
        try {for(;;){
            lock.unlock();const bool stop=cancelled&&cancelled();lock.lock();
            if(stop){++cancelled_;throw std::runtime_error("NATIVE_CANCELLED: native request cancelled while waiting for shared CPU/RAM admission");}
            unsigned allowedCpu=cpuLimit,allowedWorkers=workerLimit;
            for(const auto& item:active_){allowedCpu=std::min(allowedCpu,item.second.cpuLimit);allowedWorkers=std::min(allowedWorkers,item.second.workerLimit);}
            if(queue_.front()==ticket&&active_.size()<allowedWorkers&&threads_<=allowedCpu&&requested<=allowedCpu-threads_){
                active_.emplace(ticket,Active{requested,cpuLimit,workerLimit});queue_.pop_front();threads_+=requested;
                peakThreads_=std::max(peakThreads_,threads_);peakWorkers_=std::max(peakWorkers_,static_cast<unsigned>(active_.size()));changed_.notify_all();return Lease(this,ticket,requested);
            }
            if(!counted){counted=true;++waited_;}changed_.wait_for(lock,std::chrono::milliseconds(25));
        }}catch(...){if(!lock.owns_lock())lock.lock();remove();throw;}
    }
    Stats stats(){std::lock_guard lock(mutex_);return {threads_,static_cast<unsigned>(active_.size()),peakThreads_,peakWorkers_,queue_.size(),waited_,cancelled_};}
};
}
