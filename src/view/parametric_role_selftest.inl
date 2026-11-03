// Optional Parametric designer: the default Focused team and legacy settings stay as they
// were, the Agents menu adds one role on its own slot, and an offline Focused run publishes
// a compiled program candidate through the real Studio tool queue.
namespace parametric_role_test {
using V=arkjson::Value;
static constexpr int menuChoice=100;
static const char* program=R"JSON({"format":"ark.parametric-program.v1","name":"Posts","parameters":{"n":{"default":2,"min":1,"max":4,"integer":true}},"body":[{"for":"i","count":"n","body":[{"emit":{"op":"feature.add","type":"native_solid","name":"Post {i+1}","native":{"op":"box"},"p":{"width":4,"depth":4,"height":"{10+i}"}}}]}]})JSON";
struct Run {std::atomic_bool gate{false};std::atomic_int engineerRounds{0},programRounds{0},reviewRounds{0};std::mutex mutex;std::string engineerRequest,programRequest,candidate,reported,finishReceipt;V programTools;};
static V respond(const std::string& tool,const std::string& arguments,const std::string& text={}){
    static std::atomic<unsigned> serial{0};V response=V::makeObj(),output=V::makeArr(),item=V::makeObj(),usage=V::makeObj();
    response.set("status",V::makeStr("completed"));usage.set("input_tokens",V::makeNum(8));usage.set("output_tokens",V::makeNum(2));usage.set("total_tokens",V::makeNum(10));response.set("usage",usage);
    if(!tool.empty()){item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr("parametric_role_"+std::to_string(++serial)));item.set("name",V::makeStr(tool));item.set("arguments",V::makeStr(arguments));}
    else {V content=V::makeArr(),block=V::makeObj();item.set("type",V::makeStr("message"));item.set("role",V::makeStr("assistant"));block.set("type",V::makeStr("output_text"));block.set("text",V::makeStr(text));content.arr.push_back(block);item.set("content",content);}
    output.arr.push_back(item);response.set("output",output);return response;
}
// Newest tool receipt holding key; feedback rows can follow the receipt a round needs.
static V receiptField(const V& request,const char* key){
    const auto* history=request.find("input");if(!history||history->t!=V::Arr)return {};
    for(auto row=history->arr.rbegin();row!=history->arr.rend();++row){const auto* type=row->find("type");const auto* text=row->find("output");if(!type||type->asStr()!="function_call_output"||!text)continue;V receipt;if(!arkjson::parse(text->asStr(),receipt))continue;const V* data=receipt.find("structuredContent");if(!data)data=&receipt;if(const auto* value=data->find(key))return *value;}
    return {};
}
static std::array<AssistantTransport,teamCapacity> transports(const std::shared_ptr<Run>& run,bool programTask){
    std::array<AssistantTransport,teamCapacity> table;
    for(auto& transport:table)transport=[](const auto&,const V&,const auto*){return respond({},{},"Offline role finished.");};
    table[0]=[run,programTask](const auto&,const V& request,const auto* cancel){
        while(!run->gate){if(cancel&&*cancel)throw std::runtime_error("Offline team test cancelled");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        const int round=++run->engineerRounds;if(round==1){std::lock_guard lock(run->mutex);run->engineerRequest=request.dump();}
        if(round==1&&programTask)return respond("team_send",R"({"to":"parametric","kind":"assignment","message":"Generate the two post family in namespace posts."})");
        // Studio requires a reviewer checkpoint before a team of three can complete its goal.
        if(programTask&&round==3)return respond("team_send",R"({"to":"reviewer","kind":"assignment","message":"Check the integrated post family."})");
        if(programTask&&round==5)return respond("team_finish_goal",R"({"state":"completed","summary":"Two post program published by parametric, inspected and reviewed."})");
        if(programTask&&round==6){std::lock_guard lock(run->mutex);run->finishReceipt=receiptField(request,"error").dump();}
        return respond({},{},round==1?"Offline engineer finished.":"Integrated the reported program.");
    };
    if(programTask)table[2]=[run](const auto&,const V&,const auto*){
        const int round=++run->reviewRounds;if(round==1)return respond("doc_info","{}");
        if(round==2)return respond("team_send",R"({"to":"engineer","kind":"report","message":"Reviewed the post family at the current revision."})");
        if(round==3)return respond("team_report_task",R"({"state":"completed","summary":"Reviewed the post family; no correction proposed."})");
        return respond({},{},"Review finished.");
    };
    if(programTask)table[teamParametricSlot]=[run](const auto&,const V& request,const auto*){
        const int round=++run->programRounds;
        if(round==1){std::lock_guard lock(run->mutex);run->programRequest=request.dump();if(const auto* tools=request.find("tools"))run->programTools=*tools;return respond("doc_info","{}");}
        if(round==2)return respond("parametric_program",R"({"action":"prepare","mode":"install","namespace":"posts","expected_revision":)"+receiptField(request,"revision").dump()+R"(,"program":)"+std::string(program)+"}");
        if(round==3){const auto candidate=receiptField(request,"candidate_id").asStr();{std::lock_guard lock(run->mutex);run->candidate=candidate;}return respond("team_apply_step",R"({"expected_revision":)"+receiptField(request,"revision").dump()+R"(,"candidate_id":")"+candidate+"\"}");}
        if(round==4)return respond("native_inspect",R"({"id":"posts / Post 1","detail":"properties"})");
        if(round==5){const V report=receiptField(request,"report");const auto* valid=report.find("brep_valid");const auto evidence="Published namespace posts; Post 1 brep_valid "+(valid?valid->dump():std::string("missing"));{std::lock_guard lock(run->mutex);run->reported=evidence;}V args=V::makeObj();args.set("to",V::makeStr("engineer"));args.set("kind",V::makeStr("report"));args.set("message",V::makeStr(evidence));return respond("team_send",args.dump());}
        if(round==6)return respond("team_report_task",R"({"state":"completed","summary":"Namespace posts published with two owned posts."})");
        return respond({},{},"Program published.");
    };
    return table;
}
}
static int testParametricRole(const std::string& path){
    using namespace assistant_chat;using namespace parametric_role_test;
    auto fail=[&](int code,const std::string& text){std::fprintf(stderr,"Parametric role check %d: %s\n",code,text.c_str());std::ofstream(path+".parametric-role-test.log")<<"FAIL "<<code<<": "<<text<<'\n';return code;};
    auto resize=[&](int w,int h){RECT frame{0,0,w,h};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=w;winH=h;layout();};
    auto zone=[&](int id)->const Zone*{auto found=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});return found==g_zones.end()?nullptr:&*found;};
    auto frame=[&]{HDC dc=GetDC(g_window);g_swap=false;drawFrame(dc);glFinish();ReleaseDC(g_window,dc);};
    auto click=[&](int id){frame();const Zone* z=zone(id);if(!z)return false;const int x=(int)(z->x+z->w*.5f),y=(int)(z->y+z->h*.5f);SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));return true;};
    auto agentsMenu=[&](int choice){g_teamMenuSmoke=choice;const bool clicked=click(44010);g_teamMenuSmoke=0;return clicked;};
    auto pumpTeam=[&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);do{MSG event{};while(PeekMessage(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessage(&event);}drainUiTools();pollTeam(true);if(!g_teamSnapshot.running)return true;Sleep(2);}while(std::chrono::steady_clock::now()<deadline);return false;};
    auto cards=[&](const std::string& label,const std::vector<size_t>& slots,bool shots)->int{
        // Every active card, across its pages, at each supported size and dock width.
        for(auto size:std::vector<std::pair<int,int>>{{1180,800},{1280,800},{1366,800},{1440,900},{1600,1000},{1920,1080},{2560,1200},{3840,2160}})for(int mode:{1,2}){
            resize(size.first,size.second);setChatDockMode(mode);layout();const std::string name=label+"-"+std::to_string(size.first)+"x"+std::to_string(size.second)+(mode==2?"-expanded":"");
            frame();sync(true);std::set<int> seen;
            // The page size settles with the native header, so read it after each frame.
            for(unsigned page=0,pages=1;page<pages;++page){g_teamPage=page;frame();if(!g_teamCardContentFits)return fail(20,name+": "+g_teamCardBoundsFailure);const auto dock=chatDockGeometry();const unsigned pageSize=(unsigned)(teamCardColumns()*teamCardRows());pages=(teamActiveRoleCount()+pageSize-1)/pageSize;
                for(const auto& z:g_zones)if((z.id>=44000&&z.id<44000+(int)teamCapacity)||(z.id>=44020&&z.id<44020+(int)teamCapacity)||(z.id>=44310&&z.id<44310+(int)teamCapacity)){seen.insert(z.id);if(z.x<dock.x||z.x+z.w>winW||z.y<dock.chatTop||z.y+z.h>dock.chatBottom)return fail(21,name+": role control "+std::to_string(z.id)+" leaves the dock");}
                if(shots&&page+1==pages)captureChatDockFrame(path+"."+name+".png");}
            for(size_t position=0;position<slots.size();++position)for(int id:{44000+(int)slots[position],44020+(int)slots[position],44310+(int)position})if(!seen.count(id))return fail(22,name+": role control "+std::to_string(id)+" is unreachable");
            if(seen.size()!=slots.size()*3)return fail(23,name+": cards offer controls of an inactive role");
        }
        resize(1280,800);setChatDockMode(1);g_teamPage=0;layout();return 0;
    };
    auto worstCaseSnapshot=[&](const std::vector<size_t>& slots){g_teamStarted=true;g_teamSnapshot={};for(auto slot:slots){AgentTeamRoleSnapshot role;role.id=g_teamRoleIds[slot];role.status="Readable measured status WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";role.text="Long tool message WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";role.usage.reportedResponses=1;role.usage.totalTokens=123456789;role.usage.inputTokens=123456789;role.usage.outputTokens=123456789;role.usage.reasoningTokens=123456789;g_teamSnapshot.roles.push_back(role);}};
    auto clearSnapshot=[&]{g_teamStarted=false;resetTeamView();};
    testing=true;g_testPointer=POINT{-50,-50};g_assistantDockOpen=true;g_toolsOpen=false;g_teamMode=true;g_teamStarted=false;g_chatBusy=false;g_chatAgentDetails=true;g_chatDockUserWidth=0;setChatDockMode(1);g_chatReasoningAgent=-1;
    resize(1280,800);

    // Startup default: this test never writes the file --settings-file named.
    if(g_teamParametricRole||!g_teamFocusedWorkflow||g_teamRoleCount!=3||teamActiveRoleCount()!=3||teamRoleSlot(2)!=2||teamRoleSlot(3)!=teamCapacity)return fail(1,"Default team must stay the three Focused roles");

    // Settings written before this role keep their owners and leave it off.
    std::error_code ignored;const std::filesystem::path settings=path+".role-settings.json";std::filesystem::remove(settings,ignored);g_settingsOverride=settings;
    g_assistantConfig=assistantProviderPreset("openai");g_assistantConfig.authMode=AssistantAuthMode::ApiKey;g_assistantConfig.model="offline-base";
    for(size_t i=0;i<8;++i){g_teamModels[i]="legacy-"+std::to_string(i);g_teamEfforts[i]=i%2?"high":"low";}
    {auto connector=assistantProviderPreset("grok");connector.model="legacy-connector";g_teamConnectors[1]=connector;}
    saveUiSettings();
    auto readSettings=[&]{std::ifstream in(settings,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});V value;arkjson::parse(text,value);return value;};
    {V saved=readSettings();const auto* roles=saved.find("team_roles");const auto* connectors=saved.find("team_connectors");const auto* flag=saved.find("team_parametric_designer");const auto* count=saved.find("team_role_count");
     if(!roles||roles->arr.size()!=teamCapacity||!connectors||connectors->arr.size()!=teamCapacity||!flag||flag->b||!count||count->num!=3)return fail(2,"Saved team settings lost a role row or changed the base team");
     V legacyRoles=*roles,legacyConnectors=*connectors;legacyRoles.arr.resize(8);legacyConnectors.arr.resize(8);saved.set("team_roles",legacyRoles);saved.set("team_connectors",legacyConnectors);saved.obj.erase(std::remove_if(saved.obj.begin(),saved.obj.end(),[](const auto& entry){return entry.first=="team_parametric_designer";}),saved.obj.end());
     std::ofstream(settings,std::ios::binary|std::ios::trunc)<<saved.dump();}
    g_teamModels={};g_teamEfforts={};g_teamConnectors={};loadUiSettings();
    for(size_t i=0;i<8;++i)if(g_teamModels[i]!="legacy-"+std::to_string(i)||g_teamEfforts[i]!=(i%2?"high":"low"))return fail(3,"Legacy role "+std::to_string(i)+" lost its model or effort");
    if(!g_teamConnectors[1]||g_teamConnectors[1]->provider!="grok"||g_teamConnectors[0]||g_teamConnectors[teamParametricSlot]||!g_teamModels[teamParametricSlot].empty()||!g_teamEfforts[teamParametricSlot].empty())return fail(4,"Legacy connectors moved or leaked into the new role");
    if(g_teamParametricRole||teamActiveRoleCount()!=3)return fail(5,"Legacy settings enabled the optional role");
    g_teamConnectors[1].reset();g_chatAgentDetails=true;g_teamMode=true;g_assistantDockOpen=true;setChatDockMode(1);layout();

    // Three cards exactly as before, across their pages; no control of the new role exists.
    if(int result=cards("default-focused",{0,1,2},true))return result;

    // A Focused menu cannot resize the team; the optional role is its only choice.
    if(!agentsMenu(5)||g_teamRoleCount!=3||g_teamParametricRole)return fail(8,"Focused Agents menu changed the team size");
    if(!agentsMenu(menuChoice)||!g_teamParametricRole||teamActiveRoleCount()!=4||g_teamRoleCount!=3||!g_teamFocusedWorkflow)return fail(9,"Agents menu did not add the Parametric designer to Focused");
    if(teamRoleSlot(3)!=teamParametricSlot||teamRolePosition(teamParametricSlot)!=3||teamRolePosition(3)!=teamCapacity)return fail(10,"Active position and saved slot disagree");
    if(g_status.find("Parametric designer on")!=0)return fail(11,"Enabling the role must say so: "+g_status);
    {V saved=readSettings();const auto* flag=saved.find("team_parametric_designer");const auto* count=saved.find("team_role_count");if(!flag||!flag->b||!count||count->num!=3)return fail(12,"The switch was not saved beside an unchanged base team");}

    // Cards at every supported size, ready and with worst-case text, in both workflows.
    if(int result=cards("focused",{0,1,2,teamParametricSlot},true))return result;
    worstCaseSnapshot({0,1,2,teamParametricSlot});g_verifyTeamCardBounds=true;if(int result=cards("focused-long",{0,1,2,teamParametricSlot},false))return result;clearSnapshot();
    // Parallel stays at the eight roles AgentTeam accepts: the switch trims a full team
    // and says so, the size menu stops at seven, and a saved full team loads trimmed.
    if(!agentsMenu(menuChoice)||g_teamParametricRole)return fail(60,"Agents menu did not remove the role");
    g_teamFocusedWorkflow=false;if(!agentsMenu(8)||g_teamRoleCount!=8||teamActiveRoleCount()!=8)return fail(61,"Parallel team without the role must keep eight agents");
    if(!agentsMenu(menuChoice)||!g_teamParametricRole||g_teamRoleCount!=7||teamActiveRoleCount()!=8||g_status.find("Part designer 6 is off")==std::string::npos)return fail(62,"Enabling the role on a full team must keep eight agents and say so: "+g_status);
    if(!agentsMenu(8)||g_teamRoleCount!=7||teamActiveRoleCount()!=teamMaxRoles)return fail(63,"Size menu exceeded eight agents");
    {V saved=readSettings();saved.set("team_role_count",V::makeNum(8));saved.set("team_focused_workflow",V::makeBool(false));std::ofstream(settings,std::ios::binary|std::ios::trunc)<<saved.dump();loadUiSettings();if(!g_teamParametricRole||g_teamRoleCount!=7)return fail(64,"Saved team exceeded eight agents");g_chatAgentDetails=true;g_teamMode=true;g_assistantDockOpen=true;setChatDockMode(1);layout();}
    const std::vector<size_t> full{0,1,2,3,4,5,6,teamParametricSlot};if(teamActiveRoleCount()!=8||agentCount()!=8)return fail(13,"Parallel team must count the optional role after its seven roles");
    worstCaseSnapshot(full);if(int result=cards("parallel-long",full,true))return result;clearSnapshot();g_verifyTeamCardBounds=false;
    g_chatAgentDetails=false;layout();sync(true);{wchar_t chip[32]{};GetWindowTextW(chips[7],chip,32);const bool chipped=std::wstring(chip)==L"Parametric",overflowed=(GetWindowLongPtr(moreButton,GWL_STYLE)&WS_VISIBLE)!=0;if(chatAgentName(7)!="Parametric"||!(chipped||overflowed))return fail(14,"Parametric role must be reachable from the reasoning row");}g_chatAgentDetails=true;layout();
    g_teamFocusedWorkflow=true;g_teamRoleCount=3;layout();

    // Its card owns Settings, Connector and Reasoning on slot 8, at position 3.
    const auto beforeModels=g_teamModels;const auto beforeEfforts=g_teamEfforts;const auto beforeBase=g_assistantConfig.model;
    g_teamPage=3/(unsigned)(teamCardColumns()*teamCardRows());
    {auto preset=assistantProviderPreset("claude");std::vector<std::string> values{preset.endpoint,"offline-claude",preset.apiKeyEnv,assistantProtocolName(preset.protocol)};if(preset.protocol==AssistantProtocol::Messages)values.push_back("8192");values.push_back("auto");g_dialogOverrideQueue={{"claude","0"},values};g_dialogSmoke=3;const bool connected=click(44028);g_dialogSmoke=0;
     if(!connected||g_error||!g_teamConnectors[teamParametricSlot]||teamRoleConfig(teamParametricSlot).provider!="claude"||teamRoleConfig(teamParametricSlot).model!="offline-claude"||g_teamConnectors[3]||g_teamConnectors[0]||g_assistantConfig.model!=beforeBase)return fail(15,"Parametric connector did not stay on its own card");}
    saveUiSettings();g_teamModels={};g_teamEfforts={};g_teamConnectors={};g_teamParametricRole=false;loadUiSettings();g_chatAgentDetails=true;g_teamMode=true;g_assistantDockOpen=true;setChatDockMode(1);layout();
    if(!g_teamParametricRole||!g_teamConnectors[teamParametricSlot]||teamRoleConfig(teamParametricSlot).provider!="claude"||g_teamModels[0]!=beforeModels[0]||g_teamEfforts[7]!=beforeEfforts[7])return fail(16,"Enabled role and its connector did not persist");
    g_dialogOverrideQueue={{"inherit","0"}};g_dialogSmoke=3;const bool inherited=click(44028);g_dialogSmoke=0;if(!inherited||g_teamConnectors[teamParametricSlot])return fail(17,"Parametric card could not restore the shared connector");
    g_dialogOverrideQueue={{"offline-program","low"}};g_dialogSmoke=3;g_dialogShot=path+".role-settings.png";const bool edited=click(44008);g_dialogSmoke=0;g_dialogShot.clear();
    if(!edited||g_error||g_teamModels[teamParametricSlot]!="offline-program"||g_teamEfforts[teamParametricSlot]!="low"||teamRoleConfig(teamParametricSlot).model!="offline-program"||teamRoleConfig(teamParametricSlot).reasoningEffort!="low")return fail(18,"Parametric Settings did not choose its model and effort");
    for(size_t i=0;i<8;++i)if(g_teamModels[i]!=beforeModels[i]||g_teamEfforts[i]!=beforeEfforts[i])return fail(19,"Parametric Settings changed role "+std::to_string(i));
    if(!click(44313)||g_chatReasoningAgent!=3||g_chatAgentDetails)return fail(24,"Parametric Reasoning link must open its own panel");
    {std::wstring caption,body;if(!agentReasoning(3,caption,body)||caption.find(L"Parametric (parametric)")==std::wstring::npos||caption.find(L"PARAMETRIC DESIGNER")==std::wstring::npos)return fail(25,"Reasoning panel named the wrong role");}
    g_chatReasoningAgent=-1;g_chatAgentDetails=true;layout();

    // The pack Studio gives a real provider must expose the program tool.
    {V pack;if(!arkjson::parse(teamRoleOnboardingPack(teamParametricSlot),pack))return fail(26,"Parametric onboarding pack is unreadable");std::vector<std::string> topics;if(const auto* rows=pack.find("topics"))for(const auto& row:rows->arr)if(const auto* topic=row.find("topic"))topics.push_back(topic->asStr());if(topics!=std::vector<std::string>{"overview","programs"})return fail(27,"Parametric role must start with overview and programs only");}

    // Offline Focused run: engineer assigns, the role prepares, publishes and reports.
    {V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);if(g_error)return fail(30,"Scratch document rejected");}
    for(size_t i=0;i<3;++i){g_teamModels[i]="offline-role";g_teamEfforts[i]="low";}
    // Slow tools take Studio's worker path, as outside tests; the main thread's stack is shorter.
    const auto startRevision=g_session.revision;auto programRun=std::make_shared<Run>();g_teamTarget.clear();g_busyTestMode=true;
    sendTeam("OFFLINE: generate two posts as a parametric program.",transports(programRun,true));
    if(!g_teamStarted||!g_agentTeam.snapshot().running)return fail(31,"Offline team did not start: "+g_status);
    pollTeam(true);if(!agentsMenu(menuChoice)||!g_teamParametricRole||g_status.find("Finish or cancel")!=0)return fail(32,"A running team must refuse the role switch");
    programRun->gate=true;if(!pumpTeam())return fail(33,"Offline program run did not finish; "+g_teamSnapshot.roles[3].status+" / "+g_teamSnapshot.roles[3].error);
    {MSG event{};while(PeekMessage(&event,nullptr,0,0,PM_REMOVE)){TranslateMessage(&event);DispatchMessage(&event);}}drainUiTools();pollTeam(true);
    if(g_teamSnapshot.roles.size()!=4)return fail(34,"Focused team with the role must have four members");
    if(!g_teamSnapshot.goalDeclared||g_teamSnapshot.goalState!=AgentGoalState::Completed||g_teamSnapshot.unresolvedErrors)return fail(48,"Goal must complete with the role in the team: "+std::string(agentGoalStateName(g_teamSnapshot.goalState))+", unresolved "+std::to_string(g_teamSnapshot.unresolvedErrors)+", role task "+g_teamSnapshot.roles[3].taskState+", finish receipt "+programRun->finishReceipt);
    if(g_teamSnapshot.roles[3].taskState!="completed")return fail(49,"Role task ended "+g_teamSnapshot.roles[3].taskState);
    const auto& parametric=g_teamSnapshot.roles[3];
    if(parametric.id!="parametric"||parametric.label!="PARAMETRIC DESIGNER"||!parametric.canWrite||parametric.discipline!=AgentTeamDiscipline::ParametricDesigner||parametric.model!="offline-program"||parametric.reasoningEffort!="low")return fail(35,"Role snapshot differs from the requested configuration: "+parametric.id+" / "+parametric.model);
    if(parametric.state!=AgentTeamState::Idle||!parametric.error.empty())return fail(36,"Role ended "+std::string(agentTeamStateName(parametric.state))+": "+parametric.error+" / "+parametric.status);
    if(programRun->candidate.empty()||parametric.stepsApplied!=1||g_session.revision!=startRevision+1)return fail(37,"Program candidate was not published once; steps "+std::to_string(parametric.stepsApplied)+", revision "+std::to_string(g_session.revision));
    if(g_doc.parametricPrograms.t!=V::Arr||g_doc.parametricPrograms.arr.size()!=1||g_doc.features.size()!=2)return fail(38,"Document must hold one program and its two posts");
    double publishedVolume=0;for(const auto& [name,expected]:std::vector<std::pair<std::string,double>>{{"posts / Post 1",160},{"posts / Post 2",176}}){V args=V::makeObj();args.set("id",V::makeStr(name));args.set("detail",V::makeStr("properties"));auto result=runTool("native_inspect",args);const V* data=result.find("structuredContent");if(!data)data=&result;const auto* report=data->find("report");if(!report)report=data;const auto* valid=report->find("brep_valid");const auto* volume=report->find("volume_mm3");if(!valid||!valid->b||!volume||std::abs(volume->num-expected)>1e-3)return fail(39,name+" is not the exact generated solid: "+result.dump());publishedVolume+=volume->num;}
    {std::lock_guard lock(programRun->mutex);
     if(programRun->programRequest.find("parametric programs assigned to you")==std::string::npos)return fail(40,"Role did not receive the program workflow");
     if(programRun->programRequest.find("Own the rule-driven configurable families")==std::string::npos)return fail(41,"Role did not receive its Studio instructions");
     if(programRun->engineerRequest.find("Delegate rule-driven configurable families")==std::string::npos||programRun->engineerRequest.find("to parametric as parametric programs")==std::string::npos)return fail(42,"Lead was not told about the role");
     if(programRun->reported.find("brep_valid true")==std::string::npos||programRun->engineerRounds<2)return fail(43,"Native evidence report did not reach the engineer: "+programRun->reported);}
    if(!g_teamReceipts[teamParametricSlot]||g_teamReceipts[teamParametricSlot]->source!="studio_team_parametric"||g_teamReceipts[3])return fail(44,"Usage receipt must belong to slot 8, not position 3");
    if(g_teamActivity[teamParametricSlot].empty()||!g_teamActivity[3].empty())return fail(45,"Live activity must belong to the role's own slot");
    // The request is the role's real catalogue; Studio defers it for real providers.
    const size_t fullTools=programRun->programTools.arr.size();auto hasTool=[](const V& tools,const char* name){return std::any_of(tools.arr.begin(),tools.arr.end(),[&](const V& tool){const auto* value=tool.find("name");return value&&value->asStr()==name;});};
    if(!fullTools||fullTools>128||!hasTool(programRun->programTools,"parametric_program")||!hasTool(programRun->programTools,"team_apply_step"))return fail(46,"Role catalogue exceeds 128 functions or lacks its publish path: "+std::to_string(fullTools));
    const V deferred=assistantActiveTools(programRun->programTools,V::makeArr(),teamRoleOnboardingPack(teamParametricSlot));const size_t deferredTools=deferred.arr.size();
    if(!deferredTools||deferredTools>128||!hasTool(deferred,"parametric_program")||!hasTool(deferred,"team_apply_step")||!hasTool(deferred,"native_inspect"))return fail(47,"Deferred role catalogue lost its program tools");
    g_busyTestMode=false;frame();captureChatDockFrame(path+".team-run.png");

    // Off again: a finished team clears, saved choices stay, and the next run is the default.
    if(!agentsMenu(menuChoice)||g_teamParametricRole||g_teamStarted||teamActiveRoleCount()!=3)return fail(50,"Agents menu did not remove the role");
    if(g_teamModels[teamParametricSlot]!="offline-program")return fail(51,"Disabling the role lost its model");
    g_agentTeam.waitIdle();auto defaultRun=std::make_shared<Run>();defaultRun->gate=true;sendTeam("OFFLINE: default team check.",transports(defaultRun,false));if(!pumpTeam())return fail(52,"Default offline run did not finish");
    if(g_teamSnapshot.roles.size()!=3||g_teamSnapshot.roles[0].id!="engineer"||g_teamSnapshot.roles[1].id!="designer"||g_teamSnapshot.roles[2].id!="reviewer")return fail(53,"Default run is not engineer, designer, reviewer");
    if(g_teamSnapshot.roles[1].discipline!=AgentTeamDiscipline::PartDesigner||g_teamSnapshot.roles[2].discipline!=AgentTeamDiscipline::PhysicsReviewer||g_teamSnapshot.roles[2].canWrite)return fail(54,"Default disciplines changed");
    {std::lock_guard lock(defaultRun->mutex);if(defaultRun->engineerRequest.empty()||defaultRun->engineerRequest.find("Delegate rule-driven configurable families")!=std::string::npos)return fail(55,"Default lead must not be told about a disabled role");}
    cancelTeam();g_agentTeam.waitIdle();pollTeam(true);clearSnapshot();

    // Parallel at capacity: the optional role is the eighth agent.
    if(!agentsMenu(menuChoice)||!g_teamParametricRole)return fail(56,"Agents menu did not restore the role");
    g_teamFocusedWorkflow=false;g_teamRoleCount=7;layout();auto fullRun=std::make_shared<Run>();fullRun->gate=true;sendTeam("OFFLINE: full team check.",transports(fullRun,false));if(!g_teamStarted||!pumpTeam())return fail(57,"Eight role Parallel team did not finish: "+g_status);
    if(g_teamSnapshot.roles.size()!=8||g_teamSnapshot.roles[3].id!="designer2"||g_teamSnapshot.roles[7].id!="parametric"||g_teamSnapshot.roles[7].discipline!=AgentTeamDiscipline::ParametricDesigner)return fail(58,"Parallel team order changed");
    if(!g_teamReceipts[teamParametricSlot]||!g_teamReceipts[3]||g_teamReceipts[3]->source!="studio_team_designer2")return fail(59,"Parallel usage receipts lost their roles");
    cancelTeam();g_agentTeam.waitIdle();pollTeam(true);clearSnapshot();g_teamFocusedWorkflow=true;g_teamRoleCount=3;

    std::ofstream(path+".parametric-role-test.log")<<"PASS revision "<<startRevision<<"->"<<startRevision+1<<" volume_mm3 "<<publishedVolume<<" tools "<<fullTools<<" deferred_tools "<<deferredTools<<" program_rounds "<<programRun->programRounds<<" engineer_rounds "<<programRun->engineerRounds<<'\n';
    std::error_code removed;std::filesystem::remove(settings,removed);return 0;
}
