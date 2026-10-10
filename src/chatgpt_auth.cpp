#include "ark/chatgpt_auth.hpp"
#include "ark/assistant.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <string_view>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace ark {
namespace {
using V=arkjson::Value;
constexpr const char* issuer="https://auth.openai.com";
constexpr const char* resource="https://api.openai.com/v1";
constexpr const char* authorizeEndpoint="https://auth.openai.com/api/accounts/authorize";
constexpr const char* tokenEndpoint="https://auth.openai.com/api/accounts/oauth/token";
constexpr const char* discoveryEndpoint="https://auth.openai.com/.well-known/openid-configuration";
constexpr const char* responseEndpoint="https://api.openai.com/v1/responses";
constexpr const char* modelsEndpoint="https://api.openai.com/v1/models";
constexpr const char* scopes="openid profile email offline_access resource.invoke chatgpt.tokens.use.direct";
void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
bool constantEqual(std::string_view a,std::string_view b){size_t size=std::max(a.size(),b.size());unsigned diff=(unsigned)(a.size()!=b.size());for(size_t i=0;i<size;++i){unsigned char x=i<a.size()?(unsigned char)a[i]:0,y=i<b.size()?(unsigned char)b[i]:0;diff|=x^y;}return diff==0;}
bool validClientId(const std::string& id){return !id.empty()&&id!="dynamic_agent_client"&&id.size()<=128&&std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';});}
bool hasScope(const std::string& all,const std::string& wanted){size_t start=0;while(start<all.size()){size_t end=all.find(' ',start);if(end==std::string::npos)end=all.size();if(all.substr(start,end-start)==wanted)return true;start=end+1;}return false;}
std::string urlEncode(std::string_view value){static constexpr char hex[]="0123456789ABCDEF";std::string out;for(unsigned char c:value){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=(char)c;else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
std::string urlDecode(std::string_view value){auto nibble=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;};std::string out;for(size_t i=0;i<value.size();++i){char c=value[i];if(c=='+')out+=' ';else if(c=='%'){require(i+2<value.size(),"Invalid OAuth callback encoding");int hi=nibble(value[i+1]),lo=nibble(value[i+2]);require(hi>=0&&lo>=0,"Invalid OAuth callback encoding");out+=(char)((hi<<4)|lo);i+=2;}else out+=c;}return out;}
std::string base64UrlEncode(const unsigned char* bytes,size_t count){static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";std::string out;out.reserve((count*4+2)/3);for(size_t i=0;i<count;i+=3){unsigned x=(unsigned)bytes[i]<<16;if(i+1<count)x|=(unsigned)bytes[i+1]<<8;if(i+2<count)x|=bytes[i+2];out+=alphabet[(x>>18)&63];out+=alphabet[(x>>12)&63];if(i+1<count)out+=alphabet[(x>>6)&63];if(i+2<count)out+=alphabet[x&63];}return out;}
std::vector<unsigned char> base64UrlDecode(std::string_view value,size_t maximum){auto sextet=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='-')return 62;if(c=='_')return 63;return -1;};require(value.size()<=maximum*2&&value.size()%4!=1,"Invalid JWT base64url length");std::vector<unsigned char> out;out.reserve(value.size()*3/4);unsigned acc=0;int bits=0;for(char c:value){int x=sextet(c);require(x>=0,"Invalid JWT base64url character");acc=(acc<<6)|(unsigned)x;bits+=6;if(bits>=8){bits-=8;out.push_back((unsigned char)(acc>>bits));acc&=(1u<<bits)-1;require(out.size()<=maximum,"JWT segment too large");}}require(acc==0,"Noncanonical JWT base64url padding");return out;}
std::string textOf(const V& object,const char* key){const V* value=object.find(key);require(value&&value->t==V::Str,"Missing OpenAI identity field");return value->str;}
std::int64_t integerOf(const V& object,const char* key){const V* value=object.find(key);require(value&&value->t==V::Num&&std::isfinite(value->num)&&value->num>=0&&value->num<9007199254740992.0&&std::floor(value->num)==value->num,"Invalid OpenAI time claim");return (std::int64_t)value->num;}
V parseJson(const std::string& data,const char* reason){V value;require(data.size()<=4*1024*1024&&arkjson::parse(data,value),reason);return value;}
#ifdef _WIN32
std::string utf8(const std::wstring& value){if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);require(n>0,"Invalid Windows Unicode value");std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),out.data(),n,nullptr,nullptr);return out;}
std::wstring wide(const std::string& value){if(value.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0);require(n>0,"Invalid OAuth UTF-8 text");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),out.data(),n);return out;}
std::vector<unsigned char> randomBytes(size_t count){std::vector<unsigned char> result(count);require(count<=1024&&BCryptGenRandom(nullptr,result.data(),(ULONG)count,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Secure random generator failed");return result;}
std::array<unsigned char,32> sha256(std::string_view data){BCRYPT_ALG_HANDLE algorithm=nullptr;require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA-256 provider unavailable");DWORD objectSize=0,resultSize=0;NTSTATUS status=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,sizeof(objectSize),&resultSize,0);if(status<0){BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("SHA-256 object unavailable");}std::vector<unsigned char> object(objectSize);BCRYPT_HASH_HANDLE hash=nullptr;status=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0);std::array<unsigned char,32> digest{};if(status>=0)status=BCryptHashData(hash,(PUCHAR)data.data(),(ULONG)data.size(),0);if(status>=0)status=BCryptFinishHash(hash,digest.data(),(ULONG)digest.size(),0);if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);require(status>=0,"SHA-256 failed");return digest;}
bool verifyRsa256(const std::string& signingInput,const std::vector<unsigned char>& signature,const V& jwk){
    if(textOf(jwk,"kty")!="RSA")return false;
    auto modulus=base64UrlDecode(textOf(jwk,"n"),1024),exponent=base64UrlDecode(textOf(jwk,"e"),16);
    if(modulus.size()<256||modulus.size()>1024||exponent.empty()||signature.size()!=modulus.size())return false;
    BCRYPT_RSAKEY_BLOB head{};head.Magic=BCRYPT_RSAPUBLIC_MAGIC;head.BitLength=(ULONG)(modulus.size()*8);head.cbPublicExp=(ULONG)exponent.size();head.cbModulus=(ULONG)modulus.size();std::vector<unsigned char> blob(sizeof(head)+exponent.size()+modulus.size());std::memcpy(blob.data(),&head,sizeof(head));std::memcpy(blob.data()+sizeof(head),exponent.data(),exponent.size());std::memcpy(blob.data()+sizeof(head)+exponent.size(),modulus.data(),modulus.size());
    BCRYPT_ALG_HANDLE algorithm=nullptr;require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_RSA_ALGORITHM,nullptr,0)>=0,"RSA provider unavailable");BCRYPT_KEY_HANDLE key=nullptr;NTSTATUS status=BCryptImportKeyPair(algorithm,nullptr,BCRYPT_RSAPUBLIC_BLOB,&key,blob.data(),(ULONG)blob.size(),0);
    if(status>=0){auto digest=sha256(signingInput);BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};status=BCryptVerifySignature(key,&padding,digest.data(),(ULONG)digest.size(),(PUCHAR)signature.data(),(ULONG)signature.size(),BCRYPT_PAD_PKCS1);}
    if(key)BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(algorithm,0);return status>=0;
}
#else
std::vector<unsigned char> randomBytes(size_t){throw std::runtime_error("ChatGPT sign-in currently requires Windows");}
std::array<unsigned char,32> sha256(std::string_view){throw std::runtime_error("ChatGPT sign-in currently requires Windows");}
bool verifyRsa256(const std::string&,const std::vector<unsigned char>&,const V&){throw std::runtime_error("ChatGPT signature verification currently requires Windows");}
#endif
}

