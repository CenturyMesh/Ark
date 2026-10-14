#include "ark/assistant_connectors.hpp"
#include "assistant_vision.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string text(const V& value,const char* key){const auto* item=value.find(key);return item?item->asStr():"";}
const V& need(const V& value,const char* key){const auto* item=value.find(key);require(item!=nullptr,"Provider response/history is missing a required field.");return *item;}
void erase(V& value,const char* key){value.obj.erase(std::remove_if(value.obj.begin(),value.obj.end(),[&](const auto& item){return item.first==key;}),value.obj.end());}
bool validId(const std::string& value){return !value.empty()&&value.size()<=256&&std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=33&&c!=127;});}
bool sameReplayRoute(const AssistantConfig& config,const V& item){return text(item,"ark_provider")==config.provider&&text(item,"ark_protocol")==assistantProtocolName(config.protocol)&&text(item,"ark_endpoint")==config.endpoint&&text(item,"ark_model")==config.model&&text(item,"ark_auth_mode")== (config.authMode==AssistantAuthMode::ChatGptPlan?"chatgpt-plan":"api-key");}
void tagReplay(V& item,const AssistantConfig& config){item.set("ark_provider",V::makeStr(config.provider));item.set("ark_protocol",V::makeStr(assistantProtocolName(config.protocol)));item.set("ark_endpoint",V::makeStr(config.endpoint));item.set("ark_model",V::makeStr(config.model));item.set("ark_auth_mode",V::makeStr(config.authMode==AssistantAuthMode::ChatGptPlan?"chatgpt-plan":"api-key"));}
V message(const std::string& role,const std::string& content){V out=V::makeObj();out.set("role",V::makeStr(role));out.set("content",V::makeStr(content));return out;}
std::string messageText(const V& content){
    if(content.t==V::Str)return content.str;
    if(content.isNull())return {};
    require(content.t==V::Arr,"Provider adapter supports text messages only.");std::string out;
    for(const auto& block:content.arr){const auto kind=text(block,"type");require(kind=="output_text"||kind=="input_text"||kind=="text"||kind=="refusal","Unsupported non-text history content for this provider.");const auto* value=block.find(kind=="refusal"?"refusal":"text");require(value&&value->t==V::Str,"History text content must be a string.");if(!out.empty())out+='\n';out+=value->str;}return out;
}
V chatContent(const V& content){
    if(content.t!=V::Arr)return V::makeStr(messageText(content));
    V out=V::makeArr();bool visual=false;
    for(const auto& part:content.arr){V block=V::makeObj();if(text(part,"type")=="input_image"){
            const auto image=assistant_vision::dataUrl(need(part,"image_url"));block.set("type",V::makeStr("image_url"));V url=V::makeObj();url.set("url",V::makeStr("data:"+image.first+";base64,"+image.second));url.set("detail",V::makeStr("auto"));block.set("image_url",url);visual=true;
        }else{V one=V::makeArr();one.arr.push_back(part);block.set("type",V::makeStr("text"));block.set("text",V::makeStr(messageText(one)));}out.arr.push_back(block);
    }
    return visual?out:V::makeStr(messageText(content));
}
V chatMessages(const AssistantConfig& config,const V& canonical,const std::string& instructions){
    require(canonical.t==V::Arr,"Assistant history must be an input array.");V messages=V::makeArr();messages.arr.push_back(message("system",instructions));
    V current;std::set<std::string> pending,seen;
    auto start=[&]{if(current.t!=V::Obj)current=message("assistant","");};
    auto flush=[&]{if(current.t==V::Obj){if(!text(current,"content").empty()||current.find("tool_calls")||current.find("ark_messages_reasoning"))messages.arr.push_back(std::move(current));current=V{};}};
    for(const auto& item:canonical.arr){
        require(item.t==V::Obj,"Assistant history entries must be objects.");const auto type=text(item,"type"),role=text(item,"role");
        if(type=="reasoning"){
            if(sameReplayRoute(config,item)){
                if(const auto* reasoning=item.find("ark_chat_reasoning")){require(reasoning->t==V::Str,"Invalid retained chat reasoning.");start();current.set("reasoning_content",*reasoning);}
                if(const auto* reasoning=item.find("ark_messages_content")){require(reasoning->t==V::Arr,"Invalid retained Messages thinking.");start();V retained=current.find("ark_messages_reasoning")?*current.find("ark_messages_reasoning"):V::makeArr();for(const auto& part:reasoning->arr)retained.arr.push_back(part);current.set("ark_messages_reasoning",retained);}
            }
            continue;
        }
        if(type=="function_call"){
            require(pending.empty()||current.t==V::Obj,"Assistant tool results must precede a new call batch.");start();const auto id=text(item,"call_id");require(validId(id)&&seen.insert(id).second,"Missing or repeated retained tool call ID.");pending.insert(id);
            V call=V::makeObj(),fn=V::makeObj();call.set("id",V::makeStr(id));call.set("type",V::makeStr("function"));fn.set("name",need(item,"name"));fn.set("arguments",need(item,"arguments"));call.set("function",fn);
            V calls=current.find("tool_calls")?*current.find("tool_calls"):V::makeArr();calls.arr.push_back(call);current.set("tool_calls",calls);continue;
        }
        if(type=="function_call_output"){
            flush();const auto id=text(item,"call_id");require(pending.erase(id)==1,"Orphan or repeated retained tool result.");const auto& output=need(item,"output");require(output.t==V::Str,"Tool result must be text.");V reply=message("tool",output.str);reply.set("tool_call_id",V::makeStr(id));messages.arr.push_back(reply);continue;
        }
        if(role=="assistant"||(type=="message"&&role.empty())){
            require(pending.empty()||current.t==V::Obj,"Assistant message precedes unresolved tool results.");start();const auto value=messageText(need(item,"content"));auto combined=text(current,"content");if(!combined.empty()&&!value.empty())combined+='\n';combined+=value;current.set("content",V::makeStr(combined));continue;
        }
        require(role=="user"||role=="developer"||role=="system","Unsupported retained history item.");flush();require(pending.empty(),"Retained tool calls need results before another user/system message.");V row=message(role=="developer"?"system":role,"");row.set("content",role=="user"?chatContent(need(item,"content")):V::makeStr(messageText(need(item,"content"))));messages.arr.push_back(row);
    }
    flush();require(pending.empty(),"Retained tool call has no result; inspect document before continuing.");return messages;
}
V block(const char* kind,const std::string& value){V out=V::makeObj();out.set("type",V::makeStr(kind));out.set("text",V::makeStr(value));return out;}
V normalizedMessage(const std::string& value){V out=V::makeObj(),content=V::makeArr();out.set("type",V::makeStr("message"));out.set("role",V::makeStr("assistant"));content.arr.push_back(block("output_text",value));out.set("content",content);return out;}
V normalizedCall(const std::string& id,const std::string& name,const V& arguments){require(validId(id),"Provider tool call needs a valid ID.");require(!name.empty()&&arguments.t==V::Str,"Provider tool call needs name and JSON text arguments.");V checked;require(arkjson::parse(arguments.str,checked)&&checked.t==V::Obj,"Provider tool arguments must be a complete JSON object.");V out=V::makeObj();out.set("type",V::makeStr("function_call"));out.set("call_id",V::makeStr(id));out.set("name",V::makeStr(name));out.set("arguments",arguments);return out;}
V providerOutcome(const AssistantConfig& config,const V& source,const V& normalized,const char* normalization){
    V outcome=V::makeObj();outcome.set("protocol",V::makeStr(assistantProtocolName(config.protocol)));
    const std::set<std::string> statuses={"completed","incomplete","failed","cancelled","queued","in_progress"};const auto status=text(normalized,"status");outcome.set("response_status",V::makeStr(statuses.count(status)?status:"unknown"));
    std::string stop;if(config.protocol==AssistantProtocol::ChatCompletions){if(const auto* choices=source.find("choices");choices&&choices->t==V::Arr&&choices->arr.size()==1)stop=text(choices->arr.front(),"finish_reason");}else if(config.protocol==AssistantProtocol::Messages)stop=text(source,"stop_reason");else if(const auto* details=source.find("incomplete_details"))stop=text(*details,"reason");
    const std::set<std::string> stops={"stop","tool_calls","length","content_filter","function_call","end_turn","tool_use","stop_sequence","max_tokens","pause_turn","refusal","max_output_tokens"};outcome.set("stop_reason",V::makeStr(stop.empty()?"missing":stops.count(stop)?stop:"unknown"));
    outcome.set("normalization",V::makeStr(normalization));return outcome;
}
V normalizedUsage(const V* usage,bool messages){
    if(!usage||usage->t!=V::Obj)return V{};
    V out=V::makeObj();const auto* input=usage->find(messages?"input_tokens":"prompt_tokens"),*output=usage->find(messages?"output_tokens":"completion_tokens");
    const auto validCount=[](const V* value){return value&&value->t==V::Num&&std::isfinite(value->num)&&value->num>=0&&value->num<=9007199254740991.0&&std::floor(value->num)==value->num;};
    if(!validCount(input)||!validCount(output))return V{};out.set("input_tokens",*input);out.set("output_tokens",*output);
    if(messages){
        // Anthropic input_tokens excludes cached input; all provider-reported
        // cache creation/read tokens contribute once to consumed input.
        double cached=0,creation=0;
        for(const auto& item:std::initializer_list<std::pair<const char*,double*>>{{"cache_read_input_tokens",&cached},{"cache_creation_input_tokens",&creation}})if(const auto* count=usage->find(item.first)){if(!validCount(count))return V{};*item.second=count->num;}
        if(input->t!=V::Num||output->t!=V::Num)return V{};out.set("input_tokens",V::makeNum(input->num+cached+creation));out.set("total_tokens",V::makeNum(input->num+cached+creation+output->num));V details=V::makeObj();details.set("cached_tokens",V::makeNum(cached));out.set("input_tokens_details",details);
    }else{
        const auto* total=usage->find("total_tokens");if(!total)return V{};out.set("total_tokens",*total);
        if(const auto* details=usage->find("prompt_tokens_details"))out.set("input_tokens_details",*details);
        if(const auto* details=usage->find("completion_tokens_details"))out.set("output_tokens_details",*details);
    }
    return out;
}
std::string baseUrl(const AssistantConfig& config){const std::string suffix=config.protocol==AssistantProtocol::Responses?"/responses":config.protocol==AssistantProtocol::Messages?"/messages":"/chat/completions";require(config.endpoint.size()>suffix.size()&&config.endpoint.compare(config.endpoint.size()-suffix.size(),suffix.size(),suffix)==0,"Provider endpoint must end with the selected protocol path.");return config.endpoint.substr(0,config.endpoint.size()-suffix.size());}
}
std::vector<AssistantProviderPreset> assistantProviderPresets(){return {
    {"openai","OpenAI API","https://api.openai.com/v1/responses","OPENAI_API_KEY",AssistantProtocol::Responses},
    {"claude","Claude API","https://api.anthropic.com/v1/messages","ANTHROPIC_API_KEY",AssistantProtocol::Messages},
    {"claude-chat","Claude OpenAI compatibility","https://api.anthropic.com/v1/chat/completions","ANTHROPIC_API_KEY",AssistantProtocol::ChatCompletions},
    {"grok","Grok / xAI API","https://api.x.ai/v1/responses","XAI_API_KEY",AssistantProtocol::Responses},
    {"ollama","Ollama local","http://127.0.0.1:11434/v1/chat/completions","",AssistantProtocol::ChatCompletions},
    {"opencode","OpenCode Zen","https://opencode.ai/zen/v1/chat/completions","OPENCODE_API_KEY",AssistantProtocol::ChatCompletions},
    {"custom","Custom compatible endpoint","https://api.openai.com/v1/responses","OPENAI_API_KEY",AssistantProtocol::Responses}};}
