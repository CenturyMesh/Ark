#pragma once
#include "ark/json.hpp"
#include "ark/mesh.hpp"
#include <functional>
#include <memory>

namespace ark {
// Process-local lossless derived caches. Empty compressed output means that the
// optional codec is unavailable or gives no saving; callers retain raw bytes.
std::string compressDerivedBytes(const std::string&);
std::string expandDerivedBytes(const std::string&,size_t originalBytes);
// Value semantics over immutable derived geometry. Copies share storage; edits
// publish a separate value and cannot mutate an evaluated cache owner.
class NativePayload {
    struct Storage;
    std::shared_ptr<const Storage> storage_;
public:
    NativePayload()=default;
    NativePayload(arkjson::Value value);
    NativePayload& operator=(arkjson::Value value);
    const arkjson::Value& value() const;
    operator const arkjson::Value&() const{return value();}
    const arkjson::Value& metadata() const;
    const arkjson::Value* meshMetadata() const;
    const arkjson::Value* find(const char* name) const;
    bool isNull() const{return metadata().isNull();}
    std::string dump() const{return value().dump();}
    void set(const std::string&,arkjson::Value);
    void mutate(const std::function<void(arkjson::Value&)>&);
    const void* ownerIdentity() const{return storage_.get();}
    size_t allocatedBytes() const;
    size_t maximumRetainedBytes() const;
    // Same immutable values and packed channels, without a lazily expanded JSON
    // cache. The source owner is never mutated or stripped of validated data.
    NativePayload compacted() const;
    bool packedMesh() const;
    Mesh displayMesh() const;
};

class MeshSnapshot {
    std::shared_ptr<const Mesh> mesh_;
public:
    MeshSnapshot()=default;
    MeshSnapshot(std::unique_ptr<Mesh> mesh){*this=std::move(mesh);}
    MeshSnapshot& operator=(std::unique_ptr<Mesh> mesh){mesh_=mesh?std::make_shared<const Mesh>(std::move(*mesh)):nullptr;return *this;}
    explicit operator bool() const{return bool(mesh_);}
    bool operator==(std::nullptr_t) const{return !mesh_;}
    bool operator!=(std::nullptr_t) const{return bool(mesh_);}
    const Mesh* get() const{return mesh_.get();}
    const Mesh* operator->() const{return mesh_.get();}
    const Mesh& operator*() const{return *mesh_;}
    void reset(){mesh_.reset();}
    void mutate(const std::function<void(Mesh&)>& edit){Mesh next=mesh_?*mesh_:Mesh{};edit(next);mesh_=std::make_shared<const Mesh>(std::move(next));}
};
}