ChatGptAuthAttempt makeChatGptAuthAttempt(const std::string& hostId,const std::string& redirectUri,
    const std::string& issuedClientId,const std::string& expectedSubject,const std::string& idTokenHint,
    const std::string& loginHint,const std::string& state,const std::string& nonce,const std::string& verifier,bool requestPlanConsent){
    require(hostId.rfind("urn:uuid:",0)==0&&hostId.size()<=128,"Invalid ChatGPT host ID");
    const std::string loopback="http://127.0.0.1:";const size_t slash=redirectUri.find('/',loopback.size());
    require(redirectUri.rfind(loopback,0)==0&&slash!=std::string::npos&&redirectUri.substr(slash)=="/auth/callback","Invalid OAuth loopback callback");
    const std::string port=redirectUri.substr(loopback.size(),slash-loopback.size());require(!port.empty()&&port.size()<=5&&std::all_of(port.begin(),port.end(),[](char c){return c>='0'&&c<='9';})&&std::stoul(port)<=65535&&std::stoul(port)>0,"Invalid OAuth callback port");
    require(state.size()>=32&&state.size()<=128&&nonce.size()>=32&&nonce.size()<=128&&verifier.size()>=43&&verifier.size()<=128,"Invalid OAuth random values");
    require(issuedClientId.empty()||validClientId(issuedClientId),"Invalid issued ChatGPT client ID");
    auto digest=sha256(verifier);ChatGptAuthAttempt attempt;attempt.hostId=hostId;attempt.clientId=issuedClientId.empty()?"dynamic_agent_client":issuedClientId;attempt.expectedSubject=expectedSubject;attempt.newRegistration=issuedClientId.empty();attempt.redirectUri=redirectUri;attempt.state=state;attempt.nonce=nonce;attempt.codeVerifier=verifier;attempt.codeChallenge=base64UrlEncode(digest.data(),digest.size());
    std::vector<std::pair<std::string,std::string>> parameters={{"client_id",attempt.clientId},{"ext_agent_host_id",hostId},{"response_type","code"},{"redirect_uri",redirectUri},{"scope",scopes},{"resource",resource},{"state",state},{"nonce",nonce},{"code_challenge_method","S256"},{"code_challenge",attempt.codeChallenge}};
    if(attempt.newRegistration)parameters.emplace_back("agent_name_hint","ARK-Point");else{if(!idTokenHint.empty())parameters.emplace_back("id_token_hint",idTokenHint);if(!loginHint.empty())parameters.emplace_back("login_hint",loginHint);if(requestPlanConsent)parameters.emplace_back("prompt","consent");}
    attempt.authorizationUrl=authorizeEndpoint;attempt.authorizationUrl+='?';bool first=true;for(const auto& [key,value]:parameters){if(!first)attempt.authorizationUrl+='&';first=false;attempt.authorizationUrl+=urlEncode(key)+'='+urlEncode(value);}return attempt;
}
ChatGptCallback parseChatGptCallback(const std::string& target,const ChatGptAuthAttempt& attempt){
    require(target.size()<=8192&&target.rfind("/auth/callback?",0)==0,"Unexpected OAuth callback path");std::map<std::string,std::string> query;size_t start=15;
    while(start<target.size()){size_t end=target.find('&',start);if(end==std::string::npos)end=target.size();auto pair=target.substr(start,end-start);size_t split=pair.find('=');require(split!=std::string::npos,"Malformed OAuth callback");auto key=urlDecode(std::string_view(pair).substr(0,split)),value=urlDecode(std::string_view(pair).substr(split+1));require(!key.empty()&&query.emplace(key,value).second,"Duplicate OAuth callback field");start=end+1;}
    auto state=query.find("state");require(state!=query.end()&&constantEqual(state->second,attempt.state),"OAuth state mismatch");
    if(auto error=query.find("error");error!=query.end())throw std::runtime_error(error->second=="access_denied"?"ChatGPT sign-in was declined":"ChatGPT authorization failed");
    auto code=query.find("code");require(code!=query.end()&&!code->second.empty()&&code->second.size()<=2048,"OAuth callback has no authorization code");ChatGptCallback result;result.code=code->second;
    if(attempt.newRegistration){auto client=query.find("client_id");require(client!=query.end()&&validClientId(client->second),"ChatGPT registration did not return an issued client ID");result.issuedClientId=client->second;}
    else{result.issuedClientId=attempt.clientId;if(auto client=query.find("client_id");client!=query.end())require(constantEqual(client->second,attempt.clientId),"ChatGPT issued client ID changed during reauthorization");}
    return result;
}
ChatGptIdentity verifyChatGptIdToken(const std::string& jwt,const V& jwks,const std::string& clientId,const std::string& nonce,std::int64_t nowUnix){
    require(validClientId(clientId)&&nonce.size()>=32&&jwt.size()<=16384,"Invalid ID token verification request");size_t first=jwt.find('.'),second=jwt.find('.',first==std::string::npos?first:first+1);require(first!=std::string::npos&&second!=std::string::npos&&jwt.find('.',second+1)==std::string::npos,"Malformed ID token");
    auto headerBytes=base64UrlDecode(std::string_view(jwt).substr(0,first),4096),payloadBytes=base64UrlDecode(std::string_view(jwt).substr(first+1,second-first-1),8192),signature=base64UrlDecode(std::string_view(jwt).substr(second+1),1024);
    V header=parseJson(std::string(headerBytes.begin(),headerBytes.end()),"Invalid ID token header"),payload=parseJson(std::string(payloadBytes.begin(),payloadBytes.end()),"Invalid ID token claims");require(header.t==V::Obj&&payload.t==V::Obj&&textOf(header,"alg")=="RS256"&&header.find("crit")==nullptr,"Unsupported ID token algorithm");
    const std::string kid=textOf(header,"kid");require(kid.size()<=256&&jwks.t==V::Obj,"Invalid ID token key ID");const V* keys=jwks.find("keys");require(keys&&keys->t==V::Arr&&keys->arr.size()<=64,"Invalid OpenAI JWKS");
    const V* selected=nullptr;for(const V& key:keys->arr)if(key.t==V::Obj&&key.find("kid")&&key.find("kid")->t==V::Str&&key.find("kid")->str==kid){require(!selected,"Duplicate OpenAI JWKS key ID");selected=&key;}
    require(selected,"OpenAI ID token signing key not found");if(auto alg=selected->find("alg"))require(alg->t==V::Str&&alg->str=="RS256","Unexpected OpenAI signing key algorithm");if(auto use=selected->find("use"))require(use->t==V::Str&&use->str=="sig","Unexpected OpenAI key use");
    require(verifyRsa256(jwt.substr(0,second),signature,*selected),"OpenAI ID token signature invalid");
    ChatGptIdentity identity;identity.issuer=textOf(payload,"iss");require(identity.issuer==issuer,"OpenAI ID token issuer mismatch");
    const V* audience=payload.find("aud");bool matches=audience&&audience->t==V::Str&&audience->str==clientId;
    if(audience&&audience->t==V::Arr){for(const auto& item:audience->arr)if(item.t==V::Str&&item.str==clientId)matches=true;if(audience->arr.size()>1)require(textOf(payload,"azp")==clientId,"OpenAI authorized party mismatch");}
    require(matches,"OpenAI ID token audience mismatch");require(constantEqual(textOf(payload,"nonce"),nonce),"OpenAI ID token nonce mismatch");identity.subject=textOf(payload,"sub");require(!identity.subject.empty()&&identity.subject.size()<=256,"OpenAI ID token has no subject");identity.clientId=clientId;identity.expiresAt=integerOf(payload,"exp");auto issued=integerOf(payload,"iat");require(identity.expiresAt>nowUnix-5&&issued<=nowUnix+5&&issued<identity.expiresAt,"OpenAI ID token expired or issued in the future");if(payload.find("nbf"))require(integerOf(payload,"nbf")<=nowUnix+5,"OpenAI ID token is not yet valid");if(const V* email=payload.find("email")){require(email->t==V::Str&&email->str.size()<=320,"Invalid OpenAI email claim");identity.email=email->str;}return identity;
}
V parseChatGptSse(const std::string& body){
    require(body.size()<=16*1024*1024,"ChatGPT stream exceeded size limit");V completed;bool seen=false;size_t cursor=0,itemBytes=0;std::string data;std::map<unsigned,V> doneItems;
    auto dispatch=[&](){
        if(data.empty())return;V event=parseJson(data,"Invalid ChatGPT stream event");data.clear();const V* kind=event.find("type");if(!kind||kind->t!=V::Str)return;
        if(kind->str=="response.output_item.done"){
            require(!seen,"ChatGPT output arrived after completion");auto index=integerOf(event,"output_index");const V* item=event.find("item");
            require(index<128&&item&&item->t==V::Obj,"Invalid ChatGPT completed output item");
            auto found=doneItems.find((unsigned)index);if(found!=doneItems.end()){require(found->second.dump()==item->dump(),"Conflicting ChatGPT completed output item");return;}
            itemBytes+=item->dump().size();require(itemBytes<=4*1024*1024,"ChatGPT completed output exceeded size limit");doneItems.emplace((unsigned)index,*item);
        }else if(kind->str=="response.completed"||kind->str=="response.failed"||kind->str=="response.incomplete"){
            require(!seen,"Duplicate ChatGPT terminal response event");const V* response=event.find("response");require(response&&response->t==V::Obj,"ChatGPT terminal event has no response");completed=*response;if(!completed.find("status"))completed.set("status",V::makeStr(kind->str.substr(9)));seen=true;
        }
    };
    while(cursor<body.size()){size_t end=body.find('\n',cursor);if(end==std::string::npos)end=body.size();std::string_view line(body.data()+cursor,end-cursor);if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);if(line.empty())dispatch();else if(line.rfind("data:",0)==0){std::string_view value=line.substr(5);if(!value.empty()&&value.front()==' ')value.remove_prefix(1);if(value!="[DONE]"){if(!data.empty())data+='\n';data.append(value);require(data.size()<=4*1024*1024,"ChatGPT stream event exceeded size limit");}}cursor=end<body.size()?end+1:end;}
    dispatch();require(seen,"ChatGPT stream ended before a terminal response event");
    // Direct-plan streams may omit assembled output from the terminal response.
    // Only complete output items are replayable: partial deltas must never run tools.
    const V* output=completed.find("output");
    if(textOf(completed,"status")=="completed"&&(!output||(output->t==V::Arr&&output->arr.empty()))&&!doneItems.empty()){
        V assembled=V::makeArr();for(const auto& [index,item]:doneItems){require(index==assembled.arr.size(),"ChatGPT stream has missing completed output items");assembled.arr.push_back(item);}completed.set("output",std::move(assembled));
    }
    return completed;
}

