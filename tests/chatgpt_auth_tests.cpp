#include "ark/chatgpt_auth.hpp"
#include "ark/assistant.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
using V=arkjson::Value;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> bool rejects(F&& action){try{action();return false;}catch(const std::exception&){return true;}}
V json(const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Bad auth test JSON");return value;}
std::string b64(const unsigned char* bytes,size_t count){static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";std::string out;for(size_t i=0;i<count;i+=3){unsigned x=(unsigned)bytes[i]<<16;if(i+1<count)x|=(unsigned)bytes[i+1]<<8;if(i+2<count)x|=bytes[i+2];out+=alphabet[(x>>18)&63];out+=alphabet[(x>>12)&63];if(i+1<count)out+=alphabet[(x>>6)&63];if(i+2<count)out+=alphabet[x&63];}return out;}
std::string b64(const std::string& text){return b64((const unsigned char*)text.data(),text.size());}
#ifdef _WIN32
struct SigningKey {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_KEY_HANDLE key=nullptr;V jwks;
    SigningKey(){
        check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_RSA_ALGORITHM,nullptr,0)>=0,"RSA test provider");
        check(BCryptGenerateKeyPair(algorithm,&key,2048,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0,"RSA test keypair");
        DWORD size=0;check(BCryptExportKey(key,nullptr,BCRYPT_RSAPUBLIC_BLOB,nullptr,0,&size,0)>=0&&size>sizeof(BCRYPT_RSAKEY_BLOB),"RSA public blob size");std::vector<unsigned char> blob(size);check(BCryptExportKey(key,nullptr,BCRYPT_RSAPUBLIC_BLOB,blob.data(),size,&size,0)>=0,"RSA public blob");
        auto* header=(const BCRYPT_RSAKEY_BLOB*)blob.data();check(header->Magic==BCRYPT_RSAPUBLIC_MAGIC&&header->cbModulus==256,"RSA test public key shape");const unsigned char* exp=blob.data()+sizeof(BCRYPT_RSAKEY_BLOB),*mod=exp+header->cbPublicExp;
        V keyJson=V::makeObj(),keys=V::makeArr();keyJson.set("kid",V::makeStr("offline-key"));keyJson.set("kty",V::makeStr("RSA"));keyJson.set("alg",V::makeStr("RS256"));keyJson.set("use",V::makeStr("sig"));keyJson.set("e",V::makeStr(b64(exp,header->cbPublicExp)));keyJson.set("n",V::makeStr(b64(mod,header->cbModulus)));keys.arr.push_back(keyJson);jwks=V::makeObj();jwks.set("keys",keys);
    }
    ~SigningKey(){if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    std::string sign(const V& claims,const std::string& alg="RS256",const std::string& kid="offline-key"){
        V header=V::makeObj();header.set("alg",V::makeStr(alg));header.set("kid",V::makeStr(kid));header.set("typ",V::makeStr("JWT"));std::string signing=b64(header.dump())+"."+b64(claims.dump());
        BCRYPT_ALG_HANDLE hashAlg=nullptr;check(BCryptOpenAlgorithmProvider(&hashAlg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 test provider");DWORD objectSize=0,read=0;check(BCryptGetProperty(hashAlg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,sizeof objectSize,&read,0)>=0,"SHA256 test object");std::vector<unsigned char> object(objectSize);BCRYPT_HASH_HANDLE hash=nullptr;check(BCryptCreateHash(hashAlg,&hash,object.data(),objectSize,nullptr,0,0)>=0,"SHA256 test hash");std::array<unsigned char,32> digest{};check(BCryptHashData(hash,(PUCHAR)signing.data(),(ULONG)signing.size(),0)>=0&&BCryptFinishHash(hash,digest.data(),32,0)>=0,"SHA256 test digest");BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(hashAlg,0);
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};ULONG signatureSize=0;check(BCryptSignHash(key,&padding,digest.data(),32,nullptr,0,&signatureSize,BCRYPT_PAD_PKCS1)>=0,"RSA signature size");std::vector<unsigned char> signature(signatureSize);check(BCryptSignHash(key,&padding,digest.data(),32,signature.data(),signatureSize,&signatureSize,BCRYPT_PAD_PKCS1)>=0,"RSA signature");return signing+"."+b64(signature.data(),signatureSize);
    }
};
#endif
}

int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--status"){
        const auto status=ark::chatGptAuthStatus();
        std::cout<<"available="<<(status.available?"true":"false")<<'\n'
                 <<"connected="<<(status.connected?"true":"false")<<'\n'
                 <<"plan_authorized="<<(status.planAuthorized?"true":"false")<<'\n'
                 <<"account_count="<<status.accounts.size()<<'\n';
        return 0;
    }
    if(argc==2&&std::string(argv[1])=="--catalog"){
        try{
            for(const auto& model:ark::chatGptListModels()){
                std::cout<<"slug="<<model.slug;
                for(const auto& level:model.reasoningEfforts)std::cout<<" effort="<<level;
                if(!model.defaultReasoningEffort.empty())std::cout<<" default_effort="<<model.defaultReasoningEffort;
                std::cout<<'\n';
            }
            return 0;
        }catch(const std::exception&){std::cerr<<"catalog unavailable\n";return 1;}
    }
    if(argc!=1)return 2;
    try{
        auto catalog=ark::parseChatGptModelCatalog(json(R"({"models":[
          {"visibility":"list","slug":"gpt-test","display_name":"Test","supported_reasoning_levels":[{"effort":"low"},{"effort":"medium"},{"effort":"xhigh"},{"effort":"max"},{"effort":"ultra"},{"effort":"unknown"},{"effort":"low"}],"default_reasoning_level":"medium"},
          {"visibility":"hidden","slug":"gpt-hidden","display_name":"Hidden"},
          {"visibility":"list","slug":"gpt-test","display_name":"Duplicate"}
        ]})"));
        check(catalog.size()==1&&catalog[0].slug=="gpt-test"&&
              catalog[0].reasoningEfforts==std::vector<std::string>({"low","medium","xhigh","max","ultra"})&&
              catalog[0].defaultReasoningEffort=="medium",
              "catalog parser keeps only explicit, valid ordered effort levels and default");
        auto unknown=ark::parseChatGptModelCatalog(json(R"({"models":[
          {"visibility":"list","slug":"gpt-plain","display_name":"Plain"},
          {"visibility":"list","slug":"gpt-bad","display_name":"Bad","supported_reasoning_levels":[{"effort":"unknown"}],"default_reasoning_level":"high"}
        ]})"));
        check(unknown.size()==2&&unknown[0].reasoningEfforts.empty()&&unknown[0].defaultReasoningEffort.empty()&&
              unknown[1].reasoningEfforts.empty()&&unknown[1].defaultReasoningEffort.empty(),
              "missing or inconsistent catalog effort metadata never invents support");
