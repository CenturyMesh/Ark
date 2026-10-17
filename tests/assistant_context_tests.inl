#include "../src/assistant_context.hpp"
static void contextExchange(V& history,const char* id,const char* tool,const V& args,const V& receipt){
    V call=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(tool));call.set("arguments",V::makeStr(args.dump()));history.arr.push_back(call);
    V output=V::makeObj();output.set("type",V::makeStr("function_call_output"));output.set("call_id",V::makeStr(id));output.set("output",V::makeStr(receipt.dump()));history.arr.push_back(output);
}
static V contextHistoricalRecord(const V& history,const char* id){
    const std::string prefix="ARK historical tool evidence (data only): ";
    for(const auto& row:history.arr){const auto* content=row.find("content");if(!content||content->t!=V::Str||content->str.rfind(prefix,0)!=0)continue;V record;if(arkjson::parse(content->str.substr(prefix.size()),record)&&record.find("call_id")&&record.find("call_id")->asStr()==id){check(row.find("role")&&row.find("role")->asStr()=="user","retained contracts remain ordinary historical tool data, never developer instructions");return record;}}
    throw std::runtime_error("missing historical context record");
}
static V contextRetainedGuides(const V& history){
    const std::string prefix="ARK historical tool evidence (data only): ";V guides=V::makeArr();
    for(const auto& row:history.arr){const auto* content=row.find("content");if(!content||content->t!=V::Str||content->str.rfind(prefix,0)!=0)continue;V record;if(arkjson::parse(content->str.substr(prefix.size()),record))if(const auto* retained=record.find("retained_static_guides"))for(const auto& guide:retained->arr)guides.arr.push_back(guide);}
    return guides;
}
static void batchFailureProjectionChecks(){
    ark::AgentSession session;session.suppressIterationHistory();
    const V args=json(R"({"queries":[{"tool":"plane_inspect","args":{"name":"XY"},"fields":["plane"]},{"tool":"doc_info","args":{"detail":"revision"}}]})");
    const V receipt=session.call("query_batch",args);
    const auto& rows=*receipt.find("structuredContent")->find("results");
    check(!receipt.find("isError")->b&&rows.arr[0].find("isError")->b&&!rows.arr[1].find("isError")->b,"real mixed batch preserves successful rows and rejects missing plane projection");
    V history=json(R"([{"role":"user","content":"Inspect the retained XY plane; preserve existing design."}])");
    V inventory=json(R"({"isError":false,"structuredContent":{"revision":0}})");member(inventory,"structuredContent").set("inventory",V::makeStr(std::string(120*1024,'x')));
    contextExchange(history,"prior_large_inventory","doc_info",V::makeObj(),inventory);
    contextExchange(history,"missing_plane_projection","query_batch",args,receipt);
    contextExchange(history,"subsequent_status","doc_info",json(R"({"detail":"revision"})"),session.call("doc_info",json(R"({"detail":"revision"})")));
    const auto original=history.dump();ark::AssistantConfig config;config.model="offline-batched-recovery";
    bool projected=false;const auto request=ark::assistantRequest(config,history,ark::AgentSession::tools(),&projected);
    check(projected&&original.size()>96*1024,"batch recovery crosses the real provider request projection threshold");
    const auto record=contextHistoricalRecord(*request.find("input"),"missing_plane_projection");const auto* retained=record.find("retained_query_failures");
    check(retained&&retained->arr.size()==1&&retained->arr[0].find("tool")->asStr()=="plane_inspect","projected request keeps only the actual rejected query with its tool and index");
    const auto& evidence=*retained->arr[0].find("receipt");
    check(evidence.dump()==rows.arr[0].find("structuredContent")->dump()&&evidence.find("observed_result")->find("world_column_major")&&evidence.find("recovery")->find("args")->find("name")->asStr()=="XY","exact observed frame, available fields and executable recovery survive a subsequent read");
    check(history.dump()==original&&session.revision==0,"projection preserves original history and document revision");
    int requests=0,reads=0;auto run=ark::runAssistant(config,history,ark::AgentSession::tools(),[&](const auto& tool,const V& values){++reads;check(tool=="plane_inspect"&&values.dump()==R"({"name":"XY"})","recovery executes only the selected actual query");return session.call(tool,values);},{},nullptr,[&](const auto&,const V& outgoing,auto*){
        ++requests;const auto prior=contextHistoricalRecord(*outgoing.find("input"),"missing_plane_projection");
        if(requests==1){check(prior.find("retained_query_failures"),"real provider seam receives retained recovery evidence");return toolResponse("fresh_plane_recovery","plane_inspect",R"({"name":"XY"})");}
        check(!prior.find("retained_query_failures"),"successful direct same-query read retires its retained historical recovery packet");return finalResponse();
    });
    check(requests==2&&reads==1&&session.revision==0,"one exact read recovers without repeated batch work or any paid transport");
    for(size_t i=0;i<history.arr.size();++i)check(run.conversation.arr[i].dump()==history.arr[i].dump(),"recovery continuation retains every original host receipt");
    V mismatched=receipt;member(member(mismatched,"structuredContent"),"results").arr[0].set("tool",V::makeStr("native_inspect"));V invalidHistory=history;
    member(invalidHistory.arr[4],"output")=V::makeStr(mismatched.dump());
    auto invalid=ark::assistantRequest(config,invalidHistory,ark::AgentSession::tools());
    check(!contextHistoricalRecord(*invalid.find("input"),"missing_plane_projection").find("retained_query_failures"),"mismatched batch tool metadata cannot supply retained recovery evidence");
    check(ark::assistant_context::readEvidenceKey("math_eval",json(R"({"op":"norm","a":[1,2,3]})"))==ark::assistant_context::readEvidenceKey("math_eval",json(R"({"a":[1,2,3],"op":"norm"})")),"read evidence identity ignores JSON object ordering without changing array order");
    const V requested=json(R"({"queries":[{"tool":"doc_info","args":{},"fields":["invented_field"]}]})"),executed=json(R"({"queries":[{"tool":"doc_info","args":{"detail":"revision"},"fields":["invented_field"]}]})");
    V normalized=history;contextExchange(normalized,"host_explicit_detail","query_batch",requested,session.call("query_batch",executed));contextExchange(normalized,"later_normalized_status","doc_info",json(R"({"detail":"design"})"),session.call("doc_info",json(R"({"detail":"design"})")));
    auto normalizedRequest=ark::assistantRequest(config,normalized,ark::AgentSession::tools());const auto normalizedRecord=contextHistoricalRecord(*normalizedRequest.find("input"),"host_explicit_detail");
    const auto* normalizedFailures=normalizedRecord.find("retained_query_failures");check(normalizedFailures&&normalizedFailures->arr.size()==1&&normalizedFailures->arr[0].find("executed_arguments")->dump()==R"({"detail":"revision"})","team host's explicit compact detail preserves the actual query recovery scope");
    contextExchange(normalized,"explicit_normalized_recovery","doc_info",json(R"({"detail":"revision"})"),session.call("doc_info",json(R"({"detail":"revision"})")));
    normalizedRequest=ark::assistantRequest(config,normalized,ark::AgentSession::tools());check(!contextHistoricalRecord(*normalizedRequest.find("input"),"host_explicit_detail").find("retained_query_failures"),"successful executed-detail recovery retires its corresponding original request packet");
    check(!ark::assistant_context::declaredReadArgumentsMatch(json(R"({"id":1})"),json(R"({"id":2,"detail":"properties"})")),"declared compact detail cannot disguise another requested object");
    V reverse=history;contextExchange(reverse,"explicit_revision_failure","query_batch",executed,session.call("query_batch",executed));contextExchange(reverse,"single_default_read","doc_info",V::makeObj(),session.call("doc_info",V::makeObj()));
    auto reverseRequest=ark::assistantRequest(config,reverse,ark::AgentSession::tools());check(contextHistoricalRecord(*reverseRequest.find("input"),"explicit_revision_failure").find("retained_query_failures"),"standalone default summary does not pretend to be an explicit revision read");
    contextExchange(reverse,"team_default_read","doc_info",V::makeObj(),session.call("doc_info",json(R"({"detail":"revision"})")));
    reverseRequest=ark::assistantRequest(config,reverse,ark::AgentSession::tools());check(!contextHistoricalRecord(*reverseRequest.find("input"),"explicit_revision_failure").find("retained_query_failures"),"observed revision_status resolves a Team-normalized omitted detail without guessing standalone defaults");
    const V guideQueries=json(R"({"queries":[{"tool":"agent_onboard","args":{"topic":"solids","detail":"guide"}}]})");
    const V guideBatch=session.call("query_batch",guideQueries);
    for(int mismatch=0;mismatch<3;++mismatch){V changed=guideBatch;auto& row=member(member(changed,"structuredContent"),"results").arr[0];if(mismatch==0)row.set("tool",V::makeStr("doc_info"));if(mismatch==1)row.set("query_index",V::makeNum(1));if(mismatch==2)member(row,"args").set("topic",V::makeStr("sketch"));V candidate=history;contextExchange(candidate,"mismatched_guide_metadata","query_batch",guideQueries,changed);contextExchange(candidate,"after_mismatched_guide","doc_info",V::makeObj(),session.call("doc_info",V::makeObj()));auto projectedGuide=ark::assistantRequest(config,candidate,ark::AgentSession::tools());const auto retainedGuide=contextHistoricalRecord(*projectedGuide.find("input"),"mismatched_guide_metadata");check(!retainedGuide.find("retained_static_guides")&&!retainedGuide.find("retained_guide_fragments"),"mismatched guide row metadata cannot supply a retained contract");}
}
static void staticContractProjectionChecks(){
    batchFailureProjectionChecks();
    ark::AgentSession session;
    const V solidArgs=json(R"({"topic":"solids","detail":"guide"})"),sketchArgs=json(R"({"topic":"sketch","detail":"guide"})"),surfaceArgs=json(R"({"topic":"surfacing","detail":"guide"})");
    V solid=session.call("agent_onboard",solidArgs),sketch=session.call("agent_onboard",sketchArgs),surface=session.call("agent_onboard",surfaceArgs);
    check(ark::assistant_context::staticGuide(solidArgs,solid)&&ark::assistant_context::staticGuide(sketchArgs,sketch),"real static topic receipts carry explicit document exclusion and build identity");
    V unidentified=solid;member(unidentified,"structuredContent").set("version",V::makeStr(""));check(!ark::assistant_context::staticGuide(solidArgs,unidentified),"unidentified build cannot supply a reusable static contract");
    member(solid,"structuredContent").set("command_reference",V::makeStr("OBSOLETE_SOLID_CONTRACT"));
    V latestSolid=solid;member(latestSolid,"structuredContent").set("command_reference",V::makeStr("CURRENT_SOLID_CONTRACT"));
    member(latestSolid,"structuredContent").set("version",V::makeStr("0.9.999"));member(latestSolid,"structuredContent").set("iteration_code",V::makeStr("999A"));
    V history=json(R"([{"role":"developer","content":"Keep the assigned design role and atomic expected_revision guards."},{"role":"user","content":"Create the editable mower concept; wheelbase 240 mm. Preserve all existing work."}])");
    contextExchange(history,"old_solid","agent_onboard",solidArgs,solid);
    contextExchange(history,"old_surface","agent_onboard",surfaceArgs,surface);
    contextExchange(history,"latest_surface","agent_onboard",json(R"({"topic":"surfacing"})"),surface);
    V inventory=json(R"({"isError":false,"structuredContent":{"revision":7,"feature_count":0}})");member(inventory,"structuredContent").set("large_inventory",V::makeStr("INVENTORY_MUST_NOT_BE_RETAINED"+std::string(128*1024,'x')));
    V batch=json(R"({"isError":false,"structuredContent":{"revision":7,"results":[]}})");
    member(member(batch,"structuredContent"),"results").arr={inventory,sketch,latestSolid,solid};
    const V queries=json(R"({"queries":[{"tool":"doc_info","args":{"detail":"design"}},{"tool":"agent_onboard","args":{"topic":"sketch"}},{"tool":"agent_onboard","args":{"topic":"solids","detail":"guide"}},{"tool":"doc_info","args":{}}]})");
    contextExchange(history,"batch_contracts","query_batch",queries,batch);
    V team=json(R"({"isError":false,"structuredContent":{"response_scope":"team_progress","changed":true,"evidence_state":"changed","evidence_version":2,"document_revision_observed":true,"document_revision":7,"committed_steps":0,"roles":[{"id":"engineer","can_write":true,"task_state":"in_progress","task_summary":"Keep the requested wheelbase and author a coherent checkpoint."}],"next_action":{"action":"OBSOLETE_TEAM_ACTION","instruction":"Old observation."}}})");
    contextExchange(history,"old_team_progress","team_context",V::makeObj(),team);
    member(team,"structuredContent").set("response_scope",V::makeStr("team_diagnostics"));member(team,"structuredContent").set("evidence_version",V::makeNum(3));
    member(member(team,"structuredContent"),"next_action").set("action",V::makeStr("author_next_editable_checkpoint"));
    member(member(team,"structuredContent"),"roles").arr[0].set("request_context",json(R"({"accounting":"DIAGNOSTICS_MUST_NOT_BE_RETAINED"})"));
    member(member(team,"structuredContent"),"roles").arr[0].set("token_evidence",V::makeStr("DIAGNOSTICS_MUST_NOT_BE_RETAINED"));
    contextExchange(history,"latest_team_progress","team_context",json(R"({"detail":"diagnostics"})"),*team.find("structuredContent"));
    V failedTeam=team;failedTeam.set("isError",V::makeBool(true));member(member(failedTeam,"structuredContent"),"next_action").set("action",V::makeStr("FAILED_TEAM_ACTION"));
    contextExchange(history,"failed_team_progress","team_context",V::makeObj(),failedTeam);
    // Errors, full/live onboarding, mismatched topics and unrelated tools cannot
    // replace a successful source-owned static guide just by resembling one.
    V error=solid;error.set("isError",V::makeBool(true));error.set("error",V::makeStr("Guide request rejected; no replacement contract."));
    contextExchange(history,"failed_contract","agent_onboard",solidArgs,error);
    contextExchange(history,"wrong_topic","agent_onboard",sketchArgs,solid);
    contextExchange(history,"unrelated_guide_shape","doc_info",solidArgs,solid);
    V liveGuide=solid;member(liveGuide,"structuredContent").set("document_state_included",V::makeBool(true));member(liveGuide,"structuredContent").set("document",V::makeObj());
    contextExchange(history,"live_contract","agent_onboard",solidArgs,liveGuide);
    contextExchange(history,"explicit_full_contract","agent_onboard",json(R"({"topic":"solids","detail":"full"})"),solid);
    V mismatched=batch;member(member(mismatched,"structuredContent"),"results").arr.pop_back();
    contextExchange(history,"mismatched_batch","query_batch",queries,mismatched);
    const V fragmentQueries=json(R"({"queries":[{"tool":"agent_onboard","args":{"topic":"routing"},"fields":["rules","tools"]}]})");
    V oldFragment=json(R"({"isError":false,"structuredContent":{"results":[{"isError":false,"structuredContent":{"rules":["OBSOLETE_FRAGMENT_RULE"],"tools":["doc_apply"]}}]}})");
    contextExchange(history,"old_guide_fragment","query_batch",fragmentQueries,oldFragment);
    const V newestFragment=json(R"({"isError":false,"structuredContent":{"results":[{"isError":false,"structuredContent":{"rules":["CURRENT_FRAGMENT_RULE: retain actual path, endpoint and profile fields without rediscovering a loaded routing contract."]}}]}})");
    contextExchange(history,"new_guide_fragment","query_batch",json(R"({"queries":[{"tool":"agent_onboard","args":{"topic":"routing","detail":"guide"},"fields":["rules"]}]})"),newestFragment);
    V meta=V::makeObj();for(const char* key:{"product","version","iteration_code","topic","detail","document_state_included"})meta.set(key,*latestSolid.find("structuredContent")->find(key));
    V metaRow=V::makeObj(),metaRows=V::makeArr(),metaPayload=V::makeObj(),metaBatch=V::makeObj();metaRow.set("isError",V::makeBool(false));metaRow.set("structuredContent",meta);metaRows.arr.push_back(metaRow);metaPayload.set("results",metaRows);metaBatch.set("isError",V::makeBool(false));metaBatch.set("structuredContent",metaPayload);
    contextExchange(history,"metadata_only_fragment","query_batch",json(R"({"queries":[{"tool":"agent_onboard","args":{"topic":"solids","detail":"guide"},"fields":["product","version","iteration_code","topic","detail","document_state_included"]}]})"),metaBatch);
    contextExchange(history,"after_guides_revision","doc_info",json(R"({"detail":"revision"})"),json(R"({"isError":false,"structuredContent":{"revision":7,"feature_count":0}})"));
    contextExchange(history,"after_guides_measurement","math_eval",json(R"({"expressions":["240/2"]})"),json(R"({"isError":false,"structuredContent":{"value":120}})"));
    const auto original=history.dump();check(original.size()>96*1024,"mixed guide fixture crosses proactive history projection threshold");
    auto verify=[&](const V& projected){
        const V retained=contextRetainedGuides(projected);check(retained.arr.size()==3,"latest successful guide per topic is retained once, including standalone and batch reads");
        bool hasSolid=false,hasSketch=false,hasSurface=false;
        for(const auto& guide:retained.arr){const auto& data=*guide.find("receipt");const auto topic=data.find("topic")->asStr();
            if(topic=="solids"){hasSolid=data.dump()==latestSolid.find("structuredContent")->dump();check(guide.find("query_index")&&guide.find("query_index")->num==2,"batch guide retains its exact matching query index");}
            if(topic=="sketch"){hasSketch=data.dump()==sketch.find("structuredContent")->dump();check(data.find("example_doc_apply")&&data.find("example_doc_apply")->find("expected_revision"),"historical example revision is preserved faithfully, not rewritten to current state");}
            if(topic=="surfacing")hasSurface=data.dump()==surface.find("structuredContent")->dump();
            check(guide.find("arguments")&&data.find("detail")->asStr()=="guide"&&guide.find("evidence_scope")->asStr().find("historical")!=std::string::npos,"retained guide is bound to actual arguments, an explicit static receipt and historical scope");
        }
        check(hasSolid&&hasSketch&&hasSurface,"complete rules, examples and newest build/topic command reference survive without truncation");
        const auto oldPartial=contextHistoricalRecord(projected,"old_guide_fragment"),newPartial=contextHistoricalRecord(projected,"new_guide_fragment"),metadata=contextHistoricalRecord(projected,"metadata_only_fragment");
        check(oldPartial.find("retained_guide_fragments")&&oldPartial.find("retained_guide_fragments")->arr[0].find("receipt")->find("tools")&&!oldPartial.find("retained_guide_fragments")->arr[0].find("receipt")->find("rules"),"partial guide fields deduplicate individually without losing a separately queried field");
        const auto& retainedPart=newPartial.find("retained_guide_fragments")->arr[0];check(retainedPart.find("partial")->b&&retainedPart.find("topic")->str=="routing"&&retainedPart.find("receipt")->dump()==newestFragment.find("structuredContent")->find("results")->arr[0].find("structuredContent")->dump(),"batch field subsets retain exactly the facts actually returned even without build identity fields");
        check(metadata.find("retained_guide_fragments")&&!metadata.find("retained_static_guides")&&projected.dump().find("OBSOLETE_FRAGMENT_RULE")==std::string::npos,"metadata-only projections never replace a full contract or retain obsolete repeated fields");
        const auto direct=contextHistoricalRecord(projected,"latest_surface"),batched=contextHistoricalRecord(projected,"batch_contracts");
        check(direct.find("retained_static_guides")&&batched.find("retained_static_guides")&&!batched.find("receipt")->find("results"),"retaining batch contracts does not retain the batch inventory");
        check(!direct.find("retained_static_guides")->arr[0].find("arguments")->find("detail"),"team implicit guide default preserves actual provider arguments without inventing an explicit detail");
        check(!contextHistoricalRecord(projected,"old_solid").find("retained_static_guides")&&!contextHistoricalRecord(projected,"old_surface").find("retained_static_guides"),"repeated and superseded guides are deduplicated");
        const auto progress=contextHistoricalRecord(projected,"latest_team_progress");const auto* semantic=progress.find("retained_team_progress");
        check(semantic&&semantic->find("next_action")->find("action")->asStr()=="author_next_editable_checkpoint"&&semantic->find("roles")->arr.size()==1&&semantic->find("document_revision")->num==7&&semantic->find("evidence_version")->num==3,"latest relevant team action, roles, evidence and observed revision survive later reads");
        check(!contextHistoricalRecord(projected,"old_team_progress").find("retained_team_progress")&&!semantic->find("roles")->arr[0].find("request_context")&&!semantic->find("roles")->arr[0].find("token_evidence"),"only latest semantic team progress is retained, without diagnostic accounting");
        check(!contextHistoricalRecord(projected,"failed_team_progress").find("retained_team_progress"),"failed team receipt cannot replace the last successful semantic observation");
        check(contextHistoricalRecord(projected,"failed_contract").find("isError")->b&&!contextHistoricalRecord(projected,"failed_contract").find("retained_static_guides"),"failed onboarding remains an error and cannot become a retained contract");
        for(const char* id:{"wrong_topic","unrelated_guide_shape","live_contract","explicit_full_contract","mismatched_batch"})check(!contextHistoricalRecord(projected,id).find("retained_static_guides"),"only matching actual successful static guide calls can supply retained contracts");
    };
    verify(ark::assistant_context::project(history,ark::chatGptRequestByteLimit));
    ark::AssistantConfig config;config.authMode=ark::AssistantAuthMode::ChatGptPlan;config.model="offline-contract-projection";
    bool projected=false;const auto request=ark::assistantRequest(config,history,ark::AgentSession::tools(),&projected);check(projected,"real assistantRequest seam uses proactive projection for mixed guide history");verify(*request.find("input"));
    check(request.dump().find("INVENTORY_MUST_NOT_BE_RETAINED")==std::string::npos,"generic projection omits unrelated large inventories while preserving contracts");
    check(history.dump()==original,"direct and provider request projections leave all original host calls and receipts byte-for-byte unchanged");
    int rounds=0,writes=0,inspections=0;bool sawGuard=false;
    ark::runAssistant(config,history,ark::AgentSession::tools(),[&](const auto& name,const V&){if(name=="doc_apply"){++writes;return json(R"({"isError":false,"structuredContent":{"revision":8}})");}++inspections;return json(R"({"isError":false,"structuredContent":{"revision":7}})");},{},nullptr,[&](const auto&,const V& outgoing,auto*){
        switch(++rounds){case 1:return toolResponse("contract_stale_write","doc_apply",R"({"expected_revision":0,"commands":[]})");case 2:sawGuard=outgoing.dump().find("Read current doc_info successfully")!=std::string::npos;return toolResponse("contract_current_read","doc_info",R"({"detail":"revision"})");case 3:return toolResponse("contract_current_write","doc_apply",R"({"expected_revision":7,"commands":[]})");default:return finalResponse();}
    });
    check(sawGuard&&writes==1&&inspections==1&&rounds==4,"retained historical examples/team revision never bypass fresh observed revision guard");
    V oversized=json(R"([{"role":"user","content":"Keep all actual operation contracts."}])");V hugeGuide=surface;
    member(hugeGuide,"structuredContent").set("command_reference",V::makeStr(std::string(ark::chatGptRequestByteLimit,'k')));
    contextExchange(oversized,"oversized_static_contract","agent_onboard",surfaceArgs,hugeGuide);
    contextExchange(oversized,"after_oversized_contract","doc_info",V::makeObj(),json(R"({"isError":false,"structuredContent":{"revision":7}})"));
    fails([&]{ark::assistantRequest(config,oversized,ark::AgentSession::tools());},"irreducibly oversized static contract fails explicitly instead of silently truncating the contract");
}
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
    staticContractProjectionChecks();
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