V parseChatGptResponsesHttp(unsigned status,const std::string& body,
    const std::string& retryAfter,const std::string& requestId,std::int64_t nowUnix){
    if(status==200)return parseChatGptSse(body);
    auto safeField=[](const std::string& value,bool parameter=false){
        if(value.size()>128||!std::all_of(value.begin(),value.end(),[&](unsigned char c){
            return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
                   c=='_'||c=='-'||c=='.'||(parameter&&(c=='['||c==']'));
        }))return std::string{};
        return value;
    };
    std::string code,param;V parsed;
    // Error descriptions/detail are diagnostic text, not protocol codes, and
    // can contain sensitive request material. Never copy them into exceptions.
    if(body.size()<=65536&&arkjson::parse(body,parsed)&&parsed.t==V::Obj){
        if(const V* error=parsed.find("error");error&&error->t==V::Obj){
            if(const V* value=error->find("code");value&&value->t==V::Str)code=safeField(value->str);
            if(const V* value=error->find("param");value&&value->t==V::Str)param=safeField(value->str,true);
        }
    }
    bool retryable=false;std::string category="http_failure";
    if(code=="subscription_sharing_usage_limit_exceeded")category="plan_usage_limit";
    else if(code=="insufficient_quota")category="quota";
    else if(status==400||status==401||status==403||status==422||
            code=="subscription_sharing_user_not_eligible"||
            code=="subscription_sharing_unsupported_capability"||
            code=="subscription_sharing_route_not_supported"||
            code=="subscription_sharing_route_not_enabled"||code=="route_not_enabled"||
            code=="subscription_sharing_invalid_user"||
            code=="chatpass_v2_scope_not_authorized"||
            code=="chatpass_v2_invalid_authorization_context"||code=="invalid_api_key"||
            code=="invalid_client"||code=="model_not_found")category="configuration";
    else if(status==503&&(code=="subscription_sharing_usage_unavailable"||
                         code=="subscription_sharing_user_unavailable")){
        category="transient_plan_service";retryable=true;
    }else if(status==503&&code=="server_is_overloaded"){
        category="service_overloaded";retryable=true;
    }else if(status==429&&(code=="slow_down"||code=="rate_limit_exceeded")){
        category="rate_limit";retryable=true;
    }else if(status==503){
        // Direct admission can mean the route is not enabled/configured. A
        // bare status or detail string does not prove a transient usage error.
        // The runtime may try bounded backoff, then surface this distinction;
        // it must never repair routing by changing credentials or providers.
        category="unknown_service_unavailable";retryable=true;
    }else if(status==500||status==502||status==504){
        category="temporary_http_service";retryable=true;
    }
    throw AssistantHttpError(status,retryable,assistantRetryAfterMilliseconds(retryAfter,nowUnix),
                             code,safeField(requestId),"ChatGPT Responses",param,category);
}

