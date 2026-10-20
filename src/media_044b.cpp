#include "ark/media_044b.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#endif

// A private bounded GIF-only stb instance: GIF frames are composited by stb
// (disposal, transparency) identically on every platform.
namespace {
constexpr std::size_t gifAllocationLimit=256*1024*1024;
struct alignas(std::max_align_t) GifAllocation {std::size_t bytes;};
thread_local std::size_t gifAllocated=0;
void* gifMalloc(std::size_t bytes){if(bytes>gifAllocationLimit-sizeof(GifAllocation))return nullptr;const auto total=bytes+sizeof(GifAllocation);if(total>gifAllocationLimit-gifAllocated)return nullptr;auto* block=static_cast<GifAllocation*>(std::malloc(total));if(!block)return nullptr;block->bytes=total;gifAllocated+=total;return block+1;}
void gifFree(void* pointer){if(!pointer)return;auto* block=static_cast<GifAllocation*>(pointer)-1;gifAllocated-=block->bytes;std::free(block);}
void* gifRealloc(void* pointer,std::size_t bytes){if(!pointer)return gifMalloc(bytes);if(!bytes){gifFree(pointer);return nullptr;}auto* old=static_cast<GifAllocation*>(pointer)-1;const auto oldBytes=old->bytes;if(bytes>gifAllocationLimit-sizeof(GifAllocation))return nullptr;const auto total=bytes+sizeof(GifAllocation);if(total>gifAllocationLimit-(gifAllocated-oldBytes))return nullptr;auto* block=static_cast<GifAllocation*>(std::realloc(old,total));if(!block)return nullptr;block->bytes=total;gifAllocated=gifAllocated-oldBytes+total;return block+1;}
}
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 2048
#define STBI_MALLOC(n) gifMalloc(n)
#define STBI_REALLOC(p,n) gifRealloc(p,n)
#define STBI_FREE(p) gifFree(p)
#include "stb/stb_image.h"

namespace ark {
namespace fs=std::filesystem;
namespace {
using V=arkjson::Value;
void need(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
std::string utf8(const fs::path& path){const auto text=path.u8string();return std::string(text.begin(),text.end());}
double fract(double x){return x-std::floor(x);}
double modp(double x,double y){return x-y*std::floor(x/y);}
double hash2(double x,double y){return fract(std::sin(x*127.1+y*311.7)*43758.5453);}
double smooth(double a,double b,double x){const double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);}
double valueNoise(double x){const double i=std::floor(x),f=x-i,u=f*f*(3-2*f);return hash2(i,0)*(1-u)+hash2(i+1,0)*u;}
const char* const patternNames[]={"","twill","weave","carbon","leather","brushed","knurl","perforated"};
bool digest(const std::string& value){return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string base64Decode(const std::string& text){
    static const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";need(text.size()%4==0,"Media base64 length is invalid");std::string out;out.reserve(text.size()/4*3);
    for(size_t i=0;i<text.size();i+=4){unsigned n=0;for(unsigned j=0;j<4;++j){const char c=text[i+j];const auto at=alphabet.find(c);need(c=='='?(j>=2&&i+4==text.size()):at!=std::string::npos,"Media base64 contains invalid characters");n=(n<<6)|(c=='='?0:(unsigned)at);}
        out.push_back((char)(n>>16));if(text[i+2]!='=')out.push_back((char)(n>>8));if(text[i+3]!='=')out.push_back((char)n);}return out;}
std::string base64Encode(const std::string& bytes){static const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;out.reserve((bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3){unsigned n=(unsigned)(unsigned char)bytes[i]<<16;if(i+1<bytes.size())n|=(unsigned)(unsigned char)bytes[i+1]<<8;if(i+2<bytes.size())n|=(unsigned char)bytes[i+2];out.push_back(alphabet[(n>>18)&63]);out.push_back(alphabet[(n>>12)&63]);out.push_back(i+1<bytes.size()?alphabet[(n>>6)&63]:'=');out.push_back(i+2<bytes.size()?alphabet[n&63]:'=');}return out;}
std::string readBounded(const fs::path& path,std::uintmax_t limit){std::error_code error;need(fs::is_regular_file(path,error),"Media file was not found: "+utf8(path));need(fs::file_size(path,error)<=limit,"Media file exceeds its "+std::to_string(limit/1024/1024)+" MiB bound");std::ifstream in(path,std::ios::binary);need(bool(in),"Media file cannot be read");return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());}
bool gifMagic(const std::string& bytes){return bytes.size()>=13&&(bytes.compare(0,6,"GIF87a")==0||bytes.compare(0,6,"GIF89a")==0);}
bool mp4Magic(const std::string& bytes){return bytes.size()>=12&&bytes.compare(4,4,"ftyp")==0;}
void assetName(const std::string& value){need(!value.empty()&&value.size()<=128&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||c==127;}),"Media asset name must be 1..128 printable characters");}
void relativeVideoPath(const std::string& path){need(!path.empty()&&path.size()<=1024,"Video path must be 1..1024 characters");const auto p=fs::u8path(path);need(!p.has_root_name()&&!p.has_root_directory(),"Video path must be relative to the document folder; copy the clip beside the .ark");}
}

