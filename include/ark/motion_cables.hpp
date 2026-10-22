#pragma once
#include "ark/motion_graph.hpp"
#include <atomic>
#include <memory>
namespace ark {
// Derived display meshes from native cable sweeps at the recorded rigid frame
// poses. These are not authored geometry, continuous collision or fatigue evidence.
class MotionCableMesh {
    std::string bytes_;size_t positions_=0,indices_=0;bool compressed_=false;
public:
    explicit MotionCableMesh(const Mesh&);
    Mesh mesh() const;
    size_t bytes() const{return sizeof(*this)+bytes_.capacity()+1;}
};
struct MotionCableDisplay {
    std::vector<u64> features;
    std::vector<std::shared_ptr<const MotionCableMesh>> meshes; // frame-major
    size_t frameCount=0,bytes=0,nativeRequests=0;
};
std::vector<u64> motionCableFeatures(const Document&,const BakedMotionFrames&);
MotionCableDisplay prepareMotionCableDisplay(Document&,const BakedMotionFrames&,
    const std::atomic_bool& cancelled,const std::function<void(size_t,size_t)>& progress={});
}