#ifdef _WIN32
namespace {
struct Handle {HINTERNET value=nullptr;explicit Handle(HINTERNET v):value(v){require(v!=nullptr,"ChatGPT HTTP connection failed");}~Handle(){if(value)WinHttpCloseHandle(value);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
struct Socket {SOCKET value=INVALID_SOCKET;explicit Socket(SOCKET v):value(v){require(v!=INVALID_SOCKET,"ChatGPT loopback socket failed");}~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}Socket(const Socket&)=delete;};
struct HttpResult {DWORD status=0;std::string body,requestId,retryAfter;};
struct SecretString {std::string value;explicit SecretString(std::string text):value(std::move(text)){}~SecretString(){if(!value.empty())SecureZeroMemory(value.data(),value.size());}};
struct OAuthError:std::runtime_error {DWORD status;std::string code;OAuthError(DWORD httpStatus,std::string errorCode):std::runtime_error("ChatGPT token endpoint HTTP "+std::to_string(httpStatus)+(errorCode.empty()?"":" ("+errorCode+")")),status(httpStatus),code(std::move(errorCode)){} };
struct SseWire {
    std::string line,data,terminal,done;
    void dispatch(){if(data.empty())return;V event=parseJson(data,"Invalid ChatGPT stream event");const V* kind=event.find("type");if(kind&&kind->t==V::Str){
        if(kind->str=="response.output_item.done"){require(done.size()+data.size()+8<=4*1024*1024,"ChatGPT completed output exceeded size limit");done+="data: "+data+"\n\n";}
        else if(kind->str=="response.completed"||kind->str=="response.failed"||kind->str=="response.incomplete")terminal=std::move(done)+"data: "+data+"\n\n";
    }data.clear();}
    void feed(const char* bytes,size_t count){for(size_t i=0;i<count&&!terminal.size();++i){char c=bytes[i];if(c=='\n'){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())dispatch();else if(line.rfind("data:",0)==0){std::string_view value(line.data()+5,line.size()-5);if(!value.empty()&&value.front()==' ')value.remove_prefix(1);if(value!="[DONE]"){if(!data.empty())data+='\n';data.append(value);require(data.size()<=4*1024*1024,"ChatGPT SSE event too large");}}line.clear();}else{line+=c;require(line.size()<=4*1024*1024,"ChatGPT SSE line too large");}}}
};
std::int64_t unixNow(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
HttpResult http(const std::string& method,const std::string& endpoint,const std::string& body,
                const std::string& bearer,const std::string& contentType,size_t maxBytes,const std::atomic_bool* cancel){
    require(endpoint.rfind("https://auth.openai.com/",0)==0||endpoint.rfind("https://api.openai.com/",0)==0,"Unexpected ChatGPT HTTP origin");
    require(endpoint.find_first_of("\r\n\t #\\") == std::string::npos&&endpoint.size()<=2048,"Unsafe ChatGPT HTTP URL");
    auto url=wide(endpoint);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=(DWORD)-1;
    require(WinHttpCrackUrl(url.c_str(),(DWORD)url.size(),0,&parts)!=FALSE&&parts.nScheme==INTERNET_SCHEME_HTTPS&&parts.dwUserNameLength==0&&parts.dwPasswordLength==0,"Invalid ChatGPT HTTPS URL");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    require((host==L"auth.openai.com"||host==L"api.openai.com")&&parts.nPort==INTERNET_DEFAULT_HTTPS_PORT,"Unexpected ChatGPT host or port");
    Handle session(WinHttpOpen(L"ARK-Point-SIWC/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    require(WinHttpSetTimeouts(session.value,10000,10000,30000,30000)!=FALSE,"ChatGPT HTTP timeout setup failed");Handle connection(WinHttpConnect(session.value,host.c_str(),parts.nPort,0));
    HINTERNET raw=WinHttpOpenRequest(connection.value,wide(method).c_str(),path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);require(raw!=nullptr,"ChatGPT HTTP request failed");
    std::atomic<HINTERNET> active{raw};struct Guard{std::atomic<HINTERNET>& active;~Guard(){if(auto h=active.exchange(nullptr))WinHttpCloseHandle(h);}} guard{active};
    std::jthread watcher([&](std::stop_token stop){while(!stop.stop_requested()){if(cancel&&cancel->load()){if(auto h=active.exchange(nullptr))WinHttpCloseHandle(h);return;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}});
    auto check=[&](BOOL ok){if(cancel&&cancel->load())throw std::runtime_error("ChatGPT operation cancelled");require(ok!=FALSE,"ChatGPT HTTPS request failed");};
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;check(WinHttpSetOption(raw,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof redirect));DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;check(WinHttpSetOption(raw,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof disabled));
    std::wstring headers=L"Accept: application/json\r\n";if(endpoint==responseEndpoint)headers=L"Accept: text/event-stream\r\n";if(!contentType.empty())headers+=L"Content-Type: "+wide(contentType)+L"\r\n";
    if(!bearer.empty()){require(bearer.size()<=8192&&std::all_of(bearer.begin(),bearer.end(),[](unsigned char c){return c>=33&&c<=126;}),"Invalid ChatGPT bearer format");headers+=L"Authorization: Bearer "+wide(bearer)+L"\r\n";}
    check(WinHttpAddRequestHeaders(raw,headers.c_str(),(DWORD)headers.size(),WINHTTP_ADDREQ_FLAG_ADD));if(!bearer.empty())SecureZeroMemory(headers.data(),headers.size()*sizeof(wchar_t));
    require(body.size()<=4*1024*1024,"ChatGPT request too large");check(WinHttpSendRequest(raw,WINHTTP_NO_ADDITIONAL_HEADERS,0,body.empty()?nullptr:(void*)body.data(),(DWORD)body.size(),(DWORD)body.size(),0));check(WinHttpReceiveResponse(raw,nullptr));
    HttpResult result;DWORD size=sizeof(result.status);check(WinHttpQueryHeaders(raw,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&result.status,&size,WINHTTP_NO_HEADER_INDEX));
    wchar_t requestId[256]{};size=sizeof(requestId);if(WinHttpQueryHeaders(raw,WINHTTP_QUERY_CUSTOM,L"openai-request-id",requestId,&size,WINHTTP_NO_HEADER_INDEX)||((size=sizeof(requestId)),WinHttpQueryHeaders(raw,WINHTTP_QUERY_CUSTOM,L"x-request-id",requestId,&size,WINHTTP_NO_HEADER_INDEX))){std::wstring id(requestId);if(id.size()<=128&&std::all_of(id.begin(),id.end(),[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'_'||c==L'-'||c==L'.';}))result.requestId=utf8(id);}
    wchar_t retryAfter[256]{};size=sizeof(retryAfter);if(WinHttpQueryHeaders(raw,WINHTTP_QUERY_CUSTOM,L"Retry-After",retryAfter,&size,WINHTTP_NO_HEADER_INDEX)){
        const std::wstring value(retryAfter);if(value.size()<=128&&std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=32&&c<=126;}))result.retryAfter=utf8(value);
    }
    char buffer[16384];size_t consumed=0;SseWire stream;const bool streaming=endpoint==responseEndpoint&&result.status==200;
    // HTTP admission failures are JSON (or plain diagnostics), never an SSE
    // completion. Retain their bounded body so the decoder can inspect codes.
    const size_t bodyLimit=endpoint==responseEndpoint&&result.status!=200?std::min<size_t>(maxBytes,65536):maxBytes;
    for(;;){
        DWORD read=0;check(WinHttpReadData(raw,buffer,sizeof buffer,&read));if(read==0)break;
        consumed+=read;
        if(consumed>bodyLimit){
            if(endpoint==responseEndpoint&&result.status!=200){result.body.clear();break;}
            throw std::runtime_error("ChatGPT response too large");
        }
        if(streaming){stream.feed(buffer,read);if(!stream.terminal.empty()){result.body=std::move(stream.terminal);break;}}
        else result.body.append(buffer,read);
    }
    return result;
}
V jsonResponse(const HttpResult& response,const char* reason){require(response.status==200,reason);return parseJson(response.body,reason);}
struct Discovery {std::string jwksUri,revocationEndpoint;};
Discovery discovery(const std::atomic_bool* cancel){V info=jsonResponse(http("GET",discoveryEndpoint,{}, {},{},65536,cancel),"ChatGPT discovery failed");require(info.t==V::Obj&&textOf(info,"issuer")==issuer&&textOf(info,"authorization_endpoint")==authorizeEndpoint&&textOf(info,"token_endpoint")==tokenEndpoint,"Unexpected OpenAI OIDC issuer or endpoints");Discovery result;result.jwksUri=textOf(info,"jwks_uri");result.revocationEndpoint=textOf(info,"revocation_endpoint");require(result.jwksUri.rfind("https://auth.openai.com/",0)==0&&result.revocationEndpoint.rfind("https://auth.openai.com/",0)==0,"Unexpected OIDC metadata origin");return result;}
std::filesystem::path credentialPath(){PWSTR root=nullptr;require(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&root)==S_OK&&root!=nullptr,"Cannot locate local Windows app data");std::filesystem::path path(root);CoTaskMemFree(root);path/="ARK-Point";std::filesystem::create_directories(path);return path/"chatgpt-accounts.dpapi";}
std::string uuidHost(){auto bytes=randomBytes(16);bytes[6]=(bytes[6]&0x0f)|0x40;bytes[8]=(bytes[8]&0x3f)|0x80;std::ostringstream out;out<<"urn:uuid:"<<std::hex<<std::setfill('0');for(size_t i=0;i<bytes.size();++i){if(i==4||i==6||i==8||i==10)out<<'-';out<<std::setw(2)<<(unsigned)bytes[i];}return out.str();}
struct StoredAccount {std::string clientId,subject,email,idToken,accessToken,refreshToken,scope;std::int64_t accessExpiresAt=0,earliestRefreshAt=0;};
struct StoredAuth {std::string hostId,activeClientId;std::vector<StoredAccount> accounts;};
V storeJson(const StoredAuth& store){V result=V::makeObj(),accounts=V::makeArr();result.set("schema",V::makeNum(1));result.set("host_id",V::makeStr(store.hostId));result.set("active_client_id",V::makeStr(store.activeClientId));for(const auto& account:store.accounts){V item=V::makeObj();item.set("client_id",V::makeStr(account.clientId));item.set("subject",V::makeStr(account.subject));item.set("email",V::makeStr(account.email));item.set("id_token",V::makeStr(account.idToken));item.set("access_token",V::makeStr(account.accessToken));item.set("refresh_token",V::makeStr(account.refreshToken));item.set("scope",V::makeStr(account.scope));item.set("access_expires_at",V::makeNum((double)account.accessExpiresAt));item.set("earliest_refresh_at",V::makeNum((double)account.earliestRefreshAt));accounts.arr.push_back(item);}result.set("accounts",accounts);return result;}
StoredAuth parseStore(const V& value){require(value.t==V::Obj&&value.find("schema")&&value.find("schema")->num==1,"Unsupported ChatGPT credential format");StoredAuth store;store.hostId=textOf(value,"host_id");require(store.hostId.rfind("urn:uuid:",0)==0,"Invalid ChatGPT host ID");store.activeClientId=textOf(value,"active_client_id");const V* rows=value.find("accounts");require(rows&&rows->t==V::Arr&&rows->arr.size()<=16,"Invalid ChatGPT account list");std::set<std::string> ids;for(const V& row:rows->arr){StoredAccount account;account.clientId=textOf(row,"client_id");account.subject=textOf(row,"subject");account.email=textOf(row,"email");account.idToken=textOf(row,"id_token");account.accessToken=textOf(row,"access_token");account.refreshToken=textOf(row,"refresh_token");account.scope=textOf(row,"scope");account.accessExpiresAt=integerOf(row,"access_expires_at");if(row.find("earliest_refresh_at"))account.earliestRefreshAt=integerOf(row,"earliest_refresh_at");require(validClientId(account.clientId)&&!account.subject.empty()&&ids.insert(account.clientId).second,"Invalid or duplicate ChatGPT account");store.accounts.push_back(std::move(account));}if(!store.activeClientId.empty())require(ids.count(store.activeClientId)==1,"Active ChatGPT account is missing");return store;}
void saveStore(const StoredAuth& store,const std::filesystem::path& path=credentialPath()){std::string plaintext=storeJson(store).dump();DATA_BLOB input{(DWORD)plaintext.size(),(BYTE*)plaintext.data()},encrypted{};require(CryptProtectData(&input,L"ARK-Point ChatGPT credentials",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted)!=FALSE,"Could not protect ChatGPT credentials");SecureZeroMemory(plaintext.data(),plaintext.size());auto pending=path;pending+=L".pending";try{std::ofstream output(pending,std::ios::binary|std::ios::trunc);require((bool)output,"Could not save ChatGPT credentials");output.write((const char*)encrypted.pbData,encrypted.cbData);output.flush();require((bool)output,"Could not flush ChatGPT credentials");output.close();require(MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Could not commit ChatGPT credentials");}catch(...){std::error_code ignored;std::filesystem::remove(pending,ignored);SecureZeroMemory(encrypted.pbData,encrypted.cbData);LocalFree(encrypted.pbData);throw;}SecureZeroMemory(encrypted.pbData,encrypted.cbData);LocalFree(encrypted.pbData);}
struct AccountMutex {HANDLE handle=nullptr;explicit AccountMutex(const std::string& clientId){auto digest=sha256(clientId);std::wstring name=L"Local\\ARK-Point-SIWC-";static constexpr wchar_t hex[]=L"0123456789abcdef";for(size_t i=0;i<16;++i){name+=hex[digest[i]>>4];name+=hex[digest[i]&15];}handle=CreateMutexW(nullptr,FALSE,name.c_str());require(handle!=nullptr,"Could not serialize ChatGPT token refresh");DWORD wait=WaitForSingleObject(handle,30000);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("Timed out waiting for ChatGPT credential update");}}~AccountMutex(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}};
StoredAuth loadStore(const std::filesystem::path& path=credentialPath()){if(!std::filesystem::exists(path)){StoredAuth created;created.hostId=uuidHost();saveStore(created,path);return created;}std::error_code error;auto size=std::filesystem::file_size(path,error);require(!error&&size>0&&size<=256*1024,"Invalid protected ChatGPT credential file");std::ifstream input(path,std::ios::binary);require((bool)input,"Cannot read protected ChatGPT credential file");std::string encrypted((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());DATA_BLOB blob{(DWORD)encrypted.size(),(BYTE*)encrypted.data()},plaintext{};require(CryptUnprotectData(&blob,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plaintext)!=FALSE,"ChatGPT credentials are not available to this Windows user");std::string decoded((char*)plaintext.pbData,plaintext.cbData);SecureZeroMemory(plaintext.pbData,plaintext.cbData);LocalFree(plaintext.pbData);try{StoredAuth store=parseStore(parseJson(decoded,"Invalid protected ChatGPT credential JSON"));SecureZeroMemory(decoded.data(),decoded.size());return store;}catch(...){SecureZeroMemory(decoded.data(),decoded.size());throw;}}
struct State {std::mutex mutex;bool loaded=false;StoredAuth store;bool remoteRevocationConfirmed=true;std::vector<ChatGptModel> models;std::string modelClientId,pendingClientId;};
State& state(){static State instance;return instance;}
void ensureLoaded(State& state){if(!state.loaded){AccountMutex accountLock("ARK-Point-auth-store");state.store=loadStore();state.loaded=true;}}
StoredAccount* findAccount(StoredAuth& store,const std::string& id){for(auto& account:store.accounts)if(account.clientId==id)return &account;return nullptr;}
const StoredAccount* activeAccount(const StoredAuth& store){for(const auto& account:store.accounts)if(account.clientId==store.activeClientId)return &account;return nullptr;}
ChatGptAuthStatus summarize(const State& state){ChatGptAuthStatus status;status.available=true;status.remoteRevocationConfirmed=state.remoteRevocationConfirmed;status.activeClientId=state.store.activeClientId;status.pendingClientId=state.pendingClientId;for(const auto& account:state.store.accounts){ChatGptAccountSummary item;item.clientId=account.clientId;item.subject=account.subject;item.email=account.email;item.selected=account.clientId==state.store.activeClientId;item.authorized=hasScope(account.scope,"chatgpt.tokens.use.direct")&&!account.refreshToken.empty();item.accessExpiresAt=account.accessExpiresAt;status.accounts.push_back(item);if(item.selected){status.activeEmail=account.email;status.connected=!account.refreshToken.empty();status.planAuthorized=item.authorized;}}return status;}
V tokenResult(const std::string& form,const std::atomic_bool* cancel){HttpResult response=http("POST",tokenEndpoint,form,{},"application/x-www-form-urlencoded",65536,cancel);if(response.status!=200){std::string code;V body;if(arkjson::parse(response.body,body)&&body.t==V::Obj){const V* error=body.find("error");if(error&&error->t==V::Str)code=error->str;else if(error&&error->t==V::Obj)if(const V* value=error->find("code");value&&value->t==V::Str)code=value->str;}if(code.size()>128||!std::all_of(code.begin(),code.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}))code.clear();throw OAuthError(response.status,code);}return parseJson(response.body,"ChatGPT token exchange returned invalid JSON");}
void adoptTokens(StoredAccount& account,const V& tokens){require(tokens.t==V::Obj&&textOf(tokens,"token_type")=="Bearer","ChatGPT token type was not Bearer");account.accessToken=textOf(tokens,"access_token");account.refreshToken=textOf(tokens,"refresh_token");account.scope=textOf(tokens,"scope");require(!account.accessToken.empty()&&!account.refreshToken.empty(),"ChatGPT token response lacks renewable credentials");auto duration=integerOf(tokens,"expires_in");require(duration>=60&&duration<=86400,"Invalid ChatGPT access-token lifetime");account.accessExpiresAt=unixNow()+duration;account.earliestRefreshAt=unixNow();if(const V* earliest=tokens.find("earliest_refresh_at");earliest&&earliest->t==V::Num)account.earliestRefreshAt=integerOf(tokens,"earliest_refresh_at");}
std::string form(const std::vector<std::pair<std::string,std::string>>& entries){std::string result;bool first=true;for(const auto& [name,value]:entries){if(!first)result+='&';first=false;result+=urlEncode(name)+'='+urlEncode(value);}return result;}
struct AccountAccess {std::string clientId,token;};
AccountAccess accessToken(const std::atomic_bool* cancel){State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);StoredAccount* account=findAccount(global.store,global.store.activeClientId);require(account&&hasScope(account->scope,"chatgpt.tokens.use.direct")&&!account->refreshToken.empty(),"Continue with ChatGPT before using this assistant");if(account->accessExpiresAt>unixNow()+90&&!account->accessToken.empty())return {account->clientId,account->accessToken};
    const std::string clientId=account->clientId;
    AccountMutex rotating("ARK-Point-auth-store");StoredAuth latest=loadStore();require(latest.hostId==global.store.hostId&&latest.activeClientId==clientId,"ChatGPT account changed during token refresh");global.store=std::move(latest);account=findAccount(global.store,clientId);require(account&&!account->refreshToken.empty(),"ChatGPT renewable session was disconnected");if(account->accessExpiresAt>unixNow()+90&&!account->accessToken.empty())return {clientId,account->accessToken};
    if(account->earliestRefreshAt>unixNow()){if(account->accessExpiresAt>unixNow()+5&&!account->accessToken.empty())return {clientId,account->accessToken};throw std::runtime_error("ChatGPT token cannot be refreshed yet; sign in again if access has expired");}
    V tokens;try{tokens=tokenResult(form({{"grant_type","refresh_token"},{"client_id",account->clientId},{"refresh_token",account->refreshToken},{"resource",resource}}),cancel);}catch(const OAuthError& failure){
        const std::set<std::string> terminal={"invalid_grant","invalid_refresh_token","token_expired","refresh_token_expired","refresh_token_invalidated","refresh_token_reused"};
        if(terminal.count(failure.code)){StoredAuth disconnected=global.store;StoredAccount* record=findAccount(disconnected,clientId);record->accessToken.clear();record->refreshToken.clear();record->idToken.clear();record->scope.clear();record->accessExpiresAt=0;record->earliestRefreshAt=0;saveStore(disconnected);global.store=std::move(disconnected);throw std::runtime_error("ChatGPT renewable session expired; continue with the saved account to sign in again");}throw;
    }
    StoredAuth updated=global.store;StoredAccount* replacement=findAccount(updated,clientId);adoptTokens(*replacement,tokens);saveStore(updated);global.store=std::move(updated);return {clientId,findAccount(global.store,clientId)->accessToken};
}
} // namespace
#endif
#ifdef _WIN32
namespace {
struct WsaLifetime {WSADATA data{};WsaLifetime(){require(WSAStartup(MAKEWORD(2,2),&data)==0,"Could not start ChatGPT loopback listener");}~WsaLifetime(){WSACleanup();}};
class LoopbackListener {
    WsaLifetime wsa_;
    Socket server_;
    unsigned short port_=0;
public:
    LoopbackListener():server_(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)){
        BOOL exclusive=TRUE;require(setsockopt(server_.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof exclusive)==0,"Could not isolate ChatGPT callback port");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
        require(bind(server_.value,(sockaddr*)&address,sizeof address)==0&&listen(server_.value,4)==0,"Could not bind 127.0.0.1 OAuth callback");
        int size=sizeof address;require(getsockname(server_.value,(sockaddr*)&address,&size)==0,"Could not read OAuth callback port");port_=ntohs(address.sin_port);
    }
    std::string redirectUri() const{return "http://127.0.0.1:"+std::to_string(port_)+"/auth/callback";}
    ChatGptCallback wait(const ChatGptAuthAttempt& attempt,const std::atomic_bool* cancel){
        auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);
        while(std::chrono::steady_clock::now()<deadline){if(cancel&&cancel->load())throw std::runtime_error("ChatGPT sign-in cancelled");fd_set ready;FD_ZERO(&ready);FD_SET(server_.value,&ready);timeval timeout{0,200000};int selected=select(0,&ready,nullptr,nullptr,&timeout);require(selected>=0,"ChatGPT callback listener failed");if(selected==0)continue;
            Socket client(accept(server_.value,nullptr,nullptr));DWORD readTimeout=5000;setsockopt(client.value,SOL_SOCKET,SO_RCVTIMEO,(const char*)&readTimeout,sizeof readTimeout);std::string request;char bytes[2048];while(request.find("\r\n\r\n")==std::string::npos&&request.size()<8192){int received=recv(client.value,bytes,sizeof bytes,0);if(received<=0)break;request.append(bytes,(size_t)received);}size_t end=request.find("\r\n");if(end==std::string::npos)continue;std::string line=request.substr(0,end);if(line.rfind("GET ",0)!=0)continue;size_t pathEnd=line.find(' ',4);if(pathEnd==std::string::npos)continue;std::string target=line.substr(4,pathEnd-4);if(target.rfind("/auth/callback?",0)!=0){const char* notFound="HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send(client.value,notFound,(int)std::strlen(notFound),0);continue;}
            const std::string host="Host: 127.0.0.1:"+std::to_string(port_);require(request.find(host+"\r\n")!=std::string::npos,"OAuth callback host mismatch");
            try{ChatGptCallback callback=parseChatGptCallback(target,attempt);const char* body="<html><body>Return to ARK-Point. Sign-in is being verified.</body></html>";std::string response="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(std::strlen(body))+"\r\n\r\n"+body;send(client.value,response.data(),(int)response.size(),0);return callback;}
            catch(...){const char* invalid="HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";send(client.value,invalid,(int)std::strlen(invalid),0);throw;}
        }
        throw std::runtime_error("ChatGPT browser sign-in timed out");
    }
};
std::string randomUrl(size_t bytes){auto data=randomBytes(bytes);return base64UrlEncode(data.data(),data.size());}
ChatGptAuthStatus statusSafely(){State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);return summarize(global);}
}
#endif

ChatGptAuthStatus chatGptAuthStatus(){
#ifdef _WIN32
    try{return statusSafely();}catch(const std::exception&){ChatGptAuthStatus status;status.available=true;status.message="Stored ChatGPT sign-in is unavailable to this Windows account";return status;}
#else
    ChatGptAuthStatus status;status.message="Sign in with ChatGPT is currently available on Windows";return status;
#endif
}
ChatGptAuthStatus chatGptSignIn(const std::string& selectedClientId,const std::atomic_bool* cancel,bool requestPlanConsent){
#ifdef _WIN32
    StoredAuth before;std::optional<StoredAccount> chosen;{State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);before=global.store;if(!selectedClientId.empty()){const StoredAccount* found=findAccount(before,selectedClientId);require(found||global.pendingClientId==selectedClientId,"Selected ChatGPT registration was not found");if(found)chosen=*found;}}
    LoopbackListener listener;ChatGptAuthAttempt attempt=makeChatGptAuthAttempt(before.hostId,listener.redirectUri(),selectedClientId,chosen?chosen->subject:std::string{},chosen?chosen->idToken:std::string{},chosen?chosen->email:std::string{},randomUrl(32),randomUrl(32),randomUrl(64),requestPlanConsent);
    auto browser=wide(attempt.authorizationUrl);HINSTANCE launch=ShellExecuteW(nullptr,L"open",browser.c_str(),nullptr,nullptr,SW_SHOWNORMAL);require((INT_PTR)launch>32,"Could not open the system browser for ChatGPT sign-in");
    ChatGptCallback callback=listener.wait(attempt,cancel);{State& global=state();std::lock_guard lock(global.mutex);if(attempt.newRegistration)global.pendingClientId=callback.issuedClientId;}V tokens=tokenResult(form({{"grant_type","authorization_code"},{"client_id",callback.issuedClientId},{"code",callback.code},{"code_verifier",attempt.codeVerifier},{"redirect_uri",attempt.redirectUri},{"resource",resource}}),cancel);
    Discovery metadata=discovery(cancel);V jwks=jsonResponse(http("GET",metadata.jwksUri,{}, {},{},1024*1024,cancel),"OpenAI signing keys unavailable");const std::string idToken=textOf(tokens,"id_token");ChatGptIdentity identity=verifyChatGptIdToken(idToken,jwks,callback.issuedClientId,attempt.nonce,unixNow());
    if(chosen)require(constantEqual(identity.subject,chosen->subject),"Different ChatGPT account returned for saved registration");StoredAccount credential;credential.clientId=callback.issuedClientId;credential.subject=identity.subject;credential.email=identity.email;credential.idToken=idToken;adoptTokens(credential,tokens);
    State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);AccountMutex accountLock("ARK-Point-auth-store");StoredAuth updated=loadStore();require(updated.hostId==before.hostId,"ChatGPT host ID changed during sign-in");StoredAccount* existing=findAccount(updated,credential.clientId);if(existing){require(existing->subject==credential.subject,"Issued ChatGPT client ID belongs to another account");*existing=credential;}else{require(updated.accounts.size()<16,"ChatGPT account limit reached");updated.accounts.push_back(credential);}updated.activeClientId=credential.clientId;saveStore(updated);global.store=std::move(updated);global.models.clear();global.modelClientId.clear();global.pendingClientId.clear();global.remoteRevocationConfirmed=true;return summarize(global);