int appearancePatternKind044B(const std::string& name){if(name.empty()||name=="none")return 0;for(int i=1;i<8;++i)if(name==patternNames[i])return i;return -1;}
double appearancePatternField044B(int kind,double u,double v){
    constexpr double pi=3.14159265358979323846;
    if(kind==1||kind==2||kind==3){const double t=kind==2?2:4,x=u*t,y=v*t,cx=std::floor(x),cy=std::floor(y);const bool warp=kind==2?modp(cx+cy,2)<1:modp(cx+cy,4)<2;const double profile=warp?std::sin(pi*(y-cy)):std::sin(pi*(x-cx));
        const double lit=kind==3?(warp?.75+.25*profile:.25+.25*profile):(warp?.55+.45*profile:.1+.45*profile);return 1-lit;}
    if(kind==4){const double x=u*3,y=v*3,ix=std::floor(x),iy=std::floor(y);double d1=9,d2=9;for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i){const double gx=ix+i,gy=iy+j,px=gx+hash2(gx,gy),py=gy+hash2(gy+17.3,gx-9.1),d=std::hypot(px-x,py-y);if(d<d1){d2=d1;d1=d;}else if(d<d2)d2=d;}
        return std::clamp(.85*(1-smooth(.02,.22,d2-d1))+.15*hash2(std::floor(u*12),std::floor(v*12)),0.,1.);}
    if(kind==5)return std::clamp(.65*valueNoise(v*60)+.35*valueNoise(v*170+3.7),0.,1.);
    if(kind==6){const double a=std::abs(fract(u+v)-.5),b=std::abs(fract(u-v)-.5);return 1-2*std::min(.5-a,.5-b);}
    if(kind==7){const double row=std::floor(v),x=u+.5*modp(row,2);return 1-smooth(.26,.30,std::hypot(fract(x)-.5,fract(v)-.5));}
    return 0;
}
double appearancePatternTriplanar044B(const Appearance& a,const Vec3& p,const Vec3& n){
    const int kind=appearancePatternKind044B(a.pattern);if(kind<=0)return 0;const double angle=a.patternAngleDeg*3.14159265358979323846/180,c=std::cos(angle),s=std::sin(angle),scale=std::max(a.patternScaleMm,1e-6);
    double w[3]={std::pow(std::abs(n.x),4.),std::pow(std::abs(n.y),4.),std::pow(std::abs(n.z),4.)};const double sum=std::max(w[0]+w[1]+w[2],1e-5);
    auto field=[&](double x,double y){return appearancePatternField044B(kind,(c*x+s*y)/scale,(-s*x+c*y)/scale);};
    return (w[2]*field(p.x,p.y)+w[1]*field(p.x,p.z)+w[0]*field(p.y,p.z))/sum;
}
Vec3 appearancePatternColor044B(const Appearance& a,const Vec3& color,double field){const int kind=appearancePatternKind044B(a.pattern);if(kind<=0)return color;const Vec3 secondary=a.patternColorSet?a.patternColor:color*(kind==7?.08:.45);const double t=std::clamp(field*a.patternContrast,0.,1.);return color*(1-t)+secondary*t;}
double appearancePatternRoughness044B(const Appearance& a,double field){const int kind=appearancePatternKind044B(a.pattern);if(kind<=0||kind==5)return a.roughness;const double delta=kind==3?-(1-field)*.25:kind>=6?field*.2:(field-.5)*.35;return std::clamp(a.roughness+delta*a.patternContrast,.04,1.);}
bool appearancePatternActive044B(const Appearance& a){return !a.pattern.empty()||a.patternScaleMm!=4||a.patternAngleDeg!=0||a.patternContrast!=.35||a.patternColorSet;}
bool applyAppearancePattern044B(Appearance& a,const std::string& key,const V& value,std::string& error){
    auto number=[&](double& target,double lo,double hi){if(value.t!=V::Num||!std::isfinite(value.num)||value.num<lo||value.num>hi){error="Invalid appearance "+key;return false;}target=value.num;return true;};
    if(key=="pattern"){if(value.t!=V::Str||appearancePatternKind044B(value.str)<0){error="pattern must be none, twill, weave, carbon, leather, brushed, knurl or perforated";return false;}a.pattern=value.str=="none"?"":value.str;return true;}
    if(key=="pattern_scale_mm")return number(a.patternScaleMm,.05,1000);
    if(key=="pattern_angle_deg")return number(a.patternAngleDeg,-360,360);
    if(key=="pattern_contrast")return number(a.patternContrast,0,1);
    if(key=="pattern_color"){if(value.t==V::Null){a.patternColorSet=false;a.patternColor={0,0,0};return true;}if(value.t!=V::Arr||value.arr.size()!=3){error="pattern_color needs three RGB values or null";return false;}double rgb[3];for(int i=0;i<3;++i){if(value.arr[i].t!=V::Num||!std::isfinite(value.arr[i].num)||value.arr[i].num<0||value.arr[i].num>1){error="pattern_color must be RGB 0..1";return false;}rgb[i]=value.arr[i].num;}a.patternColor={rgb[0],rgb[1],rgb[2]};a.patternColorSet=true;return true;}
    error="Unknown appearance property: "+key;return false;
}
void appearancePatternToJson044B(const Appearance& a,V& out){
    if(!a.pattern.empty())out.set("pattern",V::makeStr(a.pattern));if(a.patternScaleMm!=4)out.set("pattern_scale_mm",V::makeNum(a.patternScaleMm));if(a.patternAngleDeg!=0)out.set("pattern_angle_deg",V::makeNum(a.patternAngleDeg));if(a.patternContrast!=.35)out.set("pattern_contrast",V::makeNum(a.patternContrast));
    if(a.patternColorSet){V color=V::makeArr();for(double c:{a.patternColor.x,a.patternColor.y,a.patternColor.z})color.arr.push_back(V::makeNum(c));out.set("pattern_color",color);}
}

