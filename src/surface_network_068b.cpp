#include "ark/surface_network_068b.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace ark { namespace {
using V=arkjson::Value;
const V& get(const V& v,const char* key){static const V nil;const auto* p=v.find(key);return p?*p:nil;}
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error("SURFACE_NETWORK: "+message);}
void keys(const V& v,std::initializer_list<const char*> allowed){require(v.t==V::Obj,"Expected object");for(const auto& p:v.obj)require(std::any_of(allowed.begin(),allowed.end(),[&](const char* key){return p.first==key;}),"Unknown field "+p.first);}
u64 feature(const Document& d,const V& value){u64 found=0;unsigned matches=0;for(const auto& f:d.features)if((value.t==V::Num&&value.num==(double)f.id)||(value.t==V::Str&&value.str==f.name)){found=f.id;++matches;}require(matches==1,"Choose one existing unambiguous feature");return found;}
V result(const V& reply){require(!get(reply,"isError").b,get(get(reply,"structuredContent"),"error").asStr());return get(reply,"structuredContent");}
void transfer(const V& a,V& b,std::initializer_list<const char*> names){for(const auto* name:names)if(const auto* p=a.find(name))b.set(name,*p);}
}
bool surfaceNetworkTool068B(AgentSession& session,const V& args,V& out){
    const auto action=get(args,"action").asStr();if(action!="network_plan"&&action!="loft_diagnose")return false;
    const auto& revision=get(args,"expected_revision");require(revision.t==V::Num&&revision.num==(double)session.revision,"Revision changed; inspect current geometry");
    if(action=="network_plan")keys(args,{"action","expected_revision","name","u_curves","v_curves","u_stations","v_stations","u_reverse","v_reverse"});
    else keys(args,{"action","expected_revision","id","sections","guides","controls"});
    Document snapshot=session.document;snapshot.evaluatingBodies=nullptr;
    if(action=="loft_diagnose"){
        Feature selected;if(args.find("id")){const auto id=feature(snapshot,get(args,"id"));selected=*snapshot.find(id);require(selected.type==FeatureType::Loft,"Select an existing loft");}
        else{selected.type=FeatureType::Loft;selected.p["solid"]="0";selected.nativeSpec=V::makeObj();}
        transfer(args,selected.nativeSpec,{"sections","guides","controls"});
        if(args.find("guides")&&get(args,"guides").t==V::Arr&&get(args,"guides").arr.empty())selected.nativeSpec.obj.erase(std::remove_if(selected.nativeSpec.obj.begin(),selected.nativeSpec.obj.end(),[](const auto& entry){return entry.first=="guides";}),selected.nativeSpec.obj.end());
        require(get(selected.nativeSpec,"sections").t==V::Arr,"Choose ordered loft sections");
        for(const auto& section:get(selected.nativeSpec,"sections").arr)if(section.t==V::Str)for(const auto& f:snapshot.features)if(f.name==section.str&&isCurveFeature(f.type))ensureNativeBody(snapshot,f.id,snapshot.evalBodies);
        for(const auto& guide:get(selected.nativeSpec,"guides").arr)if(guide.t==V::Str)for(const auto& f:snapshot.features)if(f.name==guide.str&&isCurveFeature(f.type))ensureNativeBody(snapshot,f.id,snapshot.evalBodies);
        auto request=surfaceRequestForInspection068B(snapshot,selected);request.set("op",V::makeStr("loft_diagnostics"));out=nativeModelRequest(request);out.set("revision",revision);if(args.find("id"))out.set("id",get(args,"id"));return true;
    }
    V encoded=V::makeArr(),inputs=V::makeArr();std::set<u64> unique;
    for(const auto* family:{"u_curves","v_curves"}){const auto& curves=get(args,family);require(curves.t==V::Arr&&curves.arr.size()>=2&&curves.arr.size()<=9,"Each family needs 2..9 explicit curve IDs or names");for(const auto& source:curves.arr){const auto id=feature(snapshot,source);require(unique.insert(id).second,"Each network curve may occur only once");require(isCurveFeature(snapshot.find(id)->type),"Network inputs must be native curve features");ensureNativeBody(snapshot,id,snapshot.evalBodies);const Body* body=nullptr;for(const auto& b:snapshot.evalBodies)if(b.id==id)body=&b;require(body&&body->nativeData.find("brep_base64"),"Curve has no exact native B-rep");encoded.arr.push_back(*body->nativeData.find("brep_base64"));inputs.arr.push_back(V::makeNum((double)id));}}
    V native=V::makeObj();native.set("op",V::makeStr("curve_network"));native.set("u_count",V::makeNum((double)get(args,"u_curves").arr.size()));transfer(args,native,{"u_stations","v_stations","u_reverse","v_reverse"});
    V request=native;request.set("op",V::makeStr("surface_network"));request.set("input_breps",encoded);const auto measured=nativeModelRequest(request);
    const std::string name=args.find("name")?get(args,"name").asStr():"Curve network";require(!name.empty()&&name.size()<=128,"Choose a nonempty result name");
    V command=V::makeObj();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("surface_edit"));command.set("name",V::makeStr(name));command.set("inputs",inputs);command.set("native",native);V commands=V::makeArr();commands.arr.push_back(command);V preview=V::makeObj();preview.set("expected_revision",revision);preview.set("commands",commands);preview.set("detail",V::makeStr("changes"));out=result(session.call("doc_preview",preview));require(get(out,"valid").b&&get(out,"candidate_cached").b,"Network preview did not validate");out.set("network_evidence",get(measured,"report"));out.set("coordinate_space",V::makeStr("Explicit curve definitions in shared local coordinates; no occurrence transforms are inferred"));return true;
}
void extendSurfaceNetworkSchema068B(V& schema){
    V properties=get(schema,"properties"),action=get(properties,"action"),choices=get(action,"enum");for(const char* name:{"network_plan","loft_diagnose"})choices.arr.push_back(V::makeStr(name));action.set("enum",choices);properties.set("action",action);
    V extra;arkjson::parse(R"({"u_curves":{"type":"array","minItems":2,"maxItems":9,"items":{"type":["integer","string"]}},"v_curves":{"type":"array","minItems":2,"maxItems":9,"items":{"type":["integer","string"]}},"u_stations":{"type":"array","minItems":2,"maxItems":9,"items":{"type":"number","minimum":0,"maximum":1}},"v_stations":{"type":"array","minItems":2,"maxItems":9,"items":{"type":"number","minimum":0,"maximum":1}},"u_reverse":{"type":"array","minItems":2,"maxItems":9,"items":{"type":"boolean"}},"v_reverse":{"type":"array","minItems":2,"maxItems":9,"items":{"type":"boolean"}},"sections":{"type":"array","minItems":2,"maxItems":32,"items":{"type":"string"}},"guides":{"type":"array","maxItems":8,"items":{"type":"string"}},"controls":{"type":"object"}})",extra);for(const auto& p:extra.obj)properties.set(p.first,p.second);schema.set("properties",properties);V required=V::makeArr();required.arr={V::makeStr("action"),V::makeStr("expected_revision")};schema.set("required",required);
}
}
