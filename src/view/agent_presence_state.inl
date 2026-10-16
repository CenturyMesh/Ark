// UI-thread-only observations. Never persisted as CAD or sent to a provider.
struct AgentPresence030 {
    std::string tool,phase="working",step;std::vector<u64> ids,instances;std::vector<std::string> sketches;int copy=-1;
    ULONGLONG updated=0;u64 revision=0;bool error=false,hasPoint=false,hasDirection=false;
    Vec3 point{},direction{},cachedPoint{},cachedDirection{};std::string cachedTarget;bool cachedLocated=false;
};
static bool g_showAgentPresence=true;
static std::map<std::string,AgentPresence030> g_agentPresence;
static void observePresence030(const std::string& actor,const std::string& tool,const arkjson::Value& args){
    using V=arkjson::Value;auto& presence=g_agentPresence[actor.empty()?"assistant":actor];presence.tool=tool;presence.phase="tool";presence.updated=GetTickCount64();presence.revision=g_session.revision;presence.error=false;presence.ids.clear();presence.instances.clear();presence.sketches.clear();presence.copy=-1;presence.cachedLocated=false;presence.hasPoint=presence.hasDirection=false;
    // Feature and assembly node IDs are separate namespaces. Resolve aliases once
    // against the observed source; ambiguous or absent targets remain unlocated.
    auto target=[&](const V& value,bool instance){u64 id=0;unsigned matches=0;
        if(value.t==V::Num&&value.num>0&&value.num<=9007199254740991.&&std::floor(value.num)==value.num){if(instance){for(const auto& node:g_doc.assembly)if(node.id==(u64)value.num){id=node.id;matches=1;}}else if(g_doc.find((u64)value.num)){id=(u64)value.num;matches=1;}}
        else if(value.t==V::Str&&value.str.size()<=128){if(instance){for(const auto& node:g_doc.assembly)if(node.name==value.str){id=node.id;++matches;}}else for(const auto& feature:g_doc.features)if(feature.name==value.str){id=feature.id;++matches;}}
        auto& ids=instance?presence.instances:presence.ids;if(matches==1&&ids.size()<16)ids.push_back(id);
    };
    auto vector=[&](const V& object,const char* key,Vec3& point,bool& known){if(const auto* value=object.find(key);value&&value->t==V::Arr&&value->arr.size()==3){for(const auto& n:value->arr)if(n.t!=V::Num||!std::isfinite(n.num)||std::abs(n.num)>1e6)return;point={value->arr[0].num,value->arr[1].num,value->arr[2].num};known=true;}};
    auto featureRefs=[&](const V& value,bool creating){if(!creating)if(const auto* id=value.find("id"))target(*id,false);if(const auto* refs=value.find("inputs");refs&&refs->t==V::Arr)for(const auto& ref:refs->arr)target(ref,false);};
    static const std::set<std::string> featureQueries{"native_inspect","mesh_inspect","manufacturing_inspect","sheet_inspect","frame_inspect","electronics_inspect","tube_inspect","object_context","selection_set"};
    if(featureQueries.count(tool)){featureRefs(args,false);if(const auto* instance=args.find("instance"))target(*instance,true);if(const auto* copy=args.find("copy");copy&&copy->t==V::Num&&copy->num>=0&&copy->num<=31&&copy->num==std::floor(copy->num))presence.copy=(int)copy->num;if(tool=="native_inspect")vector(args,"point",presence.point,presence.hasPoint);}
    if(tool=="assembly_info")if(const auto* drive=args.find("drive"))if(const auto* driver=drive->find("driver"))target(*driver,true);
    if(const auto* commands=args.find("commands");commands&&commands->t==V::Arr)for(const auto& command:commands->arr){const auto* field=command.find("op");if(!field||field->t!=V::Str)continue;const auto& op=field->str;
        if(op.rfind("assembly.",0)==0){if(const auto* id=command.find("id"))target(*id,true);if(const auto* driver=command.find("driver"))target(*driver,true);if(const auto* feature=command.find("feature"))target(*feature,false);}
        else if(op.rfind("mate.",0)==0){for(const auto* key:{"a","b"})if(const auto* id=command.find(key))target(*id,true);}
        else if(op.rfind("feature.",0)==0||op.rfind("sheet.",0)==0||op.rfind("frame.",0)==0||op=="light_up"||op.rfind("face_appearance.",0)==0){featureRefs(command,op=="feature.add"||op=="sheet.create");if(const auto* native=command.find("native"))vector(*native,"direction",presence.direction,presence.hasDirection);}
        else if(op.rfind("sketch.",0)==0)if(const auto* name=command.find("name");name&&name->t==V::Str&&presence.sketches.size()<8)presence.sketches.push_back(name->str);
    }

}
static void presenceEvent030(const AgentTeamEvent& event){
    if(event.roleId.empty()||event.kind==AgentTeamEvent::Usage||event.kind==AgentTeamEvent::Text)return;
    auto& presence=g_agentPresence[event.roleId];presence.updated=GetTickCount64();presence.error=event.toolError||event.kind==AgentTeamEvent::Error;
    if(!event.tool.empty())presence.tool=event.tool;
    if(event.kind==AgentTeamEvent::ConstructionQueued||event.kind==AgentTeamEvent::ConstructionStart){presence.sketches=event.sketchNames;presence.ids.clear();presence.instances.clear();presence.copy=-1;presence.hasPoint=false;presence.cachedLocated=false;presence.step=event.stepId;presence.phase=event.kind==AgentTeamEvent::ConstructionQueued?"queued draft":"applying";}
    else if(event.kind==AgentTeamEvent::ConstructionEnd){presence.phase=event.toolError?"rejected":event.hasDocumentRevision&&event.hasExpectedRevision&&event.documentRevision>event.expectedRevision?"committed":"unchanged";if(!event.toolError&&!event.createdIds.empty()){presence.ids=event.createdIds;}if(event.hasDocumentRevision)presence.revision=event.documentRevision;}
    else if(event.kind==AgentTeamEvent::Error)presence.phase="error";
    else if(event.kind==AgentTeamEvent::Status)presence.phase=event.text.substr(0,56);
}
static void finishPresence030(const std::string& actor,const arkjson::Value& reply){
    using V=arkjson::Value;auto found=g_agentPresence.find(actor.empty()?"assistant":actor);if(found==g_agentPresence.end())return;auto& presence=found->second;presence.updated=GetTickCount64();
    const auto* error=reply.find("isError"),*receipt=reply.find("structuredContent");presence.error=error&&error->t==V::Bool&&error->b;presence.phase=presence.error?"rejected":"inspected";
    if(receipt){if(const auto* revision=receipt->find("revision");revision&&revision->t==V::Num&&revision->num>=0&&revision->num<=9007199254740991.&&std::floor(revision->num)==revision->num){if(!presence.error&&revision->num>presence.revision)presence.phase="committed";presence.revision=(u64)revision->num;}
        if(!presence.error)if(const auto* ids=receipt->find("created_ids");ids&&ids->t==V::Arr&&!ids->arr.empty()){presence.ids.clear();presence.instances.clear();for(const auto& id:ids->arr)if(id.t==V::Num&&id.num>0&&id.num<=9007199254740991.&&std::floor(id.num)==id.num&&presence.ids.size()<16)presence.ids.push_back((u64)id.num);}
        if(!presence.error)if(const auto* nodes=receipt->find("created_node_ids");nodes&&nodes->t==V::Arr&&!nodes->arr.empty()){presence.instances.clear();for(const auto& node:nodes->arr)if(node.t==V::Num&&node.num>0&&node.num<=9007199254740991.&&std::floor(node.num)==node.num&&presence.instances.size()<16)presence.instances.push_back((u64)node.num);}
    }
}
