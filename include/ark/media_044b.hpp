#pragma once
#include "ark/io.hpp"
#include "ark/model.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ark {
// 044B display appearance: procedural patterns, animated media and the
// software snapshot surface shader. Display only; B-rep stays the authority.

// Procedural patterns are scalar fields in pattern periods, sampled triplanar
// in part-local millimetres. Studio's GLSL mirrors these formulas.
int appearancePatternKind044B(const std::string& name); // 0 none, 1..7 known, -1 unknown
double appearancePatternField044B(int kind,double u,double v);
double appearancePatternTriplanar044B(const Appearance& appearance,const Vec3& localPoint,const Vec3& localNormal);
Vec3 appearancePatternColor044B(const Appearance& appearance,const Vec3& color,double field);
double appearancePatternRoughness044B(const Appearance& appearance,double field);
bool appearancePatternActive044B(const Appearance& appearance);
bool applyAppearancePattern044B(Appearance& appearance,const std::string& key,const arkjson::Value& value,std::string& error);
void appearancePatternToJson044B(const Appearance& appearance,arkjson::Value& out);

// Media assets share Document::imageAssets: image/gif embeds every frame;
// video/mp4 stores a relative path beside the document plus its SHA-256 and
// optional embedded bytes (at most 2 MiB, within the 4 MiB document cap).
bool mediaMime044B(const std::string& mime);
bool animatedAsset044B(const ImageAsset& asset);
std::string mediaAssetBytes044B(const ImageAsset& asset);
void validateMediaAsset044B(const ImageAsset& asset,bool decode);
ImageAsset mediaAssetFromJson044B(const arkjson::Value& value);
// documentDirectory (optional): a video path is read there when relative and is
// stored relative to it, so the saved .ark finds the clip beside itself.
ImageAsset mediaAssetFromPath044B(const std::string& name,const std::string& path,bool embed,const std::string& documentDirectory={});
void mediaAssetToJson044B(const ImageAsset& asset,bool includePixels,arkjson::Value& out);
bool mediaAssetPathRequest044B(const std::string& path);
void stampPlaybackFromJson044B(ProjectedStamp& stamp,const arkjson::Value& value);
void stampPlaybackToJson044B(const ProjectedStamp& stamp,arkjson::Value& out);
bool media044BNeedsVersion47(const Document& document);
// Clip position for elapsed playback seconds: start_s + elapsed*speed, looped
// over the whole clip or held at its last frame; autoplay:false holds start_s.
double mediaPlaybackPosition044B(const ProjectedStamp* playback,double elapsedSeconds,double durationS);

struct MediaImage044B {unsigned width=0,height=0;std::vector<unsigned char> rgba;}; // RGBA8, top row first
struct MediaFrames044B {unsigned width=0,height=0;std::vector<std::vector<unsigned char>> frames;std::vector<double> delaysS;double durationS=0;};
MediaFrames044B decodeGif044B(const std::string& bytes);
size_t mediaFrameAt044B(const std::vector<double>& delaysS,double seconds,bool loop=true);
// Minimal GIF89a writer (fixed 9-bit LZW codes) for synthetic fixtures.
std::string encodeGif044B(unsigned width,unsigned height,const std::vector<std::array<unsigned char,3>>& palette,const std::vector<std::vector<unsigned char>>& indexedFrames,const std::vector<unsigned>& delaysCentiseconds);

// Video uses Windows Media Foundation loaded at run time. Other platforms and
// Windows without Media Foundation report VIDEO_UNSUPPORTED explicitly.
std::string videoDecoderStatus044B(); // "media_foundation" when usable
struct VideoInfo044B {unsigned width=0,height=0;double durationS=0,fps=0;};
VideoInfo044B probeVideo044B(const std::string& path);
MediaImage044B videoFrameAt044B(const std::string& path,double seconds,unsigned maxSide=1024);
bool writeVideoClip044B(const std::string& path,unsigned width,unsigned height,double fps,const std::vector<std::vector<unsigned char>>& rgbaFrames,std::string& error);
// Background sequential decoder with a bounded frame queue. Frame times are
// unwrapped (lap*duration + presentation time) so looping stays monotonic.
class VideoStream044B {
public:
    VideoStream044B(const std::string& path,double startS,bool loop,unsigned maxSide=1024,size_t queue=4);
    ~VideoStream044B();
    VideoStream044B(const VideoStream044B&)=delete;
    VideoStream044B& operator=(const VideoStream044B&)=delete;
    bool frameFor(double unwrappedSeconds,MediaImage044B& out,double* frameSeconds=nullptr);
    std::string status() const;
    double durationS() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
// A path beside the document (verified SHA-256) or the embedded bytes in a
// private temporary file. status: ok, missing, hash_mismatch.
std::string resolvedVideoPath044B(const ImageAsset& asset,const std::string& documentDirectory,std::string& status);

// Software snapshot surfaces: triplanar image textures, procedural patterns
// and surface stamps (GIF/video frames at media_time_s).
struct SnapshotAppearanceOptions044B {
    double mediaTimeS=0;
    std::string documentDirectory;
    arkjson::Value* receipt=nullptr; // optional evidence rows
    std::map<std::string,std::shared_ptr<const MediaImage044B>> images;
};
void attachSnapshotSurfaces044B(const Document& document,const Body& body,const Mesh& mesh,std::vector<SnapshotMaterial>& materials,SnapshotAppearanceOptions044B& options);
// Re-express attached surfaces for one placement (world <- body local).
void placeSnapshotMaterials044B(std::vector<SnapshotMaterial>& materials,size_t first,const Mat4& world);
}