bool mediaMime044B(const std::string& mime){return mime=="image/gif"||mime=="video/mp4";}
bool animatedAsset044B(const ImageAsset& asset){return asset.mimeType=="video/mp4"||(asset.mimeType=="image/gif"&&asset.frames>1);}
std::string mediaAssetBytes044B(const ImageAsset& asset){if(asset.base64.empty())return {};const auto bytes=base64Decode(asset.base64);need(bytes.size()<=(asset.mimeType=="video/mp4"?2u:4u)*1024u*1024u,"Embedded media exceeds its size bound");return bytes;}
void validateMediaAsset044B(const ImageAsset& asset,bool decode){
    assetName(asset.name);need(mediaMime044B(asset.mimeType),"Media asset MIME must be image/gif or video/mp4");need(digest(asset.sha256),"Media asset requires its SHA-256");
    need(std::isfinite(asset.durationS)&&asset.durationS>=0&&asset.durationS<=86400&&asset.frames<=1000000&&asset.width<=8192&&asset.height<=8192,"Media asset metadata is out of range");
    if(asset.mimeType=="image/gif"){
        need(asset.path.empty(),"An animated GIF is embedded; it has no path");const auto bytes=mediaAssetBytes044B(asset);need(gifMagic(bytes),"Embedded GIF bytes do not start with a GIF header");need(iterationSha256(bytes)==asset.sha256,"Embedded GIF SHA-256 does not match its exact source bytes");
        need(asset.width&&asset.height&&asset.width<=2048&&asset.height<=2048&&asset.frames>=1&&asset.frames<=512,"Embedded GIF must fit 2048 pixels per side and 1..512 frames");
        if(decode){const auto frames=decodeGif044B(bytes);need(frames.width==asset.width&&frames.height==asset.height&&frames.frames.size()==asset.frames,"Embedded GIF metadata differs from its decoded frames");}
    }else{relativeVideoPath(asset.path);if(!asset.base64.empty()){const auto bytes=mediaAssetBytes044B(asset);need(mp4Magic(bytes)&&iterationSha256(bytes)==asset.sha256,"Embedded video bytes differ from the declared MP4 SHA-256");}}
}
MediaFrames044B decodeGif044B(const std::string& bytes){
    need(bytes.size()<=4u*1024u*1024u&&gifMagic(bytes),"GIF must be a GIF87a/GIF89a stream of at most 4 MiB");const unsigned w=(unsigned char)bytes[6]|((unsigned char)bytes[7]<<8),h=(unsigned char)bytes[8]|((unsigned char)bytes[9]<<8);need(w&&h&&w<=2048&&h<=2048,"GIF must fit 2048 pixels per side");
    int* delays=nullptr;int width=0,height=0,layers=0,channels=0;stbi_uc* data=stbi_load_gif_from_memory((const stbi_uc*)bytes.data(),(int)bytes.size(),&delays,&width,&height,&layers,&channels,4);
    struct Release{stbi_uc* data;int* delays;~Release(){if(data)stbi_image_free(data);if(delays)gifFree(delays);}} release{data,delays};
    need(data&&width>0&&height>0&&layers>=1,std::string("GIF decode failed: ")+(stbi_failure_reason()?stbi_failure_reason():"corrupt stream"));need(layers<=512,"GIF has more than 512 frames");
    MediaFrames044B out;out.width=(unsigned)width;out.height=(unsigned)height;const size_t frame=(size_t)width*height*4;
    for(int i=0;i<layers;++i){out.frames.emplace_back(data+frame*i,data+frame*(i+1));const int ms=delays?delays[i]:0;const double s=ms<=10?.1:ms/1000.;out.delaysS.push_back(s);out.durationS+=s;}return out;
}
size_t mediaFrameAt044B(const std::vector<double>& delays,double seconds,bool loop){
    if(delays.empty())return 0;double total=0;for(double d:delays)total+=d;if(!(total>0))return 0;double t=loop?modp(seconds,total):std::clamp(seconds,0.,total);if(!loop&&t>=total)return delays.size()-1;
    for(size_t i=0;i<delays.size();++i){if(t<delays[i])return i;t-=delays[i];}return delays.size()-1;
}
std::string encodeGif044B(unsigned width,unsigned height,const std::vector<std::array<unsigned char,3>>& palette,const std::vector<std::vector<unsigned char>>& frames,const std::vector<unsigned>& delays){
    need(width&&height&&width<=2048&&height<=2048&&!frames.empty()&&frames.size()==delays.size()&&palette.size()>=2&&palette.size()<=256,"Invalid GIF fixture");std::string out="GIF89a";
    auto u16=[&](unsigned v){out.push_back((char)(v&255));out.push_back((char)((v>>8)&255));};u16(width);u16(height);out.push_back((char)0xF7);out.push_back(0);out.push_back(0);
    for(size_t i=0;i<256;++i){const auto c=i<palette.size()?palette[i]:std::array<unsigned char,3>{0,0,0};for(unsigned char v:c)out.push_back((char)v);}
    out+=std::string("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00",19);
    for(size_t f=0;f<frames.size();++f){need(frames[f].size()==(size_t)width*height,"GIF fixture frame size mismatch");out+=std::string("\x21\xF9\x04\x04",4);u16(delays[f]);out.push_back(0);out.push_back(0);out.push_back((char)0x2C);u16(0);u16(0);u16(width);u16(height);out.push_back(0);out.push_back(8);
        std::string stream;unsigned buffer=0,bits=0;auto code=[&](unsigned c){buffer|=c<<bits;bits+=9;while(bits>=8){stream.push_back((char)(buffer&255));buffer>>=8;bits-=8;}};
        code(256);size_t run=0;for(unsigned char index:frames[f]){if(run==254){code(256);run=0;}code(index);++run;}code(257);if(bits)stream.push_back((char)(buffer&255));
        for(size_t i=0;i<stream.size();i+=255){const size_t n=std::min<size_t>(255,stream.size()-i);out.push_back((char)n);out.append(stream,i,n);}out.push_back(0);}
    out.push_back(0x3B);return out;
}
ImageAsset mediaAssetFromJson044B(const V& value){
    need(value.t==V::Obj,"Media asset must be an object");static const std::set<std::string> keys{"name","mime_type","data_base64","sha256","width","height","path","duration_s","frames"};for(const auto& item:value.obj)need(keys.count(item.first)!=0,"Unknown appearance field: "+item.first);
    auto text=[&](const char* key,bool required){const auto* found=value.find(key);if(!found){need(!required,std::string("Missing appearance field: ")+key);return std::string();}need(found->t==V::Str,std::string("Appearance field must be a string: ")+key);return found->str;};
    ImageAsset asset;asset.name=text("name",true);asset.mimeType=text("mime_type",true);asset.base64=text("data_base64",false);asset.path=text("path",false);assetName(asset.name);need(mediaMime044B(asset.mimeType),"Media asset MIME must be image/gif or video/mp4");
    auto number=[&](const char* key,double fallback){const auto* found=value.find(key);if(!found)return fallback;need(found->t==V::Num&&std::isfinite(found->num)&&found->num>=0,std::string("Invalid media ")+key);return found->num;};
    if(asset.mimeType=="image/gif"){need(!asset.base64.empty(),"An animated GIF asset embeds data_base64");const auto bytes=mediaAssetBytes044B(asset);const auto frames=decodeGif044B(bytes);asset.sha256=iterationSha256(bytes);asset.width=frames.width;asset.height=frames.height;asset.frames=(unsigned)frames.frames.size();asset.durationS=frames.durationS;
        if(const auto* hash=value.find("sha256"))need(hash->t==V::Str&&hash->str==asset.sha256,"Embedded GIF SHA-256 does not match source");
        for(const auto& field:std::initializer_list<std::pair<const char*,unsigned>>{{"width",asset.width},{"height",asset.height},{"frames",asset.frames}})if(const auto* expected=value.find(field.first))need(expected->t==V::Num&&expected->num==field.second,"Embedded GIF metadata does not match its decoded frames");
        if(const auto* expected=value.find("duration_s"))need(expected->t==V::Num&&std::abs(expected->num-asset.durationS)<1e-9,"Embedded GIF duration does not match its frame delays");
    }else{asset.sha256=text("sha256",false);if(!asset.base64.empty()){const auto bytes=mediaAssetBytes044B(asset);const auto actual=iterationSha256(bytes);need(asset.sha256.empty()||asset.sha256==actual,"Embedded video SHA-256 does not match source");asset.sha256=actual;}
        asset.width=(unsigned)number("width",0);asset.height=(unsigned)number("height",0);asset.durationS=number("duration_s",0);asset.frames=(unsigned)number("frames",0);}
    validateMediaAsset044B(asset,false);return asset;
}
bool mediaAssetPathRequest044B(const std::string& path){auto extension=utf8(fs::u8path(path).extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});return extension==".gif"||extension==".mp4"||extension==".m4v";}
ImageAsset mediaAssetFromPath044B(const std::string& name,const std::string& path,bool embed,const std::string& directory){
    assetName(name);auto file=fs::u8path(path);if(!directory.empty()&&file.is_relative())file=fs::u8path(directory)/file;ImageAsset asset;asset.name=name;
    auto extension=utf8(file.extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});
    if(extension==".gif"){const auto bytes=readBounded(file,4u*1024u*1024u);V source=V::makeObj();source.set("name",V::makeStr(name));source.set("mime_type",V::makeStr("image/gif"));source.set("data_base64",V::makeStr(base64Encode(bytes)));return mediaAssetFromJson044B(source);}
    need(extension==".mp4"||extension==".m4v","Media import supports .gif, .mp4 and .m4v");const auto bytes=readBounded(file,512u*1024u*1024u);need(mp4Magic(bytes),"Video file is not an MP4 container");
    asset.mimeType="video/mp4";asset.path=path;if(!directory.empty()){std::error_code error;const auto relative=fs::relative(file,fs::u8path(directory),error);need(!error&&!relative.empty()&&!relative.has_root_name()&&!relative.has_root_directory(),"The video must be on the same volume as the document so it can be stored by a relative path");const auto generic=relative.generic_u8string();asset.path=std::string(generic.begin(),generic.end());}asset.sha256=iterationSha256(bytes);if(embed){need(bytes.size()<=2u*1024u*1024u,"Embedded video must be at most 2 MiB; keep larger clips beside the document");asset.base64=base64Encode(bytes);}
    try{const auto info=probeVideo044B(utf8(file));asset.width=info.width;asset.height=info.height;asset.durationS=info.durationS;asset.frames=(unsigned)std::llround(info.durationS*info.fps);}catch(const std::exception&){} // probe is optional metadata; playback reports its own status
    validateMediaAsset044B(asset,false);return asset;
}
void mediaAssetToJson044B(const ImageAsset& asset,bool includePixels,V& out){
    if(!asset.path.empty())out.set("path",V::makeStr(asset.path));if(asset.durationS>0)out.set("duration_s",V::makeNum(asset.durationS));if(asset.frames)out.set("frames",V::makeNum(asset.frames));
    if(!includePixels||asset.base64.empty())out.obj.erase(std::remove_if(out.obj.begin(),out.obj.end(),[](const auto& item){return item.first=="data_base64";}),out.obj.end());
}
void stampPlaybackFromJson044B(ProjectedStamp& stamp,const V& value){
    for(const char* key:{"loop","autoplay"})if(const auto* flag=value.find(key)){need(flag->t==V::Bool,std::string(key)+" must be a boolean");(std::string(key)=="loop"?stamp.loop:stamp.autoplay)=flag->b;}
    if(const auto* speed=value.find("speed")){need(speed->t==V::Num&&std::isfinite(speed->num)&&speed->num>0&&speed->num<=16,"Stamp playback speed must be in (0, 16]");stamp.speed=speed->num;}
    if(const auto* start=value.find("start_s")){need(start->t==V::Num&&std::isfinite(start->num)&&start->num>=0&&start->num<=86400,"Stamp start_s must be 0..86400 seconds");stamp.startS=start->num;}
}
void stampPlaybackToJson044B(const ProjectedStamp& stamp,V& out){if(!stamp.loop)out.set("loop",V::makeBool(false));if(!stamp.autoplay)out.set("autoplay",V::makeBool(false));if(stamp.speed!=1)out.set("speed",V::makeNum(stamp.speed));if(stamp.startS!=0)out.set("start_s",V::makeNum(stamp.startS));}
bool media044BNeedsVersion47(const Document& document){
    for(const auto& asset:document.imageAssets)if(mediaMime044B(asset.mimeType))return true;for(const auto& stamp:document.stamps)if(!stamp.loop||!stamp.autoplay||stamp.speed!=1||stamp.startS!=0)return true;
    for(const auto& material:document.materials)if(appearancePatternActive044B(material.appearance))return true;
    auto patterned=[](const V& patch){if(patch.t==V::Obj)for(const auto& field:patch.obj)if(field.first.rfind("pattern",0)==0)return true;return false;};
    for(const auto& feature:document.features){if(patterned(feature.appearance))return true;for(const auto& face:feature.faceAppearances)if(patterned(face.appearance))return true;}return false;
}
double mediaPlaybackPosition044B(const ProjectedStamp* playback,double elapsed,double duration){
    const double start=playback?playback->startS:0,speed=playback?playback->speed:1;const bool loop=playback?playback->loop:true,autoplay=playback?playback->autoplay:true;
    const double position=start+(autoplay?std::max(elapsed,0.)*speed:0);if(!(duration>0))return position;return loop?modp(position,duration):std::min(position,duration);
}

