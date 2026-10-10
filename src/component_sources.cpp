#include "ark/component_sources.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif
namespace ark {
namespace {
using V=arkjson::Value;
std::string encode(const std::string& text){const char* hex="0123456789ABCDEF";std::string result;for(unsigned char c:text){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')result+=c;else{result+='%';result+=hex[c>>4];result+=hex[c&15];}}return result;}
std::string lower(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
std::string get(const V& args,const char* field,const std::string& fallback={}){if(auto* value=args.find(field)){if(value->t!=V::Str)throw std::runtime_error(std::string(field)+" must be text");return value->str;}return fallback;}
#ifdef _WIN32
struct Http {HINTERNET h=nullptr;explicit Http(HINTERNET handle):h(handle){if(!h)throw std::runtime_error("Component source connection failed");}~Http(){WinHttpCloseHandle(h);}Http(const Http&)=delete;};
std::wstring wide(const std::string& text){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0);if(!n)throw std::runtime_error("Invalid component URL encoding");std::wstring out(n,L' ');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),out.data(),n);return out;}
#endif
std::string fetch(const std::string& url,std::size_t limit){
    validateComponentHttpsUrl(url);
#ifdef _WIN32
    auto full=wide(url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof parts;parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(full.c_str(),(DWORD)full.size(),0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS)throw std::runtime_error("Invalid HTTPS component source");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);if(path.empty())path=L"/";
    Http session(WinHttpOpen(L"ARK-Point/0.9 component-library",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));WinHttpSetTimeouts(session.h,15000,15000,15000,15000);
    Http connection(WinHttpConnect(session.h,host.c_str(),parts.nPort,0));Http request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof disabled);
    DWORD auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;WinHttpSetOption(request.h,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof auth);
    const auto began=std::chrono::steady_clock::now();
    if(!WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.h,nullptr))throw std::runtime_error("Component source HTTPS request failed");
    DWORD status=0,size=sizeof status;if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200)throw std::runtime_error("Component source returned HTTP "+std::to_string(status)+"; use a direct public HTTPS URL or import a downloaded STEP");
    std::string bytes;char buffer[65536];for(;;){auto remaining=90000-std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();if(remaining<=0)throw std::runtime_error("Component download exceeded 90 seconds");WinHttpSetTimeouts(request.h,15000,15000,15000,(int)std::min<decltype(remaining)>(15000,remaining));DWORD read=0;if(!WinHttpReadData(request.h,buffer,sizeof buffer,&read))throw std::runtime_error("Component source read failed");if(!read)break;if(read>limit-bytes.size())throw std::runtime_error("Component source exceeds size limit");bytes.append(buffer,read);}return bytes;
#else
    (void)limit;throw std::runtime_error("Public component download currently requires the native Windows HTTPS connector; local library ingestion remains available");
