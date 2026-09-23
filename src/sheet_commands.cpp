#include "ark/sheet_commands.hpp"
#include <set>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
const V& required(const V& j,const char* key){auto p=j.find(key);if(!p)throw std::runtime_error(std::string("Missing sheet edit ")+key);return *p;}
std::string text(const V& j,const char* key){auto& p=required(j,key);if(p.t!=V::Str||p.str.empty())throw std::runtime_error("Sheet edit expects a nonempty name");return p.str;}
void merge(V& target,const V& changes){if(changes.t!=V::Obj)throw std::runtime_error("Sheet values must be an object");for(auto& kv:changes.obj){if(kv.first=="id")throw std::runtime_error("Sheet entity IDs are immutable");target.set(kv.first,kv.second);}}
void promote(Feature& f){
    if(f.nativeSpec.find("base"))return;
    V base=V::makeObj();base.set("id",V::makeStr("panel_0"));base.set("length",V::makeStr(f.get("leg_a")));f.nativeSpec.set("base",base);
    V flange=V::makeObj();flange.set("id",V::makeStr("panel_1"));flange.set("parent",V::makeStr("panel_0"));flange.set("edge",V::makeStr("u_max"));flange.set("length",V::makeStr(f.get("leg_b")));flange.set("angle",V::makeStr(f.get("angle")));flange.set("inner_radius",V::makeStr(f.get("inner_radius")));
    V flanges=V::makeArr();flanges.arr.push_back(flange);f.nativeSpec.set("flanges",flanges);f.nativeSpec.set("legacy_origin",V::makeBool(true));
    if(auto list=f.nativeSpec.find("openings")){V openings=*list;for(size_t i=0;i<openings.arr.size();++i){auto& h=openings.arr[i];if(auto panel=h.find("panel");panel&&panel->t==V::Num)h.set("panel",V::makeStr("panel_"+std::to_string((int)panel->num)));if(!h.find("id"))h.set("id",V::makeStr("opening_"+std::to_string(i+1)));}f.nativeSpec.set("openings",openings);}
    for(auto key:{"leg_a","leg_b","inner_radius","angle"})f.p.erase(key);
}
}
V applySheetCommand(Document& doc,const V& command){
    const auto op=text(command,"op");V result=V::makeObj();
    if(op=="sheet.create"){
        Feature f;f.type=FeatureType::SheetMetal;f.name=command.find("name")?text(command,"name"):"Sheet metal";const auto& p=required(command,"p");if(p.t!=V::Obj)throw std::runtime_error("Sheet p must be object");
        for(auto& kv:p.obj){if(kv.second.t!=V::Str&&kv.second.t!=V::Num)throw std::runtime_error("Sheet parameter must be numeric or expression");f.p[kv.first]=kv.second.t==V::Str?kv.second.str:kv.second.dump();}
        V base=required(command,"base");if(base.t!=V::Obj)throw std::runtime_error("Sheet base must be object");if(!base.find("id"))base.set("id",V::makeStr("base"));f.nativeSpec=V::makeObj();f.nativeSpec.set("base",base);f.nativeSpec.set("flanges",V::makeArr());f.nativeSpec.set("openings",V::makeArr());f.nativeSpec.set("next_entity_id",V::makeNum(1));auto id=doc.addFeature(std::move(f));result.set("feature_id",V::makeNum((double)id));return result;
    }
    const auto& key=required(command,"id");if(key.t!=V::Num||key.num<1||key.num>9007199254740990.||key.num!=std::floor(key.num))throw std::runtime_error("Sheet edit needs a feature ID");
    auto f=doc.find((u64)key.num);if(!f||f->type!=FeatureType::SheetMetal)throw std::runtime_error("Select an authored sheet feature");promote(*f);
    if(op=="sheet.base.edit"){V base=required(f->nativeSpec,"base");merge(base,required(command,"values"));f->nativeSpec.set("base",base);return result;}
    const bool flange=op.rfind("sheet.flange.",0)==0,opening=op.rfind("sheet.opening.",0)==0;if(!flange&&!opening)throw std::runtime_error("Unknown sheet edit");
    const char* listKey=flange?"flanges":"openings";V list=f->nativeSpec.find(listKey)?*f->nativeSpec.find(listKey):V::makeArr();
    const auto action=op.substr(op.find_last_of('.')+1);std::string entity;
    if(action=="add"){
        V item=required(command,flange?"flange":"opening");if(item.t!=V::Obj)throw std::runtime_error("Sheet entity must be object");
        if(!item.find("id")){
            double next=f->nativeSpec.find("next_entity_id")?f->nativeSpec.find("next_entity_id")->asNum():1;
            if(!std::isfinite(next)||next<1||next>9007199254740990.||next!=std::floor(next))throw std::runtime_error("Invalid sheet entity counter");
            std::set<std::string> used;used.insert(text(required(f->nativeSpec,"base"),"id"));for(auto k:{"flanges","openings"})if(auto a=f->nativeSpec.find(k))for(auto& x:a->arr)if(x.find("id"))used.insert(text(x,"id"));
            do{entity=(flange?"flange_":"opening_")+std::to_string((u64)next++);}while(used.count(entity));item.set("id",V::makeStr(entity));f->nativeSpec.set("next_entity_id",V::makeNum(next));
        }else entity=text(item,"id");list.arr.push_back(item);
    }else{
        entity=text(command,"entity");auto it=std::find_if(list.arr.begin(),list.arr.end(),[&](const V& row){return row.find("id")&&row.find("id")->asStr()==entity;});if(it==list.arr.end())throw std::runtime_error("Sheet entity not found");
        if(action=="edit")merge(*it,required(command,"values"));
        else if(action=="remove"){
            if(flange){
                std::set<std::string> removed{entity};bool changed=true;while(changed){changed=false;for(auto& row:list.arr)if(removed.count(text(row,"parent")))changed|=removed.insert(text(row,"id")).second;}
                V holes=f->nativeSpec.find("openings")?*f->nativeSpec.find("openings"):V::makeArr();bool dependent=removed.size()>1;for(auto& h:holes.arr)dependent|=removed.count(text(h,"panel"))!=0;
                auto cascade=command.find("cascade");if(cascade&&cascade->t!=V::Bool)throw std::runtime_error("cascade must be boolean");if(dependent&&(!cascade||!cascade->b))throw std::runtime_error("Flange has descendants or openings; remove them explicitly or set cascade:true");
                list.arr.erase(std::remove_if(list.arr.begin(),list.arr.end(),[&](const V& row){return removed.count(text(row,"id"))!=0;}),list.arr.end());holes.arr.erase(std::remove_if(holes.arr.begin(),holes.arr.end(),[&](const V& row){return removed.count(text(row,"panel"))!=0;}),holes.arr.end());f->nativeSpec.set("openings",holes);
            }else list.arr.erase(it);
        }else throw std::runtime_error("Unknown sheet entity edit");
    }
    f->nativeSpec.set(listKey,list);result.set("feature_id",key);result.set("entity_id",V::makeStr(entity));return result;
}
}