#else
    (void)selectedClientId;(void)cancel;(void)requestPlanConsent;throw std::runtime_error("Sign in with ChatGPT is currently available on Windows");
#endif
}
ChatGptAuthStatus chatGptSelectAccount(const std::string& issuedClientId){
#ifdef _WIN32
    State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);AccountMutex accountLock("ARK-Point-auth-store");StoredAuth updated=loadStore();require(findAccount(updated,issuedClientId),"ChatGPT account registration not found");updated.activeClientId=issuedClientId;saveStore(updated);global.store=std::move(updated);global.models.clear();global.modelClientId.clear();return summarize(global);
#else
    (void)issuedClientId;throw std::runtime_error("Sign in with ChatGPT is currently available on Windows");
#endif
}
ChatGptAuthStatus chatGptSignOut(const std::atomic_bool* cancel){
#ifdef _WIN32
    State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);AccountMutex accountLock("ARK-Point-auth-store");global.store=loadStore();StoredAccount* active=findAccount(global.store,global.store.activeClientId);if(!active)return summarize(global);bool revoked=true;
    if(!active->refreshToken.empty()){revoked=false;for(int attempt=0;attempt<3&&!revoked;++attempt){try{Discovery metadata=discovery(cancel);auto response=http("POST",metadata.revocationEndpoint,form({{"token",active->refreshToken},{"token_type_hint","refresh_token"},{"client_id",active->clientId}}),{},"application/x-www-form-urlencoded",65536,cancel);revoked=response.status==200;if(response.status>=400&&response.status<500)break;}catch(const std::exception&){}if(!revoked&&attempt<2)std::this_thread::sleep_for(std::chrono::milliseconds(200*(attempt+1)));}}
    StoredAuth updated=global.store;StoredAccount* record=findAccount(updated,global.store.activeClientId);record->accessToken.clear();record->refreshToken.clear();record->idToken.clear();record->scope.clear();record->accessExpiresAt=0;saveStore(updated);global.store=std::move(updated);global.models.clear();global.modelClientId.clear();global.remoteRevocationConfirmed=revoked;ChatGptAuthStatus status=summarize(global);if(!revoked)status.message="Signed out locally; remote revocation could not be confirmed";return status;
