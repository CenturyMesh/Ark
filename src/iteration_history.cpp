#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr size_t maxNodes=512;
const V& need(const V& v,const char* key){const V* p=v.find(key);if(!p)throw std::runtime_error(std::string("History missing ")+key);return *p;}
std::string text(const V& v,const char* key){const V& p=need(v,key);if(p.t!=V::Str)throw std::runtime_error(std::string("History text required: ")+key);return p.str;}
V& member(V& v,const char* key){for(auto& p:v.obj)if(p.first==key)return p.second;throw std::runtime_error(std::string("History missing ")+key);}
void erase(V& v,const std::string& key){v.obj.erase(std::remove_if(v.obj.begin(),v.obj.end(),[&](const auto& p){return p.first==key;}),v.obj.end());}
void canonical(V& value){if(value.t==V::Obj){for(auto& item:value.obj)canonical(item.second);std::sort(value.obj.begin(),value.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(value.t==V::Arr)for(auto& item:value.arr)canonical(item);}
bool digest(const std::string& s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string utc(){auto now=std::time(nullptr);std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&now);
#else
    gmtime_r(&now,&tm);
#endif
    char buf[32];std::strftime(buf,sizeof buf,"%Y-%m-%dT%H:%M:%SZ",&tm);return buf;
}
void validateActor(const V& actor){
    if(actor.t!=V::Obj)throw std::runtime_error("actor must be an object");
    for(const auto& item:actor.obj){if(item.first!="kind"&&item.first!="id"&&item.first!="role"&&item.first!="model")throw std::runtime_error("actor accepts kind, id and optional role/model");if(item.second.t!=V::Str||item.second.str.empty()||item.second.str.size()>256)throw std::runtime_error("actor fields require 1..256 text bytes");}
    (void)text(actor,"kind");(void)text(actor,"id");
}
V pack(const V& value,V& blobs){
    if(value.t==V::Str&&value.str.size()>=1024){const std::string hash=iterationSha256(value.str);blobs.set(hash,value);V ref=V::makeObj();ref.set("$ark_history_blob",V::makeStr(hash));return ref;}
    V out=value;
    if(out.t==V::Obj){if(out.find("$ark_history_blob"))throw std::runtime_error("Reserved history blob marker in authoring data");for(auto& item:out.obj)item.second=pack(item.second,blobs);}
    if(out.t==V::Arr)for(auto& item:out.arr)item=pack(item,blobs);
    return out;
}
V unpackBounded(const V& value,const V& blobs,size_t& used){
    auto charge=[&](size_t n){if(n>maxDocumentBytes-used)throw std::runtime_error("History logical authoring capacity exceeds 4 MiB");used+=n;};
    if(value.t==V::Obj&&value.find("$ark_history_blob")){if(value.obj.size()!=1)throw std::runtime_error("Invalid history blob marker");const std::string hash=text(value,"$ark_history_blob");const V* bytes=blobs.find(hash.c_str());if(!digest(hash)||!bytes||bytes->t!=V::Str)throw std::runtime_error("History blob reference missing");charge(bytes->str.size());return *bytes;}
    charge(value.t==V::Str?value.str.size():value.t==V::Obj||value.t==V::Arr?2:8);
    V out=value;if(out.t==V::Obj)for(auto& item:out.obj){charge(item.first.size()+3);item.second=unpackBounded(item.second,blobs,used);}if(out.t==V::Arr)for(auto& item:out.arr)item=unpackBounded(item,blobs,used);return out;
}
V unpack(const V& value,const V& blobs){size_t used=0;return unpackBounded(value,blobs,used);}
void diff(const V& before,const V& after,V path,V& delta,V& blobs){
    if(before.dump()==after.dump())return;
    if(before.t==V::Obj&&after.t==V::Obj){
        for(const auto& old:before.obj)if(!after.find(old.first.c_str())){V op=V::makeObj(),p=path;p.arr.push_back(V::makeStr(old.first));op.set("op",V::makeStr("erase"));op.set("path",p);delta.arr.push_back(op);}
        for(const auto& item:after.obj){V p=path;p.arr.push_back(V::makeStr(item.first));if(auto old=before.find(item.first.c_str()))diff(*old,item.second,p,delta,blobs);else{V op=V::makeObj();op.set("op",V::makeStr("set"));op.set("path",p);op.set("value",pack(item.second,blobs));delta.arr.push_back(op);}}return;
    }
    if(before.t==V::Arr&&after.t==V::Arr){
        for(size_t i=0;i<std::min(before.arr.size(),after.arr.size());++i){V p=path;p.arr.push_back(V::makeNum((double)i));diff(before.arr[i],after.arr[i],p,delta,blobs);}
        for(size_t i=before.arr.size();i>after.arr.size();--i){V op=V::makeObj(),p=path;p.arr.push_back(V::makeNum((double)(i-1)));op.set("op",V::makeStr("erase"));op.set("path",p);delta.arr.push_back(op);}
        for(size_t i=before.arr.size();i<after.arr.size();++i){V op=V::makeObj(),p=path;p.arr.push_back(V::makeNum((double)i));op.set("op",V::makeStr("set"));op.set("path",p);op.set("value",pack(after.arr[i],blobs));delta.arr.push_back(op);}return;
    }
    V op=V::makeObj();op.set("op",V::makeStr("set"));op.set("path",path);op.set("value",pack(after,blobs));delta.arr.push_back(op);
}
size_t index(const V& v,size_t n){if(v.t!=V::Num||v.num<0||v.num!=std::floor(v.num)||v.num>=(double)n)throw std::runtime_error("Invalid history array index");return (size_t)v.num;}
void apply(V& state,const V& delta,const V& blobs){
    if(delta.t!=V::Arr||delta.arr.size()>65536)throw std::runtime_error("Invalid authoring delta");
    size_t stateBytes=state.dump().size();auto resize=[&](size_t removed,size_t added){if(removed>stateBytes||added>maxDocumentBytes-(stateBytes-removed))throw std::runtime_error("History logical authoring capacity exceeds 4 MiB");stateBytes=stateBytes-removed+added;};
    for(const auto& operation:delta.arr){const V& path=need(operation,"path");const std::string op=text(operation,"op");if(path.t!=V::Arr||path.arr.size()>64||(op!="set"&&op!="erase"))throw std::runtime_error("Invalid history delta path/op");
        V* target=&state;
        for(size_t i=0;i+1<path.arr.size();++i){const auto& key=path.arr[i];if(target->t==V::Obj&&key.t==V::Str)target=&member(*target,key.str.c_str());else if(target->t==V::Arr)target=&target->arr[index(key,target->arr.size())];else throw std::runtime_error("History delta path not found");}
        if(path.arr.empty()){if(op!="set")throw std::runtime_error("Cannot erase history root");V next=unpack(need(operation,"value"),blobs);resize(stateBytes,next.dump().size());state=std::move(next);continue;}
        const V& key=path.arr.back();if(target->t==V::Obj&&key.t==V::Str){const V* old=target->find(key.str.c_str());if(op=="erase"){if(!old)throw std::runtime_error("History erase path not found");resize(old->dump().size()+key.dump().size()+1+(target->obj.size()>1?1:0),0);erase(*target,key.str);}else{V next=unpack(need(operation,"value"),blobs);resize(old?old->dump().size():0,next.dump().size()+(old?0:key.dump().size()+1+(target->obj.empty()?0:1)));target->set(key.str,std::move(next));}}
        else if(target->t==V::Arr){if(op=="set"&&key.t==V::Num&&key.num==(double)target->arr.size()){V next=unpack(need(operation,"value"),blobs);resize(0,next.dump().size()+(target->arr.empty()?0:1));target->arr.push_back(std::move(next));}else{size_t i=index(key,target->arr.size());if(op=="erase"){resize(target->arr[i].dump().size()+(target->arr.size()>1?1:0),0);target->arr.erase(target->arr.begin()+i);}else{V next=unpack(need(operation,"value"),blobs);resize(target->arr[i].dump().size(),next.dump().size());target->arr[i]=std::move(next);}}}else throw std::runtime_error("History delta target not found");
    }
    canonical(state);
}
const V* node(const V& h,const std::string& id){for(const auto& n:need(h,"nodes").arr)if(text(n,"id")==id)return &n;return nullptr;}
std::string stateHash(const V& h,const std::string& id){if(id==text(h,"baseline_id"))return text(h,"baseline_sha256");auto n=node(h,id);if(!n)throw std::runtime_error("Iteration id not found");return text(*n,"after_sha256");}
void replayBudget(size_t& used,const V& delta){if(delta.t!=V::Arr||delta.arr.size()>131072-used)throw std::runtime_error("History replay capacity exceeds 131072 delta operations");used+=delta.arr.size();}
V replay(const V& h,const std::string& id,size_t* sharedBudget=nullptr){
    std::vector<const V*> chain;std::string cursor=id;const std::string baseline=text(h,"baseline_id");while(cursor!=baseline){auto n=node(h,cursor);if(!n||chain.size()>=maxNodes)throw std::runtime_error("Iteration id not found or cyclic history");chain.push_back(n);cursor=text(*n,"parent_id");}
    size_t localBudget=0;size_t& budget=sharedBudget?*sharedBudget:localBudget;const V& blobs=need(h,"blobs");V state=unpack(need(h,"baseline"),blobs);for(auto it=chain.rbegin();it!=chain.rend();++it){replayBudget(budget,need(**it,"delta"));apply(state,need(**it,"delta"),blobs);}return state;
}
std::string nodeId(V n){erase(n,"id");canonical(n);return "iteration-"+iterationSha256(n.dump());}
V fresh(const V& baseline){V h=V::makeObj(),blobs=V::makeObj();const std::string hash=iterationSha256(baseline.dump()),id="baseline-"+hash;h.set("schema",V::makeNum(1));h.set("baseline_id",V::makeStr(id));h.set("baseline_sha256",V::makeStr(hash));h.set("baseline",pack(baseline,blobs));h.set("head_id",V::makeStr(id));h.set("nodes",V::makeArr());h.set("blobs",blobs);h.set("undo_ids",V::makeArr());h.set("redo_ids",V::makeArr());return h;}
}

std::string iterationSha256(const std::string& bytes){
    static constexpr uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};auto rr=[](uint32_t x,int n){return (x>>n)|(x<<(32-n));};
    std::vector<unsigned char> data(bytes.begin(),bytes.end());uint64_t bits=(uint64_t)data.size()*8;data.push_back(0x80);while(data.size()%64!=56)data.push_back(0);for(int i=7;i>=0;--i)data.push_back((unsigned char)(bits>>(i*8)));
    for(size_t offset=0;offset<data.size();offset+=64){uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(data[offset+i*4])<<24)|(uint32_t(data[offset+i*4+1])<<16)|(uint32_t(data[offset+i*4+2])<<8)|data[offset+i*4+3];for(int i=16;i<64;++i){uint32_t a=w[i-15],b=w[i-2];w[i]=w[i-16]+(rr(a,7)^rr(a,18)^(a>>3))+w[i-7]+(rr(b,17)^rr(b,19)^(b>>10));}uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],q=h[7];for(int i=0;i<64;++i){uint32_t t1=q+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^((~e)&g))+k[i]+w[i],t2=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));q=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=q;}
    const char* hex="0123456789abcdef";std::string out;for(auto word:h)for(int shift=28;shift>=0;shift-=4)out+=hex[(word>>shift)&15];return out;
}
V iterationActor(const V& args){if(auto actor=args.find("actor")){validateActor(*actor);return *actor;}V actor=V::makeObj();actor.set("kind",V::makeStr("client"));actor.set("id",V::makeStr("unspecified"));return actor;}
V iterationAuthoring(const Document& document){V value;if(!arkjson::parse(documentToJson(document,false),value))throw std::runtime_error("Cannot snapshot document authoring");canonical(value);return value;}
void iterationAppend(Document& after,const Document& before,const std::string& tool,const V& args,const std::string& restoredFrom){
    const V previous=iterationAuthoring(before),next=iterationAuthoring(after);V h=before.iterationHistory.t==V::Obj?before.iterationHistory:fresh(previous);
    if(need(h,"nodes").arr.size()>=maxNodes)throw std::runtime_error("Iteration history capacity exceeded (512 nodes)");
    const std::string oldHead=text(h,"head_id"),parent=restoredFrom.empty()?oldHead:restoredFrom;
    if(iterationSha256(previous.dump())!=stateHash(h,oldHead))throw std::runtime_error("Active authoring does not match iteration history head");
    V delta=V::makeArr();diff(restoredFrom.empty()?previous:replay(h,parent),next,V::makeArr(),delta,member(h,"blobs"));
    V n=V::makeObj();n.set("parent_id",V::makeStr(parent));n.set("previous_head_id",V::makeStr(oldHead));n.set("actor",iterationActor(args));n.set("provenance",V::makeStr("caller_declared"));n.set("tool",V::makeStr(tool));
    std::string motivation;if(auto m=args.find("motivation")){if(m->t!=V::Str||m->str.size()>4096)throw std::runtime_error("motivation must be text of at most 4096 bytes");motivation=m->str;}
    n.set("motivation",V::makeStr(motivation));n.set("utc",V::makeStr(utc()));n.set("before_sha256",V::makeStr(iterationSha256(previous.dump())));n.set("after_sha256",V::makeStr(iterationSha256(next.dump())));n.set("delta",delta);if(!restoredFrom.empty())n.set("restored_from_id",V::makeStr(restoredFrom));
    std::string summary=tool;if(auto commands=args.find("commands");commands&&commands->t==V::Arr&&!commands->arr.empty()){const V& first=commands->arr.front();if(auto op=first.find("op");op&&op->t==V::Str)summary=op->str;for(const char* key:{"name","id"})if(auto subject=first.find(key)){summary+=" "+(subject->t==V::Str?subject->str:subject->dump());break;}if(commands->arr.size()>1)summary+=" (+"+std::to_string(commands->arr.size()-1)+" commands)";}if(summary.size()>300)summary.resize(300);n.set("summary",V::makeStr(summary));
    // Ordinal disambiguates identical successful mutations within one UTC second.
    n.set("ordinal",V::makeNum(need(h,"nodes").arr.size()+1));const std::string id=nodeId(n);n.set("id",V::makeStr(id));member(h,"nodes").arr.push_back(n);h.set("head_id",V::makeStr(id));
    if((tool=="doc_undo"||tool=="doc_redo")&&before.iterationHistory.t==V::Obj){auto& source=member(h,tool=="doc_undo"?"undo_ids":"redo_ids");auto& destination=member(h,tool=="doc_undo"?"redo_ids":"undo_ids");if(source.arr.empty())throw std::runtime_error("No iteration history available");source.arr.pop_back();destination.arr.push_back(V::makeStr(oldHead));}
    else{member(h,"undo_ids").arr.push_back(V::makeStr(oldHead));member(h,"redo_ids").arr.clear();}
    after.iterationHistory=std::move(h);
}
V iterationInfo(const Document& document,size_t limit,const std::string& beforeId){
    if(limit<1||limit>64)throw std::runtime_error("History limit must be an integer from 1 to 64");
    V out=V::makeObj(),rows=V::makeArr(),heads=V::makeArr();const V& h=document.iterationHistory;
    out.set("head_id",V::makeStr(h.t==V::Obj?text(h,"head_id"):""));out.set("baseline_id",V::makeStr(h.t==V::Obj?text(h,"baseline_id"):""));
    if(h.t==V::Obj){const auto& nodes=need(h,"nodes").arr;std::set<std::string> parents;for(const auto& n:nodes)parents.insert(text(n,"parent_id"));for(const auto& n:nodes)if(!parents.count(text(n,"id")))heads.arr.push_back(need(n,"id"));if(nodes.empty())heads.arr.push_back(need(h,"baseline_id"));
        size_t end=nodes.size();if(!beforeId.empty()){if(beforeId==text(h,"baseline_id"))end=0;else{auto found=std::find_if(nodes.begin(),nodes.end(),[&](const V& n){return text(n,"id")==beforeId;});if(found==nodes.end())throw std::runtime_error("History before_id cursor not found");end=(size_t)(found-nodes.begin());}}
        for(size_t i=end;i>0&&rows.arr.size()<limit;--i){V meta=nodes[i-1];erase(meta,"delta");rows.arr.push_back(meta);}const bool more=end>rows.arr.size();out.set("has_more",V::makeBool(more));out.set("next_before_id",V::makeStr(more?text(rows.arr.back(),"id"):""));out.set("node_count",V::makeNum(nodes.size()));out.set("storage_bytes",V::makeNum(h.dump().size()));
    }else{if(!beforeId.empty())throw std::runtime_error("History before_id cursor not found");out.set("has_more",V::makeBool(false));out.set("next_before_id",V::makeStr(""));out.set("node_count",V::makeNum(0));out.set("storage_bytes",V::makeNum(0));}
    out.set("nodes",rows);out.set("branch_heads",heads);out.set("max_document_bytes",V::makeNum(maxDocumentBytes));out.set("document_bytes",V::makeNum(documentToJson(document).size()));out.set("actor_identity_verified",V::makeBool(false));return out;
}
void iterationRestore(const Document& source,const std::string& id,Document& target){if(source.iterationHistory.t!=V::Obj)throw std::runtime_error("No iteration history available");V state=replay(source.iterationHistory,id);std::string error;if(!documentFromJson(state.dump(),target,&error))throw std::runtime_error("Iteration authoring invalid: "+error);target.usage=source.usage;target.agentActivity=source.agentActivity;target.iterationHistory=source.iterationHistory;target.nextFeatureId_=std::max(target.nextFeatureId_,source.nextFeatureId_);target.nextAssemblyId_=std::max(target.nextAssemblyId_,source.nextAssemblyId_);target.nextMateId_=std::max(target.nextMateId_,source.nextMateId_);}
std::string iterationUndoTarget(const Document& document,bool redo){const V& h=document.iterationHistory;if(h.t!=V::Obj)throw std::runtime_error("No iteration history available");const V& ids=need(h,redo?"redo_ids":"undo_ids");if(ids.t!=V::Arr||ids.arr.empty())throw std::runtime_error("No iteration history available");return ids.arr.back().str;}
void iterationPersist(V& root,const Document& document){if(document.iterationHistory.t!=V::Obj)return;V blobs=need(document.iterationHistory,"blobs"),packed=pack(root,blobs);if(blobs.dump()!=need(document.iterationHistory,"blobs").dump())throw std::runtime_error("Current document has unjournaled large authoring payload");root=std::move(packed);root.set("version",V::makeNum(std::max(27.,root.find("version")?root.find("version")->asNum():27.)));root.set("iteration_history",document.iterationHistory);}
bool iterationExpand(V& root,V& history,std::string& error){try{if(auto h=root.find("iteration_history")){if(!root.find("version")||(root.find("version")->num<27||root.find("version")->num>39))throw std::runtime_error("Iteration history requires a supported document version 27 or later");history=*h;erase(root,"iteration_history");root=unpack(root,need(history,"blobs"));}else if(root.find("version")&&root.find("version")->num==27)throw std::runtime_error("Version 27 requires iteration history");return true;}catch(const std::exception& e){error=e.what();return false;}}
bool iterationValidate(const Document& document,const V& h,std::string& error){
    try{if(h.t!=V::Obj||need(h,"schema").t!=V::Num||need(h,"schema").num!=1)throw std::runtime_error("Unsupported iteration history schema");const V& blobs=need(h,"blobs"),nodes=need(h,"nodes");if(blobs.t!=V::Obj||nodes.t!=V::Arr||nodes.arr.size()>maxNodes)throw std::runtime_error("Invalid history blob/node storage");
        for(const auto& blob:blobs.obj)if(!digest(blob.first)||blob.second.t!=V::Str||blob.second.str.size()<1024||iterationSha256(blob.second.str)!=blob.first)throw std::runtime_error("History blob digest mismatch");
        V state=unpack(need(h,"baseline"),blobs);const std::string base=text(h,"baseline_id"),baseHash=iterationSha256(state.dump());if(base!="baseline-"+baseHash||text(h,"baseline_sha256")!=baseHash)throw std::runtime_error("History baseline digest mismatch");
        Document parsed;std::string parseError;if(state.find("iteration_history")||state.find("usage")||!documentFromJson(state.dump(),parsed,&parseError)||iterationAuthoring(parsed).dump()!=state.dump())throw std::runtime_error("Invalid history baseline authoring: "+parseError);
        size_t replayOps=0;std::map<std::string,std::string> hashes;hashes.emplace(base,baseHash);std::string current=base;
        for(size_t i=0;i<nodes.arr.size();++i){const V& n=nodes.arr[i];const std::string id=text(n,"id"),parent=text(n,"parent_id"),previous=text(n,"previous_head_id");if(id!=nodeId(n)||hashes.count(id)||!hashes.count(parent)||!hashes.count(previous)||text(n,"before_sha256")!=hashes.at(previous)||need(n,"ordinal").num!=(double)(i+1))throw std::runtime_error("History node identity/parent/before digest mismatch");validateActor(need(n,"actor"));if(text(n,"provenance")!="caller_declared"||text(n,"motivation").size()>4096||text(n,"utc").size()!=20||text(n,"tool").empty())throw std::runtime_error("Invalid history node metadata");
            if(auto restored=n.find("restored_from_id");restored&&(restored->t!=V::Str||restored->str!=parent))throw std::runtime_error("Invalid history restore parent");
            if(parent!=current)state=replay(h,parent,&replayOps);replayBudget(replayOps,need(n,"delta"));apply(state,need(n,"delta"),blobs);const std::string hash=iterationSha256(state.dump());if(!digest(text(n,"after_sha256"))||hash!=text(n,"after_sha256"))throw std::runtime_error("History authoring delta digest mismatch");
            if(state.find("iteration_history")||state.find("usage")||!documentFromJson(state.dump(),parsed,&parseError)||iterationAuthoring(parsed).dump()!=state.dump())throw std::runtime_error("Invalid iteration authoring: "+parseError);hashes.emplace(id,hash);current=id;
        }
        const std::string head=text(h,"head_id");if(!hashes.count(head)||iterationSha256(iterationAuthoring(document).dump())!=hashes.at(head))throw std::runtime_error("Current document does not match iteration head");
        for(const char* key:{"undo_ids","redo_ids"}){const V& ids=need(h,key);if(ids.t!=V::Arr||ids.arr.size()>maxNodes)throw std::runtime_error("Invalid persistent undo/redo stack");for(const auto& id:ids.arr)if(id.t!=V::Str||!hashes.count(id.str))throw std::runtime_error("Persistent undo/redo target missing");}
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
