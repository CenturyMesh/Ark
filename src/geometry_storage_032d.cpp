#include "ark/geometry_storage_032d.hpp"
#include "ark/native_backend.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <atomic>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
// Request the optional API declarations locally. The product's minimum Windows
// target is unchanged: every symbol is still resolved dynamically below.
#pragma push_macro("NTDDI_VERSION")
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06020000
#include <compressapi.h>
#pragma pop_macro("NTDDI_VERSION")
#endif

namespace ark {
namespace {
using V=arkjson::Value;
size_t allocation(const V& v){size_t n=sizeof(V)+v.str.capacity()+1+v.arr.capacity()*sizeof(V)+v.obj.capacity()*sizeof(std::pair<std::string,V>);for(const auto& x:v.arr)n+=allocation(x)-sizeof(V);for(const auto& x:v.obj)n+=x.first.capacity()+1+allocation(x.second)-sizeof(V);return n;}
const V& empty(){static const V v;return v;}
V* member(V& v,const char* name){for(auto& p:v.obj)if(p.first==name)return &p.second;return nullptr;}
#ifdef _WIN32
struct MetadataCodec {
    // Windows documents both source buffers as const. Some MinGW headers declare
    // PVOID instead; use the documented ABI for these dynamically loaded symbols
    // without redeclaring imported functions or casting away source constness.
    using Transform = BOOL (WINAPI *)(COMPRESSOR_HANDLE,LPCVOID,SIZE_T,PVOID,SIZE_T,PSIZE_T);
    HMODULE module=LoadLibraryExW(L"cabinet.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    decltype(&CreateCompressor) create=module?reinterpret_cast<decltype(create)>(GetProcAddress(module,"CreateCompressor")):nullptr;
    Transform compress=module?reinterpret_cast<decltype(compress)>(GetProcAddress(module,"Compress")):nullptr;
    decltype(&CloseCompressor) close=module?reinterpret_cast<decltype(close)>(GetProcAddress(module,"CloseCompressor")):nullptr;
    decltype(&CreateDecompressor) createDecoder=module?reinterpret_cast<decltype(createDecoder)>(GetProcAddress(module,"CreateDecompressor")):nullptr;
    Transform decompress=module?reinterpret_cast<decltype(decompress)>(GetProcAddress(module,"Decompress")):nullptr;
    decltype(&CloseDecompressor) closeDecoder=module?reinterpret_cast<decltype(closeDecoder)>(GetProcAddress(module,"CloseDecompressor")):nullptr;
    ~MetadataCodec(){if(module)FreeLibrary(module);}
    bool available() const{return create&&compress&&close&&createDecoder&&decompress&&closeDecoder;}
};
MetadataCodec& metadataCodec(){static MetadataCodec codec;return codec;}
std::string decodeMetadata(const std::string& bytes,size_t original){
    auto& codec=metadataCodec();DECOMPRESSOR_HANDLE handle=nullptr;if(!codec.available()||!codec.createDecoder(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle))throw std::runtime_error("Retained metadata decompressor unavailable");
    struct Close {MetadataCodec& c;DECOMPRESSOR_HANDLE h;~Close(){c.closeDecoder(h);}} close{codec,handle};
    std::string out(original,'\0');SIZE_T actual=0;if(!codec.decompress(handle,bytes.data(),bytes.size(),out.data(),out.size(),&actual)||actual!=original)throw std::runtime_error("Retained metadata decompression failed");return out;
}
std::string encodeMetadata(const std::string& source){
    if(source.size()<1024)return {};auto& codec=metadataCodec();COMPRESSOR_HANDLE handle=nullptr;if(!codec.available()||!codec.create(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle))return {};
    struct Close {MetadataCodec& c;COMPRESSOR_HANDLE h;~Close(){c.close(h);}} close{codec,handle};SIZE_T size=0;codec.compress(handle,source.data(),source.size(),nullptr,0,&size);
    if(!size)return {};std::string out(size,'\0');SIZE_T actual=0;if(!codec.compress(handle,source.data(),source.size(),out.data(),out.size(),&actual)||actual>=source.size())return {};out.resize(actual);
    if(decodeMetadata(out,source.size())!=source)throw std::runtime_error("Retained metadata compression changed bytes");return std::string(out.data(),out.size());
}
#else
std::string encodeMetadata(const std::string&){return {};}
std::string decodeMetadata(const std::string&,size_t){throw std::runtime_error("Unsupported retained metadata compression");}
#endif
}
struct NativePayload::Storage {
    V metadata;
    std::string encodedMetadata;
    size_t metadataOriginalBytes=0;
    mutable std::once_flag metadataOnce;
    mutable V decodedMetadata;
    mutable std::atomic<size_t> decodedAllocation{0};
    std::vector<double> positions;
    std::vector<u32> indices;
    bool packed=false;
    mutable std::once_flag expandOnce;
    mutable V expanded;
    mutable std::atomic<size_t> expandedAllocation{0};
    Storage(const Storage& source,std::string encoded,size_t original):encodedMetadata(encoded.data(),encoded.size()),metadataOriginalBytes(original),positions(source.positions),indices(source.indices),packed(source.packed){}
    const V& meta() const{
        if(encodedMetadata.empty())return metadata;
        std::call_once(metadataOnce,[&]{V next;const auto text=metadataOriginalBytes?decodeMetadata(encodedMetadata,metadataOriginalBytes):encodedMetadata;if(!arkjson::parse(text,next))throw std::runtime_error("Retained native metadata decode failed");const auto bytes=allocation(next);decodedMetadata=std::move(next);decodedAllocation.store(bytes,std::memory_order_release);});return decodedMetadata;
    }
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
        V next=meta();if(!packed)return next;auto* mesh=member(next,"mesh");V pos=V::makeArr(),idx=V::makeArr();pos.arr.reserve(positions.size());idx.arr.reserve(indices.size());for(double n:positions)pos.arr.push_back(V::makeNum(n));for(u32 n:indices)idx.arr.push_back(V::makeNum(n));*member(*mesh,"pos")=std::move(pos);*member(*mesh,"idx")=std::move(idx);return next;
    }
    const V& full() const{
        if(!packed)return meta();
        std::call_once(expandOnce,[&]{V next=expand();const auto bytes=allocation(next);expanded=std::move(next);expandedAllocation.store(bytes,std::memory_order_release);});
        return expanded;
    }
};
NativePayload::NativePayload(V value):storage_(std::make_shared<const Storage>(std::move(value))){}
NativePayload& NativePayload::operator=(V value){storage_=std::make_shared<const Storage>(std::move(value));return *this;}
const V& NativePayload::metadata() const{return storage_?storage_->meta():empty();}
const V& NativePayload::value() const{return storage_?storage_->full():empty();}
const V* NativePayload::meshMetadata() const{return metadata().find("mesh");}
const V* NativePayload::find(const char* name) const{return std::strcmp(name,"mesh")==0?value().find(name):metadata().find(name);}
void NativePayload::set(const std::string& name,V value){V next=storage_?storage_->expand():V{};next.set(name,std::move(value));*this=std::move(next);}
void NativePayload::mutate(const std::function<void(V&)>& edit){V next=storage_?storage_->expand():V{};edit(next);*this=std::move(next);}
size_t NativePayload::allocatedBytes() const{return storage_?sizeof(Storage)+64+allocation(storage_->metadata)+storage_->encodedMetadata.capacity()+1+storage_->decodedAllocation.load(std::memory_order_acquire)+storage_->positions.capacity()*sizeof(double)+storage_->indices.capacity()*sizeof(u32)+storage_->expandedAllocation.load(std::memory_order_acquire):0;}
NativePayload NativePayload::compacted() const{
    if(!storage_)return *this;
    if(!storage_->encodedMetadata.empty()&&!storage_->decodedAllocation.load(std::memory_order_acquire)&&!storage_->expandedAllocation.load(std::memory_order_acquire))return *this;
    auto encoded=storage_->meta().dump();V verified;
    // Only a byte-identical metadata roundtrip may replace the parsed tree.
    // Duplicate keys, unsupported nesting or numeric changes keep the source.
    if(!arkjson::parse(encoded,verified)||verified.dump()!=encoded)return *this;
    size_t original=0;auto compressed=encodeMetadata(encoded);if(!compressed.empty()){original=encoded.size();encoded=std::move(compressed);}NativePayload result;result.storage_=std::make_shared<const Storage>(*storage_,std::move(encoded),original);return result;
}
size_t NativePayload::maximumRetainedBytes() const{return storage_?sizeof(Storage)+64+storage_->encodedMetadata.capacity()+1+2*allocation(storage_->meta())+storage_->positions.capacity()*sizeof(double)+storage_->indices.capacity()*sizeof(u32)+(storage_->packed?(storage_->positions.size()+storage_->indices.size())*(sizeof(V)+32):0):0;}
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
