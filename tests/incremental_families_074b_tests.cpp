#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
using namespace ark;using V=arkjson::Value;size_t checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}V json(const char* text){V v;if(!arkjson::parse(text,v))throw std::runtime_error("fixture JSON");return v;}
const V& at(const V& v,const char* key){auto p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing ")+key);return *p;}
V call(AgentSession& s,const char* tool,V args){args.set("expected_revision",V::makeNum(s.revision));const auto r=s.call(tool,args);if(at(r,"isError").b)throw std::runtime_error(r.dump());return at(r,"structuredContent");}
V apply(AgentSession& s,V commands){V args=V::makeObj();args.set("commands",commands);args.set("detail",V::makeStr("changes"));return call(s,"doc_apply",args);}
void reject(AgentSession& s,V commands){const auto source=documentToJson(s.document);const auto rev=s.revision;V a=V::makeObj();a.set("commands",commands);a.set("expected_revision",V::makeNum(rev));check(at(s.call("doc_apply",a),"isError").b&&source==documentToJson(s.document)&&rev==s.revision,"Invalid family edit lost atomicity");}
const Body& body(const Document& d,u64 id){for(const auto& b:d.evalBodies)if(b.id==id)return b;throw std::runtime_error("Missing body");}
using Cache=std::unordered_map<u64,Document::EvaluationCacheEntry>;
void reused(const Document& d,const Cache& before,std::initializer_list<u64> ids){for(u64 id:ids)check(d.evaluationCache.at(id).body==before.at(id).body&&d.evaluationCache.at(id).generation==before.at(id).generation,"Unrelated family lost immutable geometry");}
void changed(const Document& d,const Cache& before,std::initializer_list<u64> ids){for(u64 id:ids)check(d.evaluationCache.at(id).body!=before.at(id).body&&d.evaluationCache.at(id).generation!=before.at(id).generation,"Changed source reused stale family geometry");}
const V& why(const Document& d,u64 id){for(const auto& row:at(d.evaluationDiagnostics,"features").arr)if(at(row,"id").num==double(id))return row;throw std::runtime_error("Missing evaluation diagnostic");}
bool reason(const V& row,const char* key){for(const auto& value:at(row,"reasons").arr)if(value.str==key)return true;return false;}
#ifdef ARK_INCREMENTAL_FAMILIES_NATIVE_TESTS
void families(){AgentSession s;s.suppressIterationHistory();s.suppressFailureCapture();apply(s,json(R"([
 {"op":"feature.add","type":"native_solid","name":"Unrelated","native":{"op":"box"},"p":{"width":2,"depth":3,"height":4}},
 {"op":"feature.add","type":"native_solid","name":"Stable stock","native":{"op":"box"},"p":{"width":10,"depth":8,"height":5}},
 {"op":"plane.set","name":"Loft top","p":{"oz":10}},
 {"op":"sketch.set","name":"Lower","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[0,0,2]}]}},
 {"op":"sketch.set","name":"Upper","sketch":{"plane":"Loft top","entities":[{"kind":"circle","name":"rim","dof":[0,0,2]}]}},
 {"op":"feature.add","type":"loft","name":"Loft","native":{"sections":["Lower","Upper"]},"p":{"solid":1,"ruled":1}},
 {"op":"sketch.set","name":"Sweep section","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[0,0,1]}]}},
 {"op":"sketch.set","name":"Sweep path","sketch":{"plane":"XZ","entities":[{"kind":"line","name":"path","dof":[0,0,0,10]}]}},
 {"op":"feature.add","type":"sweep","name":"Sweep","native":{"section":"Sweep section","path":"Sweep path"},"p":{"solid":1}},
 {"op":"sheet.create","name":"Sheet","p":{"width":10,"thickness":1,"k_factor":0.5},"base":{"id":"base","length":20}},
 {"op":"feature.add","type":"native_solid","name":"Fillet","native":{"op":"fillet","selection":"all"},"inputs":[2],"p":{"radius":0.5}},
 {"op":"feature.add","type":"surface_edit","name":"Skin","inputs":[2],"native":{"op":"extract_face","face":"z_max"}},
 {"op":"plane.set","name":"Cut","p":{"ox":4,"ry":90}},
 {"op":"feature.add","type":"surface_edit","name":"Trim","inputs":[7],"native":{"op":"trim_plane","plane":"Cut","keep":"positive"}}
 ])"));auto before=s.document.evaluationCache;
 apply(s,json(R"([{"op":"feature.edit","id":1,"p":{"width":3}}])"));changed(s.document,before,{1});reused(s.document,before,{2,3,4,5,6,7,8});check(s.document.recomputeEvaluated==1&&s.document.recomputeReused==7,"Unrelated edit reevaluated other audited families");check(reason(why(s.document,1),"definition_changed")&&reason(why(s.document,3),"cache_reused"),"Why-rebuilt definitions/reuse not reported");
 before=s.document.evaluationCache;apply(s,json(R"([{"op":"sketch.patch","name":"Upper","entity":"rim","r":3}])"));changed(s.document,before,{3});reused(s.document,before,{1,2,4,5,6,7,8});check(reason(why(s.document,3),"named_context_changed"),"Loft source reason missing");check(std::abs(at(*body(s.document,3).nativeData.find("report"),"volume_mm3").num-190*std::acos(-1.)/3)<1e-5,"Loft section change did not reach analytic frustum volume");
 before=s.document.evaluationCache;apply(s,json(R"([{"op":"sketch.patch","name":"Sweep path","entity":"path","anchor":1,"y":12}])"));changed(s.document,before,{4});reused(s.document,before,{1,2,3,5,6,7,8});check(std::abs(at(*body(s.document,4).nativeData.find("report"),"volume_mm3").num-12*std::acos(-1.))<1e-5,"Sweep path dependency lost");
 before=s.document.evaluationCache;apply(s,json(R"([{"op":"plane.set","name":"Cut","p":{"ox":5}}])"));changed(s.document,before,{8});reused(s.document,before,{1,2,3,4,5,6,7});check(std::abs(at(*body(s.document,8).nativeData.find("report"),"area_mm2").num-40)<1e-5,"Trim datum dependency lost");
 before=s.document.evaluationCache;apply(s,json(R"([{"op":"feature.edit","id":2,"p":{"width":11}}])"));changed(s.document,before,{2,6,7,8});reused(s.document,before,{1,3,4,5});check(reason(why(s.document,6),"dependency_changed"),"Fillet input-generation reason missing");
 before=s.document.evaluationCache;apply(s,json(R"([{"op":"sheet.base.edit","id":5,"values":{"length":21}}])"));changed(s.document,before,{5});reused(s.document,before,{1,2,3,4,6,7,8});const auto sheet=call(s,"sheet_inspect",json(R"({"id":5})"));check(std::abs(at(sheet,"flat_volume_mm3").num-210)<1e-5,"Sheet own recipe change not evaluated");
 reject(s,json(R"([{"op":"feature.edit","id":6,"p":{"radius":100}}])"));reject(s,json(R"([{"op":"sketch.patch","name":"Sweep path","entity":"path","anchor":1,"y":0}])"));
 Document fresh;std::string error;check(documentFromJson(documentToJson(s.document),fresh,&error),"Fresh family reference parse");fresh.recompute();for(const auto& result:fresh.lastResults)check(result.ok,result.error.c_str());for(const auto& original:s.document.evalBodies)check(original.nativeData.find("brep_base64")->str==body(fresh,original.id).nativeData.find("brep_base64")->str,"Incremental family differs from full-reference B-rep");
 const auto authored=documentToJson(s.document,false,false);check(authored.find("evaluation-diagnostics")==std::string::npos&&authored.find("fingerprints")==std::string::npos,"Runtime diagnostics leaked into persistent source");
 apply(s,json(R"([{"op":"material.set","name":"Paint","appearance":{"roughness":0.3}}])"));check(at(s.document.evaluationDiagnostics,"mode").str=="metadata_only"&&at(s.document.evaluationDiagnostics,"evaluated").num==0,"Metadata edit exposed stale rebuild diagnostics");
}
void sheetSketchAndGuide(){AgentSession s;s.suppressIterationHistory();s.suppressFailureCapture();apply(s,json(R"([
 {"op":"sheet.create","name":"Independent sheet","p":{"width":50,"thickness":2,"k_factor":0.5},"base":{"id":"base","length":160}},
 {"op":"sketch.set","name":"Bend location","sketch":{"plane":"XZ","entities":[{"name":"FoldLine","kind":"line","construction":true,"dof":[60,0,60,60]}]}},
 {"op":"sheet.create","name":"Bent sheet","p":{"width":60,"thickness":2,"k_factor":0.5},"base":{"id":"base","length":180}},
 {"op":"sheet.sketch_bend.add","id":2,"bend":{"id":"fold","sketch":"Bend location","entity":"FoldLine","inner_radius":3,"angle":90}}
 ])"));const auto before=s.document.evaluationCache;apply(s,json(R"([{"op":"sketch.entity.set","name":"Bend location","entity":{"name":"FoldLine","kind":"line","construction":true,"dof":[65,0,65,60]}}])"));changed(s.document,before,{2});reused(s.document,before,{1});const auto report=call(s,"sheet_inspect",json(R"({"id":2})"));check(std::abs(at(at(report,"forming_ops").arr[0],"station_mm").num-65)<1e-8,"Sheet bend sketch source not retained");
 reject(s,json(R"([{"op":"sketch.entity.set","name":"Bend location","entity":{"name":"FoldLine","kind":"line","construction":true,"dof":[65,0,65,40]}}])"));
 AgentSession guided;guided.suppressIterationHistory();guided.suppressFailureCapture();apply(guided,json(R"([
 {"op":"sketch.set","name":"Low","sketch":{"entities":[{"kind":"circle","dof":[0,0,10]}]}},
 {"op":"plane.set","name":"Upper","p":{"oz":40}},
 {"op":"sketch.set","name":"High","sketch":{"plane":"Upper","entities":[{"kind":"circle","dof":[0,0,10]}]}},
 {"op":"sketch.set","name":"Bow","sketch":{"plane":"XZ","entities":[{"kind":"line","name":"a","dof":[10,0,18,20]},{"kind":"line","name":"b","dof":[18,20,10,40]}]}},
 {"op":"feature.add","type":"loft","name":"Nose","p":{"solid":1,"ruled":0},"native":{"sections":["Low","High"],"guides":["Bow"]}}
 ])"));const auto old=guided.document.evaluationCache;apply(guided,json(R"([{"op":"sketch.set","name":"Bow","sketch":{"plane":"XZ","entities":[{"kind":"line","name":"a","dof":[10,0,20,20]},{"kind":"line","name":"b","dof":[20,20,10,40]}]}}])"));changed(guided.document,old,{1});check(reason(why(guided.document,1),"named_context_changed"),"Loft guide context change was not diagnosed");}
#endif
}
int main(){try{
#ifdef ARK_INCREMENTAL_FAMILIES_NATIVE_TESTS
families();sheetSketchAndGuide();
#endif
std::cout<<"074B incremental families: "<<checks<<" source closure, diagnostic, exact geometry and rollback checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
