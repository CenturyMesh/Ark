#include "ark/geometry_storage_032d.hpp"
#include "ark/native_backend.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <atomic>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
size_t allocation(const V& v){size_t n=sizeof(V)+v.str.capacity()+1+v.arr.capacity()*sizeof(V)+v.obj.capacity()*sizeof(std::pair<std::string,V>);for(const auto& x:v.arr)n+=allocation(x)-sizeof(V);for(const auto& x:v.obj)n+=x.first.capacity()+1+allocation(x.second)-sizeof(V);return n;}
const V& empty(){static const V v;return v;}
V* member(V& v,const char* name){for(auto& p:v.obj)if(p.first==name)return &p.second;return nullptr;}
}
struct NativePayload::Storage {
    V metadata;
    std::vector<double> positions;
    std::vector<u32> indices;
    bool packed=false;
    mutable std::once_flag expandOnce;
    mutable V expanded;
    mutable std::atomic<size_t> expandedAllocation{0};
    explicit Storage(V value):metadata(std::move(value)){
        auto* mesh=member(metadata,"mesh");if(!mesh||mesh->t!=V::Obj)return;
        // A manually constructed Value may contain duplicate object keys. Keep
        // those inputs verbatim; compaction must not normalize malformed data.
        for(size_t i=0;i<mesh->obj.size();++i)for(size_t j=0;j<i;++j)if(mesh->obj[i].first==mesh->obj[j].first)return;
        const auto* pos=mesh->find("pos"),*idx=mesh->find("idx");
        if(!pos||!idx||pos->t!=V::Arr||idx->t!=V::Arr||pos->arr.size()%3||idx->arr.size()%3||pos->arr.size()>6000000||idx->arr.size()>6000000)return;
        for(const auto& n:pos->arr)if(n.t!=V::Num||!std::isfinite(n.num))return;
        for(const auto& n:idx->arr)if(n.t!=V::Num||!std::isfinite(n.num)||n.num<0||n.num>4294967295.||n.num!=std::floor(n.num))return;
        positions.reserve(pos->arr.size());for(const auto& n:pos->arr)positions.push_back(n.num);
        indices.reserve(idx->arr.size());for(const auto& n:idx->arr)indices.push_back((u32)n.num);
        // Keep original field order so a lossless expansion preserves JSON too.
        for(auto& field:mesh->obj)if(field.first=="pos"||field.first=="idx")field.second=V{};
        packed=true;
    }
    V expand() const{
        V next=metadata;if(!packed)return next;auto* mesh=member(next,"mesh");V pos=V::makeArr(),idx=V::makeArr();pos.arr.reserve(positions.size());idx.arr.reserve(indices.size());for(double n:positions)pos.arr.push_back(V::makeNum(n));for(u32 n:indices)idx.arr.push_back(V::makeNum(n));*member(*mesh,"pos")=std::move(pos);*member(*mesh,"idx")=std::move(idx);return next;
    }
    const V& full() const{
        if(!packed)return metadata;
        std::call_once(expandOnce,[&]{V next=expand();const auto bytes=allocation(next);expanded=std::move(next);expandedAllocation.store(bytes,std::memory_order_release);});
        return expanded;
    }
};
NativePayload::NativePayload(V value):storage_(std::make_shared<const Storage>(std::move(value))){}
NativePayload& NativePayload::operator=(V value){storage_=std::make_shared<const Storage>(std::move(value));return *this;}
const V& NativePayload::metadata() const{return storage_?storage_->metadata:empty();}
const V& NativePayload::value() const{return storage_?storage_->full():empty();}
const V* NativePayload::meshMetadata() const{return metadata().find("mesh");}
const V* NativePayload::find(const char* name) const{return std::strcmp(name,"mesh")==0?value().find(name):metadata().find(name);}
void NativePayload::set(const std::string& name,V value){V next=storage_?storage_->expand():V{};next.set(name,std::move(value));*this=std::move(next);}
void NativePayload::mutate(const std::function<void(V&)>& edit){V next=storage_?storage_->expand():V{};edit(next);*this=std::move(next);}
size_t NativePayload::allocatedBytes() const{return storage_?sizeof(Storage)+64+allocation(storage_->metadata)+storage_->positions.capacity()*sizeof(double)+storage_->indices.capacity()*sizeof(u32)+storage_->expandedAllocation.load(std::memory_order_acquire):0;}
size_t NativePayload::maximumRetainedBytes() const{return storage_?sizeof(Storage)+64+2*allocation(storage_->metadata)+storage_->positions.capacity()*sizeof(double)+storage_->indices.capacity()*sizeof(u32)+(storage_->packed?(storage_->positions.size()+storage_->indices.size())*(sizeof(V)+32):0):0;}
bool NativePayload::packedMesh() const{return storage_&&storage_->packed;}
Mesh NativePayload::displayMesh() const{
    if(!packedMesh())return nativeResultMesh(value());
    if(storage_->positions.empty()||storage_->indices.empty())throw std::runtime_error("Invalid native mesh buffers");
    Mesh mesh;mesh.pos.reserve(storage_->positions.size());mesh.idx=storage_->indices;
    for(double value:storage_->positions){if(std::abs(value)>1e8)throw std::runtime_error("Invalid native mesh coordinate");mesh.pos.push_back((f32)value);}
    for(u32 value:mesh.idx)if(value>=mesh.vertCount())throw std::runtime_error("Invalid native mesh index");
    mesh.computeNormals();return mesh;
}
}
