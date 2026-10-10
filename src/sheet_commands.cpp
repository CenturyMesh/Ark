#include "ark/sheet_commands.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
const V& required(const V& j,const char* key){auto p=j.find(key);if(!p)throw std::runtime_error(std::string("Missing sheet edit ")+key);return *p;}
std::string text(const V& j,const char* key){auto& p=required(j,key);if(p.t!=V::Str||p.str.empty())throw std::runtime_error("Sheet edit expects a nonempty name");return p.str;}
void merge(V& target,const V& changes){if(changes.t!=V::Obj)throw std::runtime_error("Sheet values must be an object");for(auto& kv:changes.obj){if(kv.first=="id")throw std::runtime_error("Sheet entity IDs are immutable");target.set(kv.first,kv.second);}}
std::string expression(const V& value){
    if(value.t!=V::Str&&value.t!=V::Num)throw std::runtime_error("Sheet pattern displacement must be an expression");
    return value.t==V::Str?value.str:value.dump();
}
std::set<std::string> usedIds(const Feature& feature){
    std::set<std::string> ids{text(required(feature.nativeSpec,"base"),"id")};
    for(const char* key:{"flanges","hems","openings","stamps"})if(const V* rows=feature.nativeSpec.find(key))
        for(const V& row:rows->arr)if(row.find("id"))ids.insert(text(row,"id"));
    return ids;
}
std::string generatedId(Feature& feature,const char* prefix,std::set<std::string>& used){
    double next=feature.nativeSpec.find("next_entity_id")?feature.nativeSpec.find("next_entity_id")->asNum():1;
    if(!std::isfinite(next)||next<1||next>9007199254740990.||next!=std::floor(next))throw std::runtime_error("Invalid sheet entity counter");
    std::string identity;
    do{identity=std::string(prefix)+std::to_string((u64)next++);}while(used.count(identity));
    feature.nativeSpec.set("next_entity_id",V::makeNum(next));used.insert(identity);return identity;
}
void offsetOpening(V& opening,const std::string& dx,const std::string& dy,int index){
    auto shifted=[&](const V& original,const std::string& step){
        return V::makeStr("("+expression(original)+")+("+std::to_string(index)+")*("+step+")");
    };
    if(const V* points=opening.find("points")){
        if(points->t!=V::Arr)throw std::runtime_error("Pattern polygon points must be an array");
        V moved=V::makeArr();
        for(const V& point:points->arr){
            if(point.t!=V::Arr||point.arr.size()!=2)throw std::runtime_error("Pattern polygon point must be xy");
            V row=V::makeArr();row.arr.push_back(shifted(point.arr[0],dx));row.arr.push_back(shifted(point.arr[1],dy));
            moved.arr.push_back(std::move(row));
        }
        opening.set("points",std::move(moved));
    }else{
        opening.set("x",shifted(required(opening,"x"),dx));
        opening.set("y",shifted(required(opening,"y"),dy));
    }
}
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
        V base=required(command,"base");if(base.t!=V::Obj)throw std::runtime_error("Sheet base must be object");if(!base.find("id"))base.set("id",V::makeStr("base"));const std::string baseId=text(base,"id");f.nativeSpec=V::makeObj();f.nativeSpec.set("base",base);f.nativeSpec.set("flanges",V::makeArr());f.nativeSpec.set("openings",V::makeArr());f.nativeSpec.set("next_entity_id",V::makeNum(1));auto id=doc.addFeature(std::move(f));result.set("feature_id",V::makeNum((double)id));result.set("entity_id",V::makeStr(baseId));return result;
    }
    const auto& key=required(command,"id");if(key.t!=V::Num||key.num<1||key.num>9007199254740990.||key.num!=std::floor(key.num))throw std::runtime_error("Sheet edit needs a feature ID");
    auto f=doc.find((u64)key.num);if(!f||f->type!=FeatureType::SheetMetal)throw std::runtime_error("Select an authored sheet feature");promote(*f);
    if(op=="sheet.base.edit"){V base=required(f->nativeSpec,"base");merge(base,required(command,"values"));result.set("feature_id",key);result.set("entity_id",V::makeStr(text(base,"id")));f->nativeSpec.set("base",base);return result;}
    const bool flange=op.rfind("sheet.flange.",0)==0,hem=op.rfind("sheet.hem.",0)==0,stamp=op.rfind("sheet.stamp.",0)==0,opening=op.rfind("sheet.opening.",0)==0;if(!flange&&!hem&&!stamp&&!opening)throw std::runtime_error("Unknown sheet edit");
    const char* listKey=flange?"flanges":hem?"hems":stamp?"stamps":"openings";V list=f->nativeSpec.find(listKey)?*f->nativeSpec.find(listKey):V::makeArr();
    const auto action=op.substr(op.find_last_of('.')+1);std::string entity;
    if(action=="pattern"){
        if(!opening)throw std::runtime_error("Only sheet openings support a pattern");
        const V& count=required(command,"count");
        if(count.t!=V::Num||count.num<2||count.num>32||count.num!=std::floor(count.num))
            throw std::runtime_error("Sheet opening pattern count must be 2..32");
        if(list.arr.size()+(size_t)count.num>128)throw std::runtime_error("Sheet opening pattern exceeds 128 openings");
        V source=required(command,"opening");if(source.t!=V::Obj)throw std::runtime_error("Pattern opening must be an object");
        const std::string dx=expression(required(command,"dx")),dy=expression(required(command,"dy"));
        auto used=usedIds(*f);const std::string prefix=source.find("id")?text(source,"id"):generatedId(*f,"opening_",used);
        V identities=V::makeArr();
        for(int i=0;i<(int)count.num;++i){
            V item=source;const std::string id=prefix+"."+std::to_string(i+1);
            if(!used.insert(id).second)throw std::runtime_error("Duplicate sheet opening pattern identity");
            item.set("id",V::makeStr(id));offsetOpening(item,dx,dy,i);
            list.arr.push_back(std::move(item));identities.arr.push_back(V::makeStr(id));
        }
        f->nativeSpec.set(listKey,list);result.set("feature_id",key);result.set("entity_id",V::makeStr(prefix));result.set("entity_ids",std::move(identities));return result;
    }
    if(action=="add"){
        V item=required(command,flange?"flange":hem?"hem":stamp?"stamp":"opening");if(item.t!=V::Obj)throw std::runtime_error("Sheet entity must be object");
        if(!item.find("id")){
            auto used=usedIds(*f);entity=generatedId(*f,flange?"flange_":hem?"hem_":stamp?"stamp_":"opening_",used);item.set("id",V::makeStr(entity));
        }else {entity=text(item,"id");if(usedIds(*f).count(entity))throw std::runtime_error("Duplicate sheet entity identity");}list.arr.push_back(item);
    }else{
        entity=text(command,"entity");auto it=std::find_if(list.arr.begin(),list.arr.end(),[&](const V& row){return row.find("id")&&row.find("id")->asStr()==entity;});if(it==list.arr.end())throw std::runtime_error("Sheet entity not found");
        if(action=="edit")merge(*it,required(command,"values"));
        else if(action=="remove"){
            if(flange){
                std::set<std::string> removed{entity};bool changed=true;while(changed){changed=false;for(auto& row:list.arr)if(removed.count(text(row,"parent")))changed|=removed.insert(text(row,"id")).second;}
                V holes=f->nativeSpec.find("openings")?*f->nativeSpec.find("openings"):V::makeArr();bool dependent=removed.size()>1;for(const char* deps:{"hems","stamps"})if(auto rows=f->nativeSpec.find(deps))for(auto& row:rows->arr)dependent|=removed.count(text(row,deps==std::string("hems")?"parent":"panel"))!=0;for(auto& h:holes.arr)dependent|=removed.count(text(h,"panel"))!=0;
                auto cascade=command.find("cascade");if(cascade&&cascade->t!=V::Bool)throw std::runtime_error("cascade must be boolean");if(dependent&&(!cascade||!cascade->b))throw std::runtime_error("Flange has descendants or openings; remove them explicitly or set cascade:true");
                list.arr.erase(std::remove_if(list.arr.begin(),list.arr.end(),[&](const V& row){return removed.count(text(row,"id"))!=0;}),list.arr.end());holes.arr.erase(std::remove_if(holes.arr.begin(),holes.arr.end(),[&](const V& row){return removed.count(text(row,"panel"))!=0;}),holes.arr.end());f->nativeSpec.set("openings",holes);for(const char* deps:{"hems","stamps"})if(auto rows=f->nativeSpec.find(deps)){V kept=*rows;kept.arr.erase(std::remove_if(kept.arr.begin(),kept.arr.end(),[&](const V& row){return removed.count(text(row,deps==std::string("hems")?"parent":"panel"))!=0;}),kept.arr.end());f->nativeSpec.set(deps,kept);}
            }else list.arr.erase(it);
        }else throw std::runtime_error("Unknown sheet entity edit");
    }
    f->nativeSpec.set(listKey,list);result.set("feature_id",key);result.set("entity_id",V::makeStr(entity));return result;
}
}
