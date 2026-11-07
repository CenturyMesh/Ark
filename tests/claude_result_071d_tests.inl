namespace claude_result_071d {
void checks(){
    struct Case {const char* envelope;const char* category;};
    const Case cases[]={
        {R"({"is_error":true,"terminal_reason":"prompt_too_long","api_error_status":400,"result":"SECRET_PRIVATE_PROVIDER_TEXT"})","context_limit"},
        {R"({"is_error":true,"terminal_reason":"api_error","api_error_status":401,"result":"SECRET_PRIVATE_PROVIDER_TEXT"})","authentication"},
        {R"({"is_error":true,"terminal_reason":"api_error","stop_reason":"stop_sequence","result":"OAuth session expired and could not be refreshed. SECRET_PRIVATE_PROVIDER_TEXT"})","authentication_expired"},
        {R"({"is_error":true,"terminal_reason":"blocking_limit","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","usage_limit"},
        {R"({"is_error":true,"terminal_reason":"api_error","api_error_status":429,"result":"SECRET_PRIVATE_PROVIDER_TEXT"})","usage_limit"},
        {R"({"is_error":true,"terminal_reason":"budget_exhausted","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","cli_limit"},
        {R"({"is_error":false,"stop_reason":"max_tokens","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","incomplete_output"},
        {R"({"is_error":false,"stop_reason":"refusal","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","refusal"},
        {R"({"is_error":true,"terminal_reason":"api_error","api_error_status":503,"result":"SECRET_PRIVATE_PROVIDER_TEXT"})","provider_unavailable"},
        {R"({"is_error":true,"terminal_reason":"image_error","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","image_error"},
        {R"({"is_error":true,"terminal_reason":"model_error","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","model_error"},
        {R"({"is_error":true,"terminal_reason":"completed","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","provider_error"},
        {R"({"is_error":"false","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","invalid_result"},
        {R"({"is_error":false,"terminal_reason":42,"result":"SECRET_PRIVATE_PROVIDER_TEXT"})","invalid_result"},
        {R"({"is_error":false,"api_error_status":"401","result":"SECRET_PRIVATE_PROVIDER_TEXT"})","invalid_result"},
        {R"({"is_error":true,"result":"Prompt is too long: SECRET_PRIVATE_PROVIDER_TEXT"})","context_limit"},
        {R"({"is_error":true,"errors":["Credit balance too low: SECRET_PRIVATE_PROVIDER_TEXT"],"result":""})","usage_limit"}
    };
    for(const auto& example:cases){
        auto state=std::make_shared<State>();state->terminalOverride=json(example.envelope);int reads=0,writes=0;V retained,outcome;std::string error;unsigned completions=0;
        AssistantRetryHooks hooks;hooks.acceptCompletion=[&]{++completions;return true;};
        try{runAssistant(config(state),V::makeStr("Synthetic provider failure after committed edit"),tools(),[&](const auto& name,const V&){if(name=="doc_info")++reads;else ++writes;return receipt(name=="doc_info"?7:8);},[&](const AssistantEvent& event){if(!event.providerOutcome.isNull())outcome=event.providerOutcome;},nullptr,{},{},[&](const V& value){retained=value;},hooks);}catch(const AssistantTransportError& e){error=e.what();}
        check(!error.empty()&&error.find(example.category)!=std::string::npos,"CLI failure must expose the actual categorical cause, not success-envelope wording");
        check(reads==1&&writes==1&&state->users==1&&completions==0,"Provider failure cannot replay a write, retry inference or declare completion");
        check(contains(retained,"ARK_CLI_FAILURE_V1")&&contains(retained,"ARK_ACP_INSPECT_REQUIRED"),"Failure must retain categorical evidence and fresh-inspection recovery");
        check(field(outcome,"category")==example.category&&outcome.find("failed")->b,"Usage event retains safe terminal diagnostics even on an error result");
        check(retained.dump().find("SECRET_PRIVATE_PROVIDER_TEXT")==std::string::npos&&error.find("SECRET_PRIVATE_PROVIDER_TEXT")==std::string::npos&&outcome.dump().find("SECRET_PRIVATE_PROVIDER_TEXT")==std::string::npos,"Private provider body is not copied into error/receipts");
        int outputs=0;for(const auto& row:retained.arr)if(field(row,"type")=="function_call_output")++outputs;check(outputs==2,"Failure preserves the earlier exact host read/write receipts");
    }
    for(const char* terminal:{"completed",""}){
        auto state=std::make_shared<State>();state->terminalOverride=json(R"({"is_error":false,"stop_reason":"end_turn","result":"Successful synthetic completion"})");state->terminalOverride.set("terminal_reason",V::makeStr(terminal));V outcome;
        auto result=runAssistant(config(state),V::makeStr("Synthetic completed edit"),tools(),[](const auto& name,const V&){return receipt(name=="doc_info"?7:8);},[&](const AssistantEvent& e){if(!e.providerOutcome.isNull())outcome=e.providerOutcome;});
        check(result.text=="Successful synthetic completion"&&!outcome.find("failed")->b&&!contains(result.conversation,"ARK_ACP_INSPECT_REQUIRED"),"Real and legacy successful results remain accepted unchanged");
    }
    std::cout<<"Claude result diagnostics:17 failure contracts, retained writes, no replay/private-text leak and genuine success passed\n";
}
}
