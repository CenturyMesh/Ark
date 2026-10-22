#include "ark/media_authoring.hpp"
#include "ark/media_044b.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/assistant_credentials.hpp"
#include "ark/iteration_history.hpp"
#include "ark/io.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ark {
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
thread_local const std::atomic_bool* cancellation=nullptr;
void need(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
void cancelled(){need(!cancellation||!cancellation->load(),"MEDIA_CANCELLED: generation stopped before publication");}
std::string text(const V& v,const char* key,const std::string& fallback={}){const auto* p=v.find(key);if(!p)return fallback;need(p->t==V::Str&&!p->str.empty(),std::string(key)+" must be nonempty text");return p->str;}
void keys(const V& v,const std::set<std::string>& allowed){need(v.t==V::Obj,"Media arguments must be an object");for(const auto& p:v.obj)need(allowed.count(p.first)!=0,"Unknown media field: "+p.first);}
double number(const V& v,const char* key,const ExprEnv& env,double fallback){const auto* p=v.find(key);if(!p)return fallback;bool ok=p->t==V::Num;double value=ok?p->num:0;if(p->t==V::Str){need(p->str.size()<=512,"Media expression exceeds 512 bytes");value=evalExpr(p->str,env,&ok);}need(ok&&std::isfinite(value)&&std::abs(value)<=1e6,std::string("Invalid media number/expression: ")+key);return value;}
unsigned integer(const V& v,const char* key,unsigned fallback,unsigned minimum,unsigned maximum){double n=number(v,key,{},fallback);need(n>=minimum&&n<=maximum&&std::floor(n)==n,std::string(key)+" is outside the integer range "+std::to_string(minimum)+".."+std::to_string(maximum));return (unsigned)n;}
using Color=std::array<unsigned char,3>;
Color color(const V& v,const char* key,const char* fallback){auto s=text(v,key,fallback);need(s.size()==7&&s[0]=='#'&&s.find_first_not_of("0123456789abcdefABCDEF",1)==std::string::npos,"Media colors use #RRGGBB");return {(unsigned char)std::stoul(s.substr(1,2),nullptr,16),(unsigned char)std::stoul(s.substr(3,2),nullptr,16),(unsigned char)std::stoul(s.substr(5,2),nullptr,16)};}
std::string pathUtf8(const fs::path& path){auto s=path.u8string();return std::string(s.begin(),s.end());}
std::string readBytes(const fs::path& path,size_t capacity=16*1024*1024){need(fs::is_regular_file(path)&&fs::file_size(path)<=capacity,"Media file missing or exceeds capacity");std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});}
struct Output {
    fs::path destination,temporary;bool published=false;
    explicit Output(const std::string& path,const std::string& extension){need(!path.empty()&&path.size()<2048,"A local output path is required");destination=fs::absolute(fs::u8path(path)).lexically_normal();need(destination.extension()==fs::path(extension),"Output extension must be "+extension);need(!fs::exists(destination)&&!fs::is_symlink(destination),"MEDIA_OUTPUT_EXISTS: choose a new output path");need(fs::is_directory(destination.parent_path()),"Output directory does not exist");static std::atomic_ullong serial{0};temporary=destination.parent_path()/(".ark-media-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial)+extension);need(!fs::exists(temporary)&&!fs::is_symlink(temporary),"Media staging name already exists");}
    ~Output(){std::error_code ignored;fs::remove(temporary,ignored);}
    void write(const std::string& bytes){std::ofstream out(temporary,std::ios::binary);need((bool)out,"Cannot create staged media");out.write(bytes.data(),(std::streamsize)bytes.size());out.close();need((bool)out,"Cannot finish staged media");}
    void publish(){cancelled();(void)readBytes(temporary);std::error_code error;fs::create_hard_link(temporary,destination,error);need(!error,"MEDIA_PUBLICATION_FAILED: destination exists or the filesystem cannot publish an exclusive hard link");published=true;}
    std::string path()const{auto s=destination.u8string();return std::string(s.begin(),s.end());}
};
V receipt(const Output& out,unsigned w,unsigned h,unsigned frames,double duration,const char* mime){auto bytes=readBytes(out.destination);V r=V::makeObj();r.set("path",V::makeStr(out.path()));r.set("mime_type",V::makeStr(mime));r.set("sha256",V::makeStr(iterationSha256(bytes)));r.set("bytes",V::makeNum((double)bytes.size()));r.set("width",V::makeNum(w));r.set("height",V::makeNum(h));r.set("frames",V::makeNum(frames));r.set("duration_s",V::makeNum(duration));r.set("file_created",V::makeBool(true));r.set("document_unchanged",V::makeBool(true));r.set("applied",V::makeBool(false));r.set("synchronized_to_motion",V::makeBool(false));return r;}
std::vector<unsigned char> unbase64(const std::string& s){need(!s.empty()&&s.size()%4==0&&s.size()<=16*1024*1024,"Invalid image response base64 size");std::vector<unsigned char> out;int bits=0;unsigned value=0;bool padding=false;for(char c:s){if(c=='='){padding=true;continue;}need(!padding,"Invalid image response base64 padding");size_t n=std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/").find(c);need(n!=std::string::npos,"Invalid image response base64");value=(value<<6)|(unsigned)n;bits+=6;if(bits>=8){bits-=8;out.push_back((unsigned char)(value>>bits));}}return out;}
#include "media_gif_palette.inl"
// Fixed-width LZW dictionaries reset before 10-bit codes are needed. Delta
// rectangles retain previous pixels (disposal 1), keeping screen loops compact.
std::string encodeDisplayGif(unsigned width,unsigned height,const std::vector<std::vector<unsigned char>>& frames,const std::vector<unsigned>& delays,std::array<Color,256>& palette,std::array<unsigned char,32768>& lookup){
    displayPalette(frames,palette,lookup);std::string out="GIF89a";auto u16=[&](unsigned n){out.push_back((char)n);out.push_back((char)(n>>8));};u16(width);u16(height);out.append("\xf7\0\0",3);
    for(const auto& c:palette)for(auto channel:c)out.push_back((char)channel);out.append("\x21\xff\x0bNETSCAPE2.0\x03\x01\0\0\0",19);
    std::vector<unsigned char> previous;
    for(size_t f=0;f<frames.size();++f){cancelled();const auto& rgba=frames[f];std::vector<unsigned char> indexed((size_t)width*height);for(size_t i=0;i<indexed.size();++i)indexed[i]=lookup[paletteBin(rgba[i*4],rgba[i*4+1],rgba[i*4+2])];
        unsigned left=0,top=0,right=width,bottom=height;if(!previous.empty()){left=width;top=height;right=bottom=0;for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)if(indexed[(size_t)y*width+x]!=previous[(size_t)y*width+x]){left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=std::max(bottom,y+1);}if(right==0){left=top=0;right=bottom=1;}}
        out.append("\x21\xf9\x04\x04",4);u16(delays[f]);out.append("\0\0",2);out.push_back(',');u16(left);u16(top);u16(right-left);u16(bottom-top);out.push_back(0);out.push_back(8);
        std::string packed;unsigned acc=0,bits=0;auto emit=[&](unsigned code){acc|=code<<bits;bits+=9;while(bits>=8){packed.push_back((char)acc);acc>>=8;bits-=8;}};
        std::unordered_map<unsigned,unsigned> dictionary;dictionary.reserve(256);unsigned next=258;emit(256);int prefix=-1;
        for(unsigned y=top;y<bottom;++y)for(unsigned x=left;x<right;++x){unsigned c=indexed[(size_t)y*width+x];if(prefix<0){prefix=(int)c;continue;}unsigned key=((unsigned)prefix<<8)|c;auto found=dictionary.find(key);if(found!=dictionary.end()){prefix=(int)found->second;continue;}emit((unsigned)prefix);dictionary.emplace(key,next++);if(next>=510){emit(256);dictionary.clear();next=258;}prefix=(int)c;}
        if(prefix>=0)emit((unsigned)prefix);emit(257);if(bits)packed.push_back((char)acc);for(size_t i=0;i<packed.size();i+=255){size_t count=std::min<size_t>(255,packed.size()-i);out.push_back((char)count);out.append(packed,i,count);}out.push_back(0);previous=std::move(indexed);need(out.size()<=4*1024*1024,"MEDIA_GIF_CAPACITY: GIF exceeds 4 MiB; use MP4 or reduce dimensions/frame rate");
    }out.push_back(';');return out;
}
void blend(std::vector<unsigned char>& frame,unsigned width,int x,int y,const Color& c,double opacity){size_t p=((size_t)y*width+x)*4;for(int k=0;k<3;++k)frame[p+k]=(unsigned char)std::lround(frame[p+k]*(1-opacity)+c[k]*opacity);}
struct Source {unsigned w=0,h=0;std::vector<unsigned char> rgba;};
void drawText(std::vector<unsigned char>& frame,unsigned w,unsigned h,const std::string& value,int x,int y,int size,const Color& color,double opacity){
#ifdef _WIN32
    HDC dc=CreateCompatibleDC(nullptr);need(dc!=nullptr,"Media text DC unavailable");BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-(LONG)h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* data=nullptr;HBITMAP bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);if(!bmp){DeleteDC(dc);throw std::runtime_error("Media text bitmap unavailable");}auto old=SelectObject(dc,bmp);auto* p=(unsigned char*)data;
    for(size_t i=0;i<(size_t)w*h;++i){p[i*4]=frame[i*4+2];p[i*4+1]=frame[i*4+1];p[i*4+2]=frame[i*4];p[i*4+3]=255;}
    HFONT font=CreateFontW(-size,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(color[0],color[1],color[2]));int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0);std::wstring wide(n,L' ');MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),wide.data(),n);RECT r{x,y,(LONG)w,(LONG)h};DrawTextW(dc,wide.c_str(),n,&r,DT_LEFT|DT_NOPREFIX|DT_WORDBREAK);GdiFlush();
    for(size_t i=0;i<(size_t)w*h;++i)for(int k=0;k<3;++k)frame[i*4+k]=(unsigned char)std::lround(frame[i*4+k]*(1-opacity)+p[i*4+2-k]*opacity);
    SelectObject(dc,previous);DeleteObject(font);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);
