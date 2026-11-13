#pragma once
#include "ark/json.hpp"
#include <string>
namespace ark {
// Only source-owned static guides are replaced. User messages, model responses,
// tool calls/results and unrelated developer instructions remain untouched.
inline void refreshAssistantOnboarding075D(arkjson::Value& conversation,const std::string& pack){
    if(pack.empty()||conversation.t!=arkjson::Value::Arr)return;
    constexpr const char* marker="ARK_PREBAKED_GUIDE_V1\n";
    const std::string current=marker+pack;bool found=false;
    for(auto item=conversation.arr.begin();item!=conversation.arr.end();){
        const auto* role=item->find("role");const auto* content=item->find("content");
        if(role&&role->asStr()=="developer"&&content&&content->t==arkjson::Value::Str&&content->str.rfind(marker,0)==0){
            if(found){item=conversation.arr.erase(item);continue;}
            found=true;if(content->str!=current)item->set("content",arkjson::Value::makeStr(current));
        }
        ++item;
    }
    if(!found){auto guide=arkjson::Value::makeObj();guide.set("role",arkjson::Value::makeStr("developer"));guide.set("content",arkjson::Value::makeStr(current));conversation.arr.insert(conversation.arr.begin(),std::move(guide));}
}
}
