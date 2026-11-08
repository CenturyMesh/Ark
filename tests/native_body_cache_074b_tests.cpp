#include "ark/agent.hpp"
#include "ark/native_body_cache_074b.hpp"
#include "ark/native_backend.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using namespace ark;using V=arkjson::Value;namespace fs=std::filesystem;size_t checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}const V& at(const V& v,const char* key){auto p=v.find(key);if(!p)throw std::runtime_error(key);return *p;}V json(const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("fixture JSON");return v;}
struct Env{const char* name;std::string old;Env(const char* n,std::string value):name(n),old(std::getenv(n)?std::getenv(n):""){set(value);}void set(const std::string& value){
#ifdef _WIN32
 _putenv_s(name,value.c_str());
#else
 if(value.empty())unsetenv(name);else setenv(name,value.c_str(),1);
#endif
}~Env(){set(old);}};
struct Workspace{fs::path parent,root;Workspace(){parent=fs::weakly_canonical(fs::temp_directory_path());root=parent/("ark-body-memo-074b-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(root),"Create isolated fixture workspace");}~Workspace(){std::error_code error;const auto resolved=fs::weakly_canonical(root,error);if(!error&&resolved.parent_path()==parent&&!fs::is_symlink(root,error))fs::remove_all(resolved,error);}};
std::string path(const fs::path& value){auto bytes=value.u8string();return {bytes.begin(),bytes.end()};}
V call(AgentSession& s,const char* tool,V args){if(tool==std::string("doc_apply")||tool==std::string("doc_load"))args.set("expected_revision",V::makeNum(s.revision));auto r=s.call(tool,args);if(at(r,"isError").b)throw std::runtime_error(r.dump());return at(r,"structuredContent");}
std::string read(const fs::path& name){std::ifstream input(name,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
#if defined(ARK_NATIVE_BODY_MEMO_TESTS) && defined(_WIN32)
void run(){Workspace work;fs::create_directory(work.root/"cache");Env directory("ARK_NATIVE_CACHE_DIRECTORY",path(work.root/"cache")),enabled("ARK_NATIVE_RESULT_CACHE","1"),bodyEnabled("ARK_NATIVE_BODY_CACHE","1"),ram("ARK_NATIVE_MEMORY_CACHE_MAX_MB","256"),cpu("ARK_CPU_THREADS","4"),workers("ARK_FEATURE_THREADS","1"),native("ARK_NATIVE_THREADS","1");clearNativeBodyCache074B();AgentSession s;s.suppressIterationHistory();s.suppressFailureCapture();
 call(s,"doc_apply",json(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Sphere","native":{"op":"sphere"},"p":{"radius":50}}]})"));const auto original=s.document.evalBodies[0];const auto originalPayload=original.nativeData.dump();const auto originalMesh=original.baked.get();const auto originalOwner=original.nativeData.ownerIdentity();
 call(s,"doc_apply",json(R"({"commands":[{"op":"feature.edit","id":1,"p":{"radius":51}}]})"));const auto before=nativeBodyCacheInfo074B();call(s,"doc_apply",json(R"({"commands":[{"op":"feature.edit","id":1,"p":{"radius":50}}]})"));const auto after=nativeBodyCacheInfo074B();
 check(at(after,"hits").num==at(before,"hits").num+1&&at(after,"materializations").num==at(before,"materializations").num,"Returning to an authenticated old result rebuilt its CPU body");check(s.document.evalBodies[0].baked.get()==originalMesh&&s.document.evalBodies[0].nativeData.ownerIdentity()==originalOwner,"Across-edit reuse did not share immutable buffers");check(s.document.evalBodies[0].nativeData.dump()==originalPayload,"Body memo changed producer/host evidence");
 const auto file=work.root/"source.ark";check(saveDocument(s.document,path(file).c_str()),"Save reopen fixture");AgentSession opened;opened.suppressIterationHistory();opened.suppressFailureCapture();V load=V::makeObj();load.set("path",V::makeStr(path(file)));const auto hits=at(nativeBodyCacheInfo074B(),"hits").num;const auto authenticated=at(nativeKernelInfo(),"native_result_cache_hits").num;call(opened,"doc_load",load);
 check(at(nativeBodyCacheInfo074B(),"hits").num>hits&&at(nativeKernelInfo(),"native_result_cache_hits").num>authenticated,"Reopen must authenticate current native result before sharing CPU buffers");check(opened.document.evalBodies[0].nativeData.ownerIdentity()==originalOwner&&opened.document.evalBodies[0].baked.get()==originalMesh,"Reopen did not reuse validated materialization");
 opened.document.evalBodies[0].nativeData.set("caller_only",V::makeBool(true));opened.document.evalBodies[0].baked.mutate([](Mesh& mesh){mesh.pos[0]+=100;});AgentSession isolated;isolated.suppressIterationHistory();isolated.suppressFailureCapture();call(isolated,"doc_load",load);check(!isolated.document.evalBodies[0].nativeData.find("caller_only")&&isolated.document.evalBodies[0].baked.get()==originalMesh,"Caller mutations contaminated memoized evidence or mesh");
 const auto key=at(original.nativeData,"native_cache_key").str;const auto cacheFile=work.root/"cache"/(key+".arknative");auto damaged=read(cacheFile);check(!damaged.empty(),"Authenticated disk entry missing");damaged.back()='!';{std::ofstream out(cacheFile,std::ios::binary|std::ios::trunc);out<<damaged;}
 const auto misses=at(nativeKernelInfo(),"native_result_cache_misses").num;AgentSession repaired;repaired.suppressIterationHistory();repaired.suppressFailureCapture();call(repaired,"doc_load",load);check(at(nativeKernelInfo(),"native_result_cache_misses").num>misses&&read(cacheFile)!=damaged,"Remembered CPU body masked corrupt current disk evidence");const auto& report=at(repaired.document.evalBodies[0].nativeData,"report");check(std::abs(at(report,"volume_mm3").num-4*std::acos(-1.)*125000/3)<1e-5,"Revalidated sphere has wrong analytic volume");
 repaired.document.assemblyLoadSelection=V::makeArr();repaired.document.recompute();check(repaired.document.evalBodies.empty()&&at(nativeBodyCacheInfo074B(),"entries").num==0,"Selective unload retained global evaluated buffers");
 bodyEnabled.set("0");const auto cleared=nativeBodyCacheInfo074B();check(at(cleared,"entries").num==0&&at(cleared,"target_bytes").num==0,"Explicit body memo disable must release retention");
}
#endif
}
int main(){try{
#if defined(ARK_NATIVE_BODY_MEMO_TESTS) && defined(_WIN32)
run();
#endif
std::cout<<"074B native body memo: "<<checks<<" authenticated across-edit/reopen, shared ownership, corruption and release checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