#else
    (void)frame;(void)w;(void)h;(void)value;(void)x;(void)y;(void)size;(void)color;(void)opacity;throw std::runtime_error("MEDIA_TEXT_UNSUPPORTED: text rendering requires Windows; supply a rendered image layer on this platform");
#endif
}
}
MediaAuthoringCancellation::MediaAuthoringCancellation(const std::atomic_bool* value):previous_(cancellation){cancellation=value;}
MediaAuthoringCancellation::~MediaAuthoringCancellation(){cancellation=previous_;}
V mediaAuthoringCapabilities(){
    V r=V::makeObj();r.set("compose_gif",V::makeBool(true));r.set("compose_png",V::makeBool(true));r.set("video_backend",V::makeStr(videoDecoderStatus044B()));
    AssistantConfig c;const char* key=std::getenv("OPENAI_API_KEY");bool configured=assistantApiKeyStored(c)||(key&&*key);
#ifdef _WIN32
    r.set("generate_image_configured",V::makeBool(configured));r.set("text_layers",V::makeBool(true));
#else
    r.set("generate_image_configured",V::makeBool(false));r.set("text_layers",V::makeBool(false));
#endif
    r.set("image_endpoint",V::makeStr("https://api.openai.com/v1/images/generations"));r.set("image_auth",V::makeStr("OpenAI API key saved in Studio or OPENAI_API_KEY; separate from ChatGPT plan login"));r.set("model_required",V::makeBool(true));r.set("api_access_verified",V::makeBool(false));r.set("document_unchanged",V::makeBool(true));r.set("timeline_binding",V::makeStr("A generated file is not synchronized to motion until an explicit playback binding is configured."));return r;
}
V generateDisplayImage(const V& args,const AssistantTransport& offline){
    keys(args,{"action","path","model","prompt","size","quality","expected_revision"});Output out(text(args,"path"),".png");std::string prompt=text(args,"prompt"),model=text(args,"model");need(!prompt.empty()&&prompt.size()<=32000&&!model.empty(),"Image generation requires prompt (1..32000 bytes) and explicit model");
    std::string size=text(args,"size","1024x1024"),quality=text(args,"quality","medium");need(size=="1024x1024"||size=="1536x1024"||size=="1024x1536"||size=="auto","Unsupported image size");need(quality=="low"||quality=="medium"||quality=="high"||quality=="auto","Unsupported image quality");V request=V::makeObj();request.set("model",V::makeStr(model));request.set("prompt",V::makeStr(prompt));request.set("size",V::makeStr(size));request.set("quality",V::makeStr(quality));request.set("output_format",V::makeStr("png"));request.set("n",V::makeNum(1));cancelled();auto response=requestDisplayImage(request,offline,cancellation);cancelled();const auto* data=response.find("data");need(data&&data->t==V::Arr&&data->arr.size()==1,"IMAGE_GENERATION_INVALID_RESULT: expected one generated image");auto bytes=unbase64(text(data->arr[0],"b64_json"));auto decoded=assistantImageAttachment(bytes,"generated.png");need(decoded.mimeType=="image/png","Image API did not return PNG");out.write(std::string(bytes.begin(),bytes.end()));auto check=imageAssetFromPath("Generated image",pathUtf8(out.temporary));need(check.width==decoded.width&&check.height==decoded.height,"Generated image readback differs");out.publish();V r=receipt(out,check.width,check.height,1,0,"image/png");r.set("model",V::makeStr(model));if(const auto* usage=response.find("usage"))r.set("provider_usage",*usage);else r.set("provider_usage",V{});return r;
}
V composeDisplayMedia(const Document& document,const V& args){
    keys(args,{"action","path","format","width","height","fps","duration_s","background","layers","expected_revision"});const auto format=text(args,"format","gif");need(format=="gif"||format=="mp4"||format=="png","Media format must be png, gif or mp4");Output out(text(args,"path"),"."+format);unsigned w=integer(args,"width",512,16,1024),h=integer(args,"height",288,16,1024),fps=integer(args,"fps",20,1,format=="gif"?50:60);double duration=number(args,"duration_s",{},1);need(duration>0&&duration<=60,"Media duration_s must be in (0,60]");unsigned count=format=="png"?1:(unsigned)std::ceil(duration*fps-1e-9);need(count<=512,"MEDIA_FRAME_CAPACITY: at most 512 frames; reduce fps or duration");need((uint64_t)w*h*4*count<=256ull*1024*1024,"MEDIA_RENDER_CAPACITY: decoded frames exceed 256 MiB");if(format=="mp4")need(w%2==0&&h%2==0,"MP4 requires even width and height");
    const auto* layers=args.find("layers");need(layers&&layers->t==V::Arr&&!layers->arr.empty()&&layers->arr.size()<=64,"Media composition needs 1..64 layers");need((uint64_t)w*h*count*layers->arr.size()<=1500000000ull,"MEDIA_RENDER_CAPACITY: reduce dimensions, frames or layers");Color bg=color(args,"background","#101820");std::map<std::string,Source> sources;size_t sourceBytes=0;
    for(const auto& layer:layers->arr){keys(layer,{"type","x","y","width","height","x2","y2","color","opacity","from_s","to_s","outline","line_width","text","font_size","asset","path"});auto kind=text(layer,"type");need(kind=="rect"||kind=="ellipse"||kind=="line"||kind=="text"||kind=="image","Unknown media layer type");if(kind=="text")need(text(layer,"text").size()<=4096,"Media text exceeds 4096 bytes");if(const auto* outline=layer.find("outline"))need(outline->t==V::Bool,"outline must be boolean");if(kind=="image"){need((layer.find("asset")!=nullptr)!=(layer.find("path")!=nullptr),"Image layer needs exactly one asset or local path");std::string key=layer.find("asset")?"asset:"+text(layer,"asset"):"path:"+text(layer,"path");if(!sources.count(key)){ImageAsset image;if(layer.find("asset")){const auto* found=findImageAsset(document,text(layer,"asset"));need(found!=nullptr,"Image layer asset was not found");image=*found;}else image=imageAssetFromPath("Media source",text(layer,"path"));need(image.mimeType=="image/png"||image.mimeType=="image/jpeg","Composition image layers require PNG/JPEG; GIF/MP4 are outputs");Source src{image.width,image.height,decodeImageAssetRGBA(image)};sourceBytes+=src.rgba.size();need(sourceBytes<=64*1024*1024,"MEDIA_SOURCE_CAPACITY: source images exceed 64 MiB decoded");sources.emplace(key,std::move(src));}}}
    std::vector<std::vector<unsigned char>> frames;frames.reserve(count);std::vector<unsigned> delays;ExprEnv env=document.buildEnv();env.vars["duration"]=duration;env.vars["width"]=w;env.vars["height"]=h;
    for(unsigned f=0;f<count;++f){cancelled();double t=(double)f/fps;env.vars["t"]=t;env.vars["u"]=t/duration;env.vars["frame"]=f;std::vector<unsigned char> frame((size_t)w*h*4,255);for(size_t p=0;p<frame.size();p+=4)for(int k=0;k<3;++k)frame[p+k]=bg[k];
        for(const auto& layer:layers->arr){if(t<number(layer,"from_s",env,0)||t>=number(layer,"to_s",env,duration))continue;auto kind=text(layer,"type");double x=number(layer,"x",env,0),y=number(layer,"y",env,0),bw=number(layer,"width",env,w),bh=number(layer,"height",env,h),opacity=number(layer,"opacity",env,1),stroke=number(layer,"line_width",env,2);need(bw>=0&&bh>=0&&opacity>=0&&opacity<=1&&stroke>0&&stroke<=256,"Invalid media layer size, opacity or line width");Color c=color(layer,"color","#ffffff");if(opacity==0)continue;
            if(kind=="text"){int font=(int)number(layer,"font_size",env,24);need(font>=6&&font<=192,"Text font_size must be 6..192 pixels");auto value=text(layer,"text");auto replace=[&](const std::string& tag,const std::string& val){size_t at=0;while((at=value.find(tag,at))!=std::string::npos){value.replace(at,tag.size(),val);at+=val.size();}};replace("{percent}",std::to_string((int)std::lround(100*t/duration)));replace("{frame}",std::to_string(f));drawText(frame,w,h,value,(int)x,(int)y,font,c,opacity);continue;}
            double x2=number(layer,"x2",env,x+bw),y2=number(layer,"y2",env,y+bh);bool outline=layer.find("outline")&&layer.find("outline")->b;Source* source=nullptr;if(kind=="image")source=&sources.at(layer.find("asset")?"asset:"+text(layer,"asset"):"path:"+text(layer,"path"));if((bw==0||bh==0)&&kind!="line")continue;
            int left=(int)std::max(0.,std::floor(kind=="line"?std::min(x,x2)-stroke:x)),top=(int)std::max(0.,std::floor(kind=="line"?std::min(y,y2)-stroke:y)),right=(int)std::min((double)w,std::ceil(kind=="line"?std::max(x,x2)+stroke:x+bw)),bottom=(int)std::min((double)h,std::ceil(kind=="line"?std::max(y,y2)+stroke:y+bh));
            for(int py=top;py<bottom;++py)for(int px=left;px<right;++px){double dx=px+.5-x,dy=py+.5-y;bool hit=true;double alpha=opacity;Color paint=c;
                if(kind=="ellipse"){double q=std::pow((dx-bw*.5)/(bw*.5),2)+std::pow((dy-bh*.5)/(bh*.5),2);hit=q<=1;if(hit&&outline&&bw>2*stroke&&bh>2*stroke)hit=std::pow((dx-bw*.5)/(bw*.5-stroke),2)+std::pow((dy-bh*.5)/(bh*.5-stroke),2)>=1;}
                else if(kind=="line"){double vx=x2-x,vy=y2-y,length=vx*vx+vy*vy,fraction=length>0?std::clamp((dx*vx+dy*vy)/length,0.,1.):0;hit=std::hypot(dx-fraction*vx,dy-fraction*vy)<=stroke*.5;}
                else if(kind=="rect"&&outline)hit=dx<stroke||dy<stroke||dx>bw-stroke||dy>bh-stroke;
                else if(source){unsigned sx=std::min(source->w-1,(unsigned)std::max(0.,dx/bw*source->w)),sy=std::min(source->h-1,(unsigned)std::max(0.,dy/bh*source->h));size_t p=((size_t)sy*source->w+sx)*4;paint={source->rgba[p],source->rgba[p+1],source->rgba[p+2]};alpha*=source->rgba[p+3]/255.;}
                if(hit)blend(frame,w,px,py,paint,alpha);
            }
        }frames.push_back(std::move(frame));delays.push_back((unsigned)std::lround(100.*(f+1)/fps)-(unsigned)std::lround(100.*f/fps));
    }
    cancelled();if(format=="png"){Image image;image.w=w;image.h=h;image.px.resize((size_t)w*h*3);for(size_t i=0;i<(size_t)w*h;++i)for(int k=0;k<3;++k)image.px[i*3+k]=frames[0][i*4+k];auto bytes=encodePNG(image);out.write(std::string(bytes.begin(),bytes.end()));auto check=imageAssetFromPath("Rendered image",pathUtf8(out.temporary));need(check.width==w&&check.height==h,"PNG readback failed");}
    else if(format=="gif"){std::array<Color,256> palette;std::array<unsigned char,32768> lookup;auto bytes=encodeDisplayGif(w,h,frames,delays,palette,lookup);auto check=decodeGif044B(bytes);need(check.width==w&&check.height==h&&check.frames.size()==count,"GIF readback failed");for(unsigned f=0;f<count;++f)for(size_t p=0;p<(size_t)w*h;++p){unsigned char index=lookup[paletteBin(frames[f][p*4],frames[f][p*4+1],frames[f][p*4+2])];Color expected=palette[index];for(int k=0;k<3;++k)need(check.frames[f][p*4+k]==expected[k],"GIF frame readback differs");}out.write(bytes);}
    else{std::string error;bool encoded=writeVideoClip044B(pathUtf8(out.temporary),w,h,fps,frames,error);need(encoded,"MEDIA_VIDEO_FAILED: "+error);auto check=videoFrameAt044B(pathUtf8(out.temporary),0,1024);need(check.width==w&&check.height==h,"MP4 readback failed");}
    out.publish();V r=receipt(out,w,h,count,format=="png"?0:format=="gif"?std::round(100.*count/fps)/100.:double(count)/fps,format=="png"?"image/png":format=="gif"?"image/gif":"video/mp4");r.set("requested_duration_s",V::makeNum(duration));r.set("fps",V::makeNum(fps));r.set("readback_verified",V::makeBool(true));return r;
}
}
