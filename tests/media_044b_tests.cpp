#include "ark/media_044b.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/agent.hpp"
#include "ark/assistant.hpp"
#include "ark/io.hpp"
#include "ark/serialize.hpp"
#include "ark/snapshot_032d.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using V=arkjson::Value;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
V json(const std::string& source){V value;check(arkjson::parse(source,value),"Bad test JSON");return value;}
template<class F>void rejects(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
V accepted(const V& reply){if(reply.find("isError")&&reply.find("isError")->b)throw std::runtime_error(reply.find("structuredContent")->dump());return *reply.find("structuredContent");}
V apply(ark::AgentSession& s,const V& commands){V batch=V::makeObj();batch.set("commands",commands);batch.set("expected_revision",V::makeNum((double)s.revision));return accepted(s.call("doc_apply",batch));}
double version(const ark::Document& d){return json(ark::documentToJson(d,true,false)).find("version")->num;}
std::string base64(const std::string& bytes){static const char* a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;for(size_t i=0;i<bytes.size();i+=3){unsigned n=(unsigned)(unsigned char)bytes[i]<<16;if(i+1<bytes.size())n|=(unsigned)(unsigned char)bytes[i+1]<<8;if(i+2<bytes.size())n|=(unsigned char)bytes[i+2];out+=a[(n>>18)&63];out+=a[(n>>12)&63];out+=i+1<bytes.size()?a[(n>>6)&63]:'=';out+=i+2<bytes.size()?a[n&63]:'=';}return out;}
// Three solid frames: red 0.1 s, green 0.2 s, blue 0.3 s.
std::string threeFrameGif(unsigned w=8,unsigned h=6){std::vector<std::vector<unsigned char>> frames;for(unsigned char index:{0,1,2})frames.emplace_back((size_t)w*h,index);return ark::encodeGif044B(w,h,{{{230,20,20}},{{20,210,30}},{{25,30,235}},{{0,0,0}}},frames,{10,20,30});}
V gifAsset(const std::string& name,const std::string& bytes){V asset=V::makeObj();asset.set("name",V::makeStr(name));asset.set("mime_type",V::makeStr("image/gif"));asset.set("data_base64",V::makeStr(base64(bytes)));return asset;}
V command(const char* op,V body){body.set("op",V::makeStr(op));return body;}
V commands(std::initializer_list<V> items){V out=V::makeArr();for(const auto& item:items)out.arr.push_back(item);return out;}

void patternFields(){
    for(int kind=1;kind<=7;++kind){double low=1,high=0;for(int i=0;i<400;++i){const double value=ark::appearancePatternField044B(kind,i*.0137,i*.0291);check(value>=0&&value<=1,"pattern field stays in [0,1]");low=std::min(low,value);high=std::max(high,value);}check(high-low>.2,"every pattern varies over its period");}
    check(ark::appearancePatternField044B(1,.1,.2)==ark::appearancePatternField044B(1,.1,.2),"pattern fields are deterministic");check(ark::appearancePatternKind044B("houndstooth")<0&&ark::appearancePatternKind044B("none")==0,"pattern vocabulary is explicit");
    ark::Appearance a;std::string error;check(ark::applyAppearancePatch(a,json(R"({"pattern":"twill","pattern_scale_mm":3,"pattern_angle_deg":30,"pattern_contrast":0.6,"pattern_color":[0.1,0.1,0.12]})"),error),error);
    check(!ark::applyAppearancePatch(a,json(R"({"pattern":"plaid"})"),error)&&!ark::applyAppearancePatch(a,json(R"({"pattern_scale_mm":0})"),error)&&!ark::applyAppearancePatch(a,json(R"({"pattern_contrast":2})"),error),"invalid pattern fields reject");
    const auto round=ark::appearanceToJson(a);ark::Appearance b;check(ark::applyAppearancePatch(b,round,error),error);check(round.dump()==ark::appearanceToJson(b).dump()&&b.pattern=="twill"&&b.patternColorSet,"pattern appearance round-trips");
    check(!ark::appearanceToJson(ark::Appearance{}).find("pattern"),"default appearance writes no pattern keys");
}
void patternPersistence(){
    ark::AgentSession s;apply(s,json(R"([{"op":"feature.add","type":"box","name":"Card","p":{"dx":60,"dy":40,"dz":4}}])"));check(version(s.document)<47,"plain document keeps its format");
    const auto before=version(s.document);
    auto receipt=apply(s,json(R"([{"op":"material.set","name":"Twill fabric","density":0.4,"appearance":{"base_color":[0.2,0.22,0.26],"roughness":0.8,"pattern":"twill","pattern_scale_mm":2.5,"pattern_angle_deg":45,"pattern_contrast":0.7}},{"op":"feature.edit","id":"Card","material":"Twill fabric"}])"));
    check(!receipt.find("geometry_recomputed")->b,"material pattern edit keeps geometry");check(version(s.document)==47,"pattern appearance saves version 47");
    receipt=apply(s,json(R"([{"op":"feature.edit","id":"Card","appearance":{"pattern":"leather","pattern_scale_mm":6}}])"));check(!receipt.find("geometry_recomputed")->b,"feature appearance pattern edit keeps geometry");
    const auto saved=ark::documentToJson(s.document,true,false);ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),error);check(ark::documentToJson(restored,true,false)==saved,"pattern document round-trips byte for byte");
    check(ark::resolvedAppearance(restored,restored.features[0]).pattern=="leather"&&restored.materials[0].appearance.pattern=="twill","material and override patterns persist");
    auto older=json(saved);older.set("version",V::makeNum(46));check(!ark::documentFromJson(older.dump(),restored,&error)&&error.find("version 47")!=std::string::npos,"version 46 cannot carry patterns");
    apply(s,json(R"([{"op":"feature.edit","id":"Card","material":null,"appearance":null},{"op":"material.remove","name":"Twill fabric"}])"));check(version(s.document)==before,"removing patterns restores the earlier format");
}
ark::Image render(ark::Document& d,const ark::Body& body,ark::SnapshotAppearanceOptions044B* options,ark::Vec3 direction={0,0,1}){
    const auto mesh=body.tessellate(d.tessOpt);auto materials=ark::snapshotBodyMaterials(d,body,mesh,options);ark::SnapCam camera;const auto box=mesh.computeBBox();camera.useTarget=true;camera.target=(box.mn+box.mx)*.5;camera.useDirection=true;camera.direction=direction;camera.up={0,1,0};camera.dist=120;camera.fovDeg=30;
    return ark::renderSnapshot(mesh,320,200,camera,materials);
}
// ARK_MEDIA_044B_CAPTURE_DIR keeps the software snapshots for visual review.
void keep(const ark::Image& image,const char* name){if(const char* folder=std::getenv("ARK_MEDIA_044B_CAPTURE_DIR"))ark::writePNG((std::filesystem::path(folder)/name).string().c_str(),image);}
double difference(const ark::Image& a,const ark::Image& b){double sum=0;for(size_t i=0;i<a.px.size();++i)sum+=std::abs((int)a.px[i]-(int)b.px[i]);return sum/a.px.size();}
double variance(const ark::Image& image,int x0,int y0,int x1,int y1){double sum=0,sum2=0,n=0;for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){const double v=image.px[((size_t)y*image.w+x)*3+1];sum+=v;sum2+=v*v;++n;}const double mean=sum/n;return sum2/n-mean*mean;}
void snapshotTexturesAndPatterns(){
    ark::AgentSession s;apply(s,json(R"([{"op":"feature.add","type":"box","name":"Card","p":{"dx":60,"dy":40,"dz":4}}])"));
    const auto& body=s.document.evalBodies.front();ark::SnapshotAppearanceOptions044B plainOptions;const auto plain=render(s.document,body,&plainOptions);
    ark::Image checker;checker.w=checker.h=32;checker.px.resize(32*32*3);for(int y=0;y<32;++y)for(int x=0;x<32;++x)for(int c=0;c<3;++c)checker.px[((size_t)y*32+x)*3+c]=((x/8+y/8)%2)?250:20;
    const auto attachment=ark::assistantImageAttachment(ark::encodePNG(checker),"checker.png",ark::AssistantImageDecoder::Portable);V asset=V::makeObj();asset.set("name",V::makeStr("Checker"));asset.set("mime_type",V::makeStr(attachment.mimeType));asset.set("data_base64",V::makeStr(attachment.base64));
    V set=V::makeObj();set.set("asset",asset);apply(s,commands({command("image_asset.set",set),json(R"({"op":"feature.edit","id":"Card","appearance":{"base_color":[1,1,1],"texture_asset":"Checker","texture_scale":[4,4]}})")}));
    ark::SnapshotAppearanceOptions044B textureOptions;V receipt=V::makeObj();textureOptions.receipt=&receipt;const auto textured=render(s.document,s.document.evalBodies.front(),&textureOptions);
    check(receipt.find("textured_surfaces")&&receipt.find("textured_surfaces")->num==1,"snapshot receipt counts the textured surface");check(difference(plain,textured)>5,"snapshot pixels change with an image texture");check(variance(textured,120,70,200,130)>variance(plain,120,70,200,130)+200,"triplanar checker texture is visible across the face");
    apply(s,json(R"([{"op":"feature.edit","id":"Card","appearance":{"base_color":[0.8,0.8,0.82],"pattern":"knurl","pattern_scale_mm":6,"pattern_contrast":1}}])"));ark::SnapshotAppearanceOptions044B patternOptions;const auto patterned=render(s.document,s.document.evalBodies.front(),&patternOptions);
    keep(plain,"snapshot-plain.png");keep(textured,"snapshot-texture.png");keep(patterned,"snapshot-knurl.png");check(difference(plain,patterned)>3,"snapshot pixels change with a procedural pattern");check(variance(patterned,120,70,200,130)>variance(plain,120,70,200,130)+50,"knurl pattern is visible");
    ark::SnapshotAppearanceOptions044B again;check(render(s.document,s.document.evalBodies.front(),&again).px==patterned.px,"software snapshot patterns are deterministic");
    V args=json(R"({"id":1})");const auto reply=accepted(s.call("snapshot",args));const auto& evidence=*reply.find("visual_evidence");check(evidence.find("textures_and_stamps")->b&&evidence.find("appearance_surfaces")->find("patterned_surfaces")->num==1,"snapshot tool reports textures/patterns evidence");
    std::cout<<"snapshot: texture diff "<<difference(plain,textured)<<", pattern diff "<<difference(plain,patterned)<<"\n";
}
void gifDecodeAndAssets(){
    const auto bytes=threeFrameGif();const auto frames=ark::decodeGif044B(bytes);check(frames.width==8&&frames.height==6&&frames.frames.size()==3,"GIF frame census");
    check(std::abs(frames.delaysS[0]-.1)<1e-12&&std::abs(frames.delaysS[1]-.2)<1e-12&&std::abs(frames.delaysS[2]-.3)<1e-12&&std::abs(frames.durationS-.6)<1e-12,"GIF per-frame delays");
    const unsigned char expected[3][3]={{230,20,20},{20,210,30},{25,30,235}};for(int f=0;f<3;++f)for(size_t p=0;p<48;++p)for(int c=0;c<3;++c)check(frames.frames[f][p*4+c]==expected[f][c]&&frames.frames[f][p*4+3]==255,"GIF frame colors are exact");
    check(ark::mediaFrameAt044B(frames.delaysS,.05)==0&&ark::mediaFrameAt044B(frames.delaysS,.25)==1&&ark::mediaFrameAt044B(frames.delaysS,.5)==2&&ark::mediaFrameAt044B(frames.delaysS,.65)==0&&ark::mediaFrameAt044B(frames.delaysS,.65,false)==2,"GIF frame timing loops or holds");
    std::string corrupt=bytes;corrupt.resize(40);rejects([&]{(void)ark::decodeGif044B(corrupt);},"truncated GIF rejects");rejects([&]{(void)ark::decodeGif044B("not a gif at all");},"non-GIF bytes reject");
    ark::AgentSession s;V set=V::makeObj();set.set("asset",gifAsset("Screen loop",bytes));apply(s,commands({command("image_asset.set",set)}));const auto& asset=s.document.imageAssets.front();check(asset.mimeType=="image/gif"&&asset.frames==3&&std::abs(asset.durationS-.6)<1e-12,"GIF asset metadata");
    check(version(s.document)==47,"GIF asset saves version 47");const auto saved=ark::documentToJson(s.document,true,false);ark::Document restored;std::string error;check(ark::documentFromJson(saved,restored,&error),error);check(ark::documentToJson(restored,true,false)==saved,"GIF document round-trips");
    auto tampered=json(saved);auto rows=*tampered.find("image_assets");rows.arr[0].set("sha256",V::makeStr(std::string(64,'0')));tampered.set("image_assets",rows);check(!ark::documentFromJson(tampered.dump(),restored,&error),"GIF hash mismatch rejects");
    V bad=V::makeObj();bad.set("asset",gifAsset("Broken",corrupt));const auto reply=s.call("doc_apply",[&]{V b=V::makeObj();b.set("commands",commands({command("image_asset.set",bad)}));b.set("expected_revision",V::makeNum((double)s.revision));return b;}());check(reply.find("isError")->b&&s.document.imageAssets.size()==1,"corrupt GIF import rejects atomically");
    ark::ProjectedStamp stamp;ark::stampPlaybackFromJson044B(stamp,json(R"({"loop":false,"speed":2,"start_s":0.5,"autoplay":true})"));check(!stamp.loop&&stamp.speed==2&&stamp.startS==.5,"stamp playback fields");
    check(std::abs(ark::mediaPlaybackPosition044B(&stamp,1,1.2)-1.2)<1e-12,"non-looping playback holds the last frame");stamp.loop=true;check(std::abs(ark::mediaPlaybackPosition044B(&stamp,1,1.2)-.1)<1e-12,"looping playback wraps");stamp.autoplay=false;check(ark::mediaPlaybackPosition044B(&stamp,5,1.2)==.5,"autoplay false holds start_s");
    rejects([&]{ark::ProjectedStamp bad;ark::stampPlaybackFromJson044B(bad,json(R"({"speed":0})"));},"zero speed rejects");
    std::cout<<"GIF: 3 frames, delays 0.1/0.2/0.3 s, exact colors, version 47 round-trip\n";
}
#ifdef _WIN32
std::vector<unsigned char> clipFrame(unsigned w,unsigned h,int k){static const unsigned char colors[4][3]={{220,30,30},{30,200,40},{30,40,220},{220,210,30}};std::vector<unsigned char> rgba((size_t)w*h*4,255);for(unsigned y=0;y<h/2;++y)for(unsigned x=0;x<w;++x)for(int c=0;c<3;++c)rgba[((size_t)y*w+x)*4+c]=colors[k%4][c];return rgba;}
bool near(const ark::MediaImage044B& image,unsigned x,unsigned y,const unsigned char* rgb,int tolerance){const auto* p=&image.rgba[((size_t)y*image.width+x)*4];for(int c=0;c<3;++c)if(std::abs((int)p[c]-(int)rgb[c])>tolerance)return false;return true;}
void videoClip(){
    const std::string name="ark-media-044b-test.mp4";std::filesystem::remove(name);std::vector<std::vector<unsigned char>> frames;for(int k=0;k<12;++k)frames.push_back(clipFrame(160,96,k));std::string error;
    if(!ark::writeVideoClip044B(name,160,96,12,frames,error)){std::cout<<"MP4 test skipped: H.264 encoding unavailable here ("<<error<<"); decoder status "<<ark::videoDecoderStatus044B()<<"\n";return;}
    const auto info=ark::probeVideo044B(name);check(info.width==160&&info.height==96,"decoded video size");check(std::abs(info.durationS-1)<.1,"decoded video duration");
    static const unsigned char colors[4][3]={{220,30,30},{30,200,40},{30,40,220},{220,210,30}};static const unsigned char white[3]={255,255,255};
    for(int k:{0,3,6,11}){const auto image=ark::videoFrameAt044B(name,(k+.5)/12);check(image.width==160&&image.height==96,"frame size");check(near(image,80,20,colors[k%4],40),"frame "+std::to_string(k)+" top colour within H.264 tolerance");check(near(image,80,76,white,40),"frame "+std::to_string(k)+" keeps its vertical orientation");}
    const auto small=ark::videoFrameAt044B(name,.5,64);check(small.width==53&&small.height==32,"bounded decode downscales by an integer box factor");
    std::vector<double> times;{ark::VideoStream044B stream(name,0,true,1024,4);const auto started=std::chrono::steady_clock::now();
    for(int step=0;step<=58;++step){const double target=step/24.;for(int attempt=0;attempt<25;++attempt){ark::MediaImage044B image;double time=0;if(stream.frameFor(target,image,&time)){times.push_back(time);const double clip=stream.durationS()>0?stream.durationS():1.;const int k=(int)std::lround(std::fmod(time,clip)*12)%12;check(near(image,80,20,colors[k%4],40),"streamed frame colour at "+std::to_string(time));break;}if(std::chrono::steady_clock::now()-started>std::chrono::seconds(30))throw std::runtime_error("video stream stalled: "+stream.status());std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
    }check(times.size()>=12&&times.back()>1,"stream loops past the clip end with unwrapped timestamps");for(size_t i=1;i<times.size();++i)check(times[i]>times[i-1],"stream timestamps increase");
    // Persisted by relative path + SHA-256; a changed file is detected explicitly.
    ark::AgentSession s;auto asset=ark::mediaAssetFromPath044B("Clip",name,false);check(asset.mimeType=="video/mp4"&&asset.width==160&&asset.base64.empty()&&asset.sha256.size()==64,"video asset by path");
    apply(s,json(R"([{"op":"image_asset.set","name":"Clip","path":")"+name+R"("}])"));check(s.document.imageAssets.front().path==name&&version(s.document)==47,"video asset command and version");
    std::string status;check(!ark::resolvedVideoPath044B(s.document.imageAssets.front(),"",status).empty()&&status=="ok","video resolves beside the working document folder");
    { const auto folder=std::filesystem::absolute("ark-media-044b-document");std::filesystem::create_directories(folder/"media");std::filesystem::copy_file(name,folder/"media"/"clip.mp4",std::filesystem::copy_options::overwrite_existing);
      const auto beside=ark::mediaAssetFromPath044B("Clip",(folder/"media"/"clip.mp4").string(),false,folder.string());check(beside.path=="media/clip.mp4","dialog import stores the video relative to the document folder");
      std::string located;check(!ark::resolvedVideoPath044B(beside,folder.string(),located).empty()&&located=="ok","document-relative video resolves beside the document");std::filesystem::remove_all(folder); }
    const auto embedded=ark::mediaAssetFromPath044B("Clip",name,true);check(!embedded.base64.empty(),"small video can be embedded");
    const auto saved=ark::documentToJson(s.document,true,false);ark::Document restored;check(ark::documentFromJson(saved,restored,&error),error);check(ark::documentToJson(restored,true,false)==saved,"video document round-trips");
    { std::ofstream out(name,std::ios::binary|std::ios::app);out<<"tamper"; }check(ark::resolvedVideoPath044B(restored.imageAssets.front(),"",status).empty()&&status=="hash_mismatch","changed video is detected");
    check(!ark::resolvedVideoPath044B(embedded,"",status).empty()&&status=="ok_embedded","embedded bytes stay playable when the file changes");
    std::filesystem::remove(name);check(ark::resolvedVideoPath044B(restored.imageAssets.front(),"",status).empty()&&status=="missing","missing video is explicit");
    rejects([&]{(void)ark::videoFrameAt044B("missing-clip.mp4",0);},"missing video decode rejects");
    std::cout<<"MP4: 12 frames 160x96 H.264 encoded and decoded; stream delivered "<<times.size()<<" frames to "<<times.back()<<" s\n";
}
#endif
#ifdef ARK_MEDIA_NATIVE_TESTS
void stampedScreenSnapshot(){
    ark::AgentSession s;apply(s,json(R"([{"op":"feature.add","type":"native_solid","name":"Screen","native":{"op":"box"},"p":{"width":40,"depth":30,"height":5}}])"));
    const auto topology=s.document.evalBodies.front().nativeData.find("topology")->find("brep_sha256")->str;V set=V::makeObj();set.set("asset",gifAsset("Screen loop",threeFrameGif(16,12)));
    V stamp=json(R"({"name":"Display","asset":"Screen loop","feature":"Screen","faces":["z_max"],"origin":[20,15,5],"u_axis":[1,0,0],"v_axis":[0,1,0],"width":30,"height":20,"speed":1})");stamp.set("source_topology",V::makeStr(topology));
    apply(s,commands({command("image_asset.set",set),command("stamp.set",stamp)}));
    auto frameAt=[&](double t){ark::SnapshotAppearanceOptions044B options;options.mediaTimeS=t;V receipt=V::makeObj();options.receipt=&receipt;const auto image=render(s.document,s.document.evalBodies.front(),&options);check(receipt.find("stamps_drawn")&&receipt.find("stamps_drawn")->num==1,"stamp drawn in snapshot");const auto* p=&image.px[((size_t)(image.h/2)*image.w+image.w/2)*3];return std::array<int,3>{p[0],p[1],p[2]};};
    {ark::SnapshotAppearanceOptions044B options;options.mediaTimeS=.15;keep(render(s.document,s.document.evalBodies.front(),&options,{.3,-.5,1}),"snapshot-gif-stamp-t015.png");}
    const auto red=frameAt(0),green=frameAt(.15),blue=frameAt(.45);check(red[0]>red[1]+60&&red[0]>red[2]+60,"t=0 shows the red frame");check(green[1]>green[0]+60,"t=0.15 shows the green frame");check(blue[2]>blue[0]+60,"t=0.45 shows the blue frame");
    V snapshotArgs=json(R"({"media_time_s":0.45,"view":"top"})");snapshotArgs.set("id",V::makeNum((double)s.document.features[0].id));const auto reply=accepted(s.call("snapshot",snapshotArgs));const auto& media=reply.find("visual_evidence")->find("appearance_surfaces")->find("media")->arr;check(!media.empty()&&media[0].find("status")->str=="ok"&&media[0].find("frame")->num==2,"snapshot tool selects the GIF frame at media_time_s");
    std::cout<<"stamped GIF screen: centre pixels "<<red[0]<<","<<red[1]<<","<<red[2]<<" / "<<green[0]<<","<<green[1]<<","<<green[2]<<" / "<<blue[0]<<","<<blue[1]<<","<<blue[2]<<"\n";
}
#endif
}
int main(){std::cout<<std::unitbuf;try{patternFields();patternPersistence();snapshotTexturesAndPatterns();gifDecodeAndAssets();
#ifdef _WIN32
videoClip();
#else
std::cout<<"MP4 test skipped: "<<ark::videoDecoderStatus044B()<<"\n";
#endif
#ifdef ARK_MEDIA_NATIVE_TESTS
stampedScreenSnapshot();
#endif
std::cout<<"media 044B tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
