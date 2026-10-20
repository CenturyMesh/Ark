#include "ark/snapshot_032d.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/media_044b.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace ark {
namespace {
SnapshotMaterial displayMaterial(const Appearance& a){
    SnapshotMaterial out;out.color=a.baseColor;out.opacity=a.opacity;out.emission=a.emissionColor;
    out.emissionIntensity=a.emissionIntensity;out.bloom=a.bloom;out.glow=a.glow;return out;
}
}
static std::vector<SnapshotMaterial> baseSnapshotMaterials(const Document& document,const Body& body,const Mesh& mesh){
    const auto* feature=document.find(body.id);
    std::vector<SnapshotMaterial> out(mesh.triCount(),displayMaterial(feature?resolvedAppearance(document,*feature):Appearance{}));
    if(!feature||feature->faceAppearances.empty())return out;
    const auto* nativeMesh=body.nativeData.meshMetadata();
    const auto* spans=nativeMesh?nativeMesh->find("face_spans"):nullptr;
    if(!spans||spans->t!=arkjson::Value::Arr)throw std::runtime_error("Snapshot face appearance has no producer face spans");
    for(const auto& patch:feature->faceAppearances){
        const int index=appearanceFaceIndex024B(document,feature->id,patch.face,patch.sourceTopology);
        if(index<0||(size_t)index>=spans->arr.size())throw std::runtime_error("Snapshot face appearance span is out of range");
        const auto& span=spans->arr[index];
        if(span.t!=arkjson::Value::Arr||span.arr.size()!=2||span.arr[0].t!=arkjson::Value::Num||span.arr[1].t!=arkjson::Value::Num)throw std::runtime_error("Snapshot face appearance span is invalid");
        const double start=span.arr[0].num,count=span.arr[1].num;
        if(!std::isfinite(start)||!std::isfinite(count)||start<0||count<0||start!=std::floor(start)||count!=std::floor(count)||start+count>out.size())throw std::runtime_error("Snapshot face appearance triangles are out of range");
        const auto material=displayMaterial(resolvedFaceAppearance(document,*feature,patch.face));
        std::fill(out.begin()+(size_t)start,out.begin()+(size_t)(start+count),material);
    }
    return out;
}
std::vector<SnapshotMaterial> snapshotBodyMaterials(const Document& document,const Body& body,const Mesh& mesh,SnapshotAppearanceOptions044B* options){
    auto out=baseSnapshotMaterials(document,body,mesh);if(options)attachSnapshotSurfaces044B(document,body,mesh,out,*options);return out;
}
}
