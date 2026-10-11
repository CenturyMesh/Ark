// Explicit offline Studio fixture transports simulate a feedback-capable agent.
// This adapter is never used by the native provider transport (empty callback).
// Synthetic ratings are confined to test documents; runtime never invents them.
static AssistantTransport feedbackFixtureTransport(const AssistantTransport& original){
    if(!original)return {};
    struct State {std::set<std::string> seen;std::map<std::string,arkjson::Value> calls;std::map<std::string,std::string> pending;arkjson::Value delayed,retry;std::string project;};
    auto state=std::make_shared<State>();
    return [original,state](const AssistantConfig& config,const arkjson::Value& request,const std::atomic_bool* cancel){
        using V=arkjson::Value;const auto* history=request.find("input");if(!history||history->t!=V::Arr)return original(config,request,cancel);
        auto text=[](const V& value,const char* key){const auto* p=value.find(key);return p?p->asStr():std::string();};
        for(const auto& row:history->arr){const auto type=text(row,"type"),id=text(row,"call_id");if(type=="function_call")state->calls[id]=row;
            if(type!="function_call_output"||!state->seen.insert(id).second)continue;V result;if(!arkjson::parse(text(row,"output"),result))continue;const V* data=result.find("structuredContent");if(!data)data=&result;
            const auto project=text(*data,"activity_project_id");if(!project.empty()){if(project!=state->project||(data->find("activity_boundary")&&data->find("activity_boundary")->b))state->pending.clear();state->project=project;}
            if(const auto* key=data->find("activity_id");key&&data->find("feedback_required")&&data->find("feedback_required")->b)state->pending[key->asStr()]=text(state->calls[id],"name");
            if(const auto* rated=data->find("rated_ids"))for(const auto& key:rated->arr)state->pending.erase(key.asStr());
            if(text(*data,"error").find("Record tool_feedback for your actual pending")!=std::string::npos)state->retry=state->calls[id];
        }
        auto makeCall=[&](const std::string& name,const V& args){static std::atomic<unsigned long long> serial{0};V response=V::makeObj(),output=V::makeArr(),call=V::makeObj(),usage=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("name",V::makeStr(name));call.set("call_id",V::makeStr("offline_feedback_"+std::to_string(++serial)));call.set("arguments",V::makeStr(args.dump()));output.arr.push_back(call);response.set("status",V::makeStr("completed"));response.set("output",output);for(const auto* key:{"input_tokens","output_tokens","total_tokens"})usage.set(key,V::makeNum(0));response.set("usage",usage);return response;};
        auto ratings=[&]{V args=V::makeObj(),rows=V::makeArr();for(const auto& [id,tool]:state->pending){if(rows.arr.size()>=64)break;V row=V::makeObj();row.set("activity_id",V::makeStr(id));row.set("utility",V::makeNum(3));row.set("difficulty",V::makeNum(2));row.set("action",V::makeStr("Offline UI fixture exercised "+tool));row.set("issue",V::makeStr("Synthetic test feedback; not a real model quality assessment"));rows.arr.push_back(row);}args.set("ratings",rows);return makeCall("tool_feedback",args);};
        if((state->retry.t==V::Obj||state->delayed.t==V::Obj)&&!state->pending.empty())return ratings();
        if(state->retry.t==V::Obj){V args;arkjson::parse(text(state->retry,"arguments"),args);auto name=text(state->retry,"name");state->retry=V{};return makeCall(name,args);}
        if(state->delayed.t==V::Obj){V response=std::move(state->delayed);state->delayed=V{};return response;}
        auto response=original(config,request,cancel);const auto* output=response.find("output");
        if(output&&output->t==V::Arr&&!state->pending.empty()&&text(response,"status")=="completed"&&std::none_of(output->arr.begin(),output->arr.end(),[&](const V& row){return text(row,"type")=="function_call";})){state->delayed=response;return ratings();}
        return response;
    };
}
