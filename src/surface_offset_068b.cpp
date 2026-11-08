#include "ark/surface_offset_068b.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include <cmath>
#include <stdexcept>
namespace ark { namespace {
using V=arkjson::Value;
const V& offsetField068(const V& value,const char* name){static const V nil;auto p=value.find(name);return p?*p:nil;}
void offsetNeed068(bool ok,const std::string& why){if(!ok)throw std::runtime_error("SURFACE_OFFSET: "+why);}
V offsetJson068(const char* value){V out;offsetNeed068(arkjson::parse(value,out),"Internal schema");return out;}
}
bool surfaceOffsetTool068B(AgentSession& session,const V& args,V& out){
    const auto action=offsetField068(args,"action").asStr();if(action!="offset_check"&&action!="offset_plan")return false;
    for(const auto& item:args.obj)offsetNeed068(item.first=="action"||item.first=="expected_revision"||item.first=="id"||item.first=="mode"||item.first=="amount"||item.first=="samples"||item.first=="source_brep_sha256"||item.first=="name"||item.first=="method","Unknown field: "+item.first);
    const auto& revision=offsetField068(args,"expected_revision");offsetNeed068(revision.t==V::Num&&revision.num==double(session.revision),"Revision changed; inspect again");
    const auto& selector=offsetField068(args,"id");u64 id=0;unsigned matches=0;for(const auto& f:session.document.features)if((selector.t==V::Num&&selector.num==double(f.id))||(selector.t==V::Str&&selector.str==f.name)){id=f.id;++matches;}
    offsetNeed068(matches==1,"Choose one existing, unambiguous feature");
    const auto mode=offsetField068(args,"mode").asStr();offsetNeed068(mode=="offset"||mode=="thicken","Choose offset or thicken");
    const auto& authored=offsetField068(args,"amount");bool valid=false;double amount=authored.t==V::Num?authored.num:authored.t==V::Str?evalExpr(authored.str,session.document.buildEnv(),&valid):0;
    if(authored.t==V::Num)valid=std::isfinite(amount);offsetNeed068(valid&&std::isfinite(amount)&&std::abs(amount)>1e-6&&std::abs(amount)<=100000,"Amount must be a finite nonzero length, at most 100000 mm");
    Document snapshot=session.document;snapshot.evaluatingBodies=nullptr;ensureNativeBody(snapshot,id,snapshot.evalBodies);const Body* body=nullptr;for(const auto& b:snapshot.evalBodies)if(b.id==id)body=&b;
    offsetNeed068(body&&body->nativeData.find("brep_base64")&&body->nativeData.find("topology"),"An exact source face or shell is required");
    const V native=body->nativeData;const auto& topology=offsetField068(native,"topology");const auto& hash=offsetField068(topology,"brep_sha256");offsetNeed068(hash.t==V::Str&&hash.str.size()==64,"Source has no exact fingerprint");
    if(action=="offset_plan"||args.find("source_brep_sha256"))offsetNeed068(offsetField068(args,"source_brep_sha256").asStr()==hash.str,"Source geometry changed; repeat the diagnostic");
    V request=V::makeObj();request.set("op",V::makeStr("surface_offset_diagnostics"));request.set("input_brep",offsetField068(native,"brep_base64"));request.set("topology",topology);request.set("mode",V::makeStr(mode));request.set("amount",V::makeNum(amount));if(args.find("samples"))request.set("samples",offsetField068(args,"samples"));if(args.find("method"))request.set("method",offsetField068(args,"method"));
    V diagnosis=nativeModelRequest(request);diagnosis.set("revision",revision);diagnosis.set("id",V::makeNum(double(id)));diagnosis.set("source_brep_sha256",hash);
    if(action=="offset_check"){out=diagnosis;return true;}
    offsetNeed068(offsetField068(diagnosis,"candidate_valid").b,offsetField068(diagnosis,"candidate_error").asStr());
    V command=offsetJson068(R"({"op":"feature.add"})"),parameters=V::makeObj(),inputs=V::makeArr();command.set("type",V::makeStr(mode=="offset"?"offset_surface":"thicken"));command.set("name",V::makeStr(args.find("name")?offsetField068(args,"name").asStr():mode=="offset"?"Checked surface offset":"Checked surface thickness"));parameters.set(mode=="offset"?"distance":"thickness",authored);command.set("p",parameters);inputs.arr.push_back(V::makeNum(double(id)));command.set("inputs",inputs);
    const auto* source=session.document.find(id);if(source&&!source->material.empty())command.set("material",V::makeStr(source->material));if(source&&source->appearance.t==V::Obj)command.set("appearance",source->appearance);
    if(args.find("method")){V native=V::makeObj();native.set("method",offsetField068(args,"method"));command.set("native",native);}
    V preview=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);preview.set("commands",commands);preview.set("expected_revision",revision);preview.set("detail",V::makeStr("changes"));const V response=session.call("doc_preview",preview);
    offsetNeed068(!offsetField068(response,"isError").b,offsetField068(response,"structuredContent").dump());out=offsetField068(response,"structuredContent");offsetNeed068(offsetField068(out,"valid").b&&offsetField068(out,"candidate_cached").b,"Exact preview could not be retained");out.set("offset_diagnostics",diagnosis);return true;
}
void extendSurfaceOffsetSchema068B(V& schema){
    V props=offsetField068(schema,"properties"),action=offsetField068(props,"action"),choices=offsetField068(action,"enum");for(const char* name:{"offset_check","offset_plan"})choices.arr.push_back(V::makeStr(name));action.set("enum",choices);props.set("action",action);
    props.set("mode",offsetJson068(R"({"type":"string","enum":["offset","thicken"]})"));props.set("method",offsetJson068(R"({"type":"string","enum":["simple","joined","planar"],"description":"Explicit construction: legacy simple; adjacent-face joined intersections without linearization/self-intersection healing; exact planar-face translation/prism. Unsupported sources reject; no automatic fallback."})"));props.set("amount",offsetJson068(R"({"type":["number","string"],"description":"Signed distance/thickness in mm or a named parameter expression; the source is never changed by diagnostics."})"));schema.set("properties",props);
}
}
