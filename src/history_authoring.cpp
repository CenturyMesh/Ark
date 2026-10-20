#include "ark/history_authoring.hpp"
#include "ark/assembly_authoring.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
u64 target(const Document& document,const V& command) {
    const auto* value=command.find("id");
    if(!value)throw std::runtime_error("Feature history command requires id");
    u64 found=0;
    for(const auto& feature:document.features)
        if((value->t==V::Str&&feature.name==value->str)||
           (value->t==V::Num&&std::isfinite(value->num)&&value->num==(double)feature.id)) {
            if(found)throw std::runtime_error("Ambiguous feature history target");
            found=feature.id;
        }
    if(!found)throw std::runtime_error("Feature history target not found");
    return found;
}
}
bool featureWithinHistory(const Document& document,u64 feature) {
    bool within=true;
    for(const auto& item:document.features) {
        if(item.id==feature)return within;
        if(document.historyTip&&item.id==document.historyTip)within=false;
    }
    return false;
}
std::set<u64> activeFeatureIds(const Document& document) {
    const auto loaded=assemblyLoadedFeatures(document);std::set<u64> active;bool within=true;
    for(const auto& feature:document.features){
        if(within&&!feature.suppressed&&loaded.count(feature.id))active.insert(feature.id);
        if(feature.id==document.historyTip)within=false;
    }
    return active;
}
std::set<u64> consumedFeatureInputs(const Document& document) {
    const auto active=activeFeatureIds(document);std::set<u64> consumed;
    for(const auto& feature:document.features)if(active.count(feature.id)&&featureConsumesInputBodies(feature))
        for(size_t index=0;index<feature.inputs.size();++index)if(featureConsumesInputBody(feature,index))consumed.insert(feature.inputs[index]);
    return consumed;
}
bool applyFeatureHistoryCommand(Document& document,const V& command) {
    const auto* operation=command.find("op");
    if(!operation||operation->t!=V::Str||
       (operation->str!="feature.move"&&operation->str!="feature.rollback"))return false;
    for(const auto& item:command.obj)
        if(item.first!="op"&&item.first!="id"&&!(operation->str=="feature.move"&&item.first=="delta"))
            throw std::runtime_error("Unknown feature history field: "+item.first+(operation->str=="feature.move"?"; feature.move accepts id and delta (signed integer steps in history order)":"; feature.rollback accepts id"));
    if(operation->str=="feature.rollback") {
        const auto* value=command.find("id");
        if(!value)throw std::runtime_error("feature.rollback requires id; use null to resume all history");
        document.historyTip=value->isNull()?0:target(document,command);
        return true;
    }
    if(document.historyTip)throw std::runtime_error("Resume full feature history before reordering");
    const u64 feature=target(document,command);
    const auto* delta=command.find("delta");
    if(!delta||delta->t!=V::Num||!std::isfinite(delta->num)||delta->num!=std::floor(delta->num)||
       delta->num==0||std::abs(delta->num)>1023)
        throw std::runtime_error("feature.move delta must be a nonzero integer from -1023 to 1023");
    auto first=std::find_if(document.features.begin(),document.features.end(),[&](const Feature& item){return item.id==feature;});
    const auto position=std::distance(document.features.begin(),first);
    const auto destination=position+(int)delta->num;
    if(destination<0||destination>=(std::ptrdiff_t)document.features.size())
        throw std::runtime_error("Feature move exceeds the history boundary");
    if(destination>position)std::rotate(first,first+1,document.features.begin()+destination+1);
    else std::rotate(document.features.begin()+destination,first,first+1);
    validateFeatureHistoryOrder(document);
    return true;
}
V inspectFeatureHistory(const Document& document) {
    V report=V::makeObj(),rows=V::makeArr();rows.arr.reserve(document.features.size());bool within=true;
    for(size_t index=0;index<document.features.size();++index) {
        const auto& feature=document.features[index];V row=V::makeObj(),dependencies=V::makeArr();
        row.set("id",V::makeNum((double)feature.id));row.set("name",V::makeStr(feature.name));
        row.set("position",V::makeNum((double)index));row.set("suppressed",V::makeBool(feature.suppressed));
        row.set("history_active",V::makeBool(within));
        for(u64 input:feature.inputs)dependencies.arr.push_back(V::makeNum((double)input));
        row.set("inputs",dependencies);rows.arr.push_back(std::move(row));
        if(feature.id==document.historyTip)within=false;
    }
    report.set("tip",document.historyTip?V::makeNum((double)document.historyTip):V{});
    report.set("features",rows);return report;
}
}