#ifdef _WIN32
        const std::string state(43,'s'),nonce(43,'n');
        const std::string verifier="dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
        auto first=ark::makeChatGptAuthAttempt("urn:uuid:11111111-2222-4333-8444-555555555555","http://127.0.0.1:1455/auth/callback",{}, {},{},{},state,nonce,verifier);
        check(first.newRegistration&&first.clientId=="dynamic_agent_client"&&first.codeChallenge=="E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM","RFC7636 S256 PKCE and dynamic client");
        check(first.authorizationUrl.rfind("https://auth.openai.com/api/accounts/authorize?",0)==0&&first.authorizationUrl.find("ext_agent_host_id=")!=std::string::npos&&first.authorizationUrl.find("agent_name_hint=ARK-Point")!=std::string::npos,"new registration URL has host and app name");
        check(first.authorizationUrl.find("127.0.0.1%3A1455%2Fauth%2Fcallback")!=std::string::npos&&first.authorizationUrl.find("localhost")==std::string::npos,"loopback redirect uses exact IPv4 URI");
        auto callback=ark::parseChatGptCallback("/auth/callback?code=abc123&state="+state+"&client_id=oaiapp_offline",first);check(callback.code=="abc123"&&callback.issuedClientId=="oaiapp_offline","callback binds issued client ID");
        check(rejects([&]{ark::parseChatGptCallback("/auth/callback?code=x&state=wrong&client_id=oaiapp_offline",first);}),"state mismatch rejects");
        check(rejects([&]{ark::parseChatGptCallback("/auth/callback?code=x&state="+state,first);}),"missing issued client ID rejects");
        check(rejects([&]{ark::parseChatGptCallback("/auth/callback?error=access_denied&state="+state,first);}),"declined consent rejects");
        auto returning=ark::makeChatGptAuthAttempt(first.hostId,first.redirectUri,"oaiapp_offline","subject","retained.id.token","person@example.test",state,nonce,verifier);
        check(!returning.newRegistration&&returning.authorizationUrl.find("agent_name_hint=")==std::string::npos&&returning.authorizationUrl.find("id_token_hint=")!=std::string::npos,"returning sign-in reuses client and hint");
        auto consent=ark::makeChatGptAuthAttempt(first.hostId,first.redirectUri,"oaiapp_offline","subject",{}, {},state,nonce,verifier,true);check(consent.authorizationUrl.find("prompt=consent")!=std::string::npos,"explicit enable-plan action requests consent on saved registration");
        check(ark::parseChatGptCallback("/auth/callback?code=x&state="+state,returning).issuedClientId=="oaiapp_offline","returning callback may omit client ID");
        check(rejects([&]{ark::parseChatGptCallback("/auth/callback?code=x&state="+state+"&client_id=oaiapp_other",returning);}),"returning client mismatch rejects");

        SigningKey key;const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();V claims=V::makeObj();claims.set("iss",V::makeStr("https://auth.openai.com"));claims.set("aud",V::makeStr("oaiapp_offline"));claims.set("sub",V::makeStr("subject"));claims.set("nonce",V::makeStr(nonce));claims.set("iat",V::makeNum((double)now));claims.set("exp",V::makeNum((double)(now+3600)));claims.set("email",V::makeStr("person@example.test"));
        auto valid=key.sign(claims);auto identity=ark::verifyChatGptIdToken(valid,key.jwks,"oaiapp_offline",nonce,now);check(identity.subject=="subject"&&identity.email=="person@example.test","CNG verifies RSA signature and identity");
        check(rejects([&]{ark::verifyChatGptIdToken(valid,key.jwks,"oaiapp_other",nonce,now);}),"audience mismatch rejects");
        check(rejects([&]{ark::verifyChatGptIdToken(valid,key.jwks,"oaiapp_offline",std::string(43,'x'),now);}),"nonce mismatch rejects");
        check(rejects([&]{ark::verifyChatGptIdToken(key.sign(claims,"none"),key.jwks,"oaiapp_offline",nonce,now);}),"non-RS256 algorithm rejects");
        check(rejects([&]{ark::verifyChatGptIdToken(key.sign(claims,"RS256","unknown"),key.jwks,"oaiapp_offline",nonce,now);}),"unknown key ID rejects");
        auto tampered=valid;tampered.back()=tampered.back()=='A'?'B':'A';check(rejects([&]{ark::verifyChatGptIdToken(tampered,key.jwks,"oaiapp_offline",nonce,now);}),"signature tamper rejects");
        V wrong=claims;wrong.set("iss",V::makeStr("https://example.invalid"));check(rejects([&]{ark::verifyChatGptIdToken(key.sign(wrong),key.jwks,"oaiapp_offline",nonce,now);}),"issuer mismatch rejects");
        wrong=claims;wrong.set("sub",V::makeStr(""));check(rejects([&]{ark::verifyChatGptIdToken(key.sign(wrong),key.jwks,"oaiapp_offline",nonce,now);}),"empty subject rejects");
        wrong=claims;wrong.set("exp",V::makeNum((double)(now-100)));check(rejects([&]{ark::verifyChatGptIdToken(key.sign(wrong),key.jwks,"oaiapp_offline",nonce,now);}),"expired ID token rejects");
        V wrongJwks=key.jwks;V changed=wrongJwks.find("keys")->arr[0];changed.set("use",V::makeStr("enc"));V keys=V::makeArr();keys.arr.push_back(changed);wrongJwks.set("keys",keys);check(rejects([&]{ark::verifyChatGptIdToken(valid,wrongJwks,"oaiapp_offline",nonce,now);}),"JWK encryption use rejects");

        const auto directory=std::filesystem::temp_directory_path()/("ark-siwc-test-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(directory);auto path=directory/"siwc-fixture.dpapi";auto utf=path.u8string();check(ark::chatGptDpapiRoundTripForTest(std::string(utf.begin(),utf.end())),"DPAPI fixture round-trips inside isolated temp dir");std::ifstream saved(path,std::ios::binary);const std::string ciphertext((std::istreambuf_iterator<char>(saved)),std::istreambuf_iterator<char>());saved.close();check(ciphertext.find("offline-refresh-token")==std::string::npos,"DPAPI file does not expose token bytes");std::filesystem::remove(path);std::filesystem::remove(directory);
#endif
        auto completion=ark::parseChatGptSse("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello\"}\n\ndata: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[],\"usage\":{\"input_tokens\":1,\"output_tokens\":1,\"total_tokens\":2}}}\n\n");check(completion.find("status")->str=="completed"&&completion.find("usage")->find("total_tokens")->num==2,"SSE consumes completed response and usage");
        const std::string doneReason="data: {\"type\":\"response.output_item.done\",\"output_index\":0,\"item\":{\"type\":\"reasoning\",\"id\":\"rs_fixture\",\"encrypted_content\":\"offline-opaque\"}}\n\n";
        const std::string doneCall="data: {\"type\":\"response.output_item.done\",\"output_index\":1,\"item\":{\"type\":\"function_call\",\"name\":\"doc_info\",\"namespace\":\"ark\",\"call_id\":\"fixture_call\",\"arguments\":\"{}\"}}\n\n";
        const std::string emptyTerminal="data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[],\"usage\":{\"input_tokens\":141,\"output_tokens\":13,\"total_tokens\":154}}}\n\n";
        auto restored=ark::parseChatGptSse(doneReason+doneCall+emptyTerminal);auto& replay=restored.find("output")->arr;
        check(replay.size()==2&&replay[0].find("encrypted_content")->str=="offline-opaque"&&replay[1].find("call_id")->str=="fixture_call"&&restored.find("usage")->find("total_tokens")->num==154,"empty terminal output reconstructs complete reasoning and tool items in order with usage");
        check(ark::parseChatGptSse(doneReason+doneCall+doneCall+emptyTerminal).find("output")->arr.size()==2,"identical output completion cannot duplicate a tool");
        auto conflictingCall=doneCall;conflictingCall.replace(conflictingCall.find("doc_info"),8,"doc_get");
        check(rejects([&]{ark::parseChatGptSse(doneReason+doneCall+conflictingCall+emptyTerminal);}),"conflicting output completion rejects before tool dispatch");
        check(rejects([&]{ark::parseChatGptSse(doneCall+emptyTerminal);}),"missing output item rejects instead of silently losing reasoning history");
        check(rejects([&]{ark::parseChatGptSse(doneReason+doneCall);}),"completed tool item alone never authorizes dispatch before terminal success");
        auto fullTerminal=json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}");auto fullResponse=*fullTerminal.find("response");fullResponse.set("output",*restored.find("output"));fullTerminal.set("response",fullResponse);
        check(ark::parseChatGptSse(doneReason+doneCall+"data: "+fullTerminal.dump()+"\n\n").find("output")->arr.size()==2,"assembled terminal output is not appended twice");
        auto billedFailure=ark::parseChatGptSse(doneReason+doneCall+"data: {\"type\":\"response.failed\",\"response\":{\"status\":\"failed\",\"error\":{\"code\":\"subscription_sharing_usage_limit_exceeded\"},\"usage\":{\"input_tokens\":4,\"output_tokens\":1,\"total_tokens\":5}}}\n\n");check(billedFailure.find("status")->str=="failed"&&billedFailure.find("usage")->find("total_tokens")->num==5&&!billedFailure.find("output"),"failed terminal SSE preserves usage without promoting streamed tool items");
        check(rejects([]{ark::parseChatGptSse("data: {\"type\":\"response.failed\"}\n\n");}),"SSE failed event rejects");
        check(rejects([]{ark::parseChatGptSse("data: {\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}\n\n");}),"interrupted SSE rejects");
        V tools=json(R"([{"name":"doc_info","description":"read","inputSchema":{"type":"object"}},{"name":"usage_record","description":"external receipt","inputSchema":{"type":"object"}}])");ark::AssistantConfig config;config.authMode=ark::AssistantAuthMode::ChatGptPlan;config.model="account-model";V history=json(R"([{"role":"user","content":"inspect"}])");V request=ark::assistantRequest(config,history,tools);check(request.find("store")->t==V::Bool&&!request.find("store")->b&&request.find("stream")->b,"ChatGPT plan request is ephemeral streamed Responses");const auto& functions=request.find("tools")->arr;check(functions.size()==1&&functions[0].find("type")->str=="namespace"&&functions[0].find("name")->str=="ark"&&functions[0].find("tools")->arr.size()==1,"ARK tools are namespaced and token receipt tool is hidden from model");
        config.endpoint="https://example.invalid/v1/responses";check(rejects([&]{ark::assistantRequest(config,history,tools);}),"OAuth bearer cannot be sent to a custom endpoint");
        config.endpoint="https://api.openai.com/v1/responses";int calls=0,rounds=0;
        ark::AssistantTransport transport=[&](const ark::AssistantConfig&,const V&,const std::atomic_bool*){return ++rounds==1?json(R"({"status":"completed","output":[{"type":"function_call","namespace":"ark","name":"doc_info","call_id":"call_1","arguments":"{}"}],"usage":{"input_tokens":10,"output_tokens":2,"total_tokens":12}})"):json(R"({"status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":"done"}]}],"usage":{"input_tokens":3,"output_tokens":1,"total_tokens":4}})");};
        auto run=ark::runAssistant(config,V::makeStr("inspect"),tools,[&](const std::string& name,const V&){++calls;check(name=="doc_info","only expected ARK tool dispatched");return V::makeObj();},{},nullptr,transport);
        check(run.text=="done"&&run.usage.totalTokens==16&&calls==1,"namespaced function call dispatches once through allowlist");
        calls=0;ark::AssistantTransport wrongNamespace=[](const ark::AssistantConfig&,const V&,const std::atomic_bool*){return json(R"({"status":"completed","output":[{"type":"function_call","namespace":"other","name":"doc_info","call_id":"call_bad","arguments":"{}"}]})");};
        check(rejects([&]{ark::runAssistant(config,V::makeStr("inspect"),tools,[&](const std::string&,const V&){++calls;return V::makeObj();},{},nullptr,wrongNamespace);})&&calls==0,"wrong namespace rejects before any document mutation");
        ark::AssistantUsage billed{};ark::AssistantTransport failed=[](const ark::AssistantConfig&,const V&,const std::atomic_bool*){return json(R"({"status":"failed","output":[],"error":{"code":"subscription_sharing_usage_limit_exceeded"},"usage":{"input_tokens":4,"output_tokens":1,"total_tokens":5}})");};
        std::string failureMessage;try{ark::runAssistant(config,V::makeStr("inspect"),tools,[&](const std::string&,const V&){++calls;return V::makeObj();},[&](const ark::AssistantEvent& event){if(event.kind==ark::AssistantEvent::Usage)billed=event.usage;},nullptr,failed);}catch(const std::exception& error){failureMessage=error.what();}
        check(failureMessage.find("Settings > Usage")!=std::string::npos&&failureMessage.find("Allow other apps to use credits")!=std::string::npos&&failureMessage.find("100% app limit does not enable credits")!=std::string::npos&&billed.totalTokens==5&&calls==0,"plan-limit failure explains credit permission, retains billed tokens, executes no tools");
        ark::AssistantTransport unavailable=[](const ark::AssistantConfig&,const V&,const std::atomic_bool*){return json(R"({"status":"failed","output":[],"error":{"code":"subscription_sharing_usage_unavailable"}})");};
        failureMessage.clear();try{ark::runAssistant(config,V::makeStr("inspect"),tools,[&](const std::string&,const V&){++calls;return V::makeObj();},{},nullptr,unavailable);}catch(const std::exception& error){failureMessage=error.what();}
        check(failureMessage.find("try again later")!=std::string::npos&&calls==0,"temporary plan-usage failure preserves credentials and requests later retry");
        std::cout<<"ChatGPT auth protocol tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
