#include "ark/wood_tools.hpp"
#include "ark/bom.hpp"
#include <set>
#include <stdexcept>
namespace ark {
arkjson::Value inspectWood(const Document& document,u64 revision){
    using V=arkjson::Value;V out=V::makeObj(),items=V::makeArr();const auto bom=inspectBom(document,revision);const auto env=document.buildEnv();
    for(const auto& item:bom.find("items")->arr){
        const auto id=(u64)item.find("feature")->num;const Feature* stock=document.find(id);std::set<u64> visited;
        while(stock&&visited.insert(stock->id).second){
            if(stock->type==FeatureType::NativeSolid&&featureOperation(*stock)=="wood")break;
            const auto operation=featureOperation(*stock);
            if(stock->inputs.size()!=1||stock->type!=FeatureType::NativeSolid||(operation!="transform"&&operation!="hole"&&operation!="fillet"&&operation!="chamfer"&&operation!="vent")){stock=nullptr;break;}
            stock=document.find(stock->inputs.front());
        }
        if(!stock||stock->type!=FeatureType::NativeSolid||featureOperation(*stock)!="wood")continue;
        V row=item;row.set("stock_source_feature",V::makeNum((double)stock->id));
        for(const char* key:{"width","depth","height"}){
            double value=0;bool ok=false;if(auto expression=stock->p.find(key);expression!=stock->p.end())value=evalExpr(expression->second,env,&ok);else if(const auto* dimension=stock->nativeSpec.find(key)){if(dimension->t==V::Num){value=dimension->num;ok=true;}else if(dimension->t==V::Str)value=evalExpr(dimension->str,env,&ok);}
            if(!ok||!std::isfinite(value)||value<=0)throw std::runtime_error("Wood cutlist has invalid stock dimensions");row.set(std::string(key)+"_mm",V::makeNum(value));
        }
        const auto* grain=stock->nativeSpec.find("grain_axis");row.set("grain_axis",grain?*grain:V{});row.set("cutting_allowance_declared",V::makeBool(false));items.arr.push_back(row);
    }
    out.set("schema",V::makeStr("ark.wood-cutlist.v1"));out.set("revision",V::makeNum((double)revision));out.set("items",items);out.set("quantity_scope",V::makeStr("BOM physical occurrences including hidden; authored stock before joinery; supported single-input machining lineage"));out.set("manufacturing_qualified",V::makeBool(false));out.set("structural_checked",V::makeBool(false));out.set("document_unchanged",V::makeBool(true));return out;
}
}
