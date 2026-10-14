// Include after image_attachment.inl. Keep one mailbox per Assistant run;
// queued UI work owns the shared pointer independently of the next run.
static std::shared_ptr<AssistantSteering> g_assistantSteering;
static u64 g_assistantSteeringEpoch=0;
static AssistantTransport g_assistantSteeringTransport;
struct DeferredAssistantSteering {arkjson::Value message;std::string visiblePrompt;AssistantTransport transport;u64 epoch=0;};
static std::deque<DeferredAssistantSteering> g_deferredAssistantSteering;
static bool queueActiveAssistantSteering(const arkjson::Value& message,const std::string& visiblePrompt,bool consumeComposer){
    if(!g_chatBusy||!g_assistantSteering)return false;
    if(*g_assistantCancel){g_status="Assistant is stopping; the message remains in the composer.";return false;}
    if(g_assistantSteeringEpoch!=g_documentEpoch){g_status="Drawing changed during the Assistant run. Finish stopping before sending from the current drawing.";return false;}
    if(!g_assistantSteering->enqueue(message)){
        // The worker has atomically finished, but its final UI event can still
        // be queued. Preserve this exact message and start it after that event.
        arkjson::Value input=g_assistantSteering->history();for(const auto& queued:g_deferredAssistantSteering)input.arr.push_back(queued.message);input.arr.push_back(message);
        validateAssistantSteeringInput(g_assistantConfig,input,AgentSession::tools());
        g_deferredAssistantSteering.push_back({message,visiblePrompt,g_assistantSteeringTransport,g_documentEpoch});
        g_assistantStatus="Decision queued after the finishing response.";
    }else g_assistantStatus="Decision queued for the next provider request.";
    g_chatLog.push_back({"You -> Assistant",visiblePrompt});g_chatScroll=0;g_status=g_assistantStatus;
    if(consumeComposer)clearPendingAssistantImage();g_pendingObjectTarget.reset();if(g_chatInput)SetWindowTextW(g_chatInput,L"");return true;
}
// Declare before drainUiTools, and define here after UiToolRequest gains its
// steering pointer/version. Call immediately before execution on the UI thread.
static bool assistantSteeringToolAllowed(const UiToolRequest& request){
    if(request.tool=="__ark_activity"||request.tool=="__ark_bom_completion")return true;
    static const auto tools=AgentSession::tools();if(assistantToolReadOnly(tools,request.tool,request.args))return true;
    if(request.teamOwned)return g_agentTeam.requestCurrent(request.roleId,request.steeringVersion);
    return !request.steering||request.steering->current(request.steeringVersion);
}
static arkjson::Value assistantSteeringToolRejection(){
    using V=arkjson::Value;V result=V::makeObj(),detail=V::makeObj();detail.set("error",V::makeStr("User steering arrived while this operation waited for the UI. Consume the new input before another write; this operation was not executed."));detail.set("error_code",V::makeStr("assistant_input_pending"));detail.set("document_unchanged",V::makeBool(true));result.set("isError",V::makeBool(true));result.set("structuredContent",detail);return result;
}
