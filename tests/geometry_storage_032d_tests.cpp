#include "ark/brep.hpp"
#include "ark/native_backend.hpp"
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;using V=arkjson::Value;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
size_t allocation(const V& v){size_t n=sizeof(V)+v.str.capacity()+1+v.arr.capacity()*sizeof(V)+v.obj.capacity()*sizeof(std::pair<std::string,V>);for(const auto& x:v.arr)n+=allocation(x)-sizeof(V);for(const auto& x:v.obj)n+=x.first.capacity()+1+allocation(x.second)-sizeof(V);return n;}
V fixture(){
    V result=V::makeObj(),mesh=V::makeObj(),pos=V::makeArr(),idx=V::makeArr(),spans=V::makeArr(),report=V::makeObj();
    for(int i=0;i<12000;++i){pos.arr.push_back(V::makeNum((i%3==0?0.:i%3==1?1.:0.)+i/3*1.0000000000000002));pos.arr.push_back(V::makeNum(i%3==2?1.:0.));pos.arr.push_back(V::makeNum(.12345678901234567));idx.arr.push_back(V::makeNum(i));}
    mesh.set("pos",pos);mesh.set("idx",idx);mesh.set("face_spans",spans);report.set("synthetic_metric",V::makeNum(10));report.set("retention_test_notes",V::makeStr(std::string(12000,'q')));result.set("mesh",mesh);result.set("report",report);result.set("synthetic_payload",V::makeBool(true));return result;
}
void run(){
    const auto source=fixture();const auto raw=allocation(source);NativePayload payload(source);const auto packed=payload.allocatedBytes();check(payload.packedMesh()&&packed*4<raw,"Lossless typed channels substantially compact the identical JSON mesh fixture");
    const auto a=nativeResultMesh(source),b=nativeResultMesh(payload);check(a.pos==b.pos&&a.idx==b.idx&&a.nrm==b.nrm,"Packed native display conversion preserves the existing float32 mesh exactly");check(payload.allocatedBytes()==packed,"Typed display conversion does not materialize JSON numeric arrays");
    Body first;first.nativeData=payload;first.baked=std::make_unique<Mesh>(b);Body second=first;
    check(first.nativeData.ownerIdentity()==second.nativeData.ownerIdentity()&&first.baked.get()==second.baked.get(),"Body copies share immutable native and mesh owners");
    second.nativeData.mutate([](V& value){V report=*value.find("report");report.set("synthetic_metric",V::makeNum(20));value.set("report",report);});
    check(first.nativeData.find("report")->find("synthetic_metric")->num==10&&second.nativeData.find("report")->find("synthetic_metric")->num==20,"Native edit detaches and preserves baseline evidence");check(payload.allocatedBytes()==packed,"Editing a copy does not expand the baseline payload");
    second.baked.mutate([](Mesh& mesh){mesh.pos[0]+=5;mesh.computeNormals();});check(first.baked->pos[0]!=second.baked->pos[0]&&first.baked.get()!=second.baked.get(),"Mesh edit detaches and preserves the original mesh");
    const auto* stable=&payload.value();std::vector<std::future<const V*>> jobs;for(int i=0;i<8;++i)jobs.push_back(std::async(std::launch::async,[&]{return &payload.value();}));for(auto& job:jobs)check(job.get()==stable,"Concurrent full reads share one immutable expansion");
    check(payload.dump()==source.dump(),"Lazy expansion preserves all original double coordinates, indices, metadata and JSON order");check(payload.maximumRetainedBytes()>=payload.allocatedBytes(),"Candidate accounting bounds the optional full JSON expansion");
    const auto expandedBytes=payload.allocatedBytes();const auto retained=payload.compacted();
    check(retained.ownerIdentity()!=payload.ownerIdentity()&&retained.allocatedBytes()<=packed&&payload.allocatedBytes()==expandedBytes,"Retention detaches only the expanded cache, preserving the source owner and charging packed allocations");
#ifdef _WIN32
    check(retained.allocatedBytes()+10000<packed,"Windows retained metadata uses the system lossless compressor without changing its values");
#endif
    std::vector<std::future<const V*>> metadataJobs;for(int i=0;i<8;++i)metadataJobs.push_back(std::async(std::launch::async,[&]{return &retained.metadata();}));const V* metadataOwner=nullptr;for(auto& job:metadataJobs){const auto* value=job.get();if(!metadataOwner)metadataOwner=value;check(value==metadataOwner,"Concurrent retained metadata reads decode once into one immutable owner");}
    check(nativeResultMesh(retained).pos==b.pos&&retained.dump()==source.dump(),"Recompacted retention preserves every float64 value, display channel, metadata field and JSON order");
    std::cout<<"geometry_storage raw_json_allocation_bytes="<<raw<<" packed_allocation_bytes="<<packed<<" body_copies=2 native_owners=1 mesh_owners=1\n";
    V malformed=source;V mesh=*malformed.find("mesh");V indices=*mesh.find("idx");indices.arr[0]=V::makeNum(-1);mesh.set("idx",indices);malformed.set("mesh",mesh);NativePayload invalid(malformed);bool rejected=false;try{(void)nativeResultMesh(invalid);}catch(...){rejected=true;}check(rejected,"Malformed indices remain rejected after storage compaction");
    V repeated=source;mesh=*source.find("mesh");mesh.obj.push_back({"pos",V::makeStr("invalid duplicate")});repeated.set("mesh",mesh);NativePayload duplicate(repeated);check(!duplicate.packedMesh()&&duplicate.dump()==repeated.dump(),"Packing does not normalize or lose duplicate-key Value inputs");
}
}
int main(){try{run();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
