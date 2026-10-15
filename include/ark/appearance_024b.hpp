#pragma once
#include "ark/model.hpp"

namespace ark {
// These helpers throw on invalid authoring. Normal readback omits base64 pixels.
const ImageAsset* findImageAsset(const Document& document,const std::string& name);
ImageAsset imageAssetFromPath(const std::string& name,const std::string& path);
ImageAsset imageAssetFromJson(const arkjson::Value& value);
arkjson::Value imageAssetToJson(const ImageAsset& asset,bool includePixels=false);
std::vector<unsigned char> decodeImageAssetRGBA(const ImageAsset& asset);
ProjectedStamp stampFromJson(const arkjson::Value& value);
arkjson::Value stampToJson(const ProjectedStamp& stamp);
FaceAppearance faceAppearanceFromJson(const arkjson::Value& value);
arkjson::Value faceAppearanceToJson(const FaceAppearance& appearance);
// Recognizes image_asset.set/remove, stamp.set/remove, face_appearance.set/remove
// and revision-checked, merge-preserving light_up controls.
// Call on the host's scratch document; revision/commit publication remains host-owned.
bool applyAppearance024BCommand(Document& document,const arkjson::Value& command);
void validateAppearanceAuthoring024B(const Document& document,bool validateGeometry=false);
bool appearance024BNeedsVersion39(const Document& document);
Appearance resolvedFaceAppearance(const Document& document,const Feature& feature,const std::string& face);
// Index is derived from exact producer topology, never accepted as authored input.
int appearanceFaceIndex024B(const Document& document,u64 feature,const std::string& face,const std::string& sourceTopology);
struct StampProjector {
    Vec3 origin{},uAxis{1,0,0},vAxis{0,1,0},normal{0,0,1};
    double width=100,height=100,opacity=1,depth=1;
    bool frontOnly=true;
};
StampProjector resolvedStampProjector(const Document& document,const ProjectedStamp& stamp);
// False only when an existing source dependency is inactive. Invalid/cyclic
// metadata still throws; geometry and topology remain checked on activation.
bool stampGeometryAvailable024B(const Document& document,const ProjectedStamp& stamp);
// CPU counterpart of the bounded shader projection: UV rectangle, depth slab,
// and outward-facing triangle guard. UV (0,0) is image bottom-left.
struct StampVertex {Vec3 position,normal;Vec2 uv;};
std::vector<StampVertex> projectStampTriangles(const Mesh& mesh,size_t firstTriangle,size_t triangleCount,const StampProjector& projector);
}