#ifdef _WIN32
namespace {
template<class T>struct Com {T* p=nullptr;Com()=default;~Com(){if(p)p->Release();}Com(const Com&)=delete;Com& operator=(const Com&)=delete;T** put(){if(p){p->Release();p=nullptr;}return &p;}T* operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}};
struct ComScope {bool owned=false;ComScope(){owned=SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED));}~ComScope(){if(owned)CoUninitialize();}};
std::string hresult(HRESULT hr){char text[16];std::snprintf(text,sizeof text,"0x%08lX",(unsigned long)hr);return text;}
struct MediaFoundation {
    bool ready=false;std::string error;
    decltype(&MFStartup) startup=nullptr;decltype(&MFCreateMediaType) mediaType=nullptr;decltype(&MFCreateAttributes) attributes=nullptr;decltype(&MFCreateMemoryBuffer) memoryBuffer=nullptr;decltype(&MFCreateSample) sample=nullptr;
    decltype(&MFCreateSourceReaderFromURL) reader=nullptr;decltype(&MFCreateSinkWriterFromURL) writer=nullptr;
};
const MediaFoundation& mediaFoundation(){
    static const MediaFoundation api=[]{MediaFoundation m;const HMODULE plat=LoadLibraryW(L"mfplat.dll"),rw=LoadLibraryW(L"mfreadwrite.dll");if(!plat||!rw){m.error="VIDEO_UNSUPPORTED: Windows Media Foundation is not installed (for example a Windows N edition without the Media Feature Pack)";return m;}
        auto symbol=[](HMODULE module,const char* name){return reinterpret_cast<void*>(GetProcAddress(module,name));};
        m.startup=reinterpret_cast<decltype(&MFStartup)>(symbol(plat,"MFStartup"));m.mediaType=reinterpret_cast<decltype(&MFCreateMediaType)>(symbol(plat,"MFCreateMediaType"));m.attributes=reinterpret_cast<decltype(&MFCreateAttributes)>(symbol(plat,"MFCreateAttributes"));m.memoryBuffer=reinterpret_cast<decltype(&MFCreateMemoryBuffer)>(symbol(plat,"MFCreateMemoryBuffer"));m.sample=reinterpret_cast<decltype(&MFCreateSample)>(symbol(plat,"MFCreateSample"));
        m.reader=reinterpret_cast<decltype(&MFCreateSourceReaderFromURL)>(symbol(rw,"MFCreateSourceReaderFromURL"));m.writer=reinterpret_cast<decltype(&MFCreateSinkWriterFromURL)>(symbol(rw,"MFCreateSinkWriterFromURL"));
        if(!m.startup||!m.mediaType||!m.attributes||!m.memoryBuffer||!m.sample||!m.reader||!m.writer){m.error="VIDEO_UNSUPPORTED: Media Foundation entry points are missing";return m;}
        const HRESULT hr=m.startup(MF_VERSION,MFSTARTUP_FULL);if(FAILED(hr)){m.error="VIDEO_UNSUPPORTED: MFStartup failed "+hresult(hr);return m;}m.ready=true;return m;}();
    return api;
}
const MediaFoundation& requireMediaFoundation(){const auto& mf=mediaFoundation();if(!mf.ready)throw std::runtime_error(mf.error);return mf;}
// One opened RGB32 video stream (Media Foundation video processor converts YUV).
struct VideoReader {
    Com<IMFSourceReader> reader;unsigned width=0,height=0;LONG stride=0;double duration=0,fps=0;
    explicit VideoReader(const std::string& path){
        const auto& mf=requireMediaFoundation();Com<IMFAttributes> attributes;HRESULT hr=mf.attributes(attributes.put(),2);if(FAILED(hr))throw std::runtime_error("VIDEO_DECODE_FAILED: attributes "+hresult(hr));attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
        const auto wide=fs::u8path(path).wstring();hr=mf.reader(wide.c_str(),attributes.p,reader.put());if(FAILED(hr))throw std::runtime_error("VIDEO_DECODE_FAILED: cannot open the video ("+hresult(hr)+")");
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS,FALSE);reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE);
        Com<IMFMediaType> type;mf.mediaType(type.put());type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);hr=reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.p);if(FAILED(hr))throw std::runtime_error("VIDEO_UNSUPPORTED: no decoder converts this stream to RGB32 ("+hresult(hr)+")");
        refreshType();PROPVARIANT value;PropVariantInit(&value);if(SUCCEEDED(reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE,MF_PD_DURATION,&value))&&value.vt==VT_UI8)duration=(double)value.uhVal.QuadPart/1e7;PropVariantClear(&value);
    }
    void refreshType(){Com<IMFMediaType> current;HRESULT hr=reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,current.put());if(FAILED(hr))throw std::runtime_error("VIDEO_DECODE_FAILED: no output type");UINT32 w=0,h=0;MFGetAttributeSize(current.p,MF_MT_FRAME_SIZE,&w,&h);need(w&&h&&w<=8192&&h<=8192,"VIDEO_UNSUPPORTED: frame size is outside 1..8192");width=w;height=h;
        UINT32 strideValue=0;stride=SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE,&strideValue))?(LONG)strideValue:(LONG)(w*4);UINT32 num=0,den=0;if(SUCCEEDED(MFGetAttributeRatio(current.p,MF_MT_FRAME_RATE,&num,&den))&&den)fps=(double)num/den;}
    void seek(double seconds){PROPVARIANT position;PropVariantInit(&position);position.vt=VT_I8;position.hVal.QuadPart=(LONGLONG)std::llround(std::max(seconds,0.)*1e7);const HRESULT hr=reader->SetCurrentPosition(GUID{},position);PropVariantClear(&position);if(FAILED(hr))throw std::runtime_error("VIDEO_DECODE_FAILED: seek "+hresult(hr));}
    // Next decoded frame (RGBA8, top row first, box-downscaled to maxSide), or false at end of stream.
    bool read(MediaImage044B& out,double& seconds,unsigned maxSide){
        for(int gaps=0;gaps<4096;++gaps){
            DWORD stream=0,flags=0;LONGLONG time=0;Com<IMFSample> sample;const HRESULT hr=reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&stream,&flags,&time,sample.put());if(FAILED(hr))throw std::runtime_error("VIDEO_DECODE_FAILED: "+hresult(hr));
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM)return false;if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)refreshType();if(!sample)continue;
            Com<IMFMediaBuffer> buffer;if(FAILED(sample->ConvertToContiguousBuffer(buffer.put())))throw std::runtime_error("VIDEO_DECODE_FAILED: sample buffer");
            BYTE* scan=nullptr;LONG pitch=0;Com<IMF2DBuffer> planar;bool locked2d=false;BYTE* data=nullptr;DWORD maxLength=0,length=0;
            if(SUCCEEDED(buffer->QueryInterface(__uuidof(IMF2DBuffer),reinterpret_cast<void**>(planar.put())))&&SUCCEEDED(planar->Lock2D(&scan,&pitch)))locked2d=true;
            else{if(FAILED(buffer->Lock(&data,&maxLength,&length)))throw std::runtime_error("VIDEO_DECODE_FAILED: buffer lock");pitch=stride;scan=pitch<0?data+(size_t)(height-1)*(size_t)(-pitch):data;if((size_t)std::abs(pitch)*height>length){buffer->Unlock();throw std::runtime_error("VIDEO_DECODE_FAILED: truncated frame");}}
            const unsigned factor=std::max(1u,(std::max(width,height)+maxSide-1)/std::max(maxSide,1u));out.width=width/factor;out.height=height/factor;out.rgba.assign((size_t)out.width*out.height*4,255);
            for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x){unsigned sum[3]={0,0,0};for(unsigned dy=0;dy<factor;++dy){const BYTE* row=scan+(ptrdiff_t)(y*factor+dy)*pitch;for(unsigned dx=0;dx<factor;++dx){const BYTE* px=row+(size_t)(x*factor+dx)*4;sum[0]+=px[2];sum[1]+=px[1];sum[2]+=px[0];}}
                unsigned char* q=&out.rgba[((size_t)y*out.width+x)*4];for(int c=0;c<3;++c)q[c]=(unsigned char)(sum[c]/(factor*factor));}
            if(locked2d)planar->Unlock2D();else buffer->Unlock();seconds=(double)time/1e7;return true;
        }
        throw std::runtime_error("VIDEO_DECODE_FAILED: stream produced no samples");
    }
};
}
std::string videoDecoderStatus044B(){const auto& mf=mediaFoundation();return mf.ready?"media_foundation":mf.error;}
VideoInfo044B probeVideo044B(const std::string& path){ComScope com;VideoReader reader(path);VideoInfo044B info;info.width=reader.width;info.height=reader.height;info.durationS=reader.duration;info.fps=reader.fps;return info;}
MediaImage044B videoFrameAt044B(const std::string& path,double seconds,unsigned maxSide){
    ComScope com;VideoReader reader(path);if(seconds>0)reader.seek(seconds);MediaImage044B previous,current;double time=0;bool have=false;
    while(reader.read(current,time,maxSide)){if(have&&time>seconds+1e-6)return previous;previous=std::move(current);have=true;if(time>=seconds-1e-6&&reader.fps>0&&time+1/reader.fps>seconds)return previous;}
    need(have,"VIDEO_DECODE_FAILED: the video has no decodable frame at or before "+std::to_string(seconds)+" s");return previous;
}
bool writeVideoClip044B(const std::string& path,unsigned width,unsigned height,double fps,const std::vector<std::vector<unsigned char>>& frames,std::string& error){
    try{
        const auto& mf=requireMediaFoundation();ComScope com;need(width&&height&&width%2==0&&height%2==0&&fps>0&&!frames.empty(),"Video clip needs even dimensions, fps and frames");
        Com<IMFSinkWriter> writer;const auto wide=fs::u8path(path).wstring();HRESULT hr=mf.writer(wide.c_str(),nullptr,nullptr,writer.put());if(FAILED(hr))throw std::runtime_error("VIDEO_ENCODER_UNAVAILABLE: sink writer "+hresult(hr));
        Com<IMFMediaType> output,input;mf.mediaType(output.put());output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);output->SetUINT32(MF_MT_AVG_BITRATE,4000000);output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        const UINT32 rate=(UINT32)std::llround(fps*1000);MFSetAttributeSize(output.p,MF_MT_FRAME_SIZE,width,height);MFSetAttributeRatio(output.p,MF_MT_FRAME_RATE,rate,1000);MFSetAttributeRatio(output.p,MF_MT_PIXEL_ASPECT_RATIO,1,1);
        DWORD index=0;hr=writer->AddStream(output.p,&index);if(FAILED(hr))throw std::runtime_error("VIDEO_ENCODER_UNAVAILABLE: no H.264 encoder ("+hresult(hr)+")");
        mf.mediaType(input.put());input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);MFSetAttributeSize(input.p,MF_MT_FRAME_SIZE,width,height);MFSetAttributeRatio(input.p,MF_MT_FRAME_RATE,rate,1000);MFSetAttributeRatio(input.p,MF_MT_PIXEL_ASPECT_RATIO,1,1);input->SetUINT32(MF_MT_DEFAULT_STRIDE,(UINT32)(width*4));
        hr=writer->SetInputMediaType(index,input.p,nullptr);if(FAILED(hr))throw std::runtime_error("VIDEO_ENCODER_UNAVAILABLE: RGB32 input rejected ("+hresult(hr)+")");if(FAILED(hr=writer->BeginWriting()))throw std::runtime_error("VIDEO_ENCODER_UNAVAILABLE: begin "+hresult(hr));
        const LONGLONG duration=(LONGLONG)std::llround(1e7/fps);
        for(size_t f=0;f<frames.size();++f){need(frames[f].size()==(size_t)width*height*4,"Video clip frame size mismatch");Com<IMFMediaBuffer> buffer;if(FAILED(mf.memoryBuffer(width*height*4,buffer.put())))throw std::runtime_error("Video buffer allocation failed");BYTE* data=nullptr;buffer->Lock(&data,nullptr,nullptr);
            for(size_t i=0;i<(size_t)width*height;++i){data[i*4]=frames[f][i*4+2];data[i*4+1]=frames[f][i*4+1];data[i*4+2]=frames[f][i*4];data[i*4+3]=255;}buffer->Unlock();buffer->SetCurrentLength(width*height*4);
            Com<IMFSample> sample;mf.sample(sample.put());sample->AddBuffer(buffer.p);sample->SetSampleTime((LONGLONG)f*duration);sample->SetSampleDuration(duration);if(FAILED(hr=writer->WriteSample(index,sample.p)))throw std::runtime_error("Video sample write failed "+hresult(hr));}
        if(FAILED(hr=writer->Finalize()))throw std::runtime_error("Video finalize failed "+hresult(hr));return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
