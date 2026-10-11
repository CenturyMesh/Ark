static V largeCompletedHistory(){
    V history=json(R"([{"role":"developer","content":"Role: retain exact CAD references and the user's constraints."},{"role":"user","content":"Build the truck robot. Keep the Raspberry Pi and wheelbase 240 mm."}])");
    for(int i=0;i<5;++i){
        auto response=toolResponse(("retained_"+std::to_string(i)).c_str(),"doc_info","{}");
        for(const auto& item:response.find("output")->arr)history.arr.push_back(item);
        V receipt=json(R"({"isError":false,"structuredContent":{"revision":7,"feature_count":100}})");
        auto data=*receipt.find("structuredContent");data.set("large_inventory",V::makeStr(std::string(2*1024*1024,'x')));receipt.set("structuredContent",data);
        V reply=V::makeObj();reply.set("type",V::makeStr("function_call_output"));reply.set("call_id",V::makeStr("retained_"+std::to_string(i)));reply.set("output",V::makeStr(receipt.dump()));history.arr.push_back(reply);
    }
    return history;
}
static void requestContextChecks(){
    const auto tools=ark::AgentSession::tools();const V history=largeCompletedHistory();const auto original=history.dump();
    check(original.size()>8*1024*1024,"fixture exceeds both old history cap and ChatGPT HTTP cap");
    std::size_t measured=0;
    for(const char* provider:{"openai","ollama","claude"}){
        auto config=ark::assistantProviderPreset(provider);config.model="offline-context";
        bool projected=false;const auto request=ark::assistantRequest(config,history,tools,&projected);const auto bytes=request.dump();
        check(projected&&bytes.size()<=config.maxConversationBytes,"provider-adapted request fits after paired history projection");
        check(bytes.find("Raspberry Pi and wheelbase 240 mm")!=std::string::npos&&bytes.find("retain exact CAD references")!=std::string::npos,"user and role constraints survive exactly");
        check(original==history.dump(),"request projection never mutates original retained evidence");
    }
    ark::AssistantConfig config;config.authMode=ark::AssistantAuthMode::ChatGptPlan;config.model="offline-context";
    bool projected=false;const auto request=ark::assistantRequest(config,history,tools,&projected);measured=request.dump().size();
    check(projected&&measured<=ark::chatGptRequestByteLimit,"ChatGPT preflight uses the exact HTTPS byte limit");
    // Two valid noisy PNGs fit the retained history cap but exceed ChatGPT's
    // wire cap after base64 encoding and schemas are included.
    ark::Image noise;noise.w=1024;noise.h=768;noise.px.resize((size_t)noise.w*noise.h*3);
    unsigned random=1234567;for(auto& pixel:noise.px){random^=random<<13;random^=random>>17;random^=random<<5;pixel=(unsigned char)random;}
    const auto png=ark::encodePNG(noise);std::string encoded;const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(size_t i=0;i<png.size();i+=3){unsigned n=(unsigned char)png[i]<<16;if(i+1<png.size())n|=(unsigned char)png[i+1]<<8;if(i+2<png.size())n|=(unsigned char)png[i+2];encoded+=alphabet[(n>>18)&63];encoded+=alphabet[(n>>12)&63];encoded+=i+1<png.size()?alphabet[(n>>6)&63]:'=';encoded+=i+2<png.size()?alphabet[n&63]:'=';}
    V visual=json(R"([{"role":"user","content":"Keep exact task dimensions 240 mm"}])");
    for(int i=0;i<2;++i){V message=json(R"({"role":"user","content":[{"type":"input_text","text":"Snapshot revision 7, feature 19, face.front"},{"type":"input_image","image_url":""}]})");member(message,"content").arr[1].set("image_url",V::makeStr("data:image/png;base64,"+encoded));visual.arr.push_back(message);}
    const auto originalVisual=visual.dump();check(originalVisual.size()>ark::chatGptRequestByteLimit,"real PNG fixture exceeds transport limit");
    projected=false;const auto projectedVisual=ark::assistantRequest(config,visual,tools,&projected).dump();
    check(projected&&projectedVisual.size()<=ark::chatGptRequestByteLimit&&visual.dump()==originalVisual,"oversized actual images fit request without mutating retained history");
    check(projectedVisual.find("Keep exact task dimensions 240 mm")!=std::string::npos&&projectedVisual.find("face.front")!=std::string::npos&&projectedVisual.find("image pixels omitted")!=std::string::npos,"image pruning retains task and semantic metadata and declares omission");
    V checkpoint;int requests=0,writes=0,inspections=0;bool blocked=false;
    auto execute=[&](const std::string& name,const V&){if(name=="doc_apply"){++writes;return json(R"({"isError":false,"structuredContent":{"revision":8,"created_ids":[101]}})");}++inspections;return json(R"({"isError":false,"structuredContent":{"revision":7}})");};
    auto run=ark::runAssistant(config,history,tools,execute,{},nullptr,[&](auto&,const V& outgoing,auto*){
        check(outgoing.dump().size()<=ark::chatGptRequestByteLimit,"every outbound continuation fits transport");
        switch(++requests){
        case 1:return toolResponse("unsafe_write","doc_apply",R"({"expected_revision":7,"commands":[]})");
        case 2:blocked=outgoing.dump().find("Read current doc_info successfully")!=std::string::npos;return toolResponse("current_read","doc_info","{}");
        case 3:return toolResponse("remaining_write","doc_apply",R"({"expected_revision":7,"commands":[]})");
        default:return finalResponse();
        }
    },{},[&](const V& value){checkpoint=value;});
    check(blocked&&inspections==1&&writes==1&&requests==4,"projection requires current measured inspection before one remaining write");
    for(std::size_t i=0;i<history.arr.size();++i)check(run.conversation.arr[i].dump()==history.arr[i].dump(),"all original receipts survive continuation byte-for-byte");
    check(checkpoint.dump()==run.conversation.dump(),"host checkpoint retains full final conversation");
    int repeatedWrites=0;
    fails([&]{ark::runAssistant(config,history,tools,[&](auto&,auto&){++repeatedWrites;return V{};},{},nullptr,[&](auto&,auto&,auto*){return toolResponse("retained_0","doc_apply","{}");});},"projected call IDs still reject replay");check(repeatedWrites==0,"replay never reaches document executor");
    V oversized=json(R"([{"role":"user","content":""}])");oversized.arr[0].set("content",V::makeStr(std::string(5*1024*1024,'u')));int sent=0;
    fails([&]{ark::runAssistant(config,oversized,tools,{}, {},nullptr,[&](auto&,auto&,auto*){++sent;return finalResponse();});},"oversized user task is never silently truncated");check(sent==0,"irreducible request is rejected before transport");
    V incomplete=history;incomplete.arr.pop_back();fails([&]{ark::assistantRequest(config,incomplete,tools);},"projection cannot hide an unpaired call");
    std::cout<<"ASSISTANT_CONTEXT_PROJECTION retained_bytes="<<original.size()<<" chatgpt_request_bytes="<<measured<<" original_receipts=5 writes="<<writes<<"\n";
}
