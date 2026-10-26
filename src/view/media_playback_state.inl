// Session-local, revision-bound display linkage. No hardware trigger or geometry edit.
struct MediaPlaybackBinding {
    std::string motion;u64 revision=~u64(0),epoch=~u64(0);double fps=1,duration=0;
    std::map<std::string,std::string> assets;
    std::map<u64,std::vector<double>> ledFrames;
};
static MediaPlaybackBinding g_mediaPlaybackBinding;
static u64 g_mediaEmissionFeature=0;
static bool mediaBindingCurrent(){return !g_mediaPlaybackBinding.motion.empty()&&g_mediaPlaybackBinding.revision==g_session.revision&&g_mediaPlaybackBinding.epoch==g_documentEpoch;}
static double mediaBindingTime(){const auto& b=g_mediaPlaybackBinding;const auto& p=g_bakedMotions;if(!mediaBindingCurrent()||!p.active||!p.framesValid||p.selected!=b.motion)return 0;return std::min(b.duration,std::floor(p.time*b.fps+1e-6)/b.fps);}
static std::optional<double> boundMediaTime(const ImageAsset& asset){const auto& b=g_mediaPlaybackBinding;if(b.epoch!=g_documentEpoch)return {};auto it=b.assets.find(asset.name);if(it==b.assets.end())return {};if(it->second!=asset.sha256)return 0.;return mediaBindingTime();}
static std::optional<double> boundEmissionFactor(u64 feature){const auto& b=g_mediaPlaybackBinding;if(b.epoch!=g_documentEpoch)return {};auto it=b.ledFrames.find(feature);if(it==b.ledFrames.end())return {};if(!mediaBindingCurrent())return 0.;size_t index=std::min(it->second.size()-1,(size_t)std::floor(mediaBindingTime()*b.fps+1e-6));return it->second[index];}
static arkjson::Value mediaPlaybackTool(const arkjson::Value&);