struct VideoStream044B::State {
    std::string path;double start=0;bool loop=true;unsigned maxSide=1024;size_t capacity=4;std::mutex mutex;std::condition_variable wake;bool stop=false;std::string status="opening";double duration=0;
    struct Frame {double unwrapped;MediaImage044B image;};std::deque<Frame> queue;std::thread worker;
};
VideoStream044B::VideoStream044B(const std::string& path,double startS,bool loop,unsigned maxSide,size_t queue):state_(std::make_shared<State>()){
    state_->path=path;state_->start=std::max(startS,0.);state_->loop=loop;state_->maxSide=std::max(maxSide,16u);state_->capacity=std::clamp<size_t>(queue,2,16);auto state=state_;
    state_->worker=std::thread([state]{
        try{ComScope com;VideoReader reader(state->path);{std::lock_guard<std::mutex> lock(state->mutex);state->duration=reader.duration;state->status="playing";}
            if(state->start>0)reader.seek(state->start);int lap=0;size_t lapFrames=0;
            for(;;){{std::unique_lock<std::mutex> lock(state->mutex);state->wake.wait(lock,[&]{return state->stop||state->queue.size()<state->capacity;});if(state->stop)return;}
                MediaImage044B image;double time=0;
                if(!reader.read(image,time,state->maxSide)){if(!state->loop||!(reader.duration>0)||lapFrames==0){std::unique_lock<std::mutex> lock(state->mutex);state->status=lapFrames?"ended":"VIDEO_DECODE_FAILED: no frames";state->wake.wait(lock,[&]{return state->stop;});return;}reader.seek(0);++lap;lapFrames=0;continue;}
                ++lapFrames;std::lock_guard<std::mutex> lock(state->mutex);state->queue.push_back({lap*reader.duration+time,std::move(image)});state->wake.notify_all();}
        }catch(const std::exception& error){std::lock_guard<std::mutex> lock(state->mutex);state->status=error.what();}
    });
}
VideoStream044B::~VideoStream044B(){{std::lock_guard<std::mutex> lock(state_->mutex);state_->stop=true;}state_->wake.notify_all();if(state_->worker.joinable())state_->worker.join();}
bool VideoStream044B::frameFor(double target,MediaImage044B& out,double* frameSeconds){
    std::lock_guard<std::mutex> lock(state_->mutex);bool found=false;while(!state_->queue.empty()&&state_->queue.front().unwrapped<=target+1e-6){out=std::move(state_->queue.front().image);if(frameSeconds)*frameSeconds=state_->queue.front().unwrapped;state_->queue.pop_front();found=true;}
    if(found)state_->wake.notify_all();return found;
}
std::string VideoStream044B::status() const{std::lock_guard<std::mutex> lock(state_->mutex);return state_->status;}
double VideoStream044B::durationS() const{std::lock_guard<std::mutex> lock(state_->mutex);return state_->duration;}
#else
std::string videoDecoderStatus044B(){return "VIDEO_UNSUPPORTED: video playback uses Windows Media Foundation and is unavailable on this platform";}
VideoInfo044B probeVideo044B(const std::string&){throw std::runtime_error(videoDecoderStatus044B());}
MediaImage044B videoFrameAt044B(const std::string&,double,unsigned){throw std::runtime_error(videoDecoderStatus044B());}
bool writeVideoClip044B(const std::string&,unsigned,unsigned,double,const std::vector<std::vector<unsigned char>>&,std::string& error){error=videoDecoderStatus044B();return false;}
struct VideoStream044B::State {};
VideoStream044B::VideoStream044B(const std::string&,double,bool,unsigned,size_t):state_(std::make_shared<State>()){}
VideoStream044B::~VideoStream044B()=default;
bool VideoStream044B::frameFor(double,MediaImage044B&,double*){return false;}
std::string VideoStream044B::status() const{return videoDecoderStatus044B();}
double VideoStream044B::durationS() const{return 0;}
#endif

