namespace claudeReconnect071 {
static std::unique_ptr<ark::AssistantClaudeAuth071> account=std::make_unique<ark::AssistantClaudeAuth071>();
static unsigned seenGeneration=0;
static std::string activeEndpoint;
static std::set<std::string> expiredEndpoints;
static std::map<std::string,AssistantConfig> teamRoutes;
static std::map<std::string,std::string> teamErrorSeen;
static std::map<std::string,AssistantUsage> teamUsageSeen;
static bool isClaude(const AssistantConfig& config){return config.protocol==AssistantProtocol::ClaudeCode&&config.authMode==AssistantAuthMode::CliAccount;}
static bool busy(){return account->snapshot().running;}
static bool workActive(){return g_chatBusy||(g_teamStarted&&g_agentTeam.snapshot().running)||g_authBusy;}
static std::vector<std::pair<std::string,AssistantConfig>> routes(){
    std::vector<std::pair<std::string,AssistantConfig>> result;
    auto add=[&](std::string name,const AssistantConfig& config){if(!isClaude(config))return;for(const auto& row:result)if(row.second.endpoint==config.endpoint)return;result.emplace_back(std::move(name),config);};
    add("Assistant",g_assistantConfig);for(size_t i=0;i<g_teamRoleCount;++i)add("Team role "+std::to_string(i+1),teamRoleConfig(i));return result;
}
static void observeError(const std::string& message,const AssistantConfig& config){
    const std::string prefix="Claude Code / authentication_expired";
    if(!isClaude(config)||message.rfind(prefix,0)!=0)return;
    const auto tail=message.substr(prefix.size());
    if(tail.empty()||tail.front()=='.'||tail.rfind(" /",0)==0)expiredEndpoints.insert(config.endpoint);
}
static void observeSuccess(const AssistantConfig& config,const AssistantUsage& usage){if(isClaude(config)&&usage.reportedResponses&&usage.outputTokens)expiredEndpoints.erase(config.endpoint);}
static void rememberTeam(const AgentTeamConfig& team){teamRoutes.clear();teamErrorSeen.clear();teamUsageSeen.clear();for(const auto& role:team.roles)teamRoutes[role.id]=role.assistant;}
static void observeTeamError(const AgentTeamRoleSnapshot& role){if(teamErrorSeen[role.id]==role.error)return;teamErrorSeen[role.id]=role.error;const auto found=teamRoutes.find(role.id);if(found!=teamRoutes.end()&&!role.error.empty())observeError(role.error,found->second);}
static void observeTeamEvent(const AgentTeamEvent& event){
    const auto route=teamRoutes.find(event.roleId);if(route==teamRoutes.end())return;
    if(event.kind==AgentTeamEvent::Error){teamErrorSeen[event.roleId]=event.text;observeError(event.text,route->second);}
    if(event.kind!=AgentTeamEvent::Usage)return;
    const auto previous=teamUsageSeen[event.roleId];teamUsageSeen[event.roleId]=event.usage;
    const auto* failed=event.providerOutcome.find("failed");const auto* schema=event.providerOutcome.find("schema");
    if(schema&&schema->asStr()=="ark.claude-result.v1"&&failed&&failed->t==arkjson::Value::Bool&&!failed->b&&event.usage.reportedResponses>previous.reportedResponses&&event.usage.outputTokens>previous.outputTokens)observeSuccess(route->second,event.usage);
}
static bool expired(){for(const auto& route:routes())if(expiredEndpoints.count(route.second.endpoint))return true;return false;}
static std::string label(){return busy()?"Claude Code / connecting...":expired()?"Claude Code / sign in":"Claude Code";}
static void poll(){
    const auto state=account->snapshot();if(state.generation==seenGeneration)return;seenGeneration=state.generation;
    if(state.renewedSavedLogin)expiredEndpoints.erase(activeEndpoint);
    g_status=state.message;
    if(state.phase==ark::ClaudeAuthPhase071::Ready&&expiredEndpoints.count(activeEndpoint))
        g_status="CLI metadata is available, but the observed authentication failure is not cleared. Reconnect the saved account or renew its external credential; then resume manually.";
    if(!state.running&&state.phase!=ark::ClaudeAuthPhase071::Idle)g_chatLog.push_back({"Account",g_status});
    if(g_window)InvalidateRect(g_window,nullptr,FALSE);
}
static void startWith(const AssistantConfig& config,bool interactive){
    if(workActive()){g_status="Finish or stop the current assistant/team request before reconnecting Claude Code. Current work is retained.";return;}
    if(busy()){g_status="Claude Code account operation is already running.";return;}
    activeEndpoint=config.endpoint;
    if(account->start(config,interactive)){g_chatLog.push_back({"Account",interactive?"Continue in the official Claude Code console/browser. ARK will check the account afterwards; your task is not resumed automatically.":"Checking the Claude Code account and SDK metadata without inference."});poll();}
}
static void start(bool interactive){
    if(workActive()||busy()){g_status="Finish or stop the current assistant/team/account operation before reconnecting Claude Code.";return;}
    const auto options=routes();if(options.empty()){g_status="Select a Claude Code connector first.";return;}
    size_t selected=0;
    if(options.size()>1){HMENU menu=CreatePopupMenu();if(!menu)return;for(size_t i=0;i<options.size();++i)AppendMenuA(menu,MF_STRING,(UINT_PTR)(i+1),options[i].first.c_str());POINT cursor;GetCursorPos(&cursor);const int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,g_window,nullptr);DestroyMenu(menu);if(choice<1||(size_t)choice>options.size())return;selected=(size_t)choice-1;}
    startWith(options[selected].second,interactive);
}
static void cancel(){account->cancel();g_status="Stopping the ARK-owned Claude account operation. The browser and CAD work remain open.";}
static bool appendMenu(HMENU menu){
    if(routes().empty())return false;const bool locked=workActive()||busy();
    if(expired())AppendMenuA(menu,MF_STRING|MF_GRAYED,0,"Claude Code authentication expired");
    AppendMenuA(menu,MF_STRING|(locked?MF_GRAYED:0),50100,"Reconnect Claude Code...");
    AppendMenuA(menu,MF_STRING|(locked?MF_GRAYED:0),50101,"Check Claude Code account (no inference)");
    if(busy())AppendMenuA(menu,MF_STRING,50102,"Cancel Claude account operation");
    return true;
}
static void shutdown(){account->shutdown();}
}