#else
    (void)cancel;throw std::runtime_error("Sign in with ChatGPT is currently available on Windows");
#endif
}
std::vector<ChatGptModel> parseChatGptModelCatalog(const V& catalogue){
    const V* rows=catalogue.find("models");
    require(rows&&rows->t==V::Arr&&rows->arr.size()<=1024,"Unexpected ChatGPT model list");
    std::vector<ChatGptModel> models;std::set<std::string> seen;
    auto safeSlug=[](const std::string& slug){
        return !slug.empty()&&slug.size()<=128&&std::all_of(slug.begin(),slug.end(),[](unsigned char c){
            return std::isalnum(c)||c=='_'||c=='-'||c=='.'||c==':';
        });
    };
    for(const V& item:rows->arr){
        if(item.t!=V::Obj)continue;
        const V* visibility=item.find("visibility"),*slug=item.find("slug"),*name=item.find("display_name");
        if(!visibility||visibility->t!=V::Str||visibility->str!="list"||
           !slug||slug->t!=V::Str||!safeSlug(slug->str)||
           !name||name->t!=V::Str||name->str.empty()||name->str.size()>256||
           !seen.insert(slug->str).second)continue;
        ChatGptModel model;model.slug=slug->str;model.displayName=name->str;
        auto effort=[](const V& value){
            if(value.t!=V::Str)return false;
            const std::string& text=value.str;
            return text=="none"||text=="minimal"||text=="low"||text=="medium"||
                   text=="high"||text=="xhigh"||text=="max"||text=="ultra";
        };
        if(const V* supported=item.find("supported_reasoning_levels");supported&&supported->t==V::Arr&&supported->arr.size()<=16){
            std::set<std::string> unique;
            for(const V& level:supported->arr){
                if(level.t!=V::Obj)continue;
                const V* value=level.find("effort");
                if(value&&effort(*value)&&unique.insert(value->str).second)
                    model.reasoningEfforts.push_back(value->str);
            }
        }
        if(const V* fallback=item.find("default_reasoning_level");fallback&&effort(*fallback)&&
           std::find(model.reasoningEfforts.begin(),model.reasoningEfforts.end(),fallback->str)!=model.reasoningEfforts.end())
            model.defaultReasoningEffort=fallback->str;
        models.push_back(std::move(model));
    }
    return models;
}
std::vector<ChatGptModel> chatGptListModels(const std::atomic_bool* cancel){
#ifdef _WIN32
    AccountAccess access=accessToken(cancel);SecretString bearer(std::move(access.token));HttpResult response=http("GET",modelsEndpoint,{},bearer.value,{},1024*1024,cancel);V catalogue=jsonResponse(response,"ChatGPT model list unavailable");std::vector<ChatGptModel> models=parseChatGptModelCatalog(catalogue);
    State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);require(global.store.activeClientId==access.clientId,"ChatGPT account changed while listing models");global.models=models;global.modelClientId=access.clientId;return models;