AssistantConfig assistantProviderPreset(const std::string& id){for(const auto& preset:assistantProviderPresets())if(preset.id==id){AssistantConfig out;out.provider=preset.id;out.protocol=preset.protocol;out.endpoint=preset.endpoint;out.apiKeyEnv=preset.apiKeyEnv;return out;}throw std::runtime_error("Unknown assistant provider preset.");}
std::string assistantProtocolName(AssistantProtocol protocol){switch(protocol){case AssistantProtocol::Responses:return "responses";case AssistantProtocol::ChatCompletions:return "chat-completions";case AssistantProtocol::Messages:return "messages";}throw std::runtime_error("Unknown assistant protocol.");}
AssistantProtocol assistantProtocolFromName(const std::string& name){if(name=="responses")return AssistantProtocol::Responses;if(name=="chat-completions"||name=="chat"||name=="chat_completions")return AssistantProtocol::ChatCompletions;if(name=="messages")return AssistantProtocol::Messages;throw std::runtime_error("Unknown assistant protocol; choose responses, chat-completions or messages.");}
V assistantAdaptRequest(const AssistantConfig& config,const V& canonicalRequest){
    if(config.protocol==AssistantProtocol::Responses){V out=canonicalRequest;
        V input=V::makeArr();for(const auto& item:need(out,"input").arr){
            if(text(item,"type")=="reasoning"){
                if(item.find("ark_protocol")){if(!sameReplayRoute(config,item)||text(item,"ark_protocol")!="responses")continue;V wire=item;for(const char* key:{"ark_provider","ark_protocol","ark_endpoint","ark_model","ark_auth_mode"})erase(wire,key);input.arr.push_back(wire);}
                else if(config.provider=="openai"&&config.endpoint=="https://api.openai.com/v1/responses")input.arr.push_back(item);
            }else{if(const auto* content=item.find("content");content&&content->t==V::Arr)for(const auto& part:content->arr)if(text(part,"type")=="input_image")assistant_vision::dataUrl(need(part,"image_url"));input.arr.push_back(item);}
        }out.set("input",input);
        if(config.authMode!=AssistantAuthMode::ChatGptPlan&&config.provider!="openai")erase(out,"include");return out;
    }
    require(config.authMode!=AssistantAuthMode::ChatGptPlan,"ChatGPT plan transport requires Responses.");V out=V::makeObj();out.set("model",V::makeStr(config.model));out.set("stream",V::makeBool(false));
    auto messages=chatMessages(config,need(canonicalRequest,"input"),text(canonicalRequest,"instructions"));V tools=V::makeArr();
    if(config.protocol==AssistantProtocol::ChatCompletions){
        out.set("messages",messages);out.set("parallel_tool_calls",V::makeBool(false));
        if(!config.reasoningEffort.empty())out.set("reasoning_effort",V::makeStr(config.reasoningEffort));
        for(const auto& function:need(canonicalRequest,"tools").arr){
            V tool=V::makeObj(),fn=function;erase(fn,"type");
            // An object schema may omit properties. Ollama's GGUF tool renderer
            // requires the explicit map; keep its additionalProperties policy.
            if(config.provider=="ollama")if(const auto* source=fn.find("parameters");source&&text(*source,"type")=="object"&&!source->find("properties")){
                V parameters=*source;parameters.set("properties",V::makeObj());fn.set("parameters",std::move(parameters));
            }
            tool.set("type",V::makeStr("function"));tool.set("function",fn);tools.arr.push_back(tool);
        }out.set("tools",tools);return out;
    }
    out.set("max_tokens",V::makeNum(config.maxOutputTokens));V system=V::makeArr(),wire=V::makeArr();
    auto append=[&](const std::string& role,V content){if(!wire.arr.empty()&&text(wire.arr.back(),"role")==role){V merged=*wire.arr.back().find("content");for(auto& part:content.arr)merged.arr.push_back(std::move(part));wire.arr.back().set("content",merged);}else{V row=V::makeObj();row.set("role",V::makeStr(role));row.set("content",content);wire.arr.push_back(row);}};
    for(const auto& row:messages.arr){const auto role=text(row,"role");if(role=="system"){system.arr.push_back(block("text",text(row,"content")));continue;}V content=V::makeArr();
        if(role=="tool"){V result=V::makeObj();result.set("type",V::makeStr("tool_result"));result.set("tool_use_id",need(row,"tool_call_id"));result.set("content",need(row,"content"));content.arr.push_back(result);append("user",content);continue;}
        if(const auto* reasoning=row.find("ark_messages_reasoning"))for(const auto& part:reasoning->arr)content.arr.push_back(part);
        if(const auto* parts=row.find("content");parts&&parts->t==V::Arr){for(const auto& part:parts->arr){if(text(part,"type")=="image_url"){
                    const auto image=assistant_vision::dataUrl(need(need(part,"image_url"),"url"));V visual=V::makeObj(),source=V::makeObj();visual.set("type",V::makeStr("image"));source.set("type",V::makeStr("base64"));source.set("media_type",V::makeStr(image.first));source.set("data",V::makeStr(image.second));visual.set("source",source);content.arr.push_back(visual);
                }else content.arr.push_back(block("text",text(part,"text")));}}
        else if(!text(row,"content").empty())content.arr.push_back(block("text",text(row,"content")));
        if(const auto* calls=row.find("tool_calls"))for(const auto& call:calls->arr){V use=V::makeObj(),args;const auto& fn=need(call,"function");require(arkjson::parse(text(fn,"arguments"),args)&&args.t==V::Obj,"Messages tool arguments must be a complete JSON object.");use.set("type",V::makeStr("tool_use"));use.set("id",need(call,"id"));use.set("name",need(fn,"name"));use.set("input",args);content.arr.push_back(use);}
        require(!content.arr.empty(),"Messages history has an empty content entry.");append(role,content);
    }
    for(const auto& function:need(canonicalRequest,"tools").arr){V tool=V::makeObj();tool.set("name",need(function,"name"));tool.set("description",need(function,"description"));tool.set("input_schema",need(function,"parameters"));tools.arr.push_back(tool);}out.set("tools",tools);out.set("system",system);out.set("messages",wire);return out;
}
V assistantNormalizeResponse(const AssistantConfig& config,const V& source){
    if(config.protocol==AssistantProtocol::Responses){V out=source;if(const auto* original=source.find("output");original&&original->t==V::Arr){V output=*original;for(auto& item:output.arr)if(text(item,"type")=="reasoning")tagReplay(item,config);out.set("output",output);}out.set("ark_provider_outcome",providerOutcome(config,source,out,"responses_passthrough"));return out;}
    V out=V::makeObj(),output=V::makeArr();out.set("status",V::makeStr("incomplete"));
    out.set("usage",normalizedUsage(source.find("usage"),config.protocol==AssistantProtocol::Messages));
    auto finishOutcome=[&](const char* reason){out.set("ark_provider_outcome",providerOutcome(config,source,out,reason));return out;};
    // Retain usage evidence on an incomplete/invalid response, but no output is
    // accepted for execution until the entire protocol envelope is complete.
    if(source.find("error")&&!source.find("error")->isNull()){out.set("error",V::makeObj());return finishOutcome("provider_error");}
    try{if(config.protocol==AssistantProtocol::ChatCompletions){
        const auto* choices=source.find("choices");require(choices&&choices->t==V::Arr&&choices->arr.size()==1,"Chat completion must contain exactly one choice.");const auto& choice=choices->arr.front();const auto finish=text(choice,"finish_reason");if(finish!="stop"&&finish!="tool_calls"){out.set("output",output);return finishOutcome("incomplete_finish");}
        const auto& reply=need(choice,"message");require(reply.t==V::Obj&&text(reply,"role")=="assistant","Chat response must be an assistant message.");
        if(const auto* reasoning=reply.find("reasoning_content");reasoning&&!reasoning->isNull()){require(reasoning->t==V::Str,"Invalid chat reasoning content.");V retained=V::makeObj();retained.set("type",V::makeStr("reasoning"));retained.set("ark_chat_reasoning",*reasoning);tagReplay(retained,config);output.arr.push_back(retained);}
        if(const auto* content=reply.find("content");content&&!content->isNull()){require(content->t==V::Str,"Chat response content must be text or null.");if(!content->str.empty())output.arr.push_back(normalizedMessage(content->str));}
        if(const auto* refusal=reply.find("refusal");refusal&&!refusal->isNull()){require(refusal->t==V::Str,"Chat refusal must be text.");if(!refusal->str.empty())output.arr.push_back(normalizedMessage(refusal->str));}
        std::set<std::string> ids;std::size_t count=0;
        if(const auto* calls=reply.find("tool_calls");calls&&!calls->isNull()){require(calls->t==V::Arr&&calls->arr.size()<=32,"Invalid chat tool call array.");for(const auto& call:calls->arr){require(text(call,"type")=="function","Unsupported chat tool call type.");const auto& fn=need(call,"function");auto normalized=normalizedCall(text(call,"id"),text(fn,"name"),need(fn,"arguments"));require(ids.insert(text(normalized,"call_id")).second,"Repeated provider tool call ID.");output.arr.push_back(normalized);++count;}}
        require((finish=="tool_calls")==(count>0),"Chat completion finish reason contradicts its tool calls.");
    }else{
        const auto stop=text(source,"stop_reason");if(stop!="end_turn"&&stop!="tool_use"&&stop!="stop_sequence"){out.set("output",output);return finishOutcome("incomplete_finish");}
        require(text(source,"type")=="message"&&text(source,"role")=="assistant","Messages response must be an assistant message.");const auto& content=need(source,"content");require(content.t==V::Arr,"Messages response content must be an array.");std::set<std::string> ids;std::size_t count=0;
        for(const auto& part:content.arr){const auto kind=text(part,"type");if(kind=="text"){require(need(part,"text").t==V::Str,"Messages text must be a string.");if(!text(part,"text").empty())output.arr.push_back(normalizedMessage(text(part,"text")));}
            else if(kind=="thinking"||kind=="redacted_thinking"){if(kind=="thinking")require(need(part,"thinking").t==V::Str&&need(part,"signature").t==V::Str,"Messages thinking requires a signature.");else require(need(part,"data").t==V::Str,"Invalid redacted thinking.");V retained=V::makeObj(),blocks=V::makeArr();blocks.arr.push_back(part);retained.set("type",V::makeStr("reasoning"));retained.set("ark_messages_content",blocks);tagReplay(retained,config);output.arr.push_back(retained);}
            else if(kind=="tool_use"){const auto& input=need(part,"input");require(input.t==V::Obj,"Messages tool input must be a JSON object.");auto normalized=normalizedCall(text(part,"id"),text(part,"name"),V::makeStr(input.dump()));require(ids.insert(text(normalized,"call_id")).second,"Repeated Messages tool ID.");output.arr.push_back(normalized);++count;}
            else throw std::runtime_error("Unsupported Messages response content block.");}
        require((stop=="tool_use")==(count>0),"Messages stop reason contradicts its tool uses.");
    }
    out.set("status",V::makeStr("completed"));out.set("output",output);return finishOutcome("terminal_envelope_accepted");
    }catch(const std::exception&){out.set("status",V::makeStr("incomplete"));out.set("output",V::makeArr());return finishOutcome("invalid_envelope");}
}
std::string assistantExternalMcpConfig(const std::string& target,const std::string& executable,const std::string& endpoint,const AssistantConfig& provider){
    const bool absoluteWindows=executable.size()>3&&std::isalpha((unsigned char)executable[0])&&executable[1]==':'&&(executable[2]=='\\'||executable[2]=='/');const bool unc=executable.rfind("\\\\",0)==0;
    require((absoluteWindows||unc||executable.rfind("/",0)==0)&&executable.size()<=4096&&executable.find_first_of("\r\n\t") == std::string::npos,"Export requires the actual absolute MCP executable path.");
    require(!endpoint.empty()&&endpoint.size()<=160&&std::all_of(endpoint.begin(),endpoint.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),"Export requires the active Studio pipe name.");
    V args=V::makeArr();args.arr.push_back(V::makeStr("--connect"));args.arr.push_back(V::makeStr(endpoint));
    if(target=="codex"||target=="grok-build"){
        // Grok expands ${VAR} in MCP commands even inside a quoted TOML string.
        // Reject that path rather than exporting a different executable silently.
        require(target!="grok-build"||executable.find("${")==std::string::npos,"Grok Build expands environment placeholders; the MCP executable path must not contain ${.");
        return "[mcp_servers.ark]\ncommand = "+V::makeStr(executable).dump()+"\nargs = "+args.dump()+"\n"+(target=="grok-build"?"enabled = true\n":"");
    }
    V root=V::makeObj(),servers=V::makeObj(),server=V::makeObj();
    if(target=="claude-code"||target=="claude-desktop"){if(target=="claude-code")server.set("type",V::makeStr("stdio"));server.set("command",V::makeStr(executable));server.set("args",args);servers.set("ark",server);root.set("mcpServers",servers);return root.dump();}
    require(target=="opencode","Unknown external MCP client.");V command=V::makeArr();command.arr.push_back(V::makeStr(executable));for(const auto& arg:args.arr)command.arr.push_back(arg);server.set("type",V::makeStr("local"));server.set("command",command);server.set("enabled",V::makeBool(true));servers.set("ark",server);root.set("$schema",V::makeStr("https://opencode.ai/config.json"));root.set("mcp",servers);
    if(!provider.model.empty()&&provider.authMode==AssistantAuthMode::ApiKey){
        validateAssistantConfig(provider);V providers=V::makeObj(),entry=V::makeObj(),options=V::makeObj(),models=V::makeObj(),model=V::makeObj();
        const std::string id="ark-"+provider.provider;entry.set("npm",V::makeStr(provider.protocol==AssistantProtocol::Messages?"@ai-sdk/anthropic":provider.protocol==AssistantProtocol::Responses?"@ai-sdk/openai":"@ai-sdk/openai-compatible"));entry.set("name",V::makeStr(provider.provider));options.set("baseURL",V::makeStr(baseUrl(provider)));if(!provider.apiKeyEnv.empty())options.set("apiKey",V::makeStr("{env:"+provider.apiKeyEnv+"}"));entry.set("options",options);model.set("name",V::makeStr(provider.model));models.set(provider.model,model);entry.set("models",models);providers.set(id,entry);root.set("provider",providers);root.set("model",V::makeStr(id+"/"+provider.model));
    }
    return root.dump();
}
}
