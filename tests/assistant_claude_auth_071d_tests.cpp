#include "ark/assistant_claude_auth_071d.hpp"
#include "ark/assistant_connectors.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace ark;
using V=arkjson::Value;
using namespace std::chrono_literals;
static unsigned checks=0;
static void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
static V parsed(const char* text){V value;if(!arkjson::parse(text,value))throw std::runtime_error("Bad fixture");return value;}
static V metadata(){return parsed(R"({"models":[{"value":"synthetic"}],"sdk_mcp_listed":true,"prompt_sent":false})");}
struct ScopedEnv {
    std::string name,old;bool existed=false;
    ScopedEnv(std::string key,const char* value):name(std::move(key)){if(const char* previous=std::getenv(name.c_str())){old=previous;existed=true;}
#ifdef _WIN32
        _putenv_s(name.c_str(),value?value:"");
#else
        if(value)setenv(name.c_str(),value,1);else unsetenv(name.c_str());
#endif
    }
    ~ScopedEnv(){
#ifdef _WIN32
        _putenv_s(name.c_str(),existed?old.c_str():"");
#else
        if(existed)setenv(name.c_str(),old.c_str(),1);else unsetenv(name.c_str());
#endif
    }
};
static ClaudeAuthState071 finish(AssistantClaudeAuth071& auth){const auto deadline=std::chrono::steady_clock::now()+8s;while(auth.snapshot().running){if(std::chrono::steady_clock::now()>deadline){auth.cancel();auth.shutdown();throw std::runtime_error("Offline auth fixture timed out");}std::this_thread::sleep_for(5ms);}auth.shutdown();return auth.snapshot();}
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="auth"&&std::string(argv[2])=="status"){
        const char* directory=std::getenv("CLAUDE_CONFIG_DIR");
        const bool isolated=!std::getenv("CLAUDECODE")&&!std::getenv("CLAUDE_CODE_ENTRYPOINT")&&!std::getenv("CLAUDE_CODE_SESSION_TEST")&&directory&&std::string(directory)=="synthetic-config-owner";
        std::cout<<(isolated?R"({"loggedIn":true,"authMethod":"claude.ai","email":"synthetic-private@example.invalid"})":R"({"loggedIn":false,"authMethod":"none"})");return isolated?0:1;
    }
    try{
        ScopedEnv oauth("CLAUDE_CODE_OAUTH_TOKEN",nullptr),key("ANTHROPIC_API_KEY",nullptr),token("ANTHROPIC_AUTH_TOKEN",nullptr);
        AssistantConfig config=assistantProviderPreset("claude-code");auto path=std::filesystem::absolute(argv[0]).u8string();config.endpoint=std::string(path.begin(),path.end());
        std::atomic_int logins=0,statuses=0,probes=0;
        ClaudeAuthDependencies071 dependencies;
        dependencies.run=[&](const auto& executable,const auto& args,bool interactive,const std::atomic_bool*){if(executable!=config.endpoint)throw std::runtime_error("Changed CLI identity");if(interactive){++logins;if(args!=std::vector<std::string>{"auth","login"})throw std::runtime_error("Wrong official login command");return ClaudeAuthProcessResult071{0,false,{}};}++statuses;if(args!=std::vector<std::string>{"auth","status"})throw std::runtime_error("Wrong metadata command");return ClaudeAuthProcessResult071{0,false,R"({"loggedIn":true,"authMethod":"claude.ai","email":"never-copy@example.invalid","token":"synthetic-private-marker"})"};};
        dependencies.probe=[&](const auto&,const std::atomic_bool*){++probes;return metadata();};
        {AssistantClaudeAuth071 auth(dependencies);check(auth.start(config,true),"Interactive login did not start");auto state=finish(auth);check(state.phase==ClaudeAuthPhase071::Ready&&state.loggedIn&&state.sdkReady&&state.renewedSavedLogin,"Successful official login and metadata did not become ready");check(logins==1&&statuses==1&&probes==1,"Successful flow executed unexpected operations");check(state.message.find("never-copy")==std::string::npos&&state.message.find("synthetic-private")==std::string::npos,"Private metadata reached user status");check(state.message.find("No inference")!=std::string::npos,"Metadata check claimed model inference/access");}
        {AssistantClaudeAuth071 auth(dependencies);auth.start(config,false);auto state=finish(auth);check(state.phase==ClaudeAuthPhase071::Ready&&!state.renewedSavedLogin&&logins==1,"A metadata-only check was treated as re-authentication");}
        for(const auto* variable:{"CLAUDE_CODE_OAUTH_TOKEN","ANTHROPIC_API_KEY","ANTHROPIC_AUTH_TOKEN"}){ScopedEnv override(variable,"synthetic-never-copy");AssistantClaudeAuth071 auth(dependencies);auth.start(config,true);auto state=finish(auth);check(state.phase==ClaudeAuthPhase071::Ready&&state.externalCredentialOverride&&!state.renewedSavedLogin,"Environment credential was falsely renewed by browser login");check(state.message.find("synthetic-never-copy")==std::string::npos,"Credential override value leaked");}
        {auto other=dependencies;other.run=[](auto&,auto&,bool interactive,auto*){return ClaudeAuthProcessResult071{interactive?0:1,false,interactive?"":R"({"loggedIn":false,"authMethod":"none"})"};};AssistantClaudeAuth071 auth(other);auth.start(config,true);check(finish(auth).phase==ClaudeAuthPhase071::NotConnected,"Login exit zero without an account was accepted");}
        for(const char* text:{R"({"loggedIn":true})",R"({"loggedIn":true,"authMethod":"unrecognized"})","not json",R"({"loggedIn":"true","authMethod":"claude.ai"})"}){auto invalid=dependencies;invalid.run=[text](auto&,auto&,bool,auto*){return ClaudeAuthProcessResult071{0,false,text};};AssistantClaudeAuth071 auth(invalid);auth.start(config,false);check(finish(auth).phase==ClaudeAuthPhase071::Failed,"Malformed account status was accepted");}
        {auto api=dependencies;api.run=[](auto&,auto&,bool interactive,auto*){return ClaudeAuthProcessResult071{0,false,interactive?"":R"({"loggedIn":true,"authMethod":"api_key"})"};};AssistantClaudeAuth071 auth(api);auth.start(config,true);auto state=finish(auth);check(state.phase==ClaudeAuthPhase071::Ready&&!state.renewedSavedLogin,"API-key route was silently replaced by subscription login");}
        {auto bad=dependencies;bad.probe=[](auto&,auto*){auto data=metadata();data.set("prompt_sent",V::makeBool(true));return data;};AssistantClaudeAuth071 auth(bad);auth.start(config,false);check(finish(auth).phase==ClaudeAuthPhase071::Failed,"Inference-bearing probe was accepted as metadata-only");}
        {auto bad=dependencies;bad.probe=[](auto&,auto*)->V{throw std::runtime_error("synthetic-private-provider-text");};AssistantClaudeAuth071 auth(bad);auth.start(config,false);auto state=finish(auth);check(state.phase==ClaudeAuthPhase071::Failed&&state.message.find("synthetic-private-provider-text")==std::string::npos,"Raw provider failure text leaked through authentication UI");}
        {std::atomic_bool entered=false;auto pending=dependencies;pending.run=[&](auto&,auto&,bool,auto* cancel){entered=true;while(!cancel->load())std::this_thread::sleep_for(2ms);return ClaudeAuthProcessResult071{-1,true,{}};};AssistantClaudeAuth071 auth(pending);auth.start(config,true);const auto deadline=std::chrono::steady_clock::now()+2s;while(!entered&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(2ms);check(entered,"Owned auth fixture did not begin");check(!auth.start(config,true),"Duplicate concurrent login was launched");auth.cancel();check(finish(auth).phase==ClaudeAuthPhase071::Cancelled,"Owned login cancellation failed");}
#ifdef _WIN32
        {ScopedEnv marker("CLAUDECODE","synthetic-parent"),entry("CLAUDE_CODE_ENTRYPOINT","synthetic-entry"),session("CLAUDE_CODE_SESSION_TEST","synthetic-session"),directory("CLAUDE_CONFIG_DIR","synthetic-config-owner");ClaudeAuthDependencies071 actual;actual.probe=[](auto&,auto*){return metadata();};AssistantClaudeAuth071 auth(actual);auth.start(config,false);auto state=finish(auth);check(state.phase==ClaudeAuthPhase071::Ready&&!state.interactiveLogin,"Actual hidden process did not preserve auth routing/filter parent markers");check(std::getenv("CLAUDECODE")&&std::string(std::getenv("CLAUDECODE"))=="synthetic-parent","Child environment filtering modified the parent");}
#endif
        std::cout<<"Claude auth 071D: "<<checks<<" checks passed (offline process seams, no actual login/inference)\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Claude auth 071D failed: "<<error.what()<<'\n';return 1;}
}
