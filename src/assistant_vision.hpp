#pragma once
#include "ark/json.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace ark::assistant_vision {
using V=arkjson::Value;
inline constexpr std::size_t maxImageBytes=4*1024*1024;
// No file or URL fetching: image bytes must already be in the host tool result.
inline std::pair<std::string,std::string> imageData(const std::string& mime,const std::string& data){
    if((mime!="image/png"&&mime!="image/jpeg")||data.empty()||data.size()>(maxImageBytes+2)/3*4||data.size()%4)
        throw std::runtime_error("Visual evidence requires a bounded base64 PNG or JPEG.");
    const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string bytes;bytes.reserve(data.size()/4*3);
    for(std::size_t i=0;i<data.size();i+=4){
        unsigned n=0,padding=0;
        for(unsigned j=0;j<4;++j){const char c=data[i+j];if(c=='='){if(i+4!=data.size()||j<2)throw std::runtime_error("Invalid image padding.");++padding;n<<=6;}
            else{const auto pos=alphabet.find(c);if(pos==std::string::npos||padding)throw std::runtime_error("Invalid image encoding.");n=(n<<6)|static_cast<unsigned>(pos);}}
        if(padding>2||(padding==2&&(n&0xffff))||(padding==1&&(n&0xff)))throw std::runtime_error("Invalid image encoding tail.");
        bytes.push_back(static_cast<char>(n>>16));if(padding<2)bytes.push_back(static_cast<char>(n>>8));if(!padding)bytes.push_back(static_cast<char>(n));
    }
    const bool png=bytes.size()>=24&&bytes.compare(0,8,std::string("\x89PNG\r\n\x1a\n",8))==0&&bytes.compare(12,4,"IHDR")==0;
    const bool jpeg=bytes.size()>=4&&static_cast<unsigned char>(bytes[0])==255&&static_cast<unsigned char>(bytes[1])==216&&static_cast<unsigned char>(bytes[bytes.size()-2])==255&&static_cast<unsigned char>(bytes.back())==217;
    if(bytes.size()>maxImageBytes||(mime=="image/png"?!png:!jpeg))throw std::runtime_error("Visual evidence bytes do not match their image type.");
    return {mime,data};
}
inline std::pair<std::string,std::string> dataUrl(const V& value){
    if(value.t!=V::Str)throw std::runtime_error("Image URL must be an inline data URL.");
    for(const std::string mime:{"image/png","image/jpeg"}){const auto prefix="data:"+mime+";base64,";if(value.str.rfind(prefix,0)==0)return imageData(mime,value.str.substr(prefix.size()));}
    throw std::runtime_error("Only inline PNG/JPEG image evidence is supported; paths and remote URLs are not loaded.");
}
inline std::string kind(const V& value){const auto* type=value.find("type");return type?type->asStr():"";}
inline bool containsImage(const V& message){const auto* content=message.find("content");return content&&content->t==V::Arr&&std::any_of(content->arr.begin(),content->arr.end(),[](const V& item){return kind(item)=="input_image";});}
inline bool userReference(const V& message){const auto* content=message.find("content");if(!content||content->t!=V::Arr)return false;for(const auto& part:content->arr)if(kind(part)=="input_text")if(const auto* text=part.find("text");text&&text->t==V::Str&&text->str.rfind("[ARK user image | mode: ",0)==0)return true;return false;}
inline void omitPixels(V& message){const auto* original=message.find("content");if(!original||original->t!=V::Arr)return;V content=*original;content.arr.erase(std::remove_if(content.arr.begin(),content.arr.end(),[](const V& item){return kind(item)=="input_image";}),content.arr.end());V note=V::makeObj();note.set("type",V::makeStr("input_text"));note.set("text",V::makeStr("Earlier visual evidence metadata retained; image pixels omitted from this request. Request a fresh snapshot to inspect its appearance."));content.arr.push_back(note);message.set("content",content);}
inline void prune(V& history,std::size_t byteLimit){
    // Original user references cannot be re-acquired with snapshot. Keep their
    // pixels intact; oversized user history rejects instead of inventing vision.
    const auto snapshot=[](const V& item){return containsImage(item)&&!userReference(item);};
    std::size_t images=0;for(auto it=history.arr.rbegin();it!=history.arr.rend();++it)if(snapshot(*it)&&++images>2)omitPixels(*it);
    while(history.dump().size()>byteLimit){auto it=std::find_if(history.arr.begin(),history.arr.end(),snapshot);if(it==history.arr.end())break;omitPixels(*it);}
}
}