#endif
}
}
void validateComponentHttpsUrl(const std::string& url){
    if(url.size()>8192||url.rfind("https://",0)!=0||url.find_first_of("\r\n\t\\ #")!=std::string::npos)throw std::runtime_error("Component source requires a direct HTTPS URL without credentials, fragments or whitespace");
    auto end=url.find_first_of("/?",8);auto authority=url.substr(8,end==std::string::npos?end:end-8);
    if(authority.empty()||authority.find('@')!=std::string::npos)throw std::runtime_error("Component URL credentials are not supported");
    if(std::any_of(url.begin(),url.end(),[](unsigned char c){return c<32||c==127;}))throw std::runtime_error("Invalid component URL characters");
}
V componentSources(){
    V out=V::makeObj(),sources=V::makeArr();auto add=[&](const char* id,const char* name,const char* page,const char* access,const char* note){V source=V::makeObj();source.set("id",V::makeStr(id));source.set("name",V::makeStr(name));source.set("url",V::makeStr(page));source.set("access",V::makeStr(access));source.set("notes",V::makeStr(note));sources.arr.push_back(source);};
    add("kicad","KiCad community 3D library","https://gitlab.com/kicad/libraries/kicad-packages3D","public_paged_search_and_direct_step","Use library_source_search to list categories then STEP files. CC-BY-SA-4.0 with KiCad exception; preserve attribution and library license on redistribution. Community geometry is not a supplier specification.");
    add("traceparts","TraceParts manufacturer catalogs","https://developers.traceparts.com/docs/api-related-to-the-product-content","account_api_integration_required","Official CAD request API requires account authorization and a configured part. Download under your catalog access then ingest; ARK does not bypass login or submit your email.");
    add("3dfindit","CADENAS 3Dfindit","https://www.3dfindit.com/","catalog_access_required","Mechanical and electrical manufacturer catalogs. Retain manufacturer, part number, configuration, source page and applicable license when ingesting downloaded STEP files.");
    out.set("sources",sources);out.set("search_first",V::makeStr("Search the local library by exact standard/dimensions/manufacturer before downloading another copy."));out.set("automatic_standard_recognition",V::makeBool(false));return out;
}
V searchComponentSource(const V& args){
    if(args.t!=V::Obj)throw std::runtime_error("Source search needs an object");for(const auto& field:args.obj)if(field.first!="source"&&field.first!="path"&&field.first!="page"&&field.first!="query")throw std::runtime_error("Unknown source search field");
    if(get(args,"source","kicad")!="kicad")throw std::runtime_error("This catalog requires its authorized provider integration; use library_sources for access details");
    auto path=get(args,"path"),query=lower(get(args,"query"));if(path.size()>512||query.size()>128||path.find("..")!=std::string::npos||path.find_first_of("\\\r\n")!=std::string::npos||(!path.empty()&&path.front()=='/'))throw std::runtime_error("Invalid source category path or query");
    unsigned page=1;if(auto* requested=args.find("page")){if(requested->t!=V::Num||!std::isfinite(requested->num)||requested->num<1||requested->num>1000||(unsigned)requested->num!=requested->num)throw std::runtime_error("Source page must be an integer from 1 to 1000");page=(unsigned)requested->num;}
    auto url="https://gitlab.com/api/v4/projects/kicad%2Flibraries%2Fkicad-packages3D/repository/tree?ref=master&per_page=100&page="+std::to_string(page)+"&path="+encode(path);
    V tree;if(!arkjson::parse(fetch(url,4*1024*1024),tree)||tree.t!=V::Arr)throw std::runtime_error("Invalid public catalog response");V items=V::makeArr();
    for(const auto& entry:tree.arr){const auto name=get(entry,"name"),entryPath=get(entry,"path"),type=get(entry,"type");const auto lowered=lower(name);const bool folder=type=="tree",step=lowered.size()>=5&&lowered.substr(lowered.size()-5)==".step";
        if((!folder&&!step)||(!query.empty()&&lowered.find(query)==std::string::npos))continue;
        V item=V::makeObj();item.set("name",V::makeStr(name));item.set("path",V::makeStr(entryPath));item.set("kind",V::makeStr(folder?"category":"step"));
        if(step){std::string encoded;std::size_t start=0;while(start<entryPath.size()){auto slash=entryPath.find('/',start);if(!encoded.empty())encoded+='/';encoded+=encode(entryPath.substr(start,slash==std::string::npos?slash:slash-start));if(slash==std::string::npos)break;start=slash+1;}
            item.set("download_url",V::makeStr("https://gitlab.com/kicad/libraries/kicad-packages3D/-/raw/master/"+encoded));item.set("source_url",V::makeStr("https://gitlab.com/kicad/libraries/kicad-packages3D/-/blob/master/"+encoded));item.set("license",V::makeStr("CC-BY-SA-4.0 WITH KiCad-library-exception"));item.set("license_url",V::makeStr("https://www.kicad.org/libraries/license/"));}
        items.arr.push_back(item);
    }
    V out=V::makeObj();out.set("source",V::makeStr("kicad"));out.set("path",V::makeStr(path));out.set("page",V::makeNum(page));out.set("items",items);out.set("next_page",tree.arr.size()==100?V::makeNum(page+1):V{});out.set("query_scope",V::makeStr("current directory page only; follow next_page, or select a category path"));out.set("metadata_authority",V::makeStr("catalog_names_not_verified_engineering_dimensions"));return out;
}
void downloadComponentStep(const std::string& url,const std::string& destination){
    auto bytes=fetch(url,64u*1024u*1024u);if(bytes.find("ISO-10303-21;")==std::string::npos||bytes.find("END-ISO-10303-21;")==std::string::npos)throw std::runtime_error("Downloaded content is not a STEP exchange file; no library record was created");
    std::ofstream out(std::filesystem::u8path(destination),std::ios::binary|std::ios::trunc);if(!out||!out.write(bytes.data(),(std::streamsize)bytes.size()))throw std::runtime_error("Could not save the host-owned component download");out.close();if(!out)throw std::runtime_error("Component download write failed");
}
}
