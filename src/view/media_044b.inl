// 044B display media: procedural pattern uniforms, animated GIF/video frames
// on stamps and material textures. Decoding runs off the UI thread (GIF via
// std::async, video via VideoStream044B); the GL texture receives a frame only
// when it changes. The media clock advances only while the window is visible
// and media are drawn; selftests may fix it with g_mediaClockOverride044B.
static GLuint stampAssetTexture(const std::string& name);
static double g_mediaClock044B=0,g_mediaClockOverride044B=-1;static ULONGLONG g_mediaLastTick044B=0;
struct MediaPlayer044B {
    std::string sha,status;GLuint texture=0;unsigned width=0,height=0;bool reported=false;
    std::future<MediaFrames044B> pending;std::shared_ptr<const MediaFrames044B> gif;size_t shownFrame=SIZE_MAX;
    std::unique_ptr<VideoStream044B> video;std::string videoPath;double shownTime=-1,syncTime=-1;
};
static std::map<std::string,MediaPlayer044B> g_mediaPlayers044B;
static void tickMedia044B(){const ULONGLONG now=GetTickCount64();if(g_mediaLastTick044B&&!IsIconic(g_window))g_mediaClock044B+=std::min((double)(now-g_mediaLastTick044B)*.001,.25);g_mediaLastTick044B=now;}
static std::string mediaDocumentDirectory044B(){if(g_path.empty())return {};const auto parent=std::filesystem::u8path(g_path).parent_path().u8string();return std::string(parent.begin(),parent.end());}
static void uploadMediaFrame044B(MediaPlayer044B& player,unsigned width,unsigned height,const std::vector<unsigned char>& rgba){
    if(rgba.size()!=(size_t)width*height*4||!width||!height)return;std::vector<unsigned char> flipped(rgba.size());const size_t row=(size_t)width*4;for(size_t y=0;y<height;++y)std::memcpy(&flipped[y*row],&rgba[((size_t)height-1-y)*row],row);
    if(!player.texture){glGenTextures(1,&player.texture);glBindTexture(GL_TEXTURE_2D,player.texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);}
    else glBindTexture(GL_TEXTURE_2D,player.texture);
    if(player.width==width&&player.height==height)glTexSubImage2D(GL_TEXTURE_2D,0,0,0,(GLsizei)width,(GLsizei)height,GL_RGBA,GL_UNSIGNED_BYTE,flipped.data());else glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)width,(GLsizei)height,0,GL_RGBA,GL_UNSIGNED_BYTE,flipped.data());
    player.width=width;player.height=height;
}
static void reportMedia044B(MediaPlayer044B& player,const std::string& asset,const std::string& status){player.status=status;if(!player.reported){player.reported=true;g_status="Media "+asset+": "+status;}}
// Current frame of an animated asset for one playback (a stamp) or the material default.
static GLuint mediaTexture044B(const ImageAsset& asset,const ProjectedStamp* playback){
    tickMedia044B();
    // Players of removed assets (or a replaced document) release their decoder thread and texture.
    for(auto it=g_mediaPlayers044B.begin();it!=g_mediaPlayers044B.end();)if(std::none_of(g_doc.imageAssets.begin(),g_doc.imageAssets.end(),[&](const ImageAsset& a){return a.sha256==it->second.sha;})){if(it->second.texture)glDeleteTextures(1,&it->second.texture);it=g_mediaPlayers044B.erase(it);}else ++it;
    const std::string key=asset.name+"|"+(playback?playback->name:std::string("material"));auto& player=g_mediaPlayers044B[key];
    if(player.sha!=asset.sha256){if(player.texture)glDeleteTextures(1,&player.texture);player=MediaPlayer044B{};player.sha=asset.sha256;}
    if(!player.status.empty()&&player.status!="ok"&&player.status!="decoding"&&player.status!="playing")return player.texture;
    const double elapsed=g_mediaClockOverride044B>=0?g_mediaClockOverride044B:g_mediaClock044B;
    try{
        if(asset.mimeType=="image/gif"){
            if(!player.gif){if(!player.pending.valid()){const auto bytes=mediaAssetBytes044B(asset);player.pending=std::async(std::launch::async,[bytes]{return decodeGif044B(bytes);});player.status="decoding";}
                if(player.pending.wait_for(std::chrono::seconds(g_mediaClockOverride044B>=0?5:0))!=std::future_status::ready)return player.texture;player.gif=std::make_shared<const MediaFrames044B>(player.pending.get());player.status="ok";}
            const size_t frame=mediaFrameAt044B(player.gif->delaysS,mediaPlaybackPosition044B(playback,elapsed,player.gif->durationS),playback?playback->loop:true);
            if(frame!=player.shownFrame&&frame<player.gif->frames.size()){uploadMediaFrame044B(player,player.gif->width,player.gif->height,player.gif->frames[frame]);player.shownFrame=frame;}
            return player.texture;
        }
        if(asset.mimeType!="video/mp4")return 0;
        if(player.videoPath.empty()){std::string located;player.videoPath=resolvedVideoPath044B(asset,mediaDocumentDirectory044B(),located);if(player.videoPath.empty()){reportMedia044B(player,asset.name,located=="hash_mismatch"?"video file differs from its saved SHA-256":"video file "+asset.path+" is missing beside the document");return 0;}
            const auto support=videoDecoderStatus044B();if(support!="media_foundation"){reportMedia044B(player,asset.name,support);return 0;}}
        if(g_mediaClockOverride044B>=0){const double position=mediaPlaybackPosition044B(playback,elapsed,asset.durationS);if(std::abs(position-player.syncTime)>1e-9){const auto image=videoFrameAt044B(player.videoPath,position,1024);uploadMediaFrame044B(player,image.width,image.height,image.rgba);player.syncTime=position;}player.status="ok";return player.texture;}
        if(!player.video){player.video=std::make_unique<VideoStream044B>(player.videoPath,playback?playback->startS:0,playback?playback->loop:true,1024,4);player.status="playing";}
        const double unwrapped=(playback?playback->startS:0)+((playback?playback->autoplay:true)?elapsed*(playback?playback->speed:1):0);
        MediaImage044B image;if(player.video->frameFor(unwrapped,image))uploadMediaFrame044B(player,image.width,image.height,image.rgba);
        const auto status=player.video->status();if(status!="playing"&&status!="opening"&&status!="ended")reportMedia044B(player,asset.name,status);
        return player.texture;
    }catch(const std::exception& error){reportMedia044B(player,asset.name,error.what());return player.texture;}
}
static GLuint stampTexture044B(const ProjectedStamp& stamp){const auto* asset=findImageAsset(g_doc,stamp.asset);if(asset&&mediaMime044B(asset->mimeType))return mediaTexture044B(*asset,&stamp);return stampAssetTexture(stamp.asset);}
// Material textures tile under triplanar sampling: embedded assets repeat.
static GLuint materialTexture044B(const Appearance& a){
    if(a.textureAsset.empty())return materialTexture(a.texturePath);const auto* asset=findImageAsset(g_doc,a.textureAsset);if(!asset)return 0;
    const GLuint texture=mediaMime044B(asset->mimeType)?mediaTexture044B(*asset,nullptr):stampAssetTexture(a.textureAsset);
    if(texture){glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);}return texture;
}
static void setPatternUniforms044B(const Appearance& a){
    const int kind=std::max(0,appearancePatternKind044B(a.pattern));glUniform1f(glGetUniformLocation(pSolid,"patternKind"),(float)kind);if(!kind)return;
    glUniform1f(glGetUniformLocation(pSolid,"patternScale"),(float)a.patternScaleMm);glUniform1f(glGetUniformLocation(pSolid,"patternAngle"),(float)a.patternAngleDeg);glUniform1f(glGetUniformLocation(pSolid,"patternContrast"),(float)a.patternContrast);
    glUniform1f(glGetUniformLocation(pSolid,"patternColorSet"),a.patternColorSet?1.f:0.f);glUniform3f(glGetUniformLocation(pSolid,"patternColor"),(float)a.patternColor.x,(float)a.patternColor.y,(float)a.patternColor.z);
}
static void clearPatternUniforms044B(){glUniform1f(glGetUniformLocation(pSolid,"patternKind"),0.f);}
// Text for the stamp/material dialogs and diagnostics.
static std::string mediaStatus044B(const std::string& assetName){for(const auto& [key,player]:g_mediaPlayers044B)if(key.rfind(assetName+"|",0)==0&&!player.status.empty())return player.status;return "not drawn yet";}
