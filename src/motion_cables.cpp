#include "ark/motion_cables.hpp"
#include "ark/modeling_031b.hpp"
#include "ark/electronics.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
namespace ark {
using V=arkjson::Value;
MotionCableMesh::MotionCableMesh(const Mesh& mesh):positions_(mesh.pos.size()),indices_(mesh.idx.size()){
    std::string raw(positions_*sizeof(float)+indices_*sizeof(u32),'\0');std::memcpy(raw.data(),mesh.pos.data(),positions_*sizeof(float));std::memcpy(raw.data()+positions_*sizeof(float),mesh.idx.data(),indices_*sizeof(u32));
    bytes_=compressDerivedBytes(raw);compressed_=!bytes_.empty();if(!compressed_)bytes_=std::move(raw);
    // Normals are the same deterministic computation used by nativeResultMesh.
    const auto restored=this->mesh();if(restored.pos!=mesh.pos||restored.idx!=mesh.idx||restored.nrm!=mesh.nrm)throw std::runtime_error("MOTION_CABLE_CACHE_INVALID: display roundtrip changed geometry");
}
Mesh MotionCableMesh::mesh() const{
    const auto size=positions_*sizeof(float)+indices_*sizeof(u32);const auto raw=compressed_?expandDerivedBytes(bytes_,size):bytes_;if(raw.size()!=size)throw std::runtime_error("MOTION_CABLE_CACHE_INVALID: incomplete display cache");
    Mesh mesh;mesh.pos.resize(positions_);mesh.idx.resize(indices_);std::memcpy(mesh.pos.data(),raw.data(),positions_*sizeof(float));std::memcpy(mesh.idx.data(),raw.data()+positions_*sizeof(float),indices_*sizeof(u32));mesh.computeNormals();return mesh;
}
namespace {
bool dependsOnMoving(const Document& d,const V& value,const std::set<u64>& moving){
    if(value.t==V::Obj){if(const auto* n=value.find("node")){if(n->t==V::Num&&moving.count((u64)n->num))return true;if(n->t==V::Str)for(const auto& node:d.assembly)if(node.name==n->str&&moving.count(node.id))return true;}for(const auto& kv:value.obj)if(dependsOnMoving(d,kv.second,moving))return true;}
    for(const auto& child:value.arr)if(dependsOnMoving(d,child,moving))return true;return false;
}
}
std::vector<u64> motionCableFeatures(const Document& d,const BakedMotionFrames& frames){
    const std::set<u64> moving(frames.nodes.begin(),frames.nodes.end());std::vector<u64> ids;
    for(const auto& f:d.features)if(f.type==FeatureType::CableRoute&&!f.suppressed&&dependsOnMoving(d,f.nativeSpec,moving))ids.push_back(f.id);return ids;
}
MotionCableDisplay prepareMotionCableDisplay(Document& d,const BakedMotionFrames& frames,const std::atomic_bool& cancelled,const std::function<void(size_t,size_t)>& progress){
    MotionCableDisplay result;result.features=motionCableFeatures(d,frames);result.frameCount=frames.frameCount;if(result.features.empty())return result;
    if(!frames.frameCount||frames.poses.size()!=frames.nodes.size()*frames.frameCount||frames.copies.size()!=frames.nodes.size())throw std::runtime_error("MOTION_CABLE_FRAMES_INVALID: incomplete rigid pose frames");
    if(result.features.size()>32||frames.frameCount>32768/result.features.size())throw std::runtime_error("MOTION_CABLE_CAPACITY: at most 32 flexible cables and 32768 cable frames; reduce motion fps or duration");
    std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))throw std::runtime_error(error);
    std::vector<size_t> mapping;for(size_t i=0;i<frames.nodes.size();++i){auto found=std::find_if(placements.begin(),placements.end(),[&](const auto& p){return p.node==frames.nodes[i]&&p.copy==frames.copies[i];});if(found==placements.end())throw std::runtime_error("MOTION_CABLE_STALE: a recorded occurrence no longer exists");mapping.push_back((size_t)(found-placements.begin()));}
    std::map<std::string,std::shared_ptr<const MotionCableMesh>> unique;const auto env=d.buildEnv();const size_t total=frames.frameCount*result.features.size();result.meshes.reserve(total);
    NativeRequestControlScope control([&]{return cancelled.load();},120000);
    for(size_t frame=0;frame<frames.frameCount;++frame){
        for(size_t i=0;i<mapping.size();++i)placements[mapping[i]].world=frames.poses[frame*frames.nodes.size()+i];
        for(u64 id:result.features){
            if(cancelled.load())throw std::runtime_error("MOTION_CABLE_CANCELLED");
            try{const auto* source=d.find(id);if(!source)throw std::runtime_error("Missing cable definition");Feature bound=*source;bound.nativeSpec=resolveCableBindings031B(*source,env,d,&placements);V request=V::makeObj();request.set("operation",V::makeStr("model_electronics"));request.set("request",electronicsRequest(bound,env));const auto key=iterationSha256(request.dump());auto found=unique.find(key);
                if(found==unique.end()){const auto native=nativeWorkerRequest(request);const auto display=nativeResultMesh(native);if(display.idx.empty())throw std::runtime_error("Native sweep returned no display geometry");auto mesh=std::make_shared<MotionCableMesh>(display);const size_t bytes=mesh->bytes();if(bytes>512u*1024u*1024u-result.bytes)throw std::runtime_error("MOTION_CABLE_CAPACITY: native display cache exceeds 512 MiB; reduce fps or duration");result.bytes+=bytes;++result.nativeRequests;found=unique.emplace(key,mesh).first;}
                result.meshes.push_back(found->second);if(progress)progress(result.meshes.size(),total);
            }catch(const std::exception& e){throw std::runtime_error("Cable #"+std::to_string(id)+", frame "+std::to_string(frame)+" ("+std::to_string(frame/frames.fps)+" s): "+e.what());}
        }
    }
    return result;
}
}