std::string resolvedVideoPath044B(const ImageAsset& asset,const std::string& directory,std::string& status){
    status="missing";if(asset.mimeType!="video/mp4")return {};bool mismatch=false;std::vector<fs::path> candidates;const auto relative=fs::u8path(asset.path);
    if(!asset.path.empty()){if(!directory.empty())candidates.push_back(fs::u8path(directory)/relative);candidates.push_back(relative);}
    for(const auto& candidate:candidates){std::error_code error;if(!fs::is_regular_file(candidate,error))continue;try{if(iterationSha256(readBounded(candidate,512u*1024u*1024u))==asset.sha256){status="ok";return utf8(candidate);}mismatch=true;}catch(const std::exception&){}}
    if(!asset.base64.empty()){const auto bytes=mediaAssetBytes044B(asset);if(iterationSha256(bytes)==asset.sha256){const auto file=fs::temp_directory_path()/("ark-media-"+asset.sha256.substr(0,24)+".mp4");std::error_code error;if(!fs::exists(file,error)||fs::file_size(file,error)!=bytes.size()){std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(bytes.data(),(std::streamsize)bytes.size());}status="ok_embedded";return utf8(file);}}
    if(mismatch)status="hash_mismatch";return {};
}

namespace {
struct StampLayer044B {Vec3 origin,u,v,normal;double width=1,height=1,depth=1,opacity=1;bool front=true;std::shared_ptr<const MediaImage044B> image;};
Vec3 sample(const MediaImage044B& image,double s,double t,bool repeat,double* alpha){
    // GL convention: t=0 is the bottom row; image rows are stored top first.
    double x=s*image.width-.5,y=(1-t)*image.height-.5;auto wrap=[&](long i,long n){return repeat?(long)modp((double)i,(double)n):std::clamp(i,0L,n-1);};
    const long x0=(long)std::floor(x),y0=(long)std::floor(y);const double fx=x-x0,fy=y-y0;double rgba[4]={0,0,0,0};
    for(int k=0;k<4;++k){const long xi=wrap(x0+(k&1),image.width),yi=wrap(y0+(k>>1),image.height);const double weight=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy);const unsigned char* p=&image.rgba[((size_t)yi*image.width+xi)*4];for(int c=0;c<4;++c)rgba[c]+=p[c]/255.*weight;}
    if(alpha)*alpha=rgba[3];return {rgba[0],rgba[1],rgba[2]};
}
class SurfaceShader044B final:public SnapshotShader {
public:
    Mat4 worldToLocal=Mat4::identity();Appearance appearance;bool patterned=false;std::shared_ptr<const MediaImage044B> texture;std::vector<StampLayer044B> stamps;
    Vec3 shade(const Vec3& world,const Vec3& worldNormal,const Vec3& color) const override{
        const Vec3 p=worldToLocal.transformPoint(world),n=worldToLocal.transformDir(worldNormal).normalized();Vec3 c=color;
        if(texture&&texture->width&&texture->height){double w[3]={std::pow(std::abs(n.x),4.),std::pow(std::abs(n.y),4.),std::pow(std::abs(n.z),4.)};const double sum=std::max(w[0]+w[1]+w[2],1e-5);const double su=appearance.textureScaleU/100,sv=appearance.textureScaleV/100;
            const Vec3 texel=(sample(*texture,p.x*su,p.y*sv,true,nullptr)*w[2]+sample(*texture,p.x*su,p.z*sv,true,nullptr)*w[1]+sample(*texture,p.y*su,p.z*sv,true,nullptr)*w[0])*(1/sum);c={c.x*texel.x,c.y*texel.y,c.z*texel.z};}
        if(patterned)c=appearancePatternColor044B(appearance,c,appearancePatternTriplanar044B(appearance,p,n));
        for(const auto& layer:stamps){if(!layer.image||!layer.image->width)continue;const Vec3 d=p-layer.origin;const double s=d.dot(layer.u)/layer.width+.5,t=d.dot(layer.v)/layer.height+.5;if(s<0||s>1||t<0||t>1||std::abs(d.dot(layer.normal))>layer.depth||(layer.front&&n.dot(layer.normal)<=0))continue;
            double alpha=0;const Vec3 texel=sample(*layer.image,s,t,false,&alpha);const double a=std::clamp(alpha*layer.opacity,0.,1.);c=c*(1-a)+texel*a;}
        return c;
    }
};
void tally(V* receipt,const char* key){if(!receipt)return;const auto* value=receipt->find(key);receipt->set(key,V::makeNum((value?value->num:0)+1));}
void row(V* receipt,const char* list,V item){if(!receipt)return;V rows=receipt->find(list)?*receipt->find(list):V::makeArr();if(rows.arr.size()<64)rows.arr.push_back(std::move(item));receipt->set(list,rows);}
std::shared_ptr<const MediaImage044B> assetImage(const Document& document,const std::string& name,const ProjectedStamp* playback,SnapshotAppearanceOptions044B& options){
    const auto* asset=findImageAsset(document,name);V status=V::makeObj();status.set("asset",V::makeStr(name));
    if(!asset){status.set("status",V::makeStr("missing_asset"));row(options.receipt,"media",status);return nullptr;}status.set("mime_type",V::makeStr(asset->mimeType));
    const double position=mediaPlaybackPosition044B(playback,options.mediaTimeS,asset->durationS);const std::string key=asset->sha256+"@"+std::to_string(mediaMime044B(asset->mimeType)?position:0.);
    if(const auto found=options.images.find(key);found!=options.images.end())return found->second;
    auto image=std::make_shared<MediaImage044B>();
    try{
        if(asset->mimeType=="image/gif"){const auto frames=decodeGif044B(mediaAssetBytes044B(*asset));const size_t index=mediaFrameAt044B(frames.delaysS,position,playback?playback->loop:true);image->width=frames.width;image->height=frames.height;image->rgba=frames.frames[index];status.set("frame",V::makeNum((double)index));}
        else if(asset->mimeType=="video/mp4"){std::string located;const auto path=resolvedVideoPath044B(*asset,options.documentDirectory,located);status.set("source",V::makeStr(located));if(path.empty())throw std::runtime_error("VIDEO_"+std::string(located=="hash_mismatch"?"HASH_MISMATCH: the file beside the document differs from the saved SHA-256":"MISSING: "+asset->path+" was not found beside the document"));*image=videoFrameAt044B(path,position,1024);}
        else{const auto pixels=decodeImageAssetRGBA(*asset);image->width=asset->width;image->height=asset->height;image->rgba=pixels;}
        if(mediaMime044B(asset->mimeType)){status.set("status",V::makeStr("ok"));status.set("position_s",V::makeNum(position));row(options.receipt,"media",status);}
    }catch(const std::exception& error){status.set("status",V::makeStr("unavailable"));status.set("error",V::makeStr(error.what()));row(options.receipt,"media",status);image=nullptr;}
    options.images[key]=image;return image;
}
std::shared_ptr<const MediaImage044B> pathImage(const std::string& path,SnapshotAppearanceOptions044B& options){
    const std::string key="path:"+path;if(const auto found=options.images.find(key);found!=options.images.end())return found->second;std::shared_ptr<MediaImage044B> image;
    try{auto file=fs::u8path(path);if(file.is_relative()&&!options.documentDirectory.empty()&&fs::exists(fs::u8path(options.documentDirectory)/file))file=fs::u8path(options.documentDirectory)/file;const auto asset=imageAssetFromPath("texture",utf8(file));image=std::make_shared<MediaImage044B>();image->width=asset.width;image->height=asset.height;image->rgba=decodeImageAssetRGBA(asset);}
    catch(const std::exception& error){V item=V::makeObj();item.set("texture_path",V::makeStr(path));item.set("error",V::makeStr(error.what()));row(options.receipt,"missing_textures",item);}
    options.images[key]=image;return image;
}
std::shared_ptr<SurfaceShader044B> surfaceFor(const Document& document,const Appearance& appearance,SnapshotAppearanceOptions044B& options){
    auto shader=std::make_shared<SurfaceShader044B>();shader->appearance=appearance;shader->patterned=appearancePatternKind044B(appearance.pattern)>0;
    if(!appearance.textureAsset.empty())shader->texture=assetImage(document,appearance.textureAsset,nullptr,options);else if(!appearance.texturePath.empty())shader->texture=pathImage(appearance.texturePath,options);
    if(!shader->texture&&!shader->patterned)return nullptr;if(shader->texture)tally(options.receipt,"textured_surfaces");if(shader->patterned)tally(options.receipt,"patterned_surfaces");return shader;
}
}
void attachSnapshotSurfaces044B(const Document& document,const Body& body,const Mesh& mesh,std::vector<SnapshotMaterial>& materials,SnapshotAppearanceOptions044B& options){
    const auto* feature=document.find(body.id);if(!feature||materials.size()!=mesh.triCount())return;
    const auto base=surfaceFor(document,resolvedAppearance(document,*feature),options);for(auto& material:materials)material.shader=base;
    const auto* nativeMesh=body.nativeData.meshMetadata();const auto* spans=nativeMesh?nativeMesh->find("face_spans"):nullptr;
    auto span=[&](int index,size_t& start,size_t& count){need(spans&&spans->t==V::Arr&&index>=0&&(size_t)index<spans->arr.size(),"Snapshot surface span is unavailable");const auto& item=spans->arr[(size_t)index];need(item.t==V::Arr&&item.arr.size()==2,"Snapshot surface span is invalid");start=(size_t)item.arr[0].num;count=(size_t)item.arr[1].num;need(start+count<=materials.size(),"Snapshot surface span is out of range");};
    for(const auto& patch:feature->faceAppearances){size_t start=0,n=0;span(appearanceFaceIndex024B(document,feature->id,patch.face,patch.sourceTopology),start,n);const auto shader=surfaceFor(document,resolvedFaceAppearance(document,*feature,patch.face),options);for(size_t i=start;i<start+n;++i)materials[i].shader=shader;}
    for(const auto& stamp:document.stamps){
        if(stamp.feature!=feature->id)continue;StampLayer044B layer;try{const auto projector=resolvedStampProjector(document,stamp);layer.origin=projector.origin;layer.u=projector.uAxis;layer.v=projector.vAxis;layer.normal=projector.normal;layer.width=projector.width;layer.height=projector.height;layer.depth=projector.depth;layer.opacity=projector.opacity;layer.front=projector.frontOnly;}
        catch(const std::exception& error){V item=V::makeObj();item.set("stamp",V::makeStr(stamp.name));item.set("error",V::makeStr(error.what()));row(options.receipt,"stamps_skipped",item);continue;}
        layer.image=assetImage(document,stamp.asset,&stamp,options);if(!layer.image){V item=V::makeObj();item.set("stamp",V::makeStr(stamp.name));item.set("error",V::makeStr("stamp image unavailable"));row(options.receipt,"stamps_skipped",item);continue;}
        std::map<const SnapshotShader*,std::shared_ptr<const SnapshotShader>> clones;
        for(const auto& face:stamp.faces){size_t start=0,n=0;span(appearanceFaceIndex024B(document,feature->id,face,stamp.sourceTopology),start,n);
            for(size_t i=start;i<start+n;++i){auto& clone=clones[materials[i].shader.get()];if(!clone){auto copy=materials[i].shader?std::make_shared<SurfaceShader044B>(*std::static_pointer_cast<const SurfaceShader044B>(materials[i].shader)):std::make_shared<SurfaceShader044B>();copy->stamps.push_back(layer);clone=copy;}materials[i].shader=clone;}}
        tally(options.receipt,"stamps_drawn");
    }
}
void placeSnapshotMaterials044B(std::vector<SnapshotMaterial>& materials,size_t first,const Mat4& world){
    const Mat4 inverse=world.inverted();std::map<const SnapshotShader*,std::shared_ptr<const SnapshotShader>> placed;
    for(size_t i=first;i<materials.size();++i){if(!materials[i].shader)continue;auto& copy=placed[materials[i].shader.get()];if(!copy){auto shader=std::make_shared<SurfaceShader044B>(*std::static_pointer_cast<const SurfaceShader044B>(materials[i].shader));shader->worldToLocal=inverse;copy=shader;}materials[i].shader=copy;}
}
}