#else
    (void)cancel;throw std::runtime_error("Sign in with ChatGPT is currently available on Windows");
#endif
}
bool chatGptPlanReady(){ChatGptAuthStatus status=chatGptAuthStatus();return status.connected&&status.planAuthorized;}
V chatGptResponses(const V& request,const std::atomic_bool* cancel){
#ifdef _WIN32
    require(request.t==V::Obj&&request.find("store")&&request.find("store")->t==V::Bool&&!request.find("store")->b&&request.find("stream")&&request.find("stream")->t==V::Bool&&request.find("stream")->b,"ChatGPT plan requires store:false and stream:true");const std::string model=textOf(request,"model");
    AccountAccess access=accessToken(cancel);std::vector<ChatGptModel> available;{State& global=state();std::lock_guard lock(global.mutex);ensureLoaded(global);if(global.modelClientId==access.clientId)available=global.models;}
    if(std::none_of(available.begin(),available.end(),[&](const ChatGptModel& item){return item.slug==model;})){available=chatGptListModels(cancel);State& global=state();std::lock_guard lock(global.mutex);require(global.store.activeClientId==access.clientId,"ChatGPT account changed before inference");}
    auto selected=std::find_if(available.begin(),available.end(),[&](const ChatGptModel& item){return item.slug==model;});require(selected!=available.end(),"Select a model from the signed-in ChatGPT account's model list");
    if(const V* reasoning=request.find("reasoning")){require(reasoning->t==V::Obj,"Invalid ChatGPT reasoning settings");if(const V* effort=reasoning->find("effort"))require(effort->t==V::Str&&std::find(selected->reasoningEfforts.begin(),selected->reasoningEfforts.end(),effort->str)!=selected->reasoningEfforts.end(),"Selected reasoning effort is not advertised for this ChatGPT model; choose Auto or a listed level");}
    SecretString bearer(std::move(access.token));HttpResult response=http("POST",responseEndpoint,request.dump(),bearer.value,"application/json",16*1024*1024,cancel);
    return parseChatGptResponsesHttp(response.status,response.body,response.retryAfter,response.requestId,unixNow());
#else
    (void)request;(void)cancel;throw std::runtime_error("Sign in with ChatGPT is currently available on Windows");
#endif
}
bool chatGptDpapiRoundTripForTest(const std::string& absoluteTempFile){
#ifdef _WIN32
    require(!absoluteTempFile.empty()&&absoluteTempFile.size()<=1024,"Invalid DPAPI test path");
    std::filesystem::path target(reinterpret_cast<const char8_t*>(absoluteTempFile.c_str()));
    auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());target=std::filesystem::weakly_canonical(target);
    auto parent=target.parent_path();auto expected=temp.begin(),actual=parent.begin();for(;expected!=temp.end();++expected,++actual)require(actual!=parent.end()&&*actual==*expected,"DPAPI test path must stay inside the temp directory");
    require(parent!=temp&&target.filename()==L"siwc-fixture.dpapi","DPAPI test path needs an isolated fixture directory");
    StoredAuth source;source.hostId=uuidHost();StoredAccount account;account.clientId="oaiapp_test_fixture";account.subject="offline-subject";account.email="offline@example.test";account.accessToken="offline-access-token";account.refreshToken="offline-refresh-token";account.idToken="offline-id-token";account.scope=scopes;account.accessExpiresAt=unixNow()+3600;source.accounts.push_back(account);source.activeClientId=account.clientId;
    saveStore(source,target);StoredAuth decoded=loadStore(target);return decoded.hostId==source.hostId&&decoded.accounts.size()==1&&decoded.accounts[0].refreshToken==account.refreshToken&&decoded.accounts[0].clientId==account.clientId;
#else
    (void)absoluteTempFile;return false;
#endif
}
} // namespace ark
