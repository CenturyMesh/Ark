// SDK subtype names describe the envelope, not successful task completion.
// Persist only categorical host observations; provider free text may contain
// private model data, account details or credentials and stays out of receipts.
struct ClaudeResult071 {
    bool failed=false;
    std::string category="completed",subtype,terminal,stop,advice;
    std::optional<std::uint64_t> status;
    V evidence;
};
ClaudeResult071 classifyClaudeResult071(const V& packet){
    ClaudeResult071 out;
    auto known=[](const std::string& value,std::initializer_list<const char*> values){for(auto x:values)if(value==x)return value;return value.empty()?std::string():std::string("unsupported");};
    out.subtype=known(str(packet,"subtype"),{"success","error_max_turns","error_during_execution","error_max_budget_usd","error_max_structured_output_retries"});
    out.terminal=known(str(packet,"terminal_reason"),{"completed","max_turns","tool_deferred","aborted_streaming","aborted_tools","hook_stopped","stop_hook_prevented","background_requested","blocking_limit","rapid_refill_breaker","prompt_too_long","image_error","model_error","api_error","malformed_tool_use_exhausted","budget_exhausted","structured_output_retry_exhausted","tool_deferred_unavailable","turn_setup_failed"});
    out.stop=known(str(packet,"stop_reason"),{"end_turn","stop_sequence","max_tokens","tool_use","pause_turn","refusal","model_context_window_exceeded"});
    out.status=count(&packet,"api_error_status");if(out.status&&(*out.status<100||*out.status>599))out.status.reset();
    const auto* error=packet.find("is_error");
    bool malformed=(error&&error->t!=V::Bool)||out.subtype.empty()||out.subtype=="unsupported";
    for(const char* key:{"terminal_reason","stop_reason"})if(const auto* value=packet.find(key);value&&!value->isNull()&&value->t!=V::Str)malformed=true;
    if(const auto* value=packet.find("api_error_status");value&&!value->isNull()&&!out.status)malformed=true;
    const bool terminalFailure=!out.terminal.empty()&&out.terminal!="completed";
    // API stop reasons are not SDK completion flags. A completed SDK turn can
    // legitimately follow tools; only explicit truncation/refusal stops fail.
    const bool stopFailure=out.stop=="max_tokens"||out.stop=="model_context_window_exceeded"||out.stop=="refusal";
    out.failed=malformed||out.subtype!="success"||(error&&error->t==V::Bool&&error->b)||terminalFailure||stopFailure||(out.status&&*out.status>=400);
    std::string text=str(packet,"result");if(const auto* errors=packet.find("errors");errors&&errors->t==V::Arr)for(const auto& item:errors->arr)if(item.t==V::Str&&text.size()<65536)text+='\n'+item.str.substr(0,65536-text.size());
    if(text.size()>65536)text.resize(65536);for(char& c:text)if((unsigned char)c<128)c=(char)std::tolower((unsigned char)c);
    auto has=[&](const char* value){return text.find(value)!=std::string::npos;};
    bool statusFromText=false;
    if(out.failed&&!out.status){const auto at=text.find("api error:");if(at!=std::string::npos){size_t start=at+10;while(start<text.size()&&text[start]==' ')++start;
        if(start+3<=text.size()&&text[start]>='4'&&text[start]<='5'&&std::isdigit((unsigned char)text[start+1])&&std::isdigit((unsigned char)text[start+2])&&(start+3==text.size()||!std::isdigit((unsigned char)text[start+3]))){out.status=(text[start]-'0')*100+(text[start+1]-'0')*10+text[start+2]-'0';statusFromText=true;}}}
    if(out.failed){
        if(malformed){out.category="invalid_result";out.advice="The CLI returned an unsupported result envelope. Update or inspect the configured Claude Code version.";}
        else if(out.terminal=="prompt_too_long"||out.stop=="model_context_window_exceeded"||has("prompt is too long")||has("prompt too long")||has("context length")||has("context window")){
            out.category="context_limit";out.advice="Claude Code rejected the request because its context is too large. Start a fresh ARK chat for the remaining task and inspect the current design; the drawing and committed edits are retained.";
        }else if(has("oauth")&&(has("expired")||has("refresh"))){
            out.category="authentication_expired";out.advice="The Claude Code OAuth token has expired or could not be refreshed. In ARK, open Claude Code > Reconnect Claude Code (or run claude auth login), complete the official sign-in, then resume. This is not a CAD file or local RAM limit; prior work is retained.";
        }else if((out.status&&*out.status==401)||has("/login")||has("not logged in")||has("authentication_error")||has("invalid api key")){
            out.category="authentication";out.advice="Claude Code is not signed in or its authentication was rejected. Run claude in a terminal and complete /login or correct the CLI's configured authentication; ARK did not switch providers.";
        }else if(out.terminal=="blocking_limit"||out.terminal=="rapid_refill_breaker"||(out.status&&*out.status==429)||has("rate limit")||has("usage limit")||has("credit balance")||has("hit your limit")||has("too many requests")){
            out.category="usage_limit";out.advice="Claude Code reported an account usage, rate or credit limit. Check the CLI account's limit/reset and resume afterwards; adding RAM or CAD workers does not change this provider limit.";
        }else if(has("certificate")||has("self signed")||has("unable to verify")){
            out.category="tls_error";out.advice="Claude Code could not verify the secure connection. Inspect its proxy/certificate setup; ARK does not disable TLS verification.";
        }else if(has("connection error")||has("econnrefused")||has("econnreset")||has("enotfound")||has("fetch failed")||has("socket hang")||has("timed out")){
            out.category="connection_error";out.advice="Claude Code could not reach its configured provider. Check that CLI's network/proxy connection; the CAD file was not the source of this transport failure.";
        }else if((out.status&&*out.status==403)||has("permission_error")){
            out.category="permission_error";out.advice="The provider denied this Claude Code account or request. Check the CLI account and access to the selected model.";
        }else if(out.terminal=="budget_exhausted"||out.subtype=="error_max_budget_usd"||out.terminal=="max_turns"||out.subtype=="error_max_turns"){
            out.category="cli_limit";out.advice="The Claude Code runtime reported a configured budget or turn limit. Inspect its configuration; ARK did not add a new agent turn/token budget.";
        }else if(out.stop=="max_tokens"){
            out.category="incomplete_output";out.advice="The provider stopped at an output limit. Its answer was not accepted as completed work; resume from the retained receipts.";
        }else if(out.stop=="refusal"){
            out.category="refusal";out.advice="Claude Code reported a refusal. Review the request in the CLI; no task completion was declared.";
        }else if(out.terminal=="image_error"){
            out.category="image_error";out.advice="Claude Code rejected an image in the request. Review the attachment; the design remains retained.";
        }else if(out.terminal=="model_error"){
            out.category="model_error";out.advice="Claude Code reported a model error. Refresh its model catalogue and inspect access to the explicitly selected model.";
        }else if(out.status&&*out.status>=500){out.category="provider_unavailable";out.advice="The provider reported a service error. Wait for it to recover, then resume from the retained receipts.";}
        else if((out.status&&*out.status==400)||has("invalid_request_error")){out.category="invalid_request";out.advice="Claude Code's provider rejected the request format or configuration. Inspect the CLI model/settings and connector contract; do not replay committed CAD operations.";}
        else {out.category=(out.terminal.empty()||out.terminal=="completed")?"provider_error":out.terminal;out.advice="Claude Code did not complete the request. Inspect the terminal reason/status in Run info or the CLI; prior committed work remains retained.";}
    }
    out.evidence=obj();out.evidence.set("schema",V::makeStr("ark.claude-result.v1"));out.evidence.set("provider",V::makeStr("claude-code"));out.evidence.set("protocol",V::makeStr("claude-code"));
    out.evidence.set("result_subtype",V::makeStr(out.subtype));out.evidence.set("terminal_reason",V::makeStr(out.terminal));out.evidence.set("stop_reason",V::makeStr(out.stop));out.evidence.set("category",V::makeStr(out.category));out.evidence.set("failed",V::makeBool(out.failed));
    if(error&&error->t==V::Bool)out.evidence.set("is_error",*error);if(out.status)out.evidence.set("api_error_status",V::makeNum((double)*out.status));
    out.evidence.set("status_from_text_prefix",V::makeBool(statusFromText));out.evidence.set("result_text_bytes",V::makeNum((double)str(packet,"result").size()));
    V signals=array();for(const char* term:{"connection","certificate","authentication","permission","invalid_request","model","credit","rate_limit","timeout","proxy","network","token","not found","unauthorized","forbidden","oauth","billing","expired","scope","system prompt","claude code","supported","refresh","missing","only","re-authenticate","authorization","not valid","organization","plan","prohibited"})if(has(term))signals.arr.push_back(V::makeStr(term));out.evidence.set("error_signals",signals);
    out.evidence.set("provider_text_retained",V::makeBool(false));return out;
}
